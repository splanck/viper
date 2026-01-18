//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Main entry point for the ilrun command-line tool.
// Provides a simplified interface to run IL programs.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point for the ilrun tool - a user-friendly IL runner.
/// @details Wrapper around the existing ilc -run functionality with
///          cleaner, more intuitive command-line syntax.

#include "tools/viper/cli.hpp"
#include "viper/version.hpp"
#include <iostream>
#include <string_view>

namespace
{

void printUsage()
{
    std::cerr << "ilrun v" << VIPER_VERSION_STR << " - IL Program Runner\n"
              << "\n"
              << "Usage: ilrun [options] <file.il>\n"
              << "\n"
              << "Options:\n"
              << "  --trace[=il|src]               Enable execution tracing\n"
              << "  --stdin-from FILE              Redirect stdin from file\n"
              << "  --max-steps N                  Limit execution steps\n"
              << "  --bounds-checks                Enable runtime bounds checks\n"
              << "  --break LABEL|FILE:LINE        Set breakpoint\n"
              << "  --break-src FILE:LINE          Set source breakpoint\n"
              << "  --watch NAME                   Watch variable\n"
              << "  --count                        Show instruction counts\n"
              << "  --time                         Show execution time\n"
              << "  --dump-trap                    Show detailed trap diagnostics\n"
              << "  -h, --help                     Show this help message\n"
              << "  --version                      Show version information\n"
              << "\n"
              << "Examples:\n"
              << "  ilrun program.il                      Run IL program\n"
              << "  ilrun program.il --trace              Run with tracing\n"
              << "  ilrun program.il --break main:10      Debug with breakpoint\n"
              << "  ilrun program.il --count --time       Performance profiling\n"
              << "\n"
              << "Notes:\n"
              << "  - IL files must define func @main()\n"
              << "  - Use --trace=src for source-level tracing (requires debug info)\n"
              << "  - See documentation for debugging features\n";
}

void printVersion()
{
    std::cout << "ilrun v" << VIPER_VERSION_STR << "\n";
    std::cout << "IL Program Runner\n";
    std::cout << "IL version: " << VIPER_IL_VERSION_STR << "\n";
}

} // namespace

/// @brief Main entry point for ilrun command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status: 0 on success, non-zero on error.
///
/// @details Simple wrapper that delegates to cmdRunIL with all arguments.
///          Provides a cleaner interface than `ilc -run` for users.
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    // Handle help/version flags
    std::string_view arg1 = argv[1];
    if (arg1 == "-h" || arg1 == "--help")
    {
        printUsage();
        return 0;
    }
    if (arg1 == "--version")
    {
        printVersion();
        return 0;
    }

    // Delegate directly to cmdRunIL with all arguments (skip argv[0])
    return cmdRunIL(argc - 1, argv + 1);
}
