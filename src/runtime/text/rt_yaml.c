//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_yaml.c
// Purpose: Implements a practical YAML 1.2 subset parser and formatter for the
//          Viper.Text.Yaml class. Supports scalars (string, int, float, bool,
//          null), block sequences (- item), block mappings (key: value),
//          quoted strings, comments (#), and multiline strings (| and >).
//
// Key invariants:
//   - YAML types map to: null→Box.I64(0), bool→Box.I64(0/1), int→Box.I64,
//     float→Box.F64, string→String, sequence→Seq, mapping→Map.
//   - Indentation determines nesting; tabs are not permitted as indentation.
//   - Parse returns NULL on invalid YAML (not a trap).
//   - Anchors (&) and aliases (*) are not supported in this implementation.
//   - All functions are thread-safe with no global mutable state.
//
// Ownership/Lifetime:
//   - Returned rt_map and rt_seq trees are fresh allocations owned by the caller.
//   - Formatted YAML strings are fresh rt_string allocations owned by caller.
//
// Links: src/runtime/text/rt_yaml.h (public API),
//        src/runtime/rt_map.h, rt_seq.h, rt_box.h (container types)
//
//===----------------------------------------------------------------------===//

#include "rt_yaml.h"

#include "rt_box.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Type Constants
//=============================================================================

// We use the standard rt_box_type values:
// RT_BOX_I64 = 0 (integers)
// RT_BOX_F64 = 1 (floats)
// RT_BOX_I1 = 2 (booleans)
// RT_BOX_STR = 3 (strings)
// NULL pointer represents YAML null

//=============================================================================
// Parser State
//=============================================================================

/* S-18: Maximum nesting depth before aborting */
#define YAML_MAX_DEPTH 200

typedef struct
{
    const char *input;
    size_t len;
    size_t pos;
    int line;
    int col;
    int depth; // Current nesting depth
} yaml_parser;

/// @brief Last parse error message (thread-local to avoid concurrent parse clobbering).
static _Thread_local char yaml_last_error[256];

static void set_error(const char *msg, int line)
{
    snprintf(yaml_last_error, sizeof(yaml_last_error), "Line %d: %s", line, msg);
}

static void clear_error(void)
{
    yaml_last_error[0] = '\0';
}

//=============================================================================
// Parser Helpers
//=============================================================================

static void parser_init(yaml_parser *p, const char *input, size_t len)
{
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->line = 1;
    p->col = 1;
    p->depth = 0;
}

static bool parser_eof(yaml_parser *p)
{
    return p->pos >= p->len;
}

