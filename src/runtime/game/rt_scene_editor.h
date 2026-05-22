//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_scene_editor.h
// Purpose: Scene-owned editable level document primitives for IDE scene tools.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_GAME_SCENE_CLASS_ID INT64_C(-0x510301)

void *rt_game_scene_new(int64_t width, int64_t height, int64_t tile_width, int64_t tile_height);
void *rt_game_scene_load_json(rt_string text);
void *rt_game_scene_load_file(rt_string path);
rt_string rt_game_scene_to_json(void *scene);
int8_t rt_game_scene_save_file(void *scene, rt_string path);
rt_string rt_game_scene_last_error(void *scene);
void *rt_game_scene_diagnostics(void *scene);
void *rt_game_scene_diagnostic_records(void *scene);
int8_t rt_game_scene_has_errors(void *scene);
void rt_game_scene_clear_diagnostics(void *scene);

int64_t rt_game_scene_get_width(void *scene);
int64_t rt_game_scene_get_height(void *scene);
int64_t rt_game_scene_get_tile_width(void *scene);
int64_t rt_game_scene_get_tile_height(void *scene);

int64_t rt_game_scene_add_layer(void *scene, rt_string name);
int64_t rt_game_scene_layer_count(void *scene);
rt_string rt_game_scene_layer_name(void *scene, int64_t layer);
void rt_game_scene_set_layer_name(void *scene, int64_t layer, rt_string name);
int8_t rt_game_scene_layer_visible(void *scene, int64_t layer);
void rt_game_scene_set_layer_visible(void *scene, int64_t layer, int8_t visible);
void rt_game_scene_move_layer(void *scene, int64_t from, int64_t to);
void rt_game_scene_remove_layer(void *scene, int64_t layer);
int64_t rt_game_scene_get_tile(void *scene, int64_t layer, int64_t x, int64_t y);
void rt_game_scene_set_tile(void *scene, int64_t layer, int64_t x, int64_t y, int64_t tile);
void rt_game_scene_fill_tiles(void *scene,
                              int64_t layer,
                              int64_t x,
                              int64_t y,
                              int64_t w,
                              int64_t h,
                              int64_t tile);
void rt_game_scene_set_layer_asset(void *scene, int64_t layer, rt_string asset_path);
rt_string rt_game_scene_layer_asset(void *scene, int64_t layer);

int64_t rt_game_scene_add_object(void *scene,
                                 rt_string type,
                                 rt_string id,
                                 int64_t x,
                                 int64_t y);
int64_t rt_game_scene_object_count(void *scene);
void rt_game_scene_remove_object(void *scene, int64_t index);
rt_string rt_game_scene_object_type(void *scene, int64_t index);
rt_string rt_game_scene_object_id(void *scene, int64_t index);
int64_t rt_game_scene_object_x(void *scene, int64_t index);
int64_t rt_game_scene_object_y(void *scene, int64_t index);
void rt_game_scene_set_object_position(void *scene, int64_t index, int64_t x, int64_t y);
void rt_game_scene_set_object_property(void *scene,
                                       int64_t index,
                                       rt_string key,
                                       rt_string value);
rt_string rt_game_scene_get_object_property(void *scene, int64_t index, rt_string key);
void rt_game_scene_delete_object_property(void *scene, int64_t index, rt_string key);
int64_t rt_game_scene_object_get_int(void *scene, int64_t index, rt_string key, int64_t def);
rt_string rt_game_scene_object_get_str(void *scene, int64_t index, rt_string key, rt_string def);
double rt_game_scene_object_get_float(void *scene, int64_t index, rt_string key, double def);
int8_t rt_game_scene_object_get_bool(void *scene, int64_t index, rt_string key, int8_t def);
int8_t rt_game_scene_object_has(void *scene, int64_t index, rt_string key);
void *rt_game_scene_object_keys(void *scene, int64_t index);
void rt_game_scene_object_set_int(void *scene, int64_t index, rt_string key, int64_t value);
void rt_game_scene_object_set_str(void *scene, int64_t index, rt_string key, rt_string value);
void rt_game_scene_object_set_float(void *scene, int64_t index, rt_string key, double value);
void rt_game_scene_object_set_bool(void *scene, int64_t index, rt_string key, int8_t value);
void rt_game_scene_object_remove(void *scene, int64_t index, rt_string key);
int64_t rt_game_scene_count_of_type(void *scene, rt_string type);
int64_t rt_game_scene_object_of_type(void *scene, rt_string type, int64_t n);
int64_t rt_game_scene_find_object(void *scene, rt_string id);
void rt_game_scene_move_object(void *scene, int64_t from, int64_t to);

void rt_game_scene_set_property(void *scene, rt_string key, rt_string value);
rt_string rt_game_scene_get_property(void *scene, rt_string key);
void rt_game_scene_delete_property(void *scene, rt_string key);
int64_t rt_game_scene_get_int(void *scene, rt_string key, int64_t def);
rt_string rt_game_scene_get_str(void *scene, rt_string key, rt_string def);
double rt_game_scene_get_float(void *scene, rt_string key, double def);
int8_t rt_game_scene_get_bool(void *scene, rt_string key, int8_t def);
int8_t rt_game_scene_has(void *scene, rt_string key);
void rt_game_scene_set_int(void *scene, rt_string key, int64_t value);
void rt_game_scene_set_str(void *scene, rt_string key, rt_string value);
void rt_game_scene_set_float(void *scene, rt_string key, double value);
void rt_game_scene_set_bool(void *scene, rt_string key, int8_t value);
void rt_game_scene_remove(void *scene, rt_string key);
void *rt_game_scene_asset_paths(void *scene);
void *rt_game_scene_asset_descriptors(void *scene);
void *rt_game_scene_build_tilemap(void *scene);

#ifdef __cplusplus
}
#endif
