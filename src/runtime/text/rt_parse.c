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
//   - NULL output pointers cause immediate false return.
//   - Non-NULL output pointers are always reset before parsing so failed parses
//     cannot leak a caller's previous value.
//   - Empty strings are treated as invalid for all types.
//   - Integer overflow causes false return; the output is not written.
//   - Floating-point parsing uses the C locale's decimal separator.
//   - Embedded NUL bytes are rejected so hidden suffixes cannot be ignored.
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
#include "rt_internal.h"
#include "rt_option.h"

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

/// @brief Return true for the fixed ASCII whitespace set, independent of locale.
static inline int is_ascii_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' ||
           ch == '\v';
}

/// @brief Advance a pointer past ASCII whitespace characters.
static inline const char *skip_whitespace(const char *s) {
    while (*s && is_ascii_space((unsigned char)*s))
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
        unsigned char ca = (unsigned char)*a;
        unsigned char cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
        ++a;
        ++b;
    }
    return *a == *b;
}

/// @brief Decode an ASCII digit character to its integer value, supporting bases up to 36.
/// @details Accepts decimal digits (`0-9` → 0-9), lowercase letters (`a-z` → 10-35), and
///          uppercase letters (`A-Z` → 10-35). Returns -1 for any other byte so callers
///          can detect "non-digit" without ambiguity. Used by `rt_parse_int_radix`.
static inline int radix_digit_value(unsigned char ch) {
    if (ch >= '0' && ch <= '9')
        return (int)(ch - '0');
    if (ch >= 'a' && ch <= 'z')
        return (int)(ch - 'a') + 10;
    if (ch >= 'A' && ch <= 'Z')
        return (int)(ch - 'A') + 10;
    return -1;
}

/// @brief Return @p s's byte buffer if it's a live handle with no embedded NUL, else NULL.
/// @details Combines handle validation with the embedded-NUL rejection rule: TryParse
///          callers want false (not a trap) on bad input, but they must not silently
///          accept a string whose declared length exceeds the first NUL — that would
///          let a hidden suffix bypass the parser. NULL handles, invalid handles, and
///          NUL-containing strings all map to NULL so the caller short-circuits.
static const char *string_cstr_without_embedded_nul(rt_string s) {
    if (!s || !rt_string_is_handle((const void *)s) || !s->data)
        return NULL;

    const char *text = s->data;
    size_t len = (size_t)rt_str_len(s);
    if (memchr(text, '\0', len))
        return NULL;

    return text;
}

/// @brief Walk past a well-formed decimal float at @p cursor, returning the byte after it.
/// @details Recognises the grammar `[+|-]?([0-9]+('.'[0-9]*)?|'.'[0-9]+)
///          ([eE][+|-]?[0-9]+)?`.
///          Returns NULL when no recognisable number is found at the cursor (zero
///          mantissa digits, or a missing exponent body after `e`/`E`). Otherwise
///          returns a pointer one byte past the last consumed character — callers use
///          this for trailing-suffix validation (`Try*` parsers reject any non-empty
///          tail to keep the contract strict).
static const char *scan_decimal_float(const char *cursor) {
    if (!cursor)
        return NULL;

    const char *p = cursor;
    if (*p == '+' || *p == '-')
        ++p;

    int digits = 0;
    while (*p >= '0' && *p <= '9') {
        ++digits;
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++digits;
            ++p;
        }
    }

    if (digits == 0)
        return NULL;

    if (*p == 'e' || *p == 'E') {
        const char *exp = p + 1;
        if (*exp == '+' || *exp == '-')
            ++exp;
        int exp_digits = 0;
        while (*exp >= '0' && *exp <= '9') {
            ++exp_digits;
            ++exp;
        }
        if (exp_digits == 0)
            return NULL;
        p = exp;
    }

    return p;
}

/// @brief Recognize canonical non-finite floating literals.
/// @details Accepts the formatter's `NaN`, `Inf`, and `-Inf` spellings plus a
///          leading `+` and case-insensitive input so Convert.ToString_Double
///          and Parse/Convert.ToDouble round-trip through the public APIs.
static int scan_nonfinite_float(const char *cursor, double *out_value, const char **out_end) {
    if (!cursor || !out_value || !out_end)
        return 0;
    const char *p = cursor;
    int negative = 0;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        ++p;
    }
    if ((p[0] == 'n' || p[0] == 'N') && (p[1] == 'a' || p[1] == 'A') &&
        (p[2] == 'n' || p[2] == 'N')) {
        *out_value = NAN;
        *out_end = p + 3;
        return 1;
    }
    if ((p[0] == 'i' || p[0] == 'I') && (p[1] == 'n' || p[1] == 'N') &&
        (p[2] == 'f' || p[2] == 'F')) {
        *out_value = negative ? -INFINITY : INFINITY;
        *out_end = p + 3;
        return 1;
    }
    return 0;
}

