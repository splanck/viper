//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTListI64Tests.cpp
// Purpose: Correctness tests for the unboxed int64 list (P2-3.7).
//
// Key properties verified:
//   - New list has len=0, cap >= requested
//   - Push appends elements with correct FIFO ordering
//   - Amortized growth preserves all previously pushed values
//   - Pop returns last element (LIFO), decrements len
//   - Peek returns last element without modifying len
//   - bounds-checked get/set work correctly on the populated list
//   - Refcounting retain/release are balanced
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_list_i64.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <cstdlib>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

// ============================================================================
// Construction
// ============================================================================

static void test_new_empty(void)
{
    int64_t *list = rt_list_i64_new(0);
    assert(list != nullptr);
    assert(rt_list_i64_len(list) == 0);
    assert(rt_list_i64_cap(list) >= 8); // minimum capacity

    rt_list_i64_release(list);
    printf("test_new_empty: PASSED\n");
}

static void test_new_with_cap(void)
{
    int64_t *list = rt_list_i64_new(64);
    assert(list != nullptr);
    assert(rt_list_i64_len(list) == 0);
    assert(rt_list_i64_cap(list) == 64);

    rt_list_i64_release(list);
    printf("test_new_with_cap: PASSED\n");
}

// ============================================================================
// Push — basic
// ============================================================================

static void test_push_basic(void)
{
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    int rc;
    rc = rt_list_i64_push(&list, 10); assert(rc == 0);
    rc = rt_list_i64_push(&list, 20); assert(rc == 0);
    rc = rt_list_i64_push(&list, 30); assert(rc == 0);

    assert(rt_list_i64_len(list) == 3);
    assert(rt_list_i64_get(list, 0) == 10);
    assert(rt_list_i64_get(list, 1) == 20);
    assert(rt_list_i64_get(list, 2) == 30);

    rt_list_i64_release(list);
    printf("test_push_basic: PASSED\n");
}

// ============================================================================
// Push — growth (forces buffer reallocation)
// ============================================================================

static void test_push_growth(void)
{
    // Start with capacity 4 to trigger multiple growth steps.
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    const int N = 200;
    for (int i = 0; i < N; i++)
    {
        int rc = rt_list_i64_push(&list, (int64_t)i * 3);
        assert(rc == 0);
    }

    assert(rt_list_i64_len(list) == (size_t)N);
    assert(rt_list_i64_cap(list) >= (size_t)N);

    // Verify all values are intact after multiple reallocations.
    for (int i = 0; i < N; i++)
        assert(rt_list_i64_get(list, (size_t)i) == (int64_t)i * 3);

    rt_list_i64_release(list);
    printf("test_push_growth: PASSED\n");
}

// ============================================================================
// Pop
// ============================================================================

static void test_pop_order(void)
{
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    rt_list_i64_push(&list, 1);
    rt_list_i64_push(&list, 2);
    rt_list_i64_push(&list, 3);

    assert(rt_list_i64_pop(&list) == 3);
    assert(rt_list_i64_len(list) == 2);
    assert(rt_list_i64_pop(&list) == 2);
    assert(rt_list_i64_len(list) == 1);
    assert(rt_list_i64_pop(&list) == 1);
    assert(rt_list_i64_len(list) == 0);

    rt_list_i64_release(list);
    printf("test_pop_order: PASSED\n");
}

static void test_pop_preserves_other_elements(void)
{
    int64_t *list = rt_list_i64_new(8);
    assert(list != nullptr);

    for (int64_t i = 0; i < 10; i++)
        rt_list_i64_push(&list, i);

    int64_t popped = rt_list_i64_pop(&list);
    assert(popped == 9);
    assert(rt_list_i64_len(list) == 9);

    // Remaining elements must be unchanged.
    for (size_t i = 0; i < 9; i++)
        assert(rt_list_i64_get(list, i) == (int64_t)i);

    rt_list_i64_release(list);
    printf("test_pop_preserves_other_elements: PASSED\n");
}

// ============================================================================
// Peek
// ============================================================================

static void test_peek(void)
{
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    rt_list_i64_push(&list, 42);
    rt_list_i64_push(&list, 99);

    // Peek must return last element without changing len.
    assert(rt_list_i64_peek(list) == 99);
    assert(rt_list_i64_len(list) == 2);
    assert(rt_list_i64_peek(list) == 99); // idempotent

    rt_list_i64_release(list);
    printf("test_peek: PASSED\n");
}

// ============================================================================
// Set (bounds-checked write)
// ============================================================================

static void test_set(void)
{
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    rt_list_i64_push(&list, 100);
    rt_list_i64_push(&list, 200);
    rt_list_i64_push(&list, 300);

    rt_list_i64_set(list, 1, -999);
    assert(rt_list_i64_get(list, 0) == 100);
    assert(rt_list_i64_get(list, 1) == -999);
    assert(rt_list_i64_get(list, 2) == 300);

    rt_list_i64_release(list);
    printf("test_set: PASSED\n");
}

// ============================================================================
// Refcounting
// ============================================================================

static void test_retain_release(void)
{
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    rt_list_i64_push(&list, 7);

    // Retain: now two references.
    rt_list_i64_retain(list);

    // Release one — list must still be alive.
    rt_list_i64_release(list);
    assert(rt_list_i64_len(list) == 1);
    assert(rt_list_i64_get(list, 0) == 7);

    // Release the last reference.
    rt_list_i64_release(list);

    printf("test_retain_release: PASSED\n");
}

// ============================================================================
// Negative values and edge values
// ============================================================================

static void test_edge_values(void)
{
    int64_t *list = rt_list_i64_new(4);
    assert(list != nullptr);

    rt_list_i64_push(&list, INT64_MIN);
    rt_list_i64_push(&list, INT64_MAX);
    rt_list_i64_push(&list, 0LL);
    rt_list_i64_push(&list, -1LL);

    assert(rt_list_i64_get(list, 0) == INT64_MIN);
    assert(rt_list_i64_get(list, 1) == INT64_MAX);
    assert(rt_list_i64_get(list, 2) == 0LL);
    assert(rt_list_i64_get(list, 3) == -1LL);

    rt_list_i64_release(list);
    printf("test_edge_values: PASSED\n");
}

// ============================================================================
// Entry point
// ============================================================================

int main(void)
{
    printf("=== rt_list_i64 Tests ===\n\n");

    test_new_empty();
    test_new_with_cap();
    test_push_basic();
    test_push_growth();
    test_pop_order();
    test_pop_preserves_other_elements();
    test_peek();
    test_set();
    test_retain_release();
    test_edge_values();

    printf("\nAll rt_list_i64 tests passed!\n");
    return 0;
}
