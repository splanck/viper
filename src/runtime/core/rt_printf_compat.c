//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_printf_compat.c
// Purpose: Provides weak-linked printf-style wrappers used throughout the
//          runtime for formatted output. The symbols carry weak linkage so
//          test harnesses can interpose custom implementations (e.g., to
//          capture or suppress output) without modifying production code.
//
// Key invariants:
//   - rt_snprintf forwards to vsnprintf; return semantics match vsnprintf
//     (returns number of characters that would have been written, negative
//     on encoding error).
//   - Weak linkage allows test binaries to override with strong symbols; the
//     default implementation is never called when a strong override is linked.
//   - The function is not thread-unsafe by itself; underlying vsnprintf is
//     reentrant, but stdout/stderr access is not serialized here.
//
// Ownership/Lifetime:
//   - Writes into a caller-supplied buffer; no heap allocation is performed.
//   - No state is retained between calls.
//
// Links: src/runtime/core/rt_printf_compat.h (public API),
//        src/runtime/core/rt_int_format.c (uses rt_snprintf),
//        src/runtime/core/rt_output.c (higher-level output buffering)
//
//===----------------------------------------------------------------------===//

#include "rt_printf_compat.h"
#include "rt_platform.h"

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
RT_WEAK int rt_snprintf(char *str, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(str, size, fmt, ap);
    va_end(ap);
    return written;
}
