#pragma once

#include <stdexcept>
#include <string>

namespace ledgercore {

// Root of the exception hierarchy for all LedgerCore errors. Every module
// (domain, and later ledger/formula) derives its specific exception types
// from this one, so calling code can catch ledgercore::LedgerException to
// handle any domain-rule violation without knowing which module raised it.
class LedgerException : public std::runtime_error {
public:
    explicit LedgerException(const std::string& message) : std::runtime_error(message) {}
};

} // namespace ledgercore

namespace ledgercore::domain {

// Thrown when an Account attribute violates a local invariant
// (empty name, empty code).
class InvalidAccountException : public ledgercore::LedgerException {
public:
    explicit InvalidAccountException(const std::string& message) : LedgerException(message) {}
};

// Thrown by ChartOfAccounts when addRootAccount()/addChildAccount() is
// given an AccountCode that already exists in this chart.
class DuplicateAccountCodeException : public ledgercore::LedgerException {
public:
    explicit DuplicateAccountCodeException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::domain
