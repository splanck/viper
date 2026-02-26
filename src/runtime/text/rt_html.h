//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_html.h
// Purpose: Tolerant HTML parser and utilities for Viper.Text.Html, providing parsing to a tree
// structure, text extraction, escaping, and unescaping of HTML entities.
//
// Key invariants:
//   - Parser is tolerant of malformed HTML; it does not trap on invalid input.
//   - HTML entity escaping handles the standard five entities (&, <, >, ', ").
//   - Unescaping handles named entities and numeric character references.
//   - Tree nodes use rt_map for attribute storage.
//
// Ownership/Lifetime:
//   - Returned strings and objects are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of parsing.
//
// Links: src/runtime/text/rt_html.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Parse an HTML string into a tree of map nodes.
    /// @param str HTML text to parse.
    /// @return Root node (map) with keys: "tag", "text", "attrs" (map),
    ///         "children" (seq of nodes).
    void *rt_html_parse(rt_string str);

    /// @brief Strip all HTML tags and unescape entities to get plain text.
    /// @param str HTML text.
    /// @return Plain text string with tags removed and entities unescaped.
    rt_string rt_html_to_text(rt_string str);

    /// @brief Escape HTML special characters (<, >, &, ", ').
    /// @param str Text to escape.
    /// @return Escaped HTML-safe string.
    rt_string rt_html_escape(rt_string str);

    /// @brief Unescape HTML entities (&lt;, &gt;, &amp;, &quot;, &#39;).
    /// @param str String with HTML entities.
    /// @return Unescaped string.
    rt_string rt_html_unescape(rt_string str);

    /// @brief Remove all HTML tags from a string.
    /// @param str HTML text.
    /// @return String with all tags stripped (entities NOT unescaped).
    rt_string rt_html_strip_tags(rt_string str);

    /// @brief Extract all href values from anchor (<a>) tags.
    /// @param str HTML text.
    /// @return Seq of href value strings.
    void *rt_html_extract_links(rt_string str);

    /// @brief Extract text content of all matching tags.
    /// @param str HTML text.
    /// @param tag Tag name to match (e.g., "p", "h1").
    /// @return Seq of text content strings from matching tags.
    void *rt_html_extract_text(rt_string str, rt_string tag);

#ifdef __cplusplus
}
#endif
