#pragma once

#include "ledgercore/domain/Period.h"

namespace ledgercore::ledger {

class Ledger;

// Open -> Closed is the whole lifecycle. There is deliberately no way back:
// reopening a closed period is not supported.
enum class AccountingPeriodState {
    Open,
    Closed
};

// One accounting period of a Ledger: a [start, end) business-date range
// (domain::Period, the same type and boundary convention TrialBalance
// uses) plus its lifecycle state. Pure metadata about which postings the
// Ledger accepts -- it holds no balances and never affects reports.
//
// Only Ledger creates an AccountingPeriod or changes its state (see
// Ledger::defineAccountingPeriod / closeAccountingPeriod).
class AccountingPeriod {
public:
    const domain::Period& period() const noexcept { return period_; }
    AccountingPeriodState state() const noexcept { return state_; }
    bool isClosed() const noexcept { return state_ == AccountingPeriodState::Closed; }

private:
    friend class Ledger;

    explicit AccountingPeriod(domain::Period period) noexcept
        : period_(period), state_(AccountingPeriodState::Open) {}

    domain::Period period_;
    AccountingPeriodState state_;
};

} // namespace ledgercore::ledger
