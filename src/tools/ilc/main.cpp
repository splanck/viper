// File: src/tools/ilc/main.cpp
// Purpose: Dispatch to ilc subcommand handlers.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "tools/ilc/cli.hpp"

#include <iostream>
#include <string>

/// @brief Print usage information for ilc.
void usage()
{
    std::cerr << "ilc v0.1.0\n"
              << "Usage: ilc -run <file.il> [--trace] [--stdin-from <file>] [--max-steps N]"
                 " [--bounds-checks]\n"
              << "       ilc front basic -emit-il <file.bas> [--bounds-checks]\n"
              << "       ilc front basic -run <file.bas> [--trace] [--stdin-from <file>] "
                 "[--max-steps N] [--bounds-checks]\n"
              << "       ilc il-opt <in.il> -o <out.il> --passes p1,p2\n";
}

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
    if (cmd == "front" && argc >= 3)
    {
        std::string lang = argv[2];
        if (lang == "basic")
        {
            return cmdFrontBasic(argc - 3, argv + 3);
        }
    }
    if (cmd == "--help")
    {
        usage();
        return 0;
    }
    usage();
    return 1;
}
