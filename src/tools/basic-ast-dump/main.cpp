// File: src/tools/basic-ast-dump/main.cpp
// Purpose: Command-line tool to dump BASIC AST.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded source.
// License: MIT License. See LICENSE in the project root for full license information.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using namespace il::frontends::basic;
using namespace il::support;

/**
 * @brief Entry point for the BASIC AST dump tool.
 *
 * Expects a single argument: the path to a BASIC source file. The
 * program parses and validates the command-line argument,
 * reads the source file into memory, registers it with the source
 * manager, invokes the BASIC parser to build the AST, and finally
 * prints the AST using the printer to standard output.
 *
 * @return 0 on success.
 * @return 1 if the argument count is incorrect or the file cannot be
 * opened.
 */
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
