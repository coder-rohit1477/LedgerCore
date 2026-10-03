#pragma once

#include <optional>
#include <vector>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostedJournalEntry.h"

namespace ledgercore::journalquery {

// Which JournalEntryKind a query keeps. Decided solely by
// JournalEntry::kind() -- never inferred from descriptions, accounts, or
// line shape.
enum class EntryKindFilter {
    All,
    StandardOnly,
    ClosingOnly
};

// An immutable filter over a Ledger's posted journal history. The default
// query matches every entry; each with...() returns a narrowed copy, and
// an entry is selected only if it satisfies every filter that is set:
//
//   dateRange  entry.date() inside [start, end) -- the same domain::Period
//              semantics TrialBalance uses. Purely historical: whether an
//              accounting period covering those dates is open or closed is
//              irrelevant (period locking restricts postings, not reports).
//   account    the AccountId appears on at least one line, debit or credit.
//   kind       Standard entries, Closing entries, or both.
class JournalQuery {
public:
    JournalQuery() = default;

    JournalQuery withDateRange(const domain::Period& range) const;
    JournalQuery withAccount(domain::AccountId account) const;
    JournalQuery withKind(EntryKindFilter kind) const;

    const std::optional<domain::Period>& dateRange() const noexcept { return dateRange_; }
    const std::optional<domain::AccountId>& account() const noexcept { return account_; }
    EntryKindFilter kind() const noexcept { return kind_; }

    bool matches(const domain::JournalEntry& entry) const;

private:
    std::optional<domain::Period> dateRange_;
    std::optional<domain::AccountId> account_;
    EntryKindFilter kind_ = EntryKindFilter::All;
};

// Every entry in ledger.postedEntries() that query matches, in the Ledger's
// posting order (ascending PostingId) -- the one stable, append-only order
// the journal has. Results are not re-sorted by business date: a backdated
// entry appears where it was posted, and entries sharing a date keep their
// posting order. Each entry's lines are in their recorded order.
//
// Read-only (ledger is const). Returns copies, so results stay valid after
// the Ledger is later posted to or destroyed. Linear in the number of
// posted entries times the lines per entry.
std::vector<ledger::PostedJournalEntry> findJournalEntries(const ledger::Ledger& ledger, const JournalQuery& query);

} // namespace ledgercore::journalquery
