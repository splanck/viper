//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_xml.c
// Purpose: Implements XML parsing and formatting for the Viper.Text.Xml class
//          per XML 1.0. Builds a node tree supporting elements, text content,
//          comments, and CDATA sections. Provides Parse, Format, FormatPretty,
//          and node navigation (Tag, Attr, SetAttr, Children, TextContent).
//
// Key invariants:
//   - Parse returns a document root node; invalid XML returns NULL.
//   - Element nodes carry a tag name, attribute rt_map, and children rt_seq.
//   - Text and CDATA nodes carry a text content string.
//   - Attributes are stored as an rt_map<String, String>.
//   - Format produces minimal XML (no added whitespace).
//   - The parser is NOT thread-safe; external synchronization is required.
//
// Ownership/Lifetime:
//   - The node tree is heap-allocated; all nodes are owned by their parent.
//   - The caller owns the root node returned by Parse.
//   - Formatted XML strings are fresh rt_string allocations owned by caller.
//
// Links: src/runtime/text/rt_xml.h (public API),
//        src/runtime/text/rt_html.h (tolerant HTML parser, related)
//
//===----------------------------------------------------------------------===//

#include "rt_xml.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// XML Node Structure
//=============================================================================

/// @brief Internal XML node structure.
typedef struct xml_node {
    rt_xml_node_type_t type; ///< Node type
    rt_string tag;           ///< Tag name (elements only)
    rt_string content;       ///< Text content (text/comment/cdata)
    void *attributes;        ///< Map of attributes (elements only)
    void *children;          ///< Seq of child nodes
    struct xml_node *parent; ///< Parent node (weak reference)
} xml_node;

/// @brief Last parse error message (thread-local to avoid concurrent parse clobbering).
static _Thread_local char xml_last_error[256] = {0};

//=============================================================================
// Forward Declarations
//=============================================================================

static void xml_node_finalizer(void *obj);
static void *xml_node_new(rt_xml_node_type_t type);
static void *parse_document(const char *input, size_t len);

//=============================================================================
// Helper Functions
//=============================================================================

#include "rt_trap.h"

/// @brief Stash a parse-error message in the thread-local error buffer.
///
/// Truncates to fit within the 256-byte buffer with a null terminator.
/// Subsequent calls overwrite. The error survives until `clear_error`
/// or the next failed parse on this thread, and is exposed via
/// `rt_xml_error()`.
static void set_error(const char *msg) {
    strncpy(xml_last_error, msg, sizeof(xml_last_error) - 1);
    xml_last_error[sizeof(xml_last_error) - 1] = '\0';
}

/// @brief Reset the thread-local parse-error buffer to empty.
///
/// Called at the start of every `parse_document` so success returns
/// clear `rt_xml_error()` output.
static void clear_error(void) {
    xml_last_error[0] = '\0';
}

//=============================================================================
// Node Management
//=============================================================================

/// @brief GC finalizer for XML nodes — cascades release through the tree.
///
/// Releases tag, content, attribute map, and (recursively, via the
/// children seq's own released items) every child. Children are owned
/// by their parent, so dropping the root cleans up the entire document.
/// `parent` is a weak pointer and is intentionally not released.
static void xml_node_finalizer(void *obj) {
    xml_node *node = (xml_node *)obj;
    if (!node)
        return;

    // Release tag
    if (node->tag) {
        if (rt_obj_release_check0((void *)node->tag))
            rt_obj_free((void *)node->tag);
    }

    // Release content
    if (node->content) {
        if (rt_obj_release_check0((void *)node->content))
            rt_obj_free((void *)node->content);
    }

    // Release attributes map
    if (node->attributes) {
        if (rt_obj_release_check0(node->attributes))
            rt_obj_free(node->attributes);
    }

    // Release children (parent owns each child — finalizer must release them)
    if (node->children) {
        int64_t count = rt_seq_len(node->children);
        for (int64_t i = 0; i < count; i++) {
            void *child = rt_seq_get(node->children, i);
            if (child) {
                if (rt_obj_release_check0(child))
                    rt_obj_free(child);
            }
        }
        if (rt_obj_release_check0(node->children))
            rt_obj_free(node->children);
    }

    // Note: parent is a weak reference, don't release
}

/// @brief Allocate a fresh XML node of the given type.
///
/// Hooks `xml_node_finalizer` so the GC takes care of cleanup. Element
/// and document nodes get an empty children seq; element nodes also
/// get an empty attribute map. Returns NULL on any allocation failure
/// (caller decides whether that should trap or be propagated).
///
/// @param type Node kind (element / text / comment / cdata / document).
/// @return Owned node pointer, or NULL on OOM.
static void *xml_node_new(rt_xml_node_type_t type) {
    xml_node *node = (xml_node *)rt_obj_new_i64(0, sizeof(xml_node));
    if (!node)
        return NULL;

    rt_obj_set_finalizer(node, xml_node_finalizer);

    node->type = type;
    node->tag = NULL;
    node->content = NULL;
    node->attributes = NULL;
    node->children = NULL;
    node->parent = NULL;

    // Create children seq for elements and documents
    if (type == XML_NODE_ELEMENT || type == XML_NODE_DOCUMENT) {
        node->children = rt_seq_new();
        if (!node->children) {
            rt_obj_free(node);
            return NULL;
        }
    }

    // Create attributes map for elements
    if (type == XML_NODE_ELEMENT) {
        node->attributes = rt_map_new();
        if (!node->attributes) {
            if (node->children) {
                rt_obj_release_check0(node->children);
                rt_obj_free(node->children);
            }
            rt_obj_free(node);
            return NULL;
        }
    }

    return node;
}

//=============================================================================
// Parser State
//=============================================================================

/* S-17: Maximum element nesting depth */
#define XML_MAX_DEPTH 200

typedef struct {
    const char *input;
    size_t len;
    size_t pos;
    int line;
    int col;
    int depth; // Current element nesting depth
} xml_parser;

// The parser is a hand-written recursive descent over a byte buffer.
// `pos` tracks the cursor, `line`/`col` are kept current for error
// messages, and `depth` enforces a maximum nesting depth (S-17).

/// @brief Initialize parser state at the beginning of `input`.
static void parser_init(xml_parser *p, const char *input, size_t len) {
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->line = 1;
    p->col = 1;
    p->depth = 0;
}

/// @brief True if the cursor is past the end of the input buffer.
static bool parser_eof(xml_parser *p) {
    return p->pos >= p->len;
}

