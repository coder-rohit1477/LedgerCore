#include "CommandParser.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace ledgercore::cli {

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool isFlagToken(const std::string& token) {
    return token.size() > 2 && token[0] == '-' && token[1] == '-';
}

// Tokens past the verb/subcommand, split into positional arguments and
// --flag values. A flag may repeat (e.g. --debit); every occurrence is
// collected in encounter order.
struct RawArgs {
    std::vector<std::string> positionals;
    std::map<std::string, std::vector<std::string>> flags;
};

// booleanFlags names flags that take no value (e.g. --tree): their mere
// presence is recorded (as a "true" placeholder value) without consuming
// the following token.
RawArgs splitRawArgs(const std::vector<std::string>& tokens, std::size_t startIndex,
                      const std::vector<std::string>& booleanFlags = {}) {
    RawArgs result;
    std::size_t i = startIndex;
    while (i < tokens.size()) {
        const std::string& token = tokens[i];
        if (isFlagToken(token)) {
            std::string flagName = token.substr(2);
            const bool isBoolean =
                std::find(booleanFlags.begin(), booleanFlags.end(), flagName) != booleanFlags.end();
            if (isBoolean) {
                result.flags[flagName].push_back("true");
                ++i;
                continue;
            }
            if (i + 1 >= tokens.size()) {
                throw CliUsageError("flag --" + flagName + " requires a value");
            }
            result.flags[flagName].push_back(tokens[i + 1]);
            i += 2;
        } else {
            result.positionals.push_back(token);
            ++i;
        }
    }
    return result;
}

void rejectUnknownFlags(const RawArgs& args, const std::vector<std::string>& allowed) {
    for (const auto& entry : args.flags) {
        if (std::find(allowed.begin(), allowed.end(), entry.first) == allowed.end()) {
            throw CliUsageError("unknown flag: --" + entry.first);
        }
    }
}

void rejectPositionals(const RawArgs& args) {
    if (!args.positionals.empty()) {
        throw CliUsageError("unexpected argument: '" + args.positionals.front() + "'");
    }
}

std::string requireSingle(const RawArgs& args, const std::string& flagName) {
    auto it = args.flags.find(flagName);
    if (it == args.flags.end() || it->second.empty()) {
        throw CliUsageError("missing required flag: --" + flagName);
    }
    if (it->second.size() > 1) {
        throw CliUsageError("flag --" + flagName + " may only be given once");
    }
    return it->second.front();
}

bool hasFlag(const RawArgs& args, const std::string& flagName) {
    return args.flags.find(flagName) != args.flags.end();
}

PostingLineArg parsePostingLine(const std::string& text, bool isDebit) {
    const std::size_t colon = text.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) {
        throw CliUsageError("malformed debit/credit argument: '" + text + "' (expected code:amount)");
    }
    PostingLineArg line;
    line.accountCode = text.substr(0, colon);
    line.amountText = text.substr(colon + 1);
    line.isDebit = isDebit;
    return line;
}

ParsedCommand parseAccount(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        throw CliUsageError("'account' requires a subcommand: create-root|create-child|list|show");
    }
    const std::string& sub = tokens[1];

    if (sub == "create-root") {
        RawArgs args = splitRawArgs(tokens, 2);
        rejectUnknownFlags(args, {"code", "name", "type"});
        rejectPositionals(args);
        ParsedCommand pc;
        pc.kind = CommandKind::AccountCreateRoot;
        pc.code = requireSingle(args, "code");
        pc.name = requireSingle(args, "name");
        pc.type = requireSingle(args, "type");
        return pc;
    }
    if (sub == "create-child") {
        RawArgs args = splitRawArgs(tokens, 2);
        rejectUnknownFlags(args, {"parent", "code", "name"});
        rejectPositionals(args);
        ParsedCommand pc;
        pc.kind = CommandKind::AccountCreateChild;
        pc.parentCode = requireSingle(args, "parent");
        pc.code = requireSingle(args, "code");
        pc.name = requireSingle(args, "name");
        return pc;
    }
    if (sub == "list") {
        RawArgs args = splitRawArgs(tokens, 2, {"tree"});
        rejectUnknownFlags(args, {"tree"});
        rejectPositionals(args);
        ParsedCommand pc;
        pc.kind = CommandKind::AccountList;
        pc.tree = hasFlag(args, "tree");
        return pc;
    }
    if (sub == "show") {
        RawArgs args = splitRawArgs(tokens, 2);
        rejectUnknownFlags(args, {});
        if (args.positionals.size() != 1) {
            throw CliUsageError("'account show' requires exactly one account code");
        }
        ParsedCommand pc;
        pc.kind = CommandKind::AccountShow;
        pc.code = args.positionals.front();
        return pc;
    }
    throw CliUsageError("unknown 'account' subcommand: '" + sub + "'");
}

