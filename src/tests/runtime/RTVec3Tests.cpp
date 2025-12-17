//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTVec3Tests.cpp
// Purpose: Tests for Viper.Vec3 3D vector math utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_vec3.h"

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
    void *v = rt_vec3_new(3.0, 4.0, 5.0);
    assert(v != nullptr);
    assert(approx_eq(rt_vec3_x(v), 3.0));
    assert(approx_eq(rt_vec3_y(v), 4.0));
    assert(approx_eq(rt_vec3_z(v), 5.0));
    printf("test_new: PASSED\n");
}

static void test_zero()
{
    void *v = rt_vec3_zero();
    assert(v != nullptr);
    assert(approx_eq(rt_vec3_x(v), 0.0));
    assert(approx_eq(rt_vec3_y(v), 0.0));
    assert(approx_eq(rt_vec3_z(v), 0.0));
    printf("test_zero: PASSED\n");
}

static void test_one()
{
    void *v = rt_vec3_one();
    assert(v != nullptr);
    assert(approx_eq(rt_vec3_x(v), 1.0));
    assert(approx_eq(rt_vec3_y(v), 1.0));
    assert(approx_eq(rt_vec3_z(v), 1.0));
    printf("test_one: PASSED\n");
}

// ============================================================================
// Arithmetic Operations
// ============================================================================

static void test_add()
{
    void *a = rt_vec3_new(1.0, 2.0, 3.0);
    void *b = rt_vec3_new(4.0, 5.0, 6.0);
    void *c = rt_vec3_add(a, b);
    assert(approx_eq(rt_vec3_x(c), 5.0));
    assert(approx_eq(rt_vec3_y(c), 7.0));
    assert(approx_eq(rt_vec3_z(c), 9.0));
    printf("test_add: PASSED\n");
}

static void test_sub()
{
    void *a = rt_vec3_new(5.0, 7.0, 9.0);
    void *b = rt_vec3_new(2.0, 3.0, 4.0);
    void *c = rt_vec3_sub(a, b);
    assert(approx_eq(rt_vec3_x(c), 3.0));
    assert(approx_eq(rt_vec3_y(c), 4.0));
    assert(approx_eq(rt_vec3_z(c), 5.0));
    printf("test_sub: PASSED\n");
}

static void test_mul()
{
    void *v = rt_vec3_new(3.0, 4.0, 5.0);
    void *r = rt_vec3_mul(v, 2.0);
    assert(approx_eq(rt_vec3_x(r), 6.0));
    assert(approx_eq(rt_vec3_y(r), 8.0));
    assert(approx_eq(rt_vec3_z(r), 10.0));
    printf("test_mul: PASSED\n");
}

static void test_div()
{
    void *v = rt_vec3_new(6.0, 8.0, 10.0);
    void *r = rt_vec3_div(v, 2.0);
    assert(approx_eq(rt_vec3_x(r), 3.0));
    assert(approx_eq(rt_vec3_y(r), 4.0));
    assert(approx_eq(rt_vec3_z(r), 5.0));
    printf("test_div: PASSED\n");
}

static void test_neg()
{
    void *v = rt_vec3_new(3.0, -4.0, 5.0);
    void *r = rt_vec3_neg(v);
    assert(approx_eq(rt_vec3_x(r), -3.0));
    assert(approx_eq(rt_vec3_y(r), 4.0));
    assert(approx_eq(rt_vec3_z(r), -5.0));
    printf("test_neg: PASSED\n");
}

// ============================================================================
// Vector Products
// ============================================================================

static void test_dot()
{
    void *a = rt_vec3_new(1.0, 2.0, 3.0);
    void *b = rt_vec3_new(4.0, 5.0, 6.0);
    double d = rt_vec3_dot(a, b);
    // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    assert(approx_eq(d, 32.0));
    printf("test_dot: PASSED\n");
}

static void test_cross()
{
    // Test: i × j = k (unit vectors)
    void *i = rt_vec3_new(1.0, 0.0, 0.0);
    void *j = rt_vec3_new(0.0, 1.0, 0.0);
    void *k = rt_vec3_cross(i, j);
    assert(approx_eq(rt_vec3_x(k), 0.0));
    assert(approx_eq(rt_vec3_y(k), 0.0));
    assert(approx_eq(rt_vec3_z(k), 1.0));

    // Test: j × i = -k
    void *neg_k = rt_vec3_cross(j, i);
    assert(approx_eq(rt_vec3_x(neg_k), 0.0));
    assert(approx_eq(rt_vec3_y(neg_k), 0.0));
    assert(approx_eq(rt_vec3_z(neg_k), -1.0));

    // Test: j × k = i
    void *k_vec = rt_vec3_new(0.0, 0.0, 1.0);
    void *i_result = rt_vec3_cross(j, k_vec);
    assert(approx_eq(rt_vec3_x(i_result), 1.0));
    assert(approx_eq(rt_vec3_y(i_result), 0.0));
    assert(approx_eq(rt_vec3_z(i_result), 0.0));

    printf("test_cross: PASSED\n");
}

