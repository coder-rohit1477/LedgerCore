#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "LedgerSession.h"

namespace ledgercore::cli {

// Runs one input line against session. Returns an exit code the whole
// program should stop with (the "exit"/"quit" pseudo-command always
// returns 0; an error only produces a code when exitOnFailure is true --
// i.e. script mode). Returns std::nullopt to keep going: either the
// command succeeded, the line was blank/a comment, or -- in REPL mode
// only -- the command failed but the session continues, per the approved
// design ("A LedgerException from one command must NOT terminate the
// entire session").
//
// Used by main() for both input modes; separate from it so the command
// layer can also be driven directly (tests, fuzz targets).
std::optional<int> runOneLine(const std::string& line, LedgerSession& session, std::ostream& out, std::ostream& err,
                               bool exitOnFailure);

} // namespace ledgercore::cli
