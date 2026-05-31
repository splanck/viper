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

#ifndef VIPER_ENABLE_GRAPHICS
#define VIPER_ENABLE_GRAPHICS 1
#endif

#include "rt.hpp"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_collider3d.h"
#include "rt_internal.h"
#include "rt_joints3d.h"
#include "rt_mat4.h"
#include "rt_physics3d.h"
#include "rt_pixels.h"
#include "rt_quat.h"
#include "rt_transform3d.h"
#include <cassert>
#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
extern void *rt_vec3_new(double x, double y, double z);
extern double rt_vec3_x(void *v);
extern double rt_vec3_y(void *v);
extern double rt_vec3_z(void *v);
extern int rt_obj_release_check0(void *obj);
extern void *rt_mesh3d_new(void);
extern void rt_mesh3d_add_vertex(
    void *obj, double x, double y, double z, double nx, double ny, double nz, double u, double v);
extern void rt_mesh3d_add_triangle(void *obj, int64_t i0, int64_t i1, int64_t i2);
}

static int tests_passed = 0;
static int tests_run = 0;
static const double TEST_PI = 3.14159265358979323846;

namespace {
static std::jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_expect_trap = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_expect_trap)
        std::longjmp(g_trap_jmp, 1);
    rt_abort(msg);
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

template <typename Fn> static bool expect_trap_contains(Fn &&fn, const char *needle) {
    g_last_trap = nullptr;
    g_expect_trap = true;
    if (setjmp(g_trap_jmp) == 0) {
        fn();
        g_expect_trap = false;
        return false;
    }
    g_expect_trap = false;
    return g_last_trap && (!needle || std::strstr(g_last_trap, needle) != nullptr);
}

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

static void *make_tetra_mesh(int sign) {
    void *mesh = rt_mesh3d_new();
    double s = sign >= 0 ? 1.0 : -1.0;
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0);
    rt_mesh3d_add_vertex(mesh, s, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, 0.0, s, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, 0.0, 0.0, s, 0.0, 1.0, 0.0, 1.0, 1.0);
    rt_mesh3d_add_triangle(mesh, 0, 2, 1);
    rt_mesh3d_add_triangle(mesh, 0, 1, 3);
    rt_mesh3d_add_triangle(mesh, 0, 3, 2);
    rt_mesh3d_add_triangle(mesh, 1, 2, 3);
    return mesh;
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
    EXPECT_TRUE(rt_collider3d_get_type(rt_body3d_get_collider(sphere)) == RT_COLLIDER3D_TYPE_SPHERE,
                "Sphere wrapper assigns sphere collider");
    EXPECT_TRUE(rt_collider3d_get_type(rt_body3d_get_collider(capsule)) ==
                    RT_COLLIDER3D_TYPE_CAPSULE,
                "Capsule wrapper assigns capsule collider");
}

static void test_collider_constructors_sanitize_nonfinite_dimensions() {
    double half_extents[3];
    double mn[3], mx[3];
    void *box = rt_collider3d_new_box(NAN, -2.0, INFINITY);
    rt_collider3d_get_box_half_extents_raw(box, half_extents);
    EXPECT_NEAR(
        half_extents[0], 1.0, 0.001, "Box collider NaN half extent falls back to unit extent");
    EXPECT_NEAR(half_extents[1], 2.0, 0.001, "Box collider negative half extent becomes positive");
    EXPECT_NEAR(
        half_extents[2], 1.0, 0.001, "Box collider infinite half extent falls back to unit extent");

    void *sphere = rt_collider3d_new_sphere(NAN);
    EXPECT_NEAR(rt_collider3d_get_radius_raw(sphere),
                1.0,
                0.001,
                "Sphere collider NaN radius falls back to unit radius");

    void *capsule = rt_collider3d_new_capsule(INFINITY, NAN);
    EXPECT_NEAR(rt_collider3d_get_radius_raw(capsule),
                1.0,
                0.001,
                "Capsule infinite radius falls back to unit radius");
    EXPECT_NEAR(rt_collider3d_get_height_raw(capsule),
                2.0,
                0.001,
                "Capsule NaN height falls back to diameter");

    void *pixels = rt_pixels_new(2, 2);
    rt_pixels_set(pixels, 0, 0, encode_height16(0));
    rt_pixels_set(pixels, 1, 0, encode_height16(65535));
    rt_pixels_set(pixels, 0, 1, encode_height16(0));
    rt_pixels_set(pixels, 1, 1, encode_height16(65535));
    void *heightfield = rt_collider3d_new_heightfield(pixels, NAN, INFINITY, -2.0);
    rt_collider3d_get_local_bounds_raw(heightfield, mn, mx);
    EXPECT_NEAR(mx[0], 0.5, 0.001, "Heightfield NaN X scale falls back to unit scale");
    EXPECT_NEAR(mx[1], 1.0, 0.001, "Heightfield infinite Y scale falls back to unit scale");
    EXPECT_NEAR(mx[2], 1.0, 0.001, "Heightfield negative Z scale uses absolute value");
}

static void test_mesh_collider_attaches_to_static_body() {
    void *mesh = rt_mesh3d_new_box(4.0, 1.0, 4.0);
    void *mesh_collider = rt_collider3d_new_mesh(mesh);
    void *body = rt_body3d_new(0.0);
    EXPECT_TRUE(body != nullptr, "Static body for mesh collider created");
    rt_body3d_set_collider(body, mesh_collider);
    EXPECT_TRUE(rt_body3d_get_collider(body) == mesh_collider, "Static body accepts mesh collider");
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

static void test_body_scale_affects_queries() {
    void *world = rt_world3d_new(0, 0, 0);
    void *body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    rt_body3d_set_scale(body, 3.0, 1.0, 1.0);
    rt_world3d_add(world, body);
    void *scale = rt_body3d_get_scale(body);
    EXPECT_NEAR(rt_vec3_x(scale), 3.0, 0.001, "Body scale X round-trips");
    EXPECT_NEAR(rt_vec3_y(scale), 1.0, 0.001, "Body scale Y round-trips");
    EXPECT_NEAR(rt_vec3_z(scale), 1.0, 0.001, "Body scale Z round-trips");

    void *center = rt_vec3_new(2.5, 0.0, 0.0);
    void *hits = rt_world3d_overlap_sphere(world, center, 0.1, -1);
    EXPECT_TRUE(hits != nullptr, "scaled body overlap returns a hit list");
    EXPECT_TRUE(rt_physics_hit_list3d_get_count(hits) == 1,
                "scaled body AABB participates in broadphase queries");
}

static void test_body_velocity() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_velocity(b, 1.0, 2.0, 3.0);
    void *vel = rt_body3d_get_velocity(b);
    EXPECT_NEAR(rt_vec3_x(vel), 1.0, 0.01, "Velocity X = 1");
    EXPECT_NEAR(rt_vec3_y(vel), 2.0, 0.01, "Velocity Y = 2");
    EXPECT_NEAR(rt_vec3_z(vel), 3.0, 0.01, "Velocity Z = 3");
}

static void test_body_far_origin_integrates_sub_float_delta() {
    void *world = rt_world3d_new(0.0, 0.0, 0.0);
    void *body = rt_body3d_new_aabb(0.25, 0.25, 0.25, 1.0);
    constexpr double kBase = 1000000000.0;

    rt_body3d_set_position(body, kBase, 0.0, 0.0);
    rt_body3d_set_velocity(body, 0.125, 0.0, 0.0);
    rt_world3d_add(world, body);
    rt_world3d_step(world, 1.0);

    void *pos = rt_body3d_get_position(body);
    EXPECT_NEAR(rt_vec3_x(pos),
                kBase + 0.125,
                1e-9,
                "Physics body state preserves sub-float deltas at far origin");
}

static void test_body_collision_layer_mask() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_collision_layer(b, 4);
    rt_body3d_set_collision_mask(b, 6);
    EXPECT_TRUE(rt_body3d_get_collision_layer(b) == 4, "Collision layer = 4");
    EXPECT_TRUE(rt_body3d_get_collision_mask(b) == 6, "Collision mask = 6");
    EXPECT_TRUE(expect_trap_contains([&] { rt_body3d_set_collision_layer(b, 0); }, "positive"),
                "Collision layer 0 traps");
    EXPECT_TRUE(expect_trap_contains([&] { rt_body3d_set_collision_layer(b, -1); }, "positive"),
                "Collision layer negative traps");
    EXPECT_TRUE(rt_body3d_get_collision_layer(b) == 4, "Invalid collision layer leaves value");
}

static void test_body_material_coefficients_are_sanitized() {
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);

    rt_body3d_set_restitution(b, -1.0);
    EXPECT_NEAR(rt_body3d_get_restitution(b), 0.0, 0.001, "Negative restitution clamps to zero");
    rt_body3d_set_restitution(b, 2.0);
    EXPECT_NEAR(rt_body3d_get_restitution(b), 1.0, 0.001, "Restitution clamps to one");
    rt_body3d_set_restitution(b, NAN);
    EXPECT_NEAR(rt_body3d_get_restitution(b), 0.0, 0.001, "NaN restitution clamps to zero");

    rt_body3d_set_friction(b, -1.0);
    EXPECT_NEAR(rt_body3d_get_friction(b), 0.0, 0.001, "Negative friction clamps to zero");
    rt_body3d_set_friction(b, NAN);
    EXPECT_NEAR(rt_body3d_get_friction(b), 0.0, 0.001, "NaN friction clamps to zero");
    rt_body3d_set_friction(b, 2.5);
    EXPECT_NEAR(rt_body3d_get_friction(b), 2.5, 0.001, "Finite friction is preserved");
}

