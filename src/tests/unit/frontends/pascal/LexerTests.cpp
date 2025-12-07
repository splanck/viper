//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/frontends/pascal/LexerTests.cpp
// Purpose: Comprehensive unit tests for the Viper Pascal lexer.
// Key invariants: Tests case-insensitivity, all token types, error handling.
// Ownership/Lifetime: Test suite.
// Links: docs/devdocs/ViperPascal_v0_1_Draft6_Specification.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_HAS_GTEST
#include <gtest/gtest.h>
#else
#include "../../GTestStub.hpp"
#endif

#include "frontends/pascal/Lexer.hpp"
#include "support/diagnostics.hpp"
#include <vector>

using namespace il::frontends::pascal;
using namespace il::support;

namespace
{

/// @brief Helper to collect all tokens from source.
std::vector<Token> tokenize(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 1, diag);
    std::vector<Token> tokens;
    while (true)
    {
        Token tok = lexer.next();
        if (tok.kind == TokenKind::Eof)
        {
            break;
        }
        tokens.push_back(std::move(tok));
    }
    return tokens;
}

/// @brief Helper to get single token (first non-EOF).
Token singleToken(const std::string &source)
{
    DiagnosticEngine diag;
    Lexer lexer(source, 1, diag);
    return lexer.next();
}

//===----------------------------------------------------------------------===//
// Keywords and Case Insensitivity Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, KeywordBeginCaseInsensitive)
{
    // All case variants should produce the same keyword kind
    auto tok1 = singleToken("begin");
    auto tok2 = singleToken("BEGIN");
    auto tok3 = singleToken("Begin");
    auto tok4 = singleToken("bEgIn");

    EXPECT_EQ(tok1.kind, TokenKind::KwBegin);
    EXPECT_EQ(tok2.kind, TokenKind::KwBegin);
    EXPECT_EQ(tok3.kind, TokenKind::KwBegin);
    EXPECT_EQ(tok4.kind, TokenKind::KwBegin);

    // Original spelling preserved
    EXPECT_EQ(tok1.text, "begin");
    EXPECT_EQ(tok2.text, "BEGIN");
    EXPECT_EQ(tok3.text, "Begin");

    // Canonical form is lowercase
    EXPECT_EQ(tok1.canonical, "begin");
    EXPECT_EQ(tok2.canonical, "begin");
    EXPECT_EQ(tok3.canonical, "begin");
}

TEST(PascalLexerTest, AllKeywordsRecognized)
{
    // Test all keywords from the spec
    const std::vector<std::pair<std::string, TokenKind>> keywords = {
        {"and", TokenKind::KwAnd},
        {"array", TokenKind::KwArray},
        {"begin", TokenKind::KwBegin},
        {"break", TokenKind::KwBreak},
        {"case", TokenKind::KwCase},
        {"class", TokenKind::KwClass},
        {"const", TokenKind::KwConst},
        {"constructor", TokenKind::KwConstructor},
        {"continue", TokenKind::KwContinue},
        {"destructor", TokenKind::KwDestructor},
        {"div", TokenKind::KwDiv},
        {"do", TokenKind::KwDo},
        {"downto", TokenKind::KwDownto},
        {"else", TokenKind::KwElse},
        {"end", TokenKind::KwEnd},
        {"except", TokenKind::KwExcept},
        {"finally", TokenKind::KwFinally},
        {"for", TokenKind::KwFor},
        {"function", TokenKind::KwFunction},
        {"if", TokenKind::KwIf},
        {"implementation", TokenKind::KwImplementation},
        {"in", TokenKind::KwIn},
        {"interface", TokenKind::KwInterface},
        {"mod", TokenKind::KwMod},
        {"nil", TokenKind::KwNil},
        {"not", TokenKind::KwNot},
        {"of", TokenKind::KwOf},
        {"on", TokenKind::KwOn},
        {"or", TokenKind::KwOr},
        {"override", TokenKind::KwOverride},
        {"private", TokenKind::KwPrivate},
        {"procedure", TokenKind::KwProcedure},
        {"program", TokenKind::KwProgram},
        {"public", TokenKind::KwPublic},
        {"raise", TokenKind::KwRaise},
        {"record", TokenKind::KwRecord},
        {"repeat", TokenKind::KwRepeat},
        {"then", TokenKind::KwThen},
        {"to", TokenKind::KwTo},
        {"try", TokenKind::KwTry},
        {"type", TokenKind::KwType},
        {"unit", TokenKind::KwUnit},
        {"until", TokenKind::KwUntil},
        {"uses", TokenKind::KwUses},
        {"var", TokenKind::KwVar},
        {"virtual", TokenKind::KwVirtual},
        {"weak", TokenKind::KwWeak},
        {"while", TokenKind::KwWhile},
    };

    for (const auto &[text, expectedKind] : keywords)
    {
        auto tok = singleToken(text);
        EXPECT_EQ(tok.kind, expectedKind);
    }
}

