#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>

#include "CommandParser.h"
#include "InputParsing.h"
#include "LedgerSession.h"
#include "OutputFormatting.h"

#include "ledgercore/computed/LedgerAccountResolver.h"
#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Period.h"
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/formula/Evaluator.h"
#include "ledgercore/formula/Parser.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/persistence/PersistenceExceptions.h"
#include "ledgercore/persistence/SessionStore.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/posting/PostingExceptions.h"
#include "ledgercore/reporting/BalanceSheet.h"
#include "ledgercore/reporting/IncomeStatement.h"
#include "ledgercore/trialbalance/TrialBalance.h"

namespace ledgercore::cli {
namespace {

// Resolves a user-typed account code to the Account it names, or throws
// posting::AccountNotFoundException -- the same exception posting::post()
// itself would throw for an unresolvable AccountId, reused here because a
// debit/credit line can only be constructed with an AccountId (see
// JournalEntryLine::debit()/credit()), so this resolution has to happen in
// CLI code before posting can even be attempted, but it represents exactly
// the same accounting-level fact.
const domain::Account& resolvePostingAccount(const LedgerSession& session, const std::string& codeText) {
    const domain::AccountCode code = parseAccountCode(codeText);
    const domain::Account* account = session.chart().findByCode(code);
    if (account == nullptr) {
        throw posting::AccountNotFoundException("No account found for AccountCode " + code.value());
    }
    return *account;
}

void executeAccountCreateRoot(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const domain::AccountType type = parseAccountType(pc.type);
    const domain::AccountCode code = parseAccountCode(pc.code);
    const domain::Account& account = session.chart().addRootAccount(code, pc.name, type);
    out << "created root account " << account.code().value() << " \"" << account.name() << "\"\n";
}

void executeAccountCreateChild(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const domain::AccountCode parentCode = parseAccountCode(pc.parentCode);
    domain::Account* parent = session.chart().findByCode(parentCode);
    if (parent == nullptr) {
        throw CliUsageError("no such parent account: '" + pc.parentCode + "'");
    }
    const domain::AccountCode code = parseAccountCode(pc.code);
    const domain::Account& account = session.chart().addChildAccount(*parent, code, pc.name);
    out << "created child account " << account.code().value() << " \"" << account.name() << "\" under "
        << parent->code().value() << "\n";
}

void executeAccountList(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    printAccountList(out, session.chart(), pc.tree);
}

void executeAccountShow(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const domain::AccountCode code = parseAccountCode(pc.code);
    const domain::Account* account = session.chart().findByCode(code);
    if (account == nullptr) {
        throw CliUsageError("no such account: '" + pc.code + "'");
    }
    printAccountDetail(out, *account);
}

void executePost(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const std::chrono::system_clock::time_point date = parseDate(pc.date);

    std::vector<domain::JournalEntryLine> lines;
    lines.reserve(pc.lines.size());
    for (const PostingLineArg& arg : pc.lines) {
        const domain::Account& account = resolvePostingAccount(session, arg.accountCode);
        const domain::Money amount = parseAmount(arg.amountText, session.currency());
        lines.push_back(arg.isDebit ? domain::JournalEntryLine::debit(account.id(), amount)
                                     : domain::JournalEntryLine::credit(account.id(), amount));
    }

    const domain::JournalEntry entry = domain::JournalEntry::create(date, pc.description, std::move(lines));
    const ledger::PostingId id = posting::post(entry, session.chart(), session.ledger());
    out << "posted entry #" << id.value() << "\n";
}

trialbalance::TrialBalance buildTrialBalance(const ParsedCommand& pc, LedgerSession& session) {
    if (!pc.asOf.empty()) {
        return trialbalance::TrialBalance::generateAsOf(session.chart(), session.ledger(), parseDate(pc.asOf));
    }
    if (!pc.from.empty()) {
        const domain::Period period(parseDate(pc.from), parseDate(pc.to));
        return trialbalance::TrialBalance::generateForPeriod(session.chart(), session.ledger(), period);
    }
    return trialbalance::TrialBalance::generate(session.chart(), session.ledger());
}

void executeTrialBalance(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    printTrialBalance(out, buildTrialBalance(pc, session));
}

void executeBalanceSheet(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const trialbalance::TrialBalance tb = pc.asOf.empty()
                                               ? trialbalance::TrialBalance::generate(session.chart(), session.ledger())
                                               : trialbalance::TrialBalance::generateAsOf(
                                                     session.chart(), session.ledger(), parseDate(pc.asOf));
    printBalanceSheet(out, reporting::BalanceSheet::generate(tb));
}

trialbalance::TrialBalance buildIncomeStatementTrialBalance(const ParsedCommand& pc, LedgerSession& session) {
    if (!pc.from.empty()) {
        const domain::Period period(parseDate(pc.from), parseDate(pc.to));
        return trialbalance::TrialBalance::generateForPeriod(session.chart(), session.ledger(), period);
    }
    return trialbalance::TrialBalance::generate(session.chart(), session.ledger());
}

void executeIncomeStatement(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    printIncomeStatement(out, reporting::IncomeStatement::generate(buildIncomeStatementTrialBalance(pc, session)));
}

void executeFormulaEval(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const formula::AstNodePtr ast = formula::parse(pc.expression);
    const computed::LedgerAccountResolver resolver(session.chart(), session.ledger());
    const formula::FormulaValue result = formula::evaluate(*ast, resolver);
    if (result.isMoney()) {
        out << result.asMoney().toString() << "\n";
    } else {
        out << result.asScalar().toString() << "\n";
    }
}

void executeComputedDefine(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    session.computedAccounts().define(formula::ComputedAccountName(pc.computedName), pc.expression);
    out << "defined computed account " << pc.computedName << "\n";
}

void executeComputedList(LedgerSession& session, std::ostream& out) {
    for (const computed::ComputedAccountDefinition* definition : session.computedAccounts().definitions()) {
        out << definition->name().value() << " = " << definition->formulaSource() << "\n";
    }
}

void executeComputedEval(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    const computed::LedgerAccountResolver resolver(session.chart(), session.ledger());
    const domain::Money result =
        session.computedAccounts().evaluate(formula::ComputedAccountName(pc.computedName), resolver);
    out << result.toString() << "\n";
}

void executeSave(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    persistence::save(session.chart(), session.ledger(), session.computedAccounts(), pc.path);
    out << "Saved LedgerCore session to \"" << pc.path << "\".\n";
}

void executeLoad(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    // persistence::load() does every fallible step (parsing, replaying
    // through posting::post()) before it ever returns -- if it throws,
    // execution never reaches replaceState(), and the session is left
    // completely untouched. Only a successful return here can possibly
    // reach the unconditionally-noexcept swap.
    persistence::LoadedSession loaded = persistence::load(pc.path);
    session.replaceState(std::move(loaded));
    out << "Loaded LedgerCore session from \"" << pc.path << "\".\n";
}

void execute(const ParsedCommand& pc, LedgerSession& session, std::ostream& out) {
    switch (pc.kind) {
        case CommandKind::AccountCreateRoot:
            executeAccountCreateRoot(pc, session, out);
            return;
        case CommandKind::AccountCreateChild:
            executeAccountCreateChild(pc, session, out);
            return;
        case CommandKind::AccountList:
            executeAccountList(pc, session, out);
            return;
        case CommandKind::AccountShow:
            executeAccountShow(pc, session, out);
            return;
        case CommandKind::Post:
            executePost(pc, session, out);
            return;
        case CommandKind::TrialBalance:
            executeTrialBalance(pc, session, out);
            return;
        case CommandKind::BalanceSheet:
            executeBalanceSheet(pc, session, out);
            return;
        case CommandKind::IncomeStatement:
            executeIncomeStatement(pc, session, out);
            return;
        case CommandKind::FormulaEval:
            executeFormulaEval(pc, session, out);
            return;
        case CommandKind::ComputedDefine:
            executeComputedDefine(pc, session, out);
            return;
        case CommandKind::ComputedList:
            executeComputedList(session, out);
            return;
        case CommandKind::ComputedEval:
            executeComputedEval(pc, session, out);
            return;
        case CommandKind::Save:
            executeSave(pc, session, out);
            return;
        case CommandKind::Load:
            executeLoad(pc, session, out);
            return;
        case CommandKind::Exit:
            return;
    }
}

// Runs one input line against session. Returns an exit code the whole
// program should stop with (the "exit"/"quit" pseudo-command always
// returns 0; an error only produces a code when exitOnFailure is true --
// i.e. script mode). Returns std::nullopt to keep going: either the
// command succeeded, the line was blank/a comment, or -- in REPL mode
// only -- the command failed but the session continues, per the approved
// design ("A LedgerException from one command must NOT terminate the
// entire session").
std::optional<int> runOneLine(const std::string& line, LedgerSession& session, std::ostream& out, std::ostream& err,
                               bool exitOnFailure) {
    std::optional<ParsedCommand> parsed;
    try {
        parsed = parseLine(line);
    } catch (const CliUsageError& e) {
        err << "error: " << e.what() << "\n";
        return exitOnFailure ? std::optional<int>(1) : std::nullopt;
    }

    if (!parsed) {
        return std::nullopt;
    }
    if (parsed->kind == CommandKind::Exit) {
        return 0;
    }

    try {
        execute(*parsed, session, out);
    } catch (const CliUsageError& e) {
        err << "error: " << e.what() << "\n";
        return exitOnFailure ? std::optional<int>(1) : std::nullopt;
    } catch (const persistence::PersistenceException& e) {
        // A snapshot problem (unreadable file, malformed record, unsupported
        // version) is an infrastructure/usage-level failure, not an
        // accounting-rule violation -- persistence::PersistenceException is
        // deliberately not a ledgercore::LedgerException (see
        // PersistenceExceptions.h), so it needs its own clause here rather
        // than falling into the LedgerException handler below, and is
        // mapped to the same exit code as CliUsageError.
        err << "error: " << e.what() << "\n";
        return exitOnFailure ? std::optional<int>(1) : std::nullopt;
    } catch (const ledgercore::LedgerException& e) {
        err << "error: " << e.what() << "\n";
        return exitOnFailure ? std::optional<int>(2) : std::nullopt;
    }
    return std::nullopt;
}

int runRepl(LedgerSession& session) {
    std::cout << "LedgerCore CLI -- in-memory session; state is lost when this process exits unless you 'save' it "
                  "first.\n";
    std::cout << "Commands: account, post, trial-balance, balance-sheet, income-statement, formula, computed, "
                  "save, load.\n";
    std::cout << "Type 'exit' or 'quit' to leave, or send EOF (Ctrl-D).\n";

    std::string line;
    while (true) {
        std::cout << "ledgercore> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            return 0;
        }
        const std::optional<int> stop = runOneLine(line, session, std::cout, std::cerr, /*exitOnFailure=*/false);
        if (stop.has_value()) {
            return *stop;
        }
    }
}

int runScript(const std::string& path, LedgerSession& session) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "error: cannot open script file: " << path << "\n";
        return 1;
    }

    std::string line;
    while (std::getline(in, line)) {
        const std::optional<int> stop = runOneLine(line, session, std::cout, std::cerr, /*exitOnFailure=*/true);
        if (stop.has_value()) {
            return *stop;
        }
    }
    return 0;
}

} // namespace
} // namespace ledgercore::cli

int main(int argc, char** argv) {
    try {
        ledgercore::cli::LedgerSession session(ledgercore::domain::Currency("USD"));

        if (argc == 1) {
            return ledgercore::cli::runRepl(session);
        }
        if (argc == 3 && std::string(argv[1]) == "--script") {
            return ledgercore::cli::runScript(argv[2], session);
        }

        std::cerr << "usage: ledgercore [--script <path>]\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
        return 3;
    } catch (...) {
        std::cerr << "internal error: unknown exception\n";
        return 3;
    }
}
