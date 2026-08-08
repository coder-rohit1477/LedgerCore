#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/AccountResolver.h"
#include "ledgercore/formula/Ast.h"
#include "ledgercore/formula/Evaluator.h"
#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/formula/Parser.h"
#include "ledgercore/formula/Rational.h"

using ledgercore::domain::AccountCode;
using ledgercore::domain::Currency;
using ledgercore::domain::CurrencyMismatchException;
using ledgercore::domain::Money;
using ledgercore::domain::MoneyOverflowException;
using ledgercore::formula::AccountResolver;
using ledgercore::formula::AstNodePtr;
using ledgercore::formula::evaluate;
using ledgercore::formula::FormulaEvaluationException;
using ledgercore::formula::FormulaValue;
using ledgercore::formula::parse;
using ledgercore::formula::Rational;
using ledgercore::formula::UnknownAccountReferenceException;

namespace {

// A simple in-memory resolver: exactly the kind of "test/mock" adapter
// the AccountResolver abstraction exists to make possible without any
// Ledger/ChartOfAccounts dependency.
class MapAccountResolver : public AccountResolver {
public:
    void set(const std::string& code, Money value) {
        values_.insert_or_assign(code, std::move(value));
    }

    Money resolve(const AccountCode& code) const override {
        auto it = values_.find(code.value());
        if (it == values_.end()) {
            throw UnknownAccountReferenceException("Unknown account reference: " + code.value());
        }
        return it->second;
    }

private:
    std::unordered_map<std::string, Money> values_;
};

FormulaValue evaluateFormula(const std::string& source, const AccountResolver& resolver) {
    return evaluate(*parse(source), resolver);
}

} // namespace

// ---------------------------------------------------------------------
// Literals and account references
// ---------------------------------------------------------------------

TEST(EvaluatorTest, LiteralEvaluatesToScalar) {
    MapAccountResolver resolver;
    FormulaValue result = evaluateFormula("42", resolver);
    ASSERT_TRUE(result.isScalar());
    EXPECT_EQ(result.asScalar(), Rational::ofInt(42));
}

TEST(EvaluatorTest, AccountReferenceResolvesToMoney) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));

    FormulaValue result = evaluateFormula("#1000", resolver);
    ASSERT_TRUE(result.isMoney());
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(100, 0, usd));
}

TEST(EvaluatorTest, ExactAccountCodeResolutionDoesNotCrossContaminate) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(10, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(20, 0, usd));

    EXPECT_EQ(evaluateFormula("#1000", resolver).asMoney(), Money::fromMajorUnits(10, 0, usd));
    EXPECT_EQ(evaluateFormula("#2000", resolver).asMoney(), Money::fromMajorUnits(20, 0, usd));
}

TEST(EvaluatorTest, UnknownAccountThrows) {
    MapAccountResolver resolver;
    EXPECT_THROW(evaluateFormula("#9999", resolver), UnknownAccountReferenceException);
}

// ---------------------------------------------------------------------
// Money + Money / Money - Money (reusing domain::Money's own semantics)
// ---------------------------------------------------------------------

TEST(EvaluatorTest, MoneyPlusMoneySameCurrency) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(50, 0, usd));

    FormulaValue result = evaluateFormula("#1000 + #2000", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(150, 0, usd));
}

TEST(EvaluatorTest, MoneyMinusMoneySameCurrency) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(30, 0, usd));

    FormulaValue result = evaluateFormula("#1000 - #2000", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(70, 0, usd));
}

TEST(EvaluatorTest, MoneyPlusMoneyDifferentCurrencyReusesDomainException) {
    Currency usd("USD");
    Currency eur("EUR");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(50, 0, eur));

    EXPECT_THROW(evaluateFormula("#1000 + #2000", resolver), CurrencyMismatchException);
}

TEST(EvaluatorTest, MoneyMinusMoneyDifferentCurrencyReusesDomainException) {
    Currency usd("USD");
    Currency eur("EUR");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(50, 0, eur));

    EXPECT_THROW(evaluateFormula("#1000 - #2000", resolver), CurrencyMismatchException);
}

TEST(EvaluatorTest, MoneyPlusMoneyOverflowReusesDomainException) {
    Currency usd("USD");
    MapAccountResolver resolver;
    const Money huge = Money::ofMinorUnits(std::numeric_limits<std::int64_t>::max(), usd);
    resolver.set("1000", huge);
    resolver.set("2000", Money::ofMinorUnits(1, usd));

    EXPECT_THROW(evaluateFormula("#1000 + #2000", resolver), MoneyOverflowException);
}

// ---------------------------------------------------------------------
// Money * Scalar / Money / Scalar
// ---------------------------------------------------------------------

