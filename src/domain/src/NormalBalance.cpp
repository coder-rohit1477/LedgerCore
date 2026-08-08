#include "ledgercore/domain/NormalBalance.h"

namespace ledgercore::domain {

bool isDebitNormal(AccountType type) noexcept {
    switch (type) {
        case AccountType::Asset:
        case AccountType::Expense:
            return true;
        case AccountType::Liability:
        case AccountType::Equity:
        case AccountType::Revenue:
            return false;
    }
    return true;
}

Money signedEffect(AccountType type, DebitCreditSide side, const Money& amount) {
    const bool isOnNormalSide = ((side == DebitCreditSide::Debit) == isDebitNormal(type));
    return isOnNormalSide ? amount : -amount;
}

DebitCreditAmounts debitCreditPresentation(AccountType type, const Money& balance) {
    const Money zero = Money::zero(balance.currency());
    const bool debitNormal = isDebitNormal(type);

    if (balance.isNegative()) {
        const Money magnitude = -balance;
        return debitNormal ? DebitCreditAmounts{zero, magnitude} : DebitCreditAmounts{magnitude, zero};
    }

    return debitNormal ? DebitCreditAmounts{balance, zero} : DebitCreditAmounts{zero, balance};
}

} // namespace ledgercore::domain
