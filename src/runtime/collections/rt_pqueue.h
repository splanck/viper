//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_pqueue.h
// Purpose: Runtime-backed priority queue for Viper.Collections.Heap.
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
