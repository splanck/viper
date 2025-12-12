//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTSeqTests.cpp
// Purpose: Comprehensive tests for Viper.Collections.Seq dynamic sequence.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_seq.h"

#include <cassert>
#include <csetjmp>
#include <cstring>

namespace
{
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

static void test_new_and_basic_properties()
{
    void *seq = rt_seq_new();
    assert(seq != nullptr);
    assert(rt_seq_len(seq) == 0);
    assert(rt_seq_cap(seq) >= 1);
    assert(rt_seq_is_empty(seq) == 1);
}

static void test_with_capacity()
{
    void *seq = rt_seq_with_capacity(100);
    assert(seq != nullptr);
    assert(rt_seq_len(seq) == 0);
    assert(rt_seq_cap(seq) >= 100);
    assert(rt_seq_is_empty(seq) == 1);

    // Minimum capacity is 1
    void *seq2 = rt_seq_with_capacity(0);
    assert(seq2 != nullptr);
    assert(rt_seq_cap(seq2) >= 1);

    void *seq3 = rt_seq_with_capacity(-10);
    assert(seq3 != nullptr);
    assert(rt_seq_cap(seq3) >= 1);
}

static void test_push_and_get()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30;
    rt_seq_push(seq, &a);
    assert(rt_seq_len(seq) == 1);
    assert(rt_seq_is_empty(seq) == 0);
    assert(rt_seq_get(seq, 0) == &a);

    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);
    assert(rt_seq_len(seq) == 3);
    assert(rt_seq_get(seq, 0) == &a);
    assert(rt_seq_get(seq, 1) == &b);
    assert(rt_seq_get(seq, 2) == &c);
}

static void test_set()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);

    assert(rt_seq_get(seq, 0) == &a);
    rt_seq_set(seq, 0, &c);
    assert(rt_seq_get(seq, 0) == &c);
}

static void test_pop()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);

    assert(rt_seq_len(seq) == 3);
    void *popped = rt_seq_pop(seq);
    assert(popped == &c);
    assert(rt_seq_len(seq) == 2);

    popped = rt_seq_pop(seq);
    assert(popped == &b);
    assert(rt_seq_len(seq) == 1);

    popped = rt_seq_pop(seq);
    assert(popped == &a);
    assert(rt_seq_len(seq) == 0);
    assert(rt_seq_is_empty(seq) == 1);
}

static void test_peek()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);

    assert(rt_seq_peek(seq) == &b);
    assert(rt_seq_len(seq) == 2); // peek doesn't remove

    rt_seq_pop(seq);
    assert(rt_seq_peek(seq) == &a);
}

static void test_first_and_last()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);

    assert(rt_seq_first(seq) == &a);
    assert(rt_seq_last(seq) == &c);

    // Single element
    void *seq2 = rt_seq_new();
    rt_seq_push(seq2, &b);
    assert(rt_seq_first(seq2) == &b);
    assert(rt_seq_last(seq2) == &b);
}

static void test_insert()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30, d = 40;

    // Insert at beginning of empty
    rt_seq_insert(seq, 0, &a);
    assert(rt_seq_len(seq) == 1);
    assert(rt_seq_get(seq, 0) == &a);

    // Insert at end
    rt_seq_insert(seq, 1, &c);
    assert(rt_seq_len(seq) == 2);
    assert(rt_seq_get(seq, 0) == &a);
    assert(rt_seq_get(seq, 1) == &c);

    // Insert in middle
    rt_seq_insert(seq, 1, &b);
    assert(rt_seq_len(seq) == 3);
    assert(rt_seq_get(seq, 0) == &a);
    assert(rt_seq_get(seq, 1) == &b);
    assert(rt_seq_get(seq, 2) == &c);

    // Insert at beginning
    rt_seq_insert(seq, 0, &d);
    assert(rt_seq_len(seq) == 4);
    assert(rt_seq_get(seq, 0) == &d);
    assert(rt_seq_get(seq, 1) == &a);
    assert(rt_seq_get(seq, 2) == &b);
    assert(rt_seq_get(seq, 3) == &c);
}

static void test_remove()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30, d = 40;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);
    rt_seq_push(seq, &d);

    // Remove from middle
    void *removed = rt_seq_remove(seq, 1);
    assert(removed == &b);
    assert(rt_seq_len(seq) == 3);
    assert(rt_seq_get(seq, 0) == &a);
    assert(rt_seq_get(seq, 1) == &c);
    assert(rt_seq_get(seq, 2) == &d);

    // Remove from beginning
    removed = rt_seq_remove(seq, 0);
    assert(removed == &a);
    assert(rt_seq_len(seq) == 2);
    assert(rt_seq_get(seq, 0) == &c);
    assert(rt_seq_get(seq, 1) == &d);

    // Remove from end
    removed = rt_seq_remove(seq, 1);
    assert(removed == &d);
    assert(rt_seq_len(seq) == 1);
    assert(rt_seq_get(seq, 0) == &c);
}

