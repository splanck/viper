//===----------------------------------------------------------------------===//
// File: tests/runtime/RTNullCheckTests.c
// Purpose: Verify that runtime collection constructors and weak reference
//          operations handle OOM gracefully (where testable without injecting
//          failures) and produce correct behavior on valid inputs.
//
// Bugs addressed:
//   R-09: rt_bloomfilter_new — bits calloc not null-checked
//   R-10: rt_defaultmap_new / dm_resize — bucket calloc not null-checked
//   R-01: rt_weak_store / rt_weak_load — addr not null-checked
//   R-03: rt_concqueue_enqueue — malloc not null-checked
//===----------------------------------------------------------------------===//

#include "rt_bloomfilter.h"
#include "rt_concqueue.h"
#include "rt_defaultmap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (!(cond))                                                                               \
        {                                                                                          \
            tests_failed++;                                                                        \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                       \
        }                                                                                          \
    } while (0)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static rt_string make_str(const char *s)
{
    return rt_string_from_bytes(s, strlen(s));
}

// ---------------------------------------------------------------------------
// R-09: rt_bloomfilter_new normal-path tests
// Validates that the fix did not break correct allocation and use.
// ---------------------------------------------------------------------------

static void test_bloomfilter_new_returns_non_null(void)
{
    void *bf = rt_bloomfilter_new(100, 0.01);
    ASSERT(bf != NULL);
    ASSERT(rt_bloomfilter_count(bf) == 0);
}

static void test_bloomfilter_add_and_query(void)
{
    void *bf = rt_bloomfilter_new(50, 0.05);
    ASSERT(bf != NULL);

    rt_string hello = make_str("hello");
    rt_string world = make_str("world");
    rt_string absent = make_str("absent_value_xyz");

    rt_bloomfilter_add(bf, hello);
    rt_bloomfilter_add(bf, world);

    ASSERT(rt_bloomfilter_count(bf) == 2);
    ASSERT(rt_bloomfilter_might_contain(bf, hello) == 1);
    ASSERT(rt_bloomfilter_might_contain(bf, world) == 1);
    // Absent value: bloom filter may give false positives but should not false negative.
    // Just verify the function returns without crashing.
    (void)rt_bloomfilter_might_contain(bf, absent);
}

static void test_bloomfilter_clear_resets_count(void)
{
    void *bf = rt_bloomfilter_new(20, 0.01);
    ASSERT(bf != NULL);

    rt_string s = make_str("item");
    rt_bloomfilter_add(bf, s);
    ASSERT(rt_bloomfilter_count(bf) == 1);

    rt_bloomfilter_clear(bf);
    ASSERT(rt_bloomfilter_count(bf) == 0);
    ASSERT(rt_bloomfilter_might_contain(bf, s) == 0);
}

static void test_bloomfilter_edge_params_clamped(void)
{
    // expected_items < 1 is clamped to 1; fpr out-of-range is clamped.
    void *bf1 = rt_bloomfilter_new(0, 0.01);
    ASSERT(bf1 != NULL);

    void *bf2 = rt_bloomfilter_new(10, -1.0);
    ASSERT(bf2 != NULL);

    void *bf3 = rt_bloomfilter_new(10, 2.0);
    ASSERT(bf3 != NULL);
}

// ---------------------------------------------------------------------------
// R-10: rt_defaultmap_new / dm_resize normal-path tests
// ---------------------------------------------------------------------------

static void test_defaultmap_new_returns_non_null(void)
{
    rt_string def = make_str("default");
    void *m = rt_defaultmap_new(def);
    ASSERT(m != NULL);
    ASSERT(rt_defaultmap_len(m) == 0);
}

static void test_defaultmap_get_returns_default_for_missing_key(void)
{
    rt_string def = make_str("DEFAULT");
    void *m = rt_defaultmap_new(def);
    ASSERT(m != NULL);

    rt_string key = make_str("nonexistent");
    void *got = rt_defaultmap_get(m, key);
    // Must equal the default value pointer
    ASSERT(got == (void *)def);
}

static void test_defaultmap_set_and_get(void)
{
    rt_string def = make_str("def");
    void *m = rt_defaultmap_new(def);
    ASSERT(m != NULL);

    rt_string k = make_str("key1");
    rt_string v = make_str("value1");
    rt_defaultmap_set(m, k, (void *)v);

    ASSERT(rt_defaultmap_len(m) == 1);
    ASSERT(rt_defaultmap_has(m, k) == 1);

    void *got = rt_defaultmap_get(m, k);
    ASSERT(got == (void *)v);
}

static void test_defaultmap_resize_via_many_inserts(void)
{
    // Insert more than 12 entries (75% of initial capacity 16) to trigger dm_resize.
    rt_string def = make_str("0");
    void *m = rt_defaultmap_new(def);
    ASSERT(m != NULL);

    char key_buf[16];
    char val_buf[16];
    int count = 20;
    for (int i = 0; i < count; i++)
    {
        snprintf(key_buf, sizeof(key_buf), "k%d", i);
        snprintf(val_buf, sizeof(val_buf), "v%d", i);
        rt_string k = make_str(key_buf);
        rt_string v = make_str(val_buf);
        rt_defaultmap_set(m, k, (void *)v);
    }

    ASSERT(rt_defaultmap_len(m) == count);

    // Spot-check a couple of entries survive the resize.
    rt_string k0 = make_str("k0");
    ASSERT(rt_defaultmap_has(m, k0) == 1);
    rt_string k19 = make_str("k19");
    ASSERT(rt_defaultmap_has(m, k19) == 1);
}

