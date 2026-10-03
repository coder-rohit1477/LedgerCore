#include "ledgercore/ledger/Ledger.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "ledgercore/domain/DateFormatting.h"
#include "ledgercore/ledger/LedgerExceptions.h"

namespace ledgercore::ledger {

Ledger::Ledger(domain::Currency currency) : currency_(std::move(currency)) {}

domain::Money Ledger::balance(domain::AccountId accountId) const {
    auto it = balances_.find(accountId.value());
    return it == balances_.end() ? domain::Money::zero(currency_) : it->second;
}

bool Ledger::hasPostingHistory(domain::AccountId accountId) const noexcept {
    // Invariant: commit() stores a balance for every distinct account on a
    // posted entry's lines (even one whose lines net to zero) and nothing
    // ever erases one -- so a balance entry exists exactly when the account
    // appears somewhere in postedEntries_.
    return balances_.find(accountId.value()) != balances_.end();
}

namespace {

std::string describe(const domain::Period& period) {
    return "[" + domain::formatUtc(period.start()) + ", " + domain::formatUtc(period.end()) + ")";
}

bool sameBounds(const domain::Period& lhs, const domain::Period& rhs) noexcept {
    return lhs.start() == rhs.start() && lhs.end() == rhs.end();
}

} // namespace

void Ledger::defineAccountingPeriod(const domain::Period& period) {
    for (const AccountingPeriod& existing : accountingPeriods_) {
        const domain::Period& other = existing.period();
        if (period.start() < other.end() && other.start() < period.end()) {
            throw AccountingPeriodOverlapException("Accounting period " + describe(period)
                                                   + " overlaps existing accounting period " + describe(other));
        }
    }
    const auto position = std::upper_bound(
        accountingPeriods_.begin(), accountingPeriods_.end(), period.start(),
        [](std::chrono::system_clock::time_point start, const AccountingPeriod& candidate) {
            return start < candidate.period().start();
        });
    accountingPeriods_.insert(position, AccountingPeriod(period));
}

void Ledger::closeAccountingPeriod(const domain::Period& period) {
    for (AccountingPeriod& existing : accountingPeriods_) {
        if (sameBounds(existing.period(), period)) {
            if (existing.isClosed()) {
                throw AccountingPeriodAlreadyClosedException("Accounting period " + describe(period)
                                                             + " is already closed");
            }
            existing.state_ = AccountingPeriodState::Closed;
            return;
        }
    }
    throw UnknownAccountingPeriodException("No accounting period is defined as " + describe(period));
}

const AccountingPeriod* Ledger::closedPeriodContaining(std::chrono::system_clock::time_point date) const noexcept {
    for (const AccountingPeriod& candidate : accountingPeriods_) {
        if (candidate.isClosed() && candidate.period().contains(date)) {
            return &candidate;
        }
    }
    return nullptr;
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
