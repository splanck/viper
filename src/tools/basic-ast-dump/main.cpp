//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the `basic-ast-dump` developer utility. The program loads BASIC
// source, parses it using the production front-end, and pretty prints the AST.
// The tool mirrors the diagnostics and file handling of the main compiler so it
// is suitable for manual experiments and golden test generation.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the BASIC AST dumper CLI.
/// @details The utility shares command-line parsing helpers with other BASIC
///          developer tools so diagnostics and usage text stay consistent.

#include "frontends/basic/AstPrinter.hpp"
#include "frontends/basic/DiagnosticEmitter.hpp"
#include "frontends/basic/Parser.hpp"
#include "support/diagnostics.hpp"
#include "support/source_manager.hpp"
#include "tools/basic/common.hpp"

#include <iostream>
#include <optional>
#include <string>

using namespace il::frontends::basic;
using namespace il::support;
using il::tools::basic::loadBasicSource;

/// @brief Entry point for the BASIC AST dump tool.
///
/// @details Step-by-step execution:
///          1. Validate the argument count and load the requested BASIC file via
///             @ref il::tools::basic::loadBasicSource, capturing diagnostics
///             consistent with the rest of the BASIC toolchain.
///          2. Register the file with the source manager so future diagnostics
///             resolve to readable paths.
///          3. Parse the program into an AST using
///             @ref il::frontends::basic::Parser.
///          4. Pretty print the AST with @ref il::frontends::basic::AstPrinter
///             and emit the result to stdout.
///          The function exits with @c 0 on success or @c 1 when argument
///          validation or file loading fails, matching the conventions of the
///          other developer tools.
///
/// @param argc Argument count supplied by the C runtime.
/// @param argv Argument vector containing UTF-8 encoded strings.
/// @return Zero on success; one when argument validation or file loading fails.
int main(int argc, char **argv)
{
    std::string src;
    SourceManager sm;
    std::optional<std::uint32_t> fileId = loadBasicSource(argc == 2 ? argv[1] : nullptr, src, sm);
    if (!fileId)
    {
        return 1;
    }

    il::support::DiagnosticEngine diagEngine;
    DiagnosticEmitter emitter(diagEngine, sm);
    emitter.addSource(*fileId, src);

    std::vector<std::string> includeStack;
    Parser p(src, *fileId, &emitter, &sm, &includeStack);
    auto prog = p.parseProgram();
    if (!prog)
    {
        emitter.printAll(std::cerr);
        std::cerr << std::flush;
        return 1;
    }
    AstPrinter printer;
    std::cout << printer.dump(*prog);
    return 0;
}
