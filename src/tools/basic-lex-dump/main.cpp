// File: src/tools/basic-lex-dump/main.cpp
// Purpose: Command-line tool to dump BASIC tokens for golden tests.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded source.
// Links: docs/class-catalog.md

#include "frontends/basic/Lexer.hpp"
#include "frontends/basic/Token.hpp"
#include "support/source_manager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace il::frontends::basic;
using namespace il::support;

/// @brief Tool entry point that dumps BASIC source tokens for golden tests.
///
/// Expects a single command-line argument naming a BASIC source file. When
/// the file can be opened, each token is printed on its own line using the
/// format `<line>:<column> <token-kind> [<lexeme>]`, where the lexeme is
/// included for numbers, strings, and identifiers. If the file is missing or
/// unreadable, an error message is emitted and the process exits with a non-zero
/// status.
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: basic-lex-dump <file.bas>\n";
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in)
    {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
    SourceManager sm;
    uint32_t fid = sm.addFile(argv[1]);
    Lexer lex(src, fid);
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
