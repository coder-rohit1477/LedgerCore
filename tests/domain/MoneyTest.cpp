#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <random>

#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/Money.h"

using ledgercore::domain::Currency;
using ledgercore::domain::CurrencyMismatchException;
using ledgercore::domain::InvalidCurrencyException;
using ledgercore::domain::InvalidMoneyException;
using ledgercore::domain::Money;
using ledgercore::domain::MoneyOverflowException;

namespace {
constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();
} // namespace

// ---------------------------------------------------------------------
// Currency
// ---------------------------------------------------------------------

TEST(CurrencyTest, AcceptsValidThreeLetterUppercaseCode) {
    Currency usd("USD");
    EXPECT_EQ(usd.code(), "USD");
}

TEST(CurrencyTest, RejectsWrongLength) {
    EXPECT_THROW(Currency("US"), InvalidCurrencyException);
    EXPECT_THROW(Currency("USDX"), InvalidCurrencyException);
    EXPECT_THROW(Currency(""), InvalidCurrencyException);
}

TEST(CurrencyTest, RejectsLowercaseOrNonLetterCharacters) {
    EXPECT_THROW(Currency("usd"), InvalidCurrencyException);
    EXPECT_THROW(Currency("Usd"), InvalidCurrencyException);
    EXPECT_THROW(Currency("US1"), InvalidCurrencyException);
}

TEST(CurrencyTest, EqualityAndInequality) {
    EXPECT_EQ(Currency("USD"), Currency("USD"));
    EXPECT_NE(Currency("USD"), Currency("EUR"));
}

// ---------------------------------------------------------------------
// Construction: ofMinorUnits, zero, sign predicates
// ---------------------------------------------------------------------

TEST(MoneyTest, OfMinorUnitsRoundTripsExactly) {
    Currency usd("USD");
    EXPECT_EQ(Money::ofMinorUnits(10010, usd).minorUnits(), 10010);
    EXPECT_EQ(Money::ofMinorUnits(-10010, usd).minorUnits(), -10010);
    EXPECT_EQ(Money::ofMinorUnits(0, usd).minorUnits(), 0);
}

TEST(MoneyTest, ZeroIsValidAndIsZero) {
    Currency usd("USD");
    Money zero = Money::zero(usd);
    EXPECT_TRUE(zero.isZero());
    EXPECT_FALSE(zero.isPositive());
    EXPECT_FALSE(zero.isNegative());
    EXPECT_EQ(zero, Money::ofMinorUnits(0, usd));
}

TEST(MoneyTest, PositiveAmountPredicates) {
    Currency usd("USD");
    Money m = Money::ofMinorUnits(1, usd);
    EXPECT_TRUE(m.isPositive());
    EXPECT_FALSE(m.isNegative());
    EXPECT_FALSE(m.isZero());
}

TEST(MoneyTest, NegativeAmountPredicates) {
    Currency usd("USD");
    Money m = Money::ofMinorUnits(-1, usd);
    EXPECT_TRUE(m.isNegative());
    EXPECT_FALSE(m.isPositive());
    EXPECT_FALSE(m.isZero());
}

// ---------------------------------------------------------------------
// fromMajorUnits
// ---------------------------------------------------------------------

TEST(MoneyTest, FromMajorUnitsPositive) {
    Currency usd("USD");
    EXPECT_EQ(Money::fromMajorUnits(100, 10, usd).minorUnits(), 10010);
}

TEST(MoneyTest, FromMajorUnitsZero) {
    Currency usd("USD");
    EXPECT_EQ(Money::fromMajorUnits(0, 0, usd).minorUnits(), 0);
}

TEST(MoneyTest, FromMajorUnitsNegativeViaNegativeMajor) {
    Currency usd("USD");
    EXPECT_EQ(Money::fromMajorUnits(-100, -10, usd).minorUnits(), -10010);
}

TEST(MoneyTest, FromMajorUnitsNegativeFractionalOnlyViaMinorPart) {
    Currency usd("USD");
    // The only way to express a negative amount smaller than one major
    // unit: majorUnits is 0, so minorUnitsPart alone carries the sign.
    EXPECT_EQ(Money::fromMajorUnits(0, -10, usd).minorUnits(), -10);
}

TEST(MoneyTest, FromMajorUnitsRejectsAmbiguousSignPositiveMajorNegativeMinor) {
    Currency usd("USD");
    EXPECT_THROW(Money::fromMajorUnits(100, -10, usd), InvalidMoneyException);
}

