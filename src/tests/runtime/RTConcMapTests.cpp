//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTConcMapTests.cpp
// Purpose: Tests for Viper.Threads.ConcurrentMap thread-safe hash map.
//
//===----------------------------------------------------------------------===//

#include "rt_concmap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static rt_string make_str(const char *s)
{
    return rt_const_cstr(s);
}

static void *new_obj()
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

// ============================================================================
// Basic operations
// ============================================================================

static void test_new()
{
    void *m = rt_concmap_new();
    assert(m != nullptr);
    assert(rt_concmap_len(m) == 0);
    assert(rt_concmap_is_empty(m) == 1);
    printf("test_new: PASSED\n");
}

static void test_set_get()
{
    void *m = rt_concmap_new();
    void *val = new_obj();
    rt_concmap_set(m, make_str("hello"), val);

    assert(rt_concmap_len(m) == 1);
    assert(rt_concmap_is_empty(m) == 0);

    void *result = rt_concmap_get(m, make_str("hello"));
    assert(result == val);

    printf("test_set_get: PASSED\n");
}

static void test_get_missing()
{
    void *m = rt_concmap_new();
    void *result = rt_concmap_get(m, make_str("missing"));
    assert(result == nullptr);
    printf("test_get_missing: PASSED\n");
}

static void test_get_or()
{
    void *m = rt_concmap_new();
    void *def = new_obj();

    void *result = rt_concmap_get_or(m, make_str("missing"), def);
    assert(result == def);

    void *val = new_obj();
    rt_concmap_set(m, make_str("key"), val);
    result = rt_concmap_get_or(m, make_str("key"), def);
    assert(result == val);

    printf("test_get_or: PASSED\n");
}

static void test_has()
{
    void *m = rt_concmap_new();
    assert(rt_concmap_has(m, make_str("key")) == 0);

    rt_concmap_set(m, make_str("key"), new_obj());
    assert(rt_concmap_has(m, make_str("key")) == 1);
    assert(rt_concmap_has(m, make_str("other")) == 0);

    printf("test_has: PASSED\n");
}

static void test_update()
{
    void *m = rt_concmap_new();
    void *v1 = new_obj();
    void *v2 = new_obj();
    rt_concmap_set(m, make_str("key"), v1);
    rt_concmap_set(m, make_str("key"), v2);

    assert(rt_concmap_len(m) == 1);
    void *result = rt_concmap_get(m, make_str("key"));
    assert(result == v2);

    printf("test_update: PASSED\n");
}

static void test_remove()
{
    void *m = rt_concmap_new();
    rt_concmap_set(m, make_str("key"), new_obj());
    assert(rt_concmap_len(m) == 1);

    int8_t removed = rt_concmap_remove(m, make_str("key"));
    assert(removed == 1);
    assert(rt_concmap_len(m) == 0);
    assert(rt_concmap_has(m, make_str("key")) == 0);

    removed = rt_concmap_remove(m, make_str("key"));
    assert(removed == 0);

    printf("test_remove: PASSED\n");
}

static void test_clear()
{
    void *m = rt_concmap_new();
    rt_concmap_set(m, make_str("a"), new_obj());
    rt_concmap_set(m, make_str("b"), new_obj());
    rt_concmap_set(m, make_str("c"), new_obj());
    assert(rt_concmap_len(m) == 3);

    rt_concmap_clear(m);
    assert(rt_concmap_len(m) == 0);
    assert(rt_concmap_is_empty(m) == 1);

    printf("test_clear: PASSED\n");
}

static void test_set_if_missing()
{
    void *m = rt_concmap_new();
    void *v1 = new_obj();
    void *v2 = new_obj();

    int8_t inserted = rt_concmap_set_if_missing(m, make_str("key"), v1);
    assert(inserted == 1);
    assert(rt_concmap_get(m, make_str("key")) == v1);

    inserted = rt_concmap_set_if_missing(m, make_str("key"), v2);
    assert(inserted == 0);
    assert(rt_concmap_get(m, make_str("key")) == v1);

    printf("test_set_if_missing: PASSED\n");
}

