//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_printf_compat.h
// Purpose: Overridable wrapper around libc snprintf that allows test code to interpose formatting
// behavior portably across platforms without modifying production code.
//
// Key invariants:
//   - Default implementation forwards directly to the platform's snprintf.
//   - Test code may define a strong symbol rt_snprintf to override behavior.
//   - Return value semantics match C99 snprintf (would-write count or negative on error).
//   - The weak symbol pattern works on ELF and Mach-O; MSVC requires different approach.
//
// Ownership/Lifetime:
//   - No heap allocation; output is written into a caller-supplied buffer.
//   - No ownership transfer; the function is purely a formatting utility.
//
// Links: src/runtime/core/rt_printf_compat.c (implementation), src/runtime/core/rt_string_builder.h
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
