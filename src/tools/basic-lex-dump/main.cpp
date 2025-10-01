// File: src/tools/basic-lex-dump/main.cpp
// License: MIT License. See LICENSE in the project root for full details.
// Purpose: Command-line tool to dump BASIC tokens for golden tests.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded source.
// Links: docs/codemap.md

#include "frontends/basic/Lexer.hpp"
#include "frontends/basic/Token.hpp"
#include "support/source_manager.hpp"
#include "tools/basic/common.hpp"

#include <iostream>
#include <optional>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;
using il::tools::basic::loadBasicSource;

/// @brief Tool entry point that dumps BASIC source tokens for golden tests.
///
/// Accepts a single command-line argument naming a BASIC source file. The
/// execution flow validates the argument count, loads the requested file into
/// memory, lexes tokens using the BASIC lexer, and prints each token on its own
/// line using the format `<line>:<column> <token-kind> [<lexeme>]`, where the
/// lexeme is included for numbers, strings, and identifiers. If the file is
/// missing or unreadable, an error message is emitted and the process exits with
/// a non-zero status.
int main(int argc, char **argv)
{
    std::string src;
    SourceManager sm;
    std::optional<std::uint32_t> fileId = loadBasicSource(argc == 2 ? argv[1] : nullptr, src, sm);
    if (!fileId)
    {
        return 1;
    }

    Lexer lex(src, *fileId);
    for (Token t = lex.next();; t = lex.next())
    {
        std::cout << t.loc.line << ":" << t.loc.column << " " << tokenKindToString(t.kind);
        if (t.kind == TokenKind::Number || t.kind == TokenKind::String ||
            t.kind == TokenKind::Identifier)
            std::cout << " " << t.lexeme;
        std::cout << "\n";
        if (t.kind == TokenKind::EndOfFile)
            break;
    }
    return 0;
}
