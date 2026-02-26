//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_pqueue.h
// Purpose: Runtime-backed priority queue (heap) for Viper.Collections.Heap, supporting min-heap and
// max-heap ordering with push/pop/peek operations.
//
// Key invariants:
//   - Default is a min-heap; rt_pqueue_new_max creates a max-heap.
//   - rt_pqueue_peek returns the top element without removing it.
//   - Pop on empty queue traps immediately.
//   - Heap property is maintained after every push and pop operation.
//
// Ownership/Lifetime:
//   - Heap objects are heap-allocated opaque pointers.
//   - Elements stored in the heap are not retained; callers manage element lifetimes.
//
// Links: src/runtime/collections/rt_pqueue.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty min-heap.
    /// @return Opaque pointer to the new Heap object.
    void *rt_pqueue_new(void);

    /// @brief Create a new empty heap with specified ordering.
    /// @param is_max If true, creates a max-heap; otherwise min-heap.
    /// @return Opaque pointer to the new Heap object.
    void *rt_pqueue_new_max(int8_t is_max);

    /// @brief Get the number of elements in the heap.
    /// @param obj Opaque Heap object pointer.
    /// @return Number of elements currently in the heap.
    int64_t rt_pqueue_len(void *obj);

    /// @brief Check if the heap is empty.
    /// @param obj Opaque Heap object pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_pqueue_is_empty(void *obj);

    /// @brief Check if the heap is a max-heap.
    /// @param obj Opaque Heap object pointer.
    /// @return 1 if max-heap, 0 if min-heap.
    int8_t rt_pqueue_is_max(void *obj);

    /// @brief Add an element with priority to the heap.
    /// @param obj Opaque Heap object pointer.
    /// @param priority Integer priority value.
    /// @param val Element to add.
    void rt_pqueue_push(void *obj, int64_t priority, void *val);

    /// @brief Remove and return the highest priority element.
    /// @param obj Opaque Heap object pointer.
    /// @return The removed element; traps if empty.
    void *rt_pqueue_pop(void *obj);

    /// @brief Return the highest priority element without removing it.
    /// @param obj Opaque Heap object pointer.
    /// @return The highest priority element; traps if empty.
    void *rt_pqueue_peek(void *obj);

    /// @brief Try to remove and return the highest priority element.
    /// @param obj Opaque Heap object pointer.
    /// @return The removed element, or NULL if empty.
    void *rt_pqueue_try_pop(void *obj);

    /// @brief Try to return the highest priority element without removing it.
    /// @param obj Opaque Heap object pointer.
    /// @return The highest priority element, or NULL if empty.
    void *rt_pqueue_try_peek(void *obj);

    /// @brief Remove all elements from the heap.
    /// @param obj Opaque Heap object pointer.
    void rt_pqueue_clear(void *obj);

    /// @brief Convert heap to Seq in priority order (destructive copy).
    /// @param obj Opaque Heap object pointer.
    /// @return New Seq containing elements in priority order.
    void *rt_pqueue_to_seq(void *obj);

#ifdef __cplusplus
}
#endif
