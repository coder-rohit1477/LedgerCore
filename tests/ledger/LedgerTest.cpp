#include <gtest/gtest.h>

#include <chrono>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/LedgerExceptions.h"

using ledgercore::domain::AccountId;
using ledgercore::domain::Currency;
using ledgercore::domain::Money;
using ledgercore::domain::Period;
using ledgercore::ledger::AccountingPeriodAlreadyClosedException;
using ledgercore::ledger::AccountingPeriodOverlapException;
using ledgercore::ledger::AccountingPeriodState;
using ledgercore::ledger::Ledger;
using ledgercore::ledger::UnknownAccountingPeriodException;

// Ledger's only mutation path (commit()) is private and reachable solely
// through posting::post() -- there is no way to get a non-empty Ledger
// without a ChartOfAccounts and a JournalEntry. Tests that exercise actual
// posting effects (balances after posting, PostingId assignment,
// PostedJournalEntry contents, history growth) live in
// tests/posting/PostingEngineTest.cpp, which is where that behavior is
// actually reachable. This file covers what a freshly constructed Ledger
// guarantees on its own.

TEST(LedgerTest, ConstructionStoresCurrency) {
    Currency usd("USD");
    Ledger ledger(usd);

    EXPECT_EQ(ledger.currency(), usd);
}

TEST(LedgerTest, ZeroBalanceForAnyAccountBeforeAnyPosting) {
    Currency usd("USD");
    Ledger ledger(usd);

    EXPECT_EQ(ledger.balance(AccountId(1)), Money::zero(usd));
    EXPECT_EQ(ledger.balance(AccountId(42)), Money::zero(usd));
}

TEST(LedgerTest, BalanceForUnknownAccountIsZeroNotAnError) {
    Currency usd("USD");
    Ledger ledger(usd);

    EXPECT_NO_THROW(ledger.balance(AccountId(999)));
    EXPECT_TRUE(ledger.balance(AccountId(999)).isZero());
}

TEST(LedgerTest, PostedEntriesIsEmptyBeforeAnyPosting) {
    Currency usd("USD");
    Ledger ledger(usd);

    EXPECT_TRUE(ledger.postedEntries().empty());
    EXPECT_EQ(ledger.postedEntries().size(), 0u);
}

TEST(LedgerTest, PostedEntriesAccessorReturnsStableReference) {
    Currency usd("USD");
    Ledger ledger(usd);

    const auto& first = ledger.postedEntries();
    const auto& second = ledger.postedEntries();
    EXPECT_EQ(&first, &second);
}

TEST(LedgerTest, EachLedgerHasItsOwnIndependentState) {
    Currency usd("USD");
    Currency eur("EUR");
    Ledger usdLedger(usd);
    Ledger eurLedger(eur);

    EXPECT_EQ(usdLedger.currency(), usd);
    EXPECT_EQ(eurLedger.currency(), eur);
    EXPECT_NE(usdLedger.currency(), eurLedger.currency());
}

// ---------------------------------------------------------------------
// Accounting periods (metadata; posting enforcement is tested in posting)
// ---------------------------------------------------------------------

namespace {

std::chrono::system_clock::time_point day(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * n);
}

} // namespace

TEST(LedgerPeriodTest, NoPeriodsByDefault) {
    Ledger ledger(Currency("USD"));
    EXPECT_TRUE(ledger.accountingPeriods().empty());
    EXPECT_EQ(ledger.closedPeriodContaining(day(5)), nullptr);
}

TEST(LedgerPeriodTest, DefinedPeriodStartsOpen) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ASSERT_EQ(ledger.accountingPeriods().size(), 1u);
    EXPECT_EQ(ledger.accountingPeriods()[0].state(), AccountingPeriodState::Open);
    EXPECT_EQ(ledger.accountingPeriods()[0].period().start(), day(0));
    EXPECT_EQ(ledger.accountingPeriods()[0].period().end(), day(10));
    // An open period never blocks anything.
    EXPECT_EQ(ledger.closedPeriodContaining(day(5)), nullptr);
}

TEST(LedgerPeriodTest, CloseTransitionsOpenToClosed) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ledger.closeAccountingPeriod(Period(day(0), day(10)));
    EXPECT_EQ(ledger.accountingPeriods()[0].state(), AccountingPeriodState::Closed);
    EXPECT_TRUE(ledger.accountingPeriods()[0].isClosed());
}

TEST(LedgerPeriodTest, ClosingTwiceIsRejectedAndThereIsNoWayBack) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ledger.closeAccountingPeriod(Period(day(0), day(10)));
    EXPECT_THROW(ledger.closeAccountingPeriod(Period(day(0), day(10))), AccountingPeriodAlreadyClosedException);
    // Redefining the closed range (the only other mutator) cannot reopen it.
    EXPECT_THROW(ledger.defineAccountingPeriod(Period(day(0), day(10))), AccountingPeriodOverlapException);
    EXPECT_TRUE(ledger.accountingPeriods()[0].isClosed());
    EXPECT_EQ(ledger.accountingPeriods().size(), 1u);
}

