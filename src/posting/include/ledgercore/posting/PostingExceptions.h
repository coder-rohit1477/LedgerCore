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

// Thrown by post() for a JournalEntryKind::Closing entry that is not
// exactly a complete closing entry (see post() for the precise rules):
// wrong account types, a repeated account, more than one Equity
// destination, or a Revenue/Expense balance left non-zero as of the
// entry's closing cutoff. This keeps the Closing marker -- which income
// statements use to exclude an entry from activity -- from ever hiding
// or reshaping ordinary activity.
class InvalidClosingEntryException : public ledgercore::LedgerException {
public:
    explicit InvalidClosingEntryException(const std::string& message) : LedgerException(message) {}
};

// Thrown by post() when the entry's business date lies inside a Closed
// accounting period of the target Ledger ([start, end), see
// Ledger::closeAccountingPeriod). Applies to every entry kind, closing
// entries included; nothing is posted.
class ClosedPeriodPostingException : public ledgercore::LedgerException {
public:
    explicit ClosedPeriodPostingException(const std::string& message) : LedgerException(message) {}
};

// Thrown by posting::addChildAccount() when the would-be parent already
// has posting history in the Ledger: giving it a child would turn a
// posted leaf into a group account, which can no longer be a posting
// target -- orphaning its history from Trial Balance and making that
// history impossible to replay.
class PostedAccountCannotBecomeGroupException : public ledgercore::LedgerException {
public:
    explicit PostedAccountCannotBecomeGroupException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::posting