/// @brief Return the byte at the cursor without consuming it; '\\0' at EOF.
static char parser_peek(xml_parser *p) {
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

/// @brief Consume and return the byte at the cursor, updating line/col.
///
/// Returns '\\0' at EOF without advancing. Used by every consuming
/// helper so the line/column stay accurate for error messages.
static char parser_advance(xml_parser *p) {
    if (p->pos >= p->len)
        return '\0';
    char c = p->input[p->pos++];
    if (c == '\n') {
        p->line++;
        p->col = 1;
    } else {
        p->col++;
    }
    return c;
}

/// @brief Advance past any whitespace at the cursor.
static void parser_skip_ws(xml_parser *p) {
    while (!parser_eof(p) && isspace((unsigned char)parser_peek(p)))
        parser_advance(p);
}

/// @brief Consume a literal string if it matches at the cursor; else no-op.
///
/// Returns true and advances past `str` on match, false and leaves the
/// cursor untouched otherwise. The piece-by-piece advance keeps the
/// line/col counters in sync.
static bool parser_match(xml_parser *p, const char *str) {
    size_t len = strlen(str);
    if (p->pos + len > p->len)
        return false;
    if (strncmp(p->input + p->pos, str, len) != 0)
        return false;
    for (size_t i = 0; i < len; i++)
        parser_advance(p);
    return true;
}

/// @brief Like `parser_match` but does not consume on success.
static bool parser_lookahead(xml_parser *p, const char *str) {
    size_t len = strlen(str);
    if (p->pos + len > p->len)
        return false;
    return strncmp(p->input + p->pos, str, len) == 0;
}

//=============================================================================
// Parsing Helpers
//=============================================================================

/// @brief Whether `c` may begin an XML name (letter, '_', or ':').
///
/// XML 1.0 allows a much broader set (including most Unicode letters)
/// but we restrict to ASCII for simplicity — sufficient for typical
/// configuration / data XML; will be revisited if non-ASCII tags
/// become a real requirement.
static bool is_name_start_char(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

/// @brief Whether `c` may continue an XML name (alnum, '_', ':', '-', '.').
static bool is_name_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.';
}

/// @brief Parse an XML name (tag or attribute) into a fresh `rt_string`.
///
/// Reads from `pos` up to the first non-name byte. Returns NULL (and
/// leaves the cursor untouched) if the first byte isn't a valid name
/// start. The returned string is caller-owned.
static rt_string parse_name(xml_parser *p) {
    size_t start = p->pos;

    if (parser_eof(p) || !is_name_start_char(parser_peek(p)))
        return NULL;

    while (!parser_eof(p) && is_name_char(parser_peek(p)))
        parser_advance(p);

    size_t len = p->pos - start;
    return rt_string_from_bytes(p->input + start, (int64_t)len);
}

/// @brief Decode a `&...;` entity reference into UTF-8 bytes.
///
/// Handles:
///   - Numeric: `&#NN;` (decimal) and `&#xHHHH;` (hex), encoded as 1-4 UTF-8 bytes.
///   - Named: `&lt; &gt; &amp; &quot; &apos;` only (no full HTML entity table).
/// Returns the number of bytes written to `out` (>=1), with `*consumed`
/// set to how many input bytes were eaten (including the leading `&`
/// and trailing `;`). Returns 0 if the input does not form a valid
/// entity reference, leaving the caller to copy the literal `&`.
///
/// @param str      Input cursor, must point at `&`.
/// @param len      Bytes available from `str`.
/// @param out      Destination buffer (must hold at least 4 bytes).
/// @param consumed Out: bytes consumed from `str` on success.
/// @return Bytes written to `out`, or 0 on parse failure.
static int decode_entity(const char *str, size_t len, char *out, size_t *consumed) {
    if (len < 2 || str[0] != '&')
        return 0;

    // Find semicolon
    size_t end = 1;
    while (end < len && str[end] != ';')
        end++;
    if (end >= len)
        return 0;

    *consumed = end + 1;

    // Character reference
    if (str[1] == '#') {
        unsigned int codepoint = 0;
        if (len > 2 && str[2] == 'x') {
            // Hex
            for (size_t i = 3; i < end; i++) {
                char c = str[i];
                if (c >= '0' && c <= '9')
                    codepoint = codepoint * 16 + (c - '0');
                else if (c >= 'a' && c <= 'f')
                    codepoint = codepoint * 16 + (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F')
                    codepoint = codepoint * 16 + (c - 'A' + 10);
                else
                    return 0;
            }
        } else {
            // Decimal
            for (size_t i = 2; i < end; i++) {
                char c = str[i];
                if (c >= '0' && c <= '9')
                    codepoint = codepoint * 10 + (c - '0');
                else
                    return 0;
            }
        }

        // Encode as UTF-8
        if (codepoint < 0x80) {
            out[0] = (char)codepoint;
            return 1;
        } else if (codepoint < 0x800) {
            out[0] = (char)(0xC0 | (codepoint >> 6));
            out[1] = (char)(0x80 | (codepoint & 0x3F));
            return 2;
        } else if (codepoint < 0x10000) {
            out[0] = (char)(0xE0 | (codepoint >> 12));
            out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            out[2] = (char)(0x80 | (codepoint & 0x3F));
            return 3;
        } else if (codepoint < 0x110000) {
            out[0] = (char)(0xF0 | (codepoint >> 18));
            out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
            out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            out[3] = (char)(0x80 | (codepoint & 0x3F));
            return 4;
        }
        return 0;
    }

    // Named entities
    size_t name_len = end - 1;
    if (name_len == 2 && strncmp(str + 1, "lt", 2) == 0) {
        out[0] = '<';
        return 1;
    }
    if (name_len == 2 && strncmp(str + 1, "gt", 2) == 0) {
        out[0] = '>';
        return 1;
    }
    if (name_len == 3 && strncmp(str + 1, "amp", 3) == 0) {
        out[0] = '&';
        return 1;
    }
    if (name_len == 4 && strncmp(str + 1, "quot", 4) == 0) {
        out[0] = '"';
        return 1;
    }
    if (name_len == 4 && strncmp(str + 1, "apos", 4) == 0) {
        out[0] = '\'';
        return 1;
    }

    return 0;
}

/// @brief Parse a single quoted attribute value, decoding entities.
///
/// Accepts either single or double quotes (the opening quote determines
/// the closing). Two-pass: first measures the decoded length so we can
/// allocate exactly, then re-reads to fill the buffer. Returns NULL on
/// allocation failure or unquoted input.
static rt_string parse_attr_value(xml_parser *p) {
    char quote = parser_peek(p);
    if (quote != '"' && quote != '\'')
        return NULL;
    parser_advance(p);

    // First pass: calculate decoded length
    size_t start = p->pos;
    size_t decoded_len = 0;
    while (!parser_eof(p) && parser_peek(p) != quote) {
        if (parser_peek(p) == '&') {
            char buf[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, buf, &consumed);
            if (n > 0) {
                decoded_len += n;
                p->pos += consumed;
                continue;
            }
        }
        decoded_len++;
        parser_advance(p);
    }

    // Rewind and decode
    p->pos = start;
    char *buf = malloc(decoded_len + 1);
    if (!buf)
        return NULL;

    size_t out_pos = 0;
    while (!parser_eof(p) && parser_peek(p) != quote) {
        if (parser_peek(p) == '&') {
            char decoded[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, decoded, &consumed);
            if (n > 0) {
                memcpy(buf + out_pos, decoded, n);
                out_pos += n;
                p->pos += consumed;
                continue;
            }
        }
        buf[out_pos++] = parser_advance(p);
    }
    buf[out_pos] = '\0';

    // Skip closing quote
    if (!parser_eof(p))
        parser_advance(p);

    rt_string result = rt_string_from_bytes(buf, (int64_t)out_pos);
    free(buf);
    return result;
}

/// @brief Parse element text content (everything up to the next `<`).
///
/// Same two-pass approach as `parse_attr_value`. Returns NULL when the
/// segment is empty (the caller treats that as "no text node here").
/// Whitespace-only text is filtered out one level up in `parse_node`.
static rt_string parse_text_content(xml_parser *p) {
    size_t start = p->pos;

    // First pass: find end and calculate decoded length
    size_t decoded_len = 0;
    while (!parser_eof(p) && parser_peek(p) != '<') {
        if (parser_peek(p) == '&') {
            char buf[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, buf, &consumed);
            if (n > 0) {
                decoded_len += n;
                p->pos += consumed;
                continue;
            }
        }
        decoded_len++;
        parser_advance(p);
    }

    if (decoded_len == 0)
        return NULL;

    // Rewind and decode
    p->pos = start;
    char *buf = malloc(decoded_len + 1);
    if (!buf)
        return NULL;

    size_t out_pos = 0;
    while (!parser_eof(p) && parser_peek(p) != '<') {
        if (parser_peek(p) == '&') {
            char decoded[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, decoded, &consumed);
            if (n > 0) {
                memcpy(buf + out_pos, decoded, n);
                out_pos += n;
                p->pos += consumed;
                continue;
            }
        }
        buf[out_pos++] = parser_advance(p);
    }
    buf[out_pos] = '\0';

    rt_string result = rt_string_from_bytes(buf, (int64_t)out_pos);
    free(buf);
    return result;
}

//=============================================================================
// Element Parsing
//=============================================================================

static void *parse_node(xml_parser *p);

/// @brief Parse a comment node `<!-- ... -->`.
///
/// The body is captured verbatim (no entity decoding inside comments
/// per XML 1.0). Reports an error if the closing `-->` is missing.
static void *parse_comment(xml_parser *p) {
    if (!parser_match(p, "<!--"))
        return NULL;

    size_t start = p->pos;
    while (!parser_eof(p) && !parser_lookahead(p, "-->"))
        parser_advance(p);

    size_t len = p->pos - start;

    if (!parser_match(p, "-->")) {
        set_error("Unterminated comment");
        return NULL;
    }

    void *node = xml_node_new(XML_NODE_COMMENT);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    n->content = rt_string_from_bytes(p->input + start, (int64_t)len);
    return node;
}

/// @brief Parse a CDATA section `<![CDATA[ ... ]]>`.
///
/// CDATA preserves the body byte-for-byte (no entity decoding) — useful
/// for embedding raw markup or scripts. Reports an error if the
/// closing `]]>` is missing.
static void *parse_cdata(xml_parser *p) {
    if (!parser_match(p, "<![CDATA["))
        return NULL;

    size_t start = p->pos;
    while (!parser_eof(p) && !parser_lookahead(p, "]]>"))
        parser_advance(p);

    size_t len = p->pos - start;

    if (!parser_match(p, "]]>")) {
        set_error("Unterminated CDATA section");
        return NULL;
    }

    void *node = xml_node_new(XML_NODE_CDATA);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    n->content = rt_string_from_bytes(p->input + start, (int64_t)len);
    return node;
}

/// @brief Skip past a processing instruction `<?target ... ?>`.
///
/// We don't model PIs as nodes — they're typically just `<?xml ... ?>`
/// declarations or stylesheet hints, neither of which the public API
/// exposes. Returns false (after setting the error) on missing `?>`.
static bool skip_processing_instruction(xml_parser *p) {
    if (!parser_match(p, "<?"))
        return false;

    while (!parser_eof(p) && !parser_lookahead(p, "?>"))
        parser_advance(p);

    if (!parser_match(p, "?>")) {
        set_error("Unterminated processing instruction");
        return false;
    }

    return true;
}

/// @brief Skip past a DOCTYPE declaration `<!DOCTYPE ... >`.
///
/// We don't validate against DTDs, so the contents are discarded.
/// Tracks `<` / `>` nesting (using a depth counter) so internal
/// subsets like `<!DOCTYPE foo [ ... ]>` are skipped correctly.
static bool skip_doctype(xml_parser *p) {
    if (!parser_lookahead(p, "<!DOCTYPE"))
        return false;

    parser_match(p, "<!DOCTYPE");

    int depth = 1;
    while (!parser_eof(p) && depth > 0) {
        char c = parser_peek(p);
        if (c == '<')
            depth++;
        else if (c == '>')
            depth--;
        parser_advance(p);
    }

    return true;
}

/// @brief Parse a complete element, including attributes and children.
///
/// Recursive: children are parsed by `parse_node`, which calls back into
/// `parse_element` for nested tags. `depth` is bumped on entry and
/// restored on every exit path to enforce the 200-deep limit (S-17:
/// blocks pathological recursion attacks). Verifies that the closing
/// `</tag>` matches the opening tag and reports a precise mismatch
/// error otherwise. Self-closing `<tag/>` is supported.
static void *parse_element(xml_parser *p) {
    /* S-17: Reject excessively nested documents */
    if (p->depth >= XML_MAX_DEPTH) {
        set_error("element nesting depth limit exceeded");
        return NULL;
    }
    p->depth++;

    if (!parser_match(p, "<")) {
        p->depth--;
        return NULL;
    }

    // Parse tag name
    rt_string tag = parse_name(p);
    if (!tag) {
        p->depth--;
        set_error("Expected element name");
        return NULL;
    }

    void *node = xml_node_new(XML_NODE_ELEMENT);
    if (!node) {
        p->depth--;
        if (rt_obj_release_check0((void *)tag))
            rt_obj_free((void *)tag);
        return NULL;
    }

    xml_node *elem = (xml_node *)node;
    elem->tag = tag;

    // Parse attributes
    for (;;) {
        parser_skip_ws(p);

        // Check for end of opening tag
        if (parser_lookahead(p, "/>")) {
            parser_match(p, "/>");
            p->depth--;
            return node; // Self-closing
        }
        if (parser_lookahead(p, ">")) {
            parser_match(p, ">");
            break; // Continue to content
        }

        // Parse attribute
        rt_string attr_name = parse_name(p);
        if (!attr_name) {
            p->depth--;
            set_error("Expected attribute name or tag end");
            if (rt_obj_release_check0(node))
                rt_obj_free(node);
            return NULL;
        }

        parser_skip_ws(p);
        if (!parser_match(p, "=")) {
            p->depth--;
            set_error("Expected '=' in attribute");
            if (rt_obj_release_check0((void *)attr_name))
                rt_obj_free((void *)attr_name);
            if (rt_obj_release_check0(node))
                rt_obj_free(node);
            return NULL;
        }
        parser_skip_ws(p);

        rt_string attr_value = parse_attr_value(p);
        if (!attr_value) {
            p->depth--;
            set_error("Expected attribute value");
            if (rt_obj_release_check0((void *)attr_name))
                rt_obj_free((void *)attr_name);
            if (rt_obj_release_check0(node))
                rt_obj_free(node);
            return NULL;
        }

        rt_map_set(elem->attributes, attr_name, (void *)attr_value);

        // Release our refs (map retains)
        if (rt_obj_release_check0((void *)attr_name))
            rt_obj_free((void *)attr_name);
        if (rt_obj_release_check0((void *)attr_value))
            rt_obj_free((void *)attr_value);
    }

    // Parse content
    while (!parser_eof(p)) {
        // Check for end tag
        if (parser_lookahead(p, "</")) {
            parser_match(p, "</");
            rt_string end_tag = parse_name(p);
            parser_skip_ws(p);
            parser_match(p, ">");

            // Verify tag match
            const char *start_tag_str = rt_string_cstr(elem->tag);
            const char *end_tag_str = rt_string_cstr(end_tag);
            if (strcmp(start_tag_str, end_tag_str) != 0) {
                char err[128];
                snprintf(
                    err, sizeof(err), "Mismatched tags: <%s> vs </%s>", start_tag_str, end_tag_str);
                set_error(err);
                p->depth--;
                if (rt_obj_release_check0((void *)end_tag))
                    rt_obj_free((void *)end_tag);
                if (rt_obj_release_check0(node))
                    rt_obj_free(node);
                return NULL;
            }

            if (rt_obj_release_check0((void *)end_tag))
                rt_obj_free((void *)end_tag);
            break;
        }

        // Parse child node
        void *child = parse_node(p);
        if (child) {
            xml_node *child_node = (xml_node *)child;
            child_node->parent = elem;
            rt_seq_push(elem->children, child);
            // Ownership transferred to elem; its finalizer will release child
        } else if (xml_last_error[0] != '\0') {
            // Parse error occurred
            p->depth--;
            if (rt_obj_release_check0(node))
                rt_obj_free(node);
            return NULL;
        }
    }

    p->depth--;
    return node;
}

/// @brief Dispatch to the appropriate parse routine based on the cursor.
///
/// Skips whitespace, then peeks at the upcoming bytes to decide:
/// comment, CDATA, processing instruction, DOCTYPE (skipped recursively),
/// element, or text. Drops whitespace-only text nodes so the tree
/// stays compact. Returns NULL at EOF or when the current segment
/// produces no node (e.g., whitespace-only text); a real parse error
/// is signalled via `xml_last_error[0] != 0`.
static void *parse_node(xml_parser *p) {
    parser_skip_ws(p);

    if (parser_eof(p))
        return NULL;

    // Comment
    if (parser_lookahead(p, "<!--"))
        return parse_comment(p);

    // CDATA
    if (parser_lookahead(p, "<![CDATA["))
        return parse_cdata(p);

    // Processing instruction (skip)
    if (parser_lookahead(p, "<?")) {
        skip_processing_instruction(p);
        return parse_node(p);
    }

    // DOCTYPE (skip)
    if (parser_lookahead(p, "<!DOCTYPE")) {
        skip_doctype(p);
        return parse_node(p);
    }

    // Element
    if (parser_lookahead(p, "<") && !parser_lookahead(p, "</"))
        return parse_element(p);

    // Text content
    rt_string text = parse_text_content(p);
    if (text) {
        // Skip whitespace-only text nodes
        const char *s = rt_string_cstr(text);
        bool all_ws = true;
        for (size_t i = 0; s[i]; i++) {
            if (!isspace((unsigned char)s[i])) {
                all_ws = false;
                break;
            }
        }

        if (all_ws) {
            if (rt_obj_release_check0((void *)text))
                rt_obj_free((void *)text);
            return NULL;
        }

        void *node = xml_node_new(XML_NODE_TEXT);
        if (!node) {
            if (rt_obj_release_check0((void *)text))
                rt_obj_free((void *)text);
            return NULL;
        }
        xml_node *n = (xml_node *)node;
        n->content = text;
        return node;
    }

    return NULL;
}

/// @brief Parse a complete XML document into a tree.
///
/// Builds a `XML_NODE_DOCUMENT` root and pushes every parsed top-level
/// node (typically one element plus optional comments / PIs) onto its
/// children seq. Returns NULL on parse failure, with the cause in
/// `xml_last_error`.
///
/// @param input UTF-8 XML source.
/// @param len   Length of `input` in bytes.
/// @return Owned document node, or NULL on failure.
static void *parse_document(const char *input, size_t len) {
    clear_error();

    xml_parser p;
    parser_init(&p, input, len);

    void *doc = xml_node_new(XML_NODE_DOCUMENT);
    if (!doc)
        return NULL;

    xml_node *doc_node = (xml_node *)doc;

    // Parse all root-level nodes
    while (!parser_eof(&p)) {
        parser_skip_ws(&p);
        if (parser_eof(&p))
            break;

        void *node = parse_node(&p);
        if (node) {
            xml_node *n = (xml_node *)node;
            n->parent = doc_node;
            rt_seq_push(doc_node->children, node);
            // Ownership transferred to doc; its finalizer will release node
        } else if (xml_last_error[0] != '\0') {
            // Parse error
            if (rt_obj_release_check0(doc))
                rt_obj_free(doc);
            return NULL;
        }
    }

    return doc;
}

//=============================================================================
// Public API - Parsing
//=============================================================================

/// @brief `Xml.Parse(text)` — parse XML source into a document tree.
///
/// Returns NULL on empty input or any parse error; call `Xml.Error()`
/// to retrieve a human-readable reason. Does not validate against any
/// schema or DTD.
///
/// @param text UTF-8 XML source.
/// @return Owned document node, or NULL on failure.
void *rt_xml_parse(rt_string text) {
    if (!text || rt_str_len(text) == 0) {
        set_error("Empty XML input");
        return NULL;
    }

    const char *cstr = rt_string_cstr(text);
    int64_t len = rt_str_len(text);

    return parse_document(cstr, (size_t)len);
}

/// @brief `Xml.Error()` — return the last parse error message on this thread.
///
/// Empty when the most recent parse succeeded. Errors are thread-local
/// so concurrent parses don't clobber each other's diagnostics.
///
/// @return Owned `rt_string` with the error text, or "" if none.
rt_string rt_xml_error(void) {
    return rt_string_from_bytes(xml_last_error, strlen(xml_last_error));
}

/// @brief `Xml.IsValid(text)` — boolean parse-success probe.
///
/// Internally runs `Parse` and discards the result. Useful for cheap
/// well-formedness checks without keeping the document around.
///
/// @param text Candidate XML.
/// @return 1 if it parses, 0 otherwise.
int8_t rt_xml_is_valid(rt_string text) {
    void *doc = rt_xml_parse(text);
    if (doc) {
        if (rt_obj_release_check0(doc))
            rt_obj_free(doc);
        return 1;
    }
    return 0;
}

//=============================================================================
// Public API - Node Creation
//=============================================================================

/// @brief `Xml.Element(tag)` — create a fresh element node.
///
/// The element starts with no attributes and no children. The tag
/// string is retained, so the caller can release its own reference
/// after the call.
///
/// @param tag Element tag name.
/// @return Owned element node, or NULL on OOM.
void *rt_xml_element(rt_string tag) {
    if (!tag)
        return NULL;

    void *node = xml_node_new(XML_NODE_ELEMENT);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    rt_obj_retain_maybe((void *)tag);
    n->tag = tag;
    return node;
}

/// @brief `Xml.Text(content)` — create a text node.
///
/// Used for `<elem>foo</elem>` -style content. Use `Xml.Cdata` instead
/// if the body should bypass entity escaping during formatting.
///
/// @param content Text body (NULL produces an empty text node).
/// @return Owned text node.
void *rt_xml_text(rt_string content) {
    void *node = xml_node_new(XML_NODE_TEXT);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (content)
        rt_obj_retain_maybe((void *)content);
    n->content = content;
    return node;
}

/// @brief `Xml.Comment(content)` — create a comment node.
///
/// Formats as `<!-- content -->`. The body is emitted verbatim;
/// callers must avoid embedding `--` inside (XML 1.0 forbids it).
///
/// @param content Comment text.
/// @return Owned comment node.
void *rt_xml_comment(rt_string content) {
    void *node = xml_node_new(XML_NODE_COMMENT);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (content)
        rt_obj_retain_maybe((void *)content);
    n->content = content;
    return node;
}

/// @brief `Xml.Cdata(content)` — create a CDATA section node.
///
/// Formats as `<![CDATA[content]]>` with no entity escaping. Useful
/// for embedding markup, code, or other content that would otherwise
/// require heavy escaping.
///
/// @param content Raw CDATA body.
/// @return Owned CDATA node.
void *rt_xml_cdata(rt_string content) {
    void *node = xml_node_new(XML_NODE_CDATA);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (content)
        rt_obj_retain_maybe((void *)content);
    n->content = content;
    return node;
}

//=============================================================================
// Public API - Node Properties
//=============================================================================

/// @brief `XmlNode.NodeType` — return the kind enum (element/text/etc).
///
/// Values match the `rt_xml_node_type_t` enum (0=element, 1=text,
/// 2=comment, 3=cdata, 4=document). Returns 0 for NULL.
int64_t rt_xml_node_type(void *node) {
    if (!node)
        return 0;
    xml_node *n = (xml_node *)node;
    return (int64_t)n->type;
}

/// @brief `XmlNode.Tag` — return the element tag name.
///
/// Returns "" for non-element nodes (text, comment, CDATA, document).
/// The returned string is retained and owned by the caller.
rt_string rt_xml_tag(void *node) {
    if (!node)
        return rt_str_empty();
    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->tag)
        return rt_str_empty();
    rt_obj_retain_maybe((void *)n->tag);
    return n->tag;
}

/// @brief `XmlNode.Content` — return the raw `content` string slot.
///
/// Populated for text, comment, and CDATA nodes; "" for elements and
/// documents (use `TextContent` to recursively gather descendant text).
rt_string rt_xml_content(void *node) {
    if (!node)
        return rt_str_empty();
    xml_node *n = (xml_node *)node;
    if (!n->content)
        return rt_str_empty();
    rt_obj_retain_maybe((void *)n->content);
    return n->content;
}

/// @brief Walk the tree and append every descendant text/CDATA into `sb`.
///
/// Optimization O-04: building one growing builder is O(n) total versus
/// the O(n²) you'd get with naive `result = result + child.text` chains.
/// Recurses through element and document nodes; everything else is a
/// no-op. Borrowed references throughout — children are owned by parents.
static void collect_text_content(void *node, rt_string_builder *sb) {
    if (!node)
        return;

    xml_node *n = (xml_node *)node;

    if (n->type == XML_NODE_TEXT || n->type == XML_NODE_CDATA) {
        if (n->content) {
            const char *cstr = rt_string_cstr(n->content);
            if (cstr)
                rt_sb_append_cstr(sb, cstr);
        }
        return;
    }

    if (n->type != XML_NODE_ELEMENT && n->type != XML_NODE_DOCUMENT)
        return;

    if (!n->children)
        return;

    int64_t count = rt_seq_len(n->children);
    for (int64_t i = 0; i < count; i++) {
        void *child = rt_seq_get(n->children, i);
        collect_text_content(child, sb);
        // rt_seq_get returns a borrowed reference — do not release
    }
}

/// @brief `XmlNode.TextContent` — concatenated text of this node and descendants.
///
/// For text/CDATA nodes returns the content directly. For element /
/// document nodes, recursively concatenates every descendant text +
/// CDATA into a single string via the builder helper. Returns "" for
/// nodes with no textual content.
rt_string rt_xml_text_content(void *node) {
    if (!node)
        return rt_str_empty();

    xml_node *n = (xml_node *)node;

    // For text/cdata nodes, return content directly
    if (n->type == XML_NODE_TEXT || n->type == XML_NODE_CDATA) {
        if (n->content) {
            rt_obj_retain_maybe((void *)n->content);
            return n->content;
        }
        return rt_str_empty();
    }

    // For elements/documents, gather all text content using a builder (O(n))
    if (n->type != XML_NODE_ELEMENT && n->type != XML_NODE_DOCUMENT)
        return rt_str_empty();

    if (!n->children)
        return rt_str_empty();

    rt_string_builder sb;
    rt_sb_init(&sb);
    collect_text_content(node, &sb);
    rt_string result = rt_string_from_bytes(sb.data, sb.len);
    rt_sb_free(&sb);
    return result;
}

//=============================================================================
// Public API - Attributes
//=============================================================================

/// @brief `XmlNode.Attr(name)` — get an attribute value, or "" if missing.
///
/// Case-sensitive (XML attributes are). Returns "" for non-element
/// nodes or unknown attributes; the empty-string return is
/// indistinguishable from an explicitly empty attribute, so use
/// `HasAttr` if you need to disambiguate.
rt_string rt_xml_attr(void *node, rt_string name) {
    if (!node || !name)
        return rt_str_empty();

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return rt_str_empty();

    void *value = rt_map_get(n->attributes, name);
    if (!value)
        return rt_str_empty();

    // value is already retained by rt_map_get
    return (rt_string)value;
}

/// @brief `XmlNode.HasAttr(name)` — test whether an attribute is present.
///
/// Distinguishes "attribute exists with empty value" from "attribute
/// absent". Returns 0 for non-element nodes.
int8_t rt_xml_has_attr(void *node, rt_string name) {
    if (!node || !name)
        return 0;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return 0;

    return rt_map_has(n->attributes, name);
}

/// @brief `XmlNode.SetAttr(name, value)` — set / overwrite an attribute.
///
/// Silently no-ops on non-element nodes or NULL inputs. Existing
/// values for the same name are replaced.
void rt_xml_set_attr(void *node, rt_string name, rt_string value) {
    if (!node || !name)
        return;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return;

    rt_map_set(n->attributes, name, (void *)value);
}

/// @brief `XmlNode.RemoveAttr(name)` — drop an attribute.
///
/// @return 1 if the attribute was present and removed, 0 otherwise.
int8_t rt_xml_remove_attr(void *node, rt_string name) {
    if (!node || !name)
        return 0;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return 0;

    return rt_map_remove(n->attributes, name);
}

/// @brief `XmlNode.AttrNames` — list the attribute names of an element.
///
/// Returns an owned `seq<str>`, always — empty for non-element or
/// attribute-less nodes. Order is the underlying map's iteration
/// order (effectively insertion order).
void *rt_xml_attr_names(void *node) {
    if (!node)
        return rt_seq_new();

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return rt_seq_new();

    return rt_map_keys(n->attributes);
}

//=============================================================================
// Public API - Children
//=============================================================================

/// @brief `XmlNode.Children` — return a snapshot seq of child nodes.
///
/// Returns a *copy* of the internal seq so the caller can iterate or
/// mutate it without affecting the parent. The contained node refs
/// are borrowed (parent retains ownership) — do not release them.
void *rt_xml_children(void *node) {
    if (!node)
        return rt_seq_new();

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return rt_seq_new();

    // Return a copy of the children seq
    void *copy = rt_seq_new();
    int64_t count = rt_seq_len(n->children);
    for (int64_t i = 0; i < count; i++) {
        void *child = rt_seq_get(n->children, i);
        rt_seq_push(copy, child);
        // Borrowed reference — parent owns child, do not release
    }
    return copy;
}

/// @brief `XmlNode.ChildCount` — number of direct children.
int64_t rt_xml_child_count(void *node) {
    if (!node)
        return 0;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return 0;

    return rt_seq_len(n->children);
}

/// @brief `XmlNode.ChildAt(index)` — borrowed reference to the Nth child.
///
/// Returns NULL for negative or out-of-range indices. The reference
/// is borrowed: the parent retains ownership, do not release.
void *rt_xml_child_at(void *node, int64_t index) {
    if (!node || index < 0)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (!n->children || index >= rt_seq_len(n->children))
        return NULL;

    return rt_seq_get(n->children, index);
}

/// @brief `XmlNode.Child(tag)` — first direct-child element with that tag.
///
/// Linear scan over the children seq, returning the first element
/// whose tag matches exactly. Returns NULL if no match. Borrowed
/// reference (do not release).
void *rt_xml_child(void *node, rt_string tag) {
    if (!node || !tag)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return NULL;

    const char *target = rt_string_cstr(tag);
    int64_t count = rt_seq_len(n->children);

    for (int64_t i = 0; i < count; i++) {
        void *child = rt_seq_get(n->children, i);
        xml_node *cn = (xml_node *)child;

        if (cn->type == XML_NODE_ELEMENT && cn->tag) {
            const char *child_tag = rt_string_cstr(cn->tag);
            if (strcmp(child_tag, target) == 0)
                return child; // Borrowed reference — caller must not release
        }
        // Do not release — children are owned by parent node
    }

    return NULL;
}

/// @brief `XmlNode.ChildrenByTag(tag)` — direct children matching `tag`.
///
/// Like `Child(tag)` but returns *all* matches in document order. Only
/// looks one level deep — for recursive search use `FindAll`.
void *rt_xml_children_by_tag(void *node, rt_string tag) {
    void *result = rt_seq_new();
    if (!node || !tag)
        return result;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return result;

    const char *target = rt_string_cstr(tag);
    int64_t count = rt_seq_len(n->children);

    for (int64_t i = 0; i < count; i++) {
        void *child = rt_seq_get(n->children, i);
        xml_node *cn = (xml_node *)child;

        if (cn->type == XML_NODE_ELEMENT && cn->tag) {
            const char *child_tag = rt_string_cstr(cn->tag);
            if (strcmp(child_tag, target) == 0) {
                rt_seq_push(result, child);
            }
        }
        // Do not release — children are owned by parent node
    }

    return result;
}

/// @brief `XmlNode.Append(child)` — add `child` to the end of `node.children`.
///
/// Sets the child's `parent` weak pointer. Ownership of the child
/// transfers to the parent — release tracking flows through the
/// parent's finalizer.
void rt_xml_append(void *node, void *child) {
    if (!node || !child)
        return;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return;

    xml_node *cn = (xml_node *)child;
    cn->parent = n;

    rt_seq_push(n->children, child);
}

/// @brief `XmlNode.Insert(index, child)` — splice `child` at position `index`.
///
/// Indices past the end clamp to "end of list" (effectively `Append`).
/// Negative indices are rejected (silent no-op).
void rt_xml_insert(void *node, int64_t index, void *child) {
    if (!node || !child || index < 0)
        return;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return;

    if (index > rt_seq_len(n->children))
        index = rt_seq_len(n->children);

    xml_node *cn = (xml_node *)child;
    cn->parent = n;

    rt_seq_insert(n->children, index, child);
}

/// @brief `XmlNode.Remove(child)` — detach `child` by identity.
///
/// Returns 0 if `child` isn't found in `node.children`. On success,
/// clears the child's `parent` pointer and releases the parent's
/// reference (the child is freed if there are no other holders).
int8_t rt_xml_remove(void *node, void *child) {
    if (!node || !child)
        return 0;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return 0;

    int64_t idx = rt_seq_find(n->children, child);
    if (idx < 0)
        return 0;

    xml_node *cn = (xml_node *)child;
    cn->parent = NULL;

    void *removed = rt_seq_remove(n->children, idx);
    if (removed) {
        if (rt_obj_release_check0(removed))
            rt_obj_free(removed);
    }
    return 1;
}

/// @brief `XmlNode.RemoveAt(index)` — detach the child at `index`.
///
/// Silent no-op for out-of-range or negative indices. Same ownership
/// transfer as `Remove`: the parent's reference is released after the
/// child has been pulled out of the seq.
void rt_xml_remove_at(void *node, int64_t index) {
    if (!node || index < 0)
        return;

    xml_node *n = (xml_node *)node;
    if (!n->children || index >= rt_seq_len(n->children))
        return;

    void *child = rt_seq_get(n->children, index);
    if (child) {
        xml_node *cn = (xml_node *)child;
        cn->parent = NULL;
        // Don't release here — ownership stays until seq_remove
    }

    void *removed = rt_seq_remove(n->children, index);
    if (removed) {
        // Parent relinquishes ownership: release the child now
        if (rt_obj_release_check0(removed))
            rt_obj_free(removed);
    }
}

/// @brief `XmlElement.SetText(text)` — replace all children with one text node.
///
/// Drains the element's existing children, then (if `text` is non-empty)
/// appends a single text node. Useful for `<tag>some-value</tag>`-style
/// updates where the element should hold only its text. No-op for
/// non-element receivers.
void rt_xml_set_text(void *node, rt_string text) {
    if (!node)
        return;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT)
        return;

    // Clear existing children
    if (n->children) {
        while (rt_seq_len(n->children) > 0) {
            void *removed = rt_seq_remove(n->children, 0);
            if (removed) {
                if (rt_obj_release_check0(removed))
                    rt_obj_free(removed);
            }
        }
    }

    // Add text node
    if (text && rt_str_len(text) > 0) {
        void *text_node = rt_xml_text(text);
        if (text_node) {
            xml_node *tn = (xml_node *)text_node;
            tn->parent = n;
            rt_seq_push(n->children, text_node);
            // Ownership transferred to n; its finalizer will release text_node
        }
    }
}

