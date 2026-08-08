#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/ledger/PostingId.h"

// Forward declaration only -- Ledger does not depend on ChartOfAccounts.
// This name is needed solely to name the parameter type of the single
// function granted write access below (see the friend declaration in
// Ledger).
namespace ledgercore::domain {
class ChartOfAccounts;
} // namespace ledgercore::domain

namespace ledgercore::ledger {
class Ledger;
} // namespace ledgercore::ledger

// Forward declaration of PostingEngine's entry point, needed so Ledger can
// grant it (and only it) write access below. Declaring it here does not
// make ledger depend on posting: no posting header is included, and the
// ledger library does not link against the posting library.
namespace ledgercore::posting {
ledgercore::ledger::PostingId post(const ledgercore::domain::JournalEntry& entry,
                                    const ledgercore::domain::ChartOfAccounts& chart,
                                    ledgercore::ledger::Ledger& ledger);
} // namespace ledgercore::posting

namespace ledgercore::ledger {

// Posted-state truth for one Chart of Accounts: a single fixed Currency, a
// cached signed Money balance per AccountId, and an append-only history of
// every JournalEntry actually applied.
//
// Ledger holds no AccountType logic and no reference to ChartOfAccounts --
// both belong to PostingEngine, the only component with both a
// JournalEntry and a specific chart to check it against (see
// posting::post()).
//
// The only way to mutate a Ledger is posting::post(), granted access via
// the friend declaration below -- mirroring how only ChartOfAccounts may
// construct or attach an Account. This single choke point is what
// guarantees balances_ and postedEntries_ can never drift out of sync.
class Ledger {
public:
    explicit Ledger(domain::Currency currency);

    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;

    const domain::Currency& currency() const noexcept { return currency_; }

    // Returns Money::zero(currency()) for an account with no posted
    // activity, so callers never need to distinguish "missing balance"
    // from "zero balance".
    domain::Money balance(domain::AccountId accountId) const;

    const std::vector<PostedJournalEntry>& postedEntries() const noexcept { return postedEntries_; }

private:
    friend PostingId posting::post(const domain::JournalEntry& entry,
                                    const domain::ChartOfAccounts& chart,
                                    Ledger& ledger);

    // Applies already-computed, already-checked new balances and records
    // exactly one PostedJournalEntry. By the time this runs, every
    // fallible step (account existence, posting target, currency match,
    // Money overflow) has already succeeded in posting::post(), so nothing
    // here can fail for a business reason -- there is no rollback to
    // implement.
    PostingId commit(domain::JournalEntry entry, std::vector<std::pair<domain::AccountId, domain::Money>> newBalances);

    domain::Currency currency_;
    std::unordered_map<std::uint64_t, domain::Money> balances_;
    std::vector<PostedJournalEntry> postedEntries_;
    std::uint64_t nextPostingId_ = 1;
};

} // namespace ledgercore::ledger
