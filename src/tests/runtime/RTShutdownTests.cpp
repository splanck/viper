//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTShutdownTests.cpp
// Purpose: Verify that the runtime shutdown path correctly runs finalizers
//          on GC-tracked objects and cleans up the legacy context.
// Key invariants:
//   - rt_gc_run_all_finalizers invokes all registered finalizers exactly once
//   - Finalizer pointers are cleared after invocation (no double-finalize)
//   - rt_legacy_context_shutdown cleans up file state
// Ownership/Lifetime:
//   - Test objects are heap-allocated via rt_obj_new_i64; lifetimes managed
//     by the test.
// Links: src/runtime/core/rt_gc.c, src/runtime/core/rt_context.c
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "runtime/core/rt_context.h"
#include "runtime/core/rt_gc.h"
#include "runtime/core/rt_heap.h"
#include "runtime/oop/rt_object.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// ── vm_trap override ────────────────────────────────────────────────────────
// Prevent process exit on trap during tests.
static int g_trap_count = 0;

extern "C" void vm_trap(const char *msg)
{
    (void)msg;
    g_trap_count++;
}

// ── Finalizer tracking ──────────────────────────────────────────────────────

static int g_fin_a_count = 0;
static int g_fin_b_count = 0;
static int g_fin_c_count = 0;

static void finalizer_a(void *obj)
{
    (void)obj;
    g_fin_a_count++;
}

static void finalizer_b(void *obj)
{
    (void)obj;
    g_fin_b_count++;
}

static void finalizer_c(void *obj)
{
    (void)obj;
    g_fin_c_count++;
}

// No-op GC traverse (objects have no child references)
static void noop_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx)
{
    (void)obj;
    (void)visitor;
    (void)ctx;
}

// ── Test: rt_gc_run_all_finalizers invokes all finalizers ────────────────────

static void test_gc_finalizer_sweep()
{
    printf("  test_gc_finalizer_sweep ... ");

    g_fin_a_count = 0;
    g_fin_b_count = 0;
    g_fin_c_count = 0;

    // Create three GC-tracked objects with finalizers
    void *obj_a = rt_obj_new_i64(0, 64);
    void *obj_b = rt_obj_new_i64(0, 64);
    void *obj_c = rt_obj_new_i64(0, 64);

    rt_obj_set_finalizer(obj_a, finalizer_a);
    rt_obj_set_finalizer(obj_b, finalizer_b);
    rt_obj_set_finalizer(obj_c, finalizer_c);

    rt_gc_track(obj_a, noop_traverse);
    rt_gc_track(obj_b, noop_traverse);
    rt_gc_track(obj_c, noop_traverse);

    // All three should be tracked
    assert(rt_gc_is_tracked(obj_a));
    assert(rt_gc_is_tracked(obj_b));
    assert(rt_gc_is_tracked(obj_c));

    // Run the shutdown finalizer sweep
    rt_gc_run_all_finalizers();

    // All finalizers should have been called exactly once
    assert(g_fin_a_count == 1);
    assert(g_fin_b_count == 1);
    assert(g_fin_c_count == 1);

    // Objects should still be tracked (sweep doesn't untrack)
    assert(rt_gc_is_tracked(obj_a));

    printf("OK\n");

    // Cleanup: untrack and free
    rt_gc_untrack(obj_a);
    rt_gc_untrack(obj_b);
    rt_gc_untrack(obj_c);
    rt_heap_release(obj_a);
    rt_heap_release(obj_b);
    rt_heap_release(obj_c);
}

// ── Test: double-finalization prevention ────────────────────────────────────

static void test_gc_no_double_finalize()
{
    printf("  test_gc_no_double_finalize ... ");

    g_fin_a_count = 0;

    void *obj = rt_obj_new_i64(0, 64);
    rt_obj_set_finalizer(obj, finalizer_a);
    rt_gc_track(obj, noop_traverse);

    // First sweep: finalizer should run
    rt_gc_run_all_finalizers();
    assert(g_fin_a_count == 1);

    // Second sweep: finalizer pointer was cleared, should NOT run again
    rt_gc_run_all_finalizers();
    assert(g_fin_a_count == 1);

    printf("OK\n");

    rt_gc_untrack(obj);
    rt_heap_release(obj);
}

// ── Test: sweep on empty GC table ───────────────────────────────────────────

static void test_gc_sweep_empty()
{
    printf("  test_gc_sweep_empty ... ");

    // Should be a safe no-op
    int64_t count_before = rt_gc_tracked_count();
    rt_gc_run_all_finalizers();
    int64_t count_after = rt_gc_tracked_count();
    assert(count_before == count_after);

    printf("OK\n");
}

// ── Test: objects without finalizers are skipped ─────────────────────────────

static void test_gc_sweep_no_finalizer()
{
    printf("  test_gc_sweep_no_finalizer ... ");

    g_fin_a_count = 0;

    // Object A has a finalizer
    void *obj_a = rt_obj_new_i64(0, 64);
    rt_obj_set_finalizer(obj_a, finalizer_a);
    rt_gc_track(obj_a, noop_traverse);

    // Object B has no finalizer
    void *obj_b = rt_obj_new_i64(0, 64);
    rt_gc_track(obj_b, noop_traverse);

    rt_gc_run_all_finalizers();

    // Only A's finalizer should have run
    assert(g_fin_a_count == 1);

    printf("OK\n");

    rt_gc_untrack(obj_a);
    rt_gc_untrack(obj_b);
    rt_heap_release(obj_a);
    rt_heap_release(obj_b);
}

// ── Test: legacy context shutdown ───────────────────────────────────────────

static void test_legacy_context_shutdown()
{
    printf("  test_legacy_context_shutdown ... ");

    // Force legacy context initialization
    RtContext *legacy = rt_legacy_context();
    assert(legacy != nullptr);

    // Call shutdown — should not crash
    rt_legacy_context_shutdown();

    printf("OK\n");
}

// ── Main ────────────────────────────────────────────────────────────────────

int main()
{
    printf("RTShutdownTests:\n");

    test_gc_finalizer_sweep();
    test_gc_no_double_finalize();
    test_gc_sweep_empty();
    test_gc_sweep_no_finalizer();
    test_legacy_context_shutdown();

    printf("All shutdown tests passed.\n");
    return 0;
}
