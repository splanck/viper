//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements help text and usage information for the vbasic command-line tool.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements usage and version output for the `vbasic` CLI tool.
/// @details Centralizes help text and version reporting for the BASIC frontend
///          so other entry points can remain minimal.

#include "usage.hpp"
#include "frontends/basic/Intrinsics.hpp"
#include "tools/common/CommonUsage.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace vbasic
{

/// @brief Print tool and IL version information for vbasic.
/// @details Writes the Viper BASIC tool name and versions to stdout for use by
///          scripts and diagnostic collection.
void printVersion()
{
    std::cout << "vbasic v" << VIPER_VERSION_STR << "\n";
    std::cout << "Viper BASIC Interpreter/Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

/// @brief Print usage information for the vbasic command.
/// @details Emits the CLI synopsis, supported options, examples, and a short
///          list of BASIC language notes. Built-in function names are appended
///          at runtime to stay in sync with the front end.
void printUsage()
{
    std::cerr << "vbasic v" << VIPER_VERSION_STR << " - Viper BASIC Interpreter\n"
              << "\n"
              << "Usage: vbasic [options] <file.bas>\n"
              << "\n"
              << "Usage Modes:\n"
              << "  vbasic script.bas              Run program (default)\n"
              << "  vbasic script.bas --emit-il    Emit IL to stdout\n"
              << "  vbasic script.bas -o file.il   Emit IL to file\n"
              << "\n"
              << "Options:\n";
    viper::tools::printSharedOptions(std::cerr);
    std::cerr << "\n"
              << "Examples:\n"
              << "  vbasic game.bas                           Run program\n"
              << "  vbasic game.bas --emit-il                 Show generated IL\n"
              << "  vbasic game.bas -o game.il                Save IL to file\n"
              << "  vbasic game.bas --trace --bounds-checks   Debug mode\n"
              << "  vbasic game.bas --stdin-from input.txt    Redirect input\n"
              << "\n"
              << "BASIC Language Notes:\n"
              << "  - FUNCTION must RETURN a value on all paths\n"
              << "  - SUB cannot be used as an expression\n"
              << "  - Array parameters are ByRef\n"
              << "  - Built-in functions: ";
    il::frontends::basic::intrinsics::dumpNames(std::cerr);
    std::cerr << "\n"
              << "\n"
              << "For detailed documentation, see: docs/basic-language.md\n";
}

} // namespace vbasic
