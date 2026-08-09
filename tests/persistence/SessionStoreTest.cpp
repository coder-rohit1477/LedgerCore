// Tests here verify persistence's own concerns -- round-trip fidelity,
// file-format corruption handling, atomicity, and determinism -- not
// accounting invariants already owned by the domain/ledger/posting/
// trialbalance/reporting test suites. Where a persisted record is
// syntactically well-formed but violates an accounting rule (unbalanced
// entry, duplicate code, unknown account, bad formula), these tests only
// confirm the *existing* exception type propagates; they do not re-derive
// why that rule exists.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/computed/LedgerAccountResolver.h"
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
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/persistence/PersistenceExceptions.h"
#include "ledgercore/persistence/SessionStore.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/posting/PostingExceptions.h"
#include "ledgercore/reporting/BalanceSheet.h"
#include "ledgercore/reporting/IncomeStatement.h"
#include "ledgercore/trialbalance/TrialBalance.h"

using ledgercore::computed::ComputedAccountRegistry;
using ledgercore::computed::LedgerAccountResolver;
using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::domain::Period;
using ledgercore::formula::ComputedAccountName;
using ledgercore::ledger::Ledger;
using ledgercore::persistence::LoadedSession;
using ledgercore::persistence::PersistenceException;
using ledgercore::persistence::PersistenceFormatException;
using ledgercore::persistence::PersistenceVersionException;
using ledgercore::posting::post;
using ledgercore::reporting::BalanceSheet;
using ledgercore::reporting::IncomeStatement;
using ledgercore::trialbalance::TrialBalance;

namespace {

// ---------------------------------------------------------------------
// Fixtures / helpers
// ---------------------------------------------------------------------

std::filesystem::path uniqueTempPath(const std::string& label) {
    static int counter = 0;
    ++counter;
    return std::filesystem::temp_directory_path()
           / ("ledgercore_persistence_test_" + label + "_" + std::to_string(::getpid()) + "_"
              + std::to_string(counter) + ".snapshot");
}

class ScopedTempFile {
public:
    explicit ScopedTempFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~ScopedTempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(path_.string() + ".tmp", ec);
    }
    ScopedTempFile(const ScopedTempFile&) = delete;
    ScopedTempFile& operator=(const ScopedTempFile&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}

std::chrono::system_clock::time_point day(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * n);
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

void writeRawFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

std::string readRawFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// A minimal, valid one-account, one-entry snapshot body used as a base for
// hand-crafted corruption tests (everything up to and including a
// well-formed CURRENCY line, so corruption tests only need to append or
// substitute the part they're actually testing).
std::string validHeaderAndCurrency() {
    return "LEDGERCORE-SNAPSHOT v1\nCURRENCY USD\n";
}

void expectFlatAccountsMatch(const ChartOfAccounts& original, const ChartOfAccounts& reloaded,
                              const std::vector<std::string>& codes) {
    for (const std::string& codeText : codes) {
        const AccountCode code(codeText);
        const Account* originalAccount = original.findByCode(code);
        const Account* reloadedAccount = reloaded.findByCode(code);
        ASSERT_NE(originalAccount, nullptr) << codeText;
        ASSERT_NE(reloadedAccount, nullptr) << codeText;
        EXPECT_EQ(originalAccount->name(), reloadedAccount->name()) << codeText;
        EXPECT_EQ(originalAccount->type(), reloadedAccount->type()) << codeText;
        EXPECT_EQ(originalAccount->isLeaf(), reloadedAccount->isLeaf()) << codeText;
        EXPECT_EQ(originalAccount->isRoot(), reloadedAccount->isRoot()) << codeText;
    }
}

} // namespace

// ---------------------------------------------------------------------
// 1-5: Basic round trips
// ---------------------------------------------------------------------

TEST(SessionStoreTest, EmptySessionRoundTrips) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("empty"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());

    LoadedSession loaded = ledgercore::persistence::load(file.path());
    EXPECT_TRUE(loaded.chart->rootAccounts().empty());
    EXPECT_TRUE(loaded.ledger->postedEntries().empty());
    EXPECT_TRUE(loaded.computedAccounts->definitions().empty());
    EXPECT_EQ(loaded.ledger->currency(), usd);
}

