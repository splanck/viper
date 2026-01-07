//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTVec2Tests.cpp
// Purpose: Tests for Viper.Vec2 2D vector math utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_vec2.h"
#include "tests/common/PosixCompat.h"

#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static const double EPSILON = 1e-9;

static bool approx_eq(double a, double b)
{
    return fabs(a - b) < EPSILON;
}

// ============================================================================
// Constructors
// ============================================================================

static void test_new()
{
    void *v = rt_vec2_new(3.0, 4.0);
    assert(v != nullptr);
    assert(approx_eq(rt_vec2_x(v), 3.0));
    assert(approx_eq(rt_vec2_y(v), 4.0));
    printf("test_new: PASSED\n");
}

static void test_zero()
{
    void *v = rt_vec2_zero();
    assert(v != nullptr);
    assert(approx_eq(rt_vec2_x(v), 0.0));
    assert(approx_eq(rt_vec2_y(v), 0.0));
    printf("test_zero: PASSED\n");
}

static void test_one()
{
    void *v = rt_vec2_one();
    assert(v != nullptr);
    assert(approx_eq(rt_vec2_x(v), 1.0));
    assert(approx_eq(rt_vec2_y(v), 1.0));
    printf("test_one: PASSED\n");
}

// ============================================================================
// Arithmetic Operations
// ============================================================================

static void test_add()
{
    void *a = rt_vec2_new(1.0, 2.0);
    void *b = rt_vec2_new(3.0, 4.0);
    void *c = rt_vec2_add(a, b);
    assert(approx_eq(rt_vec2_x(c), 4.0));
    assert(approx_eq(rt_vec2_y(c), 6.0));
    printf("test_add: PASSED\n");
}

static void test_sub()
{
    void *a = rt_vec2_new(5.0, 7.0);
    void *b = rt_vec2_new(2.0, 3.0);
    void *c = rt_vec2_sub(a, b);
    assert(approx_eq(rt_vec2_x(c), 3.0));
    assert(approx_eq(rt_vec2_y(c), 4.0));
    printf("test_sub: PASSED\n");
}

static void test_mul()
{
    void *v = rt_vec2_new(3.0, 4.0);
    void *r = rt_vec2_mul(v, 2.0);
    assert(approx_eq(rt_vec2_x(r), 6.0));
    assert(approx_eq(rt_vec2_y(r), 8.0));
    printf("test_mul: PASSED\n");
}

static void test_div()
{
    void *v = rt_vec2_new(6.0, 8.0);
    void *r = rt_vec2_div(v, 2.0);
    assert(approx_eq(rt_vec2_x(r), 3.0));
    assert(approx_eq(rt_vec2_y(r), 4.0));
    printf("test_div: PASSED\n");
}

static void test_neg()
{
    void *v = rt_vec2_new(3.0, -4.0);
    void *r = rt_vec2_neg(v);
    assert(approx_eq(rt_vec2_x(r), -3.0));
    assert(approx_eq(rt_vec2_y(r), 4.0));
    printf("test_neg: PASSED\n");
}

// ============================================================================
// Vector Products
// ============================================================================

static void test_dot()
{
    void *a = rt_vec2_new(1.0, 2.0);
    void *b = rt_vec2_new(3.0, 4.0);
    double d = rt_vec2_dot(a, b);
    // 1*3 + 2*4 = 11
    assert(approx_eq(d, 11.0));
    printf("test_dot: PASSED\n");
}

static void test_cross()
{
    void *a = rt_vec2_new(1.0, 2.0);
    void *b = rt_vec2_new(3.0, 4.0);
    double c = rt_vec2_cross(a, b);
    // 1*4 - 2*3 = -2
    assert(approx_eq(c, -2.0));
    printf("test_cross: PASSED\n");
}

// ============================================================================
// Length and Distance
// ============================================================================

static void test_len()
{
    void *v = rt_vec2_new(3.0, 4.0);
    double len = rt_vec2_len(v);
    assert(approx_eq(len, 5.0));
    printf("test_len: PASSED\n");
}

static void test_len_sq()
{
    void *v = rt_vec2_new(3.0, 4.0);
    double len_sq = rt_vec2_len_sq(v);
    assert(approx_eq(len_sq, 25.0));
    printf("test_len_sq: PASSED\n");
}

static void test_dist()
{
    void *a = rt_vec2_new(0.0, 0.0);
    void *b = rt_vec2_new(3.0, 4.0);
    double d = rt_vec2_dist(a, b);
    assert(approx_eq(d, 5.0));
    printf("test_dist: PASSED\n");
}

// ============================================================================
// Normalization and Interpolation
// ============================================================================

static void test_norm()
{
    void *v = rt_vec2_new(3.0, 4.0);
    void *n = rt_vec2_norm(v);
    assert(approx_eq(rt_vec2_x(n), 0.6));
    assert(approx_eq(rt_vec2_y(n), 0.8));
    // Length should be 1
    assert(approx_eq(rt_vec2_len(n), 1.0));
    printf("test_norm: PASSED\n");
}

