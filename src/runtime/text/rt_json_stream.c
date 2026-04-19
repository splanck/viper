//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_json_stream.c
// Purpose: Implements a SAX-style pull-based streaming JSON parser for the
//          Viper.Text.JsonStream class. Emits tokens one at a time: ObjectStart,
//          ObjectEnd, ArrayStart, ArrayEnd, Key, String, Number, Bool, Null.
//
// Key invariants:
//   - Maximum nesting depth is MAX_DEPTH (256); exceeding it returns an error token.
//   - The parser advances by one token per call to Next; state is maintained in
//     the stream object between calls.
//   - String token values are unescaped (\\, \", \n etc. processed).
//   - Number tokens are parsed as IEEE 754 double.
//   - Invalid JSON causes an Error token; the stream is not recoverable after error.
//   - Input is a borrowed char* with length; the caller must keep it alive.
//
// Ownership/Lifetime:
//   - The stream object is heap-allocated and managed by the runtime GC.
//   - An internal string buffer is grown dynamically and freed with the stream.
//   - Key and String token values are returned as fresh rt_string allocations.
//
// Links: src/runtime/text/rt_json_stream.h (public API),
//        src/runtime/text/rt_json.h (document-mode JSON parser for small inputs)
//
//===----------------------------------------------------------------------===//

#include "rt_json_stream.h"

#include "rt_internal.h"
#include "rt_object.h"
#include "rt_string.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 256

typedef struct {
    rt_string input_owner;
    const char *input;
    size_t len;
    size_t pos;
    rt_json_tok_type_t current_type;
    char *str_buf;
    size_t str_buf_len;
    size_t str_buf_cap;
    double num_value;
    int8_t bool_value;
    int64_t depth;
    char *error_msg;
    int8_t expect_key;
    int8_t in_object[MAX_DEPTH];
    int8_t first_value[MAX_DEPTH];
} rt_json_stream_impl;

/// @brief GC finalizer — release the input ref, scratch string buffer, and error string.
/// @details The streaming parser holds a reference to its input string
///          (`input_owner`) so the underlying bytes don't disappear
///          mid-iteration. Two heap-allocated scratches (`str_buf`
///          for value accumulation, `error_msg` for the most recent
///          error) are freed here. All pointers nulled afterwards
///          so a double finalize is safe.
static void stream_finalizer(void *obj) {
    rt_json_stream_impl *s = (rt_json_stream_impl *)obj;
    if (s) {
        if (s->input_owner) {
            rt_string_unref(s->input_owner);
            s->input_owner = NULL;
        }
        free(s->str_buf);
        s->str_buf = NULL;
        free(s->error_msg);
        s->error_msg = NULL;
    }
}

