#pragma once

#include <string>

#include "ledgercore/formula/Ast.h"

namespace ledgercore::formula {

// Parses an entire formula string into an AST via recursive descent:
//
//   expression      -> additive
//   additive        -> multiplicative (("+" | "-") multiplicative)*
//   multiplicative  -> unary (("*" | "/") unary)*
//   unary           -> ("+" | "-") unary | primary
//   primary         -> number | account_reference | "(" expression ")"
//
// Throws FormulaSyntaxException (via tokenize(), or directly on grammar
// errors: unexpected token, missing ')', trailing input after a valid
// expression, or unexpected end of input) -- always with a character
// offset.
AstNodePtr parse(const std::string& source);

} // namespace ledgercore::formula
