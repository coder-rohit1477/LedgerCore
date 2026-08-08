#pragma once

#include <cstddef>
#include <string>

namespace ledgercore::formula {

enum class TokenType {
    Number,
    AccountReference,
    Plus,
    Minus,
    Star,
    Slash,
    LeftParen,
    RightParen,
    EndOfInput,
};

// One lexical unit of a formula string: its type, raw source text (the
// digits of a Number, or the code text after '#' for an
// AccountReference -- empty for everything else), and the 0-based
// character offset it started at.
struct Token {
    TokenType type;
    std::string text;
    std::size_t offset;
};

} // namespace ledgercore::formula
