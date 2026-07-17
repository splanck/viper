//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_numeric.c
// Purpose: Locale-independent numeric parsing and formatting helpers for the
//   BASIC runtime. Provides string-to-number (rt_parse_double, rt_parse_i64)
//   and number-to-string (rt_format_double, rt_format_i64) routines that
//   recognise BASIC-style tokens ("NaN", "INF", "#INF", "#NAN") and apply
//   banker's rounding (round-half-to-even) for decimal formatting. Domain
//   errors are reported via rt_trap so VM and native backends behave identically.
//
// Key invariants:
//   - All conversions ignore the process locale; decimal point is always '.'.
//   - "NaN", "#NAN", "INF", "#INF", "-INF" are recognised case-insensitively.
//   - rt_parse_double returns NaN for unrecognised input (no trap).
//   - rt_format_double applies banker's rounding (IEEE-754 round-half-to-even).
//   - All functions are exposed with C linkage (extern "C").
//
// Ownership/Lifetime:
//   - No persistent heap allocation. Formatting uses caller-provided buffers
//     or returns rt_string objects whose lifetime follows normal GC rules.
//
// Links: src/runtime/core/rt_numeric.h (public API),
//        src/runtime/core/rt_trap.h (rt_trap for domain errors)
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt_numeric.h"
#include "rt.hpp"

#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Determine whether a character represents an ASCII digit.
/// @details BASIC numeric parsing only accepts ASCII digits for integral
///          components.  This helper deliberately ignores locale-specific
///          digits so conversions remain deterministic across platforms.
static int rt_is_digit_char(char ch) {
    return ch >= '0' && ch <= '9';
}

/// @brief Determine whether a byte is ASCII whitespace.
/// @details Keeps numeric parsing independent of the active process locale.
static int rt_is_ascii_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

/// @brief Convert an ASCII letter to lowercase without invoking locale APIs.
/// @details The runtime uses ASCII-only case folding when matching special
///          constants such as "INF".  Using a bespoke helper avoids linking
///          against locale facilities that could diverge between hosts.
static int rt_ascii_tolower(int ch) {
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 'a';
    return ch;
}

/// @brief Compare @p start against @p token case-insensitively.
/// @details Iterates over @p token and ensures each byte in @p start matches
///          after ASCII case folding.  When the token matches, @p out_end is
///          updated to point immediately past the consumed characters.
static bool rt_match_token_ci(const char *start, const char *token, const char **out_end) {
    if (!start || !token)
        return false;

    size_t index = 0;
    while (token[index] != '\0') {
        const unsigned char ch = (unsigned char)start[index];
        if (ch == '\0')
            return false;
        if (rt_ascii_tolower((int)ch) != token[index])
            return false;
        ++index;
    }

    if (out_end)
        *out_end = start + index;
    return true;
}

/// @brief Parse textual "NaN"/"INF" constants accepted by BASIC.
/// @details Handles optional sign prefixes and both the short and long
///          infinity spellings.  When a token matches, the corresponding IEEE
///          value is written to @p out_value and the consumed span returned
///          through @p out_end.
static bool rt_parse_special_constant(const char *start, char **out_end, double *out_value) {
    if (!start || !out_value)
        return false;

    const char *cursor = start;
    bool is_negative = false;
    if (*cursor == '+' || *cursor == '-') {
        is_negative = (*cursor == '-');
        ++cursor;
    }

    const char *matched_end = NULL;
    if (rt_match_token_ci(cursor, "nan", &matched_end)) {
        cursor = matched_end;
        double value = nan("");
        if (is_negative)
            value = -value;
        if (out_end)
            *out_end = (char *)(uintptr_t)cursor;
        *out_value = value;
        return true;
    }

    if (rt_match_token_ci(cursor, "inf", &matched_end)) {
        cursor = matched_end;
        const char *extended = NULL;
        if (rt_match_token_ci(cursor, "inity", &extended))
            cursor = extended;

        double value = is_negative ? -INFINITY : INFINITY;
        if (out_end)
            *out_end = (char *)(uintptr_t)cursor;
        *out_value = value;
        return true;
    }

    return false;
}

