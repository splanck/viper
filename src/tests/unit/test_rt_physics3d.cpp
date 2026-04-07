//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_physics3d.cpp
// Purpose: Unit tests for Physics3D — world step, body creation, collision
//   detection, impulse response, collision layers, character controller.
//
// Links: rt_physics3d.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_collider3d.h"
#include "rt_internal.h"
#include "rt_joints3d.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_quat.h"
#include "rt_transform3d.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
}

static int tests_passed = 0;
static int tests_run = 0;
static const double TEST_PI = 3.14159265358979323846;

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

static int64_t encode_height16(uint16_t value) {
    return ((int64_t)((value >> 8) & 0xFF) << 24) | ((int64_t)(value & 0xFF) << 16) | 0xFF;
}

/*==========================================================================
 * Body creation tests
 *=========================================================================*/

static void test_body_new_aabb() {
    void *b = rt_body3d_new_aabb(1.0, 2.0, 3.0, 10.0);
    EXPECT_TRUE(b != nullptr, "AABB body created");
    EXPECT_NEAR(rt_body3d_get_mass(b), 10.0, 0.01, "AABB mass = 10");
    EXPECT_NEAR(rt_body3d_get_restitution(b), 0.3, 0.01, "Default restitution = 0.3");
    EXPECT_NEAR(rt_body3d_get_friction(b), 0.5, 0.01, "Default friction = 0.5");
    EXPECT_TRUE(rt_body3d_is_static(b) == 0, "Dynamic body not static");
}

static void test_body_new_sphere() {
    void *b = rt_body3d_new_sphere(2.5, 5.0);
    EXPECT_TRUE(b != nullptr, "Sphere body created");
    EXPECT_NEAR(rt_body3d_get_mass(b), 5.0, 0.01, "Sphere mass = 5");
}

static void test_body_new_capsule() {
    void *b = rt_body3d_new_capsule(0.5, 2.0, 8.0);
    EXPECT_TRUE(b != nullptr, "Capsule body created");
    EXPECT_NEAR(rt_body3d_get_mass(b), 8.0, 0.01, "Capsule mass = 8");
}

static void test_body_static_zero_mass() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    EXPECT_TRUE(rt_body3d_is_static(b) != 0, "Zero-mass body is static");
}

static void test_body_new_and_set_collider() {
    void *body = rt_body3d_new(2.0);
    void *collider = rt_collider3d_new_box(1.0, 0.5, 1.0);
    EXPECT_TRUE(body != nullptr, "Generic body created");
    EXPECT_TRUE(collider != nullptr, "Collider created");
    rt_body3d_set_collider(body, collider);
    EXPECT_TRUE(rt_body3d_get_collider(body) == collider, "SetCollider stores collider");
}

static void test_wrapper_constructors_assign_colliders() {
    void *box = rt_body3d_new_aabb(1.0, 2.0, 3.0, 1.0);
    void *sphere = rt_body3d_new_sphere(1.5, 1.0);
    void *capsule = rt_body3d_new_capsule(0.5, 2.0, 1.0);
    EXPECT_TRUE(rt_collider3d_get_type(rt_body3d_get_collider(box)) == RT_COLLIDER3D_TYPE_BOX,
                "AABB wrapper assigns box collider");
    EXPECT_TRUE(rt_collider3d_get_type(rt_body3d_get_collider(sphere)) ==
                    RT_COLLIDER3D_TYPE_SPHERE,
                "Sphere wrapper assigns sphere collider");
    EXPECT_TRUE(rt_collider3d_get_type(rt_body3d_get_collider(capsule)) ==
                    RT_COLLIDER3D_TYPE_CAPSULE,
                "Capsule wrapper assigns capsule collider");
}

static void test_mesh_collider_attaches_to_static_body() {
    void *mesh = rt_mesh3d_new_box(4.0, 1.0, 4.0);
    void *mesh_collider = rt_collider3d_new_mesh(mesh);
    void *body = rt_body3d_new(0.0);
    EXPECT_TRUE(body != nullptr, "Static body for mesh collider created");
    rt_body3d_set_collider(body, mesh_collider);
    EXPECT_TRUE(rt_body3d_get_collider(body) == mesh_collider,
                "Static body accepts mesh collider");
}

/*==========================================================================
 * Property accessors
 *=========================================================================*/

static void test_body_position() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(b, 3.0, 5.0, 7.0);
    void *pos = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos), 3.0, 0.01, "Position X = 3");
    EXPECT_NEAR(rt_vec3_y(pos), 5.0, 0.01, "Position Y = 5");
    EXPECT_NEAR(rt_vec3_z(pos), 7.0, 0.01, "Position Z = 7");
}

