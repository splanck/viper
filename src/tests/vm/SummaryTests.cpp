//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/SummaryTests.cpp
// Purpose: Verify VM prints execution summary with instruction count and time. 
// Key invariants: Summary line includes baked instruction count and time field.
// Ownership/Lifetime: Test creates temporary output file.
// Links: docs/testing.md
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
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
    if (!std::getline(out, line))
    {
        std::cerr << "no summary output\n";
        return 1;
    }
    std::regex re("^\\[SUMMARY\\] instr=3 time_ms=([0-9]+\\.[0-9]+)$");
    std::smatch m;
    if (!std::regex_match(line, m, re))
    {
        std::cerr << "unexpected summary: " << line << "\n";
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
