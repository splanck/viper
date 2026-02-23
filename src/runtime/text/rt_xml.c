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
typedef struct xml_node
{
    XmlNodeType type;        ///< Node type
    rt_string tag;           ///< Tag name (elements only)
    rt_string content;       ///< Text content (text/comment/cdata)
    void *attributes;        ///< Map of attributes (elements only)
    void *children;          ///< Seq of child nodes
    struct xml_node *parent; ///< Parent node (weak reference)
} xml_node;

/// @brief Last parse error message (thread-local would be better).
static char xml_last_error[256] = {0};

//=============================================================================
// Forward Declarations
//=============================================================================

static void xml_node_finalizer(void *obj);
static void *xml_node_new(XmlNodeType type);
static void *parse_document(const char *input, size_t len);

//=============================================================================
// Helper Functions
//=============================================================================

extern void rt_trap(const char *msg);

static void set_error(const char *msg)
{
    strncpy(xml_last_error, msg, sizeof(xml_last_error) - 1);
    xml_last_error[sizeof(xml_last_error) - 1] = '\0';
}

static void clear_error(void)
{
    xml_last_error[0] = '\0';
}

//=============================================================================
// Node Management
//=============================================================================

static void xml_node_finalizer(void *obj)
{
    xml_node *node = (xml_node *)obj;
    if (!node)
        return;

    // Release tag
    if (node->tag)
    {
        if (rt_obj_release_check0((void *)node->tag))
            rt_obj_free((void *)node->tag);
    }

    // Release content
    if (node->content)
    {
        if (rt_obj_release_check0((void *)node->content))
            rt_obj_free((void *)node->content);
    }

    // Release attributes map
    if (node->attributes)
    {
        if (rt_obj_release_check0(node->attributes))
            rt_obj_free(node->attributes);
    }

    // Release children (parent owns each child — finalizer must release them)
    if (node->children)
    {
        int64_t count = rt_seq_len(node->children);
        for (int64_t i = 0; i < count; i++)
        {
            void *child = rt_seq_get(node->children, i);
            if (child)
            {
                if (rt_obj_release_check0(child))
                    rt_obj_free(child);
            }
        }
        if (rt_obj_release_check0(node->children))
            rt_obj_free(node->children);
    }

    // Note: parent is a weak reference, don't release
}

