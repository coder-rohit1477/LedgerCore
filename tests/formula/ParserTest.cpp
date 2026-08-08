#include <gtest/gtest.h>

#include "ledgercore/formula/Ast.h"
#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/formula/Parser.h"
#include "ledgercore/formula/Rational.h"

using ledgercore::formula::AccountReference;
using ledgercore::formula::AstNode;
using ledgercore::formula::AstNodePtr;
using ledgercore::formula::BinaryExpression;
using ledgercore::formula::BinaryOperator;
using ledgercore::formula::FormulaSyntaxException;
using ledgercore::formula::Literal;
using ledgercore::formula::parse;
using ledgercore::formula::Rational;
using ledgercore::formula::UnaryExpression;
using ledgercore::formula::UnaryOperator;

// ---------------------------------------------------------------------
// Primaries
// ---------------------------------------------------------------------

TEST(ParserTest, ParsesSingleLiteral) {
    AstNodePtr ast = parse("42");
    const auto& literal = std::get<Literal>(ast->value());
    EXPECT_EQ(literal.value, Rational::ofInt(42));
}

TEST(ParserTest, ParsesDecimalLiteral) {
    AstNodePtr ast = parse("1.5");
    const auto& literal = std::get<Literal>(ast->value());
    EXPECT_EQ(literal.value, Rational::fromDecimalParts(1, 5, 1));
}

TEST(ParserTest, ParsesAccountReference) {
    AstNodePtr ast = parse("#1000");
    const auto& reference = std::get<AccountReference>(ast->value());
    EXPECT_EQ(reference.code.value(), "1000");
}

// ---------------------------------------------------------------------
// Precedence and associativity
// ---------------------------------------------------------------------

TEST(ParserTest, MultiplicationBindsTighterThanAddition) {
    // 2 + 3 * 4  ==  2 + (3 * 4)
    AstNodePtr ast = parse("2 + 3 * 4");
    const auto& top = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(top.op, BinaryOperator::Add);
    EXPECT_EQ(std::get<Literal>(top.left->value()).value, Rational::ofInt(2));

    const auto& right = std::get<BinaryExpression>(top.right->value());
    EXPECT_EQ(right.op, BinaryOperator::Multiply);
    EXPECT_EQ(std::get<Literal>(right.left->value()).value, Rational::ofInt(3));
    EXPECT_EQ(std::get<Literal>(right.right->value()).value, Rational::ofInt(4));
}

TEST(ParserTest, ParenthesesOverridePrecedence) {
    // (2 + 3) * 4
    AstNodePtr ast = parse("(2 + 3) * 4");
    const auto& top = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(top.op, BinaryOperator::Multiply);

    const auto& left = std::get<BinaryExpression>(top.left->value());
    EXPECT_EQ(left.op, BinaryOperator::Add);
    EXPECT_EQ(std::get<Literal>(top.right->value()).value, Rational::ofInt(4));
}

TEST(ParserTest, SubtractionIsLeftAssociative) {
    // 10 - 3 - 2  ==  (10 - 3) - 2
    AstNodePtr ast = parse("10 - 3 - 2");
    const auto& top = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(top.op, BinaryOperator::Subtract);
    EXPECT_EQ(std::get<Literal>(top.right->value()).value, Rational::ofInt(2));

    const auto& left = std::get<BinaryExpression>(top.left->value());
    EXPECT_EQ(left.op, BinaryOperator::Subtract);
    EXPECT_EQ(std::get<Literal>(left.left->value()).value, Rational::ofInt(10));
    EXPECT_EQ(std::get<Literal>(left.right->value()).value, Rational::ofInt(3));
}

TEST(ParserTest, DivisionIsLeftAssociative) {
    // 100 / 5 / 2  ==  (100 / 5) / 2
    AstNodePtr ast = parse("100 / 5 / 2");
    const auto& top = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(top.op, BinaryOperator::Divide);
    const auto& left = std::get<BinaryExpression>(top.left->value());
    EXPECT_EQ(left.op, BinaryOperator::Divide);
}

