// File: src/runtime/rt_numeric.c
// Purpose: Implements numeric conversion helpers with BASIC semantics.
// Key invariants: Banker rounding is respected and overflow conditions clear ok flags.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md

#include "rt_numeric.h"
#include "rt.hpp"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

    static double rt_round_nearest_even(double x)
    {
        return nearbyint(x);
    }

    static int16_t rt_cast_i16(double value, bool *ok)
    {
        if (!ok)
        {
            rt_trap("rt_cast_i16: null ok");
            return 0;
        }

        if (!isfinite(value))
        {
            *ok = false;
            return 0;
        }

        if (value < (double)INT16_MIN || value > (double)INT16_MAX)
        {
            *ok = false;
            return 0;
        }

        *ok = true;
        return (int16_t)value;
    }

    static int32_t rt_cast_i32(double value, bool *ok)
    {
        if (!ok)
        {
            rt_trap("rt_cast_i32: null ok");
            return 0;
        }

        if (!isfinite(value))
        {
            *ok = false;
            return 0;
        }

        if (value < (double)INT32_MIN || value > (double)INT32_MAX)
        {
            *ok = false;
            return 0;
        }

        *ok = true;
        return (int32_t)value;
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
            *ok = true;
            return 0.0;
        }

        if (*parse == '+' || *parse == '-')
        {
            const char next = parse[1];
            if (next == '.')
            {
                if (!rt_is_digit_char(parse[2]))
                {
                    *ok = true;
                    return 0.0;
                }
            }
            else if (!rt_is_digit_char(next))
            {
                *ok = true;
                return 0.0;
            }
        }
        else if (*parse == '.')
        {
            if (!rt_is_digit_char(parse[1]))
            {
                *ok = true;
                return 0.0;
            }
        }
        else if (!rt_is_digit_char(*parse))
        {
            *ok = true;
            return 0.0;
        }

        errno = 0;
        char *end = NULL;
        double value = strtod(parse, &end);
        if (end == parse)
        {
            *ok = true;
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

