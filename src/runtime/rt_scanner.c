//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_scanner.c
// Purpose: String scanner implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_scanner.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

//=============================================================================
// Internal Structure
//=============================================================================

typedef struct
{
    rt_string source;
    const char *data;
    int64_t len;
    int64_t pos;
} Scanner;

//=============================================================================
// Scanner Creation
//=============================================================================

void *rt_scanner_new(rt_string source)
{
    Scanner *s = (Scanner *)malloc(sizeof(Scanner));
    if (!s)
        return NULL;

    s->source = source;
    s->data = rt_string_cstr(source);
    s->len = rt_str_len(source);
    s->pos = 0;
    return s;
}

//=============================================================================
// Position and State
//=============================================================================

int64_t rt_scanner_pos(void *obj)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;
    return s->pos;
}

void rt_scanner_set_pos(void *obj, int64_t pos)
{
    if (!obj)
        return;
    Scanner *s = (Scanner *)obj;
    if (pos < 0)
        pos = 0;
    if (pos > s->len)
        pos = s->len;
    s->pos = pos;
}

int8_t rt_scanner_is_end(void *obj)
{
    if (!obj)
        return 1;
    Scanner *s = (Scanner *)obj;
    return s->pos >= s->len ? 1 : 0;
}

int64_t rt_scanner_remaining(void *obj)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;
    return s->len - s->pos;
}

int64_t rt_scanner_len(void *obj)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;
    return s->len;
}

void rt_scanner_reset(void *obj)
{
    if (!obj)
        return;
    Scanner *s = (Scanner *)obj;
    s->pos = 0;
}

//=============================================================================
// Peeking
//=============================================================================

int64_t rt_scanner_peek(void *obj)
{
    if (!obj)
        return -1;
    Scanner *s = (Scanner *)obj;
    if (s->pos >= s->len)
        return -1;
    return (unsigned char)s->data[s->pos];
}

int64_t rt_scanner_peek_at(void *obj, int64_t offset)
{
    if (!obj)
        return -1;
    Scanner *s = (Scanner *)obj;
    int64_t idx = s->pos + offset;
    if (idx < 0 || idx >= s->len)
        return -1;
    return (unsigned char)s->data[idx];
}

rt_string rt_scanner_peek_str(void *obj, int64_t n)
{
    if (!obj || n <= 0)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    int64_t avail = s->len - s->pos;
    if (n > avail)
        n = avail;
    if (n <= 0)
        return rt_const_cstr("");

    return rt_string_from_bytes(s->data + s->pos, n);
}

//=============================================================================
// Reading
//=============================================================================

int64_t rt_scanner_read(void *obj)
{
    if (!obj)
        return -1;
    Scanner *s = (Scanner *)obj;
    if (s->pos >= s->len)
        return -1;
    return (unsigned char)s->data[s->pos++];
}

rt_string rt_scanner_read_str(void *obj, int64_t n)
{
    if (!obj || n <= 0)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    int64_t avail = s->len - s->pos;
    if (n > avail)
        n = avail;
    if (n <= 0)
        return rt_const_cstr("");

    rt_string result = rt_string_from_bytes(s->data + s->pos, n);
    s->pos += n;
    return result;
}

rt_string rt_scanner_read_until(void *obj, int64_t delim)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    int64_t start = s->pos;
    while (s->pos < s->len && (unsigned char)s->data[s->pos] != (unsigned char)delim)
    {
        s->pos++;
    }

    if (start == s->pos)
        return rt_const_cstr("");
    return rt_string_from_bytes(s->data + start, s->pos - start);
}

rt_string rt_scanner_read_until_any(void *obj, rt_string delims)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    const char *delim_str = rt_string_cstr(delims);
    int64_t delim_len = rt_str_len(delims);

    int64_t start = s->pos;
    while (s->pos < s->len)
    {
        char c = s->data[s->pos];
        int found = 0;
        for (int64_t i = 0; i < delim_len; i++)
        {
            if (c == delim_str[i])
            {
                found = 1;
                break;
            }
        }
        if (found)
            break;
        s->pos++;
    }

    if (start == s->pos)
        return rt_const_cstr("");
    return rt_string_from_bytes(s->data + start, s->pos - start);
}

