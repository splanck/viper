//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_basic_lexer_high_bit.cpp
// Purpose: Ensure BASIC lexer handles high-bit characters without UB. 
// Key invariants: None.
// Ownership/Lifetime: N/A (test).
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lexer.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;

int main()
{
    assert(std::string(tokenKindToString(TokenKind::Unknown)) == "?");
    {
        std::string input = std::string("1") + static_cast<char>(0x80);
        Lexer lex(input, 0);
        Token t1 = lex.next();
        assert(t1.kind == TokenKind::Number);
        Token t2 = lex.next();
        assert(t2.kind == TokenKind::Unknown);
        assert(t2.lexeme == std::string(1, static_cast<char>(0x80)));
        assert(lex.next().kind == TokenKind::EndOfFile);
    }
    {
        std::string input = std::string("A") + static_cast<char>(0x80);
        Lexer lex(input, 0);
        Token t1 = lex.next();
        assert(t1.kind == TokenKind::Identifier);
        Token t2 = lex.next();
        assert(t2.kind == TokenKind::Unknown);
        assert(t2.lexeme == std::string(1, static_cast<char>(0x80)));
        assert(lex.next().kind == TokenKind::EndOfFile);
    }
    {
        std::string input(1, static_cast<char>(0x80));
        Lexer lex(input, 0);
        Token t = lex.next();
        assert(t.kind == TokenKind::Unknown);
        assert(t.lexeme == std::string(1, static_cast<char>(0x80)));
        assert(lex.next().kind == TokenKind::EndOfFile);
    }
    {
        std::string input(1, static_cast<char>(0xFF));
        Lexer lex(input, 0);
        Token t = lex.next();
        assert(t.kind == TokenKind::Unknown);
        assert(t.lexeme == std::string(1, static_cast<char>(0xFF)));
        assert(lex.next().kind == TokenKind::EndOfFile);
    }
    {
        std::string input = std::string("REM") + static_cast<char>(0x80) + "\n1";
        Lexer lex(input, 0);
        Token t1 = lex.next();
        assert(t1.kind == TokenKind::EndOfLine);
        Token t2 = lex.next();
        assert(t2.kind == TokenKind::Number);
    }
    return 0;
}
