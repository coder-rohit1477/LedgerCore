#pragma once

#include <cstddef>
#include <string>

#include "ledgercore/formula/Ast.h"

namespace ledgercore::formula {

// The deepest formula parse() accepts. Two things are bounded by it: the
// syntax tree's depth (a leaf is 1; each binary or unary operator adds a
// level, so a + b + c is 3 deep) and the nesting of parenthesized groups
// and unary operators while parsing (((a)) nests 2 deep). Formulas people
// actually write are a few levels deep; the bound keeps parsing,
// evaluation, and destruction -- which recurse once per level -- within a
// fixed stack budget even for adversarial input such as a hand-made
// snapshot. Exceeding it throws FormulaSyntaxException.
inline constexpr std::size_t kMaxFormulaDepth = 128;

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

// The depth of root's syntax tree (1 for a single literal or reference;
// each unary or binary operator adds a level) -- the same measure parse()
// bounds by kMaxFormulaDepth. Computed iteratively, so it is safe for any
// tree shape.
std::size_t syntaxTreeDepth(const AstNode& root);

} // namespace ledgercore::formula
