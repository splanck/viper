//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/math.c
// Purpose: Mathematical functions for ViperDOS libc.
// Key invariants: IEEE 754 double precision; hardware FPU where available.
// Ownership/Lifetime: Library; all functions are stateless and pure.
// Links: user/libc/include/math.h
//
//===----------------------------------------------------------------------===//

/**
 * @file math.c
 * @brief Mathematical functions for ViperDOS libc.
 *
 * @details
 * This file implements standard C math library functions:
 *
 * - Basic operations: fabs, fmod, fmax, fmin, remainder
 * - Rounding: ceil, floor, trunc, round, nearbyint, rint
 * - Power functions: sqrt, cbrt, pow, hypot
 * - Exponential/logarithmic: exp, log, log10, log2, exp2, expm1, log1p
 * - Trigonometric: sin, cos, tan, asin, acos, atan, atan2
 * - Hyperbolic: sinh, cosh, tanh, asinh, acosh, atanh
 * - FP manipulation: frexp, ldexp, modf, scalbn, ilogb, copysign
 * - Special functions: erf, erfc, tgamma, lgamma
 *
 * Uses hardware FPU where available (Cortex-A72 has VFPv4).
 * Some functions use __builtin intrinsics for optimal codegen.
 */

#include "../include/math.h"

/* Helper: get raw bits of double */
static inline unsigned long long double_to_bits(double x) {
    union {
        double d;
        unsigned long long u;
    } u;

    u.d = x;
    return u.u;
}

/* Helper: create double from raw bits */
static inline double bits_to_double(unsigned long long bits) {
    union {
        double d;
        unsigned long long u;
    } u;

    u.u = bits;
    return u.d;
}

/*
 * Basic operations
 */

/** @brief Compute the absolute value of a double. */
double fabs(double x) {
    return __builtin_fabs(x);
}

/** @brief Compute the absolute value of a float. */
float fabsf(float x) {
    return __builtin_fabsf(x);
}

/** @brief Compute the floating-point remainder of x / y. */
double fmod(double x, double y) {
    return __builtin_fmod(x, y);
}

/** @brief Compute the floating-point remainder of x / y (float version). */
float fmodf(float x, float y) {
    return __builtin_fmodf(x, y);
}

/** @brief Compute the IEEE 754 remainder of x / y. */
double remainder(double x, double y) {
    /* IEEE 754 remainder: x - n*y where n is nearest integer to x/y */
    double n = round(x / y);
    return x - n * y;
}

/** @brief Return the larger of x and y. */
double fmax(double x, double y) {
    return __builtin_fmax(x, y);
}

/** @brief Return the smaller of x and y. */
double fmin(double x, double y) {
    return __builtin_fmin(x, y);
}

/** @brief Return the positive difference max(x - y, 0). */
double fdim(double x, double y) {
    return (x > y) ? (x - y) : 0.0;
}

/*
 * Rounding functions
 */

/** @brief Round x upward to the nearest integer. */
double ceil(double x) {
    return __builtin_ceil(x);
}

/** @brief Round x upward to the nearest integer (float version). */
float ceilf(float x) {
    return __builtin_ceilf(x);
}

/** @brief Round x downward to the nearest integer. */
double floor(double x) {
    return __builtin_floor(x);
}

/** @brief Round x downward to the nearest integer (float version). */
float floorf(float x) {
    return __builtin_floorf(x);
}

/** @brief Truncate x toward zero to the nearest integer. */
double trunc(double x) {
    return __builtin_trunc(x);
}

/** @brief Truncate x toward zero to the nearest integer (float version). */
float truncf(float x) {
    return __builtin_truncf(x);
}

/** @brief Round x to the nearest integer, rounding halfway cases away from zero. */
double round(double x) {
    return __builtin_round(x);
}

/** @brief Round x to the nearest integer (float version). */
float roundf(float x) {
    return __builtin_roundf(x);
}

/** @brief Round x to the nearest long integer. */
long lround(double x) {
    return (long)round(x);
}

/** @brief Round x to the nearest long long integer. */
long long llround(double x) {
    return (long long)round(x);
}

/** @brief Round x to the nearest integer using the current rounding mode. */
double nearbyint(double x) {
    return __builtin_nearbyint(x);
}

/** @brief Round x to the nearest integer, possibly raising inexact. */
double rint(double x) {
    return __builtin_rint(x);
}

