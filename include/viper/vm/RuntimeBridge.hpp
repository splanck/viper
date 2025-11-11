//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viper/vm/RuntimeBridge.hpp
// Purpose: Public-facing extern registration surface for the VM runtime bridge.
// Notes: See src/vm/RuntimeBridge.cpp for implementation details.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "il/runtime/signatures/Registry.hpp"

#include <string>
#include <string_view>

namespace il::vm
{

// Re-export signature type expected for extern declarations.
using Signature = il::runtime::signatures::Signature;

/// @brief Describe an externally provided runtime helper.
struct ExternDesc
{
    std::string name;   ///< Symbolic name used in IL (e.g., "rt_abs_i64").
    Signature signature; ///< Expected parameter and return kinds.
    void *fn = nullptr; ///< Function pointer matching the runtime handler ABI.
};

/// @brief Canonicalize a runtime helper name for registry lookups.
/// @details Lowercases ASCII letters and leaves other characters intact.
std::string canonicalizeExternName(std::string_view n);

class RuntimeBridge;

} // namespace il::vm