static void test_norm_zero()
{
    void *v = rt_vec2_zero();
    void *n = rt_vec2_norm(v);
    // Should return zero vector
    assert(approx_eq(rt_vec2_x(n), 0.0));
    assert(approx_eq(rt_vec2_y(n), 0.0));
    printf("test_norm_zero: PASSED\n");
}

static void test_lerp()
{
    void *a = rt_vec2_new(0.0, 0.0);
    void *b = rt_vec2_new(10.0, 20.0);

    void *mid = rt_vec2_lerp(a, b, 0.5);
    assert(approx_eq(rt_vec2_x(mid), 5.0));
    assert(approx_eq(rt_vec2_y(mid), 10.0));

    void *start = rt_vec2_lerp(a, b, 0.0);
    assert(approx_eq(rt_vec2_x(start), 0.0));
    assert(approx_eq(rt_vec2_y(start), 0.0));

    void *end = rt_vec2_lerp(a, b, 1.0);
    assert(approx_eq(rt_vec2_x(end), 10.0));
    assert(approx_eq(rt_vec2_y(end), 20.0));

    printf("test_lerp: PASSED\n");
}

// ============================================================================
// Angle and Rotation
// ============================================================================

static void test_angle()
{
    // Vector pointing right (positive x-axis)
    void *right = rt_vec2_new(1.0, 0.0);
    assert(approx_eq(rt_vec2_angle(right), 0.0));

    // Vector pointing up (positive y-axis)
    void *up = rt_vec2_new(0.0, 1.0);
    assert(approx_eq(rt_vec2_angle(up), M_PI / 2.0));

    // Vector pointing left (negative x-axis)
    void *left = rt_vec2_new(-1.0, 0.0);
    assert(approx_eq(rt_vec2_angle(left), M_PI));

    // Vector pointing down (negative y-axis)
    void *down = rt_vec2_new(0.0, -1.0);
    assert(approx_eq(rt_vec2_angle(down), -M_PI / 2.0));

    printf("test_angle: PASSED\n");
}

static void test_rotate()
{
    void *v = rt_vec2_new(1.0, 0.0);

    // Rotate 90 degrees (pi/2 radians)
    void *r90 = rt_vec2_rotate(v, M_PI / 2.0);
    assert(approx_eq(rt_vec2_x(r90), 0.0));
    assert(approx_eq(rt_vec2_y(r90), 1.0));

    // Rotate 180 degrees (pi radians)
    void *r180 = rt_vec2_rotate(v, M_PI);
    assert(approx_eq(rt_vec2_x(r180), -1.0));
    assert(approx_eq(rt_vec2_y(r180), 0.0));

    // Rotate 360 degrees (2*pi radians) - should return to original
    void *r360 = rt_vec2_rotate(v, 2.0 * M_PI);
    assert(approx_eq(rt_vec2_x(r360), 1.0));
    assert(approx_eq(rt_vec2_y(r360), 0.0));

    printf("test_rotate: PASSED\n");
}

// ============================================================================
// Combined Tests
// ============================================================================

static void test_pythagorean()
{
    // 3-4-5 triangle
    void *v = rt_vec2_new(3.0, 4.0);
    assert(approx_eq(rt_vec2_len(v), 5.0));

    // Normalize and scale back
    void *n = rt_vec2_norm(v);
    void *scaled = rt_vec2_mul(n, 5.0);
    assert(approx_eq(rt_vec2_x(scaled), 3.0));
    assert(approx_eq(rt_vec2_y(scaled), 4.0));

    printf("test_pythagorean: PASSED\n");
}

static void test_perpendicular()
{
    void *a = rt_vec2_new(1.0, 0.0);
    void *b = rt_vec2_new(0.0, 1.0);

    // Perpendicular vectors have dot product = 0
    assert(approx_eq(rt_vec2_dot(a, b), 0.0));

    // Cross product of perpendicular unit vectors = 1
    assert(approx_eq(rt_vec2_cross(a, b), 1.0));

    printf("test_perpendicular: PASSED\n");
}

int main()
{
    printf("=== Viper.Vec2 Tests ===\n\n");

    // Constructors
    test_new();
    test_zero();
    test_one();

    // Arithmetic
    test_add();
    test_sub();
    test_mul();
    test_div();
    test_neg();

    // Products
    test_dot();
    test_cross();

    // Length and distance
    test_len();
    test_len_sq();
    test_dist();

    // Normalization and interpolation
    test_norm();
    test_norm_zero();
    test_lerp();

    // Angle and rotation
    test_angle();
    test_rotate();

    // Combined
    test_pythagorean();
    test_perpendicular();

    printf("\nAll tests passed!\n");
    return 0;
}
