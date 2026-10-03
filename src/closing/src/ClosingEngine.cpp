#include "ledgercore/closing/ClosingEngine.h"

#include <string>
#include <utility>
#include <vector>

#include "ledgercore/closing/ClosingExceptions.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/posting/PostingExceptions.h"
#include "ledgercore/trialbalance/TrialBalance.h"

namespace ledgercore::closing {

namespace {

void ensureValidRetainedEarnings(const domain::ChartOfAccounts& chart, domain::AccountId retainedEarnings) {
    const domain::Account* account = chart.findById(retainedEarnings);
    if (account == nullptr) {
        throw posting::AccountNotFoundException("No account found for retained-earnings AccountId "
                                                + std::to_string(retainedEarnings.value()));
    }
    if (!account->isLeaf()) {
        throw posting::InvalidPostingTargetException("Retained-earnings account must be a leaf account, not group "
                                                     + account->code().value());
    }
    if (account->type() != domain::AccountType::Equity) {
        throw InvalidRetainedEarningsAccountException("Retained-earnings account must be an Equity account: "
                                                      + account->code().value());
    }
}

bool isTemporary(domain::AccountType type) noexcept {
    return type == domain::AccountType::Revenue || type == domain::AccountType::Expense;
}

} // namespace

ClosingResult closeTemporaryAccounts(const domain::ChartOfAccounts& chart, ledger::Ledger& ledger,
                                     domain::AccountId retainedEarnings, std::chrono::system_clock::time_point cutoff,
                                     std::string description) {
    ensureValidRetainedEarnings(chart, retainedEarnings);

    // Balances to close come from the existing as-of projection, already
    // split into a debit/credit presentation by domain::NormalBalance --
    // so no sign rule is re-derived here.
    const trialbalance::TrialBalance balances = trialbalance::TrialBalance::generateAsOf(chart, ledger, cutoff);

    std::vector<domain::JournalEntryLine> lines;
    domain::Money closingDebits = domain::Money::zero(ledger.currency());
    domain::Money closingCredits = domain::Money::zero(ledger.currency());
    for (const trialbalance::TrialBalanceLine& line : balances.lines()) {
        if (!isTemporary(line.accountType())) {
            continue;
        }
        // At most one presentation column is non-zero. Posting the same
        // amount on the opposite column brings the account to zero.
        if (line.credit().isPositive()) {
            lines.push_back(domain::JournalEntryLine::debit(line.accountId(), line.credit()));
            closingDebits = closingDebits + line.credit();
        } else if (line.debit().isPositive()) {
            lines.push_back(domain::JournalEntryLine::credit(line.accountId(), line.debit()));
            closingCredits = closingCredits + line.debit();
        }
    }

    if (lines.empty()) {
        throw NothingToCloseException("No Revenue or Expense account has a non-zero balance to close");
    }

    // Closing debits are the credit-balance (revenue-like) side, closing
    // credits the debit-balance (expense-like) side, so their difference
    // is net income. Both are non-negative and at most INT64_MAX, so the
    // difference and its negation are representable.
    const domain::Money netIncome = closingDebits - closingCredits;
    if (netIncome.isPositive()) {
        lines.push_back(domain::JournalEntryLine::credit(retainedEarnings, netIncome));
    } else if (netIncome.isNegative()) {
        lines.push_back(domain::JournalEntryLine::debit(retainedEarnings, -netIncome));
    }

    // Dated so that the entry's own closingCutoff() is exactly cutoff:
    // posting::post() then verifies completeness against precisely the
    // entries closed here (all dated before cutoff).
    if (cutoff < std::chrono::system_clock::time_point::min() + domain::kClosingCutoffOffset) {
        throw domain::InvalidJournalEntryException("Closing cutoff is too early to date a closing entry");
    }
    const std::chrono::system_clock::time_point closingDate = cutoff - domain::kClosingCutoffOffset;

    const domain::JournalEntry entry =
        domain::JournalEntry::createClosing(closingDate, std::move(description), std::move(lines));
    const ledger::PostingId postingId = posting::post(entry, chart, ledger);
    return ClosingResult{postingId, netIncome};
}

} // namespace ledgercore::closing
