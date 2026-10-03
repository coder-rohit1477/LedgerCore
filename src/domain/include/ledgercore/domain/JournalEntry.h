#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::domain {

// What a JournalEntry represents, fixed at creation.
//
// Standard: ordinary business activity.
// Closing:  a period-end closing entry that transfers Revenue/Expense
//           balances into an Equity (retained earnings) account. It is a
//           genuine balanced posting -- trial balances and balance sheets
//           include it like any other entry -- but it is not revenue or
//           expense *activity*, so income statements can exclude it (see
//           trialbalance::ClosingEntries). Because of that exclusion,
//           posting::post() only accepts a Closing entry that is exactly a
//           complete close as of its closingCutoff() (see PostingEngine.h),
//           so the marker can never hide or reshape ordinary activity.
enum class JournalEntryKind {
    Standard,
    Closing
};

// A Closing entry dated D closes all Revenue/Expense activity dated
// strictly before D + kClosingCutoffOffset -- its closingCutoff().
// Equivalently, closing as of cutoff C dates the entry at
// C - kClosingCutoffOffset. One microsecond is exactly representable by
// every supported system_clock (microseconds on macOS, nanoseconds on
// Linux, 100ns ticks on Windows) and by the snapshot format's nanosecond
// timestamps, so the same close produces the same date -- and the same
// snapshot bytes -- on every platform.
inline constexpr std::chrono::microseconds kClosingCutoffOffset{1};

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

    // Identical validation to create(), but the entry's kind() is
    // JournalEntryKind::Closing; additionally throws
    // InvalidJournalEntryException if date is so close to the end of
    // system_clock's range that closingCutoff() would not be
    // representable. Normally produced by closing::closeTemporaryAccounts()
    // rather than called directly.
    static JournalEntry createClosing(std::chrono::system_clock::time_point date,
                                       std::string description,
                                       std::vector<JournalEntryLine> lines);

    std::chrono::system_clock::time_point date() const noexcept { return date_; }
    const std::string& description() const noexcept { return description_; }
    const std::vector<JournalEntryLine>& lines() const noexcept { return lines_; }

    const Currency& currency() const noexcept { return currency_; }
    const Money& totalDebits() const noexcept { return totalDebits_; }
    const Money& totalCredits() const noexcept { return totalCredits_; }

    JournalEntryKind kind() const noexcept { return kind_; }
    bool isClosing() const noexcept { return kind_ == JournalEntryKind::Closing; }

    // date() + kClosingCutoffOffset: the instant before which this
    // Closing entry closes all Revenue/Expense activity. Throws
    // InvalidJournalEntryException for a Standard entry, which closes
    // nothing.
    std::chrono::system_clock::time_point closingCutoff() const;

private:
    static JournalEntry createValidated(std::chrono::system_clock::time_point date,
                                        std::string description,
                                        std::vector<JournalEntryLine> lines,
                                        JournalEntryKind kind);

    JournalEntry(std::chrono::system_clock::time_point date,
                 std::string description,
                 std::vector<JournalEntryLine> lines,
                 Currency currency,
                 Money totalDebits,
                 Money totalCredits,
                 JournalEntryKind kind);

    std::chrono::system_clock::time_point date_;
    std::string description_;
    std::vector<JournalEntryLine> lines_;
    Currency currency_;
    Money totalDebits_;
    Money totalCredits_;
    JournalEntryKind kind_;
};

} // namespace ledgercore::domain
