//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_rt_navagent3d.cpp
// Purpose: Unit tests for NavAgent3D path following, bindings, and warp state.
//
//===----------------------------------------------------------------------===//

#include "rt_mat4.h"
#include "rt_navagent3d.h"
#include "rt_navmesh3d.h"
#include "rt_physics3d.h"
#include "rt_scene3d.h"

#include <cmath>
#include <cstdio>
#include <limits>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern void *rt_mesh3d_new_plane(double sx, double sz);
}

static int tests_passed = 0;
static int tests_run = 0;

#define EXPECT_TRUE(cond, msg)                                                                     \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "FAIL: %s\n", msg);                                               \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define EXPECT_NEAR(a, b, eps, msg)                                                                \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (std::fabs((double)(a) - (double)(b)) > (eps)) {                                        \
            std::fprintf(                                                                          \
                stderr, "FAIL: %s (got %f, expected %f)\n", msg, (double)(a), (double)(b));        \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

static void get_node_world_position(void *node, double out_pos[3]) {
    void *world = rt_scene_node3d_get_world_matrix(node);
    void *pt = rt_mat4_transform_point(world, rt_vec3_new(0.0, 0.0, 0.0));
    out_pos[0] = pt ? rt_vec3_x(pt) : 0.0;
    out_pos[1] = pt ? rt_vec3_y(pt) : 0.0;
    out_pos[2] = pt ? rt_vec3_z(pt) : 0.0;
}

static void test_navagent_bound_node_reaches_target_in_world_space() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *agent = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    double world[3];
    void *pos;

    rt_scene_node3d_set_position(parent, 2.0, 0.0, 1.0);
    rt_scene_node3d_add_child(parent, child);
    rt_scene3d_add(scene, parent);
    rt_navagent3d_bind_node(agent, child);
    rt_navagent3d_warp(agent, rt_vec3_new(0.0, 0.0, 0.0));
    rt_navagent3d_set_desired_speed(agent, 6.0);
    rt_navagent3d_set_stopping_distance(agent, 0.2);
    rt_navagent3d_set_target(agent, rt_vec3_new(4.0, 0.0, 4.0));

    for (int i = 0; i < 80; i++)
        rt_navagent3d_update(agent, 0.05);

    get_node_world_position(child, world);
    pos = rt_navagent3d_get_position(agent);
    EXPECT_NEAR(world[0], 4.0, 0.35, "NavAgent3D drives bound SceneNode3D in world X");
    EXPECT_NEAR(world[2], 4.0, 0.35, "NavAgent3D drives bound SceneNode3D in world Z");
    EXPECT_NEAR(rt_vec3_x(pos), world[0], 0.01, "NavAgent3D position matches bound node world X");
    EXPECT_NEAR(rt_navagent3d_get_remaining_distance(agent),
                0.0,
                0.3,
                "NavAgent3D remaining distance reaches zero near the target");
    EXPECT_TRUE(rt_navagent3d_get_has_path(agent) == 0,
                "NavAgent3D clears active path state after arriving");
}

static void test_navagent_bound_character_moves_controller() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *agent = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *world = rt_world3d_new(0.0, -9.8, 0.0);
    void *character = rt_character3d_new(0.4, 1.8, 80.0);
    void *pos;
    void *vel;

    rt_character3d_set_world(character, world);
    rt_navagent3d_bind_character(agent, character);
    rt_navagent3d_set_desired_speed(agent, 4.0);
    rt_navagent3d_set_target(agent, rt_vec3_new(3.0, 0.0, 0.0));

    for (int i = 0; i < 20; i++)
        rt_navagent3d_update(agent, 0.1);

    pos = rt_character3d_get_position(character);
    vel = rt_navagent3d_get_velocity(agent);
    EXPECT_TRUE(rt_vec3_x(pos) > 1.5, "NavAgent3D advances a bound Character3D");
    EXPECT_TRUE(rt_vec3_x(vel) >= 0.0, "NavAgent3D exposes actual velocity after character motion");
    EXPECT_TRUE(rt_navagent3d_get_has_path(agent) == 0 ||
                    rt_navagent3d_get_remaining_distance(agent) < 1.5,
                "NavAgent3D reduces remaining distance while driving Character3D");
}

