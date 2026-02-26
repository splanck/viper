//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_bitset.h
// Purpose: Arbitrary-size bit array (BitSet) with 0-based indexing that auto-grows when bits are
// set beyond the current size, supporting population count, set/clear/toggle, and bitwise
// operations.
//
// Key invariants:
//   - Bit indices are 0-based; the bitset auto-grows when setting beyond current size.
//   - All bits start as 0 on allocation.
//   - Bitwise AND/OR/XOR operations require both operands to have the same size.
//   - rt_bitset_count returns the population count (number of set bits).
//
// Ownership/Lifetime:
//   - BitSet objects are heap-allocated; caller is responsible for lifetime management.
//   - Internal backing array may be reallocated on auto-grow.
//
// Links: src/runtime/collections/rt_bitset.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new BitSet with room for at least nbits bits.
    /// @param nbits Initial capacity in bits (all bits start as 0).
    /// @return Pointer to BitSet object.
    void *rt_bitset_new(int64_t nbits);

    /// @brief Get the number of bits the BitSet can currently hold.
    /// @param obj BitSet pointer.
    /// @return Bit capacity.
    int64_t rt_bitset_len(void *obj);

    /// @brief Count the number of set (1) bits (population count).
    /// @param obj BitSet pointer.
    /// @return Number of bits that are 1.
    int64_t rt_bitset_count(void *obj);

    /// @brief Check if all bits are zero.
    /// @param obj BitSet pointer.
    /// @return 1 if all bits are 0, 0 otherwise.
    int8_t rt_bitset_is_empty(void *obj);

    /// @brief Get the value of a bit at the given index.
    /// @param obj BitSet pointer.
    /// @param idx 0-based bit index.
    /// @return 1 if bit is set, 0 if clear or index out of range.
    int8_t rt_bitset_get(void *obj, int64_t idx);

    /// @brief Set a bit to 1 at the given index. Auto-grows if needed.
    /// @param obj BitSet pointer.
    /// @param idx 0-based bit index.
    void rt_bitset_set(void *obj, int64_t idx);

    /// @brief Clear a bit to 0 at the given index.
    /// @param obj BitSet pointer.
    /// @param idx 0-based bit index.
    void rt_bitset_clear(void *obj, int64_t idx);

    /// @brief Toggle a bit at the given index. Auto-grows if needed.
    /// @param obj BitSet pointer.
    /// @param idx 0-based bit index.
    void rt_bitset_toggle(void *obj, int64_t idx);

    /// @brief Set all bits to 1.
    /// @param obj BitSet pointer.
    void rt_bitset_set_all(void *obj);

    /// @brief Clear all bits to 0.
    /// @param obj BitSet pointer.
    void rt_bitset_clear_all(void *obj);

    /// @brief Bitwise AND of two BitSets. Returns a new BitSet.
    /// @param a First BitSet pointer.
    /// @param b Second BitSet pointer.
    /// @return New BitSet with size = max(a.len, b.len).
    void *rt_bitset_and(void *a, void *b);

    /// @brief Bitwise OR of two BitSets. Returns a new BitSet.
    /// @param a First BitSet pointer.
    /// @param b Second BitSet pointer.
    /// @return New BitSet with size = max(a.len, b.len).
    void *rt_bitset_or(void *a, void *b);

    /// @brief Bitwise XOR of two BitSets. Returns a new BitSet.
    /// @param a First BitSet pointer.
    /// @param b Second BitSet pointer.
    /// @return New BitSet with size = max(a.len, b.len).
    void *rt_bitset_xor(void *a, void *b);

    /// @brief Bitwise NOT of a BitSet. Returns a new BitSet.
    /// @param obj BitSet pointer.
    /// @return New BitSet with all bits flipped.
    void *rt_bitset_not(void *obj);

    /// @brief Convert the BitSet to a binary string representation.
    /// @param obj BitSet pointer.
    /// @return String like "10110100" (MSB first).
    rt_string rt_bitset_to_string(void *obj);

#ifdef __cplusplus
}
#endif
