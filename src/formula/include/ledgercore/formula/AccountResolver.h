#pragma once

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::formula {

// A narrow, caller-supplied abstraction for resolving an account
// reference to its current Money value. The Formula Engine itself never
// knows where this value comes from -- a caller might back this with a
// Ledger, a TrialBalance, other computed accounts, or a fixed map of
// values in a test. This is what keeps the parser and evaluator
// completely independent of Ledger/ChartOfAccounts/TrialBalance.
class AccountResolver {
public:
    virtual ~AccountResolver() = default;

    // Throws UnknownAccountReferenceException if code is not recognized.
    virtual domain::Money resolve(const domain::AccountCode& code) const = 0;
};

} // namespace ledgercore::formula
