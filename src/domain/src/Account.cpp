#include "ledgercore/domain/Account.h"

#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

Account::Account(AccountId id, AccountCode code, std::string name, AccountType type, Account* parent)
    : id_(id), code_(std::move(code)), name_(std::move(name)), type_(type), parent_(parent) {
    if (name_.empty()) {
        throw InvalidAccountException("Account name must not be empty");
    }
}

Account* Account::addChild(AccountId id, AccountCode code, std::string name) {
    auto child = std::unique_ptr<Account>(new Account(id, std::move(code), std::move(name), type_, this));
    Account* raw = child.get();
    children_.push_back(std::move(child));
    return raw;
}

std::vector<const Account*> Account::children() const {
    std::vector<const Account*> result;
    result.reserve(children_.size());
    for (const auto& child : children_) {
        result.push_back(child.get());
    }
    return result;
}

} // namespace ledgercore::domain