// ---------------------------------------------------------------------
// Unary operators
// ---------------------------------------------------------------------

TEST(ParserTest, UnaryMinusOnLiteral) {
    AstNodePtr ast = parse("-5");
    const auto& unary = std::get<UnaryExpression>(ast->value());
    EXPECT_EQ(unary.op, UnaryOperator::Minus);
    EXPECT_EQ(std::get<Literal>(unary.operand->value()).value, Rational::ofInt(5));
}

TEST(ParserTest, UnaryBindsTighterThanBinary) {
    // -2 * 3  ==  (-2) * 3
    AstNodePtr ast = parse("-2 * 3");
    const auto& top = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(top.op, BinaryOperator::Multiply);
    const auto& leftUnary = std::get<UnaryExpression>(top.left->value());
    EXPECT_EQ(leftUnary.op, UnaryOperator::Minus);
}

TEST(ParserTest, NestedUnaryMinusIsRightNested) {
    // --5  ==  -(-5)
    AstNodePtr ast = parse("--5");
    const auto& outer = std::get<UnaryExpression>(ast->value());
    EXPECT_EQ(outer.op, UnaryOperator::Minus);
    const auto& inner = std::get<UnaryExpression>(outer.operand->value());
    EXPECT_EQ(inner.op, UnaryOperator::Minus);
    EXPECT_EQ(std::get<Literal>(inner.operand->value()).value, Rational::ofInt(5));
}

TEST(ParserTest, UnaryPlusIsAccepted) {
    AstNodePtr ast = parse("+5");
    const auto& unary = std::get<UnaryExpression>(ast->value());
    EXPECT_EQ(unary.op, UnaryOperator::Plus);
}

// ---------------------------------------------------------------------
// Nested expressions
// ---------------------------------------------------------------------

TEST(ParserTest, DoublyNestedParentheses) {
    AstNodePtr ast = parse("((1 + 2))");
    const auto& add = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(add.op, BinaryOperator::Add);
}

TEST(ParserTest, AccountReferencesMixedWithArithmetic) {
    AstNodePtr ast = parse("#1000 + #2000 * 2");
    const auto& top = std::get<BinaryExpression>(ast->value());
    EXPECT_EQ(top.op, BinaryOperator::Add);
    EXPECT_EQ(std::get<AccountReference>(top.left->value()).code.value(), "1000");
    const auto& right = std::get<BinaryExpression>(top.right->value());
    EXPECT_EQ(std::get<AccountReference>(right.left->value()).code.value(), "2000");
}

// ---------------------------------------------------------------------
// Malformed expressions
// ---------------------------------------------------------------------

TEST(ParserTest, OperatorWithoutOperandThrowsAtOffendingToken) {
    // The exact example from the design doc: "12 + * 50"
    //                                              ^
    try {
        parse("12 + * 50");
        FAIL() << "expected FormulaSyntaxException";
    } catch (const FormulaSyntaxException& ex) {
        EXPECT_EQ(ex.offset(), 5u);
    }
}

TEST(ParserTest, MissingClosingParenThrows) {
    try {
        parse("(1 + 2");
        FAIL() << "expected FormulaSyntaxException";
    } catch (const FormulaSyntaxException& ex) {
        EXPECT_EQ(ex.offset(), 6u);  // points at end-of-input
    }
}

TEST(ParserTest, UnexpectedEndOfInputAfterOperatorThrows) {
    EXPECT_THROW(parse("1 +"), FormulaSyntaxException);
}

TEST(ParserTest, TrailingInputAfterValidExpressionThrows) {
    EXPECT_THROW(parse("1 2"), FormulaSyntaxException);
}

TEST(ParserTest, EmptyFormulaThrows) {
    EXPECT_THROW(parse(""), FormulaSyntaxException);
}

TEST(ParserTest, UnmatchedClosingParenThrows) {
    EXPECT_THROW(parse("1)"), FormulaSyntaxException);
}
