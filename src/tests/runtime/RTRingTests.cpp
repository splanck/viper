//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTRingTests.cpp
// Purpose: Comprehensive tests for Viper.Collections.Ring fixed-size circular
//          buffer collection.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_ring.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg)
{
    rt_abort(msg);
}

static void test_new_and_basic_properties()
{
    void *ring = rt_ring_new(10);
    assert(ring != nullptr);
    assert(rt_ring_len(ring) == 0);
    assert(rt_ring_cap(ring) == 10);
    assert(rt_ring_is_empty(ring) == 1);
    assert(rt_ring_is_full(ring) == 0);
}

static void test_capacity_clamped_to_minimum()
{
    // Capacity of 0 or negative should clamp to 1
    void *ring = rt_ring_new(0);
    assert(ring != nullptr);
    assert(rt_ring_cap(ring) == 1);

    ring = rt_ring_new(-5);
    assert(ring != nullptr);
    assert(rt_ring_cap(ring) == 1);
}

static void test_push_increases_length()
{
    void *ring = rt_ring_new(10);

    int a = 10, b = 20, c = 30;
    rt_ring_push(ring, &a);
    assert(rt_ring_len(ring) == 1);
    assert(rt_ring_is_empty(ring) == 0);
    assert(rt_ring_is_full(ring) == 0);

    rt_ring_push(ring, &b);
    assert(rt_ring_len(ring) == 2);

    rt_ring_push(ring, &c);
    assert(rt_ring_len(ring) == 3);
}

static void test_push_until_full()
{
    void *ring = rt_ring_new(3);

    int a = 10, b = 20, c = 30;
    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);
    rt_ring_push(ring, &c);

    assert(rt_ring_len(ring) == 3);
    assert(rt_ring_cap(ring) == 3);
    assert(rt_ring_is_full(ring) == 1);
}

static void test_fifo_order()
{
    void *ring = rt_ring_new(10);

    int a = 10, b = 20, c = 30;
    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);
    rt_ring_push(ring, &c);

    // FIFO: first pushed should be popped first
    void *popped = rt_ring_pop(ring);
    assert(popped == &a);
    assert(rt_ring_len(ring) == 2);

    popped = rt_ring_pop(ring);
    assert(popped == &b);
    assert(rt_ring_len(ring) == 1);

    popped = rt_ring_pop(ring);
    assert(popped == &c);
    assert(rt_ring_len(ring) == 0);
    assert(rt_ring_is_empty(ring) == 1);
}

static void test_peek_returns_oldest_without_removing()
{
    void *ring = rt_ring_new(10);

    int a = 10, b = 20;
    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);

    // Peek should return oldest element (first pushed)
    assert(rt_ring_peek(ring) == &a);
    // Length should be unchanged
    assert(rt_ring_len(ring) == 2);

    // Multiple peeks should return same value
    assert(rt_ring_peek(ring) == &a);
    assert(rt_ring_peek(ring) == &a);
    assert(rt_ring_len(ring) == 2);

    // Pop and peek again
    rt_ring_pop(ring);
    assert(rt_ring_peek(ring) == &b);
    assert(rt_ring_len(ring) == 1);
}

static void test_get_by_index()
{
    void *ring = rt_ring_new(10);

    int a = 10, b = 20, c = 30;
    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);
    rt_ring_push(ring, &c);

    // Get(0) = oldest, Get(len-1) = newest
    assert(rt_ring_get(ring, 0) == &a);
    assert(rt_ring_get(ring, 1) == &b);
    assert(rt_ring_get(ring, 2) == &c);

    // Length unchanged
    assert(rt_ring_len(ring) == 3);
}

static void test_get_out_of_bounds_returns_null()
{
    void *ring = rt_ring_new(10);

    int a = 10;
    rt_ring_push(ring, &a);

    assert(rt_ring_get(ring, -1) == nullptr);
    assert(rt_ring_get(ring, 1) == nullptr);
    assert(rt_ring_get(ring, 100) == nullptr);
}

static void test_overwrite_oldest_when_full()
{
    void *ring = rt_ring_new(3);

    int a = 10, b = 20, c = 30, d = 40, e = 50;
    rt_ring_push(ring, &a); // [a, _, _]
    rt_ring_push(ring, &b); // [a, b, _]
    rt_ring_push(ring, &c); // [a, b, c] - now full

    assert(rt_ring_len(ring) == 3);
    assert(rt_ring_is_full(ring) == 1);

    // Push when full should overwrite oldest (a)
    rt_ring_push(ring, &d); // [d, b, c] logically [b, c, d]
    assert(rt_ring_len(ring) == 3); // Still 3
    assert(rt_ring_is_full(ring) == 1);

    // Oldest should now be 'b', not 'a'
    assert(rt_ring_peek(ring) == &b);
    assert(rt_ring_get(ring, 0) == &b);
    assert(rt_ring_get(ring, 1) == &c);
    assert(rt_ring_get(ring, 2) == &d);

    // Push another to overwrite 'b'
    rt_ring_push(ring, &e); // logically [c, d, e]
    assert(rt_ring_peek(ring) == &c);
    assert(rt_ring_get(ring, 0) == &c);
    assert(rt_ring_get(ring, 1) == &d);
    assert(rt_ring_get(ring, 2) == &e);
}