/** @brief Round x to the nearest long integer using the current rounding mode. */
long lrint(double x) {
    return (long)rint(x);
}

/** @brief Round x to the nearest long long integer using the current rounding mode. */
long long llrint(double x) {
    return (long long)rint(x);
}

/*
 * Power functions
 */

/** @brief Compute the square root of x. */
double sqrt(double x) {
    return __builtin_sqrt(x);
}

/** @brief Compute the square root of x (float version). */
float sqrtf(float x) {
    return __builtin_sqrtf(x);
}

/** @brief Compute the cube root of x using Newton-Raphson iteration. */
double cbrt(double x) {
    /* Cube root using Newton-Raphson */
    if (x == 0.0 || !isfinite(x))
        return x;

    int neg = x < 0;
    if (neg)
        x = -x;

    /* Initial approximation using bit manipulation */
    double y = bits_to_double((double_to_bits(x) / 3) + (1ULL << 61));

    /* Newton-Raphson iterations: y = y - (y^3 - x) / (3*y^2) = (2*y + x/y^2) / 3 */
    y = (2.0 * y + x / (y * y)) / 3.0;
    y = (2.0 * y + x / (y * y)) / 3.0;
    y = (2.0 * y + x / (y * y)) / 3.0;
    y = (2.0 * y + x / (y * y)) / 3.0;

    return neg ? -y : y;
}

/** @brief Compute sqrt(x*x + y*y) with overflow protection. */
double hypot(double x, double y) {
    /* sqrt(x^2 + y^2) with overflow protection */
    x = fabs(x);
    y = fabs(y);

    if (x < y) {
        double t = x;
        x = y;
        y = t;
    }

    if (x == 0.0)
        return 0.0;

    double r = y / x;
    return x * sqrt(1.0 + r * r);
}

/**
 * @brief Raise base to the power of exponent.
 * @details Uses binary exponentiation for small integer exponents,
 *          otherwise computes exp(exponent * log(base)).
 */
double pow(double base, double exponent) {
    /* Handle special cases */
    if (exponent == 0.0)
        return 1.0;
    if (base == 1.0)
        return 1.0;
    if (base == 0.0) {
        if (exponent > 0.0)
            return 0.0;
        return INFINITY;
    }
    if (isnan(base) || isnan(exponent))
        return NAN;

    /* For integer exponents, use binary exponentiation */
    if (exponent == floor(exponent) && fabs(exponent) < 32) {
        int n = (int)exponent;
        int neg = n < 0;
        if (neg)
            n = -n;

        double result = 1.0;
        double b = base;
        while (n > 0) {
            if (n & 1)
                result *= b;
            b *= b;
            n >>= 1;
        }
        return neg ? (1.0 / result) : result;
    }

    /* General case: base^exp = e^(exp * ln(base)) */
    if (base < 0.0) {
        /* Negative base with non-integer exponent is undefined (returns NaN) */
        return NAN;
    }

    return exp(exponent * log(base));
}

/** @brief Raise base to the power of exponent (float version). */
float powf(float base, float exponent) {
    return (float)pow((double)base, (double)exponent);
}

/*
 * Exponential and logarithmic functions
 */

/* Constants for exp approximation */
#define EXP_POLY_DEGREE 13

/** @brief Compute e raised to the power x using argument reduction and Taylor series. */
double exp(double x) {
    /* Handle special cases */
    if (isnan(x))
        return NAN;
    if (x > 709.0)
        return INFINITY;
    if (x < -745.0)
        return 0.0;

    /* Reduce argument: e^x = 2^k * e^r where |r| <= ln(2)/2 */
    double ln2 = 0.693147180559945309417232121458;
    double k = floor(x / ln2 + 0.5);
    double r = x - k * ln2;

    /* Compute e^r using Taylor series */
    double sum = 1.0;
    double term = 1.0;
    for (int i = 1; i <= EXP_POLY_DEGREE; i++) {
        term *= r / i;
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum))
            break;
    }

    /* Multiply by 2^k */
    return ldexp(sum, (int)k);
}

/** @brief Compute e raised to the power x (float version). */
float expf(float x) {
    return (float)exp((double)x);
}

/** @brief Compute 2 raised to the power x. */
double exp2(double x) {
    return pow(2.0, x);
}

/** @brief Compute e^x - 1, with improved accuracy for small x. */
double expm1(double x) {
    /* e^x - 1, accurate for small x */
    if (fabs(x) < 1e-10) {
        return x + 0.5 * x * x; /* Taylor approximation */
    }
    return exp(x) - 1.0;
}

