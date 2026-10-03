#pragma once

#include <cstddef>
#include <string>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostingId.h"

namespace ledgercore::posting {

// The most closing entries (JournalEntryKind::Closing) one Ledger accepts --
// monthly closes for 83 years. Validating a closing entry replays the
// Ledger's history, so without a bound a snapshot made of many small valid
// closing entries would cost O(closings x history) to load; with it, the
// worst case is at most this many history scans. post() rejects the next
// closing entry once the Ledger holds this many
// (ClosingEntryLimitExceededException), live or during load.
inline constexpr std::size_t kMaxClosingEntries = 1000;

// The Posting Engine: the only component aware of both JournalEntry and
// ChartOfAccounts. Applies a validated, balanced JournalEntry to a
// specific ChartOfAccounts, recording the effect in a Ledger.
//
// post() never mutates entry, any Account, or chart -- the only thing it
// mutates is ledger, and only through Ledger's controlled commit path.
//
// Sequence: validate (entry currency matches ledger currency; entry date
// is not inside a Closed accounting period of ledger, else
// ClosedPeriodPostingException; every line's account exists and is a
// leaf) with no Ledger mutation, then
// compute every affected account's new balance into a local temporary
// (aggregating duplicate AccountId lines first, via domain::signedEffect()),
// then commit -- so a JournalEntry is never partially posted.
//
// A JournalEntryKind::Closing entry is excluded from income statements, so
// it is additionally validated, before any mutation, to be exactly a
// complete closing entry; otherwise InvalidClosingEntryException:
//   - every line targets a Revenue, Expense, or Equity account, and at
//     least one targets a Revenue/Expense account;
//   - no account appears on more than one line;
//   - at most one line targets an Equity account (the retained-earnings
//     destination; none only when revenue and expenses offset exactly);
//   - with the entry applied, every Revenue/Expense account in chart has a
//     zero balance counting all posted entries dated before
//     entry.closingCutoff().
// A closing entry is also rejected (ClosingEntryLimitExceededException) if
// ledger already holds kMaxClosingEntries closing entries -- checked before
// the history replay the completeness rule needs.
// These leave no freedom beyond the choice of Equity account, so a
// Closing entry -- posted live or replayed from a snapshot -- is always
// one closing::closeTemporaryAccounts() could have produced.
ledger::PostingId post(const domain::JournalEntry& entry,
                        const domain::ChartOfAccounts& chart,
                        ledger::Ledger& ledger);

// The only public way to add a child account (ChartOfAccounts's own
// child-attach operation is private, with this function as its sole
// friend). Only leaf accounts may be posting targets, and attaching a
// child turns its parent into a group --
// but ChartOfAccounts deliberately knows nothing about Ledger, so it
// cannot tell whether that parent was already posted to. This is the one
// place with both, mirroring post(): if parent has any posting history
// in ledger (even history that nets to a zero balance), throws
// PostedAccountCannotBecomeGroupException before touching chart;
// otherwise forwards to the chart's private operation unchanged, so every
// chart-level rule (foreign parent, duplicate code, empty name) is still
// enforced there, with chart and ledger both left unchanged on any
// failure.
domain::Account& addChildAccount(domain::ChartOfAccounts& chart,
                                 const ledger::Ledger& ledger,
                                 domain::Account& parent,
                                 domain::AccountCode code,
                                 std::string name);

} // namespace ledgercore::posting
