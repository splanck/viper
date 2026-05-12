//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// RTGCTests.cpp - Tests for rt_gc (cycle-detecting GC + zeroing weak refs)
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <string>

extern "C" {
#include "rt_gc.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_array_obj.h"
#include "rt_seq.h"

/// @brief Vm_trap.
void vm_trap(const char *msg) {
    fprintf(stderr, "TRAP: %s\n", msg);
    rt_abort(msg);
}

void rt_trap_set_recovery(jmp_buf *buf);
void rt_trap_clear_recovery(void);
const char *rt_trap_get_error(void);
void rt_weakref_reset(rt_weakref *ref, void *target);
}

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                                                          \
    do {                                                                                           \
        tests_run++;                                                                               \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg);                        \
        } else {                                                                                   \
            tests_passed++;                                                                        \
        }                                                                                          \
    } while (0)

//=============================================================================
// Test helpers
//=============================================================================

/// Simple test object: holds a pointer to another test object (child).
struct test_node {
    void *child; // Strong reference to another node (or NULL).
};

struct test_pair_node {
    void *child;
    void *extra;
};

static int g_cycle_finalizer_count = 0;
static int g_external_finalizer_count = 0;
static void *g_resurrected_object = NULL;

/// Traverse function for test_node: visits the child pointer.
extern "C" {
static void test_node_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    struct test_node *node = (struct test_node *)obj;
    if (node->child)
        visitor(node->child, ctx);
}

static void test_pair_node_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    struct test_pair_node *node = (struct test_pair_node *)obj;
    if (node->child)
        visitor(node->child, ctx);
    if (node->extra)
        visitor(node->extra, ctx);
}

static void gc_touching_traverse(void *obj, rt_gc_visitor_t visitor, void *ctx) {
    ASSERT(rt_gc_is_tracked(obj) == 1, "traverse can query GC tracking");
    test_node_traverse(obj, visitor, ctx);
}
}

static void *make_node() {
    void *obj = rt_obj_new_i64(0, (int64_t)sizeof(struct test_node));
    struct test_node *n = (struct test_node *)obj;
    n->child = NULL;
    return obj;
}

static void set_child_retained(void *obj, void *child) {
    struct test_node *node = (struct test_node *)obj;
    if (child)
        rt_obj_retain_maybe(child);
    node->child = child;
}

