#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountId.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"

using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::DuplicateAccountCodeException;
using ledgercore::domain::ChartDepthExceededException;
using ledgercore::domain::ForeignAccountException;
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

// ---------------------------------------------------------------------
// API boundary: no public bypass of the ledger-aware child path
//
// Detection idiom: access checking is part of template argument
// substitution (C++11 and later), so these are true exactly when an
// ordinary external caller -- like this test file -- could compile the
// call, and false when the member is private.
// ---------------------------------------------------------------------

namespace {

template <typename Chart, typename = void>
struct CanCallChartAddChildAccount : std::false_type {};

template <typename Chart>
struct CanCallChartAddChildAccount<
    Chart, std::void_t<decltype(std::declval<Chart&>().addChildAccount(
               std::declval<Account&>(), std::declval<AccountCode>(), std::declval<std::string>()))>>
    : std::true_type {};

template <typename Chart, typename = void>
struct CanCallChartAddRootAccount : std::false_type {};

template <typename Chart>
struct CanCallChartAddRootAccount<
    Chart, std::void_t<decltype(std::declval<Chart&>().addRootAccount(
               std::declval<AccountCode>(), std::declval<std::string>(), std::declval<AccountType>()))>>
    : std::true_type {};

} // namespace

// The low-level child-attach operation is unreachable from outside:
// calling it directly would skip the posting-history check that keeps a
// posted leaf from becoming a group account.
static_assert(!CanCallChartAddChildAccount<ChartOfAccounts>::value,
              "ChartOfAccounts::addChildAccount must not be callable by external code");
// Positive control: the same detection idiom does see a public member,
// so the assertion above is not vacuously true.
static_assert(CanCallChartAddRootAccount<ChartOfAccounts>::value,
              "detection idiom must report public ChartOfAccounts members as callable");
// The supported public path is the ledger-aware posting function.
static_assert(std::is_invocable_r_v<Account&, decltype(&ledgercore::posting::addChildAccount), ChartOfAccounts&,
                                    const Ledger&, Account&, AccountCode, std::string>,
              "posting::addChildAccount must remain the public way to add a child account");

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
    addChild(chart, assets, AccountCode("1110"), "Cash");

    EXPECT_THROW(
        addChild(chart, liabilities, AccountCode("1110"), "Accounts Payable"),
        DuplicateAccountCodeException);
}

TEST(ChartOfAccountsTest, AccountFromAnotherChartCannotBeUsedAsParent) {
    ChartOfAccounts chartA;
    ChartOfAccounts chartB;
    Account& assetsInA = chartA.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_THROW(
        addChild(chartB, assetsInA, AccountCode("1110"), "Cash"),
        ForeignAccountException);
}

TEST(ChartOfAccountsTest, OwnAccountIsStillAcceptedAsParent) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    Account& cash = addChild(chart, assets, AccountCode("1110"), "Cash");
    EXPECT_EQ(cash.parent(), &assets);
}

TEST(ChartOfAccountsTest, ForeignParentDoesNotCorruptEitherChartsIndex) {
    ChartOfAccounts chartA;
    ChartOfAccounts chartB;
    Account& assetsInA = chartA.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_THROW(
        addChild(chartB, assetsInA, AccountCode("1110"), "Cash"),
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

TEST(ChartOfAccountsTest, FindByIdReturnsMatchingAccount) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& cash = addChild(chart, assets, AccountCode("1110"), "Cash");

    EXPECT_EQ(chart.findById(assets.id()), &assets);
    EXPECT_EQ(chart.findById(cash.id()), &cash);
}

TEST(ChartOfAccountsTest, FindByIdReturnsNullForUnknownId) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    EXPECT_EQ(chart.findById(AccountId(999)), nullptr);
}

TEST(ChartOfAccountsTest, FindByIdDoesNotConfuseIdsAcrossTwoCharts) {
    ChartOfAccounts chartA;
    ChartOfAccounts chartB;
    Account& assetsInA = chartA.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& assetsInB = chartB.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    ASSERT_EQ(assetsInA.id(), assetsInB.id());
    EXPECT_EQ(chartA.findById(assetsInA.id()), &assetsInA);
    EXPECT_EQ(chartB.findById(assetsInB.id()), &assetsInB);
}

TEST(ChartOfAccountsTest, FindByIdIsConstCorrect) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    const ChartOfAccounts& constChart = chart;

    const Account* found = constChart.findById(assets.id());
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->code().value(), "1000");
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
    addChild(chart, assets, AccountCode("1110"), "Cash");

    EXPECT_EQ(chart.rootAccounts().size(), 1u);
}