TEST(MoneyTest, FromMajorUnitsRejectsAmbiguousSignNegativeMajorPositiveMinor) {
    Currency usd("USD");
    EXPECT_THROW(Money::fromMajorUnits(-100, 10, usd), InvalidMoneyException);
}

TEST(MoneyTest, FromMajorUnitsRejectsOutOfRangeMinorPart) {
    Currency usd("USD");
    EXPECT_THROW(Money::fromMajorUnits(0, 100, usd), InvalidMoneyException);
    EXPECT_THROW(Money::fromMajorUnits(0, -100, usd), InvalidMoneyException);
}

TEST(MoneyTest, FromMajorUnitsOverflowThrows) {
    Currency usd("USD");
    const std::int64_t maxMajor = kInt64Max / Money::kMinorUnitsPerMajorUnit;
    EXPECT_THROW(Money::fromMajorUnits(maxMajor + 1, 0, usd), MoneyOverflowException);

    const std::int64_t minMajor = kInt64Min / Money::kMinorUnitsPerMajorUnit;
    EXPECT_THROW(Money::fromMajorUnits(minMajor - 1, 0, usd), MoneyOverflowException);
}

// ---------------------------------------------------------------------
// toString
// ---------------------------------------------------------------------

TEST(MoneyTest, ToStringFormatsPositiveAmount) {
    Currency usd("USD");
    EXPECT_EQ(Money::fromMajorUnits(100, 10, usd).toString(), "100.10 USD");
}

TEST(MoneyTest, ToStringFormatsNegativeAmount) {
    Currency usd("USD");
    EXPECT_EQ(Money::fromMajorUnits(-100, -10, usd).toString(), "-100.10 USD");
}

TEST(MoneyTest, ToStringFormatsZero) {
    Currency usd("USD");
    EXPECT_EQ(Money::zero(usd).toString(), "0.00 USD");
}

TEST(MoneyTest, ToStringPadsSingleDigitMinorUnits) {
    Currency usd("USD");
    EXPECT_EQ(Money::ofMinorUnits(5, usd).toString(), "0.05 USD");
    EXPECT_EQ(Money::ofMinorUnits(-5, usd).toString(), "-0.05 USD");
}

TEST(MoneyTest, ToStringHandlesNegativeSubMajorUnitAmount) {
    Currency usd("USD");
    EXPECT_EQ(Money::ofMinorUnits(-10, usd).toString(), "-0.10 USD");
}

TEST(MoneyTest, ToStringHandlesInt64MinWithoutUndefinedBehavior) {
    Currency usd("USD");
    // Must not negate INT64_MIN internally -- verified by simply not
    // crashing/UB-sanitizing and producing a plausible, deterministic string.
    const std::string text = Money::ofMinorUnits(kInt64Min, usd).toString();
    EXPECT_EQ(text, "-92233720368547758.08 USD");
}

// ---------------------------------------------------------------------
// Equality / inequality (including cross-currency)
// ---------------------------------------------------------------------

TEST(MoneyTest, EqualityWithinSameCurrency) {
    Currency usd("USD");
    EXPECT_EQ(Money::ofMinorUnits(100, usd), Money::ofMinorUnits(100, usd));
    EXPECT_NE(Money::ofMinorUnits(100, usd), Money::ofMinorUnits(101, usd));
}

TEST(MoneyTest, EqualityAcrossCurrenciesIsFalseAndDoesNotThrow) {
    Currency usd("USD");
    Currency eur("EUR");
    EXPECT_NO_THROW({
        bool equal = (Money::ofMinorUnits(100, usd) == Money::ofMinorUnits(100, eur));
        EXPECT_FALSE(equal);
    });
    EXPECT_NE(Money::ofMinorUnits(100, usd), Money::ofMinorUnits(100, eur));
}

// ---------------------------------------------------------------------
// Ordering (including cross-currency)
// ---------------------------------------------------------------------

TEST(MoneyTest, OrderingWithinSameCurrency) {
    Currency usd("USD");
    Money five = Money::ofMinorUnits(500, usd);
    Money ten = Money::ofMinorUnits(1000, usd);

    EXPECT_LT(five, ten);
    EXPECT_LE(five, ten);
    EXPECT_LE(five, five);
    EXPECT_GT(ten, five);
    EXPECT_GE(ten, five);
    EXPECT_GE(ten, ten);
}

TEST(MoneyTest, OrderingAcrossCurrenciesThrows) {
    Currency usd("USD");
    Currency eur("EUR");
    Money usdAmount = Money::ofMinorUnits(100, usd);
    Money eurAmount = Money::ofMinorUnits(100, eur);

    EXPECT_THROW((void)(usdAmount < eurAmount), CurrencyMismatchException);
    EXPECT_THROW((void)(usdAmount <= eurAmount), CurrencyMismatchException);
    EXPECT_THROW((void)(usdAmount > eurAmount), CurrencyMismatchException);
    EXPECT_THROW((void)(usdAmount >= eurAmount), CurrencyMismatchException);
}