static void test_defaultmap_remove(void)
{
    rt_string def = make_str("def");
    void *m = rt_defaultmap_new(def);
    ASSERT(m != NULL);

    rt_string k = make_str("removeme");
    rt_string v = make_str("val");
    rt_defaultmap_set(m, k, (void *)v);
    ASSERT(rt_defaultmap_has(m, k) == 1);

    int64_t removed = rt_defaultmap_remove(m, k);
    ASSERT(removed == 1);
    ASSERT(rt_defaultmap_has(m, k) == 0);
    ASSERT(rt_defaultmap_len(m) == 0);
}

// ---------------------------------------------------------------------------
// R-01: rt_weak_store / rt_weak_load null-check tests
// ---------------------------------------------------------------------------

static void test_weak_load_null_addr_returns_null(void)
{
    // After the fix, rt_weak_load(NULL) must return NULL, not crash.
    void *result = rt_weak_load(NULL);
    ASSERT(result == NULL);
}

static void test_weak_store_and_load_valid_addr(void)
{
    void *slot = NULL;
    void *sentinel = (void *)(uintptr_t)0xDEAD;

    rt_weak_store(&slot, sentinel);
    ASSERT(slot == sentinel);

    void *loaded = rt_weak_load(&slot);
    ASSERT(loaded == sentinel);
}

static void test_weak_store_clears_to_null(void)
{
    void *slot = (void *)(uintptr_t)0x1234;
    rt_weak_store(&slot, NULL);
    ASSERT(slot == NULL);

    void *loaded = rt_weak_load(&slot);
    ASSERT(loaded == NULL);
}

// ---------------------------------------------------------------------------
// R-03: rt_concqueue_enqueue normal-path tests
// ---------------------------------------------------------------------------

static void test_concqueue_new_is_empty(void)
{
    void *q = rt_concqueue_new();
    ASSERT(q != NULL);
    ASSERT(rt_concqueue_len(q) == 0);
    ASSERT(rt_concqueue_is_empty(q) == 1);
}

static void test_concqueue_enqueue_increases_len(void)
{
    void *q = rt_concqueue_new();
    ASSERT(q != NULL);

    rt_string v1 = make_str("first");
    rt_string v2 = make_str("second");

    rt_concqueue_enqueue(q, (void *)v1);
    ASSERT(rt_concqueue_len(q) == 1);

    rt_concqueue_enqueue(q, (void *)v2);
    ASSERT(rt_concqueue_len(q) == 2);
}

static void test_concqueue_fifo_order(void)
{
    void *q = rt_concqueue_new();
    ASSERT(q != NULL);

    rt_string a = make_str("alpha");
    rt_string b = make_str("beta");
    rt_string c = make_str("gamma");

    rt_concqueue_enqueue(q, (void *)a);
    rt_concqueue_enqueue(q, (void *)b);
    rt_concqueue_enqueue(q, (void *)c);

    ASSERT(rt_concqueue_len(q) == 3);

    void *got1 = rt_concqueue_try_dequeue(q);
    void *got2 = rt_concqueue_try_dequeue(q);
    void *got3 = rt_concqueue_try_dequeue(q);

    ASSERT(got1 == (void *)a);
    ASSERT(got2 == (void *)b);
    ASSERT(got3 == (void *)c);
    ASSERT(rt_concqueue_is_empty(q) == 1);
}

static void test_concqueue_try_dequeue_empty_returns_null(void)
{
    void *q = rt_concqueue_new();
    ASSERT(q != NULL);

    void *result = rt_concqueue_try_dequeue(q);
    ASSERT(result == NULL);
}

static void test_concqueue_clear_empties_queue(void)
{
    void *q = rt_concqueue_new();
    ASSERT(q != NULL);

    rt_string s = make_str("item");
    rt_concqueue_enqueue(q, (void *)s);
    rt_concqueue_enqueue(q, (void *)s);
    ASSERT(rt_concqueue_len(q) == 2);

    rt_concqueue_clear(q);
    ASSERT(rt_concqueue_len(q) == 0);
    ASSERT(rt_concqueue_is_empty(q) == 1);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(void)
{
    // R-09: bloomfilter
    test_bloomfilter_new_returns_non_null();
    test_bloomfilter_add_and_query();
    test_bloomfilter_clear_resets_count();
    test_bloomfilter_edge_params_clamped();

    // R-10: defaultmap
    test_defaultmap_new_returns_non_null();
    test_defaultmap_get_returns_default_for_missing_key();
    test_defaultmap_set_and_get();
    test_defaultmap_resize_via_many_inserts();
    test_defaultmap_remove();

    // R-01: weak references
    test_weak_load_null_addr_returns_null();
    test_weak_store_and_load_valid_addr();
    test_weak_store_clears_to_null();

    // R-03: concqueue
    test_concqueue_new_is_empty();
    test_concqueue_enqueue_increases_len();
    test_concqueue_fifo_order();
    test_concqueue_try_dequeue_empty_returns_null();
    test_concqueue_clear_empties_queue();

    printf("%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
