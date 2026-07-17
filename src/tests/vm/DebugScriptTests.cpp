//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/vm/DebugScriptTests.cpp
// Purpose: Validate scripted breakpoint control with step and continue.
// Key invariants: Exactly two IL trace lines appear between breakpoints; final output matches
// Ownership/Lifetime: To be documented.
// Links: docs/internals/architecture.md
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::vector<std::string> readBreakLines(const std::string &path) {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("[BREAK]", 0) == 0)
            lines.push_back(line);
    }
    return lines;
}

std::string readWholeFile(const std::string &path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool writeStepModeProgram(const std::string &path) {
    std::ofstream out(path);
    if (!out)
        return false;
    out << "il 0.1\n"
        << "func @helper() -> i64 {\n"
        << "entry:\n"
        << "  br H0\n"
        << "H0:\n"
        << "  %h0 = iadd.ovf 40, 2\n"
        << "  ret %h0\n"
        << "}\n"
        << "func @main() -> i64 {\n"
        << "entry:\n"
        << "  br M0\n"
        << "M0:\n"
        << "  %m0 = iadd.ovf 1, 2\n"
        << "  %m1 = call @helper()\n"
        << "  %m2 = iadd.ovf %m1, %m0\n"
        << "  ret 0\n"
        << "}\n";
    return true;
}

bool writeScript(const std::string &path, const std::vector<std::string> &lines) {
    std::ofstream out(path);
    if (!out)
        return false;
    for (const std::string &line : lines)
        out << line << '\n';
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 4) {
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
    while (std::getline(scriptIn, scriptLine)) {
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
    while (std::getline(err, line)) {
        if (line.rfind("[BREAK]", 0) == 0)
            break;
    }
    if (line != "[BREAK] fn=@main blk=L3 reason=label")
        return 1;
    int ilLines = 0;
    while (std::getline(err, line)) {
        if (line.rfind("[IL]", 0) == 0)
            ++ilLines;
        else if (line.rfind("[BREAK]", 0) == 0) {
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
    while (std::getline(dbgO, d)) {
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
    while (std::getline(contO, d)) {
        if (!std::getline(refO2, r) || d != r)
            return 1;
    }
    if (std::getline(refO2, r))
        return 1;

    std::string stepModesIl = "debug_step_modes.il";
    std::string stepOverScript = "debug_step_over.txt";
    std::string stepOutScript = "debug_step_out.txt";
    std::string stepModesOut = "debug_step_modes.out";
    std::string stepOverErr = "debug_step_over.err";
    std::string stepOutErr = "debug_step_out.err";
    if (!writeStepModeProgram(stepModesIl) ||
        !writeScript(stepOverScript, {"step", "step-over", "continue"}) ||
        !writeScript(stepOutScript, {"step-out", "continue"}))
        return 1;

    cmd = ilc + " -run " + stepModesIl + " --trace=il --break M0 --debug-cmds " + stepOverScript +
          " >" + stepModesOut + " 2>" + stepOverErr;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    const auto stepOverBreaks = readBreakLines(stepOverErr);
    if (stepOverBreaks.size() < 3)
        return 1;
    if (stepOverBreaks[0] != "[BREAK] fn=@main blk=M0 reason=label")
        return 1;
    if (stepOverBreaks[1] != "[BREAK] fn=@main blk=M0 reason=step")
        return 1;
    if (stepOverBreaks[2] != "[BREAK] fn=@main blk=M0 reason=step-over")
        return 1;
    const std::string stepOverText = readWholeFile(stepOverErr);
    const auto helperTrace = stepOverText.find("[IL] fn=@helper blk=H0");
    const auto stepOverBreak = stepOverText.find("[BREAK] fn=@main blk=M0 reason=step-over");
    if (helperTrace == std::string::npos || stepOverBreak == std::string::npos ||
        helperTrace > stepOverBreak)
        return 1;

    cmd = ilc + " -run " + stepModesIl + " --trace=il --break H0 --debug-cmds " + stepOutScript +
          " >" + stepModesOut + " 2>" + stepOutErr;
    if (std::system(cmd.c_str()) != 0)
        return 1;
    const auto stepOutBreaks = readBreakLines(stepOutErr);
    if (stepOutBreaks.size() < 2)
        return 1;
    if (stepOutBreaks[0] != "[BREAK] fn=@helper blk=H0 reason=label")
        return 1;
    if (stepOutBreaks[1] != "[BREAK] fn=@main blk=M0 reason=step-out")
        return 1;

    std::remove(dbgOut.c_str());
    std::remove(dbgErr.c_str());
    std::remove(refOut.c_str());
    std::remove(contOut.c_str());
    std::remove(contErr.c_str());
    std::remove(scriptCRLF.c_str());
    std::remove(stepModesIl.c_str());
    std::remove(stepOverScript.c_str());
    std::remove(stepOutScript.c_str());
    std::remove(stepModesOut.c_str());
    std::remove(stepOverErr.c_str());
    std::remove(stepOutErr.c_str());
    return 0;
}
