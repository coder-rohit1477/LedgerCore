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
using ledgercore::posting::PostedAccountCannotBecomeGroupException;
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
    Ledger ledger(usd);
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& current = ledgercore::posting::addChildAccount(chart, ledger, assets, AccountCode("1100"), "Current Assets");
    ledgercore::posting::addChildAccount(chart, ledger, current, AccountCode("1110"), "Cash");
    ledgercore::posting::addChildAccount(chart, ledger, current, AccountCode("1120"), "Accounts Receivable");
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
    Ledger ledger(usd);
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    ledgercore::posting::addChildAccount(chart, ledger, assets, AccountCode("1110"), "Cash");
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
    for (const char* code : {"1000", "2000", "3000", "4000", "5000"}) {
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
// Posted leaf can never become a group (Phase 14, P0-1)
// ---------------------------------------------------------------------

TEST(SessionStoreTest, ChildAddedViaLedgerAwarePathAfterUnrelatedPostingsReloads) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    Account& receivables = chart.addRootAccount(AccountCode("1200"), "Receivables", AccountType::Asset);

    post(JournalEntry::create(day(1), "Investment",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(500, 0, usd))}),
         chart, ledger);
    // Receivables was never posted to, so it may still become a group.
    const AccountId customerA =
        ledgercore::posting::addChildAccount(chart, ledger, receivables, AccountCode("1210"), "Customer A").id();
    post(JournalEntry::create(day(2), "Credit sale",
                               {JournalEntryLine::debit(customerA, Money::fromMajorUnits(75, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(75, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("child_after_postings"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const Account* reloadedReceivables = loaded.chart->findByCode(AccountCode("1200"));
    ASSERT_NE(reloadedReceivables, nullptr);
    EXPECT_FALSE(reloadedReceivables->isLeaf());
    const TrialBalance liveTb = TrialBalance::generate(chart, ledger);
    const TrialBalance reloadedTb = TrialBalance::generate(*loaded.chart, *loaded.ledger);
    EXPECT_EQ(liveTb.totalDebits(), reloadedTb.totalDebits());
    EXPECT_EQ(reloadedTb.totalDebits(), Money::fromMajorUnits(575, 0, usd));
}

TEST(SessionStoreTest, RejectedChildUnderPostedLeafLeavesSessionSaveableAndReloadable) {
    // The Phase 13 reproduction, at library level: post to 1000, then try
    // to give it a child. That attempt is now rejected, so the session
    // keeps a valid shape and its snapshot reloads (previously: save
    // succeeded, load failed with "Cannot post to non-leaf account").
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    post(JournalEntry::create(day(1), "Investment",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(500, 0, usd))}),
         chart, ledger);

    Account* cash = chart.findByCode(AccountCode("1000"));
    ASSERT_NE(cash, nullptr);
    EXPECT_THROW(ledgercore::posting::addChildAccount(chart, ledger, *cash, AccountCode("1010"), "Petty cash"),
                 PostedAccountCannotBecomeGroupException);

    const ScopedTempFile file(uniqueTempPath("rejected_child"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    EXPECT_EQ(readRawFile(file.path()).find("1010"), std::string::npos);

    LoadedSession loaded = ledgercore::persistence::load(file.path());
    const Account* reloadedCash = loaded.chart->findByCode(AccountCode("1000"));
    ASSERT_NE(reloadedCash, nullptr);
    EXPECT_TRUE(reloadedCash->isLeaf());
    const TrialBalance reloadedTb = TrialBalance::generate(*loaded.chart, *loaded.ledger);
    EXPECT_EQ(reloadedTb.totalDebits(), Money::fromMajorUnits(500, 0, usd));
    EXPECT_EQ(reloadedTb.totalCredits(), Money::fromMajorUnits(500, 0, usd));
}

TEST(SessionStoreTest, HandCraftedChildUnderAlreadyPostedAccountIsRejectedOnLoad) {
    // save() always writes every ACCOUNT before any ENTRY, so only a
    // hand-edited file can declare a child after its parent was posted
    // to; load() must reject it rather than build the broken shape.
    const ScopedTempFile file(uniqueTempPath("child_after_entry"));
    writeRawFile(file.path(), validHeaderAndCurrency()
                                  + "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
                                    "ACCOUNT ROOT 3000 Equity \"Capital\"\n"
                                    "ENTRY 1767225600000000000 \"Owner investment\"\n"
                                    "  DEBIT 1000 50000\n"
                                    "  CREDIT 3000 50000\n"
                                    "ACCOUNT CHILD 1000 1010 \"Petty cash\"\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PostedAccountCannotBecomeGroupException);
}

// ---------------------------------------------------------------------
// Supported date range (Phase 14, P1-3)
// ---------------------------------------------------------------------

namespace {

using ledgercore::persistence::kEndOfSupportedDatesEpochSeconds;
using ledgercore::persistence::kMinSupportedDateEpochSeconds;

std::chrono::system_clock::time_point epochSeconds(std::int64_t seconds) {
    return std::chrono::system_clock::time_point{}
           + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::seconds(seconds));
}

// First and last representable instants of the supported range on this
// platform's system_clock (last == one clock tick before the exclusive end).
std::chrono::system_clock::time_point minSupportedDate() {
    return epochSeconds(kMinSupportedDateEpochSeconds);
}

std::chrono::system_clock::time_point maxSupportedDate() {
    return epochSeconds(kEndOfSupportedDatesEpochSeconds) - std::chrono::system_clock::duration(1);
}

std::int64_t nanosOf(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

void postSingleEntryAt(ChartOfAccounts& chart, Ledger& ledger, const StandardAccounts& accounts,
                       std::chrono::system_clock::time_point date, const std::string& description) {
    const Currency usd("USD");
    post(JournalEntry::create(date, description,
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1, 0, usd)),
                                JournalEntryLine::credit(accounts.equity, Money::fromMajorUnits(1, 0, usd))}),
         chart, ledger);
}

std::string entryFileWithDate(const std::string& nanosText) {
    return validHeaderAndCurrency() + "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
           + "ACCOUNT ROOT 3000 Equity \"Capital\"\n" + "ENTRY " + nanosText + " \"Dated\"\n"
           + "  DEBIT 1000 100\n  CREDIT 3000 100\n";
}

} // namespace

TEST(SessionStoreTest, SupportedDateBoundsAreTheDocumentedCalendarInstants) {
    EXPECT_EQ(kMinSupportedDateEpochSeconds, -2208988800);   // 1900-01-01T00:00:00Z
    EXPECT_EQ(kEndOfSupportedDatesEpochSeconds, 7258118400); // 2200-01-01T00:00:00Z
}

TEST(SessionStoreTest, MinimumSupportedDateRoundTripsExactly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    postSingleEntryAt(chart, ledger, accounts, minSupportedDate(), "First supported instant");

    const ScopedTempFile file(uniqueTempPath("min_date"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    EXPECT_NE(readRawFile(file.path()).find("ENTRY -2208988800000000000 "), std::string::npos);

    LoadedSession loaded = ledgercore::persistence::load(file.path());
    ASSERT_EQ(loaded.ledger->postedEntries().size(), 1u);
    EXPECT_EQ(loaded.ledger->postedEntries().front().entry().date(), minSupportedDate());
}

TEST(SessionStoreTest, MaximumSupportedDateRoundTripsExactly) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    postSingleEntryAt(chart, ledger, accounts, maxSupportedDate(), "Last supported instant");

    const ScopedTempFile file(uniqueTempPath("max_date"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    EXPECT_NE(readRawFile(file.path()).find("ENTRY " + std::to_string(nanosOf(maxSupportedDate())) + " "),
              std::string::npos);

    LoadedSession loaded = ledgercore::persistence::load(file.path());
    ASSERT_EQ(loaded.ledger->postedEntries().size(), 1u);
    EXPECT_EQ(loaded.ledger->postedEntries().front().entry().date(), maxSupportedDate());
}

TEST(SessionStoreTest, BoundaryDatesSaveLoadSaveIsByteIdentical) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    postSingleEntryAt(chart, ledger, accounts, maxSupportedDate(), "Last");
    postSingleEntryAt(chart, ledger, accounts, minSupportedDate(), "First");

    const ScopedTempFile firstFile(uniqueTempPath("boundary_first"));
    const ScopedTempFile secondFile(uniqueTempPath("boundary_second"));
    ledgercore::persistence::save(chart, ledger, registry, firstFile.path());
    LoadedSession loaded = ledgercore::persistence::load(firstFile.path());
    ledgercore::persistence::save(*loaded.chart, *loaded.ledger, *loaded.computedAccounts, secondFile.path());

    EXPECT_EQ(readRawFile(firstFile.path()), readRawFile(secondFile.path()));
}

TEST(SessionStoreTest, SaveRejectsDatesOutsideSupportedRangeAndLeavesTargetUntouched) {
    const std::vector<std::chrono::system_clock::time_point> outOfRange = {
        minSupportedDate() - std::chrono::system_clock::duration(1),   // one tick before 1900-01-01
        epochSeconds(kEndOfSupportedDatesEpochSeconds),               // 2200-01-01 itself (exclusive end)
        std::chrono::system_clock::time_point::min(),
        std::chrono::system_clock::time_point::max(),
    };

    for (const auto& date : outOfRange) {
        Currency usd("USD");
        ChartOfAccounts chart;
        StandardAccounts accounts = setUpStandardChart(chart);
        Ledger ledger(usd);
        ComputedAccountRegistry registry;

        const ScopedTempFile file(uniqueTempPath("out_of_range_save"));
        ledgercore::persistence::save(chart, ledger, registry, file.path());
        const std::string originalContent = readRawFile(file.path());

        postSingleEntryAt(chart, ledger, accounts, date, "Out of range");
        EXPECT_THROW(ledgercore::persistence::save(chart, ledger, registry, file.path()), PersistenceException);
        EXPECT_EQ(readRawFile(file.path()), originalContent);
        EXPECT_FALSE(std::filesystem::exists(file.path().string() + ".tmp"));
    }
}

TEST(SessionStoreTest, LoadAcceptsPersistedDatesAtBothBoundaries) {
    const ScopedTempFile file(uniqueTempPath("boundary_load"));

    writeRawFile(file.path(), entryFileWithDate("-2208988800000000000"));
    LoadedSession first = ledgercore::persistence::load(file.path());
    ASSERT_EQ(first.ledger->postedEntries().size(), 1u);
    EXPECT_EQ(first.ledger->postedEntries().front().entry().date(), minSupportedDate());

    writeRawFile(file.path(), entryFileWithDate("7258118399999999000"));
    LoadedSession last = ledgercore::persistence::load(file.path());
    ASSERT_EQ(last.ledger->postedEntries().size(), 1u);
    EXPECT_LT(last.ledger->postedEntries().front().entry().date(), epochSeconds(kEndOfSupportedDatesEpochSeconds));
}

TEST(SessionStoreTest, LoadRejectsPersistedDatesOutsideSupportedRange) {
    const std::vector<std::string> outOfRange = {
        "-2208988800000000001",  // 1 ns before 1900-01-01
        "7258118400000000000",   // 2200-01-01 itself (exclusive end)
        "-8032952073709551616",  // the wrapped value Phase 13 observed for 2300-01-01
        "9223372036854775807",   // std::int64_t max
        "-9223372036854775808",  // std::int64_t min
    };
    for (const std::string& nanos : outOfRange) {
        const ScopedTempFile file(uniqueTempPath("out_of_range_load"));
        writeRawFile(file.path(), entryFileWithDate(nanos));
        EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException) << nanos;
    }
}

// ---------------------------------------------------------------------
// Closing entries: CLOSING records, format v1/v2 (Phase 17)
// ---------------------------------------------------------------------

namespace {

// Standard chart plus a Retained Earnings account (3100); one sale and
// one expense in year 1, closed as of day(365).
void setUpClosedYear(ChartOfAccounts& chart, Ledger& ledger) {
    const Currency usd("USD");
    StandardAccounts accounts = setUpStandardChart(chart);
    const AccountId retained = chart.addRootAccount(AccountCode("3100"), "Retained Earnings", AccountType::Equity).id();
    post(JournalEntry::create(day(10), "Sale",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(500, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(500, 0, usd))}),
         chart, ledger);
    post(JournalEntry::create(day(20), "Supplies",
                               {JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(120, 0, usd)),
                                JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(120, 0, usd))}),
         chart, ledger);
    post(JournalEntry::createClosing(day(364), "Closing entry",
                                     {JournalEntryLine::debit(accounts.revenue, Money::fromMajorUnits(500, 0, usd)),
                                      JournalEntryLine::credit(accounts.expense, Money::fromMajorUnits(120, 0, usd)),
                                      JournalEntryLine::credit(retained, Money::fromMajorUnits(380, 0, usd))}),
         chart, ledger);
}

} // namespace

TEST(SessionStoreTest, SnapshotWithoutClosingEntriesIsStillWrittenAsV1) {
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    post(JournalEntry::create(day(1), "Sale",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(1, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(1, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("still_v1"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    const std::string content = readRawFile(file.path());
    EXPECT_EQ(content.rfind("LEDGERCORE-SNAPSHOT v1\n", 0), 0u);
    EXPECT_EQ(content.find("CLOSING"), std::string::npos);
}

TEST(SessionStoreTest, ClosingEntryIsWrittenAsClosingRecordUnderV2Header) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("USD"));
    ComputedAccountRegistry registry;
    setUpClosedYear(chart, ledger);

    const ScopedTempFile file(uniqueTempPath("v2_closing"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    const std::string content = readRawFile(file.path());
    EXPECT_EQ(content.rfind("LEDGERCORE-SNAPSHOT v2\n", 0), 0u);
    EXPECT_NE(content.find("\nCLOSING " + std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                               day(364).time_since_epoch())
                                                               .count())
                           + " \"Closing entry\"\n  DEBIT 4000 50000\n  CREDIT 5000 12000\n  CREDIT 3100 38000\n"),
              std::string::npos)
        << content;
    // Ordinary entries keep their ENTRY records.
    EXPECT_NE(content.find("\nENTRY "), std::string::npos);
}

TEST(SessionStoreTest, ClosingEntryRoundTripsWithKindBalancesAndReports) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("USD"));
    ComputedAccountRegistry registry;
    setUpClosedYear(chart, ledger);

    const ScopedTempFile file(uniqueTempPath("closing_round_trip"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 3u);
    EXPECT_FALSE(loaded.ledger->postedEntries()[0].entry().isClosing());
    EXPECT_FALSE(loaded.ledger->postedEntries()[1].entry().isClosing());
    EXPECT_TRUE(loaded.ledger->postedEntries()[2].entry().isClosing());
    EXPECT_EQ(loaded.ledger->postedEntries()[2].entry().date(), day(364));
    const Account* retained = loaded.chart->findByCode(AccountCode("3100"));
    const Account* revenue = loaded.chart->findByCode(AccountCode("4000"));
    ASSERT_NE(retained, nullptr);
    ASSERT_NE(revenue, nullptr);
    EXPECT_EQ(loaded.ledger->balance(retained->id()), Money::fromMajorUnits(380, 0, Currency("USD")));
    EXPECT_TRUE(loaded.ledger->balance(revenue->id()).isZero());

    // The income statement excluding closing entries is identical live and reloaded.
    using ledgercore::trialbalance::ClosingEntries;
    const Period year(day(0), day(365));
    EXPECT_EQ(IncomeStatement::generate(TrialBalance::generateForPeriod(chart, ledger, year, ClosingEntries::Exclude))
                  .netIncome(),
              IncomeStatement::generate(TrialBalance::generateForPeriod(*loaded.chart, *loaded.ledger, year,
                                                                        ClosingEntries::Exclude))
                  .netIncome());
}

TEST(SessionStoreTest, SaveLoadSaveWithClosingEntryIsByteIdentical) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("USD"));
    ComputedAccountRegistry registry;
    setUpClosedYear(chart, ledger);

    const ScopedTempFile firstFile(uniqueTempPath("closing_first"));
    const ScopedTempFile secondFile(uniqueTempPath("closing_second"));
    ledgercore::persistence::save(chart, ledger, registry, firstFile.path());
    LoadedSession loaded = ledgercore::persistence::load(firstFile.path());
    ledgercore::persistence::save(*loaded.chart, *loaded.ledger, *loaded.computedAccounts, secondFile.path());

    EXPECT_EQ(readRawFile(firstFile.path()), readRawFile(secondFile.path()));
}

TEST(SessionStoreTest, ClosingRecordInV1SnapshotIsRejected) {
    const ScopedTempFile file(uniqueTempPath("closing_in_v1"));
    writeRawFile(file.path(), validHeaderAndCurrency()
                                  + "ACCOUNT ROOT 3100 Equity \"Retained\"\n"
                                    "ACCOUNT ROOT 4000 Revenue \"Sales\"\n"
                                    "CLOSING 1767225600000000000 \"Close\"\n"
                                    "  DEBIT 4000 100\n"
                                    "  CREDIT 3100 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

TEST(SessionStoreTest, V2SnapshotIsAcceptedAndUnknownVersionStillRejected) {
    const ScopedTempFile file(uniqueTempPath("v2_header"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v2\nCURRENCY USD\nACCOUNT ROOT 1000 Asset \"Cash\"\n");
    LoadedSession loaded = ledgercore::persistence::load(file.path());
    EXPECT_NE(loaded.chart->findByCode(AccountCode("1000")), nullptr);

    // v3 (accounting periods) is supported since Phase 18; the next
    // unknown version must still be rejected.
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v4\nCURRENCY USD\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceVersionException);
}

TEST(SessionStoreTest, HandCraftedClosingRecordTouchingAnAssetIsRejectedOnReplay) {
    const ScopedTempFile file(uniqueTempPath("bad_closing"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v2\nCURRENCY USD\n"
                              "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
                              "ACCOUNT ROOT 4000 Revenue \"Sales\"\n"
                              "CLOSING 1767225600000000000 \"Not a close\"\n"
                              "  DEBIT 1000 100\n"
                              "  CREDIT 4000 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::posting::InvalidClosingEntryException);
}

namespace {

// v2 snapshot: Cash, Capital, Retained (3100), Other Equity (3200), Sales,
// Rent; Capital 1000 invested and Sales 100 earned in year 1. `closing`
// is appended verbatim (a CLOSING record dated day 364).
std::string openYearSnapshotWith(const std::string& closingLines) {
    const auto nanos = [](int dayNumber) {
        return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(day(dayNumber).time_since_epoch())
                                  .count());
    };
    return "LEDGERCORE-SNAPSHOT v2\nCURRENCY USD\n"
           "ACCOUNT ROOT 1000 Asset \"Cash\"\n"
           "ACCOUNT ROOT 2000 Liability \"Payable\"\n"
           "ACCOUNT ROOT 3000 Equity \"Capital\"\n"
           "ACCOUNT ROOT 3100 Equity \"Retained\"\n"
           "ACCOUNT ROOT 3200 Equity \"Other Equity\"\n"
           "ACCOUNT ROOT 4000 Revenue \"Sales\"\n"
           "ACCOUNT ROOT 5000 Expense \"Rent\"\n"
           "ENTRY " + nanos(1) + " \"Invest\"\n  DEBIT 1000 100000\n  CREDIT 3000 100000\n"
           "ENTRY " + nanos(10) + " \"Sale\"\n  DEBIT 1000 10000\n  CREDIT 4000 10000\n"
           "CLOSING " + nanos(364) + " \"Close\"\n" + closingLines;
}

} // namespace

TEST(SessionStoreTest, HandEditedClosingRecordsThatAreNotCompleteClosesAreRejected) {
    const std::vector<std::pair<std::string, std::string>> malformed = {
        {"arbitrary equity transfer riding along",
         "  DEBIT 4000 10000\n  CREDIT 3100 10000\n  DEBIT 3000 90000\n  CREDIT 3200 90000\n"},
        {"split across two equity accounts", "  DEBIT 4000 10000\n  CREDIT 3100 4000\n  CREDIT 3200 6000\n"},
        {"partial close", "  DEBIT 4000 3000\n  CREDIT 3100 3000\n"},
        {"reversed, hiding revenue", "  DEBIT 3100 50000\n  CREDIT 4000 50000\n"},
        {"leaves another temporary account non-zero", "  DEBIT 4000 10000\n  CREDIT 5000 10000\n"},
        {"touches a liability", "  DEBIT 4000 10000\n  CREDIT 2000 10000\n"},
        {"touches an asset", "  DEBIT 4000 10000\n  CREDIT 1000 10000\n"},
        {"repeats an account", "  DEBIT 4000 6000\n  DEBIT 4000 4000\n  CREDIT 3100 10000\n"},
    };
    for (const auto& [label, lines] : malformed) {
        const ScopedTempFile file(uniqueTempPath("malformed_closing"));
        writeRawFile(file.path(), openYearSnapshotWith(lines));
        EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::posting::InvalidClosingEntryException)
            << label;
    }
}

TEST(SessionStoreTest, HandWrittenCompleteClosingRecordLoadsLikeTheClosingApi) {
    const ScopedTempFile file(uniqueTempPath("valid_hand_closing"));
    writeRawFile(file.path(), openYearSnapshotWith("  DEBIT 4000 10000\n  CREDIT 3200 10000\n"));

    LoadedSession loaded = ledgercore::persistence::load(file.path());

    ASSERT_EQ(loaded.ledger->postedEntries().size(), 3u);
    EXPECT_TRUE(loaded.ledger->postedEntries().back().entry().isClosing());
    EXPECT_TRUE(loaded.ledger->balance(loaded.chart->findByCode(AccountCode("4000"))->id()).isZero());
    EXPECT_EQ(loaded.ledger->balance(loaded.chart->findByCode(AccountCode("3200"))->id()),
              Money::fromMajorUnits(100, 0, Currency("USD")));
}

TEST(SessionStoreTest, ClosingEntryTimestampIsPersistedIdenticallyOnEveryPlatform) {
    // Closing as of day(365) dates the entry exactly 1us (1000ns) earlier:
    // representable by every supported clock, so the bytes never depend on
    // the platform's system_clock resolution.
    Currency usd("USD");
    ChartOfAccounts chart;
    StandardAccounts accounts = setUpStandardChart(chart);
    const AccountId retained = chart.addRootAccount(AccountCode("3100"), "Retained", AccountType::Equity).id();
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    post(JournalEntry::create(day(10), "Sale",
                               {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(5, 0, usd)),
                                JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(5, 0, usd))}),
         chart, ledger);
    post(JournalEntry::createClosing(day(365) - ledgercore::domain::kClosingCutoffOffset, "Closing entry",
                                     {JournalEntryLine::debit(accounts.revenue, Money::fromMajorUnits(5, 0, usd)),
                                      JournalEntryLine::credit(retained, Money::fromMajorUnits(5, 0, usd))}),
         chart, ledger);

    const ScopedTempFile file(uniqueTempPath("closing_timestamp"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    const std::int64_t cutoffNanos =
        std::chrono::duration_cast<std::chrono::nanoseconds>(day(365).time_since_epoch()).count();
    EXPECT_NE(readRawFile(file.path()).find("\nCLOSING " + std::to_string(cutoffNanos - 1000) + " "),
              std::string::npos);

    LoadedSession loaded = ledgercore::persistence::load(file.path());
    EXPECT_EQ(loaded.ledger->postedEntries().back().entry().closingCutoff(), day(365));
}

TEST(SessionStoreTest, ClosingRecordDateOutsideSupportedRangeIsRejected) {
    const ScopedTempFile file(uniqueTempPath("closing_date_range"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v2\nCURRENCY USD\n"
                              "ACCOUNT ROOT 3100 Equity \"Retained\"\n"
                              "ACCOUNT ROOT 4000 Revenue \"Sales\"\n"
                              "CLOSING 7258118400000000000 \"Close\"\n"
                              "  DEBIT 4000 100\n"
                              "  CREDIT 3100 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException);
}

// ---------------------------------------------------------------------
// Accounting periods: PERIOD records, format v3 (Phase 18)
// ---------------------------------------------------------------------

namespace {

std::string nanosText(std::chrono::system_clock::time_point tp) {
    return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
}

// Standard chart; sales on day 10 and day 200, an expense on day 300;
// periods [0,100) CLOSED, [100,365) OPEN, [400,500) CLOSED (defined out
// of order); a computed definition so record ordering is visible.
struct PeriodSession {
    ChartOfAccounts chart;
    Ledger ledger{Currency("USD")};
    ComputedAccountRegistry registry;
    StandardAccounts accounts;
    PeriodSession() : accounts(setUpStandardChart(chart)) {
        const Currency usd("USD");
        post(JournalEntry::create(day(10), "Early sale",
                                   {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(40, 0, usd)),
                                    JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(40, 0, usd))}),
             chart, ledger);
        post(JournalEntry::create(day(200), "Later sale",
                                   {JournalEntryLine::debit(accounts.cash, Money::fromMajorUnits(60, 0, usd)),
                                    JournalEntryLine::credit(accounts.revenue, Money::fromMajorUnits(60, 0, usd))}),
             chart, ledger);
        post(JournalEntry::create(day(300), "Rent",
                                   {JournalEntryLine::debit(accounts.expense, Money::fromMajorUnits(25, 0, usd)),
                                    JournalEntryLine::credit(accounts.cash, Money::fromMajorUnits(25, 0, usd))}),
             chart, ledger);
        ledger.defineAccountingPeriod(Period(day(400), day(500)));
        ledger.defineAccountingPeriod(Period(day(100), day(365)));
        ledger.defineAccountingPeriod(Period(day(0), day(100)));
        ledger.closeAccountingPeriod(Period(day(0), day(100)));
        ledger.closeAccountingPeriod(Period(day(400), day(500)));
        registry.define(ComputedAccountName("Profit"), "#4000 - #5000");
    }
};

std::string periodSnapshot(const std::string& periodRecords, const std::string& version = "v3") {
    return "LEDGERCORE-SNAPSHOT " + version + "\nCURRENCY USD\n"
           + "ACCOUNT ROOT 1000 Asset \"Cash\"\nACCOUNT ROOT 4000 Revenue \"Sales\"\n"
           + "ENTRY " + nanosText(day(10)) + " \"Sale\"\n  DEBIT 1000 100\n  CREDIT 4000 100\n" + periodRecords;
}

} // namespace

TEST(SessionStoreTest, PeriodsAreWrittenAsV3RecordsAfterEntriesInStartOrder) {
    PeriodSession session;
    const ScopedTempFile file(uniqueTempPath("periods_v3"));
    ledgercore::persistence::save(session.chart, session.ledger, session.registry, file.path());
    const std::string content = readRawFile(file.path());

    EXPECT_EQ(content.rfind("LEDGERCORE-SNAPSHOT v3\n", 0), 0u);
    const std::string expectedPeriods = "PERIOD " + nanosText(day(0)) + " " + nanosText(day(100)) + " CLOSED\n"
                                        + "PERIOD " + nanosText(day(100)) + " " + nanosText(day(365)) + " OPEN\n"
                                        + "PERIOD " + nanosText(day(400)) + " " + nanosText(day(500)) + " CLOSED\n";
    const std::size_t periods = content.find(expectedPeriods);
    ASSERT_NE(periods, std::string::npos) << content;
    EXPECT_LT(content.rfind("ENTRY "), periods);           // every entry precedes the periods
    EXPECT_GT(content.find("COMPUTED "), periods);         // computed definitions follow them
}

TEST(SessionStoreTest, PeriodsAndClosedStateRoundTripAndRemainEnforced) {
    PeriodSession session;
    const ScopedTempFile file(uniqueTempPath("periods_round_trip"));
    ledgercore::persistence::save(session.chart, session.ledger, session.registry, file.path());

    // The day-10 entry lies inside the CLOSED [0,100) period: replayed
    // before the lock is reapplied, so the snapshot loads.
    LoadedSession loaded = ledgercore::persistence::load(file.path());

    const auto& periods = loaded.ledger->accountingPeriods();
    ASSERT_EQ(periods.size(), 3u);
    EXPECT_EQ(periods[0].period().start(), day(0));
    EXPECT_TRUE(periods[0].isClosed());
    EXPECT_EQ(periods[1].period().start(), day(100));
    EXPECT_FALSE(periods[1].isClosed());
    EXPECT_EQ(periods[2].period().end(), day(500));
    EXPECT_TRUE(periods[2].isClosed());
    EXPECT_EQ(loaded.ledger->postedEntries().size(), 3u);

    // The reloaded lock is real.
    const Account* cash = loaded.chart->findByCode(AccountCode("1000"));
    const Account* revenue = loaded.chart->findByCode(AccountCode("4000"));
    const Currency usd("USD");
    EXPECT_THROW(post(JournalEntry::create(day(50), "Backdated",
                                           {JournalEntryLine::debit(cash->id(), Money::fromMajorUnits(1, 0, usd)),
                                            JournalEntryLine::credit(revenue->id(), Money::fromMajorUnits(1, 0, usd))}),
                      *loaded.chart, *loaded.ledger),
                 ledgercore::posting::ClosedPeriodPostingException);
}

TEST(SessionStoreTest, SaveLoadSaveWithPeriodsIsByteIdenticalAndReportsMatch) {
    PeriodSession session;
    const ScopedTempFile firstFile(uniqueTempPath("periods_first"));
    const ScopedTempFile secondFile(uniqueTempPath("periods_second"));
    ledgercore::persistence::save(session.chart, session.ledger, session.registry, firstFile.path());
    LoadedSession loaded = ledgercore::persistence::load(firstFile.path());
    ledgercore::persistence::save(*loaded.chart, *loaded.ledger, *loaded.computedAccounts, secondFile.path());
    EXPECT_EQ(readRawFile(firstFile.path()), readRawFile(secondFile.path()));

    const TrialBalance live = TrialBalance::generateForPeriod(session.chart, session.ledger, Period(day(0), day(100)));
    const TrialBalance reloaded =
        TrialBalance::generateForPeriod(*loaded.chart, *loaded.ledger, Period(day(0), day(100)));
    EXPECT_EQ(live.totalDebits(), reloaded.totalDebits());
    EXPECT_EQ(IncomeStatement::generate(TrialBalance::generate(session.chart, session.ledger)).netIncome(),
              IncomeStatement::generate(TrialBalance::generate(*loaded.chart, *loaded.ledger)).netIncome());
    EXPECT_EQ(BalanceSheet::generate(TrialBalance::generate(session.chart, session.ledger)).assets().total(),
              BalanceSheet::generate(TrialBalance::generate(*loaded.chart, *loaded.ledger)).assets().total());
}

TEST(SessionStoreTest, PeriodsCombineWithClosingEntriesUnderV3) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("USD"));
    ComputedAccountRegistry registry;
    setUpClosedYear(chart, ledger);  // closing entry dated day(364)
    ledger.defineAccountingPeriod(Period(day(0), day(365)));
    ledger.closeAccountingPeriod(Period(day(0), day(365)));

    const ScopedTempFile file(uniqueTempPath("periods_with_closing"));
    ledgercore::persistence::save(chart, ledger, registry, file.path());
    const std::string content = readRawFile(file.path());
    EXPECT_EQ(content.rfind("LEDGERCORE-SNAPSHOT v3\n", 0), 0u);
    EXPECT_NE(content.find("\nCLOSING "), std::string::npos);

    LoadedSession loaded = ledgercore::persistence::load(file.path());
    EXPECT_TRUE(loaded.ledger->postedEntries().back().entry().isClosing());
    EXPECT_TRUE(loaded.ledger->accountingPeriods().at(0).isClosed());
}

TEST(SessionStoreTest, SaveRejectsAPeriodOutsideTheSupportedDateRange) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);
    ComputedAccountRegistry registry;
    const auto before1900 = std::chrono::system_clock::time_point{} + std::chrono::seconds(-2208988801LL);
    ledger.defineAccountingPeriod(Period(before1900, day(10)));
    const ScopedTempFile file(uniqueTempPath("period_out_of_range"));
    EXPECT_THROW(ledgercore::persistence::save(chart, ledger, registry, file.path()), PersistenceException);
    EXPECT_FALSE(std::filesystem::exists(file.path()));
}

