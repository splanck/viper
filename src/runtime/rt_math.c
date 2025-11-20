//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// Implements the BASIC runtime's thin wrappers around standard math routines.
// The helpers ensure consistent diagnostics and trap behaviour by delegating to
// the C library while adding overflow checks where BASIC requires them.  Keeping
// the wrappers isolated here allows both the VM and native toolchains to share a
// single implementation of floating-point semantics.
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Math helper implementations for the BASIC runtime.
/// @details Provides IEEE-754 compliant wrappers around the C math library and
///          supplements them with BASIC-specific overflow handling (for example
///          in @ref rt_abs_i64).  All functions are exposed with C linkage so
///          native and VM runtimes can link them directly.

#include "rt_math.h"
#include "rt.hpp"
#include <limits.h>
#include <math.h>

// Ensure functions are visible with C linkage when included in C++ builds
#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Compute the non-negative square root of the input.
    /// @details Delegates to @c sqrt from <math.h> so IEEE-754 semantics apply to
    ///          NaN and infinity.  The wrapper exists to keep the runtime ABI
    ///          stable and document the absence of explicit traps for negative
    ///          inputs.
    /// @param x Input value supplied by BASIC user code.
    /// @return Principal square root of @p x; NaN when @p x is negative.
    double rt_sqrt(double x)
    {
        return sqrt(x);
    }

    /// @brief Round a floating-point value down to the nearest integer.
    /// @details Calls @c floor so fractional inputs move toward negative
    ///          infinity.  NaN and infinities propagate unchanged, matching BASIC
    ///          expectations and avoiding additional trap hooks.
    /// @param x Input value to round downward.
    /// @return Largest integer value less than or equal to @p x.
    double rt_floor(double x)
    {
        return floor(x);
    }

    /// @brief Round a floating-point value up to the nearest integer.
    /// @details Wraps @c ceil to move fractional inputs toward positive
    ///          infinity while allowing special values (NaN, ±inf) to propagate
    ///          unchanged.
    /// @param x Input value to round upward.
    /// @return Smallest integer value greater than or equal to @p x.
    double rt_ceil(double x)
    {
        return ceil(x);
    }

    /// @brief Compute the sine of an angle expressed in radians.
    /// @details Defers to @c sin so the runtime inherits the host's precision and
    ///          handling for special values.  NaN inputs yield NaN, and infinite
    ///          arguments propagate NaN without trapping.
    /// @param x Angle in radians.
    /// @return Sine of @p x.
    double rt_sin(double x)
    {
        return sin(x);
    }

    /// @brief Compute the cosine of an angle expressed in radians.
    /// @details Uses @c cos from <math.h>, inheriting IEEE-754 behaviour for NaN
    ///          and infinity.  The wrapper preserves BASIC semantics without
    ///          introducing additional range checks.
    /// @param x Angle in radians.
    /// @return Cosine of @p x.
    double rt_cos(double x)
    {
        return cos(x);
    }

    /// @brief Compute the tangent of an angle expressed in radians.
    /// @details Delegates to @c tan from <math.h>, inheriting IEEE-754 behaviour
    ///          for NaN and infinity.  The wrapper allows BASIC programs to compute
    ///          tangents without explicit calls to sin/cos division.
    /// @param x Angle in radians.
    /// @return Tangent of @p x.
    double rt_tan(double x)
    {
        return tan(x);
    }

    /// @brief Compute the arctangent of a value.
    /// @details Uses @c atan from <math.h> to compute the principal value of the
    ///          arctangent.  Result is in radians, in the range [-π/2, π/2].
    /// @param x Input value.
    /// @return Arctangent of @p x in radians.
    double rt_atan(double x)
    {
        return atan(x);
    }

    /// @brief Compute the exponential function (e^x).
    /// @details Delegates to @c exp from <math.h>, providing the natural
    ///          exponential function for BASIC programs.  Overflow produces
    ///          infinity per IEEE-754 semantics.
    /// @param x Exponent value.
    /// @return e raised to the power of @p x.
    double rt_exp(double x)
    {
        return exp(x);
    }

    /// @brief Compute the natural logarithm (base e).
    /// @details Uses @c log from <math.h> to compute the natural logarithm.
    ///          Returns NaN for negative inputs and -infinity for zero input,
    ///          following IEEE-754 semantics.
    /// @param x Input value (must be positive for real result).
    /// @return Natural logarithm of @p x.
    double rt_log(double x)
    {
        return log(x);
    }

    /// @brief Compute the sign of a 64-bit signed integer.
    /// @details Returns -1 for negative values, 0 for zero, and 1 for positive
    ///          values.  This is the classic SGN function from BASIC.
    /// @param v Signed integer input.
    /// @return -1, 0, or 1 depending on the sign of @p v.
    long long rt_sgn_i64(long long v)
    {
        if (v < 0)
            return -1;
        if (v > 0)
            return 1;
        return 0;
    }

    /// @brief Compute the sign of a double-precision floating-point value.
    /// @details Returns -1.0 for negative values (including negative zero),
    ///          0.0 for zero, and 1.0 for positive values.  NaN returns NaN.
    /// @param v Floating-point input.
    /// @return -1.0, 0.0, or 1.0 depending on the sign of @p v.
    double rt_sgn_f64(double v)
    {
        if (isnan(v))
            return v;
        if (v < 0.0)
            return -1.0;
        if (v > 0.0)
            return 1.0;
        return 0.0;
    }

    /// @brief Compute the absolute value of a 64-bit signed integer.
    /// @details Mirrors BASIC's overflow semantics by trapping when @p v equals
    ///          @c LLONG_MIN (whose absolute value cannot be represented).  Other
    ///          values are returned as their non-negative magnitude.
    /// @param v Signed integer input.
    /// @return Absolute value of @p v when representable; returns zero after
    ///         reporting a trap for overflow.
    long long rt_abs_i64(long long v)
    {
        if (v == LLONG_MIN)
            return rt_trap("rt_abs_i64: overflow"), 0;
        return v < 0 ? -v : v;
    }

    /// @brief Compute the magnitude of a double-precision floating-point value.
    /// @details Delegates to @c fabs so NaN and infinity semantics follow the C
    ///          standard library.  The result is always non-negative, clearing
    ///          the sign bit of signed zero.
    /// @param v Floating-point input.
    /// @return Non-negative magnitude of @p v.
    double rt_abs_f64(double v)
    {
        return fabs(v);
    }

#ifdef __cplusplus
}
#endif
