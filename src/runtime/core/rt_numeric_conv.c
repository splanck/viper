//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_numeric_conv.c
// Purpose: Provides scalar numeric conversion routines that emulate BASIC
//          semantics for rounding, truncation, and safe floating-point to
//          integer casts. Covers banker's rounding (round-half-to-even),
//          range-checked casts to I32/I64, and string-to-number parsing.
//
// Key invariants:
//   - All conversion APIs validate input pointers; null ok-pointer causes a
//     trap with a descriptive message rather than a null dereference.
//   - Conversion failures are communicated through explicit bool* flags; the
//     output value is set to 0 and the flag to false on failure.
//   - Banker's rounding is implemented explicitly so it is independent of the
//     process floating-point rounding mode.
//   - Range bounds are checked after NaN/infinity rejection; out-of-range
//     values set the flag to false without trapping.
//   - No outputs are left partially initialised; on failure the output is
//     always set to a defined value (0 or 0.0).
//
// Ownership/Lifetime:
//   - Functions operate purely on caller-supplied values and buffers; no heap
//     allocation is performed and no state is retained between calls.
//
// Links: src/runtime/core/rt_numeric.h (public API),
//        src/runtime/core/rt_numeric.c (complementary numeric utilities),
//        src/runtime/core/rt_fp.c (floating-point domain checking)
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_numeric.h"
#include "rt_string.h"

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

/// @brief Round a floating-point value to the nearest even integer.
/// @details Implements round-half-to-even directly instead of using
///          @c nearbyint, whose result depends on the process floating-point
///          rounding mode. Values with magnitude >= 2^52 already have no
///          fractional part representable in double precision.
static double rt_round_nearest_even(double x) {
    if (!isfinite(x))
        return x;

    double ax = fabs(x);
    if (ax >= 0x1.0p52)
        return x;

    double lower = floor(ax);
    double frac = ax - lower;
    double rounded = lower;
    if (frac > 0.5) {
        rounded = lower + 1.0;
    } else if (frac == 0.5) {
        double half = fmod(lower, 2.0);
        if (half != 0.0)
            rounded = lower + 1.0;
    }

    return copysign(rounded, x);
}

/// @brief Validate a floating-point value before casting to an integer type.
/// @details Confirms the @p ok pointer is provided, rejects NaN or infinite
///          values, and checks the numeric range against the supplied
///          bounds.  When validation fails the helper records `false` in
///          @p ok and returns `0.0`; otherwise it preserves the original
///          value so the caller can perform the final cast.
/// @param value Floating-point candidate.
/// @param ok Pointer to a flag that receives the success status.
/// @param min_value Inclusive minimum representable value.
/// @param max_value Inclusive maximum representable value.
/// @param null_ok_trap Diagnostic string for null @p ok pointers.
/// @return The original value when it passes validation, otherwise 0.0.
static inline double rt_cast_integer_checked(
    double value, bool *ok, double min_value, double max_value, const char *null_ok_trap) {
    if (!ok) {
        rt_trap(null_ok_trap);
        return 0.0;
    }

    if (!isfinite(value)) {
        *ok = false;
        return 0.0;
    }

    if (value < min_value || value > max_value) {
        *ok = false;
        return 0.0;
    }

    *ok = true;
    return value;
}

#define RT_CAST_INTEGER(value, ok, type, min_value, max_value, null_ok_trap)                       \
    ((type)rt_cast_integer_checked(                                                                \
        (value), (ok), (double)(min_value), (double)(max_value), (null_ok_trap)))

/// @brief Cast a floating-point value to @c int16_t with validation.
/// @details Delegates to @ref rt_cast_integer_checked using the @c int16_t
///          range limits and propagates the @p ok flag.
/// @param value Floating-point value to convert.
/// @param ok Output flag recording success or failure.
/// @return The truncated 16-bit integer when valid; otherwise zero.
static int16_t rt_cast_i16(double value, bool *ok) {
    return RT_CAST_INTEGER(value, ok, int16_t, INT16_MIN, INT16_MAX, "rt_cast_i16: null ok");
}