static void test_navagent_warp_resets_motion_and_rebuilds_path() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *agent = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *pos;
    void *vel;

    rt_navagent3d_set_target(agent, rt_vec3_new(5.0, 0.0, 0.0));
    rt_navagent3d_update(agent, 0.1);
    rt_navagent3d_warp(agent, rt_vec3_new(-3.0, 0.0, 0.0));
    pos = rt_navagent3d_get_position(agent);
    vel = rt_navagent3d_get_velocity(agent);
    EXPECT_NEAR(
        rt_vec3_x(pos), -3.0, 0.2, "NavAgent3D.Warp moves immediately to the snapped position");
    EXPECT_NEAR(rt_vec3_x(vel), 0.0, 0.001, "NavAgent3D.Warp clears the previous frame velocity");
    EXPECT_TRUE(rt_navagent3d_get_has_path(agent) != 0,
                "NavAgent3D.Warp rebuilds a live path when a target is still active");

    rt_navagent3d_update(agent, 0.1);
    EXPECT_TRUE(rt_vec3_x(rt_navagent3d_get_desired_velocity(agent)) > 0.0,
                "NavAgent3D resumes steering toward the target after Warp");
}

static void test_navagent_character_binding_updates_bound_node() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *agent = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *world = rt_world3d_new(0.0, -9.8, 0.0);
    void *character = rt_character3d_new(0.4, 1.8, 80.0);
    void *scene = rt_scene3d_new();
    void *parent = rt_scene_node3d_new();
    void *child = rt_scene_node3d_new();
    double world_pos[3];
    void *char_pos;

    rt_character3d_set_world(character, world);
    rt_scene_node3d_set_position(parent, 1.0, 0.0, -2.0);
    rt_scene_node3d_add_child(parent, child);
    rt_scene3d_add(scene, parent);
    rt_navagent3d_bind_character(agent, character);
    rt_navagent3d_bind_node(agent, child);
    rt_navagent3d_set_target(agent, rt_vec3_new(2.0, 0.0, 2.0));

    for (int i = 0; i < 15; i++)
        rt_navagent3d_update(agent, 0.1);

    get_node_world_position(child, world_pos);
    char_pos = rt_character3d_get_position(character);
    EXPECT_NEAR(world_pos[0],
                rt_vec3_x(char_pos),
                0.01,
                "NavAgent3D mirrors bound Character3D X into the bound SceneNode3D");
    EXPECT_NEAR(world_pos[2],
                rt_vec3_z(char_pos),
                0.01,
                "NavAgent3D mirrors bound Character3D Z into the bound SceneNode3D");
}

static void test_navagent_avoidance_properties() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.5, 1.8);
    void *agent = rt_navagent3d_new(navmesh, 0.5, 1.8);

    EXPECT_TRUE(rt_navagent3d_get_avoidance_enabled(agent) == 0,
                "NavAgent3D.AvoidanceEnabled defaults to false");
    EXPECT_NEAR(rt_navagent3d_get_avoidance_radius(agent),
                0.5,
                0.001,
                "NavAgent3D.AvoidanceRadius defaults to the agent radius");

    rt_navagent3d_set_avoidance_enabled(agent, 1);
    EXPECT_TRUE(rt_navagent3d_get_avoidance_enabled(agent) == 1,
                "NavAgent3D.AvoidanceEnabled can be enabled");

    rt_navagent3d_set_avoidance_radius(agent, 1.25);
    EXPECT_NEAR(rt_navagent3d_get_avoidance_radius(agent),
                1.25,
                0.001,
                "NavAgent3D.AvoidanceRadius stores positive values");

    rt_navagent3d_set_avoidance_radius(agent, -2.0);
    EXPECT_NEAR(rt_navagent3d_get_avoidance_radius(agent),
                0.0,
                0.001,
                "NavAgent3D.AvoidanceRadius clamps negative values to zero");

    rt_navagent3d_set_avoidance_radius(agent, std::numeric_limits<double>::infinity());
    EXPECT_NEAR(rt_navagent3d_get_avoidance_radius(agent),
                0.0,
                0.001,
                "NavAgent3D.AvoidanceRadius clamps non-finite values to zero");

    rt_navagent3d_set_avoidance_enabled(agent, 0);
    EXPECT_TRUE(rt_navagent3d_get_avoidance_enabled(agent) == 0,
                "NavAgent3D.AvoidanceEnabled can be disabled");
}

