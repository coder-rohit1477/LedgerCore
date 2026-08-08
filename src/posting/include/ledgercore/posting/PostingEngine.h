#pragma once

#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostingId.h"

namespace ledgercore::posting {

// The Posting Engine: the only component aware of both JournalEntry and
// ChartOfAccounts. Applies a validated, balanced JournalEntry to a
// specific ChartOfAccounts, recording the effect in a Ledger.
//
// post() never mutates entry, any Account, or chart -- the only thing it
// mutates is ledger, and only through Ledger's controlled commit path.
//
// Sequence: validate every line (account exists, account is a leaf,
// entry currency matches ledger currency) with no Ledger mutation, then
// compute every affected account's new balance into a local temporary
// (aggregating duplicate AccountId lines first), then commit -- so a
// JournalEntry is never partially posted.
ledger::PostingId post(const domain::JournalEntry& entry,
                        const domain::ChartOfAccounts& chart,
                        ledger::Ledger& ledger);

// A signed Ledger balance, split into the debit/credit presentation a
// future Trial Balance needs. This is the inverse of the normal-balance
// sign convention post() applies when computing effects:
//
//   Asset / Expense   (debit-normal):  positive -> debit,  negative -> credit
//   Liability / Equity / Revenue
//                    (credit-normal):  positive -> credit, negative -> debit
//
// Deliberately does not sum across accounts or build a report -- that is
// the future TrialBalance component's job, not this one's. This helper
// only answers "what does this one signed balance mean," which is the
// piece TrialBalance will need and the piece worth getting right (and
// tested) now.
struct DebitCreditAmounts {
    domain::Money debit;
    domain::Money credit;
};

DebitCreditAmounts debitCreditPresentation(domain::AccountType type, const domain::Money& balance);

} // namespace ledgercore::posting
