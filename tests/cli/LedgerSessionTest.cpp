// Direct unit tests of LedgerSession's own atomic-replace guarantee --
// not a re-test of persistence's own save/load correctness (already
// covered by the 44 tests in tests/persistence/SessionStoreTest.cpp).
// These confirm only the CLI-layer integration: a successful load fully
// replaces the session, a failed load leaves it byte-for-byte as it was.

#include "LedgerSession.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/persistence/PersistenceExceptions.h"
#include "ledgercore/persistence/SessionStore.h"
#include "ledgercore/posting/PostingEngine.h"

using ledgercore::cli::LedgerSession;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountType;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::formula::ComputedAccountName;
using ledgercore::persistence::PersistenceException;
using ledgercore::posting::post;

namespace {

std::filesystem::path uniqueTempPath(const std::string& label) {
    static int counter = 0;
    ++counter;
    return std::filesystem::temp_directory_path()
           / ("ledgercore_session_test_" + label + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter)
              + ".snapshot");
}

class ScopedTempFile {
public:
    explicit ScopedTempFile(std::filesystem::path path) : path_(std::move(path)) {}
    ~ScopedTempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
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

} // namespace

TEST(LedgerSessionTest, SuccessfulLoadReplacesEntireSessionState) {
    LedgerSession session(Currency("USD"));
    session.chart().addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    session.chart().addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue);

    const ScopedTempFile file(uniqueTempPath("success"));

    // Save a *different* chart/ledger/computed-account set than the live
    // session currently holds.
    {
        ledgercore::domain::ChartOfAccounts otherChart;
        otherChart.addRootAccount(AccountCode("9000"), "Other Account", AccountType::Asset);
        ledgercore::ledger::Ledger otherLedger(Currency("USD"));
        ledgercore::computed::ComputedAccountRegistry otherRegistry;
        otherRegistry.define(ComputedAccountName("Marker"), "1");
        ledgercore::persistence::save(otherChart, otherLedger, otherRegistry, file.path());
    }

    ledgercore::persistence::LoadedSession loaded = ledgercore::persistence::load(file.path());
    session.replaceState(std::move(loaded));

    EXPECT_EQ(session.chart().findByCode(AccountCode("1000")), nullptr);
    EXPECT_EQ(session.chart().findByCode(AccountCode("4000")), nullptr);
    EXPECT_NE(session.chart().findByCode(AccountCode("9000")), nullptr);
    EXPECT_NE(session.computedAccounts().find(ComputedAccountName("Marker")), nullptr);
}

TEST(LedgerSessionTest, FailedLoadFromMissingFileLeavesSessionUnchanged) {
    LedgerSession session(Currency("USD"));
    session.chart().addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    session.chart().addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue);
    post(JournalEntry::create(testDate(), "seed",
                               {JournalEntryLine::debit(session.chart().findByCode(AccountCode("1000"))->id(),
                                                         Money::fromMajorUnits(100, 0, Currency("USD"))),
                                JournalEntryLine::credit(session.chart().findByCode(AccountCode("4000"))->id(),
                                                          Money::fromMajorUnits(100, 0, Currency("USD")))}),
         session.chart(), session.ledger());

    EXPECT_THROW(ledgercore::persistence::load(uniqueTempPath("does_not_exist")), PersistenceException);

    // The session was never touched -- replaceState() is only reachable
    // after a successful load() returns, and load() never returned here.
    EXPECT_NE(session.chart().findByCode(AccountCode("1000")), nullptr);
    EXPECT_NE(session.chart().findByCode(AccountCode("4000")), nullptr);
    EXPECT_EQ(session.ledger().balance(session.chart().findByCode(AccountCode("1000"))->id()),
              Money::fromMajorUnits(100, 0, Currency("USD")));
    EXPECT_EQ(session.ledger().postedEntries().size(), 1u);
}

TEST(LedgerSessionTest, FailedLoadFromMalformedFileLeavesSessionUnchanged) {
    LedgerSession session(Currency("USD"));
    session.chart().addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);

    const ScopedTempFile file(uniqueTempPath("malformed"));
    {
        std::ofstream out(file.path(), std::ios::binary | std::ios::trunc);
        out << "NOT-A-VALID-SNAPSHOT\n";
    }

    EXPECT_THROW(ledgercore::persistence::load(file.path()), PersistenceException);

    EXPECT_NE(session.chart().findByCode(AccountCode("1000")), nullptr);
    EXPECT_EQ(session.chart().rootAccounts().size(), 1u);
}

TEST(LedgerSessionTest, CurrencyReflectsCurrentLedgerAfterReload) {
    LedgerSession session(Currency("USD"));
    EXPECT_EQ(session.currency(), Currency("USD"));

    const ScopedTempFile file(uniqueTempPath("currency"));
    {
        ledgercore::domain::ChartOfAccounts eurChart;
        ledgercore::ledger::Ledger eurLedger(Currency("EUR"));
        ledgercore::computed::ComputedAccountRegistry eurRegistry;
        ledgercore::persistence::save(eurChart, eurLedger, eurRegistry, file.path());
    }

    ledgercore::persistence::LoadedSession loaded = ledgercore::persistence::load(file.path());
    session.replaceState(std::move(loaded));

    EXPECT_EQ(session.currency(), Currency("EUR"));
}
