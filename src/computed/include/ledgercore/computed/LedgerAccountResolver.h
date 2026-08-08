#pragma once

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/AccountResolver.h"
#include "ledgercore/ledger/Ledger.h"

namespace ledgercore::computed {

// Resolves a formula's #code references against a specific
// (ChartOfAccounts, Ledger) pair: chart.findByCode(code) -> AccountId ->
// ledger.balance(id). Rejects unknown codes and non-leaf (group)
// accounts -- a group account has no direct posted balance, the same
// reasoning PostingEngine/TrialBalance already apply -- both via
// formula::UnknownAccountReferenceException, so the Formula Engine's
// existing error handling applies unchanged.
//
// This is a plain reference-holding adapter, not a copy and not a
// persisted snapshot: it binds to chart/ledger for its entire lifetime.
// Callers are responsible for not mutating the referenced Ledger while a
// resolution using this resolver is in progress -- the same precondition
// every other const Ledger& reader in LedgerCore (e.g. TrialBalance)
// already carries.
class LedgerAccountResolver : public formula::AccountResolver {
public:
    LedgerAccountResolver(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger);

    domain::Money resolve(const domain::AccountCode& code) const override;

private:
    const domain::ChartOfAccounts& chart_;
    const ledger::Ledger& ledger_;
};

} // namespace ledgercore::computed