static void test_navagent_local_avoidance_reduces_head_on_velocity() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *plain_a = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *plain_b = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *avoid_a = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *avoid_b = rt_navagent3d_new(navmesh, 0.4, 1.8);
    double plain_ax;
    double plain_bx;
    double avoid_ax;
    double avoid_bx;

    rt_navagent3d_set_desired_speed(plain_a, 2.0);
    rt_navagent3d_set_desired_speed(plain_b, 2.0);
    rt_navagent3d_set_stopping_distance(plain_a, 0.05);
    rt_navagent3d_set_stopping_distance(plain_b, 0.05);
    rt_navagent3d_warp(plain_a, rt_vec3_new(-0.4, 0.0, 0.0));
    rt_navagent3d_warp(plain_b, rt_vec3_new(0.4, 0.0, 0.0));
    rt_navagent3d_set_target(plain_a, rt_vec3_new(4.0, 0.0, 0.0));
    rt_navagent3d_set_target(plain_b, rt_vec3_new(-4.0, 0.0, 0.0));
    rt_navagent3d_update(plain_a, 0.1);
    rt_navagent3d_update(plain_b, 0.1);
    plain_ax = rt_vec3_x(rt_navagent3d_get_desired_velocity(plain_a));
    plain_bx = rt_vec3_x(rt_navagent3d_get_desired_velocity(plain_b));

    rt_navagent3d_set_desired_speed(avoid_a, 2.0);
    rt_navagent3d_set_desired_speed(avoid_b, 2.0);
    rt_navagent3d_set_stopping_distance(avoid_a, 0.05);
    rt_navagent3d_set_stopping_distance(avoid_b, 0.05);
    rt_navagent3d_set_avoidance_enabled(avoid_a, 1);
    rt_navagent3d_set_avoidance_enabled(avoid_b, 1);
    rt_navagent3d_set_avoidance_radius(avoid_a, 0.8);
    rt_navagent3d_set_avoidance_radius(avoid_b, 0.8);
    rt_navagent3d_warp(avoid_a, rt_vec3_new(-0.4, 0.0, 0.0));
    rt_navagent3d_warp(avoid_b, rt_vec3_new(0.4, 0.0, 0.0));
    rt_navagent3d_set_target(avoid_a, rt_vec3_new(4.0, 0.0, 0.0));
    rt_navagent3d_set_target(avoid_b, rt_vec3_new(-4.0, 0.0, 0.0));
    rt_navagent3d_update(avoid_a, 0.1);
    rt_navagent3d_update(avoid_b, 0.1);
    avoid_ax = rt_vec3_x(rt_navagent3d_get_desired_velocity(avoid_a));
    avoid_bx = rt_vec3_x(rt_navagent3d_get_desired_velocity(avoid_b));

    EXPECT_TRUE(plain_ax > 1.0, "NavAgent3D baseline agent steers toward its target");
    EXPECT_TRUE(plain_bx < -1.0, "NavAgent3D baseline peer steers toward its target");
    EXPECT_TRUE(std::fabs(avoid_ax) < std::fabs(plain_ax) - 0.25,
                "NavAgent3D avoidance reduces head-on desired velocity");
    EXPECT_TRUE(std::fabs(avoid_bx) < std::fabs(plain_bx) - 0.25,
                "NavAgent3D avoidance reduces peer head-on desired velocity");
}

