#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/domain/NormalBalance.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/trialbalance/TrialBalance.h"
#include "ledgercore/trialbalance/TrialBalanceExceptions.h"

using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::DebitCreditAmounts;
using ledgercore::domain::debitCreditPresentation;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::domain::Period;
using ledgercore::ledger::Ledger;
using ledgercore::ledger::PostedJournalEntry;
using ledgercore::posting::post;
using ledgercore::trialbalance::TrialBalance;
using ledgercore::trialbalance::TrialBalanceLine;
using ledgercore::trialbalance::UnbalancedTrialBalanceException;

namespace {

std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}

// A synthetic, deterministic "day N" timestamp for period tests -- no
// real calendar library is needed, only time_point ordering.
std::chrono::system_clock::time_point day(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * n);
}

const TrialBalanceLine& findLine(const TrialBalance& tb, AccountId accountId) {
    for (const TrialBalanceLine& line : tb.lines()) {
        if (line.accountId() == accountId) {
            return line;
        }
    }
    throw std::runtime_error("TrialBalanceTest: account not found in generated TrialBalance");
}

} // namespace

// ---------------------------------------------------------------------
// Empty ledger / empty chart
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, EmptyChartAndLedgerProducesEmptyTrialBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    EXPECT_TRUE(tb.lines().empty());
    EXPECT_TRUE(tb.totalDebits().isZero());
    EXPECT_TRUE(tb.totalCredits().isZero());
    EXPECT_EQ(tb.currency(), usd);
}

TEST(TrialBalanceTest, UnpostedLeafAccountsAppearWithZeroBalances) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 2u);
    for (const TrialBalanceLine& line : tb.lines()) {
        EXPECT_TRUE(line.debit().isZero());
        EXPECT_TRUE(line.credit().isZero());
    }
    EXPECT_TRUE(tb.totalDebits().isZero());
    EXPECT_TRUE(tb.totalCredits().isZero());
}

