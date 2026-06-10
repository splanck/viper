//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_html.c
// Purpose: Implements a tolerant HTML parser and utility functions for the
//          Viper.Text.Html class. Builds a tree of rt_map nodes (tag, text,
//          attrs, children). Also provides Escape, Unescape, StripTags, ToText,
//          ExtractLinks, and ExtractText utilities.
//
// Key invariants:
//   - Parsing is tolerant: unclosed tags, self-closing tags, and malformed HTML
//     are handled gracefully without trapping.
//   - Each node is an rt_map with keys "tag", "text", "attrs", "children".
//   - Escape handles the 5 standard HTML entities (&, <, >, ", ') plus numerics.
//   - StripTags removes all markup and returns only the concatenated text content.
//   - ExtractLinks finds all href attribute values in anchor elements.
//   - All functions are thread-safe with no global mutable state.
//
// Ownership/Lifetime:
//   - The returned parse tree (rt_map/rt_seq nodes) is owned by the caller.
//   - All returned strings are fresh allocations; input strings are borrowed.
//
// Links: src/runtime/text/rt_html.h (public API),
//        src/runtime/text/rt_xml.h (strict XML parser, related)
//
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

#include "rt_trap.h"

//=============================================================================
// Internal Helpers
//=============================================================================

static void release_local_obj(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static int ascii_eq_ci(char a, char b) {
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

/// @brief Case-insensitive prefix match.
static int starts_with_ci(const char *s, const char *prefix) {
    while (*prefix) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int starts_with_ci_bounded(const char *s, const char *end, const char *prefix) {
    while (*prefix) {
        if (s >= end || !ascii_eq_ci(*s, *prefix))
            return 0;
        s++;
        prefix++;
    }
    return 1;
}

static int bytes_eq_ci(const char *a, size_t a_len, const char *b, size_t b_len) {
    if (a_len != b_len)
        return 0;
    for (size_t i = 0; i < a_len; i++) {
        if (!ascii_eq_ci(a[i], b[i]))
            return 0;
    }
    return 1;
}

static int slash_closes_tag(const char *p, const char *tag_end) {
    if (p >= tag_end || *p != '/')
        return 0;
    p++;
    while (p < tag_end && isspace((unsigned char)*p))
        p++;
    return p == tag_end;
}

static const char *find_byte_bounded(const char *p, const char *end, char needle) {
    while (p < end) {
        if (*p == needle)
            return p;
        p++;
    }
    return NULL;
}

static const char *find_bytes_bounded(const char *p,
                                      const char *end,
                                      const char *needle,
                                      size_t needle_len) {
    if (needle_len == 0)
        return p;
    while ((size_t)(end - p) >= needle_len) {
        if (memcmp(p, needle, needle_len) == 0)
            return p;
        p++;
    }
    return NULL;
}

static void *map_get_cstr(void *map, const char *key_cstr) {
    rt_string key = rt_const_cstr(key_cstr);
    void *value = rt_map_get(map, key);
    rt_string_unref(key);
    return value;
}

static int runtime_string_eq_ci_bytes(rt_string value, const char *bytes, size_t len) {
    if (!value)
        return len == 0;
    return bytes_eq_ci(rt_string_cstr(value), (size_t)rt_str_len(value), bytes, len);
}

static int tag_boundary(char c) {
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int tag_name_matches_at(
    const char *lt, const char *end, const char *tag, size_t tag_len, int closing) {
    if (lt >= end || *lt != '<')
        return 0;
    const char *p = lt + 1;
    if (closing) {
        if (p >= end || *p != '/')
            return 0;
        p++;
    }
    if ((size_t)(end - p) < tag_len || !bytes_eq_ci(p, tag_len, tag, tag_len))
        return 0;
    return p + tag_len == end || tag_boundary(p[tag_len]);
}

static int tag_is_text_separator(const char *tag_start, const char *tag_end) {
    static const char *const separators[] = {
        "address",    "article", "aside",  "blockquote", "br",  "dd", "div", "dl",  "dt",
        "figcaption", "figure",  "footer", "h1",         "h2",  "h3", "h4",  "h5",  "h6",
        "header",     "hr",      "li",     "main",       "nav", "ol", "p",   "pre", "section",
        "table",      "td",      "th",     "tr",         "ul",  NULL};

    const char *p = tag_start;
    while (p < tag_end && (*p == '<' || *p == '/' || isspace((unsigned char)*p)))
        p++;
    const char *name_start = p;
    while (p < tag_end && !tag_boundary(*p))
        p++;
    size_t name_len = (size_t)(p - name_start);
    if (name_len == 0)
        return 0;

    for (int i = 0; separators[i]; i++) {
        size_t sep_len = strlen(separators[i]);
        if (bytes_eq_ci(name_start, name_len, separators[i], sep_len))
            return 1;
    }
    return 0;
}

/// @brief Create a new HTML node as a map with tag, text, attrs, children.
static void *make_node(const char *tag, size_t tag_len, const char *text, size_t text_len) {
    void *node = rt_map_new();

    rt_string tag_key = rt_const_cstr("tag");
    rt_string text_key = rt_const_cstr("text");
    rt_string attrs_key = rt_const_cstr("attrs");
    rt_string children_key = rt_const_cstr("children");

    rt_string tag_val = rt_string_from_bytes(tag ? tag : "", tag_len);
    rt_string text_val = rt_string_from_bytes(text ? text : "", text_len);
    void *attrs = rt_map_new();
    void *children = rt_seq_new();
    rt_seq_set_owns_elements(children, 1);

    rt_map_set(node, tag_key, (void *)tag_val);
    rt_map_set(node, text_key, (void *)text_val);
    rt_map_set(node, attrs_key, attrs);
    rt_map_set(node, children_key, children);

    rt_string_unref(tag_key);
    rt_string_unref(text_key);
    rt_string_unref(attrs_key);
    rt_string_unref(children_key);
    rt_string_unref(tag_val);
    rt_string_unref(text_val);
    release_local_obj(attrs);
    release_local_obj(children);

    return node;
}

/// @brief Known self-closing HTML tags.
static int is_self_closing_tag(const char *tag, size_t len) {
    // Common self-closing tags
    static const char *self_closing[] = {"br",
                                         "hr",
                                         "img",
                                         "input",
                                         "meta",
                                         "link",
                                         "area",
                                         "base",
                                         "col",
                                         "embed",
                                         "param",
                                         "source",
                                         "track",
                                         "wbr",
                                         NULL};

    for (int i = 0; self_closing[i]; i++) {
        if (len == strlen(self_closing[i]) && starts_with_ci(tag, self_closing[i]))
            return 1;
    }
    return 0;
}

/// @brief Parse the attribute portion of an HTML opening tag into a name→value map.
/// @details Walks `[start, end)` (the substring between the tag name
///          and the closing `>`) and extracts each `key="val"` /
///          `key='val'` / `key=bare` / boolean (no `=`) attribute.
///          Quirks accepted by browsers, mirrored here:
///          - Single OR double quotes for the value (chosen by the
///            opening quote; the matching one terminates).
///          - Unquoted values run until whitespace or `>` / `/`.
///          - A bare attribute (no `=`) maps to an empty string —
///            matches HTML5 boolean attribute semantics
///            (`<input disabled>` ⇒ `disabled = ""`).
///          Skips whitespace runs between tokens. Names are stored
///          case-as-is (caller normalizes if needed). Stops cleanly
///          at `end` even on malformed input — never reads past.
static void parse_attrs(void *attrs_map, const char *start, const char *end) {
    const char *p = start;
    while (p < end) {
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
        if (name_len == 0) {
            p++;
            continue;
        }

        // Skip whitespace around '='
        while (p < end && (*p == ' ' || *p == '\t'))
            p++;

        if (p < end && *p == '=') {
            p++; // skip '='
            while (p < end && (*p == ' ' || *p == '\t'))
                p++;

            const char *val_start;
            size_t val_len;

            if (p < end && (*p == '"' || *p == '\'')) {
                char quote = *p;
                p++;
                val_start = p;
                while (p < end && *p != quote)
                    p++;
                val_len = (size_t)(p - val_start);
                if (p < end)
                    p++; // skip closing quote
            } else {
                val_start = p;
                while (p < end && *p != ' ' && *p != '\t' && *p != '>' && !slash_closes_tag(p, end))
                    p++;
                val_len = (size_t)(p - val_start);
            }

            rt_string key = rt_string_from_bytes(name_start, name_len);
            rt_string val = rt_string_from_bytes(val_start, val_len);
            rt_map_set(attrs_map, key, (void *)val);
            rt_string_unref(key);
            rt_string_unref(val);
        } else {
            // Boolean attribute (no value)
            rt_string key = rt_string_from_bytes(name_start, name_len);
            rt_string val = rt_string_from_bytes("", 0);
            rt_map_set(attrs_map, key, (void *)val);
            rt_string_unref(key);
            rt_string_unref(val);
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
void *rt_html_parse(rt_string str) {
    void *root = make_node("", 0, "", 0);
    if (!str)
        return root;

    const char *src = rt_string_cstr(str);
    size_t len = (size_t)rt_str_len(str);
    if (!src || len == 0)
        return root;

    const char *p = src;
    const char *end = src + len;

    // Simple stack of parent nodes (max depth 256)
    void *stack[256];
    int depth = 0;
    stack[0] = root;

    while (p < end) {
        if (*p == '<') {
            // Check for closing tag
            if ((size_t)(end - p) >= 2 && p[1] == '/') {
                const char *close_start = p + 2;
                const char *close_end = find_byte_bounded(close_start, end, '>');
                if (!close_end)
                    break;

                while (close_start < close_end && (*close_start == ' ' || *close_start == '\t' ||
                                                   *close_start == '\n' || *close_start == '\r'))
                    close_start++;
                const char *close_name_end = close_start;
                while (close_name_end < close_end && !tag_boundary(*close_name_end))
                    close_name_end++;
                size_t close_len = (size_t)(close_name_end - close_start);

                // Pop only when the closing name matches an open element.
                if (close_len > 0) {
                    for (int d = depth; d > 0; d--) {
                        rt_string open_tag = (rt_string)map_get_cstr(stack[d], "tag");
                        if (runtime_string_eq_ci_bytes(open_tag, close_start, close_len)) {
                            depth = d - 1;
                            break;
                        }
                    }
                }

                p = close_end + 1;
                continue;
            }

            // Check for comment
            if ((size_t)(end - p) >= 4 && p[1] == '!' && p[2] == '-' && p[3] == '-') {
                const char *comment_end = find_bytes_bounded(p + 4, end, "-->", 3);
                if (comment_end)
                    p = comment_end + 3;
                else
                    p = end; // Unterminated comment
                continue;
            }

            // Check for doctype / processing instruction
            if ((size_t)(end - p) >= 2 && (p[1] == '!' || p[1] == '?')) {
                const char *tag_close = find_byte_bounded(p, end, '>');
                p = tag_close ? tag_close + 1 : end;
                continue;
            }

            // Opening tag
            const char *tag_start = p + 1;
            const char *tag_close = find_byte_bounded(p, end, '>');
            if (!tag_close)
                break;

            // Extract tag name
            const char *name_end = tag_start;
            while (name_end < tag_close && *name_end != ' ' && *name_end != '\t' &&
                   *name_end != '\n' && *name_end != '/' && *name_end != '>')
                name_end++;

            size_t tag_name_len = (size_t)(name_end - tag_start);
            if (tag_name_len == 0) {
                p = tag_close + 1;
                continue;
            }

            // Create node
            void *node = make_node(tag_start, tag_name_len, "", 0);

            // Parse attributes
            if (name_end < tag_close) {
                const char *attr_end = tag_close;
                if (tag_close > tag_start && tag_close[-1] == '/')
                    attr_end = tag_close - 1;

                void *attrs = map_get_cstr(node, "attrs");
                if (attrs)
                    parse_attrs(attrs, name_end, attr_end);
            }

            // Add as child of current parent
            void *parent = stack[depth];
            void *children = map_get_cstr(parent, "children");
            rt_seq_push(children, node);

            // Check if self-closing
            int self_close = (tag_close > tag_start && tag_close[-1] == '/') ||
                             is_self_closing_tag(tag_start, tag_name_len);

            if (!self_close && depth < 255) {
                depth++;
                stack[depth] = node;
            }

            release_local_obj(node);

            p = tag_close + 1;
        } else {
            // Text content
            const char *text_start = p;
            while (p < end && *p != '<')
                p++;

            size_t text_len = (size_t)(p - text_start);

            // Skip whitespace-only text nodes
            int all_ws = 1;
            for (size_t i = 0; i < text_len; i++) {
                if (text_start[i] != ' ' && text_start[i] != '\t' && text_start[i] != '\n' &&
                    text_start[i] != '\r') {
                    all_ws = 0;
                    break;
                }
            }

            if (!all_ws && text_len > 0) {
                void *text_node = make_node("", 0, text_start, text_len);

                void *parent = stack[depth];
                void *children = map_get_cstr(parent, "children");
                rt_seq_push(children, text_node);
                release_local_obj(text_node);
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
rt_string rt_html_to_text(rt_string str) {
    rt_string stripped = rt_html_strip_tags(str);
    rt_string result = rt_html_unescape(stripped);
    rt_string_unref(stripped);
    return result;
}

/// @brief Escapes HTML special characters.
///
/// Replaces <, >, &, ", ' with their HTML entity equivalents.
///
/// @param str Text to escape.
/// @return Escaped HTML-safe string. Returns empty string for NULL input.
rt_string rt_html_escape(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(str);
    if (!src)
        return rt_string_from_bytes("", 0);

    size_t src_len = (size_t)rt_str_len(str);

    // Calculate output size
    size_t out_len = 0;
    for (size_t i = 0; i < src_len; i++) {
        switch (src[i]) {
            case '<':
            case '>':
                if (out_len > SIZE_MAX - 4) {
                    rt_trap("Html.Escape: output length overflow");
                    return rt_string_from_bytes("", 0);
                }
                out_len += 4;
                break; // &lt; &gt;
            case '&':
                if (out_len > SIZE_MAX - 5) {
                    rt_trap("Html.Escape: output length overflow");
                    return rt_string_from_bytes("", 0);
                }
                out_len += 5;
                break; // &amp;
            case '"':
                if (out_len > SIZE_MAX - 6) {
                    rt_trap("Html.Escape: output length overflow");
                    return rt_string_from_bytes("", 0);
                }
                out_len += 6;
                break; // &quot;
            case '\'':
                if (out_len > SIZE_MAX - 5) {
                    rt_trap("Html.Escape: output length overflow");
                    return rt_string_from_bytes("", 0);
                }
                out_len += 5;
                break; // &#39;
            default:
                if (out_len == SIZE_MAX) {
                    rt_trap("Html.Escape: output length overflow");
                    return rt_string_from_bytes("", 0);
                }
                out_len += 1;
                break;
        }
    }
    if (out_len == SIZE_MAX) {
        rt_trap("Html.Escape: output length overflow");
        return rt_string_from_bytes("", 0);
    }

    char *out = (char *)malloc(out_len + 1);
    if (!out) {
        rt_trap("Html.Escape: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t pos = 0;
    for (size_t i = 0; i < src_len; i++) {
        switch (src[i]) {
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
rt_string rt_html_unescape(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(str);
    if (!src)
        return rt_string_from_bytes("", 0);

    size_t src_len = (size_t)rt_str_len(str);

    char *out = (char *)malloc(src_len + 1);
    if (!out) {
        rt_trap("Html.Unescape: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t pos = 0;
    const char *p = src;
    const char *end = src + src_len;

    while (p < end) {
        if (*p == '&') {
            if (starts_with_ci_bounded(p, end, "&lt;")) {
                out[pos++] = '<';
                p += 4;
            } else if (starts_with_ci_bounded(p, end, "&gt;")) {
                out[pos++] = '>';
                p += 4;
            } else if (starts_with_ci_bounded(p, end, "&amp;")) {
                out[pos++] = '&';
                p += 5;
            } else if (starts_with_ci_bounded(p, end, "&quot;")) {
                out[pos++] = '"';
                p += 6;
            } else if (starts_with_ci_bounded(p, end, "&#39;")) {
                out[pos++] = '\'';
                p += 5;
            } else if (starts_with_ci_bounded(p, end, "&apos;")) {
                out[pos++] = '\'';
                p += 6;
            } else if (starts_with_ci_bounded(p, end, "&nbsp;")) {
                out[pos++] = ' ';
                p += 6;
            } else if (p + 1 < end && p[1] == '#') {
                // Numeric character reference
                const char *start = p + 2;
                int base = 10;
                if (start < end && (*start == 'x' || *start == 'X')) {
                    base = 16;
                    start++;
                }
                long code = 0;
                int saw_digit = 0;
                while (start < end) {
                    int digit = -1;
                    if (*start >= '0' && *start <= '9')
                        digit = *start - '0';
                    else if (base == 16 && *start >= 'a' && *start <= 'f')
                        digit = *start - 'a' + 10;
                    else if (base == 16 && *start >= 'A' && *start <= 'F')
                        digit = *start - 'A' + 10;
                    else
                        break;
                    if (digit >= base)
                        break;
                    saw_digit = 1;
                    if (code < 128)
                        code = code * base + digit;
                    start++;
                }
                if (saw_digit && start < end && *start == ';' && code > 0 && code < 128) {
                    out[pos++] = (char)code;
                    p = start + 1;
                } else {
                    out[pos++] = *p;
                    p++;
                }
            } else {
                out[pos++] = *p;
                p++;
            }
        } else {
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
rt_string rt_html_strip_tags(rt_string str) {
    if (!str)
        return rt_string_from_bytes("", 0);

    const char *src = rt_string_cstr(str);
    if (!src)
        return rt_string_from_bytes("", 0);

    size_t src_len = (size_t)rt_str_len(str);

    char *out = (char *)malloc(src_len + 1);
    if (!out) {
        rt_trap("Html.StripTags: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    size_t pos = 0;
    int in_tag = 0;
    size_t tag_start = 0;

    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '<') {
            in_tag = 1;
            tag_start = i;
        } else if (src[i] == '>') {
            in_tag = 0;
            // Separate block-like tags without inventing spaces around inline tags.
            if (pos > 0 && out[pos - 1] != ' ' && tag_is_text_separator(src + tag_start, src + i))
                out[pos++] = ' ';
        } else if (!in_tag) {
            out[pos++] = src[i];
        }
    }
    // Trim trailing whitespace added by tag-boundary spacing
    while (pos > 0 && out[pos - 1] == ' ')
        pos--;
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
void *rt_html_extract_links(rt_string str) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!str)
        return seq;

    const char *src = rt_string_cstr(str);
    if (!src)
        return seq;

    const char *end = src + rt_str_len(str);
    const char *p = src;

    while (p < end) {
        if (*p == '<' && (size_t)(end - p) >= 3 && (p[1] == 'a' || p[1] == 'A') &&
            (p[2] == ' ' || p[2] == '\t' || p[2] == '\n' || p[2] == '\r')) {
            const char *tag_end = find_byte_bounded(p, end, '>');
            if (!tag_end)
                break;

            const char *attr = p + 2;
            while (attr < tag_end) {
                while (attr < tag_end && isspace((unsigned char)*attr))
                    attr++;
                if (attr >= tag_end)
                    break;

                const char *name_start = attr;
                while (attr < tag_end && *attr != '=' && !isspace((unsigned char)*attr) &&
                       *attr != '/' && *attr != '>')
                    attr++;
                size_t name_len = (size_t)(attr - name_start);
                while (attr < tag_end && isspace((unsigned char)*attr))
                    attr++;

                const char *value_start = attr;
                const char *value_end = attr;
                int has_value = 0;
                if (attr < tag_end && *attr == '=') {
                    has_value = 1;
                    attr++;
                    while (attr < tag_end && isspace((unsigned char)*attr))
                        attr++;
                    if (attr < tag_end && (*attr == '"' || *attr == '\'')) {
                        char quote = *attr++;
                        value_start = attr;
                        while (attr < tag_end && *attr != quote)
                            attr++;
                        value_end = attr;
                        if (attr < tag_end)
                            attr++;
                    } else {
                        value_start = attr;
                        while (attr < tag_end && !isspace((unsigned char)*attr) && *attr != '>' &&
                               !slash_closes_tag(attr, tag_end))
                            attr++;
                        value_end = attr;
                    }
                }

                if (name_len == 4 && bytes_eq_ci(name_start, name_len, "href", 4)) {
                    rt_string url =
                        has_value
                            ? rt_string_from_bytes(value_start, (size_t)(value_end - value_start))
                            : rt_string_from_bytes("", 0);
                    rt_seq_push(seq, (void *)url);
                    rt_string_unref(url);
                    break;
                }

                if (!has_value && attr == name_start)
                    attr++;
            }
            p = tag_end + 1;
        } else {
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
void *rt_html_extract_text(rt_string str, rt_string tag) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    if (!str || !tag)
        return seq;

    const char *src = rt_string_cstr(str);
    const char *tag_name = rt_string_cstr(tag);
    if (!src || !tag_name)
        return seq;

    size_t tag_len = (size_t)rt_str_len(tag);
    if (tag_len == 0)
        return seq;
    const char *end = src + rt_str_len(str);
    const char *p = src;

    while (p < end) {
        if (*p == '<') {
            if (tag_name_matches_at(p, end, tag_name, tag_len, 0)) {
                const char *tag_end = find_byte_bounded(p, end, '>');
                if (!tag_end)
                    break;

                // Skip self-closing tags
                if (tag_end > p && tag_end[-1] == '/') {
                    p = tag_end + 1;
                    continue;
                }

                const char *content_start = tag_end + 1;

                // Find closing tag
                const char *close = content_start;
                while (close < end) {
                    if (*close == '<' && tag_name_matches_at(close, end, tag_name, tag_len, 1))
                        break;
                    close++;
                }

                if (close < end) {
                    size_t inner_len = (size_t)(close - content_start);
                    rt_string inner = rt_string_from_bytes(content_start, inner_len);
                    rt_string text = rt_html_strip_tags(inner);
                    rt_seq_push(seq, (void *)text);
                    rt_string_unref(inner);
                    rt_string_unref(text);
                    const char *close_end = find_byte_bounded(close, end, '>');
                    p = close_end ? close_end + 1 : close;
                } else {
                    p = tag_end + 1;
                }
            } else {
                p++;
            }
        } else {
            p++;
        }
    }
    return seq;
}
