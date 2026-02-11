//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_debug.h
// Purpose: Debug-oriented printing helpers providing deterministic, line-
//          oriented output of i32 and C-string values for IL golden tests
//          and runtime diagnostics.
// Key invariants: Each call appends a newline and flushes immediately; output
//                 is locale-independent and platform-deterministic; NULL
//                 strings are treated as empty.
// Ownership/Lifetime: Functions accept plain values or C pointers with no
//                     ownership transfer; no runtime string objects are used.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Print a signed 32-bit integer followed by a newline.
    /// @param value Value to print in decimal.
    void rt_println_i32(int32_t value);

    /// @brief Print a C string followed by a newline.
    /// @param text Null-terminated string; treated as empty when null.
    void rt_println_str(const char *text);

#ifdef __cplusplus
}
#endif
