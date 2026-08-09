#pragma once

#include <ostream>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/ChartOfAccounts.h"
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

void printBalanceSheet(std::ostream& out, const reporting::BalanceSheet& balanceSheet);

void printIncomeStatement(std::ostream& out, const reporting::IncomeStatement& incomeStatement);

// Flat (one line per account, whole chart) or indented-tree presentation
// of every account reachable from chart.rootAccounts(). Presentation-only
// traversal -- no new domain "list all accounts" API is introduced.
void printAccountList(std::ostream& out, const domain::ChartOfAccounts& chart, bool tree);

void printAccountDetail(std::ostream& out, const domain::Account& account);

} // namespace ledgercore::cli
