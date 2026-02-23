//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_diff.h
// Purpose: Line-based text difference computation using the Myers diff algorithm, producing a Seq of diff entries for added, removed, and unchanged lines.
//
// Key invariants:
//   - Uses the Myers O(D) diff algorithm for minimal edit distance.
//   - Diff entries include operation type (add/remove/equal) and line content.
//   - Input texts are split on newlines; empty strings produce empty diffs.
//   - The result Seq contains entries in document order.
//
// Ownership/Lifetime:
//   - Returned Seq and string objects are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_diff.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Compute line-by-line diff between two strings.
    /// @details Each entry in the result is prefixed: " " (same), "+" (added), "-" (removed).
    /// @param a Original text.
    /// @param b Modified text.
    /// @return Seq of diff line strings.
    void *rt_diff_lines(rt_string a, rt_string b);

    /// @brief Compute unified diff format.
    /// @param a Original text.
    /// @param b Modified text.
    /// @param context Number of context lines around changes.
    /// @return Unified diff string.
    rt_string rt_diff_unified(rt_string a, rt_string b, int64_t context);

    /// @brief Count the number of changed lines.
    /// @param a Original text.
    /// @param b Modified text.
    /// @return Number of added + removed lines.
    int64_t rt_diff_count_changes(rt_string a, rt_string b);

    /// @brief Apply a sequence of diff lines to the original to reconstruct the modified text.
    /// @param original Original text.
    /// @param diff Seq of diff lines (from rt_diff_lines).
    /// @return Reconstructed modified text.
    rt_string rt_diff_patch(rt_string original, void *diff);

#ifdef __cplusplus
}
#endif
