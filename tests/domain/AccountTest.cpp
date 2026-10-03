#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <utility>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"

using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::InvalidAccountException;
using ledgercore::domain::Currency;
using ledgercore::ledger::Ledger;

namespace {

// Attaching a child is only reachable through the ledger-aware
// posting::addChildAccount() (ChartOfAccounts's own operation is private).
// These tests exercise chart shape and chart-level rules only, so they
// pair the chart with a fresh, never-posted Ledger -- for which the
// posting-history check always passes and the call reduces to exactly
// the chart's own child-attach operation and validation.
Account& addChild(ChartOfAccounts& chart, Account& parent, AccountCode code, std::string name) {
    const Ledger unposted(Currency("USD"));
    return ledgercore::posting::addChildAccount(chart, unposted, parent, std::move(code), std::move(name));
}

} // namespace

// Account has identity and is owned only by its ChartOfAccounts: none of
// these may be possible through the public API, since each would let a
// caller replace or hollow out a chart-owned Account behind the chart's
// code/id indexes and its children's parent pointers.
static_assert(!std::is_copy_constructible_v<Account>, "Account must not be copy-constructible");
static_assert(!std::is_copy_assignable_v<Account>, "Account must not be copy-assignable");
static_assert(!std::is_move_constructible_v<Account>, "Account must not be move-constructible");
static_assert(!std::is_move_assignable_v<Account>, "Account must not be move-assignable");

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
    Account& cash = addChild(chart, assets, AccountCode("1110"), "Cash");

    EXPECT_EQ(cash.parent(), &assets);
    EXPECT_EQ(cash.type(), AccountType::Asset);
    EXPECT_FALSE(cash.isRoot());
}

TEST(AccountTest, ParentExposesItsChild) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = addChild(chart, assets, AccountCode("1110"), "Cash");

    auto children = assets.children();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children.front(), &cash);
    EXPECT_FALSE(assets.isLeaf());
}

TEST(AccountTest, MultipleChildrenUnderSameParent) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    addChild(chart, assets, AccountCode("1110"), "Cash");
    addChild(chart, assets, AccountCode("1120"), "Accounts Receivable");

    EXPECT_EQ(assets.children().size(), 2u);
}

TEST(AccountTest, MultipleNestingLevelsPreserveAncestryAndType) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& current = addChild(chart, assets, AccountCode("1100"), "Current Assets");
    Account& cash = addChild(chart, current, AccountCode("1110"), "Cash");

    ASSERT_NE(cash.parent(), nullptr);
    EXPECT_EQ(cash.parent(), &current);
    ASSERT_NE(cash.parent()->parent(), nullptr);
    EXPECT_EQ(cash.parent()->parent(), &assets);
    EXPECT_EQ(cash.type(), AccountType::Asset);
}

TEST(AccountTest, ChartOwnedAccountsKeepStableAddressesAndLinkageAcrossGrowth) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = addChild(chart, assets, AccountCode("1100"), "Cash");

    // Grow both the top-level list and the child list enough to force
    // their underlying vectors to reallocate; accounts are held by
    // unique_ptr, so neither Account is ever relocated or copied.
    for (int i = 0; i < 64; ++i) {
        chart.addRootAccount(AccountCode("9" + std::to_string(i)), "Filler", AccountType::Expense);
        addChild(chart, assets, AccountCode("12" + std::to_string(i)), "Filler child");
    }

    EXPECT_EQ(chart.findByCode(AccountCode("1000")), &assets);
    EXPECT_EQ(chart.findByCode(AccountCode("1100")), &cash);
    EXPECT_EQ(cash.parent(), &assets);
    EXPECT_EQ(assets.children().front(), &cash);
    EXPECT_EQ(chart.findById(cash.id()), &cash);
}
