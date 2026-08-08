#include <gtest/gtest.h>

#include <chrono>

#include "ledgercore/computed/LedgerAccountResolver.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"

using ledgercore::computed::LedgerAccountResolver;
using ledgercore::domain::Account;
using ledgercore::domain::AccountCode;
using ledgercore::domain::AccountType;
using ledgercore::domain::ChartOfAccounts;
using ledgercore::domain::Currency;
using ledgercore::domain::JournalEntry;
using ledgercore::domain::JournalEntryLine;
using ledgercore::domain::Money;
using ledgercore::formula::UnknownAccountReferenceException;
using ledgercore::ledger::Ledger;
using ledgercore::posting::post;

namespace {
std::chrono::system_clock::time_point testDate() {
    return std::chrono::system_clock::now();
}
} // namespace

TEST(LedgerAccountResolverTest, ResolvesRealLeafAccountBalance) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& cash = chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Account& revenue = chart.addRootAccount(AccountCode("4000"), "Revenue", AccountType::Revenue);
    Ledger ledger(usd);

    post(JournalEntry::create(testDate(), "Sale",
                               {
                                   JournalEntryLine::debit(cash.id(), Money::fromMajorUnits(100, 0, usd)),
                                   JournalEntryLine::credit(revenue.id(), Money::fromMajorUnits(100, 0, usd)),
                               }),
         chart, ledger);

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_EQ(resolver.resolve(AccountCode("1000")), Money::fromMajorUnits(100, 0, usd));
}

TEST(LedgerAccountResolverTest, UnpostedLeafAccountResolvesToZero) {
    Currency usd("USD");
    ChartOfAccounts chart;
    chart.addRootAccount(AccountCode("1000"), "Cash", AccountType::Asset);
    Ledger ledger(usd);

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_TRUE(resolver.resolve(AccountCode("1000")).isZero());
}

TEST(LedgerAccountResolverTest, UnknownAccountCodeThrows) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Ledger ledger(usd);

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(resolver.resolve(AccountCode("9999")), UnknownAccountReferenceException);
}

TEST(LedgerAccountResolverTest, NonLeafGroupAccountIsRejected) {
    Currency usd("USD");
    ChartOfAccounts chart;
    Account& assets = chart.addRootAccount(AccountCode("1000"), "Assets", AccountType::Asset);
    chart.addChildAccount(assets, AccountCode("1110"), "Cash");
    Ledger ledger(usd);

    LedgerAccountResolver resolver(chart, ledger);
    EXPECT_THROW(resolver.resolve(AccountCode("1000")), UnknownAccountReferenceException);
}
