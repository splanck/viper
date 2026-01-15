//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements usage and version output for the `zia` CLI tool.
/// @details Centralizes help text so the main entry point and compatibility
///          shims can share consistent output.

#include "usage.hpp"
#include "tools/common/CommonUsage.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace zia
{

/// @brief Print tool and IL version information.
/// @details Emits the Zia compiler name, semantic version string, and IL
///          version to stdout. This output is intended for scripting and
///          diagnostics rather than end-user help.
void printVersion()
{
    std::cout << "zia v" << VIPER_VERSION_STR << "\n";
    std::cout << "Zia Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

/// @brief Print usage information for the `zia` command.
/// @details Writes a multi-section help message to stderr including invocation
///          modes, flag descriptions, and practical examples. The messaging is
///          designed to guide new users while remaining concise for CLI users.
void printUsage()
{
    std::cerr << "zia v" << VIPER_VERSION_STR << " - Zia Compiler\n"
              << "\n"
              << "Usage: zia [options] <file.zia>\n"
              << "\n"
              << "Usage Modes:\n"
              << "  zia script.zia              Run program (default)\n"
              << "  zia script.zia --emit-il    Emit IL to stdout\n"
              << "  zia script.zia -o file.il   Emit IL to file\n"
              << "\n"
              << "Options:\n";
    viper::tools::printSharedOptions(std::cerr);
    std::cerr << "\n"
              << "Examples:\n"
              << "  zia hello.zia                           Run program\n"
              << "  zia hello.zia --emit-il                 Show generated IL\n"
              << "  zia hello.zia -o hello.il               Save IL to file\n"
              << "  zia hello.zia --trace --bounds-checks   Debug mode\n"
              << "  zia hello.zia --stdin-from input.txt    Redirect input\n"
              << "\n"
              << "Zia Language Notes:\n"
              << "  - Functions must return a value on all paths\n"
              << "  - Use 'var' for mutable variables, 'let' for immutable\n"
              << "  - Entity types are reference types, value types are copied\n"
              << "  - Use 'bind' to include other Zia modules\n"
              << "\n"
              << "For detailed documentation, see: docs/zia-guide.md\n";
}

} // namespace zia
