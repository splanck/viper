// File: src/runtime/rt_numeric.c
// Purpose: Implements numeric conversion helpers with BASIC semantics.
// Key invariants: Banker rounding is respected and overflow conditions clear ok flags.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt_numeric.h"
#include "rt.hpp"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
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

    static double rt_round_nearest_even(double x)
    {
        return nearbyint(x);
    }

    static inline double rt_cast_integer_checked(
        double value,
        bool *ok,
        double min_value,
        double max_value,
        const char *null_ok_trap)
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

#define RT_CAST_INTEGER(value, ok, type, min_value, max_value, null_ok_trap) \
    ((type)rt_cast_integer_checked(                                             \
        (value),                                                                \
        (ok),                                                                   \
        (double)(min_value),                                                    \
        (double)(max_value),                                                    \
        (null_ok_trap)))

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

    static int rt_is_digit_char(char ch)
    {
        return ch >= '0' && ch <= '9';
    }

    static int rt_ascii_tolower(int ch)
    {
        if (ch >= 'A' && ch <= 'Z')
            return ch - 'A' + 'a';
        return ch;
    }

    static bool rt_match_token_ci(const char *start, const char *token, const char **out_end)
    {
        if (!start || !token)
            return false;

        size_t index = 0;
        while (token[index] != '\0')
        {
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

    static bool rt_parse_special_constant(const char *start, char **out_end, double *out_value)
    {
        if (!start || !out_value)
            return false;

        const char *cursor = start;
        bool is_negative = false;
        if (*cursor == '+' || *cursor == '-')
        {
            is_negative = (*cursor == '-');
            ++cursor;
        }

        const char *matched_end = NULL;
        if (rt_match_token_ci(cursor, "nan", &matched_end))
        {
            cursor = matched_end;
            double value = nan("");
            if (is_negative)
                value = -value;
            if (out_end)
                *out_end = (char *)cursor;
            *out_value = value;
            return true;
        }

        if (rt_match_token_ci(cursor, "inf", &matched_end))
        {
            cursor = matched_end;
            const char *extended = NULL;
            if (rt_match_token_ci(cursor, "inity", &extended))
                cursor = extended;

            double value = is_negative ? -INFINITY : INFINITY;
            if (out_end)
                *out_end = (char *)cursor;
            *out_value = value;
            return true;
        }

        return false;
    }

#if defined(_WIN32)
    static bool rt_strtod_c_locale(const char *input, char **out_end, double *out_value)
    {
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
    static bool rt_strtod_c_locale(const char *input, char **out_end, double *out_value)
    {
        if (!input || !out_value)
            return false;

        locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
        if (!c_locale)
            return false;

        locale_t previous = uselocale(c_locale);
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

    double rt_val_to_double(const char *s, bool *ok)
    {
        if (!ok)
        {
            rt_trap("rt_val_to_double: null ok");
            return 0.0;
        }
        if (!s)
        {
            *ok = false;
            rt_trap("rt_val_to_double: null string");
            return 0.0;
        }

        const unsigned char *p = (const unsigned char *)s;
        while (*p && isspace(*p))
            ++p;

        const char *parse = (const char *)p;
        if (*parse == '\0')
        {
            *ok = false;
            return 0.0;
        }

        double special_value = 0.0;
        if (rt_parse_special_constant(parse, NULL, &special_value))
        {
            *ok = false;
            return special_value;
        }

        if (*parse == '+' || *parse == '-')
        {
            const char next = parse[1];
            if (next == '.')
            {
                if (!rt_is_digit_char(parse[2]))
                {
                    *ok = false;
                    return 0.0;
                }
            }
            else if (!rt_is_digit_char(next))
            {
                *ok = false;
                return 0.0;
            }
        }
        else if (*parse == '.')
        {
            if (!rt_is_digit_char(parse[1]))
            {
                *ok = false;
                return 0.0;
            }
        }
        else if (!rt_is_digit_char(*parse))
        {
            *ok = false;
            return 0.0;
        }

        char *end = NULL;
        double value = 0.0;
        if (!rt_strtod_c_locale(parse, &end, &value))
        {
            *ok = false;
            return 0.0;
        }

        if (end && *end == ',')
        {
            *ok = false;
            return 0.0;
        }

        if (!isfinite(value))
        {
            *ok = false;
            return value;
        }

        *ok = true;
        return value;
    }

    static void rt_format(char *out, size_t cap, const char *fmt, ...)
    {
        if (!out || cap == 0)
            rt_trap("rt_format: invalid buffer");

        va_list args;
        va_start(args, fmt);
        int written = vsnprintf(out, cap, fmt, args);
        va_end(args);

        if (written < 0)
            rt_trap("rt_format: format error");
        if ((size_t)written >= cap)
            rt_trap("rt_format: truncated");
    }

    void rt_str_from_double(double x, char *out, size_t cap, RtError *out_err)
    {
        rt_format(out, cap, "%.17g", x);
        if (out_err)
            *out_err = RT_ERROR_NONE;
    }

    void rt_str_from_float(float x, char *out, size_t cap, RtError *out_err)
    {
        rt_format(out, cap, "%.9g", (double)x);
        if (out_err)
            *out_err = RT_ERROR_NONE;
    }

    void rt_str_from_i32(int32_t x, char *out, size_t cap, RtError *out_err)
    {
        rt_format(out, cap, "%" PRId32, x);
        if (out_err)
            *out_err = RT_ERROR_NONE;
    }

    void rt_str_from_i16(int16_t x, char *out, size_t cap, RtError *out_err)
    {
        rt_format(out, cap, "%" PRId16, x);
        if (out_err)
            *out_err = RT_ERROR_NONE;
    }

#ifdef __cplusplus
}
#endif

