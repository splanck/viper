//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/RuntimeBridge.hpp
// Purpose: Public-facing extern registration surface for the VM runtime bridge.
//          Supports process-global and per-VM extern registries with optional
//          strict mode for signature mismatch detection.
// Key invariants: The process-global registry is mutex-protected. Per-VM
//                 registries rely on the VM's single-threaded execution model.
//                 In strict mode, re-registration with a different signature
//                 returns SignatureMismatch instead of silently overwriting.
// Ownership/Lifetime: ExternRegistryPtr owns registry storage via custom deleter.
//                     Per-VM registries must outlive their VM instances.
// Links: src/vm/RuntimeBridge.cpp, docs/codemap/vm-runtime.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/runtime/signatures/Registry.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace il::vm
{

// Re-export signature type expected for extern declarations.
using Signature = il::runtime::signatures::Signature;

/// @brief Describe an externally provided runtime helper.
struct ExternDesc
{
    std::string name;    ///< Symbolic name used in IL (e.g., "rt_abs_i64").
    Signature signature; ///< Expected parameter and return kinds.
    void *fn = nullptr;  ///< Function pointer matching the runtime handler ABI.
};

/// @brief Canonicalize a runtime helper name for registry lookups.
/// @details Lowercases ASCII letters and leaves other characters intact.
std::string canonicalizeExternName(std::string_view n);

//===----------------------------------------------------------------------===//
// ExternRegistry API
//===----------------------------------------------------------------------===//

/// @brief Result codes for extern registration operations.
enum class ExternRegisterResult
{
    Success,          ///< Registration succeeded.
    SignatureMismatch ///< Strict mode: name exists with different signature.
};

/// @brief Opaque handle to an extern function registry.
/// @details Holds registered external functions for resolution during IL execution.
///          The struct definition is in RuntimeBridge.cpp to keep internals private.
struct ExternRegistry;

/// @brief Custom deleter for ExternRegistry unique_ptr.
struct ExternRegistryDeleter
{
    void operator()(ExternRegistry *reg) const noexcept;
};

/// @brief Owning handle to an extern registry.
using ExternRegistryPtr = std::unique_ptr<ExternRegistry, ExternRegistryDeleter>;

/// @brief Create a new empty extern registry.
/// @details The returned registry is independent of the process-global registry
///          and can be assigned to a VM for isolated extern resolution.
/// @return Owning pointer to the newly created registry.
[[nodiscard]] ExternRegistryPtr createExternRegistry();

/// @brief Access the process-global extern registry singleton.
/// @return Reference to the global registry shared by all VMs without a per-VM registry.
ExternRegistry &processGlobalExternRegistry();

/// @brief Register an external function in a specific registry.
/// @param registry Target registry (per-VM or global).
/// @param ext Descriptor for the external function.
/// @return Success on successful registration. SignatureMismatch if strict mode
///         is enabled and a function with the same name but different signature
///         is already registered.
/// @note In non-strict mode (default), this always succeeds and overwrites any
///       existing registration with the same name.
ExternRegisterResult registerExternIn(ExternRegistry &registry, const ExternDesc &ext);

/// @brief Enable or disable strict mode for an extern registry.
/// @details In strict mode, re-registering an extern name with a different
///          signature returns SignatureMismatch instead of silently overwriting.
///          Strict mode is disabled by default for backward compatibility.
/// @param registry Target registry.
/// @param enabled True to enable strict mode, false to disable.
void setExternRegistryStrictMode(ExternRegistry &registry, bool enabled);

/// @brief Query whether strict mode is enabled for a registry.
/// @param registry Target registry.
/// @return True if strict mode is enabled, false otherwise.
bool isExternRegistryStrictMode(const ExternRegistry &registry);

/// @brief Unregister an external function from a specific registry.
/// @param registry Target registry.
/// @param name Canonical name of the function to remove.
/// @return True if a function was removed, false if not found.
bool unregisterExternIn(ExternRegistry &registry, std::string_view name);

/// @brief Look up an external function in a specific registry.
/// @param registry Target registry.
/// @param name Canonical name of the function.
/// @return Pointer to the descriptor if found, nullptr otherwise.
const ExternDesc *findExternIn(ExternRegistry &registry, std::string_view name);

class RuntimeBridge;

} // namespace il::vm
