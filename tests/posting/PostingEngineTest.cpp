#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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
#include "ledgercore/domain/NormalBalance.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/LedgerExceptions.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/ledger/PostingId.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/posting/PostingExceptions.h"

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
using ledgercore::domain::MoneyOverflowException;
using ledgercore::ledger::Ledger;
using ledgercore::ledger::LedgerCurrencyMismatchException;
using ledgercore::ledger::PostedJournalEntry;
using ledgercore::ledger::PostingId;
using ledgercore::posting::AccountNotFoundException;
using ledgercore::domain::DuplicateAccountCodeException;
using ledgercore::domain::ForeignAccountException;
using ledgercore::domain::InvalidAccountException;
using ledgercore::posting::addChildAccount;
using ledgercore::posting::ClosedPeriodPostingException;
using ledgercore::posting::InvalidClosingEntryException;
using ledgercore::posting::InvalidPostingTargetException;
using ledgercore::posting::post;
using ledgercore::posting::PostedAccountCannotBecomeGroupException;

namespace {
std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}
} // namespace

// ---------------------------------------------------------------------
// Successful posting
// ---------------------------------------------------------------------

TEST(PostingEngineTest, SuccessfulTwoLinePosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Sales Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "Cash sale", lines);

    PostingId id = post(entry, chart, ledger);

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(ledger.balance(revenue.id()), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(id.value(), 1u);
}

TEST(PostingEngineTest, SuccessfulMultiLinePosting) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Sales Revenue", AccountType::Revenue);
    Account& tax = chart.addRootAccount(AccountCode("2100"), "Sales Tax Payable", AccountType::Liability);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(108, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(tax.id(), Money::fromMajorUnits(8, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "Cash sale with tax", lines);

    post(entry, chart, ledger);

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(108, 0, usd));
    EXPECT_EQ(ledger.balance(revenue.id()), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(ledger.balance(tax.id()), Money::fromMajorUnits(8, 0, usd));
}

TEST(PostingEngineTest, AllAccountTypeDebitCreditEffectsAreCorrect) {
    Currency usd("USD");
    struct Case {
        AccountType type;
        bool debitIncreases;
    };
    const Case cases[] = {
        {AccountType::Asset, true},
        {AccountType::Expense, true},
        {AccountType::Liability, false},
        {AccountType::Equity, false},
        {AccountType::Revenue, false},
    };

    for (const Case& testCase : cases) {
        {
            ChartOfAccounts chart;
            Ledger ledger(usd);
            Account& subject = chart.addRootAccount(AccountCode("1"), "Subject", testCase.type);
            Account& other = chart.addRootAccount(AccountCode("2"), "Other", AccountType::Asset);
            std::vector<JournalEntryLine> lines{
                JournalEntryLine::debit(subject.id(), Money::fromMajorUnits(10, 0, usd)),
                JournalEntryLine::credit(other.id(), Money::fromMajorUnits(10, 0, usd)),
            };
            post(JournalEntry::create(testDate(), "Debit effect", lines), chart, ledger);

            const Money expected = testCase.debitIncreases ? Money::fromMajorUnits(10, 0, usd)
                                                             : Money::fromMajorUnits(-10, 0, usd);
            EXPECT_EQ(ledger.balance(subject.id()), expected);
        }
        {
            ChartOfAccounts chart;
            Ledger ledger(usd);
            Account& subject = chart.addRootAccount(AccountCode("1"), "Subject", testCase.type);
            Account& other = chart.addRootAccount(AccountCode("2"), "Other", AccountType::Asset);
            std::vector<JournalEntryLine> lines{
                JournalEntryLine::credit(subject.id(), Money::fromMajorUnits(10, 0, usd)),
                JournalEntryLine::debit(other.id(), Money::fromMajorUnits(10, 0, usd)),
            };
            post(JournalEntry::create(testDate(), "Credit effect", lines), chart, ledger);

            const Money expected = testCase.debitIncreases ? Money::fromMajorUnits(-10, 0, usd)
                                                             : Money::fromMajorUnits(10, 0, usd);
            EXPECT_EQ(ledger.balance(subject.id()), expected);
        }
    }
}

// ---------------------------------------------------------------------
// Validation failures
// ---------------------------------------------------------------------

TEST(PostingEngineTest, MissingAccountThrowsAccountNotFoundException) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(9999), Money::fromMajorUnits(100, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "References unknown account", lines);

    EXPECT_THROW(post(entry, chart, ledger), AccountNotFoundException);
}

TEST(PostingEngineTest, NonLeafAccountThrowsInvalidPostingTargetException) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    addChildAccount(chart, ledger, assets, AccountCode("1110"), "Cash");
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(assets.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "Posts to group account", lines);

    EXPECT_THROW(post(entry, chart, ledger), InvalidPostingTargetException);
}

TEST(PostingEngineTest, CurrencyMismatchThrowsLedgerCurrencyMismatchException) {
    Currency usd("USD");
    Currency eur("EUR");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(eur);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "USD entry into EUR ledger", lines);

    EXPECT_THROW(post(entry, chart, ledger), LedgerCurrencyMismatchException);
}

// ---------------------------------------------------------------------
// Atomicity
// ---------------------------------------------------------------------

TEST(PostingEngineTest, AtomicFailureLeavesLedgerUnchangedWhenOneAccountIsMissing) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(9999), Money::fromMajorUnits(100, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "A exists, B missing", lines);

    EXPECT_THROW(post(entry, chart, ledger), AccountNotFoundException);

    EXPECT_TRUE(ledger.balance(cash.id()).isZero());
    EXPECT_TRUE(ledger.postedEntries().empty());
}

