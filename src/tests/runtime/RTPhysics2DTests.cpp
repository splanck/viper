//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// RTPhysics2DTests.cpp - Tests for rt_physics2d (2D physics engine)
//===----------------------------------------------------------------------===//

#include <cmath>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

extern "C" {
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_physics2d.h"
#include "rt_physics2d_internal.h"
#include "rt_physics2d_joint.h"
}

//=============================================================================
// Trap infrastructure (same pattern as RTBinaryBufferTests)
//=============================================================================

namespace {
static jmp_buf g_trap_jmp;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            (void)(expr);                                                                          \
            fprintf(stderr, "FAIL [%s:%d]: Expected trap did not fire\n", __FILE__, __LINE__);     \
            tests_run++;                                                                           \
        } else {                                                                                   \
            tests_run++;                                                                           \
            tests_passed++;                                                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static int tests_run = 0;
static int tests_passed = 0;
static int g_body_finalizer_calls = 0;

static const double EPSILON = 1e-6;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define ASSERT_NEAR(a, b, msg) ASSERT(fabs((a) - (b)) < EPSILON, msg)

static void release_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

extern "C" void test_body_finalizer(void *) {
    g_body_finalizer_calls++;
}

//=============================================================================
// World tests
//=============================================================================

static void test_world_new() {
    void *world = rt_physics2d_world_new(0.0, 9.8);
    ASSERT(world != NULL, "world_new returns non-null");
    ASSERT(rt_physics2d_world_body_count(world) == 0, "new world has 0 bodies");
    rt_obj_release_check0(world);
}

static void test_world_add_remove() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);

    rt_physics2d_world_add(world, body);
    ASSERT(rt_physics2d_world_body_count(world) == 1, "1 body after add");

    rt_physics2d_world_remove(world, body);
    ASSERT(rt_physics2d_world_body_count(world) == 0, "0 bodies after remove");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_world_add_multiple() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *b1 = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b2 = rt_physics2d_body_new(50, 0, 10, 10, 1.0);
    void *b3 = rt_physics2d_body_new(100, 0, 10, 10, 1.0);

    rt_physics2d_world_add(world, b1);
    rt_physics2d_world_add(world, b2);
    rt_physics2d_world_add(world, b3);
    ASSERT(rt_physics2d_world_body_count(world) == 3, "3 bodies after adds");

    rt_obj_release_check0(b1);
    rt_obj_release_check0(b2);
    rt_obj_release_check0(b3);
    rt_obj_release_check0(world);
}

static void test_duplicate_body_add_ignored() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);

    rt_physics2d_world_add(world, body);
    rt_physics2d_world_add(world, body);
    ASSERT(rt_physics2d_world_body_count(world) == 1, "duplicate body add is ignored");

    release_obj(body);
    release_obj(world);
}

//=============================================================================
// Body tests
//=============================================================================

static void test_body_new() {
    void *body = rt_physics2d_body_new(10.0, 20.0, 30.0, 40.0, 5.0);
    ASSERT(body != NULL, "body_new returns non-null");
    ASSERT_NEAR(rt_physics2d_body_x(body), 10.0, "x = 10");
    ASSERT_NEAR(rt_physics2d_body_y(body), 20.0, "y = 20");
    ASSERT_NEAR(rt_physics2d_body_w(body), 30.0, "w = 30");
    ASSERT_NEAR(rt_physics2d_body_h(body), 40.0, "h = 40");
    ASSERT_NEAR(rt_physics2d_body_vx(body), 0.0, "vx = 0");
    ASSERT_NEAR(rt_physics2d_body_vy(body), 0.0, "vy = 0");
    ASSERT_NEAR(rt_physics2d_body_mass(body), 5.0, "mass = 5");
    ASSERT(rt_physics2d_body_is_static(body) == 0, "not static");
    rt_obj_release_check0(body);
}

static void test_body_static() {
    void *body = rt_physics2d_body_new(0, 0, 100, 10, 0.0);
    ASSERT(rt_physics2d_body_is_static(body) == 1, "mass=0 is static");
    ASSERT_NEAR(rt_physics2d_body_mass(body), 0.0, "mass = 0");
    rt_obj_release_check0(body);
}

static void test_body_set_pos() {
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    rt_physics2d_body_set_pos(body, 42.0, 99.0);
    ASSERT_NEAR(rt_physics2d_body_x(body), 42.0, "x after set_pos");
    ASSERT_NEAR(rt_physics2d_body_y(body), 99.0, "y after set_pos");
    ASSERT_NEAR(rt_physics2d_body_prev_x(body), 42.0, "prev_x after set_pos");
    ASSERT_NEAR(rt_physics2d_body_prev_y(body), 99.0, "prev_y after set_pos");
    rt_obj_release_check0(body);
}

