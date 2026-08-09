#pragma once

#include <memory>

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/persistence/SessionStore.h"

namespace ledgercore::cli {

// Owns the three pieces of state one CLI run (a REPL session or a script
// run) shares across every command: a ChartOfAccounts, a Ledger fixed to
// one Currency, and a ComputedAccountRegistry. All three are in-memory
// only by default -- nothing here talks to a file unless replaceState()
// is called with an already-loaded candidate (see below); state is lost
// when the process exits unless the 'save' command was used first.
//
// This is the one facade the CLI needs: something has to own these three
// objects for the process's lifetime so commands can share them, and
// non-copyable, non-movable ChartOfAccounts/Ledger/ComputedAccountRegistry
// each already require exactly this kind of single owner. It is not a
// generic application-service layer -- it has no behavior of its own
// beyond owning these three objects, exposing them, and atomically
// swapping all three at once for 'load'.
//
// Each object is held through a unique_ptr specifically so that swap is
// possible at all: ChartOfAccounts/Ledger/ComputedAccountRegistry remain
// exactly as non-movable as they have always been (that is not changed,
// here or anywhere else) -- but a unique_ptr *handle* to one is itself
// movable, so replaceState() can hand over an entirely new set of three
// freshly-built objects via three plain pointer moves, none of which can
// fail partway.
class LedgerSession {
public:
    explicit LedgerSession(domain::Currency currency);

    LedgerSession(const LedgerSession&) = delete;
    LedgerSession& operator=(const LedgerSession&) = delete;

    domain::ChartOfAccounts& chart() noexcept { return *chart_; }
    const domain::ChartOfAccounts& chart() const noexcept { return *chart_; }

    ledger::Ledger& ledger() noexcept { return *ledger_; }
    const ledger::Ledger& ledger() const noexcept { return *ledger_; }

    computed::ComputedAccountRegistry& computedAccounts() noexcept { return *computedAccounts_; }
    const computed::ComputedAccountRegistry& computedAccounts() const noexcept { return *computedAccounts_; }

    // Always the *current* Ledger's own currency -- not a separately
    // cached copy -- so it stays correct across a replaceState() that
    // swaps in a Ledger fixed to a different currency than the one this
    // session started with.
    const domain::Currency& currency() const noexcept { return ledger_->currency(); }

    // Atomically replaces this session's entire state with an
    // already-fully-reconstructed candidate from persistence::load().
    // Every fallible step (parsing the snapshot, replaying it through
    // posting::post()) has already happened by the time a
    // persistence::LoadedSession exists at all -- persistence::load()
    // either throws (and this is never called) or returns a complete
    // candidate. The three moves below are unconditionally noexcept, so
    // once this is called it cannot leave the session partially
    // replaced: callers must call persistence::load() first and only
    // pass its result here after it has already succeeded.
    void replaceState(persistence::LoadedSession loaded) noexcept;

private:
    std::unique_ptr<domain::ChartOfAccounts> chart_;
    std::unique_ptr<ledger::Ledger> ledger_;
    std::unique_ptr<computed::ComputedAccountRegistry> computedAccounts_;
};

} // namespace ledgercore::cli