static void test_clear_empties_ring()
{
    void *ring = rt_ring_new(5);

    int a = 10, b = 20, c = 30;
    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);
    rt_ring_push(ring, &c);

    assert(rt_ring_len(ring) == 3);
    assert(rt_ring_is_empty(ring) == 0);

    rt_ring_clear(ring);

    assert(rt_ring_len(ring) == 0);
    assert(rt_ring_is_empty(ring) == 1);
    assert(rt_ring_is_full(ring) == 0);
    // Capacity unchanged
    assert(rt_ring_cap(ring) == 5);

    // Clear on already empty should be safe
    rt_ring_clear(ring);
    assert(rt_ring_len(ring) == 0);
}

static void test_push_after_clear()
{
    void *ring = rt_ring_new(5);

    int a = 10, b = 20;
    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);
    rt_ring_clear(ring);

    int c = 30;
    rt_ring_push(ring, &c);
    assert(rt_ring_len(ring) == 1);
    assert(rt_ring_peek(ring) == &c);
}

static void test_wrap_around_indices()
{
    void *ring = rt_ring_new(5);

    // Fill the ring
    int vals[5] = {10, 20, 30, 40, 50};
    for (int i = 0; i < 5; ++i)
    {
        rt_ring_push(ring, &vals[i]);
    }

    // Pop 3 elements to advance head
    assert(rt_ring_pop(ring) == &vals[0]);
    assert(rt_ring_pop(ring) == &vals[1]);
    assert(rt_ring_pop(ring) == &vals[2]);

    // Now ring has [_, _, _, 40, 50] with head=3, count=2
    assert(rt_ring_len(ring) == 2);

    // Push 4 more elements - this will wrap around
    int more[4] = {60, 70, 80, 90};
    for (int i = 0; i < 4; ++i)
    {
        rt_ring_push(ring, &more[i]);
    }

    // Now we have 5 elements wrapping around: [70, 80, 90, 40, 50]
    // But we pushed 6 total to 2 existing, that's 6 pushes to cap 5
    // After 3 pushes: count=5, full
    // 4th push overwrites oldest (40), so we have [50, 60, 70, 80, 90]

    // Actually let's trace through:
    // Start: [_, _, _, 40, 50], head=3, count=2
    // Push 60: tail=(3+2)%5=0, [60, _, _, 40, 50], head=3, count=3
    // Push 70: tail=(3+3)%5=1, [60, 70, _, 40, 50], head=3, count=4
    // Push 80: tail=(3+4)%5=2, [60, 70, 80, 40, 50], head=3, count=5 (full)
    // Push 90: full, overwrite at head=3, [60, 70, 80, 90, 50], head=4, count=5
    //          wait, but 90 is stored at the old head=3 position
    //          Hmm, looking at the code:
    //          ring->items[ring->head] = item; then head = (head+1)%cap
    //          So we store at head=3 then head becomes 4
    //          Result: [60, 70, 80, 90, 50], head=4, count=5
    // Logical order (from head): 50, 60, 70, 80, 90

    assert(rt_ring_len(ring) == 5);
    assert(rt_ring_is_full(ring) == 1);
    assert(rt_ring_get(ring, 0) == &vals[4]); // 50
    assert(rt_ring_get(ring, 1) == &more[0]); // 60
    assert(rt_ring_get(ring, 2) == &more[1]); // 70
    assert(rt_ring_get(ring, 3) == &more[2]); // 80
    assert(rt_ring_get(ring, 4) == &more[3]); // 90
}

static void test_full_cycle_with_pop()
{
    void *ring = rt_ring_new(3);

    // Go through multiple full cycles
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        int vals[3] = {cycle * 10 + 1, cycle * 10 + 2, cycle * 10 + 3};
        rt_ring_push(ring, &vals[0]);
        rt_ring_push(ring, &vals[1]);
        rt_ring_push(ring, &vals[2]);

        assert(rt_ring_is_full(ring) == 1);

        assert(rt_ring_pop(ring) == &vals[0]);
        assert(rt_ring_pop(ring) == &vals[1]);
        assert(rt_ring_pop(ring) == &vals[2]);

        assert(rt_ring_is_empty(ring) == 1);
    }
}

static void test_null_handling()
{
    // Operations on null should return safe defaults
    assert(rt_ring_len(nullptr) == 0);
    assert(rt_ring_cap(nullptr) == 0);
    assert(rt_ring_is_empty(nullptr) == 1); // len==0 means empty (consistent with Stack/Queue)
    assert(rt_ring_is_full(nullptr) == 0);
    assert(rt_ring_pop(nullptr) == nullptr);
    assert(rt_ring_peek(nullptr) == nullptr);
    assert(rt_ring_get(nullptr, 0) == nullptr);

    // Push and clear on null should not crash
    int a = 10;
    rt_ring_push(nullptr, &a);
    rt_ring_clear(nullptr);
}

