#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <type_traits>
#include <vector>

#include "ledgercore/computed/ComputedAccountDefinition.h"
#include "ledgercore/computed/ComputedAccountExceptions.h"
#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/computed/LedgerAccountResolver.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"

using ledgercore::computed::ComputedAccountAlreadyDefinedException;
using ledgercore::computed::ComputedAccountDefinition;
using ledgercore::computed::ComputedAccountRegistry;
using ledgercore::computed::FormulaCycleException;
using ledgercore::computed::LedgerAccountResolver;
using ledgercore::computed::UnknownComputedAccountException;
using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::formula::ComputedAccountName;
using ledgercore::formula::FormulaEvaluationException;
using ledgercore::formula::UnknownAccountReferenceException;
using ledgercore::ledger::Ledger;
using ledgercore::posting::post;

namespace {

std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}

struct StandardAccounts {
    AccountId cash;
    AccountId revenue;
    AccountId cogs;
    AccountId opex;
};

StandardAccounts setUpStandardChart(ChartOfAccounts& chart) {
    const AccountId cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
    const AccountId revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue).id();
    const AccountId cogs = chart.addRootAccount(AccountCode("5000"), "Cost of Goods Sold", AccountType::Expense).id();
    const AccountId opex = chart.addRootAccount(AccountCode("6000"), "Operating Expenses", AccountType::Expense).id();
    return StandardAccounts{cash, revenue, cogs, opex};
}

} // namespace

// ---------------------------------------------------------------------
// Definition
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, DefiningComputedAccountReturnsDefinition) {
    ComputedAccountRegistry registry;
    const ComputedAccountDefinition& def = registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    EXPECT_EQ(def.name(), ComputedAccountName("GrossProfit"));
    EXPECT_EQ(def.formulaSource(), "#4000 - #5000");
}

TEST(ComputedAccountRegistryTest, DuplicateDefinitionThrows) {
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    EXPECT_THROW(registry.define(ComputedAccountName("GrossProfit"), "#4000"), ComputedAccountAlreadyDefinedException);
}

TEST(ComputedAccountRegistryTest, FormulaSourcePreservedVerbatim) {
    ComputedAccountRegistry registry;
    const ComputedAccountDefinition& def = registry.define(ComputedAccountName("Foo"), "  #1000 +  #2000 ");
    EXPECT_EQ(def.formulaSource(), "  #1000 +  #2000 ");
}

TEST(ComputedAccountRegistryTest, AstIsParsedOnceAndReused) {
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Foo"), "1 + 2");

    const auto* first = registry.find(ComputedAccountName("Foo"));
    const auto* second = registry.find(ComputedAccountName("Foo"));
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    // Same definition instance, and therefore the same already-parsed
    // AST -- not reconstructed on each lookup.
    EXPECT_EQ(first, second);
    EXPECT_EQ(&first->ast(), &second->ast());
}

TEST(ComputedAccountRegistryTest, FindReturnsNullForUndefinedName) {
    ComputedAccountRegistry registry;
    EXPECT_EQ(registry.find(ComputedAccountName("Nope")), nullptr);
}

TEST(ComputedAccountRegistryTest, MultipleComputedAccountsAreIndependentlyDefined) {
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("A"), "1");
    registry.define(ComputedAccountName("B"), "2");
    registry.define(ComputedAccountName("C"), "3");

    std::vector<const ComputedAccountDefinition*> defs = registry.definitions();
    ASSERT_EQ(defs.size(), 3u);
    EXPECT_EQ(defs[0]->name(), ComputedAccountName("A"));
    EXPECT_EQ(defs[1]->name(), ComputedAccountName("B"));
    EXPECT_EQ(defs[2]->name(), ComputedAccountName("C"));
}

// ---------------------------------------------------------------------
// Real account resolution through evaluate()
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, EvaluatesRealLeafAccountFormula) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "COGS",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::debit(accounts.cogs, Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("GrossProfit"), resolver);

    EXPECT_EQ(result, Money::fromMajorUnits(300, 0, usd));
}

TEST(ComputedAccountRegistryTest, GroupAccountReferencePropagatesRejection) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Bad"), "#1000 * 2");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("Bad"), resolver), UnknownAccountReferenceException);
}

TEST(ComputedAccountRegistryTest, UnknownRealAccountPropagates) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Bad"), "#9999 + 1");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("Bad"), resolver), UnknownAccountReferenceException);
}

