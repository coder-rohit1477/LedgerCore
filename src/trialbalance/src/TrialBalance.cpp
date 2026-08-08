#include "ledgercore/trialbalance/TrialBalance.h"

#include <algorithm>
#include <utility>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/NormalBalance.h"
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

} // namespace ledgercore::trialbalance
