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
            std::fprintf(stderr,                                                                   \
                         "FAIL: %s (got %f, expected %f)\n",                                       \
                         msg,                                                                      \
                         (double)(a),                                                              \
                         (double)(b));                                                             \
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
    EXPECT_NEAR(rt_navagent3d_get_remaining_distance(agent), 0.0, 0.3,
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
    EXPECT_TRUE(rt_navagent3d_get_has_path(agent) == 0 || rt_navagent3d_get_remaining_distance(agent) < 1.5,
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
    EXPECT_NEAR(rt_vec3_x(pos), -3.0, 0.2, "NavAgent3D.Warp moves immediately to the snapped position");
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
    EXPECT_NEAR(world_pos[0], rt_vec3_x(char_pos), 0.01,
                "NavAgent3D mirrors bound Character3D X into the bound SceneNode3D");
    EXPECT_NEAR(world_pos[2], rt_vec3_z(char_pos), 0.01,
                "NavAgent3D mirrors bound Character3D Z into the bound SceneNode3D");
}

int main() {
    test_navagent_bound_node_reaches_target_in_world_space();
    test_navagent_bound_character_moves_controller();
    test_navagent_warp_resets_motion_and_rebuilds_path();
    test_navagent_character_binding_updates_bound_node();

    std::printf("NavAgent3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