// ---------------------------------------------------------------------
// Single / multiple posted transactions
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, SingleBalancedTransactionProducesMatchingLines) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Sales Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Cash sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 2u);
    EXPECT_EQ(tb.totalDebits(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(tb.totalCredits(), Money::fromMajorUnits(100, 0, usd));
}

TEST(TrialBalanceTest, MultiplePostedTransactionsAccumulateIntoLines) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale 1",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Sale 2",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(150, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(150, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Pay rent",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(80, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(80, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 3u);
    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(270, 0, usd));
    EXPECT_EQ(findLine(tb, revenue.id()).credit(), Money::fromMajorUnits(350, 0, usd));
    EXPECT_EQ(findLine(tb, expense.id()).debit(), Money::fromMajorUnits(80, 0, usd));
}

// ---------------------------------------------------------------------
// Per-AccountType presentation
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, AssetDebitBalancePresentsAsDebit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Equity", AccountType::Equity);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Owner investment",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(equity.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& cashLine = findLine(tb, cash.id());
    EXPECT_EQ(cashLine.debit(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_TRUE(cashLine.credit().isZero());
}

TEST(TrialBalanceTest, AssetCreditNegativeBalancePresentsAsCredit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Overdraw",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& cashLine = findLine(tb, cash.id());
    EXPECT_TRUE(cashLine.debit().isZero());
    EXPECT_EQ(cashLine.credit(), Money::fromMajorUnits(200, 0, usd));
}

TEST(TrialBalanceTest, LiabilityCreditBalancePresentsAsCredit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& loan = chart.addRootAccount(AccountCode("2200"), "Bank Loan", AccountType::Liability);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Loan proceeds",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1000, 0, usd)),
                                   JournalEntryLine::credit(loan.id(), Money::fromMajorUnits(1000, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& loanLine = findLine(tb, loan.id());
    EXPECT_EQ(loanLine.credit(), Money::fromMajorUnits(1000, 0, usd));
    EXPECT_TRUE(loanLine.debit().isZero());
}

TEST(TrialBalanceTest, LiabilityDebitNegativeBalancePresentsAsDebit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& loan = chart.addRootAccount(AccountCode("2200"), "Bank Loan", AccountType::Liability);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Overpay loan",
                               {
                                   JournalEntryLine::debit(loan.id(), Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& loanLine = findLine(tb, loan.id());
    EXPECT_EQ(loanLine.debit(), Money::fromMajorUnits(300, 0, usd));
    EXPECT_TRUE(loanLine.credit().isZero());
}

TEST(TrialBalanceTest, EquityCreditBalancePresentsAsCredit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Owner's Equity", AccountType::Equity);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Owner investment",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(750, 0, usd)),
                                   JournalEntryLine::credit(equity.id(), Money::fromMajorUnits(750, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& equityLine = findLine(tb, equity.id());
    EXPECT_EQ(equityLine.credit(), Money::fromMajorUnits(750, 0, usd));
    EXPECT_TRUE(equityLine.debit().isZero());
}

TEST(TrialBalanceTest, RevenueCreditBalancePresentsAsCredit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Sales Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Cash sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(250, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(250, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& revenueLine = findLine(tb, revenue.id());
    EXPECT_EQ(revenueLine.credit(), Money::fromMajorUnits(250, 0, usd));
    EXPECT_TRUE(revenueLine.debit().isZero());
}

TEST(TrialBalanceTest, ExpenseDebitBalancePresentsAsDebit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& rent = chart.addRootAccount(AccountCode("5100"), "Rent Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Pay rent",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(120, 0, usd)),
                                   JournalEntryLine::debit(rent.id(), Money::fromMajorUnits(120, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& rentLine = findLine(tb, rent.id());
    EXPECT_EQ(rentLine.debit(), Money::fromMajorUnits(120, 0, usd));
    EXPECT_TRUE(rentLine.credit().isZero());
}

TEST(TrialBalanceTest, AllAccountTypeSignedBalancePresentationCasesAreCorrect) {
    // All 10 AccountType x sign combinations, produced by actually posting
    // through PostingEngine and reading the result back through
    // TrialBalance::generate() -- an end-to-end complement to the pure
    // unit coverage in tests/domain/NormalBalanceTest.cpp.
    Currency usd("USD");
    struct Case {
        AccountType type;
        bool debitNormal;
    };
    const Case cases[] = {
        {AccountType::Asset, true},   {AccountType::Expense, true},  {AccountType::Liability, false},
        {AccountType::Equity, false}, {AccountType::Revenue, false},
    };

    for (const Case& testCase : cases) {
        {
            // Normal-side (positive) balance.
            ChartOfAccounts chart;
            Ledger ledger(usd);
            Account& subject = chart.addRootAccount(AccountCode("1"), "Subject", testCase.type);
            Account& other = chart.addRootAccount(AccountCode("2"), "Other", AccountType::Asset);
            std::vector<JournalEntryLine> lines{
                testCase.debitNormal ? JournalEntryLine::debit(subject.id(), Money::fromMajorUnits(10, 0, usd))
                                      : JournalEntryLine::credit(subject.id(), Money::fromMajorUnits(10, 0, usd)),
                testCase.debitNormal ? JournalEntryLine::credit(other.id(), Money::fromMajorUnits(10, 0, usd))
                                      : JournalEntryLine::debit(other.id(), Money::fromMajorUnits(10, 0, usd)),
            };
            post(JournalEntry::create(testDate(), "Normal-side balance", lines), chart, ledger);

            TrialBalance tb = TrialBalance::generate(chart, ledger);
            const TrialBalanceLine& subjectLine = findLine(tb, subject.id());
            if (testCase.debitNormal) {
                EXPECT_EQ(subjectLine.debit(), Money::fromMajorUnits(10, 0, usd));
                EXPECT_TRUE(subjectLine.credit().isZero());
            } else {
                EXPECT_EQ(subjectLine.credit(), Money::fromMajorUnits(10, 0, usd));
                EXPECT_TRUE(subjectLine.debit().isZero());
            }
        }
        {
            // Opposite-side (negative) balance.
            ChartOfAccounts chart;
            Ledger ledger(usd);
            Account& subject = chart.addRootAccount(AccountCode("1"), "Subject", testCase.type);
            Account& other = chart.addRootAccount(AccountCode("2"), "Other", AccountType::Asset);
            std::vector<JournalEntryLine> lines{
                testCase.debitNormal ? JournalEntryLine::credit(subject.id(), Money::fromMajorUnits(10, 0, usd))
                                      : JournalEntryLine::debit(subject.id(), Money::fromMajorUnits(10, 0, usd)),
                testCase.debitNormal ? JournalEntryLine::debit(other.id(), Money::fromMajorUnits(10, 0, usd))
                                      : JournalEntryLine::credit(other.id(), Money::fromMajorUnits(10, 0, usd)),
            };
            post(JournalEntry::create(testDate(), "Opposite-side balance", lines), chart, ledger);

            TrialBalance tb = TrialBalance::generate(chart, ledger);
            const TrialBalanceLine& subjectLine = findLine(tb, subject.id());
            if (testCase.debitNormal) {
                EXPECT_EQ(subjectLine.credit(), Money::fromMajorUnits(10, 0, usd));
                EXPECT_TRUE(subjectLine.debit().isZero());
            } else {
                EXPECT_EQ(subjectLine.debit(), Money::fromMajorUnits(10, 0, usd));
                EXPECT_TRUE(subjectLine.credit().isZero());
            }
        }
    }
}

// ---------------------------------------------------------------------
// Account universe: zero balances included, group accounts excluded
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, GroupAccountsAreExcluded) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Cash sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 2u);
    for (const TrialBalanceLine& line : tb.lines()) {
        EXPECT_NE(line.accountId(), assets.id());
    }
}

// ---------------------------------------------------------------------
// Ordering
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, LinesAreOrderedByAscendingAccountCode) {
    Currency usd("USD");
    ChartOfAccounts chart;
    // Insert deliberately out of code order.
    chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    chart.addRootAccount(AccountCode("2000"), "Liability", AccountType::Liability);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 4u);
    EXPECT_EQ(tb.lines()[0].accountCode().value(), "1000");
    EXPECT_EQ(tb.lines()[1].accountCode().value(), "2000");
    EXPECT_EQ(tb.lines()[2].accountCode().value(), "4000");
    EXPECT_EQ(tb.lines()[3].accountCode().value(), "5000");
}

// ---------------------------------------------------------------------
// Metadata / identity / currency
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, AccountMetadataIsCopiedCorrectly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 1u);
    const TrialBalanceLine& line = tb.lines().front();
    EXPECT_EQ(line.accountCode().value(), "1000");
    EXPECT_EQ(line.accountName(), "Cash");
    EXPECT_EQ(line.accountType(), AccountType::Asset);
}

TEST(TrialBalanceTest, AccountIdIsCopiedCorrectly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 1u);
    EXPECT_EQ(tb.lines().front().accountId(), cash.id());
}

TEST(TrialBalanceTest, CurrencyMatchesLedgerCurrency) {
    Currency eur("EUR");
    ChartOfAccounts chart;
    Ledger ledger(eur);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    EXPECT_EQ(tb.currency(), eur);
}

// ---------------------------------------------------------------------
// Totals
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, TotalsReflectSumOfAllLinesAndBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(400, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(400, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Expense",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(150, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(150, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    EXPECT_EQ(tb.totalDebits(), Money::fromMajorUnits(400, 0, usd));
    EXPECT_EQ(tb.totalCredits(), Money::fromMajorUnits(400, 0, usd));
    EXPECT_EQ(tb.totalDebits(), tb.totalCredits());
}

// ---------------------------------------------------------------------
// Duplicate lines / multiple entries / negative balances / large values
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, MultipleLinesAffectingSameAccountAggregateIntoOneLine) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Two cash debits",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(150, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);

    ASSERT_EQ(tb.lines().size(), 2u);
    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(150, 0, usd));
}

TEST(TrialBalanceTest, LegitimateNegativeBalanceIsPresentedOnOppositeColumn) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Overdraw",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(900, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(900, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    const TrialBalanceLine& cashLine = findLine(tb, cash.id());
    EXPECT_TRUE(ledger.balance(cash.id()).isNegative());
    EXPECT_EQ(cashLine.credit(), Money::fromMajorUnits(900, 0, usd));
    EXPECT_TRUE(cashLine.debit().isZero());
}

TEST(TrialBalanceTest, LargeMoneyValuesArePresentedExactly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Equity", AccountType::Equity);
    Ledger ledger(usd);

    const Money huge = Money::ofMinorUnits(std::numeric_limits<std::int64_t>::max(), usd);
    post(JournalEntry::create(testDate(), "Huge deposit",
                               {
                                   JournalEntryLine::debit(cash.id(), huge),
                                   JournalEntryLine::credit(equity.id(), huge),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    EXPECT_EQ(findLine(tb, cash.id()).debit(), huge);
    EXPECT_EQ(tb.totalDebits(), huge);
    EXPECT_EQ(tb.totalCredits(), huge);
}

// ---------------------------------------------------------------------
// Snapshot independence
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, SnapshotIsIndependentOfSubsequentPosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "First sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance snapshot = TrialBalance::generate(chart, ledger);

    post(JournalEntry::create(testDate(), "Second sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    // The ledger has moved on, but the earlier snapshot must not change.
    EXPECT_EQ(snapshot.totalDebits(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(findLine(snapshot, cash.id()).debit(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(150, 0, usd));

    TrialBalance fresh = TrialBalance::generate(chart, ledger);
    EXPECT_EQ(fresh.totalDebits(), Money::fromMajorUnits(150, 0, usd));
}

// ---------------------------------------------------------------------
// Replay consistency
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, ReplayConsistencyWithPostedEntries) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale 1",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(300, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Buy supplies",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    Ledger replay(usd);
    for (const PostedJournalEntry& posted : ledger.postedEntries()) {
        post(posted.entry(), chart, replay);
    }

    TrialBalance original = TrialBalance::generate(chart, ledger);
    TrialBalance replayed = TrialBalance::generate(chart, replay);

    ASSERT_EQ(original.lines().size(), replayed.lines().size());
    for (std::size_t i = 0; i < original.lines().size(); ++i) {
        EXPECT_EQ(original.lines()[i].accountId(), replayed.lines()[i].accountId());
        EXPECT_EQ(original.lines()[i].debit(), replayed.lines()[i].debit());
        EXPECT_EQ(original.lines()[i].credit(), replayed.lines()[i].credit());
    }
    EXPECT_EQ(original.totalDebits(), replayed.totalDebits());
    EXPECT_EQ(original.totalCredits(), replayed.totalCredits());
}

// ---------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, MismatchedChartAndLedgerThrowsUnbalancedTrialBalanceException) {
    Currency usd("USD");
    ChartOfAccounts fullChart;
    Account& cash = fullChart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = fullChart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Cash sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         fullChart, ledger);

    // A different chart whose only account happens to line up (by
    // creation order) with Cash's AccountId. Revenue's offsetting balance
    // is invisible to this chart's traversal, so the derived totals
    // cannot balance.
    ChartOfAccounts partialChart;
    partialChart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);

    EXPECT_THROW(TrialBalance::generate(partialChart, ledger), UnbalancedTrialBalanceException);
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(TrialBalancePropertyTest, TotalsAlwaysBalanceAfterAnySuccessfulPostingSequence) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Equity", AccountType::Equity);
    Ledger ledger(usd);

    std::mt19937_64 rng(101);
    std::uniform_int_distribution<std::int64_t> amountDist(1, 100'000);
    const std::vector<AccountId> creditCandidates{revenue.id(), equity.id()};
    const std::vector<AccountId> debitCandidates{cash.id(), expense.id()};

    for (int trial = 0; trial < 100; ++trial) {
        const Money amount = Money::ofMinorUnits(amountDist(rng), usd);
        const AccountId debitAccount = debitCandidates[static_cast<std::size_t>(trial) % debitCandidates.size()];
        const AccountId creditAccount = creditCandidates[static_cast<std::size_t>(trial) % creditCandidates.size()];

        post(JournalEntry::create(testDate(), "Random entry",
                                   {
                                       JournalEntryLine::debit(debitAccount, amount),
                                       JournalEntryLine::credit(creditAccount, amount),
                                   }),
             chart, ledger);

        TrialBalance tb = TrialBalance::generate(chart, ledger);
        EXPECT_EQ(tb.totalDebits(), tb.totalCredits());
    }
}

TEST(TrialBalancePropertyTest, ProjectionMatchesLedgerBalancesReconstructedFromHistory) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(220, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(220, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Expense",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(70, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(70, 0, usd)),
                               }),
         chart, ledger);

    // Reconstruct a Ledger purely from postedEntries(), independent of the
    // original ledger's own cached balances.
    Ledger reconstructed(usd);
    for (const PostedJournalEntry& posted : ledger.postedEntries()) {
        post(posted.entry(), chart, reconstructed);
    }

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    for (const TrialBalanceLine& line : tb.lines()) {
        const DebitCreditAmounts expected =
            debitCreditPresentation(line.accountType(), reconstructed.balance(line.accountId()));
        EXPECT_EQ(line.debit(), expected.debit);
        EXPECT_EQ(line.credit(), expected.credit);
    }
}

// =======================================================================
// Period accounting: generateAsOf() / generateForPeriod()
// =======================================================================

// ---------------------------------------------------------------------
// generateAsOf(): cutoff boundary semantics
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, GenerateAsOfIncludesOnlyEntriesBeforeCutoff) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(1), "Before cutoff",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(10), "After cutoff",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateAsOf(chart, ledger, day(5));

    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(findLine(tb, revenue.id()).credit(), Money::fromMajorUnits(100, 0, usd));
}

TEST(TrialBalanceTest, GenerateAsOfExcludesTransactionExactlyAtCutoff) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "Exactly at cutoff",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(75, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(75, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateAsOf(chart, ledger, day(5));

    EXPECT_TRUE(findLine(tb, cash.id()).debit().isZero());
    EXPECT_TRUE(findLine(tb, revenue.id()).credit().isZero());
}

TEST(TrialBalanceTest, GenerateAsOfExcludesTransactionAfterCutoff) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(6), "After cutoff",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(75, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(75, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateAsOf(chart, ledger, day(5));

    EXPECT_TRUE(findLine(tb, cash.id()).debit().isZero());
}

// ---------------------------------------------------------------------
// generateForPeriod(): [start, end) boundary semantics
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, GenerateForPeriodIncludesTransactionExactlyAtStart) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(1), "At period start",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(40, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(40, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(40, 0, usd));
}

TEST(TrialBalanceTest, GenerateForPeriodExcludesTransactionExactlyAtEnd) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(10), "At period end",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(40, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(40, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_TRUE(findLine(tb, cash.id()).debit().isZero());
}

TEST(TrialBalanceTest, GenerateForPeriodExcludesTransactionsOutsideRange) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(0), "Before period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(10, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(10, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(5), "Inside period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(20, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(20, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(15), "After period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(30, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(30, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(20, 0, usd));
}

// ---------------------------------------------------------------------
// Backdated entries / unsorted postedEntries()
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, BackdatedEntriesAreFilteredByBusinessDateNotPostingOrder) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    // Post a "later" entry first...
    post(JournalEntry::create(day(20), "Posted first, dated later",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    // ...then a backdated entry, posted second but dated earlier.
    post(JournalEntry::create(day(1), "Posted second, backdated",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(5, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(5, 0, usd)),
                               }),
         chart, ledger);

    // Confirm postedEntries() really is out of chronological order, so
    // this test genuinely exercises the case it claims to.
    ASSERT_EQ(ledger.postedEntries().size(), 2u);
    EXPECT_GT(ledger.postedEntries()[0].entry().date(), ledger.postedEntries()[1].entry().date());

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(0), day(10)));

    // Only the backdated, earlier-dated entry falls inside the period,
    // even though it was posted second.
    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(5, 0, usd));
}

