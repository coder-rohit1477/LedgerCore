#include <gtest/gtest.h>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/ledger/Ledger.h"

using ledgercore::domain::AccountId;
using ledgercore::domain::Currency;
using ledgercore::domain::Money;
using ledgercore::ledger::Ledger;

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
