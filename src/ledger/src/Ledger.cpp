#include "ledgercore/ledger/Ledger.h"

#include <chrono>
#include <utility>

namespace ledgercore::ledger {

Ledger::Ledger(domain::Currency currency) : currency_(std::move(currency)) {}

domain::Money Ledger::balance(domain::AccountId accountId) const {
    auto it = balances_.find(accountId.value());
    return it == balances_.end() ? domain::Money::zero(currency_) : it->second;
}

PostingId Ledger::commit(domain::JournalEntry entry,
                          std::vector<std::pair<domain::AccountId, domain::Money>> newBalances) {
    for (const auto& [accountId, newBalance] : newBalances) {
        balances_.insert_or_assign(accountId.value(), newBalance);
    }

    const PostingId id(nextPostingId_++);
    PostedJournalEntry posted(id, std::move(entry), std::chrono::system_clock::now());
    postedEntries_.push_back(std::move(posted));
    return id;
}

} // namespace ledgercore::ledger
