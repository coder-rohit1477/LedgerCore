// Tests for journalquery: read-only, deterministic filtering of a Ledger's
// posted journal history.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "ledgercore/closing/ClosingEngine.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/journalquery/JournalQuery.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/posting/PostingEngine.h"

using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryKind;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::domain::Period;
using ledgercore::journalquery::EntryKindFilter;
using ledgercore::journalquery::findJournalEntries;
using ledgercore::journalquery::JournalQuery;
using ledgercore::ledger::Ledger;
using ledgercore::ledger::PostedJournalEntry;
using ledgercore::posting::post;
using TimePoint = std::chrono::system_clock::time_point;

namespace {

TimePoint day(int n) {
    return TimePoint{} + std::chrono::hours(24 * n);
}

Money usd(std::int64_t major, std::int64_t minor = 0) {
    return Money::fromMajorUnits(major, minor, Currency("USD"));
}

// Posting order (PostingId) vs business date:
//   #1 day 1    Invest   Dr Cash 1000      / Cr Capital 1000
//   #2 day 10   Sale     Dr Cash 300       / Cr Sales 300
//   #3 day 10   Rent     Dr Rent 120       / Cr Cash 120        (same date as #2)
//   #4 day 5    Loan     Dr Cash 50        / Cr Payable 50      (backdated)
//   #5 day 365-1us  Closing entry (Dr Sales 300 / Cr Rent 120 / Cr Retained 180)
//   #6 day 400  Sale     Dr Cash 70.05     / Cr Sales 70.05
struct History {
    ChartOfAccounts chart;
    Ledger ledger{Currency("USD")};
    AccountId cash{0};
    AccountId payable{0};
    AccountId capital{0};
    AccountId retained{0};
    AccountId sales{0};
    AccountId rent{0};
    AccountId unused{0};

    History() {
        cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
        payable = chart.addRootAccount(AccountCode("2000"), "Payable", AccountType::Liability).id();
        capital = chart.addRootAccount(AccountCode("3000"), "Capital", AccountType::Equity).id();
        retained = chart.addRootAccount(AccountCode("3100"), "Retained", AccountType::Equity).id();
        sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
        rent = chart.addRootAccount(AccountCode("5000"), "Rent", AccountType::Expense).id();
        unused = chart.addRootAccount(AccountCode("6000"), "Unused", AccountType::Expense).id();
        postPair(day(1), "Invest", cash, capital, usd(1000));
        postPair(day(10), "Sale", cash, sales, usd(300));
        postPair(day(10), "Rent", rent, cash, usd(120));
        postPair(day(5), "Loan", cash, payable, usd(50));
        ledgercore::closing::closeTemporaryAccounts(chart, ledger, retained, day(365));
        postPair(day(400), "Sale", cash, sales, usd(70, 5));
    }

    void postPair(TimePoint date, const std::string& description, AccountId debit, AccountId credit,
                  const Money& amount) {
        post(JournalEntry::create(date, description,
                                  {JournalEntryLine::debit(debit, amount), JournalEntryLine::credit(credit, amount)}),
             chart, ledger);
    }

    std::vector<std::uint64_t> ids(const JournalQuery& query) const {
        std::vector<std::uint64_t> result;
        for (const PostedJournalEntry& posted : findJournalEntries(ledger, query)) {
            result.push_back(posted.id().value());
        }
        return result;
    }
};

using Ids = std::vector<std::uint64_t>;

} // namespace

// ---------------------------------------------------------------------
// Basics and ordering
// ---------------------------------------------------------------------

TEST(JournalQueryTest, EmptyJournalYieldsNoEntries) {
    Ledger ledger(Currency("USD"));
    EXPECT_TRUE(findJournalEntries(ledger, JournalQuery()).empty());
    EXPECT_TRUE(findJournalEntries(ledger, JournalQuery().withKind(EntryKindFilter::ClosingOnly)).empty());
}

TEST(JournalQueryTest, SingleEntryIsReturnedWithAllItsDetails) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("USD"));
    const AccountId cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
    const AccountId sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
    post(JournalEntry::create(day(3), "Only sale",
                              {JournalEntryLine::debit(cash, usd(12, 34)), JournalEntryLine::credit(sales, usd(12, 34))}),
         chart, ledger);

    const std::vector<PostedJournalEntry> result = findJournalEntries(ledger, JournalQuery());

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id().value(), 1u);
    EXPECT_EQ(result[0].entry().date(), day(3));
    EXPECT_EQ(result[0].entry().description(), "Only sale");
    EXPECT_EQ(result[0].entry().kind(), JournalEntryKind::Standard);
    EXPECT_EQ(result[0].entry().currency(), Currency("USD"));
    ASSERT_EQ(result[0].entry().lines().size(), 2u);
    EXPECT_EQ(result[0].entry().lines()[0].accountId(), cash);
    EXPECT_TRUE(result[0].entry().lines()[0].isDebit());
    EXPECT_EQ(result[0].entry().lines()[0].amount().minorUnits(), 1234);
}

