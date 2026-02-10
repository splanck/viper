//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTParallelReduceTests.cpp
// Purpose: Tests for Parallel.Reduce functionality.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_parallel.h"
#include "rt_seq.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

/* Sum combiner: interprets void* as int64_t (boxed integer). */
static void *sum_combine(void *a, void *b)
{
    int64_t va = (int64_t)(intptr_t)a;
    int64_t vb = (int64_t)(intptr_t)b;
    return (void *)(intptr_t)(va + vb);
}

/* Max combiner. */
static void *max_combine(void *a, void *b)
{
    int64_t va = (int64_t)(intptr_t)a;
    int64_t vb = (int64_t)(intptr_t)b;
    return (void *)(intptr_t)(va > vb ? va : vb);
}

/* Product combiner. */
static void *mul_combine(void *a, void *b)
{
    int64_t va = (int64_t)(intptr_t)a;
    int64_t vb = (int64_t)(intptr_t)b;
    return (void *)(intptr_t)(va * vb);
}

static void *make_int_seq(const int64_t *vals, int n)
{
    void *seq = rt_seq_new();
    for (int i = 0; i < n; i++)
    {
        rt_seq_push(seq, (void *)(intptr_t)vals[i]);
    }
    return seq;
}

// ============================================================================
// Tests
// ============================================================================

static void test_reduce_sum()
{
    int64_t vals[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    void *seq = make_int_seq(vals, 10);

    void *result = rt_parallel_reduce(seq, (void *)sum_combine, (void *)(intptr_t)0);
    int64_t sum = (int64_t)(intptr_t)result;
    assert(sum == 55);
    printf("test_reduce_sum: PASSED\n");
}

static void test_reduce_empty()
{
    void *seq = rt_seq_new();
    void *result = rt_parallel_reduce(seq, (void *)sum_combine, (void *)(intptr_t)42);
    int64_t val = (int64_t)(intptr_t)result;
    assert(val == 42); /* Should return identity */
    printf("test_reduce_empty: PASSED\n");
}

static void test_reduce_single()
{
    int64_t vals[] = {7};
    void *seq = make_int_seq(vals, 1);

    void *result = rt_parallel_reduce(seq, (void *)sum_combine, (void *)(intptr_t)0);
    int64_t sum = (int64_t)(intptr_t)result;
    assert(sum == 7);
    printf("test_reduce_single: PASSED\n");
}

static void test_reduce_max()
{
    int64_t vals[] = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    void *seq = make_int_seq(vals, 10);

    void *result = rt_parallel_reduce(seq, (void *)max_combine, (void *)(intptr_t)0);
    int64_t max_val = (int64_t)(intptr_t)result;
    assert(max_val == 9);
    printf("test_reduce_max: PASSED\n");
}

static void test_reduce_product()
{
    int64_t vals[] = {1, 2, 3, 4, 5};
    void *seq = make_int_seq(vals, 5);

    void *result = rt_parallel_reduce(seq, (void *)mul_combine, (void *)(intptr_t)1);
    int64_t prod = (int64_t)(intptr_t)result;
    assert(prod == 120); /* 5! = 120 */
    printf("test_reduce_product: PASSED\n");
}

static void test_reduce_large()
{
    /* Large sequence to ensure parallel chunking works. */
    void *seq = rt_seq_new();
    int64_t expected = 0;
    for (int64_t i = 1; i <= 1000; i++)
    {
        rt_seq_push(seq, (void *)(intptr_t)i);
        expected += i;
    }

    void *result = rt_parallel_reduce(seq, (void *)sum_combine, (void *)(intptr_t)0);
    int64_t sum = (int64_t)(intptr_t)result;
    assert(sum == expected); /* n*(n+1)/2 = 500500 */
    printf("test_reduce_large: PASSED\n");
}

static void test_reduce_null_seq()
{
    void *result = rt_parallel_reduce(NULL, (void *)sum_combine, (void *)(intptr_t)99);
    int64_t val = (int64_t)(intptr_t)result;
    assert(val == 99); /* Should return identity */
    printf("test_reduce_null_seq: PASSED\n");
}

int main()
{
    printf("=== Parallel.Reduce Tests ===\n\n");

    test_reduce_sum();
    test_reduce_empty();
    test_reduce_single();
    test_reduce_max();
    test_reduce_product();
    test_reduce_large();
    test_reduce_null_seq();

    printf("\nAll Parallel.Reduce tests passed!\n");
    return 0;
}