static void test_body_set_vel() {
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    rt_physics2d_body_set_vel(body, 5.0, -3.0);
    ASSERT_NEAR(rt_physics2d_body_vx(body), 5.0, "vx after set_vel");
    ASSERT_NEAR(rt_physics2d_body_vy(body), -3.0, "vy after set_vel");
    rt_obj_release_check0(body);
}

static void test_body_restitution_friction() {
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    // Defaults
    ASSERT_NEAR(rt_physics2d_body_restitution(body), 0.5, "default restitution = 0.5");
    ASSERT_NEAR(rt_physics2d_body_friction(body), 0.3, "default friction = 0.3");

    rt_physics2d_body_set_restitution(body, 0.9);
    rt_physics2d_body_set_friction(body, 0.1);
    ASSERT_NEAR(rt_physics2d_body_restitution(body), 0.9, "restitution after set");
    ASSERT_NEAR(rt_physics2d_body_friction(body), 0.1, "friction after set");
    rt_obj_release_check0(body);
}

static void test_body_inputs_sanitized() {
    double nan = std::nan("");
    void *body = rt_physics2d_body_new(nan, nan, -2.0, 0.0, nan);

    ASSERT_NEAR(rt_physics2d_body_x(body), 0.0, "NaN x sanitized to 0");
    ASSERT_NEAR(rt_physics2d_body_y(body), 0.0, "NaN y sanitized to 0");
    ASSERT_NEAR(rt_physics2d_body_w(body), 1.0, "invalid width sanitized to 1");
    ASSERT_NEAR(rt_physics2d_body_h(body), 1.0, "invalid height sanitized to 1");
    ASSERT(rt_physics2d_body_is_static(body) == 1, "invalid mass becomes static");

    release_obj(body);
}

static void test_static_set_vel_ignored() {
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 0.0);
    rt_physics2d_body_set_vel(body, 100.0, -25.0);
    ASSERT_NEAR(rt_physics2d_body_vx(body), 0.0, "static body ignores set_vel vx");
    ASSERT_NEAR(rt_physics2d_body_vy(body), 0.0, "static body ignores set_vel vy");
    release_obj(body);
}

static void test_restitution_friction_clamped() {
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    rt_physics2d_body_set_restitution(body, 2.0);
    rt_physics2d_body_set_friction(body, -1.0);
    ASSERT_NEAR(rt_physics2d_body_restitution(body), 1.0, "restitution clamps high to 1");
    ASSERT_NEAR(rt_physics2d_body_friction(body), 0.0, "friction clamps low to 0");

    rt_physics2d_body_set_restitution(body, std::nan(""));
    rt_physics2d_body_set_friction(body, std::nan(""));
    ASSERT_NEAR(rt_physics2d_body_restitution(body), 0.0, "NaN restitution clamps to 0");
    ASSERT_NEAR(rt_physics2d_body_friction(body), 0.0, "NaN friction clamps to 0");
    release_obj(body);
}

//=============================================================================
// Integration tests
//=============================================================================

