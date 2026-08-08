#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/ledger/Ledger.h"

namespace ledgercore::trialbalance {

// One leaf account's balance, presented as a debit/credit pair.
//
// A flat reporting projection, not an Account clone: no parent, no
// children, no tree navigation. Everything is copied out of
// ChartOfAccounts/Ledger at generation time, so a TrialBalanceLine has no
// lifetime dependency on either afterward.
//
// Per domain::debitCreditPresentation()'s contract, debit() and credit()
// are never both non-zero: exactly one is non-zero, or -- for an account
// with no posted activity -- both are Money::zero(currency).
class TrialBalanceLine {
public:
    TrialBalanceLine(domain::AccountId accountId, domain::AccountCode accountCode, std::string accountName,
                      domain::AccountType accountType, domain::Money debit, domain::Money credit)
        : accountId_(accountId),
          accountCode_(std::move(accountCode)),
          accountName_(std::move(accountName)),
          accountType_(accountType),
          debit_(std::move(debit)),
          credit_(std::move(credit)) {}

    domain::AccountId accountId() const noexcept { return accountId_; }
    const domain::AccountCode& accountCode() const noexcept { return accountCode_; }
    const std::string& accountName() const noexcept { return accountName_; }
    domain::AccountType accountType() const noexcept { return accountType_; }
    const domain::Money& debit() const noexcept { return debit_; }
    const domain::Money& credit() const noexcept { return credit_; }

private:
    domain::AccountId accountId_;
    domain::AccountCode accountCode_;
    std::string accountName_;
    domain::AccountType accountType_;
    domain::Money debit_;
    domain::Money credit_;
};

// An immutable, disconnected snapshot of a ChartOfAccounts + Ledger pair:
// one TrialBalanceLine per leaf/posting account (including zero-balance
// ones), ordered by ascending AccountCode, plus the totals of the debit
// and credit columns.
//
// TrialBalance owns copies of everything it exposes. It retains no
// reference or pointer to the ChartOfAccounts or Ledger it was generated
// from, so it safely outlives both and is never affected by further
// posting -- generating a new TrialBalance is the only way to see later
// activity.
//
// Ledger remains the sole source of posted balances; ChartOfAccounts
// remains the sole source of account metadata. TrialBalance derives from
// both but maintains no independent accounting state of its own.
class TrialBalance {
public:
    // Walks chart for leaf/posting accounts (group accounts are excluded,
    // since PostingEngine never posts to them and they have no direct
    // Ledger balance), reads each one's balance from ledger (defaulting
    // to zero for accounts never posted to), and converts each balance to
    // a debit/credit presentation via domain::debitCreditPresentation().
    //
    // Throws UnbalancedTrialBalanceException if the resulting totals do
    // not balance -- see that exception's documentation for when this is
    // actually reachable.
    static TrialBalance generate(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger);

    const domain::Currency& currency() const noexcept { return currency_; }
    const std::vector<TrialBalanceLine>& lines() const noexcept { return lines_; }
    const domain::Money& totalDebits() const noexcept { return totalDebits_; }
    const domain::Money& totalCredits() const noexcept { return totalCredits_; }

private:
    TrialBalance(domain::Currency currency, std::vector<TrialBalanceLine> lines, domain::Money totalDebits,
                 domain::Money totalCredits);

    domain::Currency currency_;
    std::vector<TrialBalanceLine> lines_;
    domain::Money totalDebits_;
    domain::Money totalCredits_;
};

} // namespace ledgercore::trialbalance