static void test_clear()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);

    assert(rt_seq_len(seq) == 2);
    rt_seq_clear(seq);
    assert(rt_seq_len(seq) == 0);
    assert(rt_seq_is_empty(seq) == 1);

    // Clear on already empty
    rt_seq_clear(seq);
    assert(rt_seq_len(seq) == 0);
}

static void test_find_and_has()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30, d = 40;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);

    assert(rt_seq_find(seq, &a) == 0);
    assert(rt_seq_find(seq, &b) == 1);
    assert(rt_seq_find(seq, &c) == 2);
    assert(rt_seq_find(seq, &d) == -1); // not in sequence

    assert(rt_seq_has(seq, &a) == 1);
    assert(rt_seq_has(seq, &b) == 1);
    assert(rt_seq_has(seq, &c) == 1);
    assert(rt_seq_has(seq, &d) == 0);
}

static void test_reverse()
{
    // Empty sequence
    void *seq0 = rt_seq_new();
    rt_seq_reverse(seq0); // should not crash
    assert(rt_seq_len(seq0) == 0);

    // Single element
    void *seq1 = rt_seq_new();
    int a = 10;
    rt_seq_push(seq1, &a);
    rt_seq_reverse(seq1);
    assert(rt_seq_get(seq1, 0) == &a);

    // Even number of elements
    void *seq2 = rt_seq_new();
    int b = 20, c = 30, d = 40;
    rt_seq_push(seq2, &a);
    rt_seq_push(seq2, &b);
    rt_seq_push(seq2, &c);
    rt_seq_push(seq2, &d);
    rt_seq_reverse(seq2);
    assert(rt_seq_get(seq2, 0) == &d);
    assert(rt_seq_get(seq2, 1) == &c);
    assert(rt_seq_get(seq2, 2) == &b);
    assert(rt_seq_get(seq2, 3) == &a);

    // Odd number of elements
    void *seq3 = rt_seq_new();
    rt_seq_push(seq3, &a);
    rt_seq_push(seq3, &b);
    rt_seq_push(seq3, &c);
    rt_seq_reverse(seq3);
    assert(rt_seq_get(seq3, 0) == &c);
    assert(rt_seq_get(seq3, 1) == &b);
    assert(rt_seq_get(seq3, 2) == &a);
}

static void test_slice()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30, d = 40, e = 50;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);
    rt_seq_push(seq, &d);
    rt_seq_push(seq, &e);

    // Normal slice
    void *slice1 = rt_seq_slice(seq, 1, 4);
    assert(rt_seq_len(slice1) == 3);
    assert(rt_seq_get(slice1, 0) == &b);
    assert(rt_seq_get(slice1, 1) == &c);
    assert(rt_seq_get(slice1, 2) == &d);

    // Slice from beginning
    void *slice2 = rt_seq_slice(seq, 0, 2);
    assert(rt_seq_len(slice2) == 2);
    assert(rt_seq_get(slice2, 0) == &a);
    assert(rt_seq_get(slice2, 1) == &b);

    // Slice to end
    void *slice3 = rt_seq_slice(seq, 3, 5);
    assert(rt_seq_len(slice3) == 2);
    assert(rt_seq_get(slice3, 0) == &d);
    assert(rt_seq_get(slice3, 1) == &e);

    // Clamped start (negative)
    void *slice4 = rt_seq_slice(seq, -5, 2);
    assert(rt_seq_len(slice4) == 2);
    assert(rt_seq_get(slice4, 0) == &a);
    assert(rt_seq_get(slice4, 1) == &b);

    // Clamped end (beyond length)
    void *slice5 = rt_seq_slice(seq, 3, 100);
    assert(rt_seq_len(slice5) == 2);
    assert(rt_seq_get(slice5, 0) == &d);
    assert(rt_seq_get(slice5, 1) == &e);

    // Empty slice (start >= end)
    void *slice6 = rt_seq_slice(seq, 3, 2);
    assert(rt_seq_len(slice6) == 0);

    void *slice7 = rt_seq_slice(seq, 3, 3);
    assert(rt_seq_len(slice7) == 0);
}

