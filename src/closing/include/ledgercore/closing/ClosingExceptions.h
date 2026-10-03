#pragma once

#include <string>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::closing {

// The designated retained-earnings account is not an Equity account.
// Closing transfers temporary balances *into equity*; any other target
// would turn net income into an asset, liability, or another temporary
// balance.
class InvalidRetainedEarningsAccountException : public ledgercore::LedgerException {
public:
    explicit InvalidRetainedEarningsAccountException(const std::string& message) : LedgerException(message) {}
};

// Every Revenue and Expense account already has a zero balance as of the
// requested cutoff -- including because that cutoff was already closed --
// so there is no closing entry to post.
class NothingToCloseException : public ledgercore::LedgerException {
public:
    explicit NothingToCloseException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::closing
