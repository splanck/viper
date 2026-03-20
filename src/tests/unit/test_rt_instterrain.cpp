//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_instterrain.cpp
// Purpose: Unit tests for InstanceBatch3D and Terrain3D.
//
// Links: rt_instbatch3d.h, rt_terrain3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_instbatch3d.h"
#include "rt_terrain3d.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mat4_identity(void);
extern void *rt_mat4_translate(double x, double y, double z);
extern void *rt_mesh3d_new_box(double sx, double sy, double sz);
extern void *rt_material3d_new_color(double r, double g, double b);
extern void *rt_pixels_new(int64_t w, int64_t h);
extern void rt_pixels_set(void *px, int64_t x, int64_t y, int64_t color);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); } \
    else { tests_passed++; } \
} while(0)

#define EXPECT_NEAR(a, b, eps, msg) do { \
    tests_run++; \
    if (fabs((double)(a) - (double)(b)) > (eps)) { fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b)); } \
    else { tests_passed++; } \
} while(0)

/*==========================================================================
 * InstanceBatch3D tests
 *=========================================================================*/

static void test_instbatch_create()
{
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mat = rt_material3d_new_color(0.5, 0.5, 0.5);
    void *batch = rt_instbatch3d_new(mesh, mat);
    EXPECT_TRUE(batch != nullptr, "InstanceBatch3D created");
    EXPECT_TRUE(rt_instbatch3d_count(batch) == 0, "Batch starts empty");
}

static void test_instbatch_add()
{
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mat = rt_material3d_new_color(0.5, 0.5, 0.5);
    void *batch = rt_instbatch3d_new(mesh, mat);

    rt_instbatch3d_add(batch, rt_mat4_identity());
    rt_instbatch3d_add(batch, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_instbatch3d_add(batch, rt_mat4_translate(2.0, 0.0, 0.0));
    EXPECT_TRUE(rt_instbatch3d_count(batch) == 3, "Batch count = 3 after 3 adds");
}

static void test_instbatch_remove()
{
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mat = rt_material3d_new_color(0.5, 0.5, 0.5);
    void *batch = rt_instbatch3d_new(mesh, mat);

    rt_instbatch3d_add(batch, rt_mat4_identity());
    rt_instbatch3d_add(batch, rt_mat4_translate(1.0, 0.0, 0.0));
    rt_instbatch3d_remove(batch, 0);
    EXPECT_TRUE(rt_instbatch3d_count(batch) == 1, "Batch count = 1 after remove");
}

static void test_instbatch_clear()
{
    void *mesh = rt_mesh3d_new_box(1.0, 1.0, 1.0);
    void *mat = rt_material3d_new_color(0.5, 0.5, 0.5);
    void *batch = rt_instbatch3d_new(mesh, mat);

    rt_instbatch3d_add(batch, rt_mat4_identity());
    rt_instbatch3d_add(batch, rt_mat4_identity());
    rt_instbatch3d_clear(batch);
    EXPECT_TRUE(rt_instbatch3d_count(batch) == 0, "Batch count = 0 after clear");
}

/*==========================================================================
 * Terrain3D tests
 *=========================================================================*/

static void test_terrain_create()
{
    void *terrain = rt_terrain3d_new(64, 64);
    EXPECT_TRUE(terrain != nullptr, "Terrain3D created");
}

static void test_terrain_flat_height()
{
    void *terrain = rt_terrain3d_new(32, 32);
    /* Default heights are 0, so any query should return 0 */
    EXPECT_NEAR(rt_terrain3d_get_height_at(terrain, 5.0, 5.0), 0.0, 0.01,
                "Flat terrain: height = 0");
}

static void test_terrain_normal_flat()
{
    void *terrain = rt_terrain3d_new(32, 32);
    void *normal = rt_terrain3d_get_normal_at(terrain, 5.0, 5.0);
    EXPECT_NEAR(rt_vec3_x(normal), 0.0, 0.01, "Flat normal X ≈ 0");
    EXPECT_NEAR(rt_vec3_y(normal), 1.0, 0.01, "Flat normal Y ≈ 1");
    EXPECT_NEAR(rt_vec3_z(normal), 0.0, 0.01, "Flat normal Z ≈ 0");
}

static void test_terrain_scale()
{
    void *terrain = rt_terrain3d_new(32, 32);
    rt_terrain3d_set_scale(terrain, 2.0, 10.0, 2.0);
    /* Still flat (heights=0), so height at any point should be 0 */
    EXPECT_NEAR(rt_terrain3d_get_height_at(terrain, 10.0, 10.0), 0.0, 0.01,
                "Scaled flat terrain: height = 0");
}

static void test_terrain_material()
{
    void *terrain = rt_terrain3d_new(32, 32);
    void *mat = rt_material3d_new_color(0.3, 0.6, 0.2);
    rt_terrain3d_set_material(terrain, mat);
    /* Just ensure no crash — material is used during draw */
    EXPECT_TRUE(terrain != nullptr, "Terrain with material set");
}

int main()
{
    /* InstanceBatch3D */
    test_instbatch_create();
    test_instbatch_add();
    test_instbatch_remove();
    test_instbatch_clear();

    /* Terrain3D */
    test_terrain_create();
    test_terrain_flat_height();
    test_terrain_normal_flat();
    test_terrain_scale();
    test_terrain_material();

    printf("InstanceBatch3D+Terrain3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
