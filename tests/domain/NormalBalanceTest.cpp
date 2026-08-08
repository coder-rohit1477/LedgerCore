#include <gtest/gtest.h>

#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/domain/NormalBalance.h"

using ledgercore::domain::AccountType;
using ledgercore::domain::Currency;
using ledgercore::domain::DebitCreditAmounts;
using ledgercore::domain::debitCreditPresentation;
using ledgercore::domain::DebitCreditSide;
using ledgercore::domain::isDebitNormal;
using ledgercore::domain::Money;
using ledgercore::domain::signedEffect;

namespace {
struct NormalBalanceCase {
    AccountType type;
    bool debitNormal;
};

const NormalBalanceCase kAllAccountTypes[] = {
    {AccountType::Asset, true},
    {AccountType::Expense, true},
    {AccountType::Liability, false},
    {AccountType::Equity, false},
    {AccountType::Revenue, false},
};
} // namespace

// ---------------------------------------------------------------------
// isDebitNormal()
// ---------------------------------------------------------------------

TEST(NormalBalanceTest, IsDebitNormalForEachAccountType) {
    EXPECT_TRUE(isDebitNormal(AccountType::Asset));
    EXPECT_TRUE(isDebitNormal(AccountType::Expense));
    EXPECT_FALSE(isDebitNormal(AccountType::Liability));
    EXPECT_FALSE(isDebitNormal(AccountType::Equity));
    EXPECT_FALSE(isDebitNormal(AccountType::Revenue));
}

// ---------------------------------------------------------------------
// signedEffect(): all 10 AccountType x DebitCreditSide combinations
// ---------------------------------------------------------------------

TEST(NormalBalanceTest, SignedEffectForAllAccountTypeDebitCreditCombinations) {
    Currency usd("USD");
    const Money amount = Money::fromMajorUnits(10, 0, usd);

    for (const NormalBalanceCase& testCase : kAllAccountTypes) {
        const Money debitEffect = signedEffect(testCase.type, DebitCreditSide::Debit, amount);
        const Money creditEffect = signedEffect(testCase.type, DebitCreditSide::Credit, amount);

        const Money expectedDebitEffect = testCase.debitNormal ? amount : -amount;
        const Money expectedCreditEffect = testCase.debitNormal ? -amount : amount;

        EXPECT_EQ(debitEffect, expectedDebitEffect);
        EXPECT_EQ(creditEffect, expectedCreditEffect);
    }
}

// ---------------------------------------------------------------------
// debitCreditPresentation()
// ---------------------------------------------------------------------

TEST(NormalBalanceTest, DebitCreditPresentationForPositiveBalances) {
    Currency usd("USD");
    const Money positive = Money::fromMajorUnits(100, 0, usd);

    for (const NormalBalanceCase& testCase : kAllAccountTypes) {
        const DebitCreditAmounts result = debitCreditPresentation(testCase.type, positive);
        if (testCase.debitNormal) {
            EXPECT_EQ(result.debit, positive);
            EXPECT_TRUE(result.credit.isZero());
        } else {
            EXPECT_EQ(result.credit, positive);
            EXPECT_TRUE(result.debit.isZero());
        }
    }
}

TEST(NormalBalanceTest, DebitCreditPresentationForNegativeBalances) {
    Currency usd("USD");
    const Money negative = Money::fromMajorUnits(-100, 0, usd);
    const Money magnitude = Money::fromMajorUnits(100, 0, usd);

    for (const NormalBalanceCase& testCase : kAllAccountTypes) {
        const DebitCreditAmounts result = debitCreditPresentation(testCase.type, negative);
        if (testCase.debitNormal) {
            EXPECT_EQ(result.credit, magnitude);
            EXPECT_TRUE(result.debit.isZero());
        } else {
            EXPECT_EQ(result.debit, magnitude);
            EXPECT_TRUE(result.credit.isZero());
        }
    }
}

TEST(NormalBalanceTest, DebitCreditPresentationForZeroBalances) {
    Currency usd("USD");
    const Money zero = Money::zero(usd);

    for (const NormalBalanceCase& testCase : kAllAccountTypes) {
        const DebitCreditAmounts result = debitCreditPresentation(testCase.type, zero);
        EXPECT_TRUE(result.debit.isZero());
        EXPECT_TRUE(result.credit.isZero());
    }
}

// ---------------------------------------------------------------------
// Exact Money arithmetic
// ---------------------------------------------------------------------

TEST(NormalBalanceTest, SignedEffectIsExactAcrossMinorUnitAmounts) {
    // 0.1 + 0.2 != 0.3 in binary floating point; via Money's minor-unit
    // integers this must be exact.
    Currency usd("USD");
    const Money ten = Money::fromMajorUnits(0, 10, usd);
    const Money twenty = Money::fromMajorUnits(0, 20, usd);

    Money balance = Money::zero(usd);
    balance = balance + signedEffect(AccountType::Asset, DebitCreditSide::Debit, ten);
    balance = balance + signedEffect(AccountType::Asset, DebitCreditSide::Debit, twenty);

    EXPECT_EQ(balance, Money::fromMajorUnits(0, 30, usd));
}

// ---------------------------------------------------------------------
// Consistency between signedEffect() and debitCreditPresentation()
// ---------------------------------------------------------------------

TEST(NormalBalanceTest, DebitCreditPresentationInvertsSignedEffectFromZero) {
    // debitCreditPresentation() is the inverse of signedEffect(): applying
    // an amount to a zero balance and then presenting the result must
    // recover exactly the original amount on exactly the original side,
    // for every AccountType.
    Currency usd("USD");
    const Money amount = Money::fromMajorUnits(75, 0, usd);

    for (const NormalBalanceCase& testCase : kAllAccountTypes) {
        {
            const Money balance = Money::zero(usd) + signedEffect(testCase.type, DebitCreditSide::Debit, amount);
            const DebitCreditAmounts presentation = debitCreditPresentation(testCase.type, balance);
            EXPECT_EQ(presentation.debit, amount);
            EXPECT_TRUE(presentation.credit.isZero());
        }
        {
            const Money balance = Money::zero(usd) + signedEffect(testCase.type, DebitCreditSide::Credit, amount);
            const DebitCreditAmounts presentation = debitCreditPresentation(testCase.type, balance);
            EXPECT_EQ(presentation.credit, amount);
            EXPECT_TRUE(presentation.debit.isZero());
        }
    }
}
