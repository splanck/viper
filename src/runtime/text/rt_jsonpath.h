//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_jsonpath.h
// Purpose: JSONPath-like query expressions for navigating JSON objects.
// Key invariants: Supports dotted access, bracket notation, array indexing.
//                 Works with rt_json parsed objects (maps/seqs).
// Ownership/Lifetime: Returns new strings/objects. Caller manages.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Query a JSON object using a path expression.
    /// @param root Parsed JSON object (map or seq).
    /// @param path Path expression (e.g., "user.name", "items[0].id").
    /// @return The value at the path, or NULL if not found.
    void *rt_jsonpath_get(void *root, rt_string path);

    /// @brief Query a JSON object, returning a default if not found.
    /// @param root Parsed JSON object.
    /// @param path Path expression.
    /// @param def Default value to return if not found.
    /// @return The value at the path, or def if not found.
    void *rt_jsonpath_get_or(void *root, rt_string path, void *def);

    /// @brief Check if a path exists in the JSON object.
    /// @param root Parsed JSON object.
    /// @param path Path expression.
    /// @return 1 if the path exists, 0 otherwise.
    int8_t rt_jsonpath_has(void *root, rt_string path);

    /// @brief Get all values matching a wildcard path.
    /// @param root Parsed JSON object.
    /// @param path Path with wildcard (e.g., "users.*.name").
    /// @return Seq of matching values.
    void *rt_jsonpath_query(void *root, rt_string path);

    /// @brief Get string value at path, or empty string if not found.
    /// @param root Parsed JSON object.
    /// @param path Path expression.
    /// @return String value at path.
    rt_string rt_jsonpath_get_str(void *root, rt_string path);

    /// @brief Get integer value at path, or 0 if not found.
    /// @param root Parsed JSON object.
    /// @param path Path expression.
    /// @return Integer value at path.
    int64_t rt_jsonpath_get_int(void *root, rt_string path);

#ifdef __cplusplus
}
#endif
