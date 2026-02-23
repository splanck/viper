//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_fp.c
// Purpose: Implements floating-point domain helpers required by the BASIC
//          runtime. Provides exponentiation with BASIC-specific domain checks
//          (rejecting negative bases raised to non-integer exponents), a plain
//          two-argument pow wrapper for Viper.Math.Pow, and trap-on-error
//          variants used by the BASIC ^ operator in native codegen.
//
// Key invariants:
//   - rt_pow_f64_chkdom rejects negative bases with fractional exponents by
//     setting *ok=false and returning NaN; it does not trap.
//   - rt_pow_f64_basic_native traps directly on domain errors or non-finite
//     results using the standard BASIC diagnostic messages.
//   - rt_math_pow is an unchecked pass-through to pow(); domain errors are
//     the caller's responsibility.
//   - All functions are linkage-compatible with C++ builds via extern "C".
//   - VM and native builds use the same implementation, ensuring identical
//     error conditions when evaluating ^ expressions.
//
// Ownership/Lifetime:
//   - All functions operate on scalar double values; no allocation is performed.
//   - The ok pointer in rt_pow_f64_chkdom is borrowed; it must be non-NULL
//     (null causes a trap).
//
// Links: src/runtime/core/rt_fp.h (public API),
//        src/runtime/core/rt_math.c (trigonometric and other math helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_fp.h"
#include "rt.hpp"

#include <math.h>

// Ensure linkage compatibility with C++ builds.
#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Evaluate `pow(base, exp)` while checking BASIC domain rules.
    /// @details Validates the @p ok pointer, rejects negative bases raised to
    ///          non-integer exponents, and propagates infinities/NaNs produced by
    ///          the underlying `pow`.  On success @p ok is set to true; otherwise
    ///          the function returns the IEEE 754 result and marks @p ok false so
    ///          callers can convert the failure into a runtime error.
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

    /// @brief Simple 2-arg pow wrapper for IL calling convention.
    /// @details Calls the standard C pow() directly without domain checks.
    ///          Used by Viper.Math.Pow which has signature f64(f64,f64).
    double rt_math_pow(double base, double exponent)
    {
        return pow(base, exponent);
    }

    /// @brief 2-arg pow with BASIC domain checking for native codegen.
    /// @details Wraps rt_pow_f64_chkdom, providing the ok pointer internally
    ///          and trapping on domain/overflow errors. Used by BASIC ^ operator.
    double rt_pow_f64(double base, double exponent)
    {
        bool ok;
        double result = rt_pow_f64_chkdom(base, exponent, &ok);
        if (!ok)
        {
            if (base < 0.0)
                rt_trap("DomainError: negative base with fractional exponent");
            else
                rt_trap("DomainError: overflow in exponentiation");
        }
        return result;
    }

#ifdef __cplusplus
}
#endif