static void test_body_sanitizes_nonfinite_motion_state() {
    void *static_body = rt_body3d_new(NAN);
    EXPECT_NEAR(rt_body3d_get_mass(static_body), 0.0, 0.001, "NaN mass becomes static zero mass");
    EXPECT_TRUE(rt_body3d_is_static(static_body) != 0, "NaN mass body is static");

    void *b = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(b, NAN, 2.0, INFINITY);
    void *pos = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos), 0.0, 0.001, "Body non-finite position X falls back to 0");
    EXPECT_NEAR(rt_vec3_y(pos), 2.0, 0.001, "Body finite position Y is preserved");
    EXPECT_NEAR(rt_vec3_z(pos), 0.0, 0.001, "Body non-finite position Z falls back to 0");

    rt_body3d_set_velocity(b, NAN, 3.0, INFINITY);
    rt_body3d_apply_impulse(b, NAN, 2.0, INFINITY);
    void *vel = rt_body3d_get_velocity(b);
    EXPECT_NEAR(rt_vec3_x(vel), 0.0, 0.001, "Body non-finite velocity X falls back to 0");
    EXPECT_NEAR(rt_vec3_y(vel), 5.0, 0.001, "Body finite velocity and impulse are applied");
    EXPECT_NEAR(rt_vec3_z(vel), 0.0, 0.001, "Body non-finite velocity Z falls back to 0");

    rt_body3d_set_angular_velocity(b, INFINITY, 4.0, NAN);
    rt_body3d_apply_angular_impulse(b, INFINITY, 1.0, NAN);
    void *ang = rt_body3d_get_angular_velocity(b);
    EXPECT_NEAR(rt_vec3_x(ang), 0.0, 0.001, "Body non-finite angular velocity X falls back to 0");
    EXPECT_TRUE(rt_vec3_y(ang) > 4.0, "Body finite angular impulse updates Y angular velocity");
    EXPECT_NEAR(rt_vec3_z(ang), 0.0, 0.001, "Body non-finite angular velocity Z falls back to 0");

    rt_body3d_set_orientation(b, rt_quat_new(NAN, 0.0, 0.0, 0.0));
    EXPECT_NEAR(rt_quat_w(rt_body3d_get_orientation(b)),
                1.0,
                0.001,
                "Body invalid quaternion resets to identity");
    EXPECT_TRUE(expect_trap_contains([&] { rt_body3d_set_orientation(b, b); }, "Quat"),
                "Body.SetOrientation traps on non-Quat handles");
    EXPECT_NEAR(rt_quat_w(rt_body3d_get_orientation(b)), 1.0, 0.001, "Non-Quat leaves orientation");

    rt_body3d_set_linear_damping(b, INFINITY);
    rt_body3d_set_angular_damping(b, NAN);
    EXPECT_NEAR(
        rt_body3d_get_linear_damping(b), 0.0, 0.001, "Body non-finite linear damping clamps");
    EXPECT_NEAR(
        rt_body3d_get_angular_damping(b), 0.0, 0.001, "Body non-finite angular damping clamps");
}

static void test_body_extreme_motion_state_saturates() {
    const double state_max = 1000000000000.0;
    void *world = rt_world3d_new(0, 0, 0);
    void *body = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(body, 1.0e300, -1.0e300, 0.0);
    void *pos = rt_body3d_get_position(body);
    EXPECT_TRUE(std::isfinite(rt_vec3_x(pos)) && fabs(rt_vec3_x(pos)) <= state_max,
                "Extreme position X saturates to finite state bound");
    EXPECT_TRUE(std::isfinite(rt_vec3_y(pos)) && fabs(rt_vec3_y(pos)) <= state_max,
                "Extreme position Y saturates to finite state bound");

    rt_body3d_set_velocity(body, 1.0e300, 0.0, 0.0);
    rt_body3d_apply_impulse(body, 1.0e300, -1.0e300, 0.0);
    rt_body3d_apply_force(body, 1.0e300, 1.0e300, 0.0);
    rt_world3d_add(world, body);
    rt_world3d_step(world, 0.1);

    void *vel = rt_body3d_get_velocity(body);
    pos = rt_body3d_get_position(body);
    EXPECT_TRUE(std::isfinite(rt_vec3_x(vel)) && fabs(rt_vec3_x(vel)) <= state_max,
                "Extreme velocity X remains finite after impulse/force integration");
    EXPECT_TRUE(std::isfinite(rt_vec3_y(vel)) && fabs(rt_vec3_y(vel)) <= state_max,
                "Extreme velocity Y remains finite after impulse/force integration");
    EXPECT_TRUE(std::isfinite(rt_vec3_x(pos)) && fabs(rt_vec3_x(pos)) <= state_max,
                "Integrated position X remains finite after extreme motion");
    EXPECT_TRUE(std::isfinite(rt_vec3_y(pos)) && fabs(rt_vec3_y(pos)) <= state_max,
                "Integrated position Y remains finite after extreme motion");
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
        EXPECT_NEAR(rt_vec3_x(pos), 2.0, 0.05, "Kinematic body advances from explicit velocity");
        EXPECT_NEAR(rt_vec3_y(pos), 0.0, 0.05, "Kinematic body ignores gravity");
        EXPECT_TRUE(fabs(rt_quat_w(q)) < 0.95, "Kinematic angular velocity updates orientation");
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

    void *diag_world = rt_world3d_new(0, 0, 0);
    void *fast = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_velocity(fast, 1000.0, 0.0, 0.0);
    rt_body3d_set_use_ccd(fast, 1);
    rt_world3d_add(diag_world, fast);
    rt_world3d_step(diag_world, 0.1);
    EXPECT_TRUE(rt_world3d_get_last_ccd_requested_substeps(diag_world) >
                    rt_world3d_get_last_ccd_substeps(diag_world),
                "CCD diagnostics retain unclamped substep demand");
    EXPECT_TRUE(rt_world3d_get_last_ccd_substeps(diag_world) == 64,
                "CCD diagnostics report capped substeps");
    EXPECT_TRUE(rt_world3d_get_ccd_substep_clamped_count(diag_world) == 1,
                "CCD diagnostics count clamp events");

    void *huge_world = rt_world3d_new(0, 0, 0);
    void *huge = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_velocity(huge, 1.0e300, 0.0, 0.0);
    rt_body3d_set_use_ccd(huge, 1);
    rt_world3d_add(huge_world, huge);
    rt_world3d_step(huge_world, 0.1);
    EXPECT_TRUE(rt_world3d_get_last_ccd_requested_substeps(huge_world) >=
                    rt_world3d_get_last_ccd_substeps(huge_world),
                "CCD diagnostics stay ordered for saturated extreme speeds");
    EXPECT_TRUE(rt_world3d_get_last_ccd_substeps(huge_world) == 64,
                "CCD substeps remain capped for saturated extreme speeds");
    EXPECT_TRUE(rt_world3d_get_ccd_substep_clamped_count(huge_world) == 1,
                "CCD clamp diagnostics count saturated extreme speeds");
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
    EXPECT_TRUE(rt_world3d_try_add(w, b) != 0, "TryAdd returns true for a valid body");
    EXPECT_TRUE(rt_world3d_body_count(w) == 1, "Body count = 1 after add");
    EXPECT_TRUE(rt_world3d_contains_body(w, b) != 0, "World contains added body");
    EXPECT_TRUE(rt_world3d_try_add(w, b) != 0, "TryAdd returns true for an existing body");
    EXPECT_TRUE(rt_world3d_body_count(w) == 1, "TryAdd keeps duplicate body count stable");
    EXPECT_TRUE(rt_world3d_try_add(w, w) == 0, "TryAdd returns false for invalid body handles");
    rt_world3d_remove(w, b);
    EXPECT_TRUE(rt_world3d_body_count(w) == 0, "Body count = 0 after remove");
    EXPECT_TRUE(rt_world3d_contains_body(w, b) == 0, "World no longer contains removed body");
}

static void test_world_remove_purges_contacts_for_body() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 0.5, 0.0, 0.0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_collision_event_count(world) == 1,
                "World records contact before removal");

    rt_world3d_remove(world, b);
    EXPECT_TRUE(rt_world3d_get_collision_event_count(world) == 0,
                "World removal purges contacts mentioning the removed body");
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 0,
                "World removal purges enter events mentioning the removed body");
}

static void test_world_rejects_duplicate_body_adds() {
    void *w = rt_world3d_new(0, 0, 0);
    void *b = rt_body3d_new_sphere(0.5, 1.0);
    rt_world3d_add(w, b);
    rt_world3d_add(w, b);
    EXPECT_TRUE(rt_world3d_body_count(w) == 1, "World ignores duplicate body registration");

    rt_world3d_step(w, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_collision_count(w) == 0,
                "Duplicate body registration cannot self-collide");
}

static void test_world_body_storage_grows_past_initial_capacity() {
    void *w = rt_world3d_new(0, 0, 0);
    for (int i = 0; i < 300; i++) {
        void *b = rt_body3d_new_sphere(0.25, 0.0);
        rt_body3d_set_position(b, (double)i * 2.0, 0.0, 0.0);
        rt_world3d_add(w, b);
    }
    EXPECT_TRUE(rt_world3d_body_count(w) == 300, "World body storage grows past 256 bodies");
}

static void test_world_sparse_body_count_step_stress() {
    void *w = rt_world3d_new(0, 0, 0);
    for (int i = 0; i < 320; i++) {
        void *b = rt_body3d_new_sphere(0.25, 0.0);
        double x = (double)(i % 40) * 3.0;
        double z = (double)(i / 40) * 3.0;
        rt_body3d_set_position(b, x, 0.0, z);
        rt_world3d_add(w, b);
    }

    void *mover = rt_body3d_new_sphere(0.2, 1.0);
    rt_body3d_set_can_sleep(mover, 0);
    rt_body3d_set_position(mover, -10.0, 0.0, -10.0);
    rt_body3d_set_velocity(mover, 2.0, 0.0, 0.0);
    rt_world3d_add(w, mover);

    for (int i = 0; i < 16; i++)
        rt_world3d_step(w, 1.0 / 60.0);

    void *pos = rt_body3d_get_position(mover);
    EXPECT_TRUE(rt_world3d_body_count(w) == 321,
                "World step stress keeps every sparse body registered");
    EXPECT_TRUE(rt_world3d_get_collision_count(w) == 0,
                "World step stress keeps sparse bodies non-overlapping");
    EXPECT_TRUE(rt_vec3_x(pos) > -9.5, "World step stress integrates the dynamic body");
}