TEST(EvaluatorTest, MoneyTimesScalar) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(50, 0, usd));

    FormulaValue result = evaluateFormula("#1000 * 2", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(100, 0, usd));
}

TEST(EvaluatorTest, ScalarTimesMoneyIsCommutative) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(50, 0, usd));

    FormulaValue result = evaluateFormula("2 * #1000", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(100, 0, usd));
}

TEST(EvaluatorTest, MoneyDividedByScalarExact) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));

    FormulaValue result = evaluateFormula("#1000 / 2", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(50, 0, usd));
}

TEST(EvaluatorTest, CurrencyIsPreservedThroughScaling) {
    Currency eur("EUR");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(10, 0, eur));

    FormulaValue result = evaluateFormula("#1000 * 3", resolver);
    EXPECT_EQ(result.asMoney().currency(), eur);
}

TEST(EvaluatorTest, MoneyScalingOverflowThrows) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::ofMinorUnits(std::numeric_limits<std::int64_t>::max(), usd));

    EXPECT_THROW(evaluateFormula("#1000 * 2", resolver), FormulaEvaluationException);
}

// ---------------------------------------------------------------------
// Rejected operations
// ---------------------------------------------------------------------

TEST(EvaluatorTest, MoneyTimesMoneyIsRejected) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("#1000 * #2000", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, MoneyDividedByMoneyIsRejected) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("#1000 / #2000", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, ScalarDividedByMoneyIsRejected) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("2 / #1000", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, MoneyPlusScalarIsRejected) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("#1000 + 2", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, MoneyMinusScalarIsRejected) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("#1000 - 2", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, ScalarMinusMoneyIsRejected) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("2 - #1000", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, DivisionByZeroScalarThrows) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));

    EXPECT_THROW(evaluateFormula("#1000 / 0", resolver), FormulaEvaluationException);
}

TEST(EvaluatorTest, ScalarDivisionByZeroThrows) {
    MapAccountResolver resolver;
    EXPECT_THROW(evaluateFormula("1 / 0", resolver), FormulaEvaluationException);
}

// ---------------------------------------------------------------------
// Unary operators
// ---------------------------------------------------------------------

TEST(EvaluatorTest, UnaryMinusOnMoney) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(10, 0, usd));

    FormulaValue result = evaluateFormula("-#1000", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(-10, 0, usd));
}

TEST(EvaluatorTest, UnaryMinusOnScalar) {
    MapAccountResolver resolver;
    FormulaValue result = evaluateFormula("-5", resolver);
    EXPECT_EQ(result.asScalar(), Rational::ofInt(-5));
}

TEST(EvaluatorTest, UnaryPlusIsANoOp) {
    MapAccountResolver resolver;
    FormulaValue result = evaluateFormula("+5", resolver);
    EXPECT_EQ(result.asScalar(), Rational::ofInt(5));
}

// ---------------------------------------------------------------------
// Mixed expressions / parentheses
// ---------------------------------------------------------------------

TEST(EvaluatorTest, MixedExpressionWithParentheses) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(20, 0, usd));

    // (Cash - Rent) * 2 == (100 - 20) * 2 == 160
    FormulaValue result = evaluateFormula("(#1000 - #2000) * 2", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(160, 0, usd));
}

TEST(EvaluatorTest, DeeplyNestedMixedExpression) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(300, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(100, 0, usd));

    // #1000 / 2 - #2000 / 4 == 150 - 25 == 125
    FormulaValue result = evaluateFormula("#1000 / 2 - #2000 / 4", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(125, 0, usd));
}

// ---------------------------------------------------------------------
// Rounding: round-half-away-from-zero, both directions of the tie case
// ---------------------------------------------------------------------

TEST(EvaluatorTest, PositiveHalfMinorUnitRoundsAwayFromZero) {
    Currency usd("USD");
    MapAccountResolver resolver;
    // $0.01 * 0.5 == 0.5 minor units exactly -- a tie, must round away
    // from zero (up in magnitude) to 1 minor unit, not down to 0.
    resolver.set("1000", Money::ofMinorUnits(1, usd));

    FormulaValue result = evaluateFormula("#1000 * 0.5", resolver);
    EXPECT_EQ(result.asMoney(), Money::ofMinorUnits(1, usd));
}

TEST(EvaluatorTest, NegativeHalfMinorUnitRoundsAwayFromZero) {
    Currency usd("USD");
    MapAccountResolver resolver;
    // -$0.01 * 0.5 == -0.5 minor units exactly -- must round away from
    // zero (more negative) to -1 minor unit, not toward zero to 0.
    resolver.set("1000", Money::ofMinorUnits(-1, usd));

    FormulaValue result = evaluateFormula("#1000 * 0.5", resolver);
    EXPECT_EQ(result.asMoney(), Money::ofMinorUnits(-1, usd));
}

