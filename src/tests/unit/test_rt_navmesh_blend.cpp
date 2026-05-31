//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_navmesh_blend.cpp
// Purpose: Unit tests for NavMesh3D, AnimBlend3D, and BlendTree3D.
//
// Links: rt_navmesh3d.h, rt_skeleton3d.h, rt_blendtree3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_blendtree3d.h"
#include "rt_internal.h"
#include "rt_navmesh3d.h"
#include "rt_path3d.h"
#include "rt_scene3d.h"
#include "rt_skeleton3d.h"
#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mesh3d_new_plane(double sx, double sz);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *obj, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *obj, int64_t v0, int64_t v1, int64_t v2);
extern void *rt_mesh3d_new_box(double sx, double sy, double sz);
extern int64_t rt_path3d_get_point_count(void *path);
extern const char *rt_string_cstr(rt_string s);
extern void *rt_mat4_identity(void);
extern void *rt_skeleton3d_new(void);
extern int64_t rt_skeleton3d_add_bone(void *s, rt_string n, int64_t p, void *m);
extern void rt_skeleton3d_compute_inverse_bind(void *s);
extern void *rt_animation3d_new(rt_string name, double duration);
extern rt_string rt_const_cstr(const char *s);
}

static int tests_passed = 0;
static int tests_run = 0;
static std::jmp_buf trap_jmp;
static const char *last_trap = nullptr;
static bool expect_trap = false;

extern "C" void vm_trap(const char *msg) {
    last_trap = msg;
    if (expect_trap)
        std::longjmp(trap_jmp, 1);
    std::fprintf(stderr, "unexpected runtime trap: %s\n", msg ? msg : "(null)");
    std::abort();
}

template <typename Fn> static bool expect_trap_contains(Fn &&fn, const char *needle) {
    last_trap = nullptr;
    expect_trap = true;
    if (setjmp(trap_jmp) == 0) {
        fn();
        expect_trap = false;
        return false;
    }
    expect_trap = false;
    return last_trap && (!needle || std::strstr(last_trap, needle) != nullptr);
}

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

static void test_navmesh_find_path_from_shared_edge() {
    void *plane = rt_mesh3d_new_plane(20.0, 20.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);
    void *from = rt_vec3_new(1.909190416, 0.0, 1.909190416);
    void *to = rt_vec3_new(4.0, 0.0, 4.0);
    void *path = rt_navmesh3d_find_path(nm, from, to);

    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, from) != 0,
                "NavMesh treats points on shared triangle edges as walkable");
    EXPECT_TRUE(path != nullptr, "NavMesh finds a path from a shared triangle edge");
    if (path)
        EXPECT_TRUE(rt_path3d_get_point_count(path) >= 2, "Shared-edge path includes endpoints");
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

static void test_navmesh_adjacency_edge_hash() {
    /* Build from plane — should produce 2 triangles that are adjacent (share an edge) */
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);
    EXPECT_TRUE(nm != nullptr, "NavMesh adjacency: builds from plane");
    int64_t tc = rt_navmesh3d_get_triangle_count(nm);
    EXPECT_TRUE(tc == 2, "NavMesh adjacency: plane has 2 walkable triangles");
    /* If adjacency works correctly, pathfinding between opposite corners should succeed
     * (requires traversing from triangle 0 to triangle 1 via shared edge) */
    void *from = rt_vec3_new(-4.0, 0.0, -4.0);
    void *to = rt_vec3_new(4.0, 0.0, 4.0);
    void *path = rt_navmesh3d_find_path(nm, from, to);
    EXPECT_TRUE(path != nullptr, "NavMesh adjacency: path found across edge-hash adjacency");
}

static void test_navmesh_rejects_non_manifold_edges() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, -1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, -1.0, 0.0, 1.0, 0.0, 0.5, 1.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.5, 1.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, -2.0, 0.0, 1.0, 0.0, 0.5, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 1, 0, 3);
    rt_mesh3d_add_triangle(mesh, 0, 1, 4);

    EXPECT_TRUE(
        expect_trap_contains([&] { (void)rt_navmesh3d_build(mesh, 0.4, 1.8); }, "non-manifold"),
        "NavMesh rejects non-manifold edge ownership");
}

static void test_navmesh_large_mesh() {
    /* Build from a box (12 triangles) — verifies edge hash handles > 2 triangles */
    void *box = rt_mesh3d_new_box(10.0, 0.5, 10.0); /* flat-ish box */
    void *nm = rt_navmesh3d_build(box, 0.4, 1.8);
    EXPECT_TRUE(nm != nullptr, "NavMesh large: builds from box");
    int64_t tc = rt_navmesh3d_get_triangle_count(nm);
    EXPECT_TRUE(tc > 0, "NavMesh large: has walkable triangles");
}

