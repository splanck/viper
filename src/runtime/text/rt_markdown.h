//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_markdown.h
// Purpose: Basic Markdown to HTML conversion and text extraction supporting common Markdown syntax: headers, bold, italic, links, code blocks, lists, and paragraphs.
//
// Key invariants:
//   - Supports ATX-style headers (#, ##, ..., ######), bold (**), italic (*), inline code (`).
//   - Supports fenced code blocks (```), unordered lists (-/*), ordered lists (1.).
//   - Not a full CommonMark implementation; edge cases may differ.
//   - rt_markdown_extract_links returns a Seq of [text, url] pairs.
//
// Ownership/Lifetime:
//   - Returned strings are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of conversion.
//
// Links: src/runtime/text/rt_markdown.c (implementation), src/runtime/core/rt_string.h
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