static void test_body_velocity() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_velocity(b, 1.0, 2.0, 3.0);
    void *vel = rt_body3d_get_velocity(b);
    EXPECT_NEAR(rt_vec3_x(vel), 1.0, 0.01, "Velocity X = 1");
    EXPECT_NEAR(rt_vec3_y(vel), 2.0, 0.01, "Velocity Y = 2");
    EXPECT_NEAR(rt_vec3_z(vel), 3.0, 0.01, "Velocity Z = 3");
}

static void test_body_collision_layer_mask() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_collision_layer(b, 4);
    rt_body3d_set_collision_mask(b, 6);
    EXPECT_TRUE(rt_body3d_get_collision_layer(b) == 4, "Collision layer = 4");
    EXPECT_TRUE(rt_body3d_get_collision_mask(b) == 6, "Collision mask = 6");
}

static void test_body_trigger() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    EXPECT_TRUE(rt_body3d_is_trigger(b) == 0, "Default: not trigger");
    rt_body3d_set_trigger(b, 1);
    EXPECT_TRUE(rt_body3d_is_trigger(b) != 0, "Set trigger = true");
}

static void test_body_orientation_roundtrip() {
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    void *q = rt_quat_from_axis_angle(rt_vec3_new(0.0, 1.0, 0.0), TEST_PI * 0.5);
    rt_body3d_set_orientation(b, q);
    void *out = rt_body3d_get_orientation(b);
    EXPECT_NEAR(rt_quat_y(out), rt_quat_y(q), 0.001, "Orientation Y round-trips");
    EXPECT_NEAR(rt_quat_w(out), rt_quat_w(q), 0.001, "Orientation W round-trips");
}

static void test_body_torque_updates_angular_velocity_and_orientation() {
    void *w = rt_world3d_new(0, 0, 0);
    void *b = rt_body3d_new_sphere(1.0, 2.0);
    rt_world3d_add(w, b);
    rt_body3d_apply_torque(b, 0.0, 4.0, 0.0);
    rt_world3d_step(w, 1.0);

    void *ang = rt_body3d_get_angular_velocity(b);
    void *q = rt_body3d_get_orientation(b);
    EXPECT_TRUE(rt_vec3_y(ang) > 1.5, "Torque increases angular velocity");
    EXPECT_TRUE(fabs(rt_quat_w(q)) < 0.95, "Torque integration changes orientation");
}

