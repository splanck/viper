//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTQueueTests.cpp
// Purpose: Comprehensive tests for Viper.Collections.Queue FIFO collection.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_queue.h"

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

#define EXPECT_TRAP(expr)                                                      \
    do                                                                         \
    {                                                                          \
        g_trap_expected = true;                                                \
        g_last_trap = nullptr;                                                 \
        if (setjmp(g_trap_jmp) == 0)                                           \
        {                                                                      \
            expr;                                                              \
            assert(false && "Expected trap did not occur");                    \
        }                                                                      \
        g_trap_expected = false;                                               \
    } while (0)

static void test_new_and_basic_properties()
{
    void *queue = rt_queue_new();
    assert(queue != nullptr);
    assert(rt_queue_len(queue) == 0);
    assert(rt_queue_is_empty(queue) == 1);
}

static void test_add_increases_length()
{
    void *queue = rt_queue_new();

    int a = 10, b = 20, c = 30;
    rt_queue_add(queue, &a);
    assert(rt_queue_len(queue) == 1);
    assert(rt_queue_is_empty(queue) == 0);

    rt_queue_add(queue, &b);
    assert(rt_queue_len(queue) == 2);

    rt_queue_add(queue, &c);
    assert(rt_queue_len(queue) == 3);
}

static void test_fifo_order()
{
    void *queue = rt_queue_new();

    int a = 10, b = 20, c = 30;
    rt_queue_add(queue, &a);
    rt_queue_add(queue, &b);
    rt_queue_add(queue, &c);

    // FIFO: first added should be taken first
    void *taken = rt_queue_take(queue);
    assert(taken == &a);
    assert(rt_queue_len(queue) == 2);

    taken = rt_queue_take(queue);
    assert(taken == &b);
    assert(rt_queue_len(queue) == 1);

    taken = rt_queue_take(queue);
    assert(taken == &c);
    assert(rt_queue_len(queue) == 0);
    assert(rt_queue_is_empty(queue) == 1);
}

static void test_peek_returns_front_without_removing()
{
    void *queue = rt_queue_new();

    int a = 10, b = 20;
    rt_queue_add(queue, &a);
    rt_queue_add(queue, &b);

    // Peek should return front element (first added)
    assert(rt_queue_peek(queue) == &a);
    // Length should be unchanged
    assert(rt_queue_len(queue) == 2);

    // Multiple peeks should return same value
    assert(rt_queue_peek(queue) == &a);
    assert(rt_queue_peek(queue) == &a);
    assert(rt_queue_len(queue) == 2);

    // Take and peek again
    rt_queue_take(queue);
    assert(rt_queue_peek(queue) == &b);
    assert(rt_queue_len(queue) == 1);
}

static void test_clear_empties_queue()
{
    void *queue = rt_queue_new();

    int a = 10, b = 20, c = 30;
    rt_queue_add(queue, &a);
    rt_queue_add(queue, &b);
    rt_queue_add(queue, &c);

    assert(rt_queue_len(queue) == 3);
    assert(rt_queue_is_empty(queue) == 0);

    rt_queue_clear(queue);

    assert(rt_queue_len(queue) == 0);
    assert(rt_queue_is_empty(queue) == 1);

    // Clear on already empty should be safe
    rt_queue_clear(queue);
    assert(rt_queue_len(queue) == 0);
}

static void test_add_after_clear()
{
    void *queue = rt_queue_new();

    int a = 10, b = 20;
    rt_queue_add(queue, &a);
    rt_queue_add(queue, &b);
    rt_queue_clear(queue);

    int c = 30;
    rt_queue_add(queue, &c);
    assert(rt_queue_len(queue) == 1);
    assert(rt_queue_peek(queue) == &c);
}

static void test_wrap_around()
{
    void *queue = rt_queue_new();

    // Add and take to move head/tail indices
    int vals[10];
    for (int i = 0; i < 10; ++i)
    {
        vals[i] = i;
        rt_queue_add(queue, &vals[i]);
    }
    for (int i = 0; i < 8; ++i)
    {
        void *taken = rt_queue_take(queue);
        assert(taken == &vals[i]);
    }

    // Now head is at index 8, tail is at index 10
    // Add more to trigger wrap-around
    int more[10];
    for (int i = 0; i < 10; ++i)
    {
        more[i] = 100 + i;
        rt_queue_add(queue, &more[i]);
    }

    // Take remaining and verify FIFO order
    assert(rt_queue_take(queue) == &vals[8]);
    assert(rt_queue_take(queue) == &vals[9]);
    for (int i = 0; i < 10; ++i)
    {
        void *taken = rt_queue_take(queue);
        assert(taken == &more[i]);
    }
    assert(rt_queue_is_empty(queue) == 1);
}

