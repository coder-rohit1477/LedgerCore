#pragma once

#include <utility>

#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::domain {

// Which column of a JournalEntry a line belongs to. Has no meaning
// outside JournalEntryLine, so it isn't its own header the way
// AccountType is (AccountType is a general classification other domain
// concepts may reference independently; this isn't).
enum class DebitCreditSide {
    Debit,
    Credit
};

// One leg of a double-entry transaction: an account reference, a
// strictly positive magnitude, and which side it belongs to.
//
// Design decision (Phase 3, Step 1): amount and side are separate
// fields (Money + DebitCreditSide), not a signed Money amount and not
// independent debit/credit Money fields. A signed amount would overload
// Money's generic sign with accounting meaning Money itself doesn't
// carry; separate debit/credit fields would let both be simultaneously
// nonzero or both zero, states with no accounting meaning. Construction
// is only via debit()/credit(), so a line can never end up on both
// sides or neither -- there is no parameter through which "both" or
// "neither" could even be expressed.
//
// Money permits negative and zero values -- that's correct for a
// generic value type. JournalEntryLine imposes the stricter rule that
// amount must be strictly positive: a zero-amount line has no
// accounting meaning, and a negative amount would create two spellings
// of the same fact (is "-$10 debit" the same as "$10 credit"?).
//
// AccountId is stored but never validated against a ChartOfAccounts --
// that check belongs to the future Posting Engine, which is the only
// component that has both a JournalEntry and a specific chart to check
// it against.
class JournalEntryLine {
public:
    static JournalEntryLine debit(AccountId accountId, Money amount) {
        return JournalEntryLine(accountId, std::move(amount), DebitCreditSide::Debit);
    }

    static JournalEntryLine credit(AccountId accountId, Money amount) {
        return JournalEntryLine(accountId, std::move(amount), DebitCreditSide::Credit);
    }

    AccountId accountId() const noexcept { return accountId_; }
    const Money& amount() const noexcept { return amount_; }
    DebitCreditSide side() const noexcept { return side_; }

    bool isDebit() const noexcept { return side_ == DebitCreditSide::Debit; }
    bool isCredit() const noexcept { return side_ == DebitCreditSide::Credit; }

private:
    JournalEntryLine(AccountId accountId, Money amount, DebitCreditSide side)
        : accountId_(accountId), amount_(std::move(amount)), side_(side) {
        if (!amount_.isPositive()) {
            throw InvalidJournalEntryLineException("JournalEntryLine amount must be strictly positive");
        }
    }

    AccountId accountId_;
    Money amount_;
    DebitCreditSide side_;
};

} // namespace ledgercore::domain
