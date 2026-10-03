// Allocation-failure (fault-injection) tests: each mutating operation is run
// with the global operator new armed to throw std::bad_alloc at its 0th,
// 1st, 2nd, ... allocation until the operation completes. After every
// injected failure no observable state may have changed (strong
// guarantee); after the final, successful attempt the operation's effect
// must be fully visible.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <vector>

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"

namespace {

// -1: disarmed. Otherwise the number of allocations still allowed before
// the next one throws.
long g_allocationsUntilFailure = -1;

void* allocate(std::size_t size) {
    if (g_allocationsUntilFailure == 0) {
        g_allocationsUntilFailure = -1;
        throw std::bad_alloc();
    }
    if (g_allocationsUntilFailure > 0) {
        --g_allocationsUntilFailure;
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

} // namespace

// Every allocation and deallocation form goes through malloc/free, so a
// sanitizer runtime never sees memory allocated by one family and freed by
// another (the standard library uses the nothrow forms internally).
void* operator new(std::size_t size) {
    return allocate(size);
}
void* operator new[](std::size_t size) {
    return allocate(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocate(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocate(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    std::free(memory);
}
void operator delete(void* memory) noexcept {
    std::free(memory);
}
void operator delete[](void* memory) noexcept {
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

using ledgercore::computed::ComputedAccountRegistry;
using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountId;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::formula::ComputedAccountName;
using ledgercore::ledger::Ledger;
using ledgercore::posting::post;

namespace {

std::chrono::system_clock::time_point day(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * n);
}

Money usd(long long major) {
    return Money::fromMajorUnits(major, 0, Currency("USD"));
}

// Runs operation with the n-th allocation failing, for n = 0, 1, 2, ...,
// calling verifyUnchanged() after each injected failure, until operation
// succeeds. Returns the number of injected failures.
int runWithEveryAllocationFailing(const std::function<void()>& operation,
                                  const std::function<void()>& verifyUnchanged) {
    for (int failures = 0; failures < 10000; ++failures) {
        g_allocationsUntilFailure = failures;
        try {
            operation();
            g_allocationsUntilFailure = -1;
            return failures;
        } catch (const std::bad_alloc&) {
            g_allocationsUntilFailure = -1;
            verifyUnchanged();
        }
    }
    g_allocationsUntilFailure = -1;
    ADD_FAILURE() << "operation never completed";
    return -1;
}

struct Books {
    ChartOfAccounts chart;
    Ledger ledger{Currency("USD")};
    AccountId cash{0};
    AccountId sales{0};
    AccountId rent{0};
    AccountId retained{0};
    Books() {
        cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset).id();
        retained = chart.addRootAccount(AccountCode("3100"), "Retained", AccountType::Equity).id();
        sales = chart.addRootAccount(AccountCode("4000"), "Sales", AccountType::Revenue).id();
        rent = chart.addRootAccount(AccountCode("5000"), "Rent", AccountType::Expense).id();
        post(JournalEntry::create(day(1), "Sale", {JournalEntryLine::debit(cash, usd(100)),
                                                   JournalEntryLine::credit(sales, usd(100))}),
             chart, ledger);
    }
    std::vector<Money> balances() const {
        return {ledger.balance(cash), ledger.balance(sales), ledger.balance(rent), ledger.balance(retained)};
    }
};

} // namespace

TEST(AllocationFailureTest, PostingIsAllOrNothingUnderAllocationFailure) {
    Books books;
    const std::vector<Money> before = books.balances();
    // Rent has never been posted to: commit must insert a new balance key.
    const JournalEntry entry = JournalEntry::create(
        day(2), "Rent", {JournalEntryLine::debit(books.rent, usd(30)), JournalEntryLine::credit(books.cash, usd(30))});

    const int failures = runWithEveryAllocationFailing(
        [&] { post(entry, books.chart, books.ledger); },
        [&] {
            EXPECT_EQ(books.ledger.postedEntries().size(), 1u);
            EXPECT_EQ(books.balances(), before);
            EXPECT_FALSE(books.ledger.hasPostingHistory(books.rent));
        });

    EXPECT_GT(failures, 0);
    EXPECT_EQ(books.ledger.postedEntries().size(), 2u);
    EXPECT_EQ(books.ledger.balance(books.rent), usd(30));
    EXPECT_EQ(books.ledger.balance(books.cash), usd(70));
}

TEST(AllocationFailureTest, ClosingEntryIsAllOrNothingUnderAllocationFailure) {
    Books books;
    const std::vector<Money> before = books.balances();
    const JournalEntry closing = JournalEntry::createClosing(
        day(363), "Close", {JournalEntryLine::debit(books.sales, usd(100)), JournalEntryLine::credit(books.retained, usd(100))});

    runWithEveryAllocationFailing(
        [&] { post(closing, books.chart, books.ledger); },
        [&] {
            EXPECT_EQ(books.ledger.postedEntries().size(), 1u);
            EXPECT_EQ(books.ledger.closingEntryCount(), 0u);
            EXPECT_EQ(books.balances(), before);
        });

    EXPECT_EQ(books.ledger.closingEntryCount(), 1u);
    EXPECT_TRUE(books.ledger.balance(books.sales).isZero());
    EXPECT_EQ(books.ledger.balance(books.retained), usd(100));
}

TEST(AllocationFailureTest, AddingARootAccountIsAllOrNothingUnderAllocationFailure) {
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);

    runWithEveryAllocationFailing(
        [&] { chart.addRootAccount(AccountCode("2000"), "Payable", AccountType::Liability); },
        [&] {
            // No index may refer to an account the chart does not own.
            EXPECT_EQ(chart.findByCode(AccountCode("2000")), nullptr);
            EXPECT_EQ(chart.rootAccounts().size(), 1u);
        });

    const Account* payable = chart.findByCode(AccountCode("2000"));
    ASSERT_NE(payable, nullptr);
    EXPECT_EQ(chart.findById(payable->id()), payable);
    EXPECT_EQ(chart.rootAccounts().size(), 2u);
}

TEST(AllocationFailureTest, AddingAChildAccountIsAllOrNothingUnderAllocationFailure) {
    ChartOfAccounts chart;
    Ledger ledger(Currency("USD"));
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);

    runWithEveryAllocationFailing(
        [&] { ledgercore::posting::addChildAccount(chart, ledger, assets, AccountCode("1100"), "Cash"); },
        [&] {
            EXPECT_EQ(chart.findByCode(AccountCode("1100")), nullptr);
            EXPECT_TRUE(assets.isLeaf());
        });

    const Account* cash = chart.findByCode(AccountCode("1100"));
    ASSERT_NE(cash, nullptr);
    EXPECT_EQ(chart.findById(cash->id()), cash);
    EXPECT_EQ(cash->parent(), &assets);
    EXPECT_FALSE(assets.isLeaf());
}

TEST(AllocationFailureTest, DefiningAComputedAccountIsAllOrNothingUnderAllocationFailure) {
    ComputedAccountRegistry registry;
    registry.define(ComputedAccountName("Existing"), "#1000");

    runWithEveryAllocationFailing(
        [&] { registry.define(ComputedAccountName("Profit"), "#4000 - #5000"); },
        [&] {
            EXPECT_EQ(registry.find(ComputedAccountName("Profit")), nullptr);
            EXPECT_EQ(registry.definitions().size(), 1u);
        });

    ASSERT_NE(registry.find(ComputedAccountName("Profit")), nullptr);
    ASSERT_EQ(registry.definitions().size(), 2u);
    EXPECT_EQ(registry.definitions()[1]->name().value(), "Profit");
}
