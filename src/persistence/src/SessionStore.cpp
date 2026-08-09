#include "ledgercore/persistence/SessionStore.h"

#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ledgercore/domain/Account.h"
#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/JournalEntry.h"
#include "ledgercore/domain/JournalEntryLine.h"
#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/ComputedAccountName.h"
#include "ledgercore/ledger/PostedJournalEntry.h"
#include "ledgercore/persistence/PersistenceExceptions.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/posting/PostingExceptions.h"

namespace ledgercore::persistence {

namespace {

constexpr int kFormatVersion = 1;
constexpr char kHeaderPrefix[] = "LEDGERCORE-SNAPSHOT v";

// ---------------------------------------------------------------------
// Text encoding: escaping, tokenizing, account-type tokens, integers
// ---------------------------------------------------------------------

// Wraps text in a double-quoted, backslash-escaped field: '"' -> \", '\'
// -> \\, and an embedded newline -> \n (two characters), so a name,
// description, or formula source containing any of those still occupies
// exactly one line and round-trips exactly through readQuoted() below.
std::string escapeQuoted(const std::string& text) {
    std::string result;
    result.reserve(text.size() + 2);
    result.push_back('"');
    for (const char c : text) {
        switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            default:
                result.push_back(c);
        }
    }
    result.push_back('"');
    return result;
}

// An unquoted field (an AccountCode or a ComputedAccountName) has no
// escaping of its own -- the grammar relies on whitespace to separate
// fields -- so any value that could not survive that must be rejected at
// save time rather than silently producing an unparseable file.
bool isSafeUnquotedField(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    for (const char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0 || c == '"') {
            return false;
        }
    }
    return true;
}

std::string accountTypeToToken(domain::AccountType type) {
    switch (type) {
        case domain::AccountType::Asset:
            return "Asset";
        case domain::AccountType::Liability:
            return "Liability";
        case domain::AccountType::Equity:
            return "Equity";
        case domain::AccountType::Revenue:
            return "Revenue";
        case domain::AccountType::Expense:
            return "Expense";
    }
    throw PersistenceException("internal: unrecognized AccountType while saving");
}

domain::AccountType tokenToAccountType(const std::string& token, std::size_t lineNumber) {
    if (token == "Asset") {
        return domain::AccountType::Asset;
    }
    if (token == "Liability") {
        return domain::AccountType::Liability;
    }
    if (token == "Equity") {
        return domain::AccountType::Equity;
    }
    if (token == "Revenue") {
        return domain::AccountType::Revenue;
    }
    if (token == "Expense") {
        return domain::AccountType::Expense;
    }
    throw PersistenceFormatException("unknown AccountType token '" + token + "' at line "
                                      + std::to_string(lineNumber));
}

std::int64_t parseInt64Field(const std::string& text, const std::string& context, std::size_t lineNumber) {
    std::int64_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw PersistenceFormatException("malformed integer for " + context + " ('" + text + "') at line "
                                          + std::to_string(lineNumber));
    }
    return value;
}

// Splits one record line into whitespace-separated tokens, treating a
// double-quoted, backslash-escaped run (see escapeQuoted()) as a single
// token wherever it appears. Throws PersistenceFormatException for an
// unterminated quote or an unrecognized escape sequence -- both are
// structural corruption, not something a caller could recover from.
std::vector<std::string> tokenizeRecordLine(const std::string& line, std::size_t lineNumber) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    const std::size_t n = line.size();

    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i])) != 0) {
            ++i;
        }
        if (i >= n) {
            break;
        }

        if (line[i] == '"') {
            ++i;
            std::string value;
            bool closed = false;
            while (i < n) {
                const char c = line[i];
                if (c == '"') {
                    closed = true;
                    ++i;
                    break;
                }
                if (c == '\\') {
                    if (i + 1 >= n) {
                        throw PersistenceFormatException("unterminated escape sequence at line "
                                                           + std::to_string(lineNumber));
                    }
                    const char next = line[i + 1];
                    if (next == '"') {
                        value.push_back('"');
                    } else if (next == '\\') {
                        value.push_back('\\');
                    } else if (next == 'n') {
                        value.push_back('\n');
                    } else {
                        throw PersistenceFormatException(std::string("unsupported escape sequence '\\") + next
                                                           + "' at line " + std::to_string(lineNumber));
                    }
                    i += 2;
                    continue;
                }
                value.push_back(c);
                ++i;
            }
            if (!closed) {
                throw PersistenceFormatException("unterminated quoted field at line " + std::to_string(lineNumber));
            }
            tokens.push_back(std::move(value));
        } else {
            const std::size_t start = i;
            while (i < n && std::isspace(static_cast<unsigned char>(line[i])) == 0) {
                ++i;
            }
            tokens.push_back(line.substr(start, i - start));
        }
    }
    return tokens;
}

