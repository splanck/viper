//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTPerlinTests.cpp
// Purpose: Tests for Viper.Math.PerlinNoise runtime helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_perlin.h"
#include "rt_object.h"

#include <cassert>
#include <cmath>
#include <csetjmp>

namespace
{
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

static void rt_release_obj(void *p)
{
    if (p && rt_obj_release_check0(p))
        rt_obj_free(p);
}

static bool approx(double a, double b, double eps = 1e-9)
{
    return fabs(a - b) < eps;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_new_returns_nonnull()
{
    void *p = rt_perlin_new(42);
    assert(p != nullptr);
    rt_release_obj(p);
}

static void test_deterministic_seed()
{
    void *p1 = rt_perlin_new(42);
    void *p2 = rt_perlin_new(42);
    double v1 = rt_perlin_noise2d(p1, 1.5, 2.5);
    double v2 = rt_perlin_noise2d(p2, 1.5, 2.5);
    assert(v1 == v2);
    rt_release_obj(p1);
    rt_release_obj(p2);
}

static void test_different_seeds_differ()
{
    void *p1 = rt_perlin_new(1);
    void *p2 = rt_perlin_new(12345);
    // Check multiple points — at least one should differ
    bool found_diff = false;
    for (int i = 0; i < 10; ++i)
    {
        double x = 0.5 + (double)i * 1.7;
        double y = 0.5 + (double)i * 2.3;
        double v1 = rt_perlin_noise2d(p1, x, y);
        double v2 = rt_perlin_noise2d(p2, x, y);
        if (v1 != v2)
        {
            found_diff = true;
            break;
        }
    }
    assert(found_diff);
    rt_release_obj(p1);
    rt_release_obj(p2);
}

static void test_noise2d_range()
{
    void *p = rt_perlin_new(123);
    // Sample many points and verify output is in [-1, 1]
    for (int i = 0; i < 100; ++i)
    {
        double x = (double)i * 0.37;
        double y = (double)i * 0.53;
        double v = rt_perlin_noise2d(p, x, y);
        assert(v >= -1.0 && v <= 1.0);
    }
    rt_release_obj(p);
}

static void test_noise3d_range()
{
    void *p = rt_perlin_new(456);
    for (int i = 0; i < 100; ++i)
    {
        double x = (double)i * 0.29;
        double y = (double)i * 0.41;
        double z = (double)i * 0.67;
        double v = rt_perlin_noise3d(p, x, y, z);
        assert(v >= -1.5 && v <= 1.5); // 3D gradient can slightly exceed [-1,1]
    }
    rt_release_obj(p);
}

static void test_noise2d_continuity()
{
    void *p = rt_perlin_new(789);
    // Nearby points should produce similar values (continuity)
    double v1 = rt_perlin_noise2d(p, 5.0, 5.0);
    double v2 = rt_perlin_noise2d(p, 5.001, 5.001);
    assert(fabs(v1 - v2) < 0.1); // Should be very close
    rt_release_obj(p);
}

static void test_noise3d_deterministic()
{
    void *p = rt_perlin_new(42);
    double v1 = rt_perlin_noise3d(p, 1.0, 2.0, 3.0);
    double v2 = rt_perlin_noise3d(p, 1.0, 2.0, 3.0);
    assert(v1 == v2);
    rt_release_obj(p);
}

static void test_octave2d_basic()
{
    void *p = rt_perlin_new(42);
    double v = rt_perlin_octave2d(p, 1.5, 2.5, 4, 0.5);
    // Octave noise should still be in a reasonable range
    assert(v >= -2.0 && v <= 2.0);
    rt_release_obj(p);
}

static void test_octave3d_basic()
{
    void *p = rt_perlin_new(42);
    double v = rt_perlin_octave3d(p, 1.0, 2.0, 3.0, 4, 0.5);
    assert(v >= -2.0 && v <= 2.0);
    rt_release_obj(p);
}

static void test_octave_single_equals_noise()
{
    void *p = rt_perlin_new(42);
    double noise = rt_perlin_noise2d(p, 3.0, 4.0);
    double octave = rt_perlin_octave2d(p, 3.0, 4.0, 1, 0.5);
    // With 1 octave, result should equal raw noise
    assert(approx(noise, octave));
    rt_release_obj(p);
}

static void test_null_safety()
{
    assert(rt_perlin_noise2d(nullptr, 0.0, 0.0) == 0.0);
    assert(rt_perlin_noise3d(nullptr, 0.0, 0.0, 0.0) == 0.0);
    assert(rt_perlin_octave2d(nullptr, 0.0, 0.0, 4, 0.5) == 0.0);
    assert(rt_perlin_octave3d(nullptr, 0.0, 0.0, 0.0, 4, 0.5) == 0.0);
}

static void test_octave_zero_returns_zero()
{
    void *p = rt_perlin_new(42);
    assert(rt_perlin_octave2d(p, 1.0, 2.0, 0, 0.5) == 0.0);
    assert(rt_perlin_octave3d(p, 1.0, 2.0, 3.0, 0, 0.5) == 0.0);
    rt_release_obj(p);
}

static void test_integer_coordinates()
{
    void *p = rt_perlin_new(42);
    // At integer coordinates, gradient contributions cancel -> should be 0 or near 0
    double v = rt_perlin_noise2d(p, 0.0, 0.0);
    assert(fabs(v) < 0.01);
    rt_release_obj(p);
}

int main()
{
    test_new_returns_nonnull();
    test_deterministic_seed();
    test_different_seeds_differ();
    test_noise2d_range();
    test_noise3d_range();
    test_noise2d_continuity();
    test_noise3d_deterministic();
    test_octave2d_basic();
    test_octave3d_basic();
    test_octave_single_equals_noise();
    test_null_safety();
    test_octave_zero_returns_zero();
    test_integer_coordinates();

    return 0;
}
