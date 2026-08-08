#include "ledgercore/computed/LedgerAccountResolver.h"

#include "ledgercore/domain/Account.h"
#include "ledgercore/formula/FormulaExceptions.h"

namespace ledgercore::computed {

LedgerAccountResolver::LedgerAccountResolver(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger)
    : chart_(chart), ledger_(ledger) {}

domain::Money LedgerAccountResolver::resolve(const domain::AccountCode& code) const {
    const domain::Account* account = chart_.findByCode(code);
    if (account == nullptr) {
        throw formula::UnknownAccountReferenceException("No account found for AccountCode " + code.value());
    }
    if (!account->isLeaf()) {
        throw formula::UnknownAccountReferenceException(
            "Cannot reference non-leaf (group) account in a formula: " + code.value());
    }
    return ledger_.balance(account->id());
}

} // namespace ledgercore::computed