/// @brief Advance the cursor past any RFC 8259 whitespace (`space`, `tab`, `\n`, `\r`).
static void skip_whitespace(rt_json_stream_impl *s) {
    while (s->pos < s->len) {
        char c = s->input[s->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            s->pos++;
        else
            break;
    }
}

/// @brief Skip whitespace and return the next non-whitespace byte (`\0` at EOF).
/// @details Combines whitespace skipping with peeking — most state-
///          machine transitions need both, so this saves a separate
///          call at every dispatch point.
static char peek(rt_json_stream_impl *s) {
    skip_whitespace(s);
    if (s->pos >= s->len)
        return '\0';
    return s->input[s->pos];
}

/// @brief Record a parse error: switch token type to ERROR and stash a message string.
/// @details Frees any previously stored message before strdup-style
///          duplicating the new one. On allocation failure for the
///          message copy, the type still flips to ERROR but the
///          message becomes NULL — so callers should not assume a
///          non-NULL `error_msg` whenever `current_type == ERROR`.
static void set_error(rt_json_stream_impl *s, const char *msg) {
    s->current_type = RT_JSON_TOK_ERROR;
    free(s->error_msg);
    s->error_msg = NULL;
    if (msg) {
        size_t len = strlen(msg);
        s->error_msg = (char *)malloc(len + 1);
        if (s->error_msg) {
            memcpy(s->error_msg, msg, len + 1);
        }
    }
}

/// @brief Reset the value-accumulator buffer to empty (without freeing capacity).
static void str_buf_clear(rt_json_stream_impl *s) {
    s->str_buf_len = 0;
}

/// @brief Append one byte to the value buffer, growing capacity when needed.
/// @details Initial allocation grows from 0 to ≥64; subsequent growth
///          doubles. Records an error and bails on allocation failure
///          rather than crashing — the caller's next `peek` will
///          observe `current_type = ERROR`.
static void str_buf_push(rt_json_stream_impl *s, char c) {
    if (s->str_buf_len + 1 >= s->str_buf_cap) {
        size_t new_cap = s->str_buf_cap * 2;
        if (new_cap < 64)
            new_cap = 64;
        char *new_buf = (char *)realloc(s->str_buf, new_cap);
        if (!new_buf) {
            set_error(s, "out of memory");
            return;
        }
        s->str_buf = new_buf;
        s->str_buf_cap = new_cap;
    }
    s->str_buf[s->str_buf_len++] = c;
    s->str_buf[s->str_buf_len] = '\0';
}

/// @brief Parse exactly four hex digits into a 16-bit codepoint (used by `\uXXXX` escapes).
/// @details Accepts both upper and lower case hex. Returns 0 on EOF
///          mid-sequence or any non-hex byte; advances the cursor by
///          exactly 4 on success.
static int parse_hex4(rt_json_stream_impl *s, uint32_t *out) {
    uint32_t val = 0;
    for (int i = 0; i < 4; i++) {
        if (s->pos >= s->len)
            return 0;
        char c = s->input[s->pos++];
        val <<= 4;
        if (c >= '0' && c <= '9')
            val |= (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            val |= (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            val |= (uint32_t)(c - 'A' + 10);
        else
            return 0;
    }
    *out = val;
    return 1;
}

/// @brief Append `cp` (a Unicode scalar) into the value buffer as 1–4 UTF-8 bytes.
/// @details Standard UTF-8 layout:
///          - U+0000..U+007F → 1 byte (`0xxxxxxx`)
///          - U+0080..U+07FF → 2 bytes (`110xxxxx 10xxxxxx`)
///          - U+0800..U+FFFF → 3 bytes (`1110xxxx 10xxxxxx ×2`)
///          - U+10000..U+10FFFF → 4 bytes (`11110xxx 10xxxxxx ×3`)
///          Caller is expected to have already resolved surrogate
///          pairs to a single scalar.
static void encode_utf8(rt_json_stream_impl *s, uint32_t cp) {
    if (cp < 0x80) {
        str_buf_push(s, (char)cp);
    } else if (cp < 0x800) {
        str_buf_push(s, (char)(0xC0 | (cp >> 6)));
        str_buf_push(s, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        str_buf_push(s, (char)(0xE0 | (cp >> 12)));
        str_buf_push(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        str_buf_push(s, (char)(0x80 | (cp & 0x3F)));
    } else {
        str_buf_push(s, (char)(0xF0 | (cp >> 18)));
        str_buf_push(s, (char)(0x80 | ((cp >> 12) & 0x3F)));
        str_buf_push(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        str_buf_push(s, (char)(0x80 | (cp & 0x3F)));
    }
}

/// @brief Consume a JSON string literal into the value buffer (handles all escapes).
/// @details Steps from the opening `"` through to the matching close
///          quote, decoding every escape sequence:
///          - `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t` → literal byte.
///          - `\uXXXX` → Unicode scalar via `parse_hex4` + `encode_utf8`.
///          - `\uD8XX\uDCXX` → surrogate pair → single supplementary
///            scalar (Plane 1+).
///          On any malformed escape, unterminated string, or
///          incomplete surrogate pair, sets the error flag and returns 0.
static int parse_string_content(rt_json_stream_impl *s) {
    str_buf_clear(s);
    if (s->pos >= s->len || s->input[s->pos] != '"') {
        set_error(s, "expected '\"'");
        return 0;
    }
    s->pos++; /* skip opening quote */

    while (s->pos < s->len) {
        char c = s->input[s->pos++];
        if (c == '"')
            return 1;
        if (c == '\\') {
            if (s->pos >= s->len) {
                set_error(s, "unterminated escape");
                return 0;
            }
            char esc = s->input[s->pos++];
            switch (esc) {
                case '"':
                    str_buf_push(s, '"');
                    break;
                case '\\':
                    str_buf_push(s, '\\');
                    break;
                case '/':
                    str_buf_push(s, '/');
                    break;
                case 'b':
                    str_buf_push(s, '\b');
                    break;
                case 'f':
                    str_buf_push(s, '\f');
                    break;
                case 'n':
                    str_buf_push(s, '\n');
                    break;
                case 'r':
                    str_buf_push(s, '\r');
                    break;
                case 't':
                    str_buf_push(s, '\t');
                    break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parse_hex4(s, &cp)) {
                        set_error(s, "invalid unicode escape");
                        return 0;
                    }
                    /* Handle surrogate pairs */
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (s->pos + 1 < s->len && s->input[s->pos] == '\\' &&
                            s->input[s->pos + 1] == 'u') {
                            s->pos += 2;
                            uint32_t lo = 0;
                            if (!parse_hex4(s, &lo) || lo < 0xDC00 || lo > 0xDFFF) {
                                set_error(s, "invalid surrogate pair");
                                return 0;
                            }
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    encode_utf8(s, cp);
                    break;
                }
                default:
                    set_error(s, "invalid escape character");
                    return 0;
            }
        } else {
            str_buf_push(s, c);
        }
    }
    set_error(s, "unterminated string");
    return 0;
}

/// @brief Parse a JSON number into `s->num_value`, returning 1 on success.
/// @details Accepts the JSON grammar: optional `-`, integer part
///          (`0` or `1-9` followed by digits), optional fraction
///          (`. digits`), optional exponent (`[eE] [+-]? digits`).
///          The matched span is then handed to `strtod` for the
///          actual conversion. Sets an error and returns 0 on
///          malformed input (leading zero with extra digits is
///          *not* rejected by the cursor walk — `strtod` accepts it,
///          which is a slight deviation from strict RFC 8259).
static int parse_number(rt_json_stream_impl *s) {
    size_t start = s->pos;
    if (s->pos < s->len && s->input[s->pos] == '-')
        s->pos++;
    if (s->pos >= s->len || !isdigit((unsigned char)s->input[s->pos])) {
        set_error(s, "invalid number");
        return 0;
    }
    while (s->pos < s->len && isdigit((unsigned char)s->input[s->pos]))
        s->pos++;
    if (s->pos < s->len && s->input[s->pos] == '.') {
        s->pos++;
        while (s->pos < s->len && isdigit((unsigned char)s->input[s->pos]))
            s->pos++;
    }
    if (s->pos < s->len && (s->input[s->pos] == 'e' || s->input[s->pos] == 'E')) {
        s->pos++;
        if (s->pos < s->len && (s->input[s->pos] == '+' || s->input[s->pos] == '-'))
            s->pos++;
        while (s->pos < s->len && isdigit((unsigned char)s->input[s->pos]))
            s->pos++;
    }

    /* Copy number text and parse */
    size_t nlen = s->pos - start;
    char buf[64];
    if (nlen >= sizeof(buf))
        nlen = sizeof(buf) - 1;
    memcpy(buf, s->input + start, nlen);
    buf[nlen] = '\0';
    s->num_value = strtod(buf, NULL);
    return 1;
}

/// @brief Match a literal byte sequence (`true`, `false`, `null`) at the cursor and advance.
/// @details Returns 1 and steps past the match on success, 0 (cursor
///          unchanged) on EOF before completion or mismatch. The
///          length is passed explicitly so the call site doesn't pay
///          for `strlen` at every literal check.
static int match_literal(rt_json_stream_impl *s, const char *lit, size_t len) {
    if (s->pos + len > s->len)
        return 0;
    if (memcmp(s->input + s->pos, lit, len) != 0)
        return 0;
    s->pos += len;
    return 1;
}

//=============================================================================
// Public API
//=============================================================================

/// @brief Construct a streaming JSON parser positioned at the start of `json`. Returns a
/// GC-managed handle; advance through tokens via `_next` and read values via the type-specific
/// `_string_value` / `_number_value` / `_bool_value` accessors.
void *rt_json_stream_new(rt_string json) {
    rt_json_stream_impl *s =
        (rt_json_stream_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_json_stream_impl));
    if (!s) {
        rt_trap("JsonStream: memory allocation failed");
        return NULL;
    }
    s->input_owner = json ? rt_string_ref(json) : NULL;
    const char *cstr = json ? rt_string_cstr(json) : "";
    s->input = cstr ? cstr : "";
    s->len = json ? (size_t)rt_str_len(json) : 0;
    s->pos = 0;
    s->current_type = RT_JSON_TOK_NONE;
    s->str_buf = NULL;
    s->str_buf_len = 0;
    s->str_buf_cap = 0;
    s->num_value = 0.0;
    s->bool_value = 0;
    s->depth = 0;
    s->error_msg = NULL;
    s->expect_key = 0;
    memset(s->in_object, 0, sizeof(s->in_object));
    memset(s->first_value, 0, sizeof(s->first_value));

    rt_obj_set_finalizer(s, stream_finalizer);
    return s;
}

/// @brief Advance to the next token. Returns the token type (RT_JSON_TOK_* enum). Use the
/// type-specific accessors below to read the value once positioned. Returns RT_JSON_TOK_END
/// at end of input or RT_JSON_TOK_ERROR on parse failure (call `_error` for diagnostic).
int64_t rt_json_stream_next(void *parser) {
    if (!parser)
        return RT_JSON_TOK_ERROR;

    rt_json_stream_impl *s = (rt_json_stream_impl *)parser;
    if (s->current_type == RT_JSON_TOK_ERROR || s->current_type == RT_JSON_TOK_END)
        return s->current_type;

    char c = peek(s);

    /* Handle comma separators */
    if (c == ',') {
        s->pos++;
        c = peek(s);
    }

    /* Handle colon after key */
    if (c == ':') {
        s->pos++;
        c = peek(s);
    }

    if (c == '\0') {
        s->current_type = RT_JSON_TOK_END;
        return RT_JSON_TOK_END;
    }

    /* After object start or comma in object, expect key */
    if (s->depth > 0 && s->in_object[s->depth] && c == '"') {
        if (s->expect_key) {
            if (!parse_string_content(s))
                return RT_JSON_TOK_ERROR;
            s->current_type = RT_JSON_TOK_KEY;
            s->expect_key = 0;
            return RT_JSON_TOK_KEY;
        }
    }

    switch (c) {
        case '{':
            s->pos++;
            s->depth++;
            if (s->depth < MAX_DEPTH) {
                s->in_object[s->depth] = 1;
                s->first_value[s->depth] = 1;
            }
            s->expect_key = 1;
            s->current_type = RT_JSON_TOK_OBJECT_START;
            return RT_JSON_TOK_OBJECT_START;

        case '}':
            s->pos++;
            if (s->depth > 0) {
                s->in_object[s->depth] = 0;
                s->depth--;
            }
            s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
            s->current_type = RT_JSON_TOK_OBJECT_END;
            return RT_JSON_TOK_OBJECT_END;

        case '[':
            s->pos++;
            s->depth++;
            if (s->depth < MAX_DEPTH) {
                s->in_object[s->depth] = 0;
                s->first_value[s->depth] = 1;
            }
            s->expect_key = 0;
            s->current_type = RT_JSON_TOK_ARRAY_START;
            return RT_JSON_TOK_ARRAY_START;

        case ']':
            s->pos++;
            if (s->depth > 0) {
                s->in_object[s->depth] = 0;
                s->depth--;
            }
            s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
            s->current_type = RT_JSON_TOK_ARRAY_END;
            return RT_JSON_TOK_ARRAY_END;

        case '"':
            if (!parse_string_content(s))
                return RT_JSON_TOK_ERROR;
            s->current_type = RT_JSON_TOK_STRING;
            s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
            return RT_JSON_TOK_STRING;

        case 't':
            if (match_literal(s, "true", 4)) {
                s->bool_value = 1;
                s->current_type = RT_JSON_TOK_BOOL;
                s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
                return RT_JSON_TOK_BOOL;
            }
            set_error(s, "invalid token");
            return RT_JSON_TOK_ERROR;

        case 'f':
            if (match_literal(s, "false", 5)) {
                s->bool_value = 0;
                s->current_type = RT_JSON_TOK_BOOL;
                s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
                return RT_JSON_TOK_BOOL;
            }
            set_error(s, "invalid token");
            return RT_JSON_TOK_ERROR;

        case 'n':
            if (match_literal(s, "null", 4)) {
                s->current_type = RT_JSON_TOK_NULL;
                s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
                return RT_JSON_TOK_NULL;
            }
            set_error(s, "invalid token");
            return RT_JSON_TOK_ERROR;

        default:
            if (c == '-' || isdigit((unsigned char)c)) {
                if (parse_number(s)) {
                    s->current_type = RT_JSON_TOK_NUMBER;
                    s->expect_key = (s->depth > 0 && s->in_object[s->depth]) ? 1 : 0;
                    return RT_JSON_TOK_NUMBER;
                }
                return RT_JSON_TOK_ERROR;
            }
            set_error(s, "unexpected character");
            return RT_JSON_TOK_ERROR;
    }
}

/// @brief Return the type of the most-recently-consumed token (RT_JSON_TOK_* enum).
int64_t rt_json_stream_token_type(void *parser) {
    if (!parser)
        return RT_JSON_TOK_ERROR;
    return ((rt_json_stream_impl *)parser)->current_type;
}

/// @brief Read the string value at the current STRING / KEY token. Returns the unescaped
/// string content. Trap if called when the current token isn't a string-typed one.
rt_string rt_json_stream_string_value(void *parser) {
    if (!parser)
        return rt_const_cstr("");
    rt_json_stream_impl *s = (rt_json_stream_impl *)parser;
    if (s->str_buf && s->str_buf_len > 0)
        return rt_string_from_bytes(s->str_buf, s->str_buf_len);
    return rt_const_cstr("");
}

/// @brief Read the numeric value at the current NUMBER token (returns 0.0 if not a number).
double rt_json_stream_number_value(void *parser) {
    if (!parser)
        return 0.0;
    return ((rt_json_stream_impl *)parser)->num_value;
}

/// @brief Read the boolean value at the current BOOL token (1 = true, 0 = false / not bool).
int8_t rt_json_stream_bool_value(void *parser) {
    if (!parser)
        return 0;
    return ((rt_json_stream_impl *)parser)->bool_value;
}

/// @brief Current nesting depth (number of open `[`/`{` minus close `]`/`}`). 0 = top level.
int64_t rt_json_stream_depth(void *parser) {
    if (!parser)
        return 0;
    return ((rt_json_stream_impl *)parser)->depth;
}

/// @brief Skip past the current value (including nested arrays/objects). Useful for selectively
/// parsing only certain fields and ignoring large irrelevant subtrees in big JSON documents.
void rt_json_stream_skip(void *parser) {
    if (!parser)
        return;
    rt_json_stream_impl *s = (rt_json_stream_impl *)parser;

    /* If current token is a container start, skip until matching end */
    if (s->current_type == RT_JSON_TOK_OBJECT_START || s->current_type == RT_JSON_TOK_ARRAY_START) {
        int64_t target_depth = s->depth - 1;
        while (s->current_type != RT_JSON_TOK_END && s->current_type != RT_JSON_TOK_ERROR) {
            rt_json_stream_next(parser);
            if ((s->current_type == RT_JSON_TOK_OBJECT_END ||
                 s->current_type == RT_JSON_TOK_ARRAY_END) &&
                s->depth == target_depth)
                return;
        }
    }
    /* Primitive values are already consumed */
}

/// @brief Returns 1 if more tokens remain (i.e., the next `_next` won't immediately return END).
int8_t rt_json_stream_has_next(void *parser) {
    if (!parser)
        return 0;
    rt_json_stream_impl *s = (rt_json_stream_impl *)parser;
    if (s->current_type == RT_JSON_TOK_END || s->current_type == RT_JSON_TOK_ERROR)
        return 0;
    char c = peek(s);
    return c != '\0' ? 1 : 0;
}

/// @brief Return the diagnostic message for the most recent parse error (empty if none).
rt_string rt_json_stream_error(void *parser) {
    if (!parser)
        return rt_const_cstr("");
    rt_json_stream_impl *s = (rt_json_stream_impl *)parser;
    if (s->error_msg)
        return rt_const_cstr(s->error_msg);
    return rt_const_cstr("");
}
