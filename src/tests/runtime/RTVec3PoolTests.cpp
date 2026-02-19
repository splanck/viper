//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTVec3PoolTests.cpp
// Purpose: Correctness tests for the Vec3 thread-local free-list pool (P2-3.6).
//
// Mirrors RTVec2PoolTests.cpp for the 3D vector type.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_vec3.h"

#include <cassert>
#include <cmath>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static const double EPSILON = 1e-9;

static bool approx_eq(double a, double b)
{
    return fabs(a - b) < EPSILON;
}

/// Release a Vec3 object, triggering the finalizer / pool return.
static void vec3_release(void *v)
{
    if (rt_obj_release_check0(v))
        rt_obj_free(v);
}

// ============================================================================
// Pool recycling: same address returned after release
// ============================================================================

static void test_pool_recycles_address(void)
{
    void *first = rt_vec3_new(1.0, 2.0, 3.0);
    assert(first != nullptr);
    void *saved = first;

    vec3_release(first);

    void *second = rt_vec3_new(10.0, 20.0, 30.0);
    assert(second == saved && "pool should recycle the released allocation");

    assert(approx_eq(rt_vec3_x(second), 10.0));
    assert(approx_eq(rt_vec3_y(second), 20.0));
    assert(approx_eq(rt_vec3_z(second), 30.0));

    vec3_release(second);
    printf("test_pool_recycles_address: PASSED\n");
}

// ============================================================================
// Pool re-initializes: stale fields are overwritten
// ============================================================================

static void test_pool_reinitializes_values(void)
{
    void *v1 = rt_vec3_new(99.0, -99.0, 42.0);
    assert(v1 != nullptr);
    vec3_release(v1);

    void *v2 = rt_vec3_new(0.5, 0.5, 0.5);
    assert(v2 != nullptr);
    assert(approx_eq(rt_vec3_x(v2), 0.5));
    assert(approx_eq(rt_vec3_y(v2), 0.5));
    assert(approx_eq(rt_vec3_z(v2), 0.5));
    vec3_release(v2);

    printf("test_pool_reinitializes_values: PASSED\n");
}

// ============================================================================
// Pool stress: 200 alloc/release cycles must not corrupt memory
// ============================================================================

static void test_pool_stress_cycles(void)
{
    for (int i = 0; i < 200; i++)
    {
        double x = (double)i;
        double y = (double)(i * 2);
        double z = (double)(i * 3);
        void *v = rt_vec3_new(x, y, z);
        assert(v != nullptr);
        assert(approx_eq(rt_vec3_x(v), x));
        assert(approx_eq(rt_vec3_y(v), y));
        assert(approx_eq(rt_vec3_z(v), z));
        vec3_release(v);
    }
    printf("test_pool_stress_cycles: PASSED\n");
}

// ============================================================================
// Pool overflow: releasing more than capacity drains gracefully
// ============================================================================

static const int kPoolOverflowCount = 40; // VEC3_POOL_CAPACITY == 32

static void test_pool_overflow(void)
{
    void *objs[kPoolOverflowCount];
    for (int i = 0; i < kPoolOverflowCount; i++)
        objs[i] = rt_vec3_new((double)i, (double)i, (double)i);

    for (int i = kPoolOverflowCount - 1; i >= 0; i--)
        vec3_release(objs[i]);

    for (int i = 0; i < kPoolOverflowCount; i++)
    {
        void *v = rt_vec3_new(1.0, 2.0, 3.0);
        assert(v != nullptr);
        assert(approx_eq(rt_vec3_x(v), 1.0));
        assert(approx_eq(rt_vec3_y(v), 2.0));
        assert(approx_eq(rt_vec3_z(v), 3.0));
        vec3_release(v);
    }

    printf("test_pool_overflow: PASSED\n");
}

// ============================================================================
// Multiple alive objects don't interfere
// ============================================================================

static void test_pool_live_objects_independent(void)
{
    void *a = rt_vec3_new(1.0, 0.0, 0.0);
    void *b = rt_vec3_new(0.0, 1.0, 0.0);
    void *c = rt_vec3_new(0.0, 0.0, 1.0);

    assert(a != b && b != c && a != c);
    assert(approx_eq(rt_vec3_x(a), 1.0) && approx_eq(rt_vec3_y(a), 0.0) &&
           approx_eq(rt_vec3_z(a), 0.0));
    assert(approx_eq(rt_vec3_x(b), 0.0) && approx_eq(rt_vec3_y(b), 1.0) &&
           approx_eq(rt_vec3_z(b), 0.0));
    assert(approx_eq(rt_vec3_x(c), 0.0) && approx_eq(rt_vec3_y(c), 0.0) &&
           approx_eq(rt_vec3_z(c), 1.0));

    vec3_release(a);
    vec3_release(b);
    vec3_release(c);

    printf("test_pool_live_objects_independent: PASSED\n");
}

// ============================================================================
// Entry point
// ============================================================================

int main(void)
{
    printf("=== Vec3 Pool Tests ===\n\n");

    test_pool_recycles_address();
    test_pool_reinitializes_values();
    test_pool_stress_cycles();
    test_pool_overflow();
    test_pool_live_objects_independent();

    printf("\nAll Vec3 pool tests passed!\n");
    return 0;
}
