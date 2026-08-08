#pragma once

#include <string>
#include <vector>

#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/formula/ComputedAccountName.h"

namespace ledgercore::computed {

// A formula referenced a computed account (`@name`) that has not been
// defined in the ComputedAccountRegistry it is being evaluated against.
class UnknownComputedAccountException : public ledgercore::LedgerException {
public:
    explicit UnknownComputedAccountException(const std::string& message) : LedgerException(message) {}
};

// A computed account's dependency graph contains a cycle. Carries the
// complete cycle path (e.g. A -> B -> C -> A) as structured data in
// addition to the message, since the path is the entire point of this
// particular error.
class FormulaCycleException : public ledgercore::LedgerException {
public:
    FormulaCycleException(const std::string& message, std::vector<formula::ComputedAccountName> path)
        : LedgerException(message), path_(std::move(path)) {}

    const std::vector<formula::ComputedAccountName>& path() const noexcept { return path_; }

private:
    std::vector<formula::ComputedAccountName> path_;
};

// ComputedAccountRegistry::define() was called with a name that already
// has a definition.
class ComputedAccountAlreadyDefinedException : public ledgercore::LedgerException {
public:
    explicit ComputedAccountAlreadyDefinedException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::computed
