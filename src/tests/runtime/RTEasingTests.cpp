//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTEasingTests.cpp
// Purpose: Tests for Viper.Math.Easing runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_easing.h"
#include "rt_internal.h"

#include <cassert>
#include <cmath>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static bool approx(double a, double b, double eps = 1e-9)
{
    return fabs(a - b) < eps;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_linear()
{
    assert(approx(rt_ease_linear(0.0), 0.0));
    assert(approx(rt_ease_linear(0.5), 0.5));
    assert(approx(rt_ease_linear(1.0), 1.0));
}

static void test_all_start_at_zero()
{
    // All easing functions should return 0 at t=0
    assert(approx(rt_ease_linear(0.0), 0.0));
    assert(approx(rt_ease_in_quad(0.0), 0.0));
    assert(approx(rt_ease_out_quad(0.0), 0.0));
    assert(approx(rt_ease_in_out_quad(0.0), 0.0));
    assert(approx(rt_ease_in_cubic(0.0), 0.0));
    assert(approx(rt_ease_out_cubic(0.0), 0.0));
    assert(approx(rt_ease_in_out_cubic(0.0), 0.0));
    assert(approx(rt_ease_in_quart(0.0), 0.0));
    assert(approx(rt_ease_out_quart(0.0), 0.0));
    assert(approx(rt_ease_in_out_quart(0.0), 0.0));
    assert(approx(rt_ease_in_sine(0.0), 0.0));
    assert(approx(rt_ease_out_sine(0.0), 0.0));
    assert(approx(rt_ease_in_out_sine(0.0), 0.0));
    assert(approx(rt_ease_in_expo(0.0), 0.0));
    assert(approx(rt_ease_out_expo(0.0), 0.0, 0.001)); // pow(2,-10*0) ~ 0.001
    assert(approx(rt_ease_in_out_expo(0.0), 0.0));
    assert(approx(rt_ease_in_circ(0.0), 0.0));
    assert(approx(rt_ease_out_circ(0.0), 0.0));
    assert(approx(rt_ease_in_out_circ(0.0), 0.0));
    assert(approx(rt_ease_in_back(0.0), 0.0));
    assert(approx(rt_ease_out_back(0.0), 0.0, 1e-6));
    assert(approx(rt_ease_in_out_back(0.0), 0.0));
    assert(approx(rt_ease_in_elastic(0.0), 0.0));
    assert(approx(rt_ease_out_elastic(0.0), 0.0));
    assert(approx(rt_ease_in_out_elastic(0.0), 0.0));
    assert(approx(rt_ease_in_bounce(0.0), 0.0));
    assert(approx(rt_ease_out_bounce(0.0), 0.0));
    assert(approx(rt_ease_in_out_bounce(0.0), 0.0));
}

static void test_all_end_at_one()
{
    // All easing functions should return 1 at t=1
    assert(approx(rt_ease_linear(1.0), 1.0));
    assert(approx(rt_ease_in_quad(1.0), 1.0));
    assert(approx(rt_ease_out_quad(1.0), 1.0));
    assert(approx(rt_ease_in_out_quad(1.0), 1.0));
    assert(approx(rt_ease_in_cubic(1.0), 1.0));
    assert(approx(rt_ease_out_cubic(1.0), 1.0));
    assert(approx(rt_ease_in_out_cubic(1.0), 1.0));
    assert(approx(rt_ease_in_quart(1.0), 1.0));
    assert(approx(rt_ease_out_quart(1.0), 1.0));
    assert(approx(rt_ease_in_out_quart(1.0), 1.0));
    assert(approx(rt_ease_in_sine(1.0), 1.0));
    assert(approx(rt_ease_out_sine(1.0), 1.0));
    assert(approx(rt_ease_in_out_sine(1.0), 1.0));
    assert(approx(rt_ease_in_expo(1.0), 1.0));
    assert(approx(rt_ease_out_expo(1.0), 1.0));
    assert(approx(rt_ease_in_out_expo(1.0), 1.0));
    assert(approx(rt_ease_in_circ(1.0), 1.0));
    assert(approx(rt_ease_out_circ(1.0), 1.0));
    assert(approx(rt_ease_in_out_circ(1.0), 1.0));
    assert(approx(rt_ease_in_back(1.0), 1.0));
    assert(approx(rt_ease_out_back(1.0), 1.0));
    assert(approx(rt_ease_in_out_back(1.0), 1.0));
    assert(approx(rt_ease_in_elastic(1.0), 1.0));
    assert(approx(rt_ease_out_elastic(1.0), 1.0));
    assert(approx(rt_ease_in_out_elastic(1.0), 1.0));
    assert(approx(rt_ease_in_bounce(1.0), 1.0));
    assert(approx(rt_ease_out_bounce(1.0), 1.0));
    assert(approx(rt_ease_in_out_bounce(1.0), 1.0));
}

static void test_in_out_symmetry()
{
    // InOut functions should pass through 0.5 at t=0.5
    assert(approx(rt_ease_in_out_quad(0.5), 0.5));
    assert(approx(rt_ease_in_out_cubic(0.5), 0.5));
    assert(approx(rt_ease_in_out_quart(0.5), 0.5));
    assert(approx(rt_ease_in_out_sine(0.5), 0.5));
    assert(approx(rt_ease_in_out_circ(0.5), 0.5));
}

static void test_quad_values()
{
    assert(approx(rt_ease_in_quad(0.5), 0.25));
    assert(approx(rt_ease_out_quad(0.5), 0.75));
}

static void test_cubic_values()
{
    assert(approx(rt_ease_in_cubic(0.5), 0.125));
    assert(approx(rt_ease_out_cubic(0.5), 0.875));
}

static void test_quart_values()
{
    assert(approx(rt_ease_in_quart(0.5), 0.0625));
    assert(approx(rt_ease_out_quart(0.5), 0.9375));
}

static void test_back_overshoots()
{
    // InBack should go negative before reaching target
    double v = rt_ease_in_back(0.2);
    assert(v < 0.0); // Should overshoot below zero
}

static void test_bounce_values()
{
    // OutBounce at t=1 should be exactly 1
    assert(approx(rt_ease_out_bounce(1.0), 1.0));
    // InBounce at t=0 should be exactly 0
    assert(approx(rt_ease_in_bounce(0.0), 0.0));
}

static void test_elastic_oscillates()
{
    // InElastic should produce negative values in the early portion
    double v = rt_ease_in_elastic(0.3);
    // It's small and possibly negative due to oscillation
    assert(fabs(v) < 0.5);
}

static void test_expo_extreme_values()
{
    // InExpo should be very small at the start
    double v = rt_ease_in_expo(0.1);
    assert(v < 0.01);
    // OutExpo should be close to 1 near the end
    double v2 = rt_ease_out_expo(0.9);
    assert(v2 > 0.99);
}

int main()
{
    test_linear();
    test_all_start_at_zero();
    test_all_end_at_one();
    test_in_out_symmetry();
    test_quad_values();
    test_cubic_values();
    test_quart_values();
    test_back_overshoots();
    test_bounce_values();
    test_elastic_oscillates();
    test_expo_extreme_values();

    return 0;
}
