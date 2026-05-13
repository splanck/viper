//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/collections/rt_queue.h
// Purpose: Runtime-backed FIFO queue for Viper.Collections.Queue, providing enqueue/dequeue/peek
// with automatic growth and O(1) operations.
//
// Key invariants:
//   - FIFO ordering: enqueue at back, dequeue from front.
//   - Dequeue and peek on empty queue trap immediately.
//   - Internal ring buffer doubles capacity on overflow.
//   - By default elements are borrowed; runtime conversion helpers can enable
//     retained-element ownership before inserting values.
//
// Ownership/Lifetime:
//   - Queue objects are heap-allocated opaque pointers.
//   - Caller is responsible for queue lifetime management.
//   - Owned-element queues retain on push and release on pop/clear/finalize.
//
// Error conventions:
//   - Allocation failure → rt_trap()
//   - Pop/Peek on empty → rt_trap()
//   - TryPop on empty → returns NULL
//
// Links: src/runtime/collections/rt_queue.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a new empty queue with default capacity.
/// @return Opaque pointer to the new Queue object.
void *rt_queue_new(void);

/// @brief Enable or disable retained-element ownership for an empty queue.
void rt_queue_set_owns_elements(void *obj, int8_t owns);

/// @brief Return whether the queue retains its elements.
int8_t rt_queue_owns_elements(void *obj);

/// @brief Get the number of elements in the queue.
/// @param obj Opaque Queue object pointer.
/// @return Number of elements currently in the queue.
int64_t rt_queue_len(void *obj);

/// @brief Check if the queue is empty.
/// @param obj Opaque Queue object pointer.
/// @return 1 if empty, 0 otherwise.
int8_t rt_queue_is_empty(void *obj);

/// @brief Push an element to the back of the queue.
/// @param obj Opaque Queue object pointer.
/// @param elem Element to push.
void rt_queue_push(void *obj, void *elem);

/// @brief Pop and return the front element from the queue.
/// @param obj Opaque Queue object pointer.
/// @return The removed element; traps if empty.
void *rt_queue_pop(void *obj);

/// @brief Return the front element without removing it.
/// @param obj Opaque Queue object pointer.
/// @return The front element; traps if empty.
void *rt_queue_peek(void *obj);

/// @brief Remove all elements from the queue.
/// @param obj Opaque Queue object pointer.
void rt_queue_clear(void *obj);

/// @brief Check if the queue contains a given element (pointer equality).
/// @param obj Opaque Queue object pointer.
/// @param elem Element to search for.
/// @return 1 if found, 0 otherwise.
int8_t rt_queue_has(void *obj, void *elem);

/// @brief Convert the queue to a List (front-to-back order).
/// @param obj Opaque Queue object pointer.
/// @return New List containing all elements.
void *rt_queue_to_list(void *obj);

/// @brief Pop the front element, or return NULL if empty (no trap).
/// @param obj Opaque Queue object pointer.
/// @return The removed element, or NULL if empty.
void *rt_queue_try_pop(void *obj);

/// @brief Create a shallow copy of the queue.
/// @param obj Source Queue pointer.
/// @return New queue with same elements in same order.
void *rt_queue_clone(void *obj);

#ifdef __cplusplus
}
#endif
