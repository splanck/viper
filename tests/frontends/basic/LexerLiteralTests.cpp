// File: tests/frontends/basic/LexerLiteralTests.cpp
// Purpose: Verify BASIC lexer correctly tokenizes challenging literal forms.
// Key invariants: Hexadecimal floats and Unicode escapes are preserved verbatim.
// Ownership/Lifetime: Tests own all temporary buffers.
// Links: docs/codemap.md

#include "frontends/basic/Lexer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>
#include <string_view>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
Token lexFirstToken(std::string_view text, uint32_t fid)
{
    Lexer lexer(text, fid);
    return lexer.next();
}
} // namespace

int main()
{
    SourceManager sm;
    uint32_t fid = sm.addFile("lexer_literal_tests.bas");

    {
        Token tok = lexFirstToken("0x1.fp3\n", fid);
        assert(tok.kind == TokenKind::Number);
        assert(tok.lexeme == "0x1.fp3");
    }

    {
        Token tok = lexFirstToken("0x1.8P+1#\n", fid);
        assert(tok.kind == TokenKind::Number);
        assert(tok.lexeme == "0x1.8P+1#");
    }

    {
        Token tok = lexFirstToken("\"smile \\u{1F600}\"\n", fid);
        assert(tok.kind == TokenKind::String);
        assert(tok.lexeme == "smile \\u{1F600}");
    }

    {
        Token tok = lexFirstToken("\"symbol \\U0001F4A9\"\n", fid);
        assert(tok.kind == TokenKind::String);
        assert(tok.lexeme == "symbol \\U0001F4A9");
    }

    return 0;
}