static void test_body_angular_damping() {
    void *w = rt_world3d_new(0, 0, 0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_angular_velocity(b, 0.0, 4.0, 0.0);
    rt_body3d_set_angular_damping(b, 1.0);
    rt_world3d_add(w, b);

    rt_world3d_step(w, 1.0);
    {
        void *ang = rt_body3d_get_angular_velocity(b);
        EXPECT_TRUE(rt_vec3_y(ang) < 4.0, "Angular damping reduces angular velocity");
    }
}

static void test_body_sleep_and_wake() {
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    EXPECT_TRUE(rt_body3d_is_sleeping(b) == 0, "Dynamic body starts awake");
    rt_body3d_sleep(b);
    EXPECT_TRUE(rt_body3d_is_sleeping(b) != 0, "Sleep() marks body sleeping");
    rt_body3d_wake(b);
    EXPECT_TRUE(rt_body3d_is_sleeping(b) == 0, "Wake() clears sleeping state");
}

static void test_body_auto_sleep() {
    void *w = rt_world3d_new(0, 0, 0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    rt_world3d_add(w, b);
    for (int i = 0; i < 40; i++)
        rt_world3d_step(w, 1.0 / 60.0);
    EXPECT_TRUE(rt_body3d_is_sleeping(b) != 0, "Idle dynamic body auto-sleeps");
}

static void test_kinematic_body_ignores_gravity_but_integrates_velocity() {
    void *w = rt_world3d_new(0, -9.81, 0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_kinematic(b, 1);
    rt_body3d_set_velocity(b, 2.0, 0.0, 0.0);
    rt_body3d_set_angular_velocity(b, 0.0, 2.0, 0.0);
    rt_world3d_add(w, b);
    rt_world3d_step(w, 1.0);

    {
        void *pos = rt_body3d_get_position(b);
        void *q = rt_body3d_get_orientation(b);
        EXPECT_NEAR(
            rt_vec3_x(pos), 2.0, 0.05, "Kinematic body advances from explicit velocity");
        EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.05, "Kinematic body ignores gravity");
        EXPECT_TRUE(
            fabs(rt_quat_w(q)) < 0.95, "Kinematic angular velocity updates orientation");
    }
}

static void test_ccd_prevents_fast_sphere_tunneling() {
    void *w = rt_world3d_new(0, 0, 0);
    void *wall = rt_body3d_new_aabb(0.1, 2.0, 2.0, 0.0);
    void *sphere = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(wall, 5.0, 0.0, 0.0);
    rt_body3d_set_position(sphere, 0.0, 0.0, 0.0);
    rt_body3d_set_velocity(sphere, 100.0, 0.0, 0.0);
    rt_body3d_set_use_ccd(sphere, 1);
    rt_world3d_add(w, wall);
    rt_world3d_add(w, sphere);

    rt_world3d_step(w, 0.1);
    {
        void *pos = rt_body3d_get_position(sphere);
        EXPECT_TRUE(rt_vec3_x(pos) < 5.6, "CCD body does not tunnel through thin wall");
    }
}

/*==========================================================================
 * World tests
 *=========================================================================*/

static void test_world_create() {
    void *w = rt_world3d_new(0, -9.81, 0);
    EXPECT_TRUE(w != nullptr, "World created");
    EXPECT_TRUE(rt_world3d_body_count(w) == 0, "World starts empty");
}

static void test_world_add_remove() {
    void *w = rt_world3d_new(0, -9.81, 0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_world3d_add(w, b);
    EXPECT_TRUE(rt_world3d_body_count(w) == 1, "Body count = 1 after add");
    rt_world3d_remove(w, b);
    EXPECT_TRUE(rt_world3d_body_count(w) == 0, "Body count = 0 after remove");
}

static void test_gravity_integration() {
    void *w = rt_world3d_new(0, -10.0, 0);
    void *b = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    rt_body3d_set_position(b, 0, 10, 0);
    rt_world3d_add(w, b);

    /* Step 1 second: velocity += gravity*dt = -10, pos += velocity*dt = 10 + (-10)*1 = 0 */
    rt_world3d_step(w, 1.0);

    void *vel = rt_body3d_get_velocity(b);
    EXPECT_NEAR(rt_vec3_y(vel), -10.0, 0.1, "After 1s: velocity Y = -10");

    void *pos = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.1, "After 1s: position Y = 0");
}

static void test_force_application() {
    void *w = rt_world3d_new(0, 0, 0); /* no gravity */
    void *b = rt_body3d_new_aabb(0.5, 0.5, 0.5, 2.0);
    rt_body3d_set_position(b, 0, 0, 0);
    rt_world3d_add(w, b);

    /* Apply force of 10N in X. a = F/m = 5 m/s². Over 1s: v = 5, x = 5 */
    rt_body3d_apply_force(b, 10.0, 0, 0);
    rt_world3d_step(w, 1.0);

    void *vel = rt_body3d_get_velocity(b);
    EXPECT_NEAR(rt_vec3_x(vel), 5.0, 0.01, "Force: velocity X = F/m = 5");

    void *pos = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos), 5.0, 0.01, "Force: position X = 5");
}

static void test_impulse_application() {
    void *b = rt_body3d_new_aabb(0.5, 0.5, 0.5, 2.0);
    rt_body3d_apply_impulse(b, 10.0, 0, 0);
    void *vel = rt_body3d_get_velocity(b);
    /* impulse changes velocity by impulse * inv_mass = 10 * 0.5 = 5 */
    EXPECT_NEAR(rt_vec3_x(vel), 5.0, 0.01, "Impulse: velocity X = impulse/mass = 5");
}

/*==========================================================================
 * Collision tests
 *=========================================================================*/

static void test_collision_aabb_overlap() {
    void *w = rt_world3d_new(0, 0, 0); /* no gravity */
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 1.5, 0, 0); /* overlapping by 0.5 in X */

    rt_world3d_add(w, a);
    rt_world3d_add(w, b);
    rt_world3d_step(w, 0.016);

    /* After collision resolution, bodies should be pushed apart */
    void *pa = rt_body3d_get_position(a);
    void *pb = rt_body3d_get_position(b);
    double gap = rt_vec3_x(pb) - rt_vec3_x(pa);
    EXPECT_TRUE(gap > 1.5, "AABB collision pushes bodies apart");
}

