//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_html.c
/// @brief Tolerant HTML parser and utility functions for Viper.Text.Html.
///
/// This file implements a tolerant HTML parser and a suite of HTML utility
/// functions for escaping, unescaping, tag stripping, and content extraction.
///
/// **Parser:**
/// The parser builds a tree of rt_map nodes. Each node has:
/// - "tag": tag name string (empty for root/text nodes)
/// - "text": text content string
/// - "attrs": rt_map of attribute key-value pairs
/// - "children": rt_seq of child nodes
///
/// **Tolerant Parsing:**
/// - Handles unclosed tags, self-closing tags, and malformed HTML gracefully.
/// - Does not enforce strict HTML nesting rules.
///
/// **Utility Functions:**
/// - escape/unescape: Handle the 5 standard HTML entities plus numeric refs.
/// - strip_tags: Remove all tags, leaving raw text.
/// - to_text: Strip tags and unescape entities.
/// - extract_links: Find all href values in anchor tags.
/// - extract_text: Get text content of elements matching a tag name.
///
/// **Thread Safety:** All functions are thread-safe (no global mutable state).
///
/// @see rt_xml.c For the full XML parser
///
//===----------------------------------------------------------------------===//

#include "rt_html.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Helpers
//=============================================================================

/// @brief Case-insensitive prefix match.
static int starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

/// @brief Create a new HTML node as a map with tag, text, attrs, children.
static void *make_node(const char *tag, size_t tag_len,
                       const char *text, size_t text_len)
{
    void *node = rt_map_new();

    rt_string tag_key = rt_const_cstr("tag");
    rt_string text_key = rt_const_cstr("text");
    rt_string attrs_key = rt_const_cstr("attrs");
    rt_string children_key = rt_const_cstr("children");

    rt_string tag_val = rt_string_from_bytes(tag ? tag : "", tag_len);
    rt_string text_val = rt_string_from_bytes(text ? text : "", text_len);
    void *attrs = rt_map_new();
    void *children = rt_seq_new();

    rt_map_set(node, tag_key, (void *)tag_val);
    rt_map_set(node, text_key, (void *)text_val);
    rt_map_set(node, attrs_key, attrs);
    rt_map_set(node, children_key, children);

    return node;
}

/// @brief Known self-closing HTML tags.
static int is_self_closing_tag(const char *tag, size_t len)
{
    // Common self-closing tags
    static const char *self_closing[] = {
        "br", "hr", "img", "input", "meta", "link",
        "area", "base", "col", "embed", "param",
        "source", "track", "wbr", NULL};

    for (int i = 0; self_closing[i]; i++)
    {
        if (len == strlen(self_closing[i]) && starts_with_ci(tag, self_closing[i]))
            return 1;
    }
    return 0;
}

