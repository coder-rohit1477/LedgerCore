#pragma once

#include <string>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::posting {

// Thrown when a JournalEntryLine references an AccountId that does not
// exist in the target ChartOfAccounts. Ledger has no knowledge of
// ChartOfAccounts, so this can only be detected -- and thrown -- here.
class AccountNotFoundException : public ledgercore::LedgerException {
public:
    explicit AccountNotFoundException(const std::string& message) : LedgerException(message) {}
};

// Thrown when a JournalEntryLine targets a non-leaf (group) account.
// Group accounts are aggregation nodes only; only Account::isLeaf()
// accounts may receive postings.
class InvalidPostingTargetException : public ledgercore::LedgerException {
public:
    explicit InvalidPostingTargetException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::posting