TEST(JournalQueryTest, DefaultQueryReturnsEveryEntryInPostingOrder) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery()), (Ids{1, 2, 3, 4, 5, 6}));
}

TEST(JournalQueryTest, OrderIsPostingOrderNotBusinessDate) {
    // #4 is dated day 5 but was posted after #2/#3 (day 10): it stays there.
    History h;
    const std::vector<PostedJournalEntry> all = findJournalEntries(h.ledger, JournalQuery());
    EXPECT_EQ(all[3].entry().date(), day(5));
    EXPECT_GT(all[2].entry().date(), all[3].entry().date());
}

TEST(JournalQueryTest, EntriesSharingABusinessDateKeepPostingOrder) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withDateRange(Period(day(10), day(11)))), (Ids{2, 3}));
}

TEST(JournalQueryTest, RepeatedQueriesAreIdentical) {
    History h;
    const JournalQuery query = JournalQuery().withAccount(h.cash).withDateRange(Period(day(0), day(500)));
    EXPECT_EQ(h.ids(query), h.ids(query));
    EXPECT_EQ(h.ids(query), (Ids{1, 2, 3, 4, 6}));
}

// ---------------------------------------------------------------------
// Date range: [start, end)
// ---------------------------------------------------------------------

TEST(JournalQueryTest, DateRangeSelectsEntriesByBusinessDate) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withDateRange(Period(day(0), day(365)))), (Ids{1, 2, 3, 4, 5}));
    EXPECT_EQ(h.ids(JournalQuery().withDateRange(Period(day(365), day(1000)))), (Ids{6}));
}

TEST(JournalQueryTest, DateRangeIncludesItsStart) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withDateRange(Period(day(5), day(6)))), (Ids{4}));
}

TEST(JournalQueryTest, DateRangeExcludesItsEnd) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withDateRange(Period(day(1), day(10)))), (Ids{1, 4}));
    EXPECT_TRUE(h.ids(JournalQuery().withDateRange(Period(day(390), day(400)))).empty());
}

TEST(JournalQueryTest, DateRangeIgnoresWhetherAPeriodIsLocked) {
    // Period locking restricts postings, never history queries.
    History h;
    const JournalQuery query = JournalQuery().withDateRange(Period(day(0), day(365)));
    const Ids before = h.ids(query);
    h.ledger.defineAccountingPeriod(Period(day(0), day(365)));
    h.ledger.closeAccountingPeriod(Period(day(0), day(365)));
    EXPECT_EQ(h.ids(query), before);
}

// ---------------------------------------------------------------------
// Account involvement
// ---------------------------------------------------------------------

TEST(JournalQueryTest, AccountFilterMatchesDebitLines) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withAccount(h.rent)), (Ids{3, 5}));  // debited in #3, credited by the close
}

TEST(JournalQueryTest, AccountFilterMatchesCreditLines) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withAccount(h.capital)), (Ids{1}));
    EXPECT_EQ(h.ids(JournalQuery().withAccount(h.payable)), (Ids{4}));
}

TEST(JournalQueryTest, AccountFilterMatchesAnyLineOfMultiLineEntries) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withAccount(h.retained)), (Ids{5}));  // third line of the closing entry
}

TEST(JournalQueryTest, AccountFilterWithNoMatchingLinesYieldsNothing) {
    History h;
    EXPECT_TRUE(h.ids(JournalQuery().withAccount(h.unused)).empty());
    EXPECT_TRUE(h.ids(JournalQuery().withAccount(AccountId(9999))).empty());
}

TEST(JournalQueryTest, EntryRepeatingAnAccountIsReturnedOnce) {
    // JournalEntry allows the same account on several lines; the entry
    // still appears once, with every recorded line.
    History h;
    post(JournalEntry::create(day(401), "Split",
                              {JournalEntryLine::debit(h.cash, usd(4)), JournalEntryLine::debit(h.cash, usd(6)),
                               JournalEntryLine::credit(h.sales, usd(10))}),
         h.chart, h.ledger);
    const std::vector<PostedJournalEntry> result =
        findJournalEntries(h.ledger, JournalQuery().withAccount(h.cash).withDateRange(Period(day(401), day(402))));
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].entry().lines().size(), 3u);
}

// ---------------------------------------------------------------------
// Kind, combinations, values
// ---------------------------------------------------------------------

TEST(JournalQueryTest, ClosingOnlyUsesTheEntryKind) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withKind(EntryKindFilter::ClosingOnly)), (Ids{5}));
}

