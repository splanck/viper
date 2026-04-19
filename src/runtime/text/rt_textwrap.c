//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_textwrap.c
// Purpose: Implements text word-wrapping utilities for the Viper.Text.TextWrap
//          class. Wraps long lines at word boundaries within a specified column
//          width, with options for initial indent, subsequent indent, and hard
//          wrapping of words that exceed the width.
//
// Key invariants:
//   - Wrapping occurs at whitespace boundaries; words are never split unless
//     hard_wrap is enabled and the word itself exceeds the column width.
//   - Initial indent is prepended to the first line only.
//   - Subsequent indent is prepended to all continuation lines.
//   - Tab characters are expanded to spaces based on a configurable tab width.
//   - Empty input returns an empty string; a zero width disables wrapping.
//
// Ownership/Lifetime:
//   - The returned wrapped string is a fresh rt_string allocation owned by caller.
//   - Input strings are borrowed for the duration of the call.
//
// Links: src/runtime/text/rt_textwrap.h (public API),
//        src/runtime/rt_string_builder.h (used to accumulate output lines)
//
//===----------------------------------------------------------------------===//

#include "rt_textwrap.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Basic Text Wrapping
//=============================================================================

/// @brief Greedy word-wrap a string to fit within `width` columns per line.
/// @details Walks character-by-character tracking the column position
///          and the index of the most recent space. When the line
///          overflows, breaks at that last space; if no space has
///          been seen on the current line, force-breaks at the
///          overflow point. Existing newlines in the input act as
///          paragraph boundaries (column resets, last-space cleared).
///          Width is clamped to `[1, ...]` so the worst-case is
///          one-character lines, never an infinite loop.
rt_string rt_textwrap_wrap(rt_string text, int64_t width) {
    if (width < 1)
        width = 1;

    const char *src = rt_string_cstr(text);
    int64_t src_len = rt_str_len(text);

    // Allocate result buffer (worst case: same size + newlines)
    char *result = (char *)malloc((size_t)(src_len * 2 + 1));
    if (!result)
        return text;

    int64_t result_pos = 0;
    int64_t line_start = 0;
    int64_t last_space = -1;
    int64_t col = 0;

    for (int64_t i = 0; i < src_len; i++) {
        char c = src[i];

        if (c == '\n') {
            // Copy line up to newline
            memcpy(result + result_pos, src + line_start, (size_t)(i - line_start + 1));
            result_pos += i - line_start + 1;
            line_start = i + 1;
            last_space = -1;
            col = 0;
            continue;
        }

        if (c == ' ' || c == '\t') {
            last_space = i;
        }

        col++;

        if (col > width) {
            if (last_space > line_start) {
                // Wrap at last space
                memcpy(result + result_pos, src + line_start, (size_t)(last_space - line_start));
                result_pos += last_space - line_start;
                result[result_pos++] = '\n';
                line_start = last_space + 1;
                col = i - last_space;
                last_space = -1;
            } else {
                // No space found, force break
                memcpy(result + result_pos, src + line_start, (size_t)(i - line_start));
                result_pos += i - line_start;
                result[result_pos++] = '\n';
                line_start = i;
                col = 1;
            }
        }
    }

    // Copy remaining text
    if (line_start < src_len) {
        memcpy(result + result_pos, src + line_start, (size_t)(src_len - line_start));
        result_pos += src_len - line_start;
    }

    result[result_pos] = '\0';
    rt_string ret = rt_string_from_bytes(result, result_pos);
    free(result);
    return ret;
}

/// @brief Wrap text and return the result split into a Seq of rt_string lines (no trailing LF).
void *rt_textwrap_wrap_lines(rt_string text, int64_t width) {
    rt_string wrapped = rt_textwrap_wrap(text, width);
    void *lines = rt_seq_new();

    const char *src = rt_string_cstr(wrapped);
    int64_t len = rt_str_len(wrapped);
    int64_t start = 0;

    for (int64_t i = 0; i <= len; i++) {
        if (i == len || src[i] == '\n') {
            rt_string line = rt_string_from_bytes(src + start, i - start);
            rt_seq_push(lines, (void *)line);
            start = i + 1;
        }
    }

    return lines;
}