// ---------------------------------------------------------------------
// Same-currency arithmetic
// ---------------------------------------------------------------------

TEST(MoneyTest, AdditionWithinSameCurrencyIsExact) {
    Currency usd("USD");
    Money a = Money::fromMajorUnits(100, 10, usd);
    Money b = Money::ofMinorUnits(1, usd);

    EXPECT_EQ((a + b).minorUnits(), 10011);
}

TEST(MoneyTest, SubtractionWithinSameCurrencyIsExact) {
    Currency usd("USD");
    Money a = Money::fromMajorUnits(100, 11, usd);
    Money b = Money::ofMinorUnits(1, usd);

    EXPECT_EQ((a - b).minorUnits(), 10010);
}

TEST(MoneyTest, SubtractionCanProduceNegativeResult) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(100, usd);
    Money b = Money::ofMinorUnits(500, usd);

    Money result = a - b;
    EXPECT_TRUE(result.isNegative());
    EXPECT_EQ(result.minorUnits(), -400);
}

// ---------------------------------------------------------------------
// Cross-currency arithmetic
// ---------------------------------------------------------------------

TEST(MoneyTest, AdditionAcrossCurrenciesThrows) {
    Currency usd("USD");
    Currency eur("EUR");
    EXPECT_THROW(Money::ofMinorUnits(100, usd) + Money::ofMinorUnits(100, eur), CurrencyMismatchException);
}

TEST(MoneyTest, SubtractionAcrossCurrenciesThrows) {
    Currency usd("USD");
    Currency eur("EUR");
    EXPECT_THROW(Money::ofMinorUnits(100, usd) - Money::ofMinorUnits(100, eur), CurrencyMismatchException);
}

// ---------------------------------------------------------------------
// Zero and negative-value arithmetic
// ---------------------------------------------------------------------

TEST(MoneyTest, AddingZeroIsIdentity) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(12345, usd);
    EXPECT_EQ(a + Money::zero(usd), a);
    EXPECT_EQ(Money::zero(usd) + a, a);
}

TEST(MoneyTest, SubtractingZeroIsIdentity) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(12345, usd);
    EXPECT_EQ(a - Money::zero(usd), a);
}

TEST(MoneyTest, ZeroMinusValueNegatesIt) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(12345, usd);
    EXPECT_EQ(Money::zero(usd) - a, -a);
}

TEST(MoneyTest, AddingNegativeActsLikeSubtraction) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(1000, usd);
    Money negativeB = Money::ofMinorUnits(-300, usd);
    EXPECT_EQ((a + negativeB).minorUnits(), 700);
}

// ---------------------------------------------------------------------
// Unary negation
// ---------------------------------------------------------------------

TEST(MoneyTest, UnaryNegationOfPositiveGivesNegative) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(500, usd);
    EXPECT_EQ((-a).minorUnits(), -500);
}

TEST(MoneyTest, UnaryNegationOfNegativeGivesPositive) {
    Currency usd("USD");
    Money a = Money::ofMinorUnits(-500, usd);
    EXPECT_EQ((-a).minorUnits(), 500);
}

TEST(MoneyTest, UnaryNegationOfZeroIsZero) {
    Currency usd("USD");
    EXPECT_EQ((-Money::zero(usd)).minorUnits(), 0);
}

TEST(MoneyTest, UnaryNegationOfInt64MinThrows) {
    Currency usd("USD");
    Money extreme = Money::ofMinorUnits(kInt64Min, usd);
    EXPECT_THROW((void)(-extreme), MoneyOverflowException);
}

// ---------------------------------------------------------------------
// Boundary values
// ---------------------------------------------------------------------

TEST(MoneyTest, Int64MaxIsConstructibleAndUsable) {
    Currency usd("USD");
    Money m = Money::ofMinorUnits(kInt64Max, usd);
    EXPECT_EQ(m.minorUnits(), kInt64Max);
    EXPECT_TRUE(m.isPositive());
}

TEST(MoneyTest, Int64MinIsConstructibleAndUsable) {
    Currency usd("USD");
    Money m = Money::ofMinorUnits(kInt64Min, usd);
    EXPECT_EQ(m.minorUnits(), kInt64Min);
    EXPECT_TRUE(m.isNegative());
}

