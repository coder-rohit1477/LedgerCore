#pragma once

#include "ledgercore/domain/Currency.h"
#include "ledgercore/reporting/ReportLine.h"
#include "ledgercore/trialbalance/TrialBalance.h"

namespace ledgercore::reporting {

// An immutable, disconnected snapshot of Assets/Liabilities/Equity,
// derived entirely from an already-generated TrialBalance -- no
// reference to ChartOfAccounts or Ledger is ever held, since TrialBalance
// has already done the work of reading them.
//
// Revenue and Expense accounts are never closed into Equity by this
// system (no closing-entry mechanism exists), so the textbook
// Assets == Liabilities + Equity does not hold in general. The identity
// that always holds is:
//
//     Assets == Liabilities + Equity + NetIncome
//
// where NetIncome == Revenue - Expenses (see IncomeStatement). A valid
// TrialBalance generated from a Ledger mutated only through
// PostingEngine mathematically guarantees this identity -- there is no
// honest public API path to violate it -- so it is proven by tests
// across realistic posting sequences (see tests/reporting/), not
// re-validated at runtime here.
//
// Revenue and Expense accounts, and group accounts, never appear in any
// section: group accounts are already excluded by TrialBalance itself;
// Revenue/Expense are deliberately filtered out here, since they belong
// to IncomeStatement, not the Balance Sheet.
class BalanceSheet {
public:
    static BalanceSheet generate(const trialbalance::TrialBalance& trialBalance);

    const domain::Currency& currency() const noexcept { return currency_; }
    const ReportSection& assets() const noexcept { return assets_; }
    const ReportSection& liabilities() const noexcept { return liabilities_; }
    const ReportSection& equity() const noexcept { return equity_; }

private:
    BalanceSheet(domain::Currency currency, ReportSection assets, ReportSection liabilities, ReportSection equity);

    domain::Currency currency_;
    ReportSection assets_;
    ReportSection liabilities_;
    ReportSection equity_;
};

} // namespace ledgercore::reporting
