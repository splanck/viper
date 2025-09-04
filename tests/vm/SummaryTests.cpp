// File: tests/vm/SummaryTests.cpp
// Purpose: Ensure VM reports instruction count and time summary.
// Key invariants: Instruction count matches expected value.
// Ownership/Lifetime: Test manages temporary files and cleans them up.
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
        std::cerr << "usage: SummaryTests <ilc> <il file>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string outFile = "summary.out";
    std::string cmd = ilc + " -run " + ilFile + " --count --time 2>" + outFile;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream out(outFile);
    std::string line;
    bool found = false;
    while (std::getline(out, line))
    {
        if (line.rfind("[SUMMARY]", 0) == 0)
        {
            if (line.find("instr=3") == std::string::npos)
                return 1;
            if (line.find("time_ms=") == std::string::npos)
                return 1;
            found = true;
        }
    }
    std::remove(outFile.c_str());
    return found ? 0 : 1;
}