TEST(SessionStoreTest, PeriodEndingExactlyAtTheSupportedRangeEndRoundTrips) {
    const ScopedTempFile file(uniqueTempPath("period_at_range_end"));
    writeRawFile(file.path(), periodSnapshot("PERIOD " + nanosText(day(400)) + " 7258118400000000000 OPEN\n"));
    LoadedSession loaded = ledgercore::persistence::load(file.path());
    ASSERT_EQ(loaded.ledger->accountingPeriods().size(), 1u);
}

TEST(SessionStoreTest, HandEditedPeriodRecordsThatAreMalformedAreRejected) {
    const std::string start = nanosText(day(0));
    const std::string end = nanosText(day(100));
    struct Case {
        std::string label;
        std::string content;
        bool formatError;  // PersistenceFormatException vs. a domain/ledger LedgerException
    };
    const std::vector<Case> cases = {
        {"PERIOD in a v2 snapshot", periodSnapshot("PERIOD " + start + " " + end + " OPEN\n", "v2"), true},
        {"missing state", periodSnapshot("PERIOD " + start + " " + end + "\n"), true},
        {"extra field", periodSnapshot("PERIOD " + start + " " + end + " OPEN extra\n"), true},
        {"non-integer bound", periodSnapshot("PERIOD 2027-01-01 " + end + " OPEN\n"), true},
        {"unknown state", periodSnapshot("PERIOD " + start + " " + end + " LOCKED\n"), true},
        {"lower-case state", periodSnapshot("PERIOD " + start + " " + end + " closed\n"), true},
        {"start before 1900", periodSnapshot("PERIOD -2208988800000000001 " + end + " OPEN\n"), true},
        {"end after 2200", periodSnapshot("PERIOD " + start + " 7258118400000000001 OPEN\n"), true},
        {"start == end", periodSnapshot("PERIOD " + start + " " + start + " OPEN\n"), false},
        {"start > end", periodSnapshot("PERIOD " + end + " " + start + " OPEN\n"), false},
        {"duplicate period",
         periodSnapshot("PERIOD " + start + " " + end + " OPEN\nPERIOD " + start + " " + end + " CLOSED\n"), false},
        {"overlapping periods",
         periodSnapshot("PERIOD " + start + " " + end + " OPEN\nPERIOD " + nanosText(day(50)) + " "
                        + nanosText(day(150)) + " OPEN\n"),
         false},
    };
    for (const Case& c : cases) {
        const ScopedTempFile file(uniqueTempPath("bad_period"));
        writeRawFile(file.path(), c.content);
        if (c.formatError) {
            EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceFormatException) << c.label;
        } else {
            EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::LedgerException) << c.label;
        }
    }
}