/** @brief Compute the natural logarithm of x using argument reduction and series expansion. */
double log(double x) {
    /* Handle special cases */
    if (x < 0.0)
        return NAN;
    if (x == 0.0)
        return -INFINITY;
    if (isnan(x))
        return NAN;
    if (isinf(x))
        return INFINITY;

    /* Reduce to range [1, 2): x = m * 2^e where 1 <= m < 2 */
    int e;
    double m = frexp(x, &e);
    m *= 2.0;
    e--;

    /* Compute ln(m) where 1 <= m < 2 using series */
    /* ln(m) = ln((1+t)/(1-t)) = 2*(t + t^3/3 + t^5/5 + ...) where t = (m-1)/(m+1) */
    double t = (m - 1.0) / (m + 1.0);
    double t2 = t * t;

    double sum = t;
    double term = t;
    for (int i = 3; i <= 21; i += 2) {
        term *= t2;
        sum += term / i;
        if (fabs(term / i) < 1e-16 * fabs(sum))
            break;
    }
    sum *= 2.0;

    /* ln(x) = ln(m) + e * ln(2) */
    return sum + e * 0.693147180559945309417232121458;
}

/** @brief Compute the natural logarithm of x (float version). */
float logf(float x) {
    return (float)log((double)x);
}

/** @brief Compute the base-10 logarithm of x. */
double log10(double x) {
    return log(x) * 0.43429448190325182765; /* log10(e) */
}

/** @brief Compute the base-2 logarithm of x. */
double log2(double x) {
    return log(x) * 1.44269504088896340736; /* log2(e) */
}

/** @brief Compute ln(1 + x), with improved accuracy for small x. */
double log1p(double x) {
    /* ln(1 + x), accurate for small x */
    if (fabs(x) < 1e-10) {
        return x - 0.5 * x * x; /* Taylor approximation */
    }
    return log(1.0 + x);
}

/*
 * Trigonometric functions
 */

/** @brief Reduce an angle (in radians) to the range [-pi, pi]. */
static double reduce_angle(double x) {
    double twopi = 2.0 * M_PI;
    x = fmod(x, twopi);
    if (x > M_PI)
        x -= twopi;
    if (x < -M_PI)
        x += twopi;
    return x;
}

/** @brief Compute the sine of x (radians) using Taylor series with range reduction. */
double sin(double x) {
    if (!isfinite(x))
        return NAN;

    /* Reduce to [-pi, pi] */
    x = reduce_angle(x);

    /* Use Taylor series: sin(x) = x - x^3/3! + x^5/5! - ... */
    double x2 = x * x;
    double term = x;
    double sum = x;

    for (int i = 1; i <= 10; i++) {
        term *= -x2 / ((2 * i) * (2 * i + 1));
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum))
            break;
    }

    return sum;
}

/** @brief Compute the sine of x (float version). */
float sinf(float x) {
    return (float)sin((double)x);
}

/** @brief Compute the cosine of x (radians) using Taylor series with range reduction. */
double cos(double x) {
    if (!isfinite(x))
        return NAN;

    /* Reduce to [-pi, pi] */
    x = reduce_angle(x);

    /* Use Taylor series: cos(x) = 1 - x^2/2! + x^4/4! - ... */
    double x2 = x * x;
    double term = 1.0;
    double sum = 1.0;

    for (int i = 1; i <= 10; i++) {
        term *= -x2 / ((2 * i - 1) * (2 * i));
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum))
            break;
    }

    return sum;
}

/** @brief Compute the cosine of x (float version). */
float cosf(float x) {
    return (float)cos((double)x);
}

/** @brief Compute the tangent of x (radians). */
double tan(double x) {
    double c = cos(x);
    if (c == 0.0)
        return (sin(x) > 0) ? INFINITY : -INFINITY;
    return sin(x) / c;
}

/** @brief Compute the tangent of x (float version). */
float tanf(float x) {
    return (float)tan((double)x);
}

/** @brief Compute the arc sine of x; result in [-pi/2, pi/2]. */
double asin(double x) {
    if (x < -1.0 || x > 1.0)
        return NAN;
    if (x == 1.0)
        return M_PI_2;
    if (x == -1.0)
        return -M_PI_2;

    /* Use atan: asin(x) = atan(x / sqrt(1 - x^2)) */
    return atan(x / sqrt(1.0 - x * x));
}

