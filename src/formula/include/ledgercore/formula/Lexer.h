#pragma once

#include <string>
#include <vector>

#include "ledgercore/formula/Token.h"

namespace ledgercore::formula {

// Tokenizes an entire formula string in one pass. Always returns a
// stream ending in a single EndOfInput token, so the parser never needs
// to special-case "ran out of tokens".
//
// Throws FormulaSyntaxException on any character or literal that cannot
// form a valid token, with the character offset of the failure.
std::vector<Token> tokenize(const std::string& source);

} // namespace ledgercore::formula
