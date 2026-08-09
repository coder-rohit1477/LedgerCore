#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::reporting {

// One account's contribution to a report section: identity/metadata
// copied out of a TrialBalanceLine, plus one signed Money value.
//
// Deliberately different from TrialBalanceLine's debit/credit pair: a
// ReportLine always lives inside an already-known section (Assets,
// Revenue, ...), so the normal-balance direction is already implied by
// that section -- a redundant debit/credit split would just be noise
// where TrialBalance's flat, mixed-classification list genuinely needed
// one. Positive means "on this section's normal side"; negative means an
// abnormal/contra-like balance, preserved exactly, never reinterpreted.
class ReportLine {
public:
    ReportLine(domain::AccountId accountId, domain::AccountCode accountCode, std::string accountName,
               domain::Money amount)
        : accountId_(accountId),
          accountCode_(std::move(accountCode)),
          accountName_(std::move(accountName)),
          amount_(std::move(amount)) {}

    domain::AccountId accountId() const noexcept { return accountId_; }
    const domain::AccountCode& accountCode() const noexcept { return accountCode_; }
    const std::string& accountName() const noexcept { return accountName_; }
    const domain::Money& amount() const noexcept { return amount_; }

private:
    domain::AccountId accountId_;
    domain::AccountCode accountCode_;
    std::string accountName_;
    domain::Money amount_;
};

// A labeled group of ReportLines with a precomputed total (e.g.
// "Assets", "Revenue"). Shared by BalanceSheet and IncomeStatement
// rather than each hand-rolling "a list of lines plus a total"
// independently -- not a generic "Report" abstraction, just one small
// reusable value type for a genuinely repeated shape.
class ReportSection {
public:
    ReportSection(std::string label, std::vector<ReportLine> lines, domain::Money total)
        : label_(std::move(label)), lines_(std::move(lines)), total_(std::move(total)) {}

    const std::string& label() const noexcept { return label_; }
    const std::vector<ReportLine>& lines() const noexcept { return lines_; }
    const domain::Money& total() const noexcept { return total_; }

private:
    std::string label_;
    std::vector<ReportLine> lines_;
    domain::Money total_;
};

} // namespace ledgercore::reporting
