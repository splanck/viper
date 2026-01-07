//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Defines the minimal debug-printing surface used by the IL test harnesses and
// integration tools.  The helpers write to stdout using the platform C runtime
// and flush immediately so deterministic traces are available even when the
// process terminates abruptly.  Centralising these entry points keeps the
// low-level runtime ABI stable while still allowing embedders to override the
// behaviour when necessary.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Runtime helpers that print debug traces to stdout.
/// @details Exposes thin wrappers that normalise optional inputs and flush the
///          C stdio buffers eagerly.  The functions are used by diagnostics and
///          golden tests to produce deterministic textual output.

#include "rt_debug.h"

#include <stdio.h>

/// @brief Print a 32-bit integer followed by a newline.
/// @details Emits the decimal encoding of @p value with @c printf before forcing
///          a flush so the textual trace is observable even if the program
///          crashes immediately afterwards.  The function is intentionally tiny
///          to keep runtime dependencies minimal.
/// @param value Value to emit for diagnostic output.
void rt_println_i32(int32_t value)
{
    printf("%d\n", value);
    fflush(stdout);
}

/// @brief Print a UTF-8 string followed by a newline.
/// @details Treats @p text as an optional pointer, normalising @c NULL to an
///          empty string so callers are spared from performing defensive checks.
///          Output is flushed immediately to keep debugger tooling responsive
///          and deterministic.
/// @param text Null-terminated string to print (may be null).
void rt_println_str(const char *text)
{
    if (!text)
        text = "";
    printf("%s\n", text);
    fflush(stdout);
}