TEST(LedgerPeriodTest, ClosingAnUndefinedOrNonExactPeriodIsRejected) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    EXPECT_THROW(ledger.closeAccountingPeriod(Period(day(20), day(30))), UnknownAccountingPeriodException);
    EXPECT_THROW(ledger.closeAccountingPeriod(Period(day(0), day(9))), UnknownAccountingPeriodException);
    EXPECT_THROW(ledger.closeAccountingPeriod(Period(day(1), day(10))), UnknownAccountingPeriodException);
    EXPECT_FALSE(ledger.accountingPeriods()[0].isClosed());
}

TEST(LedgerPeriodTest, DuplicateAndOverlappingPeriodsAreRejected) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(10), day(20)));
    EXPECT_THROW(ledger.defineAccountingPeriod(Period(day(10), day(20))), AccountingPeriodOverlapException);
    EXPECT_THROW(ledger.defineAccountingPeriod(Period(day(5), day(11))), AccountingPeriodOverlapException);
    EXPECT_THROW(ledger.defineAccountingPeriod(Period(day(19), day(25))), AccountingPeriodOverlapException);
    EXPECT_THROW(ledger.defineAccountingPeriod(Period(day(12), day(15))), AccountingPeriodOverlapException);
    EXPECT_THROW(ledger.defineAccountingPeriod(Period(day(0), day(30))), AccountingPeriodOverlapException);
    EXPECT_EQ(ledger.accountingPeriods().size(), 1u);
}

TEST(LedgerPeriodTest, AdjacentPeriodsAndGapsAreAllowed) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ledger.defineAccountingPeriod(Period(day(10), day(20)));  // adjacent
    ledger.defineAccountingPeriod(Period(day(30), day(40)));  // gap [20, 30)
    EXPECT_EQ(ledger.accountingPeriods().size(), 3u);
}

TEST(LedgerPeriodTest, PeriodsAreKeptInStartOrderRegardlessOfDefinitionOrder) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(30), day(40)));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ledger.defineAccountingPeriod(Period(day(10), day(20)));
    ASSERT_EQ(ledger.accountingPeriods().size(), 3u);
    EXPECT_EQ(ledger.accountingPeriods()[0].period().start(), day(0));
    EXPECT_EQ(ledger.accountingPeriods()[1].period().start(), day(10));
    EXPECT_EQ(ledger.accountingPeriods()[2].period().start(), day(30));
}

TEST(LedgerPeriodTest, ClosedPeriodContainingUsesHalfOpenBoundaries) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(10), day(20)));
    ledger.closeAccountingPeriod(Period(day(10), day(20)));
    EXPECT_EQ(ledger.closedPeriodContaining(day(10) - std::chrono::microseconds(1)), nullptr);
    EXPECT_NE(ledger.closedPeriodContaining(day(10)), nullptr);  // start is inside
    EXPECT_NE(ledger.closedPeriodContaining(day(15)), nullptr);
    EXPECT_NE(ledger.closedPeriodContaining(day(20) - std::chrono::microseconds(1)), nullptr);
    EXPECT_EQ(ledger.closedPeriodContaining(day(20)), nullptr);  // end is outside
}

TEST(LedgerPeriodTest, ClosedPeriodContainingFindsTheRightPeriodAmongMany) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ledger.defineAccountingPeriod(Period(day(10), day(20)));
    ledger.defineAccountingPeriod(Period(day(30), day(40)));
    ledger.closeAccountingPeriod(Period(day(0), day(10)));
    ledger.closeAccountingPeriod(Period(day(30), day(40)));

    ASSERT_NE(ledger.closedPeriodContaining(day(5)), nullptr);
    EXPECT_EQ(ledger.closedPeriodContaining(day(5))->period().start(), day(0));
    EXPECT_EQ(ledger.closedPeriodContaining(day(15)), nullptr);  // open period
    EXPECT_EQ(ledger.closedPeriodContaining(day(25)), nullptr);  // gap
    ASSERT_NE(ledger.closedPeriodContaining(day(35)), nullptr);
    EXPECT_EQ(ledger.closedPeriodContaining(day(35))->period().start(), day(30));
    EXPECT_EQ(ledger.closedPeriodContaining(day(45)), nullptr);  // after all periods
}

TEST(LedgerPeriodTest, InvalidPeriodBoundsAreRejectedByPeriodItself) {
    EXPECT_THROW(Period(day(10), day(10)), ledgercore::domain::InvalidPeriodException);
    EXPECT_THROW(Period(day(10), day(5)), ledgercore::domain::InvalidPeriodException);
}

TEST(LedgerPeriodTest, PeriodMetadataNeverChangesBalancesOrHistory) {
    Ledger ledger(Currency("USD"));
    ledger.defineAccountingPeriod(Period(day(0), day(10)));
    ledger.closeAccountingPeriod(Period(day(0), day(10)));
    EXPECT_TRUE(ledger.postedEntries().empty());
    EXPECT_TRUE(ledger.balance(AccountId(1)).isZero());
}
