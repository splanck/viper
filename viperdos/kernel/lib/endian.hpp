//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "../include/types.hpp"

/// @file endian.hpp
/// @brief Byte-swap helpers for big-endian / little-endian conversion.
///
/// @details
/// AArch64 is little-endian; these convert to/from big-endian for hardware
/// protocols that use network byte order (fw_cfg, ramfb, virtio legacy).

namespace lib {

/// @brief Swap a 16-bit value between host and big-endian order.
constexpr u16 be16(u16 v) { return __builtin_bswap16(v); }

/// @brief Swap a 32-bit value between host and big-endian order.
constexpr u32 be32(u32 v) { return __builtin_bswap32(v); }

/// @brief Swap a 64-bit value between host and big-endian order.
constexpr u64 be64(u64 v) { return __builtin_bswap64(v); }

/// @brief Convert a 32-bit value from host order to big-endian.
constexpr u32 cpu_to_be32(u32 v) { return be32(v); }

/// @brief Convert a 64-bit value from host order to big-endian.
constexpr u64 cpu_to_be64(u64 v) { return be64(v); }

} // namespace lib
