//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_json.h
// Purpose: JSON parsing and formatting utilities.
// Key invariants: Handles all JSON types (null, bool, number, string, array, object).
// Ownership/Lifetime: Returned Seq/Map and strings are newly allocated.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Parse a JSON string into a Viper value.
    /// @param text JSON text to parse.
    /// @return Parsed value: Map (object), Seq (array), String, or boxed number/bool/null.
    /// @details Parses any valid JSON value. Arrays become Seq, objects become Map,
    ///          strings stay as String, numbers become boxed f64, bools become boxed i64.
    /// @note Traps on invalid JSON with descriptive error message.
    void *rt_json_parse(rt_string text);

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

    /// @brief Format a Viper value as compact JSON.
    /// @param obj Value to format (Map, Seq, String, boxed number/bool, or null).
    /// @return Newly allocated JSON string.
    /// @details Produces compact JSON without extra whitespace.
    rt_string rt_json_format(void *obj);

    /// @brief Format a Viper value as pretty-printed JSON.
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