TEST(PostingEngineTest, AtomicFailureOnOverflowLeavesLedgerUnchanged) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Owner's Equity", AccountType::Equity);
    Ledger ledger(usd);

    const Money huge = Money::ofMinorUnits(std::numeric_limits<std::int64_t>::max(), usd);
    std::vector<JournalEntryLine> firstLines{
        JournalEntryLine::debit(cash.id(), huge),
        JournalEntryLine::credit(equity.id(), huge),
    };
    post(JournalEntry::create(testDate(), "Near-max balance", firstLines), chart, ledger);

    ASSERT_EQ(ledger.balance(cash.id()), huge);
    ASSERT_EQ(ledger.balance(equity.id()), huge);
    ASSERT_EQ(ledger.postedEntries().size(), 1u);

    std::vector<JournalEntryLine> secondLines{
        JournalEntryLine::debit(cash.id(), Money::ofMinorUnits(1, usd)),
        JournalEntryLine::credit(equity.id(), Money::ofMinorUnits(1, usd)),
    };
    JournalEntry second = JournalEntry::create(testDate(), "Overflows Cash's balance", secondLines);

    EXPECT_THROW(post(second, chart, ledger), MoneyOverflowException);

    EXPECT_EQ(ledger.balance(cash.id()), huge);
    EXPECT_EQ(ledger.balance(equity.id()), huge);
    EXPECT_EQ(ledger.postedEntries().size(), 1u);
}

// ---------------------------------------------------------------------
// Duplicate lines, sequencing, negative balances, repeated posting
// ---------------------------------------------------------------------

TEST(PostingEngineTest, DuplicateAccountLinesAreAggregatedBeforeCommit) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(150, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "Two cash debits", lines);

    post(entry, chart, ledger);

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(150, 0, usd));
    EXPECT_EQ(ledger.balance(revenue.id()), Money::fromMajorUnits(150, 0, usd));
}

TEST(PostingEngineTest, MultipleSequentialPostingsAccumulateBalances) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    for (int i = 1; i <= 5; ++i) {
        std::vector<JournalEntryLine> lines{
            JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(10, 0, usd)),
            JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(10, 0, usd)),
        };
        post(JournalEntry::create(testDate(), "Sale " + std::to_string(i), lines), chart, ledger);
    }

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(50, 0, usd));
    EXPECT_EQ(ledger.balance(revenue.id()), Money::fromMajorUnits(50, 0, usd));
    EXPECT_EQ(ledger.postedEntries().size(), 5u);
}

TEST(PostingEngineTest, LegitimateNegativeBalanceIsPermitted) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expenses", AccountType::Expense);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(500, 0, usd)),
        JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(500, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "Overdraws cash", lines);

    EXPECT_NO_THROW(post(entry, chart, ledger));
    EXPECT_TRUE(ledger.balance(cash.id()).isNegative());
    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(-500, 0, usd));
}

TEST(PostingEngineTest, PostingTheSameEntryTwiceIsNotIdempotentByDesign) {
    // Idempotency is explicitly deferred (Phase 4 design): JournalEntry
    // has no identity, so PostingEngine cannot detect "this was already
    // posted." Calling post() twice posts twice -- pinned here as current,
    // documented behavior, not a permanent guarantee.
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
    };
    JournalEntry entry = JournalEntry::create(testDate(), "Cash sale", lines);

    post(entry, chart, ledger);
    post(entry, chart, ledger);

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(200, 0, usd));
    EXPECT_EQ(ledger.balance(revenue.id()), Money::fromMajorUnits(200, 0, usd));
    EXPECT_EQ(ledger.postedEntries().size(), 2u);
}

// ---------------------------------------------------------------------
// PostingId / PostedJournalEntry / history
// ---------------------------------------------------------------------

TEST(PostingEngineTest, PostingIdIncreasesMonotonically) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    std::vector<PostingId> ids;
    for (int i = 0; i < 4; ++i) {
        std::vector<JournalEntryLine> lines{
            JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
            JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, usd)),
        };
        ids.push_back(post(JournalEntry::create(testDate(), "Sale", lines), chart, ledger));
    }

    for (std::size_t i = 1; i < ids.size(); ++i) {
        EXPECT_GT(ids[i].value(), ids[i - 1].value());
    }
    EXPECT_EQ(ids.front().value(), 1u);
}

TEST(PostingEngineTest, PostedAtIsRecordedAtPostingTime) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    const auto before = std::chrono::system_clock::now();
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Sale", lines), chart, ledger);
    const auto after = std::chrono::system_clock::now();

    ASSERT_EQ(ledger.postedEntries().size(), 1u);
    const auto postedAt = ledger.postedEntries().front().postedAt();
    EXPECT_GE(postedAt, before);
    EXPECT_LE(postedAt, after);
}

TEST(PostingEngineTest, PostedJournalEntryRetainsTheEntryThatWasPosted) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, usd)),
    };
    PostingId id = post(JournalEntry::create(testDate(), "Traceable sale", lines), chart, ledger);

    ASSERT_EQ(ledger.postedEntries().size(), 1u);
    const PostedJournalEntry& posted = ledger.postedEntries().front();

    EXPECT_EQ(posted.id(), id);
    EXPECT_EQ(posted.entry().description(), "Traceable sale");
    EXPECT_EQ(posted.entry().totalDebits(), Money::fromMajorUnits(1, 0, usd));
}

TEST(PostingEngineTest, LedgerHistoryLengthMatchesNumberOfSuccessfulPosts) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    for (int i = 0; i < 3; ++i) {
        std::vector<JournalEntryLine> lines{
            JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
            JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, usd)),
        };
        post(JournalEntry::create(testDate(), "Sale", lines), chart, ledger);
    }

    std::vector<JournalEntryLine> badLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
        JournalEntryLine::credit(AccountId(9999), Money::fromMajorUnits(1, 0, usd)),
    };
    EXPECT_THROW(post(JournalEntry::create(testDate(), "Bad entry", badLines), chart, ledger), AccountNotFoundException);

    EXPECT_EQ(ledger.postedEntries().size(), 3u);
}