TEST(TrialBalanceTest, GenerateForPeriodDoesNotAssumePostedEntriesAreDateSorted) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    // Posted in a deliberately non-chronological order.
    post(JournalEntry::create(day(15), "Posted 1st, dated 15",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(3), "Posted 2nd, dated 3",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(2, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(2, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(9), "Posted 3rd, dated 9",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(4, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(4, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(6, 0, usd)); // day(3) + day(9), not day(15)
}

// ---------------------------------------------------------------------
// Empty period / zero-activity accounts
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, GenerateForPeriodWithNoMatchingEntriesProducesValidZeroActivity) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(100), "Far outside the period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    ASSERT_EQ(tb.lines().size(), 2u);
    EXPECT_TRUE(tb.totalDebits().isZero());
    EXPECT_TRUE(tb.totalCredits().isZero());
    for (const TrialBalanceLine& line : tb.lines()) {
        EXPECT_TRUE(line.debit().isZero());
        EXPECT_TRUE(line.credit().isZero());
    }
}

TEST(TrialBalanceTest, ZeroActivityAccountsRemainPresentInPeriodTrialBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "Sale in period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    // Expense has activity, but not during this period.
    post(JournalEntry::create(day(50), "Expense outside period",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(20, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(20, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    ASSERT_EQ(tb.lines().size(), 3u);
    EXPECT_TRUE(findLine(tb, expense.id()).debit().isZero());
}

// ---------------------------------------------------------------------
// All five AccountTypes / negative balances / exact arithmetic
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, AllAccountTypesRespectPeriodFilteringCorrectly) {
    Currency usd("USD");
    struct Case {
        AccountType type;
        bool debitNormal;
    };
    const Case cases[] = {
        {AccountType::Asset, true},   {AccountType::Expense, true},  {AccountType::Liability, false},
        {AccountType::Equity, false}, {AccountType::Revenue, false},
    };

    for (const Case& testCase : cases) {
        ChartOfAccounts chart;
        Ledger ledger(usd);
        Account& subject = chart.addRootAccount(AccountCode("1"), "Subject", testCase.type);
        Account& other = chart.addRootAccount(AccountCode("2"), "Other", AccountType::Asset);

        post(JournalEntry::create(day(5), "In period",
                                   {
                                       testCase.debitNormal
                                           ? JournalEntryLine::debit(subject.id(), Money::fromMajorUnits(10, 0, usd))
                                           : JournalEntryLine::credit(subject.id(), Money::fromMajorUnits(10, 0, usd)),
                                       testCase.debitNormal
                                           ? JournalEntryLine::credit(other.id(), Money::fromMajorUnits(10, 0, usd))
                                           : JournalEntryLine::debit(other.id(), Money::fromMajorUnits(10, 0, usd)),
                                   }),
             chart, ledger);
        // Outside the period -- must not affect the period result.
        post(JournalEntry::create(day(50), "Outside period",
                                   {
                                       testCase.debitNormal
                                           ? JournalEntryLine::debit(subject.id(), Money::fromMajorUnits(999, 0, usd))
                                           : JournalEntryLine::credit(subject.id(), Money::fromMajorUnits(999, 0, usd)),
                                       testCase.debitNormal
                                           ? JournalEntryLine::credit(other.id(), Money::fromMajorUnits(999, 0, usd))
                                           : JournalEntryLine::debit(other.id(), Money::fromMajorUnits(999, 0, usd)),
                                   }),
             chart, ledger);

        TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));
        const TrialBalanceLine& subjectLine = findLine(tb, subject.id());

        if (testCase.debitNormal) {
            EXPECT_EQ(subjectLine.debit(), Money::fromMajorUnits(10, 0, usd));
            EXPECT_TRUE(subjectLine.credit().isZero());
        } else {
            EXPECT_EQ(subjectLine.credit(), Money::fromMajorUnits(10, 0, usd));
            EXPECT_TRUE(subjectLine.debit().isZero());
        }
    }
}

TEST(TrialBalanceTest, NegativeBalanceIsPresentedOnOppositeColumnInPeriodTrialBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "Overdraw in period",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(300, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    const TrialBalanceLine& cashLine = findLine(tb, cash.id());
    EXPECT_EQ(cashLine.credit(), Money::fromMajorUnits(300, 0, usd));
    EXPECT_TRUE(cashLine.debit().isZero());
}

TEST(TrialBalanceTest, ExactMoneyArithmeticInPeriodTrialBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(2), "Cents 1",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(0, 30, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(0, 30, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(4), "Cents 2",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(0, 10, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(0, 10, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(4), "Cents 3",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(0, 20, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(0, 20, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    // 0.30 + 0.10 + 0.20 == 0.60 exactly.
    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(0, 60, usd));
}

// ---------------------------------------------------------------------
// Currency / ordering
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, GenerateAsOfAndGenerateForPeriodPreserveCurrency) {
    Currency eur("EUR");
    ChartOfAccounts chart;
    Ledger ledger(eur);

    TrialBalance asOf = TrialBalance::generateAsOf(chart, ledger, day(10));
    TrialBalance forPeriod = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_EQ(asOf.currency(), eur);
    EXPECT_EQ(forPeriod.currency(), eur);
}

TEST(TrialBalanceTest, PeriodTrialBalanceLinesAreOrderedByAscendingAccountCode) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    chart.addRootAccount(AccountCode("2000"), "Liability", AccountType::Liability);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    ASSERT_EQ(tb.lines().size(), 3u);
    EXPECT_EQ(tb.lines()[0].accountCode().value(), "1000");
    EXPECT_EQ(tb.lines()[1].accountCode().value(), "2000");
    EXPECT_EQ(tb.lines()[2].accountCode().value(), "4000");
}

// ---------------------------------------------------------------------
// Ledger immutability / snapshot independence
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, GenerateAsOfAndGenerateForPeriodNeverMutateLedger) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "Sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    const Money cashBefore = ledger.balance(cash.id());
    const std::size_t historyBefore = ledger.postedEntries().size();

    TrialBalance::generateAsOf(chart, ledger, day(10));
    TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_EQ(ledger.balance(cash.id()), cashBefore);
    EXPECT_EQ(ledger.postedEntries().size(), historyBefore);
}

TEST(TrialBalanceTest, PeriodSnapshotIsIndependentOfLaterPosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "April sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    const Period april(day(1), day(10));
    TrialBalance aprilSnapshot = TrialBalance::generateForPeriod(chart, ledger, april);

    post(JournalEntry::create(day(15), "May sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(999, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(999, 0, usd)),
                               }),
         chart, ledger);

    EXPECT_EQ(findLine(aprilSnapshot, cash.id()).debit(), Money::fromMajorUnits(100, 0, usd));

    TrialBalance freshApril = TrialBalance::generateForPeriod(chart, ledger, april);
    EXPECT_EQ(findLine(freshApril, cash.id()).debit(), Money::fromMajorUnits(100, 0, usd));
}

TEST(TrialBalanceTest, AsOfSnapshotIsIndependentOfLaterPosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "Sale before cutoff",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance snapshot = TrialBalance::generateAsOf(chart, ledger, day(10));

    post(JournalEntry::create(day(15), "Sale after snapshot",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);

    EXPECT_EQ(findLine(snapshot, cash.id()).debit(), Money::fromMajorUnits(200, 0, usd));
    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(700, 0, usd));
}

// ---------------------------------------------------------------------
// Whole JournalEntry inclusion/exclusion
// ---------------------------------------------------------------------

TEST(TrialBalanceTest, WholeJournalEntryIncludedOrExcludedAsOneUnit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& ar = chart.addRootAccount(AccountCode("1100"), "Accounts Receivable", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    // A multi-line entry INSIDE the period: all lines must be applied
    // together.
    post(JournalEntry::create(day(5), "Split sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(60, 0, usd)),
                                   JournalEntryLine::debit(ar.id(), Money::fromMajorUnits(40, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    // The same shape, but OUTSIDE the period: none of its lines may
    // count.
    post(JournalEntry::create(day(50), "Split sale outside period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(600, 0, usd)),
                                   JournalEntryLine::debit(ar.id(), Money::fromMajorUnits(400, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1000, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generateForPeriod(chart, ledger, Period(day(1), day(10)));

    EXPECT_EQ(findLine(tb, cash.id()).debit(), Money::fromMajorUnits(60, 0, usd));
    EXPECT_EQ(findLine(tb, ar.id()).debit(), Money::fromMajorUnits(40, 0, usd));
    EXPECT_EQ(findLine(tb, revenue.id()).credit(), Money::fromMajorUnits(100, 0, usd));
    // If a partial inclusion had happened, debit would not equal credit.
    EXPECT_EQ(tb.totalDebits(), tb.totalCredits());
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(TrialBalancePropertyTest, AsOfTrialBalanceEqualsIndependentReplayThroughCutoff) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(day(1), "E1",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(20), "E2, dated after cutoff",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(20, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(20, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(5), "E3, posted after E2 but dated before it",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    const auto cutoff = day(10);

    Ledger independentReplay(usd);
    for (const PostedJournalEntry& posted : ledger.postedEntries()) {
        if (posted.entry().date() < cutoff) {
            post(posted.entry(), chart, independentReplay);
        }
    }
    TrialBalance expected = TrialBalance::generate(chart, independentReplay);
    TrialBalance actual = TrialBalance::generateAsOf(chart, ledger, cutoff);

    ASSERT_EQ(expected.lines().size(), actual.lines().size());
    for (std::size_t i = 0; i < expected.lines().size(); ++i) {
        EXPECT_EQ(expected.lines()[i].accountId(), actual.lines()[i].accountId());
        EXPECT_EQ(expected.lines()[i].debit(), actual.lines()[i].debit());
        EXPECT_EQ(expected.lines()[i].credit(), actual.lines()[i].credit());
    }
}

TEST(TrialBalancePropertyTest, PeriodActivityEqualsIndependentReplayInsidePeriod) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(day(0), "Before period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(30), "After period, posted before the in-period entry",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(80, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(80, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(5), "Inside period",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(60, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(60, 0, usd)),
                               }),
         chart, ledger);

    const Period period(day(1), day(10));

    Ledger independentReplay(usd);
    for (const PostedJournalEntry& posted : ledger.postedEntries()) {
        if (period.contains(posted.entry().date())) {
            post(posted.entry(), chart, independentReplay);
        }
    }
    TrialBalance expected = TrialBalance::generate(chart, independentReplay);
    TrialBalance actual = TrialBalance::generateForPeriod(chart, ledger, period);

    ASSERT_EQ(expected.lines().size(), actual.lines().size());
    for (std::size_t i = 0; i < expected.lines().size(); ++i) {
        EXPECT_EQ(expected.lines()[i].debit(), actual.lines()[i].debit());
        EXPECT_EQ(expected.lines()[i].credit(), actual.lines()[i].credit());
    }
}

TEST(TrialBalancePropertyTest, AdjacentPeriodsDoNotDoubleCountEntries) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(day(5), "April",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(10), "Exactly on the shared boundary",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(30, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(30, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(15), "May",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    const Period april(day(1), day(10));
    const Period may(day(10), day(20));

    TrialBalance aprilTb = TrialBalance::generateForPeriod(chart, ledger, april);
    TrialBalance mayTb = TrialBalance::generateForPeriod(chart, ledger, may);

    EXPECT_EQ(findLine(aprilTb, cash.id()).debit(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(findLine(mayTb, cash.id()).debit(), Money::fromMajorUnits(230, 0, usd));

    // The combined total across both non-overlapping periods equals the
    // cumulative total -- nothing counted twice, nothing dropped.
    TrialBalance cumulative = TrialBalance::generate(chart, ledger);
    const Money combined = findLine(aprilTb, cash.id()).debit() + findLine(mayTb, cash.id()).debit();
    EXPECT_EQ(combined, findLine(cumulative, cash.id()).debit());
}

TEST(TrialBalancePropertyTest, RandomBalancedPostingSequencesRespectPeriodBoundaries) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    std::mt19937_64 rng(777);
    std::uniform_int_distribution<int> dateDist(0, 40);
    std::uniform_int_distribution<std::int64_t> amountDist(1, 10000);
    std::uniform_int_distribution<int> sideDist(0, 1);

    const Period period(day(10), day(30));

    for (int trial = 0; trial < 60; ++trial) {
        const auto date = day(dateDist(rng));
        const Money amount = Money::ofMinorUnits(amountDist(rng), usd);
        if (sideDist(rng) == 0) {
            post(JournalEntry::create(date, "Random revenue entry",
                                       {
                                           JournalEntryLine::debit(cash.id(), amount),
                                           JournalEntryLine::credit(revenue.id(), amount),
                                       }),
                 chart, ledger);
        } else {
            post(JournalEntry::create(date, "Random expense entry",
                                       {
                                           JournalEntryLine::credit(cash.id(), amount),
                                           JournalEntryLine::debit(expense.id(), amount),
                                       }),
                 chart, ledger);
        }
    }

    Ledger independentReplay(usd);
    for (const PostedJournalEntry& posted : ledger.postedEntries()) {
        if (period.contains(posted.entry().date())) {
            post(posted.entry(), chart, independentReplay);
        }
    }
    TrialBalance expected = TrialBalance::generate(chart, independentReplay);
    TrialBalance actual = TrialBalance::generateForPeriod(chart, ledger, period);

    ASSERT_EQ(expected.lines().size(), actual.lines().size());
    for (std::size_t i = 0; i < expected.lines().size(); ++i) {
        EXPECT_EQ(expected.lines()[i].debit(), actual.lines()[i].debit());
        EXPECT_EQ(expected.lines()[i].credit(), actual.lines()[i].credit());
    }
    EXPECT_EQ(actual.totalDebits(), actual.totalCredits());
}
