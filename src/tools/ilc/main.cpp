// File: src/tools/ilc/main.cpp
// Purpose: Dispatcher for ilc subcommands.
// Key invariants: None.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "cli.hpp"
#include <iostream>
#include <string>

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
        << "\n"
        << "       --break <label|file:line>  break at a block label or source line\n"
        << "       --break-src <file:line>    alias for --break file:line\n";
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
    if (cmd == "front" && argc >= 3 && std::string(argv[2]) == "basic")
    {
        return cmdFrontBasic(argc - 3, argv + 3);
    }
    usage();
    return 1;
}