TEST(PascalLexerTest, IdentifiersCaseInsensitive)
{
    auto tok1 = singleToken("MyVar");
    auto tok2 = singleToken("myvar");
    auto tok3 = singleToken("MYVAR");

    EXPECT_EQ(tok1.kind, TokenKind::Identifier);
    EXPECT_EQ(tok2.kind, TokenKind::Identifier);
    EXPECT_EQ(tok3.kind, TokenKind::Identifier);

    // Original spelling preserved
    EXPECT_EQ(tok1.text, "MyVar");
    EXPECT_EQ(tok2.text, "myvar");
    EXPECT_EQ(tok3.text, "MYVAR");

    // Canonical form is lowercase
    EXPECT_EQ(tok1.canonical, "myvar");
    EXPECT_EQ(tok2.canonical, "myvar");
    EXPECT_EQ(tok3.canonical, "myvar");
}

TEST(PascalLexerTest, IdentifierWithUnderscores)
{
    auto tok = singleToken("my_variable_name");
    EXPECT_EQ(tok.kind, TokenKind::Identifier);
    EXPECT_EQ(tok.text, "my_variable_name");
}

TEST(PascalLexerTest, IdentifierWithDigits)
{
    auto tok = singleToken("var123");
    EXPECT_EQ(tok.kind, TokenKind::Identifier);
    EXPECT_EQ(tok.text, "var123");
}

//===----------------------------------------------------------------------===//
// Predefined Identifiers Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, PredefinedIdentifiers)
{
    const std::vector<std::string> predefined = {
        "Self", "Result", "True", "False",
        "Integer", "Real", "Boolean", "String", "Exception"
    };

    for (const auto &name : predefined)
    {
        auto tok = singleToken(name);
        EXPECT_EQ(tok.kind, TokenKind::Identifier);
        EXPECT_TRUE(tok.isPredefined);
    }

    // Case insensitive
    auto tok = singleToken("SELF");
    EXPECT_TRUE(tok.isPredefined);

    tok = singleToken("true");
    EXPECT_TRUE(tok.isPredefined);
}

TEST(PascalLexerTest, RegularIdentifierNotPredefined)
{
    auto tok = singleToken("MyVariable");
    EXPECT_EQ(tok.kind, TokenKind::Identifier);
    EXPECT_FALSE(tok.isPredefined);
}

//===----------------------------------------------------------------------===//
// Comment Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, LineComment)
{
    auto tokens = tokenize("begin // this is a comment\nend");
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwEnd);
    EXPECT_EQ(tokens[1].loc.line, 2u);
}

TEST(PascalLexerTest, BraceBlockComment)
{
    auto tokens = tokenize("begin { block comment } end");
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwEnd);
}

TEST(PascalLexerTest, ParenStarBlockComment)
{
    auto tokens = tokenize("begin (* block comment *) end");
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwEnd);
}

TEST(PascalLexerTest, MultilineBlockComment)
{
    auto tokens = tokenize("begin { comment\nspanning\nlines } end");
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwEnd);
    // end should be on line 3
    EXPECT_EQ(tokens[1].loc.line, 3u);
}

TEST(PascalLexerTest, CommentLineColumnTracking)
{
    // Check that line/column is updated correctly after comments
    auto tokens = tokenize("a // comment\nb");
    EXPECT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].loc.line, 1u);
    EXPECT_EQ(tokens[0].loc.column, 1u);
    EXPECT_EQ(tokens[1].loc.line, 2u);
    EXPECT_EQ(tokens[1].loc.column, 1u);
}

//===----------------------------------------------------------------------===//
// Numeric Literal Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, IntegerLiterals)
{
    auto tok = singleToken("42");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 42);
    EXPECT_EQ(tok.text, "42");

    tok = singleToken("0");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 0);

    tok = singleToken("12345678901234");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 12345678901234LL);
}

TEST(PascalLexerTest, RealLiterals)
{
    auto tok = singleToken("3.14");
    EXPECT_EQ(tok.kind, TokenKind::RealLiteral);
    EXPECT_TRUE(tok.realValue > 3.13 && tok.realValue < 3.15);
    EXPECT_EQ(tok.text, "3.14");

    tok = singleToken("1.0");
    EXPECT_EQ(tok.kind, TokenKind::RealLiteral);
    EXPECT_TRUE(tok.realValue > 0.99 && tok.realValue < 1.01);
}

