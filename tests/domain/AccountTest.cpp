#include <gtest/gtest.h>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/DomainExceptions.h"

using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::InvalidAccountException;

TEST(AccountCodeTest, RejectsEmptyValue) {
    EXPECT_THROW(AccountCode(""), InvalidAccountException);
}

TEST(AccountCodeTest, AcceptsNonEmptyValue) {
    AccountCode code("1000");
    EXPECT_EQ(code.value(), "1000");
}

TEST(AccountTest, RootAccountConstructionSetsAllFields) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_EQ(assets.code().value(), "1000");
    EXPECT_EQ(assets.name(), "Assets");
    EXPECT_EQ(assets.type(), AccountType::Asset);
    EXPECT_TRUE(assets.isRoot());
    EXPECT_EQ(assets.parent(), nullptr);
    EXPECT_TRUE(assets.isLeaf());
}

TEST(AccountTest, EmptyNameIsRejected) {
    ChartOfAccounts chart;
    EXPECT_THROW(chart.addRootAccount(AccountCode("1000"), "", AccountType::Asset), InvalidAccountException);
}

TEST(AccountTest, AccountIdsAreUniquePerAccount) {
    ChartOfAccounts chart;
    Account& a = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& b = chart.addRootAccount(AccountCode("2000"), "Liabilities", AccountType::Liability);

    EXPECT_NE(a.id(), b.id());
}

TEST(AccountTest, ChildAccountCreationSetsParentAndInheritsType) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = chart.addChildAccount(assets, AccountCode("1110"), "Cash");

    EXPECT_EQ(cash.parent(), &assets);
    EXPECT_EQ(cash.type(), AccountType::Asset);
    EXPECT_FALSE(cash.isRoot());
}

TEST(AccountTest, ParentExposesItsChild) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = chart.addChildAccount(assets, AccountCode("1110"), "Cash");

    auto children = assets.children();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children.front(), &cash);
    EXPECT_FALSE(assets.isLeaf());
}

TEST(AccountTest, MultipleChildrenUnderSameParent) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    chart.addChildAccount(assets, AccountCode("1120"), "Accounts Receivable");

    EXPECT_EQ(assets.children().size(), 2u);
}

TEST(AccountTest, MultipleNestingLevelsPreserveAncestryAndType) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& current = chart.addChildAccount(assets, AccountCode("1100"), "Current Assets");
    Account& cash = chart.addChildAccount(current, AccountCode("1110"), "Cash");

    ASSERT_NE(cash.parent(), nullptr);
    EXPECT_EQ(cash.parent(), &current);
    ASSERT_NE(cash.parent()->parent(), nullptr);
    EXPECT_EQ(cash.parent()->parent(), &assets);
    EXPECT_EQ(cash.type(), AccountType::Asset);
}