static void *make_narrow_corridor_navmesh(double agent_radius) {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, -3.0, 0.0, -0.3, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, -0.3, 0.0, 1.0, 0.0, 0.5, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.3, 0.0, 1.0, 0.0, 0.5, 1.0);
    rt_mesh3d_add_vertex(mesh, -3.0, 0.0, 0.3, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 3.0, 0.0, -0.3, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 3.0, 0.0, 0.3, 0.0, 1.0, 0.0, 1.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 2, 1);
    rt_mesh3d_add_triangle(mesh, 0, 3, 2);
    rt_mesh3d_add_triangle(mesh, 1, 5, 4);
    rt_mesh3d_add_triangle(mesh, 1, 2, 5);
    return rt_navmesh3d_build(mesh, agent_radius, 1.8);
}

static void test_navmesh_agent_radius_blocks_narrow_portals() {
    void *small_agent = make_narrow_corridor_navmesh(0.2);
    void *wide_agent = make_narrow_corridor_navmesh(0.4);
    void *from = rt_vec3_new(-2.0, 0.0, 0.0);
    void *to = rt_vec3_new(2.0, 0.0, 0.0);

    EXPECT_TRUE(small_agent != nullptr && wide_agent != nullptr,
                "NavMesh radius corridors: meshes build");
    EXPECT_TRUE(rt_navmesh3d_find_path(small_agent, from, to) != nullptr,
                "NavMesh radius corridors: small agent traverses a wide-enough portal");
    EXPECT_TRUE(rt_navmesh3d_find_path(wide_agent, from, to) == nullptr,
                "NavMesh radius corridors: wide agent cannot traverse a narrow portal");
}

static void *make_two_island_navmesh() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, -4.0, 0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, -2.0, 0.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, -2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 1.0);
    rt_mesh3d_add_vertex(mesh, -4.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 2.0, 0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 4.0, 0.0, -1.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 4.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 2.0, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 2, 1);
    rt_mesh3d_add_triangle(mesh, 0, 3, 2);
    rt_mesh3d_add_triangle(mesh, 4, 6, 5);
    rt_mesh3d_add_triangle(mesh, 4, 7, 6);
    return rt_navmesh3d_build(mesh, 0.4, 1.8);
}

static void test_navmesh_offmesh_links_bridge_islands() {
    void *nm = make_two_island_navmesh();
    void *from = rt_vec3_new(-3.0, 0.0, 0.0);
    void *to = rt_vec3_new(3.0, 0.0, 0.0);
    void *link_from = rt_vec3_new(-2.5, 0.0, 0.0);
    void *link_to = rt_vec3_new(2.5, 0.0, 0.0);

    EXPECT_TRUE(nm != nullptr, "NavMesh off-mesh: island mesh builds");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == 4, "NavMesh off-mesh: islands are walkable");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) == nullptr,
                "NavMesh off-mesh: disconnected islands have no path before link");
    EXPECT_TRUE(rt_navmesh3d_get_offmesh_link_count(nm) == 0,
                "NavMesh off-mesh: new mesh starts with no links");
    EXPECT_TRUE(rt_navmesh3d_add_offmesh_link(nm, link_from, link_to, 1) != 0,
                "NavMesh off-mesh: AddOffMeshLink accepts endpoints on walkable islands");
    EXPECT_TRUE(rt_navmesh3d_get_offmesh_link_count(nm) == 1,
                "NavMesh off-mesh: OffMeshLinkCount tracks authored links");

    void *path = rt_navmesh3d_find_path(nm, from, to);
    EXPECT_TRUE(path != nullptr, "NavMesh off-mesh: bidirectional link bridges islands");
    if (path)
        EXPECT_TRUE(rt_path3d_get_point_count(path) >= 4,
                    "NavMesh off-mesh: path includes endpoint and link waypoints");
}

static void test_navmesh_offmesh_links_validate_and_direct() {
    void *nm = make_two_island_navmesh();
    void *left = rt_vec3_new(-3.0, 0.0, 0.0);
    void *right = rt_vec3_new(3.0, 0.0, 0.0);
    void *bad = rt_vec3_new(100.0, 0.0, 0.0);

    EXPECT_TRUE(rt_navmesh3d_add_offmesh_link(nm, left, bad, 1) == 0,
                "NavMesh off-mesh: rejects endpoints outside walkable polygons");
    EXPECT_TRUE(rt_navmesh3d_add_offmesh_link(nullptr, left, right, 1) == 0,
                "NavMesh off-mesh: rejects NULL navmesh");
    EXPECT_TRUE(rt_navmesh3d_get_offmesh_link_count(left) == 0,
                "NavMesh off-mesh: accessors reject non-navmesh handles");
    EXPECT_TRUE(rt_navmesh3d_add_offmesh_link(nm, left, right, 0) != 0,
                "NavMesh off-mesh: one-way link can be authored");

    void *forward = rt_navmesh3d_find_path(nm, left, right);
    void *reverse = rt_navmesh3d_find_path(nm, right, left);
    EXPECT_TRUE(forward != nullptr, "NavMesh off-mesh: one-way link traverses forward");
    EXPECT_TRUE(reverse == nullptr, "NavMesh off-mesh: one-way link blocks reverse traversal");
}

