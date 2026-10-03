#include "ledgercore/computed/ComputedAccountRegistry.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ledgercore/computed/ComputedAccountExceptions.h"
#include "ledgercore/formula/Evaluator.h"
#include "ledgercore/formula/FormulaExceptions.h"

namespace ledgercore::computed {

namespace {

// Exception-safe push/pop of the per-evaluation resolution stack used
// for cycle detection: pushes name on construction, pops on destruction
// (including via exception unwinding), so the stack is always left
// correctly balanced regardless of how resolution completes.
class StackGuard {
public:
    StackGuard(std::vector<formula::ComputedAccountName>& stack, formula::ComputedAccountName name) : stack_(stack) {
        stack_.push_back(std::move(name));
    }

    ~StackGuard() { stack_.pop_back(); }

    StackGuard(const StackGuard&) = delete;
    StackGuard& operator=(const StackGuard&) = delete;

private:
    std::vector<formula::ComputedAccountName>& stack_;
};

// Adds a definition's syntax-tree depth to the session's running
// evaluation depth for the duration of its evaluation (exception-safe).
class DepthGuard {
public:
    DepthGuard(std::size_t& depth, std::size_t added) : depth_(depth), added_(added) { depth_ += added_; }
    ~DepthGuard() { depth_ -= added_; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;

private:
    std::size_t& depth_;
    std::size_t added_;
};

// Resolves @name references for exactly one top-level
// ComputedAccountRegistry::evaluate() call: owns the DFS "currently
// resolving" stack that both detects cycles and, via formula::evaluate's
// recursive calls back into this same resolver, drives lazy recursive
// evaluation of the dependency graph.
//
// Freshly constructed per call, so no state persists across separate
// evaluate() calls. Within one call each computed account is evaluated at
// most once: a value fully resolved on one branch of a diamond dependency
// is memoized and reused on the others (it is popped off the stack as soon
// as its own resolution completes, so reusing it is not a cycle).
class ResolutionSession : public formula::ComputedAccountResolver {
public:
    ResolutionSession(const ComputedAccountRegistry& registry, const formula::AccountResolver& realResolver)
        : registry_(registry), realResolver_(realResolver) {}

    domain::Money resolve(const formula::ComputedAccountName& name) const override {
        for (const formula::ComputedAccountName& visiting : stack_) {
            if (visiting == name) {
                throw buildCycleException(name);
            }
        }

        // Already fully resolved earlier in this session (a shared
        // dependency reached again through another branch): reuse it, so
        // each computed account is evaluated at most once per evaluate()
        // call. A resolved account can never be on the stack, so this does
        // not affect cycle detection above.
        const auto cached = resolved_.find(name.value());
        if (cached != resolved_.end()) {
            return cached->second;
        }

        const ComputedAccountDefinition* definition = registry_.find(name);
        if (definition == nullptr) {
            throw UnknownComputedAccountException("Unknown computed account reference: @" + name.value());
        }

        if (evaluationDepth_ + definition->astDepth() > ComputedAccountRegistry::kMaxEvaluationDepth) {
            throw ComputedAccountDepthExceededException(
                "Computed account '" + name.value() + "' nests formulas deeper than the evaluation limit of "
                + std::to_string(ComputedAccountRegistry::kMaxEvaluationDepth) + " levels");
        }

        const StackGuard guard(stack_, name);
        const DepthGuard depthGuard(evaluationDepth_, definition->astDepth());
        const formula::FormulaValue result = formula::evaluate(definition->ast(), realResolver_, *this);

        if (!result.isMoney()) {
            throw formula::FormulaEvaluationException("Computed account '" + name.value()
                                                        + "' does not evaluate to a Money value");
        }
        resolved_.emplace(name.value(), result.asMoney());
        return result.asMoney();
    }

private:
    FormulaCycleException buildCycleException(const formula::ComputedAccountName& repeated) const {
        std::vector<formula::ComputedAccountName> path(stack_);
        path.push_back(repeated);

        std::string message = "Computed account dependency cycle detected: ";
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (i != 0) {
                message += " -> ";
            }
            message += path[i].value();
        }

        return FormulaCycleException(message, std::move(path));
    }

    const ComputedAccountRegistry& registry_;
    const formula::AccountResolver& realResolver_;
    mutable std::vector<formula::ComputedAccountName> stack_;
    // Sum of astDepth() over every definition on stack_.
    mutable std::size_t evaluationDepth_ = 0;
    // Memo of fully resolved accounts, owned by this one session: the
    // ledger and registry cannot change during a single evaluate() call,
    // and nothing is shared across calls.
    mutable std::unordered_map<std::string, domain::Money> resolved_;
};

} // namespace

const ComputedAccountDefinition& ComputedAccountRegistry::define(formula::ComputedAccountName name,
                                                                   std::string formulaSource) {
    const std::string key = name.value();
    if (definitionsByName_.find(key) != definitionsByName_.end()) {
        throw ComputedAccountAlreadyDefinedException("Computed account already defined: " + key);
    }

    // Every allocation happens before the first visible change, and the
    // final append cannot throw (capacity reserved, string moved), so a
    // definition is either fully registered -- found by find() and listed
    // by definitions() -- or not at all.
    std::string orderKey = key;
    if (insertionOrder_.size() == insertionOrder_.capacity()) {
        insertionOrder_.reserve(std::max<std::size_t>(8, insertionOrder_.capacity() * 2));
    }
    auto definition = std::make_unique<ComputedAccountDefinition>(std::move(name), std::move(formulaSource));
    ComputedAccountDefinition* raw = definition.get();
    definitionsByName_.emplace(key, std::move(definition));
    insertionOrder_.push_back(std::move(orderKey));
    return *raw;
}

const ComputedAccountDefinition* ComputedAccountRegistry::find(const formula::ComputedAccountName& name) const {
    auto it = definitionsByName_.find(name.value());
    return it == definitionsByName_.end() ? nullptr : it->second.get();
}

std::vector<const ComputedAccountDefinition*> ComputedAccountRegistry::definitions() const {
    std::vector<const ComputedAccountDefinition*> result;
    result.reserve(insertionOrder_.size());
    for (const std::string& key : insertionOrder_) {
        result.push_back(definitionsByName_.at(key).get());
    }
    return result;
}

domain::Money ComputedAccountRegistry::evaluate(const formula::ComputedAccountName& name,
                                                  const formula::AccountResolver& realResolver) const {
    ResolutionSession session(*this, realResolver);
    return session.resolve(name);
}

} // namespace ledgercore::computed