bool isBlank(const std::string& line) {
    for (const char c : line) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------
// Date encoding: full-precision, deterministic, not locale-dependent
// ---------------------------------------------------------------------

std::int64_t nanosSinceEpoch(std::chrono::system_clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
}

std::chrono::system_clock::time_point timePointFromNanos(std::int64_t nanos) {
    return std::chrono::system_clock::time_point{}
           + std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(nanos));
}

// ---------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------

void writeAccountsPreOrder(std::ostream& out, const domain::Account& account, const domain::Account* parent) {
    if (!isSafeUnquotedField(account.code().value())) {
        throw PersistenceException("AccountCode '" + account.code().value()
                                    + "' cannot be represented in this file format (contains whitespace or '\"')");
    }
    if (parent == nullptr) {
        out << "ACCOUNT ROOT " << account.code().value() << ' ' << accountTypeToToken(account.type()) << ' '
            << escapeQuoted(account.name()) << '\n';
    } else {
        out << "ACCOUNT CHILD " << parent->code().value() << ' ' << account.code().value() << ' '
            << escapeQuoted(account.name()) << '\n';
    }
    for (const domain::Account* child : account.children()) {
        writeAccountsPreOrder(out, *child, &account);
    }
}

void writeSnapshot(std::ostream& out, const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
                    const computed::ComputedAccountRegistry& computedAccounts) {
    out << kHeaderPrefix << kFormatVersion << '\n';
    out << "CURRENCY " << ledger.currency().code() << '\n';

    for (const domain::Account* root : chart.rootAccounts()) {
        writeAccountsPreOrder(out, *root, nullptr);
    }

    for (const ledger::PostedJournalEntry& posted : ledger.postedEntries()) {
        const domain::JournalEntry& entry = posted.entry();
        out << "ENTRY " << nanosSinceEpoch(entry.date()) << ' ' << escapeQuoted(entry.description()) << '\n';
        for (const domain::JournalEntryLine& line : entry.lines()) {
            const domain::Account* account = chart.findById(line.accountId());
            if (account == nullptr) {
                throw PersistenceException(
                    "cannot resolve a posted line's account while saving -- chart does not correspond to ledger");
            }
            if (!isSafeUnquotedField(account->code().value())) {
                throw PersistenceException("AccountCode '" + account->code().value()
                                            + "' cannot be represented in this file format");
            }
            out << (line.isDebit() ? "  DEBIT " : "  CREDIT ") << account->code().value() << ' '
                << line.amount().minorUnits() << '\n';
        }
    }

    for (const computed::ComputedAccountDefinition* definition : computedAccounts.definitions()) {
        if (!isSafeUnquotedField(definition->name().value())) {
            throw PersistenceException("computed account name '" + definition->name().value()
                                        + "' cannot be represented in this file format");
        }
        out << "COMPUTED " << definition->name().value() << ' ' << escapeQuoted(definition->formulaSource())
            << '\n';
    }
}

// ---------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------

