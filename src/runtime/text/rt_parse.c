//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_parse.c
// Purpose: Implements safe parsing utility functions for the Viper.Parse
//          namespace. Provides TryParseInt, TryParseLong, TryParseFloat,
//          TryParseBool, TryParseDate, and related functions that return
//          false instead of trapping on invalid input.
//
// Key invariants:
//   - All TryParse* functions return false on invalid input; they never trap.
//   - NULL output pointers cause immediate false return without side effects.
//   - Empty strings are treated as invalid for all types.
//   - Integer overflow causes false return; the output is not written.
//   - Floating-point parsing uses the current locale's decimal separator.
//   - Bool parsing accepts "true"/"false" case-insensitively.
//
// Ownership/Lifetime:
//   - All functions are purely computational; no heap allocations or retained
//     state exist between calls.
//
// Links: src/runtime/text/rt_parse.h (public API),
//        src/runtime/text/rt_scanner.h (lower-level character scanning)
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt_parse.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Advance a pointer past ASCII whitespace characters.
static inline const char *skip_whitespace(const char *s) {
    while (*s && isspace((unsigned char)*s))
        ++s;
    return s;
}

/// @brief Check if string has only trailing whitespace after cursor.
static inline int is_end_of_input(const char *s) {
    s = skip_whitespace(s);
    return *s == '\0';
}

/// @brief Case-insensitive string comparison.
static inline int str_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

/// @brief Parse a string as a 64-bit decimal integer; on success writes to `*out_value` and
/// returns 1. Strict validation: rejects empty input, non-numeric trailing characters, and
/// overflow (ERANGE). Leading/trailing whitespace is tolerated. Never traps — designed for
/// caller-supplied "did the parse succeed?" branching rather than exceptions.
int8_t rt_parse_try_int(rt_string s, int64_t *out_value) {
    if (!out_value)
        return 0;

    const char *text = rt_string_cstr(s);
    if (!text)
        return 0;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return 0;

    errno = 0;
    char *endptr = NULL;
    long long parsed = strtoll(cursor, &endptr, 10);

    if (errno == ERANGE)
        return 0;
    if (!endptr || endptr == cursor)
        return 0;
    if (!is_end_of_input(endptr))
        return 0;

    *out_value = (int64_t)parsed;
    return 1;
}

/// @brief Parse a string as a double-precision float. **Locale-isolated:** temporarily switches
/// the LC_NUMERIC locale to "C" so the decimal separator is always `.` regardless of the user's
/// system locale (avoids the classic French/German `1,5` parse failure). Rejects NaN/Inf via
/// `isfinite`. On Win32 uses `_strtod_l` (per-call locale); on POSIX uses `uselocale` thread-local.
int8_t rt_parse_try_num(rt_string s, double *out_value) {
    if (!out_value)
        return 0;

    const char *text = rt_string_cstr(s);
    if (!text)
        return 0;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return 0;

    errno = 0;
    char *endptr = NULL;
    double value = 0.0;

#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (!c_locale)
        return 0;
    value = _strtod_l(cursor, &endptr, c_locale);
    _free_locale(c_locale);
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!c_locale)
        return 0;
    locale_t previous = uselocale(c_locale);
    value = strtod(cursor, &endptr);
    uselocale(previous);
    freelocale(c_locale);
#endif

    if (errno == ERANGE || !isfinite(value))
        return 0;
    if (!endptr || endptr == cursor)
        return 0;
    if (!is_end_of_input(endptr))
        return 0;

    *out_value = value;
    return 1;
}

/// @brief Parse a string as a boolean. Accepts case-insensitively: true/yes/1/on → true,
/// false/no/0/off → false. Anything else fails. Tolerates leading/trailing whitespace; multiple
/// words are rejected (so "true today" doesn't parse as true).
int8_t rt_parse_try_bool(rt_string s, int8_t *out_value) {
    if (!out_value)
        return 0;

    const char *text = rt_string_cstr(s);
    if (!text)
        return 0;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return 0;

    // Extract the word (until whitespace or end)
    char word[16];
    size_t i = 0;
    while (*cursor && !isspace((unsigned char)*cursor) && i < sizeof(word) - 1)
        word[i++] = *cursor++;
    word[i] = '\0';

    // Check for trailing non-whitespace
    if (!is_end_of_input(cursor))
        return 0;

    // Check for true values
    if (str_eq_ci(word, "true") || str_eq_ci(word, "yes") || str_eq_ci(word, "1") ||
        str_eq_ci(word, "on")) {
        *out_value = 1;
        return 1;
    }

    // Check for false values
    if (str_eq_ci(word, "false") || str_eq_ci(word, "no") || str_eq_ci(word, "0") ||
        str_eq_ci(word, "off")) {
        *out_value = 0;
        return 1;
    }

    return 0;
}

/// @brief Parse-or-default convenience for integers. Equivalent to `try_int(s, &x) ? x : default`.
int64_t rt_parse_int_or(rt_string s, int64_t default_value) {
    int64_t result;
    if (rt_parse_try_int(s, &result))
        return result;
    return default_value;
}

/// @brief Parse-or-default convenience for doubles.
double rt_parse_num_or(rt_string s, double default_value) {
    double result;
    if (rt_parse_try_num(s, &result))
        return result;
    return default_value;
}

/// @brief Parse-or-default convenience for booleans.
int8_t rt_parse_bool_or(rt_string s, int8_t default_value) {
    int8_t result;
    if (rt_parse_try_bool(s, &result))
        return result;
    return default_value;
}

/// @brief Returns 1 if `s` parses as a valid integer (no value extracted). Equivalent to a
/// `try_int` call that discards the result — handy for input validation gates.
int8_t rt_parse_is_int(rt_string s) {
    int64_t dummy;
    return rt_parse_try_int(s, &dummy);
}

/// @brief Returns 1 if `s` parses as a valid double (locale-isolated, like `try_num`).
int8_t rt_parse_is_num(rt_string s) {
    double dummy;
    return rt_parse_try_num(s, &dummy);
}

/// @brief Parse an integer in any radix from 2 to 36 (covers binary, octal, decimal, hex, base32,
/// base36 for short URLs). Returns `default_value` for out-of-range radix or any parse failure.
/// Like the standard `strtoll`, alphabetic digits beyond 9 use `a-z`/`A-Z` (case-insensitive).
int64_t rt_parse_int_radix(rt_string s, int64_t radix, int64_t default_value) {
    // Validate radix range
    if (radix < 2 || radix > 36)
        return default_value;

    const char *text = rt_string_cstr(s);
    if (!text)
        return default_value;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return default_value;

    errno = 0;
    char *endptr = NULL;
    long long parsed = strtoll(cursor, &endptr, (int)radix);

    if (errno == ERANGE)
        return default_value;
    if (!endptr || endptr == cursor)
        return default_value;
    if (!is_end_of_input(endptr))
        return default_value;

    return (int64_t)parsed;
}

#ifdef __cplusplus
}
#endif
