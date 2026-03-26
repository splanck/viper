//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_transform_path.cpp
// Purpose: Unit tests for Transform3D and Path3D.
//
// Links: rt_transform3d.h, rt_path3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_path3d.h"
#include "rt_transform3d.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C"
{
    extern void *rt_vec3_new(double x, double y, double z);
    extern double rt_vec3_x(void *v);
    extern double rt_vec3_y(void *v);
    extern double rt_vec3_z(void *v);
    extern void *rt_quat_new(double x, double y, double z, double w);
    extern double rt_quat_w(void *q);
    extern void *rt_mat4_new(double m0,
                             double m1,
                             double m2,
                             double m3,
                             double m4,
                             double m5,
                             double m6,
                             double m7,
                             double m8,
                             double m9,
                             double m10,
                             double m11,
                             double m12,
                             double m13,
                             double m14,
                             double m15);
    extern double rt_mat4_get(void *m, int64_t r, int64_t c);
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

/*==========================================================================
 * Transform3D tests
 *=========================================================================*/

static void test_transform_default()
{
    void *xf = rt_transform3d_new();
    EXPECT_TRUE(xf != nullptr, "Transform3D created");

    void *pos = rt_transform3d_get_position(xf);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.01, "Default pos X = 0");
    EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.01, "Default pos Y = 0");
    EXPECT_NEAR(rt_vec3_z(pos), 0.0, 0.01, "Default pos Z = 0");

    void *scl = rt_transform3d_get_scale(xf);
    EXPECT_NEAR(rt_vec3_x(scl), 1.0, 0.01, "Default scale X = 1");
    EXPECT_NEAR(rt_vec3_y(scl), 1.0, 0.01, "Default scale Y = 1");
    EXPECT_NEAR(rt_vec3_z(scl), 1.0, 0.01, "Default scale Z = 1");
}

static void test_transform_position()
{
    void *xf = rt_transform3d_new();
    rt_transform3d_set_position(xf, 3.0, 5.0, 7.0);

    void *mat = rt_transform3d_get_matrix(xf);
    /* Translation is in column 3 (row-major: indices 3, 7, 11) */
    EXPECT_NEAR(rt_mat4_get(mat, 0, 3), 3.0, 0.01, "Matrix tx = 3");
    EXPECT_NEAR(rt_mat4_get(mat, 1, 3), 5.0, 0.01, "Matrix ty = 5");
    EXPECT_NEAR(rt_mat4_get(mat, 2, 3), 7.0, 0.01, "Matrix tz = 7");
}

static void test_transform_scale()
{
    void *xf = rt_transform3d_new();
    rt_transform3d_set_scale(xf, 2.0, 3.0, 4.0);

    void *mat = rt_transform3d_get_matrix(xf);
    /* For identity rotation, diagonal = scale */
    EXPECT_NEAR(rt_mat4_get(mat, 0, 0), 2.0, 0.01, "Matrix sx = 2");
    EXPECT_NEAR(rt_mat4_get(mat, 1, 1), 3.0, 0.01, "Matrix sy = 3");
    EXPECT_NEAR(rt_mat4_get(mat, 2, 2), 4.0, 0.01, "Matrix sz = 4");
}

static void test_transform_translate()
{
    void *xf = rt_transform3d_new();
    rt_transform3d_set_position(xf, 1.0, 2.0, 3.0);
    rt_transform3d_translate(xf, rt_vec3_new(10.0, 20.0, 30.0));

    void *pos = rt_transform3d_get_position(xf);
    EXPECT_NEAR(rt_vec3_x(pos), 11.0, 0.01, "Translate: X = 1+10 = 11");
    EXPECT_NEAR(rt_vec3_y(pos), 22.0, 0.01, "Translate: Y = 2+20 = 22");
    EXPECT_NEAR(rt_vec3_z(pos), 33.0, 0.01, "Translate: Z = 3+30 = 33");
}

static void test_transform_euler()
{
    void *xf = rt_transform3d_new();
    /* 90° rotation around Y axis (yaw = π/2) */
    rt_transform3d_set_euler(xf, 0.0, 3.14159265358979323846 / 2.0, 0.0);

    void *rot = rt_transform3d_get_rotation(xf);
    /* Should produce a valid quaternion (w≈0.707, y≈0.707 for 90° Y rotation) */
    double w = rt_quat_w(rot);
    EXPECT_NEAR(fabs(w), 0.707, 0.02, "Euler 90° Y: quat w ≈ 0.707");
}

