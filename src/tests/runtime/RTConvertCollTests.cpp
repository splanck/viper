//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTConvertCollTests.cpp
// Purpose: Validate collection conversion utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_convert_coll.h"
#include "rt_deque.h"
#include "rt_list.h"
#include "rt_object.h"
#include "rt_queue.h"
#include "rt_ring.h"
#include "rt_seq.h"
#include "rt_set.h"
#include "rt_stack.h"

#include <cassert>
#include <cstdio>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Create a new GC-managed object for testing.
static void *new_obj()
{
    void *p = rt_obj_new_i64(0, 8);
    assert(p != nullptr);
    return p;
}

// Test values - created fresh for each test
static void *val1 = nullptr;
static void *val2 = nullptr;
static void *val3 = nullptr;
static void *val4 = nullptr;

static void setup_test_values()
{
    val1 = new_obj();
    val2 = new_obj();
    val3 = new_obj();
    val4 = new_obj();
}

//=============================================================================
// Seq Conversion Tests
//=============================================================================

static void test_seq_to_list()
{
    printf("Testing Seq to List:\n");

    void *seq = rt_seq_new();
    rt_seq_push(seq, val1);
    rt_seq_push(seq, val2);
    rt_seq_push(seq, val3);

    void *list = rt_seq_to_list(seq);
    test_result("List created", list != NULL);
    test_result("List has 3 elements", rt_list_len(list) == 3);
    test_result("First element correct", rt_list_get(list, 0) == val1);
    test_result("Third element correct", rt_list_get(list, 2) == val3);

    printf("\n");
}

static void test_seq_to_set()
{
    printf("Testing Seq to Set:\n");

    void *seq = rt_seq_new();
    rt_seq_push(seq, val1);
    rt_seq_push(seq, val2);
    rt_seq_push(seq, val1); // Duplicate

    void *set = rt_seq_to_set(seq);
    test_result("Set created", set != NULL);
    test_result("Set has 2 unique elements", rt_set_len(set) == 2);
    test_result("Set contains val1", rt_set_has(set, val1) == 1);
    test_result("Set contains val2", rt_set_has(set, val2) == 1);

    printf("\n");
}

static void test_seq_to_stack()
{
    printf("Testing Seq to Stack:\n");

    void *seq = rt_seq_new();
    rt_seq_push(seq, val1);
    rt_seq_push(seq, val2);

    void *stack = rt_seq_to_stack(seq);
    test_result("Stack created", stack != NULL);
    test_result("Stack has 2 elements", rt_stack_len(stack) == 2);
    // Top of stack should be last element added (val2)
    test_result("Top is val2", rt_stack_peek(stack) == val2);

    printf("\n");
}

static void test_seq_to_queue()
{
    printf("Testing Seq to Queue:\n");

    void *seq = rt_seq_new();
    rt_seq_push(seq, val1);
    rt_seq_push(seq, val2);

    void *queue = rt_seq_to_queue(seq);
    test_result("Queue created", queue != NULL);
    test_result("Queue has 2 elements", rt_queue_len(queue) == 2);
    // Front of queue should be first element added (val1)
    test_result("Front is val1", rt_queue_peek(queue) == val1);

    printf("\n");
}

static void test_seq_to_deque()
{
    printf("Testing Seq to Deque:\n");

    void *seq = rt_seq_new();
    rt_seq_push(seq, val1);
    rt_seq_push(seq, val2);
    rt_seq_push(seq, val3);

    void *deque = rt_seq_to_deque(seq);
    test_result("Deque created", deque != NULL);
    test_result("Deque has 3 elements", rt_deque_len(deque) == 3);
    test_result("Front is val1", rt_deque_peek_front(deque) == val1);
    test_result("Back is val3", rt_deque_peek_back(deque) == val3);

    printf("\n");
}

//=============================================================================
// List Conversion Tests
//=============================================================================

static void test_list_to_seq()
{
    printf("Testing List to Seq:\n");

    void *list = rt_ns_list_new();
    rt_list_push(list, val1);
    rt_list_push(list, val2);

    void *seq = rt_list_to_seq(list);
    test_result("Seq created", seq != NULL);
    test_result("Seq has 2 elements", rt_seq_len(seq) == 2);
    test_result("First element correct", rt_seq_get(seq, 0) == val1);

    printf("\n");
}

