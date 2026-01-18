//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/DebugScriptTests.cpp
// Purpose: Validate scripted breakpoint control with step and continue.
// Key invariants: Exactly two IL trace lines appear between breakpoints; final output matches
// Ownership/Lifetime: To be documented.
// Links: docs/architecture.md
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
        std::cerr << "usage: DebugScriptTests <ilc> <il file> <script>\n";
        return 1;
    }
    std::string ilc = argv[1];
    std::string ilFile = argv[2];
    std::string script = argv[3];
    std::ifstream scriptIn(script);
    if (!scriptIn)
        return 1;
    std::string scriptCRLF = "debug_script_crlf.txt";
    std::ofstream scriptOut(scriptCRLF, std::ios::binary);
    if (!scriptOut)
        return 1;
    std::string scriptLine;
    while (std::getline(scriptIn, scriptLine))
    {
        scriptOut << "\t  " << scriptLine << "  \t\r\n";
    }
    scriptOut << " \t \t\r\n";
    scriptOut.close();
    std::string dbgOut = "dbg.out";
    std::string dbgErr = "dbg.err";
    std::string refOut = "ref.out";
    std::string cmd = ilc + " -run " + ilFile + " --step >step.out 2>step.err";
    int ret = std::system(cmd.c_str());
#ifdef _WIN32
    // Windows returns exit code directly
    if (ret != 10)
        return 1;
#else
    // POSIX returns status in format exit_code * 256
    if (ret != 10 * 256)
        return 1;
#endif
    std::remove("step.out");
    std::remove("step.err");
    cmd = ilc + " -run " + ilFile + " --trace=il --break L3 --debug-cmds " + scriptCRLF + " >" +
          dbgOut + " 2>" + dbgErr;
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
    std::string contOut = "cont.out";
    std::string contErr = "cont.err";
    cmd = ilc + " -run " + ilFile + " --break L3 --continue >" + contOut + " 2>" + contErr;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    std::ifstream contE(contErr);
    if (std::getline(contE, line))
        return 1;
    std::ifstream contO(contOut);
    std::ifstream refO2(refOut);
    while (std::getline(contO, d))
    {
        if (!std::getline(refO2, r) || d != r)
            return 1;
    }
    if (std::getline(refO2, r))
        return 1;
    std::remove(dbgOut.c_str());
    std::remove(dbgErr.c_str());
    std::remove(refOut.c_str());
    std::remove(contOut.c_str());
    std::remove(contErr.c_str());
    std::remove(scriptCRLF.c_str());
    return 0;
}
