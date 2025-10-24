// File: src/runtime/rt_numeric_conv.c
// Purpose: Implements numeric conversion helpers with BASIC semantics.
// Key invariants: Banker rounding is respected and overflow conditions clear ok flags.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt.hpp"
#include "rt_numeric.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <xlocale.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    static double rt_round_nearest_even(double x)
    {
        return nearbyint(x);
    }

    static inline double rt_cast_integer_checked(
        double value, bool *ok, double min_value, double max_value, const char *null_ok_trap)
    {
        if (!ok)
        {
            rt_trap(null_ok_trap);
            return 0.0;
        }

        if (!isfinite(value))
        {
            *ok = false;
            return 0.0;
        }

        if (value < min_value || value > max_value)
        {
            *ok = false;
            return 0.0;
        }

        *ok = true;
        return value;
    }

#define RT_CAST_INTEGER(value, ok, type, min_value, max_value, null_ok_trap)                       \
    ((type)rt_cast_integer_checked(                                                                \
        (value), (ok), (double)(min_value), (double)(max_value), (null_ok_trap)))

    static int16_t rt_cast_i16(double value, bool *ok)
    {
        return RT_CAST_INTEGER(value, ok, int16_t, INT16_MIN, INT16_MAX, "rt_cast_i16: null ok");
    }

    static int32_t rt_cast_i32(double value, bool *ok)
    {
        return RT_CAST_INTEGER(value, ok, int32_t, INT32_MIN, INT32_MAX, "rt_cast_i32: null ok");
    }

    int16_t rt_cint_from_double(double x, bool *ok)
    {
        const double rounded = rt_round_nearest_even(x);
        return rt_cast_i16(rounded, ok);
    }

    int32_t rt_clng_from_double(double x, bool *ok)
    {
        const double rounded = rt_round_nearest_even(x);
        return rt_cast_i32(rounded, ok);
    }

    float rt_csng_from_double(double x, bool *ok)
    {
        if (!ok)
        {
            rt_trap("rt_csng_from_double: null ok");
            return NAN;
        }

        if (!isfinite(x))
        {
            *ok = false;
            return NAN;
        }

        const float result = (float)x;
        if (!isfinite(result))
        {
            *ok = false;
            return result;
        }

        *ok = true;
        return result;
    }

    double rt_cdbl_from_any(double x)
    {
        return x;
    }

    double rt_int_floor(double x)
    {
        return floor(x);
    }

    double rt_fix_trunc(double x)
    {
        return trunc(x);
    }

    double rt_round_even(double x, int ndigits)
    {
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

    static inline const unsigned char *rt_skip_ascii_space(const unsigned char *cursor)
    {
        while (*cursor && isspace(*cursor))
            ++cursor;
        return cursor;
    }

    int32_t rt_parse_int64(const char *text, int64_t *out_value)
    {
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

    static int32_t rt_parse_double_impl(const char *text, double *out_value)
    {
        const unsigned char *cursor = rt_skip_ascii_space((const unsigned char *)text);
        if (*cursor == '\0')
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

    int32_t rt_parse_double(const char *text, double *out_value)
    {
        if (!text || !out_value)
            return (int32_t)Err_InvalidOperation;
        return rt_parse_double_impl(text, out_value);
    }

#ifdef __cplusplus
}
#endif