static void test_navagent_avoidance_breaks_head_on_deadlock() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *a = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *b = rt_navagent3d_new(navmesh, 0.4, 1.8);
    rt_navagent3d_set_desired_speed(a, 2.0);
    rt_navagent3d_set_desired_speed(b, 2.0);
    rt_navagent3d_set_stopping_distance(a, 0.05);
    rt_navagent3d_set_stopping_distance(b, 0.05);
    rt_navagent3d_set_avoidance_enabled(a, 1);
    rt_navagent3d_set_avoidance_enabled(b, 1);
    rt_navagent3d_set_avoidance_radius(a, 0.5);
    rt_navagent3d_set_avoidance_radius(b, 0.5);
    /* Start on opposite sides heading to each other's start: a perfectly symmetric head-on. */
    rt_navagent3d_warp(a, rt_vec3_new(-3.0, 0.0, 0.0));
    rt_navagent3d_warp(b, rt_vec3_new(3.0, 0.0, 0.0));
    rt_navagent3d_set_target(a, rt_vec3_new(3.0, 0.0, 0.0));
    rt_navagent3d_set_target(b, rt_vec3_new(-3.0, 0.0, 0.0));

    double min_dist = 1e9;
    for (int i = 0; i < 200; ++i) {
        rt_navagent3d_update(a, 1.0 / 30.0);
        rt_navagent3d_update(b, 1.0 / 30.0);
        void *pa = rt_navagent3d_get_position(a);
        void *pb = rt_navagent3d_get_position(b);
        double dx = rt_vec3_x(pa) - rt_vec3_x(pb);
        double dz = rt_vec3_z(pa) - rt_vec3_z(pb);
        double d = std::sqrt(dx * dx + dz * dz);
        if (d < min_dist)
            min_dist = d;
    }
    void *pa = rt_navagent3d_get_position(a);
    void *pb = rt_navagent3d_get_position(b);
    /* Without a tie-break the symmetric head-on stalls both agents at x~0; the perpendicular
     * tie-break lets them veer to opposite sides and cross past each other to their swapped goals. */
    EXPECT_TRUE(rt_vec3_x(pa) > 1.0, "Head-on agent A crosses past the meeting point (no deadlock)");
    EXPECT_TRUE(rt_vec3_x(pb) < -1.0, "Head-on agent B crosses past the meeting point (no deadlock)");
    EXPECT_TRUE(min_dist > 0.4, "Head-on agents avoid deep interpenetration while passing");
}

