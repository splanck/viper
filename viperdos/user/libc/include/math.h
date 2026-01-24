#ifndef _MATH_H
#define _MATH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Mathematical constants */
#define M_E 2.71828182845904523536        /* e */
#define M_LOG2E 1.44269504088896340736    /* log2(e) */
#define M_LOG10E 0.43429448190325182765   /* log10(e) */
#define M_LN2 0.69314718055994530942      /* ln(2) */
#define M_LN10 2.30258509299404568402     /* ln(10) */
#define M_PI 3.14159265358979323846       /* pi */
#define M_PI_2 1.57079632679489661923     /* pi/2 */
#define M_PI_4 0.78539816339744830962     /* pi/4 */
#define M_1_PI 0.31830988618379067154     /* 1/pi */
#define M_2_PI 0.63661977236758134308     /* 2/pi */
#define M_2_SQRTPI 1.12837916709551257390 /* 2/sqrt(pi) */
#define M_SQRT2 1.41421356237309504880    /* sqrt(2) */
#define M_SQRT1_2 0.70710678118654752440  /* 1/sqrt(2) */

/* Infinity and NaN */
#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inf()
#define NAN __builtin_nan("")

/* Classification macros */
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4

#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)

/* Basic operations */
double fabs(double x);
float fabsf(float x);
double fmod(double x, double y);
float fmodf(float x, float y);
double remainder(double x, double y);
double fmax(double x, double y);
double fmin(double x, double y);
double fdim(double x, double y);

/* Exponential and logarithmic functions */
double exp(double x);
float expf(float x);
double exp2(double x);
double expm1(double x);
double log(double x);
float logf(float x);
double log10(double x);
double log2(double x);
double log1p(double x);

/* Power functions */
double pow(double base, double exp);
float powf(float base, float exp);
double sqrt(double x);
float sqrtf(float x);
double cbrt(double x);
double hypot(double x, double y);

/* Trigonometric functions */
double sin(double x);
float sinf(float x);
double cos(double x);
float cosf(float x);
double tan(double x);
float tanf(float x);
double asin(double x);
float asinf(float x);
double acos(double x);
float acosf(float x);
double atan(double x);
float atanf(float x);
double atan2(double y, double x);
float atan2f(float y, float x);

/* Hyperbolic functions */
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);

/* Rounding and remainder functions */
double ceil(double x);
float ceilf(float x);
double floor(double x);
float floorf(float x);
double trunc(double x);
float truncf(float x);
double round(double x);
float roundf(float x);
long lround(double x);
long long llround(double x);
double nearbyint(double x);
double rint(double x);
long lrint(double x);
long long llrint(double x);

/* Floating-point manipulation */
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *iptr);
double scalbn(double x, int n);
int ilogb(double x);
double logb(double x);
double copysign(double x, double y);

/* Error and gamma functions (stubs for now) */
double erf(double x);
double erfc(double x);
double tgamma(double x);
double lgamma(double x);

#ifdef __cplusplus
}
#endif

#endif /* _MATH_H */
