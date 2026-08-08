#include "ledgercore/domain/ChartOfAccounts.h"

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

Account& ChartOfAccounts::addChildAccount(Account& parent, AccountCode code, std::string name) {
    ensureBelongsToThisChart(parent);
    ensureCodeIsUnique(code);

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