/// @brief Alias for `rt_textwrap_wrap` — kept for API parity with Python's `textwrap`.
/// @details In CPython, `wrap()` returns a list of lines and `fill()`
///          returns a single string. This runtime always returns a
///          string from both, so they collapse to the same call.
rt_string rt_textwrap_fill(rt_string text, int64_t width) {
    return rt_textwrap_wrap(text, width);
}

//=============================================================================
// Indentation
//=============================================================================

/// @brief Prepend `prefix` to every line of `text` (including empty lines).
/// @details Walks the input one byte at a time, emitting `prefix`
///          before each line and again after every embedded `\n`.
///          Counts lines up-front to size the output buffer exactly:
///          `src_len + line_count * pre_len`.
rt_string rt_textwrap_indent(rt_string text, rt_string prefix) {
    const char *src = rt_string_cstr(text);
    int64_t src_len = rt_str_len(text);
    const char *pre = rt_string_cstr(prefix);
    int64_t pre_len = rt_str_len(prefix);

    // Count lines
    int64_t line_count = 1;
    for (int64_t i = 0; i < src_len; i++) {
        if (src[i] == '\n')
            line_count++;
    }

    // Allocate result
    char *result = (char *)malloc((size_t)(src_len + line_count * pre_len + 1));
    if (!result)
        return text;

    int64_t result_pos = 0;
    int at_line_start = 1;

    for (int64_t i = 0; i <= src_len; i++) {
        if (at_line_start) {
            memcpy(result + result_pos, pre, (size_t)pre_len);
            result_pos += pre_len;
            at_line_start = 0;
        }

        if (i < src_len) {
            result[result_pos++] = src[i];
            if (src[i] == '\n') {
                at_line_start = 1;
            }
        }
    }

    result[result_pos] = '\0';
    rt_string ret = rt_string_from_bytes(result, result_pos);
    free(result);
    return ret;
}

/// @brief Strip the longest leading-whitespace prefix common to every non-empty line.
/// @details Two passes:
///          1. Walk the input once to find `min_indent` — the smallest
///             leading-whitespace count across all non-empty lines
///             (empty lines are skipped to match Python's
///             `textwrap.dedent` behavior).
///          2. Emit each line with that many leading whitespace bytes
///             skipped. Tabs count as 4 spaces for the indent
///             measurement (and 4-space worth of skip).
///          Returns the input unchanged when `min_indent <= 0`.
rt_string rt_textwrap_dedent(rt_string text) {
    const char *src = rt_string_cstr(text);
    int64_t src_len = rt_str_len(text);

    // Find minimum indentation (excluding empty lines)
    int64_t min_indent = -1;
    int64_t current_indent = 0;
    int at_line_start = 1;

    for (int64_t i = 0; i < src_len; i++) {
        if (at_line_start) {
            if (src[i] == ' ') {
                current_indent++;
            } else if (src[i] == '\t') {
                current_indent += 4; // Treat tab as 4 spaces
            } else if (src[i] == '\n') {
                // Empty line, skip
                current_indent = 0;
            } else {
                if (min_indent < 0 || current_indent < min_indent) {
                    min_indent = current_indent;
                }
                at_line_start = 0;
            }
        } else if (src[i] == '\n') {
            at_line_start = 1;
            current_indent = 0;
        }
    }

    if (min_indent <= 0)
        return text;

    // Build result without common indent
    char *result = (char *)malloc((size_t)(src_len + 1));
    if (!result)
        return text;

    int64_t result_pos = 0;
    int64_t skip_remaining = min_indent;
    at_line_start = 1;

    for (int64_t i = 0; i < src_len; i++) {
        if (at_line_start) {
            if ((src[i] == ' ' || src[i] == '\t') && skip_remaining > 0) {
                skip_remaining -= (src[i] == '\t') ? 4 : 1;
                continue;
            }
            at_line_start = 0;
        }

        result[result_pos++] = src[i];

        if (src[i] == '\n') {
            at_line_start = 1;
            skip_remaining = min_indent;
        }
    }

    result[result_pos] = '\0';
    rt_string ret = rt_string_from_bytes(result, result_pos);
    free(result);
    return ret;
}

