//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_pluralize.h
// Purpose: English pluralization and singularization.
// Key invariants: Handles common English rules, irregular forms, and uncountable
//                 nouns. Not a full NLP engine - covers the ~95% common case.
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