// ---------------------------------------------------------------------
// Exact arithmetic and overall balance correctness
// ---------------------------------------------------------------------

TEST(PostingEngineTest, ExactMoneyArithmeticAcrossManyPostings) {
    // 0.1 + 0.2 != 0.3 in binary floating point; via Money's minor-unit
    // integers, this must be exact.
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> firstLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(0, 30, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(0, 30, usd)),
    };
    post(JournalEntry::create(testDate(), "Thirty cents", firstLines), chart, ledger);

    std::vector<JournalEntryLine> secondLines{
        JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(0, 10, usd)),
        JournalEntryLine::debit(revenue.id(), Money::fromMajorUnits(0, 10, usd)),
    };
    post(JournalEntry::create(testDate(), "Reverse ten cents", secondLines), chart, ledger);

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(0, 20, usd));
}

TEST(PostingEngineTest, BalanceCorrectnessAcrossMixedEntries) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Rent Expense", AccountType::Expense);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> saleLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(500, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(500, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Cash sale", saleLines), chart, ledger);

    std::vector<JournalEntryLine> rentLines{
        JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
        JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(200, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Pay rent", rentLines), chart, ledger);

    EXPECT_EQ(ledger.balance(cash.id()), Money::fromMajorUnits(300, 0, usd));
    EXPECT_EQ(ledger.balance(revenue.id()), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(ledger.balance(expense.id()), Money::fromMajorUnits(200, 0, usd));
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------
//
// domain::debitCreditPresentation() itself is unit-tested exhaustively in
// tests/domain/NormalBalanceTest.cpp. The property test below verifies
// PostingEngine and that shared helper agree end to end through an actual
// posting sequence.

TEST(PostingEnginePropertyTest, ReplayingPostedEntriesReproducesBalancesExactly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> lines1{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(300, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Sale 1", lines1), chart, ledger);

    std::vector<JournalEntryLine> lines2{
        JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(50, 0, usd)),
        JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(50, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Buy supplies", lines2), chart, ledger);

    std::vector<JournalEntryLine> lines3{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(120, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(120, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Sale 2", lines3), chart, ledger);

    // Replay every posted entry into a fresh Ledger through the same
    // production posting path, and confirm the resulting balances match
    // the original Ledger's cached balances exactly.
    Ledger replay(usd);
    for (const PostedJournalEntry& posted : ledger.postedEntries()) {
        post(posted.entry(), chart, replay);
    }

    EXPECT_EQ(replay.balance(cash.id()), ledger.balance(cash.id()));
    EXPECT_EQ(replay.balance(revenue.id()), ledger.balance(revenue.id()));
    EXPECT_EQ(replay.balance(expense.id()), ledger.balance(expense.id()));
}

TEST(PostingEnginePropertyTest, FailedPostNeverChangesLedgerState) {
    Currency usd("USD");
    Currency eur("EUR");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& group = chart.addRootAccount(AccountCode("2000"), "Liabilities", AccountType::Liability);
    addChildAccount(chart, ledger, group, AccountCode("2100"), "Accounts Payable");

    std::vector<JournalEntryLine> baselineLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Baseline sale", baselineLines), chart, ledger);

    const Money cashBalance = ledger.balance(cash.id());
    const Money revenueBalance = ledger.balance(revenue.id());
    const std::size_t historyLength = ledger.postedEntries().size();

    std::vector<JournalEntryLine> missingAccountLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
        JournalEntryLine::credit(AccountId(9999), Money::fromMajorUnits(1, 0, usd)),
    };
    EXPECT_THROW(
        post(JournalEntry::create(testDate(), "Missing account", missingAccountLines), chart, ledger),
        AccountNotFoundException);

    std::vector<JournalEntryLine> groupTargetLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, usd)),
        JournalEntryLine::credit(group.id(), Money::fromMajorUnits(1, 0, usd)),
    };
    EXPECT_THROW(
        post(JournalEntry::create(testDate(), "Group account target", groupTargetLines), chart, ledger),
        InvalidPostingTargetException);

    std::vector<JournalEntryLine> wrongCurrencyLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1, 0, eur)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(1, 0, eur)),
    };
    EXPECT_THROW(
        post(JournalEntry::create(testDate(), "Wrong currency", wrongCurrencyLines), chart, ledger),
        LedgerCurrencyMismatchException);

    EXPECT_EQ(ledger.balance(cash.id()), cashBalance);
    EXPECT_EQ(ledger.balance(revenue.id()), revenueBalance);
    EXPECT_EQ(ledger.postedEntries().size(), historyLength);
}

TEST(PostingEnginePropertyTest, TrialBalanceTotalsMatchAfterSuccessfulPostings) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Equity", AccountType::Equity);
    Ledger ledger(usd);

    std::vector<JournalEntryLine> investmentLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(1000, 0, usd)),
        JournalEntryLine::credit(equity.id(), Money::fromMajorUnits(1000, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Owner investment", investmentLines), chart, ledger);

    std::vector<JournalEntryLine> saleLines{
        JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
        JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(300, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Cash sale", saleLines), chart, ledger);

    std::vector<JournalEntryLine> rentLines{
        JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(150, 0, usd)),
        JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(150, 0, usd)),
    };
    post(JournalEntry::create(testDate(), "Pay rent", rentLines), chart, ledger);

    const std::vector<std::pair<AccountId, AccountType>> accounts{
        {cash.id(), AccountType::Asset},
        {revenue.id(), AccountType::Revenue},
        {expense.id(), AccountType::Expense},
        {equity.id(), AccountType::Equity},
    };

    Money totalDebits = Money::zero(usd);
    Money totalCredits = Money::zero(usd);
    for (const auto& [accountId, type] : accounts) {
        const DebitCreditAmounts presentation = debitCreditPresentation(type, ledger.balance(accountId));
        totalDebits = totalDebits + presentation.debit;
        totalCredits = totalCredits + presentation.credit;
    }

    EXPECT_EQ(totalDebits, totalCredits);
    EXPECT_EQ(totalDebits, Money::fromMajorUnits(1300, 0, usd));
}

// ---------------------------------------------------------------------
// Ledger-aware child creation (posting::addChildAccount)
//
// A posted leaf must never become a group account: TrialBalance would
// silently drop its history and persistence could no longer replay it.
// ---------------------------------------------------------------------

namespace {

struct PostedPair {
    Account* cash;
    Account* equity;
};

// Cash (asset) and Capital (equity) roots, with `amount` posted Dr Cash /
// Cr Capital.
PostedPair setUpPostedCashAndCapital(ChartOfAccounts& chart, Ledger& ledger, const Money& amount) {
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Capital", AccountType::Equity);
    post(JournalEntry::create(testDate(), "Owner investment",
                               {
                                   JournalEntryLine::debit(cash.id(), amount),
                                   JournalEntryLine::credit(equity.id(), amount),
                               }),
         chart, ledger);
    return PostedPair{&cash, &equity};
}

} // namespace

TEST(LedgerPostingHistoryTest, HistoryIsTrackedForEveryPostedAccountOnBothSides) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    const AccountId untouched =
        chart.addRootAccount(AccountCode("5000"), "Unused", AccountType::Expense).id();
    EXPECT_FALSE(ledger.hasPostingHistory(untouched));

    PostedPair accounts = setUpPostedCashAndCapital(chart, ledger, Money::fromMajorUnits(500, 0, usd));

    EXPECT_TRUE(ledger.hasPostingHistory(accounts.cash->id()));
    EXPECT_TRUE(ledger.hasPostingHistory(accounts.equity->id()));
    EXPECT_FALSE(ledger.hasPostingHistory(untouched));
}

TEST(PostingEngineTest, AddChildAccountRejectsParentWithPostedBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    PostedPair accounts = setUpPostedCashAndCapital(chart, ledger, Money::fromMajorUnits(500, 0, usd));

    EXPECT_THROW(addChildAccount(chart, ledger, *accounts.cash, AccountCode("1010"), "Petty cash"),
                 PostedAccountCannotBecomeGroupException);
    EXPECT_TRUE(accounts.cash->isLeaf());
}

TEST(PostingEngineTest, AddChildAccountRejectsParentWhoseHistoryNetsToZero) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    PostedPair accounts = setUpPostedCashAndCapital(chart, ledger, Money::fromMajorUnits(500, 0, usd));
    // Reverse the investment: Cash's balance is now exactly zero, but it
    // still has posting history that must remain replayable.
    post(JournalEntry::create(testDate(), "Reversal",
                               {
                                   JournalEntryLine::debit(accounts.equity->id(), Money::fromMajorUnits(500, 0, usd)),
                                   JournalEntryLine::credit(accounts.cash->id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);
    ASSERT_TRUE(ledger.balance(accounts.cash->id()).isZero());

    EXPECT_THROW(addChildAccount(chart, ledger, *accounts.cash, AccountCode("1010"), "Petty cash"),
                 PostedAccountCannotBecomeGroupException);
    EXPECT_TRUE(accounts.cash->isLeaf());
}

TEST(PostingEngineTest, AddChildAccountAllowsFirstChildUnderUnpostedLeaf) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    setUpPostedCashAndCapital(chart, ledger, Money::fromMajorUnits(500, 0, usd));
    Account& receivables = chart.addRootAccount(AccountCode("1200"), "Receivables", AccountType::Asset);

    Account& customerA = addChildAccount(chart, ledger, receivables, AccountCode("1210"), "Customer A");

    EXPECT_FALSE(receivables.isLeaf());
    EXPECT_EQ(customerA.parent(), &receivables);
    EXPECT_EQ(customerA.type(), AccountType::Asset);
    EXPECT_EQ(chart.findByCode(AccountCode("1210")), &customerA);
}

TEST(PostingEngineTest, AddChildAccountAllowsSiblingUnderGroupWhoseChildrenArePosted) {
    // The group itself was never a posting target (it can't be), so
    // growing it is safe even though its existing children have history.
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = addChildAccount(chart, ledger, assets, AccountCode("1100"), "Cash");
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Capital", AccountType::Equity);
    post(JournalEntry::create(testDate(), "Owner investment",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(10, 0, usd)),
                                   JournalEntryLine::credit(equity.id(), Money::fromMajorUnits(10, 0, usd)),
                               }),
         chart, ledger);

    Account& bank = addChildAccount(chart, ledger, assets, AccountCode("1200"), "Bank");
    EXPECT_EQ(bank.parent(), &assets);
    EXPECT_EQ(assets.children().size(), 2u);
}

