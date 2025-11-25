//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/frontends/basic/LexerFileIoTests.cpp
// Purpose: Ensure BASIC lexer recognizes file I/O related keywords and '#'.
// Key invariants: Lexer should classify new keywords distinctly.
// Ownership/Lifetime: Test owns source buffers used for lexing.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "frontends/basic/Lexer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
Token nextToken(std::string_view text)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("lexer_file_io.bas");
    Lexer lexer(text, fid);
    return lexer.next();
}
} // namespace

int main()
{
    {
        Token tok = nextToken("OPEN");
        assert(tok.kind == TokenKind::KeywordOpen);
    }

    {
        Token tok = nextToken("FOR");
        assert(tok.kind == TokenKind::KeywordFor);
    }

    {
        Token tok = nextToken("AS");
        assert(tok.kind == TokenKind::KeywordAs);
    }

    {
        Token tok = nextToken("CLOSE");
        assert(tok.kind == TokenKind::KeywordClose);
    }

    {
        Token tok = nextToken("OUTPUT");
        assert(tok.kind == TokenKind::KeywordOutput);
    }

    {
        Token tok = nextToken("APPEND");
        assert(tok.kind == TokenKind::KeywordAppend);
    }

    {
        Token tok = nextToken("BINARY");
        assert(tok.kind == TokenKind::KeywordBinary);
    }

    {
        Token tok = nextToken("RANDOM");
        assert(tok.kind == TokenKind::KeywordRandom);
    }

    {
        SourceManager sm;
        uint32_t fid = sm.addFile("hash_literal.bas");
        Lexer lexer("#1", fid);
        Token tok = lexer.next();
        assert(tok.kind == TokenKind::Hash);
        assert(tok.lexeme == "#");
        tok = lexer.next();
        assert(tok.kind == TokenKind::Number);
        assert(tok.lexeme == "1");
    }

    {
        Token tok = nextToken("LINE");
        // 'LINE' is a soft keyword; lex as identifier. Parser recognises 'LINE INPUT'.
        assert(tok.kind == TokenKind::Identifier);
    }

    {
        Token tok = nextToken("INPUT");
        assert(tok.kind == TokenKind::KeywordInput);
    }

    {
        Token tok = nextToken("EOF");
        assert(tok.kind == TokenKind::KeywordEof);
    }

    {
        Token tok = nextToken("LOF");
        assert(tok.kind == TokenKind::KeywordLof);
    }

    return 0;
}