std::vector<std::string> readAllLines(std::istream& in) {
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

void parseAccountRecord(const std::vector<std::string>& tokens, std::size_t lineNumber, domain::ChartOfAccounts& chart) {
    if (tokens.size() < 2) {
        throw PersistenceFormatException("malformed ACCOUNT record at line " + std::to_string(lineNumber));
    }
    const std::string& kind = tokens[1];
    if (kind == "ROOT") {
        if (tokens.size() != 5) {
            throw PersistenceFormatException("malformed ACCOUNT ROOT record at line " + std::to_string(lineNumber));
        }
        const domain::AccountType type = tokenToAccountType(tokens[3], lineNumber);
        chart.addRootAccount(domain::AccountCode(tokens[2]), tokens[4], type);
    } else if (kind == "CHILD") {
        if (tokens.size() != 5) {
            throw PersistenceFormatException("malformed ACCOUNT CHILD record at line " + std::to_string(lineNumber));
        }
        domain::Account* parent = chart.findByCode(domain::AccountCode(tokens[2]));
        if (parent == nullptr) {
            throw PersistenceFormatException("ACCOUNT CHILD references unknown parent AccountCode '" + tokens[2]
                                              + "' at line " + std::to_string(lineNumber));
        }
        chart.addChildAccount(*parent, domain::AccountCode(tokens[3]), tokens[4]);
    } else {
        throw PersistenceFormatException("unknown ACCOUNT kind '" + kind + "' at line " + std::to_string(lineNumber));
    }
}

// Consumes the DEBIT/CREDIT continuation lines belonging to one ENTRY
// record, advancing *index past them. A line whose first token is not
// exactly "DEBIT" or "CREDIT" (including a blank line, or a mistyped
// token) ends the entry's line collection without being consumed --
// the outer loop re-examines it and reports it as an unrecognized record
// if it is not itself a valid top-level record.
std::vector<domain::JournalEntryLine> parseEntryLines(const std::vector<std::string>& lines, std::size_t& index,
                                                        const domain::ChartOfAccounts& chart,
                                                        const domain::Currency& currency) {
    std::vector<domain::JournalEntryLine> result;
    while (index < lines.size()) {
        const std::string& rawLine = lines[index];
        if (isBlank(rawLine)) {
            break;
        }
        // Leading whitespace is the structural signal that this line
        // belongs to the current entry (see writeSnapshot(): DEBIT/CREDIT
        // lines are always written indented, top-level records never
        // are). An unindented line ends the entry's line collection
        // without being consumed, letting the outer loop reinterpret it
        // as the next top-level record. An *indented* line that isn't
        // exactly DEBIT/CREDIT is unambiguously a corrupted entry line,
        // not a plausible new top-level record -- reported here, rather
        // than silently treated as "not part of this entry" and left to
        // surface only indirectly as a too-few-lines domain exception.
        if (std::isspace(static_cast<unsigned char>(rawLine.front())) == 0) {
            break;
        }
        const std::size_t lineNumber = index + 1;
        std::vector<std::string> tokens = tokenizeRecordLine(rawLine, lineNumber);
        if (tokens.empty()) {
            break;
        }
        if (tokens[0] != "DEBIT" && tokens[0] != "CREDIT") {
            throw PersistenceFormatException("expected DEBIT or CREDIT at line " + std::to_string(lineNumber)
                                              + ", found '" + tokens[0] + "'");
        }
        if (tokens.size() != 3) {
            throw PersistenceFormatException("malformed " + tokens[0] + " record at line "
                                              + std::to_string(lineNumber));
        }
        const domain::AccountCode code(tokens[1]);
        const domain::Account* account = chart.findByCode(code);
        if (account == nullptr) {
            throw posting::AccountNotFoundException("No account found for AccountCode " + code.value());
        }
        const std::int64_t minorUnits = parseInt64Field(tokens[2], "money amount", lineNumber);
        const domain::Money amount = domain::Money::ofMinorUnits(minorUnits, currency);
        result.push_back(tokens[0] == "DEBIT" ? domain::JournalEntryLine::debit(account->id(), amount)
                                               : domain::JournalEntryLine::credit(account->id(), amount));
        ++index;
    }
    return result;
}

} // namespace

