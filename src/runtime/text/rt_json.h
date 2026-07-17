//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_json.h
// Purpose: JSON parsing and formatting utilities for Zanna.Data.Json, handling all JSON types
// (null, bool, number, string, array, object) and producing pretty-printed or compact output.
//
// Key invariants:
//   - Parses all standard JSON types: null, boolean, number, string, array, object.
//   - Numbers are stored as f64; integer parsing preserves exact values up to 53 bits.
//   - JSON null is represented by a NULL value; invalid JSON is reported by trap
//     or by the status result from rt_json_try_parse.
//   - rt_json_format produces compact JSON; rt_json_pretty produces indented output.
//
// Ownership/Lifetime:
//   - Returned Seq, Map, and string objects are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of parsing.
//
// Links: src/runtime/text/rt_json.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Parse a JSON string into a Zanna value.
/// @param text JSON text to parse.
/// @return Parsed value: Map (object), Seq (array), String, boxed number/bool, or NULL for JSON
/// null.
/// @details Parses any valid JSON value. Arrays become Seq, objects become Map,
///          strings stay as String, numbers become boxed f64, bools become boxed i1.
/// @note Traps on invalid JSON with descriptive error message; use rt_json_try_parse
///       when the caller needs to distinguish JSON null from a syntax failure without trapping.
void *rt_json_parse(rt_string text);

/// @brief Parse JSON without trapping on syntax errors.
/// @param text JSON text to parse.
/// @param out_value Receives the parsed value on success. JSON null is reported
///                  as `*out_value == NULL`; the integer return value still indicates success.
/// @param out_message Receives an owned diagnostic string on failure when non-null.
/// @param out_line Receives 1-based source line on failure when non-null.
/// @param out_column Receives 1-based source column on failure when non-null.
/// @return 1 on success, 0 on invalid input or syntax error.
int8_t rt_json_try_parse(rt_string text,
                         void **out_value,
                         rt_string *out_message,
                         int64_t *out_line,
                         int64_t *out_column);

/// @brief Parse JSON expecting an object at the root.
/// @param text JSON text to parse.
/// @return Newly allocated Map containing the object properties.
/// @note Traps if the root is not an object.
void *rt_json_parse_object(rt_string text);

/// @brief Parse JSON expecting an array at the root.
/// @param text JSON text to parse.
/// @return Newly allocated Seq containing the array elements.
/// @note Traps if the root is not an array.
void *rt_json_parse_array(rt_string text);

/// @brief Format a Zanna value as compact JSON.
/// @param obj Value to format (Map, Seq, String, boxed number/bool, or null).
/// @return Newly allocated JSON string.
/// @details Produces compact JSON without extra whitespace.
rt_string rt_json_format(void *obj);

/// @brief Format a Zanna value as pretty-printed JSON.
/// @param obj Value to format (Map, Seq, String, boxed number/bool, or null).
/// @param indent Number of spaces per indentation level (typically 2 or 4).
/// @return Newly allocated JSON string with indentation and newlines.
rt_string rt_json_format_pretty(void *obj, int64_t indent);

/// @brief Check if a string contains valid JSON.
/// @param text String to validate.
/// @return 1 if valid JSON, 0 otherwise.
/// @note Does not allocate; only validates syntax.
int8_t rt_json_is_valid(rt_string text);

/// @brief Get the JSON type of a parsed value.
/// @param obj Parsed JSON value.
/// @return String describing the type: "null", "bool", "number", "string", "array", "object".
rt_string rt_json_type_of(void *obj);

#ifdef __cplusplus
}
#endif
