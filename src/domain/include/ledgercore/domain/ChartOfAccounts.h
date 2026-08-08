#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"

namespace ledgercore::domain {

// Aggregate root and sole factory for Account instances.
//
// ChartOfAccounts is the only class that can construct an Account (it is
// declared a friend in Account), which makes the following invariants
// enforceable in one place rather than trusted to every call site:
//   - Account codes are globally unique within this chart.
//   - Every Account has at most one parent (a tree, not a DAG).
//   - The tree can never contain a cycle -- not because of a runtime
//     check, but because addChildAccount() can only attach a child to a
//     parent that already exists, so no reachable cycle is constructible.
//   - A child Account always has the same AccountType as its parent --
//     addChildAccount() takes no AccountType parameter, so a mismatch
//     cannot be expressed, let alone rejected.
//   - addChildAccount() rejects a parent Account that does not belong to
//     this ChartOfAccounts (e.g. one obtained from a different chart),
//     since attaching to it would register the new account's code in
//     this chart's index while the Account itself lives in the other
//     chart's tree -- a dangling pointer once that chart is destroyed.
//
// ChartOfAccounts owns its top-level accounts directly; there is no
// synthetic root node, since a chart of accounts has no single real
// top-level account in accounting practice.
class ChartOfAccounts {
public:
    ChartOfAccounts() = default;

    ChartOfAccounts(const ChartOfAccounts&) = delete;
    ChartOfAccounts& operator=(const ChartOfAccounts&) = delete;

    Account& addRootAccount(AccountCode code, std::string name, AccountType type);

    // AccountType is deliberately not a parameter here: the child always
    // inherits parent.type().
    Account& addChildAccount(Account& parent, AccountCode code, std::string name);

    Account* findByCode(const AccountCode& code);
    const Account* findByCode(const AccountCode& code) const;

    // AccountId lookup for consumers that only have an AccountId (e.g. a
    // JournalEntryLine, which references accounts by AccountId rather than
    // AccountCode). Read-only: unlike findByCode(), there is no mutable
    // overload, since nothing outside ChartOfAccounts is allowed to mutate
    // an Account through this path.
    const Account* findById(AccountId id) const;

    bool contains(const AccountCode& code) const;

    std::vector<const Account*> rootAccounts() const;

private:
    AccountId nextAccountId();
    void registerAccount(Account* account);
    void ensureCodeIsUnique(const AccountCode& code) const;
    void ensureBelongsToThisChart(const Account& account) const;

    std::vector<std::unique_ptr<Account>> topLevelAccounts_;
    std::unordered_map<std::string, Account*> codeIndex_;
    std::unordered_map<std::uint64_t, Account*> idIndex_;
    std::uint64_t nextId_ = 1;
};

} // namespace ledgercore::domain