rt_string rt_scanner_read_while(void *obj, int8_t (*pred)(int64_t))
{
    if (!obj || !pred)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    int64_t start = s->pos;
    while (s->pos < s->len && pred((unsigned char)s->data[s->pos]))
    {
        s->pos++;
    }

    if (start == s->pos)
        return rt_const_cstr("");
    return rt_string_from_bytes(s->data + start, s->pos - start);
}

//=============================================================================
// Matching
//=============================================================================

int8_t rt_scanner_match(void *obj, int64_t c)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;
    if (s->pos >= s->len)
        return 0;
    return (unsigned char)s->data[s->pos] == (unsigned char)c ? 1 : 0;
}

int8_t rt_scanner_match_str(void *obj, rt_string str)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;

    const char *str_data = rt_string_cstr(str);
    int64_t str_len = rt_str_len(str);

    if (s->pos + str_len > s->len)
        return 0;

    return memcmp(s->data + s->pos, str_data, (size_t)str_len) == 0 ? 1 : 0;
}

int8_t rt_scanner_accept(void *obj, int64_t c)
{
    if (!rt_scanner_match(obj, c))
        return 0;
    Scanner *s = (Scanner *)obj;
    s->pos++;
    return 1;
}

int8_t rt_scanner_accept_str(void *obj, rt_string str)
{
    if (!rt_scanner_match_str(obj, str))
        return 0;
    Scanner *s = (Scanner *)obj;
    s->pos += rt_str_len(str);
    return 1;
}

int8_t rt_scanner_accept_any(void *obj, rt_string chars)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;
    if (s->pos >= s->len)
        return 0;

    const char *char_str = rt_string_cstr(chars);
    int64_t char_len = rt_str_len(chars);
    char c = s->data[s->pos];

    for (int64_t i = 0; i < char_len; i++)
    {
        if (c == char_str[i])
        {
            s->pos++;
            return 1;
        }
    }
    return 0;
}

//=============================================================================
// Skipping
//=============================================================================

void rt_scanner_skip(void *obj, int64_t n)
{
    if (!obj || n <= 0)
        return;
    Scanner *s = (Scanner *)obj;
    s->pos += n;
    if (s->pos > s->len)
        s->pos = s->len;
}

