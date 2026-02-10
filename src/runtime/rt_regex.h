//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_regex.h
// Purpose: Regular expression pattern matching.
// Key invariants: Patterns are compiled and cached; results are newly allocated.
// Ownership/Lifetime: Returned strings/objects are newly allocated.
// Links: docs/viperlib/text.md
//
// Supported syntax:
// - Literals, dot (.), anchors (^$), character classes ([abc], [a-z], [^abc])
// - Shorthand classes: \d \D \w \W \s \S
// - Quantifiers: * + ? with non-greedy variants *? +? ??
// - Grouping: () and alternation: |
//
// NOT supported: backreferences, lookahead/lookbehind, named groups,
//                unicode categories, possessive quantifiers
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
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @return 1 if pattern matches anywhere in text, 0 otherwise.
    /// @note Traps on invalid pattern syntax.
    int8_t rt_pattern_is_match(rt_string pattern, rt_string text);

    /// @brief Find first match of pattern in text.
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @return First matching substring, or empty string if no match.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_find(rt_string pattern, rt_string text);

    /// @brief Find first match starting at or after given position.
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @param start Starting position (0-based).
    /// @return First matching substring at or after start, or empty string.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_find_from(rt_string pattern, rt_string text, int64_t start);

    /// @brief Find position of first match.
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @return Start position of first match, or -1 if no match.
    /// @note Traps on invalid pattern syntax.
    int64_t rt_pattern_find_pos(rt_string pattern, rt_string text);

    /// @brief Find all non-overlapping matches.
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @return Seq of all matching substrings.
    /// @note Traps on invalid pattern syntax.
    void *rt_pattern_find_all(rt_string pattern, rt_string text);

    /// @brief Replace all matches with replacement string.
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @param replacement Replacement string.
    /// @return New string with all matches replaced.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_replace(rt_string pattern, rt_string text, rt_string replacement);

    /// @brief Replace first match only.
    /// @param pattern Regex pattern string.
    /// @param text Text to search.
    /// @param replacement Replacement string.
    /// @return New string with first match replaced.
    /// @note Traps on invalid pattern syntax.
    rt_string rt_pattern_replace_first(rt_string pattern, rt_string text, rt_string replacement);

    /// @brief Split text by pattern matches.
    /// @param pattern Regex pattern string.
    /// @param text Text to split.
    /// @return Seq of substrings between matches.
    /// @note Traps on invalid pattern syntax.
    void *rt_pattern_split(rt_string pattern, rt_string text);

    /// @brief Escape special regex characters in text.
    /// @param text Text to escape.
    /// @return Text with special characters escaped for literal matching.
    rt_string rt_pattern_escape(rt_string text);

#ifdef __cplusplus
}
#endif
