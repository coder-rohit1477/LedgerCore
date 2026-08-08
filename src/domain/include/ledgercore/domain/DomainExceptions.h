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

// Thrown by ChartOfAccounts::addChildAccount() when the supplied parent
// Account does not belong to this ChartOfAccounts instance (e.g. it was
// obtained from a different chart).
class ForeignAccountException : public ledgercore::LedgerException {
public:
    explicit ForeignAccountException(const std::string& message) : LedgerException(message) {}
};

// Thrown when a Currency code is not exactly three uppercase ASCII
// letters (the ISO 4217 alpha-3 shape).
class InvalidCurrencyException : public ledgercore::LedgerException {
public:
    explicit InvalidCurrencyException(const std::string& message) : LedgerException(message) {}
};

// Thrown by Money::fromMajorUnits() when minorUnitsPart is out of range,
// or its sign conflicts with majorUnits's sign (an ambiguous amount).
class InvalidMoneyException : public ledgercore::LedgerException {
public:
    explicit InvalidMoneyException(const std::string& message) : LedgerException(message) {}
};

// Thrown when a Money operation combines or orders two values that have
// different currencies and therefore have no well-defined result.
class CurrencyMismatchException : public ledgercore::LedgerException {
public:
    explicit CurrencyMismatchException(const std::string& message) : LedgerException(message) {}
};

// Thrown when a Money arithmetic operation would overflow or underflow
// the representable range of std::int64_t, including negating the
// minimum representable value.
class MoneyOverflowException : public ledgercore::LedgerException {
public:
    explicit MoneyOverflowException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::domain
