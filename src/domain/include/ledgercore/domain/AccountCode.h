#pragma once

#include <string>
#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

// The business-facing account code (e.g. "1110"). Validated non-empty on
// construction and immutable thereafter, so any AccountCode object that
// exists is guaranteed valid -- callers never need to re-check it.
class AccountCode {
public:
    explicit AccountCode(std::string value) : value_(std::move(value)) {
        if (value_.empty()) {
            throw InvalidAccountException("Account code must not be empty");
        }
    }

    const std::string& value() const noexcept { return value_; }

    friend bool operator==(const AccountCode& lhs, const AccountCode& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const AccountCode& lhs, const AccountCode& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    std::string value_;
};

} // namespace ledgercore::domain
