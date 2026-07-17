//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE in the project root for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_json_internal.h
// Purpose: Shared parser state and cursor primitives for the JSON runtime.
//          Used by the parsing (rt_json_parse.c) and validation
//          (rt_json_validate.c) translation units, which both walk a flat
//          input buffer through the same json_parser cursor.
//
// Key invariants:
//   - Cursor primitives are static inline so each translation unit gets an
//     internal-linkage copy; no external symbol is exported (avoids link-time
//     collisions with the identically named static helpers in rt_yaml.c /
//     rt_xml.c) and no source is duplicated.
//   - JSON_MAX_DEPTH bounds nesting for both parsing and formatting (S-16).
//
// Ownership/Lifetime:
//   - json_parser borrows the input buffer; it owns no heap state.
//
// Links: src/runtime/text/rt_json.h (public API),
//        src/runtime/text/rt_json_parse.c, rt_json_format.c, rt_json_validate.c
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//=============================================================================
// Parser limits
//=============================================================================

/* S-16: Maximum nesting depth before aborting (stack overflow / DoS guard).
 * Shared by the parser (rt_json_parse.c) and the formatter's cycle/depth
 * guard (rt_json_format.c). */
#define JSON_MAX_DEPTH 200

//=============================================================================
// Parser State
//=============================================================================

typedef struct {
    const char *input;
    size_t len;
    size_t pos;
    int depth;          // Current nesting depth
    int depth_exceeded; // S-16: set when depth limit hit (unwinds without trap)
    int trap_errors;
    int has_error;
    char error_message[160];
    int64_t error_line;
    int64_t error_column;
} json_parser;

// ---------------------------------------------------------------------------
// Parser primitives over a flat byte buffer. Tracks nesting depth so callers
// can fail gracefully on adversarial inputs (S-16) instead of blowing the C
// stack via recursion.
// ---------------------------------------------------------------------------

/// @brief Initialise a parser onto fresh input. depth=0, no errors.
static inline void parser_init(json_parser *p, const char *input, size_t len) {
    p->input = input;
    p->len = len;
    p->pos = 0;
    p->depth = 0;
    p->depth_exceeded = 0;
    p->trap_errors = 1;
    p->has_error = 0;
    p->error_message[0] = '\0';
    p->error_line = 0;
    p->error_column = 0;
}

/// @brief True if the cursor has consumed all input bytes.
static inline bool parser_eof(json_parser *p) {
    if (p->has_error)
        return true;
    return p->pos >= p->len;
}

/// @brief Look at the byte under the cursor; returns `\0` at EOF.
static inline char parser_peek(json_parser *p) {
    if (p->has_error)
        return '\0';
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos];
}

/// @brief Consume and return the byte under the cursor.
static inline char parser_consume(json_parser *p) {
    if (p->has_error)
        return '\0';
    if (p->pos >= p->len)
        return '\0';
    return p->input[p->pos++];
}

/// @brief Skip JSON whitespace (space, tab, newline, CR — RFC 8259 §2).
/// @brief Validate one raw (unescaped) UTF-8 sequence beginning at `lead`.
/// @details Shared by the parser, validator, and tokenizer so IsValid/Parse
///          agree byte-for-byte on what constitutes interoperable JSON text.
///          Overlong encodings, surrogate codepoints, and values above
///          U+10FFFF are rejected.
/// @param lead First byte already consumed.
/// @param rest Remaining unconsumed input bytes.
/// @param rest_len Number of bytes available in @p rest.
/// @param extra_out Receives the number of continuation bytes to consume.
/// @return 1 when the sequence is valid, 0 otherwise.
static inline int json_raw_utf8_sequence_valid(unsigned char lead,
                                               const char *rest,
                                               size_t rest_len,
                                               size_t *extra_out) {
    size_t extra = 0;
    uint32_t cp = 0;
    if (extra_out)
        *extra_out = 0;
    if (lead < 0x80)
        return 1;
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        cp = lead & 0x1Fu;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        cp = lead & 0x0Fu;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        cp = lead & 0x07u;
    } else {
        return 0;
    }
    if (rest_len < extra)
        return 0;
    for (size_t i = 0; i < extra; i++) {
        unsigned char ch = (unsigned char)rest[i];
        if ((ch & 0xC0u) != 0x80u)
            return 0;
        cp = (cp << 6) | (uint32_t)(ch & 0x3Fu);
    }
    if ((extra == 2 && cp < 0x800u) || (extra == 3 && cp < 0x10000u))
        return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu)
        return 0;
    if (cp > 0x10FFFFu)
        return 0;
    if (extra_out)
        *extra_out = extra;
    return 1;
}

static inline void parser_skip_whitespace(json_parser *p) {
    while (!parser_eof(p)) {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->pos++;
        else
            break;
    }
}
