#pragma once

#include <cstdint>

namespace ledgercore::ledger {

// Stable identity for one posted transaction within a single Ledger,
// distinct from any identity JournalEntry itself might gain in a future
// phase. Ledger-assigned and monotonically increasing; only Ledger
// constructs values of this type (see Ledger::commit()).
//
// JournalEntry deliberately has no ID of its own (Phase 3 decision) --
// this is the identity that Phase 3's header anticipated: it belongs to
// the fact that an entry was posted, not to the entry itself.
class PostingId {
public:
    explicit PostingId(std::uint64_t value) noexcept : value_(value) {}

    std::uint64_t value() const noexcept { return value_; }

    friend bool operator==(const PostingId& lhs, const PostingId& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const PostingId& lhs, const PostingId& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    std::uint64_t value_;
};

} // namespace ledgercore::ledger