// ---------------------------------------------------------------------
// Computed-account dependencies
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, ComputedAccountResolvesAnotherComputedAccount) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "COGS",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::debit(accounts.cogs, Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
    registry.define(ComputedAccountName("DoubleGrossProfit"), "@GrossProfit * 2");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("DoubleGrossProfit"), resolver);

    EXPECT_EQ(result, Money::fromMajorUnits(600, 0, usd));
}

TEST(ComputedAccountRegistryTest, MultiLevelDependencyChain) {
    // OperatingIncome = GrossProfit - OperatingExpenses
    // GrossProfit = Revenue - CostOfGoodsSold
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1000, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(1000, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "COGS",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(400, 0, usd)),
                                   JournalEntryLine::debit(accounts.cogs, Money::fromMajorUnits(400, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "OpEx",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(150, 0, usd)),
                                   JournalEntryLine::debit(accounts.opex, Money::fromMajorUnits(150, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
    registry.define(ComputedAccountName("OperatingIncome"), "@GrossProfit - #6000");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("OperatingIncome"), resolver);

    // GrossProfit = 1000 - 400 = 600; OperatingIncome = 600 - 150 = 450
    EXPECT_EQ(result, Money::fromMajorUnits(450, 0, usd));
}

TEST(ComputedAccountRegistryTest, SharedDependencyDiamondIsNotACycle) {
    // A = B + C; B = D * 2; C = D * 3; D = #4000
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("D"), "#4000");
    registry.define(ComputedAccountName("B"), "@D * 2");
    registry.define(ComputedAccountName("C"), "@D * 3");
    registry.define(ComputedAccountName("A"), "@B + @C");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("A"), resolver);

    // D=100, B=200, C=300, A=500 -- D is resolved twice (once via B, once
    // via C) without being flagged as a cycle, since it is popped off
    // the DFS stack as soon as each branch's resolution of it completes.
    EXPECT_EQ(result, Money::fromMajorUnits(500, 0, usd));
}

// ---------------------------------------------------------------------
// Cycle detection
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, SelfCycleThrowsFormulaCycleException) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("A"), "@A + 1");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("A"), resolver), FormulaCycleException);
}

TEST(ComputedAccountRegistryTest, MultiNodeCycleThrowsFormulaCycleException) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("A"), "@B + 1");
    registry.define(ComputedAccountName("B"), "@C + 1");
    registry.define(ComputedAccountName("C"), "@A + 1");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("A"), resolver), FormulaCycleException);
}

TEST(ComputedAccountRegistryTest, CyclePathIsReportedCompletely) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("A"), "@B + 1");
    registry.define(ComputedAccountName("B"), "@C + 1");
    registry.define(ComputedAccountName("C"), "@A + 1");

    LedgerAccountResolver resolver(chart, ledger);
    try {
        registry.evaluate(ComputedAccountName("A"), resolver);
        FAIL() << "expected FormulaCycleException";
    } catch (const FormulaCycleException& ex) {
        ASSERT_EQ(ex.path().size(), 4u);
        EXPECT_EQ(ex.path()[0], ComputedAccountName("A"));
        EXPECT_EQ(ex.path()[1], ComputedAccountName("B"));
        EXPECT_EQ(ex.path()[2], ComputedAccountName("C"));
        EXPECT_EQ(ex.path()[3], ComputedAccountName("A"));
    }
}

TEST(ComputedAccountRegistryTest, UnknownComputedReferenceThrows) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("A"), "@NeverDefined + 1");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("A"), resolver), UnknownComputedAccountException);
}

TEST(ComputedAccountRegistryTest, EvaluatingUndefinedTopLevelNameThrows) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("Nope"), resolver), UnknownComputedAccountException);
}

// ---------------------------------------------------------------------
// Determinism, results, arithmetic, currency
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, DeterministicRepeatedEvaluation) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(300, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Doubled"), "#4000 * 2");

    LedgerAccountResolver resolver(chart, ledger);
    Money first = registry.evaluate(ComputedAccountName("Doubled"), resolver);
    Money second = registry.evaluate(ComputedAccountName("Doubled"), resolver);
    Money third = registry.evaluate(ComputedAccountName("Doubled"), resolver);

    EXPECT_EQ(first, second);
    EXPECT_EQ(second, third);
    EXPECT_EQ(first, Money::fromMajorUnits(600, 0, usd));
}

