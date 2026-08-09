#include "OutputFormatting.h"

#include <iomanip>
#include <string>

#include "ledgercore/domain/AccountType.h"
#include "ledgercore/reporting/ReportLine.h"

namespace ledgercore::cli {

namespace {

constexpr int kCodeWidth = 10;
constexpr int kNameWidth = 24;
constexpr int kAmountWidth = 18;

std::string accountTypeToString(domain::AccountType type) {
    switch (type) {
        case domain::AccountType::Asset:
            return "asset";
        case domain::AccountType::Liability:
            return "liability";
        case domain::AccountType::Equity:
            return "equity";
        case domain::AccountType::Revenue:
            return "revenue";
        case domain::AccountType::Expense:
            return "expense";
    }
    return "unknown";
}

void printSection(std::ostream& out, const reporting::ReportSection& section) {
    out << section.label() << ":\n";
    for (const reporting::ReportLine& line : section.lines()) {
        out << "  " << std::left << std::setw(kCodeWidth) << line.accountCode().value() << std::setw(kNameWidth)
            << line.accountName() << std::right << std::setw(kAmountWidth) << line.amount().toString() << "\n";
    }
    out << "  " << std::left << std::setw(kCodeWidth + kNameWidth) << "Total" << std::right
        << std::setw(kAmountWidth) << section.total().toString() << "\n";
}

void printAccountFlat(std::ostream& out, const domain::Account* account) {
    out << std::left << std::setw(kCodeWidth) << account->code().value() << std::setw(kNameWidth) << account->name()
        << std::setw(11) << accountTypeToString(account->type()) << (account->isLeaf() ? "leaf" : "group") << "\n";
    for (const domain::Account* child : account->children()) {
        printAccountFlat(out, child);
    }
}

void printAccountTree(std::ostream& out, const domain::Account* account, std::size_t depth) {
    out << std::string(depth * 2, ' ') << account->code().value() << "  " << account->name() << "\n";
    for (const domain::Account* child : account->children()) {
        printAccountTree(out, child, depth + 1);
    }
}

} // namespace

void printTrialBalance(std::ostream& out, const trialbalance::TrialBalance& trialBalance) {
    out << "Trial Balance (" << trialBalance.currency().code() << ")\n";
    out << std::left << std::setw(kCodeWidth) << "CODE" << std::setw(kNameWidth) << "NAME" << std::right
        << std::setw(kAmountWidth) << "DEBIT" << std::setw(kAmountWidth) << "CREDIT" << "\n";
    for (const trialbalance::TrialBalanceLine& line : trialBalance.lines()) {
        out << std::left << std::setw(kCodeWidth) << line.accountCode().value() << std::setw(kNameWidth)
            << line.accountName() << std::right << std::setw(kAmountWidth) << line.debit().toString()
            << std::setw(kAmountWidth) << line.credit().toString() << "\n";
    }
    out << std::left << std::setw(kCodeWidth + kNameWidth) << "TOTAL" << std::right << std::setw(kAmountWidth)
        << trialBalance.totalDebits().toString() << std::setw(kAmountWidth) << trialBalance.totalCredits().toString()
        << "\n";
}

void printBalanceSheet(std::ostream& out, const reporting::BalanceSheet& balanceSheet) {
    out << "Balance Sheet (" << balanceSheet.currency().code() << ")\n";
    printSection(out, balanceSheet.assets());
    printSection(out, balanceSheet.liabilities());
    printSection(out, balanceSheet.equity());
}

void printIncomeStatement(std::ostream& out, const reporting::IncomeStatement& incomeStatement) {
    out << "Income Statement (" << incomeStatement.currency().code() << ")\n";
    printSection(out, incomeStatement.revenue());
    printSection(out, incomeStatement.expenses());
    out << std::left << std::setw(kCodeWidth + kNameWidth) << "Net Income" << std::right << std::setw(kAmountWidth)
        << incomeStatement.netIncome().toString() << "\n";
}

void printAccountList(std::ostream& out, const domain::ChartOfAccounts& chart, bool tree) {
    for (const domain::Account* root : chart.rootAccounts()) {
        if (tree) {
            printAccountTree(out, root, 0);
        } else {
            printAccountFlat(out, root);
        }
    }
}

void printAccountDetail(std::ostream& out, const domain::Account& account) {
    out << "code:   " << account.code().value() << "\n";
    out << "name:   " << account.name() << "\n";
    out << "type:   " << accountTypeToString(account.type()) << "\n";
    out << "id:     " << account.id().value() << "\n";
    out << "kind:   " << (account.isLeaf() ? "leaf" : "group") << (account.isRoot() ? ", root" : "") << "\n";
    if (account.parent() != nullptr) {
        out << "parent: " << account.parent()->code().value() << "\n";
    }
}

} // namespace ledgercore::cli