static void test_transform_dirty_flag()
{
    void *xf = rt_transform3d_new();
    void *mat1 = rt_transform3d_get_matrix(xf);     /* triggers compute */
    rt_transform3d_set_position(xf, 5.0, 0.0, 0.0); /* marks dirty */
    void *mat2 = rt_transform3d_get_matrix(xf);     /* triggers recompute */
    EXPECT_NEAR(rt_mat4_get(mat2, 0, 3), 5.0, 0.01, "Dirty flag: new position reflected");
    (void)mat1;
}

/*==========================================================================
 * Path3D tests
 *=========================================================================*/

static void test_path_linear()
{
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(10, 0, 0));

    void *p0 = rt_path3d_get_position_at(path, 0.0);
    EXPECT_NEAR(rt_vec3_x(p0), 0.0, 0.1, "Path t=0: X = 0");

    void *p1 = rt_path3d_get_position_at(path, 1.0);
    EXPECT_NEAR(rt_vec3_x(p1), 10.0, 0.1, "Path t=1: X = 10");

    void *pm = rt_path3d_get_position_at(path, 0.5);
    EXPECT_NEAR(rt_vec3_x(pm), 5.0, 0.5, "Path t=0.5: X ≈ 5");
}

static void test_path_catmull_rom()
{
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(5, 3, 0));
    rt_path3d_add_point(path, rt_vec3_new(10, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(15, 3, 0));

    /* Catmull-Rom passes through control points */
    void *p0 = rt_path3d_get_position_at(path, 0.0);
    EXPECT_NEAR(rt_vec3_x(p0), 0.0, 0.1, "CatmullRom t=0: first point");

    void *p1 = rt_path3d_get_position_at(path, 1.0);
    EXPECT_NEAR(rt_vec3_x(p1), 15.0, 0.1, "CatmullRom t=1: last point");

    EXPECT_TRUE(rt_path3d_get_point_count(path) == 4, "Point count = 4");
}

static void test_path_direction()
{
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(10, 0, 0));

    void *dir = rt_path3d_get_direction_at(path, 0.5);
    /* Direction should be roughly along +X */
    EXPECT_TRUE(rt_vec3_x(dir) > 0.9, "Direction along X for horizontal path");
    EXPECT_NEAR(rt_vec3_y(dir), 0.0, 0.1, "Direction Y ≈ 0");
}

static void test_path_length()
{
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(10, 0, 0));

    double len = rt_path3d_get_length(path);
    EXPECT_NEAR(len, 10.0, 0.5, "Straight path length ≈ 10");
}

static void test_path_looping()
{
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(5, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(5, 5, 0));
    rt_path3d_add_point(path, rt_vec3_new(0, 5, 0));
    rt_path3d_set_looping(path, 1);

    /* At t=0 and t=1, should be at same position (loop wraps) */
    void *p0 = rt_path3d_get_position_at(path, 0.0);
    void *p1 = rt_path3d_get_position_at(path, 1.0);
    double dx = rt_vec3_x(p1) - rt_vec3_x(p0);
    double dy = rt_vec3_y(p1) - rt_vec3_y(p0);
    EXPECT_NEAR(sqrt(dx * dx + dy * dy), 0.0, 1.0, "Looping path: t=0 ≈ t=1");
}

static void test_path_clear()
{
    void *path = rt_path3d_new();
    rt_path3d_add_point(path, rt_vec3_new(0, 0, 0));
    rt_path3d_add_point(path, rt_vec3_new(5, 0, 0));
    EXPECT_TRUE(rt_path3d_get_point_count(path) == 2, "Before clear: 2 points");
    rt_path3d_clear(path);
    EXPECT_TRUE(rt_path3d_get_point_count(path) == 0, "After clear: 0 points");
}

int main()
{
    /* Transform3D */
    test_transform_default();
    test_transform_position();
    test_transform_scale();
    test_transform_translate();
    test_transform_euler();
    test_transform_dirty_flag();

    /* Path3D */
    test_path_linear();
    test_path_catmull_rom();
    test_path_direction();
    test_path_length();
    test_path_looping();
    test_path_clear();

    printf("Transform3D+Path3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
