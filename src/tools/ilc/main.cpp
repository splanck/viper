//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the top-level `ilc` driver. The executable dispatches to
// subcommands that run IL programs, compile BASIC, or apply optimizer passes.
// Shared CLI plumbing lives in cli.cpp; this file wires those helpers into the
// `main` entry point and prints user-facing usage information.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Entry point and usage utilities for the `ilc` driver.
/// @details The translation unit owns only user-interface glue; heavy lifting
///          such as pass management or VM execution is delegated to subcommands.

#include "cli.hpp"
#include "frontends/basic/Intrinsics.hpp"
#include "il/core/Module.hpp"
#include <iostream>
#include <string>
#include <string_view>

namespace
{

constexpr std::string_view kIlcVersion = "0.1.0";

/// @brief Print the ilc version banner and runtime configuration summary.
///
/// @details The banner includes the ilc version, current IL version, and whether
///          deterministic numerics are enabled. The routine is factored out so
///          both `main` and future subcommands can reuse it when handling
///          `--version` flags.
void printVersion()
{
    std::cout << "ilc v" << kIlcVersion << "\n";
    const il::core::Module module;
    std::cout << "IL version: " << module.version << "\n";
    std::cout << "Precise Numerics: enabled\n";
}

} // namespace

/// @brief Print synopsis and option hints for the `ilc` CLI.
///
/// @details Step-by-step summary:
///          1. Emit the tool banner with version information.
///          2. Print usage lines for the `-run`, `front basic`, and `il-opt`
///             subcommands, mirroring the behaviour of their handlers.
///          3. Provide IL and BASIC specific notes, including intrinsic listings
///             supplied by the BASIC front end.
void usage()
{
    std::cerr
        << "ilc v" << kIlcVersion << "\n"
        << "Usage: ilc -run <file.il> [--trace=il|src] [--stdin-from <file>] [--max-steps N]"
           " [--break label|file:line]* [--break-src file:line]* [--watch name]* [--bounds-checks] "
           "[--count] [--time] [--dump-trap]\n"
        << "       ilc front basic -emit-il <file.bas> [--bounds-checks]\n"
        << "       ilc front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] "
           "[--max-steps N] [--break label|file:line]* [--break-src file:line]* [--bounds-checks] "
           "[--dump-trap]\n"
        << "       ilc il-opt <in.il> -o <out.il> --passes p1,p2\n"
        << "\nIL notes:\n"
        << "  IL modules executed with -run must define func @main().\n"
        << "\nBASIC notes:\n"
        << "  FUNCTION must RETURN a value on all paths.\n"
        << "  SUB cannot be used as an expression.\n"
        << "  Array parameters are ByRef; pass the array variable, not an index.\n"
        << "  Intrinsics: ";
    il::frontends::basic::intrinsics::dumpNames(std::cerr);
    std::cerr << "\n";
}

/// @brief Program entry for the `ilc` command-line tool.
///
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status of the selected subcommand or `1` on error.
/// @details The first argument determines which handler processes the request:
///          `cmdRunIL` executes `.il` programs, `cmdILOpt` performs optimization
///          passes, and `cmdFrontBasic` drives the BASIC front end. Step-by-step
///          summary:
///          1. Verify that at least one subcommand argument is provided.
///          2. Handle `--version` by delegating to @ref printVersion.
///          3. Dispatch to the matching handler with the remaining arguments.
///          4. Fall back to displaying usage when no match exists.
int main(int argc, char **argv)
{
    if (argc < 2)
    {
        usage();
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "--version")
    {
        printVersion();
        return 0;
    }
    if (cmd == "-run")
    {
        return cmdRunIL(argc - 2, argv + 2);
    }
    if (cmd == "il-opt")
    {
        return cmdILOpt(argc - 2, argv + 2);
    }
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "basic")
    {
        return cmdFrontBasic(argc - 3, argv + 3);
    }
    usage();
    return 1;
}
