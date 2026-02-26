//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_json_stream.h
// Purpose: SAX-style streaming JSON parser for large or incremental JSON data, providing a
// pull-based token stream over a JSON string without building an in-memory tree.
//
// Key invariants:
//   - Stateful, pull-based: call rt_json_stream_next to advance to the next token.
//   - Token types: object_start, object_end, array_start, array_end, key, string, number, bool,
//   null.
//   - No memory allocation for the token stream itself; caller reads token data inline.
//   - Returns an error token on malformed JSON; does not trap.
//
// Ownership/Lifetime:
//   - Stream objects are heap-allocated; caller must free after use.
//   - Input string is borrowed for the lifetime of the stream.
//
// Links: src/runtime/text/rt_json_stream.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Token types emitted by the streaming parser.
    typedef enum
    {
        RT_JSON_TOK_NONE = 0,
        RT_JSON_TOK_OBJECT_START = 1,
        RT_JSON_TOK_OBJECT_END = 2,
        RT_JSON_TOK_ARRAY_START = 3,
        RT_JSON_TOK_ARRAY_END = 4,
        RT_JSON_TOK_KEY = 5,
        RT_JSON_TOK_STRING = 6,
        RT_JSON_TOK_NUMBER = 7,
        RT_JSON_TOK_BOOL = 8,
        RT_JSON_TOK_NULL = 9,
        RT_JSON_TOK_ERROR = 10,
        RT_JSON_TOK_END = 11
    } rt_json_tok_type;

    /// @brief Create a new streaming JSON parser from a string.
    /// @param json JSON string to parse.
    /// @return Opaque parser handle.
    void *rt_json_stream_new(rt_string json);

    /// @brief Advance to the next token.
    /// @param parser Parser handle.
    /// @return Token type of the next token.
    int64_t rt_json_stream_next(void *parser);

    /// @brief Get the current token type.
    /// @param parser Parser handle.
    /// @return Current token type as int64_t.
    int64_t rt_json_stream_token_type(void *parser);

    /// @brief Get the current token's string value (for key, string tokens).
    /// @param parser Parser handle.
    /// @return String value, or empty string if not applicable.
    rt_string rt_json_stream_string_value(void *parser);

    /// @brief Get the current token's numeric value (for number tokens).
    /// @param parser Parser handle.
    /// @return Double value, or 0.0 if not a number token.
    double rt_json_stream_number_value(void *parser);

    /// @brief Get the current token's boolean value (for bool tokens).
    /// @param parser Parser handle.
    /// @return 1 for true, 0 for false.
    int8_t rt_json_stream_bool_value(void *parser);

    /// @brief Get the current nesting depth.
    /// @param parser Parser handle.
    /// @return Nesting depth (0 = top level).
    int64_t rt_json_stream_depth(void *parser);

    /// @brief Skip the current value (object, array, or primitive).
    /// @param parser Parser handle.
    void rt_json_stream_skip(void *parser);

    /// @brief Check if the parser has more tokens.
    /// @param parser Parser handle.
    /// @return 1 if more tokens, 0 if at end.
    int8_t rt_json_stream_has_next(void *parser);

    /// @brief Get error message if current token is ERROR.
    /// @param parser Parser handle.
    /// @return Error description, or empty string if no error.
    rt_string rt_json_stream_error(void *parser);

#ifdef __cplusplus
}
#endif