#if defined(_WIN32)
/// @brief Parse a double using the C locale on Windows.
/// @details Materialises a per-call `_locale_t`, calls `_strtod_l`, and then
///          tears the locale down. Per-call allocation avoids unsynchronised
///          lazy global state during first-use races.
static bool rt_strtod_c_locale(const char *input, char **out_end, double *out_value) {
    if (!input || !out_value)
        return false;

    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (!c_locale)
        return false;

    errno = 0;
    char *endptr = NULL;
    double value = _strtod_l(input, &endptr, c_locale);
    _free_locale(c_locale);

    if (endptr == input)
        return false;

    if (out_end)
        *out_end = endptr;
    *out_value = value;
    return true;
}
#else
/// @brief Parse a double using the C locale on POSIX platforms.
/// @details Creates a transient locale object with `newlocale`, installs it
///          with @c uselocale for the duration of the conversion, and then
///          restores the previous locale.  Mirrors the Windows behaviour so
///          callers can rely on consistent locale-independent parsing.
static bool rt_strtod_c_locale(const char *input, char **out_end, double *out_value) {
    if (!input || !out_value)
        return false;

    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
    if (!c_locale)
        return false;

    locale_t previous = uselocale(c_locale);
    if (previous == (locale_t)0) {
        freelocale(c_locale);
        return false;
    }
    errno = 0;
    char *endptr = NULL;
    double value = strtod(input, &endptr);
    uselocale(previous);
    freelocale(c_locale);

    if (endptr == input)
        return false;

    if (out_end)
        *out_end = endptr;
    *out_value = value;
    return true;
}
#endif

/// @brief Return the end of a strict decimal floating literal.
/// @details Accepts `[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`.
///          Hexadecimal C float syntax is intentionally rejected.
static const unsigned char *rt_scan_decimal_float(const unsigned char *cursor) {
    if (!cursor)
        return NULL;

    const unsigned char *p = cursor;
    if (*p == '+' || *p == '-')
        ++p;

    int digits = 0;
    while (rt_is_digit_char((char)*p)) {
        ++digits;
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (rt_is_digit_char((char)*p)) {
            ++digits;
            ++p;
        }
    }

    if (digits == 0)
        return NULL;

    if (*p == 'e' || *p == 'E') {
        const unsigned char *exp = p + 1;
        if (*exp == '+' || *exp == '-')
            ++exp;
        int exp_digits = 0;
        while (rt_is_digit_char((char)*exp)) {
            ++exp_digits;
            ++exp;
        }
        if (exp_digits == 0)
            return NULL;
        p = exp;
    }

    return p;
}

/// @brief Convert a BASIC numeric literal into a double value.
/// @details Skips leading whitespace, recognises special constants (INF/NAN)
///          in a case-insensitive fashion, and validates that the remaining
///          characters form a locale-independent floating literal.  The
///          result is produced using the C locale so commas are rejected as
///          decimal separators.  Failures set @p ok to false and return
///          either zero or the offending special value.
double rt_val_to_double(const char *s, bool *ok) {
    if (!ok) {
        rt_trap("rt_val_to_double: null ok");
        return 0.0;
    }
    if (!s) {
        *ok = false;
        rt_trap("rt_val_to_double: null string");
        return 0.0;
    }

    const unsigned char *p = (const unsigned char *)s;
    while (*p && rt_is_ascii_space(*p))
        ++p;

    const char *parse = (const char *)p;
    if (*parse == '\0') {
        *ok = false;
        return 0.0;
    }

    double special_value = 0.0;
    if (rt_parse_special_constant(parse, NULL, &special_value)) {
        *ok = false;
        return special_value;
    }

    const unsigned char *literal_end = rt_scan_decimal_float((const unsigned char *)parse);
    if (!literal_end) {
        *ok = false;
        return 0.0;
    }

    const unsigned char *literal_tail = literal_end;
    while (*literal_tail != '\0' && rt_is_ascii_space(*literal_tail))
        ++literal_tail;
    if (*literal_tail != '\0') {
        *ok = false;
        return 0.0;
    }

    char *end = NULL;
    double value = 0.0;
    if (!rt_strtod_c_locale(parse, &end, &value)) {
        *ok = false;
        return 0.0;
    }

    if ((const unsigned char *)end != literal_end) {
        *ok = false;
        return 0.0;
    }

    if (!isfinite(value)) {
        *ok = false;
        return value;
    }

    *ok = true;
    return value;
}