/// @brief Cast a floating-point value to @c int32_t with validation.
/// @details Applies the @ref rt_cast_integer_checked guard using @c int32_t
///          limits so that overflow and NaN conditions surface through the
///          @p ok flag.
/// @param value Floating-point value to convert.
/// @param ok Output flag recording success or failure.
/// @return The truncated 32-bit integer when valid; otherwise zero.
static int32_t rt_cast_i32(double value, bool *ok) {
    return RT_CAST_INTEGER(value, ok, int32_t, INT32_MIN, INT32_MAX, "rt_cast_i32: null ok");
}

/// @brief Convert a double to BASIC's CINT result with banker rounding.
/// @details Applies nearest-even rounding via @ref rt_round_nearest_even
///          before casting to @c int16_t.  The @p ok flag communicates
///          whether the rounded value fit within the target range.
/// @param x Input double.
/// @param ok Output flag updated with the conversion status.
/// @return The converted 16-bit integer, or zero if the cast failed.
int16_t rt_cint_from_double(double x, bool *ok) {
    const double rounded = rt_round_nearest_even(x);
    return rt_cast_i16(rounded, ok);
}

/// @brief Convert a double to BASIC's CLNG result with banker rounding.
/// @details Mirrors @ref rt_cint_from_double but targets @c int32_t,
///          allowing larger integer ranges while still respecting the
///          nearest-even rounding rule.
/// @param x Input double.
/// @param ok Output flag updated with the conversion status.
/// @return The converted 32-bit integer, or zero if the cast failed.
int32_t rt_clng_from_double(double x, bool *ok) {
    const double rounded = rt_round_nearest_even(x);
    return rt_cast_i32(rounded, ok);
}

/// @brief Convert a double to BASIC's CSNG single-precision result.
/// @details Validates the @p ok pointer, rejects non-finite inputs, casts to
///          @c float, and ensures the result remains finite.  The helper
///          sets @p ok to `true` only when all checks succeed.
/// @param x Input double.
/// @param ok Output flag updated with the conversion status.
/// @return The converted single-precision value or NaN when validation
///         fails.
float rt_csng_from_double(double x, bool *ok) {
    if (!ok) {
        rt_trap("rt_csng_from_double: null ok");
        return NAN;
    }

    if (!isfinite(x)) {
        *ok = false;
        return NAN;
    }

    const float result = (float)x;
    if (!isfinite(result)) {
        *ok = false;
        return result;
    }

    *ok = true;
    return result;
}

/// @brief Return the provided value unchanged for CDbl conversions.
/// @details BASIC's CDbl accepts any numeric input and returns a double.
///          The runtime stores doubles internally, so the helper simply
///          forwards the argument.
/// @param x Input numeric value.
/// @return The unchanged double precision value.
double rt_cdbl_from_any(double x) {
    return x;
}

/// @brief Compute BASIC's INT result by flooring the argument.
/// @details Delegates to @ref floor to obtain the greatest integer less than
///          or equal to @p x.
/// @param x Input double.
/// @return Floored value as a double.
double rt_int_floor(double x) {
    return floor(x);
}

/// @brief Compute BASIC's FIX result by truncating towards zero.
/// @details Uses @ref trunc so positive and negative numbers move towards
///          zero, matching BASIC semantics.
/// @param x Input double.
/// @return Truncated value as a double.
double rt_fix_trunc(double x) {
    return trunc(x);
}

/// @brief Convert a double to a 64-bit signed integer by truncating toward zero.
/// @details Provides a direct conversion from floating-point to integer,
///          suitable for ViperLang's Number to Integer conversion. NaN becomes
///          0; infinities and finite out-of-range values clamp to the nearest
///          signed 64-bit endpoint.
/// @param x Input double.
/// @return Truncated value as 64-bit signed integer.
long long rt_f64_to_i64(double x) {
    if (isnan(x))
        return 0;
    if (x >= 0x1.0p63)
        return INT64_MAX;
    if (x < -0x1.0p63)
        return INT64_MIN;
    return (long long)trunc(x);
}

