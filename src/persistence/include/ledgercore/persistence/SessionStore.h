#pragma once

#include <filesystem>
#include <memory>

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/ledger/Ledger.h"

namespace ledgercore::persistence {

// A fully reconstructed, self-contained accounting session, produced only
// by a load() call that replayed every persisted record successfully.
//
// domain::ChartOfAccounts, ledger::Ledger, and computed::ComputedAccountRegistry
// are each deliberately non-copyable and non-movable -- that is not changed
// here. LoadedSession instead owns each through a unique_ptr, so the
// *handle* is movable even though the underlying objects never are. This
// is what lets load() return a complete candidate by value: the three
// objects are allocated once, up front, and mutated in place through their
// own public APIs for the rest of the load; if any record fails to parse
// or replay, the partially-built objects are destroyed along with load()'s
// local unique_ptrs during stack unwinding, and no LoadedSession is ever
// constructed or observable by the caller. A caller that wants to swap a
// live session for a freshly loaded one gets an all-or-nothing handoff for
// free, with no partial-state window.
struct LoadedSession {
    std::unique_ptr<domain::ChartOfAccounts> chart;
    std::unique_ptr<ledger::Ledger> ledger;
    std::unique_ptr<computed::ComputedAccountRegistry> computedAccounts;
};

// Writes a complete, deterministic snapshot of chart/ledger/computedAccounts
// to path: chart accounts (by AccountCode, parent-before-child, mirroring
// the tree), ledger.postedEntries() in their exact original order (never
// re-sorted), and computedAccounts.definitions() in insertion order.
// Ledger cached balances, TrialBalance/BalanceSheet/IncomeStatement,
// PostingId, PostedJournalEntry::postedAt(), and AccountId numeric values
// are never written -- see the class-level rationale in this header and in
// SessionStore.cpp.
//
// The write is atomic with respect to the file at path: a temporary file
// in the same directory is written, flushed, and closed, then renamed over
// path only once complete. A write failure removes the temporary file
// (best effort) and leaves any pre-existing file at path untouched, then
// throws PersistenceException.
//
// Throws PersistenceException if an account or computed-account name
// referenced by an unquoted field (an AccountCode, or a ComputedAccountName)
// contains whitespace or a double-quote character, since the file format's
// grammar cannot represent that without ambiguity, or if a posted line's
// AccountId cannot be resolved back to an account in chart (chart/ledger
// mismatch).
void save(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
          const computed::ComputedAccountRegistry& computedAccounts, const std::filesystem::path& path);

// Reads and fully replays a snapshot from path. Reconstruction order is
// chart -> journal entries (via posting::post(), never bypassed) -> computed
// account definitions, exactly as approved. AccountCode is the persisted
// account identity; AccountId is never read from the file -- each journal
// line's AccountCode is resolved through the freshly-reconstructed chart to
// obtain the AccountId posting::post() actually needs.
//
// Throws PersistenceVersionException if the file declares an unsupported
// format version, PersistenceFormatException for any structural problem
// (missing/malformed header, wrong field count, malformed integer, unknown
// record or AccountType token, unterminated quoted string, a truncated
// file, or an ACCOUNT CHILD record naming a parent AccountCode this file
// has not already declared), or the relevant existing domain/posting/formula
// exception (e.g. domain::DuplicateAccountCodeException,
// domain::UnbalancedJournalEntryException, posting::AccountNotFoundException,
// formula::FormulaSyntaxException, domain::InvalidCurrencyException) for a
// syntactically well-formed record that violates an accounting invariant --
// persistence never re-validates what those types already validate.
//
// Never returns a partially reconstructed result: a LoadedSession is
// produced only after the entire file has been read and replayed without
// error.
LoadedSession load(const std::filesystem::path& path);

} // namespace ledgercore::persistence
