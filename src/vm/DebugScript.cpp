//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/vm/DebugScript.cpp
// Purpose: Implement the queue-based script loader that drives the interactive
//          VM debugger.
// Links: docs/runtime-vm.md#debugger
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Parses debugger command scripts into queued actions.
/// @details Keeps I/O and parsing logic out of the header so the debugger can
///          include lightweight declarations while the implementation handles
///          error messaging and command expansion.
#include "vm/DebugScript.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace il::vm
{
/// @brief Construct a script by loading actions from a command file.
/// @details Reads the file line-by-line, interpreting recognised commands into
///          queued actions.  "continue" lines enqueue a Continue action while
///          "step" and "step N" emit Step actions with appropriate counts.
///          Unknown lines emit `[DEBUG]` messages to stderr but do not abort
///          parsing, allowing iterative script development.
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
        while (!line.empty() && (line.back() == '\r' || line.back() == '\f' || line.back() == '\v'))
        {
            line.pop_back();
        }
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
/// @details Allows tooling to append scripted actions after construction without
///          re-reading a file.  Actions are enqueued in FIFO order so appended
///          steps execute after any previously loaded commands.
void DebugScript::addStep(uint64_t count)
{
    actions.push({DebugActionKind::Step, count});
}

/// @brief Retrieve the next queued action.
/// @details Pops the next action from the queue, defaulting to a Continue action
///          when the queue is empty.  Returning a Continue action in the empty
///          case lets the debugger resume execution naturally without checking
///          for null results.
DebugAction DebugScript::nextAction()
{
    if (actions.empty())
        return {DebugActionKind::Continue, 0};
    auto act = actions.front();
    actions.pop();
    return act;
}

/// @brief Determine whether all actions have been consumed.
/// @details Exposes the queue emptiness predicate so driver code can decide when
///          to re-read scripts or fall back to interactive mode.
bool DebugScript::empty() const
{
    return actions.empty();
}

} // namespace il::vm
