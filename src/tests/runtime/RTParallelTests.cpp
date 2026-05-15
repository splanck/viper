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
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_seq_internal.h"
#include "rt_string.h"
#include "rt_threadpool.h"

#include "common/ProcessIsolation.hpp"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static void test_result(bool cond, const char *name) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", name);
        assert(false);
    }
}

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

//=============================================================================
// Default Workers Tests
//=============================================================================

static void test_default_workers() {
    int64_t workers = rt_parallel_default_workers();
    test_result(workers >= 1, "default_workers: should be at least 1");
    test_result(workers <= 1024, "default_workers: should be reasonable");
    printf("  Detected %lld CPU cores\n", (long long)workers);
}

static void test_default_pool() {
    void *pool = rt_parallel_default_pool();
    test_result(pool != NULL, "default_pool: should return a pool");

    // Same pool on repeated calls
    void *pool2 = rt_parallel_default_pool();
    test_result(pool == pool2, "default_pool: should return same pool");

    if (rt_obj_release_check0(pool))
        rt_obj_free(pool);
    if (rt_obj_release_check0(pool2))
        rt_obj_free(pool2);
}

//=============================================================================
// Parallel ForEach Tests
//=============================================================================

static std::atomic<int64_t> g_foreach_counter{0};

static void foreach_increment(void *item) {
    (void)item;
    g_foreach_counter++;
}

static void test_foreach_basic() {
    // Create a sequence with 10 items
    void *seq = rt_seq_new();
    for (int i = 0; i < 10; i++) {
        rt_seq_push(seq, (void *)(intptr_t)i);
    }

    g_foreach_counter = 0;
    rt_parallel_foreach(seq, (void *)foreach_increment);

    test_result(g_foreach_counter == 10, "foreach_basic: should process all items");
}

static void test_foreach_empty() {
    void *seq = rt_seq_new();

    g_foreach_counter = 0;
    rt_parallel_foreach(seq, (void *)foreach_increment);

    test_result(g_foreach_counter == 0, "foreach_empty: should handle empty seq");
}

static void test_foreach_null() {
    // Should not crash on NULL
    rt_parallel_foreach(NULL, (void *)foreach_increment);
    rt_parallel_foreach(rt_seq_new(), NULL);

    test_result(true, "foreach_null: should handle NULL safely");
}

static void call_foreach_shutdown_pool() {
    void *seq = rt_seq_new();
    rt_seq_push(seq, (void *)1);
    void *pool = rt_threadpool_new(1);
    rt_threadpool_shutdown(pool);
    rt_parallel_foreach_pool(seq, (void *)foreach_increment, pool);
}

static void foreach_trap(void *item) {
    (void)item;
    rt_trap("parallel foreach trap");
}

static void call_foreach_trapping_task() {
    void *seq = rt_seq_new();
    rt_seq_push(seq, (void *)1);
    void *pool = rt_threadpool_new(1);
    rt_parallel_foreach_pool(seq, (void *)foreach_trap, pool);
}

static void corrupt_seq_len(void *seq, int64_t len) {
    ((rt_seq_impl *)seq)->len = len;
}

static void call_foreach_huge_seq_len() {
    void *seq = rt_seq_new();
    corrupt_seq_len(seq, INT64_MAX);
    void *pool = rt_threadpool_new(1);
    rt_parallel_foreach_pool(seq, (void *)foreach_increment, pool);
}

//=============================================================================
// Parallel Map Tests
//=============================================================================

static void *map_double(void *item) {
    intptr_t val = (intptr_t)item;
    return (void *)(val * 2);
}

static void *map_new_seq(void *item) {
    (void)item;
    return rt_seq_new();
}

static void *map_identity(void *item) {
    return item;
}

static void *g_borrowed_map_result = nullptr;

static void *map_borrowed_shared(void *item) {
    (void)item;
    return g_borrowed_map_result;
}