TEST(SessionStoreTest, RootAccountsRoundTrip) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("roots"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    expectFlatAccountsMatch(chart, *loaded.chart, {"1000", "2000", "3000", "4000", "5000"});
}

TEST(SessionStoreTest, MultiLevelChartHierarchyRoundTrips) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& current = chart.addChildAccount(assets, AccountCode("1100"), "Current Assets");
    chart.addChildAccount(current, AccountCode("1110"), "Cash");
    chart.addChildAccount(current, AccountCode("1120"), "Accounts Receivable");
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("multilevel"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    expectFlatAccountsMatch(chart, *loaded.chart, {"1000", "1100", "1110", "1120"});

    const Account* reloadedCash = loaded.chart->findByCode(AccountCode("1110"));
    ASSERT_NE(reloadedCash, nullptr);
    ASSERT_NE(reloadedCash->parent(), nullptr);
    EXPECT_EQ(reloadedCash->parent()->code().value(), "1100");
    ASSERT_NE(reloadedCash->parent()->parent(), nullptr);
    EXPECT_EQ(reloadedCash->parent()->parent()->code().value(), "1000");
}

TEST(SessionStoreTest, LeafAndGroupStructurePreserved) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("leafgroup"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const Account* reloadedAssets = loaded.chart->findByCode(AccountCode("1000"));
    const Account* reloadedCash = loaded.chart->findByCode(AccountCode("1110"));
    ASSERT_NE(reloadedAssets, nullptr);
    ASSERT_NE(reloadedCash, nullptr);
    EXPECT_FALSE(reloadedAssets->isLeaf());
    EXPECT_TRUE(reloadedCash->isLeaf());
}

