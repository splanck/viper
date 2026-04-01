//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_leveldata.h
// Purpose: JSON-based level loader with tilemap + entity spawn objects.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Load level from JSON file. Returns LevelData object.
void *rt_leveldata_load(void *path);

/// @brief Get the tilemap from a loaded level.
void *rt_leveldata_get_tilemap(void *level);

/// @brief Get the number of objects in the level.
int64_t rt_leveldata_object_count(void *level);

/// @brief Get object type string at index.
rt_string rt_leveldata_object_type(void *level, int64_t index);

/// @brief Get object id string at index.
rt_string rt_leveldata_object_id(void *level, int64_t index);

/// @brief Get object X position at index.
int64_t rt_leveldata_object_x(void *level, int64_t index);

/// @brief Get object Y position at index.
int64_t rt_leveldata_object_y(void *level, int64_t index);

/// @brief Get player start X.
int64_t rt_leveldata_player_start_x(void *level);

/// @brief Get player start Y.
int64_t rt_leveldata_player_start_y(void *level);

/// @brief Get level theme string.
rt_string rt_leveldata_get_theme(void *level);

#ifdef __cplusplus
}
#endif
