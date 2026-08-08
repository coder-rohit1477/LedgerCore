#pragma once

namespace ledgercore::domain {

// Design decision (Phase 1, Step 1): AccountType is always inherited from
// the parent when a child Account is created -- ChartOfAccounts::
// addChildAccount() takes no AccountType parameter, so a child can never
// specify one independently. As a consequence, every branch of the Chart
// of Accounts, from a top-level account down to its deepest leaf,
// represents exactly one of these categories consistently.
enum class AccountType {
    Asset,
    Liability,
    Equity,
    Revenue,
    Expense
};

} // namespace ledgercore::domain
