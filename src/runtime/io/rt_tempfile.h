//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_tempfile.h
// Purpose: Temporary file utilities that auto-cleanup.
// Key invariants: Temp files are created in system temp directory.
// Ownership/Lifetime: Returned paths are newly allocated.
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

    /// @brief Get the system temporary directory path.
    /// @return Path to system temp directory.
    rt_string rt_tempfile_dir(void);

    /// @brief Create a unique temporary file path.
    /// @details Generates a unique filename in the temp directory.
    ///          Does NOT create the file - just generates the path.
    /// @return Unique temporary file path.
    rt_string rt_tempfile_path(void);

    /// @brief Create a unique temporary file path with prefix.
    /// @details Generates a unique filename with given prefix.
    /// @param prefix Prefix for the filename (e.g., "myapp_").
    /// @return Unique temporary file path.
    rt_string rt_tempfile_path_with_prefix(rt_string prefix);

    /// @brief Create a unique temporary file path with prefix and extension.
    /// @details Generates a unique filename with given prefix and extension.
    /// @param prefix Prefix for the filename (e.g., "myapp_").
    /// @param extension File extension (e.g., ".txt").
    /// @return Unique temporary file path.
    rt_string rt_tempfile_path_with_ext(rt_string prefix, rt_string extension);

    /// @brief Create an empty temporary file and return its path.
    /// @details Creates the file on disk (empty).
    /// @return Path to the created temporary file.
    rt_string rt_tempfile_create(void);

    /// @brief Create an empty temporary file with prefix.
    /// @param prefix Prefix for the filename.
    /// @return Path to the created temporary file.
    rt_string rt_tempfile_create_with_prefix(rt_string prefix);

    /// @brief Create a unique temporary directory.
    /// @details Creates an empty directory in the temp directory.
    /// @return Path to the created temporary directory.
    rt_string rt_tempdir_create(void);

    /// @brief Create a unique temporary directory with prefix.
    /// @param prefix Prefix for the directory name.
    /// @return Path to the created temporary directory.
    rt_string rt_tempdir_create_with_prefix(rt_string prefix);

#ifdef __cplusplus
}
#endif
