#include "LedgerSession.h"

#include <memory>
#include <utility>

namespace ledgercore::cli {

LedgerSession::LedgerSession(domain::Currency currency)
    : chart_(std::make_unique<domain::ChartOfAccounts>()),
      ledger_(std::make_unique<ledger::Ledger>(std::move(currency))),
      computedAccounts_(std::make_unique<computed::ComputedAccountRegistry>()) {}

void LedgerSession::replaceState(persistence::LoadedSession loaded) noexcept {
    chart_ = std::move(loaded.chart);
    ledger_ = std::move(loaded.ledger);
    computedAccounts_ = std::move(loaded.computedAccounts);
}

} // namespace ledgercore::cli