static void test_capacity_growth()
{
    void *queue = rt_queue_new();

    // Add many elements to trigger capacity growth
    int vals[100];
    for (int i = 0; i < 100; ++i)
    {
        vals[i] = i;
        rt_queue_add(queue, &vals[i]);
    }

    assert(rt_queue_len(queue) == 100);

    // Verify FIFO order by taking all
    for (int i = 0; i < 100; ++i)
    {
        void *taken = rt_queue_take(queue);
        assert(taken == &vals[i]);
    }

    assert(rt_queue_is_empty(queue) == 1);
}

static void test_growth_with_wrap_around()
{
    void *queue = rt_queue_new();

    // Fill half, take half to move head
    int first[8];
    for (int i = 0; i < 8; ++i)
    {
        first[i] = i;
        rt_queue_add(queue, &first[i]);
    }
    for (int i = 0; i < 6; ++i)
    {
        rt_queue_take(queue);
    }

    // Now add enough to trigger growth with wrapped data
    int second[20];
    for (int i = 0; i < 20; ++i)
    {
        second[i] = 100 + i;
        rt_queue_add(queue, &second[i]);
    }

    // Verify remaining first elements
    assert(rt_queue_take(queue) == &first[6]);
    assert(rt_queue_take(queue) == &first[7]);

    // Verify second elements
    for (int i = 0; i < 20; ++i)
    {
        void *taken = rt_queue_take(queue);
        assert(taken == &second[i]);
    }

    assert(rt_queue_is_empty(queue) == 1);
}

static void test_null_handling()
{
    // Operations on null should return safe defaults
    assert(rt_queue_len(nullptr) == 0);
    assert(rt_queue_is_empty(nullptr) == 1);

    // Clear on null should not crash
    rt_queue_clear(nullptr);
}

static void test_take_empty_traps()
{
    void *queue = rt_queue_new();
    EXPECT_TRAP(rt_queue_take(queue));

    // Also test after adding and taking
    int a = 10;
    rt_queue_add(queue, &a);
    rt_queue_take(queue);
    EXPECT_TRAP(rt_queue_take(queue));
}

static void test_peek_empty_traps()
{
    void *queue = rt_queue_new();
    EXPECT_TRAP(rt_queue_peek(queue));

    // Also test after clear
    int a = 10;
    rt_queue_add(queue, &a);
    rt_queue_clear(queue);
    EXPECT_TRAP(rt_queue_peek(queue));
}

static void test_null_queue_traps()
{
    int a = 10;

    EXPECT_TRAP(rt_queue_add(nullptr, &a));
    EXPECT_TRAP(rt_queue_take(nullptr));
    EXPECT_TRAP(rt_queue_peek(nullptr));
}

static void test_add_null_value()
{
    void *queue = rt_queue_new();

    // Adding null value should be allowed
    rt_queue_add(queue, nullptr);
    assert(rt_queue_len(queue) == 1);
    assert(rt_queue_peek(queue) == nullptr);
    assert(rt_queue_take(queue) == nullptr);
    assert(rt_queue_is_empty(queue) == 1);
}

static void test_interleaved_operations()
{
    void *queue = rt_queue_new();

    int a = 1, b = 2, c = 3, d = 4;

    rt_queue_add(queue, &a);
    rt_queue_add(queue, &b);
    assert(rt_queue_take(queue) == &a);

    rt_queue_add(queue, &c);
    rt_queue_add(queue, &d);
    assert(rt_queue_peek(queue) == &b);
    assert(rt_queue_len(queue) == 3);

    assert(rt_queue_take(queue) == &b);
    assert(rt_queue_take(queue) == &c);
    assert(rt_queue_take(queue) == &d);
    assert(rt_queue_is_empty(queue) == 1);
}

int main()
{
    test_new_and_basic_properties();
    test_add_increases_length();
    test_fifo_order();
    test_peek_returns_front_without_removing();
    test_clear_empties_queue();
    test_add_after_clear();
    test_wrap_around();
    test_capacity_growth();
    test_growth_with_wrap_around();
    test_null_handling();
    test_take_empty_traps();
    test_peek_empty_traps();
    test_null_queue_traps();
    test_add_null_value();
    test_interleaved_operations();

    return 0;
}
