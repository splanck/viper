//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file
 * @brief Adapter between VM execution and the C runtime library.
 *
 * Declares the bridge used to invoke runtime helpers, manage trap diagnostics,
 * and register external functions callable from IL. The bridge supports both a
 * process-global extern registry and optional per-VM registries.
 *
 * @section registry Extern Registry Design
 * The extern registry maps external function names to their descriptors and
 * native implementations, enabling IL code to call host-provided functions.
 *
 * @par Resolution order
 * - The active VM's per-VM registry is consulted first when present
 * - The process-global registry is consulted next
 * - Built-in runtime descriptors are used only if no extern override matches
 *
 * @par Current implementation
 * - All process-global registrations are stored in a singleton registry
 * - A VM may also point at its own `ExternRegistry`
 * - Registration and lookup are mutex-protected per registry
 * - Functions registered via `registerExtern()` are visible process-wide unless
 *   a VM-specific registration with the same name overrides them
 *
 * This design supports both shared host integrations and per-VM embedding
 * scenarios without changing the extern call surface seen by IL code.
 */
//===----------------------------------------------------------------------===//

#pragma once

#include "rt.hpp"
#include "support/source_location.hpp"
#include "viper/vm/RuntimeBridge.hpp"
#include "vm/Trap.hpp"

#include <cstddef>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace il::runtime {
struct RuntimeDescriptor;
struct RuntimeSignature;
using RuntimeHandler = void (*)(void **args, void *result);
} // namespace il::runtime

namespace il::vm {

union Slot; // defined in VM.hpp

//===----------------------------------------------------------------------===//
// ExternRegistry - Abstraction for external function registration
//===----------------------------------------------------------------------===//

/**
 * @brief Opaque handle to an extern registry.
 *
 * This struct wraps the underlying storage for external function registrations.
 * Registries may be used process-wide or attached to individual VM instances.
 * The implementation stays opaque so registry storage, locking, and lifetime
 * management can evolve without changing the public API surface.
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
/// @details Returns the active VM's per-VM registry when one is attached,
///          otherwise falls back to the process-global registry.
/// @return Reference to the appropriate extern registry.
ExternRegistry &currentExternRegistry();

/// @brief Increment the lifetime reference count for @p registry.
/// @details Registries are shared across VMs and worker payloads. Callers that
///          store a raw registry pointer beyond the lifetime of an owning
///          ExternRegistryPtr must retain it explicitly.
void retainExternRegistry(ExternRegistry *registry);

/// @brief Release a previously retained extern registry reference.
/// @details Deletes the registry when the final retained reference is released.
void releaseExternRegistry(ExternRegistry *registry) noexcept;

/// @brief Register an external function in the specified registry.
/// @param registry Target registry (use `processGlobalExternRegistry()` for global).
/// @param ext Descriptor of the external function to register.
/// @return Success on successful registration. SignatureMismatch if strict mode
///         is enabled and a function with the same name but different signature
///         is already registered.
/// @note In non-strict mode (default), this always succeeds and overwrites any
///       existing registration with the same name (case-insensitive).
ExternRegisterResult registerExternIn(ExternRegistry &registry, const ExternDesc &ext);

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
struct RuntimeCallContext {
    il::support::SourceLoc loc{}; ///< Source location of the active runtime call.
    std::string function;         ///< Name of the calling function.
    std::string block;            ///< Label of the calling basic block.
    std::string message;          ///< Supplemental diagnostic message from runtime.
    const il::runtime::RuntimeDescriptor *descriptor =
        nullptr;              ///< Descriptor of active runtime helper.
    Slot *argBegin = nullptr; ///< Pointer to first argument slot for the active call.
    std::size_t argCount = 0; ///< Number of argument slots.
};

/// @brief Exception used to route runtime traps back into alternate executors.
/// @details Standard VM execution routes traps through @ref vm_raise. Bytecode
///          execution can temporarily install a thread-local interceptor so
///          runtime faults become BytecodeVM traps instead of aborting.
struct RuntimeTrapSignal : std::exception {
    TrapKind kind{};
    int32_t code = 0;
    std::string message;
    il::support::SourceLoc loc{};
    std::string function;
    std::string block;

    RuntimeTrapSignal(TrapKind trapKind,
                      int32_t trapCode,
                      std::string trapMessage,
                      il::support::SourceLoc trapLoc,
                      std::string trapFunction,
                      std::string trapBlock)
        : kind(trapKind), code(trapCode), message(std::move(trapMessage)), loc(trapLoc),
          function(std::move(trapFunction)), block(std::move(trapBlock)) {}

    const char *what() const noexcept override {
        return message.c_str();
    }
};

using RuntimeTrapInterceptor = void (*)(const RuntimeTrapSignal &signal, void *userData);

/// @brief RAII helper that installs a thread-local runtime trap interceptor.
class ScopedRuntimeTrapInterceptor {
  public:
    ScopedRuntimeTrapInterceptor(RuntimeTrapInterceptor interceptor, void *userData);
    ~ScopedRuntimeTrapInterceptor();

    ScopedRuntimeTrapInterceptor(const ScopedRuntimeTrapInterceptor &) = delete;
    ScopedRuntimeTrapInterceptor &operator=(const ScopedRuntimeTrapInterceptor &) = delete;

  private:
    RuntimeTrapInterceptor previousInterceptor_ = nullptr;
    void *previousUserData_ = nullptr;
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
class RuntimeBridge {
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
    /// @details The common path does not return, but tests and embedders may
    ///          override `vm_trap()` with an observer that records the trap and
    ///          continues execution.
    static void trap(TrapKind kind,
                     const std::string &msg,
                     const il::support::SourceLoc &loc,
                     const std::string &fn,
                     const std::string &block,
                     int32_t code = 0);

    /// @brief Access the runtime call context active on the current thread.
    /// @return Pointer to the call context when a runtime call is executing; nullptr otherwise.
    static const RuntimeCallContext *activeContext();

    /// @brief Indicate whether a VM instance is actively executing on this thread.
    static bool hasActiveVm();

    /// @brief Retrieve the per-VM extern registry for the active VM, if any.
    /// @return Pointer to the active VM's registry, or nullptr if no VM is active
    ///         or the active VM has no per-VM registry assigned.
    static ExternRegistry *activeVmRegistry();

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
