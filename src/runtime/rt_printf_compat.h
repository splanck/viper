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
//
//===----------------------------------------------------------------------===//

#ifndef RT_PRINTF_COMPAT_H
#define RT_PRINTF_COMPAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// What: snprintf-compatible formatting wrapper.
    /// Why:  Allow tests to interpose formatting and capture output.
    /// How:  Forwards to platform snprintf by default; may be overridden in tests.
    int rt_snprintf(char *str, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // RT_PRINTF_COMPAT_H
