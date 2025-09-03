#include "frontends/basic/Lexer.hpp"
#include <cassert>
#include <string>
#include <vector>

using namespace il::frontends::basic;
using il::support::SourceManager;

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
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
    {
        std::string src = ".5  X#\n";
        Lexer lex(src, fid);
        Token t = lex.next();
        assert(t.kind == TokenKind::Number && t.lexeme == ".5");
        t = lex.next();
        assert(t.kind == TokenKind::Identifier && t.lexeme == "X#");
    }
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
