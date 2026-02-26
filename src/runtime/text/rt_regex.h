//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_regex.h
// Purpose: Regular expression pattern matching supporting literals, anchors, character classes,
// shorthand classes, quantifiers (greedy/non-greedy), grouping, and alternation.
//
// Key invariants:
//   - Patterns are compiled and cached internally for repeated use.
//   - Supported syntax: ., ^$, [...], \d\w\s, *, +, ?, *?+???, (), |.
//   - Unsupported: backreferences, lookahead/lookbehind, named groups.
//   - rt_regex_match returns 1 if the full string matches; rt_regex_find finds first match.
//
// Ownership/Lifetime:
//   - Returned strings and Seqs from capture operations are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of matching.
//
// Links: src/runtime/text/rt_regex.c (implementation), src/runtime/text/rt_regex_internal.h,
// src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Test if pattern matches anywhere in text.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @return 1 if pattern matches anywhere in text, 0 otherwise.
    /// @note Traps on invalid pattern syntax.
    int8_t rt_pattern_is_match(rt_string text, rt_string pattern);

    /// @brief Find first match of pattern in text.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @return First matching substring, or empty string if no match.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_find(rt_string text, rt_string pattern);

    /// @brief Find first match starting at or after given position.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @param start Starting position (0-based).
    /// @return First matching substring at or after start, or empty string.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_find_from(rt_string text, rt_string pattern, int64_t start);

    /// @brief Find position of first match.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @return Start position of first match, or -1 if no match.
    /// @note Traps on invalid pattern syntax.
    int64_t rt_pattern_find_pos(rt_string text, rt_string pattern);

    /// @brief Find all non-overlapping matches.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @return Seq of all matching substrings.
    /// @note Traps on invalid pattern syntax.
    void *rt_pattern_find_all(rt_string text, rt_string pattern);

    /// @brief Replace all matches with replacement string.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @param replacement Replacement string.
    /// @return New string with all matches replaced.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_replace(rt_string text, rt_string pattern, rt_string replacement);

    /// @brief Replace first match only.
    /// @param text Text to search.
    /// @param pattern Regex pattern string.
    /// @param replacement Replacement string.
    /// @return New string with first match replaced.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_replace_first(rt_string text, rt_string pattern, rt_string replacement);

    /// @brief Split text by pattern matches.
    /// @param text Text to split.
    /// @param pattern Regex pattern string.
    /// @return Seq of substrings between matches.
    /// @note Traps on invalid pattern syntax.
    void *rt_pattern_split(rt_string text, rt_string pattern);

    /// @brief Escape special regex characters in text.
    /// @param text Text to escape.
    /// @return Text with special characters escaped for literal matching.
    rt_string rt_pattern_escape(rt_string text);

#ifdef __cplusplus
}
#endif