static void release_obj(void *obj) {
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void assert_weakref_get(rt_weakref *ref, void *expected, const char *message) {
    void *got = rt_weakref_get(ref);
    ASSERT(got == expected, message);
    release_obj(got);
}

static void assert_weak_load(void **slot, void *expected, const char *message) {
    void *got = rt_weak_load(slot);
    ASSERT(got == expected, message);
    release_obj(got);
}

static void count_cycle_finalizer(void *obj) {
    (void)obj;
    g_cycle_finalizer_count++;
}

static void count_external_finalizer(void *obj) {
    (void)obj;
    g_external_finalizer_count++;
}

static void resurrecting_finalizer(void *obj) {
    g_resurrected_object = obj;
    rt_obj_resurrect(obj);
}

static void trapping_finalizer(void *obj) {
    (void)obj;
    rt_trap("gc finalizer boom");
}

//=============================================================================
// GC Tracking Tests
//=============================================================================

static void test_track_untrack() {
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

static void test_track_null_safety() {
    rt_gc_track(NULL, test_node_traverse);
    rt_gc_track(make_node(), NULL);
    rt_gc_untrack(NULL);
    ASSERT(rt_gc_is_tracked(NULL) == 0, "null is not tracked");
}

static void test_track_rejects_non_object_payload() {
    void **arr = rt_arr_obj_new(0);
    ASSERT(arr != NULL, "object array allocated");

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        rt_gc_track(arr, test_node_traverse);
        rt_trap_clear_recovery();
        ASSERT(0, "tracking arrays for object GC should trap");
    } else {
        std::string message = rt_trap_get_error();
        rt_trap_clear_recovery();
        ASSERT(message.find("not a heap object") != std::string::npos,
               "non-object GC track trap mentions heap object");
    }

    rt_arr_obj_release(arr);
}

static void test_tracked_count() {
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

static void test_double_track() {
    void *obj = make_node();
    int64_t base = rt_gc_tracked_count();

    rt_gc_track(obj, test_node_traverse);
    /// @brief Rt_gc_track.
    rt_gc_track(obj, test_node_traverse); // should not duplicate

    ASSERT(rt_gc_tracked_count() == base + 1, "double track doesn't duplicate");

    rt_gc_untrack(obj);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

//=============================================================================
// Weak Reference Tests
//=============================================================================

static void test_weakref_basic() {
    void *obj = make_node();
    rt_weakref *ref = rt_weakref_new(obj);

    ASSERT(ref != NULL, "weakref created");
    assert_weakref_get(ref, obj, "weakref returns target");
    ASSERT(rt_weakref_alive(ref) == 1, "weakref alive");

    rt_weakref_free(ref);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_weakref_null_target() {
    rt_weakref *ref = rt_weakref_new(NULL);
    ASSERT(ref != NULL, "weakref with null target created");
    assert_weakref_get(ref, NULL, "weakref to null returns null");
    ASSERT(rt_weakref_alive(ref) == 0, "weakref to null not alive");
    rt_weakref_free(ref);
}

static void test_weakref_null_ref() {
    assert_weakref_get(NULL, NULL, "get(null) = null");
    ASSERT(rt_weakref_alive(NULL) == 0, "alive(null) = 0");
    /// @brief Rt_weakref_free.
    rt_weakref_free(NULL); // should not crash
    ASSERT(1, "free(null) no crash");
}

static void test_weakref_clear_on_free() {
    void *obj = make_node();
    rt_weakref *ref1 = rt_weakref_new(obj);
    rt_weakref *ref2 = rt_weakref_new(obj);

    assert_weakref_get(ref1, obj, "ref1 alive before clear");
    assert_weakref_get(ref2, obj, "ref2 alive before clear");

    // Simulate object being freed - clear weak refs
    rt_gc_clear_weak_refs(obj);

    assert_weakref_get(ref1, NULL, "ref1 cleared");
    assert_weakref_get(ref2, NULL, "ref2 cleared");
    ASSERT(rt_weakref_alive(ref1) == 0, "ref1 not alive");
    ASSERT(rt_weakref_alive(ref2) == 0, "ref2 not alive");

    rt_weakref_free(ref1);
    rt_weakref_free(ref2);
    if (rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static void test_weakref_free_unregisters() {
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

static void test_weakref_reset_after_clear() {
    void *old_target = make_node();
    void *new_target = make_node();
    rt_weakref *ref1 = rt_weakref_new(old_target);
    rt_weakref *ref2 = rt_weakref_new(old_target);

    rt_gc_clear_weak_refs(old_target);
    assert_weakref_get(ref1, NULL, "ref1 cleared before reset");
    assert_weakref_get(ref2, NULL, "ref2 cleared before reset");

    rt_weakref_reset(ref1, new_target);
    rt_weakref_reset(ref2, new_target);
    assert_weakref_get(ref1, new_target, "ref1 reset to new target");
    assert_weakref_get(ref2, new_target, "ref2 reset to new target");

    rt_gc_clear_weak_refs(new_target);
    assert_weakref_get(ref1, NULL, "ref1 cleared after reset target freed");
    assert_weakref_get(ref2, NULL, "ref2 cleared after reset target freed");

    rt_weakref_free(ref1);
    rt_weakref_free(ref2);
    release_obj(old_target);
    release_obj(new_target);
}

static void test_weakref_survives_finalizer_resurrection() {
    g_resurrected_object = NULL;
    void *obj = make_node();
    rt_weakref *ref = rt_weakref_new(obj);
    rt_obj_set_finalizer(obj, resurrecting_finalizer);

    release_obj(obj);
    ASSERT(g_resurrected_object == obj, "finalizer resurrected object");
    ASSERT(rt_heap_is_payload(obj) == 1, "resurrected object remains live");
    assert_weakref_get(ref, obj, "weak ref still points at resurrected object");

    release_obj(obj);
    ASSERT(rt_heap_is_payload(obj) == 0, "second release frees resurrected object");
    assert_weakref_get(ref, NULL, "weak ref cleared after real free");
    rt_weakref_free(ref);
}

//=============================================================================
// Cycle Collection Tests
//=============================================================================

static void test_collect_empty() {
    int64_t base_count = rt_gc_tracked_count();
    // Make sure no tracked objects exist beyond the base
    int64_t freed = rt_gc_collect();
    // freed might be 0 if nothing is cyclic
    ASSERT(freed >= 0, "collect on empty returns >= 0");
    ASSERT(rt_gc_pass_count() > 0, "pass count incremented");
}

static void test_collect_no_cycle() {
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

static void test_collect_simple_cycle() {
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

static void test_collect_self_cycle() {
    // a -> a (self-referencing)
    void *a = make_node();
    struct test_node *na = (struct test_node *)a;
    na->child = a;

    rt_gc_track(a, test_node_traverse);

    // trial_rc starts at 1, a->a decrements to 0 -> freed
    int64_t freed = rt_gc_collect();
    ASSERT(freed == 1, "self-cycle freed");
}

static void test_collect_preserves_reachable() {
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

static void test_collect_preserves_cycle_with_extra_external_ref() {
    void *a = make_node();
    void *b = make_node();

    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = a;

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);
    rt_obj_retain_maybe(a); // extra external retain must keep the cycle reachable

    int64_t freed = rt_gc_collect();
    ASSERT(freed == 0, "cycle with extra external retain is preserved");
    ASSERT(rt_gc_is_tracked(a) == 1, "a remains tracked");
    ASSERT(rt_gc_is_tracked(b) == 1, "b remains tracked");

    rt_gc_untrack(a);
    rt_gc_untrack(b);
    na->child = NULL;
    nb->child = NULL;
    release_obj(a); // drop extra retain
    release_obj(a); // drop initial retain
    release_obj(b);
}

static void test_promoted_root_restores_young_child() {
    void *parent = make_node();
    rt_gc_track(parent, test_node_traverse);

    for (int i = 0; i < 10; ++i)
        ASSERT(rt_gc_collect() == 0, "promotion warmup keeps parent");
    while ((rt_gc_pass_count() % 16) == 0)
        ASSERT(rt_gc_collect() == 0, "advance to non-full promoted pass");

    g_external_finalizer_count = 0;
    void *child = make_node();
    rt_obj_set_finalizer(child, count_external_finalizer);
    set_child_retained(parent, child);
    rt_gc_track(child, test_node_traverse);
    release_obj(child);

    int64_t freed = rt_gc_collect();
    ASSERT(freed == 0, "promoted root restores reachable young child");
    ASSERT(rt_heap_is_payload(child) == 1, "young child remains live");
    ASSERT(g_external_finalizer_count == 0, "young child finalizer did not run");

    rt_gc_untrack(child);
    rt_gc_untrack(parent);
    struct test_node *np = (struct test_node *)parent;
    np->child = NULL;
    release_obj(child);
    release_obj(parent);
}

static void test_weakref_cleared_by_collect() {
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
    assert_weakref_get(ref_a, NULL, "ref_a null after collect");
    assert_weakref_get(ref_b, NULL, "ref_b null after collect");

    rt_weakref_free(ref_a);
    rt_weakref_free(ref_b);
}

static void test_weak_field_zeroed_by_collect() {
    void *a = make_node();
    void *b = make_node();
    void *slot = NULL;

    struct test_node *na = (struct test_node *)a;
    struct test_node *nb = (struct test_node *)b;
    na->child = b;
    nb->child = a;

    rt_weak_store(&slot, a);
    assert_weak_load(&slot, a, "weak field returns live target before collect");

    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    int64_t freed = rt_gc_collect();
    ASSERT(freed == 2, "cycle with weak field collected");
    assert_weak_load(&slot, NULL, "weak field cleared after collect");

    rt_weak_store(&slot, NULL);
}

static void test_collect_reclaims_cycle_storage_and_finalizers() {
    g_cycle_finalizer_count = 0;

    void *a = make_node();
    void *b = make_node();
    rt_obj_set_finalizer(a, count_cycle_finalizer);
    rt_obj_set_finalizer(b, count_cycle_finalizer);

    set_child_retained(a, b);
    set_child_retained(b, a);

    rt_weakref *ref_a = rt_weakref_new(a);
    rt_weakref *ref_b = rt_weakref_new(b);
    rt_gc_track(a, test_node_traverse);
    rt_gc_track(b, test_node_traverse);

    ASSERT(rt_obj_release_check0(a) == 0, "drop external a leaves cycle ref");
    ASSERT(rt_obj_release_check0(b) == 0, "drop external b leaves cycle ref");

    int64_t initial_collected = rt_gc_total_collected();
    int64_t freed = rt_gc_collect();

    ASSERT(freed == 2, "retained cycle storage reclaimed");
    ASSERT(rt_gc_total_collected() == initial_collected + 2, "total_collected counts actual frees");
    ASSERT(g_cycle_finalizer_count == 2, "cycle finalizers run");
    ASSERT(rt_heap_is_payload(a) == 0, "a removed from heap registry");
    ASSERT(rt_heap_is_payload(b) == 0, "b removed from heap registry");
    assert_weakref_get(ref_a, NULL, "weak ref a zeroed by reclaim");
    assert_weakref_get(ref_b, NULL, "weak ref b zeroed by reclaim");

    rt_weakref_free(ref_a);
    rt_weakref_free(ref_b);
}

static void test_collect_releases_untracked_external_children() {
    g_external_finalizer_count = 0;
    g_cycle_finalizer_count = 0;

    void *a = rt_obj_new_i64(0, (int64_t)sizeof(struct test_pair_node));
    void *b = rt_obj_new_i64(0, (int64_t)sizeof(struct test_pair_node));
    void *external = make_node();
    rt_obj_set_finalizer(a, count_cycle_finalizer);
    rt_obj_set_finalizer(external, count_external_finalizer);

    struct test_pair_node *na = (struct test_pair_node *)a;
    struct test_pair_node *nb = (struct test_pair_node *)b;
    na->child = b;
    rt_obj_retain_maybe(b);
    nb->child = a;
    rt_obj_retain_maybe(a);
    na->extra = external;
    rt_obj_retain_maybe(external);

    rt_gc_track(a, test_pair_node_traverse);
    rt_gc_track(b, test_pair_node_traverse);

    ASSERT(rt_obj_release_check0(a) == 0, "drop external a leaves cycle ref");
    ASSERT(rt_obj_release_check0(b) == 0, "drop external b leaves cycle ref");
    ASSERT(rt_obj_release_check0(external) == 0, "external retained only by cycle");

    int64_t freed = rt_gc_collect();
    ASSERT(freed == 2, "cycle members freed");
    ASSERT(g_cycle_finalizer_count == 1, "cycle member finalizer runs");
    ASSERT(g_external_finalizer_count == 1, "external child released by cycle collection");
    ASSERT(rt_heap_is_payload(external) == 0, "external child freed");
}

static void test_traverse_can_touch_gc_without_deadlock() {
    void *a = make_node();
    rt_gc_track(a, gc_touching_traverse);
    int64_t freed = rt_gc_collect();
    ASSERT(freed == 0, "live node not collected");
    rt_gc_untrack(a);
    release_obj(a);
}

static void test_collecting_flag_cleared_after_finalizer_trap() {
    void *a = make_node();
    rt_obj_set_finalizer(a, trapping_finalizer);
    struct test_node *na = (struct test_node *)a;
    na->child = a;
    rt_gc_track(a, test_node_traverse);

    jmp_buf recovery;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0) {
        (void)rt_gc_collect();
        rt_trap_clear_recovery();
        ASSERT(0, "GC finalizer trap should be recovered");
    } else {
        std::string message = rt_trap_get_error();
        rt_trap_clear_recovery();
        ASSERT(message.find("gc finalizer boom") != std::string::npos,
               "GC finalizer trap is propagated");
    }

    int64_t passes = rt_gc_pass_count();
    (void)rt_gc_collect();
    ASSERT(rt_gc_pass_count() > passes, "collecting flag cleared after trap");
    if (rt_heap_is_payload(a))
        rt_heap_free_zero_ref(a);
}

//=============================================================================
// Statistics Tests
//=============================================================================

static void test_statistics() {
    int64_t initial_collected = rt_gc_total_collected();
    int64_t initial_passes = rt_gc_pass_count();

    // Run a collect
    rt_gc_collect();

    ASSERT(rt_gc_pass_count() > initial_passes, "pass count increases");
    ASSERT(rt_gc_total_collected() >= initial_collected, "total_collected >= initial");
}

static void test_threshold_get_set_contract() {
    rt_gc_set_threshold(-100);
    ASSERT(rt_gc_get_threshold() == 0, "negative threshold disables auto GC");
    rt_gc_set_threshold(13);
    ASSERT(rt_gc_get_threshold() == 13, "positive threshold is reported");
    rt_gc_set_threshold(0);
    ASSERT(rt_gc_get_threshold() == 0, "zero threshold disables auto GC");
}

static void test_shutdown_resets_statistics() {
    (void)rt_gc_collect();
    ASSERT(rt_gc_pass_count() > 0, "pass count non-zero before shutdown");
    rt_gc_shutdown();
    ASSERT(rt_gc_tracked_count() == 0, "tracked count reset by shutdown");
    ASSERT(rt_gc_pass_count() == 0, "pass count reset by shutdown");
    ASSERT(rt_gc_total_collected() == 0, "total collected reset by shutdown");
}

//=============================================================================
// Main
//=============================================================================

int main() {
    // Tracking
    test_track_untrack();
    test_track_null_safety();
    test_track_rejects_non_object_payload();
    test_tracked_count();
    test_double_track();

    // Weak references
    test_weakref_basic();
    test_weakref_null_target();
    test_weakref_null_ref();
    test_weakref_clear_on_free();
    test_weakref_free_unregisters();
    test_weakref_reset_after_clear();
    test_weakref_survives_finalizer_resurrection();

    // Cycle collection
    test_collect_empty();
    test_collect_no_cycle();
    test_collect_simple_cycle();
    test_collect_self_cycle();
    test_collect_preserves_reachable();
    test_collect_preserves_cycle_with_extra_external_ref();
    test_promoted_root_restores_young_child();
    test_weakref_cleared_by_collect();
    test_weak_field_zeroed_by_collect();
    test_collect_reclaims_cycle_storage_and_finalizers();
    test_collect_releases_untracked_external_children();
    test_traverse_can_touch_gc_without_deadlock();
    test_collecting_flag_cleared_after_finalizer_trap();

    // Statistics
    test_statistics();
    test_threshold_get_set_contract();
    test_shutdown_resets_statistics();

    printf("GC tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
