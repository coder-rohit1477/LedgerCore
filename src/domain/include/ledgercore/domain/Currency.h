#pragma once

#include <string>
#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

// A currency code in the ISO 4217 alpha-3 shape (e.g. "USD"). Validated
// as exactly three uppercase ASCII letters on construction and immutable
// thereafter, so any Currency object that exists is guaranteed
// well-formed.
//
// This checks format only -- it does not maintain a registry of
// real-world currencies. Nothing in LedgerCore currently needs to
// validate "is this a real currency," only that two Money values are
// being combined or compared under a consistent code.
class Currency {
public:
    explicit Currency(std::string code) : code_(std::move(code)) {
        if (code_.size() != 3) {
            throw InvalidCurrencyException("Currency code must be exactly 3 letters: " + code_);
        }
        for (char c : code_) {
            if (c < 'A' || c > 'Z') {
                throw InvalidCurrencyException("Currency code must be uppercase ASCII letters: " + code_);
            }
        }
    }

    const std::string& code() const noexcept { return code_; }

    friend bool operator==(const Currency& lhs, const Currency& rhs) noexcept {
        return lhs.code_ == rhs.code_;
    }

    friend bool operator!=(const Currency& lhs, const Currency& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    std::string code_;
};

} // namespace ledgercore::domain
