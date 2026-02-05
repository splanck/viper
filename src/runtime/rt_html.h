//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_html.h
// Purpose: Tolerant HTML parser and utility functions for Viper.Text.Html.
// Key invariants: Parser is tolerant of malformed HTML; escape/unescape handle
//                 standard HTML entities. Tree nodes use rt_map.
// Ownership/Lifetime: Returned strings and objects are newly allocated.
// Links: docs/viperlib.md
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