static void test_gravity_integration() {
    void *world = rt_physics2d_world_new(0.0, 10.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    rt_physics2d_world_add(world, body);

    // Step 1 second
    rt_physics2d_world_step(world, 1.0);

    // After 1s with gravity 10: vy = 10, y = 10
    ASSERT_NEAR(rt_physics2d_body_vy(body), 10.0, "vy = 10 after 1s gravity");
    ASSERT_NEAR(rt_physics2d_body_y(body), 10.0, "y = 10 after 1s gravity");
    ASSERT_NEAR(rt_physics2d_body_x(body), 0.0, "x unchanged");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_velocity_integration() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    rt_physics2d_body_set_vel(body, 100.0, 50.0);
    rt_physics2d_world_add(world, body);

    rt_physics2d_world_step(world, 0.5);

    ASSERT_NEAR(rt_physics2d_body_x(body), 50.0, "x = 50 after 0.5s at vx=100");
    ASSERT_NEAR(rt_physics2d_body_y(body), 25.0, "y = 25 after 0.5s at vy=50");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_static_body_no_gravity() {
    void *world = rt_physics2d_world_new(0.0, 100.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 0.0); // static
    rt_physics2d_world_add(world, body);

    rt_physics2d_world_step(world, 1.0);

    ASSERT_NEAR(rt_physics2d_body_x(body), 0.0, "static x unchanged");
    ASSERT_NEAR(rt_physics2d_body_y(body), 0.0, "static y unchanged");
    ASSERT_NEAR(rt_physics2d_body_vy(body), 0.0, "static vy unchanged");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_force_application() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 2.0);
    rt_physics2d_world_add(world, body);

    // Apply force of 20 to mass-2 body => acceleration = 10
    rt_physics2d_body_apply_force(body, 20.0, 0.0);
    rt_physics2d_world_step(world, 1.0);

    ASSERT_NEAR(rt_physics2d_body_vx(body), 10.0, "vx = F/m * t = 10");
    ASSERT_NEAR(rt_physics2d_body_x(body), 10.0, "x = v*t = 10");

    // Force should be cleared after step
    rt_physics2d_world_step(world, 1.0);
    ASSERT_NEAR(rt_physics2d_body_vx(body), 10.0, "vx unchanged (no new force)");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_impulse_application() {
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 2.0);

    // Impulse of 10 on mass-2 body => dv = impulse * inv_mass = 10 * 0.5 = 5
    rt_physics2d_body_apply_impulse(body, 10.0, 0.0);
    ASSERT_NEAR(rt_physics2d_body_vx(body), 5.0, "impulse changes velocity instantly");

    // Static body ignores impulse
    void *staticBody = rt_physics2d_body_new(0, 0, 10, 10, 0.0);
    rt_physics2d_body_apply_impulse(staticBody, 100.0, 100.0);
    ASSERT_NEAR(rt_physics2d_body_vx(staticBody), 0.0, "static ignores impulse");

    rt_obj_release_check0(body);
    rt_obj_release_check0(staticBody);
}

//=============================================================================
// Collision tests
//=============================================================================

static void test_collision_detection() {
    void *world = rt_physics2d_world_new(0.0, 0.0);

    // Two overlapping bodies: overlap of 5 units on x-axis
    void *a = rt_physics2d_body_new(0, 0, 20, 20, 1.0);
    void *b = rt_physics2d_body_new(15, 0, 20, 20, 1.0);

    // Moving toward each other
    rt_physics2d_body_set_vel(a, 10.0, 0.0);
    rt_physics2d_body_set_vel(b, -10.0, 0.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);

    // After step, collision should modify velocities
    double va_before = rt_physics2d_body_vx(a);
    double vb_before = rt_physics2d_body_vx(b);
    /// @brief Rt_physics2d_world_step.
    rt_physics2d_world_step(world, 0.001); // tiny dt to minimize integration drift

    double va = rt_physics2d_body_vx(a);
    double vb = rt_physics2d_body_vx(b);
    // Bodies were overlapping and moving toward each other,
    // so collision resolution should change their velocities
    ASSERT(fabs(va - va_before) > EPSILON || fabs(vb - vb_before) > EPSILON,
           "collision changed at least one velocity");

    rt_obj_release_check0(a);
    rt_obj_release_check0(b);
    rt_obj_release_check0(world);
}

static void test_no_collision_separated() {
    void *world = rt_physics2d_world_new(0.0, 0.0);

    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(100, 0, 10, 10, 1.0);

    rt_physics2d_body_set_vel(a, 5.0, 0.0);
    rt_physics2d_body_set_vel(b, -5.0, 0.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);

    rt_physics2d_world_step(world, 0.016);

    // Bodies are far apart, velocities should be unchanged
    ASSERT_NEAR(rt_physics2d_body_vx(a), 5.0, "separated A vx unchanged");
    ASSERT_NEAR(rt_physics2d_body_vx(b), -5.0, "separated B vx unchanged");

    rt_obj_release_check0(a);
    rt_obj_release_check0(b);
    rt_obj_release_check0(world);
}

static void test_collision_with_static() {
    void *world = rt_physics2d_world_new(0.0, 0.0);

    // Dynamic body overlapping static body
    void *dynamic = rt_physics2d_body_new(0, 0, 20, 20, 1.0);
    void *wall = rt_physics2d_body_new(15, 0, 20, 20, 0.0); // static

    rt_physics2d_body_set_vel(dynamic, 10.0, 0.0);
    rt_physics2d_body_set_restitution(dynamic, 1.0);
    rt_physics2d_body_set_restitution(wall, 1.0);

    rt_physics2d_world_add(world, dynamic);
    rt_physics2d_world_add(world, wall);

    rt_physics2d_world_step(world, 0.016);

    // Static body should remain in place
    ASSERT_NEAR(rt_physics2d_body_x(wall), 15.0, "static wall x unchanged");
    ASSERT_NEAR(rt_physics2d_body_vx(wall), 0.0, "static wall vx = 0");

    rt_obj_release_check0(dynamic);
    rt_obj_release_check0(wall);
    rt_obj_release_check0(world);
}

static void test_set_gravity() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    rt_physics2d_world_add(world, body);

    // No gravity initially
    rt_physics2d_world_step(world, 1.0);
    ASSERT_NEAR(rt_physics2d_body_vy(body), 0.0, "no gravity, vy=0");

    // Set gravity
    rt_physics2d_world_set_gravity(world, 0.0, 5.0);
    rt_physics2d_world_step(world, 1.0);
    ASSERT_NEAR(rt_physics2d_body_vy(body), 5.0, "vy=5 after gravity set");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_invalid_dt_noop() {
    void *world = rt_physics2d_world_new(0.0, 10.0);
    void *body = rt_physics2d_body_new(5.0, 5.0, 10.0, 10.0, 1.0);
    rt_physics2d_world_add(world, body);

    rt_physics2d_world_step(world, std::nan(""));
    ASSERT_NEAR(rt_physics2d_body_x(body), 5.0, "x unchanged with NaN dt");
    ASSERT_NEAR(rt_physics2d_body_y(body), 5.0, "y unchanged with NaN dt");
    ASSERT_NEAR(rt_physics2d_body_vy(body), 0.0, "velocity unchanged with NaN dt");

    release_obj(body);
    release_obj(world);
}

static void test_separating_overlap_still_corrected() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(5, 0, 10, 10, 1.0);
    rt_physics2d_body_set_vel(a, -10.0, 0.0);
    rt_physics2d_body_set_vel(b, 10.0, 0.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);
    rt_physics2d_world_step(world, 0.001);

    ASSERT(rt_physics2d_body_x(b) - rt_physics2d_body_x(a) > 5.5,
           "separating overlap still receives positional correction");

    release_obj(a);
    release_obj(b);
    release_obj(world);
}