ParsedCommand parsePost(const std::vector<std::string>& tokens) {
    RawArgs args = splitRawArgs(tokens, 1);
    rejectUnknownFlags(args, {"date", "description", "debit", "credit"});
    rejectPositionals(args);

    ParsedCommand pc;
    pc.kind = CommandKind::Post;
    pc.date = requireSingle(args, "date");
    pc.description = requireSingle(args, "description");

    auto debitIt = args.flags.find("debit");
    if (debitIt != args.flags.end()) {
        for (const std::string& value : debitIt->second) {
            pc.lines.push_back(parsePostingLine(value, /*isDebit=*/true));
        }
    }
    auto creditIt = args.flags.find("credit");
    if (creditIt != args.flags.end()) {
        for (const std::string& value : creditIt->second) {
            pc.lines.push_back(parsePostingLine(value, /*isDebit=*/false));
        }
    }
    return pc;
}

ParsedCommand parseTrialBalance(const std::vector<std::string>& tokens) {
    RawArgs args = splitRawArgs(tokens, 1);
    rejectUnknownFlags(args, {"as-of", "from", "to"});
    rejectPositionals(args);

    const bool hasAsOf = hasFlag(args, "as-of");
    const bool hasFrom = hasFlag(args, "from");
    const bool hasTo = hasFlag(args, "to");
    if (hasAsOf && (hasFrom || hasTo)) {
        throw CliUsageError("--as-of cannot be combined with --from/--to");
    }
    if (hasFrom != hasTo) {
        throw CliUsageError("--from and --to must be given together");
    }

    ParsedCommand pc;
    pc.kind = CommandKind::TrialBalance;
    if (hasAsOf) {
        pc.asOf = requireSingle(args, "as-of");
    }
    if (hasFrom) {
        pc.from = requireSingle(args, "from");
        pc.to = requireSingle(args, "to");
    }
    return pc;
}

ParsedCommand parseBalanceSheet(const std::vector<std::string>& tokens) {
    RawArgs args = splitRawArgs(tokens, 1);
    rejectUnknownFlags(args, {"as-of"});
    rejectPositionals(args);

    ParsedCommand pc;
    pc.kind = CommandKind::BalanceSheet;
    if (hasFlag(args, "as-of")) {
        pc.asOf = requireSingle(args, "as-of");
    }
    return pc;
}

ParsedCommand parseIncomeStatement(const std::vector<std::string>& tokens) {
    RawArgs args = splitRawArgs(tokens, 1);
    rejectUnknownFlags(args, {"from", "to"});
    rejectPositionals(args);

    const bool hasFrom = hasFlag(args, "from");
    const bool hasTo = hasFlag(args, "to");
    if (hasFrom != hasTo) {
        throw CliUsageError("--from and --to must be given together");
    }

    ParsedCommand pc;
    pc.kind = CommandKind::IncomeStatement;
    if (hasFrom) {
        pc.from = requireSingle(args, "from");
        pc.to = requireSingle(args, "to");
    }
    return pc;
}

ParsedCommand parseFormula(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2 || tokens[1] != "eval") {
        throw CliUsageError("'formula' requires subcommand: eval <expression>");
    }
    if (tokens.size() < 3) {
        throw CliUsageError("'formula eval' requires an expression");
    }
    std::string expression = tokens[2];
    for (std::size_t i = 3; i < tokens.size(); ++i) {
        expression += " " + tokens[i];
    }
    ParsedCommand pc;
    pc.kind = CommandKind::FormulaEval;
    pc.expression = expression;
    return pc;
}

