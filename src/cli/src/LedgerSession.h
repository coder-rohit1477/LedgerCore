#pragma once

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/ledger/Ledger.h"

namespace ledgercore::cli {

// Owns the three pieces of state one CLI run (a REPL session or a script
// run) shares across every command: a ChartOfAccounts, a Ledger fixed to
// one Currency, and a ComputedAccountRegistry. All three are in-memory
// only -- LedgerSession introduces no persistence, and its state is lost
// when the process exits.
//
// This is the one facade the CLI needs: something has to own these three
// objects for the process's lifetime so commands can share them, and
// non-copyable ChartOfAccounts/Ledger/ComputedAccountRegistry each already
// require exactly this kind of single owner. It is not a generic
// application-service layer -- it has no behavior of its own beyond
// owning these three objects and exposing them.
class LedgerSession {
public:
    explicit LedgerSession(domain::Currency currency);

    LedgerSession(const LedgerSession&) = delete;
    LedgerSession& operator=(const LedgerSession&) = delete;

    domain::ChartOfAccounts& chart() noexcept { return chart_; }
    const domain::ChartOfAccounts& chart() const noexcept { return chart_; }

    ledger::Ledger& ledger() noexcept { return ledger_; }
    const ledger::Ledger& ledger() const noexcept { return ledger_; }

    computed::ComputedAccountRegistry& computedAccounts() noexcept { return computedAccounts_; }
    const computed::ComputedAccountRegistry& computedAccounts() const noexcept { return computedAccounts_; }

    const domain::Currency& currency() const noexcept { return currency_; }

private:
    domain::Currency currency_;
    domain::ChartOfAccounts chart_;
    ledger::Ledger ledger_;
    computed::ComputedAccountRegistry computedAccounts_;
};

} // namespace ledgercore::cli
