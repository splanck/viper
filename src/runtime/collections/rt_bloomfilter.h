//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_bloomfilter.h
// Purpose: Probabilistic set membership structure using multiple hash functions, guaranteeing no false negatives while allowing configurable false positive rates.
//
// Key invariants:
//   - False positives are possible; false negatives are not.
//   - The filter cannot remove elements once added.
//   - Optimal bit array size and hash count are computed from expected_items and false_positive_rate.
//   - rt_bloomfilter_contains returns 1 for possible membership, 0 for definite absence.
//
// Ownership/Lifetime:
//   - Filter objects are GC-managed opaque pointers.
//   - Callers must not free filter objects directly.
//
// Links: src/runtime/collections/rt_bloomfilter.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Create a new Bloom filter.
    /// @param expected_items Expected number of items.
    /// @param false_positive_rate Desired false positive rate (0.0-1.0).
    /// @return Bloom filter object.
    void *rt_bloomfilter_new(int64_t expected_items, double false_positive_rate);

    /// @brief Add a string to the filter.
    /// @param filter Bloom filter object.
    /// @param item String to add.
    void rt_bloomfilter_add(void *filter, rt_string item);

    /// @brief Check if a string might be in the filter.
    /// @param filter Bloom filter object.
    /// @param item String to check.
    /// @return 1 if possibly present, 0 if definitely not present.
    int64_t rt_bloomfilter_might_contain(void *filter, rt_string item);

    /// @brief Get the number of items added.
    /// @param filter Bloom filter object.
    /// @return Number of items added.
    int64_t rt_bloomfilter_count(void *filter);

    /// @brief Get the estimated false positive rate based on current fill.
    /// @param filter Bloom filter object.
    /// @return Estimated false positive rate (0.0-1.0).
    double rt_bloomfilter_fpr(void *filter);

    /// @brief Clear the filter (remove all items).
    /// @param filter Bloom filter object.
    void rt_bloomfilter_clear(void *filter);

    /// @brief Merge two filters (union).
    /// @param filter First filter (modified in place).
    /// @param other Second filter (must have same parameters).
    /// @return 1 on success, 0 if filters are incompatible.
    int64_t rt_bloomfilter_merge(void *filter, void *other);

#ifdef __cplusplus
}
#endif
