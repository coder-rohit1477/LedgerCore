#include "ledgercore/formula/Parser.h"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <variant>
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
        std::size_t depth = 0;
        AstNodePtr result = parseAdditive(depth);
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

    void ensureDepth(std::size_t depth, std::size_t offset) const {
        if (depth > kMaxFormulaDepth) {
            throw FormulaSyntaxException(
                "Formula is nested more than " + std::to_string(kMaxFormulaDepth) + " levels deep", offset);
        }
    }

    // Guards a recursive descent (into '(' or a unary operator) *before*
    // it happens, so the parser's own stack is bounded as well.
    class DescentGuard {
    public:
        DescentGuard(const ParserImpl& parser, std::size_t& nesting, std::size_t offset) : nesting_(nesting) {
            parser.ensureDepth(nesting_ + 1, offset);
            ++nesting_;
        }
        ~DescentGuard() { --nesting_; }
        DescentGuard(const DescentGuard&) = delete;
        DescentGuard& operator=(const DescentGuard&) = delete;

    private:
        std::size_t& nesting_;
    };

    // Each parse step reports its subtree's depth (1 for a leaf) through
    // `depth`. Depth is tracked so operator chains -- built in loops, not
    // by recursion -- are bounded too: evaluating and destroying the AST
    // recurse once per level. The bound is checked before each node is
    // allocated.
    AstNodePtr parseAdditive(std::size_t& depth) {
        AstNodePtr left = parseMultiplicative(depth);
        while (check(TokenType::Plus) || check(TokenType::Minus)) {
            const BinaryOperator op = check(TokenType::Plus) ? BinaryOperator::Add : BinaryOperator::Subtract;
            const std::size_t offset = advance().offset;
            std::size_t rightDepth = 0;
            AstNodePtr right = parseMultiplicative(rightDepth);
            depth = 1 + std::max(depth, rightDepth);
            ensureDepth(depth, offset);
            left = std::make_unique<AstNode>(BinaryExpression{op, std::move(left), std::move(right)});
        }
        return left;
    }

    AstNodePtr parseMultiplicative(std::size_t& depth) {
        AstNodePtr left = parseUnary(depth);
        while (check(TokenType::Star) || check(TokenType::Slash)) {
            const BinaryOperator op = check(TokenType::Star) ? BinaryOperator::Multiply : BinaryOperator::Divide;
            const std::size_t offset = advance().offset;
            std::size_t rightDepth = 0;
            AstNodePtr right = parseUnary(rightDepth);
            depth = 1 + std::max(depth, rightDepth);
            ensureDepth(depth, offset);
            left = std::make_unique<AstNode>(BinaryExpression{op, std::move(left), std::move(right)});
        }
        return left;
    }

    AstNodePtr parseUnary(std::size_t& depth) {
        if (check(TokenType::Plus) || check(TokenType::Minus)) {
            const UnaryOperator op = check(TokenType::Plus) ? UnaryOperator::Plus : UnaryOperator::Minus;
            const std::size_t offset = advance().offset;
            const DescentGuard guard(*this, nesting_, offset);
            AstNodePtr operand = parseUnary(depth);
            depth += 1;
            ensureDepth(depth, offset);
            return std::make_unique<AstNode>(UnaryExpression{op, std::move(operand)});
        }
        return parsePrimary(depth);
    }

    AstNodePtr parsePrimary(std::size_t& depth) {
        const Token& token = current();
        if (token.type == TokenType::Number) {
            advance();
            depth = 1;
            return std::make_unique<AstNode>(Literal{parseNumberLiteral(token)});
        }
        if (token.type == TokenType::AccountReference) {
            advance();
            depth = 1;
            return std::make_unique<AstNode>(AccountReference{domain::AccountCode(token.text)});
        }
        if (token.type == TokenType::ComputedAccountReference) {
            advance();
            depth = 1;
            return std::make_unique<AstNode>(ComputedAccountReference{ComputedAccountName(token.text)});
        }
        if (token.type == TokenType::LeftParen) {
            const std::size_t offset = advance().offset;
            const DescentGuard guard(*this, nesting_, offset);
            AstNodePtr inner = parseAdditive(depth);
            expect(TokenType::RightParen, "Expected ')'");
            advance();
            return inner;
        }
        throw FormulaSyntaxException("Expected a number, account reference, or '('", token.offset);
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::size_t nesting_ = 0;
};

} // namespace

std::size_t syntaxTreeDepth(const AstNode& root) {
    std::size_t deepest = 0;
    std::vector<std::pair<const AstNode*, std::size_t>> pending{{&root, 1}};
    while (!pending.empty()) {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        deepest = std::max(deepest, depth);
        if (const auto* unary = std::get_if<UnaryExpression>(&node->value())) {
            pending.emplace_back(unary->operand.get(), depth + 1);
        } else if (const auto* binary = std::get_if<BinaryExpression>(&node->value())) {
            pending.emplace_back(binary->left.get(), depth + 1);
            pending.emplace_back(binary->right.get(), depth + 1);
        }
    }
    return deepest;
}

AstNodePtr parse(const std::string& source) {
    ParserImpl parser(tokenize(source));
    return parser.parseExpression();
}

} // namespace ledgercore::formula
