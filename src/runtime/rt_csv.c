//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_csv.c
// Purpose: CSV parsing and formatting utilities (RFC 4180 compliant).
// Key invariants: Handles quoted fields, escaped quotes, newlines in quotes.
// Ownership/Lifetime: Returned Seq and strings are newly allocated.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#include "rt_csv.h"

#include "rt_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/// Default CSV delimiter.
#define DEFAULT_DELIMITER ','

/// @brief Get delimiter character from string.
static char get_delim(rt_string delim)
{
    const char *s = rt_string_cstr(delim);
    if (s && s[0] != '\0')
        return s[0];
    return DEFAULT_DELIMITER;
}

/// @brief Check if field needs quoting for CSV output.
static bool needs_quoting(const char *field, size_t len, char delim)
{
    for (size_t i = 0; i < len; i++)
    {
        char c = field[i];
        if (c == delim || c == '"' || c == '\n' || c == '\r')
            return true;
    }
    return false;
}

//=============================================================================
// Parsing Implementation
//=============================================================================

/// @brief Parse state for RFC 4180 CSV parsing.
typedef struct
{
    const char *input; ///< Input string.
    size_t len;        ///< Total length.
    size_t pos;        ///< Current position.
    char delim;        ///< Delimiter character.
} csv_parser;

/// @brief Initialize parser state.
static void parser_init(csv_parser *p, const char *input, size_t len, char delim)
{
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->delim = delim;
}

/// @brief Check if parser is at end of input.
static bool parser_eof(csv_parser *p)
{
    return p->pos >= p->len;
}

/// @brief Peek current character without advancing.
static char parser_peek(csv_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

/// @brief Consume current character and advance.
static char parser_consume(csv_parser *p)
{
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos++];
}

/// @brief Parse a single field (possibly quoted).
/// @param p Parser state.
/// @param at_line_end Output: set to true if field ends at line boundary.
/// @return Newly allocated field string.
static rt_string parse_field(csv_parser *p, bool *at_line_end)
{
    *at_line_end = false;

    // EOF case - return empty field and signal end of line
    if (parser_eof(p))
    {
        *at_line_end = true;
        return rt_string_from_bytes("", 0);
    }

    // Check for quoted field
    if (parser_peek(p) == '"')
    {
        parser_consume(p); // consume opening quote

        // Build field content with escaped quotes handled
        size_t cap = 64;
        size_t len = 0;
        char *buf = (char *)malloc(cap);
        if (!buf)
            rt_trap("Csv.Parse: memory allocation failed");

        while (!parser_eof(p))
        {
            char c = parser_consume(p);

            if (c == '"')
            {
                // Check for escaped quote
                if (parser_peek(p) == '"')
                {
                    // Escaped quote - consume and add single quote
                    parser_consume(p);
                    if (len + 1 >= cap)
                    {
                        cap *= 2;
                        char *tmp = (char *)realloc(buf, cap);
                        if (!tmp)
                        {
                            free(buf);
                            rt_trap("Csv.Parse: memory allocation failed");
                        }
                        buf = tmp;
                    }
                    buf[len++] = '"';
                }
                else
                {
                    // End of quoted field
                    break;
                }
            }
            else
            {
                // Regular character (including newlines in quoted fields)
                if (len + 1 >= cap)
                {
                    cap *= 2;
                    char *tmp = (char *)realloc(buf, cap);
                    if (!tmp)
                    {
                        free(buf);
                        rt_trap("Csv.Parse: memory allocation failed");
                    }
                    buf = tmp;
                }
                buf[len++] = c;
            }
        }

        buf[len] = '\0';
        rt_string result = rt_string_from_bytes(buf, len);
        free(buf);

        // Skip to delimiter or line end
        if (!parser_eof(p))
        {
            char c = parser_peek(p);
            if (c == p->delim)
            {
                parser_consume(p);
            }
            else if (c == '\r')
            {
                parser_consume(p);
                if (parser_peek(p) == '\n')
                    parser_consume(p);
                *at_line_end = true;
            }
            else if (c == '\n')
            {
                parser_consume(p);
                *at_line_end = true;
            }
        }
        else
        {
            *at_line_end = true;
        }

        return result;
    }
    else
    {
        // Unquoted field - read until delimiter or line end
        size_t start = p->pos;
        while (!parser_eof(p))
        {
            char c = parser_peek(p);
            if (c == p->delim || c == '\r' || c == '\n')
                break;
            parser_consume(p);
        }
        size_t field_len = p->pos - start;
        rt_string result = rt_string_from_bytes(p->input + start, field_len);

        // Handle delimiter or line end
        if (!parser_eof(p))
        {
            char c = parser_peek(p);
            if (c == p->delim)
            {
                parser_consume(p);
            }
            else if (c == '\r')
            {
                parser_consume(p);
                if (parser_peek(p) == '\n')
                    parser_consume(p);
                *at_line_end = true;
            }
            else if (c == '\n')
            {
                parser_consume(p);
                *at_line_end = true;
            }
        }
        else
        {
            *at_line_end = true;
        }

        return result;
    }
}

/// @brief Parse a single row (line) of CSV.
static void *parse_row(csv_parser *p)
{
    void *row = rt_seq_new();
    bool at_line_end = false;

    // Use do-while to ensure we process trailing empty fields after delimiter
    do
    {
        rt_string field = parse_field(p, &at_line_end);
        rt_seq_push(row, (void *)field);
    } while (!at_line_end);

    return row;
}

//=============================================================================
// Formatting Implementation
//=============================================================================

