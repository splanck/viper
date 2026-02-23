//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_json.c
// Purpose: Implements JSON parsing and formatting for the Viper.Text.Json class
//          per ECMA-404 / RFC 8259. Maps JSON types to Viper runtime types:
//          null→Box.I64(0), bool→Box.I64, number→Box.F64, string→String,
//          array→Seq, object→Map<String,*>.
//
// Key invariants:
//   - All JSON numbers are parsed as IEEE 754 double (Box.F64).
//   - Unicode escape sequences (\uXXXX) are decoded during parsing.
//   - JSON null is represented as Box.I64(0) with a null type tag.
//   - Format produces compact JSON (no whitespace); FormatPretty indents.
//   - Parse returns NULL on invalid JSON input (not a trap).
//   - All functions are thread-safe with no global mutable state.
//
// Ownership/Lifetime:
//   - Returned Map and Seq trees are fresh allocations owned by the caller.
//   - Formatted JSON strings are fresh rt_string allocations owned by caller.
//
// Links: src/runtime/text/rt_json.h (public API),
//        src/runtime/text/rt_json_stream.h (streaming SAX parser for large JSON),
//        src/runtime/rt_map.h, rt_seq.h, rt_box.h (container types)
//
//===----------------------------------------------------------------------===//

#include "rt_json.h"

#include "rt_box.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// JSON Type Tags (stored in box type field)
//=============================================================================

#define JSON_TYPE_NULL 100
#define JSON_TYPE_BOOL 101
#define JSON_TYPE_NUMBER 102

//=============================================================================
// Parser State
//=============================================================================

/* S-16: Maximum nesting depth before aborting (stack overflow / DoS guard) */
#define JSON_MAX_DEPTH 200

typedef struct
{
    const char *input;
    size_t len;
    size_t pos;
    int depth;          // Current nesting depth
    int depth_exceeded; // S-16: set when depth limit hit (unwinds without trap)
} json_parser;

static void parser_init(json_parser *p, const char *input, size_t len)
{
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->depth = 0;
    p->depth_exceeded = 0;
}

static bool parser_eof(json_parser *p)
{
    return p->pos >= p->len;
}

static char parser_peek(json_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

static char parser_consume(json_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos++];
}

static void parser_skip_whitespace(json_parser *p)
{
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->pos++;
        else
            break;
    }
}

static void parser_error(json_parser *p, const char *msg)
{
    // Calculate line and column for error message
    size_t line = 1;
    size_t col = 1;
    for (size_t i = 0; i < p->pos && i < p->len; i++)
    {
        if (p->input[i] == '\n')
        {
            line++;
            col = 1;
        }
        else
        {
            col++;
        }
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "Json.Parse: %s at line %zu, column %zu", msg, line, col);
    rt_trap(buf);
}

//=============================================================================
// Forward Declarations
//=============================================================================

static void *parse_value(json_parser *p);

//=============================================================================
// String Parsing
//=============================================================================

