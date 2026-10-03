#include "ledgercore/posting/PostingEngine.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/NormalBalance.h"
#include "ledgercore/ledger/LedgerExceptions.h"
#include "ledgercore/posting/PostingExceptions.h"

namespace ledgercore::posting {

ledger::PostingId post(const domain::JournalEntry& entry,
                        const domain::ChartOfAccounts& chart,
                        ledger::Ledger& ledger) {
    // Phase 1: validate. No Ledger state is touched below this point
    // until every line has been checked.
    if (entry.currency() != ledger.currency()) {
        throw ledger::LedgerCurrencyMismatchException(
            "JournalEntry currency " + entry.currency().code() + " does not match Ledger currency "
            + ledger.currency().code());
    }

    std::vector<const domain::Account*> resolvedAccounts;
    resolvedAccounts.reserve(entry.lines().size());
    for (const domain::JournalEntryLine& line : entry.lines()) {
        const domain::Account* account = chart.findById(line.accountId());
        if (account == nullptr) {
            throw AccountNotFoundException(
                "No account found for AccountId " + std::to_string(line.accountId().value())
                + " referenced by a JournalEntryLine");
        }
        if (!account->isLeaf()) {
            throw InvalidPostingTargetException(
                "Cannot post to non-leaf (group) account: " + account->code().value());
        }
        resolvedAccounts.push_back(account);
    }

    if (entry.isClosing()) {
        bool touchesTemporaryAccount = false;
        for (const domain::Account* account : resolvedAccounts) {
            const domain::AccountType type = account->type();
            if (type == domain::AccountType::Revenue || type == domain::AccountType::Expense) {
                touchesTemporaryAccount = true;
            } else if (type != domain::AccountType::Equity) {
                throw InvalidClosingEntryException("A closing entry may only post to Revenue, Expense, and Equity "
                                                   "accounts, not to account "
                                                   + account->code().value());
            }
        }
        if (!touchesTemporaryAccount) {
            throw InvalidClosingEntryException("A closing entry must close at least one Revenue or Expense account");
        }
    }

    // Phase 2: compute. Aggregate duplicate AccountId lines into one net
    // delta per account, then compute each affected account's new
    // balance. Still no Ledger mutation: MoneyOverflowException here
    // leaves the Ledger untouched.
    std::vector<std::pair<domain::AccountId, domain::Money>> deltas;
    std::unordered_map<std::uint64_t, std::size_t> deltaIndexByAccountId;
    for (std::size_t i = 0; i < entry.lines().size(); ++i) {
        const domain::JournalEntryLine& line = entry.lines()[i];
        const domain::Money effect = domain::signedEffect(resolvedAccounts[i]->type(), line.side(), line.amount());

        auto it = deltaIndexByAccountId.find(line.accountId().value());
        if (it == deltaIndexByAccountId.end()) {
            deltaIndexByAccountId.emplace(line.accountId().value(), deltas.size());
            deltas.emplace_back(line.accountId(), effect);
        } else {
            std::pair<domain::AccountId, domain::Money>& existing = deltas[it->second];
            existing.second = existing.second + effect;
        }
    }

    std::vector<std::pair<domain::AccountId, domain::Money>> newBalances;
    newBalances.reserve(deltas.size());
    for (const auto& [accountId, delta] : deltas) {
        newBalances.emplace_back(accountId, ledger.balance(accountId) + delta);
    }

    // Phase 3: commit. Everything fallible has already succeeded.
    return ledger.commit(entry, std::move(newBalances));
}

domain::Account& addChildAccount(domain::ChartOfAccounts& chart,
                                 const ledger::Ledger& ledger,
                                 domain::Account& parent,
                                 domain::AccountCode code,
                                 std::string name) {
    // Only consult ledger for an Account that genuinely belongs to chart:
    // a foreign parent's AccountId is meaningless against this ledger, and
    // must still be rejected by chart's own ForeignAccountException.
    const bool belongsToChart = chart.findById(parent.id()) == &parent;
    if (belongsToChart && ledger.hasPostingHistory(parent.id())) {
        throw PostedAccountCannotBecomeGroupException(
            "Cannot add a child to account " + parent.code().value()
            + ": it has posting history, and a group account cannot be a posting target");
    }
    return chart.addChildAccount(parent, std::move(code), std::move(name));
}

} // namespace ledgercore::posting