/** @brief Compute the arc sine of x (float version). */
float asinf(float x) {
    return (float)asin((double)x);
}

/** @brief Compute the arc cosine of x; result in [0, pi]. */
double acos(double x) {
    if (x < -1.0 || x > 1.0)
        return NAN;
    return M_PI_2 - asin(x);
}

/** @brief Compute the arc cosine of x (float version). */
float acosf(float x) {
    return (float)acos((double)x);
}

/** @brief Compute the arc tangent of x; result in [-pi/2, pi/2]. */
double atan(double x) {
    /* Handle special cases */
    if (isnan(x))
        return NAN;
    if (x == INFINITY)
        return M_PI_2;
    if (x == -INFINITY)
        return -M_PI_2;

    /* Reduce argument to |x| <= 1 using atan(x) = pi/2 - atan(1/x) */
    int invert = 0;
    int neg = x < 0;
    if (neg)
        x = -x;
    if (x > 1.0) {
        x = 1.0 / x;
        invert = 1;
    }

    /* Further reduction using atan(x) = atan(c) + atan((x-c)/(1+x*c)) */
    /* Use c = 0.5, atan(0.5) ≈ 0.4636476... */
    double result;
    if (x > 0.4) {
        double c = 0.5;
        double atanc = 0.4636476090008061;
        double t = (x - c) / (1.0 + x * c);

        /* Taylor series for small t */
        double t2 = t * t;
        double sum = t;
        double term = t;
        for (int i = 1; i <= 15; i++) {
            term *= -t2;
            sum += term / (2 * i + 1);
        }
        result = atanc + sum;
    } else {
        /* Direct Taylor series: atan(x) = x - x^3/3 + x^5/5 - ... */
        double x2 = x * x;
        double sum = x;
        double term = x;
        for (int i = 1; i <= 15; i++) {
            term *= -x2;
            sum += term / (2 * i + 1);
        }
        result = sum;
    }

    if (invert)
        result = M_PI_2 - result;
    if (neg)
        result = -result;

    return result;
}

/** @brief Compute the arc tangent of x (float version). */
float atanf(float x) {
    return (float)atan((double)x);
}

/** @brief Compute the arc tangent of y/x, using signs to determine the quadrant. */
double atan2(double y, double x) {
    /* Handle special cases */
    if (isnan(x) || isnan(y))
        return NAN;

    if (x > 0.0) {
        return atan(y / x);
    } else if (x < 0.0) {
        if (y >= 0.0) {
            return atan(y / x) + M_PI;
        } else {
            return atan(y / x) - M_PI;
        }
    } else { /* x == 0 */
        if (y > 0.0)
            return M_PI_2;
        if (y < 0.0)
            return -M_PI_2;
        return 0.0; /* Both zero */
    }
}

/** @brief Compute the arc tangent of y/x (float version). */
float atan2f(float y, float x) {
    return (float)atan2((double)y, (double)x);
}

/*
 * Hyperbolic functions
 */

/** @brief Compute the hyperbolic sine of x. */
double sinh(double x) {
    if (fabs(x) < 1e-10) {
        return x; /* Taylor: sinh(x) ≈ x for small x */
    }
    double ex = exp(x);
    return (ex - 1.0 / ex) / 2.0;
}

/** @brief Compute the hyperbolic cosine of x. */
double cosh(double x) {
    double ex = exp(x);
    return (ex + 1.0 / ex) / 2.0;
}

/** @brief Compute the hyperbolic tangent of x. */
double tanh(double x) {
    if (x > 20.0)
        return 1.0;
    if (x < -20.0)
        return -1.0;
    double ex = exp(2.0 * x);
    return (ex - 1.0) / (ex + 1.0);
}

/** @brief Compute the inverse hyperbolic sine of x. */
double asinh(double x) {
    /* asinh(x) = ln(x + sqrt(x^2 + 1)) */
    if (fabs(x) < 1e-10)
        return x;
    return log(x + sqrt(x * x + 1.0));
}

/** @brief Compute the inverse hyperbolic cosine of x (x must be >= 1). */
double acosh(double x) {
    if (x < 1.0)
        return NAN;
    return log(x + sqrt(x * x - 1.0));
}

