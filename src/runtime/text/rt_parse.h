//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_parse.h
// Purpose: Safe parsing functions for Viper.Parse providing TryXxx, XxxOr, and IsXxx variants that
// never trap on invalid input and handle all edge cases gracefully.
//
// Key invariants:
//   - TryXxx functions store the parsed value at a pointer and return true on success.
//   - XxxOr functions return the parsed value or a caller-specified default on failure.
//   - IsXxx functions return true when the string is a valid representation of the type.
//   - None of these functions trap or have undefined behavior on invalid input.
//
// Ownership/Lifetime:
//   - No heap allocation; all functions are pure parsing utilities.
//   - Input strings are borrowed; callers retain ownership.
//
// Links: src/runtime/text/rt_parse.c (implementation), src/runtime/core/rt_string.h
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
