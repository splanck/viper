//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements help text and usage information for the vpascal command-line tool.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements usage and version output for the `vpascal` CLI tool.
/// @details Centralizes help text and version reporting for the Pascal frontend
///          so the main driver stays minimal.

#include "usage.hpp"
#include "tools/common/CommonUsage.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace vpascal
{

/// @brief Print tool and IL version information for vpascal.
/// @details Writes the Pascal tool name and versions to stdout for scripting
///          and diagnostic collection.
void printVersion()
{
    std::cout << "vpascal v" << VIPER_VERSION_STR << "\n";
    std::cout << "Viper Pascal Interpreter/Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

/// @brief Print usage information for the vpascal command.
/// @details Emits the CLI synopsis, supported options, examples, and a short
///          list of Pascal language notes to help users get started quickly.
void printUsage()
{
    std::cerr << "vpascal v" << VIPER_VERSION_STR << " - Viper Pascal Interpreter\n"
              << "\n"
              << "Usage: vpascal [options] <file.pas>\n"
              << "\n"
              << "Usage Modes:\n"
              << "  vpascal program.pas              Run program (default)\n"
              << "  vpascal program.pas --emit-il    Emit IL to stdout\n"
              << "  vpascal program.pas -o file.il   Emit IL to file\n"
              << "\n"
              << "Options:\n";
    viper::tools::printSharedOptions(std::cerr);
    std::cerr << "\n"
              << "Examples:\n"
              << "  vpascal hello.pas                         Run program\n"
              << "  vpascal hello.pas --emit-il               Show generated IL\n"
              << "  vpascal hello.pas -o hello.il             Save IL to file\n"
              << "  vpascal hello.pas --trace --bounds-checks Debug mode\n"
              << "  vpascal hello.pas --stdin-from input.txt  Redirect input\n"
              << "\n"
              << "Pascal Language Notes:\n"
              << "  - Programs start with 'program Name;' and end with 'end.'\n"
              << "  - Units use 'unit Name;' with interface/implementation sections\n"
              << "  - Functions must assign to function name to return a value\n"
              << "  - Built-in: WriteLn, ReadLn, IntToStr, Length, Copy, Ord, Chr\n"
              << "\n"
              << "For detailed documentation, see: docs/pascal-language.md\n";
}

} // namespace vpascal
