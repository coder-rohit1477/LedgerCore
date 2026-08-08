#include "ledgercore/posting/PostingEngine.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/ledger/LedgerExceptions.h"
#include "ledgercore/posting/PostingExceptions.h"

namespace ledgercore::posting {

namespace {

// Whether AccountType's normal balance is Debit (true) or Credit (false).
// The only place in LedgerCore that AccountType and posting effects meet
// -- deliberately not in Account or JournalEntry.
bool isDebitNormal(domain::AccountType type) {
    switch (type) {
        case domain::AccountType::Asset:
        case domain::AccountType::Expense:
            return true;
        case domain::AccountType::Liability:
        case domain::AccountType::Equity:
        case domain::AccountType::Revenue:
            return false;
    }
    return true;
}

// The signed effect of one JournalEntryLine on its account's balance,
// under the normal-balance sign convention: a line on the account type's
// normal side increases the balance, a line on the opposite side
// decreases it.
domain::Money signedEffect(domain::AccountType type, const domain::JournalEntryLine& line) {
    const bool lineIsOnNormalSide = (line.isDebit() == isDebitNormal(type));
    return lineIsOnNormalSide ? line.amount() : -line.amount();
}

} // namespace

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

    // Phase 2: compute. Aggregate duplicate AccountId lines into one net
    // delta per account, then compute each affected account's new
    // balance. Still no Ledger mutation: MoneyOverflowException here
    // leaves the Ledger untouched.
    std::vector<std::pair<domain::AccountId, domain::Money>> deltas;
    std::unordered_map<std::uint64_t, std::size_t> deltaIndexByAccountId;
    for (std::size_t i = 0; i < entry.lines().size(); ++i) {
        const domain::JournalEntryLine& line = entry.lines()[i];
        const domain::Money effect = signedEffect(resolvedAccounts[i]->type(), line);

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

DebitCreditAmounts debitCreditPresentation(domain::AccountType type, const domain::Money& balance) {
    const domain::Money zero = domain::Money::zero(balance.currency());
    const bool debitNormal = isDebitNormal(type);

    if (balance.isNegative()) {
        const domain::Money magnitude = -balance;
        return debitNormal ? DebitCreditAmounts{zero, magnitude} : DebitCreditAmounts{magnitude, zero};
    }

    return debitNormal ? DebitCreditAmounts{balance, zero} : DebitCreditAmounts{zero, balance};
}

} // namespace ledgercore::posting
