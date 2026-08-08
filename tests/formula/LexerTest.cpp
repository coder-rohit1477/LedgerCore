#include <gtest/gtest.h>

#include <vector>

#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/formula/Lexer.h"
#include "ledgercore/formula/Token.h"

using ledgercore::formula::FormulaSyntaxException;
using ledgercore::formula::Token;
using ledgercore::formula::TokenType;
using ledgercore::formula::tokenize;

TEST(LexerTest, IntegerLiteral) {
    std::vector<Token> tokens = tokenize("123");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::Number);
    EXPECT_EQ(tokens[0].text, "123");
    EXPECT_EQ(tokens[0].offset, 0u);
    EXPECT_EQ(tokens[1].type, TokenType::EndOfInput);
}

TEST(LexerTest, DecimalLiteral) {
    std::vector<Token> tokens = tokenize("100.25");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::Number);
    EXPECT_EQ(tokens[0].text, "100.25");
}

TEST(LexerTest, WhitespaceIsSkippedEverywhere) {
    std::vector<Token> tokens = tokenize("  1 \t + \n 2  ");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::Number);
    EXPECT_EQ(tokens[0].text, "1");
    EXPECT_EQ(tokens[1].type, TokenType::Plus);
    EXPECT_EQ(tokens[2].type, TokenType::Number);
    EXPECT_EQ(tokens[2].text, "2");
}

TEST(LexerTest, AllOperatorsAndParens) {
    std::vector<Token> tokens = tokenize("+-*/()");
    ASSERT_EQ(tokens.size(), 7u);
    EXPECT_EQ(tokens[0].type, TokenType::Plus);
    EXPECT_EQ(tokens[1].type, TokenType::Minus);
    EXPECT_EQ(tokens[2].type, TokenType::Star);
    EXPECT_EQ(tokens[3].type, TokenType::Slash);
    EXPECT_EQ(tokens[4].type, TokenType::LeftParen);
    EXPECT_EQ(tokens[5].type, TokenType::RightParen);
    EXPECT_EQ(tokens[6].type, TokenType::EndOfInput);
}

TEST(LexerTest, AccountReferenceSimple) {
    std::vector<Token> tokens = tokenize("#1000");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::AccountReference);
    EXPECT_EQ(tokens[0].text, "1000");
    EXPECT_EQ(tokens[0].offset, 0u);
}

TEST(LexerTest, AccountReferenceWithDotsAndDashes) {
    std::vector<Token> tokens = tokenize("#1000.1-a_b");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::AccountReference);
    EXPECT_EQ(tokens[0].text, "1000.1-a_b");
}

TEST(LexerTest, NoWhitespaceRequiredBetweenTokens) {
    std::vector<Token> tokens = tokenize("1+2");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].type, TokenType::Number);
    EXPECT_EQ(tokens[1].type, TokenType::Plus);
    EXPECT_EQ(tokens[2].type, TokenType::Number);
}

TEST(LexerTest, InvalidCharacterThrowsWithCorrectOffset) {
    try {
        tokenize("1 + $2");
        FAIL() << "expected FormulaSyntaxException";
    } catch (const FormulaSyntaxException& ex) {
        EXPECT_EQ(ex.offset(), 4u);
    }
}

TEST(LexerTest, MalformedNumberTrailingDotThrows) {
    try {
        tokenize("5.");
        FAIL() << "expected FormulaSyntaxException";
    } catch (const FormulaSyntaxException& ex) {
        EXPECT_EQ(ex.offset(), 1u);
    }
}

TEST(LexerTest, LeadingDotIsAnUnexpectedCharacter) {
    // A leading '.' never enters number-lexing (which requires a digit
    // first), so it is reported as an unexpected character, not a
    // malformed number.
    EXPECT_THROW(tokenize(".5"), FormulaSyntaxException);
}

TEST(LexerTest, HashWithNothingAfterThrows) {
    try {
        tokenize("#");
        FAIL() << "expected FormulaSyntaxException";
    } catch (const FormulaSyntaxException& ex) {
        EXPECT_EQ(ex.offset(), 0u);
    }
}

TEST(LexerTest, HashFollowedByOperatorThrows) {
    EXPECT_THROW(tokenize("#+1"), FormulaSyntaxException);
}

TEST(LexerTest, SourcePositionsAreOffsetsIntoOriginalString) {
    std::vector<Token> tokens = tokenize("#1000 + 2");
    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[0].offset, 0u);  // '#1000'
    EXPECT_EQ(tokens[1].offset, 6u);  // '+'
    EXPECT_EQ(tokens[2].offset, 8u);  // '2'
    EXPECT_EQ(tokens[3].offset, 9u);  // end of input
}

TEST(LexerTest, EmptySourceProducesOnlyEndOfInput) {
    std::vector<Token> tokens = tokenize("");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::EndOfInput);
    EXPECT_EQ(tokens[0].offset, 0u);
}

TEST(LexerTest, EndOfInputOffsetIsSourceLength) {
    std::vector<Token> tokens = tokenize("12");
    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[1].offset, 2u);
}
