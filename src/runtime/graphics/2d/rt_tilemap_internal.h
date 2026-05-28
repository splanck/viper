//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_tilemap_internal.h
// Purpose: Shared internal tilemap state for rendering, metadata, and I/O.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#define MAX_TILE_COLLISION_IDS 4096
#define TM_MAX_LAYERS 16
#define TM_MAX_TILE_ANIMS 64
#define TM_MAX_ANIM_FRAMES 8
#define MAX_TILE_PROPS 256
#define MAX_PROP_KEYS 8
#define MAX_PROP_KEY_LEN 32
#define MAX_AUTOTILE_RULES 64

typedef struct {
    char key[MAX_PROP_KEY_LEN];
    int64_t value;
} tile_prop_entry;

typedef struct {
    tile_prop_entry entries[MAX_PROP_KEYS];
    int32_t count;
} tile_props;

typedef struct {
    int64_t base_tile;
    int64_t variants[16];
    int8_t active;
} autotile_rule;

typedef struct {
    int64_t *tiles;
    void *tileset;
    int64_t tileset_cols;
    int64_t tileset_rows;
    int64_t tile_count;
    char name[32];
    int8_t visible;
    int8_t owns_tiles;
} tm_layer;

typedef struct {
    int64_t base_tile_id;
    int64_t frame_tiles[TM_MAX_ANIM_FRAMES];
    int32_t frame_count;
    int64_t ms_per_frame;
    int64_t timer;
    int32_t current_frame;
} tm_tile_anim;

typedef struct rt_tilemap_impl {
    int64_t width;
    int64_t height;
    int64_t tile_width;
    int64_t tile_height;
    int64_t tileset_cols;
    int64_t tileset_rows;
    int64_t tile_count;
    void *tileset;
    int64_t *tiles;
    int8_t collision[MAX_TILE_COLLISION_IDS];
    tm_layer layers[TM_MAX_LAYERS];
    int32_t layer_count;
    int32_t collision_layer;
    tm_tile_anim tile_anims[TM_MAX_TILE_ANIMS];
    int32_t tile_anim_count;
    tile_props tile_props[MAX_TILE_PROPS];
    autotile_rule autotile_rules[MAX_AUTOTILE_RULES];
    int32_t autotile_count;
} rt_tilemap_impl;
