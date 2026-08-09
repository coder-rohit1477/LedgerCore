#include "ledgercore/reporting/BalanceSheet.h"

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

BalanceSheet::BalanceSheet(domain::Currency currency, ReportSection assets, ReportSection liabilities,
                            ReportSection equity)
    : currency_(std::move(currency)),
      assets_(std::move(assets)),
      liabilities_(std::move(liabilities)),
      equity_(std::move(equity)) {}

BalanceSheet BalanceSheet::generate(const trialbalance::TrialBalance& trialBalance) {
    ReportSection assets = buildSection("Assets", trialBalance, domain::AccountType::Asset);
    ReportSection liabilities = buildSection("Liabilities", trialBalance, domain::AccountType::Liability);
    ReportSection equity = buildSection("Equity", trialBalance, domain::AccountType::Equity);

    return BalanceSheet(trialBalance.currency(), std::move(assets), std::move(liabilities), std::move(equity));
}

} // namespace ledgercore::reporting
