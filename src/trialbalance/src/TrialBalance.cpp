#include "ledgercore/trialbalance/TrialBalance.h"

#include <algorithm>
#include <functional>
#include <utility>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/NormalBalance.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/trialbalance/TrialBalanceExceptions.h"

namespace ledgercore::trialbalance {

namespace {

void collectLeafAccounts(const domain::Account& account, std::vector<const domain::Account*>& leaves) {
    if (account.isLeaf()) {
        leaves.push_back(&account);
        return;
    }
    for (const domain::Account* child : account.children()) {
        collectLeafAccounts(*child, leaves);
    }
}

// Replays ledger.postedEntries(), including a whole JournalEntry's lines
// only when includeEntry(entry.date()) is true, and accumulates each
// account's normal-balance-signed net via domain::signedEffect() --
// the same effect calculation PostingEngine itself uses, applied here to
// a date-filtered subset of history instead of a single new entry.
//
// Entries are never assumed to be sorted by date (Ledger does not
// guarantee this -- backdated entries are legal), so every posted entry
// is individually checked; there is no early-exit shortcut.
//
// If chart does not correspond to ledger (a caller error also possible
// for generate()), a line's AccountId may not resolve in chart; such
// lines are skipped rather than dereferencing a null Account, and the
// resulting imbalance is caught by finalize()'s existing balance check,
// exactly like generate()'s own handling of a mismatched pair.
std::unordered_map<std::uint64_t, domain::Money> replayBalances(
    const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
    const std::function<bool(std::chrono::system_clock::time_point)>& includeEntry) {
    std::unordered_map<std::uint64_t, domain::Money> balances;

    for (const ledger::PostedJournalEntry& posted : ledger.postedEntries()) {
        if (!includeEntry(posted.entry().date())) {
            continue;
        }
        for (const domain::JournalEntryLine& line : posted.entry().lines()) {
            const domain::Account* account = chart.findById(line.accountId());
            if (account == nullptr) {
                continue;
            }
            const domain::Money effect = domain::signedEffect(account->type(), line.side(), line.amount());

            const std::uint64_t key = line.accountId().value();
            auto it = balances.find(key);
            if (it == balances.end()) {
                balances.emplace(key, effect);
            } else {
                it->second = it->second + effect;
            }
        }
    }

    return balances;
}

} // namespace

TrialBalance::TrialBalance(domain::Currency currency, std::vector<TrialBalanceLine> lines,
                            domain::Money totalDebits, domain::Money totalCredits)
    : currency_(std::move(currency)),
      lines_(std::move(lines)),
      totalDebits_(std::move(totalDebits)),
      totalCredits_(std::move(totalCredits)) {}

TrialBalance TrialBalance::generate(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger) {
    std::vector<const domain::Account*> leaves;
    for (const domain::Account* root : chart.rootAccounts()) {
        collectLeafAccounts(*root, leaves);
    }

    std::vector<TrialBalanceLine> lines;
    lines.reserve(leaves.size());
    for (const domain::Account* account : leaves) {
        const domain::Money balance = ledger.balance(account->id());
        const domain::DebitCreditAmounts presentation = domain::debitCreditPresentation(account->type(), balance);
        lines.emplace_back(account->id(), account->code(), account->name(), account->type(), presentation.debit,
                            presentation.credit);
    }

    std::sort(lines.begin(), lines.end(), [](const TrialBalanceLine& lhs, const TrialBalanceLine& rhs) {
        return lhs.accountCode().value() < rhs.accountCode().value();
    });

    domain::Money totalDebits = domain::Money::zero(ledger.currency());
    domain::Money totalCredits = domain::Money::zero(ledger.currency());
    for (const TrialBalanceLine& line : lines) {
        totalDebits = totalDebits + line.debit();
        totalCredits = totalCredits + line.credit();
    }

    if (totalDebits != totalCredits) {
        throw UnbalancedTrialBalanceException("Trial Balance does not balance: total debits "
                                               + totalDebits.toString() + " vs total credits "
                                               + totalCredits.toString());
    }

    return TrialBalance(ledger.currency(), std::move(lines), std::move(totalDebits), std::move(totalCredits));
}

TrialBalance TrialBalance::finalize(const domain::ChartOfAccounts& chart,
                                     const std::unordered_map<std::uint64_t, domain::Money>& balances,
                                     const domain::Currency& currency) {
    std::vector<const domain::Account*> leaves;
    for (const domain::Account* root : chart.rootAccounts()) {
        collectLeafAccounts(*root, leaves);
    }

    std::vector<TrialBalanceLine> lines;
    lines.reserve(leaves.size());
    for (const domain::Account* account : leaves) {
        auto it = balances.find(account->id().value());
        const domain::Money balance = (it == balances.end()) ? domain::Money::zero(currency) : it->second;
        const domain::DebitCreditAmounts presentation = domain::debitCreditPresentation(account->type(), balance);
        lines.emplace_back(account->id(), account->code(), account->name(), account->type(), presentation.debit,
                            presentation.credit);
    }

    std::sort(lines.begin(), lines.end(), [](const TrialBalanceLine& lhs, const TrialBalanceLine& rhs) {
        return lhs.accountCode().value() < rhs.accountCode().value();
    });

    domain::Money totalDebits = domain::Money::zero(currency);
    domain::Money totalCredits = domain::Money::zero(currency);
    for (const TrialBalanceLine& line : lines) {
        totalDebits = totalDebits + line.debit();
        totalCredits = totalCredits + line.credit();
    }

    if (totalDebits != totalCredits) {
        throw UnbalancedTrialBalanceException("Trial Balance does not balance: total debits "
                                               + totalDebits.toString() + " vs total credits "
                                               + totalCredits.toString());
    }

    return TrialBalance(currency, std::move(lines), std::move(totalDebits), std::move(totalCredits));
}

TrialBalance TrialBalance::generateAsOf(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
                                         std::chrono::system_clock::time_point cutoff) {
    const std::unordered_map<std::uint64_t, domain::Money> balances =
        replayBalances(chart, ledger, [cutoff](std::chrono::system_clock::time_point date) { return date < cutoff; });

    return finalize(chart, balances, ledger.currency());
}

TrialBalance TrialBalance::generateForPeriod(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
                                              const domain::Period& period) {
    const std::unordered_map<std::uint64_t, domain::Money> balances = replayBalances(
        chart, ledger, [&period](std::chrono::system_clock::time_point date) { return period.contains(date); });

    return finalize(chart, balances, ledger.currency());
}

} // namespace ledgercore::trialbalance
