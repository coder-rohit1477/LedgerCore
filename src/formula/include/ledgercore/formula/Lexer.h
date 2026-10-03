#pragma once

#include <string>
#include <vector>

#include "ledgercore/formula/Token.h"

namespace ledgercore::formula {

// Tokenizes an entire formula string in one pass. Always returns a
// stream ending in a single EndOfInput token, so the parser never needs
// to special-case "ran out of tokens".
//
// Reference grammar (deliberate, and locked down by LexerTest):
//
//   account-reference  := '#' code-char+
//   computed-reference := '@' code-char+
//   code-char          := ASCII letter | digit | '.' | '_' | '-'
//
// A reference is maximal munch: it extends over every following
// code-char. '-' and '.' are code-chars on purpose -- AccountCode accepts
// any non-empty text, and codes such as "1000-01" or "1000.10" are
// ordinary chart-numbering styles a formula must be able to name. The
// consequence is that subtraction directly after a reference must be
// separated from it:
//
//   #1000-1      one reference, to account code "1000-1"
//   #1000 - 1    #1000 minus the literal 1
//   (#1000)-1    #1000 minus the literal 1
//   #1000-#2000  syntax error: reference "1000-" followed by #2000
//   #1000 -#2000 #1000 minus #2000
//   #1000+#2000  #1000 plus #2000 ('+' is never a code-char)
//
// '-' as the *first* character after '#'/'@' is likewise part of the
// code, so "#-1" names account "-1".
//
// Throws FormulaSyntaxException on any character or literal that cannot
// form a valid token, with the character offset of the failure.
std::vector<Token> tokenize(const std::string& source);

} // namespace ledgercore::formula
