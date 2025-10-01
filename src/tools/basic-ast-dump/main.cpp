// File: src/tools/basic-ast-dump/main.cpp
// Purpose: Command-line tool to dump BASIC AST.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded source.
// License: MIT License. See LICENSE in the project root for full license information.
// Links: docs/codemap.md

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/source_manager.hpp"
#include "tools/basic/common.hpp"

#include <iostream>
#include <optional>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;
using il::tools::basic::loadBasicSource;

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
    std::string src;
    SourceManager sm;
    std::optional<std::uint32_t> fileId = loadBasicSource(argc == 2 ? argv[1] : nullptr, src, sm);
    if (!fileId)
    {
        return 1;
    }

    Parser p(src, *fileId);
    auto prog = p.parseProgram();
    AstPrinter printer;
    std::cout << printer.dump(*prog);
    return 0;
}