static void test_circle_body_collides_with_aabb() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *circle = rt_physics2d_circle_body_new(0.0, 0.0, 5.0, 1.0);
    void *wall = rt_physics2d_body_new(4.0, -5.0, 10.0, 10.0, 0.0);
    rt_physics2d_body_set_vel(circle, 10.0, 0.0);

    rt_physics2d_world_add(world, circle);
    rt_physics2d_world_add(world, wall);
    rt_physics2d_world_step(world, 0.001);

    ASSERT(rt_physics2d_body_vx(circle) < 10.0 - EPSILON,
           "circle-vs-AABB overlap changes velocity");

    release_obj(circle);
    release_obj(wall);
    release_obj(world);
}

static void test_swept_aabb_hits_thin_wall() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    void *wall = rt_physics2d_body_new(50.0, 0.0, 1.0, 10.0, 0.0);
    rt_physics2d_body_set_vel(body, 1000.0, 0.0);

    rt_physics2d_world_add(world, body);
    rt_physics2d_world_add(world, wall);
    rt_physics2d_world_step(world, 0.1);

    ASSERT(rt_physics2d_body_x(body) <= 40.0 + EPSILON,
           "swept AABB collision stops at thin wall contact");
    ASSERT(rt_physics2d_body_vx(body) < 0.0, "swept AABB collision applies bounce impulse");

    release_obj(body);
    release_obj(wall);
    release_obj(world);
}

static void test_swept_circle_hits_thin_wall() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *circle = rt_physics2d_circle_body_new(0.0, 5.0, 5.0, 1.0);
    void *wall = rt_physics2d_body_new(50.0, 0.0, 1.0, 10.0, 0.0);
    rt_physics2d_body_set_vel(circle, 1000.0, 0.0);

    rt_physics2d_world_add(world, circle);
    rt_physics2d_world_add(world, wall);
    rt_physics2d_world_step(world, 0.1);

    ASSERT(rt_physics2d_body_x(circle) <= 45.0 + EPSILON,
           "swept circle collision catches thin wall");
    ASSERT(rt_physics2d_body_vx(circle) < 0.0, "swept circle collision applies bounce impulse");

    release_obj(circle);
    release_obj(wall);
    release_obj(world);
}

static void test_swept_circle_misses_near_diagonal_target() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *circle = rt_physics2d_circle_body_new(0.0, 0.0, 5.0, 1.0);
    void *target = rt_physics2d_circle_body_new(60.0, 40.0, 5.0, 0.0);
    rt_physics2d_body_set_vel(circle, 1000.0, 1000.0);

    rt_physics2d_world_add(world, circle);
    rt_physics2d_world_add(world, target);
    rt_physics2d_world_step(world, 0.1);

    ASSERT_NEAR(rt_physics2d_body_vx(circle), 1000.0, "swept circle miss leaves vx unchanged");
    ASSERT_NEAR(rt_physics2d_body_vy(circle), 1000.0, "swept circle miss leaves vy unchanged");
    ASSERT(rt_physics2d_world_contact_count(world) == 0, "swept circle miss records no contact");

    release_obj(circle);
    release_obj(target);
    release_obj(world);
}