TEST(ComputedAccountRegistryTest, NegativeComputedResult) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Small sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Big COGS",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(400, 0, usd)),
                                   JournalEntryLine::debit(accounts.cogs, Money::fromMajorUnits(400, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("GrossProfit"), resolver);

    EXPECT_TRUE(result.isNegative());
    EXPECT_EQ(result, Money::fromMajorUnits(-300, 0, usd));
}

TEST(ComputedAccountRegistryTest, ZeroComputedResult) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Matching COGS",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::debit(accounts.cogs, Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("GrossProfit"), resolver);

    EXPECT_TRUE(result.isZero());
}

TEST(ComputedAccountRegistryTest, ExactMoneyArithmeticThroughDependencyChain) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 33, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 33, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "COGS",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(0, 11, usd)),
                                   JournalEntryLine::debit(accounts.cogs, Money::fromMajorUnits(0, 11, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("GrossProfit"), resolver);

    EXPECT_EQ(result, Money::fromMajorUnits(100, 22, usd));
}

TEST(ComputedAccountRegistryTest, CurrencyIsPreservedThroughComputedEvaluation) {
    Currency eur("EUR");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(eur);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(50, 0, eur)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(50, 0, eur)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("DoubleRevenue"), "#4000 * 2");

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("DoubleRevenue"), resolver);

    EXPECT_EQ(result.currency(), eur);
    EXPECT_EQ(result, Money::fromMajorUnits(100, 0, eur));
}

// ---------------------------------------------------------------------
// Error propagation
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, FormulaEvaluationExceptionPropagates) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Bad"), "#4000 / 0");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("Bad"), resolver), FormulaEvaluationException);
}

TEST(ComputedAccountRegistryTest, RejectedMoneyTimesMoneyPropagates) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Bad"), "#4000 * #5000");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("Bad"), resolver), FormulaEvaluationException);
}

TEST(ComputedAccountRegistryTest, BareScalarFormulaIsRejectedByComputedEvaluation) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("JustAScalar"), "2 + 3");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("JustAScalar"), resolver), FormulaEvaluationException);
}

TEST(ComputedAccountRegistryTest, DependencyErrorPropagatesThroughChain) {
    // OperatingIncome depends on GrossProfit, which references an
    // account that does not exist -- the failure must surface at the
    // top-level evaluate() call, not be silently swallowed.
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#9999 - #5000");
    registry.define(ComputedAccountName("OperatingIncome"), "@GrossProfit - #6000");

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(registry.evaluate(ComputedAccountName("OperatingIncome"), resolver), UnknownAccountReferenceException);
}

// ---------------------------------------------------------------------
// Ledger isolation / posting impossibility
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, EvaluatingComputedAccountNeverMutatesLedger) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(400, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(400, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
    registry.define(ComputedAccountName("OperatingIncome"), "@GrossProfit - #6000");

    const Money cashBefore = ledger.balance(accounts.cash);
    const Money revenueBefore = ledger.balance(accounts.revenue);
    const std::size_t historyBefore = ledger.postedEntries().size();

    LedgerAccountResolver resolver(chart, ledger);
    registry.evaluate(ComputedAccountName("OperatingIncome"), resolver);

    EXPECT_EQ(ledger.balance(accounts.cash), cashBefore);
    EXPECT_EQ(ledger.balance(accounts.revenue), revenueBefore);
    EXPECT_EQ(ledger.postedEntries().size(), historyBefore);
}

TEST(ComputedAccountRegistryTest, ComputedAccountNameCannotBeUsedToConstructAnAccountId) {
    // A ComputedAccountName has no conversion path to AccountId, so
    // referencing a computed account from a JournalEntryLine (and
    // therefore from PostingEngine::post()) is a compile-time
    // impossibility, not a runtime check. This test documents that
    // guarantee: it would fail to compile, not fail at runtime, if that
    // guarantee were ever broken.
    static_assert(!std::is_constructible_v<AccountId, ComputedAccountName>,
                  "ComputedAccountName must never be usable to construct an AccountId");
    static_assert(!std::is_convertible_v<ComputedAccountName, AccountId>,
                  "ComputedAccountName must never be implicitly convertible to AccountId");
    SUCCEED();
}

// ---------------------------------------------------------------------
// Different Ledger contexts, larger graphs
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryTest, SameRegistryEvaluatesDifferentLedgerContextsIndependently) {
    Currency usd("USD");

    ChartOfAccounts chartA;
    Account& cashA = chartA.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenueA = chartA.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledgerA(usd);
    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cashA.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenueA.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chartA, ledgerA);

    ChartOfAccounts chartB;
    Account& cashB = chartB.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenueB = chartB.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledgerB(usd);
    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cashB.id(), Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(revenueB.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chartB, ledgerB);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Total"), "#1000 + #4000");

    LedgerAccountResolver resolverA(chartA, ledgerA);
    LedgerAccountResolver resolverB(chartB, ledgerB);

    EXPECT_EQ(registry.evaluate(ComputedAccountName("Total"), resolverA), Money::fromMajorUnits(200, 0, usd));
    EXPECT_EQ(registry.evaluate(ComputedAccountName("Total"), resolverB), Money::fromMajorUnits(1000, 0, usd));
}

