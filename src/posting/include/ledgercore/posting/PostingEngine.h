#pragma once

#include <string>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
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
