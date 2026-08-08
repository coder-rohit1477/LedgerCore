#pragma once

#include <chrono>
#include <utility>

#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/ledger/PostingId.h"

namespace ledgercore::ledger {

class Ledger;

// The fact that a validated JournalEntry was actually applied to a Ledger:
// a Ledger-assigned PostingId, a copy of the JournalEntry that was posted,
// and the time the Ledger recorded it (distinct from the entry's own
// business date()).
//
// This is the "proposed vs. posted" distinction: JournalEntry is a
// balanced, validated, but not-yet-applied value; PostedJournalEntry is
// the record that it was applied, with identity and a timestamp that
// JournalEntry itself deliberately does not carry.
//
// Only Ledger constructs a PostedJournalEntry, since only Ledger assigns
// PostingId and knows the moment of posting -- mirrors how only
// ChartOfAccounts constructs an Account.
class PostedJournalEntry {
public:
    PostingId id() const noexcept { return id_; }
    const domain::JournalEntry& entry() const noexcept { return entry_; }
    std::chrono::system_clock::time_point postedAt() const noexcept { return postedAt_; }

private:
    friend class Ledger;

    PostedJournalEntry(PostingId id, domain::JournalEntry entry, std::chrono::system_clock::time_point postedAt)
        : id_(id), entry_(std::move(entry)), postedAt_(postedAt) {}

    PostingId id_;
    domain::JournalEntry entry_;
    std::chrono::system_clock::time_point postedAt_;
};

} // namespace ledgercore::ledger
