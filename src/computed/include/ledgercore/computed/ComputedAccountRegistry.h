#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ledgercore/computed/ComputedAccountDefinition.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/AccountResolver.h"
#include "ledgercore/formula/ComputedAccountName.h"

namespace ledgercore::computed {

// A mutable, incrementally-built collection of computed-account
// definitions -- the sole source of truth for `@name` references.
// Mirrors ChartOfAccounts's shape (a growing collection of named
// definitions, built up over configuration time) rather than
// TrialBalance's (a fresh snapshot regenerated on every read):
// computed-account definitions change rarely, closer to chart
// configuration than to a Ledger balance.
//
// Not copyable: like ChartOfAccounts, this has identity.
class ComputedAccountRegistry {
public:
    ComputedAccountRegistry() = default;

    ComputedAccountRegistry(const ComputedAccountRegistry&) = delete;
    ComputedAccountRegistry& operator=(const ComputedAccountRegistry&) = delete;

    // Parses formulaSource once (via ComputedAccountDefinition's
    // constructor) and stores the definition. Throws
    // ComputedAccountAlreadyDefinedException if name is already defined,
    // or formula::FormulaSyntaxException if formulaSource does not
    // parse.
    const ComputedAccountDefinition& define(formula::ComputedAccountName name, std::string formulaSource);

    const ComputedAccountDefinition* find(const formula::ComputedAccountName& name) const;

    // Insertion order.
    std::vector<const ComputedAccountDefinition*> definitions() const;

    // Evaluates name's formula against realResolver for every #code
    // reference, recursively resolving @name references against this
    // registry's own definitions, with cycle detection. Throws
    // UnknownComputedAccountException if name (or any @name it
    // transitively depends on) is not defined, FormulaCycleException on
    // a dependency cycle, FormulaEvaluationException if the formula's
    // result is not a Money value, or whatever formula::evaluate() /
    // realResolver otherwise throws (e.g. UnknownAccountReferenceException,
    // domain::CurrencyMismatchException, domain::MoneyOverflowException).
    domain::Money evaluate(const formula::ComputedAccountName& name, const formula::AccountResolver& realResolver) const;

private:
    std::unordered_map<std::string, std::unique_ptr<ComputedAccountDefinition>> definitionsByName_;
    std::vector<std::string> insertionOrder_;
};

} // namespace ledgercore::computed
