// File: tests/unit/test_basic_parser_peek.cpp
// Purpose: Verify Parser::peek clamps negative lookahead without consuming extra tokens.
// Key invariants: Negative index is treated as zero; lexer not advanced beyond current token.
// Ownership/Lifetime: Test owns parser and source. Links: docs/class-catalog.md

#define private public
#include "frontends/basic/Parser.hpp"
#undef private
#include "support/source_manager.hpp"
#include <cassert>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;

int main()
{
    std::string src = "10 END\n";
    SourceManager sm;
    uint32_t fid = sm.addFile("test.bas");
    Parser p(src, fid);

    size_t before = p.tokens_.size();

    const Token &t = p.peek(-1);
    assert(t.kind == TokenKind::Number);
    assert(p.tokens_.size() == before);

    const Token &t0 = p.peek(0);
    assert(t0.lexeme == t.lexeme);
    assert(p.tokens_.size() == before);

    return 0;
}
