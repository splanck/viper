//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTStackTests.cpp
// Purpose: Comprehensive tests for Viper.Collections.Stack LIFO collection.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_stack.h"

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
    void *stack = rt_stack_new();
    assert(stack != nullptr);
    assert(rt_stack_len(stack) == 0);
    assert(rt_stack_is_empty(stack) == 1);
}

static void test_push_increases_length()
{
    void *stack = rt_stack_new();

    int a = 10, b = 20, c = 30;
    rt_stack_push(stack, &a);
    assert(rt_stack_len(stack) == 1);
    assert(rt_stack_is_empty(stack) == 0);

    rt_stack_push(stack, &b);
    assert(rt_stack_len(stack) == 2);

    rt_stack_push(stack, &c);
    assert(rt_stack_len(stack) == 3);
}

static void test_lifo_order()
{
    void *stack = rt_stack_new();

    int a = 10, b = 20, c = 30;
    rt_stack_push(stack, &a);
    rt_stack_push(stack, &b);
    rt_stack_push(stack, &c);

    // LIFO: last pushed should be popped first
    void *popped = rt_stack_pop(stack);
    assert(popped == &c);
    assert(rt_stack_len(stack) == 2);

    popped = rt_stack_pop(stack);
    assert(popped == &b);
    assert(rt_stack_len(stack) == 1);

    popped = rt_stack_pop(stack);
    assert(popped == &a);
    assert(rt_stack_len(stack) == 0);
    assert(rt_stack_is_empty(stack) == 1);
}

static void test_peek_returns_top_without_removing()
{
    void *stack = rt_stack_new();

    int a = 10, b = 20;
    rt_stack_push(stack, &a);
    rt_stack_push(stack, &b);

    // Peek should return top element
    assert(rt_stack_peek(stack) == &b);
    // Length should be unchanged
    assert(rt_stack_len(stack) == 2);

    // Multiple peeks should return same value
    assert(rt_stack_peek(stack) == &b);
    assert(rt_stack_peek(stack) == &b);
    assert(rt_stack_len(stack) == 2);

    // Pop and peek again
    rt_stack_pop(stack);
    assert(rt_stack_peek(stack) == &a);
    assert(rt_stack_len(stack) == 1);
}

static void test_clear_empties_stack()
{
    void *stack = rt_stack_new();

    int a = 10, b = 20, c = 30;
    rt_stack_push(stack, &a);
    rt_stack_push(stack, &b);
    rt_stack_push(stack, &c);

    assert(rt_stack_len(stack) == 3);
    assert(rt_stack_is_empty(stack) == 0);

    rt_stack_clear(stack);

    assert(rt_stack_len(stack) == 0);
    assert(rt_stack_is_empty(stack) == 1);

    // Clear on already empty should be safe
    rt_stack_clear(stack);
    assert(rt_stack_len(stack) == 0);
}

static void test_push_after_clear()
{
    void *stack = rt_stack_new();

    int a = 10, b = 20;
    rt_stack_push(stack, &a);
    rt_stack_push(stack, &b);
    rt_stack_clear(stack);

    int c = 30;
    rt_stack_push(stack, &c);
    assert(rt_stack_len(stack) == 1);
    assert(rt_stack_peek(stack) == &c);
}

static void test_capacity_growth()
{
    void *stack = rt_stack_new();

    // Push many elements to trigger capacity growth
    int vals[100];
    for (int i = 0; i < 100; ++i)
    {
        vals[i] = i;
        rt_stack_push(stack, &vals[i]);
    }

    assert(rt_stack_len(stack) == 100);

    // Verify LIFO order by popping all
    for (int i = 99; i >= 0; --i)
    {
        void *popped = rt_stack_pop(stack);
        assert(popped == &vals[i]);
    }

    assert(rt_stack_is_empty(stack) == 1);
}

static void test_null_handling()
{
    // Operations on null should return safe defaults
    assert(rt_stack_len(nullptr) == 0);
    assert(rt_stack_is_empty(nullptr) == 1);

    // Clear on null should not crash
    rt_stack_clear(nullptr);
}

static void test_pop_empty_traps()
{
    void *stack = rt_stack_new();
    EXPECT_TRAP(rt_stack_pop(stack));

    // Also test after pushing and popping
    int a = 10;
    rt_stack_push(stack, &a);
    rt_stack_pop(stack);
    EXPECT_TRAP(rt_stack_pop(stack));
}

static void test_peek_empty_traps()
{
    void *stack = rt_stack_new();
    EXPECT_TRAP(rt_stack_peek(stack));

    // Also test after clear
    int a = 10;
    rt_stack_push(stack, &a);
    rt_stack_clear(stack);
    EXPECT_TRAP(rt_stack_peek(stack));
}

static void test_null_stack_traps()
{
    int a = 10;

    EXPECT_TRAP(rt_stack_push(nullptr, &a));
    EXPECT_TRAP(rt_stack_pop(nullptr));
    EXPECT_TRAP(rt_stack_peek(nullptr));
}

static void test_push_null_value()
{
    void *stack = rt_stack_new();

    // Pushing null value should be allowed
    rt_stack_push(stack, nullptr);
    assert(rt_stack_len(stack) == 1);
    assert(rt_stack_peek(stack) == nullptr);
    assert(rt_stack_pop(stack) == nullptr);
    assert(rt_stack_is_empty(stack) == 1);
}

static void test_interleaved_operations()
{
    void *stack = rt_stack_new();

    int a = 1, b = 2, c = 3, d = 4;

    rt_stack_push(stack, &a);
    rt_stack_push(stack, &b);
    assert(rt_stack_pop(stack) == &b);

    rt_stack_push(stack, &c);
    rt_stack_push(stack, &d);
    assert(rt_stack_peek(stack) == &d);
    assert(rt_stack_len(stack) == 3);

    assert(rt_stack_pop(stack) == &d);
    assert(rt_stack_pop(stack) == &c);
    assert(rt_stack_pop(stack) == &a);
    assert(rt_stack_is_empty(stack) == 1);
}

int main()
{
    test_new_and_basic_properties();
    test_push_increases_length();
    test_lifo_order();
    test_peek_returns_top_without_removing();
    test_clear_empties_stack();
    test_push_after_clear();
    test_capacity_growth();
    test_null_handling();
    test_pop_empty_traps();
    test_peek_empty_traps();
    test_null_stack_traps();
    test_push_null_value();
    test_interleaved_operations();

    return 0;
}
