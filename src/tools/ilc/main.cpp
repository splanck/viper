// File: src/tools/ilc/main.cpp
// Purpose: Dispatcher for ilc subcommands.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// License: MIT.
// Links: docs/codemap.md

#include "cli.hpp"
#include "frontends/basic/Intrinsics.hpp"
#include <iostream>
#include <string>

/// @brief Print synopsis and option hints for the `ilc` CLI.
/// @details Lists supported subcommands (`-run`, `front basic`, and `il-opt`)
///          along with their expected arguments. This function serves as a
///          reference when no or invalid arguments are provided and mirrors the
///          capabilities of the associated handlers `cmdRunIL`, `cmdFrontBasic`,
///          and `cmdILOpt`. Step-by-step summary:
///          1. Print the tool banner and each usage synopsis line.
///          2. Emit BASIC-specific guidance.
///          3. Append the intrinsic name list provided by the BASIC frontend.
void usage()
{
    std::cerr
        << "ilc v0.1.0\n"
        << "Usage: ilc -run <file.il> [--trace=il|src] [--stdin-from <file>] [--max-steps N]"
           " [--break label|file:line]* [--break-src file:line]* [--watch name]* [--bounds-checks] "
           "[--count] [--time]\n"
        << "       ilc front basic -emit-il <file.bas> [--bounds-checks]\n"
        << "       ilc front basic -run <file.bas> [--trace=il|src] [--stdin-from <file>] "
           "[--max-steps N] [--break label|file:line]* [--break-src file:line]* [--bounds-checks]\n"
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
/// @param argc Number of command-line arguments.
/// @param argv Array of argument strings.
/// @return Exit status of the selected subcommand or `1` on error.
/// @details The first argument determines which handler processes the request:
///          `cmdRunIL` executes `.il` programs, `cmdILOpt` performs optimization
///          passes, and `cmdFrontBasic` drives the BASIC front end. Step-by-step
///          summary:
///          1. Verify that at least one subcommand argument is provided.
///          2. Parse the subcommand token to determine the execution mode.
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
