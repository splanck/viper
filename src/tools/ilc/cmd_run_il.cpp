// File: src/tools/ilc/cmd_run_il.cpp
// Purpose: Handle `ilc -run <file.il>` invocation.
// Key invariants: IL file must parse and verify before execution.
// Ownership/Lifetime: Tool owns loaded modules.
// Links: docs/class-catalog.md

#include "tools/ilc/cli.hpp"

#include "il/io/Parser.hpp"
#include "il/verify/Verifier.hpp"
#include "vm/VM.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

using namespace il;

extern void usage();

/// @brief Run an IL module through the VM.
int cmdRunIL(int argc, char **argv)
{
    if (argc < 1)
    {
        usage();
        return 1;
    }
    bool trace = false;
    std::string ilFile = argv[0];
    std::string stdinPath;
    uint64_t maxSteps = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--trace")
        {
            trace = true;
        }
        else if (arg == "--stdin-from" && i + 1 < argc)
        {
            stdinPath = argv[++i];
        }
        else if (arg == "--max-steps" && i + 1 < argc)
        {
            maxSteps = std::stoull(argv[++i]);
        }
        else if (arg == "--bounds-checks")
        {
        }
        else
        {
            usage();
            return 1;
        }
    }
    std::ifstream ifs(ilFile);
    if (!ifs)
    {
        std::cerr << "unable to open " << ilFile << "\n";
        return 1;
    }
    core::Module m;
    if (!io::Parser::parse(ifs, m, std::cerr))
        return 1;
    if (!verify::Verifier::verify(m, std::cerr))
        return 1;
    if (!stdinPath.empty())
    {
        if (!freopen(stdinPath.c_str(), "r", stdin))
        {
            std::cerr << "unable to open stdin file\n";
            return 1;
        }
    }
    vm::VM vm(m, trace, maxSteps);
    return static_cast<int>(vm.run());
}
