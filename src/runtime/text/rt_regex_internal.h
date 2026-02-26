//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_regex_internal.h
// Purpose: Internal regex engine types and NFA/DFA state structures shared between rt_regex.c and
// rt_compiled_pattern.c, not part of the public API.
//
// Key invariants:
//   - This header is internal; it must not be included by code outside the text/ directory.
//   - Defines the compiled NFA state representation and matching engine entry points.
//   - rt_regex_compile_internal is the shared compilation entry point.
//   - rt_regex_exec_internal runs the NFA on a subject string.
//
// Ownership/Lifetime:
//   - Compiled regex objects are owned by their enclosing rt_regex or rt_compiled_pattern.
//   - No direct public ownership semantics; accessed only through the wrapper APIs.
//
// Links: src/runtime/text/rt_regex.c, src/runtime/text/rt_compiled_pattern.c (internal users)
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_REGEX_INTERNAL_H
#define VIPER_RT_REGEX_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Forward declaration of internal compiled pattern
    typedef struct re_compiled_pattern re_compiled_pattern;

    /// @brief Compile a pattern string into an internal representation.
    /// @param pattern The regex pattern string.
    /// @return Compiled pattern, or traps on error.
    re_compiled_pattern *re_compile(const char *pattern);

    /// @brief Free a compiled pattern.
    /// @param cp The compiled pattern to free.
    void re_free(re_compiled_pattern *cp);

    /// @brief Get the pattern string from a compiled pattern.
    /// @param cp The compiled pattern.
    /// @return The original pattern string.
    const char *re_get_pattern(re_compiled_pattern *cp);

    /// @brief Find a match in text, returning start and end positions.
    /// @param cp Compiled pattern.
    /// @param text Text to search.
    /// @param text_len Length of text.
    /// @param start_from Position to start searching from.
    /// @param match_start Output: start position of match.
    /// @param match_end Output: end position of match.
    /// @return true if match found.
    bool re_find_match(re_compiled_pattern *cp,
                       const char *text,
                       int text_len,
                       int start_from,
                       int *match_start,
                       int *match_end);

    /// @brief Find a match and capture groups.
    /// @param cp Compiled pattern.
    /// @param text Text to search.
    /// @param text_len Length of text.
    /// @param start_from Position to start searching from.
    /// @param match_start Output: start position of full match.
    /// @param match_end Output: end position of full match.
    /// @param group_starts Output array for group start positions (must be pre-allocated).
    /// @param group_ends Output array for group end positions (must be pre-allocated).
    /// @param max_groups Maximum number of groups to capture.
    /// @param num_groups Output: actual number of groups captured.
    /// @return true if match found.
    bool re_find_match_with_groups(re_compiled_pattern *cp,
                                   const char *text,
                                   int text_len,
                                   int start_from,
                                   int *match_start,
                                   int *match_end,
                                   int *group_starts,
                                   int *group_ends,
                                   int max_groups,
                                   int *num_groups);

    /// @brief Get number of capture groups in pattern.
    /// @param cp Compiled pattern.
    /// @return Number of capture groups (not including group 0).
    int re_group_count(re_compiled_pattern *cp);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_REGEX_INTERNAL_H
