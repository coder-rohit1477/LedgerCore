#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
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
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/reporting/IncomeStatement.h"
#include "ledgercore/trialbalance/TrialBalance.h"

using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::ledger::Ledger;
using ledgercore::posting::post;
using ledgercore::reporting::IncomeStatement;
using ledgercore::trialbalance::TrialBalance;

namespace {

std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}

struct StandardAccounts {
    AccountId cash;
    AccountId payable;
    AccountId equity;
    AccountId revenue;
    AccountId expense;
};

StandardAccounts setUpStandardChart(ChartOfAccounts& chart) {
    const AccountId cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
    const AccountId payable =
        chart.addRootAccount(AccountCode("2000"), "Accounts Payable", AccountType::Liability).id();
    const AccountId equity = chart.addRootAccount(AccountCode("3000"), "Owner's Equity", AccountType::Equity).id();
    const AccountId revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue).id();
    const AccountId expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense).id();
    return StandardAccounts{cash, payable, equity, revenue, expense};
}

} // namespace

// ---------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------

TEST(IncomeStatementTest, RevenueSectionPopulatedCorrectly) {
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

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.revenue().lines().size(), 1u);
    EXPECT_EQ(is.revenue().lines().front().amount(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(is.revenue().total(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(is.revenue().label(), "Revenue");
}

TEST(IncomeStatementTest, ExpensesSectionPopulatedCorrectly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Expense",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(150, 0, usd)),
                                   JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(150, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.expenses().lines().size(), 1u);
    EXPECT_EQ(is.expenses().lines().front().amount(), Money::fromMajorUnits(150, 0, usd));
    EXPECT_EQ(is.expenses().total(), Money::fromMajorUnits(150, 0, usd));
    EXPECT_EQ(is.expenses().label(), "Expenses");
}

TEST(IncomeStatementTest, MultipleRevenueAccounts) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& salesRevenue = chart.addRootAccount(AccountCode("4000"), "Sales Revenue", AccountType::Revenue);
    Account& serviceRevenue = chart.addRootAccount(AccountCode("4100"), "Service Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sales",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::credit(salesRevenue.id(), Money::fromMajorUnits(300, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Services",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(serviceRevenue.id(), Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.revenue().lines().size(), 2u);
    EXPECT_EQ(is.revenue().total(), Money::fromMajorUnits(500, 0, usd));
}

TEST(IncomeStatementTest, MultipleExpenseAccounts) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& rent = chart.addRootAccount(AccountCode("5000"), "Rent Expense", AccountType::Expense);
    Account& utilities = chart.addRootAccount(AccountCode("5100"), "Utilities Expense", AccountType::Expense);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Rent",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(120, 0, usd)),
                                   JournalEntryLine::debit(rent.id(), Money::fromMajorUnits(120, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Utilities",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(80, 0, usd)),
                                   JournalEntryLine::debit(utilities.id(), Money::fromMajorUnits(80, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.expenses().lines().size(), 2u);
    EXPECT_EQ(is.expenses().total(), Money::fromMajorUnits(200, 0, usd));
}

// ---------------------------------------------------------------------
// Negative / zero balances
// ---------------------------------------------------------------------

TEST(IncomeStatementTest, NegativeRevenueBalancePreserved) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    // A refund/reversal exceeding recorded revenue -- debiting Revenue
    // directly, which is unusual but not disallowed by the model.
    post(JournalEntry::create(testDate(), "Refund exceeds recorded sales",
                               {
                                   JournalEntryLine::debit(revenue.id(), Money::fromMajorUnits(150, 0, usd)),
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(150, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.revenue().lines().size(), 1u);
    EXPECT_TRUE(is.revenue().lines().front().amount().isNegative());
    EXPECT_EQ(is.revenue().total(), Money::fromMajorUnits(-150, 0, usd));
}

TEST(IncomeStatementTest, ZeroBalanceAccountIsIncluded) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.expenses().lines().size(), 1u);
    EXPECT_TRUE(is.expenses().lines().front().amount().isZero());
    EXPECT_TRUE(is.expenses().total().isZero());
}

// ---------------------------------------------------------------------
// Net income / net loss
// ---------------------------------------------------------------------

TEST(IncomeStatementTest, NetIncomeWhenRevenueExceedsExpenses) {
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
    post(JournalEntry::create(testDate(), "Expense",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    EXPECT_EQ(is.netIncome(), Money::fromMajorUnits(300, 0, usd));
    EXPECT_TRUE(is.netIncome().isPositive());
}

TEST(IncomeStatementTest, NetLossWhenExpensesExceedRevenue) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Expense",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(400, 0, usd)),
                                   JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(400, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    EXPECT_EQ(is.netIncome(), Money::fromMajorUnits(-300, 0, usd));
    EXPECT_TRUE(is.netIncome().isNegative());
}

// ---------------------------------------------------------------------
// Exclusions
// ---------------------------------------------------------------------

TEST(IncomeStatementTest, AssetLiabilityEquityAccountsAreExcluded) {
    // setUpStandardChart's Revenue/Expense accounts are never posted to
    // here, so they still appear in their own sections at zero balance
    // (already-tested, expected behavior) -- what this test verifies is
    // that Asset/Liability/Equity never leak into either Income
    // Statement section, not that the sections are empty.
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1000, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(1000, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    for (const auto& line : is.revenue().lines()) {
        EXPECT_NE(line.accountId(), accounts.cash);
        EXPECT_NE(line.accountId(), accounts.payable);
        EXPECT_NE(line.accountId(), accounts.equity);
    }
    for (const auto& line : is.expenses().lines()) {
        EXPECT_NE(line.accountId(), accounts.cash);
        EXPECT_NE(line.accountId(), accounts.payable);
        EXPECT_NE(line.accountId(), accounts.equity);
    }

    ASSERT_EQ(is.revenue().lines().size(), 1u);
    EXPECT_EQ(is.revenue().lines().front().accountId(), accounts.revenue);
    ASSERT_EQ(is.expenses().lines().size(), 1u);
    EXPECT_EQ(is.expenses().lines().front().accountId(), accounts.expense);
}

// ---------------------------------------------------------------------
// Currency / ordering / snapshot
// ---------------------------------------------------------------------

TEST(IncomeStatementTest, CurrencyMatchesTrialBalance) {
    Currency eur("EUR");
    ChartOfAccounts chart;
    Ledger ledger(eur);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    EXPECT_EQ(is.currency(), eur);
}

TEST(IncomeStatementTest, LinesAreOrderedByAscendingAccountCode) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("4200"), "Other Revenue", AccountType::Revenue);
    chart.addRootAccount(AccountCode("4000"), "Sales Revenue", AccountType::Revenue);
    chart.addRootAccount(AccountCode("4100"), "Service Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement is = IncomeStatement::generate(tb);

    ASSERT_EQ(is.revenue().lines().size(), 3u);
    EXPECT_EQ(is.revenue().lines()[0].accountCode().value(), "4000");
    EXPECT_EQ(is.revenue().lines()[1].accountCode().value(), "4100");
    EXPECT_EQ(is.revenue().lines()[2].accountCode().value(), "4200");
}

TEST(IncomeStatementTest, SnapshotIsIndependentOfSubsequentPosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement snapshot = IncomeStatement::generate(tb);

    post(JournalEntry::create(testDate(), "More sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    EXPECT_EQ(snapshot.revenue().total(), Money::fromMajorUnits(100, 0, usd));

    TrialBalance freshTb = TrialBalance::generate(chart, ledger);
    IncomeStatement fresh = IncomeStatement::generate(freshTb);
    EXPECT_EQ(fresh.revenue().total(), Money::fromMajorUnits(150, 0, usd));
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(IncomeStatementPropertyTest, NetIncomeEqualsRevenueMinusExpensesAfterRandomPostings) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    const std::vector<AccountId> allAccounts{accounts.cash, accounts.payable, accounts.equity, accounts.revenue,
                                              accounts.expense};

    std::mt19937_64 rng(4242);
    std::uniform_int_distribution<std::size_t> accountDist(0, allAccounts.size() - 1);
    std::uniform_int_distribution<std::int64_t> amountDist(1, 100000);

    for (int trial = 0; trial < 50; ++trial) {
        std::size_t debitIndex = accountDist(rng);
        std::size_t creditIndex = accountDist(rng);
        while (creditIndex == debitIndex) {
            creditIndex = accountDist(rng);
        }
        const Money amount = Money::ofMinorUnits(amountDist(rng), usd);

        post(JournalEntry::create(testDate(), "Random entry " + std::to_string(trial),
                                   {
                                       JournalEntryLine::debit(allAccounts[debitIndex], amount),
                                       JournalEntryLine::credit(allAccounts[creditIndex], amount),
                                   }),
             chart, ledger);

        TrialBalance tb = TrialBalance::generate(chart, ledger);
        IncomeStatement is = IncomeStatement::generate(tb);

        EXPECT_EQ(is.netIncome(), is.revenue().total() - is.expenses().total()) << "trial " << trial;
    }
}

TEST(IncomeStatementPropertyTest, ReportsAreDeterministic) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(321, 45, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(321, 45, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    IncomeStatement first = IncomeStatement::generate(tb);
    IncomeStatement second = IncomeStatement::generate(tb);

    EXPECT_EQ(first.revenue().total(), second.revenue().total());
    EXPECT_EQ(first.expenses().total(), second.expenses().total());
    EXPECT_EQ(first.netIncome(), second.netIncome());
}

TEST(IncomeStatementPropertyTest, GeneratingNeverMutatesLedger) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    const Money cashBefore = ledger.balance(accounts.cash);
    const std::size_t historyBefore = ledger.postedEntries().size();

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    for (int i = 0; i < 10; ++i) {
        IncomeStatement::generate(tb);
    }

    EXPECT_EQ(ledger.balance(accounts.cash), cashBefore);
    EXPECT_EQ(ledger.postedEntries().size(), historyBefore);
}