static void test_collision_layer_filtering() {
    void *w = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 1.5, 0, 0);

    /* Put them on different layers that don't match each other's masks */
    rt_body3d_set_collision_layer(a, 1);
    rt_body3d_set_collision_mask(a, 1); /* a only collides with layer 1 */
    rt_body3d_set_collision_layer(b, 2);
    rt_body3d_set_collision_mask(b, 2); /* b only collides with layer 2 */

    rt_world3d_add(w, a);
    rt_world3d_add(w, b);
    rt_world3d_step(w, 0.016);

    /* Bodies should NOT be pushed apart — layers don't match */
    void *pa = rt_body3d_get_position(a);
    void *pb = rt_body3d_get_position(b);
    double gap = rt_vec3_x(pb) - rt_vec3_x(pa);
    EXPECT_NEAR(gap, 1.5, 0.01, "Filtered layers: no collision push");
}

static void test_trigger_no_push() {
    void *w = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 1.5, 0, 0);
    rt_body3d_set_trigger(a, 1); /* a is a trigger — overlap only */

    rt_world3d_add(w, a);
    rt_world3d_add(w, b);
    rt_world3d_step(w, 0.016);

    void *pa = rt_body3d_get_position(a);
    void *pb = rt_body3d_get_position(b);
    double gap = rt_vec3_x(pb) - rt_vec3_x(pa);
    EXPECT_NEAR(gap, 1.5, 0.01, "Trigger body: no collision push");
}

static void test_ground_detection() {
    void *w = rt_world3d_new(0, -10.0, 0);
    /* Floor: static AABB at y=0, half-extents (10,0.5,10) */
    void *floor = rt_body3d_new_aabb(10.0, 0.5, 10.0, 0.0);
    rt_body3d_set_position(floor, 0, -0.5, 0); /* top surface at y=0 */

    /* Dynamic box sitting on top of floor */
    void *box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    rt_body3d_set_position(box, 0, 0.5, 0); /* bottom at y=0, touching floor top */

    rt_world3d_add(w, floor);
    rt_world3d_add(w, box);

    /* Step a few frames to let gravity pull box down and collision resolve */
    for (int i = 0; i < 10; i++)
        rt_world3d_step(w, 0.016);

    EXPECT_TRUE(rt_body3d_is_grounded(box) != 0, "Box on floor is grounded");
}

/*==========================================================================
 * Character controller tests
 *=========================================================================*/

static void test_character_create() {
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    EXPECT_TRUE(c != nullptr, "Character controller created");
    EXPECT_NEAR(rt_character3d_get_step_height(c), 0.3, 0.01, "Default step height = 0.3");
}

static void test_character_position() {
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    rt_character3d_set_position(c, 1.0, 2.0, 3.0);
    void *pos = rt_character3d_get_position(c);
    EXPECT_NEAR(rt_vec3_x(pos), 1.0, 0.01, "Character pos X = 1");
    EXPECT_NEAR(rt_vec3_y(pos), 2.0, 0.01, "Character pos Y = 2");
    EXPECT_NEAR(rt_vec3_z(pos), 3.0, 0.01, "Character pos Z = 3");
}

static void test_character_step_height() {
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    rt_character3d_set_step_height(c, 0.5);
    EXPECT_NEAR(rt_character3d_get_step_height(c), 0.5, 0.01, "Step height set to 0.5");
}

static void test_character_world_binding() {
    void *w = rt_world3d_new(0, -9.81, 0);
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    rt_character3d_set_world(c, w);
    EXPECT_TRUE(rt_character3d_get_world(c) == w, "Character world getter returns bound world");
    rt_character3d_set_world(c, NULL);
    EXPECT_TRUE(rt_character3d_get_world(c) == nullptr, "Character world can be cleared");
}

static void test_character_slide_against_wall() {
    void *w = rt_world3d_new(0, 0, 0);
    void *floor = rt_body3d_new_aabb(10.0, 0.5, 10.0, 0.0);
    void *wall = rt_body3d_new_aabb(0.5, 2.0, 2.0, 0.0);
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    void *v = rt_vec3_new(3.0, 0.0, 0.0);
    rt_body3d_set_position(floor, 0.0, -0.5, 0.0);
    rt_body3d_set_position(wall, 2.0, 1.5, 0.0);
    rt_world3d_add(w, floor);
    rt_world3d_add(w, wall);
    rt_character3d_set_world(c, w);
    rt_character3d_set_position(c, 0.0, 1.0, 0.0);

    rt_character3d_move(c, v, 1.0);

    {
        void *pos = rt_character3d_get_position(c);
        EXPECT_TRUE(rt_vec3_x(pos) <= 1.05, "Character stops before wall instead of tunneling");
        EXPECT_TRUE(rt_character3d_is_grounded(c) != 0, "Character remains grounded while sliding");
    }
}

