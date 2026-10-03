#include "ledgercore/domain/ChartOfAccounts.h"

#include <string>
#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

Account& ChartOfAccounts::addRootAccount(AccountCode code, std::string name, AccountType type) {
    ensureCodeIsUnique(code);

    const AccountId id = nextAccountId();
    auto account = std::unique_ptr<Account>(new Account(id, std::move(code), std::move(name), type, nullptr));
    Account* raw = account.get();
    registerAccount(raw);
    topLevelAccounts_.push_back(std::move(account));
    return *raw;
}

ChartOfAccounts::~ChartOfAccounts() {
    // Post-order teardown without recursion or allocation: descend to a
    // leaf, destroy it by popping it off its parent's children (it has no
    // children of its own, so its destructor does not recurse), then
    // continue from the parent.
    for (std::unique_ptr<Account>& root : topLevelAccounts_) {
        Account* node = root.get();
        while (node != nullptr) {
            if (!node->children_.empty()) {
                node = node->children_.back().get();
                continue;
            }
            Account* parent = node->parent_;
            if (parent != nullptr) {
                parent->children_.pop_back();
            } else {
                root.reset();
            }
            node = parent;
        }
    }
}

Account& ChartOfAccounts::addChildAccount(Account& parent, AccountCode code, std::string name) {
    ensureBelongsToThisChart(parent);
    ensureCodeIsUnique(code);
    if (parent.depth_ >= kMaxDepth) {
        throw ChartDepthExceededException("Cannot add a child to account " + parent.code().value()
                                          + ": the account tree may be at most " + std::to_string(kMaxDepth)
                                          + " levels deep");
    }

    const AccountId id = nextAccountId();
    Account* child = parent.addChild(id, std::move(code), std::move(name));
    registerAccount(child);
    return *child;
}

const Account* ChartOfAccounts::findByCode(const AccountCode& code) const {
    auto it = codeIndex_.find(code.value());
    return it == codeIndex_.end() ? nullptr : it->second;
}

Account* ChartOfAccounts::findByCode(const AccountCode& code) {
    // Standard const-overload-calls-const-then-const_casts-back idiom:
    // safe here because *this is genuinely non-const in this overload.
    return const_cast<Account*>(static_cast<const ChartOfAccounts&>(*this).findByCode(code));
}

const Account* ChartOfAccounts::findById(AccountId id) const {
    auto it = idIndex_.find(id.value());
    return it == idIndex_.end() ? nullptr : it->second;
}

bool ChartOfAccounts::contains(const AccountCode& code) const {
    return codeIndex_.find(code.value()) != codeIndex_.end();
}

std::vector<const Account*> ChartOfAccounts::rootAccounts() const {
    std::vector<const Account*> result;
    result.reserve(topLevelAccounts_.size());
    for (const auto& account : topLevelAccounts_) {
        result.push_back(account.get());
    }
    return result;
}

void ChartOfAccounts::forEachAccountPreOrder(
    const std::function<void(const Account&, std::size_t depth)>& visit) const {
    // Explicit stack; children pushed in reverse so they pop in insertion
    // order, reproducing the recursive pre-order exactly.
    std::vector<std::pair<const Account*, std::size_t>> pending;
    for (auto root = topLevelAccounts_.rbegin(); root != topLevelAccounts_.rend(); ++root) {
        pending.emplace_back(root->get(), 1);
    }
    while (!pending.empty()) {
        const auto [account, depth] = pending.back();
        pending.pop_back();
        visit(*account, depth);
        for (auto child = account->children_.rbegin(); child != account->children_.rend(); ++child) {
            pending.emplace_back(child->get(), depth + 1);
        }
    }
}

AccountId ChartOfAccounts::nextAccountId() {
    return AccountId(nextId_++);
}

void ChartOfAccounts::registerAccount(Account* account) {
    codeIndex_.emplace(account->code().value(), account);
    idIndex_.emplace(account->id().value(), account);
}

void ChartOfAccounts::ensureCodeIsUnique(const AccountCode& code) const {
    if (contains(code)) {
        throw DuplicateAccountCodeException("Account code already exists: " + code.value());
    }
}

void ChartOfAccounts::ensureBelongsToThisChart(const Account& account) const {
    auto it = codeIndex_.find(account.code().value());
    if (it == codeIndex_.end() || it->second != &account) {
        throw ForeignAccountException("Account does not belong to this ChartOfAccounts: " + account.code().value());
    }
}

} // namespace ledgercore::domain