TEST(SessionStoreTest, SingleCurrencyConstraintIsPreserved) {
    // Ledger is fixed to exactly one Currency at construction (no public
    // API exists to change it), so a session's file records exactly one
    // CURRENCY line and reload reconstructs a Ledger with that same
    // single currency -- there is no "multiple currencies in one session"
    // case to support.
    Currency eur("EUR");
    ChartOfAccounts chart;
    Ledger ledger(eur);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("currency"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    EXPECT_EQ(loaded.ledger->currency(), eur);
    EXPECT_NE(readRawFile(file.path()).find("CURRENCY EUR"), std::string::npos);
}

// ---------------------------------------------------------------------
// 6-14: Journal
// ---------------------------------------------------------------------

TEST(SessionStoreTest, SingleBalancedEntryRoundTrips) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Investment",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1000, 0, usd)),
                                   JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(1000, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("single_entry"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 1u);
    const AccountId reloadedCash = loaded.chart->findByCode(AccountCode("1000"))->id();
    EXPECT_EQ(loaded.ledger->balance(reloadedCash), Money::fromMajorUnits(1000, 0, usd));
}

TEST(SessionStoreTest, MultipleEntriesRoundTrip) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Sale 1",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Sale 2",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(50, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(50, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("multi_entry"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 2u);
    const AccountId reloadedCash = loaded.chart->findByCode(AccountCode("1000"))->id();
    EXPECT_EQ(loaded.ledger->balance(reloadedCash), Money::fromMajorUnits(150, 0, usd));
}

TEST(SessionStoreTest, SplitDebitCreditLinesRoundTrip) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& ar = chart.addRootAccount(AccountCode("1100"), "Accounts Receivable", AccountType::Asset);
    Account& payable = chart.addRootAccount(AccountCode("2000"), "Accounts Payable", AccountType::Liability);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Split",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(300, 0, usd)),
                                   JournalEntryLine::debit(ar.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::credit(payable.id(), Money::fromMajorUnits(500, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("split_lines"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 1u);
    EXPECT_EQ(loaded.ledger->postedEntries().front().entry().lines().size(), 3u);
}

TEST(SessionStoreTest, NegativeBalanceRoundTrips) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Overdraw",
                               {
                                   JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(200, 0, usd)),
                                   JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(200, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("negative_balance"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const AccountId reloadedCash = loaded.chart->findByCode(AccountCode("1000"))->id();
    const Money balance = loaded.ledger->balance(reloadedCash);
    EXPECT_TRUE(balance.isNegative());
    EXPECT_EQ(balance, Money::fromMajorUnits(-200, 0, usd));
}

TEST(SessionStoreTest, LargeMoneyValuesRoundTripExactly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& equity = chart.addRootAccount(AccountCode("3000"), "Equity", AccountType::Equity);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    constexpr std::int64_t kLarge = 9000000000000000000LL;
    post(JournalEntry::create(testDate(), "Large",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::ofMinorUnits(kLarge, usd)),
                                   JournalEntryLine::credit(equity.id(), Money::ofMinorUnits(kLarge, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("large_money"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const AccountId reloadedCash = loaded.chart->findByCode(AccountCode("1000"))->id();
    EXPECT_EQ(loaded.ledger->balance(reloadedCash), Money::ofMinorUnits(kLarge, usd));
}

TEST(SessionStoreTest, ExactMinorUnitPreservationAcrossManyValues) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const std::vector<std::int64_t> values{1, 99, 100, 12345, -1, -99,
                                            std::numeric_limits<std::int64_t>::max() / 4,
                                            -(std::numeric_limits<std::int64_t>::max() / 4)};
    for (std::int64_t value : values) {
        const Money amount = Money::ofMinorUnits(value, usd);
        if (value >= 0) {
            post(JournalEntry::create(testDate(), "line",
                                       {JournalEntryLine::debit(cash.id(), amount.isZero() ? Money::ofMinorUnits(1, usd) : amount),
                                        JournalEntryLine::credit(revenue.id(), amount.isZero() ? Money::ofMinorUnits(1, usd) : amount)}),
                 chart, ledger);
        } else {
            const Money magnitude = Money::ofMinorUnits(-value, usd);
            post(JournalEntry::create(testDate(), "line",
                                       {JournalEntryLine::credit(cash.id(), magnitude), JournalEntryLine::debit(revenue.id(), magnitude)}),
                 chart, ledger);
        }
    }

    const ScopedTempFile file(uniqueTempPath("exact_minor_units"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const AccountId reloadedCash = loaded.chart->findByCode(AccountCode("1000"))->id();
    const AccountId liveCash = cash.id();
    EXPECT_EQ(loaded.ledger->balance(reloadedCash), ledger.balance(liveCash));
}

TEST(SessionStoreTest, OriginalEntryOrderIsPreservedNotSorted) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    // Post out of date order deliberately (backdated second entry) --
    // persistence must preserve *posting* order, not re-sort by date.
    post(JournalEntry::create(day(10), "Later business date, posted first",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(10, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(10, 0, usd)),
                               }),
         chart, ledger);
    post(JournalEntry::create(day(1), "Earlier business date, posted second",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(20, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(20, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("order_preserved"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 2u);
    EXPECT_EQ(loaded.ledger->postedEntries()[0].entry().description(), "Later business date, posted first");
    EXPECT_EQ(loaded.ledger->postedEntries()[1].entry().description(), "Earlier business date, posted second");
    EXPECT_EQ(loaded.ledger->postedEntries()[0].entry().date(), day(10));
    EXPECT_EQ(loaded.ledger->postedEntries()[1].entry().date(), day(1));
}

TEST(SessionStoreTest, FullPrecisionDateRoundTripsExactly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const auto preciseDate = std::chrono::system_clock::now();
    post(JournalEntry::create(preciseDate, "Precise",
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(1, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("precise_date"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 1u);
    // Nanosecond-count round trip: exact whenever the platform's native
    // system_clock resolution is at or finer than nanoseconds (true for
    // this project's actual build/runtime platform).
    const auto reloadedDate = loaded.ledger->postedEntries().front().entry().date();
    const auto originalNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(preciseDate.time_since_epoch());
    const auto reloadedNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(reloadedDate.time_since_epoch());
    EXPECT_EQ(originalNanos, reloadedNanos);
}

TEST(SessionStoreTest, DescriptionsWithSpacesQuotesAndBackslashesRoundTrip) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const std::string description = R"(Sold "widgets" via C:\invoices\path and a plain space)";
    post(JournalEntry::create(testDate(), description,
                               {
                                   JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1, 0, usd)),
                                   JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(1, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("quoted_description"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 1u);
    EXPECT_EQ(loaded.ledger->postedEntries().front().entry().description(), description);
}

// ---------------------------------------------------------------------
// 15-18: Accounts
// ---------------------------------------------------------------------

TEST(SessionStoreTest, AccountNamesWithSpacesRoundTrip) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Petty Cash and Equivalents", AccountType::Asset);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("name_spaces"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const Account* reloaded = loaded.chart->findByCode(AccountCode("1000"));
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->name(), "Petty Cash and Equivalents");
}

TEST(SessionStoreTest, DeterministicAccountOrderMatchesInsertionOrder) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("3000"), "Third", AccountType::Equity);
    chart.addRootAccount(AccountCode("1000"), "First", AccountType::Asset);
    chart.addRootAccount(AccountCode("2000"), "Second", AccountType::Liability);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("account_order"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());

    const std::string content = readRawFile(file.path());
    const std::size_t posThird = content.find("ACCOUNT ROOT 3000");
    const std::size_t posFirst = content.find("ACCOUNT ROOT 1000");
    const std::size_t posSecond = content.find("ACCOUNT ROOT 2000");
    ASSERT_NE(posThird, std::string::npos);
    ASSERT_NE(posFirst, std::string::npos);
    ASSERT_NE(posSecond, std::string::npos);
    // Insertion order (3000, 1000, 2000), not code-sorted order.
    EXPECT_LT(posThird, posFirst);
    EXPECT_LT(posFirst, posSecond);
}

TEST(SessionStoreTest, JournalLinesResolveByAccountCodeNotRawId) {
    Currency usd("USD");
    ChartOfAccounts chart;
    // Create accounts in an order that gives "4000" a *different* raw
    // AccountId than it would get if this file were the only history --
    // the point of this test is that resolution goes through AccountCode
    // regardless of what numeric id ends up assigned.
    chart.addRootAccount(AccountCode("9999"), "Unused filler", AccountType::Asset);
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(75, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(75, 0, usd)),
                               }),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("resolve_by_code"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const AccountId reloadedCash = loaded.chart->findByCode(AccountCode("1000"))->id();
    const AccountId reloadedRevenue = loaded.chart->findByCode(AccountCode("4000"))->id();
    EXPECT_EQ(loaded.ledger->balance(reloadedCash), Money::fromMajorUnits(75, 0, usd));
    // Revenue is normal-credit; a credit line increases it, so its balance
    // is +75, not -75.
    EXPECT_EQ(loaded.ledger->balance(reloadedRevenue), Money::fromMajorUnits(75, 0, usd));
}

TEST(SessionStoreTest, AccountIdsAreRegeneratedNotPersisted) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile file(uniqueTempPath("ids_regenerated"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());

    // The file itself never encodes an AccountId -- only AccountCode.
    const std::string content = readRawFile(file.path());
    EXPECT_EQ(content.find("AccountId"), std::string::npos);

    // Two independent loads of the same file deterministically derive the
    // *same* fresh id sequence from file order (1, 2, 3, ...) -- proving
    // ids are mechanically re-derived from replay order, never read back
    // from a stored field.
    LoadedSession firstLoad = ledgercore::persistence::load(file.path());
    LoadedSession secondLoad = ledgercore::persistence::load(file.path());
    for (const std::string& code : {"1000", "2000", "3000", "4000", "5000"}) {
        EXPECT_EQ(firstLoad.chart->findByCode(AccountCode(code))->id(),
                  secondLoad.chart->findByCode(AccountCode(code))->id());
    }
    EXPECT_EQ(firstLoad.chart->findByCode(AccountCode("1000"))->id(), AccountId(1));
}

// ---------------------------------------------------------------------
// 19-21: Computed accounts
// ---------------------------------------------------------------------

TEST(SessionStoreTest, ComputedDefinitionRoundTrips) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    const ScopedTempFile file(uniqueTempPath("computed_round_trip"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const auto* definition = loaded.computedAccounts->find(ComputedAccountName("GrossProfit"));
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->formulaSource(), "#4000 - #5000");
}

TEST(SessionStoreTest, FormulaSourcePreservedExactlyIncludingWhitespace) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Spaced"), "  #4000 +  #5000 ");

    const ScopedTempFile file(uniqueTempPath("formula_whitespace"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const auto* definition = loaded.computedAccounts->find(ComputedAccountName("Spaced"));
    ASSERT_NE(definition, nullptr);
    EXPECT_EQ(definition->formulaSource(), "  #4000 +  #5000 ");
}

TEST(SessionStoreTest, ComputedAccountEvaluatesCorrectlyAfterReload) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Account& expense = chart.addRootAccount(AccountCode("5000"), "Expense", AccountType::Expense);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "sale",
                               {JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd))}),
         chart, ledger);
    post(JournalEntry::create(testDate(), "cost",
                               {JournalEntryLine::debit(expense.id(), Money::fromMajorUnits(40, 0, usd)),
                                JournalEntryLine::credit(cash.id(), Money::fromMajorUnits(40, 0, usd))}),
         chart, ledger);

    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
    registry.define(ComputedAccountName("DoubleGrossProfit"), "@GrossProfit * 2");

    const ScopedTempFile file(uniqueTempPath("computed_eval_after_reload"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const LedgerAccountResolver resolver(*loaded.chart, *loaded.ledger);
    const Money grossProfit = loaded.computedAccounts->evaluate(ComputedAccountName("GrossProfit"), resolver);
    const Money doubled = loaded.computedAccounts->evaluate(ComputedAccountName("DoubleGrossProfit"), resolver);
    EXPECT_EQ(grossProfit, Money::fromMajorUnits(60, 0, usd));
    EXPECT_EQ(doubled, Money::fromMajorUnits(120, 0, usd));
}

// ---------------------------------------------------------------------
// 22-25: Reports
// ---------------------------------------------------------------------

TEST(SessionStoreTest, TrialBalanceEqualBeforeAndAfterReload) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Investment",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1000, 0, usd)),
                                JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(1000, 0, usd))}),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Sale",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(300, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(300, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("tb_equal"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const TrialBalance liveTb = TrialBalance::generate(chart, ledger);
    const TrialBalance reloadedTb = TrialBalance::generate(*loaded.chart, *loaded.ledger);

    ASSERT_EQ(liveTb.lines().size(), reloadedTb.lines().size());
    for (std::size_t i = 0; i < liveTb.lines().size(); ++i) {
        EXPECT_EQ(liveTb.lines()[i].accountCode().value(), reloadedTb.lines()[i].accountCode().value());
        EXPECT_EQ(liveTb.lines()[i].debit(), reloadedTb.lines()[i].debit());
        EXPECT_EQ(liveTb.lines()[i].credit(), reloadedTb.lines()[i].credit());
    }
    EXPECT_EQ(liveTb.totalDebits(), reloadedTb.totalDebits());
    EXPECT_EQ(liveTb.totalCredits(), reloadedTb.totalCredits());
}

TEST(SessionStoreTest, BalanceSheetEqualBeforeAndAfterReload) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Loan",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                JournalEntryLine::credit(accounts.payable, Money::fromMajorUnits(500, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("bs_equal"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const BalanceSheet liveBs = BalanceSheet::generate(TrialBalance::generate(chart, ledger));
    const BalanceSheet reloadedBs = BalanceSheet::generate(TrialBalance::generate(*loaded.chart, *loaded.ledger));

    EXPECT_EQ(liveBs.assets().total(), reloadedBs.assets().total());
    EXPECT_EQ(liveBs.liabilities().total(), reloadedBs.liabilities().total());
    EXPECT_EQ(liveBs.equity().total(), reloadedBs.equity().total());
}

TEST(SessionStoreTest, IncomeStatementEqualBeforeAndAfterReload) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(testDate(), "Sale",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(500, 0, usd))}),
         chart, ledger);
    post(JournalEntry::create(testDate(), "Expense",
                               {JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(200, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("is_equal"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const IncomeStatement liveIs = IncomeStatement::generate(TrialBalance::generate(chart, ledger));
    const IncomeStatement reloadedIs = IncomeStatement::generate(TrialBalance::generate(*loaded.chart, *loaded.ledger));

    EXPECT_EQ(liveIs.revenue().total(), reloadedIs.revenue().total());
    EXPECT_EQ(liveIs.expenses().total(), reloadedIs.expenses().total());
    EXPECT_EQ(liveIs.netIncome(), reloadedIs.netIncome());
}

TEST(SessionStoreTest, PeriodAndAsOfReportsEqualAfterReload) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    post(JournalEntry::create(day(1), "April",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(100, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(100, 0, usd))}),
         chart, ledger);
    post(JournalEntry::create(day(40), "May",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(200, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(200, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("period_equal"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const TrialBalance liveAsOf = TrialBalance::generateAsOf(chart, ledger, day(40));
    const TrialBalance reloadedAsOf = TrialBalance::generateAsOf(*loaded.chart, *loaded.ledger, day(40));
    EXPECT_EQ(liveAsOf.totalDebits(), reloadedAsOf.totalDebits());

    const Period period(day(0), day(40));
    const TrialBalance livePeriod = TrialBalance::generateForPeriod(chart, ledger, period);
    const TrialBalance reloadedPeriod = TrialBalance::generateForPeriod(*loaded.chart, *loaded.ledger, period);
    EXPECT_EQ(livePeriod.totalDebits(), reloadedPeriod.totalDebits());
    EXPECT_EQ(livePeriod.totalDebits(), Money::fromMajorUnits(100, 0, usd));
}

// ---------------------------------------------------------------------
// 26-36: Corruption
// ---------------------------------------------------------------------

TEST(SessionStoreTest, InvalidHeaderThrowsPersistenceFormatException) {
    const ScopedTempFile file(uniqueTempPath("bad_header"));
    writeRawFile(file.path(), "NOT-A-LEDGERCORE-FILE\nCURRENCY USD\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, UnsupportedVersionThrowsPersistenceVersionException) {
    const ScopedTempFile file(uniqueTempPath("bad_version"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v99\nCURRENCY USD\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceVersionException);
}

TEST(SessionStoreTest, MalformedRecordWrongFieldCountThrowsFormatException) {
    const ScopedTempFile file(uniqueTempPath("bad_field_count"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, InvalidNumericFieldThrowsFormatException) {
    const ScopedTempFile file(uniqueTempPath("bad_numeric"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n" + "ENTRY not-a-number \"x\"\n"
                                   + "  DEBIT 1000 100\n  CREDIT 1000 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, UnknownAccountTypeTokenThrowsFormatException) {
    const ScopedTempFile file(uniqueTempPath("bad_account_type"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 NotARealType \"Cash\"\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, InvalidDebitCreditTokenThrowsFormatException) {
    const ScopedTempFile file(uniqueTempPath("bad_debit_credit"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
                                   + "ACCOUNT ROOT 4000 Revenue \"Sales\"\n" + "ENTRY 0 \"x\"\n"
                                   + "  DEBT 1000 100\n  CREDIT 4000 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, MissingParentAccountThrowsFormatException) {
    const ScopedTempFile file(uniqueTempPath("missing_parent"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT CHILD 9999 1110 \"Cash\"\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, DuplicateAccountCodePropagatesDomainException) {
    const ScopedTempFile file(uniqueTempPath("dup_code"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
                                   + "ACCOUNT ROOT 1000 Asset \"Cash Again\"\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::domain::DuplicateAccountCodeException);
}

TEST(SessionStoreTest, UnknownJournalAccountPropagatesAccountNotFoundException) {
    const ScopedTempFile file(uniqueTempPath("unknown_journal_account"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n" + "ENTRY 0 \"x\"\n"
                                   + "  DEBIT 1000 100\n  CREDIT 9999 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::posting::AccountNotFoundException);
}

TEST(SessionStoreTest, UnbalancedJournalPropagatesDomainException) {
    const ScopedTempFile file(uniqueTempPath("unbalanced"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
                                   + "ACCOUNT ROOT 4000 Revenue \"Sales\"\n" + "ENTRY 0 \"x\"\n"
                                   + "  DEBIT 1000 100\n  CREDIT 4000 99\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::domain::UnbalancedJournalEntryException);
}

TEST(SessionStoreTest, InvalidFormulaPropagatesFormulaSyntaxException) {
    const ScopedTempFile file(uniqueTempPath("bad_formula"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "COMPUTED Bad \"#4000 + \"\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::formula::FormulaSyntaxException);
}

TEST(SessionStoreTest, InvalidCurrencyPropagatesDomainException) {
    const ScopedTempFile file(uniqueTempPath("bad_currency"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v1\nCURRENCY usd\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::domain::InvalidCurrencyException);
}

// ---------------------------------------------------------------------
// 37-40: Safety
// ---------------------------------------------------------------------

TEST(SessionStoreTest, FailedLoadDoesNotAffectAnAlreadyLoadedLiveSession) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const ScopedTempFile goodFile(uniqueTempPath("good_for_safety"));
    ledgercore::persistence::save(chart, ledger, registry, goodFile.path());
    LoadedSession goodSession = ledgercore::persistence::load(goodFile.path());
    const std::size_t accountCountBefore = goodSession.chart->rootAccounts().size();

    const ScopedTempFile badFile(uniqueTempPath("bad_for_safety"));
    writeRawFile(badFile.path(), "GARBAGE\n");
    EXPECT_THROW(ledgercore::persistence::load(badFile.path()), PersistenceFormatException);

    // The earlier, already-successful LoadedSession is completely
    // unaffected by the later failed load attempt -- no shared state.
    EXPECT_EQ(goodSession.chart->rootAccounts().size(), accountCountBefore);
    EXPECT_NE(goodSession.chart->findByCode(AccountCode("1000")), nullptr);
}

TEST(SessionStoreTest, FailedSaveLeavesExistingTargetFileUntouched) {
    Currency usd("USD");
    ChartOfAccounts chart;
    setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("ledgercore_persistence_save_safety_" + std::to_string(::getpid()));
    std::filesystem::create_directories(directory);
    const std::filesystem::path target = directory / "session.snapshot";

    ledgercore::persistence::save(chart, ledger, registry, target);
    ASSERT_TRUE(std::filesystem::exists(target));
    const std::string originalContent = readRawFile(target);

    // Remove write permission on the directory so creating the temporary
    // file (needed before the atomic rename) deterministically fails,
    // while the pre-existing target file's own content is untouched.
    std::filesystem::permissions(directory, std::filesystem::perms::owner_write,
                                  std::filesystem::perm_options::remove);

    bool threw = false;
    try {
        ledgercore::persistence::save(chart, ledger, registry, target);
    } catch (const PersistenceException&) {
        threw = true;
    }

    std::filesystem::permissions(directory, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);

    EXPECT_TRUE(threw);
    EXPECT_EQ(readRawFile(target), originalContent);

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
}

TEST(SessionStoreTest, TruncatedFileMidQuoteIsRejected) {
    const ScopedTempFile file(uniqueTempPath("truncated_quote"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, TruncatedFileMissingEntryLinesPropagatesDomainException) {
    const ScopedTempFile file(uniqueTempPath("truncated_entry"));
    writeRawFile(file.path(), validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n" + "ENTRY 0 \"x\"\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::domain::InvalidJournalEntryException);
}

TEST(SessionStoreTest, SaveLoadSaveIsByteIdenticalWhenStateIsUnchanged) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");

    post(JournalEntry::create(day(5), "Sale",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(42, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(42, 0, usd))}),
         chart, ledger);

    const ScopedTempFile firstFile(uniqueTempPath("determinism_first"));
    const ScopedTempFile secondFile(uniqueTempPath("determinism_second"));

    ledgercore::persistence::save(chart, ledger, registry, firstFile.path());
    LoadedSession loaded = ledgercore::persistence::load(firstFile.path());
    ledgercore::persistence::save(*loaded.chart, *loaded.ledger, *loaded.computedAccounts, secondFile.path());

    EXPECT_EQ(readRawFile(firstFile.path()), readRawFile(secondFile.path()));
}

// ---------------------------------------------------------------------
// Property-style tests
// ---------------------------------------------------------------------

TEST(SessionStorePropertyTest, RandomBalancedPostingSequencesPreserveTrialBalanceAfterReload) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    const std::vector<AccountId> allAccounts{accounts.cash, accounts.payable, accounts.equity, accounts.revenue,
                                              accounts.expense};
    std::mt19937_64 rng(9001);
    std::uniform_int_distribution<std::size_t> accountDist(0, allAccounts.size() - 1);
    std::uniform_int_distribution<std::int64_t> amountDist(1, 100000);

    for (int trial = 0; trial < 20; ++trial) {
        std::size_t debitIndex = accountDist(rng);
        std::size_t creditIndex = accountDist(rng);
        while (creditIndex == debitIndex) {
            creditIndex = accountDist(rng);
        }
        const Money amount = Money::ofMinorUnits(amountDist(rng), usd);
        post(JournalEntry::create(testDate(), "trial " + std::to_string(trial),
                                   {JournalEntryLine::debit(allAccounts[debitIndex], amount),
                                    JournalEntryLine::credit(allAccounts[creditIndex], amount)}),
             chart, ledger);
    }

    const ScopedTempFile file(uniqueTempPath("property_round_trip"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const TrialBalance liveTb = TrialBalance::generate(chart, ledger);
    const TrialBalance reloadedTb = TrialBalance::generate(*loaded.chart, *loaded.ledger);
    EXPECT_EQ(liveTb.totalDebits(), reloadedTb.totalDebits());
    EXPECT_EQ(liveTb.totalCredits(), reloadedTb.totalCredits());
    ASSERT_EQ(liveTb.lines().size(), reloadedTb.lines().size());
    for (std::size_t i = 0; i < liveTb.lines().size(); ++i) {
        EXPECT_EQ(liveTb.lines()[i].debit(), reloadedTb.lines()[i].debit()) << "line " << i;
        EXPECT_EQ(liveTb.lines()[i].credit(), reloadedTb.lines()[i].credit()) << "line " << i;
    }
}

TEST(SessionStorePropertyTest, SaveIsDeterministicAcrossRepeatedCallsWithUnchangedState) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;

    std::mt19937_64 rng(4242);
    std::uniform_int_distribution<std::int64_t> amountDist(1, 5000);
    for (int i = 0; i < 10; ++i) {
        const Money amount = Money::ofMinorUnits(amountDist(rng), usd);
        post(JournalEntry::create(testDate(), "entry " + std::to_string(i),
                                   {JournalEntryLine::debit(accounts.cash, amount), JournalEntryLine::credit(accounts.revenue, amount)}),
             chart, ledger);
    }

    const ScopedTempFile fileA(uniqueTempPath("determinism_a"));
    const ScopedTempFile fileB(uniqueTempPath("determinism_b"));
    ledgercore::persistence::save(chart, ledger, registry, fileA.path());
    ledgercore::persistence::save(chart, ledger, registry, fileB.path());

    EXPECT_EQ(readRawFile(fileA.path()), readRawFile(fileB.path()));
}
