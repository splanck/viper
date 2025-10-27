//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
// See LICENSE for license information.
//
// File: src/runtime/rt_debug.c
// Purpose: Implement debug printing helpers for deterministic IL tests.
// Key invariants: Functions flush stdout to ensure immediate visibility.
// Ownership/Lifetime: Borrowed strings remain owned by the caller.
// Links: docs/codemap.md
//
//===----------------------------------------------------------------------===//

#include "rt_debug.h"

#include <stdio.h>

/// @brief Print a 32-bit integer followed by a newline.
/// @param value Value to emit for diagnostic output.
void rt_println_i32(int32_t value)
{
    printf("%d\n", value);
    fflush(stdout);
}

/// @brief Print a UTF-8 string followed by a newline.
/// @details Null inputs are normalised to an empty string before printing so
///          callers can forward optional pointers without checks.
/// @param text Null-terminated string to print (may be null).
void rt_println_str(const char *text)
{
    if (!text)
        text = "";
    printf("%s\n", text);
    fflush(stdout);
}
