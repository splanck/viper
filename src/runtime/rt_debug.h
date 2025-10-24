// File: src/runtime/rt_debug.h
// Purpose: Declares debug-friendly runtime printing helpers for IL tests.
// Key invariants: Helper output is line-oriented and flushed for deterministic tests.
// Ownership/Lifetime: Callers retain ownership of provided strings.
// Links: docs/codemap.md
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