TEST(PascalLexerTest, RealLiteralsWithExponent)
{
    auto tok = singleToken("1.0e-5");
    EXPECT_EQ(tok.kind, TokenKind::RealLiteral);
    EXPECT_TRUE(tok.realValue > 0.9e-5 && tok.realValue < 1.1e-5);

    tok = singleToken("2.5E+10");
    EXPECT_EQ(tok.kind, TokenKind::RealLiteral);
    EXPECT_TRUE(tok.realValue > 2.4e10 && tok.realValue < 2.6e10);

    tok = singleToken("1e3");
    EXPECT_EQ(tok.kind, TokenKind::RealLiteral);
    EXPECT_TRUE(tok.realValue > 999 && tok.realValue < 1001);
}

TEST(PascalLexerTest, HexLiterals)
{
    auto tok = singleToken("$FF");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 255);
    EXPECT_EQ(tok.text, "$FF");

    tok = singleToken("$DEADBEEF");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 0xDEADBEEF);

    tok = singleToken("$0");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 0);

    // Lowercase hex digits
    tok = singleToken("$ff");
    EXPECT_EQ(tok.kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tok.intValue, 255);
}

TEST(PascalLexerTest, IntegerBeforeRangeOperator)
{
    // 1..10 should be: IntegerLiteral(1), DotDot, IntegerLiteral(10)
    auto tokens = tokenize("1..10");
    EXPECT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tokens[0].intValue, 1);
    EXPECT_EQ(tokens[1].kind, TokenKind::DotDot);
    EXPECT_EQ(tokens[2].kind, TokenKind::IntegerLiteral);
    EXPECT_EQ(tokens[2].intValue, 10);
}

//===----------------------------------------------------------------------===//
// String Literal Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, SimpleString)
{
    auto tok = singleToken("'Hello'");
    EXPECT_EQ(tok.kind, TokenKind::StringLiteral);
    EXPECT_EQ(tok.canonical, "Hello");
    EXPECT_EQ(tok.text, "'Hello'");
}

TEST(PascalLexerTest, StringWithDoubledApostrophe)
{
    auto tok = singleToken("'It''s fine'");
    EXPECT_EQ(tok.kind, TokenKind::StringLiteral);
    EXPECT_EQ(tok.canonical, "It's fine");
}

TEST(PascalLexerTest, EmptyString)
{
    auto tok = singleToken("''");
    EXPECT_EQ(tok.kind, TokenKind::StringLiteral);
    EXPECT_EQ(tok.canonical, "");
}

TEST(PascalLexerTest, StringWithMultipleApostrophes)
{
    auto tok = singleToken("'Don''t say ''never'''");
    EXPECT_EQ(tok.kind, TokenKind::StringLiteral);
    EXPECT_EQ(tok.canonical, "Don't say 'never'");
}

//===----------------------------------------------------------------------===//
// Operator Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, SingleCharOperators)
{
    EXPECT_EQ(singleToken("+").kind, TokenKind::Plus);
    EXPECT_EQ(singleToken("-").kind, TokenKind::Minus);
    EXPECT_EQ(singleToken("*").kind, TokenKind::Star);
    EXPECT_EQ(singleToken("/").kind, TokenKind::Slash);
    EXPECT_EQ(singleToken("=").kind, TokenKind::Equal);
    EXPECT_EQ(singleToken("<").kind, TokenKind::Less);
    EXPECT_EQ(singleToken(">").kind, TokenKind::Greater);
}

TEST(PascalLexerTest, TwoCharOperators)
{
    EXPECT_EQ(singleToken(":=").kind, TokenKind::Assign);
    EXPECT_EQ(singleToken("<>").kind, TokenKind::NotEqual);
    EXPECT_EQ(singleToken("<=").kind, TokenKind::LessEqual);
    EXPECT_EQ(singleToken(">=").kind, TokenKind::GreaterEqual);
    EXPECT_EQ(singleToken("??").kind, TokenKind::NilCoalesce);
    EXPECT_EQ(singleToken("..").kind, TokenKind::DotDot);
}

TEST(PascalLexerTest, OperatorKeywords)
{
    // div, mod, and, or, not are keywords that act as operators
    EXPECT_EQ(singleToken("div").kind, TokenKind::KwDiv);
    EXPECT_EQ(singleToken("mod").kind, TokenKind::KwMod);
    EXPECT_EQ(singleToken("and").kind, TokenKind::KwAnd);
    EXPECT_EQ(singleToken("or").kind, TokenKind::KwOr);
    EXPECT_EQ(singleToken("not").kind, TokenKind::KwNot);
}

