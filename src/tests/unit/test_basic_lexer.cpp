//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lexer.cpp
// Purpose: Unit tests for BASIC lexer tokenization across common statements.
// Key invariants: Tokens emitted match expected kinds and lexemes.
// Ownership/Lifetime: N/A (test).
// Links: docs/internals/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lexer.hpp"
#include "support/source_manager.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using il::support::SourceManager;

// Test strategy: These tests cover representative BASIC constructs to ensure the lexer
// recognizes keywords, identifiers, literals, and punctuation correctly. Scenarios include a
// PRINT statement with concatenation, a LET assignment, fractional numbers with type-suffixed
// identifiers, and a function call using parentheses and a string argument.
int main() {
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    // Expect correct tokens for a PRINT statement combining a string literal and arithmetic.
    {
        std::string src = "10 PRINT \"HI\"+20\n";
        Lexer lex(src, fid);
        std::vector<TokenKind> kinds;
        for (Token t = lex.next(); t.kind != TokenKind::EndOfFile; t = lex.next())
            kinds.push_back(t.kind);
        std::vector<TokenKind> expected = {TokenKind::Number,
                                           TokenKind::KeywordPrint,
                                           TokenKind::String,
                                           TokenKind::Plus,
                                           TokenKind::Number,
                                           TokenKind::EndOfLine};
        assert(kinds == expected);
    }
    // CRLF and bare CR are logical line endings too. LexerCursor consumes a
    // CRLF pair atomically, so BASIC must recognize the CR before horizontal
    // whitespace skipping can discard the complete boundary.
    for (const char *lineEnding : {"\r\n", "\r"}) {
        std::string src = std::string("10 PRINT \"A\"") + lineEnding + "20 PRINT \"B\"";
        Lexer lex(src, fid);
        std::vector<TokenKind> kinds;
        for (Token t = lex.next(); t.kind != TokenKind::EndOfFile; t = lex.next())
            kinds.push_back(t.kind);
        const std::vector<TokenKind> expected = {TokenKind::Number,
                                                 TokenKind::KeywordPrint,
                                                 TokenKind::String,
                                                 TokenKind::EndOfLine,
                                                 TokenKind::Number,
                                                 TokenKind::KeywordPrint,
                                                 TokenKind::String};
        assert(kinds == expected);
    }
    // Verify recognition of a LET assignment with identifier and numeric literal.
    {
        std::string src = "LET X=1\n";
        Lexer lex(src, fid);
        Token t = lex.next();
        assert(t.kind == TokenKind::KeywordLet);
        t = lex.next();
        assert(t.kind == TokenKind::Identifier);
        t = lex.next();
        assert(t.kind == TokenKind::Equal);
        t = lex.next();
        assert(t.kind == TokenKind::Number);
    }
    // Confirm support for fractional numbers and type-suffixed identifiers.
    {
        std::string src = ".5  X#\n";
        Lexer lex(src, fid);
        Token t = lex.next();
        assert(t.kind == TokenKind::Number && t.lexeme == ".5");
        t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "X#");
    }
    // Verify hex and binary integer literals are single Number tokens, including
    // legacy BASIC and C-style prefixes with integer suffixes.
    {
        std::string src = "&H2A &hdead &h2a& &B101010 0x2A 0X2a& 0b101010 0B101010%\n";
        Lexer lex(src, fid);
        const std::vector<std::string> expected = {
            "&H2A", "&HDEAD", "&H2A&", "&B101010", "0X2A", "0X2A&", "0B101010", "0B101010%"};
        for (const auto &lexeme : expected) {
            Token t = lex.next();
            assert(t.kind == TokenKind::Number);
            assert(t.lexeme == lexeme);
        }
    }
    // Check lexing of a function call with string argument and parentheses.
    {
        std::string src = "LEN(\"A\")\n";
        Lexer lex(src, fid);
        Token t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "LEN");
        t = lex.next();
        assert(t.kind == TokenKind::LParen);
        t = lex.next();
        assert(t.kind == TokenKind::String);
        t = lex.next();
        assert(t.kind == TokenKind::RParen);
    }
    // Validate case-insensitive keyword recognition via lookup table.
    {
        struct KeywordCase {
            const char *input;
            const char *canonical;
            TokenKind kind;
        };

        const KeywordCase cases[] = {{"print", "PRINT", TokenKind::KeywordPrint},
                                     {"RanDoMize", "RANDOMIZE", TokenKind::KeywordRandomize},
                                     {"elseIf", "ELSEIF", TokenKind::KeywordElseIf},
                                     {"wHiLe", "WHILE", TokenKind::KeywordWhile},
                                     {"WeNd", "WEND", TokenKind::KeywordWend},
                                     {"oReLsE", "ORELSE", TokenKind::KeywordOrElse},
                                     {"oR", "OR", TokenKind::KeywordOr},
                                     {"fUnCtIoN", "FUNCTION", TokenKind::KeywordFunction}};
        for (const auto &kw : cases) {
            std::string src = std::string(kw.input) + "\n";
            Lexer lex(src, fid);
            Token t = lex.next();
            assert(t.kind == kw.kind);
            assert(t.lexeme == kw.canonical);
        }
    }
    // Ensure identifiers remain uppercased when source is lower or mixed case.
    {
        std::string src = "alpha beta$ Gamma#\n";
        Lexer lex(src, fid);
        Token t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "ALPHA");
        t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "BETA$");
        t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "GAMMA#");
    }
    // Accept underscores within identifiers while preserving trailing suffixes.
    {
        std::string src = "SQR_INT%\n";
        Lexer lex(src, fid);
        Token t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "SQR_INT%");
    }
    return 0;
}