//=============================================================================
// Public API - Navigation
//=============================================================================

/// @brief `XmlNode.Parent` — retained reference to the containing node, or NULL.
///
/// Returns NULL for detached nodes and the document root. Bumps the
/// parent's refcount before returning so it survives at least until
/// the caller releases it.
void *rt_xml_parent(void *node) {
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (!n->parent)
        return NULL;

    rt_obj_retain_maybe(n->parent);
    return n->parent;
}

/// @brief `XmlDocument.Root` — first element child of the document.
///
/// XML 1.0 mandates a single root element; this helper finds it
/// while skipping any leading processing instructions / comments.
/// Returns NULL if the document is non-document or has no element
/// children. Borrowed reference (do not release).
void *rt_xml_root(void *doc) {
    if (!doc)
        return NULL;

    xml_node *n = (xml_node *)doc;
    if (n->type != XML_NODE_DOCUMENT || !n->children)
        return NULL;

    // Find first element child
    int64_t count = rt_seq_len(n->children);
    for (int64_t i = 0; i < count; i++) {
        void *child = rt_seq_get(n->children, i);
        xml_node *cn = (xml_node *)child;
        if (cn->type == XML_NODE_ELEMENT)
            return child; // Borrowed reference — parent owns it
        // Do not release — children are owned by parent node
    }

    return NULL;
}

