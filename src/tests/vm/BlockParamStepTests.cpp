//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/BlockParamStepTests.cpp
// Purpose: Ensure block parameters transfer correctly while stepping through a call. 
// Key invariants: Scripted stepping still yields callee arguments and prints a step break.
// Ownership/Lifetime: Test creates temporary stderr capture file and deletes it.
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
        std::cerr << "usage: BlockParamStepTests <ilc> <il file> <script>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string script = argv[3];
    std::string errFile = "block_param_step.err";
    std::string cmd =
        ilc + " -run " + ilFile + " --break entry --debug-cmds " + script + " 2>" + errFile;
    int rc = std::system(cmd.c_str());
    if (rc != 7 * 256)
    {
        std::cerr << "unexpected exit status: " << rc << "\n";
        std::remove(errFile.c_str());
        return 1;
    }
    std::ifstream err(errFile);
    std::string line;
    bool sawStep = false;
    while (std::getline(err, line))
    {
        if (line.rfind("[BREAK]", 0) == 0 && line.find("reason=step") != std::string::npos)
        {
            sawStep = true;
            break;
        }
    }
    std::remove(errFile.c_str());
    if (!sawStep)
    {
        std::cerr << "missing step break output\n";
        return 1;
    }
    return 0;
}
