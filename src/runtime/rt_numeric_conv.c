// File: src/runtime/rt_numeric_conv.c
// Purpose: Implements numeric conversion helpers with BASIC semantics.
// Key invariants: Banker rounding is respected and overflow conditions clear ok flags.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "rt_numeric.h"
#include "rt.hpp"

#include <limits.h>
#include <math.h>

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

#ifdef __cplusplus
}
#endif