TEST(PostingEngineTest, RejectedChildCreationLeavesChartAndLedgerUnchanged) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    PostedPair accounts = setUpPostedCashAndCapital(chart, ledger, Money::fromMajorUnits(500, 0, usd));
    const AccountId cashId = accounts.cash->id();
    const std::size_t historyBefore = ledger.postedEntries().size();
    const Money cashBefore = ledger.balance(cashId);
    const Money equityBefore = ledger.balance(accounts.equity->id());

    EXPECT_THROW(addChildAccount(chart, ledger, *accounts.cash, AccountCode("1010"), "Petty cash"),
                 PostedAccountCannotBecomeGroupException);

    // Chart: no new account, no new child, parent still a leaf root.
    EXPECT_FALSE(chart.contains(AccountCode("1010")));
    EXPECT_TRUE(accounts.cash->isLeaf());
    EXPECT_TRUE(accounts.cash->children().empty());
    EXPECT_EQ(chart.rootAccounts().size(), 2u);
    EXPECT_EQ(chart.findById(cashId), accounts.cash);
    // Ledger: untouched.
    EXPECT_EQ(ledger.postedEntries().size(), historyBefore);
    EXPECT_EQ(ledger.balance(cashId), cashBefore);
    EXPECT_EQ(ledger.balance(accounts.equity->id()), equityBefore);
    // The account is still a valid posting target afterwards.
    post(JournalEntry::create(testDate(), "Follow-up",
                               {
                                   JournalEntryLine::debit(cashId, Money::fromMajorUnits(1, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity->id(), Money::fromMajorUnits(1, 0, usd)),
                               }),
         chart, ledger);
    EXPECT_EQ(ledger.balance(cashId), Money::fromMajorUnits(501, 0, usd));
}

