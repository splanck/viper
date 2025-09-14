// File: runtime/rt_math.c
// Purpose: Implements portable math helpers for the runtime.
// Key invariants: Follows IEEE semantics; no traps on domain errors.
// Ownership/Lifetime: None.
// Links: docs/runtime-abi.md

#include "rt_math.h"
#include "rt.hpp"
#include <limits.h>
#include <math.h>

// Ensure functions are visible with C linkage when included in C++ builds
#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * Computes the non-negative square root of @p x.
     *
     * @param x Input value. Finite negative values yield NaN, while NaN and
     * infinity are propagated per IEEE-754.
     * @return The non-negative square root of @p x. Zero preserves its sign.
     *
     * Edge cases: negative finite inputs result in NaN; no domain-error traps
     * are raised.
     */
    double rt_sqrt(double x)
    {
        return sqrt(x);
    }

    /**
     * Rounds @p x downward to the nearest integral value.
     *
     * @param x Input value.
     * @return Largest integral value not greater than @p x.
     *
     * Edge cases: NaN propagates; ±infinity return themselves. No
     * domain-error traps are raised and IEEE-754 semantics are followed.
     */
    double rt_floor(double x)
    {
        return floor(x);
    }

    /**
     * Rounds @p x upward to the nearest integral value.
     *
     * @param x Input value.
     * @return Smallest integral value not less than @p x.
     *
     * Edge cases: NaN propagates; ±infinity return themselves. No
     * domain-error traps are raised and IEEE-754 semantics are followed.
     */
    double rt_ceil(double x)
    {
        return ceil(x);
    }

    /**
     * Computes the sine of @p x, where @p x is expressed in radians.
     *
     * @param x Input angle in radians.
     * @return Sine of @p x.
     *
     * Edge cases: NaN propagates; ±infinity yield NaN without trapping.
     * Follows IEEE-754 semantics and raises no domain-error traps.
     */
    double rt_sin(double x)
    {
        return sin(x);
    }

    /**
     * Computes the cosine of @p x, where @p x is expressed in radians.
     *
     * @param x Input angle in radians.
     * @return Cosine of @p x.
     *
     * Edge cases: NaN propagates; ±infinity yield NaN without trapping.
     * Follows IEEE-754 semantics and raises no domain-error traps.
     */
    double rt_cos(double x)
    {
        return cos(x);
    }

    /**
     * Raises @p x to the power @p y.
     *
     * @param x Base value.
     * @param y Exponent value.
     * @return @p x raised to @p y following IEEE-754 semantics.
     *
     * Edge cases: NaN inputs propagate. Results for negative bases with
     * fractional exponents and 0^0 follow the platform's IEEE-754 behavior;
     * no domain-error traps are raised.
     */
    double rt_pow(double x, double y)
    {
        return pow(x, y);
    }

    /**
     * Computes the absolute value of a signed 64-bit integer.
     *
     * @param v Input value.
     * @return Absolute value of @p v. Traps if @p v is `LLONG_MIN`.
     */
    long long rt_abs_i64(long long v)
    {
        if (v == LLONG_MIN)
            return rt_trap("rt_abs_i64: overflow"), 0;
        return v < 0 ? -v : v;
    }

    /**
     * Computes the absolute value of a double-precision floating-point number.
     *
     * @param v Input value.
     * @return Non-negative magnitude of @p v. NaN inputs propagate; the sign of
     * zero is cleared. No domain-error traps are raised and IEEE-754 semantics
     * are followed.
     */
    double rt_abs_f64(double v)
    {
        return fabs(v);
    }

#ifdef __cplusplus
}
#endif
