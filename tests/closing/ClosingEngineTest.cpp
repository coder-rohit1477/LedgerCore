// Tests for closing::closeTemporaryAccounts(): closing Revenue/Expense
// balances into a retained-earnings Equity account through one genuine,
// balanced JournalEntryKind::Closing entry posted via posting::post().

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "ledgercore/closing/ClosingEngine.h"
#include "ledgercore/closing/ClosingExceptions.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/posting/PostingExceptions.h"
#include "ledgercore/reporting/BalanceSheet.h"
#include "ledgercore/reporting/IncomeStatement.h"
#include "ledgercore/trialbalance/TrialBalance.h"

using ledgercore::closing::closeTemporaryAccounts;
using ledgercore::closing::ClosingResult;
using ledgercore::closing::InvalidRetainedEarningsAccountException;
using ledgercore::closing::NothingToCloseException;
using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::InvalidJournalEntryException;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryKind;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::domain::MoneyOverflowException;
using ledgercore::domain::Period;
using ledgercore::ledger::Ledger;
using ledgercore::ledger::PostedJournalEntry;
using ledgercore::posting::AccountNotFoundException;
using ledgercore::posting::InvalidPostingTargetException;
using ledgercore::posting::post;
using ledgercore::reporting::BalanceSheet;
using ledgercore::reporting::IncomeStatement;
using ledgercore::trialbalance::ClosingEntries;
using ledgercore::trialbalance::TrialBalance;
using TimePoint = std::chrono::system_clock::time_point;

namespace {

TimePoint day(int n) {
    return TimePoint{} + std::chrono::hours(24 * n);
}

Money usd(std::int64_t major) {
    return Money::fromMajorUnits(major, 0, Currency("USD"));
}

// Cash, Payable, Capital, Retained Earnings, an Equity group, two Revenue
// leaves under a Revenue group, two Expense leaves under an Expense group.
struct Books {
    ChartOfAccounts chart;
    Ledger ledger{Currency("USD")};
    AccountId cash{0};
    AccountId payable{0};
    AccountId capital{0};
    AccountId retained{0};
    AccountId equityGroup{0};
    AccountId revenueGroup{0};
    AccountId sales{0};
    AccountId services{0};
    AccountId expenseGroup{0};
    AccountId rent{0};
    AccountId wages{0};

    Books() {
        cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
        payable = chart.addRootAccount(AccountCode("2000"), "Payable", AccountType::Liability).id();
        capital = chart.addRootAccount(AccountCode("3000"), "Capital", AccountType::Equity).id();
        retained = chart.addRootAccount(AccountCode("3100"), "Retained Earnings", AccountType::Equity).id();
        Account& equity = chart.addRootAccount(AccountCode("3900"), "Other Equity", AccountType::Equity);
        ledgercore::posting::addChildAccount(chart, ledger, equity, AccountCode("3910"), "Reserve");
        equityGroup = equity.id();
        Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
        revenueGroup = revenue.id();
        sales = ledgercore::posting::addChildAccount(chart, ledger, revenue, AccountCode("4100"), "Sales").id();
        services =
            ledgercore::posting::addChildAccount(chart, ledger, revenue, AccountCode("4200"), "Services").id();
        Account& expense = chart.addRootAccount(AccountCode("5000"), "Expenses", AccountType::Expense);
        expenseGroup = expense.id();
        rent = ledgercore::posting::addChildAccount(chart, ledger, expense, AccountCode("5100"), "Rent").id();
        wages = ledgercore::posting::addChildAccount(chart, ledger, expense, AccountCode("5200"), "Wages").id();
    }

    void postEntry(TimePoint date, AccountId debitAccount, AccountId creditAccount, const Money& amount) {
        post(JournalEntry::create(date, "Activity",
                                  {JournalEntryLine::debit(debitAccount, amount),
                                   JournalEntryLine::credit(creditAccount, amount)}),
             chart, ledger);
    }

    // Revenue recognised for cash: Dr Cash, Cr <revenue account>.
    void earn(TimePoint date, AccountId revenueAccount, std::int64_t major) {
        postEntry(date, cash, revenueAccount, usd(major));
    }

    // Expense paid in cash: Dr <expense account>, Cr Cash.
    void spend(TimePoint date, AccountId expenseAccount, std::int64_t major) {
        postEntry(date, expenseAccount, cash, usd(major));
    }

