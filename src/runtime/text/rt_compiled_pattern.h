//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_compiled_pattern.h
// Purpose: Pre-compiled regular expression pattern for efficient repeated matching without per-call
// recompilation overhead, supporting find, capture, replace, and split.
//
// Key invariants:
//   - Pattern compilation traps on invalid syntax.
//   - Compiled patterns are opaque objects managed by the runtime object system.
//   - Match results use Seq containers for multi-capture returns.
//   - rt_compiled_pattern_replace replaces the first or all occurrences based on the replace_all
//   flag.
//
// Ownership/Lifetime:
//   - CompiledPattern objects are runtime-managed; caller does not need to free them.
//   - Returned strings and Seqs are newly allocated and owned by the caller.
//
// Links: src/runtime/text/rt_compiled_pattern.c (implementation),
// src/runtime/text/rt_regex_internal.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_COMPILED_PATTERN_H
#define VIPER_RT_COMPILED_PATTERN_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=============================================================================
    // Creation and Lifecycle
    //=============================================================================

    /// @brief Compile a regex pattern for repeated use.
    /// @param pattern The regex pattern string.
    /// @return Opaque CompiledPattern object pointer.
    /// @note Traps on invalid pattern syntax.
    void *rt_compiled_pattern_new(rt_string pattern);

    /// @brief Get the original pattern string.
    /// @param obj CompiledPattern pointer.
    /// @return The pattern string used to compile this pattern.
    rt_string rt_compiled_pattern_get_pattern(void *obj);

    //=============================================================================
    // Matching Operations
    //=============================================================================

    /// @brief Test if this pattern matches anywhere in text.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @return 1 if pattern matches anywhere in text, 0 otherwise.
    int8_t rt_compiled_pattern_is_match(void *obj, rt_string text);

    /// @brief Find first match of this pattern in text.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @return First matching substring, or empty string if no match.
    rt_string rt_compiled_pattern_find(void *obj, rt_string text);

    /// @brief Find first match starting at or after given position.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @param start Starting position (0-based).
    /// @return First matching substring at or after start, or empty string.
    rt_string rt_compiled_pattern_find_from(void *obj, rt_string text, int64_t start);

    /// @brief Find position of first match.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @return Start position of first match, or -1 if no match.
    int64_t rt_compiled_pattern_find_pos(void *obj, rt_string text);

    /// @brief Find all non-overlapping matches.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @return Seq of all matching substrings.
    void *rt_compiled_pattern_find_all(void *obj, rt_string text);

    //=============================================================================
    // Capture Groups
    //=============================================================================

    /// @brief Find first match and return capture groups.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @return Seq of captured groups (group 0 = full match), empty Seq if no match.
    void *rt_compiled_pattern_captures(void *obj, rt_string text);

    /// @brief Find first match at/after start and return capture groups.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @param start Starting position (0-based).
    /// @return Seq of captured groups (group 0 = full match), empty Seq if no match.
    void *rt_compiled_pattern_captures_from(void *obj, rt_string text, int64_t start);

    //=============================================================================
    // Replacement Operations
    //=============================================================================

    /// @brief Replace all matches with replacement string.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @param replacement Replacement string.
    /// @return New string with all matches replaced.
    rt_string rt_compiled_pattern_replace(void *obj, rt_string text, rt_string replacement);

    /// @brief Replace first match only.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to search.
    /// @param replacement Replacement string.
    /// @return New string with first match replaced.
    rt_string rt_compiled_pattern_replace_first(void *obj, rt_string text, rt_string replacement);

    //=============================================================================
    // Split Operation
    //=============================================================================

    /// @brief Split text by pattern matches.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to split.
    /// @return Seq of substrings between matches.
    void *rt_compiled_pattern_split(void *obj, rt_string text);

    /// @brief Split text by pattern matches with limit.
    /// @param obj CompiledPattern pointer.
    /// @param text Text to split.
    /// @param limit Maximum number of splits (0 = unlimited).
    /// @return Seq of substrings between matches.
    void *rt_compiled_pattern_split_n(void *obj, rt_string text, int64_t limit);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_COMPILED_PATTERN_H