TEST(PascalLexerTest, OperatorSequence)
{
    auto tokens = tokenize(":= = <> < > <= >= ?? + - * / div mod and or not");
    EXPECT_EQ(tokens.size(), 17u);
    EXPECT_EQ(tokens[0].kind, TokenKind::Assign);
    EXPECT_EQ(tokens[1].kind, TokenKind::Equal);
    EXPECT_EQ(tokens[2].kind, TokenKind::NotEqual);
    EXPECT_EQ(tokens[3].kind, TokenKind::Less);
    EXPECT_EQ(tokens[4].kind, TokenKind::Greater);
    EXPECT_EQ(tokens[5].kind, TokenKind::LessEqual);
    EXPECT_EQ(tokens[6].kind, TokenKind::GreaterEqual);
    EXPECT_EQ(tokens[7].kind, TokenKind::NilCoalesce);
    EXPECT_EQ(tokens[8].kind, TokenKind::Plus);
    EXPECT_EQ(tokens[9].kind, TokenKind::Minus);
    EXPECT_EQ(tokens[10].kind, TokenKind::Star);
    EXPECT_EQ(tokens[11].kind, TokenKind::Slash);
    EXPECT_EQ(tokens[12].kind, TokenKind::KwDiv);
    EXPECT_EQ(tokens[13].kind, TokenKind::KwMod);
    EXPECT_EQ(tokens[14].kind, TokenKind::KwAnd);
    EXPECT_EQ(tokens[15].kind, TokenKind::KwOr);
    EXPECT_EQ(tokens[16].kind, TokenKind::KwNot);
}

//===----------------------------------------------------------------------===//
// Punctuation Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, Punctuation)
{
    EXPECT_EQ(singleToken(".").kind, TokenKind::Dot);
    EXPECT_EQ(singleToken(",").kind, TokenKind::Comma);
    EXPECT_EQ(singleToken(";").kind, TokenKind::Semicolon);
    EXPECT_EQ(singleToken(":").kind, TokenKind::Colon);
    EXPECT_EQ(singleToken("(").kind, TokenKind::LParen);
    EXPECT_EQ(singleToken(")").kind, TokenKind::RParen);
    EXPECT_EQ(singleToken("[").kind, TokenKind::LBracket);
    EXPECT_EQ(singleToken("]").kind, TokenKind::RBracket);
    EXPECT_EQ(singleToken("^").kind, TokenKind::Caret);
    EXPECT_EQ(singleToken("@").kind, TokenKind::At);
}

//===----------------------------------------------------------------------===//
// Error Handling Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, UnterminatedString)
{
    DiagnosticEngine diag;
    Lexer lexer("'Hello", 1, diag);
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Error);
    EXPECT_EQ(diag.errorCount(), 1u);
}

TEST(PascalLexerTest, UnterminatedBraceComment)
{
    DiagnosticEngine diag;
    Lexer lexer("{ unterminated comment", 1, diag);
    // Just trying to get a token should trigger the error
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Eof);
    EXPECT_EQ(diag.errorCount(), 1u);
}

TEST(PascalLexerTest, UnterminatedParenStarComment)
{
    DiagnosticEngine diag;
    Lexer lexer("(* unterminated comment", 1, diag);
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Eof);
    EXPECT_EQ(diag.errorCount(), 1u);
}

TEST(PascalLexerTest, IllegalCharacter)
{
    DiagnosticEngine diag;
    Lexer lexer("~", 1, diag);
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Error);
    EXPECT_EQ(diag.errorCount(), 1u);
}

TEST(PascalLexerTest, NewlineInString)
{
    DiagnosticEngine diag;
    Lexer lexer("'Hello\nWorld'", 1, diag);
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Error);
    EXPECT_EQ(diag.errorCount(), 1u);
}

TEST(PascalLexerTest, InvalidHexLiteral)
{
    DiagnosticEngine diag;
    Lexer lexer("$", 1, diag);
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Error);
    EXPECT_EQ(diag.errorCount(), 1u);
}

TEST(PascalLexerTest, SingleQuestionMark)
{
    // Single ? is now valid (optional type suffix)
    DiagnosticEngine diag;
    Lexer lexer("?", 1, diag);
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::Question);
    EXPECT_EQ(diag.errorCount(), 0u);
}

