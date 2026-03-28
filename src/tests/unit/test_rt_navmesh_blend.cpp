//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_navmesh_blend.cpp
// Purpose: Unit tests for NavMesh3D and AnimBlend3D.
//
// Links: rt_navmesh3d.h, rt_skeleton3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_navmesh3d.h"
#include "rt_path3d.h"
#include "rt_skeleton3d.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mesh3d_new_plane(double sx, double sz);
extern void *rt_mesh3d_new_box(double sx, double sy, double sz);
extern int64_t rt_path3d_get_point_count(void *path);
extern void *rt_mat4_identity(void);
extern void *rt_skeleton3d_new(void);
extern int64_t rt_skeleton3d_add_bone(void *s, rt_string n, int64_t p, void *m);
extern void rt_skeleton3d_compute_inverse_bind(void *s);
extern void *rt_animation3d_new(rt_string name, double duration);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s\n", msg);                                                    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (fabs((double)(a) - (double)(b)) > (eps)) {                                             \
            fprintf(stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));    \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

/*==========================================================================
 * NavMesh3D tests
 *=========================================================================*/

static void test_navmesh_build_plane() {
    /* A plane mesh is flat (normal = 0,1,0), so all triangles should be walkable */
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);
    EXPECT_TRUE(nm != nullptr, "NavMesh built from plane");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) > 0, "Plane has walkable triangles");
}

static void test_navmesh_is_walkable() {
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);

    /* Center of plane should be walkable */
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, center) != 0, "Center of plane is walkable");
}

static void test_navmesh_not_walkable() {
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);

    /* Far away point should not be walkable */
    void *far_pt = rt_vec3_new(100.0, 0.0, 100.0);
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, far_pt) == 0, "Far point is not walkable");
}

static void test_navmesh_sample_position() {
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);

    void *pt = rt_vec3_new(2.0, 5.0, 2.0); /* above the plane */
    void *snapped = rt_navmesh3d_sample_position(nm, pt);
    /* Y should be snapped to surface (near 0) */
    EXPECT_NEAR(rt_vec3_y(snapped), 0.0, 0.5, "Sampled Y ≈ 0 (plane surface)");
}

static void test_navmesh_find_path() {
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);

    void *from = rt_vec3_new(-3.0, 0.0, -3.0);
    void *to = rt_vec3_new(3.0, 0.0, 3.0);
    void *path = rt_navmesh3d_find_path(nm, from, to);
    EXPECT_TRUE(path != nullptr, "Path found on plane");
    if (path) {
        EXPECT_TRUE(rt_path3d_get_point_count(path) >= 2, "Path has at least 2 points");
    }
}

static void test_navmesh_box_slope_filter() {
    /* A box has vertical walls which should be excluded by slope filter */
    void *box = rt_mesh3d_new_box(5.0, 5.0, 5.0);
    void *nm = rt_navmesh3d_build(box, 0.4, 1.8);

    int64_t tri_count = rt_navmesh3d_get_triangle_count(nm);
    /* Box has 12 triangles total (6 faces × 2 tris each).
     * Only top face (2 tris) should be walkable (normal pointing up).
     * Bottom face also has upward normal from the inside, so could be 2-4 walkable. */
    EXPECT_TRUE(tri_count > 0 && tri_count < 12, "Box: not all triangles walkable (slope filter)");
}

/*==========================================================================
 * AnimBlend3D tests
 *=========================================================================*/

static void test_blend_create() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, nullptr, -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *blend = rt_anim_blend3d_new(skel);
    EXPECT_TRUE(blend != nullptr, "AnimBlend3D created");
    EXPECT_TRUE(rt_anim_blend3d_state_count(blend) == 0, "Starts with 0 states");
}

static void test_blend_add_state() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, nullptr, -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *blend = rt_anim_blend3d_new(skel);

    void *anim = rt_animation3d_new(nullptr, 1.0);
    int64_t idx = rt_anim_blend3d_add_state(blend, nullptr, anim);
    EXPECT_TRUE(idx == 0, "First state index = 0");
    EXPECT_TRUE(rt_anim_blend3d_state_count(blend) == 1, "State count = 1");
}

static void test_blend_weight() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, nullptr, -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *blend = rt_anim_blend3d_new(skel);
    void *anim = rt_animation3d_new(nullptr, 1.0);
    rt_anim_blend3d_add_state(blend, nullptr, anim);

    rt_anim_blend3d_set_weight(blend, 0, 0.75);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0), 0.75, 0.01, "Weight set to 0.75");
}

static void test_blend_update_no_crash() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, nullptr, -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *blend = rt_anim_blend3d_new(skel);
    void *anim = rt_animation3d_new(nullptr, 1.0);
    rt_anim_blend3d_add_state(blend, nullptr, anim);
    rt_anim_blend3d_set_weight(blend, 0, 1.0);

    /* Should not crash */
    rt_anim_blend3d_update(blend, 0.016);
    rt_anim_blend3d_update(blend, 0.016);
    EXPECT_TRUE(1, "AnimBlend3D update runs without crash");
}

int main() {
    /* NavMesh3D */
    test_navmesh_build_plane();
    test_navmesh_is_walkable();
    test_navmesh_not_walkable();
    test_navmesh_sample_position();
    test_navmesh_find_path();
    test_navmesh_box_slope_filter();

    /* AnimBlend3D */
    test_blend_create();
    test_blend_add_state();
    test_blend_weight();
    test_blend_update_no_crash();

    printf("NavMesh3D+AnimBlend3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
