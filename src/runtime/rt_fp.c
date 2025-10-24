// File: src/runtime/rt_fp.c
// Purpose: Implements floating-point helpers that enforce BASIC domain rules.
// Key invariants: IEEE-754 default rounding remains unchanged; failures only report via ok flag.
// Ownership/Lifetime: None.
// Links: docs/specs/numerics.md

#include "rt_fp.h"
#include "rt.hpp"

#include <math.h>

// Ensure linkage compatibility with C++ builds.
#ifdef __cplusplus
extern "C"
{
#endif

    double rt_pow_f64_chkdom(double base, double exp, bool *ok)
    {
        if (!ok)
        {
            rt_trap("rt_pow_f64_chkdom: null ok");
            return NAN;
        }

        bool exponentIntegral = false;
        if (isfinite(exp))
        {
            const double truncated = trunc(exp);
            exponentIntegral = (exp == truncated);
        }

        if (base < 0.0 && !exponentIntegral)
        {
            *ok = false;
            return NAN;
        }

        const double result = pow(base, exp);
        if (!isfinite(result))
        {
            *ok = false;
            return result;
        }

        *ok = true;
        return result;
    }

#ifdef __cplusplus
}
#endif
