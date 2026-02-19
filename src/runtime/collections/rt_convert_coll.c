//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_convert_coll.c
/// @brief Implementation of collection conversion utilities.
///
//===----------------------------------------------------------------------===//

#include "rt_convert_coll.h"
#include "rt_bag.h"
#include "rt_deque.h"
#include "rt_list.h"
#include "rt_map.h"
#include "rt_queue.h"
#include "rt_ring.h"
#include "rt_seq.h"
#include "rt_set.h"
#include "rt_stack.h"
#include <stdarg.h>
#include <stdlib.h>

//=============================================================================
// Seq Conversions
//=============================================================================

void *rt_seq_to_list(void *seq)
{
    void *list = rt_ns_list_new();
    if (!seq || !list)
        return list;

    int64_t len = rt_seq_len(seq);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(seq, i);
        rt_list_push(list, elem);
    }
    return list;
}

void *rt_seq_to_set(void *seq)
{
    void *set = rt_set_new();
    if (!seq || !set)
        return set;

    int64_t len = rt_seq_len(seq);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(seq, i);
        rt_set_put(set, elem);
    }
    return set;
}

void *rt_seq_to_stack(void *seq)
{
    void *stack = rt_stack_new();
    if (!seq || !stack)
        return stack;

    int64_t len = rt_seq_len(seq);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(seq, i);
        rt_stack_push(stack, elem);
    }
    return stack;
}

void *rt_seq_to_queue(void *seq)
{
    void *queue = rt_queue_new();
    if (!seq || !queue)
        return queue;

    int64_t len = rt_seq_len(seq);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(seq, i);
        rt_queue_push(queue, elem);
    }
    return queue;
}

void *rt_seq_to_deque(void *seq)
{
    void *deque = rt_deque_new();
    if (!seq || !deque)
        return deque;

    int64_t len = rt_seq_len(seq);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(seq, i);
        rt_deque_push_back(deque, elem);
    }
    return deque;
}

void *rt_seq_to_bag(void *seq)
{
    void *bag = rt_bag_new();
    if (!seq || !bag)
        return bag;

    int64_t len = rt_seq_len(seq);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(seq, i);
        rt_bag_put(bag, (rt_string)elem);
    }
    return bag;
}

//=============================================================================
// List Conversions
//=============================================================================

void *rt_list_to_seq(void *list)
{
    void *seq = rt_seq_new();
    if (!list || !seq)
        return seq;

    int64_t len = rt_list_len(list);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_list_get(list, i);
        rt_seq_push(seq, elem);
    }
    return seq;
}

void *rt_list_to_set(void *list)
{
    void *set = rt_set_new();
    if (!list || !set)
        return set;

    int64_t len = rt_list_len(list);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_list_get(list, i);
        rt_set_put(set, elem);
    }
    return set;
}

void *rt_list_to_stack(void *list)
{
    void *stack = rt_stack_new();
    if (!list || !stack)
        return stack;

    int64_t len = rt_list_len(list);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_list_get(list, i);
        rt_stack_push(stack, elem);
    }
    return stack;
}

void *rt_list_to_queue(void *list)
{
    void *queue = rt_queue_new();
    if (!list || !queue)
        return queue;

    int64_t len = rt_list_len(list);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_list_get(list, i);
        rt_queue_push(queue, elem);
    }
    return queue;
}

//=============================================================================
// Set Conversions
//=============================================================================

void *rt_set_to_seq(void *set)
{
    if (!set)
        return rt_seq_new();
    // rt_set_items already returns a Seq
    return rt_set_items(set);
}

void *rt_set_to_list(void *set)
{
    void *seq = rt_set_to_seq(set);
    void *list = rt_seq_to_list(seq);
    return list;
}

//=============================================================================
// Stack Conversions
//=============================================================================

void *rt_stack_to_seq(void *stack)
{
    void *seq = rt_seq_new();
    if (!stack || !seq)
        return seq;

    int64_t len = rt_stack_len(stack);
    // We need to iterate from bottom to top
    // Stack doesn't expose indexed access, so we need to use peek_at if available
    // For now, use rt_stack_to_array if available, otherwise reverse approach

    // Create temporary array by popping all
    void **temp = malloc(sizeof(void *) * len);
    if (!temp)
        return seq;

    int64_t count = 0;
    while (!rt_stack_is_empty(stack))
    {
        temp[count++] = rt_stack_pop(stack);
    }

    // Add to seq in reverse order (bottom to top)
    for (int64_t i = count - 1; i >= 0; i--)
    {
        rt_seq_push(seq, temp[i]);
        // Also restore the stack
        rt_stack_push(stack, temp[i]);
    }

    free(temp);
    return seq;
}

