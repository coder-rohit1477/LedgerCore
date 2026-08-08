#include <gtest/gtest.h>

#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"

using ledgercore::domain::AccountId;
using ledgercore::domain::Currency;
using ledgercore::domain::CurrencyMismatchException;
using ledgercore::domain::DebitCreditSide;
using ledgercore::domain::InvalidJournalEntryException;
using ledgercore::domain::InvalidJournalEntryLineException;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::domain::UnbalancedJournalEntryException;

namespace {
std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}
} // namespace

// ---------------------------------------------------------------------
// JournalEntryLine: factories, validation, side exclusivity
// ---------------------------------------------------------------------

TEST(JournalEntryLineTest, DebitFactoryProducesDebitSide) {
    Currency usd("USD");
    JournalEntryLine line = JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd));

    EXPECT_EQ(line.side(), DebitCreditSide::Debit);
    EXPECT_TRUE(line.isDebit());
    EXPECT_FALSE(line.isCredit());
}

TEST(JournalEntryLineTest, CreditFactoryProducesCreditSide) {
    Currency usd("USD");
    JournalEntryLine line = JournalEntryLine::credit(AccountId(1), Money::fromMajorUnits(100, 0, usd));

    EXPECT_EQ(line.side(), DebitCreditSide::Credit);
    EXPECT_TRUE(line.isCredit());
    EXPECT_FALSE(line.isDebit());
}

TEST(JournalEntryLineTest, SideIsExclusiveForBothFactories) {
    Currency usd("USD");
    JournalEntryLine debitLine = JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(1, 0, usd));
    JournalEntryLine creditLine = JournalEntryLine::credit(AccountId(1), Money::fromMajorUnits(1, 0, usd));

    EXPECT_NE(debitLine.isDebit(), debitLine.isCredit());
    EXPECT_NE(creditLine.isDebit(), creditLine.isCredit());
}

TEST(JournalEntryLineTest, ZeroAmountThrows) {
    Currency usd("USD");
    EXPECT_THROW(JournalEntryLine::debit(AccountId(1), Money::zero(usd)), InvalidJournalEntryLineException);
    EXPECT_THROW(JournalEntryLine::credit(AccountId(1), Money::zero(usd)), InvalidJournalEntryLineException);
}

TEST(JournalEntryLineTest, NegativeAmountThrows) {
    Currency usd("USD");
    Money negative = Money::ofMinorUnits(-100, usd);
    EXPECT_THROW(JournalEntryLine::debit(AccountId(1), negative), InvalidJournalEntryLineException);
    EXPECT_THROW(JournalEntryLine::credit(AccountId(1), negative), InvalidJournalEntryLineException);
}

TEST(JournalEntryLineTest, AccountIdRoundTrips) {
    Currency usd("USD");
    AccountId id(42);
    JournalEntryLine line = JournalEntryLine::debit(id, Money::fromMajorUnits(1, 0, usd));
    EXPECT_EQ(line.accountId(), id);
}

TEST(JournalEntryLineTest, AmountPreservesCurrency) {
    Currency eur("EUR");
    JournalEntryLine line = JournalEntryLine::credit(AccountId(1), Money::fromMajorUnits(50, 0, eur));
    EXPECT_EQ(line.amount().currency(), eur);
}

// ---------------------------------------------------------------------
// JournalEntry: valid construction
// ---------------------------------------------------------------------

TEST(JournalEntryTest, ValidTwoLineBalancedEntry) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(100, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Cash sale", lines);

    EXPECT_EQ(entry.lines().size(), 2u);
    EXPECT_EQ(entry.totalDebits(), entry.totalCredits());
}

TEST(JournalEntryTest, ValidMultiLineBalancedEntry) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(60, 0, usd)),
        JournalEntryLine::debit(AccountId(2), Money::fromMajorUnits(40, 0, usd)),
        JournalEntryLine::credit(AccountId(3), Money::fromMajorUnits(100, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Split debit entry", lines);

    EXPECT_EQ(entry.totalDebits(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(entry.totalCredits(), Money::fromMajorUnits(100, 0, usd));
}

TEST(JournalEntryTest, DebitSplitAcrossMultipleCredits) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(60, 0, usd)),
        JournalEntryLine::credit(AccountId(3), Money::fromMajorUnits(40, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Payment split across two accounts", lines);

    EXPECT_EQ(entry.totalDebits(), Money::fromMajorUnits(100, 0, usd));
    EXPECT_EQ(entry.totalCredits(), Money::fromMajorUnits(100, 0, usd));
}

// ---------------------------------------------------------------------
// JournalEntry: rejected construction
// ---------------------------------------------------------------------

TEST(JournalEntryTest, UnbalancedEntryThrows) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(99, 99, usd)),
    };

    EXPECT_THROW(JournalEntry::create(testDate(), "Off by one cent", lines), UnbalancedJournalEntryException);
}