    ClosingResult close(TimePoint cutoff) { return closeTemporaryAccounts(chart, ledger, retained, cutoff); }

    Money balance(AccountId id) const { return ledger.balance(id); }
};

const PostedJournalEntry& lastPosted(const Ledger& ledger) {
    return ledger.postedEntries().back();
}

void expectTemporaryBalancesZero(const Books& books) {
    for (AccountId id : {books.sales, books.services, books.rent, books.wages}) {
        EXPECT_TRUE(books.balance(id).isZero()) << id.value();
    }
}

// Snapshot of everything observable about a Ledger, for atomicity checks.
struct LedgerSnapshot {
    std::size_t historySize;
    std::vector<Money> balances;
};

LedgerSnapshot snapshot(const Books& books) {
    std::vector<Money> balances;
    for (AccountId id : {books.cash, books.payable, books.capital, books.retained, books.sales, books.services,
                         books.rent, books.wages}) {
        balances.push_back(books.balance(id));
    }
    return LedgerSnapshot{books.ledger.postedEntries().size(), balances};
}

void expectUnchanged(const Books& books, const LedgerSnapshot& before) {
    const LedgerSnapshot after = snapshot(books);
    EXPECT_EQ(after.historySize, before.historySize);
    ASSERT_EQ(after.balances.size(), before.balances.size());
    for (std::size_t i = 0; i < after.balances.size(); ++i) {
        EXPECT_EQ(after.balances[i], before.balances[i]) << i;
    }
}

const TimePoint kYearEnd = day(365);

} // namespace

// ---------------------------------------------------------------------
// 1-9: what gets closed, and where it goes
// ---------------------------------------------------------------------

TEST(ClosingEngineTest, RevenueOnlyClosingCreditsRetainedEarnings) {
    Books books;
    books.earn(day(10), books.sales, 500);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, usd(500));
    EXPECT_TRUE(books.balance(books.sales).isZero());
    EXPECT_EQ(books.balance(books.retained), usd(500));  // credit-normal: positive == credit balance
    const JournalEntry& entry = lastPosted(books.ledger).entry();
    ASSERT_EQ(entry.lines().size(), 2u);
    EXPECT_EQ(entry.lines()[0].accountId(), books.sales);
    EXPECT_TRUE(entry.lines()[0].isDebit());
    EXPECT_EQ(entry.lines()[1].accountId(), books.retained);
    EXPECT_TRUE(entry.lines()[1].isCredit());
}

TEST(ClosingEngineTest, ExpenseOnlyClosingDebitsRetainedEarnings) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(1000));
    books.spend(day(10), books.rent, 300);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, -usd(300));
    EXPECT_TRUE(books.balance(books.rent).isZero());
    EXPECT_EQ(books.balance(books.retained), -usd(300));  // debit (deficit) balance on a credit-normal account
    const JournalEntry& entry = lastPosted(books.ledger).entry();
    ASSERT_EQ(entry.lines().size(), 2u);
    EXPECT_EQ(entry.lines()[0].accountId(), books.rent);
    EXPECT_TRUE(entry.lines()[0].isCredit());
    EXPECT_EQ(entry.lines()[1].accountId(), books.retained);
    EXPECT_TRUE(entry.lines()[1].isDebit());
}

TEST(ClosingEngineTest, RevenueAndExpenseProducingNetIncome) {
    Books books;
    books.earn(day(10), books.sales, 800);
    books.spend(day(20), books.rent, 300);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, usd(500));
    EXPECT_EQ(books.balance(books.retained), usd(500));
    expectTemporaryBalancesZero(books);
}

TEST(ClosingEngineTest, RevenueAndExpenseProducingNetLoss) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(1000));
    books.earn(day(10), books.sales, 200);
    books.spend(day(20), books.wages, 450);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, -usd(250));
    EXPECT_EQ(books.balance(books.retained), -usd(250));
    expectTemporaryBalancesZero(books);
}

TEST(ClosingEngineTest, MultipleRevenueAccountsAreAllClosed) {
    Books books;
    books.earn(day(10), books.sales, 700);
    books.earn(day(11), books.services, 125);
    books.earn(day(12), books.sales, 75);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, usd(900));
    EXPECT_EQ(books.balance(books.retained), usd(900));
    expectTemporaryBalancesZero(books);
    // One line per account (aggregated balance), plus retained earnings.
    EXPECT_EQ(lastPosted(books.ledger).entry().lines().size(), 3u);
}