/// @brief Round a double to a specified number of digits using banker's rounding.
/// @details Applies nearest-even rounding with optional scaling so callers
///          can request decimal precision.  Extremely large magnitude digit
///          counts short-circuit to the original value to avoid overflow.
/// @param x Input double to round.
/// @param ndigits Number of digits after the decimal point.
/// @return Rounded value according to BASIC's ROUND behaviour.
double rt_round_even(double x, int ndigits) {
    if (!isfinite(x))
        return x;

    if (ndigits == 0)
        return rt_round_nearest_even(x);

    const double absDigits = fabs((double)ndigits);
    if (absDigits > 308.0)
        return x;

    const double factor = pow(10.0, (double)ndigits);
    if (!isfinite(factor) || factor == 0.0)
        return x;

    const double scaled = x * factor;
    if (!isfinite(scaled))
        return x;

    const double rounded = rt_round_nearest_even(scaled);
    return rounded / factor;
}

/// @brief Return true for the fixed ASCII whitespace set, independent of locale.
static inline int rt_is_ascii_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' ||
           ch == '\v';
}

/// @brief Locale-independent test for ASCII digits 0-9.
/// @details Used by the parsers in this file instead of `isdigit` so the result is
///          deterministic regardless of the process's `LC_CTYPE` setting.
static inline int rt_is_ascii_digit(unsigned char ch) {
    return ch >= '0' && ch <= '9';
}

/// @brief Advance a pointer past ASCII whitespace characters.
/// @details Uses a fixed byte set instead of the process locale so numeric
///          parsing remains deterministic.
/// @param cursor Current position within a byte buffer.
/// @return Pointer to the first non-whitespace byte (or the terminator).
static inline const unsigned char *rt_skip_ascii_space(const unsigned char *cursor) {
    while (*cursor && rt_is_ascii_space(*cursor))
        ++cursor;
    return cursor;
}