static void test_world_contact_storage_grows_past_initial_capacity() {
    void *w = rt_world3d_new(0, 0, 0);
    void *volume = rt_body3d_new_aabb(1000.0, 1000.0, 1000.0, 0.0);
    rt_body3d_set_trigger(volume, 1);
    rt_world3d_add(w, volume);
    for (int i = 0; i < 160; i++) {
        void *b = rt_body3d_new_sphere(0.25, 1.0);
        rt_body3d_set_position(b, -240.0 + (double)i * 3.0, 0.0, 0.0);
        rt_body3d_set_trigger(b, 1);
        rt_world3d_add(w, b);
    }
    rt_world3d_step(w, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_collision_count(w) >= 160,
                "World contact storage grows past 128 contacts");
    EXPECT_TRUE(rt_world3d_get_enter_event_count(w) >= 160,
                "World enter-event storage grows with contacts");
}

static void test_world_broadphase_rejects_separated_bodies() {
    void *w = rt_world3d_new(0, 0, 0);
    for (int i = 0; i < 300; i++) {
        void *b = rt_body3d_new_sphere(0.25, 1.0);
        rt_body3d_set_position(b, (double)i * 4.0, 0.0, 0.0);
        rt_body3d_set_trigger(b, 1);
        rt_world3d_add(w, b);
    }
    rt_world3d_step(w, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_collision_count(w) == 0,
                "Sweep-and-prune broadphase keeps separated bodies out of narrowphase");
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

static void test_impulse_at_point_adds_spin() {
    void *b = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    rt_body3d_set_position(b, 0.0, 0.0, 0.0);
    /* +X impulse applied at a point offset +Z from the center: linear +X plus a
     * torque about Y (r x impulse) — an off-center hit spins the body. */
    rt_body3d_apply_impulse_at_point(b, 1.0, 0.0, 0.0, 0.0, 0.0, 0.5);
    void *vel = rt_body3d_get_velocity(b);
    void *ang = rt_body3d_get_angular_velocity(b);
    EXPECT_NEAR(rt_vec3_x(vel), 1.0, 0.05, "Off-center impulse still adds linear velocity");
    EXPECT_TRUE(fabs(rt_vec3_y(ang)) > 0.2, "Off-center impulse adds angular velocity (spin)");
    /* A central impulse adds no spin, by contrast. */
    void *b2 = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    rt_body3d_apply_impulse_at_point(b2, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    void *ang2 = rt_body3d_get_angular_velocity(b2);
    EXPECT_NEAR(rt_vec3_y(ang2), 0.0, 0.001, "Central impulse adds no spin");
}

static void test_force_at_point_adds_spin() {
    void *w = rt_world3d_new(0, 0, 0);
    void *box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    rt_body3d_set_position(box, 0.0, 0.0, 0.0);
    rt_world3d_add(w, box);
    /* Force is cleared each step, so re-apply before each. +X force at a +Z point
     * builds linear +X velocity plus a spin about Y. */
    for (int i = 0; i < 5; i++) {
        rt_body3d_apply_force_at_point(box, 1.0, 0.0, 0.0, 0.0, 0.0, 0.5);
        rt_world3d_step(w, 1.0 / 60.0);
    }
    void *vel = rt_body3d_get_velocity(box);
    void *ang = rt_body3d_get_angular_velocity(box);
    EXPECT_TRUE(rt_vec3_x(vel) > 0.01, "Off-center force builds linear velocity");
    EXPECT_TRUE(fabs(rt_vec3_y(ang)) > 0.01, "Off-center force builds angular velocity (spin)");
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

/* Drop a stack of unit boxes onto a static floor and settle it for 600 fixed
 * steps. Writes each box's final center Y into out_y[] and its position drift
 * over the last 20 steps into out_drift[]. Exercises the warm-started
 * sequential-impulse solver's resting stability (AC-008). */
static void run_box_stack(int count, double *out_y, double *out_drift) {
    void *w = rt_world3d_new(0.0, -9.8, 0.0);
    /* Stacking is the canonical hard case for sequential-impulse solvers:
     * support must propagate up the stack, one contact per Gauss-Seidel sweep.
     * Games tune SolverIterations for stacks; use a realistic stacking budget. */
    rt_world3d_set_solver_iterations(w, 20);
    void *floor = rt_body3d_new_aabb(20.0, 0.5, 20.0, 0.0);
    rt_body3d_set_position(floor, 0.0, -0.5, 0.0); /* top surface at y=0 */
    rt_world3d_add(w, floor);

    void *boxes[16];
    for (int i = 0; i < count; ++i) {
        boxes[i] = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0); /* unit cube, mass 1 */
        /* Spawn the stack in resting contact (box i bottom on box i-1 top). */
        rt_body3d_set_position(boxes[i], 0.0, 0.5 + (double)i, 0.0);
        rt_world3d_add(w, boxes[i]);
    }

    double mid_y[16];
    for (int step = 0; step < 600; ++step) {
        rt_world3d_step(w, 1.0 / 60.0);
        if (step == 579) {
            for (int i = 0; i < count; ++i)
                mid_y[i] = rt_vec3_y(rt_body3d_get_position(boxes[i]));
        }
    }
    for (int i = 0; i < count; ++i) {
        out_y[i] = rt_vec3_y(rt_body3d_get_position(boxes[i]));
        out_drift[i] = fabs(out_y[i] - mid_y[i]);
    }
}

static void test_box_stack_rests_stably() {
    const int count = 5;
    double y1[5];
    double drift1[5];
    double y2[5];
    double drift2[5];
    run_box_stack(count, y1, drift1);
    run_box_stack(count, y2, drift2); /* identical second run for determinism */

    for (int i = 0; i < count; ++i) {
        double expected = 0.5 + (double)i; /* touching rest heights: 0.5, 1.5, ... */
        /* Rests near the ideal touching height — not fallen through, not
         * exploded. The tolerance absorbs the small per-contact penetration slop
         * accumulating down the loaded stack. */
        EXPECT_NEAR(y1[i], expected, 0.15, "Box stack: box rests near its touching height");
        /* Settled: negligible drift over the final 20 steps (no sink / no jitter). */
        EXPECT_TRUE(drift1[i] < 0.01, "Box stack: box has settled to rest (no drift)");
        /* Determinism: an identical run reproduces the resting state exactly. */
        EXPECT_NEAR(y1[i], y2[i], 1e-9, "Box stack: resting state is deterministic");
    }
    /* Stack stays ordered and separated (no box sinks through its neighbour). */
    for (int i = 1; i < count; ++i)
        EXPECT_TRUE(y1[i] > y1[i - 1] + 0.85, "Box stack: boxes stay separated and ordered");
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

static void test_character_sanitizes_motion_config() {
    void *c = rt_character3d_new(0.5, 2.0, 80.0);
    rt_character3d_set_step_height(c, NAN);
    EXPECT_NEAR(rt_character3d_get_step_height(c), 0.0, 0.001, "NaN step height clamps to zero");
    rt_character3d_set_step_height(c, -1.0);
    EXPECT_NEAR(
        rt_character3d_get_step_height(c), 0.0, 0.001, "Negative step height clamps to zero");

    rt_character3d_set_position(c, 1.0, 2.0, 3.0);
    rt_character3d_move(c, rt_vec3_new(NAN, 0.0, INFINITY), 1.0);
    void *pos = rt_character3d_get_position(c);
    EXPECT_TRUE(std::isfinite(rt_vec3_x(pos)) && std::isfinite(rt_vec3_y(pos)) &&
                    std::isfinite(rt_vec3_z(pos)),
                "Character move ignores non-finite velocity components");
    rt_character3d_move(c, rt_vec3_new(1.0, 0.0, 0.0), NAN);
    pos = rt_character3d_get_position(c);
    EXPECT_TRUE(std::isfinite(rt_vec3_x(pos)) && std::isfinite(rt_vec3_y(pos)) &&
                    std::isfinite(rt_vec3_z(pos)),
                "Character move ignores non-finite dt");
    double before_x = rt_vec3_x(pos);
    rt_character3d_move(c, c, 1.0);
    pos = rt_character3d_get_position(c);
    EXPECT_NEAR(rt_vec3_x(pos), before_x, 0.001, "Character3D.Move rejects non-Vec3 handles");
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

static void test_rotated_capsule_aabb_collision() {
    void *world = rt_world3d_new(0, 0, 0);
    void *capsule = rt_body3d_new_capsule(0.25, 4.0, 1.0);
    void *wall = rt_body3d_new_aabb(0.25, 0.5, 0.5, 0.0);
    void *q = rt_quat_from_axis_angle(rt_vec3_new(0.0, 0.0, 1.0), TEST_PI * 0.5);
    rt_body3d_set_orientation(capsule, q);
    rt_body3d_set_position(capsule, 0.0, 0.0, 0.0);
    rt_body3d_set_position(wall, 2.1, 0.0, 0.0);
    rt_world3d_add(world, capsule);
    rt_world3d_add(world, wall);
    rt_world3d_step(world, 1.0 / 60.0);

    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                "rotated capsule-aabb: oriented capsule axis participates in collision");
}

static void test_offcenter_contact_generates_angular_velocity() {
    void *world = rt_world3d_new(0, 0, 0);
    void *capsule = rt_body3d_new_capsule(0.2, 1.2, 1.8);
    void *sphere = rt_body3d_new_sphere(0.22, 3.0);
    rt_body3d_set_position(capsule, 0.0, 0.6, 0.0);
    rt_body3d_set_position(sphere, -0.35, 0.9, 0.0);
    rt_body3d_set_velocity(sphere, 5.0, 0.0, 0.0);
    rt_world3d_add(world, capsule);
    rt_world3d_add(world, sphere);

    rt_world3d_step(world, 1.0 / 60.0);

    {
        void *ang = rt_body3d_get_angular_velocity(capsule);
        double angular_speed =
            sqrt(rt_vec3_x(ang) * rt_vec3_x(ang) + rt_vec3_y(ang) * rt_vec3_y(ang) +
                 rt_vec3_z(ang) * rt_vec3_z(ang));
        EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                    "off-center contact: collision recorded");
        EXPECT_TRUE(angular_speed > 0.1,
                    "off-center contact: angular velocity generated from contact point");
    }
}

static void test_large_floor_contact_point_stays_near_dynamic_body() {
    void *world = rt_world3d_new(0, 0, 0);
    void *floor = rt_body3d_new_aabb(100.0, 0.05, 100.0, 0.0);
    void *sphere = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(floor, 0.0, -0.05, 0.0);
    rt_body3d_set_position(sphere, 10.0, 0.95, 20.0);
    rt_world3d_add(world, floor);
    rt_world3d_add(world, sphere);

    rt_world3d_step(world, 1.0 / 60.0);

    {
        void *event = rt_world3d_get_collision_event(world, 0);
        void *point = rt_collision_event3d_get_contact_point(event, 0);
        EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                    "floor contact point: collision recorded");
        EXPECT_NEAR(rt_vec3_x(point), 10.0, 0.05, "floor contact point: x remains near sphere");
        EXPECT_NEAR(rt_vec3_z(point), 20.0, 0.05, "floor contact point: z remains near sphere");
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

static void test_convex_hull_gjk_detects_contained_sphere() {
    void *world = rt_world3d_new(0, 0, 0);
    void *mesh = make_tetra_mesh(1);
    void *collider = rt_collider3d_new_convex_hull(mesh);
    void *hull_body = rt_body3d_new(0.0);
    void *sphere = rt_body3d_new_sphere(0.05, 1.0);
    rt_body3d_set_collider(hull_body, collider);
    rt_body3d_set_position(hull_body, 0.0, 0.0, 0.0);
    rt_body3d_set_position(sphere, 0.25, 0.25, 0.25);
    rt_world3d_add(world, hull_body);
    rt_world3d_add(world, sphere);

    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                "convex hull GJK detects a simple collider fully contained by the hull");
}

static void test_convex_hull_gjk_rejects_aabb_false_positive() {
    void *world = rt_world3d_new(0, 0, 0);
    void *mesh_a = make_tetra_mesh(1);
    void *mesh_b = make_tetra_mesh(-1);
    void *collider_a = rt_collider3d_new_convex_hull(mesh_a);
    void *collider_b = rt_collider3d_new_convex_hull(mesh_b);
    void *body_a = rt_body3d_new(0.0);
    void *body_b = rt_body3d_new(1.0);
    rt_body3d_set_collider(body_a, collider_a);
    rt_body3d_set_collider(body_b, collider_b);
    rt_body3d_set_position(body_a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(body_b, 0.8, 0.8, 0.8);
    rt_world3d_add(world, body_a);
    rt_world3d_add(world, body_b);

    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 0,
                "convex hull GJK rejects separated hulls with overlapping AABBs");
}

static void test_convex_hull_gjk_detects_overlapping_hulls() {
    void *world = rt_world3d_new(0, 0, 0);
    void *mesh_a = make_tetra_mesh(1);
    void *mesh_b = make_tetra_mesh(-1);
    void *collider_a = rt_collider3d_new_convex_hull(mesh_a);
    void *collider_b = rt_collider3d_new_convex_hull(mesh_b);
    void *body_a = rt_body3d_new(0.0);
    void *body_b = rt_body3d_new(1.0);
    rt_body3d_set_collider(body_a, collider_a);
    rt_body3d_set_collider(body_b, collider_b);
    rt_body3d_set_position(body_a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(body_b, 0.4, 0.4, 0.4);
    rt_world3d_add(world, body_a);
    rt_world3d_add(world, body_b);

    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                "convex hull GJK/EPA detects overlapping hulls");
    if (rt_world3d_get_collision_count(world) > 0) {
        void *normal = rt_world3d_get_collision_normal(world, 0);
        EXPECT_TRUE(normal != nullptr, "convex hull GJK/EPA reports a contact normal");
        EXPECT_TRUE(rt_world3d_get_collision_depth(world, 0) >= 0.0,
                    "convex hull GJK/EPA reports finite penetration depth");
    }
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

static void test_mesh_collider_narrowphase_builds_bvh_for_sphere_and_box() {
    {
        void *world = rt_world3d_new(0, 0, 0);
        void *mesh = rt_mesh3d_new_box(8.0, 1.0, 8.0);
        rt_mesh3d *mesh_impl = (rt_mesh3d *)mesh;
        void *collider = rt_collider3d_new_mesh(mesh);
        void *floor_body = rt_body3d_new(0.0);
        void *sphere = rt_body3d_new_sphere(0.75, 1.0);
        rt_body3d_set_collider(floor_body, collider);
        rt_body3d_set_position(floor_body, 0.0, -0.5, 0.0);
        rt_body3d_set_position(sphere, 0.0, 0.25, 0.0);
        rt_world3d_add(world, floor_body);
        rt_world3d_add(world, sphere);
        rt_world3d_step(world, 0.016);
        EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0,
                    "mesh narrow phase: sphere overlap reports contact");
        EXPECT_TRUE(mesh_impl->physics_bvh_node_count > 0,
                    "mesh narrow phase: sphere path builds physics BVH");
        EXPECT_TRUE(mesh_impl->physics_bvh_revision == mesh_impl->geometry_revision,
                    "mesh narrow phase: sphere path marks BVH revision current");
    }

    {
        void *world = rt_world3d_new(0, 0, 0);
        void *mesh = rt_mesh3d_new_box(8.0, 1.0, 8.0);
        rt_mesh3d *mesh_impl = (rt_mesh3d *)mesh;
        void *collider = rt_collider3d_new_mesh(mesh);
        void *floor_body = rt_body3d_new(0.0);
        void *box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
        rt_body3d_set_collider(floor_body, collider);
        rt_body3d_set_position(floor_body, 0.0, -0.5, 0.0);
        rt_body3d_set_position(box, 0.0, 0.25, 0.0);
        rt_world3d_add(world, floor_body);
        rt_world3d_add(world, box);
        rt_world3d_step(world, 0.016);
        EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0,
                    "mesh narrow phase: box overlap reports contact");
        EXPECT_TRUE(mesh_impl->physics_bvh_node_count > 0,
                    "mesh narrow phase: box path builds physics BVH");
        EXPECT_TRUE(mesh_impl->physics_bvh_revision == mesh_impl->geometry_revision,
                    "mesh narrow phase: box path marks BVH revision current");
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

static void test_compound_collider_rejects_transitive_cycle() {
    void *a = rt_collider3d_new_compound();
    void *b = rt_collider3d_new_compound();
    rt_collider3d_add_child(a, b, NULL);
    EXPECT_TRUE(rt_collider3d_get_child_count_raw(a) == 1,
                "Compound cycle test starts with one child");
    EXPECT_TRUE(expect_trap_contains([&] { rt_collider3d_add_child(b, a, NULL); }, "cycle"),
                "Compound collider rejects transitive cycles");
    EXPECT_TRUE(rt_collider3d_get_child_count_raw(b) == 0,
                "Rejected compound cycle does not mutate child list");
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

static void test_heightfield_box_samples_bottom_edges() {
    void *world = rt_world3d_new(0, 0, 0);
    void *pixels = rt_pixels_new(3, 3);
    for (int64_t z = 0; z < 3; ++z) {
        for (int64_t x = 0; x < 3; ++x)
            rt_pixels_set(pixels, x, z, encode_height16(0));
    }
    rt_pixels_set(pixels, 2, 1, encode_height16(65535));

    void *heightfield = rt_collider3d_new_heightfield(pixels, 1.0, 1.0, 1.0);
    void *terrain_body = rt_body3d_new(0.0);
    void *box = rt_body3d_new_aabb(1.0, 0.25, 1.0, 1.0);
    rt_body3d_set_collider(terrain_body, heightfield);
    rt_body3d_set_position(box, 0.0, 0.5, 0.0);
    rt_world3d_add(world, terrain_body);
    rt_world3d_add(world, box);
    rt_world3d_step(world, 0.016);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) > 0,
                "heightfield box contact detects edge ridge");
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

    void *event0 = rt_world3d_get_collision_event(world, 0);
    void *event1 = rt_world3d_get_collision_event(world, 0);
    EXPECT_TRUE(event0 != nullptr && event0 == event1,
                "collision event objects are cached per frame");
}

static void test_world_raycast_returns_nearest_hit() {
    void *world = rt_world3d_new(0, 0, 0);
    void *near_box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);
    void *far_box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);
    void *origin = rt_vec3_new(0.0, 0.0, 0.0);
    void *dir = rt_vec3_new(1.0, 0.0, 0.0);
    rt_body3d_set_position(near_box, 5.0, 0.0, 0.0);
    rt_body3d_set_position(far_box, 8.0, 0.0, 0.0);
    rt_world3d_add(world, near_box);
    rt_world3d_add(world, far_box);

    {
        void *hit = rt_world3d_raycast(world, origin, dir, 20.0, 1);
        EXPECT_TRUE(hit != nullptr, "raycast: hit returned");
        EXPECT_TRUE(rt_physics_hit3d_get_body(hit) == near_box, "raycast: nearest body returned");
        EXPECT_TRUE(rt_physics_hit3d_get_distance(hit) > 4.0 &&
                        rt_physics_hit3d_get_distance(hit) < 5.5,
                    "raycast: distance near expected surface");
        EXPECT_TRUE(rt_physics_hit3d_get_started_penetrating(hit) == 0,
                    "raycast: not starting in penetration");
    }
}