static void test_circle_inside_aabb_resolves_outward() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *circle = rt_physics2d_circle_body_new(1.0, 5.0, 2.0, 1.0);
    void *wall = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 0.0);

    rt_physics2d_world_add(world, circle);
    rt_physics2d_world_add(world, wall);
    rt_physics2d_world_step(world, 0.001);

    ASSERT(rt_physics2d_body_x(circle) < 1.0,
           "circle center inside AABB resolves toward nearest exit side");

    release_obj(circle);
    release_obj(wall);
    release_obj(world);
}

static void test_circle_radius_allows_subunit_values() {
    void *circle = rt_physics2d_circle_body_new(0.0, 0.0, 0.25, 1.0);
    ASSERT_NEAR(rt_physics2d_body_radius(circle), 0.25, "circle radius preserves subunit value");
    release_obj(circle);
}

static void test_circle_aabb_tangent_is_not_overlap() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *circle = rt_physics2d_circle_body_new(0.0, 5.0, 5.0, 1.0);
    void *wall = rt_physics2d_body_new(5.0, 0.0, 10.0, 10.0, 0.0);

    rt_physics2d_world_add(world, circle);
    rt_physics2d_world_add(world, wall);
    rt_physics2d_world_step(world, 0.001);

    ASSERT(rt_physics2d_world_contact_count(world) == 0, "circle-AABB tangency is not overlap");

    release_obj(circle);
    release_obj(wall);
    release_obj(world);
}

static void test_world_records_step_contacts() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    void *b = rt_physics2d_body_new(5.0, 0.0, 10.0, 10.0, 1.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);
    rt_physics2d_world_step(world, 0.001);

    ASSERT(rt_physics2d_world_contact_count(world) >= 1, "overlap records at least one contact");
    ASSERT(rt_physics2d_world_contact_body_a(world, 0) == a, "contact body A is exposed");
    ASSERT(rt_physics2d_world_contact_body_b(world, 0) == b, "contact body B is exposed");
    ASSERT(std::isfinite(rt_physics2d_world_contact_nx(world, 0)), "contact nx is finite");
    ASSERT(std::isfinite(rt_physics2d_world_contact_ny(world, 0)), "contact ny is finite");
    ASSERT(rt_physics2d_world_contact_depth(world, 0) > 0.0, "overlap contact has depth");

    ASSERT(rt_physics2d_world_contact_body_a(world, -1) == NULL, "negative contact index is NULL");
    ASSERT(rt_physics2d_world_contact_body_b(world, 999) == NULL, "large contact index is NULL");
    ASSERT(rt_physics2d_world_contact_depth(world, 999) == 0.0, "invalid contact depth is zero");

    release_obj(a);
    release_obj(b);
    release_obj(world);
}

static void test_world_clears_contacts_on_noop_step() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    void *b = rt_physics2d_body_new(5.0, 0.0, 10.0, 10.0, 1.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);
    rt_physics2d_world_step(world, 0.001);
    ASSERT(rt_physics2d_world_contact_count(world) > 0, "setup records contact");

    rt_physics2d_world_step(world, 0.0);
    ASSERT(rt_physics2d_world_contact_count(world) == 0, "zero-dt step clears old contacts");

    release_obj(a);
    release_obj(b);
    release_obj(world);
}

static void test_world_remove_clears_contacts() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    void *b = rt_physics2d_body_new(5.0, 0.0, 10.0, 10.0, 1.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);
    rt_physics2d_world_step(world, 0.001);
    ASSERT(rt_physics2d_world_contact_count(world) > 0, "setup records removable contact");

    rt_physics2d_world_remove(world, b);
    ASSERT(rt_physics2d_world_contact_count(world) == 0, "remove clears old contacts");
    ASSERT(rt_physics2d_world_contact_body_a(world, 0) == NULL, "removed contact body A hidden");

    release_obj(a);
    release_obj(b);
    release_obj(world);
}

static void test_world_contact_overflow_flag() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    void *b = rt_physics2d_body_new(5.0, 0.0, 10.0, 10.0, 1.0);

    for (int i = 0; i < PH_MAX_CONTACTS; i++) {
        world_record_contact((rt_world_impl *)world, (rt_body_impl *)a, (rt_body_impl *)b, 1.0, 0.0, 1.0);
    }
    ASSERT(rt_physics2d_world_contact_count(world) == PH_MAX_CONTACTS, "contact count reaches cap");
    ASSERT(rt_physics2d_world_contact_overflowed(world) == 0, "exact cap does not report overflow");

    world_record_contact((rt_world_impl *)world, (rt_body_impl *)a, (rt_body_impl *)b, 1.0, 0.0, 1.0);
    ASSERT(rt_physics2d_world_contact_count(world) == PH_MAX_CONTACTS, "overflow keeps capped count");
    ASSERT(rt_physics2d_world_contact_overflowed(world) == 1, "overflow flag reports omitted contacts");

    rt_physics2d_world_step(world, 0.0);
    ASSERT(rt_physics2d_world_contact_count(world) == 0, "clear removes capped contacts");
    ASSERT(rt_physics2d_world_contact_overflowed(world) == 0, "clear resets overflow flag");

    release_obj(a);
    release_obj(b);
    release_obj(world);
}