TEST(ClosingEngineTest, MultipleExpenseAccountsAreAllClosed) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(5000));
    books.spend(day(10), books.rent, 1200);
    books.spend(day(11), books.wages, 900);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, -usd(2100));
    EXPECT_EQ(books.balance(books.retained), -usd(2100));
    expectTemporaryBalancesZero(books);
    EXPECT_EQ(lastPosted(books.ledger).entry().lines().size(), 3u);
}

TEST(ClosingEngineTest, ZeroBalanceTemporaryAccountsGetNoClosingLine) {
    Books books;
    books.earn(day(10), books.sales, 400);
    // Services and Wages both had activity that nets to exactly zero.
    books.earn(day(11), books.services, 50);
    books.postEntry(day(12), books.services, books.cash, usd(50));
    books.spend(day(13), books.wages, 20);
    books.postEntry(day(14), books.cash, books.wages, usd(20));

    books.close(kYearEnd);

    const JournalEntry& entry = lastPosted(books.ledger).entry();
    ASSERT_EQ(entry.lines().size(), 2u);
    EXPECT_EQ(entry.lines()[0].accountId(), books.sales);
    EXPECT_EQ(entry.lines()[1].accountId(), books.retained);
}

TEST(ClosingEngineTest, RetainedEarningsReceivesExactNetEffectOnTopOfPriorBalance) {
    Books books;
    books.postEntry(day(1), books.cash, books.retained, usd(10000));  // opening retained earnings
    books.earn(day(10), books.sales, 1234);
    books.spend(day(11), books.rent, 234);

    books.close(kYearEnd);

    EXPECT_EQ(books.balance(books.retained), usd(11000));
}

TEST(ClosingEngineTest, AllTemporaryAccountsAreZeroAfterClosingAndPermanentAccountsUntouched) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(2000));
    books.postEntry(day(2), books.cash, books.payable, usd(300));
    books.earn(day(10), books.sales, 900);
    books.earn(day(11), books.services, 100);
    books.spend(day(12), books.rent, 400);
    books.spend(day(13), books.wages, 250);
    const Money cashBefore = books.balance(books.cash);
    const Money payableBefore = books.balance(books.payable);
    const Money capitalBefore = books.balance(books.capital);

    books.close(kYearEnd);

    expectTemporaryBalancesZero(books);
    EXPECT_EQ(books.balance(books.cash), cashBefore);
    EXPECT_EQ(books.balance(books.payable), payableBefore);
    EXPECT_EQ(books.balance(books.capital), capitalBefore);
    EXPECT_EQ(books.balance(books.retained), usd(350));
}

TEST(ClosingEngineTest, ContraBalancesOnTemporaryAccountsAreClosedOnTheirOwnColumn) {
    // Sales with a net *debit* balance (returns exceeding sales) and Rent
    // with a net *credit* balance (a refund exceeding the charge).
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(1000));
    books.earn(day(10), books.sales, 100);
    books.postEntry(day(11), books.sales, books.cash, usd(160));  // returns: Dr Sales 160
    books.spend(day(12), books.rent, 50);
    books.postEntry(day(13), books.cash, books.rent, usd(80));  // refund: Cr Rent 80

    const ClosingResult result = books.close(kYearEnd);

    // Revenue -60, expenses -30 => net income -30.
    EXPECT_EQ(result.netIncome, -usd(30));
    expectTemporaryBalancesZero(books);
    EXPECT_EQ(books.balance(books.retained), -usd(30));
    const JournalEntry& entry = lastPosted(books.ledger).entry();
    EXPECT_TRUE(entry.lines()[0].isCredit());  // Sales debit balance is credited
    EXPECT_TRUE(entry.lines()[1].isDebit());   // Rent credit balance is debited
}

TEST(ClosingEngineTest, OffsettingRevenueAndExpenseNeedNoRetainedEarningsLine) {
    Books books;
    books.earn(day(10), books.sales, 300);
    books.spend(day(11), books.rent, 300);

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_TRUE(result.netIncome.isZero());
    EXPECT_TRUE(books.balance(books.retained).isZero());
    expectTemporaryBalancesZero(books);
    const JournalEntry& entry = lastPosted(books.ledger).entry();
    ASSERT_EQ(entry.lines().size(), 2u);
    for (const JournalEntryLine& line : entry.lines()) {
        EXPECT_NE(line.accountId(), books.retained);
    }
}

