// File: src/tools/basic-ast-dump/main.cpp
// Purpose: Command-line tool to dump BASIC AST.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded source.
// Links: docs/class-catalog.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace il::frontends::basic;
using namespace il::support;

/// @brief Entry point for the BASIC AST dumper tool.
/// Expects a single argument: the path to a BASIC source file. The file is
/// read into memory, parsed into an AST, and the resulting tree is printed to
/// standard output.
///
/// Returns a non-zero status if the argument count is incorrect or the file
/// cannot be opened. Resources such as the input stream, source manager, and
/// AST are owned by this function and cleaned up on scope exit.
int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: basic-ast-dump <file.bas>\n";
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
    Parser p(src, fid);
    auto prog = p.parseProgram();
    AstPrinter printer;
    std::cout << printer.dump(*prog);
    return 0;
}
