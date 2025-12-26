//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTMathExtTests.cpp
// Purpose: Validate extended runtime math operations in rt_math.c.
// Key invariants: Math functions produce correct results, constants are
//                 accurate, wrap/clamp handle edge cases correctly.
// Links: docs/viperlib.md

#include "rt_math.h"

#include <cassert>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Check if two doubles are approximately equal.
static bool approx_eq(double a, double b, double eps = 1e-10)
{
    return fabs(a - b) < eps;
}

/// @brief Test inverse trigonometric functions.
static void test_inverse_trig()
{
    printf("Testing inverse trig:\n");

    // atan2
    test_result("atan2(1,1) = pi/4", approx_eq(rt_atan2(1.0, 1.0), M_PI / 4.0));
    test_result("atan2(0,1) = 0", approx_eq(rt_atan2(0.0, 1.0), 0.0));
    test_result("atan2(1,0) = pi/2", approx_eq(rt_atan2(1.0, 0.0), M_PI / 2.0));

    // asin
    test_result("asin(0) = 0", approx_eq(rt_asin(0.0), 0.0));
    test_result("asin(1) = pi/2", approx_eq(rt_asin(1.0), M_PI / 2.0));
    test_result("asin(-1) = -pi/2", approx_eq(rt_asin(-1.0), -M_PI / 2.0));

    // acos
    test_result("acos(1) = 0", approx_eq(rt_acos(1.0), 0.0));
    test_result("acos(0) = pi/2", approx_eq(rt_acos(0.0), M_PI / 2.0));
    test_result("acos(-1) = pi", approx_eq(rt_acos(-1.0), M_PI));

    printf("\n");
}

/// @brief Test hyperbolic functions.
static void test_hyperbolic()
{
    printf("Testing hyperbolic:\n");

    test_result("sinh(0) = 0", approx_eq(rt_sinh(0.0), 0.0));
    test_result("cosh(0) = 1", approx_eq(rt_cosh(0.0), 1.0));
    test_result("tanh(0) = 0", approx_eq(rt_tanh(0.0), 0.0));

    // sinh and cosh relationship: cosh^2 - sinh^2 = 1
    double x = 1.5;
    double s = rt_sinh(x);
    double c = rt_cosh(x);
    test_result("cosh^2 - sinh^2 = 1", approx_eq(c * c - s * s, 1.0));

    printf("\n");
}

/// @brief Test rounding functions.
static void test_rounding()
{
    printf("Testing rounding:\n");

    // round
    test_result("round(2.3) = 2", approx_eq(rt_round(2.3), 2.0));
    test_result("round(2.7) = 3", approx_eq(rt_round(2.7), 3.0));
    test_result("round(-2.3) = -2", approx_eq(rt_round(-2.3), -2.0));
    test_result("round(-2.7) = -3", approx_eq(rt_round(-2.7), -3.0));

    // trunc
    test_result("trunc(2.7) = 2", approx_eq(rt_trunc(2.7), 2.0));
    test_result("trunc(-2.7) = -2", approx_eq(rt_trunc(-2.7), -2.0));

    printf("\n");
}

/// @brief Test logarithm functions.
static void test_logarithms()
{
    printf("Testing logarithms:\n");

    test_result("log10(10) = 1", approx_eq(rt_log10(10.0), 1.0));
    test_result("log10(100) = 2", approx_eq(rt_log10(100.0), 2.0));
    test_result("log2(2) = 1", approx_eq(rt_log2(2.0), 1.0));
    test_result("log2(8) = 3", approx_eq(rt_log2(8.0), 3.0));

    printf("\n");
}

/// @brief Test clamp functions.
static void test_clamp()
{
    printf("Testing clamp:\n");

    // Float clamp
    test_result("clamp(5, 0, 10) = 5", approx_eq(rt_clamp_f64(5.0, 0.0, 10.0), 5.0));
    test_result("clamp(-5, 0, 10) = 0", approx_eq(rt_clamp_f64(-5.0, 0.0, 10.0), 0.0));
    test_result("clamp(15, 0, 10) = 10", approx_eq(rt_clamp_f64(15.0, 0.0, 10.0), 10.0));

    // Int clamp
    test_result("clampInt(5, 0, 10) = 5", rt_clamp_i64(5, 0, 10) == 5);
    test_result("clampInt(-5, 0, 10) = 0", rt_clamp_i64(-5, 0, 10) == 0);
    test_result("clampInt(15, 0, 10) = 10", rt_clamp_i64(15, 0, 10) == 10);

    printf("\n");
}