TEST(ClosingEngineTest, ClosingEntryIsAClosingKindEntryWithDeterministicLineOrder) {
    Books books;
    books.spend(day(5), books.wages, 40);
    books.earn(day(6), books.services, 90);
    books.spend(day(7), books.rent, 10);
    books.earn(day(8), books.sales, 60);

    const ClosingResult result = books.close(kYearEnd);

    const PostedJournalEntry& posted = lastPosted(books.ledger);
    EXPECT_EQ(posted.id(), result.postingId);
    EXPECT_EQ(posted.entry().kind(), JournalEntryKind::Closing);
    EXPECT_EQ(posted.entry().description(), "Closing entry");
    // Ascending AccountCode (4100, 4200, 5100, 5200), retained earnings last.
    const std::vector<AccountId> expectedOrder = {books.sales, books.services, books.rent, books.wages,
                                                  books.retained};
    ASSERT_EQ(posted.entry().lines().size(), expectedOrder.size());
    for (std::size_t i = 0; i < expectedOrder.size(); ++i) {
        EXPECT_EQ(posted.entry().lines()[i].accountId(), expectedOrder[i]) << i;
    }
    EXPECT_EQ(posted.entry().totalDebits(), posted.entry().totalCredits());
}

TEST(ClosingEngineTest, CustomDescriptionIsUsed) {
    Books books;
    books.earn(day(10), books.sales, 10);
    closeTemporaryAccounts(books.chart, books.ledger, books.retained, kYearEnd, "FY2026 close");
    EXPECT_EQ(lastPosted(books.ledger).entry().description(), "FY2026 close");
}

// ---------------------------------------------------------------------
// 10-16: invalid targets and currency, all rejected before mutation
// ---------------------------------------------------------------------

TEST(ClosingEngineTest, AssetTargetRejected) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.cash, kYearEnd),
                 InvalidRetainedEarningsAccountException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, LiabilityTargetRejected) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.payable, kYearEnd),
                 InvalidRetainedEarningsAccountException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, RevenueTargetRejected) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.services, kYearEnd),
                 InvalidRetainedEarningsAccountException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, ExpenseTargetRejected) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.rent, kYearEnd),
                 InvalidRetainedEarningsAccountException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, NonExistentTargetRejected) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, AccountId(9999), kYearEnd),
                 AccountNotFoundException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, GroupAccountTargetRejectedEvenWhenEquity) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.equityGroup, kYearEnd),
                 InvalidPostingTargetException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, ClosingIsDenominatedInTheLedgerCurrency) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("EUR"));
    const Money eur100 = Money::fromMajorUnits(100, 0, Currency("EUR"));
    const AccountId cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
    const AccountId sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
    const AccountId retained = chart.addRootAccount(AccountCode("3100"), "RE", AccountType::Equity).id();
    post(JournalEntry::create(day(1), "Sale",
                              {JournalEntryLine::debit(cash, eur100), JournalEntryLine::credit(sales, eur100)}),
         chart, ledger);

    const ClosingResult result = closeTemporaryAccounts(chart, ledger, retained, kYearEnd);

    EXPECT_EQ(result.netIncome, eur100);
    EXPECT_EQ(lastPosted(ledger).entry().currency(), Currency("EUR"));
    EXPECT_EQ(ledger.balance(retained), eur100);
}

// ---------------------------------------------------------------------
// 17-18: error paths leave the Ledger and its history untouched
// ---------------------------------------------------------------------