/// @brief DFS helper for `FindAll` — push every descendant element matching `tag`.
///
/// Each match is retained before being pushed so the result seq holds
/// strong refs (callers can outlive the source tree). Recursion is
/// pre-order, so results appear in document order.
static void find_all_recursive(void *node, const char *tag, void *result) {
    xml_node *n = (xml_node *)node;

    // Check this node
    if (n->type == XML_NODE_ELEMENT && n->tag) {
        const char *node_tag = rt_string_cstr(n->tag);
        if (strcmp(node_tag, tag) == 0) {
            rt_obj_retain_maybe(node);
            rt_seq_push(result, node);
        }
    }

    // Recurse into children
    if (n->children) {
        int64_t count = rt_seq_len(n->children);
        for (int64_t i = 0; i < count; i++) {
            void *child = rt_seq_get(n->children, i);
            find_all_recursive(child, tag, result);
            // Borrowed reference — parent owns child, do not release
        }
    }
}

/// @brief `XmlNode.FindAll(tag)` — recursive search returning all matches.
///
/// Walks the entire subtree (including the receiver itself) collecting
/// every element whose tag equals `tag`. Returns an owned seq of
/// retained node references — safe to keep after the source tree is
/// dropped.
void *rt_xml_find_all(void *node, rt_string tag) {
    void *result = rt_seq_new();
    if (!node || !tag)
        return result;

    const char *target = rt_string_cstr(tag);
    find_all_recursive(node, target, result);
    return result;
}

