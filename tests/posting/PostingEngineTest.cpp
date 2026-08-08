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
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::domain::MoneyOverflowException;
using ledgercore::ledger::Ledger;
using ledgercore::ledger::LedgerCurrencyMismatchException;
using ledgercore::ledger::PostedJournalEntry;
using ledgercore::ledger::PostingId;
using ledgercore::posting::AccountNotFoundException;
using ledgercore::posting::DebitCreditAmounts;
using ledgercore::posting::debitCreditPresentation;
using ledgercore::posting::InvalidPostingTargetException;
using ledgercore::posting::post;

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
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

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
// Trial Balance derivation helper
// ---------------------------------------------------------------------

TEST(DebitCreditPresentationTest, AllAccountTypeSignCombinationsDeriveCorrectColumn) {
    Currency usd("USD");
    struct Case {
        AccountType type;
        bool debitNormal;
    };
    const Case cases[] = {
        {AccountType::Asset, true},
        {AccountType::Expense, true},
        {AccountType::Liability, false},
        {AccountType::Equity, false},
        {AccountType::Revenue, false},
    };

    const Money positive = Money::fromMajorUnits(100, 0, usd);
    const Money negative = Money::fromMajorUnits(-100, 0, usd);

    for (const Case& testCase : cases) {
        const DebitCreditAmounts positiveResult = debitCreditPresentation(testCase.type, positive);
        const DebitCreditAmounts negativeResult = debitCreditPresentation(testCase.type, negative);

        if (testCase.debitNormal) {
            EXPECT_EQ(positiveResult.debit, positive);
            EXPECT_TRUE(positiveResult.credit.isZero());
            EXPECT_EQ(negativeResult.credit, positive);
            EXPECT_TRUE(negativeResult.debit.isZero());
        } else {
            EXPECT_EQ(positiveResult.credit, positive);
            EXPECT_TRUE(positiveResult.debit.isZero());
            EXPECT_EQ(negativeResult.debit, positive);
            EXPECT_TRUE(negativeResult.credit.isZero());
        }
    }
}

TEST(DebitCreditPresentationTest, ZeroBalanceProducesZeroInBothColumns) {
    Currency usd("USD");
    const DebitCreditAmounts result = debitCreditPresentation(AccountType::Asset, Money::zero(usd));
    EXPECT_TRUE(result.debit.isZero());
    EXPECT_TRUE(result.credit.isZero());
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

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
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& group = chart.addRootAccount(AccountCode("2000"), "Liabilities", AccountType::Liability);
    chart.addChildAccount(group, AccountCode("2100"), "Accounts Payable");
    Ledger ledger(usd);

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
