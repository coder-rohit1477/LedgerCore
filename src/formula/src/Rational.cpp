#include "ledgercore/formula/Rational.h"

#include <sstream>

#include "ledgercore/domain/CheckedArithmetic.h"
#include "ledgercore/formula/FormulaExceptions.h"

namespace ledgercore::formula {

namespace {

using domain::checked::magnitudeOf;
using domain::checked::wouldAddOverflow;
using domain::checked::wouldMultiplyOverflow;
using domain::checked::wouldSubtractOverflow;

using domain::checked::kInt64Max;
using domain::checked::kInt64Min;

std::uint64_t gcdOf(std::uint64_t a, std::uint64_t b) noexcept {
    while (b != 0) {
        const std::uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

} // namespace

Rational::Rational(std::int64_t numerator, std::int64_t denominator) {
    // Every caller in this file guarantees denominator != 0. A negative
    // denominator (only operator/ can produce one, from a negative
    // divisor numerator) is normalized by negating both parts -- which is
    // not representable when either part is std::int64_t::min(). That
    // case is reachable: wouldMultiplyOverflow() deliberately allows
    // min() * 1, so e.g. 1 / min() reaches here as (1, min()), and
    // min() / -1 as (min(), -1). Reject it before negating, exactly as
    // operator-(const Rational&) already does for a min() numerator.
    if (denominator < 0) {
        if (numerator == kInt64Min || denominator == kInt64Min) {
            throw FormulaEvaluationException("Rational sign normalization overflows the representable range");
        }
        numerator = -numerator;
        denominator = -denominator;
    }
    if (numerator == 0) {
        numerator_ = 0;
        denominator_ = 1;
        return;
    }
    const std::uint64_t g = gcdOf(magnitudeOf(numerator), magnitudeOf(denominator));
    numerator_ = numerator / static_cast<std::int64_t>(g);
    denominator_ = denominator / static_cast<std::int64_t>(g);
}

Rational Rational::ofInt(std::int64_t value) noexcept {
    return Rational(value, 1);
}

Rational Rational::fromDecimalParts(std::int64_t integerPart, std::int64_t fractionalPart, int fractionalDigitCount) {
    std::int64_t scale = 1;
    for (int i = 0; i < fractionalDigitCount; ++i) {
        if (wouldMultiplyOverflow(scale, 10)) {
            throw FormulaEvaluationException("Numeric literal has too many fractional digits to represent exactly");
        }
        scale *= 10;
    }

    if (wouldMultiplyOverflow(integerPart, scale)) {
        throw FormulaEvaluationException("Numeric literal is out of representable range");
    }
    const std::int64_t scaledInteger = integerPart * scale;

    if (wouldAddOverflow(scaledInteger, fractionalPart)) {
        throw FormulaEvaluationException("Numeric literal is out of representable range");
    }

    return Rational(scaledInteger + fractionalPart, scale);
}

std::string Rational::toString() const {
    std::ostringstream out;
    out << numerator_ << '/' << denominator_;
    return out.str();
}

Rational operator+(const Rational& lhs, const Rational& rhs) {
    if (wouldMultiplyOverflow(lhs.numerator_, rhs.denominator_)
        || wouldMultiplyOverflow(rhs.numerator_, lhs.denominator_)) {
        throw FormulaEvaluationException("Rational addition overflows the representable range");
    }
    const std::int64_t leftScaled = lhs.numerator_ * rhs.denominator_;
    const std::int64_t rightScaled = rhs.numerator_ * lhs.denominator_;

    if (wouldAddOverflow(leftScaled, rightScaled) || wouldMultiplyOverflow(lhs.denominator_, rhs.denominator_)) {
        throw FormulaEvaluationException("Rational addition overflows the representable range");
    }

    return Rational(leftScaled + rightScaled, lhs.denominator_ * rhs.denominator_);
}

Rational operator-(const Rational& lhs, const Rational& rhs) {
    if (wouldMultiplyOverflow(lhs.numerator_, rhs.denominator_)
        || wouldMultiplyOverflow(rhs.numerator_, lhs.denominator_)) {
        throw FormulaEvaluationException("Rational subtraction overflows the representable range");
    }
    const std::int64_t leftScaled = lhs.numerator_ * rhs.denominator_;
    const std::int64_t rightScaled = rhs.numerator_ * lhs.denominator_;

    if (wouldSubtractOverflow(leftScaled, rightScaled) || wouldMultiplyOverflow(lhs.denominator_, rhs.denominator_)) {
        throw FormulaEvaluationException("Rational subtraction overflows the representable range");
    }

    return Rational(leftScaled - rightScaled, lhs.denominator_ * rhs.denominator_);
}

Rational operator*(const Rational& lhs, const Rational& rhs) {
    if (wouldMultiplyOverflow(lhs.numerator_, rhs.numerator_) || wouldMultiplyOverflow(lhs.denominator_, rhs.denominator_)) {
        throw FormulaEvaluationException("Rational multiplication overflows the representable range");
    }
    return Rational(lhs.numerator_ * rhs.numerator_, lhs.denominator_ * rhs.denominator_);
}

Rational operator/(const Rational& lhs, const Rational& rhs) {
    if (rhs.isZero()) {
        throw FormulaEvaluationException("Division by zero");
    }
    if (wouldMultiplyOverflow(lhs.numerator_, rhs.denominator_) || wouldMultiplyOverflow(lhs.denominator_, rhs.numerator_)) {
        throw FormulaEvaluationException("Rational division overflows the representable range");
    }
    return Rational(lhs.numerator_ * rhs.denominator_, lhs.denominator_ * rhs.numerator_);
}

Rational operator-(const Rational& value) {
    if (value.numerator_ == kInt64Min) {
        throw FormulaEvaluationException("Cannot negate the minimum representable Rational numerator");
    }
    return Rational(-value.numerator_, value.denominator_);
}

bool operator==(const Rational& lhs, const Rational& rhs) noexcept {
    return lhs.numerator() == rhs.numerator() && lhs.denominator() == rhs.denominator();
}

bool operator!=(const Rational& lhs, const Rational& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace ledgercore::formula