TEST(PostingEngineTest, AddChildAccountStillEnforcesChartLevelRules) {
    Currency usd("USD");
    ChartOfAccounts chart;
    ChartOfAccounts otherChart;
    Ledger ledger(usd);
    setUpPostedCashAndCapital(chart, ledger, Money::fromMajorUnits(500, 0, usd));
    Account& receivables = chart.addRootAccount(AccountCode("1200"), "Receivables", AccountType::Asset);
    // Same AccountId value (1) as this chart's posted Cash account, but
    // foreign: must be rejected as foreign, not judged by this ledger.
    Account& foreign = otherChart.addRootAccount(AccountCode("9000"), "Foreign", AccountType::Asset);
    ASSERT_EQ(foreign.id(), chart.findByCode(AccountCode("1000"))->id());

    EXPECT_THROW(addChildAccount(chart, ledger, foreign, AccountCode("9100"), "Child"), ForeignAccountException);
    EXPECT_THROW(addChildAccount(chart, ledger, receivables, AccountCode("3000"), "Dup"),
                 DuplicateAccountCodeException);
    EXPECT_TRUE(receivables.isLeaf());
}

TEST(PostingEngineTest, AddChildAccountDefersEmptyNameValidationToDomain) {
    // The posting layer adds only the history check; Account's own
    // non-empty-name rule still fires from the domain, unchanged, and the
    // rejected attempt leaves the parent a leaf.
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    Account& receivables = chart.addRootAccount(AccountCode("1200"), "Receivables", AccountType::Asset);

    EXPECT_THROW(addChildAccount(chart, ledger, receivables, AccountCode("1210"), ""), InvalidAccountException);
    EXPECT_TRUE(receivables.isLeaf());
    EXPECT_FALSE(chart.contains(AccountCode("1210")));
}

// ---------------------------------------------------------------------
// Closing-kind entries: shape validated before commit
// ---------------------------------------------------------------------

namespace {

std::chrono::system_clock::time_point day(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * n);
}

struct ClosingChart {
    ChartOfAccounts chart;
    AccountId cash{0};
    AccountId sales{0};
    AccountId retained{0};
    AccountId capital{0};
    AccountId otherEquity{0};
    AccountId rent{0};
    ClosingChart() {
        cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
        capital = chart.addRootAccount(AccountCode("3000"), "Capital", AccountType::Equity).id();
        retained = chart.addRootAccount(AccountCode("3100"), "Retained", AccountType::Equity).id();
        otherEquity = chart.addRootAccount(AccountCode("3200"), "Other Equity", AccountType::Equity).id();
        sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
        rent = chart.addRootAccount(AccountCode("5000"), "Rent", AccountType::Expense).id();
    }
};

// ClosingChart with Capital 1000 invested on day 1 and Sales 100 earned on
// day 10 -- one open temporary balance (Sales, credit 100) to close.
struct OpenYear : ClosingChart {
    Currency usd{"USD"};
    Ledger ledger{Currency("USD")};
    OpenYear() {
        postStandard(day(1), cash, capital, 1000);
        postStandard(day(10), cash, sales, 100);
    }
    void postStandard(std::chrono::system_clock::time_point date, AccountId debit, AccountId credit,
                      std::int64_t major) {
        post(JournalEntry::create(date, "Activity",
                                  {JournalEntryLine::debit(debit, Money::fromMajorUnits(major, 0, usd)),
                                   JournalEntryLine::credit(credit, Money::fromMajorUnits(major, 0, usd))}),
             chart, ledger);
    }
    JournalEntryLine dr(AccountId id, std::int64_t major) const {
        return JournalEntryLine::debit(id, Money::fromMajorUnits(major, 0, usd));
    }
    JournalEntryLine cr(AccountId id, std::int64_t major) const {
        return JournalEntryLine::credit(id, Money::fromMajorUnits(major, 0, usd));
    }
    // Posts a closing-kind entry dated day(364) (cutoff day(364) + 1us).
    void postClosing(std::vector<JournalEntryLine> lines) {
        post(JournalEntry::createClosing(day(364), "Close", std::move(lines)), chart, ledger);
    }
    std::size_t historySize() const { return ledger.postedEntries().size(); }
};



} // namespace

TEST(PostingEngineTest, ValidClosingEntryPostsAndKeepsItsKind) {
    Currency usd("USD");
    ClosingChart c;
    Ledger ledger(usd);
    post(JournalEntry::create(testDate(), "Sale",
                              {JournalEntryLine::debit(c.cash, Money::fromMajorUnits(10, 0, usd)),
                               JournalEntryLine::credit(c.sales, Money::fromMajorUnits(10, 0, usd))}),
         c.chart, ledger);

    post(JournalEntry::createClosing(testDate(), "Close",
                                     {JournalEntryLine::debit(c.sales, Money::fromMajorUnits(10, 0, usd)),
                                      JournalEntryLine::credit(c.retained, Money::fromMajorUnits(10, 0, usd))}),
         c.chart, ledger);

    EXPECT_TRUE(ledger.postedEntries().back().entry().isClosing());
    EXPECT_TRUE(ledger.balance(c.sales).isZero());
    EXPECT_EQ(ledger.balance(c.retained), Money::fromMajorUnits(10, 0, usd));
}