static void *xml_node_new(XmlNodeType type)
{
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
    if (type == XML_NODE_ELEMENT || type == XML_NODE_DOCUMENT)
    {
        node->children = rt_seq_new();
        if (!node->children)
        {
            rt_obj_free(node);
            return NULL;
        }
    }

    // Create attributes map for elements
    if (type == XML_NODE_ELEMENT)
    {
        node->attributes = rt_map_new();
        if (!node->attributes)
        {
            if (node->children)
            {
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

typedef struct
{
    const char *input;
    size_t len;
    size_t pos;
    int line;
    int col;
    int depth; // Current element nesting depth
} xml_parser;

static void parser_init(xml_parser *p, const char *input, size_t len)
{
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->line = 1;
    p->col = 1;
    p->depth = 0;
}

static bool parser_eof(xml_parser *p)
{
    return p->pos >= p->len;
}

static char parser_peek(xml_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

static char parser_advance(xml_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    char c = p->input[p->pos++];
    if (c == '\n')
    {
        p->line++;
        p->col = 1;
    }
    else
    {
        p->col++;
    }
    return c;
}

static void parser_skip_ws(xml_parser *p)
{
    while (!parser_eof(p) && isspace((unsigned char)parser_peek(p)))
        parser_advance(p);
}

static bool parser_match(xml_parser *p, const char *str)
{
    size_t len = strlen(str);
    if (p->pos + len > p->len)
        return false;
    if (strncmp(p->input + p->pos, str, len) != 0)
        return false;
    for (size_t i = 0; i < len; i++)
        parser_advance(p);
    return true;
}

static bool parser_lookahead(xml_parser *p, const char *str)
{
    size_t len = strlen(str);
    if (p->pos + len > p->len)
        return false;
    return strncmp(p->input + p->pos, str, len) == 0;
}

//=============================================================================
// Parsing Helpers
//=============================================================================

static bool is_name_start_char(char c)
{
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

static bool is_name_char(char c)
{
    return isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' || c == '.';
}

/// @brief Parse an XML name (tag name, attribute name).
static rt_string parse_name(xml_parser *p)
{
    size_t start = p->pos;

    if (parser_eof(p) || !is_name_start_char(parser_peek(p)))
        return NULL;

    while (!parser_eof(p) && is_name_char(parser_peek(p)))
        parser_advance(p);

    size_t len = p->pos - start;
    return rt_string_from_bytes(p->input + start, (int64_t)len);
}

/// @brief Decode a single character reference or entity.
static int decode_entity(const char *str, size_t len, char *out, size_t *consumed)
{
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
    if (str[1] == '#')
    {
        unsigned int codepoint = 0;
        if (len > 2 && str[2] == 'x')
        {
            // Hex
            for (size_t i = 3; i < end; i++)
            {
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
        }
        else
        {
            // Decimal
            for (size_t i = 2; i < end; i++)
            {
                char c = str[i];
                if (c >= '0' && c <= '9')
                    codepoint = codepoint * 10 + (c - '0');
                else
                    return 0;
            }
        }

        // Encode as UTF-8
        if (codepoint < 0x80)
        {
            out[0] = (char)codepoint;
            return 1;
        }
        else if (codepoint < 0x800)
        {
            out[0] = (char)(0xC0 | (codepoint >> 6));
            out[1] = (char)(0x80 | (codepoint & 0x3F));
            return 2;
        }
        else if (codepoint < 0x10000)
        {
            out[0] = (char)(0xE0 | (codepoint >> 12));
            out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            out[2] = (char)(0x80 | (codepoint & 0x3F));
            return 3;
        }
        else if (codepoint < 0x110000)
        {
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
    if (name_len == 2 && strncmp(str + 1, "lt", 2) == 0)
    {
        out[0] = '<';
        return 1;
    }
    if (name_len == 2 && strncmp(str + 1, "gt", 2) == 0)
    {
        out[0] = '>';
        return 1;
    }
    if (name_len == 3 && strncmp(str + 1, "amp", 3) == 0)
    {
        out[0] = '&';
        return 1;
    }
    if (name_len == 4 && strncmp(str + 1, "quot", 4) == 0)
    {
        out[0] = '"';
        return 1;
    }
    if (name_len == 4 && strncmp(str + 1, "apos", 4) == 0)
    {
        out[0] = '\'';
        return 1;
    }

    return 0;
}

/// @brief Parse attribute value (quoted string with entity decoding).
static rt_string parse_attr_value(xml_parser *p)
{
    char quote = parser_peek(p);
    if (quote != '"' && quote != '\'')
        return NULL;
    parser_advance(p);

    // First pass: calculate decoded length
    size_t start = p->pos;
    size_t decoded_len = 0;
    while (!parser_eof(p) && parser_peek(p) != quote)
    {
        if (parser_peek(p) == '&')
        {
            char buf[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, buf, &consumed);
            if (n > 0)
            {
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
    while (!parser_eof(p) && parser_peek(p) != quote)
    {
        if (parser_peek(p) == '&')
        {
            char decoded[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, decoded, &consumed);
            if (n > 0)
            {
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

/// @brief Parse text content with entity decoding.
static rt_string parse_text_content(xml_parser *p)
{
    size_t start = p->pos;

    // First pass: find end and calculate decoded length
    size_t decoded_len = 0;
    while (!parser_eof(p) && parser_peek(p) != '<')
    {
        if (parser_peek(p) == '&')
        {
            char buf[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, buf, &consumed);
            if (n > 0)
            {
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
    while (!parser_eof(p) && parser_peek(p) != '<')
    {
        if (parser_peek(p) == '&')
        {
            char decoded[4];
            size_t consumed;
            int n = decode_entity(p->input + p->pos, p->len - p->pos, decoded, &consumed);
            if (n > 0)
            {
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

/// @brief Parse a comment: <!-- ... -->
static void *parse_comment(xml_parser *p)
{
    if (!parser_match(p, "<!--"))
        return NULL;

    size_t start = p->pos;
    while (!parser_eof(p) && !parser_lookahead(p, "-->"))
        parser_advance(p);

    size_t len = p->pos - start;

    if (!parser_match(p, "-->"))
    {
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

/// @brief Parse a CDATA section: <![CDATA[ ... ]]>
static void *parse_cdata(xml_parser *p)
{
    if (!parser_match(p, "<![CDATA["))
        return NULL;

    size_t start = p->pos;
    while (!parser_eof(p) && !parser_lookahead(p, "]]>"))
        parser_advance(p);

    size_t len = p->pos - start;

    if (!parser_match(p, "]]>"))
    {
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

/// @brief Parse processing instruction: <?target ... ?>
static bool skip_processing_instruction(xml_parser *p)
{
    if (!parser_match(p, "<?"))
        return false;

    while (!parser_eof(p) && !parser_lookahead(p, "?>"))
        parser_advance(p);

    if (!parser_match(p, "?>"))
    {
        set_error("Unterminated processing instruction");
        return false;
    }

    return true;
}

/// @brief Parse DOCTYPE declaration (skip it)
static bool skip_doctype(xml_parser *p)
{
    if (!parser_lookahead(p, "<!DOCTYPE"))
        return false;

    parser_match(p, "<!DOCTYPE");

    int depth = 1;
    while (!parser_eof(p) && depth > 0)
    {
        char c = parser_peek(p);
        if (c == '<')
            depth++;
        else if (c == '>')
            depth--;
        parser_advance(p);
    }

    return true;
}

/// @brief Parse an element: <tag attr="value">...</tag>
static void *parse_element(xml_parser *p)
{
    /* S-17: Reject excessively nested documents */
    if (p->depth >= XML_MAX_DEPTH)
    {
        set_error("element nesting depth limit exceeded");
        return NULL;
    }
    p->depth++;

    if (!parser_match(p, "<"))
    {
        p->depth--;
        return NULL;
    }

    // Parse tag name
    rt_string tag = parse_name(p);
    if (!tag)
    {
        p->depth--;
        set_error("Expected element name");
        return NULL;
    }

    void *node = xml_node_new(XML_NODE_ELEMENT);
    if (!node)
    {
        p->depth--;
        if (rt_obj_release_check0((void *)tag))
            rt_obj_free((void *)tag);
        return NULL;
    }

    xml_node *elem = (xml_node *)node;
    elem->tag = tag;

    // Parse attributes
    for (;;)
    {
        parser_skip_ws(p);

        // Check for end of opening tag
        if (parser_lookahead(p, "/>"))
        {
            parser_match(p, "/>");
            p->depth--;
            return node; // Self-closing
        }
        if (parser_lookahead(p, ">"))
        {
            parser_match(p, ">");
            break; // Continue to content
        }

        // Parse attribute
        rt_string attr_name = parse_name(p);
        if (!attr_name)
        {
            p->depth--;
            set_error("Expected attribute name or tag end");
            if (rt_obj_release_check0(node))
                rt_obj_free(node);
            return NULL;
        }

        parser_skip_ws(p);
        if (!parser_match(p, "="))
        {
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
        if (!attr_value)
        {
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
    while (!parser_eof(p))
    {
        // Check for end tag
        if (parser_lookahead(p, "</"))
        {
            parser_match(p, "</");
            rt_string end_tag = parse_name(p);
            parser_skip_ws(p);
            parser_match(p, ">");

            // Verify tag match
            const char *start_tag_str = rt_string_cstr(elem->tag);
            const char *end_tag_str = rt_string_cstr(end_tag);
            if (strcmp(start_tag_str, end_tag_str) != 0)
            {
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
        if (child)
        {
            xml_node *child_node = (xml_node *)child;
            child_node->parent = elem;
            rt_seq_push(elem->children, child);
            // Ownership transferred to elem; its finalizer will release child
        }
        else if (xml_last_error[0] != '\0')
        {
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

/// @brief Parse any node type.
static void *parse_node(xml_parser *p)
{
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
    if (parser_lookahead(p, "<?"))
    {
        skip_processing_instruction(p);
        return parse_node(p);
    }

    // DOCTYPE (skip)
    if (parser_lookahead(p, "<!DOCTYPE"))
    {
        skip_doctype(p);
        return parse_node(p);
    }

    // Element
    if (parser_lookahead(p, "<") && !parser_lookahead(p, "</"))
        return parse_element(p);

    // Text content
    rt_string text = parse_text_content(p);
    if (text)
    {
        // Skip whitespace-only text nodes
        const char *s = rt_string_cstr(text);
        bool all_ws = true;
        for (size_t i = 0; s[i]; i++)
        {
            if (!isspace((unsigned char)s[i]))
            {
                all_ws = false;
                break;
            }
        }

        if (all_ws)
        {
            if (rt_obj_release_check0((void *)text))
                rt_obj_free((void *)text);
            return NULL;
        }

        void *node = xml_node_new(XML_NODE_TEXT);
        if (!node)
        {
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

/// @brief Parse complete document.
static void *parse_document(const char *input, size_t len)
{
    clear_error();

    xml_parser p;
    parser_init(&p, input, len);

    void *doc = xml_node_new(XML_NODE_DOCUMENT);
    if (!doc)
        return NULL;

    xml_node *doc_node = (xml_node *)doc;

    // Parse all root-level nodes
    while (!parser_eof(&p))
    {
        parser_skip_ws(&p);
        if (parser_eof(&p))
            break;

        void *node = parse_node(&p);
        if (node)
        {
            xml_node *n = (xml_node *)node;
            n->parent = doc_node;
            rt_seq_push(doc_node->children, node);
            // Ownership transferred to doc; its finalizer will release node
        }
        else if (xml_last_error[0] != '\0')
        {
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

void *rt_xml_parse(rt_string text)
{
    if (!text || rt_str_len(text) == 0)
    {
        set_error("Empty XML input");
        return NULL;
    }

    const char *cstr = rt_string_cstr(text);
    int64_t len = rt_str_len(text);

    return parse_document(cstr, (size_t)len);
}

rt_string rt_xml_error(void)
{
    return rt_string_from_bytes(xml_last_error, (int64_t)strlen(xml_last_error));
}

int8_t rt_xml_is_valid(rt_string text)
{
    void *doc = rt_xml_parse(text);
    if (doc)
    {
        if (rt_obj_release_check0(doc))
            rt_obj_free(doc);
        return 1;
    }
    return 0;
}

//=============================================================================
// Public API - Node Creation
//=============================================================================

void *rt_xml_element(rt_string tag)
{
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

void *rt_xml_text(rt_string content)
{
    void *node = xml_node_new(XML_NODE_TEXT);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (content)
        rt_obj_retain_maybe((void *)content);
    n->content = content;
    return node;
}

void *rt_xml_comment(rt_string content)
{
    void *node = xml_node_new(XML_NODE_COMMENT);
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (content)
        rt_obj_retain_maybe((void *)content);
    n->content = content;
    return node;
}

void *rt_xml_cdata(rt_string content)
{
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

int64_t rt_xml_node_type(void *node)
{
    if (!node)
        return 0;
    xml_node *n = (xml_node *)node;
    return (int64_t)n->type;
}

rt_string rt_xml_tag(void *node)
{
    if (!node)
        return rt_str_empty();
    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->tag)
        return rt_str_empty();
    rt_obj_retain_maybe((void *)n->tag);
    return n->tag;
}

rt_string rt_xml_content(void *node)
{
    if (!node)
        return rt_str_empty();
    xml_node *n = (xml_node *)node;
    if (!n->content)
        return rt_str_empty();
    rt_obj_retain_maybe((void *)n->content);
    return n->content;
}

/* O-04: Helper that appends all text content to a builder, avoiding O(n²) concat */
static void collect_text_content(void *node, rt_string_builder *sb)
{
    if (!node)
        return;

    xml_node *n = (xml_node *)node;

    if (n->type == XML_NODE_TEXT || n->type == XML_NODE_CDATA)
    {
        if (n->content)
        {
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
    for (int64_t i = 0; i < count; i++)
    {
        void *child = rt_seq_get(n->children, i);
        collect_text_content(child, sb);
        // rt_seq_get returns a borrowed reference — do not release
    }
}

rt_string rt_xml_text_content(void *node)
{
    if (!node)
        return rt_str_empty();

    xml_node *n = (xml_node *)node;

    // For text/cdata nodes, return content directly
    if (n->type == XML_NODE_TEXT || n->type == XML_NODE_CDATA)
    {
        if (n->content)
        {
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

rt_string rt_xml_attr(void *node, rt_string name)
{
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

int8_t rt_xml_has_attr(void *node, rt_string name)
{
    if (!node || !name)
        return 0;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return 0;

    return rt_map_has(n->attributes, name);
}

void rt_xml_set_attr(void *node, rt_string name, rt_string value)
{
    if (!node || !name)
        return;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return;

    rt_map_set(n->attributes, name, (void *)value);
}

int8_t rt_xml_remove_attr(void *node, rt_string name)
{
    if (!node || !name)
        return 0;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT || !n->attributes)
        return 0;

    return rt_map_remove(n->attributes, name);
}

void *rt_xml_attr_names(void *node)
{
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

void *rt_xml_children(void *node)
{
    if (!node)
        return rt_seq_new();

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return rt_seq_new();

    // Return a copy of the children seq
    void *copy = rt_seq_new();
    int64_t count = rt_seq_len(n->children);
    for (int64_t i = 0; i < count; i++)
    {
        void *child = rt_seq_get(n->children, i);
        rt_seq_push(copy, child);
        // Borrowed reference — parent owns child, do not release
    }
    return copy;
}

int64_t rt_xml_child_count(void *node)
{
    if (!node)
        return 0;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return 0;

    return rt_seq_len(n->children);
}

void *rt_xml_child_at(void *node, int64_t index)
{
    if (!node || index < 0)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (!n->children || index >= rt_seq_len(n->children))
        return NULL;

    return rt_seq_get(n->children, index);
}

void *rt_xml_child(void *node, rt_string tag)
{
    if (!node || !tag)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return NULL;

    const char *target = rt_string_cstr(tag);
    int64_t count = rt_seq_len(n->children);

    for (int64_t i = 0; i < count; i++)
    {
        void *child = rt_seq_get(n->children, i);
        xml_node *cn = (xml_node *)child;

        if (cn->type == XML_NODE_ELEMENT && cn->tag)
        {
            const char *child_tag = rt_string_cstr(cn->tag);
            if (strcmp(child_tag, target) == 0)
                return child; // Borrowed reference — caller must not release
        }
        // Do not release — children are owned by parent node
    }

    return NULL;
}

void *rt_xml_children_by_tag(void *node, rt_string tag)
{
    void *result = rt_seq_new();
    if (!node || !tag)
        return result;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return result;

    const char *target = rt_string_cstr(tag);
    int64_t count = rt_seq_len(n->children);

    for (int64_t i = 0; i < count; i++)
    {
        void *child = rt_seq_get(n->children, i);
        xml_node *cn = (xml_node *)child;

        if (cn->type == XML_NODE_ELEMENT && cn->tag)
        {
            const char *child_tag = rt_string_cstr(cn->tag);
            if (strcmp(child_tag, target) == 0)
            {
                rt_seq_push(result, child);
            }
        }
        // Do not release — children are owned by parent node
    }

    return result;
}

void rt_xml_append(void *node, void *child)
{
    if (!node || !child)
        return;

    xml_node *n = (xml_node *)node;
    if (!n->children)
        return;

    xml_node *cn = (xml_node *)child;
    cn->parent = n;

    rt_seq_push(n->children, child);
}

void rt_xml_insert(void *node, int64_t index, void *child)
{
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

int8_t rt_xml_remove(void *node, void *child)
{
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
    if (removed)
    {
        if (rt_obj_release_check0(removed))
            rt_obj_free(removed);
    }
    return 1;
}

void rt_xml_remove_at(void *node, int64_t index)
{
    if (!node || index < 0)
        return;

    xml_node *n = (xml_node *)node;
    if (!n->children || index >= rt_seq_len(n->children))
        return;

    void *child = rt_seq_get(n->children, index);
    if (child)
    {
        xml_node *cn = (xml_node *)child;
        cn->parent = NULL;
        // Don't release here — ownership stays until seq_remove
    }

    void *removed = rt_seq_remove(n->children, index);
    if (removed)
    {
        // Parent relinquishes ownership: release the child now
        if (rt_obj_release_check0(removed))
            rt_obj_free(removed);
    }
}

void rt_xml_set_text(void *node, rt_string text)
{
    if (!node)
        return;

    xml_node *n = (xml_node *)node;
    if (n->type != XML_NODE_ELEMENT)
        return;

    // Clear existing children
    if (n->children)
    {
        while (rt_seq_len(n->children) > 0)
        {
            void *removed = rt_seq_remove(n->children, 0);
            if (removed)
            {
                if (rt_obj_release_check0(removed))
                    rt_obj_free(removed);
            }
        }
    }

    // Add text node
    if (text && rt_str_len(text) > 0)
    {
        void *text_node = rt_xml_text(text);
        if (text_node)
        {
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

void *rt_xml_parent(void *node)
{
    if (!node)
        return NULL;

    xml_node *n = (xml_node *)node;
    if (!n->parent)
        return NULL;

    rt_obj_retain_maybe(n->parent);
    return n->parent;
}

void *rt_xml_root(void *doc)
{
    if (!doc)
        return NULL;

    xml_node *n = (xml_node *)doc;
    if (n->type != XML_NODE_DOCUMENT || !n->children)
        return NULL;

    // Find first element child
    int64_t count = rt_seq_len(n->children);
    for (int64_t i = 0; i < count; i++)
    {
        void *child = rt_seq_get(n->children, i);
        xml_node *cn = (xml_node *)child;
        if (cn->type == XML_NODE_ELEMENT)
            return child; // Borrowed reference — parent owns it
        // Do not release — children are owned by parent node
    }

    return NULL;
}

static void find_all_recursive(void *node, const char *tag, void *result)
{
    xml_node *n = (xml_node *)node;

    // Check this node
    if (n->type == XML_NODE_ELEMENT && n->tag)
    {
        const char *node_tag = rt_string_cstr(n->tag);
        if (strcmp(node_tag, tag) == 0)
        {
            rt_obj_retain_maybe(node);
            rt_seq_push(result, node);
        }
    }

    // Recurse into children
    if (n->children)
    {
        int64_t count = rt_seq_len(n->children);
        for (int64_t i = 0; i < count; i++)
        {
            void *child = rt_seq_get(n->children, i);
            find_all_recursive(child, tag, result);
            // Borrowed reference — parent owns child, do not release
        }
    }
}

void *rt_xml_find_all(void *node, rt_string tag)
{
    void *result = rt_seq_new();
    if (!node || !tag)
        return result;

    const char *target = rt_string_cstr(tag);
    find_all_recursive(node, target, result);
    return result;
}

static void *find_first_recursive(void *node, const char *tag)
{
    xml_node *n = (xml_node *)node;

    // Check this node
    if (n->type == XML_NODE_ELEMENT && n->tag)
    {
        const char *node_tag = rt_string_cstr(n->tag);
        if (strcmp(node_tag, tag) == 0)
        {
            rt_obj_retain_maybe(node);
            return node;
        }
    }

    // Recurse into children
    if (n->children)
    {
        int64_t count = rt_seq_len(n->children);
        for (int64_t i = 0; i < count; i++)
        {
            void *child = rt_seq_get(n->children, i);
            void *found = find_first_recursive(child, tag);
            // Borrowed reference — parent owns child, do not release
            if (found)
                return found;
        }
    }

    return NULL;
}

void *rt_xml_find(void *node, rt_string tag)
{
    if (!node || !tag)
        return NULL;

    const char *target = rt_string_cstr(tag);
    return find_first_recursive(node, target);
}

//=============================================================================
// Public API - Formatting
//=============================================================================

static void format_node(void *node, int indent, int level, char **buf, size_t *cap, size_t *len);

static void buf_append(char **buf, size_t *cap, size_t *len, const char *str)
{
    size_t slen = strlen(str);
    while (*len + slen + 1 > *cap)
    {
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

static void buf_append_n(char **buf, size_t *cap, size_t *len, const char *str, size_t n)
{
    while (*len + n + 1 > *cap)
    {
        size_t new_cap = (*cap == 0) ? 256 : (*cap * 2);
        char *tmp = (char *)realloc(*buf, new_cap);
        if (!tmp)
            rt_trap("XML format: memory allocation failed");
        *buf = tmp;
        *cap = new_cap;
    }
    memcpy(*buf + *len, str, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static void buf_append_char(char **buf, size_t *cap, size_t *len, char c)
{
    if (*len + 2 > *cap)
    {
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

static void buf_append_indent(char **buf, size_t *cap, size_t *len, int spaces)
{
    for (int i = 0; i < spaces; i++)
        buf_append_char(buf, cap, len, ' ');
}

static void buf_append_escaped(char **buf, size_t *cap, size_t *len, const char *str, int for_attr)
{
    for (size_t i = 0; str[i]; i++)
    {
        switch (str[i])
        {
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

static void format_element(
    xml_node *elem, int indent, int level, char **buf, size_t *cap, size_t *len)
{
    // Indentation
    if (indent > 0 && level > 0)
        buf_append_indent(buf, cap, len, indent * level);

    // Opening tag
    buf_append_char(buf, cap, len, '<');
    buf_append(buf, cap, len, rt_string_cstr(elem->tag));

    // Attributes
    if (elem->attributes)
    {
        void *keys = rt_map_keys(elem->attributes);
        int64_t nkeys = rt_seq_len(keys);
        for (int64_t i = 0; i < nkeys; i++)
        {
            void *key = rt_seq_get(keys, i);
            void *val = rt_map_get(elem->attributes, (rt_string)key);

            buf_append_char(buf, cap, len, ' ');
            buf_append(buf, cap, len, rt_string_cstr((rt_string)key));
            buf_append(buf, cap, len, "=\"");
            buf_append_escaped(buf, cap, len, rt_string_cstr((rt_string)val), 1);
            buf_append_char(buf, cap, len, '"');

            if (rt_obj_release_check0(key))
                rt_obj_free(key);
            if (rt_obj_release_check0(val))
                rt_obj_free(val);
        }
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
    }

    // Check for children
    int64_t nchildren = elem->children ? rt_seq_len(elem->children) : 0;
    if (nchildren == 0)
    {
        buf_append(buf, cap, len, "/>");
        if (indent > 0)
            buf_append_char(buf, cap, len, '\n');
        return;
    }

    buf_append_char(buf, cap, len, '>');

    // Check if only text content
    bool text_only = true;
    for (int64_t i = 0; i < nchildren; i++)
    {
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
    for (int64_t i = 0; i < nchildren; i++)
    {
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

static void format_node(void *node, int indent, int level, char **buf, size_t *cap, size_t *len)
{
    xml_node *n = (xml_node *)node;

    switch (n->type)
    {
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
            if (n->children)
            {
                int64_t count = rt_seq_len(n->children);
                for (int64_t i = 0; i < count; i++)
                {
                    void *child = rt_seq_get(n->children, i);
                    format_node(child, indent, 0, buf, cap, len);
                    // Borrowed reference — parent owns child, do not release
                }
            }
            break;
    }
}

rt_string rt_xml_format(void *node)
{
    if (!node)
        return rt_str_empty();

    char *buf = NULL;
    size_t cap = 0, len = 0;

    format_node(node, 0, 0, &buf, &cap, &len);

    rt_string result = rt_string_from_bytes(buf ? buf : "", (int64_t)len);
    free(buf);
    return result;
}

rt_string rt_xml_format_pretty(void *node, int64_t indent)
{
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

rt_string rt_xml_escape(rt_string text)
{
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

rt_string rt_xml_unescape(rt_string text)
{
    if (!text)
        return rt_str_empty();

    const char *src = rt_string_cstr(text);
    size_t src_len = strlen(src);

    char *buf = malloc(src_len + 1);
    if (!buf)
        return rt_str_empty();

    size_t out = 0;
    for (size_t i = 0; i < src_len;)
    {
        if (src[i] == '&')
        {
            char decoded[4];
            size_t consumed;
            int n = decode_entity(src + i, src_len - i, decoded, &consumed);
            if (n > 0)
            {
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
