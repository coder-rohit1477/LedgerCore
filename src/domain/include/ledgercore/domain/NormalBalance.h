#pragma once

#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::domain {

// The accounting convention for which side of a transaction increases a
// given AccountType's balance, and the two directions that convention is
// used in: applying a debit/credit line to a balance (posting), and
// splitting an existing signed balance back into a debit/credit
// presentation (reporting). Both PostingEngine and TrialBalance need this
// same pure fact about AccountType + Money; neither owns it, so it lives
// here rather than being duplicated in either.

// Whether AccountType's normal balance is Debit (true) or Credit (false).
bool isDebitNormal(AccountType type) noexcept;

// A signed Ledger balance, split into the debit/credit presentation a
// report (e.g. Trial Balance) needs.
struct DebitCreditAmounts {
    Money debit;
    Money credit;
};

// The signed effect of one debit/credit amount on its account's balance,
// under the normal-balance sign convention: an amount on the account
// type's normal side increases the balance, an amount on the opposite
// side decreases it.
Money signedEffect(AccountType type, DebitCreditSide side, const Money& amount);

// The inverse of signedEffect(): splits an existing signed balance into
// the debit/credit presentation a report needs.
DebitCreditAmounts debitCreditPresentation(AccountType type, const Money& balance);

} // namespace ledgercore::domain