static void test_navmesh_offmesh_link_metadata_affects_cost() {
    void *nm = make_two_island_navmesh();
    void *left = rt_vec3_new(-3.0, 0.0, 0.0);
    void *right = rt_vec3_new(3.0, 0.0, 0.0);
    rt_string jump = rt_const_cstr("jump");

    EXPECT_TRUE(rt_navmesh3d_add_offmesh_link(nm, left, right, 1) != 0,
                "NavMesh off-mesh metadata: link can be authored");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, left, right) != nullptr,
                "NavMesh off-mesh metadata: baseline linked path exists");
    double base_cost = rt_navmesh3d_get_last_path_cost(nm);
    EXPECT_TRUE(rt_navmesh3d_set_offmesh_link_metadata(nm, 0, jump, 4.0, 7) != 0,
                "NavMesh off-mesh metadata: metadata setter accepts valid link");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_navmesh3d_get_offmesh_link_kind(nm, 0)), "jump") == 0,
                "NavMesh off-mesh metadata: kind is stored");
    EXPECT_NEAR(rt_navmesh3d_get_offmesh_link_traversal_cost(nm, 0),
                4.0,
                0.001,
                "NavMesh off-mesh metadata: traversal cost is stored");
    EXPECT_TRUE(rt_navmesh3d_get_offmesh_link_state(nm, 0) == 7,
                "NavMesh off-mesh metadata: state flags are stored");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, left, right) != nullptr,
                "NavMesh off-mesh metadata: high-cost linked path still exists");
    EXPECT_TRUE(rt_navmesh3d_get_last_path_cost(nm) > base_cost * 3.0,
                "NavMesh off-mesh metadata: link traversal cost contributes to A* cost");
    EXPECT_TRUE(rt_navmesh3d_set_offmesh_link_metadata(nm, 3, jump, 2.0, 0) == 0,
                "NavMesh off-mesh metadata: invalid link index is rejected");
    EXPECT_TRUE(rt_navmesh3d_get_offmesh_link_state(nm, 3) == 0,
                "NavMesh off-mesh metadata: invalid getter returns neutral state");
}

static void *make_navmesh_obstacle_strip() {
    void *mesh = rt_mesh3d_new();
    for (int i = 0; i <= 4; i++) {
        double x = -4.0 + (double)i * 2.0;
        rt_mesh3d_add_vertex(mesh, x, 0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0);
        rt_mesh3d_add_vertex(mesh, x, 0.0, 1.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    }
    for (int i = 0; i < 4; i++) {
        int64_t bl = i * 2;
        int64_t tl = i * 2 + 1;
        int64_t br = (i + 1) * 2;
        int64_t tr = (i + 1) * 2 + 1;
        rt_mesh3d_add_triangle(mesh, bl, tr, br);
        rt_mesh3d_add_triangle(mesh, bl, tl, tr);
    }
    return rt_navmesh3d_build(mesh, 0.25, 1.8);
}

static void test_navmesh_obstacle_carving_uses_triangle_footprint() {
    void *mesh = rt_mesh3d_new();
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 10.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 2, 1);
    void *nm = rt_navmesh3d_build(mesh, 0.25, 1.8);
    void *outside_min = rt_vec3_new(9.0, -0.2, 9.0);
    void *outside_max = rt_vec3_new(9.5, 1.0, 9.5);
    void *inside_min = rt_vec3_new(1.0, -0.2, 1.0);
    void *inside_max = rt_vec3_new(1.5, 1.0, 1.5);

    EXPECT_TRUE(nm != nullptr, "NavMesh obstacle exact: single triangle builds");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == 1,
                "NavMesh obstacle exact: triangle starts walkable");
    EXPECT_TRUE(rt_navmesh3d_add_obstacle(nm, outside_min, outside_max) != 0,
                "NavMesh obstacle exact: AABB-overlap-only obstacle is accepted");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == 1,
                "NavMesh obstacle exact: obstacle outside triangle footprint does not carve");
    EXPECT_TRUE(rt_navmesh3d_update_obstacle(nm, 0, inside_min, inside_max) != 0,
                "NavMesh obstacle exact: obstacle can move into triangle footprint");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == 0,
                "NavMesh obstacle exact: obstacle inside triangle footprint carves polygon");
}

