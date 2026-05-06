//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_yaml.h
// Purpose: YAML 1.2 subset parser and formatter for Viper.Data.Yaml.
//          Supports scalars (string/int/float/bool/null), block and flow
//          sequences and mappings, and multi-document streams.
// Key invariants:
//   - Parses the YAML 1.2 core schema; unrecognised constructs produce an error.
//   - rt_yaml_parse returns NULL both for YAML null and for invalid syntax;
//     invalid syntax records an error retrievable via rt_yaml_error().
//   - Multi-document streams (separated by '---') return a Seq of documents.
//   - Formatter always emits block-style collections and quotes ambiguous
//     scalars so output is guaranteed to round-trip through the parser.
// Ownership/Lifetime:
//   - All returned objects are newly heap-allocated; caller must release.
//   - Input rt_string values are borrowed for the duration of the call.
// Links: src/runtime/text/rt_yaml.c (implementation),
//        src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// YAML Parsing
//=========================================================================

/// @brief Parse YAML string into a Viper value.
/// @param text YAML text to parse.
/// @return Parsed value: Map (mapping), Seq (sequence), String, boxed number/bool, or NULL.
/// @note YAML null and empty documents return NULL with no error. Invalid YAML returns NULL and
///       records an error.
void *rt_yaml_parse(rt_string text);

/// @brief Get the last parse error message.
/// @return Error message string, or empty string if no error.
rt_string rt_yaml_error(void);

/// @brief Check if a string contains valid YAML.
/// @param text String to validate.
/// @return 1 if valid YAML, 0 otherwise.
int8_t rt_yaml_is_valid(rt_string text);

//=========================================================================
// YAML Formatting
//=========================================================================

/// @brief Format a Viper value as YAML.
/// @param obj Value to format (Map, Seq, String, boxed number/bool, or null).
/// @return Newly allocated YAML string.
rt_string rt_yaml_format(void *obj);

/// @brief Format a Viper value as YAML with custom indentation.
/// @param obj Value to format.
/// @param indent Number of spaces per indentation level (typically 2).
/// @return Newly allocated YAML string.
rt_string rt_yaml_format_indent(void *obj, int64_t indent);

//=========================================================================
// Type Inspection
//=========================================================================

/// @brief Get the YAML type of a parsed value.
/// @param obj Parsed YAML value.
/// @return String describing the type: "null", "bool", "int", "float", "string", "sequence",
/// "mapping".
rt_string rt_yaml_type_of(void *obj);

#ifdef __cplusplus
}
#endif
