//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_atomic_compat.h
// Purpose: Provides GCC/Clang __atomic_* builtin compatibility for MSVC.
//          Maps __atomic_load_n, __atomic_store_n, __atomic_compare_exchange_n,
//          __atomic_fetch_add, and __atomic_fetch_sub to MSVC Interlocked
//          intrinsics via C11 _Generic dispatch.
//
// Key invariants:
//   - On GCC/Clang this header is a no-op (builtins are native).
//   - On MSVC, dispatches to 32-bit or 64-bit intrinsics based on operand size.
//   - Memory ordering constants (__ATOMIC_RELAXED, etc.) are defined but MSVC
//     intrinsics provide full barrier semantics regardless of ordering.
//
// Ownership/Lifetime:
//   - Header-only, no state.
//
// Links: src/runtime/core/rt_crc32.c, rt_output.c, rt_gc.c (consumers)
//
//===----------------------------------------------------------------------===//

#pragma once

#ifdef _MSC_VER

#include "../rt_platform.h"

#endif /* _MSC_VER */