/// @brief Parse a JSON string (starting after the opening quote).
static rt_string parse_string(json_parser *p)
{
    if (parser_consume(p) != '"')
    {
        parser_error(p, "expected string");
        return rt_string_from_bytes("", 0);
    }

    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf)
    {
        rt_trap("Json.Parse: memory allocation failed");
        return rt_string_from_bytes("", 0);
    }

    while (!parser_eof(p))
    {
        char c = parser_consume(p);

        if (c == '"')
        {
            // End of string
            buf[len] = '\0';
            rt_string result = rt_string_from_bytes(buf, len);
            free(buf);
            return result;
        }

        if (c == '\\')
        {
            // Escape sequence
            if (parser_eof(p))
            {
                free(buf);
                parser_error(p, "unexpected end of string");
                return rt_string_from_bytes("", 0);
            }

            char esc = parser_consume(p);
            char decoded;
            switch (esc)
            {
                case '"':
                    decoded = '"';
                    break;
                case '\\':
                    decoded = '\\';
                    break;
                case '/':
                    decoded = '/';
                    break;
                case 'b':
                    decoded = '\b';
                    break;
                case 'f':
                    decoded = '\f';
                    break;
                case 'n':
                    decoded = '\n';
                    break;
                case 'r':
                    decoded = '\r';
                    break;
                case 't':
                    decoded = '\t';
                    break;
                case 'u':
                {
                    // Unicode escape: \uXXXX
                    if (p->pos + 4 > p->len)
                    {
                        free(buf);
                        parser_error(p, "incomplete unicode escape");
                        return rt_string_from_bytes("", 0);
                    }

                    unsigned int codepoint = 0;
                    for (int i = 0; i < 4; i++)
                    {
                        char hex = parser_consume(p);
                        unsigned int digit;
                        if (hex >= '0' && hex <= '9')
                            digit = hex - '0';
                        else if (hex >= 'a' && hex <= 'f')
                            digit = 10 + hex - 'a';
                        else if (hex >= 'A' && hex <= 'F')
                            digit = 10 + hex - 'A';
                        else
                        {
                            free(buf);
                            parser_error(p, "invalid unicode escape");
                            return rt_string_from_bytes("", 0);
                        }
                        codepoint = (codepoint << 4) | digit;
                    }

                    // Encode codepoint as UTF-8
                    if (codepoint < 0x80)
                    {
                        if (len + 1 >= cap)
                        {
                            cap *= 2;
                            char *tmp = (char *)realloc(buf, cap);
                            if (!tmp)
                            {
                                free(buf);
                                rt_trap("Json.Parse: memory allocation failed");
                            }
                            buf = tmp;
                        }
                        buf[len++] = (char)codepoint;
                    }
                    else if (codepoint < 0x800)
                    {
                        if (len + 2 >= cap)
                        {
                            cap *= 2;
                            char *tmp = (char *)realloc(buf, cap);
                            if (!tmp)
                            {
                                free(buf);
                                rt_trap("Json.Parse: memory allocation failed");
                            }
                            buf = tmp;
                        }
                        buf[len++] = (char)(0xC0 | (codepoint >> 6));
                        buf[len++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                    else
                    {
                        if (len + 3 >= cap)
                        {
                            cap *= 2;
                            char *tmp = (char *)realloc(buf, cap);
                            if (!tmp)
                            {
                                free(buf);
                                rt_trap("Json.Parse: memory allocation failed");
                            }
                            buf = tmp;
                        }
                        buf[len++] = (char)(0xE0 | (codepoint >> 12));
                        buf[len++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                        buf[len++] = (char)(0x80 | (codepoint & 0x3F));
                    }
                    continue;
                }
                default:
                    free(buf);
                    parser_error(p, "invalid escape sequence");
                    return rt_string_from_bytes("", 0);
            }

            if (len + 1 >= cap)
            {
                cap *= 2;
                char *tmp = (char *)realloc(buf, cap);
                if (!tmp)
                {
                    free(buf);
                    rt_trap("Json.Parse: memory allocation failed");
                }
                buf = tmp;
            }
            buf[len++] = decoded;
        }
        else if ((unsigned char)c < 0x20)
        {
            // Control characters not allowed in strings
            free(buf);
            parser_error(p, "control character in string");
            return rt_string_from_bytes("", 0);
        }
        else
        {
            // Regular character
            if (len + 1 >= cap)
            {
                cap *= 2;
                char *tmp = (char *)realloc(buf, cap);
                if (!tmp)
                {
                    free(buf);
                    rt_trap("Json.Parse: memory allocation failed");
                }
                buf = tmp;
            }
            buf[len++] = c;
        }
    }

    free(buf);
    parser_error(p, "unterminated string");
    return rt_string_from_bytes("", 0);
}

//=============================================================================
// Number Parsing
//=============================================================================

static void *parse_number(json_parser *p)
{
    size_t start = p->pos;

    // Optional minus sign
    if (parser_peek(p) == '-')
        parser_consume(p);

    // Integer part
    if (parser_peek(p) == '0')
    {
        parser_consume(p);
    }
    else if (parser_peek(p) >= '1' && parser_peek(p) <= '9')
    {
        while (parser_peek(p) >= '0' && parser_peek(p) <= '9')
            parser_consume(p);
    }
    else
    {
        parser_error(p, "invalid number");
        return rt_box_f64(0.0);
    }

    // Fractional part
    if (parser_peek(p) == '.')
    {
        parser_consume(p);
        if (parser_peek(p) < '0' || parser_peek(p) > '9')
        {
            parser_error(p, "invalid number: expected digit after decimal point");
            return rt_box_f64(0.0);
        }
        while (parser_peek(p) >= '0' && parser_peek(p) <= '9')
            parser_consume(p);
    }

    // Exponent part
    if (parser_peek(p) == 'e' || parser_peek(p) == 'E')
    {
        parser_consume(p);
        if (parser_peek(p) == '+' || parser_peek(p) == '-')
            parser_consume(p);
        if (parser_peek(p) < '0' || parser_peek(p) > '9')
        {
            parser_error(p, "invalid number: expected digit in exponent");
            return rt_box_f64(0.0);
        }
        while (parser_peek(p) >= '0' && parser_peek(p) <= '9')
            parser_consume(p);
    }

    // Extract the number string and parse it
    size_t num_len = p->pos - start;
    char *num_str = (char *)malloc(num_len + 1);
    if (!num_str)
    {
        rt_trap("Json.Parse: memory allocation failed");
        return rt_box_f64(0.0);
    }
    memcpy(num_str, p->input + start, num_len);
    num_str[num_len] = '\0';

    double value = strtod(num_str, NULL);
    free(num_str);

    return rt_box_f64(value);
}

//=============================================================================
// Array Parsing
//=============================================================================

static void *parse_array(json_parser *p)
{
    /* S-16: Reject deeply nested documents */
    if (p->depth >= JSON_MAX_DEPTH)
    {
        p->depth_exceeded = 1;
        return NULL;
    }
    p->depth++;

    if (parser_consume(p) != '[')
    {
        p->depth--;
        parser_error(p, "expected array");
        return rt_seq_new();
    }

    void *seq = rt_seq_new();
    parser_skip_whitespace(p);

    // Empty array
    if (parser_peek(p) == ']')
    {
        parser_consume(p);
        p->depth--;
        return seq;
    }

    // Parse elements
    while (true)
    {
        parser_skip_whitespace(p);
        void *value = parse_value(p);
        /* S-16: depth limit hit inside nested value — bail out cleanly */
        if (p->depth_exceeded)
            return seq;
        rt_seq_push(seq, value);

        parser_skip_whitespace(p);
        char c = parser_peek(p);

        if (c == ']')
        {
            parser_consume(p);
            break;
        }
        else if (c == ',')
        {
            parser_consume(p);
        }
        else
        {
            parser_error(p, "expected ',' or ']' in array");
            p->depth--;
            return seq;
        }
    }

    p->depth--;
    return seq;
}

//=============================================================================
// Object Parsing
//=============================================================================

static void *parse_object(json_parser *p)
{
    /* S-16: Reject deeply nested documents */
    if (p->depth >= JSON_MAX_DEPTH)
    {
        p->depth_exceeded = 1;
        return NULL;
    }
    p->depth++;

    if (parser_consume(p) != '{')
    {
        p->depth--;
        parser_error(p, "expected object");
        return rt_map_new();
    }

    void *map = rt_map_new();
    parser_skip_whitespace(p);

    // Empty object
    if (parser_peek(p) == '}')
    {
        parser_consume(p);
        return map;
    }

    // Parse key-value pairs
    while (true)
    {
        parser_skip_whitespace(p);

        if (parser_peek(p) != '"')
        {
            parser_error(p, "expected string key in object");
            return map;
        }

        rt_string key = parse_string(p);
        parser_skip_whitespace(p);

        if (parser_consume(p) != ':')
        {
            rt_str_release_maybe(key);
            parser_error(p, "expected ':' after key in object");
            return map;
        }

        parser_skip_whitespace(p);
        void *value = parse_value(p);
        /* S-16: depth limit hit inside nested value — bail out cleanly */
        if (p->depth_exceeded)
        {
            rt_str_release_maybe(key);
            return map;
        }

        rt_map_set(map, key, value);
        rt_str_release_maybe(key);

        parser_skip_whitespace(p);
        char c = parser_peek(p);

        if (c == '}')
        {
            parser_consume(p);
            break;
        }
        else if (c == ',')
        {
            parser_consume(p);
        }
        else
        {
            parser_error(p, "expected ',' or '}' in object");
            p->depth--;
            return map;
        }
    }

    p->depth--;
    return map;
}

//=============================================================================
// Value Parsing
//=============================================================================

static void *parse_value(json_parser *p)
{
    /* S-16: Propagate depth-exceeded without trapping */
    if (p->depth_exceeded)
        return NULL;

    parser_skip_whitespace(p);

    if (parser_eof(p))
    {
        parser_error(p, "unexpected end of input");
        return NULL;
    }

    char c = parser_peek(p);

    // String
    if (c == '"')
    {
        return (void *)parse_string(p);
    }

    // Number
    if (c == '-' || (c >= '0' && c <= '9'))
    {
        return parse_number(p);
    }

    // Array
    if (c == '[')
    {
        return parse_array(p);
    }

    // Object
    if (c == '{')
    {
        return parse_object(p);
    }

    // true
    if (p->pos + 4 <= p->len && strncmp(p->input + p->pos, "true", 4) == 0)
    {
        p->pos += 4;
        return rt_box_i1(1);
    }

    // false
    if (p->pos + 5 <= p->len && strncmp(p->input + p->pos, "false", 5) == 0)
    {
        p->pos += 5;
        return rt_box_i1(0);
    }

    // null
    if (p->pos + 4 <= p->len && strncmp(p->input + p->pos, "null", 4) == 0)
    {
        p->pos += 4;
        // Return NULL for JSON null
        return NULL;
    }

    parser_error(p, "unexpected character");
    return NULL;
}

//=============================================================================
// String Formatting Helpers
//=============================================================================

typedef struct
{
    char *buf;
    size_t len;
    size_t cap;
} string_builder;

static void sb_init(string_builder *sb)
{
    sb->cap = 256;
    sb->len = 0;
    sb->buf = (char *)malloc(sb->cap);
    if (!sb->buf)
        rt_trap("Json.Format: memory allocation failed");
    sb->buf[0] = '\0';
}

static void sb_grow(string_builder *sb, size_t needed)
{
    if (sb->len + needed < sb->cap)
        return;

    while (sb->cap <= sb->len + needed)
        sb->cap *= 2;

    char *tmp = (char *)realloc(sb->buf, sb->cap);
    if (!tmp)
    {
        free(sb->buf);
        rt_trap("Json.Format: memory allocation failed");
    }
    sb->buf = tmp;
}

static void sb_append(string_builder *sb, const char *s)
{
    size_t slen = strlen(s);
    sb_grow(sb, slen + 1);
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void sb_append_char(string_builder *sb, char c)
{
    sb_grow(sb, 2);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static void sb_append_indent(string_builder *sb, int64_t indent, int64_t level)
{
    if (indent <= 0)
        return;

    int64_t spaces = indent * level;
    sb_grow(sb, (size_t)spaces + 1);
    for (int64_t i = 0; i < spaces; i++)
        sb->buf[sb->len++] = ' ';
    sb->buf[sb->len] = '\0';
}

static rt_string sb_finish(string_builder *sb)
{
    rt_string result = rt_string_from_bytes(sb->buf, sb->len);
    free(sb->buf);
    return result;
}

//=============================================================================
// JSON String Escaping
//=============================================================================

static void format_string(string_builder *sb, rt_string s)
{
    sb_append_char(sb, '"');

    const char *str = rt_string_cstr(s);
    if (!str)
    {
        sb_append_char(sb, '"');
        return;
    }

    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)str[i];

        switch (c)
        {
            case '"':
                sb_append(sb, "\\\"");
                break;
            case '\\':
                sb_append(sb, "\\\\");
                break;
            case '\b':
                sb_append(sb, "\\b");
                break;
            case '\f':
                sb_append(sb, "\\f");
                break;
            case '\n':
                sb_append(sb, "\\n");
                break;
            case '\r':
                sb_append(sb, "\\r");
                break;
            case '\t':
                sb_append(sb, "\\t");
                break;
            default:
                if (c < 0x20)
                {
                    // Escape control characters as \uXXXX
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    sb_append(sb, esc);
                }
                else
                {
                    sb_append_char(sb, (char)c);
                }
                break;
        }
    }

    sb_append_char(sb, '"');
}

//=============================================================================
// Value Formatting
//=============================================================================

static void format_value(string_builder *sb, void *obj, int64_t indent, int64_t level);

static void format_array(string_builder *sb, void *seq, int64_t indent, int64_t level)
{
    int64_t len = rt_seq_len(seq);

    if (len == 0)
    {
        sb_append(sb, "[]");
        return;
    }

    sb_append_char(sb, '[');
    if (indent > 0)
        sb_append_char(sb, '\n');

    for (int64_t i = 0; i < len; i++)
    {
        if (indent > 0)
            sb_append_indent(sb, indent, level + 1);

        void *item = rt_seq_get(seq, i);
        format_value(sb, item, indent, level + 1);

        if (i < len - 1)
            sb_append_char(sb, ',');
        if (indent > 0)
            sb_append_char(sb, '\n');
    }

    if (indent > 0)
        sb_append_indent(sb, indent, level);
    sb_append_char(sb, ']');
}

static void format_object(string_builder *sb, void *map, int64_t indent, int64_t level)
{
    int64_t len = rt_map_len(map);

    if (len == 0)
    {
        sb_append(sb, "{}");
        return;
    }

    sb_append_char(sb, '{');
    if (indent > 0)
        sb_append_char(sb, '\n');

    void *keys = rt_map_keys(map);
    int64_t keys_len = rt_seq_len(keys);

    for (int64_t i = 0; i < keys_len; i++)
    {
        if (indent > 0)
            sb_append_indent(sb, indent, level + 1);

        rt_string key = (rt_string)rt_seq_get(keys, i);
        format_string(sb, key);

        sb_append_char(sb, ':');
        if (indent > 0)
            sb_append_char(sb, ' ');

        void *value = rt_map_get(map, key);
        format_value(sb, value, indent, level + 1);

        if (i < keys_len - 1)
            sb_append_char(sb, ',');
        if (indent > 0)
            sb_append_char(sb, '\n');
    }

    if (indent > 0)
        sb_append_indent(sb, indent, level);
    sb_append_char(sb, '}');
}

static void format_value(string_builder *sb, void *obj, int64_t indent, int64_t level)
{
    // null
    if (!obj)
    {
        sb_append(sb, "null");
        return;
    }

    // Check if it's a string handle (most common case for strings)
    if (rt_string_is_handle(obj))
    {
        format_string(sb, (rt_string)obj);
        return;
    }

    // Distinguish between boxes and collections using allocation size.
    // The heap header stores the payload size:
    // - rt_box_t = 16 bytes (tag + union)
    // - rt_seq_impl = 24 bytes (len + cap + items pointer)
    // - rt_map_impl = 32 bytes (vptr + buckets + capacity + count)
    //
    // This is more reliable than checking the first field (which would be
    // box.tag, seq.len, or map.vptr - all potentially overlapping values).

    rt_heap_hdr_t *hdr = rt_heap_hdr(obj);
    if (hdr && hdr->magic == RT_MAGIC)
    {
        size_t obj_size = hdr->len;

        // Box is exactly 16 bytes (sizeof(rt_box_t))
        if (obj_size == 16)
        {
            // This is a box - check its type
            int64_t box_type = rt_box_type(obj);

            if (box_type == RT_BOX_I64)
            {
                int64_t val = rt_unbox_i64(obj);
                char buf[32];
                snprintf(buf, sizeof(buf), "%lld", (long long)val);
                sb_append(sb, buf);
                return;
            }

            if (box_type == RT_BOX_F64)
            {
                double val = rt_unbox_f64(obj);
                if (isnan(val))
                {
                    sb_append(sb, "null");
                    return;
                }
                if (isinf(val))
                {
                    sb_append(sb, "null");
                    return;
                }
                char buf[64];
                snprintf(buf, sizeof(buf), "%.17g", val);
                sb_append(sb, buf);
                return;
            }

            if (box_type == RT_BOX_I1)
            {
                int8_t val = rt_unbox_i1(obj);
                sb_append(sb, val ? "true" : "false");
                return;
            }

            if (box_type == RT_BOX_STR)
            {
                rt_string str = rt_unbox_str(obj);
                format_string(sb, str);
                return;
            }

            // Unknown box type - format as null
            sb_append(sb, "null");
            return;
        }

        // Seq is 24 bytes
        if (obj_size == 24)
        {
            format_array(sb, obj, indent, level);
            return;
        }

        // Map is 32 bytes
        if (obj_size == 32)
        {
            format_object(sb, obj, indent, level);
            return;
        }
    }

    // Unknown or invalid object - format as null
    sb_append(sb, "null");
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Parses a JSON string into a Viper value.
///
/// Parses any valid JSON text and returns the corresponding Viper value.
/// The return type depends on the JSON content:
/// - Objects become Map (string-keyed)
/// - Arrays become Seq
/// - Strings stay as String
/// - Numbers become boxed f64
/// - Booleans become boxed i64 (0 or 1)
/// - null becomes NULL
///
/// **Example:**
/// ```
/// Dim text = "{\"name\": \"Alice\", \"items\": [1, 2, 3]}"
/// Dim obj = Json.Parse(text)
/// Print obj.Get("name")           ' "Alice"
/// Dim items = obj.Get("items")
/// Print items.Get(0)              ' 1.0
/// ```
///
/// **Error handling:**
/// Traps with descriptive error message on invalid JSON, including line
/// and column information.
///
/// @param text The JSON text to parse.
///
/// @return Parsed value (Map, Seq, String, boxed number, boxed bool, or NULL).
///
/// @note O(n) time complexity where n is the text length.
/// @note Thread-safe (no global state).
///
/// @see rt_json_parse_object For parsing specifically as object
/// @see rt_json_parse_array For parsing specifically as array
/// @see rt_json_format For the inverse operation
void *rt_json_parse(rt_string text)
{
    const char *input = rt_string_cstr(text);
    if (!input)
        return NULL;

    size_t len = strlen(input);
    if (len == 0)
    {
        rt_trap("Json.Parse: empty input");
        return NULL;
    }

    json_parser p;
    parser_init(&p, input, len);

    void *result = parse_value(&p);

    /* S-16: If depth limit was hit, return NULL without inspecting trailing chars */
    if (p.depth_exceeded)
        return NULL;

    // Check for trailing content
    parser_skip_whitespace(&p);
    if (!parser_eof(&p))
    {
        parser_error(&p, "unexpected content after JSON value");
    }

    return result;
}

/// @brief Parses a JSON string expecting an object at the root.
///
/// Like rt_json_parse, but validates that the root value is a JSON object.
/// Returns a Map containing the object's key-value pairs.
///
/// **Example:**
/// ```
/// Dim config = Json.ParseObject("{\"debug\": true, \"port\": 8080}")
/// Print config.Get("port")  ' 8080.0
/// ```
///
/// @param text The JSON text to parse.
///
/// @return Map containing the object properties.
///
/// @note Traps if the root value is not an object.
///
/// @see rt_json_parse For parsing any JSON value
/// @see rt_json_parse_array For parsing specifically as array
void *rt_json_parse_object(rt_string text)
{
    const char *input = rt_string_cstr(text);
    if (!input)
    {
        rt_trap("Json.ParseObject: null input");
        return rt_map_new();
    }

    size_t len = strlen(input);
    if (len == 0)
    {
        rt_trap("Json.ParseObject: empty input");
        return rt_map_new();
    }

    json_parser p;
    parser_init(&p, input, len);
    parser_skip_whitespace(&p);

    if (parser_peek(&p) != '{')
    {
        rt_trap("Json.ParseObject: expected object at root");
        return rt_map_new();
    }

    void *result = parse_object(&p);

    parser_skip_whitespace(&p);
    if (!parser_eof(&p))
    {
        parser_error(&p, "unexpected content after JSON object");
    }

    return result;
}

/// @brief Parses a JSON string expecting an array at the root.
///
/// Like rt_json_parse, but validates that the root value is a JSON array.
/// Returns a Seq containing the array's elements.
///
/// **Example:**
/// ```
/// Dim items = Json.ParseArray("[1, 2, 3, \"four\"]")
/// Print items.Len()    ' 4
/// Print items.Get(3)   ' "four"
/// ```
///
/// @param text The JSON text to parse.
///
/// @return Seq containing the array elements.
///
/// @note Traps if the root value is not an array.
///
/// @see rt_json_parse For parsing any JSON value
/// @see rt_json_parse_object For parsing specifically as object
void *rt_json_parse_array(rt_string text)
{
    const char *input = rt_string_cstr(text);
    if (!input)
    {
        rt_trap("Json.ParseArray: null input");
        return rt_seq_new();
    }

    size_t len = strlen(input);
    if (len == 0)
    {
        rt_trap("Json.ParseArray: empty input");
        return rt_seq_new();
    }

    json_parser p;
    parser_init(&p, input, len);
    parser_skip_whitespace(&p);

    if (parser_peek(&p) != '[')
    {
        rt_trap("Json.ParseArray: expected array at root");
        return rt_seq_new();
    }

    void *result = parse_array(&p);

    parser_skip_whitespace(&p);
    if (!parser_eof(&p))
    {
        parser_error(&p, "unexpected content after JSON array");
    }

    return result;
}

/// @brief Formats a Viper value as compact JSON.
///
/// Converts a Viper value to its JSON representation without extra whitespace.
/// Produces minimal, single-line output suitable for APIs and storage.
///
/// **Type mappings:**
/// - Map -> JSON object
/// - Seq -> JSON array
/// - String -> JSON string
/// - Boxed f64 -> JSON number
/// - Boxed i64 -> JSON number or boolean
/// - NULL -> JSON null
///
/// **Example:**
/// ```
/// Dim obj = Map.New()
/// obj.Set("name", "Alice")
/// obj.Set("age", Box.F64(30.0))
/// Print Json.Format(obj)
/// ' Output: {"name":"Alice","age":30}
/// ```
///
/// @param obj The Viper value to format.
///
/// @return Compact JSON string.
///
/// @note O(n) time complexity where n is the total data size.
/// @note NaN and Infinity are formatted as null.
///
/// @see rt_json_format_pretty For human-readable output
/// @see rt_json_parse For the inverse operation
rt_string rt_json_format(void *obj)
{
    string_builder sb;
    sb_init(&sb);
    format_value(&sb, obj, 0, 0);
    return sb_finish(&sb);
}

/// @brief Formats a Viper value as pretty-printed JSON.
///
/// Converts a Viper value to its JSON representation with indentation and
/// newlines for human readability. Each nesting level is indented by the
/// specified number of spaces.
///
/// **Example:**
/// ```
/// Dim obj = Map.New()
/// obj.Set("name", "Alice")
/// Dim items = Seq.New()
/// items.Push(Box.F64(1))
/// items.Push(Box.F64(2))
/// obj.Set("items", items)
/// Print Json.FormatPretty(obj, 2)
/// ' Output:
/// ' {
/// '   "name": "Alice",
/// '   "items": [
/// '     1,
/// '     2
/// '   ]
/// ' }
/// ```
///
/// @param obj The Viper value to format.
/// @param indent Number of spaces per indentation level (typically 2 or 4).
///
/// @return Pretty-printed JSON string.
///
/// @note O(n) time complexity where n is the total data size.
/// @note If indent <= 0, behaves like rt_json_format (compact output).
///
/// @see rt_json_format For compact output
rt_string rt_json_format_pretty(void *obj, int64_t indent)
{
    if (indent <= 0)
        return rt_json_format(obj);

    string_builder sb;
    sb_init(&sb);
    format_value(&sb, obj, indent, 0);
    return sb_finish(&sb);
}

/// @brief Checks if a string contains valid JSON.
///
/// Validates JSON syntax without allocating memory for the parsed result.
/// Useful for validation before parsing or for filtering invalid input.
///
/// **Example:**
/// ```
/// Print Json.IsValid("{\"key\": 123}")    ' True
/// Print Json.IsValid("{key: 123}")        ' False (unquoted key)
/// Print Json.IsValid("not json")          ' False
/// ```
///
/// @param text The string to validate.
///
/// @return 1 if valid JSON, 0 otherwise.
///
/// @note O(n) time complexity where n is the text length.
/// @note Does not allocate memory (validation only).
///
/// @see rt_json_parse For parsing with full result
/// Non-trapping JSON validator — returns 1 if the cursor was advanced past
/// a valid JSON value, 0 on any syntax error.
static int validate_value(json_parser *p)
{
    parser_skip_whitespace(p);
    if (parser_eof(p))
        return 0;

    char c = parser_peek(p);

    if (c == '"')
    {
        // String: consume opening quote, scan to closing quote handling escapes
        parser_consume(p);
        while (!parser_eof(p))
        {
            char ch = parser_consume(p);
            if (ch == '"')
                return 1;
            if (ch == '\\')
            {
                if (parser_eof(p))
                    return 0;
                parser_consume(p); // skip escaped char
            }
        }
        return 0; // unterminated string
    }

    if (c == '-' || (c >= '0' && c <= '9'))
    {
        // Number
        if (c == '-')
            parser_consume(p);
        if (parser_eof(p))
            return 0;
        c = parser_peek(p);
        if (!(c >= '0' && c <= '9'))
            return 0;
        while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
            parser_consume(p);
        if (!parser_eof(p) && parser_peek(p) == '.')
        {
            parser_consume(p);
            if (parser_eof(p) || !(parser_peek(p) >= '0' && parser_peek(p) <= '9'))
                return 0;
            while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
                parser_consume(p);
        }
        if (!parser_eof(p) && (parser_peek(p) == 'e' || parser_peek(p) == 'E'))
        {
            parser_consume(p);
            if (!parser_eof(p) && (parser_peek(p) == '+' || parser_peek(p) == '-'))
                parser_consume(p);
            if (parser_eof(p) || !(parser_peek(p) >= '0' && parser_peek(p) <= '9'))
                return 0;
            while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
                parser_consume(p);
        }
        return 1;
    }

    if (c == '{')
    {
        parser_consume(p);
        parser_skip_whitespace(p);
        if (!parser_eof(p) && parser_peek(p) == '}')
        {
            parser_consume(p);
            return 1;
        }
        for (;;)
        {
            parser_skip_whitespace(p);
            if (parser_eof(p) || parser_peek(p) != '"')
                return 0;
            if (!validate_value(p))
                return 0; // key
            parser_skip_whitespace(p);
            if (parser_eof(p) || parser_consume(p) != ':')
                return 0;
            if (!validate_value(p))
                return 0; // value
            parser_skip_whitespace(p);
            if (parser_eof(p))
                return 0;
            c = parser_consume(p);
            if (c == '}')
                return 1;
            if (c != ',')
                return 0;
        }
    }

    if (c == '[')
    {
        parser_consume(p);
        parser_skip_whitespace(p);
        if (!parser_eof(p) && parser_peek(p) == ']')
        {
            parser_consume(p);
            return 1;
        }
        for (;;)
        {
            if (!validate_value(p))
                return 0;
            parser_skip_whitespace(p);
            if (parser_eof(p))
                return 0;
            c = parser_consume(p);
            if (c == ']')
                return 1;
            if (c != ',')
                return 0;
        }
    }

    // Keywords: true, false, null
    if (c == 't')
    {
        if (p->pos + 4 <= p->len && strncmp(p->input + p->pos, "true", 4) == 0)
        {
            p->pos += 4;
            return 1;
        }
        return 0;
    }
    if (c == 'f')
    {
        if (p->pos + 5 <= p->len && strncmp(p->input + p->pos, "false", 5) == 0)
        {
            p->pos += 5;
            return 1;
        }
        return 0;
    }
    if (c == 'n')
    {
        if (p->pos + 4 <= p->len && strncmp(p->input + p->pos, "null", 4) == 0)
        {
            p->pos += 4;
            return 1;
        }
        return 0;
    }

    return 0;
}

int8_t rt_json_is_valid(rt_string text)
{
    const char *input = rt_string_cstr(text);
    if (!input || strlen(input) == 0)
        return 0;

    json_parser p;
    parser_init(&p, input, strlen(input));

    if (!validate_value(&p))
        return 0;

    // Ensure no trailing content after the JSON value
    parser_skip_whitespace(&p);
    return parser_eof(&p) ? 1 : 0;
}

/// @brief Gets the JSON type of a parsed value.
///
/// Returns a string describing the JSON type of a value. Useful for
/// type checking before accessing specific methods.
///
/// **Possible return values:**
/// - "null" - for NULL values
/// - "boolean" - for boxed i64 values 0 or 1
/// - "number" - for boxed f64 values
/// - "string" - for String values
/// - "array" - for Seq values
/// - "object" - for Map values
///
/// **Example:**
/// ```
/// Dim obj = Json.Parse("[1, \"hello\", null]")
/// Print Json.TypeOf(obj)           ' "array"
/// Print Json.TypeOf(obj.Get(0))    ' "number"
/// Print Json.TypeOf(obj.Get(1))    ' "string"
/// Print Json.TypeOf(obj.Get(2))    ' "null"
/// ```
///
/// @param obj The parsed JSON value.
///
/// @return String describing the type.
rt_string rt_json_type_of(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("null", 4);

    if (rt_string_is_handle(obj))
        return rt_string_from_bytes("string", 6);

    // Use allocation size to distinguish between boxes and collections
    rt_heap_hdr_t *hdr = rt_heap_hdr(obj);
    if (hdr && hdr->magic == RT_MAGIC)
    {
        size_t obj_size = hdr->len;

        // Box is 16 bytes
        if (obj_size == 16)
        {
            int64_t box_type = rt_box_type(obj);
            if (box_type == RT_BOX_I64 || box_type == RT_BOX_F64)
                return rt_string_from_bytes("number", 6);
            if (box_type == RT_BOX_I1)
                return rt_string_from_bytes("boolean", 7);
            if (box_type == RT_BOX_STR)
                return rt_string_from_bytes("string", 6);
        }

        // Seq is 24 bytes
        if (obj_size == 24)
            return rt_string_from_bytes("array", 5);

        // Map is 32 bytes
        if (obj_size == 32)
            return rt_string_from_bytes("object", 6);
    }

    return rt_string_from_bytes("unknown", 7);
}
