// File: src/il/runtime/RuntimeSignatures.hpp
// Purpose: Declares the shared registry of runtime helper signatures.
// Key invariants: Entries mirror the runtime C ABI and remain stable for consumers.
// Ownership/Lifetime: Registry data lives for the duration of the process.
// Links: docs/il-spec.md
#pragma once

#include "il/core/Type.hpp"
#include <string_view>
#include <unordered_map>
#include <vector>

namespace il::runtime
{

/// @brief Describes the IL signature for a runtime helper function.
/// @notes Parameter order matches the runtime C ABI.
struct RuntimeSignature
{
    il::core::Type retType;                  ///< Return type of the helper.
    std::vector<il::core::Type> paramTypes;  ///< Parameter types in declaration order.
};

/// @brief Access the registry of runtime signatures keyed by symbol name.
/// @return Mapping from runtime symbol to its IL signature metadata.
const std::unordered_map<std::string_view, RuntimeSignature> &runtimeSignatures();

/// @brief Look up the signature for a runtime helper if it exists.
/// @param name Runtime symbol name, e.g., "rt_print_str".
/// @return Pointer to the signature when registered; nullptr otherwise.
const RuntimeSignature *findRuntimeSignature(std::string_view name);

} // namespace il::runtime