TEST(PostingEngineTest, ClosingEntryTouchingAnAssetIsRejectedWithoutMutation) {
    // The Closing marker excludes an entry from income statements, so it
    // must never be allowed to hide an ordinary asset movement.
    Currency usd("USD");
    ClosingChart c;
    Ledger ledger(usd);

    EXPECT_THROW(post(JournalEntry::createClosing(
                          testDate(), "Not a close",
                          {JournalEntryLine::debit(c.cash, Money::fromMajorUnits(10, 0, usd)),
                           JournalEntryLine::credit(c.sales, Money::fromMajorUnits(10, 0, usd))}),
                      c.chart, ledger),
                 InvalidClosingEntryException);
    EXPECT_TRUE(ledger.postedEntries().empty());
    EXPECT_TRUE(ledger.balance(c.cash).isZero());
    EXPECT_TRUE(ledger.balance(c.sales).isZero());
}

TEST(PostingEngineTest, ClosingEntryWithNoTemporaryAccountIsRejected) {
    Currency usd("USD");
    ClosingChart c;
    Ledger ledger(usd);

    EXPECT_THROW(post(JournalEntry::createClosing(
                          testDate(), "Equity shuffle",
                          {JournalEntryLine::debit(c.capital, Money::fromMajorUnits(10, 0, usd)),
                           JournalEntryLine::credit(c.retained, Money::fromMajorUnits(10, 0, usd))}),
                      c.chart, ledger),
                 InvalidClosingEntryException);
    EXPECT_TRUE(ledger.postedEntries().empty());
}

TEST(PostingEngineTest, ClosingEntryInAnotherCurrencyIsRejected) {
    Currency usd("USD");
    Currency eur("EUR");
    ClosingChart c;
    Ledger ledger(usd);

    EXPECT_THROW(post(JournalEntry::createClosing(
                          testDate(), "Close",
                          {JournalEntryLine::debit(c.sales, Money::fromMajorUnits(10, 0, eur)),
                           JournalEntryLine::credit(c.retained, Money::fromMajorUnits(10, 0, eur))}),
                      c.chart, ledger),
                 LedgerCurrencyMismatchException);
    EXPECT_TRUE(ledger.postedEntries().empty());
}

// The audit's malformed-but-balanced "closing" entries. Each must be
// rejected before commit, leaving balances and history untouched.

TEST(PostingEngineTest, ClosingEntryWithTwoEquityDestinationsIsRejected) {
    OpenYear y;
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 40), y.cr(y.otherEquity, 60)}),
                 InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 2u);
    EXPECT_EQ(y.ledger.balance(y.sales), Money::fromMajorUnits(100, 0, y.usd));
    EXPECT_TRUE(y.ledger.balance(y.retained).isZero());
}

TEST(PostingEngineTest, ClosingEntryCarryingAnEquityToEquityTransferIsRejected) {
    OpenYear y;
    EXPECT_THROW(
        y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 100), y.dr(y.capital, 900), y.cr(y.otherEquity, 900)}),
        InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 2u);
    EXPECT_EQ(y.ledger.balance(y.capital), Money::fromMajorUnits(1000, 0, y.usd));
}

TEST(PostingEngineTest, PartialClosingEntryIsRejected) {
    OpenYear y;
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 30), y.cr(y.retained, 30)}), InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 2u);
    EXPECT_EQ(y.ledger.balance(y.sales), Money::fromMajorUnits(100, 0, y.usd));
}

TEST(PostingEngineTest, ReversedClosingEntryHidingRevenueIsRejected) {
    OpenYear y;
    EXPECT_THROW(y.postClosing({y.dr(y.retained, 500), y.cr(y.sales, 500)}), InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 2u);
    EXPECT_EQ(y.ledger.balance(y.sales), Money::fromMajorUnits(100, 0, y.usd));
}

TEST(PostingEngineTest, ClosingEntryThatLeavesAnotherTemporaryAccountNonZeroIsRejected) {
    // Dr Sales 100 / Cr Rent 100 zeroes Sales but drives Rent (which had no
    // balance) to a 100 credit balance that income statements would never see.
    OpenYear y;
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 100), y.cr(y.rent, 100)}), InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 2u);
    EXPECT_TRUE(y.ledger.balance(y.rent).isZero());
}

TEST(PostingEngineTest, ClosingEntryThatSkipsAnOpenTemporaryAccountIsRejected) {
    OpenYear y;
    y.postStandard(day(20), y.rent, y.cash, 30);  // Rent now has a debit balance too
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 100)}), InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 3u);
}

TEST(PostingEngineTest, ClosingEntryRepeatingAnAccountIsRejected) {
    OpenYear y;
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 60), y.dr(y.sales, 40), y.cr(y.retained, 100)}),
                 InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 2u);
}

TEST(PostingEngineTest, CompleteClosingIntoAnyEquityAccountIsAccepted) {
    // Which Equity account receives the result is the caller's choice;
    // closing::closeTemporaryAccounts() accepts any leaf Equity target too.
    OpenYear y;
    y.postClosing({y.dr(y.sales, 100), y.cr(y.otherEquity, 100)});
    EXPECT_TRUE(y.ledger.balance(y.sales).isZero());
    EXPECT_EQ(y.ledger.balance(y.otherEquity), Money::fromMajorUnits(100, 0, y.usd));
}

