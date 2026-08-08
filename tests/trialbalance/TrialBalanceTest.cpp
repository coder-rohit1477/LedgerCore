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