TEST(ClosingEngineTest, OverflowWhileComputingBalancesLeavesLedgerUnchanged) {
    // Sales (credit) and Wages (debit) are each at INT64_MAX minor units;
    // the as-of trial balance's debit column then overflows.
    Books books;
    const Money max = Money::ofMinorUnits(std::numeric_limits<std::int64_t>::max(), Currency("USD"));
    books.postEntry(day(1), books.cash, books.sales, max);
    books.postEntry(day(2), books.wages, books.payable, max);
    const LedgerSnapshot before = snapshot(books);

    EXPECT_THROW(books.close(kYearEnd), MoneyOverflowException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, EmptyDescriptionIsRejectedWithoutPosting) {
    Books books;
    books.earn(day(10), books.sales, 100);
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.retained, kYearEnd, ""),
                 InvalidJournalEntryException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, FailedClosingLeavesJournalHistoryUnchanged) {
    Books books;
    books.earn(day(10), books.sales, 100);
    books.spend(day(11), books.rent, 40);
    const std::vector<PostedJournalEntry> historyBefore = books.ledger.postedEntries();

    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.cash, kYearEnd),
                 InvalidRetainedEarningsAccountException);
    EXPECT_THROW(closeTemporaryAccounts(books.chart, books.ledger, books.retained, day(1)), NothingToCloseException);

    const std::vector<PostedJournalEntry>& historyAfter = books.ledger.postedEntries();
    ASSERT_EQ(historyAfter.size(), historyBefore.size());
    for (std::size_t i = 0; i < historyAfter.size(); ++i) {
        EXPECT_EQ(historyAfter[i].id(), historyBefore[i].id());
        EXPECT_EQ(historyAfter[i].entry().kind(), JournalEntryKind::Standard);
    }
}

// ---------------------------------------------------------------------
// 19: repeated closing
// ---------------------------------------------------------------------

TEST(ClosingEngineTest, NothingToCloseWhenNoTemporaryActivity) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(100));
    const LedgerSnapshot before = snapshot(books);
    EXPECT_THROW(books.close(kYearEnd), NothingToCloseException);
    expectUnchanged(books, before);
}

TEST(ClosingEngineTest, ClosingTheSameCutoffTwiceIsRejectedAndChangesNothing) {
    Books books;
    books.earn(day(10), books.sales, 500);
    books.close(kYearEnd);
    const LedgerSnapshot afterFirstClose = snapshot(books);

    // The first closing entry is dated before the cutoff, so the second
    // close sees zero temporary balances: deterministic rejection, no
    // second entry, no hidden "closed" flag involved.
    EXPECT_THROW(books.close(kYearEnd), NothingToCloseException);
    expectUnchanged(books, afterFirstClose);
    EXPECT_EQ(books.balance(books.retained), usd(500));
}

TEST(ClosingEngineTest, BackdatedActivityAfterClosingIsClosedByAReClose) {
    Books books;
    books.earn(day(10), books.sales, 500);
    books.close(kYearEnd);
    books.earn(day(200), books.services, 40);  // late, backdated adjustment

    const ClosingResult second = books.close(kYearEnd);

    EXPECT_EQ(second.netIncome, usd(40));
    EXPECT_EQ(books.balance(books.retained), usd(540));
    expectTemporaryBalancesZero(books);
    // The second closing entry closes only the residual.
    const JournalEntry& entry = lastPosted(books.ledger).entry();
    ASSERT_EQ(entry.lines().size(), 2u);
    EXPECT_EQ(entry.lines()[0].accountId(), books.services);
}

// ---------------------------------------------------------------------
// 20: as-of / period semantics
// ---------------------------------------------------------------------

TEST(ClosingEngineTest, OnlyActivityBeforeTheCutoffIsClosed) {
    Books books;
    books.earn(day(100), books.sales, 300);
    books.earn(kYearEnd, books.sales, 70);           // exactly at the cutoff: next period
    books.earn(day(400), books.services, 20);        // after the cutoff

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, usd(300));
    EXPECT_EQ(books.balance(books.sales), usd(70));
    EXPECT_EQ(books.balance(books.services), usd(20));
    EXPECT_EQ(books.balance(books.retained), usd(300));
}

TEST(ClosingEngineTest, ClosingEntryIsDatedOneMicrosecondBeforeTheCutoffOnEveryPlatform) {
    Books books;
    books.earn(day(100), books.sales, 300);
    books.close(kYearEnd);
    const JournalEntry& entry = lastPosted(books.ledger).entry();
    EXPECT_EQ(entry.date(), kYearEnd - std::chrono::microseconds(1));
    EXPECT_EQ(entry.closingCutoff(), kYearEnd);
}