static void test_world_raycast_all_sorted() {
    void *world = rt_world3d_new(0, 0, 0);
    void *near_box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);
    void *far_box = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);
    void *origin = rt_vec3_new(0.0, 0.0, 0.0);
    void *dir = rt_vec3_new(1.0, 0.0, 0.0);
    rt_body3d_set_position(near_box, 4.0, 0.0, 0.0);
    rt_body3d_set_position(far_box, 7.0, 0.0, 0.0);
    rt_world3d_add(world, near_box);
    rt_world3d_add(world, far_box);

    {
        void *hits = rt_world3d_raycast_all(world, origin, dir, 20.0, 1);
        EXPECT_TRUE(hits != nullptr, "raycast all: list returned");
        EXPECT_TRUE(rt_physics_hit_list3d_get_count(hits) == 2, "raycast all: two hits");
        {
            void *hit0 = rt_physics_hit_list3d_get(hits, 0);
            void *hit1 = rt_physics_hit_list3d_get(hits, 1);
            EXPECT_TRUE(rt_physics_hit3d_get_body(hit0) == near_box, "raycast all: hit0 is near");
            EXPECT_TRUE(rt_physics_hit3d_get_body(hit1) == far_box, "raycast all: hit1 is far");
            EXPECT_TRUE(rt_physics_hit3d_get_distance(hit0) < rt_physics_hit3d_get_distance(hit1),
                        "raycast all: hits sorted by distance");
        }
    }
}

