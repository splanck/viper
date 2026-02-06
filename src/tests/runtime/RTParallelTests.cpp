//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// Tests for Viper.Threads.Parallel
//
//===----------------------------------------------------------------------===//

#include "rt_parallel.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>

static void test_result(bool cond, const char *name)
{
    if (!cond)
    {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
}

//=============================================================================
// Default Workers Tests
//=============================================================================

static void test_default_workers()
{
    int64_t workers = rt_parallel_default_workers();
    test_result(workers >= 1, "default_workers: should be at least 1");
    test_result(workers <= 1024, "default_workers: should be reasonable");
    printf("  Detected %lld CPU cores\n", (long long)workers);
}

static void test_default_pool()
{
    void *pool = rt_parallel_default_pool();
    test_result(pool != NULL, "default_pool: should return a pool");

    // Same pool on repeated calls
    void *pool2 = rt_parallel_default_pool();
    test_result(pool == pool2, "default_pool: should return same pool");
}

//=============================================================================
// Parallel ForEach Tests
//=============================================================================

static std::atomic<int64_t> g_foreach_counter{0};

static void foreach_increment(void *item)
{
    (void)item;
    g_foreach_counter++;
}

static void test_foreach_basic()
{
    // Create a sequence with 10 items
    void *seq = rt_seq_new();
    for (int i = 0; i < 10; i++)
    {
        rt_seq_push(seq, (void *)(intptr_t)i);
    }

    g_foreach_counter = 0;
    rt_parallel_foreach(seq, (void *)foreach_increment);

    test_result(g_foreach_counter == 10, "foreach_basic: should process all items");
}

static void test_foreach_empty()
{
    void *seq = rt_seq_new();

    g_foreach_counter = 0;
    rt_parallel_foreach(seq, (void *)foreach_increment);

    test_result(g_foreach_counter == 0, "foreach_empty: should handle empty seq");
}

static void test_foreach_null()
{
    // Should not crash on NULL
    rt_parallel_foreach(NULL, (void *)foreach_increment);
    rt_parallel_foreach(rt_seq_new(), NULL);

    test_result(true, "foreach_null: should handle NULL safely");
}

//=============================================================================
// Parallel Map Tests
//=============================================================================

static void *map_double(void *item)
{
    intptr_t val = (intptr_t)item;
    return (void *)(val * 2);
}

static void test_map_basic()
{
    // Create sequence [1, 2, 3]
    void *seq = rt_seq_new();
    rt_seq_push(seq, (void *)1);
    rt_seq_push(seq, (void *)2);
    rt_seq_push(seq, (void *)3);

    void *result = rt_parallel_map(seq, (void *)map_double);

    test_result(rt_seq_len(result) == 3, "map_basic: should have same length");

    // Check values are doubled and in order
    test_result((intptr_t)rt_seq_get(result, 0) == 2, "map_basic: first value");
    test_result((intptr_t)rt_seq_get(result, 1) == 4, "map_basic: second value");
    test_result((intptr_t)rt_seq_get(result, 2) == 6, "map_basic: third value");
}

static void test_map_empty()
{
    void *seq = rt_seq_new();
    void *result = rt_parallel_map(seq, (void *)map_double);

    test_result(rt_seq_len(result) == 0, "map_empty: should return empty seq");
}

static void test_map_order_preserved()
{
    // Create larger sequence to test order preservation
    void *seq = rt_seq_new();
    for (int i = 0; i < 100; i++)
    {
        rt_seq_push(seq, (void *)(intptr_t)i);
    }

    void *result = rt_parallel_map(seq, (void *)map_double);

    test_result(rt_seq_len(result) == 100, "map_order: should have same length");

    // Verify order is preserved
    for (int i = 0; i < 100; i++)
    {
        intptr_t expected = i * 2;
        intptr_t actual = (intptr_t)rt_seq_get(result, i);
        if (actual != expected)
        {
            printf("  Order mismatch at index %d: expected %ld, got %ld\n",
                   i,
                   (long)expected,
                   (long)actual);
            test_result(false, "map_order: order must be preserved");
        }
    }
    test_result(true, "map_order: order preserved correctly");
}

//=============================================================================
// Parallel For Tests
//=============================================================================

static std::atomic<int64_t> g_for_sum{0};

static void for_accumulate(int64_t index)
{
    g_for_sum += index;
}

static void test_for_basic()
{
    // Sum 0 + 1 + 2 + ... + 9 = 45
    g_for_sum = 0;
    rt_parallel_for(0, 10, (void *)for_accumulate);

    test_result(g_for_sum == 45, "for_basic: should sum correctly");
}

static void test_for_empty_range()
{
    g_for_sum = 0;
    rt_parallel_for(5, 5, (void *)for_accumulate); // Empty range

    test_result(g_for_sum == 0, "for_empty_range: should do nothing");
}

static void test_for_single()
{
    g_for_sum = 0;
    rt_parallel_for(7, 8, (void *)for_accumulate); // Single iteration

    test_result(g_for_sum == 7, "for_single: should execute once");
}

//=============================================================================
// Parallel Invoke Tests
//=============================================================================

static std::atomic<int> g_invoke_a{0};
static std::atomic<int> g_invoke_b{0};
static std::atomic<int> g_invoke_c{0};

static void invoke_set_a()
{
    g_invoke_a = 1;
}

static void invoke_set_b()
{
    g_invoke_b = 1;
}

static void invoke_set_c()
{
    g_invoke_c = 1;
}

static void test_invoke_basic()
{
    g_invoke_a = 0;
    g_invoke_b = 0;
    g_invoke_c = 0;

    void *funcs = rt_seq_new();
    rt_seq_push(funcs, (void *)invoke_set_a);
    rt_seq_push(funcs, (void *)invoke_set_b);
    rt_seq_push(funcs, (void *)invoke_set_c);

    rt_parallel_invoke(funcs);

    test_result(g_invoke_a == 1, "invoke_basic: a should be set");
    test_result(g_invoke_b == 1, "invoke_basic: b should be set");
    test_result(g_invoke_c == 1, "invoke_basic: c should be set");
}

static void test_invoke_empty()
{
    void *funcs = rt_seq_new();
    rt_parallel_invoke(funcs); // Should not crash

    test_result(true, "invoke_empty: should handle empty sequence");
}

//=============================================================================
// Main
//=============================================================================

int main()
{
    // Default workers/pool tests
    test_default_workers();
    test_default_pool();

    // ForEach tests
    test_foreach_basic();
    test_foreach_empty();
    test_foreach_null();

    // Map tests
    test_map_basic();
    test_map_empty();
    test_map_order_preserved();

    // For tests
    test_for_basic();
    test_for_empty_range();
    test_for_single();

    // Invoke tests
    test_invoke_basic();
    test_invoke_empty();

    printf("All Parallel tests passed!\n");
    return 0;
}