TEST(JournalEntryTest, ZeroLinesThrows) {
    std::vector<JournalEntryLine> lines;
    EXPECT_THROW(JournalEntry::create(testDate(), "Empty entry", lines), InvalidJournalEntryException);
}

TEST(JournalEntryTest, OneLineThrows) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd)),
    };

    EXPECT_THROW(JournalEntry::create(testDate(), "Single line", lines), InvalidJournalEntryException);
}

TEST(JournalEntryTest, EmptyDescriptionThrows) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(100, 0, usd)),
    };

    EXPECT_THROW(JournalEntry::create(testDate(), "", lines), InvalidJournalEntryException);
}

TEST(JournalEntryTest, CrossCurrencyLinesThrows) {
    Currency usd("USD");
    Currency eur("EUR");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(100, 0, eur)),
    };

    EXPECT_THROW(JournalEntry::create(testDate(), "Mismatched currencies", lines), CurrencyMismatchException);
}

// ---------------------------------------------------------------------
// JournalEntry: same currency, duplicate accounts
// ---------------------------------------------------------------------

TEST(JournalEntryTest, SameCurrencyLinesSucceed) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(10, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(5, 0, usd)),
        JournalEntryLine::credit(AccountId(3), Money::fromMajorUnits(5, 0, usd)),
    };

    EXPECT_NO_THROW(JournalEntry::create(testDate(), "All USD", lines));
}

