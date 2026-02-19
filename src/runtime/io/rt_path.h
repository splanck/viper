//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_path.h
// Purpose: Cross-platform file path manipulation utilities for Viper.IO.Path.
// Key invariants: All functions return newly allocated runtime strings.
// Ownership/Lifetime: Caller must release returned strings.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Join two path components with the platform separator.
    /// @param a First path component.
    /// @param b Second path component.
    /// @return Newly allocated joined path.
    /// @details If b is absolute, returns b. Otherwise joins with platform separator.
    rt_string rt_path_join(rt_string a, rt_string b);

    /// @brief Get the directory portion of a path.
    /// @param path Path to extract directory from.
    /// @return Newly allocated directory path, or "." if no directory component.
    /// @details For "/foo/bar/baz.txt" returns "/foo/bar".
    ///          For "baz.txt" returns ".".
    rt_string rt_path_dir(rt_string path);

    /// @brief Get the filename portion of a path.
    /// @param path Path to extract filename from.
    /// @return Newly allocated filename string.
    /// @details For "/foo/bar/baz.txt" returns "baz.txt".
    rt_string rt_path_name(rt_string path);

    /// @brief Get the filename without extension.
    /// @param path Path to extract stem from.
    /// @return Newly allocated stem string.
    /// @details For "/foo/bar/baz.txt" returns "baz".
    ///          For ".hidden" returns ".hidden" (no extension).
    rt_string rt_path_stem(rt_string path);

    /// @brief Get the file extension including the dot.
    /// @param path Path to extract extension from.
    /// @return Newly allocated extension string (e.g., ".txt"), or empty if none.
    /// @details For "/foo/bar/baz.txt" returns ".txt".
    ///          For ".hidden" returns "" (leading dot is not an extension).
    rt_string rt_path_ext(rt_string path);

    /// @brief Replace the extension of a path.
    /// @param path Original path.
    /// @param new_ext New extension (with or without leading dot).
    /// @return Newly allocated path with new extension.
    /// @details For "/foo/bar.txt" with ".md" returns "/foo/bar.md".
    rt_string rt_path_with_ext(rt_string path, rt_string new_ext);

    /// @brief Check if a path is absolute.
    /// @param path Path to check.
    /// @return 1 if absolute, 0 if relative.
    /// @details On Unix: starts with "/".
    ///          On Windows: starts with drive letter (C:\) or UNC path (\\server).
    int64_t rt_path_is_abs(rt_string path);

    /// @brief Convert a relative path to absolute.
    /// @param path Path to convert.
    /// @return Newly allocated absolute path.
    /// @details Prepends current working directory and normalizes.
    rt_string rt_path_abs(rt_string path);

    /// @brief Normalize a path by removing redundant separators and resolving . and ..
    /// @param path Path to normalize.
    /// @return Newly allocated normalized path.
    /// @details Removes "." components, resolves ".." where possible,
    ///          collapses multiple separators, returns "." for empty result.
    rt_string rt_path_norm(rt_string path);

    /// @brief Get the platform-specific path separator.
    /// @return Newly allocated string containing "/" (Unix) or "\" (Windows).
    rt_string rt_path_sep(void);

#ifdef __cplusplus
}
#endif