static void test_navmesh_add_obstacle_carves_walkable_triangles() {
    void *nm = make_navmesh_obstacle_strip();
    void *from = rt_vec3_new(-3.0, 0.0, 0.0);
    void *to = rt_vec3_new(3.0, 0.0, 0.0);
    void *blocked = rt_vec3_new(0.0, 0.0, 0.0);
    void *min_v = rt_vec3_new(-0.8, -0.2, -1.2);
    void *max_v = rt_vec3_new(0.8, 1.0, 1.2);
    void *far_min = rt_vec3_new(10.0, -0.2, 10.0);
    void *far_max = rt_vec3_new(12.0, 1.0, 12.0);
    void *bad_min = rt_vec3_new(NAN, 0.0, 0.0);
    int64_t before_count = rt_navmesh3d_get_triangle_count(nm);

    EXPECT_TRUE(nm != nullptr, "NavMesh obstacle: strip mesh builds");
    EXPECT_TRUE(before_count == 8, "NavMesh obstacle: strip starts with eight walkable triangles");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) != nullptr,
                "NavMesh obstacle: strip paths before carving");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 0,
                "NavMesh obstacle: new mesh starts with no obstacles");
    EXPECT_TRUE(rt_navmesh3d_add_obstacle(nm, bad_min, max_v) == 0,
                "NavMesh obstacle: rejects non-finite obstacle bounds");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 0,
                "NavMesh obstacle: rejected obstacle does not increment telemetry");
    EXPECT_TRUE(rt_navmesh3d_add_obstacle(nm, min_v, max_v) != 0,
                "NavMesh obstacle: AddObstacle accepts finite AABB bounds");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 1,
                "NavMesh obstacle: ObstacleCount tracks authored obstacles");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) < before_count,
                "NavMesh obstacle: obstacle removes overlapping walkable triangles");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, blocked) == 0,
                "NavMesh obstacle: carved obstacle center is not walkable");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) == nullptr,
                "NavMesh obstacle: carved strip no longer has a corridor path");
    EXPECT_TRUE(rt_navmesh3d_update_obstacle(nm, 0, bad_min, far_max) == 0,
                "NavMesh obstacle: UpdateObstacle rejects non-finite bounds");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 1,
                "NavMesh obstacle: rejected update keeps authored obstacle");
    EXPECT_TRUE(rt_navmesh3d_update_obstacle(nm, 0, far_min, far_max) != 0,
                "NavMesh obstacle: UpdateObstacle moves a carved obstacle");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 1,
                "NavMesh obstacle: UpdateObstacle preserves obstacle count");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == before_count,
                "NavMesh obstacle: moving obstacle out of bounds restores triangles");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, blocked) != 0,
                "NavMesh obstacle: moved obstacle restores walkability");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) != nullptr,
                "NavMesh obstacle: moved obstacle restores path corridor");
    EXPECT_TRUE(rt_navmesh3d_update_obstacle(nm, 0, min_v, max_v) != 0,
                "NavMesh obstacle: UpdateObstacle can carve again");
    EXPECT_TRUE(rt_navmesh3d_remove_obstacle(nm, -1) == 0,
                "NavMesh obstacle: RemoveObstacle rejects negative indices");
    EXPECT_TRUE(rt_navmesh3d_remove_obstacle(nm, 1) == 0,
                "NavMesh obstacle: RemoveObstacle rejects out-of-range indices");
    EXPECT_TRUE(rt_navmesh3d_remove_obstacle(nm, 0) != 0,
                "NavMesh obstacle: RemoveObstacle removes authored obstacles");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 0,
                "NavMesh obstacle: RemoveObstacle updates obstacle count");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == before_count,
                "NavMesh obstacle: removing obstacle restores walkable triangles");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) != nullptr,
                "NavMesh obstacle: removing obstacle restores path corridor");
    EXPECT_TRUE(rt_navmesh3d_add_obstacle(from, min_v, max_v) == 0,
                "NavMesh obstacle: rejects non-navmesh handles");
}

static void test_navmesh_area_metadata_and_traversal_costs() {
    void *plane = rt_mesh3d_new_plane(10.0, 10.0);
    void *nm = rt_navmesh3d_build(plane, 0.4, 1.8);
    void *from = rt_vec3_new(-4.0, 0.0, 0.0);
    void *to = rt_vec3_new(4.0, 0.0, 0.0);
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    void *min_v = rt_vec3_new(-5.0, -0.2, -5.0);
    void *max_v = rt_vec3_new(5.0, 1.0, 5.0);
    rt_string mud = rt_const_cstr("mud");

    EXPECT_TRUE(nm != nullptr, "NavMesh area: plane builds");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) != nullptr,
                "NavMesh area: baseline path exists");
    double base_cost = rt_navmesh3d_get_last_path_cost(nm);
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_navmesh3d_get_area(nm, center)), "default") == 0,
                "NavMesh area: default area name is reported");
    EXPECT_NEAR(rt_navmesh3d_get_traversal_cost(nm, center),
                1.0,
                0.001,
                "NavMesh area: default traversal cost is one");
    EXPECT_TRUE(rt_navmesh3d_set_area(nm, min_v, max_v, mud, 3.0) != 0,
                "NavMesh area: SetArea accepts an authored area and cost");
    EXPECT_TRUE(std::strcmp(rt_string_cstr(rt_navmesh3d_get_area(nm, center)), "mud") == 0,
                "NavMesh area: authored area name is reported");
    EXPECT_NEAR(rt_navmesh3d_get_traversal_cost(nm, center),
                3.0,
                0.001,
                "NavMesh area: authored traversal cost is reported");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) != nullptr,
                "NavMesh area: path remains available after metadata assignment");
    EXPECT_TRUE(rt_navmesh3d_get_last_path_cost(nm) > base_cost * 2.5,
                "NavMesh area: polygon traversal cost contributes to A* cost");
    EXPECT_TRUE(rt_navmesh3d_set_area(nm, from, max_v, nullptr, 2.0) == 0,
                "NavMesh area: invalid area string is rejected");
}

