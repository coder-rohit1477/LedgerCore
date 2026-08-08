#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"

namespace ledgercore::domain {

// A node in the Chart of Accounts tree.
//
// Account is immutable after construction: identity, code, name, type,
// and parent linkage never change, and there are no setters. Only
// ChartOfAccounts may construct an Account or attach a child to one (see
// the `friend` declaration below); this is what lets ChartOfAccounts
// enforce invariants -- unique codes, a single parent, AccountType
// inheritance -- at one choke point instead of trusting every call site.
//
// Design decision (Phase 1, Step 1): a child Account always inherits its
// parent's AccountType. addChild() takes no AccountType parameter, so a
// mismatched type is not just rejected -- it cannot be expressed. This
// guarantees every branch of the tree represents exactly one accounting
// category from root to leaf.
//
// Account holds no balance and no posting logic. Both belong to the
// Ledger module, which does not exist yet in this phase.
class Account {
public:
    AccountId id() const noexcept { return id_; }
    const AccountCode& code() const noexcept { return code_; }
    const std::string& name() const noexcept { return name_; }
    AccountType type() const noexcept { return type_; }

    const Account* parent() const noexcept { return parent_; }
    bool isRoot() const noexcept { return parent_ == nullptr; }
    bool isLeaf() const noexcept { return children_.empty(); }

    std::vector<const Account*> children() const;

private:
    friend class ChartOfAccounts;

    Account(AccountId id, AccountCode code, std::string name, AccountType type, Account* parent);

    // Constructs a child owned by this Account, inheriting this
    // Account's type. Returns a non-owning pointer to the new child.
    Account* addChild(AccountId id, AccountCode code, std::string name);

    AccountId id_;
    AccountCode code_;
    std::string name_;
    AccountType type_;
    Account* parent_;
    std::vector<std::unique_ptr<Account>> children_;
};

} // namespace ledgercore::domain