static void test_clone()
{
    void *seq = rt_seq_new();

    int a = 10, b = 20, c = 30;
    rt_seq_push(seq, &a);
    rt_seq_push(seq, &b);
    rt_seq_push(seq, &c);

    void *cloned = rt_seq_clone(seq);
    assert(cloned != seq);
    assert(rt_seq_len(cloned) == 3);
    assert(rt_seq_get(cloned, 0) == &a);
    assert(rt_seq_get(cloned, 1) == &b);
    assert(rt_seq_get(cloned, 2) == &c);

    // Modifying original doesn't affect clone
    int d = 40;
    rt_seq_push(seq, &d);
    assert(rt_seq_len(seq) == 4);
    assert(rt_seq_len(cloned) == 3);

    // Clone empty
    void *empty = rt_seq_new();
    void *cloned_empty = rt_seq_clone(empty);
    assert(rt_seq_len(cloned_empty) == 0);
}

static void test_capacity_growth()
{
    void *seq = rt_seq_with_capacity(2);
    int64_t initial_cap = rt_seq_cap(seq);

    int vals[100];
    for (int i = 0; i < 100; ++i)
    {
        vals[i] = i;
        rt_seq_push(seq, &vals[i]);
    }

    assert(rt_seq_len(seq) == 100);
    assert(rt_seq_cap(seq) > initial_cap);

    // Verify all elements
    for (int i = 0; i < 100; ++i)
    {
        assert(rt_seq_get(seq, i) == &vals[i]);
    }
}

static void test_null_handling()
{
    // Most operations on null should return safe defaults or 0
    assert(rt_seq_len(nullptr) == 0);
    assert(rt_seq_cap(nullptr) == 0);
    assert(rt_seq_is_empty(nullptr) == 1);
    assert(rt_seq_find(nullptr, nullptr) == -1);
    assert(rt_seq_has(nullptr, nullptr) == 0);

    // Clear on null should not crash
    rt_seq_clear(nullptr);

    // Reverse on null should not crash
    rt_seq_reverse(nullptr);

    // Slice on null returns new empty seq
    void *slice = rt_seq_slice(nullptr, 0, 10);
    assert(slice != nullptr);
    assert(rt_seq_len(slice) == 0);

    // Clone on null returns new empty seq
    void *cloned = rt_seq_clone(nullptr);
    assert(cloned != nullptr);
    assert(rt_seq_len(cloned) == 0);
}

static void test_bounds_errors()
{
    void *seq = rt_seq_new();
    int a = 10;
    rt_seq_push(seq, &a);

    // Get out of bounds
    EXPECT_TRAP(rt_seq_get(seq, 1));
    EXPECT_TRAP(rt_seq_get(seq, -1));

    // Set out of bounds
    EXPECT_TRAP(rt_seq_set(seq, 1, &a));
    EXPECT_TRAP(rt_seq_set(seq, -1, &a));

    // Remove out of bounds
    EXPECT_TRAP(rt_seq_remove(seq, 1));
    EXPECT_TRAP(rt_seq_remove(seq, -1));

    // Insert out of bounds
    EXPECT_TRAP(rt_seq_insert(seq, 2, &a));
    EXPECT_TRAP(rt_seq_insert(seq, -1, &a));

    // Pop empty
    rt_seq_pop(seq);
    EXPECT_TRAP(rt_seq_pop(seq));

    // Peek empty
    EXPECT_TRAP(rt_seq_peek(seq));

    // First/Last empty
    EXPECT_TRAP(rt_seq_first(seq));
    EXPECT_TRAP(rt_seq_last(seq));
}

static void test_null_seq_errors()
{
    int a = 10;

    EXPECT_TRAP(rt_seq_get(nullptr, 0));
    EXPECT_TRAP(rt_seq_set(nullptr, 0, &a));
    EXPECT_TRAP(rt_seq_push(nullptr, &a));
    EXPECT_TRAP(rt_seq_pop(nullptr));
    EXPECT_TRAP(rt_seq_peek(nullptr));
    EXPECT_TRAP(rt_seq_first(nullptr));
    EXPECT_TRAP(rt_seq_last(nullptr));
    EXPECT_TRAP(rt_seq_insert(nullptr, 0, &a));
    EXPECT_TRAP(rt_seq_remove(nullptr, 0));
}

int main()
{
    test_new_and_basic_properties();
    test_with_capacity();
    test_push_and_get();
    test_set();
    test_pop();
    test_peek();
    test_first_and_last();
    test_insert();
    test_remove();
    test_clear();
    test_find_and_has();
    test_reverse();
    test_slice();
    test_clone();
    test_capacity_growth();
    test_null_handling();
    test_bounds_errors();
    test_null_seq_errors();

    return 0;
}