static void test_map_basic() {
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

static void test_map_empty() {
    void *seq = rt_seq_new();
    void *result = rt_parallel_map(seq, (void *)map_double);

    test_result(rt_seq_len(result) == 0, "map_empty: should return empty seq");
}

static void test_map_order_preserved() {
    // Create larger sequence to test order preservation
    void *seq = rt_seq_new();
    for (int i = 0; i < 100; i++) {
        rt_seq_push(seq, (void *)(intptr_t)i);
    }

    void *result = rt_parallel_map(seq, (void *)map_double);

    test_result(rt_seq_len(result) == 100, "map_order: should have same length");

    // Verify order is preserved
    for (int i = 0; i < 100; i++) {
        intptr_t expected = i * 2;
        intptr_t actual = (intptr_t)rt_seq_get(result, i);
        if (actual != expected) {
            printf("  Order mismatch at index %d: expected %ld, got %ld\n",
                   i,
                   (long)expected,
                   (long)actual);
            test_result(false, "map_order: order must be preserved");
        }
    }
    test_result(true, "map_order: order preserved correctly");
}

static void test_map_retains_callback_results_in_seq() {
    void *seq = rt_seq_new();
    for (int i = 0; i < 8; i++)
        rt_seq_push(seq, (void *)(intptr_t)i);

    void *result = rt_parallel_map(seq, (void *)map_new_seq);
    test_result(rt_seq_len(result) == 8, "map_retain_result: should have same length");
    for (int i = 0; i < 8; i++)
        test_result(rt_seq_len(rt_seq_get(result, i)) == 0, "map_retain_result: result usable");

    if (rt_obj_release_check0(result))
        rt_obj_free(result);
}

static void test_map_retains_borrowed_input_results() {
    void *seq = rt_seq_new();
    void *item = rt_seq_new();
    rt_seq_push(seq, item);

    void *result = rt_parallel_map(seq, (void *)map_identity);
    test_result(rt_seq_len(result) == 1, "map_borrowed_input: should have one result");
    test_result(rt_seq_get(result, 0) == item, "map_borrowed_input: result should be input");

    if (rt_obj_release_check0(item))
        rt_obj_free(item);
    test_result(rt_seq_len(rt_seq_get(result, 0)) == 0,
                "map_borrowed_input: result should retain input object");

    if (rt_obj_release_check0(result))
        rt_obj_free(result);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);
}

static void test_map_retains_borrowed_non_input_results() {
    void *seq = rt_seq_new();
    for (int i = 0; i < 3; i++)
        rt_seq_push(seq, (void *)(intptr_t)(i + 1));

    void *borrowed = rt_seq_new();
    g_borrowed_map_result = borrowed;
    void *result = rt_parallel_map(seq, (void *)map_borrowed_shared);
    test_result(rt_seq_len(result) == 3, "map_borrowed_shared: should have same length");
    for (int i = 0; i < 3; i++)
        test_result(rt_seq_get(result, i) == borrowed, "map_borrowed_shared: result identity");

    if (rt_obj_release_check0(borrowed))
        rt_obj_free(borrowed);
    g_borrowed_map_result = nullptr;

    for (int i = 0; i < 3; i++)
        test_result(rt_seq_len(rt_seq_get(result, i)) == 0,
                    "map_borrowed_shared: retained borrowed result should remain usable");

    if (rt_obj_release_check0(result))
        rt_obj_free(result);
    if (rt_obj_release_check0(seq))
        rt_obj_free(seq);
}

static std::atomic<int64_t> g_nested_parallel_sum{0};

static void nested_for_accumulate(int64_t index) {
    g_nested_parallel_sum += index;
}

static void nested_parallel_task(void *arg) {
    void *pool = arg;
    rt_parallel_for_pool(0, 10, (void *)nested_for_accumulate, pool);
}

static void test_nested_parallel_same_pool_runs_inline() {
    g_nested_parallel_sum = 0;
    void *pool = rt_threadpool_new(1);
    test_result(pool != NULL, "nested_parallel: should create pool");
    test_result(rt_threadpool_submit(pool, (void *)nested_parallel_task, pool) == 1,
                "nested_parallel: should submit outer task");
    test_result(rt_threadpool_wait_for(pool, 1000) == 1,
                "nested_parallel: should not self-deadlock");
    test_result(g_nested_parallel_sum == 45, "nested_parallel: should run nested work");
    rt_threadpool_shutdown(pool);
}

static void call_map_huge_seq_len() {
    void *seq = rt_seq_new();
    corrupt_seq_len(seq, INT64_MAX);
    void *pool = rt_threadpool_new(1);
    (void)rt_parallel_map_pool(seq, (void *)map_double, pool);
}

//=============================================================================
// Parallel For Tests
//=============================================================================

static std::atomic<int64_t> g_for_sum{0};

static void for_accumulate(int64_t index) {
    g_for_sum += index;
}

static void test_for_basic() {
    // Sum 0 + 1 + 2 + ... + 9 = 45
    g_for_sum = 0;
    rt_parallel_for(0, 10, (void *)for_accumulate);

    test_result(g_for_sum == 45, "for_basic: should sum correctly");
}

