//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_pluralize.h
// Purpose: English pluralization and singularization handling common rules, irregular forms, and
// uncountable nouns for the ~95% common case.
//
// Key invariants:
//   - Handles common English pluralization rules (e.g., -s, -es, -ies, -ves).
//   - Irregular forms (e.g., 'child'/'children', 'person'/'people') use a lookup table.
//   - Uncountable nouns (e.g., 'information', 'water') are returned unchanged.
//   - Not a full NLP engine; uncommon edge cases may be incorrect.
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated; caller must release.
//   - Input strings are borrowed; callers retain ownership.
//
// Links: src/runtime/text/rt_pluralize.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Pluralize an English noun.
    /// @param word Singular noun.
    /// @return Plural form (e.g., "cat" -> "cats", "child" -> "children").
    rt_string rt_pluralize(rt_string word);

    /// @brief Singularize an English noun.
    /// @param word Plural noun.
    /// @return Singular form (e.g., "cats" -> "cat", "children" -> "child").
    rt_string rt_singularize(rt_string word);

    /// @brief Format a count with the correct singular/plural noun.
    /// @param count Number of items.
    /// @param word Singular noun.
    /// @return Formatted string (e.g., "1 item", "5 items", "0 items").
    rt_string rt_pluralize_count(int64_t count, rt_string word);

#ifdef __cplusplus
}
#endif