/// @brief Locale-isolated `vsnprintf` that always uses the C locale's numeric formatting.
/// @details Mirror of `rt_format_vsnprintf_c_locale` for the numeric-conversion path —
///          guarantees deterministic decimal points and digit grouping regardless of the
///          process's `LC_NUMERIC` setting. Win32 uses `_create_locale` + `_vsnprintf_l`;
///          POSIX uses `newlocale` + `uselocale` + `vsnprintf`. Returns -1 on locale
///          acquisition failure so callers can fall back to a deterministic default.
static int rt_vsnprintf_c_locale(char *out, size_t cap, const char *fmt, va_list args) {
#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (!c_locale)
        return -1;
#if !defined(_MSC_VER)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int written = _vsnprintf_l(out, cap, fmt, c_locale, args);
#if !defined(_MSC_VER)
#pragma GCC diagnostic pop
#endif
    _free_locale(c_locale);
    return written;
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", NULL);
    if (!c_locale)
        return -1;
    locale_t previous = uselocale(c_locale);
    if (previous == (locale_t)0) {
        freelocale(c_locale);
        return -1;
    }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
    int written = vsnprintf(out, cap, fmt, args);
#pragma GCC diagnostic pop
    uselocale(previous);
    freelocale(c_locale);
    return written;
#endif
}

/// @brief Format a floating-point or integer value into a caller buffer.
/// @details Invokes `vsnprintf` with the provided format string, trapping on
///          null buffers, formatting failures, or truncation.  A dedicated
///          helper keeps the exported formatting functions concise.
static void rt_format(char *out, size_t cap, const char *fmt, ...) {
    if (!out || cap == 0) {
        rt_trap("rt_format: invalid buffer");
        return;
    }

    va_list args;
    va_start(args, fmt);
#if !defined(_MSC_VER)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    int written = rt_vsnprintf_c_locale(out, cap, fmt, args);
#if !defined(_MSC_VER)
#pragma GCC diagnostic pop
#endif
    va_end(args);

    if (written < 0) {
        rt_trap("rt_format: format error");
        return;
    }
    if ((size_t)written >= cap) {
        rt_trap("rt_format: truncated");
        return;
    }
}

/// @brief Serialise a double to text using BASIC's precision rules.
/// @details Formats the value with 17 significant digits so that round-trips
///          through text preserve 64-bit precision.  Any optional error slot
///          receives the canonical success sentinel.
void rt_str_from_double(double x, char *out, size_t cap, RtError *out_err) {
    rt_format(out, cap, "%.17g", x);
    if (out_err)
        *out_err = RT_ERROR_NONE;
}

/// @brief Serialise a float to text using BASIC's precision rules.
/// @details Casts to double and prints nine significant digits, matching the
///          runtime's single-precision formatting expectations.  Signals
///          success through @p out_err when provided.
void rt_str_from_float(float x, char *out, size_t cap, RtError *out_err) {
    rt_format(out, cap, "%.9g", (double)x);
    if (out_err)
        *out_err = RT_ERROR_NONE;
}

/// @brief Format a 32-bit signed integer using the C locale.
/// @details Delegates to @ref rt_format to trap on invalid buffers and
///          writes the textual representation into @p out.  Consumers receive
///          `RT_ERROR_NONE` when formatting succeeds.
void rt_str_from_i32(int32_t x, char *out, size_t cap, RtError *out_err) {
    rt_format(out, cap, "%" PRId32, x);
    if (out_err)
        *out_err = RT_ERROR_NONE;
}

/// @brief Format a 16-bit signed integer into a caller buffer.
/// @details Uses @ref rt_format to apply bounds checking and populate the
///          provided buffer with a decimal representation.
void rt_str_from_i16(int16_t x, char *out, size_t cap, RtError *out_err) {
    rt_format(out, cap, "%" PRId16, x);
    if (out_err)
        *out_err = RT_ERROR_NONE;
}

#ifdef __cplusplus
}
#endif