TEST(PostingEngineTest, OffsettingClosingWithoutEquityLineIsAcceptedOnlyWhenComplete) {
    OpenYear y;
    y.postStandard(day(20), y.rent, y.cash, 100);  // revenue 100, expense 100
    y.postClosing({y.dr(y.sales, 100), y.cr(y.rent, 100)});
    EXPECT_TRUE(y.ledger.balance(y.sales).isZero());
    EXPECT_TRUE(y.ledger.balance(y.rent).isZero());
    EXPECT_TRUE(y.ledger.balance(y.retained).isZero());
}

TEST(PostingEngineTest, ClosingCompletenessIgnoresActivityAtOrAfterTheCutoff) {
    OpenYear y;
    y.postStandard(day(400), y.cash, y.sales, 70);  // next period, posted first
    y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 100)});
    EXPECT_EQ(y.ledger.balance(y.sales), Money::fromMajorUnits(70, 0, y.usd));
    EXPECT_EQ(y.ledger.balance(y.retained), Money::fromMajorUnits(100, 0, y.usd));
}

TEST(PostingEngineTest, SecondClosingEntryForAnAlreadyClosedCutoffIsRejected) {
    OpenYear y;
    y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 100)});
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 100)}), InvalidClosingEntryException);
    EXPECT_EQ(y.historySize(), 3u);
}

// ---------------------------------------------------------------------
// Accounting-period lock: post() rejects entries dated in a Closed period
// ---------------------------------------------------------------------

namespace {

// OpenYear (Capital 1000 on day 1, Sales 100 on day 10) with the period
// [day 0, day 365) defined and closed -- its history was posted while open.
struct LockedYear : OpenYear {
    const ledgercore::domain::Period year{day(0), day(365)};
    LockedYear() {
        ledger.defineAccountingPeriod(year);
        ledger.closeAccountingPeriod(year);
    }
    void postSale(std::chrono::system_clock::time_point date, std::int64_t major) {
        postStandard(date, cash, sales, major);
    }
};

} // namespace

TEST(PostingEngineTest, PostingBeforeAClosedPeriodSucceeds) {
    OpenYear y;
    y.ledger.defineAccountingPeriod(ledgercore::domain::Period(day(100), day(200)));
    y.ledger.closeAccountingPeriod(ledgercore::domain::Period(day(100), day(200)));
    y.postStandard(day(99), y.cash, y.sales, 5);
    y.postStandard(day(100) - std::chrono::microseconds(1), y.cash, y.sales, 5);
    EXPECT_EQ(y.historySize(), 4u);
}

TEST(PostingEngineTest, PostingAtClosedPeriodStartIsRejected) {
    LockedYear y;
    EXPECT_THROW(y.postSale(day(0), 5), ClosedPeriodPostingException);
}

TEST(PostingEngineTest, PostingInsideClosedPeriodIsRejected) {
    LockedYear y;
    EXPECT_THROW(y.postSale(day(200), 5), ClosedPeriodPostingException);
    EXPECT_THROW(y.postSale(day(365) - std::chrono::microseconds(1), 5), ClosedPeriodPostingException);
}

TEST(PostingEngineTest, PostingAtClosedPeriodEndSucceedsBecauseEndIsExclusive) {
    LockedYear y;
    y.postSale(day(365), 5);
    EXPECT_EQ(y.historySize(), 3u);
    EXPECT_EQ(y.ledger.balance(y.sales), Money::fromMajorUnits(105, 0, y.usd));
}

TEST(PostingEngineTest, PostingAfterClosedPeriodSucceeds) {
    LockedYear y;
    y.postSale(day(400), 5);
    EXPECT_EQ(y.historySize(), 3u);
}

TEST(PostingEngineTest, RejectedBackdatedPostingLeavesLedgerHistoryAndPeriodUnchanged) {
    LockedYear y;
    const Money cashBefore = y.ledger.balance(y.cash);
    const Money salesBefore = y.ledger.balance(y.sales);
    const std::size_t historyBefore = y.historySize();

    try {
        y.postSale(day(50), 999);
        FAIL() << "expected ClosedPeriodPostingException";
    } catch (const ClosedPeriodPostingException& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("1970-02-20"), std::string::npos) << message;              // posting date (day 50)
        EXPECT_NE(message.find("[1970-01-01, 1971-01-01)"), std::string::npos) << message;  // closed period
    }

    EXPECT_EQ(y.ledger.balance(y.cash), cashBefore);
    EXPECT_EQ(y.ledger.balance(y.sales), salesBefore);
    EXPECT_EQ(y.historySize(), historyBefore);
    ASSERT_EQ(y.ledger.accountingPeriods().size(), 1u);
    EXPECT_TRUE(y.ledger.accountingPeriods()[0].isClosed());
}

TEST(PostingEngineTest, PeriodLockAndOtherValidationFailuresNeverPartiallyMutate) {
    LockedYear y;
    const std::size_t historyBefore = y.historySize();
    const Money cashBefore = y.ledger.balance(y.cash);
    // Unknown account, dated after the lock: fails for the other reason.
    EXPECT_THROW(post(JournalEntry::create(day(400), "Bad",
                                           {y.dr(y.cash, 5), JournalEntryLine::credit(AccountId(9999),
                                                                                      Money::fromMajorUnits(5, 0, y.usd))}),
                      y.chart, y.ledger),
                 AccountNotFoundException);
    // Unknown account *and* dated inside the lock: the lock is reported.
    EXPECT_THROW(post(JournalEntry::create(day(50), "Bad",
                                           {y.dr(y.cash, 5), JournalEntryLine::credit(AccountId(9999),
                                                                                      Money::fromMajorUnits(5, 0, y.usd))}),
                      y.chart, y.ledger),
                 ClosedPeriodPostingException);
    EXPECT_EQ(y.historySize(), historyBefore);
    EXPECT_EQ(y.ledger.balance(y.cash), cashBefore);
    EXPECT_TRUE(y.ledger.accountingPeriods()[0].isClosed());
}

