//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_glob.h
// Purpose: File glob pattern matching (e.g., "*.txt", "src/**/*.cpp").
// Key invariants: Glob patterns support *, **, and ? wildcards.
// Ownership/Lifetime: Returned sequences are newly allocated.
// Links: docs/viperlib/io.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Check if a path matches a glob pattern.
    /// @details Supports wildcards:
    ///          - * matches any sequence of characters except /
    ///          - ** matches any sequence including /
    ///          - ? matches any single character except /
    /// @param path The path to test.
    /// @param pattern The glob pattern (e.g., "*.txt", "src/*.c").
    /// @return 1 if path matches pattern, 0 otherwise.
    int8_t rt_glob_match(rt_string path, rt_string pattern);

    /// @brief Find all files matching a glob pattern in a directory.
    /// @details Searches in the specified directory (non-recursive).
    ///          The pattern is matched against file names only.
    /// @param dir The directory to search in.
    /// @param pattern The glob pattern (e.g., "*.txt").
    /// @return Seq of matching file paths (full paths).
    void *rt_glob_files(rt_string dir, rt_string pattern);

    /// @brief Find all files matching a glob pattern recursively.
    /// @details Searches in the specified directory and all subdirectories.
    ///          The pattern is matched against the relative path from base.
    ///          Supports ** for recursive matching.
    /// @param base The base directory to start searching from.
    /// @param pattern The glob pattern (e.g., "**/*.txt", "src/**/*.c").
    /// @return Seq of matching file paths (full paths).
    void *rt_glob_files_recursive(rt_string base, rt_string pattern);

    /// @brief Find all entries (files and dirs) matching a glob pattern.
    /// @details Searches in the specified directory (non-recursive).
    /// @param dir The directory to search in.
    /// @param pattern The glob pattern (e.g., "*.txt").
    /// @return Seq of matching entry paths (full paths).
    void *rt_glob_entries(rt_string dir, rt_string pattern);

#ifdef __cplusplus
}
#endif
