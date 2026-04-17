//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/test_rt_physics_joints.cpp
// Purpose: Unit tests for physics joints (Distance, Spring, Hinge, Rope),
//   circle bodies, world joint management, and constraint solving.
//
// Key invariants:
//   - Distance joints converge bodies toward target distance.
//   - Spring joints apply elastic force.
//   - Rope joints only constrain when taut.
//   - Circle bodies have radius > 0 and is_circle == 1.
//   - NULL body produces NULL joint.
//
// Ownership/Lifetime:
//   - Uses runtime library. Physics objects are GC-managed.
//
// Links: src/runtime/graphics/rt_physics2d_joint.h
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_internal.h"
#include "rt_physics2d.h"
#include "rt_physics2d_joint.h"
#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static int tests_passed = 0;
static int tests_total = 0;

#define TEST(name)                                                                                 \
    do {                                                                                           \
        tests_total++;                                                                             \
        printf("  [%d] %s... ", tests_total, name);                                                \
    } while (0)

#define PASS()                                                                                     \
    do {                                                                                           \
        tests_passed++;                                                                            \
        printf("ok\n");                                                                            \
    } while (0)

static double dist_between(void *a, void *b) {
    double ax = rt_physics2d_body_x(a);
    double ay = rt_physics2d_body_y(a);
    double bx = rt_physics2d_body_x(b);
    double by = rt_physics2d_body_y(b);
    // For AABB bodies, use center
    double aw = rt_physics2d_body_w(a);
    double ah = rt_physics2d_body_h(a);
    double bw = rt_physics2d_body_w(b);
    double bh = rt_physics2d_body_h(b);
    double acx = rt_physics2d_body_is_circle(a) ? ax : ax + aw / 2;
    double acy = rt_physics2d_body_is_circle(a) ? ay : ay + ah / 2;
    double bcx = rt_physics2d_body_is_circle(b) ? bx : bx + bw / 2;
    double bcy = rt_physics2d_body_is_circle(b) ? by : by + bh / 2;
    double dx = bcx - acx;
    double dy = bcy - acy;
    return sqrt(dx * dx + dy * dy);
}

//=============================================================================
// Tests
//=============================================================================

static void test_distance_joint_creation(void) {
    TEST("Distance joint creation");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(100, 0, 10, 10, 1.0);
    void *j = rt_physics2d_distance_joint_new(a, b, 50.0);
    assert(j != NULL);
    assert(rt_physics2d_joint_get_type(j) == RT_JOINT_DISTANCE);
    assert(rt_physics2d_distance_joint_get_length(j) == 50.0);
    assert(rt_physics2d_joint_get_body_a(j) == a);
    assert(rt_physics2d_joint_get_body_b(j) == b);
    assert(rt_physics2d_joint_is_active(j) == 1);
    PASS();
}

static void test_distance_joint_converges(void) {
    TEST("Distance joint converges toward target");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(200, 0, 10, 10, 1.0);
    void *j = rt_physics2d_distance_joint_new(a, b, 50.0);
    void *w = rt_physics2d_world_new(0.0, 0.0);
    rt_physics2d_world_add(w, a);
    rt_physics2d_world_add(w, b);
    rt_physics2d_world_add_joint(w, j);

    // Step several times
    for (int i = 0; i < 100; i++)
        rt_physics2d_world_step(w, 1.0 / 60.0);

    double d = dist_between(a, b);
    assert(fabs(d - 50.0) < 5.0); // Within tolerance
    PASS();
}

static void test_spring_joint_creation(void) {
    TEST("Spring joint creation");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(100, 0, 10, 10, 1.0);
    void *j = rt_physics2d_spring_joint_new(a, b, 50.0, 100.0, 5.0);
    assert(j != NULL);
    assert(rt_physics2d_joint_get_type(j) == RT_JOINT_SPRING);
    assert(rt_physics2d_spring_joint_get_stiffness(j) == 100.0);
    assert(rt_physics2d_spring_joint_get_damping(j) == 5.0);
    PASS();
}