TEST(ChartOfAccountsTest, FindByCodeCanLocateAnAccountToAttachFurtherChildrenTo) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    Account* assets = chart.findByCode(AccountCode("1000"));
    ASSERT_NE(assets, nullptr);

    Account& cash = addChild(chart, *assets, AccountCode("1110"), "Cash");
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
            current = &addChild(chart, *current, AccountCode("1000." + std::to_string(i)), "Level");
        }
    }
    SUCCEED();
}

// ---------------------------------------------------------------------
// Bounded depth and iterative traversal (Phase 20)
// ---------------------------------------------------------------------

namespace {

// A single chain root -> ... -> leaf of exactly `depth` levels; returns the
// deepest account.
Account& buildChain(ChartOfAccounts& chart, std::size_t depth) {
    Account* current = &chart.addRootAccount(AccountCode("L1"), "Level", AccountType::Asset);
    for (std::size_t level = 2; level <= depth; ++level) {
        current = &addChild(chart, *current, AccountCode("L" + std::to_string(level)), "Level");
    }
    return *current;
}

} // namespace

TEST(ChartOfAccountsTest, ChartExactlyAtMaximumDepthIsAllowed) {
    ChartOfAccounts chart;
    Account& deepest = buildChain(chart, ChartOfAccounts::kMaxDepth);
    EXPECT_TRUE(deepest.isLeaf());
    EXPECT_EQ(chart.findByCode(AccountCode("L" + std::to_string(ChartOfAccounts::kMaxDepth))), &deepest);
}

TEST(ChartOfAccountsTest, ChildBeyondMaximumDepthIsRejectedWithoutMutation) {
    ChartOfAccounts chart;
    Account& deepest = buildChain(chart, ChartOfAccounts::kMaxDepth);
    EXPECT_THROW(addChild(chart, deepest, AccountCode("TooDeep"), "Too deep"), ChartDepthExceededException);
    EXPECT_FALSE(chart.contains(AccountCode("TooDeep")));
    EXPECT_TRUE(deepest.isLeaf());
    // Shallower accounts can still grow.
    Account* level2 = chart.findByCode(AccountCode("L2"));
    ASSERT_NE(level2, nullptr);
    EXPECT_NO_THROW(addChild(chart, *level2, AccountCode("Sibling"), "Sibling"));
}

TEST(ChartOfAccountsTest, MaximumDepthChartsAreDestroyedWithoutRecursion) {
    // Several maximal chains plus a wide fan-out, created and destroyed
    // repeatedly; teardown is iterative, so this is bounded regardless.
    for (int round = 0; round < 3; ++round) {
        ChartOfAccounts chart;
        buildChain(chart, ChartOfAccounts::kMaxDepth);
        Account& wide = chart.addRootAccount(AccountCode("W"), "Wide", AccountType::Expense);
        for (int i = 0; i < 2000; ++i) {
            addChild(chart, wide, AccountCode("W" + std::to_string(i)), "Leaf");
        }
    }
    SUCCEED();
}

TEST(ChartOfAccountsTest, PreOrderVisitIsParentFirstChildrenInInsertionOrderWithDepths) {
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    Account& current = addChild(chart, assets, AccountCode("1100"), "Current");
    addChild(chart, current, AccountCode("1110"), "Cash");
    addChild(chart, current, AccountCode("1120"), "Receivables");
    addChild(chart, assets, AccountCode("1200"), "Fixed");
    chart.addRootAccount(AccountCode("2000"), "Liabilities", AccountType::Liability);

    std::vector<std::string> visited;
    chart.forEachAccountPreOrder([&visited](const Account& account, std::size_t depth) {
        visited.push_back(account.code().value() + "@" + std::to_string(depth));
    });

    EXPECT_EQ(visited, (std::vector<std::string>{"1000@1", "1100@2", "1110@3", "1120@3", "1200@2", "2000@1"}));
}

TEST(ChartOfAccountsTest, PreOrderVisitHandlesMaximumDepth) {
    ChartOfAccounts chart;
    buildChain(chart, ChartOfAccounts::kMaxDepth);
    std::size_t count = 0;
    std::size_t deepest = 0;
    chart.forEachAccountPreOrder([&](const Account&, std::size_t depth) {
        ++count;
        deepest = std::max(deepest, depth);
    });
    EXPECT_EQ(count, ChartOfAccounts::kMaxDepth);
    EXPECT_EQ(deepest, ChartOfAccounts::kMaxDepth);
}