static void test_character_step_up() {
    void *w = rt_world3d_new(0, 0, 0);
    void *floor = rt_body3d_new_aabb(10.0, 0.5, 10.0, 0.0);
    void *step = rt_body3d_new_aabb(0.5, 0.125, 0.5, 0.0);
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    void *v = rt_vec3_new(2.0, 0.0, 0.0);
    rt_body3d_set_position(floor, 0.0, -0.5, 0.0);
    rt_body3d_set_position(step, 1.2, 0.125, 0.0);
    rt_world3d_add(w, floor);
    rt_world3d_add(w, step);
    rt_character3d_set_world(c, w);
    rt_character3d_set_position(c, 0.0, 1.0, 0.0);

    rt_character3d_move(c, v, 1.0);

    {
        void *pos = rt_character3d_get_position(c);
        EXPECT_TRUE(rt_vec3_x(pos) > 1.0, "Character steps forward across low obstacle");
        EXPECT_TRUE(rt_vec3_y(pos) > 1.1, "Character steps up onto walkable obstacle");
    }
}

/*==========================================================================
 * Trigger3D tests
 *=========================================================================*/

/*==========================================================================
 * Sphere-sphere collision tests
 *=========================================================================*/

static void test_sphere_sphere_collision() {
    /* Two spheres overlapping — should produce radial normal */
    void *world = rt_world3d_new(0, 0, 0);
    void *s1 = rt_body3d_new_sphere(1.0, 1.0);
    void *s2 = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(s1, 0, 0, 0);
    rt_body3d_set_position(s2, 1.5, 0, 0); /* overlap: sum_r=2 > dist=1.5 */
    rt_world3d_add(world, s1);
    rt_world3d_add(world, s2);
    rt_world3d_step(world, 1.0 / 60.0);

    /* After collision, s2 should have moved right (positive X) — radial pushout */
    void *pos2 = rt_body3d_get_position(s2);
    EXPECT_TRUE(rt_vec3_x(pos2) > 1.5, "sphere-sphere: s2 pushed right after collision");
    /* Collision event should have been recorded with radial normal */
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                "sphere-sphere: 1 collision event recorded");
}

static void test_sphere_sphere_no_overlap() {
    /* Two spheres not overlapping — no collision */
    void *world = rt_world3d_new(0, 0, 0);
    void *s1 = rt_body3d_new_sphere(1.0, 1.0);
    void *s2 = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(s1, 0, 0, 0);
    rt_body3d_set_position(s2, 3.0, 0, 0); /* no overlap: sum_r=2 < dist=3 */
    rt_world3d_add(world, s1);
    rt_world3d_add(world, s2);
    rt_world3d_step(world, 1.0 / 60.0);

    /* Collision count should be 0 */
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 0,
                "sphere-sphere: no collision when separated");
}

static void test_aabb_sphere_collision() {
    /* AABB and sphere overlapping */
    void *world = rt_world3d_new(0, 0, 0);
    void *box = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *sph = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(box, 0, 0, 0);
    rt_body3d_set_position(sph, 1.5, 0, 0); /* overlap: box extends to 1.0, sphere from 0.5 */
    rt_world3d_add(world, box);
    rt_world3d_add(world, sph);
    rt_world3d_step(world, 1.0 / 60.0);

    EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0, "aabb-sphere: collision detected");
}

static void test_capsule_aabb_collision() {
    void *world = rt_world3d_new(0, 0, 0);
    void *capsule = rt_body3d_new_capsule(0.5, 2.0, 1.0);
    void *wall = rt_body3d_new_aabb(0.5, 2.0, 2.0, 0.0);
    rt_body3d_set_position(capsule, 1.2, 1.0, 0.0);
    rt_body3d_set_position(wall, 2.0, 1.0, 0.0);
    rt_world3d_add(world, capsule);
    rt_world3d_add(world, wall);
    rt_world3d_step(world, 1.0 / 60.0);

    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1, "capsule-aabb: collision recorded");
    {
        void *pos = rt_body3d_get_position(capsule);
        EXPECT_TRUE(rt_vec3_x(pos) < 1.2, "capsule-aabb: capsule pushed out of wall");
    }
}

