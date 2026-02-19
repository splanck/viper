//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_textwrap.h
// Purpose: Text wrapping utilities for formatting text to specified widths.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Basic Text Wrapping
    //=========================================================================

    /// @brief Wrap text to specified width.
    /// @param text Text to wrap.
    /// @param width Maximum line width.
    /// @return Wrapped text with newlines inserted.
    rt_string rt_textwrap_wrap(rt_string text, int64_t width);

    /// @brief Wrap text and return as sequence of lines.
    /// @param text Text to wrap.
    /// @param width Maximum line width.
    /// @return Sequence of line strings.
    void *rt_textwrap_wrap_lines(rt_string text, int64_t width);

    /// @brief Fill text by joining wrapped lines with single newline.
    /// @param text Text to fill.
    /// @param width Maximum line width.
    /// @return Filled text.
    rt_string rt_textwrap_fill(rt_string text, int64_t width);

    //=========================================================================
    // Indentation
    //=========================================================================

    /// @brief Indent text with prefix.
    /// @param text Text to indent.
    /// @param prefix Prefix to add to each line.
    /// @return Indented text.
    rt_string rt_textwrap_indent(rt_string text, rt_string prefix);

    /// @brief Remove common leading whitespace from all lines.
    /// @param text Text to dedent.
    /// @return Dedented text.
    rt_string rt_textwrap_dedent(rt_string text);

    /// @brief Indent text except first line.
    /// @param text Text to indent.
    /// @param prefix Prefix for subsequent lines.
    /// @return Text with indented continuation lines.
    rt_string rt_textwrap_hang(rt_string text, rt_string prefix);

    //=========================================================================
    // Truncation
    //=========================================================================

    /// @brief Truncate text to max length with ellipsis.
    /// @param text Text to truncate.
    /// @param width Maximum width.
    /// @return Truncated text with "..." if needed.
    rt_string rt_textwrap_truncate(rt_string text, int64_t width);

    /// @brief Truncate text with custom suffix.
    /// @param text Text to truncate.
    /// @param width Maximum width.
    /// @param suffix Suffix to add when truncating (e.g., "...").
    /// @return Truncated text.
    rt_string rt_textwrap_truncate_with(rt_string text, int64_t width, rt_string suffix);

    /// @brief Shorten text in middle with ellipsis.
    /// @param text Text to shorten.
    /// @param width Maximum width.
    /// @return Text with middle portion replaced by "...".
    rt_string rt_textwrap_shorten(rt_string text, int64_t width);

    //=========================================================================
    // Alignment
    //=========================================================================

    /// @brief Left-align text in specified width.
    /// @param text Text to align.
    /// @param width Target width.
    /// @return Left-aligned text padded with spaces.
    rt_string rt_textwrap_left(rt_string text, int64_t width);

    /// @brief Right-align text in specified width.
    /// @param text Text to align.
    /// @param width Target width.
    /// @return Right-aligned text padded with spaces.
    rt_string rt_textwrap_right(rt_string text, int64_t width);

    /// @brief Center text in specified width.
    /// @param text Text to center.
    /// @param width Target width.
    /// @return Centered text padded with spaces.
    rt_string rt_textwrap_center(rt_string text, int64_t width);

    //=========================================================================
    // Utility
    //=========================================================================

    /// @brief Count lines in text.
    /// @param text Text to count.
    /// @return Number of lines.
    int64_t rt_textwrap_line_count(rt_string text);

    /// @brief Get the maximum line length.
    /// @param text Text to measure.
    /// @return Length of longest line.
    int64_t rt_textwrap_max_line_len(rt_string text);

#ifdef __cplusplus
}
#endif
