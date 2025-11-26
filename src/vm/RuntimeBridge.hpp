//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/RuntimeBridge.hpp
// Purpose: Declares adapter between VM and runtime library.
// Key invariants: None.
// Ownership/Lifetime: VM owns the bridge.
// Links: docs/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt.hpp"
#include "support/source_location.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <span>

namespace il::runtime
{
struct RuntimeDescriptor;
}

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
    const il::runtime::RuntimeDescriptor *descriptor =
        nullptr;              ///< Descriptor of active runtime helper.
    Slot *argBegin = nullptr; ///< Pointer to first argument slot for the active call.
    std::size_t argCount = 0; ///< Number of argument slots.
};

/**
 * @brief Bridge between VM execution and native runtime functions.
 *
 * Provides a registry for external functions and manages the calling
 * convention between IL code and native C/C++ implementations.
 * All methods are static as this serves as a global registry.
 *
 * @invariant External functions must be registered before VM execution begins.
 * @invariant Runtime calls maintain a thread-local context stack.
 */
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
                     std::string_view name,
                     std::span<const Slot> args,
                     const il::support::SourceLoc &loc,
                     const std::string &fn,
                     const std::string &block);

    // Backward-compatible overload accepting std::vector to avoid callers copying into spans.
    static Slot call(RuntimeCallContext &ctx,
                     std::string_view name,
                     const std::vector<Slot> &args,
                     const il::support::SourceLoc &loc,
                     const std::string &fn,
                     const std::string &block);

    // Convenience overload: initializer-list of Slots
    static Slot call(RuntimeCallContext &ctx,
                     std::string_view name,
                     std::initializer_list<Slot> args,
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

    /// @brief Indicate whether a VM instance is actively executing on this thread.
    static bool hasActiveVm();

    // Runtime extern registry -------------------------------------------------
    static void registerExtern(const ExternDesc &);
    static bool unregisterExtern(std::string_view name);
    static const ExternDesc *findExtern(std::string_view name);
};

} // namespace il::vm