/** @brief Compute the inverse hyperbolic tangent of x (|x| must be < 1). */
double atanh(double x) {
    if (x <= -1.0 || x >= 1.0)
        return NAN;
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

/*
 * Floating-point manipulation functions
 */

/**
 * @brief Decompose x into a normalized fraction in [0.5, 1) and a power of 2.
 * @param exp Receives the exponent such that x = fraction * 2^exp.
 */
double frexp(double x, int *exp) {
    if (x == 0.0 || !isfinite(x)) {
        *exp = 0;
        return x;
    }

    unsigned long long bits = double_to_bits(x);
    int e = (int)((bits >> 52) & 0x7FF) - 1022;
    *exp = e;

    /* Set exponent to -1 (biased: 1022) to get mantissa in [0.5, 1) */
    bits = (bits & 0x800FFFFFFFFFFFFFULL) | (1022ULL << 52);
    return bits_to_double(bits);
}

/** @brief Multiply x by 2 raised to the power exp. */
double ldexp(double x, int exp) {
    if (x == 0.0 || !isfinite(x))
        return x;

    unsigned long long bits = double_to_bits(x);
    int e = (int)((bits >> 52) & 0x7FF);
    e += exp;

    if (e >= 2047)
        return (x > 0) ? INFINITY : -INFINITY;
    if (e <= 0)
        return 0.0;

    bits = (bits & 0x800FFFFFFFFFFFFFULL) | ((unsigned long long)e << 52);
    return bits_to_double(bits);
}

/**
 * @brief Split x into integer and fractional parts.
 * @param iptr Receives the integer part (with the same sign as x).
 * @return The fractional part of x.
 */
double modf(double x, double *iptr) {
    double i = trunc(x);
    if (iptr)
        *iptr = i;
    return x - i;
}

/** @brief Scale x by FLT_RADIX raised to the power n (equivalent to ldexp). */
double scalbn(double x, int n) {
    return ldexp(x, n);
}

/** @brief Extract the exponent of x as a signed integer. */
int ilogb(double x) {
    if (x == 0.0)
        return -2147483647 - 1; /* FP_ILOGB0 */
    if (!isfinite(x))
        return 2147483647; /* FP_ILOGBNAN/INF */

    int exp;
    frexp(x, &exp);
    return exp - 1;
}

/** @brief Extract the exponent of x as a double. */
double logb(double x) {
    return (double)ilogb(x);
}

/** @brief Return x with the sign of y. */
double copysign(double x, double y) {
    return __builtin_copysign(x, y);
}

/*
 * Error and gamma functions (basic implementations)
 */

/** @brief Compute the error function of x using Horner's method approximation. */
double erf(double x) {
    /* Approximation using Horner's method */
    /* erf(x) ≈ 1 - (a1*t + a2*t^2 + a3*t^3 + a4*t^4 + a5*t^5) * e^(-x^2) */
    /* where t = 1/(1 + p*x) */
    const double a1 = 0.254829592;
    const double a2 = -0.284496736;
    const double a3 = 1.421413741;
    const double a4 = -1.453152027;
    const double a5 = 1.061405429;
    const double p = 0.3275911;

    int sign = (x < 0) ? -1 : 1;
    x = fabs(x);

    double t = 1.0 / (1.0 + p * x);
    double y = 1.0 - (((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-x * x);

    return sign * y;
}

/** @brief Compute the complementary error function: 1 - erf(x). */
double erfc(double x) {
    return 1.0 - erf(x);
}

/** @brief Compute the gamma function of x using the Lanczos approximation. */
double tgamma(double x) {
    if (x <= 0.0 && x == floor(x)) {
        return NAN; /* Undefined for non-positive integers */
    }

    /* Reflection formula for x < 0.5 */
    if (x < 0.5) {
        return M_PI / (sin(M_PI * x) * tgamma(1.0 - x));
    }

    x -= 1.0;

    /* Lanczos coefficients for g = 7 */
    static const double c[] = {0.99999999999980993,
                               676.5203681218851,
                               -1259.1392167224028,
                               771.32342877765313,
                               -176.61502916214059,
                               12.507343278686905,
                               -0.13857109526572012,
                               9.9843695780195716e-6,
                               1.5056327351493116e-7};

    double sum = c[0];
    for (int i = 1; i < 9; i++) {
        sum += c[i] / (x + i);
    }

    double t = x + 7.5;
    return sqrt(2.0 * M_PI) * pow(t, x + 0.5) * exp(-t) * sum;
}

/** @brief Compute the natural log of the absolute value of gamma(x). */
double lgamma(double x) {
    return log(fabs(tgamma(x)));
}
