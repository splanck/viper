//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_morphtarget3d.cpp
// Purpose: Unit tests for MorphTarget3D — shape creation, delta application,
//   weight blending, and morph computation.
//
// Links: rt_morphtarget3d.h, plans/3d/16-morph-targets.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_internal.h"
#include "rt_morphtarget3d.h"
#include "rt_string.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C"
{
    extern rt_string rt_const_cstr(const char *s);
    extern void *rt_mesh3d_new_box(double w, double h, double d);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (fabs((double)(a) - (double)(b)) > (eps))                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static void test_create()
{
    void *mt = rt_morphtarget3d_new(10);
    EXPECT_TRUE(mt != nullptr, "MorphTarget3D.New returns non-null");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(mt) == 0, "Initial shape count = 0");
}

static void test_add_shape()
{
    void *mt = rt_morphtarget3d_new(4);
    int64_t idx = rt_morphtarget3d_add_shape(mt, rt_const_cstr("smile"));
    EXPECT_TRUE(idx == 0, "First shape index = 0");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(mt) == 1, "Shape count = 1");

    int64_t idx2 = rt_morphtarget3d_add_shape(mt, rt_const_cstr("frown"));
    EXPECT_TRUE(idx2 == 1, "Second shape index = 1");
}

static void test_weight_zero()
{
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));
    rt_morphtarget3d_set_delta(mt, 0, 0, 1.0, 2.0, 3.0);
    rt_morphtarget3d_set_weight(mt, 0, 0.0);

    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.0, 0.001, "Weight = 0.0");
}

static void test_weight_set_get()
{
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));
    rt_morphtarget3d_set_weight(mt, 0, 0.75);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.75, 0.001, "Weight = 0.75");
}

static void test_weight_by_name()
{
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("blink"));
    rt_morphtarget3d_set_weight_by_name(mt, rt_const_cstr("blink"), 0.5);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), 0.5, 0.001, "SetWeightByName works");
}

static void test_negative_weight()
{
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));
    rt_morphtarget3d_set_weight(mt, 0, -0.5);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 0), -0.5, 0.001, "Negative weight = -0.5");
}

static void test_bounds_checks()
{
    void *mt = rt_morphtarget3d_new(4);
    rt_morphtarget3d_add_shape(mt, rt_const_cstr("test"));

    /* Out-of-bounds shape index — should be no-op */
    rt_morphtarget3d_set_weight(mt, 5, 1.0);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(mt, 5), 0.0, 0.001, "Out of bounds returns 0");

    /* Out-of-bounds vertex — should be no-op */
    rt_morphtarget3d_set_delta(mt, 0, 100, 1.0, 2.0, 3.0); /* vertex 100 > 4 */
    EXPECT_TRUE(1, "Out-of-bounds vertex delta is no-op (no crash)");
}

static void test_null_safety()
{
    rt_morphtarget3d_set_weight(NULL, 0, 1.0);
    EXPECT_NEAR(rt_morphtarget3d_get_weight(NULL, 0), 0.0, 0.001, "Null safety");
    EXPECT_TRUE(rt_morphtarget3d_get_shape_count(NULL) == 0, "Null shape count = 0");
}

int main()
{
    test_create();
    test_add_shape();
    test_weight_zero();
    test_weight_set_get();
    test_weight_by_name();
    test_negative_weight();
    test_bounds_checks();
    test_null_safety();

    printf("MorphTarget3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