static void test_world_overlap_hit_list_reports_truncation() {
    void *world = rt_world3d_new(0, 0, 0);
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    for (int i = 0; i < 260; i++) {
        void *body = rt_body3d_new_sphere(0.1, 0.0);
        double x = ((double)(i % 26) - 13.0) * 0.25;
        double z = (double)(i / 26) * 0.25;
        rt_body3d_set_position(body, x, 0.0, z);
        EXPECT_TRUE(rt_world3d_try_add(world, body) != 0, "TryAdd succeeds while filling overlap set");
    }

    void *hits = rt_world3d_overlap_sphere(world, center, 100.0, 1);
    EXPECT_TRUE(hits != nullptr, "OverlapSphere returns a hit list");
    EXPECT_TRUE(rt_physics_hit_list3d_get_count(hits) == 256, "OverlapSphere stores bounded hits");
    EXPECT_TRUE(rt_physics_hit_list3d_get_total_count(hits) == 260,
                "OverlapSphere reports total matching hits");
    EXPECT_TRUE(rt_physics_hit_list3d_get_truncated(hits) != 0,
                "OverlapSphere marks truncated hit lists");
}

static void test_world_sweep_sphere_reports_started_penetrating() {
    void *world = rt_world3d_new(0, 0, 0);
    void *wall = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *center = rt_vec3_new(0.25, 0.0, 0.0);
    void *delta = rt_vec3_new(2.0, 0.0, 0.0);
    rt_body3d_set_position(wall, 0.0, 0.0, 0.0);
    rt_world3d_add(world, wall);

    {
        void *hit = rt_world3d_sweep_sphere(world, center, 0.5, delta, 1);
        EXPECT_TRUE(hit != nullptr, "sweep sphere: hit returned");
        EXPECT_TRUE(rt_physics_hit3d_get_started_penetrating(hit) != 0,
                    "sweep sphere: started penetrating reported");
        EXPECT_NEAR(rt_physics_hit3d_get_fraction(hit), 0.0, 1e-6, "sweep sphere: fraction zero");
    }
}

static void test_world_overlap_queries_honor_mask() {
    void *world = rt_world3d_new(0, 0, 0);
    void *body_a = rt_body3d_new_sphere(0.5, 0.0);
    void *body_b = rt_body3d_new_sphere(0.5, 0.0);
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    void *minv = rt_vec3_new(-1.0, -1.0, -1.0);
    void *maxv = rt_vec3_new(1.0, 1.0, 1.0);
    rt_body3d_set_position(body_a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(body_b, 0.0, 0.0, 0.0);
    rt_body3d_set_collision_layer(body_a, 1);
    rt_body3d_set_collision_layer(body_b, 2);
    rt_world3d_add(world, body_a);
    rt_world3d_add(world, body_b);

    {
        void *hits1 = rt_world3d_overlap_sphere(world, center, 1.0, 1);
        void *hits2 = rt_world3d_overlap_aabb(world, minv, maxv, 2);
        EXPECT_TRUE(hits1 != nullptr, "overlap sphere: list returned");
        EXPECT_TRUE(rt_physics_hit_list3d_get_count(hits1) == 1, "overlap sphere: one masked hit");
        EXPECT_TRUE(rt_physics_hit3d_get_body(rt_physics_hit_list3d_get(hits1, 0)) == body_a,
                    "overlap sphere: layer-1 body returned");
        EXPECT_TRUE(hits2 != nullptr, "overlap aabb: list returned");
        EXPECT_TRUE(rt_physics_hit_list3d_get_count(hits2) == 1, "overlap aabb: one masked hit");
        EXPECT_TRUE(rt_physics_hit3d_get_body(rt_physics_hit_list3d_get(hits2, 0)) == body_b,
                    "overlap aabb: layer-2 body returned");
    }
}

static void test_world_overlap_queries_reject_nonfinite_inputs() {
    void *world = rt_world3d_new(0, 0, 0);
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    void *bad_center = rt_vec3_new(NAN, 0.0, 0.0);
    void *minv = rt_vec3_new(-1.0, -1.0, -1.0);
    void *bad_maxv = rt_vec3_new(INFINITY, 1.0, 1.0);

    EXPECT_TRUE(rt_world3d_overlap_sphere(world, center, NAN, 1) == nullptr,
                "OverlapSphere rejects non-finite radius");
    EXPECT_TRUE(rt_world3d_overlap_sphere(world, bad_center, 1.0, 1) == nullptr,
                "OverlapSphere rejects non-finite center");
    EXPECT_TRUE(rt_world3d_overlap_aabb(world, minv, bad_maxv, 1) == nullptr,
                "OverlapAabb rejects non-finite bounds");
}

static void test_world_query_broadphase_cache_invalidates_after_body_move() {
    void *world = rt_world3d_new(0, 0, 0);
    void *body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    rt_body3d_set_position(body, 0.0, 0.0, 0.0);
    rt_world3d_add(world, body);

    void *hits = rt_world3d_overlap_sphere(world, center, 0.25, -1);
    EXPECT_TRUE(hits != nullptr && rt_physics_hit_list3d_get_count(hits) == 1,
                "query broadphase initially sees the body");

    rt_body3d_set_position(body, 10.0, 0.0, 0.0);
    hits = rt_world3d_overlap_sphere(world, center, 0.25, -1);
    EXPECT_TRUE(hits == nullptr || rt_physics_hit_list3d_get_count(hits) == 0,
                "query broadphase cache invalidates after body movement");
}

static void test_world_rebase_origin_shifts_body_contact_and_query_state() {
    void *world = rt_world3d_new(0, 0, 0);
    void *query_body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *a = rt_body3d_new_sphere(1.0, 1.0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    rt_body3d_set_position(query_body, 1000.0, 50.0, 0.0);
    rt_body3d_set_position(a, 1004.0, 0.0, 0.0);
    rt_body3d_set_position(b, 1005.5, 0.0, 0.0);
    rt_world3d_add(world, query_body);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);

    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_collision_event_count(world) >= 1,
                "RebaseOrigin setup records an overlapping contact");
    void *event = rt_world3d_get_collision_event(world, 0);
    void *point = rt_collision_event3d_get_contact_point(event, 0);
    double before_x = rt_vec3_x(point);

    void *far_center = rt_vec3_new(1000.0, 50.0, 0.0);
    void *hits = rt_world3d_overlap_sphere(world, far_center, 0.25, -1);
    EXPECT_TRUE(hits != nullptr && rt_physics_hit_list3d_get_count(hits) == 1,
                "RebaseOrigin setup warms the query broadphase at the far origin");

    rt_world3d_rebase_origin(world, 1000.0, 0.0, 0.0);

    void *query_pos = rt_body3d_get_position(query_body);
    EXPECT_NEAR(rt_vec3_x(query_pos), 0.0, 0.000001, "RebaseOrigin shifts body X");
    EXPECT_NEAR(rt_vec3_y(query_pos), 50.0, 0.000001, "RebaseOrigin preserves body Y");

    void *near_center = rt_vec3_new(0.0, 50.0, 0.0);
    hits = rt_world3d_overlap_sphere(world, near_center, 0.25, -1);
    EXPECT_TRUE(hits != nullptr && rt_physics_hit_list3d_get_count(hits) == 1,
                "RebaseOrigin invalidates query broadphase caches");

    event = rt_world3d_get_collision_event(world, 0);
    point = rt_collision_event3d_get_contact_point(event, 0);
    EXPECT_NEAR(rt_vec3_x(point), before_x - 1000.0, 0.000001,
                "RebaseOrigin shifts cached contact points");
}