static void test_list_to_set()
{
    printf("Testing List to Set:\n");

    void *list = rt_ns_list_new();
    rt_list_push(list, val1);
    rt_list_push(list, val1); // Duplicate

    void *set = rt_list_to_set(list);
    test_result("Set created", set != NULL);
    test_result("Set has 1 unique element", rt_set_len(set) == 1);

    printf("\n");
}

//=============================================================================
// Set Conversion Tests
//=============================================================================

static void test_set_to_seq()
{
    printf("Testing Set to Seq:\n");

    void *set = rt_set_new();
    rt_set_put(set, val1);
    rt_set_put(set, val2);

    void *seq = rt_set_to_seq(set);
    test_result("Seq created", seq != NULL);
    test_result("Seq has 2 elements", rt_seq_len(seq) == 2);

    printf("\n");
}

//=============================================================================
// Deque Conversion Tests
//=============================================================================

static void test_deque_to_seq()
{
    printf("Testing Deque to Seq:\n");

    void *deque = rt_deque_new();
    rt_deque_push_back(deque, val1);
    rt_deque_push_back(deque, val2);
    rt_deque_push_back(deque, val3);

    void *seq = rt_deque_to_seq(deque);
    test_result("Seq created", seq != NULL);
    test_result("Seq has 3 elements", rt_seq_len(seq) == 3);
    test_result("Order preserved: first is val1", rt_seq_get(seq, 0) == val1);
    test_result("Order preserved: last is val3", rt_seq_get(seq, 2) == val3);

    printf("\n");
}

//=============================================================================
// Ring Conversion Tests
//=============================================================================

static void test_ring_to_seq()
{
    printf("Testing Ring to Seq:\n");

    void *ring = rt_ring_new(4);
    rt_ring_push(ring, val1);
    rt_ring_push(ring, val2);

    void *seq = rt_ring_to_seq(ring);
    test_result("Seq created", seq != NULL);
    test_result("Seq has 2 elements", rt_seq_len(seq) == 2);

    printf("\n");
}

//=============================================================================
// Utility Function Tests
//=============================================================================

static void test_seq_of()
{
    printf("Testing rt_seq_of:\n");

    void *seq = rt_seq_of(3, val1, val2, val3);
    test_result("Seq created", seq != NULL);
    test_result("Seq has 3 elements", rt_seq_len(seq) == 3);
    test_result("First element correct", rt_seq_get(seq, 0) == val1);
    test_result("Second element correct", rt_seq_get(seq, 1) == val2);
    test_result("Third element correct", rt_seq_get(seq, 2) == val3);

    printf("\n");
}

static void test_list_of()
{
    printf("Testing rt_list_of:\n");

    void *list = rt_list_of(2, val1, val2);
    test_result("List created", list != NULL);
    test_result("List has 2 elements", rt_list_len(list) == 2);

    printf("\n");
}

static void test_set_of()
{
    printf("Testing rt_set_of:\n");

    void *set = rt_set_of(3, val1, val2, val1); // Duplicate
    test_result("Set created", set != NULL);
    test_result("Set has 2 unique elements", rt_set_len(set) == 2);

    printf("\n");
}

//=============================================================================
// NULL Handling Tests
//=============================================================================

static void test_null_handling()
{
    printf("Testing NULL handling:\n");

    void *list = rt_seq_to_list(NULL);
    test_result("NULL seq to list returns empty list",
                list != NULL && rt_list_len(list) == 0);

    void *seq = rt_list_to_seq(NULL);
    test_result("NULL list to seq returns empty seq", seq != NULL && rt_seq_len(seq) == 0);

    void *set = rt_seq_to_set(NULL);
    test_result("NULL seq to set returns empty set", set != NULL && rt_set_len(set) == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Collection Conversion Tests ===\n\n");

    // Setup test values (GC-managed objects)
    setup_test_values();

    // Seq conversions
    test_seq_to_list();
    test_seq_to_set();
    test_seq_to_stack();
    test_seq_to_queue();
    test_seq_to_deque();

    // List conversions
    test_list_to_seq();
    test_list_to_set();

    // Set conversions
    test_set_to_seq();

    // Deque conversions
    test_deque_to_seq();

    // Ring conversions
    test_ring_to_seq();

    // Utility functions
    test_seq_of();
    test_list_of();
    test_set_of();

    // NULL handling
    test_null_handling();

    printf("All collection conversion tests passed!\n");
    return 0;
}
