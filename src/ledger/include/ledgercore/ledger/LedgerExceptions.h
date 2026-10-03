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

// Ledger::defineAccountingPeriod(): the new period overlaps (or
// duplicates) an existing one. Every business date belongs to at most one
// accounting period, so no date can ever match two conflicting states.
class AccountingPeriodOverlapException : public ledgercore::LedgerException {
public:
    explicit AccountingPeriodOverlapException(const std::string& message) : LedgerException(message) {}
};

// Ledger::closeAccountingPeriod(): no defined period has exactly the given
// [start, end) bounds.
class UnknownAccountingPeriodException : public ledgercore::LedgerException {
public:
    explicit UnknownAccountingPeriodException(const std::string& message) : LedgerException(message) {}
};

// Ledger::closeAccountingPeriod(): the period is already closed. Closing is
// a one-way transition; there is no reopen.
class AccountingPeriodAlreadyClosedException : public ledgercore::LedgerException {
public:
    explicit AccountingPeriodAlreadyClosedException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::ledger