static void test_collision_events_enter_stay_exit() {
    void *world = rt_world3d_new(0, 0, 0);
    void *floor = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *box = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_position(floor, 0.0, 0.0, 0.0);
    rt_body3d_set_position(box, 0.5, 0.0, 0.0);
    rt_world3d_add(world, floor);
    rt_world3d_add(world, box);

    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 1,
                "collision events: first step enters");
    EXPECT_TRUE(rt_world3d_get_stay_event_count(world) == 0,
                "collision events: no stay on first step");
    EXPECT_TRUE(rt_world3d_get_exit_event_count(world) == 0,
                "collision events: no exit on first step");

    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 0, "collision events: no re-enter");
    EXPECT_TRUE(rt_world3d_get_stay_event_count(world) == 1,
                "collision events: stay on second step");

    rt_body3d_set_position(box, 5.0, 0.0, 0.0);
    rt_body3d_set_velocity(box, 0.0, 0.0, 0.0);
    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_exit_event_count(world) == 1,
                "collision events: exit when separated");
}

static void test_query_mask_zero_matches_no_layers() {
    void *world = rt_world3d_new(0, 0, 0);
    void *body = rt_body3d_new_sphere(0.5, 0.0);
    void *origin = rt_vec3_new(-2.0, 0.0, 0.0);
    void *dir = rt_vec3_new(1.0, 0.0, 0.0);
    void *center = rt_vec3_new(0.0, 0.0, 0.0);
    void *delta = rt_vec3_new(2.0, 0.0, 0.0);
    void *minv = rt_vec3_new(-1.0, -1.0, -1.0);
    void *maxv = rt_vec3_new(1.0, 1.0, 1.0);
    rt_body3d_set_position(body, 0.0, 0.0, 0.0);
    rt_world3d_add(world, body);

    EXPECT_TRUE(rt_world3d_raycast(world, origin, dir, 10.0, 0) == nullptr,
                "LayerMask.None raycast returns no hit");
    EXPECT_TRUE(rt_world3d_raycast_all(world, origin, dir, 10.0, 0) == nullptr,
                "LayerMask.None raycast all returns no list");
    EXPECT_TRUE(rt_world3d_overlap_sphere(world, center, 1.0, 0) == nullptr,
                "LayerMask.None overlap sphere returns no list");
    EXPECT_TRUE(rt_world3d_overlap_aabb(world, minv, maxv, 0) == nullptr,
                "LayerMask.None overlap aabb returns no list");
    EXPECT_TRUE(rt_world3d_sweep_sphere(world, origin, 0.25, delta, 0) == nullptr,
                "LayerMask.None sphere sweep returns no hit");
}

static void test_kinematic_static_trigger_contacts_are_reported() {
    void *world = rt_world3d_new(0, 0, 0);
    void *trigger = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_trigger(trigger, 1);
    rt_body3d_set_kinematic(body, 1);
    rt_body3d_set_position(trigger, 0.0, 0.0, 0.0);
    rt_body3d_set_position(body, 0.5, 0.0, 0.0);
    rt_world3d_add(world, trigger);
    rt_world3d_add(world, body);

    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                "kinematic-static trigger contact is reported");
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 1,
                "kinematic-static trigger contact emits enter");
}

static void test_contact_identity_survives_broadphase_order_flip() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    void *b = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_trigger(a, 1);
    rt_body3d_set_trigger(b, 1);
    rt_body3d_set_kinematic(a, 1);
    rt_body3d_set_kinematic(b, 1);
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 0.5, 0.0, 0.0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);

    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 1,
                "order flip setup enters on first frame");

    rt_body3d_set_position(a, 1.0, 0.0, 0.0);
    rt_body3d_set_position(b, 0.5, 0.0, 0.0);
    rt_world3d_step(world, 1.0 / 60.0);
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 0,
                "order-independent contact identity prevents false re-enter");
    EXPECT_TRUE(rt_world3d_get_stay_event_count(world) == 1,
                "order-independent contact identity emits stay after sort reversal");
    EXPECT_TRUE(rt_world3d_get_exit_event_count(world) == 0,
                "order-independent contact identity prevents false exit");
}

static void test_ccd_substep_contact_generates_frame_event() {
    void *world = rt_world3d_new(0, 0, 0);
    void *trigger = rt_body3d_new_aabb(0.05, 1.0, 1.0, 0.0);
    void *sphere = rt_body3d_new_sphere(0.1, 1.0);
    rt_body3d_set_trigger(trigger, 1);
    rt_body3d_set_position(trigger, 0.0, 0.0, 0.0);
    rt_body3d_set_position(sphere, -2.0, 0.0, 0.0);
    rt_body3d_set_velocity(sphere, 40.0, 0.0, 0.0);
    rt_body3d_set_use_ccd(sphere, 1);
    rt_world3d_add(world, trigger);
    rt_world3d_add(world, sphere);

    rt_world3d_step(world, 0.1);
    EXPECT_TRUE(rt_world3d_get_collision_count(world) == 1,
                "CCD substep-only contact is retained for the frame");
    EXPECT_TRUE(rt_world3d_get_enter_event_count(world) == 1,
                "CCD substep-only contact emits enter");
}

static void test_collision_event_surface_and_trigger_flag() {
    void *world = rt_world3d_new(0, 0, 0);
    void *trigger = rt_body3d_new_aabb(1.0, 1.0, 1.0, 0.0);
    void *body = rt_body3d_new_aabb(1.0, 1.0, 1.0, 1.0);
    rt_body3d_set_trigger(trigger, 1);
    rt_body3d_set_position(trigger, 0.0, 0.0, 0.0);
    rt_body3d_set_position(body, 0.5, 0.0, 0.0);
    rt_world3d_add(world, trigger);
    rt_world3d_add(world, body);
    rt_world3d_step(world, 1.0 / 60.0);

    {
        void *event = rt_world3d_get_collision_event(world, 0);
        EXPECT_TRUE(event != nullptr, "collision event: event object returned");
        EXPECT_TRUE(rt_collision_event3d_get_is_trigger(event) != 0,
                    "collision event: trigger flag propagated");
        EXPECT_TRUE(rt_collision_event3d_get_contact_count(event) > 1,
                    "collision event: AABB pair exposes manifold points");
        EXPECT_NEAR(rt_collision_event3d_get_normal_impulse(event),
                    0.0,
                    1e-9,
                    "collision event: trigger has zero impulse");
        {
            void *contact = rt_collision_event3d_get_contact(event, 0);
            void *contact1 = rt_collision_event3d_get_contact(event, 1);
            EXPECT_TRUE(contact != nullptr, "collision event: contact object returned");
            EXPECT_TRUE(contact1 != nullptr, "collision event: second manifold contact returned");
            EXPECT_TRUE(rt_contact_point3d_get_point(contact) != nullptr,
                        "collision event: contact point object returned");
            EXPECT_TRUE(rt_collision_event3d_get_contact_separation(event, 0) < 0.0,
                        "collision event: separation negative while penetrating");
        }
    }
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
    EXPECT_TRUE(rt_trigger3d_contains(t, t) == 0, "Trigger3D.Contains rejects non-Vec3 handles");
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

static void test_joints_retain_bodies_and_sanitize_parameters() {
    void *a = rt_body3d_new_sphere(1.0, 1.0);
    void *b = rt_body3d_new_sphere(1.0, 1.0);
    void *distance = rt_distance_joint3d_new(a, b, INFINITY);
    int a_reached_zero = rt_obj_release_check0(a);
    int b_reached_zero = rt_obj_release_check0(b);

    EXPECT_TRUE(a_reached_zero == 0, "DistanceJoint3D retains body A");
    EXPECT_TRUE(b_reached_zero == 0, "DistanceJoint3D retains body B");
    EXPECT_NEAR(rt_distance_joint3d_get_distance(distance),
                0.0,
                0.01,
                "DistanceJoint3D converts infinite distances to zero");

    void *c = rt_body3d_new_sphere(1.0, 1.0);
    void *d = rt_body3d_new_sphere(1.0, 1.0);
    void *spring = rt_spring_joint3d_new(c, d, NAN, INFINITY, -1.0);
    int c_reached_zero = rt_obj_release_check0(c);
    int d_reached_zero = rt_obj_release_check0(d);
    EXPECT_TRUE(c_reached_zero == 0, "SpringJoint3D retains body A");
    EXPECT_TRUE(d_reached_zero == 0, "SpringJoint3D retains body B");
    EXPECT_NEAR(rt_spring_joint3d_get_rest_length(spring),
                0.0,
                0.01,
                "SpringJoint3D converts NaN rest length to zero");
    EXPECT_NEAR(rt_spring_joint3d_get_stiffness(spring),
                0.0,
                0.01,
                "SpringJoint3D converts infinite stiffness to zero");
    EXPECT_NEAR(rt_spring_joint3d_get_damping(spring),
                0.0,
                0.01,
                "SpringJoint3D converts negative damping to zero");
    rt_spring_joint3d_set_stiffness(spring, 1.0e100);
    rt_spring_joint3d_set_damping(spring, 1.0e100);
    EXPECT_NEAR(
        rt_spring_joint3d_get_stiffness(spring), 1.0e9, 1.0, "SpringJoint3D clamps huge stiffness");
    EXPECT_NEAR(
        rt_spring_joint3d_get_damping(spring), 1.0e9, 1.0, "SpringJoint3D clamps huge damping");

    void *e = rt_body3d_new_sphere(1.0, 1.0);
    void *f = rt_body3d_new_sphere(1.0, 1.0);
    void *rope = rt_rope_joint3d_new(e, f, INFINITY);
    int e_reached_zero = rt_obj_release_check0(e);
    int f_reached_zero = rt_obj_release_check0(f);
    EXPECT_TRUE(e_reached_zero == 0, "RopeJoint3D retains body A");
    EXPECT_TRUE(f_reached_zero == 0, "RopeJoint3D retains body B");
    EXPECT_NEAR(rt_rope_joint3d_get_max_length(rope),
                0.0,
                0.01,
                "RopeJoint3D converts infinite max length to zero");
    rt_rope_joint3d_set_max_length(rope, 1.0e100);
    EXPECT_NEAR(rt_rope_joint3d_get_max_length(rope),
                1.0e9,
                1.0,
                "RopeJoint3D clamps huge max length");
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

static void test_hinge_joint_motor_drives_rotation() {
    void *world = rt_world3d_new(0, 0, 0); /* no gravity */
    void *base = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);  /* static anchor */
    void *wheel = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0); /* dynamic */
    rt_body3d_set_position(base, 0, 0, 0);
    rt_body3d_set_position(wheel, 0, 0, 0);
    /* Keep them off each other's collision masks so only the hinge acts. */
    rt_body3d_set_collision_layer(base, 1);
    rt_body3d_set_collision_mask(base, 1);
    rt_body3d_set_collision_layer(wheel, 2);
    rt_body3d_set_collision_mask(wheel, 2);
    rt_world3d_add(world, base);
    rt_world3d_add(world, wheel);

    void *hinge = rt_hinge_joint3d_new(base, wheel, rt_vec3_new(0, 0, 0), rt_vec3_new(0, 1, 0));
    rt_hinge_joint3d_set_motor(hinge, 1, 2.0, 10.0); /* drive +Y at 2 rad/s */
    rt_world3d_add_joint(world, hinge, RT_JOINT_HINGE);

    for (int i = 0; i < 10; i++)
        rt_world3d_step(world, 1.0 / 60.0);
    void *av = rt_body3d_get_angular_velocity(wheel);
    EXPECT_NEAR(rt_vec3_y(av), 2.0, 0.1, "Hinge motor drives the wheel to target angular velocity");

    /* Re-target the motor to zero — it should brake the wheel to a stop. */
    rt_hinge_joint3d_set_motor(hinge, 1, 0.0, 10.0);
    for (int i = 0; i < 10; i++)
        rt_world3d_step(world, 1.0 / 60.0);
    av = rt_body3d_get_angular_velocity(wheel);
    EXPECT_NEAR(rt_vec3_y(av), 0.0, 0.1, "Hinge motor brakes the wheel toward zero");
}

