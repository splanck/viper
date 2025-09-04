// File: lib/VM/DebugScript.cpp
// Purpose: Implement parsing of debug command scripts for the VM.
// Key invariants: Unknown lines emit [DEBUG] messages and are skipped.
// Ownership/Lifetime: Reads from filesystem at construction; no further I/O.
// Links: docs/dev/vm.md
#include "VM/DebugScript.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace il::vm
{

DebugScript::DebugScript(const std::string &path)
{
    std::ifstream f(path);
    if (!f)
    {
        std::cerr << "[DEBUG] unable to open " << path << "\n";
        return;
    }
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty())
            continue;
        if (line == "continue")
        {
            actions.push({DebugActionKind::Continue, 0});
        }
        else if (line == "step")
        {
            actions.push({DebugActionKind::Step, 1});
        }
        else if (line.rfind("step ", 0) == 0)
        {
            std::istringstream iss(line.substr(5));
            uint64_t n = 0;
            if (iss >> n)
                actions.push({DebugActionKind::Step, n});
            else
                std::cerr << "[DEBUG] ignored: " << line << "\n";
        }
        else
        {
            std::cerr << "[DEBUG] ignored: " << line << "\n";
        }
    }
}

void DebugScript::addStep(uint64_t count)
{
    actions.push({DebugActionKind::Step, count});
}

DebugAction DebugScript::nextAction()
{
    if (actions.empty())
        return {DebugActionKind::Continue, 0};
    auto act = actions.front();
    actions.pop();
    return act;
}

bool DebugScript::empty() const
{
    return actions.empty();
}

} // namespace il::vm
