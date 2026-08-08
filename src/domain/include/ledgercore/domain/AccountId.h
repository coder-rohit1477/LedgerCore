#pragma once

#include <cstdint>

namespace ledgercore::domain {

// Stable, system-assigned identity for an Account, distinct from
// AccountCode. Only ChartOfAccounts generates values of this type.
//
// This exists separately from AccountCode so that a future module (e.g.
// Journal Entry) can reference an account by something that survives a
// business-code renumbering -- if journal lines referenced AccountCode
// directly, renaming "1110" to "1115" would silently orphan history.
class AccountId {
public:
    explicit AccountId(std::uint64_t value) noexcept : value_(value) {}

    std::uint64_t value() const noexcept { return value_; }

    friend bool operator==(const AccountId& lhs, const AccountId& rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const AccountId& lhs, const AccountId& rhs) noexcept {
        return !(lhs == rhs);
    }

private:
    std::uint64_t value_;
};

} // namespace ledgercore::domain
