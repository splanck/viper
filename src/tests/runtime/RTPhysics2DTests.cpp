//===----------------------------------------------------------------------===//
// RTPhysics2DTests.cpp - Tests for rt_physics2d (2D physics engine)
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C"
{
#include "rt_object.h"
#include "rt_physics2d.h"

    void vm_trap(const char *msg)
    {
        fprintf(stderr, "TRAP: %s\n", msg);
    }
}

static int tests_run = 0;
static int tests_passed = 0;

static const double EPSILON = 1e-6;

#define ASSERT(cond, msg)                                                                          \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

#define ASSERT_NEAR(a, b, msg) ASSERT(fabs((a) - (b)) < EPSILON, msg)

//=============================================================================
// World tests
//=============================================================================

static void test_world_new()
{
    void *world = rt_physics2d_world_new(0.0, 9.8);
    ASSERT(world != NULL, "world_new returns non-null");
    ASSERT(rt_physics2d_world_body_count(world) == 0, "new world has 0 bodies");
    rt_obj_release_check0(world);
}

static void test_world_add_remove()
{
    void *world = rt_physics2d_world_new(0.0, 0.0);
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);

    rt_physics2d_world_add(world, body);
    ASSERT(rt_physics2d_world_body_count(world) == 1, "1 body after add");

    rt_physics2d_world_remove(world, body);
    ASSERT(rt_physics2d_world_body_count(world) == 0, "0 bodies after remove");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

static void test_world_add_multiple()
{
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

//=============================================================================
// Body tests
//=============================================================================

static void test_body_new()
{
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

static void test_body_static()
{
    void *body = rt_physics2d_body_new(0, 0, 100, 10, 0.0);
    ASSERT(rt_physics2d_body_is_static(body) == 1, "mass=0 is static");
    ASSERT_NEAR(rt_physics2d_body_mass(body), 0.0, "mass = 0");
    rt_obj_release_check0(body);
}

static void test_body_set_pos()
{
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    rt_physics2d_body_set_pos(body, 42.0, 99.0);
    ASSERT_NEAR(rt_physics2d_body_x(body), 42.0, "x after set_pos");
    ASSERT_NEAR(rt_physics2d_body_y(body), 99.0, "y after set_pos");
    rt_obj_release_check0(body);
}

static void test_body_set_vel()
{
    void *body = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    rt_physics2d_body_set_vel(body, 5.0, -3.0);
    ASSERT_NEAR(rt_physics2d_body_vx(body), 5.0, "vx after set_vel");
    ASSERT_NEAR(rt_physics2d_body_vy(body), -3.0, "vy after set_vel");
    rt_obj_release_check0(body);
}

static void test_body_restitution_friction()
{
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

//=============================================================================
// Integration tests
//=============================================================================

static void test_gravity_integration()
{
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

static void test_velocity_integration()
{
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

static void test_static_body_no_gravity()
{
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

static void test_force_application()
{
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

static void test_impulse_application()
{
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

static void test_collision_detection()
{
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

static void test_no_collision_separated()
{
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

static void test_collision_with_static()
{
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

static void test_set_gravity()
{
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

//=============================================================================
// Null safety
//=============================================================================

static void test_null_safety()
{
    // World functions
    ASSERT(rt_physics2d_world_body_count(NULL) == 0, "null world count = 0");
    rt_physics2d_world_step(NULL, 1.0);         // should not crash
    rt_physics2d_world_add(NULL, NULL);         // should not crash
    rt_physics2d_world_remove(NULL, NULL);      // should not crash
    rt_physics2d_world_set_gravity(NULL, 0, 0); // should not crash

    // Body functions
    ASSERT_NEAR(rt_physics2d_body_x(NULL), 0.0, "null body x = 0");
    ASSERT_NEAR(rt_physics2d_body_y(NULL), 0.0, "null body y = 0");
    ASSERT_NEAR(rt_physics2d_body_vx(NULL), 0.0, "null body vx = 0");
    ASSERT_NEAR(rt_physics2d_body_vy(NULL), 0.0, "null body vy = 0");
    ASSERT_NEAR(rt_physics2d_body_mass(NULL), 0.0, "null body mass = 0");
    rt_physics2d_body_set_pos(NULL, 0, 0);       // should not crash
    rt_physics2d_body_set_vel(NULL, 0, 0);       // should not crash
    rt_physics2d_body_apply_force(NULL, 0, 0);   // should not crash
    rt_physics2d_body_apply_impulse(NULL, 0, 0); // should not crash

    tests_run++;
    tests_passed++; // If we get here, null safety passed
}

static void test_zero_dt()
{
    void *world = rt_physics2d_world_new(0.0, 10.0);
    void *body = rt_physics2d_body_new(5.0, 5.0, 10.0, 10.0, 1.0);
    rt_physics2d_world_add(world, body);

    rt_physics2d_world_step(world, 0.0); // zero dt should be no-op
    ASSERT_NEAR(rt_physics2d_body_x(body), 5.0, "x unchanged with dt=0");
    ASSERT_NEAR(rt_physics2d_body_y(body), 5.0, "y unchanged with dt=0");

    rt_physics2d_world_step(world, -1.0); // negative dt should be no-op
    ASSERT_NEAR(rt_physics2d_body_x(body), 5.0, "x unchanged with dt<0");

    rt_obj_release_check0(body);
    rt_obj_release_check0(world);
}

int main()
{
    // World tests
    test_world_new();
    test_world_add_remove();
    test_world_add_multiple();

    // Body tests
    test_body_new();
    test_body_static();
    test_body_set_pos();
    test_body_set_vel();
    test_body_restitution_friction();

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

    // Safety tests
    test_null_safety();
    test_zero_dt();

    printf("Physics2D tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
