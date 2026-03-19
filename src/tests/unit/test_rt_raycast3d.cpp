//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_raycast3d.cpp
// Purpose: Unit tests for Ray3D, AABB3D collision — ray-triangle, ray-AABB,
//   ray-sphere, AABB overlap, AABB penetration.
//
// Links: rt_raycast3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_internal.h"
#include "rt_raycast3d.h"
#include "rt_string.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C"
{
    extern void *rt_vec3_new(double x, double y, double z);
    extern double rt_vec3_x(void *v);
    extern double rt_vec3_y(void *v);
    extern double rt_vec3_z(void *v);
    extern void *rt_mesh3d_new_box(double w, double h, double d);
    extern void *rt_mat4_identity(void);
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

static void test_ray_triangle_hit()
{
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *v0 = rt_vec3_new(-1, -1, 0);
    void *v1 = rt_vec3_new(1, -1, 0);
    void *v2 = rt_vec3_new(0, 1, 0);
    double t = rt_ray3d_intersect_triangle(origin, dir, v0, v1, v2);
    EXPECT_NEAR(t, 5.0, 0.01, "Ray-triangle hit at distance 5");
}

static void test_ray_triangle_miss()
{
    void *origin = rt_vec3_new(5, 5, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *v0 = rt_vec3_new(-1, -1, 0);
    void *v1 = rt_vec3_new(1, -1, 0);
    void *v2 = rt_vec3_new(0, 1, 0);
    double t = rt_ray3d_intersect_triangle(origin, dir, v0, v1, v2);
    EXPECT_TRUE(t < 0, "Ray-triangle miss returns -1");
}

static void test_ray_triangle_parallel()
{
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(1, 0, 0); /* parallel to triangle */
    void *v0 = rt_vec3_new(-1, -1, 0);
    void *v1 = rt_vec3_new(1, -1, 0);
    void *v2 = rt_vec3_new(0, 1, 0);
    double t = rt_ray3d_intersect_triangle(origin, dir, v0, v1, v2);
    EXPECT_TRUE(t < 0, "Ray parallel to triangle returns -1");
}

static void test_ray_aabb_hit()
{
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_NEAR(t, 4.0, 0.01, "Ray-AABB hit at distance 4");
}

static void test_ray_aabb_miss()
{
    void *origin = rt_vec3_new(5, 5, 5);
    void *dir = rt_vec3_new(0, 1, 0); /* pointing away */
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_TRUE(t < 0, "Ray-AABB miss returns -1");
}

static void test_ray_aabb_inside()
{
    void *origin = rt_vec3_new(0, 0, 0);
    void *dir = rt_vec3_new(0, 0, -1);
    void *mn = rt_vec3_new(-1, -1, -1);
    void *mx = rt_vec3_new(1, 1, 1);
    double t = rt_ray3d_intersect_aabb(origin, dir, mn, mx);
    EXPECT_NEAR(t, 0.0, 0.01, "Ray inside AABB returns 0");
}

static void test_ray_sphere_hit()
{
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *center = rt_vec3_new(0, 0, 0);
    double t = rt_ray3d_intersect_sphere(origin, dir, center, 1.0);
    EXPECT_NEAR(t, 4.0, 0.01, "Ray-sphere hit at distance 4");
}

static void test_ray_sphere_miss()
{
    void *origin = rt_vec3_new(5, 5, 5);
    void *dir = rt_vec3_new(0, 1, 0);
    void *center = rt_vec3_new(0, 0, 0);
    double t = rt_ray3d_intersect_sphere(origin, dir, center, 1.0);
    EXPECT_TRUE(t < 0, "Ray-sphere miss returns -1");
}

static void test_aabb_overlaps()
{
    void *a0 = rt_vec3_new(0, 0, 0), *a1 = rt_vec3_new(2, 2, 2);
    void *b0 = rt_vec3_new(1, 1, 1), *b1 = rt_vec3_new(3, 3, 3);
    EXPECT_TRUE(rt_aabb3d_overlaps(a0, a1, b0, b1) == 1, "Overlapping AABBs");
}

static void test_aabb_separate()
{
    void *a0 = rt_vec3_new(0, 0, 0), *a1 = rt_vec3_new(1, 1, 1);
    void *b0 = rt_vec3_new(5, 5, 5), *b1 = rt_vec3_new(6, 6, 6);
    EXPECT_TRUE(rt_aabb3d_overlaps(a0, a1, b0, b1) == 0, "Separate AABBs");
}

static void test_aabb_penetration()
{
    void *a0 = rt_vec3_new(0, 0, 0), *a1 = rt_vec3_new(2, 2, 2);
    void *b0 = rt_vec3_new(1.5, 0, 0), *b1 = rt_vec3_new(3.5, 2, 2);
    void *pen = rt_aabb3d_penetration(a0, a1, b0, b1);
    /* Overlap on X is 0.5, Y is 2.0, Z is 2.0. Min overlap = X. */
    double px = fabs(rt_vec3_x(pen));
    EXPECT_NEAR(px, 0.5, 0.01, "AABB penetration X = 0.5");
    EXPECT_NEAR(rt_vec3_y(pen), 0.0, 0.01, "AABB penetration Y = 0 (not min axis)");
}

static void test_ray_mesh()
{
    void *origin = rt_vec3_new(0, 0, 5);
    void *dir = rt_vec3_new(0, 0, -1);
    void *box = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *hit = rt_ray3d_intersect_mesh(origin, dir, box, rt_mat4_identity());
    EXPECT_TRUE(hit != nullptr, "Ray-mesh hit returns non-null");
    if (hit)
    {
        EXPECT_NEAR(rt_ray3d_hit_distance(hit), 4.0, 0.01, "Ray-mesh distance = 4");
        void *pt = rt_ray3d_hit_point(hit);
        EXPECT_NEAR(rt_vec3_z(pt), 1.0, 0.01, "Hit point Z = 1 (front face of box)");
    }
}

int main()
{
    test_ray_triangle_hit();
    test_ray_triangle_miss();
    test_ray_triangle_parallel();
    test_ray_aabb_hit();
    test_ray_aabb_miss();
    test_ray_aabb_inside();
    test_ray_sphere_hit();
    test_ray_sphere_miss();
    test_aabb_overlaps();
    test_aabb_separate();
    test_aabb_penetration();
    test_ray_mesh();

    printf("Raycast3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
