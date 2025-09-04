// File: lib/VM/DebugScript.h
// Purpose: Parse debug command scripts for automated VM break handling.
// Key invariants: Unknown commands are ignored; actions returned in FIFO order.
// Ownership/Lifetime: Holds parsed actions only; does not own external resources.
// Links: docs/dev/vm.md
#pragma once

#include <cstdint>
#include <deque>
#include <string>

namespace il::vm
{

/// @brief Supported debug action types.
enum class DebugActionKind
{
    Continue, ///< Resume normal execution
    Step      ///< Step a number of instructions
};

/// @brief Parsed action from a debug script.
struct DebugAction
{
    DebugActionKind kind; ///< Action kind
    uint64_t count;       ///< Instruction count for stepping (unused for Continue)
};

/// @brief FIFO script of debug actions.
class DebugScript
{
  public:
    /// @brief Create an empty script.
    DebugScript() = default;

    /// @brief Load actions from script file @p path.
    explicit DebugScript(const std::string &path);

    /// @brief Retrieve next action; defaults to Continue when empty.
    DebugAction nextAction();

    /// @brief Prepend a step action to the script.
    void prependStep(uint64_t count);

    /// @brief Check if there are no pending actions.
    bool empty() const
    {
        return actions.empty();
    }

  private:
    std::deque<DebugAction> actions; ///< Pending actions
};

} // namespace il::vm
