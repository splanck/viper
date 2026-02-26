//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_ring.h
// Purpose: Fixed-capacity circular buffer (Ring) with FIFO ordering that overwrites the oldest
// entry when full, useful for rolling logs and event histories.
//
// Key invariants:
//   - Fixed capacity set at creation; cannot be resized.
//   - When full, push overwrites the oldest element (no trap).
//   - Peek returns the oldest (front) element without removing it.
//   - Elements are retained when pushed and released when overwritten or popped.
//
// Ownership/Lifetime:
//   - Ring objects are heap-allocated; caller is responsible for lifetime management.
//   - No reference counting; explicit destruction is required.
//
// Links: src/runtime/collections/rt_ring.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new ring buffer with specified capacity.
    /// @param capacity Maximum number of elements (must be > 0).
    /// @return Pointer to ring object.
    void *rt_ring_new(int64_t capacity);

    /// @brief Create a new ring buffer with default capacity (16).
    /// @return Pointer to ring object.
    void *rt_ring_new_default(void);

    /// @brief Get number of elements in ring.
    /// @param obj Ring pointer.
    /// @return Element count.
    int64_t rt_ring_len(void *obj);

    /// @brief Get capacity of ring.
    /// @param obj Ring pointer.
    /// @return Maximum capacity.
    int64_t rt_ring_cap(void *obj);

    /// @brief Check if ring is empty.
    /// @param obj Ring pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_ring_is_empty(void *obj);

    /// @brief Check if ring is full.
    /// @param obj Ring pointer.
    /// @return 1 if full, 0 otherwise.
    int8_t rt_ring_is_full(void *obj);

    /// @brief Push element to ring (overwrites oldest if full).
    /// @param obj Ring pointer.
    /// @param item Element to push (will be retained).
    void rt_ring_push(void *obj, void *item);

    /// @brief Pop oldest element from ring.
    /// @param obj Ring pointer.
    /// @return Oldest element, or NULL if empty.
    void *rt_ring_pop(void *obj);

    /// @brief Peek at oldest element without removing.
    /// @param obj Ring pointer.
    /// @return Oldest element, or NULL if empty.
    void *rt_ring_peek(void *obj);

    /// @brief Get element by logical index (0 = oldest).
    /// @param obj Ring pointer.
    /// @param index Logical index (0 to len-1).
    /// @return Element at index, or NULL if out of bounds.
    void *rt_ring_get(void *obj, int64_t index);

    /// @brief Remove all elements from ring.
    /// @param obj Ring pointer.
    void rt_ring_clear(void *obj);

#ifdef __cplusplus
}
#endif