static void test_pop_empty_returns_null()
{
    void *ring = rt_ring_new(3);

    // Pop on empty returns NULL (not a trap for Ring)
    assert(rt_ring_pop(ring) == nullptr);

    // Also test after adding and popping all
    int a = 10;
    rt_ring_push(ring, &a);
    rt_ring_pop(ring);
    assert(rt_ring_pop(ring) == nullptr);
}

static void test_peek_empty_returns_null()
{
    void *ring = rt_ring_new(3);

    // Peek on empty returns NULL
    assert(rt_ring_peek(ring) == nullptr);

    // Also test after clear
    int a = 10;
    rt_ring_push(ring, &a);
    rt_ring_clear(ring);
    assert(rt_ring_peek(ring) == nullptr);
}

static void test_push_null_value()
{
    void *ring = rt_ring_new(5);

    // Pushing null value should be allowed
    rt_ring_push(ring, nullptr);
    assert(rt_ring_len(ring) == 1);
    assert(rt_ring_peek(ring) == nullptr);
    assert(rt_ring_pop(ring) == nullptr);
    assert(rt_ring_is_empty(ring) == 1);
}

static void test_interleaved_operations()
{
    void *ring = rt_ring_new(4);

    int a = 1, b = 2, c = 3, d = 4, e = 5;

    rt_ring_push(ring, &a);
    rt_ring_push(ring, &b);
    assert(rt_ring_pop(ring) == &a);

    rt_ring_push(ring, &c);
    rt_ring_push(ring, &d);
    assert(rt_ring_peek(ring) == &b);
    assert(rt_ring_len(ring) == 3);

    rt_ring_push(ring, &e);
    assert(rt_ring_is_full(ring) == 1);

    assert(rt_ring_pop(ring) == &b);
    assert(rt_ring_pop(ring) == &c);
    assert(rt_ring_pop(ring) == &d);
    assert(rt_ring_pop(ring) == &e);
    assert(rt_ring_is_empty(ring) == 1);
}

static void test_capacity_one()
{
    void *ring = rt_ring_new(1);

    assert(rt_ring_cap(ring) == 1);
    assert(rt_ring_is_empty(ring) == 1);
    assert(rt_ring_is_full(ring) == 0);

    int a = 10, b = 20;
    rt_ring_push(ring, &a);
    assert(rt_ring_len(ring) == 1);
    assert(rt_ring_is_full(ring) == 1);
    assert(rt_ring_peek(ring) == &a);

    // Push when full should overwrite
    rt_ring_push(ring, &b);
    assert(rt_ring_len(ring) == 1);
    assert(rt_ring_peek(ring) == &b);

    assert(rt_ring_pop(ring) == &b);
    assert(rt_ring_is_empty(ring) == 1);
}

static void test_large_capacity()
{
    void *ring = rt_ring_new(1000);

    assert(rt_ring_cap(ring) == 1000);

    // Fill to capacity
    int vals[1000];
    for (int i = 0; i < 1000; ++i)
    {
        vals[i] = i;
        rt_ring_push(ring, &vals[i]);
    }

    assert(rt_ring_len(ring) == 1000);
    assert(rt_ring_is_full(ring) == 1);

    // Verify all elements
    for (int i = 0; i < 1000; ++i)
    {
        assert(rt_ring_get(ring, i) == &vals[i]);
    }

    // Pop all and verify FIFO
    for (int i = 0; i < 1000; ++i)
    {
        assert(rt_ring_pop(ring) == &vals[i]);
    }

    assert(rt_ring_is_empty(ring) == 1);
}

static void test_overwrite_sequence()
{
    // Test a longer sequence of overwrites
    void *ring = rt_ring_new(3);

    int vals[10];
    for (int i = 0; i < 10; ++i)
    {
        vals[i] = i * 10;
    }

    // Push all 10 values - only last 3 should remain
    for (int i = 0; i < 10; ++i)
    {
        rt_ring_push(ring, &vals[i]);
    }

    assert(rt_ring_len(ring) == 3);
    assert(rt_ring_is_full(ring) == 1);

    // Should have vals[7], vals[8], vals[9]
    assert(rt_ring_get(ring, 0) == &vals[7]);
    assert(rt_ring_get(ring, 1) == &vals[8]);
    assert(rt_ring_get(ring, 2) == &vals[9]);

    assert(rt_ring_pop(ring) == &vals[7]);
    assert(rt_ring_pop(ring) == &vals[8]);
    assert(rt_ring_pop(ring) == &vals[9]);
}

int main()
{
    test_new_and_basic_properties();
    test_capacity_clamped_to_minimum();
    test_push_increases_length();
    test_push_until_full();
    test_fifo_order();
    test_peek_returns_oldest_without_removing();
    test_get_by_index();
    test_get_out_of_bounds_returns_null();
    test_overwrite_oldest_when_full();
    test_clear_empties_ring();
    test_push_after_clear();
    test_wrap_around_indices();
    test_full_cycle_with_pop();
    test_null_handling();
    test_pop_empty_returns_null();
    test_peek_empty_returns_null();
    test_push_null_value();
    test_interleaved_operations();
    test_capacity_one();
    test_large_capacity();
    test_overwrite_sequence();

    return 0;
}
