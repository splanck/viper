//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/TraceILTests.cpp
// Purpose: Verify IL tracing emits deterministic lines and disables by default. 
// Key invariants: Trace output matches golden file exactly.
// Ownership/Lifetime: Test owns temporary files and cleans them up.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: TraceILTests <ilc> <il file> <golden>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string golden = argv[3];
    std::string outFile = "trace.out";
    std::string cmd = ilc + " -run " + ilFile + " --trace=il 2>" + outFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream out(outFile);
    std::ifstream gold(golden);
    std::string o, g;
    while (std::getline(out, o))
    {
        if (!std::getline(gold, g) || o != g)
        {
            std::cerr << "trace mismatch\n";
            return 1;
        }
    }
    if (std::getline(gold, g))
    {
        std::cerr << "golden has extra lines\n";
        return 1;
    }
    std::string noneFile = "none.out";
    cmd = ilc + " -run " + ilFile + " 2>" + noneFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream none(noneFile);
    if (none.peek() != EOF)
    {
        std::cerr << "trace emitted without flag\n";
        return 1;
    }
    std::remove(outFile.c_str());
    std::remove(noneFile.c_str());
    return 0;
}
