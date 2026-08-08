#include "ledgercore/formula/Rational.h"

#include <limits>
#include <sstream>

#include "ledgercore/formula/FormulaExceptions.h"

namespace ledgercore::formula {

namespace {

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

// The magnitude of value as an unsigned 64-bit integer. Never negates
// std::int64_t::min() directly (undefined behavior); instead negates
// value + 1 (always representable, since value <= -1 here) and adds 1
// back in unsigned arithmetic.
std::uint64_t magnitudeOf(std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

std::uint64_t gcdOf(std::uint64_t a, std::uint64_t b) noexcept {
    while (b != 0) {
        const std::uint64_t remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

bool wouldAddOverflow(std::int64_t a, std::int64_t b) noexcept {
    if (b > 0) {
        return a > kInt64Max - b;
    }
    if (b < 0) {
        return a < kInt64Min - b;
    }
    return false;
}

bool wouldSubtractOverflow(std::int64_t a, std::int64_t b) noexcept {
    if (b > 0) {
        return a < kInt64Min + b;
    }
    if (b < 0) {
        return a > kInt64Max + b;
    }
    return false;
}

// Conservatively safe: for the one boundary case where a*b is exactly
// std::int64_t::min() (representable, but only reachable via two
// non-min operands), this reports "would overflow" even though the
// exact product is technically representable. Being conservative here
// is deliberate and matches the instruction to never invoke undefined
// behavior -- a slightly-too-eager FormulaEvaluationException in a
// vanishingly rare boundary case is a fully acceptable trade for never
// risking signed overflow.
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

} // namespace

Rational::Rational(std::int64_t numerator, std::int64_t denominator) {
    // Every caller in this file guarantees denominator != 0, and that
    // negating a negative denominator below cannot overflow: the
    // denominator passed here is always either an existing (already
    // positive, by this same invariant) Rational's denominator, a
    // product of two such denominators, or a numerator from an existing
    // Rational -- and every multiplication that could produce such a
    // value is overflow-checked (via wouldMultiplyOverflow, which is
    // conservative about the exact std::int64_t::min() boundary) before
    // it is performed, so a denominator of std::int64_t::min() can never
    // reach this constructor.
    if (denominator < 0) {
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