int64_t rt_scanner_skip_whitespace(void *obj)
{
    if (!obj)
        return 0;
    Scanner *s = (Scanner *)obj;

    int64_t start = s->pos;
    while (s->pos < s->len)
    {
        char c = s->data[s->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            s->pos++;
        else
            break;
    }
    return s->pos - start;
}

int64_t rt_scanner_skip_while(void *obj, int8_t (*pred)(int64_t))
{
    if (!obj || !pred)
        return 0;
    Scanner *s = (Scanner *)obj;

    int64_t start = s->pos;
    while (s->pos < s->len && pred((unsigned char)s->data[s->pos]))
    {
        s->pos++;
    }
    return s->pos - start;
}

//=============================================================================
// Token Helpers
//=============================================================================

rt_string rt_scanner_read_ident(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    if (s->pos >= s->len)
        return rt_const_cstr("");

    char c = s->data[s->pos];
    // Must start with letter or underscore
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
        return rt_const_cstr("");

    int64_t start = s->pos;
    s->pos++;

    while (s->pos < s->len)
    {
        c = s->data[s->pos];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            s->pos++;
        else
            break;
    }

    return rt_string_from_bytes(s->data + start, s->pos - start);
}

rt_string rt_scanner_read_int(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    if (s->pos >= s->len)
        return rt_const_cstr("");

    int64_t start = s->pos;

    // Optional sign
    if (s->data[s->pos] == '+' || s->data[s->pos] == '-')
    {
        s->pos++;
        if (s->pos >= s->len || !(s->data[s->pos] >= '0' && s->data[s->pos] <= '9'))
        {
            s->pos = start;
            return rt_const_cstr("");
        }
    }

    // Must have at least one digit
    if (!(s->data[s->pos] >= '0' && s->data[s->pos] <= '9'))
    {
        s->pos = start;
        return rt_const_cstr("");
    }

    while (s->pos < s->len && s->data[s->pos] >= '0' && s->data[s->pos] <= '9')
    {
        s->pos++;
    }

    return rt_string_from_bytes(s->data + start, s->pos - start);
}

rt_string rt_scanner_read_number(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    if (s->pos >= s->len)
        return rt_const_cstr("");

    int64_t start = s->pos;

    // Optional sign
    if (s->data[s->pos] == '+' || s->data[s->pos] == '-')
    {
        s->pos++;
    }

    // Integer part
    int has_digits = 0;
    while (s->pos < s->len && s->data[s->pos] >= '0' && s->data[s->pos] <= '9')
    {
        s->pos++;
        has_digits = 1;
    }

    // Decimal point and fraction
    if (s->pos < s->len && s->data[s->pos] == '.')
    {
        s->pos++;
        while (s->pos < s->len && s->data[s->pos] >= '0' && s->data[s->pos] <= '9')
        {
            s->pos++;
            has_digits = 1;
        }
    }

    // Exponent - only consume if there are digits after e/E and optional sign
    if (s->pos < s->len && (s->data[s->pos] == 'e' || s->data[s->pos] == 'E'))
    {
        int64_t exp_start = s->pos;
        s->pos++;
        if (s->pos < s->len && (s->data[s->pos] == '+' || s->data[s->pos] == '-'))
            s->pos++;
        // Must have at least one digit after exponent
        if (s->pos < s->len && s->data[s->pos] >= '0' && s->data[s->pos] <= '9')
        {
            while (s->pos < s->len && s->data[s->pos] >= '0' && s->data[s->pos] <= '9')
            {
                s->pos++;
            }
        }
        else
        {
            // No digits after exponent, rollback
            s->pos = exp_start;
        }
    }

    if (!has_digits)
    {
        s->pos = start;
        return rt_const_cstr("");
    }

    return rt_string_from_bytes(s->data + start, s->pos - start);
}

rt_string rt_scanner_read_quoted(void *obj, int64_t quote)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    if (s->pos >= s->len || s->data[s->pos] != (char)quote)
        return rt_const_cstr("");

    s->pos++; // Skip opening quote

    // Build result, handling escapes
    char buf[4096];
    int64_t buf_pos = 0;

    while (s->pos < s->len && s->data[s->pos] != (char)quote)
    {
        if (s->data[s->pos] == '\\' && s->pos + 1 < s->len)
        {
            s->pos++;
            char esc = s->data[s->pos++];
            char actual;
            switch (esc)
            {
                case 'n':
                    actual = '\n';
                    break;
                case 't':
                    actual = '\t';
                    break;
                case 'r':
                    actual = '\r';
                    break;
                case '\\':
                    actual = '\\';
                    break;
                case '"':
                    actual = '"';
                    break;
                case '\'':
                    actual = '\'';
                    break;
                default:
                    actual = esc;
                    break;
            }
            if (buf_pos < 4095)
                buf[buf_pos++] = actual;
        }
        else
        {
            if (buf_pos < 4095)
                buf[buf_pos++] = s->data[s->pos];
            s->pos++;
        }
    }

    // Skip closing quote
    if (s->pos < s->len && s->data[s->pos] == (char)quote)
        s->pos++;

    buf[buf_pos] = '\0';
    return rt_string_from_bytes(buf, buf_pos);
}

rt_string rt_scanner_read_line(void *obj)
{
    if (!obj)
        return rt_const_cstr("");
    Scanner *s = (Scanner *)obj;

    int64_t start = s->pos;
    while (s->pos < s->len && s->data[s->pos] != '\n' && s->data[s->pos] != '\r')
    {
        s->pos++;
    }

    rt_string result = rt_string_from_bytes(s->data + start, s->pos - start);

    // Skip newline(s)
    if (s->pos < s->len && s->data[s->pos] == '\r')
        s->pos++;
    if (s->pos < s->len && s->data[s->pos] == '\n')
        s->pos++;

    return result;
}

//=============================================================================
// Character Class Predicates
//=============================================================================

int8_t rt_scanner_is_digit(int64_t c)
{
    return (c >= '0' && c <= '9') ? 1 : 0;
}

int8_t rt_scanner_is_alpha(int64_t c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) ? 1 : 0;
}

int8_t rt_scanner_is_alnum(int64_t c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) ? 1 : 0;
}

int8_t rt_scanner_is_space(int64_t c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r') ? 1 : 0;
}
