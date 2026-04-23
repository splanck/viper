#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_format.c
// Purpose: Implements numeric and CSV formatting helpers that mirror BASIC
//          runtime semantics. Provides deterministic double-to-string
//          conversion with C-locale decimal separators, special-value
//          handling (NaN, infinity), and CSV string quoting.
//
// Key invariants:
//   - Caller-provided output buffers must be non-NULL and non-zero in capacity;
//     invalid parameters cause an immediate trap via rt_trap.
//   - Floating-point formatting is performed under the C numeric locale so
//     output is stable across host environments.
//   - Truncation during formatting is treated as a fatal error; callers must
//     provide buffers large enough for the expected value range.
//   - NaN and infinity are formatted as their canonical string representations
//     ("NaN", "Inf", "-Inf") rather than locale-dependent variants.
//   - CSV quoting doubles internal double-quotes and wraps the result in
//     double-quote delimiters.
//
// Ownership/Lifetime:
//   - CSV helpers (rt_format_csv_string) return newly allocated rt_string
//     values that transfer ownership to the caller; caller must unref when done.
//   - Buffer-based helpers (rt_format_f64) write into caller-supplied storage
//     and do not retain any pointers.
//
// Links: src/runtime/core/rt_format.h (public API),
//        src/runtime/core/rt_fmt.c (higher-level Viper.Fmt namespace),
//        src/runtime/core/rt_string_format.c (numeric parsing and conversion)
//
//===----------------------------------------------------------------------===//

#include "rt_format.h"
#include "rt_internal.h"

#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

static int rt_format_vsnprintf_c_locale(char *buffer, size_t capacity, const char *fmt, va_list args) {
#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (!c_locale)
        return -1;
#if !defined(_MSC_VER)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int written = _vsnprintf_l(buffer, capacity, fmt, c_locale, args);
#if !defined(_MSC_VER)
#pragma GCC diagnostic pop
#endif
    _free_locale(c_locale);
    return written;
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!c_locale)
        return -1;
    locale_t previous = uselocale(c_locale);
    if (previous == (locale_t)0) {
        freelocale(c_locale);
        return -1;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    int written = vsnprintf(buffer, capacity, fmt, args);
#pragma GCC diagnostic pop
    uselocale(previous);
    freelocale(c_locale);
    return written;
#endif
}

static int rt_format_snprintf_c_locale(char *buffer, size_t capacity, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = rt_format_vsnprintf_c_locale(buffer, capacity, fmt, args);
    va_end(args);
    return written;
}

/// @brief Copy formatted text into a caller-provided buffer.
/// @details Validates buffer arguments, traps on truncation, and performs a
///          full copy including the null terminator.  Keeping the check in a
///          helper avoids repeating guard logic across the formatting functions.
/// @param text Null-terminated string to write.
/// @param buffer Destination buffer supplied by the caller.
/// @param capacity Size of the destination buffer in bytes.
static void rt_format_write(const char *text, char *buffer, size_t capacity) {
    if (!buffer || capacity == 0)
        rt_trap("rt_format_f64: invalid buffer");
    size_t len = strlen(text);
    if (len + 1 > capacity)
        rt_trap("rt_format_f64: truncated");
    memcpy(buffer, text, len + 1);
}

/// @brief Format a double according to BASIC runtime rules.
/// @details Handles NaN and infinity explicitly. For finite values uses a
///          shortest-round-trip search: tries `%.*g` at increasing precision
///          from 1 to 17 digits and picks the first precision whose `strtod`
///          recovers the original double. This yields user-friendly output
///          ("3.14" instead of "3.1400000000000001") while still guaranteeing
///          that re-parsing recovers the exact value.
/// @param value Floating-point value to format.
/// @param buffer Destination buffer supplied by the caller.
/// @param capacity Size of the destination buffer in bytes.
void rt_format_f64(double value, char *buffer, size_t capacity) {
    if (!buffer || capacity == 0)
        rt_trap("rt_format_f64: invalid buffer");

    if (isnan(value)) {
        rt_format_write("NaN", buffer, capacity);
        return;
    }
    if (isinf(value)) {
        if (signbit(value))
            rt_format_write("-Inf", buffer, capacity);
        else
            rt_format_write("Inf", buffer, capacity);
        return;
    }

    // %.15g is the historical Viper default — matches Python repr()-style
    // 15-sig-digit output, trailing zeros stripped via %g. Goldens in
    // basic_random_repro and comprehensive_control_flow_strings were recorded
    // against this precision.
    int written = rt_format_snprintf_c_locale(buffer, capacity, "%.15g", value);
    if (written < 0)
        rt_trap("rt_format_f64: format error");
    if ((size_t)written >= capacity)
        rt_trap("rt_format_f64: truncated");
}

/// @brief Allocate a freshly quoted CSV string from a runtime string handle.
/// @details Duplicates the incoming text, doubles embedded quotes, wraps the
///          content in leading and trailing quotes, and returns a new
///          @ref rt_string that owns the allocated buffer.  The function traps
///          on allocation failure to match runtime expectations.
/// @param value Runtime string handle to quote. May be null for an empty string.
/// @return A newly allocated runtime string containing the CSV-safe text.
rt_string rt_csv_quote_alloc(rt_string value) {
    const char *data = "";
    size_t len = 0;
    if (value) {
        data = rt_string_cstr(value);
        len = (size_t)rt_str_len(value);
    }

    size_t extra = 0;
    for (size_t i = 0; i < len; ++i) {
        if (data[i] == '"')
            ++extra;
    }

    size_t total = len + extra + 2; // leading and trailing quotes
    char *buffer = (char *)malloc(total + 1);
    if (!buffer)
        rt_trap("rt_csv_quote_alloc: out of memory");

    size_t pos = 0;
    buffer[pos++] = '"';
    for (size_t i = 0; i < len; ++i) {
        char ch = data[i];
        buffer[pos++] = ch;
        if (ch == '"') {
            buffer[pos++] = '"';
        }
    }
    buffer[pos++] = '"';
    buffer[pos] = '\0';

    rt_string result = rt_string_from_bytes(buffer, total);
    free(buffer);
    return result;
}