static void test_hinge_joint_get_angle() {
    void *base = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);
    void *arm = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    void *hinge = rt_hinge_joint3d_new(base, arm, rt_vec3_new(0, 0, 0), rt_vec3_new(0, 1, 0));
    EXPECT_NEAR(rt_hinge_joint3d_get_angle(hinge), 0.0, 0.01, "Fresh hinge reads a zero angle");
    /* Rotate the arm 0.5 rad about the hinge axis; the measured angle follows. */
    rt_body3d_set_orientation(arm, rt_quat_from_axis_angle(rt_vec3_new(0, 1, 0), 0.5));
    EXPECT_NEAR(fabs(rt_hinge_joint3d_get_angle(hinge)), 0.5, 0.02,
                "Hinge angle tracks rotation about the axis");
}

static void test_hinge_joint_angle_limit() {
    void *world = rt_world3d_new(0, 0, 0);
    void *base = rt_body3d_new_aabb(0.5, 0.5, 0.5, 0.0);
    void *arm = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
    rt_body3d_set_position(base, 0, 0, 0);
    rt_body3d_set_position(arm, 0, 0, 0);
    rt_body3d_set_collision_layer(base, 1);
    rt_body3d_set_collision_mask(base, 1);
    rt_body3d_set_collision_layer(arm, 2);
    rt_body3d_set_collision_mask(arm, 2);
    rt_world3d_add(world, base);
    rt_world3d_add(world, arm);

    void *hinge = rt_hinge_joint3d_new(base, arm, rt_vec3_new(0, 0, 0), rt_vec3_new(0, 1, 0));
    rt_hinge_joint3d_set_motor(hinge, 1, 2.0, 10.0); /* drive toward +angle */
    rt_hinge_joint3d_set_limits(hinge, -0.5, 0.5);   /* but stop at 0.5 rad */
    rt_world3d_add_joint(world, hinge, RT_JOINT_HINGE);

    for (int i = 0; i < 120; i++)
        rt_world3d_step(world, 1.0 / 60.0);

    double angle = rt_hinge_joint3d_get_angle(hinge);
    /* The motor would spin freely, but the upper limit stops it near +0.5 rad. */
    EXPECT_TRUE(angle <= 0.55, "Hinge angle limit stops rotation at the upper bound");
    EXPECT_TRUE(angle >= 0.45, "Hinge reaches the upper limit");
    void *av = rt_body3d_get_angular_velocity(arm);
    EXPECT_TRUE(fabs(rt_vec3_y(av)) < 0.2, "Hinge is held still at the limit");
}

static void test_hinge_joint_anchor_constraint() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.25, 0.0);
    void *b = rt_body3d_new_sphere(0.25, 1.0);
    void *anchor = rt_vec3_new(0.0, 0.0, 0.0);
    void *axis = rt_vec3_new(0.0, 1.0, 0.0);
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 2.0, 0.0, 0.0);
    rt_body3d_set_velocity(b, 0.0, 10.0, 0.0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, rt_hinge_joint3d_new(a, b, anchor, axis), RT_JOINT_HINGE);

    rt_world3d_step(world, 1.0 / 60.0);

    void *pos_b = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos_b), 2.0, 0.05, "HingeJoint3D keeps authored anchor length");
    EXPECT_NEAR(rt_vec3_y(pos_b), 0.0, 0.05, "HingeJoint3D corrects anchor drift");
}

static void test_rope_joint_max_length_constraint() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.25, 0.0);
    void *b = rt_body3d_new_sphere(0.25, 1.0);
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 5.0, 0.0, 0.0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, rt_rope_joint3d_new(a, b, 2.0), RT_JOINT_ROPE);

    rt_world3d_step(world, 1.0 / 60.0);

    void *pos_b = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos_b), 2.0, 0.05, "RopeJoint3D enforces MaxLength");

    void *world2 = rt_world3d_new(0, 0, 0);
    void *c = rt_body3d_new_sphere(0.25, 0.0);
    void *d = rt_body3d_new_sphere(0.25, 1.0);
    rt_body3d_set_position(c, 0.0, 0.0, 0.0);
    rt_body3d_set_position(d, 1.0, 0.0, 0.0);
    rt_world3d_add(world2, c);
    rt_world3d_add(world2, d);
    rt_world3d_add_joint(world2, rt_rope_joint3d_new(c, d, 2.0), RT_JOINT_ROPE);
    rt_world3d_step(world2, 1.0 / 60.0);

    void *pos_d = rt_body3d_get_position(d);
    EXPECT_NEAR(rt_vec3_x(pos_d), 1.0, 0.05, "RopeJoint3D does not pull inside MaxLength");
}

static void test_sixdof_joint_linear_motor() {
    void *world = rt_world3d_new(0, 0, 0);
    void *base = rt_body3d_new_sphere(0.25, 0.0);   /* static anchor */
    void *slider = rt_body3d_new_sphere(0.25, 1.0); /* dynamic */
    /* Off each other's collision masks so only the joint acts. */
    rt_body3d_set_collision_layer(base, 1);
    rt_body3d_set_collision_mask(base, 1);
    rt_body3d_set_collision_layer(slider, 2);
    rt_body3d_set_collision_mask(slider, 2);
    void *joint = rt_sixdof_joint3d_new(base, slider, rt_mat4_identity(), rt_mat4_identity());
    /* X free (slider rail), Y/Z locked. */
    rt_sixdof_joint3d_set_linear_limits(
        joint, rt_vec3_new(-10.0, 0.0, 0.0), rt_vec3_new(10.0, 0.0, 0.0));
    rt_sixdof_joint3d_set_linear_motor(joint, 1, rt_vec3_new(2.0, 0.0, 0.0), 10.0);
    rt_body3d_set_position(base, 0.0, 0.0, 0.0);
    rt_body3d_set_position(slider, 0.0, 0.0, 0.0);
    rt_world3d_add(world, base);
    rt_world3d_add(world, slider);
    rt_world3d_add_joint(world, joint, RT_JOINT_SIXDOF);

    for (int i = 0; i < 5; i++)
        rt_world3d_step(world, 1.0 / 60.0);

    void *vel = rt_body3d_get_velocity(slider);
    EXPECT_NEAR(rt_vec3_x(vel), 2.0, 0.1, "SixDof linear motor drives the slider along the free axis");
    EXPECT_TRUE(fabs(rt_vec3_y(vel)) < 0.1, "SixDof keeps the locked axis still");
}

static void test_sixdof_joint_frame_anchor_constraint() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.25, 0.0);
    void *b = rt_body3d_new_sphere(0.25, 1.0);
    void *frame_a = rt_mat4_identity();
    void *frame_b = rt_mat4_identity();
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 3.0, 0.0, 0.0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, rt_sixdof_joint3d_new(a, b, frame_a, frame_b), RT_JOINT_SIXDOF);

    rt_world3d_step(world, 1.0 / 60.0);

    void *pos_b = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos_b), 0.0, 0.05, "SixDofJoint3D locks frame anchors");
}

