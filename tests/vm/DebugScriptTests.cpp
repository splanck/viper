// File: tests/vm/DebugScriptTests.cpp
// Purpose: Validate scripted breakpoint control with step and continue.
// Key invariants: Exactly two IL trace lines appear between breakpoints; final output matches
// normal run. Ownership/Lifetime: Test creates and removes temporary files. Links: docs/testing.md
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: DebugScriptTests <ilc> <il file> <script>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string script = argv[3];
    std::string dbgOut = "dbg.out";
    std::string dbgErr = "dbg.err";
    std::string refOut = "ref.out";
    std::string cmd = ilc + " -run " + ilFile + " --trace=il --break L3 --debug-cmds " + script +
                      " >" + dbgOut + " 2>" + dbgErr;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream err(dbgErr);
    std::string line;
    while (std::getline(err, line))
    {
        if (line.rfind("[BREAK]", 0) == 0)
            break;
    }
    if (line != "[BREAK] fn=@main blk=L3 reason=label")
        return 1;
    int ilLines = 0;
    while (std::getline(err, line))
    {
        if (line.rfind("[IL]", 0) == 0)
            ++ilLines;
        else if (line.rfind("[BREAK]", 0) == 0)
        {
            if (line != "[BREAK] fn=@main blk=L3 reason=step")
                return 1;
            break;
        }
    }
    if (ilLines != 2)
        return 1;
    cmd = ilc + " -run " + ilFile + " >" + refOut;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream dbgO(dbgOut);
    std::ifstream refO(refOut);
    std::string d, r;
    while (std::getline(dbgO, d))
    {
        if (!std::getline(refO, r) || d != r)
            return 1;
    }
    if (std::getline(refO, r))
        return 1;
    std::remove(dbgOut.c_str());
    std::remove(dbgErr.c_str());
    std::remove(refOut.c_str());
    return 0;
}
