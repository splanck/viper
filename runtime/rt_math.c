// File: runtime/rt_math.c
// Purpose: Implements portable math helpers for the runtime.
// Key invariants: Follows IEEE semantics; no traps on domain errors.
// Ownership/Lifetime: None.
// Links: docs/runtime-abi.md

#include "rt_math.h"
#include <math.h>

// Ensure functions are visible with C linkage when included in C++ builds
#ifdef __cplusplus
extern "C"
{
#endif

    double rt_sqrt(double x)
    {
        return sqrt(x);
    }

    double rt_floor(double x)
    {
        return floor(x);
    }

    double rt_ceil(double x)
    {
        return ceil(x);
    }

    long long rt_abs_i64(long long v)
    {
        return v < 0 ? -v : v;
    }

    double rt_abs_f64(double v)
    {
        return fabs(v);
    }

#ifdef __cplusplus
}
#endif
