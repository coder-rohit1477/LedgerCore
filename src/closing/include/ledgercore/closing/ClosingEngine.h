#pragma once

#include <chrono>
#include <string>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostingId.h"

namespace ledgercore::closing {

struct ClosingResult {
    // The posted closing entry.
    ledger::PostingId postingId;

    // Revenue minus expenses that were closed: positive (net income) was
    // credited to retained earnings, negative (net loss) was debited,
    // zero means revenue and expenses offset exactly.
    domain::Money netIncome;
};

// Closes every Revenue and Expense account into retainedEarnings through
// one genuine, balanced JournalEntryKind::Closing entry posted with
// posting::post() -- never by touching Ledger balances directly.
//
// Scope: all temporary activity dated strictly before cutoff -- exactly
// the balances TrialBalance::generateAsOf(chart, ledger, cutoff) shows
// (prior closing entries included, so already-closed results are not
// closed again). The closing entry is dated
// cutoff - domain::kClosingCutoffOffset (one microsecond, identical on
// every platform), so its closingCutoff() is exactly cutoff and afterwards
// generateAsOf(..., cutoff) is a post-closing trial balance: every
// Revenue/Expense balance is zero and retainedEarnings holds the result.
// posting::post() independently re-verifies that the entry is a complete
// close as of that cutoff.
//
// Lines, in ascending AccountCode order (TrialBalance's order): each
// temporary account with a non-zero balance gets one line on the
// opposite column of its trial-balance presentation (a credit balance is
// debited, a debit balance is credited), which zeroes it under the
// NormalBalance convention; then, if the closing lines do not already
// balance, one retainedEarnings line for the difference -- a credit for
// net income, a debit for net loss.
//
// Throws, always before the Ledger is touched:
//   posting::AccountNotFoundException          retainedEarnings not in chart
//   posting::InvalidPostingTargetException     retainedEarnings is a group
//   InvalidRetainedEarningsAccountException    retainedEarnings not Equity
//   NothingToCloseException                    no non-zero temporary balance
//   domain::InvalidJournalEntryException       empty description
// plus anything TrialBalance::generateAsOf() or posting::post() throws
// (e.g. domain::MoneyOverflowException), each of which leaves the Ledger
// unchanged.
ClosingResult closeTemporaryAccounts(const domain::ChartOfAccounts& chart, ledger::Ledger& ledger,
                                     domain::AccountId retainedEarnings, std::chrono::system_clock::time_point cutoff,
                                     std::string description = "Closing entry");

} // namespace ledgercore::closing
