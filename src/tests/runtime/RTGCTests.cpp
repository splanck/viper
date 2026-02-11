//===----------------------------------------------------------------------===//
// RTGCTests.cpp - Tests for rt_gc (cycle-detecting GC + zeroing weak refs)
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C"
{
#include "rt_gc.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"

    void vm_trap(const char *msg)
    {
        fprintf(stderr, "TRAP: %s\n", msg);
        rt_abort(msg);
    }
}

static int tests_run = 0;
static int tests_passed = 0;

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

//=============================================================================
// Test helpers
//=============================================================================

/// Simple test object: holds a pointer to another test object (child).
struct test_node
{
    void *child; // Strong reference to another node (or NULL).
};

/// Traverse function for test_node: visits the child pointer.
extern "C"
{
    static void test_node_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx)
    {
        struct test_node *node = (struct test_node *)obj;
        if (node->child)
            visitor(node->child, ctx);
    }
}

static void *make_node()
{
    void *obj = rt_obj_new_i64(0, (int64_t)sizeof(struct test_node));
    struct test_node *n = (struct test_node *)obj;
    n->child = NULL;
    return obj;
}

//=============================================================================
// GC Tracking Tests
//=============================================================================

static void test_track_untrack()
{
    void *obj = make_node();

    ASSERT(rt_gc_is_tracked(obj) == 0, "not tracked initially");

    rt_gc_track(obj, test_node_traverse);
    ASSERT(rt_gc_is_tracked(obj) == 1, "tracked after track()");

    rt_gc_untrack(obj);
    ASSERT(rt_gc_is_tracked(obj) == 0, "untracked after untrack()");

    // Clean up
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_track_null_safety()
{
    rt_gc_track(NULL, test_node_traverse);
    rt_gc_track(make_node(), NULL);
    rt_gc_untrack(NULL);
    ASSERT(rt_gc_is_tracked(NULL) == 0, "null is not tracked");
}

static void test_tracked_count()
{
    int64_t base = rt_gc_tracked_count();

    void *a = make_node();
    void *b = make_node();
    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    ASSERT(rt_gc_tracked_count() == base + 2, "count after tracking 2");

    rt_gc_untrack(a);
    ASSERT(rt_gc_tracked_count() == base + 1, "count after untracking 1");

    rt_gc_untrack(b);
    ASSERT(rt_gc_tracked_count() == base, "count back to base");

    if (rt_obj_release_check0(a))
        rt_obj_free(a);
    if (rt_obj_release_check0(b))
        rt_obj_free(b);
}

static void test_double_track()
{
    void *obj = make_node();
    int64_t base = rt_gc_tracked_count();

    rt_gc_track(obj, test_node_traverse);
    rt_gc_track(obj, test_node_traverse); // should not duplicate

    ASSERT(rt_gc_tracked_count() == base + 1, "double track doesn't duplicate");

    rt_gc_untrack(obj);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

//=============================================================================
// Weak Reference Tests
//=============================================================================

static void test_weakref_basic()
{
    void *obj = make_node();
    rt_weakref *ref = rt_weakref_new(obj);

    ASSERT(ref != NULL, "weakref created");
    ASSERT(rt_weakref_get(ref) == obj, "weakref returns target");
    ASSERT(rt_weakref_alive(ref) == 1, "weakref alive");

    rt_weakref_free(ref);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_weakref_null_target()
{
    rt_weakref *ref = rt_weakref_new(NULL);
    ASSERT(ref != NULL, "weakref with null target created");
    ASSERT(rt_weakref_get(ref) == NULL, "weakref to null returns null");
    ASSERT(rt_weakref_alive(ref) == 0, "weakref to null not alive");
    rt_weakref_free(ref);
}

static void test_weakref_null_ref()
{
    ASSERT(rt_weakref_get(NULL) == NULL, "get(null) = null");
    ASSERT(rt_weakref_alive(NULL) == 0, "alive(null) = 0");
    rt_weakref_free(NULL); // should not crash
    ASSERT(1, "free(null) no crash");
}

static void test_weakref_clear_on_free()
{
    void *obj = make_node();
    rt_weakref *ref1 = rt_weakref_new(obj);
    rt_weakref *ref2 = rt_weakref_new(obj);

    ASSERT(rt_weakref_get(ref1) == obj, "ref1 alive before clear");
    ASSERT(rt_weakref_get(ref2) == obj, "ref2 alive before clear");

    // Simulate object being freed - clear weak refs
    rt_gc_clear_weak_refs(obj);

    ASSERT(rt_weakref_get(ref1) == NULL, "ref1 cleared");
    ASSERT(rt_weakref_get(ref2) == NULL, "ref2 cleared");
    ASSERT(rt_weakref_alive(ref1) == 0, "ref1 not alive");
    ASSERT(rt_weakref_alive(ref2) == 0, "ref2 not alive");

    rt_weakref_free(ref1);
    rt_weakref_free(ref2);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_weakref_free_unregisters()
{
    void *obj = make_node();
    rt_weakref *ref = rt_weakref_new(obj);

    // Free the weak ref before the object
    rt_weakref_free(ref);

    // Clearing weak refs for this target should not crash
    rt_gc_clear_weak_refs(obj);
    ASSERT(1, "clear after weakref_free no crash");

    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

//=============================================================================
// Cycle Collection Tests
//=============================================================================

static void test_collect_empty()
{
    int64_t base_count = rt_gc_tracked_count();
    // Make sure no tracked objects exist beyond the base
    int64_t freed = rt_gc_collect();
    // freed might be 0 if nothing is cyclic
    ASSERT(freed >= 0, "collect on empty returns >= 0");
    ASSERT(rt_gc_pass_count() > 0, "pass count incremented");
}

static void test_collect_no_cycle()
{
    // Linear chain: a -> b -> c (no cycle)
    void *a = make_node();
    void *b = make_node();
    void *c = make_node();

    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = c;

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);
    rt_gc_track(c, test_node_traverse);

    int64_t freed = rt_gc_collect();
    // These objects are all tracked with trial_rc starting at 1.
    // a->b means b gets trial_rc decremented to 0
    // b->c means c gets trial_rc decremented to 0
    // a keeps trial_rc 1 (nothing points to it within tracked set)
    // So a is reachable, and it reaches b and c -> all reachable
    // freed should be 0
    ASSERT(freed == 0, "no cycle -> nothing freed");

    rt_gc_untrack(a);
    rt_gc_untrack(b);
    rt_gc_untrack(c);

    if (rt_obj_release_check0(c))
        rt_obj_free(c);
    if (rt_obj_release_check0(b))
        rt_obj_free(b);
    if (rt_obj_release_check0(a))
        rt_obj_free(a);
}

static void test_collect_simple_cycle()
{
    // a -> b -> a (cycle, no external references)
    void *a = make_node();
    void *b = make_node();

    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = a;

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    // Both start at trial_rc=1.
    // a->b: b's trial_rc -> 0
    // b->a: a's trial_rc -> 0
    // Neither has trial_rc > 0 -> both are white -> both freed
    int64_t freed = rt_gc_collect();
    ASSERT(freed == 2, "2-node cycle freed");
    ASSERT(rt_gc_is_tracked(a) == 0, "a untracked after collection");
    ASSERT(rt_gc_is_tracked(b) == 0, "b untracked after collection");
}

static void test_collect_self_cycle()
{
    // a -> a (self-referencing)
    void *a = make_node();
    struct test_node *na = (struct test_node *)a;
    na->child = a;

    rt_gc_track(a, test_node_traverse);

    // trial_rc starts at 1, a->a decrements to 0 -> freed
    int64_t freed = rt_gc_collect();
    ASSERT(freed == 1, "self-cycle freed");
}

static void test_collect_preserves_reachable()
{
    // a -> b -> c -> b (b-c cycle, but a has external ref via trial_rc=1)
    void *a = make_node();
    void *b = make_node();
    void *c = make_node();

    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    struct test_node *nc = (struct test_node *)c;
    na->child = b;
    nb->child = c;
    nc->child = b; // cycle between b and c

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);
    rt_gc_track(c, test_node_traverse);

    // trial_rc: a=1, b=1, c=1
    // After decrements: a->b: b=0; b->c: c=0; c->b: b=-1
    // a has trial_rc=1 -> black -> mark reachable children
    // a reaches b -> b becomes black -> b reaches c -> c becomes black
    // All reachable -> freed = 0
    int64_t freed = rt_gc_collect();
    ASSERT(freed == 0, "cycle reachable from external -> not freed");

    rt_gc_untrack(a);
    rt_gc_untrack(b);
    rt_gc_untrack(c);

    if (rt_obj_release_check0(c))
        rt_obj_free(c);
    if (rt_obj_release_check0(b))
        rt_obj_free(b);
    if (rt_obj_release_check0(a))
        rt_obj_free(a);
}

static void test_weakref_cleared_by_collect()
{
    // Create a cycle and weak ref to one of the nodes
    void *a = make_node();
    void *b = make_node();

    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = a;

    rt_weakref *ref_a = rt_weakref_new(a);
    rt_weakref *ref_b = rt_weakref_new(b);

    ASSERT(rt_weakref_alive(ref_a) == 1, "ref_a alive before collect");
    ASSERT(rt_weakref_alive(ref_b) == 1, "ref_b alive before collect");

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    int64_t freed = rt_gc_collect();
    ASSERT(freed == 2, "cycle freed");

    ASSERT(rt_weakref_alive(ref_a) == 0, "ref_a dead after collect");
    ASSERT(rt_weakref_alive(ref_b) == 0, "ref_b dead after collect");
    ASSERT(rt_weakref_get(ref_a) == NULL, "ref_a null after collect");
    ASSERT(rt_weakref_get(ref_b) == NULL, "ref_b null after collect");

    rt_weakref_free(ref_a);
    rt_weakref_free(ref_b);
}

//=============================================================================
// Statistics Tests
//=============================================================================

static void test_statistics()
{
    int64_t initial_collected = rt_gc_total_collected();
    int64_t initial_passes = rt_gc_pass_count();

    // Run a collect
    rt_gc_collect();

    ASSERT(rt_gc_pass_count() > initial_passes, "pass count increases");
    ASSERT(rt_gc_total_collected() >= initial_collected, "total_collected >= initial");
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    // Tracking
    test_track_untrack();
    test_track_null_safety();
    test_tracked_count();
    test_double_track();

    // Weak references
    test_weakref_basic();
    test_weakref_null_target();
    test_weakref_null_ref();
    test_weakref_clear_on_free();
    test_weakref_free_unregisters();

    // Cycle collection
    test_collect_empty();
    test_collect_no_cycle();
    test_collect_simple_cycle();
    test_collect_self_cycle();
    test_collect_preserves_reachable();
    test_weakref_cleared_by_collect();

    // Statistics
    test_statistics();

    printf("GC tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