static void test_convex_hull_collider_blocks_sphere() {
    void *world = rt_world3d_new(0, 0, 0);
    void *mesh = rt_mesh3d_new_box(2.0, 2.0, 2.0);
    void *collider = rt_collider3d_new_convex_hull(mesh);
    void *hull_body = rt_body3d_new(0.0);
    void *sphere = rt_body3d_new_sphere(0.6, 1.0);
    rt_body3d_set_collider(hull_body, collider);
    rt_body3d_set_position(hull_body, 0.0, 0.0, 0.0);
    rt_body3d_set_position(sphere, 1.3, 0.0, 0.0);
    rt_world3d_add(world, hull_body);
    rt_world3d_add(world, sphere);
    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0,
                "convex hull: collision detected against sphere");
}

static void test_mesh_collider_blocks_falling_sphere() {
    void *world = rt_world3d_new(0, -9.81, 0);
    void *mesh = rt_mesh3d_new_box(8.0, 1.0, 8.0);
    void *collider = rt_collider3d_new_mesh(mesh);
    void *floor_body = rt_body3d_new(0.0);
    void *sphere = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_collider(floor_body, collider);
    rt_body3d_set_position(floor_body, 0.0, -0.5, 0.0);
    rt_body3d_set_position(sphere, 0.0, 2.0, 0.0);
    rt_world3d_add(world, floor_body);
    rt_world3d_add(world, sphere);
    for (int i = 0; i < 90; ++i)
        rt_world3d_step(world, 1.0 / 60.0);
    {
        void *pos = rt_body3d_get_position(sphere);
        EXPECT_TRUE(rt_vec3_y(pos) > -0.2, "mesh collider: sphere stays above floor");
    }
}

static void test_compound_collider_child_transform_affects_contact() {
    void *world = rt_world3d_new(0, 0, 0);
    void *compound = rt_collider3d_new_compound();
    void *child = rt_collider3d_new_box(0.5, 0.5, 0.5);
    void *xf = rt_transform3d_new();
    void *compound_body = rt_body3d_new(0.0);
    void *sphere = rt_body3d_new_sphere(0.45, 1.0);
    rt_transform3d_set_position(xf, 2.0, 0.0, 0.0);
    rt_collider3d_add_child(compound, child, xf);
    rt_body3d_set_collider(compound_body, compound);
    rt_body3d_set_position(sphere, 2.2, 0.0, 0.0);
    rt_world3d_add(world, compound_body);
    rt_world3d_add(world, sphere);
    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0,
                "compound collider: child offset affects contacts");
}

static void test_heightfield_collider_supports_ground_contact() {
    void *world = rt_world3d_new(0, 0, 0);
    void *pixels = rt_pixels_new(4, 4);
    void *heightfield;
    void *terrain_body;
    void *sphere;
    for (int64_t z = 0; z < 4; ++z) {
        for (int64_t x = 0; x < 4; ++x) {
            uint16_t h = (uint16_t)(32768 + x * 4096);
            rt_pixels_set(pixels, x, z, encode_height16(h));
        }
    }
    heightfield = rt_collider3d_new_heightfield(pixels, 1.0, 2.0, 1.0);
    terrain_body = rt_body3d_new(0.0);
    sphere = rt_body3d_new_sphere(0.35, 1.0);
    rt_body3d_set_collider(terrain_body, heightfield);
    rt_body3d_set_position(sphere, 0.8, 1.1, 0.0);
    rt_world3d_add(world, terrain_body);
    rt_world3d_add(world, sphere);
    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0,
                "heightfield collider: contact detected");
}

/*==========================================================================
 * Collision event queue tests
 *=========================================================================*/

static void test_collision_event_count() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 0.5, 0, 0); /* overlapping */
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_step(world, 1.0 / 60.0);

    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1, "collision event: 1 contact recorded");
}

static void test_collision_event_bodies() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 0.5, 0, 0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_step(world, 1.0 / 60.0);

    void *body_a = rt_world3d_get_collision_body_a(world, 0);
    void *body_b = rt_world3d_get_collision_body_b(world, 0);
    EXPECT_TRUE(body_a == a, "collision event: body A matches");
    EXPECT_TRUE(body_b == b, "collision event: body B matches");

    void *normal = rt_world3d_get_collision_normal(world, 0);
    EXPECT_TRUE(normal != nullptr, "collision event: normal is non-null");

    double depth = rt_world3d_get_collision_depth(world, 0);
    EXPECT_TRUE(depth > 0, "collision event: depth > 0");
}

/*==========================================================================
 * Trigger3D tests
 *=========================================================================*/

static void test_trigger_create() {
    void *t = rt_trigger3d_new(-1, -1, -1, 1, 1, 1);
    EXPECT_TRUE(t != nullptr, "Trigger3D created");
}

