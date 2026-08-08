#pragma once

#include <string>
#include <utility>

#include "ledgercore/formula/FormulaExceptions.h"

namespace ledgercore::formula {

// The identifier for a computed-account reference (`@name` in formula
// source). Deliberately a distinct type from domain::AccountCode:
// computed accounts are not part of the real chart of accounts, so their
// names must never be confusable with -- or accidentally interchangeable
// with -- a real AccountCode, even though both are validated non-empty
// strings.
class ComputedAccountName {
public:
    explicit ComputedAccountName(std::string value) : value_(std::move(value)) {
        if (value_.empty()) {
            throw FormulaEvaluationException("ComputedAccountName must not be empty");
        }
    }

    const std::string& value() const noexcept { return value_; }

    friend bool operator==(const ComputedAccountName& lhs, const ComputedAccountName& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const ComputedAccountName& lhs, const ComputedAccountName& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    std::string value_;
};

} // namespace ledgercore::formula
