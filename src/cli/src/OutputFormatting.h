#pragma once

#include <ostream>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/reporting/BalanceSheet.h"
#include "ledgercore/reporting/IncomeStatement.h"
#include "ledgercore/trialbalance/TrialBalance.h"

namespace ledgercore::cli {

// Deterministic, human-readable text rendering of the engine's own value
// objects. Every monetary value is printed via domain::Money::toString()
// -- never hand-reformatted -- and ordering is exactly whatever the
// underlying object already provides (TrialBalance::lines() is already
// sorted by AccountCode; ChartOfAccounts preserves insertion order).

void printTrialBalance(std::ostream& out, const trialbalance::TrialBalance& trialBalance);

// No closing entries exist, so Revenue/Expense balances are never moved
// into Equity and the identity that holds is Assets == Liabilities +
// Equity + Net Income (see reporting::BalanceSheet). The Balance Sheet is
// therefore printed together with incomeStatement.netIncome() as
// current-period (unclosed) earnings, followed by the Liabilities +
// Equity + Net Income total that matches Total Assets. Both reports must
// come from the same TrialBalance.
void printBalanceSheet(std::ostream& out, const reporting::BalanceSheet& balanceSheet,
                       const reporting::IncomeStatement& incomeStatement);

void printIncomeStatement(std::ostream& out, const reporting::IncomeStatement& incomeStatement);

// Flat (one line per account, whole chart) or indented-tree presentation
// of every account reachable from chart.rootAccounts(). Presentation-only
// traversal -- no new domain "list all accounts" API is introduced.
void printAccountList(std::ostream& out, const domain::ChartOfAccounts& chart, bool tree);

void printAccountDetail(std::ostream& out, const domain::Account& account);

// "Journal (<currency>): N entries", then for each entry, in the given
// order, a header "#<posting id>  <date>  standard|closing  \"<description>\""
// followed by its lines in recorded order: "  DEBIT|CREDIT  <code> <name>
// <amount>". Account code/name come from chart (the CLI's presentation
// concern); the entries themselves are a journalquery result.
void printJournal(std::ostream& out, const std::vector<ledger::PostedJournalEntry>& entries,
                  const domain::ChartOfAccounts& chart, const domain::Currency& currency);

// One line per accounting period, in the Ledger's own order (ascending
// start): "<start>  <end>  open|closed", end exclusive.
void printAccountingPeriods(std::ostream& out, const ledger::Ledger& ledger);

} // namespace ledgercore::cli