/// @brief Parse a string as a 64-bit decimal integer; on success writes to `*out_value` and
/// returns 1. Strict validation: rejects empty input, non-numeric trailing characters, and
/// overflow (ERANGE). Leading/trailing whitespace is tolerated. Never traps — designed for
/// caller-supplied "did the parse succeed?" branching rather than exceptions.
int8_t rt_parse_try_int(rt_string s, int64_t *out_value) {
    if (!out_value)
        return 0;
    *out_value = 0;
    if (!s)
        return 0;

    const char *text = string_cstr_without_embedded_nul(s);
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
/// system locale (avoids the classic French/German `1,5` parse failure). Accepts explicit
/// NaN/Inf spellings, while decimal overflow and non-finite decimal results fail. On Win32 uses
/// `_strtod_l` (per-call locale); on POSIX uses `uselocale` thread-local.
int8_t rt_parse_try_num(rt_string s, double *out_value) {
    if (!out_value)
        return 0;
    *out_value = 0.0;
    if (!s)
        return 0;

    const char *text = string_cstr_without_embedded_nul(s);
    if (!text)
        return 0;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return 0;

    const char *nonfinite_end = NULL;
    double nonfinite = 0.0;
    if (scan_nonfinite_float(cursor, &nonfinite, &nonfinite_end)) {
        if (!is_end_of_input(nonfinite_end))
            return 0;
        *out_value = nonfinite;
        return 1;
    }

    const char *literal_end = scan_decimal_float(cursor);
    if (!literal_end || !is_end_of_input(literal_end))
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
    if (previous == (locale_t)0) {
        freelocale(c_locale);
        return 0;
    }
    value = strtod(cursor, &endptr);
    uselocale(previous);
    freelocale(c_locale);
#endif

    if (!isfinite(value))
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
    *out_value = 0;
    if (!s)
        return 0;

    const char *text = string_cstr_without_embedded_nul(s);
    if (!text)
        return 0;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return 0;

    // Extract the word (until whitespace or end)
    char word[16];
    size_t i = 0;
    while (*cursor && !is_ascii_space((unsigned char)*cursor) && i < sizeof(word) - 1)
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

/// @brief Parse an integer in any radix from 2 to 36 (covers binary, octal,
/// decimal, hex, base32, base36). Returns `default_value` for out-of-range
/// radix or any parse failure. Alphabetic digits beyond 9 use `a-z`/`A-Z`
/// case-insensitively. Decimal input may use a leading '+' or '-' so decimal
/// Fmt.IntRadix output round-trips; non-decimal input rejects signs and parses
/// the full unsigned 64-bit bit pattern before casting it back to int64_t.
int64_t rt_parse_int_radix(rt_string s, int64_t radix, int64_t default_value) {
    // Validate radix range
    if (radix < 2 || radix > 36)
        return default_value;
    if (!s)
        return default_value;

    const char *text = string_cstr_without_embedded_nul(s);
    if (!text)
        return default_value;

    const char *cursor = skip_whitespace(text);
    if (*cursor == '\0')
        return default_value;

    int negative = 0;
    if (*cursor == '-') {
        if (radix != 10)
            return default_value;
        negative = 1;
        ++cursor;
    } else if (*cursor == '+') {
        if (radix != 10)
            return default_value;
        ++cursor;
    }

    if (*cursor == '\0')
        return default_value;

    uint64_t value = 0;
    uint64_t limit = UINT64_MAX;
    if (radix == 10)
        limit = negative ? ((uint64_t)INT64_MAX + 1ULL) : (uint64_t)INT64_MAX;

    const char *p = cursor;
    for (; *p && !is_ascii_space((unsigned char)*p); ++p) {
        int digit = radix_digit_value((unsigned char)*p);
        if (digit < 0 || digit >= radix)
            return default_value;
        uint64_t udigit = (uint64_t)digit;
        uint64_t uradix = (uint64_t)radix;
        if (value > (limit - udigit) / uradix)
            return default_value;
        value = value * uradix + udigit;
    }

    if (p == cursor || !is_end_of_input(p))
        return default_value;

    if (radix == 10) {
        if (negative) {
            if (value == ((uint64_t)INT64_MAX + 1ULL))
                return INT64_MIN;
            return -(int64_t)value;
        }
        return (int64_t)value;
    }

    int64_t result = 0;
    memcpy(&result, &value, sizeof(result));
    return result;
}

void *rt_parse_double_option(rt_string s) {
    double value = 0.0;
    if (!rt_parse_try_num(s, &value))
        return rt_option_none();
    return rt_option_some_f64(value);
}

void *rt_parse_int64_option(rt_string s) {
    int64_t value = 0;
    if (!rt_parse_try_int(s, &value))
        return rt_option_none();
    return rt_option_some_i64(value);
}

void *rt_parse_bool_option(rt_string s) {
    int8_t value = 0;
    if (!rt_parse_try_bool(s, &value))
        return rt_option_none();
    return rt_option_some_i1(value);
}

#ifdef __cplusplus
}
#endif