static void test_trigger_contains_inside() {
    void *t = rt_trigger3d_new(-2, -2, -2, 2, 2, 2);
    void *p = rt_vec3_new(0, 0, 0);
    EXPECT_TRUE(rt_trigger3d_contains(t, p) != 0, "Origin inside [-2,2] trigger");
}

static void test_trigger_contains_outside() {
    void *t = rt_trigger3d_new(-1, -1, -1, 1, 1, 1);
    void *p = rt_vec3_new(5, 0, 0);
    EXPECT_TRUE(rt_trigger3d_contains(t, p) == 0, "Point at (5,0,0) outside [-1,1] trigger");
}

static void test_trigger_enter_detection() {
    void *w = rt_world3d_new(0, 0, 0);
    void *body = rt_body3d_new_aabb(0.1, 0.1, 0.1, 1.0);
    rt_body3d_set_position(body, 5, 0, 0);
    rt_world3d_add(w, body);

    void *t = rt_trigger3d_new(-1, -1, -1, 1, 1, 1);

    /* Frame 1: body outside */
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 0, "Frame 1: no enter");

    /* Move body inside the trigger */
    rt_body3d_set_position(body, 0, 0, 0);

    /* Frame 2: body now inside — should detect enter */
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 1, "Frame 2: enter detected");
    EXPECT_TRUE(rt_trigger3d_get_exit_count(t) == 0, "Frame 2: no exit");
}

static void test_trigger_exit_detection() {
    void *w = rt_world3d_new(0, 0, 0);
    void *body = rt_body3d_new_aabb(0.1, 0.1, 0.1, 1.0);
    rt_body3d_set_position(body, 0, 0, 0);
    rt_world3d_add(w, body);

    void *t = rt_trigger3d_new(-1, -1, -1, 1, 1, 1);

    /* Frame 1: body inside */
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 1, "Frame 1: initial enter");

    /* Move body outside */
    rt_body3d_set_position(body, 5, 0, 0);

    /* Frame 2: body now outside — should detect exit */
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_exit_count(t) == 1, "Frame 2: exit detected");
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 0, "Frame 2: no re-enter");
}

static void test_trigger_multiple_bodies() {
    void *w = rt_world3d_new(0, 0, 0);
    void *b1 = rt_body3d_new_aabb(0.1, 0.1, 0.1, 1.0);
    void *b2 = rt_body3d_new_aabb(0.1, 0.1, 0.1, 1.0);
    rt_body3d_set_position(b1, 5, 0, 0);
    rt_body3d_set_position(b2, 5, 0, 0);
    rt_world3d_add(w, b1);
    rt_world3d_add(w, b2);

    void *t = rt_trigger3d_new(-1, -1, -1, 1, 1, 1);

    /* Frame 1: both outside */
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 0, "Frame 1: both outside");

    /* Move only b1 inside */
    rt_body3d_set_position(b1, 0, 0, 0);
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 1, "Frame 2: one enters");

    /* Move b2 inside too */
    rt_body3d_set_position(b2, 0, 0, 0);
    rt_trigger3d_update(t, w);
    EXPECT_TRUE(rt_trigger3d_get_enter_count(t) == 1, "Frame 3: second body enters");
}

static void test_trigger_set_bounds() {
    void *t = rt_trigger3d_new(0, 0, 0, 1, 1, 1);
    void *p = rt_vec3_new(5, 5, 5);
    EXPECT_TRUE(rt_trigger3d_contains(t, p) == 0, "Before resize: outside");

    rt_trigger3d_set_bounds(t, 0, 0, 0, 10, 10, 10);
    EXPECT_TRUE(rt_trigger3d_contains(t, p) != 0, "After resize: inside");
}

/*==========================================================================
 * Joint tests
 *=========================================================================*/

static void test_distance_joint_create() {
    void *a = rt_body3d_new_sphere(1.0, 1.0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    void *j = rt_distance_joint3d_new(a, b, 5.0);
    EXPECT_TRUE(j != nullptr, "DistanceJoint3D.New creates joint");
    EXPECT_NEAR(rt_distance_joint3d_get_distance(j), 5.0, 0.01, "DistanceJoint3D distance = 5.0");
}

static void test_distance_joint_constraint() {
    /* Two bodies separated by 10 units, connected by distance joint of 5 */
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.5, 1.0);
    void *b = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 10, 0, 0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, rt_distance_joint3d_new(a, b, 5.0), RT_JOINT_DISTANCE);

    /* Step several times — bodies should move toward target distance */
    for (int i = 0; i < 60; i++)
        rt_world3d_step(world, 1.0 / 60.0);

    void *pos_a = rt_body3d_get_position(a);
    void *pos_b = rt_body3d_get_position(b);
    double dist = fabs(rt_vec3_x(pos_b) - rt_vec3_x(pos_a));
    EXPECT_NEAR(dist, 5.0, 0.5, "DistanceJoint: bodies converge to target distance");
}

