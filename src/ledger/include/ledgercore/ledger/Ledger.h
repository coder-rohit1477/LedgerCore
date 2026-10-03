#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/ledger/AccountingPeriod.h"
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
// The only way to change a Ledger's balances or history is
// posting::post(), granted access via the friend declaration below --
// mirroring how only ChartOfAccounts may construct or attach an Account.
// This single choke point is what guarantees balances_ and postedEntries_
// can never drift out of sync. The Ledger also owns its accounting
// periods (defineAccountingPeriod / closeAccountingPeriod): metadata that
// post() consults to refuse postings into a closed period, but which can
// never change a balance or a posted entry.
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

    // Whether any posted JournalEntry has a line referencing accountId.
    // Answered from postedEntries() -- the authoritative history -- not
    // from balance(): an account whose postings net to zero still has
    // history. Linear in the total number of posted lines.
    bool hasPostingHistory(domain::AccountId accountId) const noexcept;

    const std::vector<PostedJournalEntry>& postedEntries() const noexcept { return postedEntries_; }

    // Accounting periods: metadata restricting *which business dates* this
    // Ledger accepts new postings for. They never change a balance or a
    // posted entry, and reports ignore them.
    //
    // defineAccountingPeriod() adds an Open period. Throws
    // AccountingPeriodOverlapException if it overlaps or duplicates an
    // existing period; adjacent periods (one's end == the next's start)
    // and gaps between periods are allowed, and periods may be defined in
    // any order. Strong exception guarantee.
    void defineAccountingPeriod(const domain::Period& period);

    // Open -> Closed for the period with exactly these bounds. From then
    // on posting::post() rejects any entry whose date() lies in
    // [start, end). Throws UnknownAccountingPeriodException if no period
    // has exactly these bounds, AccountingPeriodAlreadyClosedException if
    // it is already closed. There is no reopen.
    void closeAccountingPeriod(const domain::Period& period);

    // Every defined period, ordered by ascending start (deterministic).
    const std::vector<AccountingPeriod>& accountingPeriods() const noexcept { return accountingPeriods_; }

    // The Closed period whose [start, end) contains date, or nullptr.
    const AccountingPeriod* closedPeriodContaining(std::chrono::system_clock::time_point date) const noexcept;

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
    std::vector<AccountingPeriod> accountingPeriods_;
    std::uint64_t nextPostingId_ = 1;
};

} // namespace ledgercore::ledger
