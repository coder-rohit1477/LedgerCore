#pragma once

#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/JournalEntry.h"
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
// (aggregating duplicate AccountId lines first, via domain::signedEffect()),
// then commit -- so a JournalEntry is never partially posted.
ledger::PostingId post(const domain::JournalEntry& entry,
                        const domain::ChartOfAccounts& chart,
                        ledger::Ledger& ledger);

} // namespace ledgercore::posting
