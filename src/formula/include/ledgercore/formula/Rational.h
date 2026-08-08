#pragma once

#include <cstdint>
#include <string>

namespace ledgercore::formula {

// An exact rational number: a numerator and a strictly positive
// denominator, always kept in lowest terms (gcd(|numerator|, denominator)
// == 1, or 0/1 for zero). Backed entirely by std::int64_t -- never
// double/float, for the same reason Money is: this is the dimensionless
// scalar type formula literals and pure-scalar arithmetic use, and it
// must stay exact through arbitrarily many operations (e.g. `2/3 * 3`
// must be exactly 2, never an approximation).
//
// Every arithmetic operation checks for std::int64_t overflow *before*
// performing it and throws FormulaEvaluationException rather than
// invoking undefined signed-integer-overflow behavior -- mirroring
// Money's own overflow-checking discipline exactly. Rational never falls
// back to floating point, anywhere.
class Rational {
public:
    static Rational ofInt(std::int64_t value) noexcept;

    // integerPart and fractionalPart are the digit groups of a decimal
    // literal like "100.25" (integerPart=100, fractionalPart=25,
    // fractionalDigitCount=2), both non-negative -- formula literals
    // never carry a sign themselves (unary minus is a separate AST node,
    // not part of literal syntax). Throws FormulaEvaluationException if
    // the exact value would overflow std::int64_t.
    static Rational fromDecimalParts(std::int64_t integerPart, std::int64_t fractionalPart, int fractionalDigitCount);

    std::int64_t numerator() const noexcept { return numerator_; }
    std::int64_t denominator() const noexcept { return denominator_; }

    bool isZero() const noexcept { return numerator_ == 0; }
    bool isPositive() const noexcept { return numerator_ > 0; }
    bool isNegative() const noexcept { return numerator_ < 0; }

    // Deterministic, locale-independent: "numerator/denominator".
    std::string toString() const;

    friend Rational operator+(const Rational& lhs, const Rational& rhs);
    friend Rational operator-(const Rational& lhs, const Rational& rhs);
    friend Rational operator*(const Rational& lhs, const Rational& rhs);
    friend Rational operator/(const Rational& lhs, const Rational& rhs);
    friend Rational operator-(const Rational& value);

private:
    Rational(std::int64_t numerator, std::int64_t denominator);

    std::int64_t numerator_;
    std::int64_t denominator_;
};

// Throws FormulaEvaluationException on overflow.
Rational operator+(const Rational& lhs, const Rational& rhs);
Rational operator-(const Rational& lhs, const Rational& rhs);
Rational operator*(const Rational& lhs, const Rational& rhs);

// Throws FormulaEvaluationException on overflow or division by zero.
Rational operator/(const Rational& lhs, const Rational& rhs);

// Throws FormulaEvaluationException when negating the minimum
// representable numerator, which has no representable positive
// counterpart.
Rational operator-(const Rational& value);

bool operator==(const Rational& lhs, const Rational& rhs) noexcept;
bool operator!=(const Rational& lhs, const Rational& rhs) noexcept;

} // namespace ledgercore::formula
