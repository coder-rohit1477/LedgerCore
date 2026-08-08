#pragma once

#include <string>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::ledger {

// Thrown when a JournalEntry's currency does not match the fixed Currency
// of the Ledger it is being posted into. A Ledger's Currency is part of
// its own invariant (fixed at construction), which is why this exception
// lives here rather than among the account/chart-lookup exceptions in
// PostingExceptions.h.
class LedgerCurrencyMismatchException : public ledgercore::LedgerException {
public:
    explicit LedgerCurrencyMismatchException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::ledger
