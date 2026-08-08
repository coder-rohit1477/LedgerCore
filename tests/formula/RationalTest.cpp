#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/formula/Rational.h"

using ledgercore::formula::FormulaEvaluationException;
using ledgercore::formula::Rational;

// ---------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------

TEST(RationalTest, OfIntProducesDenominatorOne) {
    Rational r = Rational::ofInt(5);
    EXPECT_EQ(r.numerator(), 5);
    EXPECT_EQ(r.denominator(), 1);
}

TEST(RationalTest, OfIntNegative) {
    Rational r = Rational::ofInt(-5);
    EXPECT_EQ(r.numerator(), -5);
    EXPECT_TRUE(r.isNegative());
}

TEST(RationalTest, FromDecimalPartsProducesExactReducedFraction) {
    // 100.25 == 401/4
    Rational r = Rational::fromDecimalParts(100, 25, 2);
    EXPECT_EQ(r.numerator(), 401);
    EXPECT_EQ(r.denominator(), 4);
}

TEST(RationalTest, FromDecimalPartsWithNoFractionalDigitsIsAnInteger) {
    Rational r = Rational::fromDecimalParts(7, 0, 0);
    EXPECT_EQ(r.numerator(), 7);
    EXPECT_EQ(r.denominator(), 1);
}

TEST(RationalTest, ZeroIsCanonicalZeroOverOne) {
    Rational r = Rational::ofInt(0);
    EXPECT_EQ(r.numerator(), 0);
    EXPECT_EQ(r.denominator(), 1);
    EXPECT_TRUE(r.isZero());
}

// ---------------------------------------------------------------------
// Reduction to lowest terms
// ---------------------------------------------------------------------

TEST(RationalTest, DivisionReducesToLowestTerms) {
    // 2/4 == 1/2
    Rational r = Rational::ofInt(2) / Rational::ofInt(4);
    EXPECT_EQ(r.numerator(), 1);
    EXPECT_EQ(r.denominator(), 2);
}

TEST(RationalTest, MultiplicationReducesToLowestTerms) {
    // (1/2) * (2/3) == 1/3, not 2/6
    Rational half = Rational::ofInt(1) / Rational::ofInt(2);
    Rational twoThirds = Rational::ofInt(2) / Rational::ofInt(3);
    Rational result = half * twoThirds;
    EXPECT_EQ(result.numerator(), 1);
    EXPECT_EQ(result.denominator(), 3);
}

// ---------------------------------------------------------------------
// Arithmetic correctness
// ---------------------------------------------------------------------

TEST(RationalTest, AdditionOfFractions) {
    // 1/2 + 1/3 == 5/6
    Rational half = Rational::ofInt(1) / Rational::ofInt(2);
    Rational third = Rational::ofInt(1) / Rational::ofInt(3);
    Rational sum = half + third;
    EXPECT_EQ(sum.numerator(), 5);
    EXPECT_EQ(sum.denominator(), 6);
}

TEST(RationalTest, SubtractionOfFractions) {
    // 1/2 - 1/3 == 1/6
    Rational half = Rational::ofInt(1) / Rational::ofInt(2);
    Rational third = Rational::ofInt(1) / Rational::ofInt(3);
    Rational diff = half - third;
    EXPECT_EQ(diff.numerator(), 1);
    EXPECT_EQ(diff.denominator(), 6);
}

TEST(RationalTest, DivisionByFraction) {
    // 1 / (1/2) == 2
    Rational half = Rational::ofInt(1) / Rational::ofInt(2);
    Rational result = Rational::ofInt(1) / half;
    EXPECT_EQ(result, Rational::ofInt(2));
}

TEST(RationalTest, DivisionByZeroThrows) {
    EXPECT_THROW(Rational::ofInt(1) / Rational::ofInt(0), FormulaEvaluationException);
}

TEST(RationalTest, ExactRoundTripThroughDivideThenMultiply) {
    // (1/3) * 3 must be exactly 1 -- the whole point of exact rational
    // arithmetic instead of fixed-decimal or floating point.
    Rational third = Rational::ofInt(1) / Rational::ofInt(3);
    Rational result = third * Rational::ofInt(3);
    EXPECT_EQ(result, Rational::ofInt(1));
}

// ---------------------------------------------------------------------
// Negation
// ---------------------------------------------------------------------

TEST(RationalTest, UnaryNegationOfPositive) {
    Rational r = -Rational::ofInt(5);
    EXPECT_EQ(r.numerator(), -5);
}

TEST(RationalTest, UnaryNegationOfNegative) {
    Rational r = -Rational::ofInt(-5);
    EXPECT_EQ(r.numerator(), 5);
}

TEST(RationalTest, UnaryNegationOfZeroIsZero) {
    Rational r = -Rational::ofInt(0);
    EXPECT_TRUE(r.isZero());
}

TEST(RationalTest, UnaryNegationOfInt64MinThrows) {
    Rational r = Rational::ofInt(std::numeric_limits<std::int64_t>::min());
    EXPECT_THROW(-r, FormulaEvaluationException);
}

// ---------------------------------------------------------------------
// Overflow
// ---------------------------------------------------------------------

TEST(RationalTest, MultiplicationOverflowThrows) {
    constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
    Rational huge = Rational::ofInt(kInt64Max);
    EXPECT_THROW(huge * Rational::ofInt(2), FormulaEvaluationException);
}

TEST(RationalTest, AdditionOverflowThrows) {
    constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
    Rational huge = Rational::ofInt(kInt64Max);
    EXPECT_THROW(huge + Rational::ofInt(1), FormulaEvaluationException);
}

TEST(RationalTest, FromDecimalPartsOverflowThrows) {
    constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
    EXPECT_THROW(Rational::fromDecimalParts(kInt64Max, 5, 1), FormulaEvaluationException);
}

TEST(RationalTest, CrossMultiplicationOverflowInAdditionThrows) {
    // Denominators large enough that lhs.numerator * rhs.denominator
    // (the cross-multiplication step of addition) overflows even though
    // neither operand alone is anywhere near the int64_t range.
    constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
    Rational a = Rational::ofInt(kInt64Max) / Rational::ofInt(3);
    Rational b = Rational::ofInt(1) / Rational::ofInt(3);
    EXPECT_THROW(a + b, FormulaEvaluationException);
}

// ---------------------------------------------------------------------
// Sign predicates and equality
// ---------------------------------------------------------------------

TEST(RationalTest, SignPredicates) {
    EXPECT_TRUE(Rational::ofInt(5).isPositive());
    EXPECT_FALSE(Rational::ofInt(5).isNegative());
    EXPECT_TRUE(Rational::ofInt(-5).isNegative());
    EXPECT_FALSE(Rational::ofInt(-5).isPositive());
    EXPECT_TRUE(Rational::ofInt(0).isZero());
}

TEST(RationalTest, EqualityComparesReducedForm) {
    EXPECT_EQ(Rational::ofInt(2) / Rational::ofInt(4), Rational::ofInt(1) / Rational::ofInt(2));
    EXPECT_NE(Rational::ofInt(1), Rational::ofInt(2));
}

TEST(RationalTest, ToStringIsDeterministic) {
    Rational r = Rational::ofInt(1) / Rational::ofInt(2);
    EXPECT_EQ(r.toString(), "1/2");
}