ParsedCommand parseComputed(const std::vector<std::string>& tokens) {
    if (tokens.size() < 2) {
        throw CliUsageError("'computed' requires a subcommand: define|list|eval");
    }
    const std::string& sub = tokens[1];

    if (sub == "define") {
        RawArgs args = splitRawArgs(tokens, 2);
        rejectUnknownFlags(args, {"name", "formula"});
        rejectPositionals(args);
        ParsedCommand pc;
        pc.kind = CommandKind::ComputedDefine;
        pc.computedName = requireSingle(args, "name");
        pc.expression = requireSingle(args, "formula");
        return pc;
    }
    if (sub == "list") {
        RawArgs args = splitRawArgs(tokens, 2);
        rejectUnknownFlags(args, {});
        rejectPositionals(args);
        ParsedCommand pc;
        pc.kind = CommandKind::ComputedList;
        return pc;
    }
    if (sub == "eval") {
        if (tokens.size() != 3) {
            throw CliUsageError("'computed eval' requires exactly one computed-account name");
        }
        ParsedCommand pc;
        pc.kind = CommandKind::ComputedEval;
        pc.computedName = tokens[2];
        return pc;
    }
    throw CliUsageError("unknown 'computed' subcommand: '" + sub + "'");
}

ParsedCommand parseSave(const std::vector<std::string>& tokens) {
    RawArgs args = splitRawArgs(tokens, 1);
    rejectUnknownFlags(args, {});
    if (args.positionals.size() != 1) {
        throw CliUsageError("'save' requires exactly one path argument");
    }
    ParsedCommand pc;
    pc.kind = CommandKind::Save;
    pc.path = args.positionals.front();
    return pc;
}

ParsedCommand parseLoad(const std::vector<std::string>& tokens) {
    RawArgs args = splitRawArgs(tokens, 1);
    rejectUnknownFlags(args, {});
    if (args.positionals.size() != 1) {
        throw CliUsageError("'load' requires exactly one path argument");
    }
    ParsedCommand pc;
    pc.kind = CommandKind::Load;
    pc.path = args.positionals.front();
    return pc;
}

} // namespace

std::vector<std::string> tokenizeLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }

        if (line[i] == '"') {
            ++i;
            const std::size_t start = i;
            while (i < line.size() && line[i] != '"') {
                ++i;
            }
            tokens.push_back(line.substr(start, i - start));
            if (i < line.size()) {
                ++i;
            }
        } else {
            const std::size_t start = i;
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) == 0) {
                ++i;
            }
            tokens.push_back(line.substr(start, i - start));
        }
    }
    return tokens;
}

std::optional<ParsedCommand> parseLine(const std::string& line) {
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#') {
        return std::nullopt;
    }

    const std::vector<std::string> tokens = tokenizeLine(trimmed);
    if (tokens.empty()) {
        return std::nullopt;
    }

    const std::string& verb = tokens[0];
    if (verb == "exit" || verb == "quit") {
        ParsedCommand pc;
        pc.kind = CommandKind::Exit;
        return pc;
    }
    if (verb == "account") {
        return parseAccount(tokens);
    }
    if (verb == "post") {
        return parsePost(tokens);
    }
    if (verb == "trial-balance") {
        return parseTrialBalance(tokens);
    }
    if (verb == "balance-sheet") {
        return parseBalanceSheet(tokens);
    }
    if (verb == "income-statement") {
        return parseIncomeStatement(tokens);
    }
    if (verb == "formula") {
        return parseFormula(tokens);
    }
    if (verb == "computed") {
        return parseComputed(tokens);
    }
    if (verb == "save") {
        return parseSave(tokens);
    }
    if (verb == "load") {
        return parseLoad(tokens);
    }

    throw CliUsageError("unknown command: '" + verb + "'");
}

} // namespace ledgercore::cli
