/**
 * @file mathtest.cpp
 * @brief Math library test program for ViperDOS
 */

#include <math.h>
#include <stdio.h>

/* Helper to check if two doubles are approximately equal */
static int approx_equal(double a, double b, double epsilon)
{
    if (isnan(a) && isnan(b))
        return 1;
    if (isinf(a) && isinf(b))
        return (a > 0) == (b > 0);
    return fabs(a - b) < epsilon;
}

#define TEST(name, expr, expected, eps)                                                            \
    do                                                                                             \
    {                                                                                              \
        double result = (expr);                                                                    \
        double exp = (expected);                                                                   \
        if (approx_equal(result, exp, eps))                                                        \
        {                                                                                          \
            printf("[PASS] %s = %f\n", name, result);                                              \
            passed++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("[FAIL] %s = %f (expected %f)\n", name, result, exp);                           \
            failed++;                                                                              \
        }                                                                                          \
    } while (0)

extern "C" void _start()
{
    int passed = 0;
    int failed = 0;

    printf("\n=== ViperDOS Math Library Tests ===\n\n");

    /* Basic operations */
    printf("--- Basic Operations ---\n");
    TEST("fabs(-3.5)", fabs(-3.5), 3.5, 1e-10);
    TEST("fmod(5.3, 2.0)", fmod(5.3, 2.0), 1.3, 1e-10);
    TEST("fmax(3.0, 5.0)", fmax(3.0, 5.0), 5.0, 1e-10);
    TEST("fmin(3.0, 5.0)", fmin(3.0, 5.0), 3.0, 1e-10);

    /* Rounding */
    printf("\n--- Rounding ---\n");
    TEST("floor(2.7)", floor(2.7), 2.0, 1e-10);
    TEST("floor(-2.7)", floor(-2.7), -3.0, 1e-10);
    TEST("ceil(2.3)", ceil(2.3), 3.0, 1e-10);
    TEST("ceil(-2.3)", ceil(-2.3), -2.0, 1e-10);
    TEST("round(2.5)", round(2.5), 3.0, 1e-10);
    TEST("round(-2.5)", round(-2.5), -3.0, 1e-10);
    TEST("trunc(2.7)", trunc(2.7), 2.0, 1e-10);
    TEST("trunc(-2.7)", trunc(-2.7), -2.0, 1e-10);

    /* Power functions */
    printf("\n--- Power Functions ---\n");
    TEST("sqrt(4.0)", sqrt(4.0), 2.0, 1e-10);
    TEST("sqrt(2.0)", sqrt(2.0), M_SQRT2, 1e-10);
    TEST("cbrt(8.0)", cbrt(8.0), 2.0, 1e-6);
    TEST("cbrt(-8.0)", cbrt(-8.0), -2.0, 1e-6);
    TEST("pow(2.0, 10.0)", pow(2.0, 10.0), 1024.0, 1e-10);
    TEST("pow(2.0, -1.0)", pow(2.0, -1.0), 0.5, 1e-10);
    TEST("hypot(3.0, 4.0)", hypot(3.0, 4.0), 5.0, 1e-10);

    /* Exponential and logarithmic */
    printf("\n--- Exponential/Logarithmic ---\n");
    TEST("exp(1.0)", exp(1.0), M_E, 1e-10);
    TEST("exp(0.0)", exp(0.0), 1.0, 1e-10);
    TEST("log(M_E)", log(M_E), 1.0, 1e-10);
    TEST("log(1.0)", log(1.0), 0.0, 1e-10);
    TEST("log10(100.0)", log10(100.0), 2.0, 1e-10);
    TEST("log2(8.0)", log2(8.0), 3.0, 1e-10);

    /* Trigonometric */
    printf("\n--- Trigonometric ---\n");
    TEST("sin(0.0)", sin(0.0), 0.0, 1e-10);
    TEST("sin(M_PI_2)", sin(M_PI_2), 1.0, 1e-10);
    TEST("sin(M_PI)", sin(M_PI), 0.0, 1e-10);
    TEST("cos(0.0)", cos(0.0), 1.0, 1e-10);
    TEST("cos(M_PI_2)", cos(M_PI_2), 0.0, 1e-10);
    TEST("cos(M_PI)", cos(M_PI), -1.0, 1e-10);
    TEST("tan(0.0)", tan(0.0), 0.0, 1e-10);
    TEST("tan(M_PI_4)", tan(M_PI_4), 1.0, 1e-10);

    /* Inverse trigonometric */
    printf("\n--- Inverse Trigonometric ---\n");
    TEST("asin(0.0)", asin(0.0), 0.0, 1e-10);
    TEST("asin(1.0)", asin(1.0), M_PI_2, 1e-10);
    TEST("acos(1.0)", acos(1.0), 0.0, 1e-10);
    TEST("acos(0.0)", acos(0.0), M_PI_2, 1e-10);
    TEST("atan(0.0)", atan(0.0), 0.0, 1e-10);
    TEST("atan(1.0)", atan(1.0), M_PI_4, 1e-6);
    TEST("atan2(1.0, 1.0)", atan2(1.0, 1.0), M_PI_4, 1e-6);
    TEST("atan2(1.0, 0.0)", atan2(1.0, 0.0), M_PI_2, 1e-10);

    /* Hyperbolic */
    printf("\n--- Hyperbolic ---\n");
    TEST("sinh(0.0)", sinh(0.0), 0.0, 1e-10);
    TEST("cosh(0.0)", cosh(0.0), 1.0, 1e-10);
    TEST("tanh(0.0)", tanh(0.0), 0.0, 1e-10);
    TEST("tanh(100.0)", tanh(100.0), 1.0, 1e-10);

    /* Special values */
    printf("\n--- Special Values ---\n");
    TEST("isnan(NAN)", isnan(NAN) ? 1.0 : 0.0, 1.0, 1e-10);
    TEST("isinf(INFINITY)", isinf(INFINITY) ? 1.0 : 0.0, 1.0, 1e-10);
    TEST("isfinite(1.0)", isfinite(1.0) ? 1.0 : 0.0, 1.0, 1e-10);
    TEST("isfinite(INFINITY)", isfinite(INFINITY) ? 1.0 : 0.0, 0.0, 1e-10);

    printf("\n=== Results: %d passed, %d failed ===\n\n", passed, failed);

    /* Exit with failure count as status */
    asm volatile("mov x0, %0; mov x8, #1; svc #0" ::"r"((long)failed));
    __builtin_unreachable();
}