/// @brief DFS helper for `Find` — return first descendant element matching `tag`.
///
/// Pre-order traversal, returns the first hit. Retains the returned
/// node before propagating up the call stack so the caller owns it.
static void *find_first_recursive(void *node, const char *tag) {
    xml_node *n = (xml_node *)node;

    // Check this node
    if (n->type == XML_NODE_ELEMENT && n->tag) {
        const char *node_tag = rt_string_cstr(n->tag);
        if (strcmp(node_tag, tag) == 0) {
            rt_obj_retain_maybe(node);
            return node;
        }
    }

    // Recurse into children
    if (n->children) {
        int64_t count = rt_seq_len(n->children);
        for (int64_t i = 0; i < count; i++) {
            void *child = rt_seq_get(n->children, i);
            void *found = find_first_recursive(child, tag);
            // Borrowed reference — parent owns child, do not release
            if (found)
                return found;
        }
    }

    return NULL;
}

/// @brief `XmlNode.Find(tag)` — first descendant element matching `tag`.
///
/// DFS pre-order; returns the receiver itself if it matches. Returns
/// NULL when no match is found. The returned node is retained.
void *rt_xml_find(void *node, rt_string tag) {
    if (!node || !tag)
        return NULL;

    const char *target = rt_string_cstr(tag);
    return find_first_recursive(node, target);
}

