#include "ledgercore/domain/ChartOfAccounts.h"

#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

Account& ChartOfAccounts::addRootAccount(AccountCode code, std::string name, AccountType type) {
    ensureCodeIsUnique(code);

    const AccountId id = nextAccountId();
    auto account = std::unique_ptr<Account>(new Account(id, std::move(code), std::move(name), type, nullptr));
    Account* raw = account.get();
    registerCode(raw);
    topLevelAccounts_.push_back(std::move(account));
    return *raw;
}

Account& ChartOfAccounts::addChildAccount(Account& parent, AccountCode code, std::string name) {
    ensureCodeIsUnique(code);

    const AccountId id = nextAccountId();
    Account* child = parent.addChild(id, std::move(code), std::move(name));
    registerCode(child);
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

void ChartOfAccounts::registerCode(Account* account) {
    codeIndex_.emplace(account->code().value(), account);
}

void ChartOfAccounts::ensureCodeIsUnique(const AccountCode& code) const {
    if (contains(code)) {
        throw DuplicateAccountCodeException("Account code already exists: " + code.value());
    }
}

} // namespace ledgercore::domain