TEST(JournalEntryTest, DuplicateAccountIdLinesAreAllowed) {
    Currency usd("USD");
    AccountId sameAccount(7);
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(sameAccount, Money::fromMajorUnits(50, 0, usd)),
        JournalEntryLine::credit(sameAccount, Money::fromMajorUnits(50, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Self-offsetting adjustment", lines);
    EXPECT_EQ(entry.lines().size(), 2u);
    EXPECT_EQ(entry.lines()[0].accountId(), entry.lines()[1].accountId());
}

// ---------------------------------------------------------------------
// JournalEntry: accessors
// ---------------------------------------------------------------------

TEST(JournalEntryTest, TotalDebitsReflectsSumOfDebitLines) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(30, 0, usd)),
        JournalEntryLine::debit(AccountId(2), Money::fromMajorUnits(20, 0, usd)),
        JournalEntryLine::credit(AccountId(3), Money::fromMajorUnits(50, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Two debits", lines);
    EXPECT_EQ(entry.totalDebits(), Money::fromMajorUnits(50, 0, usd));
}

TEST(JournalEntryTest, TotalCreditsReflectsSumOfCreditLines) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(50, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(30, 0, usd)),
        JournalEntryLine::credit(AccountId(3), Money::fromMajorUnits(20, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Two credits", lines);
    EXPECT_EQ(entry.totalCredits(), Money::fromMajorUnits(50, 0, usd));
}

TEST(JournalEntryTest, CurrencyReflectsLinesCurrency) {
    Currency eur("EUR");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(10, 0, eur)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(10, 0, eur)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Euro entry", lines);
    EXPECT_EQ(entry.currency(), eur);
}

TEST(JournalEntryTest, ValidEntryTotalsAreNeverZero) {
    // Every line is strictly positive and both sides must be non-empty
    // to balance, so a valid entry's totals can never be zero.
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(1, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(1, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Minimal entry", lines);
    EXPECT_TRUE(entry.totalDebits().isPositive());
    EXPECT_TRUE(entry.totalCredits().isPositive());
}

// ---------------------------------------------------------------------
// JournalEntry: immutability
// ---------------------------------------------------------------------

TEST(JournalEntryTest, LinesAccessorReturnsStableReference) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(10, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(10, 0, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Stable reference check", lines);
    const std::vector<JournalEntryLine>& first = entry.lines();
    const std::vector<JournalEntryLine>& second = entry.lines();
    EXPECT_EQ(&first, &second);
}

TEST(JournalEntryTest, CopyIsIndependentAndEqualInContent) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(10, 0, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(10, 0, usd)),
    };

    JournalEntry original = JournalEntry::create(testDate(), "Original", lines);
    JournalEntry copy = original;

    EXPECT_EQ(copy.description(), original.description());
    EXPECT_EQ(copy.totalDebits(), original.totalDebits());
    EXPECT_EQ(copy.totalCredits(), original.totalCredits());
    EXPECT_EQ(copy.lines().size(), original.lines().size());
}

// ---------------------------------------------------------------------
// Exact arithmetic and scale
// ---------------------------------------------------------------------

TEST(JournalEntryTest, ExactMoneyArithmeticAcrossManySmallLines) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(100, 10, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(100, 9, usd)),
        JournalEntryLine::credit(AccountId(3), Money::ofMinorUnits(1, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Exact split", lines);
    EXPECT_EQ(entry.totalDebits(), entry.totalCredits());
    EXPECT_EQ(entry.totalDebits(), Money::fromMajorUnits(100, 10, usd));
}

TEST(JournalEntryTest, ClassicFloatingPointDriftCaseBalancesExactly) {
    // 0.1 + 0.2 != 0.3 in binary floating point. In minor-unit integers,
    // this is exact: 10 + 20 == 30.
    Currency usd("USD");
    std::vector<JournalEntryLine> lines{
        JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(0, 30, usd)),
        JournalEntryLine::credit(AccountId(2), Money::fromMajorUnits(0, 10, usd)),
        JournalEntryLine::credit(AccountId(3), Money::fromMajorUnits(0, 20, usd)),
    };

    JournalEntry entry = JournalEntry::create(testDate(), "Float drift regression", lines);
    EXPECT_EQ(entry.totalDebits(), Money::fromMajorUnits(0, 30, usd));
    EXPECT_EQ(entry.totalCredits(), Money::fromMajorUnits(0, 30, usd));
}

TEST(JournalEntryTest, LargeNumberOfLinesBalancesExactly) {
    Currency usd("USD");
    std::vector<JournalEntryLine> lines;
    lines.push_back(JournalEntryLine::debit(AccountId(1), Money::fromMajorUnits(10, 0, usd)));
    for (int i = 0; i < 1000; ++i) {
        lines.push_back(JournalEntryLine::credit(AccountId(2), Money::ofMinorUnits(1, usd)));
    }

    JournalEntry entry = JournalEntry::create(testDate(), "One large debit vs 1000 small credits", lines);
    EXPECT_EQ(entry.lines().size(), 1001u);
    EXPECT_EQ(entry.totalDebits(), Money::fromMajorUnits(10, 0, usd));
    EXPECT_EQ(entry.totalCredits(), Money::fromMajorUnits(10, 0, usd));
}

// ---------------------------------------------------------------------
// Property-style tests (lightweight generators over GoogleTest, per the
// Phase 0 decision to defer a dedicated property-based testing library)
// ---------------------------------------------------------------------

TEST(JournalEntryPropertyTest, RandomlySplitBalancedEntriesAlwaysBalance) {
    Currency usd("USD");
    std::mt19937_64 rng(46);
    std::uniform_int_distribution<std::int64_t> totalDist(1, 1'000'000);
    std::uniform_int_distribution<int> splitDist(1, 5);

    for (int trial = 0; trial < 100; ++trial) {
        std::int64_t total = totalDist(rng);
        std::vector<JournalEntryLine> lines;
        lines.push_back(JournalEntryLine::debit(AccountId(1), Money::ofMinorUnits(total, usd)));

        int creditLineCount = splitDist(rng);
        std::int64_t remaining = total;
        std::uint64_t nextAccountId = 2;
        for (int i = 0; i < creditLineCount - 1; ++i) {
            std::int64_t share = remaining / (creditLineCount - i);
            if (share < 1) {
                share = 1;
            }
            lines.push_back(JournalEntryLine::credit(AccountId(nextAccountId++), Money::ofMinorUnits(share, usd)));
            remaining -= share;
        }
        lines.push_back(JournalEntryLine::credit(AccountId(nextAccountId), Money::ofMinorUnits(remaining, usd)));

        JournalEntry entry = JournalEntry::create(testDate(), "Randomly split entry", lines);

        EXPECT_EQ(entry.totalDebits(), entry.totalCredits());
        EXPECT_TRUE(entry.totalDebits().isPositive());
        for (const JournalEntryLine& line : entry.lines()) {
            EXPECT_TRUE(line.amount().isPositive());
        }
    }
}

TEST(JournalEntryPropertyTest, PerturbedEntriesAlwaysRejectedAsUnbalanced) {
    Currency usd("USD");
    std::mt19937_64 rng(47);
    std::uniform_int_distribution<std::int64_t> amountDist(1, 1'000'000);
    std::uniform_int_distribution<std::int64_t> perturbDist(1, 1000);

    for (int trial = 0; trial < 100; ++trial) {
        std::int64_t amount = amountDist(rng);
        std::int64_t perturbedAmount = amount + perturbDist(rng);

        std::vector<JournalEntryLine> lines{
            JournalEntryLine::debit(AccountId(1), Money::ofMinorUnits(amount, usd)),
            JournalEntryLine::credit(AccountId(2), Money::ofMinorUnits(perturbedAmount, usd)),
        };

        EXPECT_THROW(JournalEntry::create(testDate(), "Perturbed entry", lines), UnbalancedJournalEntryException);
    }
}