static void *make_scene_bake_fixture() {
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *floor = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_plane(6.0, 6.0);

    rt_scene_node3d_set_position(parent, 2.0, 0.0, -3.0);
    rt_scene_node3d_set_position(floor, 1.0, 0.0, 2.0);
    rt_scene_node3d_set_mesh(floor, mesh);
    rt_scene_node3d_add_child(parent, floor);
    rt_scene3d_add(scene, parent);
    return scene;
}

static void test_navmesh_bake_scene_flattens_transformed_nodes() {
    void *scene = make_scene_bake_fixture();
    void *nm = rt_navmesh3d_bake(scene, 0.4, 1.8, 45.0, 0.3);
    void *center = rt_vec3_new(3.0, 0.0, -1.0);
    void *outside = rt_vec3_new(-1.0, 0.0, 3.0);
    void *from = rt_vec3_new(1.0, 0.0, -3.0);
    void *to = rt_vec3_new(5.0, 0.0, 1.0);

    EXPECT_TRUE(nm != nullptr, "NavMesh Bake: scene with transformed mesh bakes");
    /* The voxel baker emits a shared-corner grid mesh (a quad per walkable cell), so the triangle
     * count reflects the cell_size grid resolution, not the 2 input plane triangles. A 6x6 plane at
     * cell_size 0.3, eroded by the 0.4 agent radius, yields a ~17x17 walkable-cell grid. */
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) > 100,
                "NavMesh Bake: voxelizes into a grid decoupled from input triangle density");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, center) != 0,
                "NavMesh Bake: world-space transformed center is walkable");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, outside) == 0,
                "NavMesh Bake: points outside transformed world bounds are not walkable");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm, from, to) != nullptr,
                "NavMesh Bake: pathfinding works across baked scene geometry");
    EXPECT_TRUE(rt_navmesh3d_bake(rt_mesh3d_new_plane(2.0, 2.0), 0.4, 1.8, 45.0, 0.3) ==
                    nullptr,
                "NavMesh Bake: rejects non-Scene3D handles");
}

static void test_navmesh_bake_tiled_and_rebuild_tile_baseline() {
    void *scene = make_scene_bake_fixture();
    void *nm = rt_navmesh3d_bake_tiled(scene, 8.0, 0.4, 1.8, 45.0, 0.3);
    void *center = rt_vec3_new(3.0, 0.0, -1.0);

    EXPECT_TRUE(nm != nullptr, "NavMesh BakeTiled: scene bakes through tiled API");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) > 100,
                "NavMesh BakeTiled: voxelizes the full scene into a navmesh grid");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, center) != 0,
                "NavMesh BakeTiled: transformed geometry is walkable");
    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(nm, 0, 0) != 0,
                "NavMesh RebuildTile: tiled mesh re-carves a tile in place");
    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(scene, 0, 0) == 0,
                "NavMesh RebuildTile: rejects non-navmesh handles");
}

/* R-NAV-002: RebuildTile re-carves only its own tile, not the whole mesh. We inject an obstacle
 * WITHOUT re-flagging (a deliberately stale tiled mesh), then show that rebuilding a far-away tile
 * leaves the obstacle's cell walkable, while rebuilding the obstacle's own tile carves it. The two
 * outcomes are indistinguishable for a whole-mesh refilter — only a genuinely tile-local rebuild
 * can leave one tile stale while updating another. */
static void test_navmesh_rebuild_tile_is_tile_local() {
    void *scene = make_scene_bake_fixture();
    void *nm = rt_navmesh3d_bake_tiled(scene, 2.0, 0.4, 1.8, 45.0, 0.3);
    EXPECT_TRUE(nm != nullptr, "RebuildTile local: tiled bake succeeds");
    int64_t base = rt_navmesh3d_get_triangle_count(nm);
    EXPECT_TRUE(base > 100, "RebuildTile local: baked grid has many walkable triangles");

    /* The transformed plane center is walkable (same point the bake-fixture test checks). */
    void *p = rt_vec3_new(3.0, 0.0, -1.0);
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p) != 0, "RebuildTile local: target cell starts walkable");

    int64_t ptx = 0, ptz = 0;
    EXPECT_TRUE(rt_navmesh3d_test_tile_of_point(nm, 3.0, -1.0, &ptx, &ptz) != 0,
                "RebuildTile local: tiled mesh maps a point to a tile");

    /* Inject an obstacle over the target cell with NO re-flag — the mesh is now stale. */
    void *omin = rt_vec3_new(2.6, -0.5, -1.4);
    void *omax = rt_vec3_new(3.4, 0.5, -0.6);
    EXPECT_TRUE(rt_navmesh3d_test_inject_obstacle(nm, omin, omax) != 0,
                "RebuildTile local: obstacle injected");
    EXPECT_TRUE(rt_navmesh3d_get_obstacle_count(nm) == 1,
                "RebuildTile local: injected obstacle is tracked");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p) != 0,
                "RebuildTile local: cell still walkable before any tile is rebuilt (stale)");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == base,
                "RebuildTile local: injecting without rebuild leaves the walkable count unchanged");

    /* Rebuild a far-away tile: must NOT carve the obstacle's cell. */
    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(nm, ptx + 5, ptz + 5) != 0,
                "RebuildTile local: rebuilding a far tile succeeds");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p) != 0,
                "RebuildTile local: far-tile rebuild leaves the obstacle's tile untouched");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) == base,
                "RebuildTile local: far-tile rebuild does not carve the obstacle");

    /* Rebuild the obstacle's own tile: now the cell is carved and the walkable count drops. */
    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(nm, ptx, ptz) != 0,
                "RebuildTile local: rebuilding the obstacle's tile succeeds");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p) == 0,
                "RebuildTile local: rebuilding the obstacle's own tile carves the cell");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) < base,
                "RebuildTile local: tile rebuild reduces the walkable triangle count");
}

