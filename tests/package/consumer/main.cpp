// Exercises LedgerCore through installed headers only: chart, posting,
// trial balance, income statement, computed account, closing, journal
// query, and a save/load round trip. Exits non-zero on any mismatch.

// On Windows, <windows.h> comes first (without NOMINMAX), as in many
// Windows applications: the installed headers must not break under its
// min/max macros.
#ifdef _WIN32
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>

#include "ledgercore/closing/ClosingEngine.h"
#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/computed/LedgerAccountResolver.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/journalquery/JournalQuery.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/persistence/SessionStore.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/reporting/IncomeStatement.h"
#include "ledgercore/trialbalance/TrialBalance.h"

namespace lc = ledgercore;
using lc::domain::AccountCode;
using lc::domain::AccountType;
using lc::domain::JournalEntry;
using lc::domain::JournalEntryLine;
using lc::domain::Money;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
        ++failures;
    }
}

std::chrono::system_clock::time_point day(int daysSinceEpoch) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * daysSinceEpoch);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const lc::domain::Currency usd("USD");
        const auto amount = [&usd](long long major) { return Money::fromMajorUnits(major, 0, usd); };

        lc::domain::ChartOfAccounts chart;
        const auto cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
        const auto retained = chart.addRootAccount(AccountCode("3100"), "Retained", AccountType::Equity).id();
        const auto sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
        const auto rent = chart.addRootAccount(AccountCode("5000"), "Rent", AccountType::Expense).id();
        lc::ledger::Ledger ledger(usd);

        lc::posting::post(JournalEntry::create(day(20000), "Sale", {JournalEntryLine::debit(cash, amount(700)),
                                                                    JournalEntryLine::credit(sales, amount(700))}),
                          chart, ledger);
        lc::posting::post(JournalEntry::create(day(20010), "Rent", {JournalEntryLine::debit(rent, amount(250)),
                                                                    JournalEntryLine::credit(cash, amount(250))}),
                          chart, ledger);

        const auto trialBalance = lc::trialbalance::TrialBalance::generate(chart, ledger);
        check(trialBalance.totalDebits() == amount(700), "trial balance debits are 700 (cash 450 + rent 250)");
        check(trialBalance.totalDebits() == trialBalance.totalCredits(), "trial balance balances");
        check(lc::reporting::IncomeStatement::generate(trialBalance).netIncome() == amount(450), "net income is 450");

        lc::computed::ComputedAccountRegistry registry;
        registry.define(lc::formula::ComputedAccountName("Profit"), "#4000 - #5000");
        const lc::computed::LedgerAccountResolver resolver(chart, ledger);
        check(registry.evaluate(lc::formula::ComputedAccountName("Profit"), resolver) == amount(450),
              "computed Profit is 450");

        const auto closed = lc::closing::closeTemporaryAccounts(chart, ledger, retained, day(20100));
        check(closed.netIncome == amount(450), "closing moves 450");
        check(ledger.balance(retained) == amount(450), "retained earnings holds 450");
        const auto closings = lc::journalquery::findJournalEntries(
            ledger, lc::journalquery::JournalQuery().withKind(lc::journalquery::EntryKindFilter::ClosingOnly));
        check(closings.size() == 1, "one closing entry");

        const std::filesystem::path snapshot =
            std::filesystem::path(argc > 1 ? argv[1] : ".") / "ledgercore_consumer.snapshot";
        lc::persistence::save(chart, ledger, registry, snapshot);
        const auto loaded = lc::persistence::load(snapshot);
        std::filesystem::remove(snapshot);
        check(loaded.ledger->postedEntries().size() == 3, "reloaded ledger has 3 entries");
        check(loaded.ledger->balance(loaded.chart->findByCode(AccountCode("3100"))->id()) == amount(450),
              "reloaded retained earnings holds 450");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: unexpected exception: %s\n", e.what());
        return 1;
    }

    if (failures != 0) {
        return 1;
    }
    std::printf("LedgerCore consumer OK\n");
    return 0;
}