/// @brief Parse attributes from a tag body like: key="value" key2='value2'.
static void parse_attrs(void *attrs_map, const char *start, const char *end)
{
    const char *p = start;
    while (p < end)
    {
        // Skip whitespace
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
            p++;
        if (p >= end)
            break;

        // Read attribute name
        const char *name_start = p;
        while (p < end && *p != '=' && *p != ' ' && *p != '\t' && *p != '>' && *p != '/')
            p++;
        size_t name_len = (size_t)(p - name_start);
        if (name_len == 0)
        {
            p++;
            continue;
        }

        // Skip whitespace around '='
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        if (p < end && *p == '=')
        {
            p++; // skip '='
            while (p < end && (*p == ' ' || *p == '\t'))
                p++;

            const char *val_start;
            size_t val_len;

            if (p < end && (*p == '"' || *p == '\''))
            {
                char quote = *p;
                p++;
                val_start = p;
                while (p < end && *p != quote)
                    p++;
                val_len = (size_t)(p - val_start);
                if (p < end)
                    p++; // skip closing quote
            }
            else
            {
                val_start = p;
                while (p < end && *p != ' ' && *p != '\t' && *p != '>' && *p != '/')
                    p++;
                val_len = (size_t)(p - val_start);
            }

            rt_string key = rt_string_from_bytes(name_start, name_len);
            rt_string val = rt_string_from_bytes(val_start, val_len);
            rt_map_set(attrs_map, key, (void *)val);
        }
        else
        {
            // Boolean attribute (no value)
            rt_string key = rt_string_from_bytes(name_start, name_len);
            rt_string val = rt_string_from_bytes("", 0);
            rt_map_set(attrs_map, key, (void *)val);
        }
    }
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Parses HTML text into a tree of map nodes.
///
/// Creates a root node with tag="" and populates it with children representing
/// the parsed HTML structure. Each node is a map with "tag", "text", "attrs",
/// and "children" keys.
///
/// @param str HTML text to parse.
/// @return Root map node. Returns an empty root node for NULL/empty input.
void *rt_html_parse(rt_string str)
{
    void *root = make_node("", 0, "", 0);
    if (!str)
        return root;

    const char *src = rt_string_cstr(str);
    if (!src || !*src)
        return root;

    size_t len = strlen(src);
    const char *p = src;
    const char *end = src + len;

    // Simple stack of parent nodes (max depth 256)
    void *stack[256];
    int depth = 0;
    stack[0] = root;

    while (p < end)
    {
        if (*p == '<')
        {
            // Check for closing tag
            if (p + 1 < end && p[1] == '/')
            {
                const char *close_start = p + 2;
                const char *close_end = strchr(close_start, '>');
                if (!close_end)
                    break;

                // Pop from stack if tag matches
                if (depth > 0)
                    depth--;

                p = close_end + 1;
                continue;
            }

            // Check for comment
            if (p + 3 < end && p[1] == '!' && p[2] == '-' && p[3] == '-')
            {
                const char *comment_end = strstr(p + 4, "-->");
                if (comment_end)
                    p = comment_end + 3;
                else
                    p = end; // Unterminated comment
                continue;
            }

            // Check for doctype / processing instruction
            if (p + 1 < end && (p[1] == '!' || p[1] == '?'))
            {
                const char *tag_close = strchr(p, '>');
                p = tag_close ? tag_close + 1 : end;
                continue;
            }

            // Opening tag
            const char *tag_start = p + 1;
            const char *tag_close = strchr(p, '>');
            if (!tag_close)
                break;

            // Extract tag name
            const char *name_end = tag_start;
            while (name_end < tag_close && *name_end != ' ' && *name_end != '\t' &&
                   *name_end != '\n' && *name_end != '/' && *name_end != '>')
                name_end++;

            size_t tag_name_len = (size_t)(name_end - tag_start);
            if (tag_name_len == 0)
            {
                p = tag_close + 1;
                continue;
            }

            // Create node
            void *node = make_node(tag_start, tag_name_len, "", 0);

            // Parse attributes
            if (name_end < tag_close)
            {
                const char *attr_end = tag_close;
                if (tag_close > tag_start && tag_close[-1] == '/')
                    attr_end = tag_close - 1;

                rt_string attrs_key = rt_const_cstr("attrs");
                void *attrs = rt_map_get(node, attrs_key);
                if (attrs)
                    parse_attrs(attrs, name_end, attr_end);
            }

            // Add as child of current parent
            void *parent = stack[depth];
            rt_string children_key = rt_const_cstr("children");
            void *children = rt_map_get(parent, children_key);
            rt_seq_push(children, node);

            // Check if self-closing
            int self_close = (tag_close > tag_start && tag_close[-1] == '/') ||
                             is_self_closing_tag(tag_start, tag_name_len);

            if (!self_close && depth < 255)
            {
                depth++;
                stack[depth] = node;
            }

            p = tag_close + 1;
        }
        else
        {
            // Text content
            const char *text_start = p;
            while (p < end && *p != '<')
                p++;

            size_t text_len = (size_t)(p - text_start);

            // Skip whitespace-only text nodes
            int all_ws = 1;
            for (size_t i = 0; i < text_len; i++)
            {
                if (text_start[i] != ' ' && text_start[i] != '\t' &&
                    text_start[i] != '\n' && text_start[i] != '\r')
                {
                    all_ws = 0;
                    break;
                }
            }

            if (!all_ws && text_len > 0)
            {
                void *text_node = make_node("", 0, text_start, text_len);

                void *parent = stack[depth];
                rt_string children_key = rt_const_cstr("children");
                void *children = rt_map_get(parent, children_key);
                rt_seq_push(children, text_node);
            }
        }
    }

    return root;
}

/// @brief Strips all HTML tags and unescapes entities to produce plain text.
///
/// Combines strip_tags + unescape for a single call to get readable text.
///
/// @param str HTML text.
/// @return Plain text string.
rt_string rt_html_to_text(rt_string str)
{
    rt_string stripped = rt_html_strip_tags(str);
    rt_string result = rt_html_unescape(stripped);
    return result;
}

/// @brief Escapes HTML special characters.
///
/// Replaces <, >, &, ", ' with their HTML entity equivalents.
///
/// @param str Text to escape.
/// @return Escaped HTML-safe string. Returns empty string for NULL input.
rt_string rt_html_escape(rt_string str)
{
    if (!str)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(str);
    if (!src)
        return rt_string_from_bytes("", 0);

    size_t src_len = strlen(src);

    // Calculate output size
    size_t out_len = 0;
    for (size_t i = 0; i < src_len; i++)
    {
        switch (src[i])
        {
        case '<':
        case '>':
            out_len += 4;
            break; // &lt; &gt;
        case '&':
            out_len += 5;
            break; // &amp;
        case '"':
            out_len += 6;
            break; // &quot;
        case '\'':
            out_len += 5;
            break; // &#39;
        default:
            out_len += 1;
            break;
        }
    }

    char *out = (char *)malloc(out_len + 1);
    if (!out)
    {
        rt_trap("Html.Escape: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t pos = 0;
    for (size_t i = 0; i < src_len; i++)
    {
        switch (src[i])
        {
        case '<':
            memcpy(out + pos, "&lt;", 4);
            pos += 4;
            break;
        case '>':
            memcpy(out + pos, "&gt;", 4);
            pos += 4;
            break;
        case '&':
            memcpy(out + pos, "&amp;", 5);
            pos += 5;
            break;
        case '"':
            memcpy(out + pos, "&quot;", 6);
            pos += 6;
            break;
        case '\'':
            memcpy(out + pos, "&#39;", 5);
            pos += 5;
            break;
        default:
            out[pos++] = src[i];
            break;
        }
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}

/// @brief Unescapes HTML entities to their character equivalents.
///
/// Handles: &lt; &gt; &amp; &quot; &#39; &apos; &nbsp; and numeric
/// character references (&#NNN; and &#xHHH;) for ASCII range.
///
/// @param str String with HTML entities.
/// @return Unescaped string. Returns empty string for NULL input.
rt_string rt_html_unescape(rt_string str)
{
    if (!str)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(str);
    if (!src)
        return rt_string_from_bytes("", 0);

    size_t src_len = strlen(src);

    char *out = (char *)malloc(src_len + 1);
    if (!out)
    {
        rt_trap("Html.Unescape: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t pos = 0;
    const char *p = src;

    while (*p)
    {
        if (*p == '&')
        {
            if (starts_with_ci(p, "&lt;"))
            {
                out[pos++] = '<';
                p += 4;
            }
            else if (starts_with_ci(p, "&gt;"))
            {
                out[pos++] = '>';
                p += 4;
            }
            else if (starts_with_ci(p, "&amp;"))
            {
                out[pos++] = '&';
                p += 5;
            }
            else if (starts_with_ci(p, "&quot;"))
            {
                out[pos++] = '"';
                p += 6;
            }
            else if (starts_with_ci(p, "&#39;"))
            {
                out[pos++] = '\'';
                p += 5;
            }
            else if (starts_with_ci(p, "&apos;"))
            {
                out[pos++] = '\'';
                p += 6;
            }
            else if (starts_with_ci(p, "&nbsp;"))
            {
                out[pos++] = ' ';
                p += 6;
            }
            else if (p[1] == '#')
            {
                // Numeric character reference
                const char *start = p + 2;
                int base = 10;
                if (*start == 'x' || *start == 'X')
                {
                    base = 16;
                    start++;
                }
                char *num_end;
                long code = strtol(start, &num_end, base);
                if (*num_end == ';' && code > 0 && code < 128)
                {
                    out[pos++] = (char)code;
                    p = num_end + 1;
                }
                else
                {
                    out[pos++] = *p;
                    p++;
                }
            }
            else
            {
                out[pos++] = *p;
                p++;
            }
        }
        else
        {
            out[pos++] = *p;
            p++;
        }
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}

/// @brief Removes all HTML tags from a string.
///
/// Simple state machine that strips everything between < and >.
/// Does NOT unescape entities (use rt_html_to_text for that).
///
/// @param str HTML text.
/// @return String with tags stripped. Returns empty string for NULL input.
rt_string rt_html_strip_tags(rt_string str)
{
    if (!str)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(str);
    if (!src)
        return rt_string_from_bytes("", 0);

    size_t src_len = strlen(src);

    char *out = (char *)malloc(src_len + 1);
    if (!out)
    {
        rt_trap("Html.StripTags: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t pos = 0;
    int in_tag = 0;
    const char *p = src;

    while (*p)
    {
        if (*p == '<')
        {
            in_tag = 1;
        }
        else if (*p == '>')
        {
            in_tag = 0;
        }
        else if (!in_tag)
        {
            out[pos++] = *p;
        }
        p++;
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}

/// @brief Extracts all href values from anchor (<a>) tags.
///
/// Scans through HTML for <a ...href="url"...> patterns and extracts the
/// URL values.
///
/// @param str HTML text.
/// @return Seq of href value strings. Empty seq for NULL input.
void *rt_html_extract_links(rt_string str)
{
    void *seq = rt_seq_new();
    if (!str)
        return seq;

    const char *src = rt_string_cstr(str);
    if (!src)
        return seq;

    const char *p = src;

    while (*p)
    {
        if (*p == '<' && (p[1] == 'a' || p[1] == 'A') &&
            (p[2] == ' ' || p[2] == '\t' || p[2] == '\n'))
        {
            const char *tag_end = strchr(p, '>');
            if (!tag_end)
                break;

            // Search for href= within the tag
            const char *href = p + 2;
            while (href < tag_end)
            {
                if (starts_with_ci(href, "href="))
                {
                    href += 5;
                    // Skip whitespace
                    while (href < tag_end && (*href == ' ' || *href == '\t'))
                        href++;

                    char quote = 0;
                    if (href < tag_end && (*href == '"' || *href == '\''))
                    {
                        quote = *href;
                        href++;
                    }

                    const char *url_start = href;
                    if (quote)
                    {
                        while (href < tag_end && *href != quote)
                            href++;
                    }
                    else
                    {
                        while (href < tag_end && *href != ' ' && *href != '>')
                            href++;
                    }

                    size_t url_len = (size_t)(href - url_start);
                    rt_string url = rt_string_from_bytes(url_start, url_len);
                    rt_seq_push(seq, (void *)url);
                    break;
                }
                href++;
            }
            p = tag_end + 1;
        }
        else
        {
            p++;
        }
    }
    return seq;
}

/// @brief Extracts text content of all elements matching a tag name.
///
/// Finds all occurrences of <tag>...</tag> and extracts the text between
/// them (with inner tags stripped).
///
/// @param str HTML text.
/// @param tag Tag name to match (case-insensitive).
/// @return Seq of text content strings. Empty seq for NULL input.
void *rt_html_extract_text(rt_string str, rt_string tag)
{
    void *seq = rt_seq_new();
    if (!str || !tag)
        return seq;

    const char *src = rt_string_cstr(str);
    const char *tag_name = rt_string_cstr(tag);
    if (!src || !tag_name)
        return seq;

    size_t tag_len = strlen(tag_name);
    const char *p = src;

    while (*p)
    {
        if (*p == '<')
        {
            const char *start = p + 1;
            if (starts_with_ci(start, tag_name) &&
                (start[tag_len] == '>' || start[tag_len] == ' ' ||
                 start[tag_len] == '\t' || start[tag_len] == '/'))
            {
                const char *tag_end = strchr(p, '>');
                if (!tag_end)
                    break;

                // Skip self-closing tags
                if (tag_end > p && tag_end[-1] == '/')
                {
                    p = tag_end + 1;
                    continue;
                }

                const char *content_start = tag_end + 1;

                // Build close pattern: </tagname
                char close_pat[64];
                close_pat[0] = '/';
                size_t copy_len = tag_len < 60 ? tag_len : 60;
                memcpy(close_pat + 1, tag_name, copy_len);
                close_pat[1 + copy_len] = '\0';

                // Find closing tag
                const char *close = content_start;
                while (*close)
                {
                    if (*close == '<' && starts_with_ci(close + 1, close_pat))
                        break;
                    close++;
                }

                if (*close)
                {
                    size_t inner_len = (size_t)(close - content_start);
                    rt_string inner = rt_string_from_bytes(content_start, inner_len);
                    rt_string text = rt_html_strip_tags(inner);
                    rt_seq_push(seq, (void *)text);
                    const char *close_end = strchr(close, '>');
                    p = close_end ? close_end + 1 : close;
                }
                else
                {
                    p = tag_end + 1;
                }
            }
            else
            {
                p++;
            }
        }
        else
        {
            p++;
        }
    }
    return seq;
}
