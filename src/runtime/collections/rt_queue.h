//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_queue.h
// Purpose: Runtime-backed FIFO queue for Viper.Collections.Queue.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new empty queue with default capacity.
    /// @return Opaque pointer to the new Queue object.
    void *rt_queue_new(void);

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
    /// @param val Element to push.
    void rt_queue_push(void *obj, void *val);

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

#ifdef __cplusplus
}
#endif