/// @brief Test lerp function.
static void test_lerp()
{
    printf("Testing lerp:\n");

    test_result("lerp(0, 10, 0) = 0", approx_eq(rt_lerp(0.0, 10.0, 0.0), 0.0));
    test_result("lerp(0, 10, 1) = 10", approx_eq(rt_lerp(0.0, 10.0, 1.0), 10.0));
    test_result("lerp(0, 10, 0.5) = 5", approx_eq(rt_lerp(0.0, 10.0, 0.5), 5.0));
    test_result("lerp(10, 20, 0.25) = 12.5", approx_eq(rt_lerp(10.0, 20.0, 0.25), 12.5));

    printf("\n");
}

/// @brief Test wrap functions.
static void test_wrap()
{
    printf("Testing wrap:\n");

    // Float wrap
    test_result("wrap(5, 0, 10) = 5", approx_eq(rt_wrap_f64(5.0, 0.0, 10.0), 5.0));
    test_result("wrap(12, 0, 10) = 2", approx_eq(rt_wrap_f64(12.0, 0.0, 10.0), 2.0));
    test_result("wrap(-3, 0, 10) = 7", approx_eq(rt_wrap_f64(-3.0, 0.0, 10.0), 7.0));
    test_result("wrap(360, 0, 360) = 0", approx_eq(rt_wrap_f64(360.0, 0.0, 360.0), 0.0));

    // Int wrap
    test_result("wrapInt(5, 0, 10) = 5", rt_wrap_i64(5, 0, 10) == 5);
    test_result("wrapInt(12, 0, 10) = 2", rt_wrap_i64(12, 0, 10) == 2);
    test_result("wrapInt(-3, 0, 10) = 7", rt_wrap_i64(-3, 0, 10) == 7);

    printf("\n");
}

/// @brief Test mathematical constants.
static void test_constants()
{
    printf("Testing constants:\n");

    test_result("Pi approx 3.14159", approx_eq(rt_math_pi(), 3.14159265358979323846, 1e-14));
    test_result("E approx 2.71828", approx_eq(rt_math_e(), 2.71828182845904523536, 1e-14));
    test_result("Tau = 2*Pi", approx_eq(rt_math_tau(), 2.0 * rt_math_pi()));

    printf("\n");
}

/// @brief Test angle conversion functions.
static void test_angle_conversion()
{
    printf("Testing angle conversion:\n");

    test_result("deg(pi) = 180", approx_eq(rt_deg(M_PI), 180.0));
    test_result("deg(pi/2) = 90", approx_eq(rt_deg(M_PI / 2.0), 90.0));
    test_result("rad(180) = pi", approx_eq(rt_rad(180.0), M_PI));
    test_result("rad(90) = pi/2", approx_eq(rt_rad(90.0), M_PI / 2.0));

    // Round trip
    test_result("deg(rad(45)) = 45", approx_eq(rt_deg(rt_rad(45.0)), 45.0));

    printf("\n");
}

/// @brief Test utility functions.
static void test_utility()
{
    printf("Testing utility:\n");

    // fmod
    test_result("fmod(5.5, 2.0) = 1.5", approx_eq(rt_fmod(5.5, 2.0), 1.5));
    test_result("fmod(-5.5, 2.0) = -1.5", approx_eq(rt_fmod(-5.5, 2.0), -1.5));

    // hypot
    test_result("hypot(3, 4) = 5", approx_eq(rt_hypot(3.0, 4.0), 5.0));
    test_result("hypot(5, 12) = 13", approx_eq(rt_hypot(5.0, 12.0), 13.0));

    printf("\n");
}

/// @brief Entry point for extended math tests.
int main()
{
    printf("=== RT Math Extension Tests ===\n\n");

    test_inverse_trig();
    test_hyperbolic();
    test_rounding();
    test_logarithms();
    test_clamp();
    test_lerp();
    test_wrap();
    test_constants();
    test_angle_conversion();
    test_utility();

    printf("All extended math tests passed!\n");
    return 0;
}
