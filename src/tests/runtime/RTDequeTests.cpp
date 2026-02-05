//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTDequeTests.cpp
// Purpose: Validate Deque (double-ended queue).
//
//===----------------------------------------------------------------------===//

#include "rt_deque.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// Deque Tests
//=============================================================================

static void test_deque_creation()
{
    printf("Testing Deque Creation:\n");

    // Test 1: Create empty deque
    {
        void *d = rt_deque_new();
        test_result("New deque is empty", rt_deque_is_empty(d) == 1);
        test_result("New deque len is 0", rt_deque_len(d) == 0);
    }

    // Test 2: Create with capacity
    {
        void *d = rt_deque_with_capacity(100);
        test_result("Deque with capacity", rt_deque_cap(d) >= 100);
    }

    printf("\n");
}

static void test_deque_front_operations()
{
    printf("Testing Deque Front Operations:\n");

    int a = 1, b = 2, c = 3;

    // Test 1: PushFront and PeekFront
    {
        void *d = rt_deque_new();
        rt_deque_push_front(d, &a);
        test_result("PushFront increases len", rt_deque_len(d) == 1);
        test_result("PeekFront returns value", rt_deque_peek_front(d) == &a);
    }

    // Test 2: Multiple PushFront (LIFO order at front)
    {
        void *d = rt_deque_new();
        rt_deque_push_front(d, &a);
        rt_deque_push_front(d, &b);
        rt_deque_push_front(d, &c);
        test_result("Multiple PushFront len", rt_deque_len(d) == 3);
        test_result("Front is last pushed", rt_deque_peek_front(d) == &c);
    }

    // Test 3: PopFront
    {
        void *d = rt_deque_new();
        rt_deque_push_front(d, &a);
        rt_deque_push_front(d, &b);
        void *result = rt_deque_pop_front(d);
        test_result("PopFront returns front", result == &b);
        test_result("PopFront decreases len", rt_deque_len(d) == 1);
    }

    printf("\n");
}

static void test_deque_back_operations()
{
    printf("Testing Deque Back Operations:\n");

    int a = 1, b = 2, c = 3;

    // Test 1: PushBack and PeekBack
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        test_result("PushBack increases len", rt_deque_len(d) == 1);
        test_result("PeekBack returns value", rt_deque_peek_back(d) == &a);
    }

    // Test 2: Multiple PushBack (FIFO order)
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);
        rt_deque_push_back(d, &c);
        test_result("Multiple PushBack len", rt_deque_len(d) == 3);
        test_result("Back is last pushed", rt_deque_peek_back(d) == &c);
        test_result("Front is first pushed", rt_deque_peek_front(d) == &a);
    }

    // Test 3: PopBack
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);
        void *result = rt_deque_pop_back(d);
        test_result("PopBack returns back", result == &b);
        test_result("PopBack decreases len", rt_deque_len(d) == 1);
    }

    printf("\n");
}

static void test_deque_mixed_operations()
{
    printf("Testing Deque Mixed Operations:\n");

    int a = 1, b = 2, c = 3, d_val = 4;

    // Test: Push and pop from both ends
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);   // [a]
        rt_deque_push_front(d, &b);  // [b, a]
        rt_deque_push_back(d, &c);   // [b, a, c]
        rt_deque_push_front(d, &d_val); // [d, b, a, c]

        test_result("Mixed push len", rt_deque_len(d) == 4);
        test_result("Front after mixed push", rt_deque_peek_front(d) == &d_val);
        test_result("Back after mixed push", rt_deque_peek_back(d) == &c);

        void *f = rt_deque_pop_front(d); // [b, a, c]
        test_result("PopFront result", f == &d_val);

        void *back = rt_deque_pop_back(d); // [b, a]
        test_result("PopBack result", back == &c);

        test_result("Final len", rt_deque_len(d) == 2);
    }

    printf("\n");
}

static void test_deque_random_access()
{
    printf("Testing Deque Random Access:\n");

    int a = 1, b = 2, c = 3;

    // Test: Get and Set
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);
        rt_deque_push_back(d, &c);

        test_result("Get index 0", rt_deque_get(d, 0) == &a);
        test_result("Get index 1", rt_deque_get(d, 1) == &b);
        test_result("Get index 2", rt_deque_get(d, 2) == &c);

        int new_val = 99;
        rt_deque_set(d, 1, &new_val);
        test_result("Set updates value", rt_deque_get(d, 1) == &new_val);
    }

    printf("\n");
}

static void test_deque_utility()
{
    printf("Testing Deque Utility:\n");

    int a = 1, b = 2, c = 3;

    // Test 1: Clear
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);
        rt_deque_clear(d);
        test_result("Clear makes empty", rt_deque_is_empty(d) == 1);
    }

    // Test 2: Has
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);
        test_result("Has finds element", rt_deque_has(d, &a) == 1);
        test_result("Has returns 0 for missing", rt_deque_has(d, &c) == 0);
    }

    // Test 3: Reverse
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);
        rt_deque_push_back(d, &c);
        rt_deque_reverse(d);
        test_result("Reverse front", rt_deque_peek_front(d) == &c);
        test_result("Reverse back", rt_deque_peek_back(d) == &a);
    }

    // Test 4: Clone
    {
        void *d = rt_deque_new();
        rt_deque_push_back(d, &a);
        rt_deque_push_back(d, &b);

        void *clone = rt_deque_clone(d);
        test_result("Clone has same len", rt_deque_len(clone) == rt_deque_len(d));
        test_result("Clone has same front", rt_deque_peek_front(clone) == rt_deque_peek_front(d));
        test_result("Clone has same back", rt_deque_peek_back(clone) == rt_deque_peek_back(d));

        // Modifying clone doesn't affect original
        rt_deque_pop_front(clone);
        test_result("Clone is independent", rt_deque_len(d) == 2);
    }

    printf("\n");
}

static void test_deque_wraparound()
{
    printf("Testing Deque Wraparound:\n");

    // Test circular buffer wraparound behavior
    {
        void *d = rt_deque_with_capacity(4);
        int vals[10];
        for (int i = 0; i < 10; i++)
            vals[i] = i;

        // Fill to capacity
        rt_deque_push_back(d, &vals[0]);
        rt_deque_push_back(d, &vals[1]);
        rt_deque_push_back(d, &vals[2]);
        rt_deque_push_back(d, &vals[3]);

        // Pop from front, push to back - causes wraparound
        rt_deque_pop_front(d);
        rt_deque_push_back(d, &vals[4]);

        test_result("Wraparound maintains len", rt_deque_len(d) == 4);
        test_result("Wraparound front correct", rt_deque_peek_front(d) == &vals[1]);
        test_result("Wraparound back correct", rt_deque_peek_back(d) == &vals[4]);

        // Pop again and verify order
        test_result("Order after wraparound 0", rt_deque_pop_front(d) == &vals[1]);
        test_result("Order after wraparound 1", rt_deque_pop_front(d) == &vals[2]);
        test_result("Order after wraparound 2", rt_deque_pop_front(d) == &vals[3]);
        test_result("Order after wraparound 3", rt_deque_pop_front(d) == &vals[4]);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Deque Tests ===\n\n");

    test_deque_creation();
    test_deque_front_operations();
    test_deque_back_operations();
    test_deque_mixed_operations();
    test_deque_random_access();
    test_deque_utility();
    test_deque_wraparound();

    printf("All Deque tests passed!\n");
    return 0;
}
