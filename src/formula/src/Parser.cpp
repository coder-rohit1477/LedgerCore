#include "ledgercore/formula/Parser.h"

#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/formula/Lexer.h"

namespace ledgercore::formula {

namespace {

// Parses a run of decimal digits (exactly the shape the Lexer guarantees
// for a Number token's integer/fractional part) into an unsigned
// std::int64_t, with its own overflow-checked digit accumulation -- a
// formula could contain a literal far longer than 19 digits.
std::int64_t parseUnsignedDigits(const std::string& digits) {
    constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
    std::int64_t value = 0;
    for (char c : digits) {
        const std::int64_t digit = c - '0';
        if (value > (kInt64Max - digit) / 10) {
            throw FormulaEvaluationException("Numeric literal is out of representable range");
        }
        value = value * 10 + digit;
    }
    return value;
}

// Converts a Number token's raw text ("digits" or "digits.digits") into
// an exact Rational. Any overflow detected while doing so is reported as
// a FormulaSyntaxException at the literal's own offset, not the
// FormulaEvaluationException that Rational/parseUnsignedDigits raise
// internally -- an unrepresentably large literal is a property of the
// formula text itself, not something evaluation-time discovered.
Rational parseNumberLiteral(const Token& token) {
    try {
        const std::string& text = token.text;
        const std::size_t dot = text.find('.');
        if (dot == std::string::npos) {
            return Rational::ofInt(parseUnsignedDigits(text));
        }
        const std::string integerPart = text.substr(0, dot);
        const std::string fractionalPart = text.substr(dot + 1);
        const std::int64_t integerValue = parseUnsignedDigits(integerPart);
        const std::int64_t fractionalValue = parseUnsignedDigits(fractionalPart);
        return Rational::fromDecimalParts(integerValue, fractionalValue, static_cast<int>(fractionalPart.size()));
    } catch (const FormulaEvaluationException&) {
        throw FormulaSyntaxException("Numeric literal is out of representable range", token.offset);
    }
}

class ParserImpl {
public:
    explicit ParserImpl(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    AstNodePtr parseExpression() {
        AstNodePtr result = parseAdditive();
        expect(TokenType::EndOfInput, "Unexpected trailing input");
        return result;
    }

private:
    const Token& current() const { return tokens_[position_]; }

    const Token& advance() {
        const Token& token = tokens_[position_];
        if (token.type != TokenType::EndOfInput) {
            ++position_;
        }
        return token;
    }

    bool check(TokenType type) const { return current().type == type; }

    void expect(TokenType type, const std::string& message) const {
        if (!check(type)) {
            throw FormulaSyntaxException(message, current().offset);
        }
    }

    AstNodePtr parseAdditive() {
        AstNodePtr left = parseMultiplicative();
        while (check(TokenType::Plus) || check(TokenType::Minus)) {
            const BinaryOperator op = check(TokenType::Plus) ? BinaryOperator::Add : BinaryOperator::Subtract;
            advance();
            AstNodePtr right = parseMultiplicative();
            left = std::make_unique<AstNode>(BinaryExpression{op, std::move(left), std::move(right)});
        }
        return left;
    }

    AstNodePtr parseMultiplicative() {
        AstNodePtr left = parseUnary();
        while (check(TokenType::Star) || check(TokenType::Slash)) {
            const BinaryOperator op = check(TokenType::Star) ? BinaryOperator::Multiply : BinaryOperator::Divide;
            advance();
            AstNodePtr right = parseUnary();
            left = std::make_unique<AstNode>(BinaryExpression{op, std::move(left), std::move(right)});
        }
        return left;
    }

    AstNodePtr parseUnary() {
        if (check(TokenType::Plus) || check(TokenType::Minus)) {
            const UnaryOperator op = check(TokenType::Plus) ? UnaryOperator::Plus : UnaryOperator::Minus;
            advance();
            AstNodePtr operand = parseUnary();
            return std::make_unique<AstNode>(UnaryExpression{op, std::move(operand)});
        }
        return parsePrimary();
    }

    AstNodePtr parsePrimary() {
        const Token& token = current();
        if (token.type == TokenType::Number) {
            advance();
            return std::make_unique<AstNode>(Literal{parseNumberLiteral(token)});
        }
        if (token.type == TokenType::AccountReference) {
            advance();
            return std::make_unique<AstNode>(AccountReference{domain::AccountCode(token.text)});
        }
        if (token.type == TokenType::LeftParen) {
            advance();
            AstNodePtr inner = parseAdditive();
            expect(TokenType::RightParen, "Expected ')'");
            advance();
            return inner;
        }
        throw FormulaSyntaxException("Expected a number, account reference, or '('", token.offset);
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
};

} // namespace

AstNodePtr parse(const std::string& source) {
    ParserImpl parser(tokenize(source));
    return parser.parseExpression();
}

} // namespace ledgercore::formula
