// File: tests/unit/test_basic_lexer.cpp
// Purpose: Unit tests for BASIC lexer tokenization across common statements.
// Key invariants: Tokens emitted match expected kinds and lexemes.
// Ownership/Lifetime: N/A (test).
// Links: docs/class-catalog.md

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
int main()
{
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
    return 0;
}
