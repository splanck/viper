//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: vm/VMConstants.hpp
// Purpose: Centralized constants for VM configuration and limits
// Key invariants: All constants are compile-time evaluable
// Ownership/Lifetime: Static constants with program lifetime
// Links: docs/vm-design.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>

namespace il::vm
{

/// @brief Default operand stack size per frame in bytes.
/// @details Sized for typical alloca usage in BASIC programs.
///          1KB accommodates temporary strings and small arrays.
constexpr size_t kDefaultFrameStackSize = 1024;

/// @brief Maximum recursion depth for interpreter.
/// @details Prevents stack overflow from unbounded recursion.
///          This limit is conservative and can be adjusted based on
///          platform stack size and profiling data.
constexpr size_t kMaxRecursionDepth = 1000;

/// @brief Maximum number of instructions to execute before interrupt check.
/// @details Balances responsiveness vs overhead. Can be overridden via
///          VIPER_INTERRUPT_EVERY_N environment variable.
constexpr uint64_t kDefaultInterruptCheckInterval = 10000;

/// @brief Initial capacity hint for function map.
/// @details Most modules have < 100 functions. Using power-of-2 size
///          reduces hash collisions.
constexpr size_t kFunctionMapInitialCapacity = 128;

/// @brief Initial capacity hint for string literal cache.
/// @details Most programs have < 200 unique string literals.
constexpr size_t kStringCacheInitialCapacity = 256;

/// @brief Maximum arguments for optimized marshalling.
/// @details Runtime calls with <= this many args use stack allocation.
///          Larger argument lists fall back to heap allocation.
constexpr size_t kMaxStackAllocatedArgs = 8;

/// @brief Minimum function size for switch cache optimization.
/// @details Small functions don't benefit from switch dispatch caching.
constexpr size_t kMinFunctionSizeForSwitchCache = 5;

} // namespace il::vm
