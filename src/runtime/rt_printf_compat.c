//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_printf_compat.c
// Purpose: Default definitions for printf-style wrappers used by the runtime.
//          Marked weak so tests can provide overrides without linker errors.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Default implementations for printf-compatible runtime wrappers.
/// @details The symbols are marked weak so test harnesses can provide stronger
///          overrides without modifying production code.

#include "rt_printf_compat.h"

#include <stdarg.h>
#include <stdio.h>

/// @brief snprintf-compatible formatting wrapper with weak linkage.
/// @details Forwards to `vsnprintf` using a varargs interface. Marked weak so
///          tests may interpose custom formatting behavior or capture output.
/// @param str Destination buffer.
/// @param size Size of the destination buffer in bytes.
/// @param fmt printf-style format string.
/// @return Number of characters that would have been written (excluding NUL),
///         or a negative value on encoding error, mirroring `vsnprintf`.
__attribute__((weak)) int rt_snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return written;
}