TEST(ClosingEngineTest, ActivityInTheFinalMicrosecondBeforeTheCutoffIsClosed) {
    // The closing entry's own date (cutoff - 1us) is inside the closed
    // range, and so is anything dated at or after it but before cutoff.
    Books books;
    books.earn(kYearEnd - std::chrono::microseconds(1), books.sales, 25);
    const ClosingResult result = books.close(kYearEnd);
    EXPECT_EQ(result.netIncome, usd(25));
    EXPECT_TRUE(books.balance(books.sales).isZero());
}

TEST(ClosingEngineTest, AsOfCutoffBecomesAPostClosingTrialBalance) {
    Books books;
    books.earn(day(100), books.sales, 300);
    books.spend(day(101), books.rent, 100);
    const TrialBalance preClosing = TrialBalance::generateAsOf(books.chart, books.ledger, kYearEnd);
    const TrialBalance earlierBeforeClose = TrialBalance::generateAsOf(books.chart, books.ledger, day(101));

    books.close(kYearEnd);

    const TrialBalance postClosing = TrialBalance::generateAsOf(books.chart, books.ledger, kYearEnd);
    for (const auto& line : postClosing.lines()) {
        if (line.accountType() == AccountType::Revenue || line.accountType() == AccountType::Expense) {
            EXPECT_TRUE(line.debit().isZero() && line.credit().isZero()) << line.accountCode().value();
        }
        if (line.accountId() == books.retained) {
            EXPECT_EQ(line.credit(), usd(200));
        }
    }
    EXPECT_EQ(postClosing.totalDebits(), postClosing.totalCredits());
    EXPECT_EQ(preClosing.totalDebits(), usd(300));  // Cash 200 Dr + Rent 100 Dr
    // An earlier as-of view (only the sale) is unaffected by the closing entry.
    const TrialBalance earlier = TrialBalance::generateAsOf(books.chart, books.ledger, day(101));
    ASSERT_EQ(earlier.lines().size(), earlierBeforeClose.lines().size());
    for (std::size_t i = 0; i < earlier.lines().size(); ++i) {
        EXPECT_EQ(earlier.lines()[i].debit(), earlierBeforeClose.lines()[i].debit());
        EXPECT_EQ(earlier.lines()[i].credit(), earlierBeforeClose.lines()[i].credit());
    }
    EXPECT_EQ(earlier.totalDebits(), usd(300));
}

TEST(ClosingEngineTest, PeriodIncomeStatementExcludingClosingEntriesIsUnchangedByClosing) {
    Books books;
    books.earn(day(100), books.sales, 900);
    books.spend(day(150), books.wages, 350);
    const Period year(day(0), kYearEnd);
    const Money netIncomeBefore =
        IncomeStatement::generate(TrialBalance::generateForPeriod(books.chart, books.ledger, year,
                                                                  ClosingEntries::Exclude))
            .netIncome();

    books.close(kYearEnd);

    const IncomeStatement afterExcluding = IncomeStatement::generate(
        TrialBalance::generateForPeriod(books.chart, books.ledger, year, ClosingEntries::Exclude));
    EXPECT_EQ(afterExcluding.netIncome(), netIncomeBefore);
    EXPECT_EQ(afterExcluding.netIncome(), usd(550));
    // Including closing entries the period's temporary activity nets to
    // zero -- exactly why income statements exclude them.
    const IncomeStatement afterIncluding =
        IncomeStatement::generate(TrialBalance::generateForPeriod(books.chart, books.ledger, year));
    EXPECT_TRUE(afterIncluding.netIncome().isZero());
}

// ---------------------------------------------------------------------
// 22-25: reports and invariants after closing
// ---------------------------------------------------------------------

TEST(ClosingEngineTest, ClosedNetIncomeMatchesTheIncomeStatementOfTheSameCutoff) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(5000));
    books.earn(day(10), books.sales, 1800);
    books.earn(day(20), books.services, 450);
    books.spend(day(30), books.rent, 900);
    books.spend(day(40), books.wages, 1100);
    const Money expected =
        IncomeStatement::generate(TrialBalance::generateAsOf(books.chart, books.ledger, kYearEnd)).netIncome();

    const ClosingResult result = books.close(kYearEnd);

    EXPECT_EQ(result.netIncome, expected);
    EXPECT_EQ(result.netIncome, usd(250));
}