/// @brief Format a single field for CSV output.
/// @param field Field string.
/// @param delim Delimiter character.
/// @param out Output buffer (must have enough space).
/// @return Number of bytes written.
static size_t format_field(const char *field, size_t field_len, char delim, char *out)
{
    if (!needs_quoting(field, field_len, delim))
    {
        // No quoting needed
        memcpy(out, field, field_len);
        return field_len;
    }

    // Need quoting
    size_t o = 0;
    out[o++] = '"';
    for (size_t i = 0; i < field_len; i++)
    {
        char c = field[i];
        if (c == '"')
        {
            out[o++] = '"';
            out[o++] = '"';
        }
        else
        {
            out[o++] = c;
        }
    }
    out[o++] = '"';
    return o;
}

/// @brief Calculate output size for a formatted field.
static size_t calc_field_size(const char *field, size_t field_len, char delim)
{
    if (!needs_quoting(field, field_len, delim))
        return field_len;

    // 2 for quotes + escaped quotes
    size_t size = 2;
    for (size_t i = 0; i < field_len; i++)
    {
        size += (field[i] == '"') ? 2 : 1;
    }
    return size;
}

//=============================================================================
// Public API
//=============================================================================

void *rt_csv_parse_line(rt_string line)
{
    return rt_csv_parse_line_with(line, rt_const_cstr(","));
}

void *rt_csv_parse_line_with(rt_string line, rt_string delim)
{
    const char *input = rt_string_cstr(line);
    if (!input)
        return rt_seq_new();

    size_t len = strlen(input);
    char d = get_delim(delim);

    csv_parser p;
    parser_init(&p, input, len, d);

    return parse_row(&p);
}

void *rt_csv_parse(rt_string text)
{
    return rt_csv_parse_with(text, rt_const_cstr(","));
}

void *rt_csv_parse_with(rt_string text, rt_string delim)
{
    const char *input = rt_string_cstr(text);
    if (!input)
        return rt_seq_new();

    size_t len = strlen(input);
    if (len == 0)
        return rt_seq_new();

    char d = get_delim(delim);

    csv_parser p;
    parser_init(&p, input, len, d);

    void *rows = rt_seq_new();

    while (!parser_eof(&p))
    {
        void *row = parse_row(&p);
        rt_seq_push(rows, row);
    }

    return rows;
}

rt_string rt_csv_format_line(void *fields)
{
    return rt_csv_format_line_with(fields, rt_const_cstr(","));
}

rt_string rt_csv_format_line_with(void *fields, rt_string delim)
{
    if (!fields)
        return rt_string_from_bytes("", 0);

    char d = get_delim(delim);
    int64_t count = rt_seq_len(fields);

    if (count == 0)
        return rt_string_from_bytes("", 0);

    // Calculate total output size
    size_t total_size = 0;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string field = (rt_string)rt_seq_get(fields, i);
        const char *str = rt_string_cstr(field);
        if (!str)
            str = "";
        size_t field_len = strlen(str);
        total_size += calc_field_size(str, field_len, d);
        if (i < count - 1)
            total_size++; // delimiter
    }

    char *out = (char *)malloc(total_size + 1);
    if (!out)
        rt_trap("Csv.FormatLine: memory allocation failed");

    size_t pos = 0;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string field = (rt_string)rt_seq_get(fields, i);
        const char *str = rt_string_cstr(field);
        if (!str)
            str = "";
        size_t field_len = strlen(str);
        pos += format_field(str, field_len, d, out + pos);
        if (i < count - 1)
            out[pos++] = d;
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}

rt_string rt_csv_format(void *rows)
{
    return rt_csv_format_with(rows, rt_const_cstr(","));
}

rt_string rt_csv_format_with(void *rows, rt_string delim)
{
    if (!rows)
        return rt_string_from_bytes("", 0);

    char d = get_delim(delim);
    int64_t row_count = rt_seq_len(rows);

    if (row_count == 0)
        return rt_string_from_bytes("", 0);

    // Calculate total output size
    size_t total_size = 0;
    for (int64_t r = 0; r < row_count; r++)
    {
        void *row = rt_seq_get(rows, r);
        if (!row)
            continue;

        int64_t count = rt_seq_len(row);
        for (int64_t i = 0; i < count; i++)
        {
            rt_string field = (rt_string)rt_seq_get(row, i);
            const char *str = rt_string_cstr(field);
            if (!str)
                str = "";
            size_t field_len = strlen(str);
            total_size += calc_field_size(str, field_len, d);
            if (i < count - 1)
                total_size++; // delimiter
        }
        total_size++; // newline
    }

    char *out = (char *)malloc(total_size + 1);
    if (!out)
        rt_trap("Csv.Format: memory allocation failed");

    size_t pos = 0;
    for (int64_t r = 0; r < row_count; r++)
    {
        void *row = rt_seq_get(rows, r);
        if (!row)
        {
            out[pos++] = '\n';
            continue;
        }

        int64_t count = rt_seq_len(row);
        for (int64_t i = 0; i < count; i++)
        {
            rt_string field = (rt_string)rt_seq_get(row, i);
            const char *str = rt_string_cstr(field);
            if (!str)
                str = "";
            size_t field_len = strlen(str);
            pos += format_field(str, field_len, d, out + pos);
            if (i < count - 1)
                out[pos++] = d;
        }
        out[pos++] = '\n';
    }
    out[pos] = '\0';

    rt_string result = rt_string_from_bytes(out, pos);
    free(out);
    return result;
}