TEST(ComputedAccountRegistryTest, LargerDependencyGraphEvaluatesCorrectly) {
    // Node0 = #4000; Node_i = @Node_{i-1} * 2 for i = 1..9.
    // (Money + a bare scalar is rejected -- dimensional mismatch, per
    // the Formula Engine's own rules -- so the chain scales by a scalar
    // multiplier rather than adding one.)
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Seed",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(10, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(10, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Node0"), "#4000");
    for (int i = 1; i <= 9; ++i) {
        registry.define(ComputedAccountName("Node" + std::to_string(i)),
                         "@Node" + std::to_string(i - 1) + " * 2");
    }

    LedgerAccountResolver resolver(chart, ledger);
    Money result = registry.evaluate(ComputedAccountName("Node9"), resolver);

    // 10 * 2^9 = 5120
    EXPECT_EQ(result, Money::fromMajorUnits(5120, 0, usd));
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(ComputedAccountRegistryPropertyTest, AcyclicDependencyGraphsAlwaysTerminate) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);
    post(JournalEntry::create(testDate(), "Seed",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Base"), "#4000");
    for (int i = 0; i < 20; ++i) {
        const std::string current = "N" + std::to_string(i);
        const std::string previous = (i == 0) ? "Base" : ("N" + std::to_string(i - 1));
        registry.define(ComputedAccountName(current), "@" + previous + " * 1");
    }

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_NO_THROW(registry.evaluate(ComputedAccountName("N19"), resolver));
}

TEST(ComputedAccountRegistryPropertyTest, CyclicGraphsAreAlwaysRejected) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    LedgerAccountResolver resolver(chart, ledger);

    struct Case {
        std::vector<std::pair<std::string, std::string>> definitions; // name -> formula
        std::string evaluate;
    };
    const std::vector<Case> cases = {
        {{{"A", "@A + 1"}}, "A"},
        {{{"A", "@B + 1"}, {"B", "@A + 1"}}, "A"},
        {{{"A", "@B + 1"}, {"B", "@C + 1"}, {"C", "@A + 1"}}, "A"},
    };

    for (const Case& testCase : cases) {
        ComputedAccountRegistry registry;
        for (const auto& [name, formulaSource] : testCase.definitions) {
            registry.define(ComputedAccountName(name), formulaSource);
        }
        EXPECT_THROW(registry.evaluate(ComputedAccountName(testCase.evaluate), resolver), FormulaCycleException);
    }
}

TEST(ComputedAccountRegistryPropertyTest, RepeatedEvaluationWithUnchangedInputsIsDeterministic) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(777, 77, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(777, 77, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
    registry.define(ComputedAccountName("OperatingIncome"), "@GrossProfit - #6000");

    LedgerAccountResolver resolver(chart, ledger);
    const Money reference = registry.evaluate(ComputedAccountName("OperatingIncome"), resolver);

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(registry.evaluate(ComputedAccountName("OperatingIncome"), resolver), reference);
    }
}

TEST(ComputedAccountRegistryPropertyTest, EvaluatingNeverMutatesLedgerAcrossManyCalls) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
    registry.define(ComputedAccountName("OperatingIncome"), "@GrossProfit - #6000");

    const std::size_t historyBefore = ledger.postedEntries().size();
    LedgerAccountResolver resolver(chart, ledger);
    for (int i = 0; i < 25; ++i) {
        registry.evaluate(ComputedAccountName("OperatingIncome"), resolver);
    }

    EXPECT_EQ(ledger.postedEntries().size(), historyBefore);
}
