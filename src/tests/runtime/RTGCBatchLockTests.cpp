//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTGCBatchLockTests.cpp
// Purpose: Verify GC cycle collection still works correctly after the lock
//          granularity optimization (batch locking in Phases 2-3).
// Key invariants:
//   - Cyclic references are still detected and collected.
//   - No double-free or use-after-free from the lock change.
//   - Non-cyclic objects are NOT collected.
// Links: src/runtime/core/rt_gc.c
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdio>
#include <cstring>

extern "C"
{
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"

    void vm_trap(const char *msg)
    {
        fprintf(stderr, "TRAP: %s\n", msg);
        rt_abort(msg);
    }
}

/// @brief Simple node for testing cycle detection.
struct test_node
{
    void *child;
    int id;
};

extern "C"
{
    static void test_node_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx)
    {
        struct test_node *node = (struct test_node *)obj;
        if (node->child)
            visitor(node->child, ctx);
    }
}

static void *make_node(int id)
{
    void *obj = rt_obj_new_i64(0, (int64_t)sizeof(struct test_node));
    struct test_node *n = (struct test_node *)obj;
    n->child = NULL;
    n->id = id;
    return obj;
}

static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Test: Self-referencing cycle is collected
//=============================================================================

static void test_self_cycle()
{
    printf("Testing self-cycle collection:\n");

    void *a = make_node(1);
    struct test_node *na = (struct test_node *)a;
    na->child = a; // self-cycle

    rt_gc_track(a, test_node_traverse);
    int64_t freed = rt_gc_collect();
    test_result("Self-cycle collected", freed == 1);

    printf("\n");
}

//=============================================================================
// Test: Two-node cycle is collected
//=============================================================================

static void test_two_node_cycle()
{
    printf("Testing two-node cycle collection:\n");

    void *a = make_node(1);
    void *b = make_node(2);
    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = a; // cycle: a → b → a

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    int64_t freed = rt_gc_collect();
    test_result("Two-node cycle collected (freed 2)", freed == 2);

    printf("\n");
}

//=============================================================================
// Test: Non-cyclic object is NOT collected
//=============================================================================

static void test_no_false_positive()
{
    printf("Testing non-cyclic object retention:\n");

    void *a = make_node(1);
    // No cycle — child is NULL
    rt_gc_track(a, test_node_traverse);

    int64_t freed = rt_gc_collect();
    test_result("Non-cyclic object retained", freed == 0);

    rt_gc_untrack(a);
    if (rt_obj_release_check0(a))
        rt_obj_free(a);

    printf("\n");
}

//=============================================================================
// Test: Multiple collections don't corrupt state
//=============================================================================

static void test_multiple_passes()
{
    printf("Testing multiple GC passes:\n");

    // Create and collect a cycle
    void *a = make_node(1);
    void *b = make_node(2);
    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = a;
    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    int64_t freed1 = rt_gc_collect();
    test_result("First pass collects cycle", freed1 == 2);

    // Second pass on empty table should be fine
    int64_t freed2 = rt_gc_collect();
    test_result("Second pass collects nothing", freed2 == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT GC Batch Lock Tests ===\n\n");

    test_self_cycle();
    test_two_node_cycle();
    test_no_false_positive();
    test_multiple_passes();

    printf("All GC batch lock tests passed!\n");
    return 0;
}
