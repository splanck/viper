// File: src/vm/RuntimeBridge.hpp
// Purpose: Declares adapter between VM and runtime library.
// Key invariants: None.
// Ownership/Lifetime: VM owns the bridge.
// Links: docs/il-guide.md#reference
#pragma once

#include "rt.hpp"
#include "support/source_location.hpp"
#include "vm/Trap.hpp"
#include <string>
#include <vector>

namespace il::vm
{

union Slot; // defined in VM.hpp

/// @brief Stores runtime call metadata for trap diagnostics.
struct RuntimeCallContext
{
    il::support::SourceLoc loc{}; ///< Source location of the active runtime call.
    std::string function;         ///< Name of the calling function.
    std::string block;            ///< Label of the calling basic block.
    std::string message;          ///< Supplemental diagnostic message from runtime.
};

/// @brief Provides entry points from the VM into the C runtime library.
class RuntimeBridge
{
  public:
    /// @brief Invoke runtime function @p name with arguments @p args.
    /// @param name Runtime function symbol.
    /// @param args Evaluated argument slots.
    /// @param loc Source location of call instruction.
    /// @param fn Calling function name.
    /// @param block Calling block label.
    /// @return Result slot from runtime call.
    static Slot call(RuntimeCallContext &ctx,
                     const std::string &name,
                     const std::vector<Slot> &args,
                     const il::support::SourceLoc &loc,
                     const std::string &fn,
                     const std::string &block);

    /// @brief Report a trap with source location @p loc within function @p fn and
    /// block @p block.
    static void trap(TrapKind kind,
                     const std::string &msg,
                     const il::support::SourceLoc &loc,
                     const std::string &fn,
                     const std::string &block);

    /// @brief Access the runtime call context active on the current thread.
    /// @return Pointer to the call context when a runtime call is executing; nullptr otherwise.
    static const RuntimeCallContext *activeContext();
};

} // namespace il::vm
