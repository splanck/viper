//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/BreakLabelTests.cpp
// Purpose: Verify that label breakpoints halt execution before block entry.
// Key invariants: VM prints a single BREAK line and executes no block instructions.
// Ownership/Lifetime: Test creates temporary output file.
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
    if (argc != 3)
    {
        std::cerr << "usage: BreakLabelTests <ilc> <il file>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string outFile = "break.out";
    std::string cmd = ilc + " -run " + ilFile + " --break L3 2>" + outFile;
    if (std::system(cmd.c_str()) != 10 * 256)
        return 1;
    std::ifstream out(outFile);
    std::string line;
    if (!std::getline(out, line))
    {
        std::cerr << "no break output\n";
        return 1;
    }
    if (line != "[BREAK] fn=@main blk=L3 reason=label")
    {
        std::cerr << "unexpected break line: " << line << "\n";
        return 1;
    }
    if (std::getline(out, line))
    {
        std::cerr << "extra output\n";
        return 1;
    }
    std::remove(outFile.c_str());
    return 0;
}