static void test_for_empty_range() {
    g_for_sum = 0;
    /// @brief Rt_parallel_for.
    rt_parallel_for(5, 5, (void *)for_accumulate); // Empty range

    test_result(g_for_sum == 0, "for_empty_range: should do nothing");
}

static void test_for_single() {
    g_for_sum = 0;
    /// @brief Rt_parallel_for.
    rt_parallel_for(7, 8, (void *)for_accumulate); // Single iteration

    test_result(g_for_sum == 7, "for_single: should execute once");
}

static void call_for_range_too_large() {
    rt_parallel_for(INT64_MIN, INT64_MAX, (void *)for_accumulate);
}

static void call_invoke_huge_seq_len() {
    void *funcs = rt_seq_new();
    corrupt_seq_len(funcs, INT64_MAX);
    void *pool = rt_threadpool_new(1);
    rt_parallel_invoke_pool(funcs, pool);
}

static void *reduce_passthrough(void *accum, void *item) {
    (void)item;
    return accum;
}

static void call_reduce_huge_seq_len() {
    void *seq = rt_seq_new();
    corrupt_seq_len(seq, INT64_MAX);
    void *pool = rt_threadpool_new(1);
    (void)rt_parallel_reduce_pool(seq, (void *)reduce_passthrough, NULL, pool);
}

//=============================================================================
// Parallel Invoke Tests
//=============================================================================

static std::atomic<int> g_invoke_a{0};
static std::atomic<int> g_invoke_b{0};
static std::atomic<int> g_invoke_c{0};

static void invoke_set_a() {
    g_invoke_a = 1;
}

static void invoke_set_b() {
    g_invoke_b = 1;
}

static void invoke_set_c() {
    g_invoke_c = 1;
}

static void test_invoke_basic() {
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

static void test_invoke_empty() {
    void *funcs = rt_seq_new();
    /// @brief Rt_parallel_invoke.
    rt_parallel_invoke(funcs); // Should not crash

    test_result(true, "invoke_empty: should handle empty sequence");
}

//=============================================================================
// Main
//=============================================================================

int main(int argc, char *argv[]) {
    viper::tests::registerChildFunction(call_foreach_shutdown_pool);
    viper::tests::registerChildFunction(call_foreach_trapping_task);
    viper::tests::registerChildFunction(call_foreach_huge_seq_len);
    viper::tests::registerChildFunction(call_map_huge_seq_len);
    viper::tests::registerChildFunction(call_for_range_too_large);
    viper::tests::registerChildFunction(call_invoke_huge_seq_len);
    viper::tests::registerChildFunction(call_reduce_huge_seq_len);
    if (viper::tests::dispatchChild(argc, argv))
        return 0;

    // Isolated child tests must run before the shared default pool starts worker
    // threads; forked children cannot safely inherit locked pthread mutexes.
    auto result = viper::tests::runIsolated(call_foreach_shutdown_pool);
    test_result(result.stderrText.find("Parallel.ForEach: failed to submit work") !=
                    std::string::npos,
                "foreach_shutdown_pool: should trap without hanging");
    result = viper::tests::runIsolated(call_foreach_trapping_task);
    test_result(result.stderrText.find("parallel foreach trap") != std::string::npos,
                "foreach_task_trap: should preserve worker trap message");
    result = viper::tests::runIsolated(call_foreach_huge_seq_len);
    test_result(result.stderrText.find("Parallel.ForEach: allocation size overflow") !=
                    std::string::npos,
                "foreach_huge_seq: should trap before overflowing allocation");
    result = viper::tests::runIsolated(call_map_huge_seq_len);
    test_result(result.stderrText.find("Parallel.Map: allocation size overflow") !=
                    std::string::npos,
                "map_huge_seq: should trap before overflowing allocation");
    result = viper::tests::runIsolated(call_for_range_too_large);
    test_result(result.stderrText.find("Parallel.For: range too large") != std::string::npos,
                "for_range_too_large: should trap instead of overflowing");
    result = viper::tests::runIsolated(call_invoke_huge_seq_len);
    test_result(result.stderrText.find("Parallel.Invoke: allocation size overflow") !=
                    std::string::npos,
                "invoke_huge_seq: should trap before overflowing allocation");
    result = viper::tests::runIsolated(call_reduce_huge_seq_len);
    test_result(result.stderrText.find("Parallel.Reduce: allocation size overflow") !=
                    std::string::npos,
                "reduce_huge_seq: should trap before overflowing allocation");

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
    test_map_retains_callback_results_in_seq();
    test_map_retains_borrowed_input_results();
    test_map_retains_borrowed_non_input_results();
    test_nested_parallel_same_pool_runs_inline();

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
