//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/2d/rt_tilemap_internal.h
// Purpose: Shared private Tilemap storage for projection, imported artwork,
//   animation, rendering, metadata, and serialization.
//
// Key invariants:
//   - Logical cell dimensions remain independent from imported source frames.
//   - Dynamic animation arrays are owned by the containing Tilemap.
//
// Ownership/Lifetime:
//   - Layer tiles, cloned tilesets, and animation arrays release in the finalizer.
//   - The base tile array remains inline after rt_tilemap_impl.
//
// Links: rt_tilemap.h, rt_tilemap.c, rt_tilemap_io.c,
//   docs/adr/0144-complete-tiled-map-import.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#define MAX_TILE_COLLISION_IDS 4096
#define TM_MAX_LAYERS 16
#define TM_MAX_TILE_ANIMS 64
#define TM_MAX_ANIM_FRAMES 8
#define TM_MAX_IMPORT_ANIM_FRAMES 4096
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
    double import_offset_x;
    double import_offset_y;
    double import_parallax_x;
    double import_parallax_y;
} tm_layer;

typedef struct {
    int64_t base_tile_id;
    int64_t *frame_tiles;
    int64_t *frame_durations;
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
    int64_t source_frame_width;
    int64_t source_frame_height;
    int64_t import_draw_offset_x;
    int64_t import_draw_offset_y;
    int64_t import_origin_tile_x;
    int64_t import_origin_tile_y;
    int64_t import_projection_height;
    int64_t import_hex_side_length;
    int32_t import_orientation;
    int32_t import_render_order;
    int32_t import_stagger_axis;
    int8_t import_stagger_even;
    double import_skew_x;
    double import_skew_y;
    double import_parallax_origin_x;
    double import_parallax_origin_y;
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
