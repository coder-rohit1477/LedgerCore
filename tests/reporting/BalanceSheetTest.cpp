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
#include "ledgercore/reporting/BalanceSheet.h"
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
using ledgercore::reporting::BalanceSheet;
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

TEST(BalanceSheetTest, AssetsSectionPopulatedCorrectly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Owner investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1000, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(1000, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.assets().lines().size(), 1u);
    EXPECT_EQ(bs.assets().lines().front().accountId(), accounts.cash);
    EXPECT_EQ(bs.assets().lines().front().amount(), Money::fromMajorUnits(1000, 0, usd));
    EXPECT_EQ(bs.assets().total(), Money::fromMajorUnits(1000, 0, usd));
    EXPECT_EQ(bs.assets().label(), "Assets");
}

TEST(BalanceSheetTest, LiabilitiesSectionPopulatedCorrectly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Loan",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(accounts.payable, Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.liabilities().lines().size(), 1u);
    EXPECT_EQ(bs.liabilities().lines().front().amount(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(bs.liabilities().total(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(bs.liabilities().label(), "Liabilities");
}

TEST(BalanceSheetTest, EquitySectionPopulatedCorrectly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Owner investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(2000, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(2000, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.equity().lines().size(), 1u);
    EXPECT_EQ(bs.equity().total(), Money::fromMajorUnits(2000, 0, usd));
    EXPECT_EQ(bs.equity().label(), "Equity");
}

TEST(BalanceSheetTest, MultipleAccountsPerSection) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& ar = chart.addRootAccount(AccountCode("1100"), "Accounts Receivable", AccountType::Asset);
    Account& payable = chart.addRootAccount(AccountCode("2000"), "Accounts Payable", AccountType::Liability);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Setup",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::debit(ar.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(payable.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.assets().lines().size(), 2u);
    EXPECT_EQ(bs.assets().total(), Money::fromMajorUnits(500, 0, usd));
}

// ---------------------------------------------------------------------
// Negative / zero balances
// ---------------------------------------------------------------------

TEST(BalanceSheetTest, NegativeBalancePreservedAsSignedValue) {
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
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.assets().lines().size(), 1u);
    EXPECT_TRUE(bs.assets().lines().front().amount().isNegative());
    EXPECT_EQ(bs.assets().lines().front().amount(), Money::fromMajorUnits(-200, 0, usd));
    EXPECT_EQ(bs.assets().total(), Money::fromMajorUnits(-200, 0, usd));
}

TEST(BalanceSheetTest, ZeroBalanceAccountIsIncluded) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.assets().lines().size(), 1u);
    EXPECT_TRUE(bs.assets().lines().front().amount().isZero());
    EXPECT_TRUE(bs.assets().total().isZero());
}

// ---------------------------------------------------------------------
// Exclusions
// ---------------------------------------------------------------------

TEST(BalanceSheetTest, RevenueAndExpenseAccountsAreExcluded) {
    // setUpStandardChart's Liability/Equity accounts are never posted to
    // here, so they still appear in their own sections at zero balance
    // (already-tested, expected behavior) -- what this test verifies is
    // that Revenue/Expense never leak into any Balance Sheet section,
    // not that the other sections are empty.
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
    BalanceSheet bs = BalanceSheet::generate(tb);

    for (const auto& line : bs.assets().lines()) {
        EXPECT_NE(line.accountId(), accounts.revenue);
        EXPECT_NE(line.accountId(), accounts.expense);
    }
    for (const auto& line : bs.liabilities().lines()) {
        EXPECT_NE(line.accountId(), accounts.revenue);
        EXPECT_NE(line.accountId(), accounts.expense);
    }
    for (const auto& line : bs.equity().lines()) {
        EXPECT_NE(line.accountId(), accounts.revenue);
        EXPECT_NE(line.accountId(), accounts.expense);
    }

    ASSERT_EQ(bs.assets().lines().size(), 1u);
    EXPECT_EQ(bs.assets().lines().front().accountId(), accounts.cash);
    ASSERT_EQ(bs.liabilities().lines().size(), 1u);
    EXPECT_EQ(bs.liabilities().lines().front().accountId(), accounts.payable);
    ASSERT_EQ(bs.equity().lines().size(), 1u);
    EXPECT_EQ(bs.equity().lines().front().accountId(), accounts.equity);
}

TEST(BalanceSheetTest, GroupAccountsAreExcluded) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Equity", AccountType::Equity);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Seed",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(equity.id(), Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.assets().lines().size(), 1u);
    EXPECT_EQ(bs.assets().lines().front().accountId(), cash.id());
    EXPECT_NE(bs.assets().lines().front().accountId(), assets.id());
}

