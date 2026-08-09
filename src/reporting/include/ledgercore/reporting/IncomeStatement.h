#pragma once

#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/reporting/ReportLine.h"
#include "ledgercore/trialbalance/TrialBalance.h"

namespace ledgercore::reporting {

// An immutable, disconnected snapshot of Revenue/Expenses and the Net
// Income (or, if negative, Net Loss) they produce, derived entirely from
// an already-generated TrialBalance.
//
// Cumulative, not period-scoped: LedgerCore has no accounting-period
// abstraction and no date-bounded balance query (JournalEntry carries a
// date, but Ledger::balance() is always the full-history running total).
// This Income Statement therefore reports cumulative Revenue/Expense
// activity since the Ledger's inception, not a specific fiscal period --
// callers should present it accordingly.
//
// Asset/Liability/Equity accounts, and group accounts, never appear in
// either section: group accounts are already excluded by TrialBalance
// itself; Asset/Liability/Equity are deliberately filtered out here,
// since they belong to the Balance Sheet, not the Income Statement.
class IncomeStatement {
public:
    static IncomeStatement generate(const trialbalance::TrialBalance& trialBalance);

    const domain::Currency& currency() const noexcept { return currency_; }
    const ReportSection& revenue() const noexcept { return revenue_; }
    const ReportSection& expenses() const noexcept { return expenses_; }

    // revenue().total() - expenses().total(), precomputed. Negative
    // means Net Loss -- the sign carries that meaning; there is no
    // separate NetLoss type.
    const domain::Money& netIncome() const noexcept { return netIncome_; }

private:
    IncomeStatement(domain::Currency currency, ReportSection revenue, ReportSection expenses,
                     domain::Money netIncome);

    domain::Currency currency_;
    ReportSection revenue_;
    ReportSection expenses_;
    domain::Money netIncome_;
};

} // namespace ledgercore::reporting