static void test_navmesh_rebuild_tile_refreshes_retained_geometry_source() {
    void *scene = make_scene_bake_fixture();
    void *nm = rt_navmesh3d_bake_tiled(scene, 2.0, 0.4, 1.8, 45.0, 0.3);
    void *p = rt_vec3_new(3.0, 0.0, -1.0);
    void *p_raised = rt_vec3_new(3.0, 2.0, -1.0);
    int64_t tx = 0, tz = 0;
    EXPECT_TRUE(nm != nullptr, "RebuildTile source: tiled bake succeeds");
    EXPECT_TRUE(rt_navmesh3d_test_tile_of_point(nm, 3.0, -1.0, &tx, &tz) != 0,
                "RebuildTile source: target point maps to a tile");
    void *sample0 = rt_navmesh3d_sample_position(nm, p);
    EXPECT_NEAR(rt_vec3_y(sample0), 0.0, 0.05, "RebuildTile source: tile starts at base height");

    EXPECT_TRUE(rt_navmesh3d_test_set_tile_source(nm, tx, tz, 2.0, 1) != 0,
                "RebuildTile source: retained tile geometry source can be edited");
    void *stale = rt_navmesh3d_sample_position(nm, p);
    EXPECT_NEAR(rt_vec3_y(stale), 0.0, 0.05,
                "RebuildTile source: source edit stays stale before rebuild");
    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(nm, tx + 5, tz + 5) != 0,
                "RebuildTile source: far tile rebuild succeeds");
    void *far = rt_navmesh3d_sample_position(nm, p);
    EXPECT_NEAR(rt_vec3_y(far), 0.0, 0.05,
                "RebuildTile source: far tile rebuild leaves edited tile stale");

    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(nm, tx, tz) != 0,
                "RebuildTile source: own tile rebuild succeeds");
    void *raised = rt_navmesh3d_sample_position(nm, p);
    EXPECT_TRUE(rt_vec3_y(raised) > 1.5,
                "RebuildTile source: own tile rebuild refreshes retained geometry height");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p_raised) != 0,
                "RebuildTile source: raised tile remains walkable when source says walkable");

    int64_t before_block = rt_navmesh3d_get_triangle_count(nm);
    EXPECT_TRUE(rt_navmesh3d_test_set_tile_source(nm, tx, tz, 2.0, 0) != 0,
                "RebuildTile source: retained tile geometry can become unwalkable");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p_raised) != 0,
                "RebuildTile source: unwalkable source edit is stale before own tile rebuild");
    EXPECT_TRUE(rt_navmesh3d_rebuild_tile(nm, tx, tz) != 0,
                "RebuildTile source: own tile rebuild applies unwalkable source edit");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm, p_raised) == 0,
                "RebuildTile source: own tile rebuild removes edited source geometry");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm) < before_block,
                "RebuildTile source: source geometry removal drops walkable triangle count");
}

/* A narrow plank: 1.0 wide (x) x 10.0 long (z), centered at origin. */
static void *make_narrow_plank_scene() {
    void *scene = rt_scene3d_new();
    void *node = rt_scene_node3d_new();
    void *mesh = rt_mesh3d_new_plane(1.0, 10.0);
    rt_scene_node3d_set_mesh(node, mesh);
    rt_scene3d_add(scene, node);
    return scene;
}

