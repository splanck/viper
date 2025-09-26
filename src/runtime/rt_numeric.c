// File: src/runtime/rt_numeric.c
// Purpose: Implements numeric conversion helpers with BASIC semantics.
// Key invariants: Banker rounding is respected and overflow conditions clear ok flags.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md

#include "rt_numeric.h"
#include "rt.hpp"

#include <float.h>
#include <limits.h>
#include <math.h>

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

#ifdef __cplusplus
}
#endif