TEST(SessionStoreTest, ClosedPeriodPlacedBeforeAnEntryInsideItIsRejected) {
    // A legitimate snapshot always lists periods after the history; a file
    // that closes a period and then "adds" an entry dated inside it
    // describes a posting the live system would have refused.
    const ScopedTempFile file(uniqueTempPath("closed_before_entry"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v3\nCURRENCY USD\n"
                              "ACCOUNT ROOT 1000 Asset \"Cash\"\nACCOUNT ROOT 4000 Revenue \"Sales\"\n"
                              "PERIOD " + nanosText(day(0)) + " " + nanosText(day(100)) + " CLOSED\n"
                              "ENTRY " + nanosText(day(10)) + " \"Sale\"\n  DEBIT 1000 100\n  CREDIT 4000 100\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::posting::ClosedPeriodPostingException);
}

TEST(SessionStoreTest, MalformedPeriodAndMalformedJournalReportTheFirstProblemInFileOrder) {
    const ScopedTempFile file(uniqueTempPath("bad_period_and_entry"));
    writeRawFile(file.path(), "LEDGERCORE-SNAPSHOT v3\nCURRENCY USD\n"
                              "ACCOUNT ROOT 1000 Asset \"Cash\"\nACCOUNT ROOT 4000 Revenue \"Sales\"\n"
                              "ENTRY " + nanosText(day(10)) + " \"Bad\"\n  DEBIT 1000 100\n  CREDIT 4000 99\n"
                              "PERIOD " + nanosText(day(0)) + " nope OPEN\n");
    EXPECT_THROW(ledgercore::persistence::load(file.path()), ledgercore::domain::UnbalancedJournalEntryException);
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