static void test_rope_joint_slack(void) {
    TEST("Rope joint allows slack");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 0.0);  // static
    void *b = rt_physics2d_body_new(20, 0, 10, 10, 1.0); // dynamic
    void *j = rt_physics2d_rope_joint_new(a, b, 100.0);
    void *w = rt_physics2d_world_new(0.0, 0.0);
    rt_physics2d_world_add(w, a);
    rt_physics2d_world_add(w, b);
    rt_physics2d_world_add_joint(w, j);

    // Bodies are 20 apart, rope is 100 — should have no effect
    double d_before = dist_between(a, b);
    for (int i = 0; i < 10; i++)
        rt_physics2d_world_step(w, 1.0 / 60.0);
    double d_after = dist_between(a, b);
    assert(fabs(d_after - d_before) < 1.0); // Basically unchanged
    PASS();
}

static void test_rope_joint_clamps(void) {
    TEST("Rope joint clamps distance");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 0.0);   // static
    void *b = rt_physics2d_body_new(200, 0, 10, 10, 1.0); // dynamic, far away
    void *j = rt_physics2d_rope_joint_new(a, b, 50.0);
    void *w = rt_physics2d_world_new(0.0, 0.0);
    rt_physics2d_world_add(w, a);
    rt_physics2d_world_add(w, b);
    rt_physics2d_world_add_joint(w, j);

    for (int i = 0; i < 100; i++)
        rt_physics2d_world_step(w, 1.0 / 60.0);

    double d = dist_between(a, b);
    assert(d <= 55.0); // Clamped (within tolerance)
    PASS();
}

static void test_hinge_joint_angle(void) {
    TEST("Hinge joint angle");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(100, 0, 10, 10, 1.0);
    void *j = rt_physics2d_hinge_joint_new(a, b, 50.0, 0.0);
    assert(j != NULL);
    assert(rt_physics2d_joint_get_type(j) == RT_JOINT_HINGE);
    double angle = rt_physics2d_hinge_joint_get_angle(j);
    assert(fabs(angle) < 0.1); // roughly 0 radians (bodies at same Y)
    PASS();
}

static void test_circle_body(void) {
    TEST("Circle body creation");
    void *c = rt_physics2d_circle_body_new(50.0, 50.0, 25.0, 1.0);
    assert(c != NULL);
    assert(rt_physics2d_body_is_circle(c) == 1);
    assert(rt_physics2d_body_radius(c) == 25.0);
    assert(rt_physics2d_body_x(c) == 50.0); // center for circles
    PASS();
}

static void test_aabb_body_not_circle(void) {
    TEST("AABB body is not circle");
    void *b = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    assert(rt_physics2d_body_is_circle(b) == 0);
    assert(rt_physics2d_body_radius(b) == 0.0);
    PASS();
}

static void test_world_joint_count(void) {
    TEST("World joint count management");
    void *w = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(50, 0, 10, 10, 1.0);

    void *j1 = rt_physics2d_distance_joint_new(a, b, 50.0);
    void *j2 = rt_physics2d_spring_joint_new(a, b, 50.0, 10.0, 1.0);
    void *j3 = rt_physics2d_rope_joint_new(a, b, 100.0);

    rt_physics2d_world_add_joint(w, j1);
    rt_physics2d_world_add_joint(w, j2);
    rt_physics2d_world_add_joint(w, j3);
    assert(rt_physics2d_world_joint_count(w) == 3);

    rt_physics2d_world_remove_joint(w, j2);
    assert(rt_physics2d_world_joint_count(w) == 2);
    PASS();
}