/// @brief Return the end of a strict decimal floating literal.
/// @details Accepts `[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?`.
///          Hexadecimal C99 float syntax is intentionally rejected so the
///          public parser matches the documented decimal grammar.
static const unsigned char *rt_scan_decimal_float(const unsigned char *cursor) {
    if (!cursor)
        return NULL;

    const unsigned char *p = cursor;
    if (*p == '+' || *p == '-')
        ++p;

    int digits = 0;
    while (rt_is_ascii_digit(*p)) {
        ++digits;
        ++p;
    }

    if (*p == '.') {
        ++p;
        while (rt_is_ascii_digit(*p)) {
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
        while (rt_is_ascii_digit(*exp)) {
            ++exp_digits;
            ++exp;
        }
        if (exp_digits == 0)
            return NULL;
        p = exp;
    }

    return p;
}

/// @brief Parse a signed 64-bit integer from ASCII text.
/// @details Skips leading whitespace, invokes @ref strtoll using base 10,
///          validates that the entire string was consumed, and reports
///          overflow or invalid input through BASIC error codes.  The result
///          is stored in @p out_value when successful.
/// @param text Null-terminated string containing the number.
/// @param out_value Output pointer receiving the parsed integer.
/// @return BASIC error code (`Err_None` on success).
int32_t rt_parse_int64(const char *text, int64_t *out_value) {
    if (out_value)
        *out_value = 0;
    if (!text || !out_value)
        return (int32_t)Err_InvalidOperation;

    const unsigned char *cursor = rt_skip_ascii_space((const unsigned char *)text);
    if (*cursor == '\0')
        return (int32_t)Err_InvalidCast;

    errno = 0;
    char *endptr = NULL;
    long long parsed = strtoll((const char *)cursor, &endptr, 10);
    if (errno == ERANGE)
        return (int32_t)Err_Overflow;

    if (!endptr || endptr == (const char *)cursor)
        return (int32_t)Err_InvalidCast;

    const unsigned char *rest = (const unsigned char *)endptr;
    rest = rt_skip_ascii_space(rest);
    if (*rest != '\0')
        return (int32_t)Err_InvalidCast;

    *out_value = (int64_t)parsed;
    return (int32_t)Err_None;
}

/// @brief Resolve the underlying byte buffer of @p text, returning NULL when unavailable.
/// @details Defensive helper used by the public parsers — guards against a NULL handle
///          and against a string struct with a NULL data pointer (which would otherwise
///          crash in `strtol`/`strtod`). The returned pointer aliases the string's
///          storage; callers must not retain it past the lifetime of @p text.
static const char *rt_parse_string_text(rt_string text) {
    if (!text || !rt_string_is_handle((const void *)text) || !text->data)
        return NULL;
    size_t len = (size_t)rt_str_len(text);
    if (memchr(text->data, '\0', len))
        return NULL;
    return text->data;
}

/// @brief Parse a signed 64-bit integer from a runtime string.
int32_t rt_parse_int64_str(rt_string text, int64_t *out_value) {
    if (out_value)
        *out_value = 0;
    if (!out_value)
        return (int32_t)Err_InvalidOperation;
    const char *cstr = rt_parse_string_text(text);
    if (!cstr)
        return (int32_t)Err_InvalidCast;
    return rt_parse_int64(cstr, out_value);
}

/// @brief Implementation helper that parses a double using the C locale.
/// @details Establishes a temporary C locale so decimal points use '.',
///          delegates to the appropriate `strtod` flavour depending on the
///          platform, validates complete consumption, and checks for range
///          errors.  Errors are reported via BASIC error codes.
/// @param text Null-terminated string containing the number.
/// @param out_value Output pointer receiving the parsed double.
/// @return BASIC error code (`Err_None` on success).
static int32_t rt_parse_double_impl(const char *text, double *out_value) {
    const unsigned char *cursor = rt_skip_ascii_space((const unsigned char *)text);
    if (*cursor == '\0')
        return (int32_t)Err_InvalidCast;

    const unsigned char *literal_end = rt_scan_decimal_float(cursor);
    if (!literal_end)
        return (int32_t)Err_InvalidCast;
    const unsigned char *literal_tail = rt_skip_ascii_space(literal_end);
    if (*literal_tail != '\0')
        return (int32_t)Err_InvalidCast;

    errno = 0;
    char *endptr = NULL;
    double value = 0.0;

#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (!c_locale)
        return (int32_t)Err_RuntimeError;
    value = _strtod_l((const char *)cursor, &endptr, c_locale);
    _free_locale(c_locale);
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
    if (!c_locale)
        return (int32_t)Err_RuntimeError;
    locale_t previous = uselocale(c_locale);
    if (previous == (locale_t)0) {
        freelocale(c_locale);
        return (int32_t)Err_RuntimeError;
    }
    value = strtod((const char *)cursor, &endptr);
    uselocale(previous);
    freelocale(c_locale);
#endif

    if (endptr == (const char *)cursor)
        return (int32_t)Err_InvalidCast;

    if (errno == ERANGE || !isfinite(value))
        return (int32_t)Err_Overflow;

    const unsigned char *rest = rt_skip_ascii_space((const unsigned char *)endptr);
    if (*rest != '\0')
        return (int32_t)Err_InvalidCast;

    *out_value = value;
    return (int32_t)Err_None;
}

/// @brief Parse a double from ASCII text respecting BASIC error codes.
/// @details Validates parameters before delegating to
///          @ref rt_parse_double_impl so the public API consistently rejects
///          null pointers with @ref Err_InvalidOperation.
/// @param text Null-terminated string containing the number.
/// @param out_value Output pointer receiving the parsed double.
/// @return BASIC error code (`Err_None` on success).
int32_t rt_parse_double(const char *text, double *out_value) {
    if (out_value)
        *out_value = 0.0;
    if (!text || !out_value)
        return (int32_t)Err_InvalidOperation;
    return rt_parse_double_impl(text, out_value);
}

/// @brief Parse a double from a runtime string.
int32_t rt_parse_double_str(rt_string text, double *out_value) {
    if (out_value)
        *out_value = 0.0;
    if (!out_value)
        return (int32_t)Err_InvalidOperation;
    const char *cstr = rt_parse_string_text(text);
    if (!cstr)
        return (int32_t)Err_InvalidCast;
    return rt_parse_double(cstr, out_value);
}

#ifdef __cplusplus
}
#endif
