//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: include/viperdos/types.hpp
// Purpose: Shared fixed-width type aliases for kernel and user-space.
// Key invariants: Freestanding; no stdlib required.
// Ownership/Lifetime: Header-only; type definitions.
// Links: kernel/include/types.hpp (kernel wrapper)
//
//===----------------------------------------------------------------------===//

/**
 * @file types.hpp
 * @brief Shared fixed-width type aliases for ViperDOS kernel and user-space.
 *
 * @details
 * This header provides the fundamental fixed-width integer and size aliases
 * used throughout ViperDOS. It is designed for freestanding environments
 * and does not require the C/C++ standard library.
 *
 * Both kernel and user-space code include this header to ensure type
 * consistency across the ABI boundary.
 *
 * The aliases mirror common low-level systems conventions:
 * - `u*` for unsigned integers, `i*` for signed integers
 * - `usize`/`isize` for sizes and signed sizes
 * - `uintptr` for holding pointer values as integers
 */

#pragma once

/// @brief Unsigned 8-bit integer.
using u8 = unsigned char;
/// @brief Unsigned 16-bit integer.
using u16 = unsigned short;
/// @brief Unsigned 32-bit integer.
using u32 = unsigned int;
/// @brief Unsigned 64-bit integer.
using u64 = unsigned long long;

/// @brief Signed 8-bit integer.
using i8 = signed char;
/// @brief Signed 16-bit integer.
using i16 = signed short;
/// @brief Signed 32-bit integer.
using i32 = signed int;
/// @brief Signed 64-bit integer.
using i64 = signed long long;

/// @brief Unsigned size type used for byte counts and lengths.
using usize = u64;
/// @brief Signed size type used where negative sizes have meaning.
using isize = i64;

/// @brief Integer type capable of holding a pointer value (64-bit).
using uintptr = u64;
