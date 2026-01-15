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
static inline unsigned long long double_to_bits(double x)
{
    union
    {
        double d;
        unsigned long long u;
    } u;

    u.d = x;
    return u.u;
}

/* Helper: create double from raw bits */
static inline double bits_to_double(unsigned long long bits)
{
    union
    {
        double d;
        unsigned long long u;
    } u;

    u.u = bits;
    return u.d;
}

/*
 * Basic operations
 */

double fabs(double x)
{
    return __builtin_fabs(x);
}

float fabsf(float x)
{
    return __builtin_fabsf(x);
}

double fmod(double x, double y)
{
    return __builtin_fmod(x, y);
}

float fmodf(float x, float y)
{
    return __builtin_fmodf(x, y);
}

double remainder(double x, double y)
{
    /* IEEE 754 remainder: x - n*y where n is nearest integer to x/y */
    double n = round(x / y);
    return x - n * y;
}

double fmax(double x, double y)
{
    return __builtin_fmax(x, y);
}

double fmin(double x, double y)
{
    return __builtin_fmin(x, y);
}

double fdim(double x, double y)
{
    return (x > y) ? (x - y) : 0.0;
}

/*
 * Rounding functions
 */

double ceil(double x)
{
    return __builtin_ceil(x);
}

float ceilf(float x)
{
    return __builtin_ceilf(x);
}

double floor(double x)
{
    return __builtin_floor(x);
}

float floorf(float x)
{
    return __builtin_floorf(x);
}

double trunc(double x)
{
    return __builtin_trunc(x);
}

float truncf(float x)
{
    return __builtin_truncf(x);
}

double round(double x)
{
    return __builtin_round(x);
}

float roundf(float x)
{
    return __builtin_roundf(x);
}

long lround(double x)
{
    return (long)round(x);
}

long long llround(double x)
{
    return (long long)round(x);
}

double nearbyint(double x)
{
    return __builtin_nearbyint(x);
}

double rint(double x)
{
    return __builtin_rint(x);
}

long lrint(double x)
{
    return (long)rint(x);
}

long long llrint(double x)
{
    return (long long)rint(x);
}

/*
 * Power functions
 */

double sqrt(double x)
{
    return __builtin_sqrt(x);
}

float sqrtf(float x)
{
    return __builtin_sqrtf(x);
}

double cbrt(double x)
{
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

double hypot(double x, double y)
{
    /* sqrt(x^2 + y^2) with overflow protection */
    x = fabs(x);
    y = fabs(y);

    if (x < y)
    {
        double t = x;
        x = y;
        y = t;
    }

    if (x == 0.0)
        return 0.0;

    double r = y / x;
    return x * sqrt(1.0 + r * r);
}

double pow(double base, double exponent)
{
    /* Handle special cases */
    if (exponent == 0.0)
        return 1.0;
    if (base == 1.0)
        return 1.0;
    if (base == 0.0)
    {
        if (exponent > 0.0)
            return 0.0;
        return INFINITY;
    }
    if (isnan(base) || isnan(exponent))
        return NAN;

    /* For integer exponents, use binary exponentiation */
    if (exponent == floor(exponent) && fabs(exponent) < 32)
    {
        int n = (int)exponent;
        int neg = n < 0;
        if (neg)
            n = -n;

        double result = 1.0;
        double b = base;
        while (n > 0)
        {
            if (n & 1)
                result *= b;
            b *= b;
            n >>= 1;
        }
        return neg ? (1.0 / result) : result;
    }

    /* General case: base^exp = e^(exp * ln(base)) */
    if (base < 0.0)
    {
        /* Negative base with non-integer exponent is undefined (returns NaN) */
        return NAN;
    }

    return exp(exponent * log(base));
}

float powf(float base, float exponent)
{
    return (float)pow((double)base, (double)exponent);
}

/*
 * Exponential and logarithmic functions
 */

/* Constants for exp approximation */
#define EXP_POLY_DEGREE 13

double exp(double x)
{
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
    for (int i = 1; i <= EXP_POLY_DEGREE; i++)
    {
        term *= r / i;
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum))
            break;
    }

    /* Multiply by 2^k */
    return ldexp(sum, (int)k);
}

