//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_savedata.h
// Purpose: Cross-platform game save/load persistence system. Provides a
//   key-value store (string keys → int64/string values) that serializes
//   to JSON in platform-appropriate directories.
//
// Key invariants:
//   - Keys are unique; setting a key overwrites any previous value.
//   - Save/Load are whole-file operations (atomic write, full read).
//   - File path is computed from game name using platform conventions.
//
// Ownership/Lifetime:
//   - SaveData objects are GC-managed via rt_obj_new_i64 with a finalizer.
//   - Internal strings (keys, string values) are heap-allocated with strdup.
//
// Links: src/runtime/io/rt_savedata.c (implementation),
//        src/runtime/io/rt_dir.h (directory creation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a new SaveData instance for the given game name.
/// @param game_name Identifier used to compute the save file path.
/// @return Opaque SaveData handle.
void *rt_savedata_new(rt_string game_name);

/// @brief Store an integer value.
void rt_savedata_set_int(void *sd, rt_string key, int64_t value);

/// @brief Store a string value.
void rt_savedata_set_string(void *sd, rt_string key, rt_string value);

/// @brief Retrieve an integer value, or default if key not found.
int64_t rt_savedata_get_int(void *sd, rt_string key, int64_t default_val);

/// @brief Retrieve a string value, or default if key not found.
rt_string rt_savedata_get_string(void *sd, rt_string key, rt_string default_val);

/// @brief Write all key-value pairs to the save file.
/// @return 1 on success, 0 on failure.
int8_t rt_savedata_save(void *sd);

/// @brief Load key-value pairs from the save file, merging into current data.
/// @return 1 on success, 0 if file not found or parse error.
int8_t rt_savedata_load(void *sd);

/// @brief Check if a key exists.
int8_t rt_savedata_has_key(void *sd, rt_string key);

/// @brief Remove a key and its value.
/// @return 1 if removed, 0 if not found.
int8_t rt_savedata_remove(void *sd, rt_string key);

/// @brief Remove all key-value pairs.
void rt_savedata_clear(void *sd);

/// @brief Get the number of stored key-value pairs.
int64_t rt_savedata_count(void *sd);

/// @brief Get the computed save file path.
rt_string rt_savedata_get_path(void *sd);

#ifdef __cplusplus
}
#endif