static void test_sixdof_joint_limits() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.25, 0.0);
    void *b = rt_body3d_new_sphere(0.25, 1.0);
    void *joint = rt_sixdof_joint3d_new(a, b, rt_mat4_identity(), rt_mat4_identity());
    rt_sixdof_joint3d_set_linear_limits(
        joint, rt_vec3_new(-1.0, 0.0, 0.0), rt_vec3_new(1.0, 0.0, 0.0));
    rt_body3d_set_position(a, 0.0, 0.0, 0.0);
    rt_body3d_set_position(b, 3.0, 0.0, 0.0);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, joint, RT_JOINT_SIXDOF);
    rt_world3d_step(world, 1.0 / 60.0);

    void *pos_b = rt_body3d_get_position(b);
    EXPECT_NEAR(rt_vec3_x(pos_b), 1.0, 0.05, "SixDofJoint3D SetLinearLimits clamps separation");

    void *world2 = rt_world3d_new(0, 0, 0);
    void *c = rt_body3d_new_sphere(0.25, 0.0);
    void *d = rt_body3d_new_sphere(0.25, 1.0);
    void *angular_joint = rt_sixdof_joint3d_new(c, d, rt_mat4_identity(), rt_mat4_identity());
    rt_sixdof_joint3d_set_linear_limits(
        angular_joint, rt_vec3_new(-10.0, -10.0, -10.0), rt_vec3_new(10.0, 10.0, 10.0));
    rt_sixdof_joint3d_set_angular_limits(
        angular_joint, rt_vec3_new(-1.0, -1.0, -1.0), rt_vec3_new(1.0, 1.0, 1.0));
    rt_body3d_set_position(c, 0.0, 0.0, 0.0);
    rt_body3d_set_position(d, 3.0, 0.0, 0.0);
    rt_body3d_set_angular_velocity(d, 5.0, 0.0, 0.0);
    rt_world3d_add(world2, c);
    rt_world3d_add(world2, d);
    rt_world3d_add_joint(world2, angular_joint, RT_JOINT_SIXDOF);
    rt_world3d_step(world2, 1.0 / 60.0);

    void *ang_d = rt_body3d_get_angular_velocity(d);
    EXPECT_TRUE(fabs(rt_vec3_x(ang_d)) <= 1.05,
                "SixDofJoint3D SetAngularLimits clamps relative angular velocity");

    EXPECT_TRUE(expect_trap_contains(
                    [&]() { rt_sixdof_joint3d_set_linear_limits(angular_joint, nullptr, nullptr); },
                    "min and max must be finite Vec3 values"),
                "SixDofJoint3D.SetLinearLimits rejects invalid Vec3 handles");
}

static void test_joint_type_validation_for_new_joint_classes() {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.25, 1.0);
    void *b = rt_body3d_new_sphere(0.25, 1.0);
    void *anchor = rt_vec3_new(0.0, 0.0, 0.0);
    void *axis = rt_vec3_new(0.0, 1.0, 0.0);
    void *hinge = rt_hinge_joint3d_new(a, b, anchor, axis);
    EXPECT_TRUE(expect_trap_contains(
                    [&]() { rt_world3d_add_joint(world, hinge, RT_JOINT_ROPE); },
                    "joint object does not match joint type"),
                "World.AddJoint rejects mismatched new joint type tags");
    EXPECT_TRUE(expect_trap_contains(
                    [&]() {
                        rt_hinge_joint3d_new(a, b, anchor, rt_vec3_new(0.0, 0.0, 0.0));
                    },
                    "axis must be a non-zero finite Vec3"),
                "HingeJoint3D rejects zero axes");
}

static double spring_velocity_after_solver_iterations(int64_t iterations) {
    void *world = rt_world3d_new(0, 0, 0);
    void *a = rt_body3d_new_sphere(0.5, 1.0);
    void *b = rt_body3d_new_sphere(0.5, 1.0);
    rt_body3d_set_position(a, 0, 0, 0);
    rt_body3d_set_position(b, 10, 0, 0);
    rt_world3d_set_solver_iterations(world, iterations);
    rt_world3d_add(world, a);
    rt_world3d_add(world, b);
    rt_world3d_add_joint(world, rt_spring_joint3d_new(a, b, 3.0, 10.0, 0.0), RT_JOINT_SPRING);
    rt_world3d_step(world, 1.0 / 60.0);
    void *velocity = rt_body3d_get_velocity(b);
    return fabs(rt_vec3_x(velocity));
}

/* Top-box height of a 3-box stack after a short awake settling window. In a
 * sequential-impulse solver support propagates up a stack one contact per
 * Gauss-Seidel sweep, so more iterations let the stack carry its own weight with
 * less compression: the top box settles higher. (The warm-started solver
 * decouples positional correction from iteration count, so this measures the
 * iterative support of a coupled stack, not the old per-iteration Baumgarte
 * push.) */
static double stack_top_height_after_solver_iterations(int64_t iterations) {
    void *world = rt_world3d_new(0.0, -9.8, 0.0);
    rt_world3d_set_solver_iterations(world, iterations);
    void *floor = rt_body3d_new_aabb(20.0, 0.5, 20.0, 0.0);
    rt_body3d_set_position(floor, 0.0, -0.5, 0.0);
    rt_world3d_add(world, floor);
    void *boxes[3];
    for (int i = 0; i < 3; ++i) {
        boxes[i] = rt_body3d_new_aabb(0.5, 0.5, 0.5, 1.0);
        rt_body3d_set_restitution(boxes[i], 0.0);
        rt_body3d_set_position(boxes[i], 0.0, 0.5 + (double)i, 0.0);
        rt_world3d_add(world, boxes[i]);
    }
    for (int step = 0; step < 8; ++step) /* awake settling window (pre-sleep) */
        rt_world3d_step(world, 1.0 / 60.0);
    return rt_vec3_y(rt_body3d_get_position(boxes[2])); /* top box */
}

static void test_world_solver_iteration_controls() {
    void *world = rt_world3d_new(0, 0, 0);
    EXPECT_TRUE(rt_world3d_get_solver_iterations(world) == 6,
                "World: SolverIterations defaults to historical six passes");
    rt_world3d_set_solver_iterations(world, 0);
    EXPECT_TRUE(rt_world3d_get_solver_iterations(world) == 1,
                "World: SetSolverIterations clamps low values to one");
    rt_world3d_set_solver_iterations(world, 999);
    EXPECT_TRUE(rt_world3d_get_solver_iterations(world) == 64,
                "World: SetSolverIterations clamps high values to max");
    rt_world3d_set_solver_iterations(world, 4);
    EXPECT_TRUE(rt_world3d_get_solver_iterations(world) == 4,
                "World: SetSolverIterations stores valid values");

    double one_iter_velocity = spring_velocity_after_solver_iterations(1);
    double four_iter_velocity = spring_velocity_after_solver_iterations(4);
    EXPECT_TRUE(four_iter_velocity > one_iter_velocity * 2.0,
                "World: SolverIterations changes iterative spring solving");

    double one_iter_top = stack_top_height_after_solver_iterations(1);
    double eight_iter_top = stack_top_height_after_solver_iterations(8);
    EXPECT_TRUE(eight_iter_top > one_iter_top + 0.001,
                "World: more SolverIterations support a stack with less compression");
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
    test_collider_constructors_sanitize_nonfinite_dimensions();
    test_mesh_collider_attaches_to_static_body();

    /* Property accessors */
    test_body_position();
    test_body_scale_affects_queries();
    test_body_orientation_roundtrip();
    test_body_velocity();
    test_body_far_origin_integrates_sub_float_delta();
    test_body_collision_layer_mask();
    test_body_material_coefficients_are_sanitized();
    test_body_sanitizes_nonfinite_motion_state();
    test_body_extreme_motion_state_saturates();
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
    test_world_remove_purges_contacts_for_body();
    test_world_rejects_duplicate_body_adds();
    test_world_body_storage_grows_past_initial_capacity();
    test_world_sparse_body_count_step_stress();
    test_world_contact_storage_grows_past_initial_capacity();
    test_world_broadphase_rejects_separated_bodies();
    test_gravity_integration();
    test_force_application();
    test_impulse_application();
    test_impulse_at_point_adds_spin();
    test_force_at_point_adds_spin();

    /* Collision */
    test_collision_aabb_overlap();
    test_collision_layer_filtering();
    test_trigger_no_push();
    test_ground_detection();
    test_box_stack_rests_stably();

    /* Character controller */
    test_character_create();
    test_character_position();
    test_character_step_height();
    test_character_sanitizes_motion_config();
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
    test_rotated_capsule_aabb_collision();
    test_offcenter_contact_generates_angular_velocity();
    test_large_floor_contact_point_stays_near_dynamic_body();
    test_convex_hull_collider_blocks_sphere();
    test_convex_hull_gjk_detects_contained_sphere();
    test_convex_hull_gjk_rejects_aabb_false_positive();
    test_convex_hull_gjk_detects_overlapping_hulls();
    test_mesh_collider_blocks_falling_sphere();
    test_mesh_collider_narrowphase_builds_bvh_for_sphere_and_box();
    test_compound_collider_child_transform_affects_contact();
    test_compound_collider_rejects_transitive_cycle();
    test_heightfield_collider_supports_ground_contact();
    test_heightfield_box_samples_bottom_edges();

    /* Collision event queue */
    test_collision_event_count();
    test_collision_event_bodies();
    test_world_raycast_returns_nearest_hit();
    test_world_raycast_all_sorted();
    test_world_overlap_hit_list_reports_truncation();
    test_world_sweep_sphere_reports_started_penetrating();
    test_world_overlap_queries_honor_mask();
    test_world_overlap_queries_reject_nonfinite_inputs();
    test_world_query_broadphase_cache_invalidates_after_body_move();
    test_world_rebase_origin_shifts_body_contact_and_query_state();
    test_collision_events_enter_stay_exit();
    test_query_mask_zero_matches_no_layers();
    test_kinematic_static_trigger_contacts_are_reported();
    test_contact_identity_survives_broadphase_order_flip();
    test_ccd_substep_contact_generates_frame_event();
    test_collision_event_surface_and_trigger_flag();

    /* Joint tests */
    test_distance_joint_create();
    test_joints_retain_bodies_and_sanitize_parameters();
    test_distance_joint_constraint();
    test_spring_joint_create();
    test_spring_joint_force();
    test_hinge_joint_anchor_constraint();
    test_hinge_joint_motor_drives_rotation();
    test_hinge_joint_get_angle();
    test_hinge_joint_angle_limit();
    test_rope_joint_max_length_constraint();
    test_sixdof_joint_frame_anchor_constraint();
    test_sixdof_joint_limits();
    test_sixdof_joint_linear_motor();
    test_joint_type_validation_for_new_joint_classes();
    test_world_solver_iteration_controls();
    test_world_joint_management();

    printf("Physics3D tests: %d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
