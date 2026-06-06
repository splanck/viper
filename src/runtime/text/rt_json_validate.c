//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_json_validate.c
// Purpose: Non-allocating JSON syntax validation for the Viper.Text.Json class
//          per ECMA-404 / RFC 8259. Mirrors the structure of the recursive
//          descent parser but never allocates and never traps — it returns a
//          boolean verdict, so callers can cheaply test parseability before
//          committing to a full parse.
//
// Key invariants:
//   - Shares the json_parser cursor with rt_json_parse.c (rt_json_internal.h)
//     but allocates no Viper objects.
//   - Returns 0 on any malformed input; 1 only when a complete JSON value is
//     consumed with no trailing content.
//
// Ownership/Lifetime:
//   - Allocates no heap state; the json_parser borrows the input buffer.
//
// Links: src/runtime/text/rt_json.h (public API),
//        src/runtime/text/rt_json_internal.h (shared parser cursor),
//        src/runtime/text/rt_json_parse.c (allocating parser)
//
//===----------------------------------------------------------------------===//

#include "rt_json.h"

#include "rt_internal.h"
#include "rt_json_internal.h"
#include "rt_string.h"

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief True if the span [start, start+len) is a finite double under strtod.
/// @details Used by the number validator to reject values that overflow to
///          infinity or otherwise fail to round-trip through strtod.
static int json_number_is_finite_span(const char *start, size_t len) {
    if (!start || len == 0)
        return 0;

    char *num_str = (char *)malloc(len + 1);
    if (!num_str)
        rt_trap("Json: memory allocation failed");
    memcpy(num_str, start, len);
    num_str[len] = '\0';

    errno = 0;
    char *endptr = NULL;
    double value = strtod(num_str, &endptr);
    int ok = endptr == num_str + len && errno != ERANGE && isfinite(value);
    free(num_str);
    return ok;
}

/// @brief Consume exactly four hex digits, accumulating their value into `*out`.
/// @return 1 if four hex digits were consumed, 0 otherwise.
static int validate_hex4(json_parser *p, unsigned int *out) {
    if (p->pos + 4 > p->len)
        return 0;
    unsigned int codepoint = 0;
    for (int i = 0; i < 4; i++) {
        int digit = rt_hex_digit_value(parser_consume(p));
        if (digit < 0)
            return 0;
        codepoint = (codepoint << 4) | (unsigned int)digit;
    }
    *out = codepoint;
    return 1;
}

/// @brief Non-trapping JSON validator: advance the cursor past one JSON value.
/// @details Mirrors the structure of `parse_value` but never allocates
///          and never calls `rt_trap` — instead it returns 0 on any
///          malformed input. Mutually recursive on objects and arrays.
///          Used by `rt_json_is_valid` for cheap pre-validation
///          when callers want to test parseability before committing.
/// @return 1 if a complete JSON value was consumed, 0 otherwise.
static int validate_value(json_parser *p) {
    parser_skip_whitespace(p);
    if (parser_eof(p))
        return 0;

    char c = parser_peek(p);

    if (c == '"') {
        // String: consume opening quote, scan to closing quote handling escapes
        parser_consume(p);
        while (!parser_eof(p)) {
            unsigned char ch = (unsigned char)parser_consume(p);
            if (ch == '"')
                return 1;
            if (ch < 0x20)
                return 0;
            if (ch == '\\') {
                if (parser_eof(p))
                    return 0;
                char esc = parser_consume(p);
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                    case 'b':
                    case 'f':
                    case 'n':
                    case 'r':
                    case 't':
                        break;
                    case 'u': {
                        unsigned int codepoint = 0;
                        if (!validate_hex4(p, &codepoint))
                            return 0;
                        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                            if (p->pos + 6 > p->len || parser_peek(p) != '\\' ||
                                p->input[p->pos + 1] != 'u')
                                return 0;
                            parser_consume(p);
                            parser_consume(p);
                            unsigned int low = 0;
                            if (!validate_hex4(p, &low))
                                return 0;
                            if (low < 0xDC00 || low > 0xDFFF)
                                return 0;
                        }
                        if (codepoint >= 0xDC00 && codepoint <= 0xDFFF)
                            return 0;
                        break;
                    }
                    default:
                        return 0;
                }
            }
        }
        return 0; // unterminated string
    }

    if (c == '-' || (c >= '0' && c <= '9')) {
        // Number
        size_t number_start = p->pos;
        if (c == '-')
            parser_consume(p);
        if (parser_eof(p))
            return 0;
        c = parser_peek(p);
        if (c == '0') {
            parser_consume(p);
            if (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
                return 0;
        } else if (c >= '1' && c <= '9') {
            parser_consume(p);
            while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
                parser_consume(p);
        } else {
            return 0;
        }
        if (!parser_eof(p) && parser_peek(p) == '.') {
            parser_consume(p);
            if (parser_eof(p) || !(parser_peek(p) >= '0' && parser_peek(p) <= '9'))
                return 0;
            while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
                parser_consume(p);
        }
        if (!parser_eof(p) && (parser_peek(p) == 'e' || parser_peek(p) == 'E')) {
            parser_consume(p);
            if (!parser_eof(p) && (parser_peek(p) == '+' || parser_peek(p) == '-'))
                parser_consume(p);
            if (parser_eof(p) || !(parser_peek(p) >= '0' && parser_peek(p) <= '9'))
                return 0;
            while (!parser_eof(p) && parser_peek(p) >= '0' && parser_peek(p) <= '9')
                parser_consume(p);
        }
        if (!json_number_is_finite_span(p->input + number_start, p->pos - number_start))
            return 0;
        return 1;
    }

    if (c == '{') {
        parser_consume(p);
        parser_skip_whitespace(p);
        if (!parser_eof(p) && parser_peek(p) == '}') {
            parser_consume(p);
            return 1;
        }
        for (;;) {
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

    if (c == '[') {
        parser_consume(p);
        parser_skip_whitespace(p);
        if (!parser_eof(p) && parser_peek(p) == ']') {
            parser_consume(p);
            return 1;
        }
        for (;;) {
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
    if (c == 't') {
        if (p->pos + 4 <= p->len && strncmp(p->input + p->pos, "true", 4) == 0) {
            p->pos += 4;
            return 1;
        }
        return 0;
    }
    if (c == 'f') {
        if (p->pos + 5 <= p->len && strncmp(p->input + p->pos, "false", 5) == 0) {
            p->pos += 5;
            return 1;
        }
        return 0;
    }
    if (c == 'n') {
        if (p->pos + 4 <= p->len && strncmp(p->input + p->pos, "null", 4) == 0) {
            p->pos += 4;
            return 1;
        }
        return 0;
    }

    return 0;
}

/// @brief Quick syntactic check: is `text` parseable as JSON?
///
/// Runs a non-allocating validator that mirrors the real parser,
/// returning a boolean instead of either the value or a trap.
/// Useful for input validation before committing to a parse.
/// @return 1 if valid, 0 if any parse error occurs.
int8_t rt_json_is_valid(rt_string text) {
    if (!text || rt_str_len(text) == 0)
        return 0;

    const char *input = rt_string_cstr(text);
    size_t len = (size_t)rt_str_len(text);
    json_parser p;
    parser_init(&p, input, len);

    if (!validate_value(&p))
        return 0;

    // Ensure no trailing content after the JSON value
    parser_skip_whitespace(&p);
    return parser_eof(&p) ? 1 : 0;
}
