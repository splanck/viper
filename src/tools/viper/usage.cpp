//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Implements usage and version output for the `viper` CLI tool.
/// @details Centralizes help text so the main entry point and compatibility
///          shims can share consistent output.

#include "usage.hpp"
#include "viper/version.hpp"
#include <iostream>

namespace viperlang
{

/// @brief Print tool and IL version information.
/// @details Emits the ViperLang compiler name, semantic version string, and IL
///          version to stdout. This output is intended for scripting and
///          diagnostics rather than end-user help.
void printVersion()
{
    std::cout << "viper v" << VIPER_VERSION_STR << "\n";
    std::cout << "ViperLang Compiler\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

/// @brief Print usage information for the `viper` command.
/// @details Writes a multi-section help message to stderr including invocation
///          modes, flag descriptions, and practical examples. The messaging is
///          designed to guide new users while remaining concise for CLI users.
void printUsage()
{
    std::cerr << "viper v" << VIPER_VERSION_STR << " - ViperLang Compiler\n"
              << "\n"
              << "Usage: viper [options] <file.viper>\n"
              << "\n"
              << "Usage Modes:\n"
              << "  viper script.viper              Run program (default)\n"
              << "  viper script.viper --emit-il    Emit IL to stdout\n"
              << "  viper script.viper -o file.il   Emit IL to file\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output FILE              Output file for IL\n"
              << "  --emit-il                      Emit IL instead of running\n"
              << "  --trace[=il|src]               Enable execution tracing\n"
              << "  -h, --help                     Show this help message\n"
              << "  --version                      Show version information\n"
              << "\n"
              << "Examples:\n"
              << "  viper hello.viper              Run program\n"
              << "  viper hello.viper --emit-il    Show generated IL\n"
              << "\n";
}

} // namespace viperlang
