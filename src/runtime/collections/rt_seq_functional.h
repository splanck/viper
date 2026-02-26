//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_seq_functional.h
// Purpose: IL-compatible wrapper functions for Seq functional operations (filter, reject, map,
// reduce, each, find) accepting function pointers as void* for IL calling convention.
//
// Key invariants:
//   - Function pointers are passed as void* and cast internally to the correct type.
//   - Predicate callbacks must be of type int8_t (*)(void *) for filter/reject/find.
//   - Map callbacks must be of type void *(*)(void *) for element transformation.
//   - These wrappers bridge the IL calling convention to the typed Seq API.
//
// Ownership/Lifetime:
//   - Returned Seq objects are newly allocated; caller is responsible for lifetime management.
//   - Source sequences are not modified or consumed.
//
// Links: src/runtime/collections/rt_seq_functional.c (implementation),
// src/runtime/collections/rt_seq.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Keep elements matching predicate (wrapper for IL).
    /// @param seq Seq object.
    /// @param pred Predicate function (int8_t (*)(void *) cast to void*).
    /// @return New Seq with matching elements.
    void *rt_seq_keep_wrapper(void *seq, void *pred);

    /// @brief Reject elements matching predicate (wrapper for IL).
    void *rt_seq_reject_wrapper(void *seq, void *pred);

    /// @brief Apply transform to each element (wrapper for IL).
    void *rt_seq_apply_wrapper(void *seq, void *fn);

    /// @brief Check if all elements match predicate (wrapper for IL).
    int8_t rt_seq_all_wrapper(void *seq, void *pred);

    /// @brief Check if any element matches predicate (wrapper for IL).
    int8_t rt_seq_any_wrapper(void *seq, void *pred);

    /// @brief Check if no elements match predicate (wrapper for IL).
    int8_t rt_seq_none_wrapper(void *seq, void *pred);

    /// @brief Count elements matching predicate (wrapper for IL).
    int64_t rt_seq_count_where_wrapper(void *seq, void *pred);

    /// @brief Find first element matching predicate (wrapper for IL).
    void *rt_seq_find_where_wrapper(void *seq, void *pred);

    /// @brief Take elements while predicate is true (wrapper for IL).
    void *rt_seq_take_while_wrapper(void *seq, void *pred);

    /// @brief Drop elements while predicate is true (wrapper for IL).
    void *rt_seq_drop_while_wrapper(void *seq, void *pred);

    /// @brief Fold/reduce sequence with accumulator (wrapper for IL).
    void *rt_seq_fold_wrapper(void *seq, void *init, void *fn);

#ifdef __cplusplus
}
#endif
