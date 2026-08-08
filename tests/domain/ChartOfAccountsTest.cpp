#include <gtest/gtest.h>

#include <string>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/DomainExceptions.h"

using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::DuplicateAccountCodeException;
using ledgercore::domain::ForeignAccountException;

TEST(ChartOfAccountsTest, DuplicateRootCodeIsRejected) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_THROW(
        chart.addRootAccount(AccountCode("1000"), "Duplicate", AccountType::Liability),
        DuplicateAccountCodeException);
}

TEST(ChartOfAccountsTest, DuplicateChildCodeIsRejectedEvenAcrossBranches) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& liabilities = chart.addRootAccount(AccountCode("2000"), "Liabilities", AccountType::Liability);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");

    EXPECT_THROW(
        chart.addChildAccount(liabilities, AccountCode("1110"), "Accounts Payable"),
        DuplicateAccountCodeException);
}

TEST(ChartOfAccountsTest, AccountFromAnotherChartCannotBeUsedAsParent) {
    ChartOfAccounts chartA;
    ChartOfAccounts chartB;
    Account& assetsInA = chartA.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_THROW(
        chartB.addChildAccount(assetsInA, AccountCode("1110"), "Cash"),
        ForeignAccountException);
}

TEST(ChartOfAccountsTest, OwnAccountIsStillAcceptedAsParent) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    Account& cash = chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    EXPECT_EQ(cash.parent(), &assets);
}

TEST(ChartOfAccountsTest, ForeignParentDoesNotCorruptEitherChartsIndex) {
    ChartOfAccounts chartA;
    ChartOfAccounts chartB;
    Account& assetsInA = chartA.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_THROW(
        chartB.addChildAccount(assetsInA, AccountCode("1110"), "Cash"),
        ForeignAccountException);

    EXPECT_FALSE(chartA.contains(AccountCode("1110")));
    EXPECT_FALSE(chartB.contains(AccountCode("1110")));
    EXPECT_TRUE(assetsInA.isLeaf());
}

TEST(ChartOfAccountsTest, FindByCodeReturnsMatchingAccount) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_EQ(chart.findByCode(AccountCode("1000")), &assets);
}

TEST(ChartOfAccountsTest, FindByCodeReturnsNullForUnknownCode) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_EQ(chart.findByCode(AccountCode("9999")), nullptr);
}

TEST(ChartOfAccountsTest, ContainsReflectsKnownCodes) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_TRUE(chart.contains(AccountCode("1000")));
    EXPECT_FALSE(chart.contains(AccountCode("9999")));
}

TEST(ChartOfAccountsTest, RootAccountsReturnsAllTopLevelAccountsInInsertionOrder) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& liabilities = chart.addRootAccount(AccountCode("2000"), "Liabilities", AccountType::Liability);

    auto roots = chart.rootAccounts();
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0], &assets);
    EXPECT_EQ(roots[1], &liabilities);
}

TEST(ChartOfAccountsTest, RootAccountsDoesNotIncludeChildren) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");

    EXPECT_EQ(chart.rootAccounts().size(), 1u);
}

TEST(ChartOfAccountsTest, FindByCodeCanLocateAnAccountToAttachFurtherChildrenTo) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    Account* assets = chart.findByCode(AccountCode("1000"));
    ASSERT_NE(assets, nullptr);

    Account& cash = chart.addChildAccount(*assets, AccountCode("1110"), "Cash");
    EXPECT_EQ(cash.parent(), assets);
}

TEST(ChartOfAccountsTest, AccountReferencesRemainStableAsMoreAccountsAreAdded) {
    ChartOfAccounts chart;
    Account& first = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    const auto firstId = first.id();

    for (int i = 0; i < 50; ++i) {
        chart.addRootAccount(AccountCode(std::to_string(9000 + i)), "Filler", AccountType::Expense);
    }

    EXPECT_EQ(first.id(), firstId);
    EXPECT_EQ(first.code().value(), "1000");
    EXPECT_EQ(chart.findByCode(AccountCode("1000")), &first);
}

TEST(ChartOfAccountsTest, DeepTreeDestructionDoesNotCrash) {
    {
        ChartOfAccounts chart;
        Account* current = &chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
        for (int i = 0; i < 100; ++i) {
            current = &chart.addChildAccount(*current, AccountCode("1000." + std::to_string(i)), "Level");
        }
    }
    SUCCEED();
}
