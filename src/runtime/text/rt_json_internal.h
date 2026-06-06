//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
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
static inline void parser_skip_whitespace(json_parser *p) {
    while (!parser_eof(p)) {
        char c = parser_peek(p);
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->pos++;
        else
            break;
    }
}