TEST(ClosingEngineTest, AccountingEquationHoldsWithNoUnclosedIncomeAfterClosing) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(3000));
    books.postEntry(day(2), books.cash, books.payable, usd(700));
    books.earn(day(10), books.sales, 1500);
    books.spend(day(20), books.rent, 600);

    books.close(kYearEnd);

    const TrialBalance tb = TrialBalance::generate(books.chart, books.ledger);
    const BalanceSheet bs = BalanceSheet::generate(tb);
    const IncomeStatement unclosed = IncomeStatement::generate(tb);
    EXPECT_TRUE(unclosed.netIncome().isZero());
    EXPECT_EQ(bs.assets().total(), bs.liabilities().total() + bs.equity().total() + unclosed.netIncome());
    EXPECT_EQ(bs.assets().total(), usd(4600));
    EXPECT_EQ(bs.equity().total(), usd(3900));
    // The lifetime income statement (closing entries excluded) still
    // reports the year's result.
    EXPECT_EQ(IncomeStatement::generate(TrialBalance::generate(books.chart, books.ledger, ClosingEntries::Exclude))
                  .netIncome(),
              usd(900));
}

TEST(ClosingEngineTest, TrialBalanceStaysBalancedInEveryViewAfterClosing) {
    Books books;
    books.postEntry(day(1), books.cash, books.capital, usd(1000));
    books.earn(day(10), books.sales, 640);
    books.spend(day(11), books.wages, 220);
    books.close(kYearEnd);
    books.earn(day(400), books.services, 75);

    for (ClosingEntries mode : {ClosingEntries::Include, ClosingEntries::Exclude}) {
        const TrialBalance cumulative = TrialBalance::generate(books.chart, books.ledger, mode);
        const TrialBalance asOf = TrialBalance::generateAsOf(books.chart, books.ledger, kYearEnd, mode);
        const TrialBalance period =
            TrialBalance::generateForPeriod(books.chart, books.ledger, Period(day(0), day(500)), mode);
        EXPECT_EQ(cumulative.totalDebits(), cumulative.totalCredits());
        EXPECT_EQ(asOf.totalDebits(), asOf.totalCredits());
        EXPECT_EQ(period.totalDebits(), period.totalCredits());
    }
    // Cumulative with closing entries included matches the Ledger cache.
    const TrialBalance fromCache = TrialBalance::generate(books.chart, books.ledger);
    const TrialBalance fromReplay = TrialBalance::generateAsOf(books.chart, books.ledger, day(1000));
    EXPECT_EQ(fromCache.totalDebits(), fromReplay.totalDebits());
}

TEST(ClosingEngineTest, MultipleIndependentYearsCloseOnlyTheirOwnResults) {
    Books books;
    const TimePoint year2End = day(730);
    books.earn(day(100), books.sales, 1000);
    books.spend(day(200), books.rent, 400);
    const ClosingResult year1 = books.close(kYearEnd);

    books.earn(day(400), books.services, 300);
    books.spend(day(500), books.wages, 500);
    const ClosingResult year2 = books.close(year2End);

    EXPECT_EQ(year1.netIncome, usd(600));
    EXPECT_EQ(year2.netIncome, -usd(200));
    EXPECT_EQ(books.balance(books.retained), usd(400));
    expectTemporaryBalancesZero(books);
    // Each year's income statement (closing entries excluded) is intact.
    EXPECT_EQ(IncomeStatement::generate(TrialBalance::generateForPeriod(books.chart, books.ledger,
                                                                        Period(day(0), kYearEnd),
                                                                        ClosingEntries::Exclude))
                  .netIncome(),
              usd(600));
    EXPECT_EQ(IncomeStatement::generate(TrialBalance::generateForPeriod(books.chart, books.ledger,
                                                                        Period(kYearEnd, year2End),
                                                                        ClosingEntries::Exclude))
                  .netIncome(),
              -usd(200));
}

TEST(ClosingEngineTest, IndependentLedgersCloseIndependently) {
    Books first;
    Books second;
    first.earn(day(10), first.sales, 100);
    second.spend(day(10), second.rent, 70);

    EXPECT_EQ(first.close(kYearEnd).netIncome, usd(100));
    EXPECT_EQ(second.close(kYearEnd).netIncome, -usd(70));
    EXPECT_EQ(first.balance(first.retained), usd(100));
    EXPECT_EQ(second.balance(second.retained), -usd(70));
}
