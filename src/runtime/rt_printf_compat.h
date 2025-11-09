//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the MIT License.
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
extern "C" {
#endif

int rt_snprintf(char *str, size_t size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // RT_PRINTF_COMPAT_H