void save(const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
          const computed::ComputedAccountRegistry& computedAccounts, const std::filesystem::path& path) {
    const std::filesystem::path tempPath = path.string() + ".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            throw PersistenceException("cannot open temporary file for writing: " + tempPath.string());
        }

        try {
            writeSnapshot(out, chart, ledger, computedAccounts);
        } catch (...) {
            out.close();
            std::error_code removeError;
            std::filesystem::remove(tempPath, removeError);
            throw;
        }

        out.flush();
        if (!out) {
            out.close();
            std::error_code removeError;
            std::filesystem::remove(tempPath, removeError);
            throw PersistenceException("failed writing snapshot to temporary file: " + tempPath.string());
        }
        out.close();
    }

    std::error_code renameError;
    std::filesystem::rename(tempPath, path, renameError);
    if (renameError) {
        std::error_code removeError;
        std::filesystem::remove(tempPath, removeError);
        throw PersistenceException("failed to finalize snapshot at " + path.string() + ": " + renameError.message());
    }
}

LoadedSession load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw PersistenceException("cannot open file for reading: " + path.string());
    }
    const std::vector<std::string> lines = readAllLines(in);
    in.close();

    std::size_t index = 0;
    const auto skipBlank = [&]() {
        while (index < lines.size() && isBlank(lines[index])) {
            ++index;
        }
    };

    skipBlank();
    if (index >= lines.size()) {
        throw PersistenceFormatException("empty file: missing header");
    }
    const std::string& headerLine = lines[index];
    if (headerLine.rfind(kHeaderPrefix, 0) != 0) {
        throw PersistenceFormatException("missing or malformed header at line " + std::to_string(index + 1));
    }
    const std::string versionText = headerLine.substr(std::char_traits<char>::length(kHeaderPrefix));
    const std::int64_t version = parseInt64Field(versionText, "header version", index + 1);
    if (version != kFormatVersion) {
        throw PersistenceVersionException("unsupported snapshot version: " + versionText);
    }
    ++index;

    skipBlank();
    if (index >= lines.size()) {
        throw PersistenceFormatException("truncated file: missing CURRENCY record");
    }
    {
        const std::vector<std::string> currencyTokens = tokenizeRecordLine(lines[index], index + 1);
        if (currencyTokens.size() != 2 || currencyTokens[0] != "CURRENCY") {
            throw PersistenceFormatException("expected CURRENCY record at line " + std::to_string(index + 1));
        }
        ++index;

        const domain::Currency currency(currencyTokens[1]);

        auto chart = std::make_unique<domain::ChartOfAccounts>();
        auto ledgerPtr = std::make_unique<ledger::Ledger>(currency);
        auto registry = std::make_unique<computed::ComputedAccountRegistry>();

        while (true) {
            skipBlank();
            if (index >= lines.size()) {
                break;
            }
            const std::size_t lineNumber = index + 1;
            std::vector<std::string> tokens = tokenizeRecordLine(lines[index], lineNumber);
            ++index;
            if (tokens.empty()) {
                continue;
            }
            const std::string& recordType = tokens[0];

            if (recordType == "ACCOUNT") {
                parseAccountRecord(tokens, lineNumber, *chart);
            } else if (recordType == "ENTRY") {
                if (tokens.size() != 3) {
                    throw PersistenceFormatException("malformed ENTRY record at line " + std::to_string(lineNumber));
                }
                const std::int64_t nanos = parseInt64Field(tokens[1], "ENTRY date", lineNumber);
                const std::chrono::system_clock::time_point date = timePointFromNanos(nanos);
                const std::string& description = tokens[2];

                std::vector<domain::JournalEntryLine> entryLines = parseEntryLines(lines, index, *chart, currency);
                const domain::JournalEntry entry = domain::JournalEntry::create(date, description, std::move(entryLines));
                posting::post(entry, *chart, *ledgerPtr);
            } else if (recordType == "COMPUTED") {
                if (tokens.size() != 3) {
                    throw PersistenceFormatException("malformed COMPUTED record at line " + std::to_string(lineNumber));
                }
                registry->define(formula::ComputedAccountName(tokens[1]), tokens[2]);
            } else {
                throw PersistenceFormatException("unknown record type '" + recordType + "' at line "
                                                  + std::to_string(lineNumber));
            }
        }

        return LoadedSession{std::move(chart), std::move(ledgerPtr), std::move(registry)};
    }
}

} // namespace ledgercore::persistence