TEST(PostingEngineTest, OpenPeriodDoesNotRestrictPosting) {
    OpenYear y;
    y.ledger.defineAccountingPeriod(ledgercore::domain::Period(day(0), day(365)));
    y.postStandard(day(50), y.cash, y.sales, 5);
    EXPECT_EQ(y.historySize(), 3u);
}

TEST(PostingEngineTest, MultipleClosedPeriodsEachRejectAndGapsAndOpenPeriodsAccept) {
    OpenYear y;
    using ledgercore::domain::Period;
    y.ledger.defineAccountingPeriod(Period(day(100), day(200)));  // closed
    y.ledger.defineAccountingPeriod(Period(day(200), day(300)));  // adjacent, stays open
    y.ledger.defineAccountingPeriod(Period(day(400), day(500)));  // closed, after a gap
    y.ledger.closeAccountingPeriod(Period(day(100), day(200)));
    y.ledger.closeAccountingPeriod(Period(day(400), day(500)));

    EXPECT_THROW(y.postStandard(day(150), y.cash, y.sales, 1), ClosedPeriodPostingException);
    EXPECT_THROW(y.postStandard(day(450), y.cash, y.sales, 1), ClosedPeriodPostingException);
    y.postStandard(day(250), y.cash, y.sales, 1);  // inside the open period
    y.postStandard(day(350), y.cash, y.sales, 1);  // between closed periods (gap)
    y.postStandard(day(500), y.cash, y.sales, 1);  // end of the latest closed period
    y.postStandard(day(600), y.cash, y.sales, 1);  // after every period
    EXPECT_EQ(y.historySize(), 6u);
}

TEST(PostingEngineTest, BackdatedClosingEntryIntoClosedPeriodIsRejected) {
    LockedYear y;
    // A complete, otherwise-valid closing entry dated inside the lock.
    EXPECT_THROW(y.postClosing({y.dr(y.sales, 100), y.cr(y.retained, 100)}), ClosedPeriodPostingException);
    EXPECT_EQ(y.historySize(), 2u);
    EXPECT_EQ(y.ledger.balance(y.sales), Money::fromMajorUnits(100, 0, y.usd));
}

// ---------------------------------------------------------------------
// Phase 20: O(1) posting-history check, deterministic closing errors
// ---------------------------------------------------------------------

TEST(LedgerPostingHistoryTest, HistoryIsFalseUntilPostedThenTrueForever) {
    OpenYear y;
    EXPECT_TRUE(y.ledger.hasPostingHistory(y.cash));
    EXPECT_TRUE(y.ledger.hasPostingHistory(y.capital));
    EXPECT_FALSE(y.ledger.hasPostingHistory(y.rent));
    EXPECT_FALSE(y.ledger.hasPostingHistory(AccountId(9999)));
    y.postStandard(day(20), y.rent, y.cash, 30);
    EXPECT_TRUE(y.ledger.hasPostingHistory(y.rent));
    y.postStandard(day(21), y.cash, y.rent, 30);  // rent back to a zero balance
    EXPECT_TRUE(y.ledger.balance(y.rent).isZero());
    EXPECT_TRUE(y.ledger.hasPostingHistory(y.rent));
}

TEST(LedgerPostingHistoryTest, AccountWhoseLinesNetToZeroWithinOneEntryHasHistory) {
    OpenYear y;
    post(JournalEntry::create(day(30), "Wash",
                              {y.dr(y.rent, 5), y.cr(y.rent, 5), y.dr(y.cash, 1), y.cr(y.otherEquity, 1)}),
         y.chart, y.ledger);
    EXPECT_TRUE(y.ledger.balance(y.rent).isZero());
    EXPECT_TRUE(y.ledger.hasPostingHistory(y.rent));
    EXPECT_THROW(addChildAccount(y.chart, y.ledger, *y.chart.findByCode(AccountCode("5000")),
                                 AccountCode("5010"), "Sub-rent"),
                 PostedAccountCannotBecomeGroupException);
}

TEST(LedgerPostingHistoryTest, HistoryStaysCorrectAcrossManyPostings) {
    OpenYear y;
    for (int i = 0; i < 2000; ++i) {
        y.postStandard(day(20) + std::chrono::minutes(i), y.cash, y.sales, 1);
    }
    EXPECT_TRUE(y.ledger.hasPostingHistory(y.sales));
    EXPECT_FALSE(y.ledger.hasPostingHistory(y.rent));
    EXPECT_FALSE(y.ledger.hasPostingHistory(y.retained));
}

TEST(PostingEngineTest, IncompleteClosingErrorNamesTheLowestCodeAccountRegardlessOfLineOrder) {
    // Sales (4000) and Rent (5000) both left non-zero; whichever order the
    // lines are written in, the message deterministically names 4000.
    for (int order = 0; order < 2; ++order) {
        OpenYear y;
        y.postStandard(day(20), y.rent, y.cash, 30);
        std::vector<JournalEntryLine> lines = order == 0
            ? std::vector<JournalEntryLine>{y.dr(y.sales, 40), y.cr(y.rent, 10), y.cr(y.retained, 30)}
            : std::vector<JournalEntryLine>{y.cr(y.retained, 30), y.cr(y.rent, 10), y.dr(y.sales, 40)};
        try {
            y.postClosing(std::move(lines));
            FAIL() << "expected an incomplete closing entry to be rejected";
        } catch (const InvalidClosingEntryException& e) {
            EXPECT_EQ(std::string(e.what()),
                      "A closing entry must bring every Revenue and Expense account to zero as of its cutoff; "
                      "account 4000 would be left at 60.00 USD");
        }
    }
}
