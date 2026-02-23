//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_toml.h
// Purpose: TOML (Tom's Obvious Minimal Language) configuration file parser returning a nested Map structure representing keys, sections, and arrays.
//
// Key invariants:
//   - Parses basic TOML: key-value pairs, sections ([table]), arrays, inline tables.
//   - Returns a Map where section names are keys and values are Maps.
//   - Top-level key-value pairs appear in the root Map.
//   - Returns NULL on invalid TOML syntax.
//
// Ownership/Lifetime:
//   - Returned Map objects are newly allocated; caller must release.
//   - Input strings are borrowed for the duration of parsing.
//
// Links: src/runtime/text/rt_toml.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Parse a TOML string into a Map of Maps.
    /// @param src TOML content string.
    /// @return Map object with string keys and values, or NULL on error.
    void *rt_toml_parse(rt_string src);

    /// @brief Check if a string is valid TOML.
    /// @param src TOML content string.
    /// @return 1 if valid, 0 if not.
    int8_t rt_toml_is_valid(rt_string src);

    /// @brief Format a Map as a TOML string.
    /// @param map Map object (string keys, string/Map values).
    /// @return TOML formatted string.
    rt_string rt_toml_format(void *map);

    /// @brief Get a value from parsed TOML using dotted key path.
    /// @param root Root Map from rt_toml_parse, or a raw TOML string.
    /// @param key_path Dotted key path (e.g., "server.host").
    /// @return Value at path, or NULL if not found.
    void *rt_toml_get(void *root, rt_string key_path);

    /// @brief Get a string value from TOML using dotted key path.
    /// @param root Root Map from rt_toml_parse, or a raw TOML string.
    /// @param key_path Dotted key path (e.g., "server.host").
    /// @return String value, or empty string if not found.
    rt_string rt_toml_get_str(void *root, rt_string key_path);

#ifdef __cplusplus
}
#endif
