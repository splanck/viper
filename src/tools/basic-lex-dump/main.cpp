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
