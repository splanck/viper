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

    /// @brief Return the smaller of two double-precision floating-point values.
    /// @details Implements BASIC MIN for floating-point arguments.
    /// @param a First input value.
    /// @param b Second input value.
    /// @return The smaller of @p a and @p b.
    double rt_min_f64(double a, double b)
    {
        return a < b ? a : b;
    }

    /// @brief Return the larger of two double-precision floating-point values.
    /// @details Implements BASIC MAX for floating-point arguments.
    /// @param a First input value.
    /// @param b Second input value.
    /// @return The larger of @p a and @p b.
    double rt_max_f64(double a, double b)
    {
        return a > b ? a : b;
    }

    /// @brief Return the smaller of two 64-bit signed integers.
    /// @details Implements BASIC MIN for integer arguments.
    /// @param a First input value.
    /// @param b Second input value.
    /// @return The smaller of @p a and @p b.
    long long rt_min_i64(long long a, long long b)
    {
        return a < b ? a : b;
    }

    /// @brief Return the larger of two 64-bit signed integers.
    /// @details Implements BASIC MAX for integer arguments.
    /// @param a First input value.
    /// @param b Second input value.
    /// @return The larger of @p a and @p b.
    long long rt_max_i64(long long a, long long b)
    {
        return a > b ? a : b;
    }

    //=========================================================================
    // Additional Math Functions
    //=========================================================================

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif
#define M_TAU (2.0 * M_PI)

    /// @brief Compute arc tangent of y/x using signs to determine quadrant.
    /// @param y Y coordinate.
    /// @param x X coordinate.
    /// @return Angle in radians in range [-pi, pi].
    double rt_atan2(double y, double x)
    {
        return atan2(y, x);
    }

    /// @brief Compute arc sine of x.
    /// @param x Input in range [-1, 1].
    /// @return Angle in radians in range [-pi/2, pi/2].
    double rt_asin(double x)
    {
        return asin(x);
    }

    /// @brief Compute arc cosine of x.
    /// @param x Input in range [-1, 1].
    /// @return Angle in radians in range [0, pi].
    double rt_acos(double x)
    {
        return acos(x);
    }

    /// @brief Compute hyperbolic sine of x.
    /// @param x Input value.
    /// @return sinh(x).
    double rt_sinh(double x)
    {
        return sinh(x);
    }

    /// @brief Compute hyperbolic cosine of x.
    /// @param x Input value.
    /// @return cosh(x).
    double rt_cosh(double x)
    {
        return cosh(x);
    }

    /// @brief Compute hyperbolic tangent of x.
    /// @param x Input value.
    /// @return tanh(x).
    double rt_tanh(double x)
    {
        return tanh(x);
    }

    /// @brief Round to nearest integer, away from zero on tie.
    /// @param x Input value.
    /// @return Rounded value.
    double rt_round(double x)
    {
        return round(x);
    }

    /// @brief Truncate toward zero.
    /// @param x Input value.
    /// @return Truncated value.
    double rt_trunc(double x)
    {
        return trunc(x);
    }

    /// @brief Compute base-10 logarithm.
    /// @param x Input (must be positive).
    /// @return log10(x).
    double rt_log10(double x)
    {
        return log10(x);
    }

    /// @brief Compute base-2 logarithm.
    /// @param x Input (must be positive).
    /// @return log2(x).
    double rt_log2(double x)
    {
        return log2(x);
    }

    /// @brief Compute floating-point remainder.
    /// @param x Dividend.
    /// @param y Divisor.
    /// @return Remainder of x/y.
    double rt_fmod(double x, double y)
    {
        return fmod(x, y);
    }

    /// @brief Compute sqrt(x*x + y*y) without overflow.
    /// @param x First value.
    /// @param y Second value.
    /// @return Hypotenuse.
    double rt_hypot(double x, double y)
    {
        return hypot(x, y);
    }

    /// @brief Clamp a value to a range [lo, hi].
    /// @param val Value to clamp.
    /// @param lo Lower bound.
    /// @param hi Upper bound.
    /// @return Clamped value.
    double rt_clamp_f64(double val, double lo, double hi)
    {
        if (val < lo)
            return lo;
        if (val > hi)
            return hi;
        return val;
    }

    /// @brief Clamp an integer to a range [lo, hi].
    /// @param val Value to clamp.
    /// @param lo Lower bound.
    /// @param hi Upper bound.
    /// @return Clamped value.
    long long rt_clamp_i64(long long val, long long lo, long long hi)
    {
        if (val < lo)
            return lo;
        if (val > hi)
            return hi;
        return val;
    }

    /// @brief Linear interpolation between a and b.
    /// @param a Start value.
    /// @param b End value.
    /// @param t Interpolation factor (0 = a, 1 = b).
    /// @return Interpolated value.
    double rt_lerp(double a, double b, double t)
    {
        return a + t * (b - a);
    }

    /// @brief Wrap a value to range [lo, hi).
    /// @param val Value to wrap.
    /// @param lo Lower bound (inclusive).
    /// @param hi Upper bound (exclusive).
    /// @return Wrapped value.
    double rt_wrap_f64(double val, double lo, double hi)
    {
        double range = hi - lo;
        if (range <= 0.0)
            return lo;

        double result = fmod(val - lo, range);
        if (result < 0.0)
            result += range;
        return result + lo;
    }

    /// @brief Wrap an integer to range [lo, hi).
    /// @param val Value to wrap.
    /// @param lo Lower bound (inclusive).
    /// @param hi Upper bound (exclusive).
    /// @return Wrapped value.
    long long rt_wrap_i64(long long val, long long lo, long long hi)
    {
        long long range = hi - lo;
        if (range <= 0)
            return lo;

        long long result = (val - lo) % range;
        if (result < 0)
            result += range;
        return result + lo;
    }

    /// @brief Return the constant Pi.
    /// @return Pi (3.14159...).
    double rt_math_pi(void)
    {
        return M_PI;
    }

    /// @brief Return Euler's number.
    /// @return e (2.71828...).
    double rt_math_e(void)
    {
        return M_E;
    }

    /// @brief Return Tau (2*Pi).
    /// @return Tau (6.28318...).
    double rt_math_tau(void)
    {
        return M_TAU;
    }

    /// @brief Convert radians to degrees.
    /// @param radians Angle in radians.
    /// @return Angle in degrees.
    double rt_deg(double radians)
    {
        return radians * (180.0 / M_PI);
    }

    /// @brief Convert degrees to radians.
    /// @param degrees Angle in degrees.
    /// @return Angle in radians.
    double rt_rad(double degrees)
    {
        return degrees * (M_PI / 180.0);
    }

#ifdef __cplusplus
}
#endif
