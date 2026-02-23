//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_deque.h
// Purpose: Double-ended queue (deque) providing O(1) push/pop at both front and back with O(1) random access by index, implemented as a ring buffer.
//
// Key invariants:
//   - Indices are 0-based with front at index 0.
//   - Pop and peek operations on an empty deque trap immediately.
//   - Capacity is always >= 1; the ring buffer doubles on overflow.
//   - Element equality for rt_deque_has uses pointer comparison.
//
// Ownership/Lifetime:
//   - Deque objects are GC-managed; elements are retained on insertion and released on removal.
//   - Callers should not free deque objects directly.
//
// Links: src/runtime/collections/rt_deque.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Deque Creation
    //=========================================================================

    /// @brief Create a new empty deque with default capacity.
    /// @return Opaque Deque object pointer.
    void *rt_deque_new(void);

    /// @brief Create a new empty deque with specified initial capacity.
    /// @param cap Initial capacity (minimum 1).
    /// @return Opaque Deque object pointer.
    void *rt_deque_with_capacity(int64_t cap);

    //=========================================================================
    // Size Operations
    //=========================================================================

    /// @brief Get the number of elements in the deque.
    /// @param obj Opaque Deque object pointer.
    /// @return Number of elements currently in the deque.
    int64_t rt_deque_len(void *obj);

    /// @brief Get the current capacity of the deque.
    /// @param obj Opaque Deque object pointer.
    /// @return Current capacity.
    int64_t rt_deque_cap(void *obj);

    /// @brief Check if the deque is empty.
    /// @param obj Opaque Deque object pointer.
    /// @return 1 if empty, 0 otherwise.
    int8_t rt_deque_is_empty(void *obj);

    //=========================================================================
    // Front Operations
    //=========================================================================

    /// @brief Add an element to the front of the deque.
    /// @param obj Opaque Deque object pointer.
    /// @param val Element to add.
    void rt_deque_push_front(void *obj, void *val);

    /// @brief Remove and return the element at the front.
    /// @param obj Opaque Deque object pointer.
    /// @return The removed element; traps if empty.
    void *rt_deque_pop_front(void *obj);

    /// @brief Get the element at the front without removing it.
    /// @param obj Opaque Deque object pointer.
    /// @return The front element; traps if empty.
    void *rt_deque_peek_front(void *obj);

    //=========================================================================
    // Back Operations
    //=========================================================================

    /// @brief Add an element to the back of the deque.
    /// @param obj Opaque Deque object pointer.
    /// @param val Element to add.
    void rt_deque_push_back(void *obj, void *val);

    /// @brief Remove and return the element at the back.
    /// @param obj Opaque Deque object pointer.
    /// @return The removed element; traps if empty.
    void *rt_deque_pop_back(void *obj);

    /// @brief Get the element at the back without removing it.
    /// @param obj Opaque Deque object pointer.
    /// @return The back element; traps if empty.
    void *rt_deque_peek_back(void *obj);

    //=========================================================================
    // Random Access
    //=========================================================================

    /// @brief Get the element at the specified index.
    /// @param obj Opaque Deque object pointer.
    /// @param idx Index of element to retrieve (0 is front).
    /// @return Element at the index; traps if out of bounds.
    void *rt_deque_get(void *obj, int64_t idx);

    /// @brief Set the element at the specified index.
    /// @param obj Opaque Deque object pointer.
    /// @param idx Index of element to set.
    /// @param val Value to store.
    void rt_deque_set(void *obj, int64_t idx, void *val);

    //=========================================================================
    // Utility
    //=========================================================================

    /// @brief Remove all elements from the deque.
    /// @param obj Opaque Deque object pointer.
    void rt_deque_clear(void *obj);

    /// @brief Check if the deque contains an element.
    /// @param obj Opaque Deque object pointer.
    /// @param val Element to check for (compared by pointer equality).
    /// @return 1 if found, 0 otherwise.
    int8_t rt_deque_has(void *obj, void *val);

    /// @brief Reverse the elements in the deque in place.
    /// @param obj Opaque Deque object pointer.
    void rt_deque_reverse(void *obj);

    /// @brief Create a shallow copy of the deque.
    /// @param obj Source Deque object pointer.
    /// @return New deque with same elements.
    void *rt_deque_clone(void *obj);

#ifdef __cplusplus
}
#endif