static void test_cross_general()
{
    // General cross product: (1,2,3) × (4,5,6)
    // = (2*6 - 3*5, 3*4 - 1*6, 1*5 - 2*4)
    // = (12 - 15, 12 - 6, 5 - 8)
    // = (-3, 6, -3)
    void *a = rt_vec3_new(1.0, 2.0, 3.0);
    void *b = rt_vec3_new(4.0, 5.0, 6.0);
    void *c = rt_vec3_cross(a, b);
    assert(approx_eq(rt_vec3_x(c), -3.0));
    assert(approx_eq(rt_vec3_y(c), 6.0));
    assert(approx_eq(rt_vec3_z(c), -3.0));
    printf("test_cross_general: PASSED\n");
}

// ============================================================================
// Length and Distance
// ============================================================================

static void test_len()
{
    // 3-4-5 right triangle in 3D: (0,3,4) has length 5
    void *v = rt_vec3_new(0.0, 3.0, 4.0);
    double len = rt_vec3_len(v);
    assert(approx_eq(len, 5.0));
    printf("test_len: PASSED\n");
}

static void test_len_sq()
{
    void *v = rt_vec3_new(1.0, 2.0, 2.0);
    double len_sq = rt_vec3_len_sq(v);
    // 1 + 4 + 4 = 9
    assert(approx_eq(len_sq, 9.0));
    printf("test_len_sq: PASSED\n");
}

static void test_dist()
{
    void *a = rt_vec3_new(0.0, 0.0, 0.0);
    void *b = rt_vec3_new(0.0, 3.0, 4.0);
    double d = rt_vec3_dist(a, b);
    assert(approx_eq(d, 5.0));
    printf("test_dist: PASSED\n");
}

// ============================================================================
// Normalization and Interpolation
// ============================================================================

static void test_norm()
{
    void *v = rt_vec3_new(0.0, 3.0, 4.0);
    void *n = rt_vec3_norm(v);
    assert(approx_eq(rt_vec3_x(n), 0.0));
    assert(approx_eq(rt_vec3_y(n), 0.6));
    assert(approx_eq(rt_vec3_z(n), 0.8));
    // Length should be 1
    assert(approx_eq(rt_vec3_len(n), 1.0));
    printf("test_norm: PASSED\n");
}

static void test_norm_zero()
{
    void *v = rt_vec3_zero();
    void *n = rt_vec3_norm(v);
    // Should return zero vector
    assert(approx_eq(rt_vec3_x(n), 0.0));
    assert(approx_eq(rt_vec3_y(n), 0.0));
    assert(approx_eq(rt_vec3_z(n), 0.0));
    printf("test_norm_zero: PASSED\n");
}

static void test_lerp()
{
    void *a = rt_vec3_new(0.0, 0.0, 0.0);
    void *b = rt_vec3_new(10.0, 20.0, 30.0);

    void *mid = rt_vec3_lerp(a, b, 0.5);
    assert(approx_eq(rt_vec3_x(mid), 5.0));
    assert(approx_eq(rt_vec3_y(mid), 10.0));
    assert(approx_eq(rt_vec3_z(mid), 15.0));

    void *start = rt_vec3_lerp(a, b, 0.0);
    assert(approx_eq(rt_vec3_x(start), 0.0));
    assert(approx_eq(rt_vec3_y(start), 0.0));
    assert(approx_eq(rt_vec3_z(start), 0.0));

    void *end = rt_vec3_lerp(a, b, 1.0);
    assert(approx_eq(rt_vec3_x(end), 10.0));
    assert(approx_eq(rt_vec3_y(end), 20.0));
    assert(approx_eq(rt_vec3_z(end), 30.0));

    printf("test_lerp: PASSED\n");
}

// ============================================================================
// Combined Tests
// ============================================================================

static void test_perpendicular()
{
    void *a = rt_vec3_new(1.0, 0.0, 0.0);
    void *b = rt_vec3_new(0.0, 1.0, 0.0);

    // Perpendicular vectors have dot product = 0
    assert(approx_eq(rt_vec3_dot(a, b), 0.0));

    // Cross product of perpendicular unit vectors = unit vector
    void *c = rt_vec3_cross(a, b);
    assert(approx_eq(rt_vec3_len(c), 1.0));

    printf("test_perpendicular: PASSED\n");
}

static void test_cross_perpendicular()
{
    // Cross product is perpendicular to both input vectors
    void *a = rt_vec3_new(1.0, 2.0, 3.0);
    void *b = rt_vec3_new(4.0, 5.0, 6.0);
    void *c = rt_vec3_cross(a, b);

    // c should be perpendicular to both a and b
    assert(approx_eq(rt_vec3_dot(c, a), 0.0));
    assert(approx_eq(rt_vec3_dot(c, b), 0.0));

    printf("test_cross_perpendicular: PASSED\n");
}

int main()
{
    printf("=== Viper.Vec3 Tests ===\n\n");

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
    test_cross_general();

    // Length and distance
    test_len();
    test_len_sq();
    test_dist();

    // Normalization and interpolation
    test_norm();
    test_norm_zero();
    test_lerp();

    // Combined
    test_perpendicular();
    test_cross_perpendicular();

    printf("\nAll tests passed!\n");
    return 0;
}
