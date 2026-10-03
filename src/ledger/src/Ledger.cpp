#include "ledgercore/ledger/Ledger.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

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

// commit()'s final phase must not throw; these are what it relies on.
static_assert(std::is_nothrow_move_constructible_v<PostedJournalEntry>,
              "appending a PostedJournalEntry into reserved capacity must not throw");
static_assert(std::is_nothrow_move_assignable_v<domain::Money>, "storing a new balance must not throw");

PostingId Ledger::commit(domain::JournalEntry entry,
                          std::vector<std::pair<domain::AccountId, domain::Money>> newBalances) {
    // Strong exception guarantee, so balances_ and postedEntries_ stay in
    // sync even if an allocation fails: every step that can allocate runs
    // before anything observable changes (or is rolled back), and the
    // final phase cannot throw. Capacity grows geometrically (only when
    // full), so posting stays amortized O(1) rather than reallocating the
    // whole history on every commit.
    if (postedEntries_.size() == postedEntries_.capacity()) {
        postedEntries_.reserve(std::max<std::size_t>(16, postedEntries_.capacity() * 2));
    }

    // Make sure every affected account has a balance slot, remembering a
    // pointer to each (references to unordered_map elements survive
    // rehashing) and which slots were newly created, so they can be
    // removed again if a later insertion fails.
    std::vector<domain::Money*> slots;
    slots.reserve(newBalances.size());
    std::vector<std::uint64_t> insertedKeys;
    insertedKeys.reserve(newBalances.size());
    try {
        for (const auto& [accountId, newBalance] : newBalances) {
            const auto [slot, inserted] = balances_.try_emplace(accountId.value(), domain::Money::zero(currency_));
            slots.push_back(&slot->second);
            if (inserted) {
                insertedKeys.push_back(accountId.value());
            }
        }
    } catch (...) {
        for (const std::uint64_t key : insertedKeys) {
            balances_.erase(key);
        }
        throw;
    }

    // Nothing below can throw.
    for (std::size_t i = 0; i < newBalances.size(); ++i) {
        *slots[i] = std::move(newBalances[i].second);
    }
    const PostingId id(nextPostingId_++);
    if (entry.isClosing()) {
        ++closingEntryCount_;
    }
    postedEntries_.push_back(PostedJournalEntry(id, std::move(entry), std::chrono::system_clock::now()));
    return id;
}

} // namespace ledgercore::ledger
