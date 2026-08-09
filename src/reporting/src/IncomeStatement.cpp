#include "ledgercore/reporting/IncomeStatement.h"

#include <string>
#include <utility>
#include <vector>

#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/NormalBalance.h"

namespace ledgercore::reporting {

namespace {

// The account's own-normal-side signed value, derived from
// TrialBalanceLine's debit/credit columns rather than re-fetched from
// Ledger -- exactly recovers the account's original normal-balance-signed
// Ledger balance, reusing domain::isDebitNormal() rather than
// duplicating the normal-balance direction table.
domain::Money netValue(const trialbalance::TrialBalanceLine& line) {
    return domain::isDebitNormal(line.accountType()) ? (line.debit() - line.credit())
                                                       : (line.credit() - line.debit());
}

ReportSection buildSection(std::string label, const trialbalance::TrialBalance& trialBalance,
                            domain::AccountType type) {
    std::vector<ReportLine> lines;
    domain::Money total = domain::Money::zero(trialBalance.currency());

    for (const trialbalance::TrialBalanceLine& line : trialBalance.lines()) {
        if (line.accountType() != type) {
            continue;
        }
        const domain::Money amount = netValue(line);
        lines.emplace_back(line.accountId(), line.accountCode(), line.accountName(), amount);
        total = total + amount;
    }

    return ReportSection(std::move(label), std::move(lines), std::move(total));
}

} // namespace

IncomeStatement::IncomeStatement(domain::Currency currency, ReportSection revenue, ReportSection expenses,
                                  domain::Money netIncome)
    : currency_(std::move(currency)),
      revenue_(std::move(revenue)),
      expenses_(std::move(expenses)),
      netIncome_(std::move(netIncome)) {}

IncomeStatement IncomeStatement::generate(const trialbalance::TrialBalance& trialBalance) {
    ReportSection revenue = buildSection("Revenue", trialBalance, domain::AccountType::Revenue);
    ReportSection expenses = buildSection("Expenses", trialBalance, domain::AccountType::Expense);
    domain::Money netIncome = revenue.total() - expenses.total();

    return IncomeStatement(trialBalance.currency(), std::move(revenue), std::move(expenses), std::move(netIncome));
}

} // namespace ledgercore::reporting