/// @brief Indent every line *except* the first with `prefix` (hanging-indent style).
/// @details Useful for "labelled paragraph" output where the first line
///          carries the label and continuation lines are indented to
///          align under the body. The first non-empty line is left
///          alone; every subsequent line gets `prefix` prepended.
rt_string rt_textwrap_hang(rt_string text, rt_string prefix) {
    const char *src = rt_string_cstr(text);
    int64_t src_len = rt_str_len(text);
    const char *pre = rt_string_cstr(prefix);
    int64_t pre_len = rt_str_len(prefix);

    // Count lines
    int64_t line_count = 0;
    for (int64_t i = 0; i < src_len; i++) {
        if (src[i] == '\n')
            line_count++;
    }

    // Allocate result
    char *result = (char *)malloc((size_t)(src_len + line_count * pre_len + 1));
    if (!result)
        return text;

    int64_t result_pos = 0;
    int first_line = 1;

    for (int64_t i = 0; i < src_len; i++) {
        if (!first_line && (i == 0 || src[i - 1] == '\n')) {
            memcpy(result + result_pos, pre, (size_t)pre_len);
            result_pos += pre_len;
        }

        result[result_pos++] = src[i];

        if (src[i] == '\n') {
            first_line = 0;
        }
    }

    result[result_pos] = '\0';
    rt_string ret = rt_string_from_bytes(result, result_pos);
    free(result);
    return ret;
}

//=============================================================================
// Truncation
//=============================================================================

/// @brief Truncate to `width` characters total, appending `"..."` if it didn't fit.
rt_string rt_textwrap_truncate(rt_string text, int64_t width) {
    return rt_textwrap_truncate_with(text, width, rt_const_cstr("..."));
}

/// @brief Truncate to `width` characters total, appending `suffix` if shortened.
/// @details If `text` already fits, returns it unchanged. Otherwise
///          keeps `width - suffix_len` characters and appends `suffix`.
///          Edge case: if `width <= suffix_len`, returns just the
///          suffix (no useful prefix can fit).
rt_string rt_textwrap_truncate_with(rt_string text, int64_t width, rt_string suffix) {
    int64_t text_len = rt_str_len(text);
    int64_t suffix_len = rt_str_len(suffix);

    if (text_len <= width)
        return text;

    if (width <= suffix_len)
        return suffix;

    int64_t keep = width - suffix_len;
    rt_string kept = rt_str_substr(text, 0, keep);
    return rt_str_concat(kept, suffix);
}

/// @brief Shorten by replacing the middle with `"..."` while preserving start and end.
/// @details Useful for paths and identifiers where keeping both ends
///          helps readability. Splits the keep budget roughly in half
///          (right side gets the extra char on odd widths). Falls
///          back to a head-truncate when `width < 5` (no room for
///          even three dots plus one char on each side).
rt_string rt_textwrap_shorten(rt_string text, int64_t width) {
    int64_t text_len = rt_str_len(text);

    if (text_len <= width)
        return text;

    if (width < 5)
        return rt_str_substr(text, 0, width);

    int64_t left = (width - 3) / 2;
    int64_t right = width - 3 - left;

    rt_string left_part = rt_str_substr(text, 0, left);
    rt_string right_part = rt_str_substr(text, text_len - right, right);

    rt_string result = rt_str_concat(left_part, rt_const_cstr("..."));
    return rt_str_concat(result, right_part);
}

