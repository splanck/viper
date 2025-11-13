//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// This file declares debug-oriented printing helpers designed for IL test programs
// and runtime diagnostics. Unlike the full BASIC PRINT statement implementation,
// these functions provide minimal, deterministic output suitable for automated
// test verification and debugging.
//
// Viper's IL testing infrastructure requires reproducible output across platforms
// and execution environments. The standard library's printf family has subtle
// platform differences in formatting, buffering, and locale handling. These helpers
// provide a controlled interface with guaranteed line-oriented output and
// immediate flushing.
//
// Key Design Properties:
// - Line-oriented output: Each function appends a newline and flushes immediately
// - Deterministic formatting: No locale dependencies or platform-specific quirks
// - Minimal dependencies: Uses only standard C output, no runtime string objects
// - Test-friendly: Output appears in predictable order even with mixed stdout/stderr
//
// These functions are primarily used by IL golden tests that verify runtime behavior
// by comparing actual output against expected text files. They're not intended for
// production BASIC programs, which use the full PRINT statement lowering.
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