float expf(float x)
{
    return (float)exp((double)x);
}

double exp2(double x)
{
    return pow(2.0, x);
}

double expm1(double x)
{
    /* e^x - 1, accurate for small x */
    if (fabs(x) < 1e-10)
    {
        return x + 0.5 * x * x; /* Taylor approximation */
    }
    return exp(x) - 1.0;
}

double log(double x)
{
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
    for (int i = 3; i <= 21; i += 2)
    {
        term *= t2;
        sum += term / i;
        if (fabs(term / i) < 1e-16 * fabs(sum))
            break;
    }
    sum *= 2.0;

    /* ln(x) = ln(m) + e * ln(2) */
    return sum + e * 0.693147180559945309417232121458;
}

float logf(float x)
{
    return (float)log((double)x);
}

double log10(double x)
{
    return log(x) * 0.43429448190325182765; /* log10(e) */
}

double log2(double x)
{
    return log(x) * 1.44269504088896340736; /* log2(e) */
}

double log1p(double x)
{
    /* ln(1 + x), accurate for small x */
    if (fabs(x) < 1e-10)
    {
        return x - 0.5 * x * x; /* Taylor approximation */
    }
    return log(1.0 + x);
}

/*
 * Trigonometric functions
 */

/* Reduce angle to [-pi, pi] */
static double reduce_angle(double x)
{
    double twopi = 2.0 * M_PI;
    x = fmod(x, twopi);
    if (x > M_PI)
        x -= twopi;
    if (x < -M_PI)
        x += twopi;
    return x;
}

double sin(double x)
{
    if (!isfinite(x))
        return NAN;

    /* Reduce to [-pi, pi] */
    x = reduce_angle(x);

    /* Use Taylor series: sin(x) = x - x^3/3! + x^5/5! - ... */
    double x2 = x * x;
    double term = x;
    double sum = x;

    for (int i = 1; i <= 10; i++)
    {
        term *= -x2 / ((2 * i) * (2 * i + 1));
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum))
            break;
    }

    return sum;
}

float sinf(float x)
{
    return (float)sin((double)x);
}

double cos(double x)
{
    if (!isfinite(x))
        return NAN;

    /* Reduce to [-pi, pi] */
    x = reduce_angle(x);

    /* Use Taylor series: cos(x) = 1 - x^2/2! + x^4/4! - ... */
    double x2 = x * x;
    double term = 1.0;
    double sum = 1.0;

    for (int i = 1; i <= 10; i++)
    {
        term *= -x2 / ((2 * i - 1) * (2 * i));
        sum += term;
        if (fabs(term) < 1e-16 * fabs(sum))
            break;
    }

    return sum;
}

float cosf(float x)
{
    return (float)cos((double)x);
}

double tan(double x)
{
    double c = cos(x);
    if (c == 0.0)
        return (sin(x) > 0) ? INFINITY : -INFINITY;
    return sin(x) / c;
}

float tanf(float x)
{
    return (float)tan((double)x);
}

double asin(double x)
{
    if (x < -1.0 || x > 1.0)
        return NAN;
    if (x == 1.0)
        return M_PI_2;
    if (x == -1.0)
        return -M_PI_2;

    /* Use atan: asin(x) = atan(x / sqrt(1 - x^2)) */
    return atan(x / sqrt(1.0 - x * x));
}

float asinf(float x)
{
    return (float)asin((double)x);
}

double acos(double x)
{
    if (x < -1.0 || x > 1.0)
        return NAN;
    return M_PI_2 - asin(x);
}

float acosf(float x)
{
    return (float)acos((double)x);
}

