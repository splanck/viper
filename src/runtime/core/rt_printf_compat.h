//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_printf_compat.h
// Purpose: Provide overridable wrappers around libc printf-family functions so
//          tests can interpose behaviour portably across platforms.
// Key invariants: Default implementation forwards to libc; tests may define a
//                 strong symbol to override.
// Ownership: Runtime C API header.
// Lifetime: Stateless function; no managed resources.
// Links: rt_string_builder.h
//
//===----------------------------------------------------------------------===//

#ifndef RT_PRINTF_COMPAT_H
#define RT_PRINTF_COMPAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief snprintf-compatible formatting wrapper.
    /// @details Forwards to the platform's snprintf by default. Tests may define
    ///          a strong symbol to override this function and capture or redirect
    ///          formatted output portably across platforms.
    /// @param str  Destination buffer to write formatted output into.
    /// @param size Maximum number of bytes to write (including NUL terminator).
    /// @param fmt  printf-style format string.
    /// @param ...  Variadic arguments corresponding to format specifiers.
    /// @return Number of characters that would have been written (excluding NUL),
    ///         or a negative value on encoding error (same semantics as snprintf).
    int rt_snprintf(char *str, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // RT_PRINTF_COMPAT_H
