// File: tests/frontends/basic/LexerSelectCaseTests.cpp
// Purpose: Verify BASIC lexer recognizes SELECT CASE constructs.
// Key invariants: Keywords must be matched case-insensitively.
// Ownership/Lifetime: Test owns the source buffers.
// Links: docs/codemap.md

#include "frontends/basic/Lexer.hpp"
#include "support/source_manager.hpp"

#include <cassert>
#include <string>
#include <string_view>
#include <vector>

using namespace il::frontends::basic;
using namespace il::support;

namespace
{
std::vector<TokenKind> lexKinds(std::string_view text)
{
    SourceManager sm;
    uint32_t fid = sm.addFile("lexer_select_case.bas");
    Lexer lexer(text, fid);

    std::vector<TokenKind> kinds;
    for (Token tok = lexer.next();; tok = lexer.next())
    {
        kinds.push_back(tok.kind);
        if (tok.kind == TokenKind::EndOfFile)
        {
            break;
        }
    }
    return kinds;
}
} // namespace

int main()
{
    {
        const auto kinds = lexKinds("SELECT CASE X\n");
        const std::vector<TokenKind> expected{
            TokenKind::KeywordSelect,
            TokenKind::KeywordCase,
            TokenKind::Identifier,
            TokenKind::EndOfLine,
            TokenKind::EndOfFile,
        };
        assert(kinds == expected);
    }

    {
        const auto kinds = lexKinds("CASE 1, 2, 3\n");
        const std::vector<TokenKind> expected{
            TokenKind::KeywordCase,
            TokenKind::Number,
            TokenKind::Comma,
            TokenKind::Number,
            TokenKind::Comma,
            TokenKind::Number,
            TokenKind::EndOfLine,
            TokenKind::EndOfFile,
        };
        assert(kinds == expected);
    }

    {
        const auto kinds = lexKinds("CASE ELSE\n");
        const std::vector<TokenKind> expected{
            TokenKind::KeywordCase,
            TokenKind::KeywordElse,
            TokenKind::EndOfLine,
            TokenKind::EndOfFile,
        };
        assert(kinds == expected);
    }

    {
        const auto kinds = lexKinds("END SELECT\n");
        const std::vector<TokenKind> expected{
            TokenKind::KeywordEnd,
            TokenKind::KeywordSelect,
            TokenKind::EndOfLine,
            TokenKind::EndOfFile,
        };
        assert(kinds == expected);
    }

    return 0;
}