static void test_spring_joint_create() {
    void *a = rt_body3d_new_sphere(1.0, 1.0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    void *j = rt_spring_joint3d_new(a, b, 3.0, 100.0, 5.0);
    EXPECT_TRUE(j != nullptr, "SpringJoint3D.New creates joint");
    EXPECT_NEAR(rt_spring_joint3d_get_stiffness(j), 100.0, 0.01, "SpringJoint3D stiffness = 100");
    EXPECT_NEAR(rt_spring_joint3d_get_damping(j), 5.0, 0.01, "SpringJoint3D damping = 5");
    EXPECT_NEAR(rt_spring_joint3d_get_rest_length(j), 3.0, 0.01, "SpringJoint3D rest length = 3");
}

static void test_spring_joint_force() {
    /* Two bodies separated by 10 units, spring rest length 3, high stiffness */
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.5, 1.0);
    void *b = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 10, 0, 0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, rt_spring_joint3d_new(a, b, 3.0, 50.0, 10.0), RT_JOINT_SPRING);

    /* After stepping, bodies should move closer due to spring force */
    for (int i = 0; i < 30; i++)
        rt_world3d_step(world, 1.0 / 60.0);

    void *pos_b = rt_body3d_get_position(b);
    EXPECT_TRUE(rt_vec3_x(pos_b) < 10.0, "SpringJoint: body B pulled toward A by spring");
}

static void test_world_joint_management() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(1.0, 1.0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    void *j = rt_distance_joint3d_new(a, b, 5.0);

    EXPECT_TRUE(rt_world3d_joint_count(world) == 0, "World: 0 joints initially");
    rt_world3d_add_joint(world, j, RT_JOINT_DISTANCE);
    EXPECT_TRUE(rt_world3d_joint_count(world) == 1, "World: 1 joint after add");
    rt_world3d_remove_joint(world, j);
    EXPECT_TRUE(rt_world3d_joint_count(world) == 0, "World: 0 joints after remove");
}

int main() {
    /* Body creation */
    test_body_new_aabb();
    test_body_new_sphere();
    test_body_new_capsule();
    test_body_static_zero_mass();
    test_body_new_and_set_collider();
    test_wrapper_constructors_assign_colliders();
    test_mesh_collider_attaches_to_static_body();

    /* Property accessors */
    test_body_position();
    test_body_orientation_roundtrip();
    test_body_velocity();
    test_body_collision_layer_mask();
    test_body_trigger();
    test_body_torque_updates_angular_velocity_and_orientation();
    test_body_angular_damping();
    test_body_sleep_and_wake();
    test_body_auto_sleep();
    test_kinematic_body_ignores_gravity_but_integrates_velocity();
    test_ccd_prevents_fast_sphere_tunneling();

    /* World */
    test_world_create();
    test_world_add_remove();
    test_gravity_integration();
    test_force_application();
    test_impulse_application();

    /* Collision */
    test_collision_aabb_overlap();
    test_collision_layer_filtering();
    test_trigger_no_push();
    test_ground_detection();

    /* Character controller */
    test_character_create();
    test_character_position();
    test_character_step_height();
    test_character_world_binding();
    test_character_slide_against_wall();
    test_character_step_up();

    /* Sphere-sphere collision tests — declared below */

    /* Trigger3D */
    test_trigger_create();
    test_trigger_contains_inside();
    test_trigger_contains_outside();
    test_trigger_enter_detection();
    test_trigger_exit_detection();
    test_trigger_multiple_bodies();
    test_trigger_set_bounds();

    /* Sphere-sphere collision */
    test_sphere_sphere_collision();
    test_sphere_sphere_no_overlap();
    test_aabb_sphere_collision();
    test_capsule_aabb_collision();
    test_convex_hull_collider_blocks_sphere();
    test_mesh_collider_blocks_falling_sphere();
    test_compound_collider_child_transform_affects_contact();
    test_heightfield_collider_supports_ground_contact();

    /* Collision event queue */
    test_collision_event_count();
    test_collision_event_bodies();

    /* Joint tests */
    test_distance_joint_create();
    test_distance_joint_constraint();
    test_spring_joint_create();
    test_spring_joint_force();
    test_world_joint_management();

    printf("Physics3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
