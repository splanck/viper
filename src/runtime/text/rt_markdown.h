//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_markdown.h
// Purpose: Basic Markdown to HTML conversion and text extraction.
// Key invariants: Supports common Markdown: headers, bold, italic, links, code,
//                 lists, paragraphs. Not a full CommonMark implementation.
// Ownership/Lifetime: Returns new strings. Caller manages.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Convert Markdown to HTML.
    /// @param md Markdown source string.
    /// @return HTML string.
    rt_string rt_markdown_to_html(rt_string md);

    /// @brief Strip Markdown to plain text.
    /// @param md Markdown source string.
    /// @return Plain text string with formatting removed.
    rt_string rt_markdown_to_text(rt_string md);

    /// @brief Extract all links from Markdown.
    /// @param md Markdown source string.
    /// @return Seq of URL strings.
    void *rt_markdown_extract_links(rt_string md);

    /// @brief Extract all headings from Markdown.
    /// @param md Markdown source string.
    /// @return Seq of heading text strings.
    void *rt_markdown_extract_headings(rt_string md);

#ifdef __cplusplus
}
#endif
