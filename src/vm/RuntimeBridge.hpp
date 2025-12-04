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
//
// EXTERN REGISTRY DESIGN
// ======================
//
// The extern registry maps external function names to their descriptors and
// native implementations. This enables IL code to call host-provided functions.
//
// CURRENT IMPLEMENTATION (Process-Global):
// ----------------------------------------
// The extern registry is currently **process-global**:
//
//   - All VM instances in the process share the same registry.
//   - Registration and lookup are protected by a single mutex.
//   - Functions registered via `registerExtern()` are visible to all VMs.
//
// This design is acceptable for the current CLI/single-tenant model where:
//   - One VM executes at a time, or
//   - Multiple VMs need access to the same set of external functions.
//
// FUTURE CONSIDERATION (Per-VM Scoping):
// --------------------------------------
// For multi-tenant embedding scenarios, per-VM extern scoping may be desirable:
//
//   - Each VM instance could have its own ExternRegistry.
//   - Host functions could be selectively exposed per tenant.
//   - Sandboxing and isolation would be improved.
//
// The `ExternRegistry` abstraction below is designed to facilitate this future
// refactoring. Currently, `processGlobalExternRegistry()` returns the singleton;
// a future version could return a per-VM registry from the active VM.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt.hpp"
#include "support/source_location.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace il::runtime
{
struct RuntimeDescriptor;
struct RuntimeSignature;
using RuntimeHandler = void (*)(void **args, void *result);
} // namespace il::runtime

namespace il::vm
{

union Slot; // defined in VM.hpp

//===----------------------------------------------------------------------===//
// ExternRegistry - Abstraction for external function registration
//===----------------------------------------------------------------------===//

/**
 * @brief Opaque handle to an extern registry.
 *
 * This struct wraps the underlying storage for external function registrations.
 * It is designed as an abstraction layer to facilitate future per-VM scoping
 * without changing the API surface.
 *
 * ## Current Behavior
 *
 * There is a single process-global registry accessed via
 * `processGlobalExternRegistry()`. All VMs in the process share this registry.
 *
 * ## Future Extension
 *
 * To support per-VM extern scoping:
 * 1. Add an `ExternRegistry*` member to the VM class.
 * 2. Modify `currentExternRegistry()` to return the active VM's registry.
 * 3. Provide APIs to create/destroy per-VM registries.
 *
 * @note This struct intentionally hides its implementation details. Use the
 *       free functions below to interact with registries.
 */
struct ExternRegistry;

/// @brief Access the process-global extern registry singleton.
/// @details This registry is shared by all VM instances in the process.
///          It is lazily initialized on first access and persists for the
///          lifetime of the process.
/// @return Reference to the process-global extern registry.
/// @note Thread-safe: the registry uses internal synchronization.
ExternRegistry &processGlobalExternRegistry();

/// @brief Access the extern registry for the current context.
/// @details Currently returns the process-global registry. In a future
///          per-VM scoping implementation, this would return the registry
///          associated with the active VM (if any), falling back to the
///          process-global registry.
/// @return Reference to the appropriate extern registry.
ExternRegistry &currentExternRegistry();

/// @brief Register an external function in the specified registry.
/// @param registry Target registry (use `processGlobalExternRegistry()` for global).
/// @param ext Descriptor of the external function to register.
/// @note Overwrites any existing registration with the same name (case-insensitive).
void registerExternIn(ExternRegistry &registry, const ExternDesc &ext);

/// @brief Unregister an external function from the specified registry.
/// @param registry Target registry.
/// @param name Name of the external function to unregister (case-insensitive).
/// @return True if a function was unregistered; false if not found.
bool unregisterExternIn(ExternRegistry &registry, std::string_view name);

/// @brief Find an external function descriptor in the specified registry.
/// @param registry Target registry.
/// @param name Name of the external function to find (case-insensitive).
/// @return Pointer to the descriptor if found; nullptr otherwise.
const ExternDesc *findExternIn(ExternRegistry &registry, std::string_view name);

/// @brief Resolve an external function for invocation.
/// @param registry Target registry.
/// @param name Name of the external function (case-insensitive).
/// @param[out] outSig Receives the runtime signature if found.
/// @param[out] outHandler Receives the native handler if found.
/// @return Pointer to the public descriptor if found; nullptr otherwise.
const ExternDesc *resolveExternIn(ExternRegistry &registry,
                                  std::string_view name,
                                  il::runtime::RuntimeSignature *outSig,
                                  il::runtime::RuntimeHandler *outHandler);

//===----------------------------------------------------------------------===//
// RuntimeCallContext
//===----------------------------------------------------------------------===//

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
 * ## Extern Registry
 *
 * The extern registry methods (`registerExtern`, `unregisterExtern`, `findExtern`)
 * operate on the **process-global** extern registry. All VM instances in the
 * process share this registry. For explicit registry control, use the free
 * functions `registerExternIn()`, `unregisterExternIn()`, and `findExternIn()`
 * with `processGlobalExternRegistry()` or a future per-VM registry.
 *
 * @invariant External functions must be registered before VM execution begins.
 * @invariant Runtime calls maintain a thread-local context stack.
 *
 * @see ExternRegistry for the underlying abstraction.
 * @see processGlobalExternRegistry() for explicit global registry access.
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

    //=========================================================================
    // Extern Registry (Process-Global)
    //=========================================================================
    // These methods operate on the process-global extern registry shared by
    // all VM instances. For explicit registry control, use the free functions
    // registerExternIn(), unregisterExternIn(), findExternIn() instead.

    /// @brief Register an external function in the process-global registry.
    /// @param ext Descriptor of the external function to register.
    /// @note Equivalent to `registerExternIn(processGlobalExternRegistry(), ext)`.
    static void registerExtern(const ExternDesc &ext);

    /// @brief Unregister an external function from the process-global registry.
    /// @param name Name of the function to unregister (case-insensitive).
    /// @return True if a function was unregistered; false if not found.
    /// @note Equivalent to `unregisterExternIn(processGlobalExternRegistry(), name)`.
    static bool unregisterExtern(std::string_view name);

    /// @brief Find an external function in the process-global registry.
    /// @param name Name of the function to find (case-insensitive).
    /// @return Pointer to the descriptor if found; nullptr otherwise.
    /// @note Equivalent to `findExternIn(processGlobalExternRegistry(), name)`.
    static const ExternDesc *findExtern(std::string_view name);
};

} // namespace il::vm
