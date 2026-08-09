#include "LedgerSession.h"

#include <utility>

namespace ledgercore::cli {

LedgerSession::LedgerSession(domain::Currency currency)
    : currency_(currency), chart_(), ledger_(std::move(currency)), computedAccounts_() {}

} // namespace ledgercore::cli
