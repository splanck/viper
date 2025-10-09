//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the tiny script loader used by the VM debugger.  Scripts are text
// files containing imperative commands such as "continue" or "step N" which are
// fed to the debugger to automate interactive sessions.
//
//===----------------------------------------------------------------------===//
#include "vm/DebugScript.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace il::vm
{
/// @brief Construct a script by loading actions from a command file.
///
/// Lines containing "continue" enqueue a Continue action; lines with "step" or
/// "step N" enqueue a Step action with the requested instruction count.  Unknown
/// commands emit a `[DEBUG]` message and are ignored so partially written scripts
/// remain usable.
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

/// @brief Queue a step action for @p count instructions.
///
/// Useful for programmatic tooling that wants to append commands after loading
/// a script file.
void DebugScript::addStep(uint64_t count)
{
    actions.push({DebugActionKind::Step, count});
}

/// @brief Retrieve the next queued action.
///
/// If no actions remain, a Continue action is returned to resume normal
/// execution. Otherwise, the next action is removed from the queue and
/// returned to the caller.
DebugAction DebugScript::nextAction()
{
    if (actions.empty())
        return {DebugActionKind::Continue, 0};
    auto act = actions.front();
    actions.pop();
    return act;
}

/// @brief Determine whether all actions have been consumed.
///
/// @return True if no actions remain in the queue, false otherwise.
bool DebugScript::empty() const
{
    return actions.empty();
}

} // namespace il::vm