static void test_extreme_forces_are_sanitized() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    rt_physics2d_world_add(world, body);

    double huge = std::numeric_limits<double>::max() / 4.0;
    rt_physics2d_body_apply_impulse(body, huge, -huge);
    rt_physics2d_body_apply_force(body, huge, huge);
    rt_physics2d_world_step(world, huge);

    ASSERT(std::isfinite(rt_physics2d_body_x(body)), "extreme force leaves finite x");
    ASSERT(std::isfinite(rt_physics2d_body_y(body)), "extreme force leaves finite y");
    ASSERT(std::isfinite(rt_physics2d_body_vx(body)), "extreme force leaves finite vx");
    ASSERT(std::isfinite(rt_physics2d_body_vy(body)), "extreme force leaves finite vy");

    release_obj(body);
    release_obj(world);
}

//=============================================================================
// Null safety
//=============================================================================

static void test_null_safety() {
    // World functions
    ASSERT(rt_physics2d_world_body_count(NULL) == 0, "null world count = 0");
    /// @brief Rt_physics2d_world_step.
    rt_physics2d_world_step(NULL, 1.0);         // should not crash
                                                /// @brief Rt_physics2d_world_add.
    rt_physics2d_world_add(NULL, NULL);         // should not crash
                                                /// @brief Rt_physics2d_world_remove.
    rt_physics2d_world_remove(NULL, NULL);      // should not crash
                                                /// @brief Rt_physics2d_world_set_gravity.
    rt_physics2d_world_set_gravity(NULL, 0, 0); // should not crash

    // Body functions
    ASSERT_NEAR(rt_physics2d_body_x(NULL), 0.0, "null body x = 0");
    ASSERT_NEAR(rt_physics2d_body_y(NULL), 0.0, "null body y = 0");
    ASSERT_NEAR(rt_physics2d_body_vx(NULL), 0.0, "null body vx = 0");
    ASSERT_NEAR(rt_physics2d_body_vy(NULL), 0.0, "null body vy = 0");
    ASSERT_NEAR(rt_physics2d_body_mass(NULL), 0.0, "null body mass = 0");
    /// @brief Rt_physics2d_body_set_pos.
    rt_physics2d_body_set_pos(NULL, 0, 0);       // should not crash
                                                 /// @brief Rt_physics2d_body_set_vel.
    rt_physics2d_body_set_vel(NULL, 0, 0);       // should not crash
                                                 /// @brief Rt_physics2d_body_apply_force.
    rt_physics2d_body_apply_force(NULL, 0, 0);   // should not crash
                                                 /// @brief Rt_physics2d_body_apply_impulse.
    rt_physics2d_body_apply_impulse(NULL, 0, 0); // should not crash

    tests_run++;
    tests_passed++; // If we get here, null safety passed
}

static void test_wrong_handle_traps() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0.0, 0.0, 10.0, 10.0, 1.0);
    void *fake = rt_obj_new_i64(0, 8);

    EXPECT_TRAP(rt_physics2d_world_add(world, fake));
    EXPECT_TRAP(rt_physics2d_distance_joint_new(body, fake, 10.0));
    EXPECT_TRAP(rt_physics2d_world_add_joint(world, fake));
    EXPECT_TRAP(rt_physics2d_body_radius(fake));

    release_obj(fake);
    release_obj(body);
    release_obj(world);
}

