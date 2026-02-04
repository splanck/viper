//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements usage and version output for the `vbasic` CLI tool.

#include "usage.hpp"
#include "tools/common/CommonUsage.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace vbasic
{

void printVersion()
{
    std::cout << "vbasic v" << VIPER_VERSION_STR << "\n";
    std::cout << "Viper BASIC Interpreter/Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

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
              << "  vbasic game.bas -- arg1 arg2              Pass args to program\n"
              << "  vbasic game.bas --stdin-from input.txt    Redirect input\n"
              << "\n"
              << "Documentation: docs/basic-reference.md\n";
}

} // namespace vbasic
