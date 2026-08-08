#include "ledgercore/formula/Lexer.h"

#include <cctype>

#include "ledgercore/formula/FormulaExceptions.h"

namespace ledgercore::formula {

namespace {

bool isAccountCodeChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '-';
}

bool isDigitChar(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

} // namespace

std::vector<Token> tokenize(const std::string& source) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    const std::size_t n = source.size();

    while (i < n) {
        const char c = source[i];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++i;
            continue;
        }

        const std::size_t start = i;

        if (c == '+') {
            tokens.push_back(Token{TokenType::Plus, "+", start});
            ++i;
            continue;
        }
        if (c == '-') {
            tokens.push_back(Token{TokenType::Minus, "-", start});
            ++i;
            continue;
        }
        if (c == '*') {
            tokens.push_back(Token{TokenType::Star, "*", start});
            ++i;
            continue;
        }
        if (c == '/') {
            tokens.push_back(Token{TokenType::Slash, "/", start});
            ++i;
            continue;
        }
        if (c == '(') {
            tokens.push_back(Token{TokenType::LeftParen, "(", start});
            ++i;
            continue;
        }
        if (c == ')') {
            tokens.push_back(Token{TokenType::RightParen, ")", start});
            ++i;
            continue;
        }

        if (c == '#') {
            ++i;
            const std::size_t codeStart = i;
            while (i < n && isAccountCodeChar(source[i])) {
                ++i;
            }
            if (i == codeStart) {
                throw FormulaSyntaxException("Expected account code after '#'", start);
            }
            tokens.push_back(Token{TokenType::AccountReference, source.substr(codeStart, i - codeStart), start});
            continue;
        }

        if (isDigitChar(c)) {
            while (i < n && isDigitChar(source[i])) {
                ++i;
            }
            if (i < n && source[i] == '.') {
                const std::size_t dotPos = i;
                ++i;
                const std::size_t fractionStart = i;
                while (i < n && isDigitChar(source[i])) {
                    ++i;
                }
                if (i == fractionStart) {
                    throw FormulaSyntaxException("Expected digit after '.' in numeric literal", dotPos);
                }
            }
            tokens.push_back(Token{TokenType::Number, source.substr(start, i - start), start});
            continue;
        }

        throw FormulaSyntaxException(std::string("Unexpected character '") + c + "'", start);
    }

    tokens.push_back(Token{TokenType::EndOfInput, "", n});
    return tokens;
}

} // namespace ledgercore::formula