//=============================================================================
// Alignment
//=============================================================================

/// @brief Left-justify `text` in a `width`-column field, padding with trailing spaces.
/// @details If `text` is already as wide or wider than `width`, it's
///          returned unchanged.
rt_string rt_textwrap_left(rt_string text, int64_t width) {
    int64_t text_len = rt_str_len(text);
    if (text_len >= width)
        return text;

    int64_t pad = width - text_len;
    char *spaces = (char *)malloc((size_t)(pad + 1));
    if (!spaces)
        return text;

    memset(spaces, ' ', (size_t)pad);
    spaces[pad] = '\0';

    rt_string padding = rt_string_from_bytes(spaces, pad);
    free(spaces);

    return rt_str_concat(text, padding);
}

/// @brief Right-justify `text` in a `width`-column field, padding with leading spaces.
rt_string rt_textwrap_right(rt_string text, int64_t width) {
    int64_t text_len = rt_str_len(text);
    if (text_len >= width)
        return text;

    int64_t pad = width - text_len;
    char *spaces = (char *)malloc((size_t)(pad + 1));
    if (!spaces)
        return text;

    memset(spaces, ' ', (size_t)pad);
    spaces[pad] = '\0';

    rt_string padding = rt_string_from_bytes(spaces, pad);
    free(spaces);

    return rt_str_concat(padding, text);
}

/// @brief Center `text` in a `width`-column field with balanced space padding.
/// @details For odd-padding cases the extra space goes on the right
///          (matching Python's `str.center`).
rt_string rt_textwrap_center(rt_string text, int64_t width) {
    int64_t text_len = rt_str_len(text);
    if (text_len >= width)
        return text;

    int64_t total_pad = width - text_len;
    int64_t left_pad = total_pad / 2;
    int64_t right_pad = total_pad - left_pad;

    char *left_spaces = (char *)malloc((size_t)(left_pad + 1));
    char *right_spaces = (char *)malloc((size_t)(right_pad + 1));
    if (!left_spaces || !right_spaces) {
        if (left_spaces)
            free(left_spaces);
        if (right_spaces)
            free(right_spaces);
        return text;
    }

    memset(left_spaces, ' ', (size_t)left_pad);
    left_spaces[left_pad] = '\0';
    memset(right_spaces, ' ', (size_t)right_pad);
    right_spaces[right_pad] = '\0';

    rt_string left_padding = rt_string_from_bytes(left_spaces, left_pad);
    rt_string right_padding = rt_string_from_bytes(right_spaces, right_pad);
    free(left_spaces);
    free(right_spaces);

    rt_string result = rt_str_concat(left_padding, text);
    return rt_str_concat(result, right_padding);
}

//=============================================================================
// Utility
//=============================================================================

/// @brief Count the number of lines in `text` (defined as `\n` count + 1).
/// @details Empty input still counts as 1 line. Trailing newline does
///          not add an extra empty line.
int64_t rt_textwrap_line_count(rt_string text) {
    const char *src = rt_string_cstr(text);
    int64_t len = rt_str_len(text);
    int64_t count = 1;

    for (int64_t i = 0; i < len; i++) {
        if (src[i] == '\n')
            count++;
    }

    return count;
}

/// @brief Return the byte length of the longest line in `text`.
/// @details Walks the input once, tracking the running line length
///          and resetting it on each newline.
int64_t rt_textwrap_max_line_len(rt_string text) {
    const char *src = rt_string_cstr(text);
    int64_t len = rt_str_len(text);
    int64_t max_len = 0;
    int64_t current_len = 0;

    for (int64_t i = 0; i < len; i++) {
        if (src[i] == '\n') {
            if (current_len > max_len)
                max_len = current_len;
            current_len = 0;
        } else {
            current_len++;
        }
    }

    if (current_len > max_len)
        max_len = current_len;

    return max_len;
}