static void test_duplicate_world_add_joint_is_ignored(void) {
    TEST("World ignores duplicate joint add");
    void *w = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(50, 0, 10, 10, 1.0);
    void *j = rt_physics2d_distance_joint_new(a, b, 50.0);

    rt_physics2d_world_add_joint(w, j);
    rt_physics2d_world_add_joint(w, j);
    assert(rt_physics2d_world_joint_count(w) == 1);
    PASS();
}

static void test_removed_joint_reports_inactive(void) {
    TEST("Removed joint reports inactive");
    void *w = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(50, 0, 10, 10, 1.0);
    void *j = rt_physics2d_distance_joint_new(a, b, 50.0);

    rt_physics2d_world_add_joint(w, j);
    assert(rt_physics2d_joint_is_active(j) == 1);
    rt_physics2d_world_remove_joint(w, j);
    assert(rt_physics2d_joint_is_active(j) == 0);
    PASS();
}

static void test_removing_body_clears_attached_joints(void) {
    TEST("Removing body clears attached joints");
    void *w = rt_physics2d_world_new(0.0, 0.0);
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(50, 0, 10, 10, 1.0);
    void *j = rt_physics2d_distance_joint_new(a, b, 50.0);

    rt_physics2d_world_add(w, a);
    rt_physics2d_world_add(w, b);
    rt_physics2d_world_add_joint(w, j);
    assert(rt_physics2d_world_joint_count(w) == 1);

    rt_physics2d_world_remove(w, a);
    assert(rt_physics2d_world_joint_count(w) == 0);
    assert(rt_physics2d_joint_is_active(j) == 0);
    PASS();
}

static void test_null_body_safety(void) {
    TEST("NULL body returns NULL joint");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    assert(rt_physics2d_distance_joint_new(NULL, a, 50.0) == NULL);
    assert(rt_physics2d_distance_joint_new(a, NULL, 50.0) == NULL);
    assert(rt_physics2d_distance_joint_new(a, a, 50.0) == NULL); // self-joint
    assert(rt_physics2d_spring_joint_new(NULL, a, 50.0, 10.0, 1.0) == NULL);
    assert(rt_physics2d_rope_joint_new(NULL, NULL, 50.0) == NULL);
    assert(rt_physics2d_hinge_joint_new(a, NULL, 0.0, 0.0) == NULL);
    PASS();
}

static void test_negative_params_clamped(void) {
    TEST("Negative parameters clamped to 0");
    void *a = rt_physics2d_body_new(0, 0, 10, 10, 1.0);
    void *b = rt_physics2d_body_new(50, 0, 10, 10, 1.0);

    void *dj = rt_physics2d_distance_joint_new(a, b, -10.0);
    assert(rt_physics2d_distance_joint_get_length(dj) == 0.0);

    void *sj = rt_physics2d_spring_joint_new(a, b, -5.0, -3.0, -1.0);
    assert(rt_physics2d_spring_joint_get_stiffness(sj) == 0.0);
    assert(rt_physics2d_spring_joint_get_damping(sj) == 0.0);
    PASS();
}

static void test_circle_body_min_radius(void) {
    TEST("Circle body minimum radius");
    void *c = rt_physics2d_circle_body_new(0, 0, -5.0, 1.0);
    assert(rt_physics2d_body_radius(c) == 1.0); // Clamped to 1.0
    PASS();
}

int main() {
    printf("test_rt_physics_joints:\n");

    test_distance_joint_creation();
    test_distance_joint_converges();
    test_spring_joint_creation();
    test_rope_joint_slack();
    test_rope_joint_clamps();
    test_hinge_joint_angle();
    test_circle_body();
    test_aabb_body_not_circle();
    test_world_joint_count();
    test_duplicate_world_add_joint_is_ignored();
    test_removed_joint_reports_inactive();
    test_removing_body_clears_attached_joints();
    test_null_body_safety();
    test_negative_params_clamped();
    test_circle_body_min_radius();

    printf("\n  %d/%d tests passed\n", tests_passed, tests_total);
    assert(tests_passed == tests_total);
    return 0;
}