static void test_navmesh_bake_agent_radius_erodes_narrow_corridor() {
    /* The voxel baker erodes the walkable set by the agent radius. On a 1.0-wide plank an agent of
     * radius 0.2 (clearance 0.4 < 1.0) keeps an inset walkable strip, while an agent of radius 0.6
     * (clearance 1.2 > 1.0) cannot fit, so the whole plank erodes away and the bake yields no
     * navmesh. The pre-voxel passthrough baker ignored agent_radius, so the wide-agent NULL is a
     * genuine fail-before/pass-after for R-NAV-003. */
    void *narrow = make_narrow_plank_scene();
    void *nm_small = rt_navmesh3d_bake(narrow, 0.2, 1.8, 45.0, 0.1);
    EXPECT_TRUE(nm_small != nullptr, "NavMesh erosion: small agent keeps the plank walkable");
    EXPECT_TRUE(rt_navmesh3d_get_triangle_count(nm_small) > 0,
                "NavMesh erosion: small-agent navmesh has walkable triangles");
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    void *near_edge = rt_vec3_new(0.45, 0.0, 0.0);
    void *path_from = rt_vec3_new(0.0, 0.0, -4.0);
    void *path_to = rt_vec3_new(0.0, 0.0, 4.0);
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm_small, center) != 0,
                "NavMesh erosion: plank center stays walkable for the small agent");
    EXPECT_TRUE(rt_navmesh3d_is_walkable(nm_small, near_edge) == 0,
                "NavMesh erosion: the agent-radius margin near the plank edge is eroded");
    EXPECT_TRUE(rt_navmesh3d_find_path(nm_small, path_from, path_to) != nullptr,
                "NavMesh erosion: small agent paths along the eroded corridor");

    void *wide = make_narrow_plank_scene();
    void *nm_wide = rt_navmesh3d_bake(wide, 0.6, 1.8, 45.0, 0.1);
    EXPECT_TRUE(nm_wide == nullptr,
                "NavMesh erosion: an agent wider than the corridor erodes it away entirely");
}

static void test_navmesh_query_grid_matches_linear_scan() {
    /* A voxel bake builds the spatial query grid that accelerates point location. Verify the
     * grid-backed find-triangle agrees with a brute-force linear scan at every sampled point, so
     * the acceleration is exact (no popping / missed snaps for agents querying the navmesh). */
    void *scene = make_scene_bake_fixture();
    void *nm = rt_navmesh3d_bake(scene, 0.4, 1.8, 45.0, 0.3);
    EXPECT_TRUE(nm != nullptr, "NavMesh query grid: scene bakes with a spatial index");
    EXPECT_TRUE(rt_navmesh3d_check_query_grid_parity(nm) != 0,
                "NavMesh query grid: grid point location matches the linear scan everywhere");
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

static void test_blend_weight_sanitizes_inputs() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);

    void *blend = rt_anim_blend3d_new(skel);
    void *anim = rt_animation3d_new(rt_const_cstr("walk"), 1.0);
    rt_anim_blend3d_add_state(blend, rt_const_cstr("walk"), anim);

    rt_anim_blend3d_set_weight(blend, 0, -2.0);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0),
                0.0,
                0.01,
                "AnimBlend3D clamps negative weights to zero");
    rt_anim_blend3d_set_weight(blend, 0, 2.0);
    EXPECT_NEAR(
        rt_anim_blend3d_get_weight(blend, 0), 1.0, 0.01, "AnimBlend3D clamps high weights to one");
    rt_anim_blend3d_set_weight(blend, 0, NAN);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0),
                0.0,
                0.01,
                "AnimBlend3D converts NaN weights to zero");
    rt_anim_blend3d_set_weight_by_name(blend, rt_const_cstr("walk"), 0.25);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0),
                0.25,
                0.01,
                "AnimBlend3D clamps named weights through the same path");
    EXPECT_TRUE(rt_anim_blend3d_state_count(skel) == 0,
                "AnimBlend3D accessors reject non-blender handles");
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

/*==========================================================================
 * BlendTree3D tests
 *=========================================================================*/

static void *make_blendtree_test_skeleton() {
    void *skel = rt_skeleton3d_new();
    rt_skeleton3d_add_bone(skel, rt_const_cstr("root"), -1, rt_mat4_identity());
    rt_skeleton3d_compute_inverse_bind(skel);
    return skel;
}

static void *make_blendtree_test_animation(const char *name) {
    return rt_animation3d_new(rt_const_cstr(name), 1.0);
}

static void test_blendtree_1d_weights() {
    void *skel = make_blendtree_test_skeleton();
    void *tree = rt_blend_tree3d_new_1d(skel);
    void *blend = rt_blend_tree3d_get_blend(tree);

    EXPECT_TRUE(tree != nullptr, "BlendTree3D.New1D creates a tree");
    EXPECT_TRUE(blend != nullptr, "BlendTree3D exposes an internal AnimBlend3D");
    EXPECT_TRUE(rt_blend_tree3d_add_sample(tree, make_blendtree_test_animation("idle"), 0.0, 0.0) ==
                    0,
                "BlendTree3D.AddSample returns first sample index");
    EXPECT_TRUE(rt_blend_tree3d_add_sample(tree, make_blendtree_test_animation("run"), 1.0, 0.0) ==
                    1,
                "BlendTree3D.AddSample returns second sample index");
    EXPECT_TRUE(rt_blend_tree3d_get_sample_count(tree) == 2, "BlendTree3D.SampleCount tracks samples");

    rt_blend_tree3d_set_param(tree, 0.25, 0.0);
    rt_blend_tree3d_update(tree, 0.0);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0),
                0.75,
                0.001,
                "BlendTree3D 1D lower sample weight is linear");
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 1),
                0.25,
                0.001,
                "BlendTree3D 1D upper sample weight is linear");

    rt_blend_tree3d_set_param(tree, -2.0, 0.0);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0),
                1.0,
                0.001,
                "BlendTree3D 1D clamps below range to first sample");
    rt_blend_tree3d_set_param(tree, 2.0, 0.0);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 1),
                1.0,
                0.001,
                "BlendTree3D 1D clamps above range to last sample");
}

