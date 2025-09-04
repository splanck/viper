// File: tests/vm/BreakLabelTests.cpp
// Purpose: Verify VM halts at block label breakpoints.
// Key invariants: Breakpoint message emitted exactly once.
// Ownership/Lifetime: Test owns temporary files.
// Links: docs/testing.md
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
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream out(outFile);
    std::string line;
    if (!std::getline(out, line) || line != "[BREAK] fn=@main blk=L3 reason=label")
        return 1;
    if (std::getline(out, line))
        return 1;
    std::remove(outFile.c_str());
    return 0;
}