// ---------------------------------------------------------------------
// Currency / ordering / snapshot
// ---------------------------------------------------------------------

TEST(BalanceSheetTest, CurrencyMatchesTrialBalance) {
    Currency eur("EUR");
    ChartOfAccounts chart;
    Ledger ledger(eur);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    EXPECT_EQ(bs.currency(), eur);
}

TEST(BalanceSheetTest, LinesAreOrderedByAscendingAccountCode) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1200"), "Accounts Receivable", AccountType::Asset);
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    chart.addRootAccount(AccountCode("1100"), "Inventory", AccountType::Asset);
    Ledger ledger(usd);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);

    ASSERT_EQ(bs.assets().lines().size(), 3u);
    EXPECT_EQ(bs.assets().lines()[0].accountCode().value(), "1000");
    EXPECT_EQ(bs.assets().lines()[1].accountCode().value(), "1100");
    EXPECT_EQ(bs.assets().lines()[2].accountCode().value(), "1200");
}

TEST(BalanceSheetTest, SnapshotIsIndependentOfSubsequentPosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet snapshot = BalanceSheet::generate(tb);

    post(JournalEntry::create(testDate(), "More investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    EXPECT_EQ(snapshot.assets().total(), Money::fromMajorUnits(100, 0, usd));

    TrialBalance freshTb = TrialBalance::generate(chart, ledger);
    BalanceSheet fresh = BalanceSheet::generate(freshTb);
    EXPECT_EQ(fresh.assets().total(), Money::fromMajorUnits(150, 0, usd));
}

// ---------------------------------------------------------------------
// Accounting equation
// ---------------------------------------------------------------------

TEST(BalanceSheetTest, AccountingEquationHoldsWithRevenueAndExpenseActivity) {
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
    post(JournalEntry::create(testDate(), "Loan",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(accounts.payable, Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(300, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Expense",
                               {
                                   JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(120, 0, usd)),
                                   JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(120, 0, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet bs = BalanceSheet::generate(tb);
    IncomeStatement is = IncomeStatement::generate(tb);

    // Assets == Liabilities + Equity + NetIncome
    Money combined = bs.liabilities().total() + bs.equity().total() + is.netIncome();
    EXPECT_EQ(bs.assets().total(), combined);

    EXPECT_EQ(bs.assets().total(), Money::fromMajorUnits(1680, 0, usd));
    EXPECT_EQ(bs.liabilities().total(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(bs.equity().total(), Money::fromMajorUnits(1000, 0, usd));
    EXPECT_EQ(is.netIncome(), Money::fromMajorUnits(180, 0, usd));
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(BalanceSheetPropertyTest, AccountingEquationHoldsAfterRandomBalancedPostings) {
    // Proves Assets == Liabilities + Equity + NetIncome across arbitrary
    // realistic posting sequences -- this replaces the runtime validation
    // the design deliberately does not perform, since a valid
    // TrialBalance mathematically guarantees the identity and there is no
    // honest public API path to violate it.
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    const std::vector<AccountId> allAccounts{accounts.cash, accounts.payable, accounts.equity, accounts.revenue,
                                              accounts.expense};

    std::mt19937_64 rng(2024);
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
        BalanceSheet bs = BalanceSheet::generate(tb);
        IncomeStatement is = IncomeStatement::generate(tb);

        const Money combined = bs.liabilities().total() + bs.equity().total() + is.netIncome();
        EXPECT_EQ(bs.assets().total(), combined) << "trial " << trial;
    }
}

TEST(BalanceSheetPropertyTest, ReportsAreDeterministic) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(777, 77, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(777, 77, usd)),
                               }),
         chart, ledger);

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    BalanceSheet first = BalanceSheet::generate(tb);
    BalanceSheet second = BalanceSheet::generate(tb);

    EXPECT_EQ(first.assets().total(), second.assets().total());
    EXPECT_EQ(first.liabilities().total(), second.liabilities().total());
    EXPECT_EQ(first.equity().total(), second.equity().total());
}

TEST(BalanceSheetPropertyTest, GeneratingNeverMutatesLedger) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    const Money cashBefore = ledger.balance(accounts.cash);
    const std::size_t historyBefore = ledger.postedEntries().size();

    TrialBalance tb = TrialBalance::generate(chart, ledger);
    for (int i = 0; i < 10; ++i) {
        BalanceSheet::generate(tb);
    }

    EXPECT_EQ(ledger.balance(accounts.cash), cashBefore);
    EXPECT_EQ(ledger.postedEntries().size(), historyBefore);
}