static void test_navagent_crowd_multiple_pairs_cross() {
    /* Several head-on pairs in well-separated Z lanes exercise the avoidance with many simultaneous
     * agents in the shared registry. Lanes are far enough apart not to interfere, so each pair
     * independently resolves its head-on; all agents reach their swapped goals. */
    void *mesh = rt_mesh3d_new_plane(40.0, 40.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    const int pairs = 3;
    void *as[3];
    void *bs[3];
    for (int p = 0; p < pairs; ++p) {
        double z = (double)(p - 1) * 5.0; /* lanes z = -5, 0, +5 */
        as[p] = rt_navagent3d_new(navmesh, 0.4, 1.8);
        bs[p] = rt_navagent3d_new(navmesh, 0.4, 1.8);
        rt_navagent3d_set_desired_speed(as[p], 2.0);
        rt_navagent3d_set_desired_speed(bs[p], 2.0);
        rt_navagent3d_set_stopping_distance(as[p], 0.05);
        rt_navagent3d_set_stopping_distance(bs[p], 0.05);
        rt_navagent3d_set_avoidance_enabled(as[p], 1);
        rt_navagent3d_set_avoidance_enabled(bs[p], 1);
        rt_navagent3d_set_avoidance_radius(as[p], 0.5);
        rt_navagent3d_set_avoidance_radius(bs[p], 0.5);
        rt_navagent3d_warp(as[p], rt_vec3_new(-3.0, 0.0, z));
        rt_navagent3d_warp(bs[p], rt_vec3_new(3.0, 0.0, z));
        rt_navagent3d_set_target(as[p], rt_vec3_new(3.0, 0.0, z));
        rt_navagent3d_set_target(bs[p], rt_vec3_new(-3.0, 0.0, z));
    }
    for (int i = 0; i < 250; ++i) {
        for (int p = 0; p < pairs; ++p) {
            rt_navagent3d_update(as[p], 1.0 / 30.0);
            rt_navagent3d_update(bs[p], 1.0 / 30.0);
        }
    }
    for (int p = 0; p < pairs; ++p) {
        EXPECT_TRUE(rt_vec3_x(rt_navagent3d_get_position(as[p])) > 1.0,
                    "Crowd: each lane's agent A crosses to its goal");
        EXPECT_TRUE(rt_vec3_x(rt_navagent3d_get_position(bs[p])) < -1.0,
                    "Crowd: each lane's agent B crosses to its goal");
    }
}

static void test_navagent_avoidance_grid_matches_full_scan() {
    /* O(N) peer-culling correctness: populate the shared registry with many agents spread across
     * several spatial-grid cells, give them crossing targets so they converge into shared cells,
     * and assert the grid-based avoidance query yields the same steering as a brute-force full
     * registry scan at every checked frame. A missed neighbor would diverge the two. */
    void *mesh = rt_mesh3d_new_plane(60.0, 60.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    const int n = 24;
    void *agents[24];
    for (int i = 0; i < n; ++i) {
        double x = (double)((i % 6) * 4) - 10.0; /* spread across X cells */
        double z = (double)((i / 6) * 4) - 6.0;  /* and Z cells */
        agents[i] = rt_navagent3d_new(navmesh, 0.4, 1.8);
        rt_navagent3d_set_desired_speed(agents[i], 2.0);
        rt_navagent3d_set_avoidance_enabled(agents[i], 1);
        rt_navagent3d_set_avoidance_radius(agents[i], 0.6);
        rt_navagent3d_warp(agents[i], rt_vec3_new(x, 0.0, z));
        rt_navagent3d_set_target(agents[i], rt_vec3_new(-x, 0.0, -z)); /* cross through the middle */
    }
    EXPECT_TRUE(rt_navagent3d_check_avoidance_grid_parity() == 1,
                "NavAgent grid avoidance matches full-scan at rest");
    for (int step = 0; step < 60; ++step) {
        for (int i = 0; i < n; ++i)
            rt_navagent3d_update(agents[i], 1.0 / 30.0);
        if (step % 15 == 0)
            EXPECT_TRUE(rt_navagent3d_check_avoidance_grid_parity() == 1,
                        "NavAgent grid avoidance matches full-scan during crowd motion");
    }
    EXPECT_TRUE(rt_navagent3d_check_avoidance_grid_parity() == 1,
                "NavAgent grid avoidance matches full-scan after crowd converges");
}

static void test_navagent_rejects_wrong_handle_types() {
    void *mesh = rt_mesh3d_new_plane(20.0, 20.0);
    void *navmesh = rt_navmesh3d_build(mesh, 0.4, 1.8);
    void *agent = rt_navagent3d_new(navmesh, 0.4, 1.8);
    void *bad_node = rt_scene_node3d_new();
    void *pos;

    EXPECT_TRUE(rt_navagent3d_new(bad_node, 0.4, 1.8) == nullptr,
                "NavAgent3D.New rejects non-NavMesh3D handles");

    rt_navagent3d_bind_node(agent, navmesh);
    rt_navagent3d_set_target(agent, agent);
    EXPECT_TRUE(rt_navagent3d_get_has_path(agent) == 0,
                "NavAgent3D.SetTarget rejects non-Vec3 handles");

    rt_navagent3d_warp(agent, agent);
    pos = rt_navagent3d_get_position(agent);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.001, "NavAgent3D.Warp rejects non-Vec3 X");
    EXPECT_NEAR(rt_vec3_z(pos), 0.0, 0.001, "NavAgent3D.Warp rejects non-Vec3 Z");
}

int main() {
    test_navagent_bound_node_reaches_target_in_world_space();
    test_navagent_bound_character_moves_controller();
    test_navagent_warp_resets_motion_and_rebuilds_path();
    test_navagent_character_binding_updates_bound_node();
    test_navagent_avoidance_properties();
    test_navagent_local_avoidance_reduces_head_on_velocity();
    test_navagent_avoidance_breaks_head_on_deadlock();
    test_navagent_crowd_multiple_pairs_cross();
    test_navagent_avoidance_grid_matches_full_scan();
    test_navagent_rejects_wrong_handle_types();

    std::printf("NavAgent3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
