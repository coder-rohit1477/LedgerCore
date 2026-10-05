// libFuzzer target: arbitrary text as a ledgercore script, run line by line
// through the CLI's real command layer (cli::runOneLine) on a fresh
// in-memory session, as the REPL would.
//
// `save` and `load` lines are skipped so the target never touches the
// filesystem (the snapshot format has its own target); nothing else is
// filtered, and no shell is involved.
//
// Contract checked for every input: runOneLine() handles every failure
// itself (an exception escaping it would be the CLI's "internal error",
// exit code 3, and is reported as a crash), and the transcript of output
// and errors is deterministic.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>

#include "CommandExecution.h"
#include "CommandParser.h"
#include "LedgerSession.h"

#include "ledgercore/domain/Currency.h"

namespace {

using namespace ledgercore::cli;

bool touchesFilesystem(const std::string& line) {
    try {
        const std::optional<ParsedCommand> parsed = parseLine(line);
        return parsed && (parsed->kind == CommandKind::Save || parsed->kind == CommandKind::Load);
    } catch (const CliUsageError&) {
        return false;  // runOneLine reports the usage error itself
    }
}

std::string runScript(const std::string& script) {
    LedgerSession session(ledgercore::domain::Currency("USD"));
    std::ostringstream transcript;
    std::istringstream lines(script);
    std::string line;
    while (std::getline(lines, line)) {
        if (touchesFilesystem(line)) {
            continue;
        }
        const std::optional<int> stop = runOneLine(line, session, transcript, transcript, /*exitOnFailure=*/false);
        if (stop.has_value()) {
            transcript << "[exit " << *stop << "]\n";
            break;
        }
    }
    return transcript.str();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string script(reinterpret_cast<const char*>(data), size);
    if (runScript(script) != runScript(script)) {
        std::fprintf(stderr, "cli fuzzer: running the same script twice gave different transcripts\n");
        std::abort();
    }
    return 0;
}
