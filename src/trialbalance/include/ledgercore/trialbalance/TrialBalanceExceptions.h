#pragma once

#include <string>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::trialbalance {

// Thrown by TrialBalance::generate() when the derived total debits do not
// exactly equal total credits.
//
// Under correct use -- a ChartOfAccounts and Ledger that genuinely
// correspond to each other, with the Ledger mutated only through
// PostingEngine -- this cannot happen: PostingEngine only ever posts
// already-balanced JournalEntry objects, and debitCreditPresentation() is
// the exact inverse of the signedEffect() used to post them. It becomes
// reachable if generate() is given a ChartOfAccounts that does not
// actually correspond to the Ledger it is paired with (e.g. one missing
// the offsetting side of a posted transaction) -- something TrialBalance
// has no other way to detect, since Ledger and ChartOfAccounts have no
// linkage back to each other by design.
class UnbalancedTrialBalanceException : public ledgercore::LedgerException {
public:
    explicit UnbalancedTrialBalanceException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::trialbalance