static char parser_peek(yaml_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

static char parser_peek_at(yaml_parser *p, size_t offset)
{
    if (p->pos + offset >= p->len)
        return '\0';
    return p->input[p->pos + offset];
}

static char parser_advance(yaml_parser *p)
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

static void parser_skip_spaces(yaml_parser *p)
{
    while (!parser_eof(p) && (parser_peek(p) == ' ' || parser_peek(p) == '\t'))
        parser_advance(p);
}

static void parser_skip_to_eol(yaml_parser *p)
{
    while (!parser_eof(p) && parser_peek(p) != '\n')
        parser_advance(p);
}

static void parser_skip_comment(yaml_parser *p)
{
    if (parser_peek(p) == '#')
        parser_skip_to_eol(p);
}

static void parser_skip_whitespace_and_comments(yaml_parser *p)
{
    while (!parser_eof(p))
    {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t')
        {
            parser_advance(p);
        }
        else if (c == '#')
        {
            parser_skip_to_eol(p);
        }
        else if (c == '\n')
        {
            parser_advance(p);
        }
        else
        {
            break;
        }
    }
}

static int get_indent(yaml_parser *p)
{
    int indent = 0;
    size_t pos = p->pos;
    while (pos < p->len && p->input[pos] == ' ')
    {
        indent++;
        pos++;
    }
    return indent;
}

//=============================================================================
// Scalar Parsing
//=============================================================================

static bool is_special_value(const char *str, size_t len, const char *value)
{
    size_t vlen = strlen(value);
    if (len != vlen)
        return false;
    for (size_t i = 0; i < len; i++)
    {
        if (tolower((unsigned char)str[i]) != tolower((unsigned char)value[i]))
            return false;
    }
    return true;
}

static void *parse_scalar(const char *str, size_t len)
{
    if (len == 0)
        return NULL; // YAML null

    // Null
    if (is_special_value(str, len, "null") || is_special_value(str, len, "~") ||
        (len == 4 && strncmp(str, "Null", 4) == 0) || (len == 4 && strncmp(str, "NULL", 4) == 0))
    {
        return NULL;
    }

    // Boolean
    if (is_special_value(str, len, "true") || is_special_value(str, len, "yes") ||
        is_special_value(str, len, "on"))
    {
        return rt_box_i1(1);
    }
    if (is_special_value(str, len, "false") || is_special_value(str, len, "no") ||
        is_special_value(str, len, "off"))
    {
        return rt_box_i1(0);
    }

    // Try integer
    char *end;
    char *buf = malloc(len + 1);
    if (!buf)
        return rt_string_from_bytes(str, (int64_t)len);
    memcpy(buf, str, len);
    buf[len] = '\0';

    // Check for hex
    if (len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X'))
    {
        long long val = strtoll(buf, &end, 16);
        if (*end == '\0')
        {
            free(buf);
            return rt_box_i64(val);
        }
    }
    // Check for octal
    else if (len > 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O'))
    {
        long long val = strtoll(buf + 2, &end, 8);
        if (*end == '\0')
        {
            free(buf);
            return rt_box_i64(val);
        }
    }
    else
    {
        // Decimal integer
        long long val = strtoll(buf, &end, 10);
        if (*end == '\0')
        {
            free(buf);
            return rt_box_i64(val);
        }

        // Try float
        double fval = strtod(buf, &end);
        if (*end == '\0')
        {
            free(buf);
            return rt_box_f64(fval);
        }

        // Check for special floats
        if (is_special_value(str, len, ".inf") || is_special_value(str, len, ".Inf") ||
            is_special_value(str, len, ".INF"))
        {
            free(buf);
            return rt_box_f64(INFINITY);
        }
        if (is_special_value(str, len, "-.inf") || is_special_value(str, len, "-.Inf") ||
            is_special_value(str, len, "-.INF"))
        {
            free(buf);
            return rt_box_f64(-INFINITY);
        }
        if (is_special_value(str, len, ".nan") || is_special_value(str, len, ".NaN") ||
            is_special_value(str, len, ".NAN"))
        {
            free(buf);
            return rt_box_f64(NAN);
        }
    }

    free(buf);

    // Default to string
    return rt_string_from_bytes(str, (int64_t)len);
}

static rt_string parse_quoted_string(yaml_parser *p, char quote)
{
    parser_advance(p); // Skip opening quote

    size_t capacity = 64;
    char *buf = malloc(capacity);
    if (!buf)
        rt_trap("rt_yaml: memory allocation failed");
    size_t len = 0;

    while (!parser_eof(p) && parser_peek(p) != quote)
    {
        char c = parser_peek(p);

        if (c == '\\' && quote == '"')
        {
            parser_advance(p);
            c = parser_advance(p);
            switch (c)
            {
                case 'n':
                    c = '\n';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case '\\':
                    c = '\\';
                    break;
                case '"':
                    c = '"';
                    break;
                case '\'':
                    c = '\'';
                    break;
                case '0':
                    c = '\0';
                    break;
                default:
                    break;
            }
        }
        else
        {
            parser_advance(p);
        }

        if (len >= capacity - 1)
        {
            capacity *= 2;
            buf = realloc(buf, capacity);
            if (!buf)
                rt_trap("rt_yaml: memory allocation failed");
        }
        buf[len++] = c;
    }

    if (parser_peek(p) == quote)
        parser_advance(p); // Skip closing quote

    buf[len] = '\0';
    rt_string result = rt_string_from_bytes(buf, (int64_t)len);
    free(buf);
    return result;
}

//=============================================================================
// Value Parsing
//=============================================================================

static void *parse_value(yaml_parser *p, int base_indent);

static void *parse_block_sequence(yaml_parser *p, int base_indent)
{
    /* S-18: Guard against deeply nested documents */
    if (p->depth >= YAML_MAX_DEPTH)
    {
        set_error("sequence nesting depth limit exceeded", p->line);
        return rt_seq_new();
    }
    p->depth++;

    void *seq = rt_seq_new();
    if (!seq)
    {
        p->depth--;
        return NULL;
    }

    while (!parser_eof(p))
    {
        parser_skip_whitespace_and_comments(p);
        if (parser_eof(p))
            break;

        int indent = get_indent(p);
        if (indent < base_indent)
            break;

        // Skip to the '-'
        while (parser_peek(p) == ' ')
            parser_advance(p);

        if (parser_peek(p) != '-')
            break;

        parser_advance(p); // Skip '-'

        // Check for nested content
        if (parser_peek(p) == ' ' || parser_peek(p) == '\n')
        {
            if (parser_peek(p) == ' ')
                parser_advance(p); // Skip space after '-'

            void *item = parse_value(p, indent + 1);
            if (item)
            {
                rt_seq_push(seq, item);
                if (rt_obj_release_check0(item))
                    rt_obj_free(item);
            }
        }
        else
        {
            break;
        }
    }

    p->depth--;
    return seq;
}

static void *parse_block_mapping(yaml_parser *p, int base_indent)
{
    /* S-18: Guard against deeply nested documents */
    if (p->depth >= YAML_MAX_DEPTH)
    {
        set_error("mapping nesting depth limit exceeded", p->line);
        return rt_map_new();
    }
    p->depth++;

    void *map = rt_map_new();
    if (!map)
    {
        p->depth--;
        return NULL;
    }

    while (!parser_eof(p))
    {
        parser_skip_whitespace_and_comments(p);
        if (parser_eof(p))
            break;

        int indent = get_indent(p);
        if (indent < base_indent)
            break;

        // Skip indentation
        while (parser_peek(p) == ' ')
            parser_advance(p);

        // Check for sequence item
        if (parser_peek(p) == '-')
            break;

        // Parse key
        size_t key_start = p->pos;
        while (!parser_eof(p) && parser_peek(p) != ':' && parser_peek(p) != '\n')
            parser_advance(p);

        if (parser_peek(p) != ':')
            break;

        size_t key_len = p->pos - key_start;
        // Trim trailing spaces from key
        while (key_len > 0 && p->input[key_start + key_len - 1] == ' ')
            key_len--;

        rt_string key = rt_string_from_bytes(p->input + key_start, (int64_t)key_len);

        parser_advance(p); // Skip ':'
        parser_skip_spaces(p);

        void *value;
        if (parser_peek(p) == '\n' || parser_peek(p) == '#')
        {
            // Value on next line(s)
            parser_skip_comment(p);
            if (parser_peek(p) == '\n')
                parser_advance(p);

            value = parse_value(p, indent + 1);
        }
        else
        {
            // Inline value
            value = parse_value(p, indent);
        }

        if (value)
        {
            rt_map_set(map, key, value);
            if (rt_obj_release_check0(value))
                rt_obj_free(value);
        }

        if (rt_obj_release_check0((void *)key))
            rt_obj_free((void *)key);
    }

    p->depth--;
    return map;
}

static void *parse_value(yaml_parser *p, int base_indent)
{
    parser_skip_whitespace_and_comments(p);

    if (parser_eof(p))
        return NULL; // YAML null

    int indent = get_indent(p);

    // Skip to content
    while (parser_peek(p) == ' ')
        parser_advance(p);

    char c = parser_peek(p);

    // Quoted string
    if (c == '"' || c == '\'')
    {
        return (void *)parse_quoted_string(p, c);
    }

    // Sequence
    if (c == '-' && (parser_peek_at(p, 1) == ' ' || parser_peek_at(p, 1) == '\n'))
    {
        return parse_block_sequence(p, indent);
    }

    // Literal block scalar
    if (c == '|' || c == '>')
    {
        bool folded = (c == '>');
        parser_advance(p);
        parser_skip_to_eol(p);
        if (parser_peek(p) == '\n')
            parser_advance(p);

        // Get block indent
        int block_indent = get_indent(p);
        if (block_indent <= indent)
            return (void *)rt_str_empty();

        size_t capacity = 256;
        char *buf = malloc(capacity);
        if (!buf)
            rt_trap("rt_yaml: memory allocation failed");
        size_t len = 0;

        while (!parser_eof(p))
        {
            int line_indent = get_indent(p);
            if (line_indent < block_indent && p->input[p->pos + line_indent] != '\n')
                break;

            // Skip block indent
            for (int i = 0; i < block_indent && parser_peek(p) == ' '; i++)
                parser_advance(p);

            // Read line
            while (!parser_eof(p) && parser_peek(p) != '\n')
            {
                if (len >= capacity - 2)
                {
                    capacity *= 2;
                    buf = realloc(buf, capacity);
                    if (!buf)
                        rt_trap("rt_yaml: memory allocation failed");
                }
                buf[len++] = parser_advance(p);
            }

            if (parser_peek(p) == '\n')
            {
                parser_advance(p);
                if (folded)
                    buf[len++] = ' ';
                else
                    buf[len++] = '\n';
            }
        }

        // Trim trailing newline/space
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == ' '))
            len--;

        buf[len] = '\0';
        rt_string result = rt_string_from_bytes(buf, (int64_t)len);
        free(buf);
        return (void *)result;
    }

    // Check if it's a mapping (look for ':')
    {
        size_t scan = p->pos;
        while (scan < p->len && p->input[scan] != '\n' && p->input[scan] != '#')
        {
            if (p->input[scan] == ':' &&
                (scan + 1 >= p->len || p->input[scan + 1] == ' ' || p->input[scan + 1] == '\n'))
            {
                return parse_block_mapping(p, indent);
            }
            scan++;
        }
    }

    // Plain scalar
    size_t start = p->pos;
    while (!parser_eof(p) && parser_peek(p) != '\n' && parser_peek(p) != '#')
    {
        if (parser_peek(p) == ':' && (parser_peek_at(p, 1) == ' ' || parser_peek_at(p, 1) == '\n'))
            break;
        parser_advance(p);
    }

    size_t len = p->pos - start;
    // Trim trailing spaces
    while (len > 0 && p->input[start + len - 1] == ' ')
        len--;

    return parse_scalar(p->input + start, len);
}

//=============================================================================
// Public API - Parsing
//=============================================================================

void *rt_yaml_parse(rt_string text)
{
    clear_error();

    if (!text || rt_str_len(text) == 0)
        return NULL; // YAML null

    const char *cstr = rt_string_cstr(text);
    int64_t len = rt_str_len(text);

    yaml_parser p;
    parser_init(&p, cstr, (size_t)len);

    return parse_value(&p, 0);
}

rt_string rt_yaml_error(void)
{
    return rt_string_from_bytes(yaml_last_error, strlen(yaml_last_error));
}

int8_t rt_yaml_is_valid(rt_string text)
{
    clear_error();

    if (!text || rt_str_len(text) == 0)
        return 1; // Empty is valid

    void *result = rt_yaml_parse(text);
    if (result)
    {
        if (rt_obj_release_check0(result))
            rt_obj_free(result);
        return 1;
    }
    return 0;
}

//=============================================================================
// Formatting Helpers
//=============================================================================

static void format_value(void *obj, int indent, int level, char **buf, size_t *cap, size_t *len);

static void buf_append(char **buf, size_t *cap, size_t *len, const char *str)
{
    size_t slen = strlen(str);
    while (*len + slen + 1 > *cap)
    {
        *cap = (*cap == 0) ? 256 : (*cap * 2);
        *buf = realloc(*buf, *cap);
        if (!*buf)
            rt_trap("rt_yaml: memory allocation failed");
    }
    memcpy(*buf + *len, str, slen);
    *len += slen;
    (*buf)[*len] = '\0';
}

static void buf_append_char(char **buf, size_t *cap, size_t *len, char c)
{
    if (*len + 2 > *cap)
    {
        *cap = (*cap == 0) ? 256 : (*cap * 2);
        *buf = realloc(*buf, *cap);
        if (!*buf)
            rt_trap("rt_yaml: memory allocation failed");
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

static bool needs_quoting(const char *str)
{
    if (!str || !*str)
        return true;

    // Check for special values
    if (strcmp(str, "true") == 0 || strcmp(str, "false") == 0 || strcmp(str, "null") == 0 ||
        strcmp(str, "~") == 0 || strcmp(str, "yes") == 0 || strcmp(str, "no") == 0)
        return true;

    // Check first char
    char c = str[0];
    if (c == '-' || c == ':' || c == '[' || c == ']' || c == '{' || c == '}' || c == '#' ||
        c == '&' || c == '*' || c == '!' || c == '|' || c == '>' || c == '\'' || c == '"' ||
        c == '%' || c == '@' || c == '`')
        return true;

    // Check for special chars
    for (const char *p = str; *p; p++)
    {
        if (*p == '\n' || *p == '\r' || *p == ':')
            return true;
    }

    // Check if it looks like a number
    char *end;
    strtod(str, &end);
    if (*end == '\0')
        return true;

    return false;
}

static void format_string(const char *str, char **buf, size_t *cap, size_t *len)
{
    if (!str || !*str)
    {
        buf_append(buf, cap, len, "''");
        return;
    }

    if (!needs_quoting(str))
    {
        buf_append(buf, cap, len, str);
        return;
    }

    // Check if multiline
    bool has_newline = strchr(str, '\n') != NULL;
    if (has_newline)
    {
        buf_append(buf, cap, len, "|\n");
        // Add with indentation
        const char *p = str;
        while (*p)
        {
            buf_append(buf, cap, len, "  ");
            while (*p && *p != '\n')
            {
                buf_append_char(buf, cap, len, *p);
                p++;
            }
            if (*p == '\n')
            {
                buf_append_char(buf, cap, len, '\n');
                p++;
            }
        }
        return;
    }

    // Quote with double quotes
    buf_append_char(buf, cap, len, '"');
    for (const char *p = str; *p; p++)
    {
        switch (*p)
        {
            case '"':
                buf_append(buf, cap, len, "\\\"");
                break;
            case '\\':
                buf_append(buf, cap, len, "\\\\");
                break;
            case '\n':
                buf_append(buf, cap, len, "\\n");
                break;
            case '\t':
                buf_append(buf, cap, len, "\\t");
                break;
            case '\r':
                buf_append(buf, cap, len, "\\r");
                break;
            default:
                buf_append_char(buf, cap, len, *p);
                break;
        }
    }
    buf_append_char(buf, cap, len, '"');
}

static void format_value(void *obj, int indent, int level, char **buf, size_t *cap, size_t *len)
{
    if (!obj)
    {
        buf_append(buf, cap, len, "null");
        return;
    }

    // Check for boxed values using standard rt_box_type
    int64_t type_tag = rt_box_type(obj);
    if (type_tag == RT_BOX_I1) // Boolean
    {
        buf_append(buf, cap, len, rt_unbox_i1(obj) ? "true" : "false");
        return;
    }
    if (type_tag == RT_BOX_I64) // Integer
    {
        char num[32];
        snprintf(num, sizeof(num), "%lld", (long long)rt_unbox_i64(obj));
        buf_append(buf, cap, len, num);
        return;
    }
    if (type_tag == RT_BOX_F64) // Float
    {
        double val = rt_unbox_f64(obj);
        if (isinf(val))
        {
            buf_append(buf, cap, len, val > 0 ? ".inf" : "-.inf");
        }
        else if (isnan(val))
        {
            buf_append(buf, cap, len, ".nan");
        }
        else
        {
            char num[64];
            snprintf(num, sizeof(num), "%g", val);
            buf_append(buf, cap, len, num);
        }
        return;
    }
    if (type_tag == RT_BOX_STR) // Boxed string
    {
        rt_string s = rt_unbox_str(obj);
        const char *str = rt_string_cstr(s);
        format_string(str, buf, cap, len);
        if (rt_obj_release_check0((void *)s))
            rt_obj_free((void *)s);
        return;
    }

    // Check for string
    const char *str = rt_string_cstr((rt_string)obj);
    if (str)
    {
        format_string(str, buf, cap, len);
        return;
    }

    // Check for sequence
    int64_t seq_len = rt_seq_len(obj);
    if (seq_len >= 0)
    {
        if (seq_len == 0)
        {
            buf_append(buf, cap, len, "[]");
            return;
        }

        for (int64_t i = 0; i < seq_len; i++)
        {
            if (i > 0 || level > 0)
            {
                buf_append_char(buf, cap, len, '\n');
                buf_append_indent(buf, cap, len, indent * level);
            }
            buf_append(buf, cap, len, "- ");

            void *item = rt_seq_get(obj, i);
            format_value(item, indent, level + 1, buf, cap, len);
            if (item && rt_obj_release_check0(item))
                rt_obj_free(item);
        }
        return;
    }

    // Check for map
    void *keys = rt_map_keys(obj);
    if (keys)
    {
        int64_t nkeys = rt_seq_len(keys);
        if (nkeys == 0)
        {
            buf_append(buf, cap, len, "{}");
            if (rt_obj_release_check0(keys))
                rt_obj_free(keys);
            return;
        }

        for (int64_t i = 0; i < nkeys; i++)
        {
            if (i > 0 || level > 0)
            {
                buf_append_char(buf, cap, len, '\n');
                buf_append_indent(buf, cap, len, indent * level);
            }

            void *key = rt_seq_get(keys, i);
            const char *key_str = rt_string_cstr((rt_string)key);
            if (needs_quoting(key_str))
            {
                format_string(key_str, buf, cap, len);
            }
            else
            {
                buf_append(buf, cap, len, key_str);
            }
            buf_append(buf, cap, len, ": ");

            void *val = rt_map_get(obj, (rt_string)key);

            // Check if value needs newline (sequences and maps are complex)
            int64_t val_type = rt_box_type(val);
            bool complex_value =
                (val_type < 0 && rt_seq_len(val) > 0) || (rt_map_keys(val) != NULL);

            if (complex_value)
            {
                format_value(val, indent, level + 1, buf, cap, len);
            }
            else
            {
                format_value(val, indent, level, buf, cap, len);
            }

            if (val && rt_obj_release_check0(val))
                rt_obj_free(val);
            if (key && rt_obj_release_check0(key))
                rt_obj_free(key);
        }

        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
        return;
    }

    // Unknown type - format as string
    buf_append(buf, cap, len, "null");
}

//=============================================================================
// Public API - Formatting
//=============================================================================

rt_string rt_yaml_format(void *obj)
{
    return rt_yaml_format_indent(obj, 2);
}

rt_string rt_yaml_format_indent(void *obj, int64_t indent)
{
    if (indent < 1)
        indent = 2;
    if (indent > 8)
        indent = 8;

    char *buf = NULL;
    size_t cap = 0, len = 0;

    format_value(obj, (int)indent, 0, &buf, &cap, &len);

    rt_string result = rt_string_from_bytes(buf ? buf : "", (int64_t)len);
    free(buf);
    return result;
}

//=============================================================================
// Public API - Type Inspection
//=============================================================================

rt_string rt_yaml_type_of(void *obj)
{
    if (!obj)
        return rt_string_from_bytes("null", 4);

    // Check for boxed values
    int64_t type_tag = rt_box_type(obj);
    if (type_tag == RT_BOX_I1)
        return rt_string_from_bytes("bool", 4);
    if (type_tag == RT_BOX_I64)
        return rt_string_from_bytes("int", 3);
    if (type_tag == RT_BOX_F64)
        return rt_string_from_bytes("float", 5);
    if (type_tag == RT_BOX_STR)
        return rt_string_from_bytes("string", 6);

    // String check (non-boxed)
    const char *str = rt_string_cstr((rt_string)obj);
    if (str)
        return rt_string_from_bytes("string", 6);

    // Sequence check
    if (rt_seq_len(obj) >= 0)
        return rt_string_from_bytes("sequence", 8);

    // Map check
    void *keys = rt_map_keys(obj);
    if (keys)
    {
        if (rt_obj_release_check0(keys))
            rt_obj_free(keys);
        return rt_string_from_bytes("mapping", 7);
    }

    return rt_string_from_bytes("unknown", 7);
}
