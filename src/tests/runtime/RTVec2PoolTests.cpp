//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTVec2PoolTests.cpp
// Purpose: Correctness tests for the Vec2 thread-local free-list pool (P2-3.6).
//
// Key properties verified:
//   - Pool recycles: releasing then re-allocating returns the same address
//   - Values are freshly initialized (no stale data from pool reuse)
//   - Many alloc/release cycles don't corrupt memory
//   - Pool overflow: more releases than capacity are freed normally
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_vec2.h"

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

/// Release a Vec2 object, triggering the finalizer / pool return.
static void vec2_release(void *v)
{
    if (rt_obj_release_check0(v))
        rt_obj_free(v);
}

// ============================================================================
// Pool recycling: same address returned after release
// ============================================================================

static void test_pool_recycles_address(void)
{
    void *first = rt_vec2_new(1.0, 2.0);
    assert(first != nullptr);
    void *saved = first;

    // Release — finalizer should pool this allocation.
    vec2_release(first);

    // Next allocation must come from the pool (same address, single-threaded).
    void *second = rt_vec2_new(10.0, 20.0);
    assert(second == saved && "pool should recycle the released allocation");

    // Values must be freshly initialized (no stale x=1, y=2).
    assert(approx_eq(rt_vec2_x(second), 10.0));
    assert(approx_eq(rt_vec2_y(second), 20.0));

    vec2_release(second);
    printf("test_pool_recycles_address: PASSED\n");
}

// ============================================================================
// Pool re-initializes: stale fields from previous use are overwritten
// ============================================================================

static void test_pool_reinitializes_values(void)
{
    void *v1 = rt_vec2_new(99.0, -99.0);
    assert(v1 != nullptr);
    vec2_release(v1); // back to pool

    void *v2 = rt_vec2_new(0.5, 0.5);
    assert(v2 != nullptr);
    // Old values (99, -99) must be gone.
    assert(approx_eq(rt_vec2_x(v2), 0.5));
    assert(approx_eq(rt_vec2_y(v2), 0.5));
    vec2_release(v2);

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
        void *v = rt_vec2_new(x, y);
        assert(v != nullptr);
        assert(approx_eq(rt_vec2_x(v), x));
        assert(approx_eq(rt_vec2_y(v), y));
        vec2_release(v);
    }
    printf("test_pool_stress_cycles: PASSED\n");
}

// ============================================================================
// Pool overflow: releasing more than capacity drains gracefully
// ============================================================================

// VEC2_POOL_CAPACITY is 32; release 40 to fill the pool and spill 8 to free.
static const int kPoolOverflowCount = 40;

static void test_pool_overflow(void)
{
    void *objs[kPoolOverflowCount];
    for (int i = 0; i < kPoolOverflowCount; i++)
        objs[i] = rt_vec2_new((double)i, (double)i);

    // Release all — pool captures first 32, the remaining 8 are freed normally.
    for (int i = kPoolOverflowCount - 1; i >= 0; i--)
        vec2_release(objs[i]);

    // Now allocate kPoolOverflowCount new objects; pool supplies up to 32.
    for (int i = 0; i < kPoolOverflowCount; i++)
    {
        void *v = rt_vec2_new(7.0, 8.0);
        assert(v != nullptr);
        assert(approx_eq(rt_vec2_x(v), 7.0));
        assert(approx_eq(rt_vec2_y(v), 8.0));
        vec2_release(v);
    }

    printf("test_pool_overflow: PASSED\n");
}

// ============================================================================
// Multiple alive objects don't interfere with each other via the pool
// ============================================================================

static void test_pool_live_objects_independent(void)
{
    void *a = rt_vec2_new(1.0, 0.0);
    void *b = rt_vec2_new(0.0, 1.0);
    void *c = rt_vec2_new(3.0, 4.0);

    // All three must be distinct and retain correct values.
    assert(a != b && b != c && a != c);
    assert(approx_eq(rt_vec2_x(a), 1.0) && approx_eq(rt_vec2_y(a), 0.0));
    assert(approx_eq(rt_vec2_x(b), 0.0) && approx_eq(rt_vec2_y(b), 1.0));
    assert(approx_eq(rt_vec2_x(c), 3.0) && approx_eq(rt_vec2_y(c), 4.0));

    vec2_release(a);
    vec2_release(b);
    vec2_release(c);

    printf("test_pool_live_objects_independent: PASSED\n");
}

// ============================================================================
// Entry point
// ============================================================================

int main(void)
{
    printf("=== Vec2 Pool Tests ===\n\n");

    test_pool_recycles_address();
    test_pool_reinitializes_values();
    test_pool_stress_cycles();
    test_pool_overflow();
    test_pool_live_objects_independent();

    printf("\nAll Vec2 pool tests passed!\n");
    return 0;
}