static void test_blendtree_2d_weights() {
    void *skel = make_blendtree_test_skeleton();
    void *tree = rt_blend_tree3d_new_2d(skel);
    void *blend = rt_blend_tree3d_get_blend(tree);

    EXPECT_TRUE(tree != nullptr, "BlendTree3D.New2D creates a tree");
    rt_blend_tree3d_add_sample(tree, make_blendtree_test_animation("center"), 0.0, 0.0);
    rt_blend_tree3d_add_sample(tree, make_blendtree_test_animation("right"), 1.0, 0.0);
    rt_blend_tree3d_add_sample(tree, make_blendtree_test_animation("up"), 0.0, 1.0);
    EXPECT_TRUE(rt_blend_tree3d_get_sample_count(tree) == 3, "BlendTree3D 2D stores all samples");

    rt_blend_tree3d_set_param(tree, 1.0, 0.0);
    rt_blend_tree3d_update(tree, 0.0);
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 0),
                0.0,
                0.001,
                "BlendTree3D 2D exact coordinate clears other samples");
    EXPECT_NEAR(rt_anim_blend3d_get_weight(blend, 1),
                1.0,
                0.001,
                "BlendTree3D 2D exact coordinate selects matching sample");

    rt_blend_tree3d_set_param(tree, 0.5, 0.0);
    rt_blend_tree3d_update(tree, 0.0);
    double w0 = rt_anim_blend3d_get_weight(blend, 0);
    double w1 = rt_anim_blend3d_get_weight(blend, 1);
    double w2 = rt_anim_blend3d_get_weight(blend, 2);
    EXPECT_NEAR(w0 + w1 + w2, 1.0, 0.001, "BlendTree3D 2D weights stay normalized");
    EXPECT_TRUE(w0 > w2 && w1 > w2, "BlendTree3D 2D weights favor nearer samples");
}

static void test_blendtree_rejects_bad_handles() {
    void *skel = make_blendtree_test_skeleton();
    void *tree = rt_blend_tree3d_new_1d(skel);

    EXPECT_TRUE(rt_blend_tree3d_new_1d(nullptr) == nullptr, "BlendTree3D rejects NULL skeletons");
    EXPECT_TRUE(rt_blend_tree3d_add_sample(tree, skel, 0.0, 0.0) == -1,
                "BlendTree3D rejects non-animation samples");
    EXPECT_TRUE(rt_blend_tree3d_get_sample_count(skel) == 0,
                "BlendTree3D accessors reject non-tree handles");
}

int main() {
    /* NavMesh3D */
    test_navmesh_build_plane();
    test_navmesh_is_walkable();
    test_navmesh_not_walkable();
    test_navmesh_sample_position();
    test_navmesh_find_path();
    test_navmesh_find_path_from_shared_edge();
    test_navmesh_box_slope_filter();
    test_navmesh_adjacency_edge_hash();
    test_navmesh_rejects_non_manifold_edges();
    test_navmesh_large_mesh();
    test_navmesh_agent_radius_blocks_narrow_portals();
    test_navmesh_offmesh_links_bridge_islands();
    test_navmesh_offmesh_links_validate_and_direct();
    test_navmesh_offmesh_link_metadata_affects_cost();
    test_navmesh_obstacle_carving_uses_triangle_footprint();
    test_navmesh_add_obstacle_carves_walkable_triangles();
    test_navmesh_area_metadata_and_traversal_costs();
    test_navmesh_bake_scene_flattens_transformed_nodes();
    test_navmesh_bake_tiled_and_rebuild_tile_baseline();
    test_navmesh_rebuild_tile_is_tile_local();
    test_navmesh_rebuild_tile_refreshes_retained_geometry_source();
    test_navmesh_bake_agent_radius_erodes_narrow_corridor();
    test_navmesh_query_grid_matches_linear_scan();

    /* AnimBlend3D */
    test_blend_create();
    test_blend_add_state();
    test_blend_weight();
    test_blend_weight_sanitizes_inputs();
    test_blend_update_no_crash();

    /* BlendTree3D */
    test_blendtree_1d_weights();
    test_blendtree_2d_weights();
    test_blendtree_rejects_bad_handles();

    printf("NavMesh3D+AnimBlend3D+BlendTree3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