//=============================================================================
// Public API - Formatting
//=============================================================================

static void format_node(void *node, int indent, int level, char **buf, size_t *cap, size_t *len);

// Format-side scratch helpers — a tiny growable c-string used during
// `Format` / `FormatPretty`. `*cap` doubles on overflow; OOM traps.

/// @brief Append the c-string `str` to the growing format buffer.
static void buf_append(char **buf, size_t *cap, size_t *len, const char *str) {
    size_t slen = strlen(str);
    while (*len + slen + 1 > *cap) {
        size_t new_cap = (*cap == 0) ? 256 : (*cap * 2);
        char *tmp = (char *)realloc(*buf, new_cap);
        if (!tmp)
            rt_trap("XML format: memory allocation failed");
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, str, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

/// @brief Append a single character to the growing format buffer.
static void buf_append_char(char **buf, size_t *cap, size_t *len, char c) {
    if (*len + 2 > *cap) {
        size_t new_cap = (*cap == 0) ? 256 : (*cap * 2);
        char *tmp = (char *)realloc(*buf, new_cap);
        if (!tmp)
            rt_trap("XML format: memory allocation failed");
        *buf = tmp;
        *cap = new_cap;
    }
    (*buf)[*len] = c;
    (*len)++;
    (*buf)[*len] = '\0';
}

/// @brief Append `spaces` blanks to the buffer (used for pretty-printing).
static void buf_append_indent(char **buf, size_t *cap, size_t *len, int spaces) {
    for (int i = 0; i < spaces; i++)
        buf_append_char(buf, cap, len, ' ');
}

/// @brief Append `str` with XML special-character escaping.
///
/// Always escapes `&`, `<`, `>`. Quotes (`"` and `'`) are escaped only
/// when `for_attr` is set — text content can carry literal quotes
/// freely. This matches the recommendations of XML 1.0 §2.4.
static void buf_append_escaped(
    char **buf, size_t *cap, size_t *len, const char *str, int for_attr) {
    for (size_t i = 0; str[i]; i++) {
        switch (str[i]) {
            case '&':
                buf_append(buf, cap, len, "&amp;");
                break;
            case '<':
                buf_append(buf, cap, len, "&lt;");
                break;
            case '>':
                buf_append(buf, cap, len, "&gt;");
                break;
            case '"':
                if (for_attr)
                    buf_append(buf, cap, len, "&quot;");
                else
                    buf_append_char(buf, cap, len, '"');
                break;
            case '\'':
                if (for_attr)
                    buf_append(buf, cap, len, "&apos;");
                else
                    buf_append_char(buf, cap, len, '\'');
                break;
            default:
                buf_append_char(buf, cap, len, str[i]);
                break;
        }
    }
}

/// @brief Emit a single element (opening tag, attrs, children, closing tag).
///
/// Self-closes (`<tag/>`) when there are no children. Switches between
/// inline and indented output: an element with only text/CDATA children
/// is emitted on one line; mixed/element children get newlines and
/// indentation when `indent > 0`. `level` is the current depth (used
/// to compute the leading indent).
static void format_element(
    xml_node *elem, int indent, int level, char **buf, size_t *cap, size_t *len) {
    // Indentation
    if (indent > 0 && level > 0)
        buf_append_indent(buf, cap, len, indent * level);

    // Opening tag
    buf_append_char(buf, cap, len, '<');
    buf_append(buf, cap, len, rt_string_cstr(elem->tag));

    // Attributes
    if (elem->attributes) {
        void *keys = rt_map_keys(elem->attributes);
        int64_t nkeys = rt_seq_len(keys);
        for (int64_t i = 0; i < nkeys; i++) {
            void *key = rt_seq_get(keys, i);
            void *val = rt_map_get(elem->attributes, (rt_string)key);

            buf_append_char(buf, cap, len, ' ');
            buf_append(buf, cap, len, rt_string_cstr((rt_string)key));
            buf_append(buf, cap, len, "=\"");
            buf_append_escaped(buf, cap, len, rt_string_cstr((rt_string)val), 1);
            buf_append_char(buf, cap, len, '"');
        }
        // Release the keys Seq (its owns_elements finalizer releases the strings)
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }

    // Check for children
    int64_t nchildren = elem->children ? rt_seq_len(elem->children) : 0;
    if (nchildren == 0) {
        buf_append(buf, cap, len, "/>");
        if (indent > 0)
            buf_append_char(buf, cap, len, '\n');
        return;
    }

    buf_append_char(buf, cap, len, '>');

    // Check if only text content
    bool text_only = true;
    for (int64_t i = 0; i < nchildren; i++) {
        void *child = rt_seq_get(elem->children, i);
        xml_node *cn = (xml_node *)child;
        if (cn->type != XML_NODE_TEXT && cn->type != XML_NODE_CDATA)
            text_only = false;
        // Borrowed reference — parent owns child, do not release
        if (!text_only)
            break;
    }

    if (!text_only && indent > 0)
        buf_append_char(buf, cap, len, '\n');

    // Children
    for (int64_t i = 0; i < nchildren; i++) {
        void *child = rt_seq_get(elem->children, i);
        format_node(child, text_only ? 0 : indent, level + 1, buf, cap, len);
        // Borrowed reference — parent owns child, do not release
    }

    // Closing tag
    if (!text_only && indent > 0 && level > 0)
        buf_append_indent(buf, cap, len, indent * level);
    buf_append(buf, cap, len, "</");
    buf_append(buf, cap, len, rt_string_cstr(elem->tag));
    buf_append_char(buf, cap, len, '>');
    if (indent > 0)
        buf_append_char(buf, cap, len, '\n');
}

/// @brief Dispatch formatter — emits any node by kind.
///
/// Element nodes go to `format_element`; text is escaped; comments
/// and CDATA are emitted with their delimiters; documents iterate
/// their children at level 0.
static void format_node(void *node, int indent, int level, char **buf, size_t *cap, size_t *len) {
    xml_node *n = (xml_node *)node;

    switch (n->type) {
        case XML_NODE_ELEMENT:
            format_element(n, indent, level, buf, cap, len);
            break;

        case XML_NODE_TEXT:
            if (n->content)
                buf_append_escaped(buf, cap, len, rt_string_cstr(n->content), 0);
            break;

        case XML_NODE_COMMENT:
            if (indent > 0 && level > 0)
                buf_append_indent(buf, cap, len, indent * level);
            buf_append(buf, cap, len, "<!--");
            if (n->content)
                buf_append(buf, cap, len, rt_string_cstr(n->content));
            buf_append(buf, cap, len, "-->");
            if (indent > 0)
                buf_append_char(buf, cap, len, '\n');
            break;

        case XML_NODE_CDATA:
            buf_append(buf, cap, len, "<![CDATA[");
            if (n->content)
                buf_append(buf, cap, len, rt_string_cstr(n->content));
            buf_append(buf, cap, len, "]]>");
            break;

        case XML_NODE_DOCUMENT:
            if (n->children) {
                int64_t count = rt_seq_len(n->children);
                for (int64_t i = 0; i < count; i++) {
                    void *child = rt_seq_get(n->children, i);
                    format_node(child, indent, 0, buf, cap, len);
                    // Borrowed reference — parent owns child, do not release
                }
            }
            break;
    }
}

/// @brief `Xml.Format(node)` — serialize a node tree to compact XML text.
///
/// No added whitespace, no XML declaration. For human-readable output
/// use `FormatPretty`. Returns "" for NULL.
///
/// @param node Node to serialize.
/// @return Owned `rt_string` containing the XML.
rt_string rt_xml_format(void *node) {
    if (!node)
        return rt_str_empty();

    char *buf = NULL;
    size_t cap = 0, len = 0;

    format_node(node, 0, 0, &buf, &cap, &len);

    rt_string result = rt_string_from_bytes(buf ? buf : "", (int64_t)len);
    free(buf);
    return result;
}

/// @brief `Xml.FormatPretty(node, indent)` — serialize with indentation.
///
/// `indent` is the number of spaces per nesting level (clamped to
/// 0..8). A trailing newline is trimmed for consistency with most
/// pretty-printers. Mixed-content elements (interleaved text and
/// element children) are kept inline since reflowing them can change
/// semantics — see `format_element` for details.
///
/// @param node   Node to serialize.
/// @param indent Spaces per indent level (0..8).
/// @return Owned `rt_string` containing the indented XML.
rt_string rt_xml_format_pretty(void *node, int64_t indent) {
    if (!node)
        return rt_str_empty();

    if (indent < 0)
        indent = 0;
    if (indent > 8)
        indent = 8;

    char *buf = NULL;
    size_t cap = 0, len = 0;

    format_node(node, (int)indent, 0, &buf, &cap, &len);

    // Remove trailing newline for consistency
    if (len > 0 && buf[len - 1] == '\n')
        len--;

    rt_string result = rt_string_from_bytes(buf ? buf : "", (int64_t)len);
    free(buf);
    return result;
}

//=============================================================================
// Public API - Utility
//=============================================================================

/// @brief `Xml.Escape(text)` — apply XML text-content escaping.
///
/// Escapes `&`, `<`, `>`. Quotes are *not* escaped (they're safe in
/// text content); use this on attribute values only when the caller
/// is also wrapping in quotes manually — `Xml.Format` handles the
/// quote escaping for attributes automatically.
rt_string rt_xml_escape(rt_string text) {
    if (!text)
        return rt_str_empty();

    const char *src = rt_string_cstr(text);
    char *buf = NULL;
    size_t cap = 0, len = 0;

    buf_append_escaped(&buf, &cap, &len, src, 0);

    rt_string result = rt_string_from_bytes(buf ? buf : "", (int64_t)len);
    free(buf);
    return result;
}

/// @brief `Xml.Unescape(text)` — decode XML entity references back to characters.
///
/// Inverse of `Escape`. Recognises numeric (`&#NN;` / `&#xHH;`) and the
/// five named XML entities. Unknown entities are left in place
/// (consistent with `decode_entity`'s 0-return policy).
rt_string rt_xml_unescape(rt_string text) {
    if (!text)
        return rt_str_empty();

    const char *src = rt_string_cstr(text);
    size_t src_len = strlen(src);

    char *buf = malloc(src_len + 1);
    if (!buf)
        return rt_str_empty();

    size_t out = 0;
    for (size_t i = 0; i < src_len;) {
        if (src[i] == '&') {
            char decoded[4];
            size_t consumed;
            int n = decode_entity(src + i, src_len - i, decoded, &consumed);
            if (n > 0) {
                memcpy(buf + out, decoded, n);
                out += n;
                i += consumed;
                continue;
            }
        }
        buf[out++] = src[i++];
    }
    buf[out] = '\0';

    rt_string result = rt_string_from_bytes(buf, (int64_t)out);
    free(buf);
    return result;
}