TEST(JournalQueryTest, StandardOnlyExcludesClosingEntries) {
    History h;
    EXPECT_EQ(h.ids(JournalQuery().withKind(EntryKindFilter::StandardOnly)), (Ids{1, 2, 3, 4, 6}));
    EXPECT_EQ(h.ids(JournalQuery().withKind(EntryKindFilter::All)), (Ids{1, 2, 3, 4, 5, 6}));
}

TEST(JournalQueryTest, FiltersCombineWithLogicalAnd) {
    History h;
    const JournalQuery firstYearSales = JournalQuery().withAccount(h.sales).withDateRange(Period(day(0), day(365)));
    EXPECT_EQ(h.ids(firstYearSales), (Ids{2, 5}));
    EXPECT_EQ(h.ids(firstYearSales.withKind(EntryKindFilter::StandardOnly)), (Ids{2}));
    EXPECT_EQ(h.ids(firstYearSales.withKind(EntryKindFilter::ClosingOnly)), (Ids{5}));
    EXPECT_TRUE(h.ids(JournalQuery().withAccount(h.payable).withKind(EntryKindFilter::ClosingOnly)).empty());
}

TEST(JournalQueryTest, QueryValuesAreImmutable) {
    History h;
    const JournalQuery base;
    const JournalQuery narrowed = base.withAccount(h.cash).withKind(EntryKindFilter::StandardOnly);
    EXPECT_FALSE(base.account().has_value());
    EXPECT_EQ(base.kind(), EntryKindFilter::All);
    ASSERT_TRUE(narrowed.account().has_value());
    EXPECT_EQ(*narrowed.account(), h.cash);
    EXPECT_FALSE(narrowed.dateRange().has_value());
}

TEST(JournalQueryTest, MoneyAndCurrencyAreReturnedExactly) {
    History h;
    const std::vector<PostedJournalEntry> last =
        findJournalEntries(h.ledger, JournalQuery().withDateRange(Period(day(400), day(401))));
    ASSERT_EQ(last.size(), 1u);
    EXPECT_EQ(last[0].entry().lines()[0].amount(), usd(70, 5));
    EXPECT_EQ(last[0].entry().lines()[0].amount().toString(), "70.05 USD");

    ChartOfAccounts chart;
    Ledger eurLedger(Currency("EUR"));
    const AccountId cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
    const AccountId sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
    const Money eur = Money::fromMajorUnits(9, 99, Currency("EUR"));
    post(JournalEntry::create(day(1), "Vente", {JournalEntryLine::debit(cash, eur), JournalEntryLine::credit(sales, eur)}),
         chart, eurLedger);
    const std::vector<PostedJournalEntry> eurResult = findJournalEntries(eurLedger, JournalQuery());
    ASSERT_EQ(eurResult.size(), 1u);
    EXPECT_EQ(eurResult[0].entry().currency(), Currency("EUR"));
    EXPECT_EQ(eurResult[0].entry().totalDebits().toString(), "9.99 EUR");
}

// ---------------------------------------------------------------------
// Read-only and lifetime
// ---------------------------------------------------------------------

TEST(JournalQueryTest, QueryingNeverChangesTheLedger) {
    History h;
    h.ledger.defineAccountingPeriod(Period(day(0), day(100)));
    const Money cash = h.ledger.balance(h.cash);
    const Money retained = h.ledger.balance(h.retained);
    const std::size_t history = h.ledger.postedEntries().size();

    for (const JournalQuery& query :
         {JournalQuery(), JournalQuery().withAccount(h.cash), JournalQuery().withKind(EntryKindFilter::ClosingOnly),
          JournalQuery().withDateRange(Period(day(0), day(50)))}) {
        findJournalEntries(h.ledger, query);
    }

    EXPECT_EQ(h.ledger.balance(h.cash), cash);
    EXPECT_EQ(h.ledger.balance(h.retained), retained);
    EXPECT_EQ(h.ledger.postedEntries().size(), history);
    ASSERT_EQ(h.ledger.accountingPeriods().size(), 1u);
    EXPECT_FALSE(h.ledger.accountingPeriods()[0].isClosed());
}

TEST(JournalQueryTest, ResultsStayValidAfterLaterPostings) {
    History h;
    const std::vector<PostedJournalEntry> snapshot = findJournalEntries(h.ledger, JournalQuery());
    for (int i = 0; i < 64; ++i) {  // force the Ledger's history vector to reallocate
        h.postPair(day(500 + i), "More", h.cash, h.sales, usd(1));
    }
    ASSERT_EQ(snapshot.size(), 6u);
    EXPECT_EQ(snapshot[0].entry().description(), "Invest");
    EXPECT_EQ(snapshot[5].entry().lines()[0].amount(), usd(70, 5));
}
