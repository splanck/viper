//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_toml.h
// Purpose: TOML (Tom's Obvious Minimal Language) configuration parser.
// Key invariants: Parses basic TOML key-value pairs, sections, arrays.
//                 Returns nested Map structure.
// Ownership/Lifetime: Returns new Map objects. Caller manages.
// Links: docs/viperlib.md
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