// ---------------------------------------------------------------------
// Overflow detection
// ---------------------------------------------------------------------

TEST(MoneyTest, AdditionOverflowThrows) {
    Currency usd("USD");
    Money maxValue = Money::ofMinorUnits(kInt64Max, usd);
    Money one = Money::ofMinorUnits(1, usd);
    EXPECT_THROW(maxValue + one, MoneyOverflowException);
}

TEST(MoneyTest, SubtractionOverflowOnLowerBoundThrows) {
    Currency usd("USD");
    Money minValue = Money::ofMinorUnits(kInt64Min, usd);
    Money one = Money::ofMinorUnits(1, usd);
    EXPECT_THROW(minValue - one, MoneyOverflowException);
}

TEST(MoneyTest, SubtractionOverflowOnUpperBoundThrows) {
    Currency usd("USD");
    Money maxValue = Money::ofMinorUnits(kInt64Max, usd);
    Money negativeOne = Money::ofMinorUnits(-1, usd);
    // maxValue - (-1) is equivalent to maxValue + 1: overflows upward.
    EXPECT_THROW(maxValue - negativeOne, MoneyOverflowException);
}

// ---------------------------------------------------------------------
// No floating-point behavior
// ---------------------------------------------------------------------

TEST(MoneyTest, ClassicFloatingPointDriftCaseIsExact) {
    Currency usd("USD");
    // 0.1 + 0.2 != 0.3 in binary floating point. In minor-unit integers,
    // this is just 10 + 20 == 30 -- exact, no drift possible.
    Money a = Money::fromMajorUnits(0, 10, usd);
    Money b = Money::fromMajorUnits(0, 20, usd);
    EXPECT_EQ(a + b, Money::fromMajorUnits(0, 30, usd));
}

TEST(MoneyTest, RepeatedAdditionOfOneCentIsExactlyOneDollar) {
    Currency usd("USD");
    // Summing 0.01 one hundred times is a classic case where repeated
    // double addition does not always land exactly on 1.0.
    Money total = Money::zero(usd);
    Money oneCent = Money::ofMinorUnits(1, usd);
    for (int i = 0; i < 100; ++i) {
        total = total + oneCent;
    }
    EXPECT_EQ(total, Money::fromMajorUnits(1, 0, usd));
}

// ---------------------------------------------------------------------
// Property-style tests (lightweight generators over GoogleTest, per the
// Phase 0 decision to defer a dedicated property-based testing library).
// ---------------------------------------------------------------------

TEST(MoneyPropertyTest, AdditionIsCommutativeWhenNoOverflowOccurs) {
    Currency usd("USD");
    std::mt19937_64 rng(42);
    // Bounded well within int64 range so that no pairwise sum can overflow.
    std::uniform_int_distribution<std::int64_t> dist(-1'000'000'000'000LL, 1'000'000'000'000LL);

    for (int i = 0; i < 200; ++i) {
        Money a = Money::ofMinorUnits(dist(rng), usd);
        Money b = Money::ofMinorUnits(dist(rng), usd);
        EXPECT_EQ(a + b, b + a);
    }
}

TEST(MoneyPropertyTest, SubtractionUndoesAdditionWhenNoOverflowOccurs) {
    Currency usd("USD");
    std::mt19937_64 rng(43);
    std::uniform_int_distribution<std::int64_t> dist(-1'000'000'000'000LL, 1'000'000'000'000LL);

    for (int i = 0; i < 200; ++i) {
        Money a = Money::ofMinorUnits(dist(rng), usd);
        Money b = Money::ofMinorUnits(dist(rng), usd);
        EXPECT_EQ((a + b) - b, a);
    }
}

TEST(MoneyPropertyTest, AdditiveInverseYieldsZeroWhenNegationIsRepresentable) {
    Currency usd("USD");
    std::mt19937_64 rng(44);
    std::uniform_int_distribution<std::int64_t> dist(-1'000'000'000'000LL, 1'000'000'000'000LL);

    for (int i = 0; i < 200; ++i) {
        Money a = Money::ofMinorUnits(dist(rng), usd);
        EXPECT_EQ(a + (-a), Money::zero(usd));
    }
}

TEST(MoneyPropertyTest, OfMinorUnitsRoundTripsForArbitraryValues) {
    Currency usd("USD");
    std::mt19937_64 rng(45);
    std::uniform_int_distribution<std::int64_t> dist(kInt64Min, kInt64Max);

    for (int i = 0; i < 200; ++i) {
        std::int64_t v = dist(rng);
        EXPECT_EQ(Money::ofMinorUnits(v, usd).minorUnits(), v);
    }
}
