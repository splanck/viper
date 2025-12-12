//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_dir.h
// Purpose: Cross-platform directory operations for Viper.IO.Dir.
// Key invariants: Directory operations are platform-independent, List/Files/Dirs
//                 return Seq objects that must be released by the caller.
// Ownership/Lifetime: All functions returning strings or sequences allocate
//                     new objects that the caller must release.
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

    /// @brief Check if a directory exists.
    /// @param path Path to check.
    /// @return 1 if directory exists, 0 otherwise.
    int64_t rt_dir_exists(rt_string path);

    /// @brief Create a single directory.
    /// @param path Directory path to create.
    /// @details Parent directory must exist. Traps if creation fails.
    void rt_dir_make(rt_string path);

    /// @brief Create a directory and all parent directories.
    /// @param path Directory path to create.
    /// @details Creates all intermediate directories as needed.
    void rt_dir_make_all(rt_string path);

    /// @brief Remove an empty directory.
    /// @param path Directory path to remove.
    /// @details Traps if directory is not empty or cannot be removed.
    void rt_dir_remove(rt_string path);

    /// @brief Recursively remove a directory and all its contents.
    /// @param path Directory path to remove.
    /// @details Removes all files and subdirectories, then the directory itself.
    void rt_dir_remove_all(rt_string path);

    /// @brief List all entries in a directory.
    /// @param path Directory path to list.
    /// @return Seq of entry names (excluding . and ..).
    void *rt_dir_list(rt_string path);

    /// @brief List only files in a directory.
    /// @param path Directory path to list.
    /// @return Seq of file names (no subdirectories).
    void *rt_dir_files(rt_string path);

    /// @brief List only subdirectories in a directory.
    /// @param path Directory path to list.
    /// @return Seq of subdirectory names (excluding . and ..).
    void *rt_dir_dirs(rt_string path);

    /// @brief Get the current working directory.
    /// @return Newly allocated string with current directory path.
    rt_string rt_dir_current(void);

    /// @brief Change the current working directory.
    /// @param path New working directory path.
    /// @details Traps if directory does not exist or cannot be accessed.
    void rt_dir_set_current(rt_string path);

    /// @brief Move/rename a directory.
    /// @param src Source directory path.
    /// @param dst Destination directory path.
    /// @details Traps if move fails.
    void rt_dir_move(rt_string src, rt_string dst);

#ifdef __cplusplus
}
#endif
