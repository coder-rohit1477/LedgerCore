#include "ledgercore/formula/Evaluator.h"

#include <cstdint>
#include <limits>
#include <type_traits>
#include <variant>

namespace ledgercore::formula {

namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

std::uint64_t magnitudeOf(std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

// See Rational.cpp for the identical helper and the reasoning behind its
// conservative std::int64_t::min() boundary handling; duplicated here
// (not shared) because it is a mechanical integer-overflow primitive,
// not a business rule -- the same category of narrow local helper Money
// and Rational each already keep to themselves.
bool wouldMultiplyOverflow(std::int64_t a, std::int64_t b) noexcept {
    if (a == 0 || b == 0) {
        return false;
    }
    if (a == kInt64Min || b == kInt64Min) {
        const std::int64_t other = (a == kInt64Min) ? b : a;
        return other != 1;
    }
    const std::int64_t absA = a < 0 ? -a : a;
    const std::int64_t absB = b < 0 ? -b : b;
    return absA > kInt64Max / absB;
}

// Rounds the exact value (p / q) to the nearest std::int64_t using
// round-half-away-from-zero. q must be nonzero; callers check this
// before calling (division by zero is reported with a more specific
// message at the call site).
std::int64_t roundHalfAwayFromZero(std::int64_t p, std::int64_t q) {
    if (q < 0) {
        // Negating q (and p, to preserve the sign of p/q) is only unsafe
        // if either is std::int64_t::min(); guard before negating rather
        // than after.
        if (p == kInt64Min || q == kInt64Min) {
            throw FormulaEvaluationException("Money scaling overflows the representable range");
        }
        p = -p;
        q = -q;
    }

    const std::int64_t quotient = p / q;
    const std::int64_t remainder = p % q;

    if (remainder == 0) {
        return quotient;
    }

    // |remainder| < q <= kInt64Max, so remainderMagnitude * 2 is always
    // well within std::uint64_t's range -- no overflow here.
    const std::uint64_t remainderMagnitude = magnitudeOf(remainder);
    const bool roundsAwayFromZero = (remainderMagnitude * 2) >= static_cast<std::uint64_t>(q);

    if (!roundsAwayFromZero) {
        return quotient;
    }

    if (p > 0) {
        if (quotient == kInt64Max) {
            throw FormulaEvaluationException("Money scaling overflows the representable range");
        }
        return quotient + 1;
    }
    if (quotient == kInt64Min) {
        throw FormulaEvaluationException("Money scaling overflows the representable range");
    }
    return quotient - 1;
}

// Computes money * (multiplierNumerator / multiplierDenominator),
// rounding to the nearest whole minor unit (round-half-away-from-zero)
// only when that product is not already exact. multiplierDenominator
// must be nonzero.
domain::Money scaleMoney(const domain::Money& money, std::int64_t multiplierNumerator,
                          std::int64_t multiplierDenominator) {
    if (wouldMultiplyOverflow(money.minorUnits(), multiplierNumerator)) {
        throw FormulaEvaluationException("Money scaling overflows the representable range");
    }
    const std::int64_t scaledMinorUnits = money.minorUnits() * multiplierNumerator;
    const std::int64_t roundedMinorUnits = roundHalfAwayFromZero(scaledMinorUnits, multiplierDenominator);
    return domain::Money::ofMinorUnits(roundedMinorUnits, money.currency());
}

FormulaValue evaluateNode(const AstNode& node, const AccountResolver& resolver);

FormulaValue evaluateLiteral(const Literal& literal) {
    return FormulaValue(literal.value);
}

FormulaValue evaluateAccountReference(const AccountReference& reference, const AccountResolver& resolver) {
    return FormulaValue(resolver.resolve(reference.code));
}

FormulaValue evaluateUnary(const UnaryExpression& expr, const AccountResolver& resolver) {
    FormulaValue operand = evaluateNode(*expr.operand, resolver);
    if (expr.op == UnaryOperator::Plus) {
        return operand;
    }
    if (operand.isMoney()) {
        return FormulaValue(-operand.asMoney());
    }
    return FormulaValue(-operand.asScalar());
}

FormulaValue evaluateBinary(const BinaryExpression& expr, const AccountResolver& resolver) {
    const FormulaValue left = evaluateNode(*expr.left, resolver);
    const FormulaValue right = evaluateNode(*expr.right, resolver);
    const bool leftIsMoney = left.isMoney();
    const bool rightIsMoney = right.isMoney();

    switch (expr.op) {
        case BinaryOperator::Add:
            if (leftIsMoney && rightIsMoney) {
                return FormulaValue(left.asMoney() + right.asMoney());
            }
            if (!leftIsMoney && !rightIsMoney) {
                return FormulaValue(left.asScalar() + right.asScalar());
            }
            throw FormulaEvaluationException("Cannot add a Money value and a dimensionless scalar");

        case BinaryOperator::Subtract:
            if (leftIsMoney && rightIsMoney) {
                return FormulaValue(left.asMoney() - right.asMoney());
            }
            if (!leftIsMoney && !rightIsMoney) {
                return FormulaValue(left.asScalar() - right.asScalar());
            }
            throw FormulaEvaluationException("Cannot subtract a Money value and a dimensionless scalar");

        case BinaryOperator::Multiply:
            if (leftIsMoney && rightIsMoney) {
                throw FormulaEvaluationException("Cannot multiply two Money values");
            }
            if (!leftIsMoney && !rightIsMoney) {
                return FormulaValue(left.asScalar() * right.asScalar());
            }
            if (leftIsMoney) {
                const Rational& scalar = right.asScalar();
                return FormulaValue(scaleMoney(left.asMoney(), scalar.numerator(), scalar.denominator()));
            }
            {
                const Rational& scalar = left.asScalar();
                return FormulaValue(scaleMoney(right.asMoney(), scalar.numerator(), scalar.denominator()));
            }

        case BinaryOperator::Divide:
            if (leftIsMoney && rightIsMoney) {
                throw FormulaEvaluationException("Cannot divide a Money value by a Money value");
            }
            if (!leftIsMoney && !rightIsMoney) {
                return FormulaValue(left.asScalar() / right.asScalar());
            }
            if (leftIsMoney) {
                const Rational& scalar = right.asScalar();
                if (scalar.isZero()) {
                    throw FormulaEvaluationException("Division by zero");
                }
                return FormulaValue(scaleMoney(left.asMoney(), scalar.denominator(), scalar.numerator()));
            }
            throw FormulaEvaluationException("Cannot divide a dimensionless scalar by a Money value");
    }

    throw FormulaEvaluationException("Unknown binary operator");
}

FormulaValue evaluateNode(const AstNode& node, const AccountResolver& resolver) {
    return std::visit(
        [&resolver](const auto& alternative) -> FormulaValue {
            using T = std::decay_t<decltype(alternative)>;
            if constexpr (std::is_same_v<T, Literal>) {
                return evaluateLiteral(alternative);
            } else if constexpr (std::is_same_v<T, AccountReference>) {
                return evaluateAccountReference(alternative, resolver);
            } else if constexpr (std::is_same_v<T, UnaryExpression>) {
                return evaluateUnary(alternative, resolver);
            } else {
                return evaluateBinary(alternative, resolver);
            }
        },
        node.value());
}

} // namespace

FormulaValue evaluate(const AstNode& root, const AccountResolver& resolver) {
    return evaluateNode(root, resolver);
}

} // namespace ledgercore::formula
