//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_glob.h
// Purpose: File glob pattern matching supporting *, **, and ? wildcards, returning matching file paths as a Seq.
//
// Key invariants:
//   - Supports *, **, and ? wildcards with standard glob semantics.
//   - ** matches across directory separators (recursive).
//   - Patterns are matched against absolute or relative paths consistently.
//   - Returns an empty Seq (not NULL) when no files match.
//
// Ownership/Lifetime:
//   - Returned Seq objects are newly allocated; caller is responsible for releasing.
//   - String paths within the Seq are also newly allocated.
//
// Links: src/runtime/io/rt_glob.c (implementation), src/runtime/core/rt_string.h
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