double atan(double x)
{
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
    if (x > 1.0)
    {
        x = 1.0 / x;
        invert = 1;
    }

    /* Further reduction using atan(x) = atan(c) + atan((x-c)/(1+x*c)) */
    /* Use c = 0.5, atan(0.5) ≈ 0.4636476... */
    double result;
    if (x > 0.4)
    {
        double c = 0.5;
        double atanc = 0.4636476090008061;
        double t = (x - c) / (1.0 + x * c);

        /* Taylor series for small t */
        double t2 = t * t;
        double sum = t;
        double term = t;
        for (int i = 1; i <= 15; i++)
        {
            term *= -t2;
            sum += term / (2 * i + 1);
        }
        result = atanc + sum;
    }
    else
    {
        /* Direct Taylor series: atan(x) = x - x^3/3 + x^5/5 - ... */
        double x2 = x * x;
        double sum = x;
        double term = x;
        for (int i = 1; i <= 15; i++)
        {
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

float atanf(float x)
{
    return (float)atan((double)x);
}

double atan2(double y, double x)
{
    /* Handle special cases */
    if (isnan(x) || isnan(y))
        return NAN;

    if (x > 0.0)
    {
        return atan(y / x);
    }
    else if (x < 0.0)
    {
        if (y >= 0.0)
        {
            return atan(y / x) + M_PI;
        }
        else
        {
            return atan(y / x) - M_PI;
        }
    }
    else
    { /* x == 0 */
        if (y > 0.0)
            return M_PI_2;
        if (y < 0.0)
            return -M_PI_2;
        return 0.0; /* Both zero */
    }
}

float atan2f(float y, float x)
{
    return (float)atan2((double)y, (double)x);
}

/*
 * Hyperbolic functions
 */

double sinh(double x)
{
    if (fabs(x) < 1e-10)
    {
        return x; /* Taylor: sinh(x) ≈ x for small x */
    }
    double ex = exp(x);
    return (ex - 1.0 / ex) / 2.0;
}

double cosh(double x)
{
    double ex = exp(x);
    return (ex + 1.0 / ex) / 2.0;
}

double tanh(double x)
{
    if (x > 20.0)
        return 1.0;
    if (x < -20.0)
        return -1.0;
    double ex = exp(2.0 * x);
    return (ex - 1.0) / (ex + 1.0);
}

double asinh(double x)
{
    /* asinh(x) = ln(x + sqrt(x^2 + 1)) */
    if (fabs(x) < 1e-10)
        return x;
    return log(x + sqrt(x * x + 1.0));
}

double acosh(double x)
{
    if (x < 1.0)
        return NAN;
    return log(x + sqrt(x * x - 1.0));
}

double atanh(double x)
{
    if (x <= -1.0 || x >= 1.0)
        return NAN;
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

/*
 * Floating-point manipulation functions
 */

double frexp(double x, int *exp)
{
    if (x == 0.0 || !isfinite(x))
    {
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

double ldexp(double x, int exp)
{
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

double modf(double x, double *iptr)
{
    double i = trunc(x);
    if (iptr)
        *iptr = i;
    return x - i;
}

double scalbn(double x, int n)
{
    return ldexp(x, n);
}

int ilogb(double x)
{
    if (x == 0.0)
        return -2147483647 - 1; /* FP_ILOGB0 */
    if (!isfinite(x))
        return 2147483647; /* FP_ILOGBNAN/INF */

    int exp;
    frexp(x, &exp);
    return exp - 1;
}

double logb(double x)
{
    return (double)ilogb(x);
}

double copysign(double x, double y)
{
    return __builtin_copysign(x, y);
}

/*
 * Error and gamma functions (basic implementations)
 */

double erf(double x)
{
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

double erfc(double x)
{
    return 1.0 - erf(x);
}

/* Lanczos approximation for gamma function */
double tgamma(double x)
{
    if (x <= 0.0 && x == floor(x))
    {
        return NAN; /* Undefined for non-positive integers */
    }

    /* Reflection formula for x < 0.5 */
    if (x < 0.5)
    {
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
    for (int i = 1; i < 9; i++)
    {
        sum += c[i] / (x + i);
    }

    double t = x + 7.5;
    return sqrt(2.0 * M_PI) * pow(t, x + 0.5) * exp(-t) * sum;
}

double lgamma(double x)
{
    return log(fabs(tgamma(x)));
}