static void test_zero_dt() {
    void *world = rt_physics2d_world_new(0.0, 10.0);
    void *body = rt_physics2d_body_new(5.0, 5.0, 10.0, 10.0, 1.0);
    rt_physics2d_world_add(world, body);

    /// @brief Rt_physics2d_world_step.
    rt_physics2d_world_step(world, 0.0); // zero dt should be no-op
    ASSERT_NEAR(rt_physics2d_body_x(body), 5.0, "x unchanged with dt=0");
    ASSERT_NEAR(rt_physics2d_body_y(body), 5.0, "y unchanged with dt=0");

    /// @brief Rt_physics2d_world_step.
    rt_physics2d_world_step(world, -1.0); // negative dt should be no-op
    ASSERT_NEAR(rt_physics2d_body_x(body), 5.0, "x unchanged with dt<0");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

//=============================================================================
// GAME-H-6: collision_mask default should include bit 31 (0xFFFFFFFF)
//=============================================================================

static void test_collision_mask_default_full() {
    // Default mask must cover every 64-bit layer.
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    int64_t mask = rt_physics2d_body_collision_mask(body);
    ASSERT((mask & (int64_t)0x80000000LL) != 0, "default mask covers bit 31 (GAME-H-6 fix)");
    ASSERT(mask == INT64_C(-1), "default mask covers all 64 bits");
    rt_obj_release_check0(body);
}

static void test_collision_layer31_collides_with_default_mask() {
    void *world = rt_physics2d_world_new(0.0, 0.0);

    // Body A on layer 31 — before fix it was excluded from the default mask
    void *a = rt_physics2d_body_new(0, 0, 20, 20, 1.0);
    rt_physics2d_body_set_collision_layer(a, (int64_t)1 << 31);
    // keep default mask (0xFFFFFFFF)

    // Body B on layer 1 with default mask
    void *b = rt_physics2d_body_new(10, 0, 20, 20, 1.0);

    rt_physics2d_body_set_vel(a, 5.0, 0.0);
    rt_physics2d_body_set_vel(b, -5.0, 0.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);

    double va_before = rt_physics2d_body_vx(a);
    rt_physics2d_world_step(world, 0.001);
    double va_after = rt_physics2d_body_vx(a);

    // Collision must have changed velocity (bodies overlap, masks allow collision)
    ASSERT(fabs(va_after - va_before) > EPSILON,
           "layer-31 body collides with default-mask body (GAME-H-6 fix)");

    rt_obj_release_check0(a);
    rt_obj_release_check0(b);
    rt_obj_release_check0(world);
}

static void test_collision_mask_filtering_works() {
    // Bodies on incompatible layers should NOT collide
    void *world = rt_physics2d_world_new(0.0, 0.0);

    void *a = rt_physics2d_body_new(0, 0, 20, 20, 1.0);
    rt_physics2d_body_set_collision_layer(a, 1);
    /// @brief Rt_physics2d_body_set_collision_mask.
    rt_physics2d_body_set_collision_mask(a, 2); // only collides with layer 2

    void *b = rt_physics2d_body_new(10, 0, 20, 20, 1.0);
    /// @brief Rt_physics2d_body_set_collision_layer.
    rt_physics2d_body_set_collision_layer(b, 4); // layer 4, not in A's mask
    rt_physics2d_body_set_collision_mask(b, 1);

    rt_physics2d_body_set_vel(a, 10.0, 0.0);
    rt_physics2d_body_set_vel(b, -10.0, 0.0);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);
    rt_physics2d_world_step(world, 0.001);

    // Velocities must be unchanged — no collision between incompatible layers
    ASSERT_NEAR(rt_physics2d_body_vx(a), 10.0, "layer-filtered A vx unchanged");
    ASSERT_NEAR(rt_physics2d_body_vx(b), -10.0, "layer-filtered B vx unchanged");

    rt_obj_release_check0(a);
    rt_obj_release_check0(b);
    rt_obj_release_check0(world);
}

static void test_dense_cell_overflow_falls_back_to_all_pairs() {
    void *world = rt_physics2d_world_new(0.0, 0.0);

    void *wall = rt_physics2d_body_new(0, 0, 20, 20, 0.0);
    rt_physics2d_body_set_collision_layer(wall, 1);
    rt_physics2d_body_set_collision_mask(wall, 1);
    rt_physics2d_world_add(world, wall);

    for (int i = 0; i < 32; i++) {
        void *filler = rt_physics2d_body_new(0, 0, 20, 20, 1.0);
        rt_physics2d_body_set_collision_layer(filler, 2);
        rt_physics2d_body_set_collision_mask(filler, 0);
        rt_physics2d_world_add(world, filler);
        release_obj(filler);
    }

    // This body lands beyond the 32-body cell cap and used to be dropped from broad-phase cells.
    void *target = rt_physics2d_body_new(1, 0, 20, 20, 1.0);
    rt_physics2d_body_set_collision_layer(target, 1);
    rt_physics2d_body_set_collision_mask(target, 1);
    rt_physics2d_body_set_vel(target, -5.0, 0.0);
    rt_physics2d_world_add(world, target);

    rt_physics2d_world_step(world, 0.001);
    ASSERT(fabs(rt_physics2d_body_vx(target) + 5.0) > EPSILON,
           "dense-cell overflow still resolves the dropped pair via O(n^2) fallback");

    release_obj(target);
    release_obj(wall);
    release_obj(world);
}