//===----------------------------------------------------------------------===//
// Location Tracking Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, LocationTracking)
{
    auto tokens = tokenize("begin\n  x := 1;\nend");

    EXPECT_EQ(tokens[0].loc.line, 1u);
    EXPECT_EQ(tokens[0].loc.column, 1u);

    // x on line 2
    EXPECT_EQ(tokens[1].loc.line, 2u);
    EXPECT_EQ(tokens[1].loc.column, 3u);

    // := on line 2
    EXPECT_EQ(tokens[2].loc.line, 2u);
    EXPECT_EQ(tokens[2].loc.column, 5u);

    // 1 on line 2
    EXPECT_EQ(tokens[3].loc.line, 2u);
    EXPECT_EQ(tokens[3].loc.column, 8u);

    // ; on line 2
    EXPECT_EQ(tokens[4].loc.line, 2u);
    EXPECT_EQ(tokens[4].loc.column, 9u);

    // end on line 3
    EXPECT_EQ(tokens[5].loc.line, 3u);
    EXPECT_EQ(tokens[5].loc.column, 1u);
}

//===----------------------------------------------------------------------===//
// Peek Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, PeekDoesNotConsume)
{
    DiagnosticEngine diag;
    Lexer lexer("begin end", 1, diag);

    const Token &peeked = lexer.peek();
    EXPECT_EQ(peeked.kind, TokenKind::KwBegin);

    // Peek again - should return same token
    const Token &peeked2 = lexer.peek();
    EXPECT_EQ(peeked2.kind, TokenKind::KwBegin);

    // Now consume
    Token tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::KwBegin);

    // Next should be 'end'
    tok = lexer.next();
    EXPECT_EQ(tok.kind, TokenKind::KwEnd);
}

//===----------------------------------------------------------------------===//
// Integration Tests
//===----------------------------------------------------------------------===//

TEST(PascalLexerTest, SimpleProgramTokenization)
{
    const std::string source = R"(
program Hello;
begin
  WriteLn('Hello, World!');
end.
)";

    auto tokens = tokenize(source);

    // program Hello ; begin WriteLn ( 'Hello, World!' ) ; end .
    EXPECT_EQ(tokens.size(), 11u);
    EXPECT_EQ(tokens[0].kind, TokenKind::KwProgram);
    EXPECT_EQ(tokens[1].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[1].text, "Hello");
    EXPECT_EQ(tokens[2].kind, TokenKind::Semicolon);
    EXPECT_EQ(tokens[3].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[4].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[4].canonical, "writeln");
    EXPECT_EQ(tokens[5].kind, TokenKind::LParen);
    EXPECT_EQ(tokens[6].kind, TokenKind::StringLiteral);
    EXPECT_EQ(tokens[6].canonical, "Hello, World!");
    EXPECT_EQ(tokens[7].kind, TokenKind::RParen);
    EXPECT_EQ(tokens[8].kind, TokenKind::Semicolon);
    EXPECT_EQ(tokens[9].kind, TokenKind::KwEnd);
    EXPECT_EQ(tokens[10].kind, TokenKind::Dot);
}

TEST(PascalLexerTest, MixedKeywordsAndIdentifiers)
{
    auto tokens = tokenize("begin BEGIN Begin MyVar myvar MYVAR Self Result Integer String");

    EXPECT_EQ(tokens.size(), 10u);

    // All three 'begin' variants are the same keyword
    EXPECT_EQ(tokens[0].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[1].kind, TokenKind::KwBegin);
    EXPECT_EQ(tokens[2].kind, TokenKind::KwBegin);

    // MyVar variants are identifiers
    EXPECT_EQ(tokens[3].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[4].kind, TokenKind::Identifier);
    EXPECT_EQ(tokens[5].kind, TokenKind::Identifier);

    // Predefined identifiers
    EXPECT_EQ(tokens[6].kind, TokenKind::Identifier);
    EXPECT_TRUE(tokens[6].isPredefined);  // Self
    EXPECT_EQ(tokens[7].kind, TokenKind::Identifier);
    EXPECT_TRUE(tokens[7].isPredefined);  // Result
    EXPECT_EQ(tokens[8].kind, TokenKind::Identifier);
    EXPECT_TRUE(tokens[8].isPredefined);  // Integer
    EXPECT_EQ(tokens[9].kind, TokenKind::Identifier);
    EXPECT_TRUE(tokens[9].isPredefined);  // String
}

} // namespace

#ifndef VIPER_HAS_GTEST
int main()
{
    return RUN_ALL_TESTS();
}
#endif
