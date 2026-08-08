#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::domain {

// An immutable, internally-balanced double-entry transaction: a date, a
// required description, and a fixed set of lines.
//
// Design decision (Phase 3, Step 1): the transaction date is a bare
// std::chrono::system_clock::time_point, not a custom wrapper. Every
// time_point is definitionally a valid point in time -- there's no
// analogous "invalid" state the way an empty AccountCode is invalid --
// so a wrapper would add ceremony without buying any real safety.
//
// JournalEntry has no ID of its own. No aggregate root exists yet to
// assign or consume one; the Ledger/Posting Engine phases will decide
// the right identity scheme when they actually need it.
//
// JournalEntry does not validate that its lines' AccountIds exist
// anywhere -- it has no dependency on ChartOfAccounts, by design. That
// check belongs to the future Posting Engine, which is the only
// component that has both a JournalEntry and a specific chart to check
// it against. This mirrors the local-vs-global invariant split already
// used for Account (validates locally) and ChartOfAccounts (validates
// globally).
//
// Only JournalEntry::create() can produce a JournalEntry, and it always
// either returns a fully-valid, balanced entry or throws -- there is no
// path to a partially-valid instance.
class JournalEntry {
public:
    static JournalEntry create(std::chrono::system_clock::time_point date,
                                std::string description,
                                std::vector<JournalEntryLine> lines);

    std::chrono::system_clock::time_point date() const noexcept { return date_; }
    const std::string& description() const noexcept { return description_; }
    const std::vector<JournalEntryLine>& lines() const noexcept { return lines_; }

    const Currency& currency() const noexcept { return currency_; }
    const Money& totalDebits() const noexcept { return totalDebits_; }
    const Money& totalCredits() const noexcept { return totalCredits_; }

private:
    JournalEntry(std::chrono::system_clock::time_point date,
                 std::string description,
                 std::vector<JournalEntryLine> lines,
                 Currency currency,
                 Money totalDebits,
                 Money totalCredits);

    std::chrono::system_clock::time_point date_;
    std::string description_;
    std::vector<JournalEntryLine> lines_;
    Currency currency_;
    Money totalDebits_;
    Money totalCredits_;
};

} // namespace ledgercore::domain