static void test_keys_values()
{
    void *m = rt_concmap_new();
    rt_concmap_set(m, make_str("a"), new_obj());
    rt_concmap_set(m, make_str("b"), new_obj());

    void *keys = rt_concmap_keys(m);
    assert(rt_seq_len(keys) == 2);

    void *values = rt_concmap_values(m);
    assert(rt_seq_len(values) == 2);

    printf("test_keys_values: PASSED\n");
}

static void test_many_entries()
{
    void *m = rt_concmap_new();
    char buf[32];
    void *vals[100];

    /* Insert enough to trigger resize */
    for (int i = 0; i < 100; i++)
    {
        snprintf(buf, sizeof(buf), "key_%d", i);
        vals[i] = new_obj();
        rt_concmap_set(m, make_str(buf), vals[i]);
    }
    assert(rt_concmap_len(m) == 100);

    /* Verify all retrievable */
    for (int i = 0; i < 100; i++)
    {
        snprintf(buf, sizeof(buf), "key_%d", i);
        void *val = rt_concmap_get(m, make_str(buf));
        assert(val == vals[i]);
    }

    printf("test_many_entries: PASSED\n");
}

// ============================================================================
// Concurrency tests
// ============================================================================

static void test_concurrent_writes()
{
    void *m = rt_concmap_new();
    const int N = 100;
    const int T = 4;

    auto worker = [&](int thread_id)
    {
        char buf[64];
        for (int i = 0; i < N; i++)
        {
            snprintf(buf, sizeof(buf), "t%d_key_%d", thread_id, i);
            void *val = rt_obj_new_i64(0, 8);
            rt_concmap_set(m, rt_const_cstr(buf), val);
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < T; t++)
    {
        threads.emplace_back(worker, t);
    }
    for (auto &th : threads)
    {
        th.join();
    }

    assert(rt_concmap_len(m) == N * T);
    printf("test_concurrent_writes: PASSED\n");
}

static void test_concurrent_read_write()
{
    void *m = rt_concmap_new();

    /* Pre-populate */
    for (int i = 0; i < 50; i++)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "key_%d", i);
        rt_concmap_set(m, rt_const_cstr(buf), new_obj());
    }

    /* Readers and writers running concurrently */
    auto writer = [&]()
    {
        for (int i = 50; i < 100; i++)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "key_%d", i);
            rt_concmap_set(m, rt_const_cstr(buf), rt_obj_new_i64(0, 8));
        }
    };

    auto reader = [&]()
    {
        for (int i = 0; i < 50; i++)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "key_%d", i);
            /* Value may or may not be present during writes, but should not crash */
            (void)rt_concmap_get(m, rt_const_cstr(buf));
        }
    };

    std::thread w(writer);
    std::thread r1(reader);
    std::thread r2(reader);

    w.join();
    r1.join();
    r2.join();

    assert(rt_concmap_len(m) == 100);
    printf("test_concurrent_read_write: PASSED\n");
}

static void test_concurrent_set_if_missing()
{
    void *m = rt_concmap_new();
    const int T = 4;

    /* All threads try to set the same key — only one should succeed */
    auto worker = [&](int thread_id)
    {
        void *val = rt_obj_new_i64(0, 8);
        rt_concmap_set_if_missing(m, rt_const_cstr("shared_key"), val);
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < T; t++)
    {
        threads.emplace_back(worker, t);
    }
    for (auto &th : threads)
    {
        th.join();
    }

    assert(rt_concmap_len(m) == 1);
    printf("test_concurrent_set_if_missing: PASSED\n");
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    printf("=== ConcurrentMap Tests ===\n\n");

    /* Basic operations */
    test_new();
    test_set_get();
    test_get_missing();
    test_get_or();
    test_has();
    test_update();
    test_remove();
    test_clear();
    test_set_if_missing();
    test_keys_values();
    test_many_entries();

    /* Concurrency */
    test_concurrent_writes();
    test_concurrent_read_write();
    test_concurrent_set_if_missing();

    printf("\nAll ConcurrentMap tests passed!\n");
    return 0;
}