TEST(EvaluatorTest, PositiveNonHalfFractionRoundsToNearest) {
    Currency usd("USD");
    MapAccountResolver resolver;
    // $1.00 / 3 == 33.333... minor units -- rounds down (toward zero) to
    // 33, since the fractional part is well under half.
    resolver.set("1000", Money::fromMajorUnits(1, 0, usd));

    FormulaValue result = evaluateFormula("#1000 / 3", resolver);
    EXPECT_EQ(result.asMoney(), Money::ofMinorUnits(33, usd));
}

TEST(EvaluatorTest, NegativeNonHalfFractionRoundsToNearest) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(-1, 0, usd));

    FormulaValue result = evaluateFormula("#1000 / 3", resolver);
    EXPECT_EQ(result.asMoney(), Money::ofMinorUnits(-33, usd));
}

TEST(EvaluatorTest, FractionJustOverHalfRoundsAwayFromZero) {
    Currency usd("USD");
    MapAccountResolver resolver;
    // $0.02 / 3 == 0.6666... minor units -- rounds away from zero to 1.
    resolver.set("1000", Money::ofMinorUnits(2, usd));

    FormulaValue result = evaluateFormula("#1000 / 3", resolver);
    EXPECT_EQ(result.asMoney(), Money::ofMinorUnits(1, usd));
}

TEST(EvaluatorTest, DivisionThenMultiplicationMayDriftByOneCentByDesign) {
    // $100.00 / 3 * 3 does NOT necessarily reproduce $100.00 exactly,
    // because rounding happens at each Money-producing operation, not
    // deferred to the end. This is documented, deterministic behavior,
    // not silent truncation: $100.00 / 3 rounds to $33.33, and
    // $33.33 * 3 == $99.99.
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));

    FormulaValue result = evaluateFormula("#1000 / 3 * 3", resolver);
    EXPECT_EQ(result.asMoney(), Money::fromMajorUnits(99, 99, usd));
}

// ---------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------

TEST(EvaluatorTest, EvaluationIsDeterministic) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(100, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(40, 0, usd));

    AstNodePtr ast = parse("(#1000 - #2000) * 2 / 3");
    FormulaValue first = evaluate(*ast, resolver);
    FormulaValue second = evaluate(*ast, resolver);

    EXPECT_EQ(first.asMoney(), second.asMoney());
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(EvaluatorPropertyTest, AddingZeroIsIdentityForMoneyAndScalar) {
    // Money + Scalar is rejected regardless of the scalar's value (even
    // zero) -- the two operands must share the same "unit". So the
    // Money identity case adds a zero-valued Money account reference,
    // not the bare scalar literal 0.
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(123, 45, usd));
    resolver.set("2000", Money::zero(usd));

    EXPECT_EQ(evaluateFormula("#1000 + #2000", resolver).asMoney(), Money::fromMajorUnits(123, 45, usd));
    EXPECT_EQ(evaluateFormula("7 + 0", resolver).asScalar(), Rational::ofInt(7));
}

TEST(EvaluatorPropertyTest, MultiplyingByOneIsIdentityForMoneyAndScalar) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(123, 45, usd));

    EXPECT_EQ(evaluateFormula("#1000 * 1", resolver).asMoney(), Money::fromMajorUnits(123, 45, usd));
    EXPECT_EQ(evaluateFormula("7 * 1", resolver).asScalar(), Rational::ofInt(7));
}

TEST(EvaluatorPropertyTest, SubtractingSelfIsZero) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(50, 0, usd));

    EXPECT_TRUE(evaluateFormula("#1000 - #1000", resolver).asMoney().isZero());
    EXPECT_TRUE(evaluateFormula("9 - 9", resolver).asScalar().isZero());
}

TEST(EvaluatorPropertyTest, ParenthesizationEquivalenceWhenExactlyRepresentable) {
    Currency usd("USD");
    MapAccountResolver resolver;
    resolver.set("1000", Money::fromMajorUnits(30, 0, usd));
    resolver.set("2000", Money::fromMajorUnits(20, 0, usd));
    resolver.set("3000", Money::fromMajorUnits(10, 0, usd));

    // (a + b) + c == a + (b + c), no rounding involved anywhere.
    FormulaValue left = evaluateFormula("(#1000 + #2000) + #3000", resolver);
    FormulaValue right = evaluateFormula("#1000 + (#2000 + #3000)", resolver);
    EXPECT_EQ(left.asMoney(), right.asMoney());
}
