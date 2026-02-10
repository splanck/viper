//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_parse.h
// Purpose: Safe parsing functions for Viper.Parse namespace.
//
// Safe Parsing Philosophy:
// - TryXxx: Parse and store result at pointer, return true/false
// - XxxOr: Return parsed value or default on failure
// - IsXxx: Return true if string is valid for type
//
// These functions never trap or throw - they handle invalid input gracefully.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Try to parse an integer from string, storing result at out_value.
    /// @param s Input string to parse.
    /// @param out_value Pointer to store parsed integer (must not be NULL).
    /// @return 1 if parsing succeeded, 0 otherwise.
    int8_t rt_parse_try_int(rt_string s, int64_t *out_value);

    /// @brief Try to parse a number from string, storing result at out_value.
    /// @param s Input string to parse.
    /// @param out_value Pointer to store parsed double (must not be NULL).
    /// @return 1 if parsing succeeded, 0 otherwise.
    int8_t rt_parse_try_num(rt_string s, double *out_value);

    /// @brief Try to parse a boolean from string, storing result at out_value.
    /// @param s Input string to parse (true/false/yes/no/1/0, case insensitive).
    /// @param out_value Pointer to store parsed boolean (must not be NULL).
    /// @return 1 if parsing succeeded, 0 otherwise.
    int8_t rt_parse_try_bool(rt_string s, int8_t *out_value);

    /// @brief Parse an integer from string, returning default on failure.
    /// @param s Input string to parse.
    /// @param default_value Value to return if parsing fails.
    /// @return Parsed integer or default_value.
    int64_t rt_parse_int_or(rt_string s, int64_t default_value);

    /// @brief Parse a number from string, returning default on failure.
    /// @param s Input string to parse.
    /// @param default_value Value to return if parsing fails.
    /// @return Parsed double or default_value.
    double rt_parse_num_or(rt_string s, double default_value);

    /// @brief Parse a boolean from string, returning default on failure.
    /// @param s Input string to parse (true/false/yes/no/1/0, case insensitive).
    /// @param default_value Value to return if parsing fails.
    /// @return Parsed boolean or default_value.
    int8_t rt_parse_bool_or(rt_string s, int8_t default_value);

    /// @brief Check if string represents a valid integer.
    /// @param s Input string to check.
    /// @return 1 if string can be parsed as an integer, 0 otherwise.
    int8_t rt_parse_is_int(rt_string s);

    /// @brief Check if string represents a valid number.
    /// @param s Input string to check.
    /// @return 1 if string can be parsed as a number, 0 otherwise.
    int8_t rt_parse_is_num(rt_string s);

    /// @brief Parse an integer with specified radix, returning default on failure.
    /// @param s Input string to parse.
    /// @param radix Base for parsing (2-36).
    /// @param default_value Value to return if parsing fails or radix is invalid.
    /// @return Parsed integer or default_value.
    int64_t rt_parse_int_radix(rt_string s, int64_t radix, int64_t default_value);

#ifdef __cplusplus
}
#endif
