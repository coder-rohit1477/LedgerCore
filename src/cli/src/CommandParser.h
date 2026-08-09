#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ledgercore::cli {

// Thrown for any CLI-level syntax problem: an unknown command/subcommand,
// an unknown flag, a missing required flag or value, mutually exclusive
// flags used together, or a malformed "code:amount" pair. Deliberately not
// a ledgercore::LedgerException -- it never represents an accounting-rule
// violation, only a problem with how the command itself was written, and
// the CLI's top-level error boundary maps the two to different exit codes.
class CliUsageError : public std::runtime_error {
public:
    explicit CliUsageError(const std::string& message) : std::runtime_error(message) {}
};

// The nine top-level verbs, plus their subcommands where they have more
// than one, plus the REPL/script "exit" pseudo-command.
enum class CommandKind {
    AccountCreateRoot,
    AccountCreateChild,
    AccountList,
    AccountShow,
    Post,
    TrialBalance,
    BalanceSheet,
    IncomeStatement,
    FormulaEval,
    ComputedDefine,
    ComputedList,
    ComputedEval,
    Save,
    Load,
    Exit
};

// One --debit/--credit line as written by the user: still plain text --
// InputParsing resolves accountCode/amountText into real domain types only
// once a command actually executes.
struct PostingLineArg {
    std::string accountCode;
    std::string amountText;
    bool isDebit = false;
};

// A syntactically-valid command, translated no further than "which field
// does this flag's text belong to." Every value here is exactly the text
// the user typed (or empty/false if the corresponding flag was not given);
// CommandParser never inspects whether a value is a valid AccountCode, a
// valid amount, or a valid date -- that translation, and any accounting
// invariant, belongs entirely to InputParsing and the domain APIs it
// forwards to.
struct ParsedCommand {
    CommandKind kind = CommandKind::Exit;

    // account create-root / create-child / show
    std::string code;
    std::string name;
    std::string type;        // create-root only
    std::string parentCode;  // create-child only
    bool tree = false;       // account list only

    // post
    std::string date;
    std::string description;
    std::vector<PostingLineArg> lines;

    // trial-balance / balance-sheet / income-statement
    std::string asOf;
    std::string from;
    std::string to;

    // formula eval / computed define / computed eval
    std::string expression;
    std::string computedName;

    // save / load
    std::string path;
};

// Splits one input line into tokens on whitespace, with basic
// double-quote support (e.g. --description "Sold widgets") so a value may
// contain spaces. No escape-sequence support -- not needed for this
// grammar's fixed, non-adversarial vocabulary.
std::vector<std::string> tokenizeLine(const std::string& line);

// Parses one already-trimmed input line into a ParsedCommand. Returns
// std::nullopt for a blank line or a '#'-prefixed comment (both silently
// skipped by the caller -- primarily useful in script files). Throws
// CliUsageError for any syntax problem. Performs no accounting validation
// whatsoever.
std::optional<ParsedCommand> parseLine(const std::string& line);

} // namespace ledgercore::cli