void *rt_stack_to_list(void *stack)
{
    void *seq = rt_stack_to_seq(stack);
    void *list = rt_seq_to_list(seq);
    return list;
}

//=============================================================================
// Queue Conversions
//=============================================================================

void *rt_queue_to_seq(void *queue)
{
    void *seq = rt_seq_new();
    if (!queue || !seq)
        return seq;

    int64_t len = rt_queue_len(queue);

    // Create temporary array by dequeuing all
    void **temp = malloc(sizeof(void *) * len);
    if (!temp)
        return seq;

    int64_t count = 0;
    while (!rt_queue_is_empty(queue))
    {
        temp[count++] = rt_queue_pop(queue);
    }

    // Add to seq in order (front to back) and restore queue
    for (int64_t i = 0; i < count; i++)
    {
        rt_seq_push(seq, temp[i]);
        rt_queue_push(queue, temp[i]);
    }

    free(temp);
    return seq;
}

void *rt_queue_to_list(void *queue)
{
    void *seq = rt_queue_to_seq(queue);
    void *list = rt_seq_to_list(seq);
    return list;
}

//=============================================================================
// Deque Conversions
//=============================================================================

void *rt_deque_to_seq(void *deque)
{
    void *seq = rt_seq_new();
    if (!deque || !seq)
        return seq;

    int64_t len = rt_deque_len(deque);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_deque_get(deque, i);
        rt_seq_push(seq, elem);
    }
    return seq;
}

void *rt_deque_to_list(void *deque)
{
    void *seq = rt_deque_to_seq(deque);
    void *list = rt_seq_to_list(seq);
    return list;
}

//=============================================================================
// Map Conversions
//=============================================================================

void *rt_map_keys_to_seq(void *map)
{
    if (!map)
        return rt_seq_new();
    // rt_map_keys already returns a Seq
    return rt_map_keys(map);
}

void *rt_map_values_to_seq(void *map)
{
    if (!map)
        return rt_seq_new();
    // rt_map_values already returns a Seq
    return rt_map_values(map);
}

//=============================================================================
// Bag Conversions
//=============================================================================

void *rt_bag_to_seq(void *bag)
{
    if (!bag)
        return rt_seq_new();
    // Bag stores unique strings, rt_bag_items already returns a Seq
    return rt_bag_items(bag);
}

void *rt_bag_to_set(void *bag)
{
    void *set = rt_set_new();
    if (!bag || !set)
        return set;

    // Get unique elements
    void *items = rt_bag_items(bag);
    if (!items)
        return set;

    int64_t len = rt_seq_len(items);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_seq_get(items, i);
        rt_set_put(set, elem);
    }
    return set;
}

//=============================================================================
// Ring Conversions
//=============================================================================

void *rt_ring_to_seq(void *ring)
{
    void *seq = rt_seq_new();
    if (!ring || !seq)
        return seq;

    int64_t len = rt_ring_len(ring);
    for (int64_t i = 0; i < len; i++)
    {
        void *elem = rt_ring_get(ring, i);
        rt_seq_push(seq, elem);
    }
    return seq;
}

//=============================================================================
// Utility Functions
//=============================================================================

void *rt_seq_of(int64_t count, ...)
{
    void *seq = rt_seq_new();
    if (!seq || count <= 0)
        return seq;

    va_list args;
    va_start(args, count);
    for (int64_t i = 0; i < count; i++)
    {
        void *elem = va_arg(args, void *);
        rt_seq_push(seq, elem);
    }
    va_end(args);

    return seq;
}

void *rt_list_of(int64_t count, ...)
{
    void *list = rt_ns_list_new();
    if (!list || count <= 0)
        return list;

    va_list args;
    va_start(args, count);
    for (int64_t i = 0; i < count; i++)
    {
        void *elem = va_arg(args, void *);
        rt_list_push(list, elem);
    }
    va_end(args);

    return list;
}

void *rt_set_of(int64_t count, ...)
{
    void *set = rt_set_new();
    if (!set || count <= 0)
        return set;

    va_list args;
    va_start(args, count);
    for (int64_t i = 0; i < count; i++)
    {
        void *elem = va_arg(args, void *);
        rt_set_put(set, elem);
    }
    va_end(args);

    return set;
}