static void test_world_finalizer_releases_joint_retained_bodies() {
    g_body_finalizer_calls = 0;

    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(20, 0, 10, 10, 1.0);
    void *joint = rt_physics2d_distance_joint_new(a, b, 20.0);
    rt_obj_set_finalizer(a, test_body_finalizer);
    rt_obj_set_finalizer(b, test_body_finalizer);

    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);
    rt_physics2d_world_add_joint(world, joint);

    // Drop the caller's reference; the world should own the last remaining joint reference.
    ASSERT(rt_obj_release_check0(joint) == 0, "joint retained by world before world finalizer");

    release_obj(a);
    release_obj(b);
    release_obj(world);

    ASSERT(g_body_finalizer_calls == 2, "joint/world finalizers release retained bodies");
}

static void test_add_joint_requires_world_bodies() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(20, 0, 10, 10, 1.0);
    void *joint = rt_physics2d_distance_joint_new(a, b, 20.0);

    EXPECT_TRAP(rt_physics2d_world_add_joint(world, joint));

    release_obj(joint);
    release_obj(a);
    release_obj(b);
    release_obj(world);
}

static void test_joint_limit_traps() {
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(20, 0, 10, 10, 1.0);
    rt_physics2d_world_add(world, a);
    rt_physics2d_world_add(world, b);

    for (int i = 0; i < 64; i++) {
        void *joint = rt_physics2d_distance_joint_new(a, b, 20.0 + i);
        rt_physics2d_world_add_joint(world, joint);
        release_obj(joint);
    }
    ASSERT(rt_physics2d_world_joint_count(world) == 64, "64 joints at limit");

    void *extra = rt_physics2d_distance_joint_new(a, b, 100.0);
    EXPECT_TRAP(rt_physics2d_world_add_joint(world, extra));
    release_obj(extra);

    release_obj(a);
    release_obj(b);
    release_obj(world);
}

//=============================================================================
// GAME-C-4: PH_MAX_BODIES exceeded should trap
//=============================================================================

static void test_body_limit_traps() {
    void *world = rt_physics2d_world_new(0.0, 0.0);

    // Fill to the limit (256 bodies)
    for (int i = 0; i < 256; i++) {
        void *body = rt_physics2d_body_new((double)i, 0, 1, 1, 1.0);
        rt_physics2d_world_add(world, body);
        rt_obj_release_check0(body);
    }
    ASSERT(rt_physics2d_world_body_count(world) == 256, "256 bodies at limit");

    // Adding one more must trap (GAME-C-4 fix)
    void *extra = rt_physics2d_body_new(0, 0, 1, 1, 1.0);
    EXPECT_TRAP(rt_physics2d_world_add(world, extra));
    rt_obj_release_check0(extra);

    rt_obj_release_check0(world);
}

/// @brief Main.
int main() {
    // World tests
    test_world_new();
    test_world_add_remove();
    test_world_add_multiple();
    test_duplicate_body_add_ignored();

    // Body tests
    test_body_new();
    test_body_static();
    test_body_set_pos();
    test_body_set_vel();
    test_body_restitution_friction();
    test_body_inputs_sanitized();
    test_static_set_vel_ignored();
    test_restitution_friction_clamped();

    // Integration tests
    test_gravity_integration();
    test_velocity_integration();
    test_static_body_no_gravity();
    test_force_application();
    test_impulse_application();

    // Collision tests
    test_collision_detection();
    test_no_collision_separated();
    test_collision_with_static();
    test_set_gravity();
    test_invalid_dt_noop();
    test_separating_overlap_still_corrected();
    test_circle_body_collides_with_aabb();
    test_swept_aabb_hits_thin_wall();
    test_swept_circle_hits_thin_wall();
    test_swept_circle_misses_near_diagonal_target();
    test_circle_inside_aabb_resolves_outward();
    test_circle_radius_allows_subunit_values();
    test_circle_aabb_tangent_is_not_overlap();
    test_world_records_step_contacts();
    test_world_clears_contacts_on_noop_step();
    test_world_remove_clears_contacts();
    test_world_contact_overflow_flag();
    test_extreme_forces_are_sanitized();

    // Safety tests
    test_null_safety();
    test_wrong_handle_traps();
    test_zero_dt();

    // GAME-H-6: collision_mask default covers all 32 layers
    test_collision_mask_default_full();
    test_collision_layer31_collides_with_default_mask();
    test_collision_mask_filtering_works();
    test_dense_cell_overflow_falls_back_to_all_pairs();
    test_world_finalizer_releases_joint_retained_bodies();
    test_add_joint_requires_world_bodies();
    test_joint_limit_traps();

    // GAME-C-4: body limit traps
    test_body_limit_traps();

    printf("Physics2D tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
