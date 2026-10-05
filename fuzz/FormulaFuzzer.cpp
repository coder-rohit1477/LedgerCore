// libFuzzer target: arbitrary text as a formula, parsed and evaluated
// against a fixed chart and ledger -- directly, and as a computed account
// next to fixed definitions, so `@name` references, cycles, and the
// evaluation depth budget are reachable.
//
// Contract checked for every input: parsing and evaluation either produce
// a value or throw a ledgercore::LedgerException (anything else escaping is
// reported as a crash), and the outcome is deterministic.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/computed/LedgerAccountResolver.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/formula/Evaluator.h"
#include "ledgercore/formula/Parser.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"

namespace {

using namespace ledgercore;

// Accounts 1000 (Asset), 2000 (Liability), 3000 (Equity), 4000 (Revenue),
// 5000 (Expense) and a group 6000 with leaf 6000-1, with non-trivial
// balances including a large one for overflow paths.
struct Books {
    domain::ChartOfAccounts chart;
    ledger::Ledger ledger{domain::Currency("USD")};

    Books() {
        const auto usd = domain::Currency("USD");
        const auto add = [this](const char* code, const char* name, domain::AccountType type) {
            return chart.addRootAccount(domain::AccountCode(code), name, type).id();
        };
        const auto cash = add("1000", "Cash", domain::AccountType::Asset);
        const auto payable = add("2000", "Payable", domain::AccountType::Liability);
        const auto capital = add("3000", "Capital", domain::AccountType::Equity);
        const auto sales = add("4000", "Sales", domain::AccountType::Revenue);
        const auto rent = add("5000", "Rent", domain::AccountType::Expense);
        auto& group = chart.addRootAccount(domain::AccountCode("6000"), "Group", domain::AccountType::Asset);
        const auto leaf = posting::addChildAccount(chart, ledger, group, domain::AccountCode("6000-1"), "Leaf").id();
        const auto day = std::chrono::system_clock::time_point{} + std::chrono::hours(24 * 20000);
        const auto post = [&](domain::AccountId debit, domain::AccountId credit, std::int64_t minor) {
            const domain::Money amount = domain::Money::ofMinorUnits(minor, usd);
            posting::post(domain::JournalEntry::create(day, "fixture", {domain::JournalEntryLine::debit(debit, amount),
                                                                        domain::JournalEntryLine::credit(credit, amount)}),
                          chart, ledger);
        };
        post(cash, sales, 123456);
        post(rent, payable, 7890);
        post(leaf, capital, 4'000'000'000'000'000'000);
    }
};

const Books& books() {
    static const Books instance;
    return instance;
}

std::string evaluateDirectly(const std::string& source, const formula::AccountResolver& resolver) {
    try {
        const formula::AstNodePtr ast = formula::parse(source);
        const formula::FormulaValue value = formula::evaluate(*ast, resolver);
        return value.isMoney() ? "money " + value.asMoney().toString() : "scalar " + value.asScalar().toString();
    } catch (const LedgerException& e) {
        return std::string("error ") + e.what();
    }
}

std::string evaluateAsComputedAccount(const std::string& source, const formula::AccountResolver& resolver) {
    try {
        computed::ComputedAccountRegistry registry;
        registry.define(formula::ComputedAccountName("Profit"), "#4000 - #5000");
        registry.define(formula::ComputedAccountName("Double"), "@Profit * 2");
        registry.define(formula::ComputedAccountName("Fuzz"), source);
        registry.define(formula::ComputedAccountName("UsesFuzz"), "@Fuzz + @Double");
        return "money " + registry.evaluate(formula::ComputedAccountName("UsesFuzz"), resolver).toString();
    } catch (const LedgerException& e) {
        return std::string("error ") + e.what();
    }
}

std::string outcome(const std::string& source) {
    const computed::LedgerAccountResolver resolver(books().chart, books().ledger);
    return evaluateDirectly(source, resolver) + "\n" + evaluateAsComputedAccount(source, resolver);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string source(reinterpret_cast<const char*>(data), size);
    if (outcome(source) != outcome(source)) {
        std::fprintf(stderr, "formula fuzzer: evaluating the same formula twice gave different outcomes\n");
        std::abort();
    }
    return 0;
}
