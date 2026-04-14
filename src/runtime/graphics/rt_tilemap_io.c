//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_tilemap_io.c
// Purpose: File I/O (JSON save/load, CSV import) and auto-tiling for tilemaps.
//
// Key invariants:
//   - JSON format version 1; includes layers, collision, tile properties.
//   - CSV import creates a single-layer tilemap from comma-separated values.
//   - Auto-tiling uses 4-bit neighbor bitmask (up|right|down|left) → 16 variants.
//   - Tile properties are stored in a flat array indexed by tile ID.
//
// Ownership/Lifetime:
//   - LoadFromFile/LoadCSV return newly allocated tilemaps.
//   - Property storage is embedded in the tilemap struct.
//
// Links: rt_tilemap.h, rt_json.h, rt_csv.h
//
//===----------------------------------------------------------------------===//

#include "rt_tilemap.h"
#include "rt_tilemap_internal.h"

#include "rt_box.h"
#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Set the tile property of the tilemap.
void rt_tilemap_set_tile_property(void *tm, int64_t tile_index, rt_string key, int64_t value) {
    if (!tm || tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;
    const char *ckey = rt_string_cstr(key);
    if (!ckey)
        return;

    tile_props *p = &tilemap->tile_props[tile_index];
    // Check if key exists
    for (int32_t i = 0; i < p->count; i++) {
        if (strcmp(p->entries[i].key, ckey) == 0) {
            p->entries[i].value = value;
            return;
        }
    }
    // Add new
    if (p->count >= MAX_PROP_KEYS)
        return;
    size_t klen = strlen(ckey);
    if (klen > MAX_PROP_KEY_LEN - 1)
        klen = MAX_PROP_KEY_LEN - 1;
    memcpy(p->entries[p->count].key, ckey, klen);
    p->entries[p->count].key[klen] = '\0';
    p->entries[p->count].value = value;
    p->count++;
}

/// @brief Look up a custom integer property attached to tile `tile_index` (e.g., "damage",
/// "speed_modifier"). Returns `default_val` if the tile has no such property or inputs are
/// invalid. Properties are stored per-tile-type (not per-cell), max 8 keys per tile.
int64_t rt_tilemap_get_tile_property(void *tm,
                                     int64_t tile_index,
                                     rt_string key,
                                     int64_t default_val) {
    if (!tm || tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return default_val;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;
    const char *ckey = rt_string_cstr(key);
    if (!ckey)
        return default_val;

    tile_props *p = &tilemap->tile_props[tile_index];
    for (int32_t i = 0; i < p->count; i++) {
        if (strcmp(p->entries[i].key, ckey) == 0)
            return p->entries[i].value;
    }
    return default_val;
}

/// @brief Has the tile property of the tilemap.
int8_t rt_tilemap_has_tile_property(void *tm, int64_t tile_index, rt_string key) {
    if (!tm || tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return 0;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;
    const char *ckey = rt_string_cstr(key);
    if (!ckey)
        return 0;

    tile_props *p = &tilemap->tile_props[tile_index];
    for (int32_t i = 0; i < p->count; i++) {
        if (strcmp(p->entries[i].key, ckey) == 0)
            return 1;
    }
    return 0;
}

//=============================================================================
// Auto-Tiling
//=============================================================================

/// Find or create an autotile rule for a base tile
static autotile_rule *find_or_create_rule(rt_tilemap_impl *tilemap, int64_t base_tile) {
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        if (tilemap->autotile_rules[i].base_tile == base_tile)
            return &tilemap->autotile_rules[i];
    }
    if (tilemap->autotile_count >= MAX_AUTOTILE_RULES)
        return NULL;
    autotile_rule *r = &tilemap->autotile_rules[tilemap->autotile_count++];
    memset(r, 0, sizeof(autotile_rule));
    r->base_tile = base_tile;
    r->active = 1;
    return r;
}

/// @brief Register the *low half* (variants 0..7) of an autotile rule for `base_tile`. Each
/// `vN` is the tile index to use for one of the 16 neighbor-bitmask cases. Pair with
/// `_set_autotile_hi` to cover variants 8..15. Splits across two calls because the runtime ABI
/// caps function args at 8.
void rt_tilemap_set_autotile_lo(void *tm,
                                int64_t base_tile,
                                int64_t v0,
                                int64_t v1,
                                int64_t v2,
                                int64_t v3,
                                int64_t v4,
                                int64_t v5,
                                int64_t v6,
                                int64_t v7) {
    if (!tm)
        return;
    autotile_rule *r = find_or_create_rule((rt_tilemap_impl *)tm, base_tile);
    if (!r)
        return;
    r->variants[0] = v0;
    r->variants[1] = v1;
    r->variants[2] = v2;
    r->variants[3] = v3;
    r->variants[4] = v4;
    r->variants[5] = v5;
    r->variants[6] = v6;
    r->variants[7] = v7;
    r->active = 1;
}

/// @brief Register the *high half* (variants 8..15) of an autotile rule for `base_tile`. The
/// 16 entries jointly cover every neighbor pattern (4-bit bitmask of N/E/S/W or NW/NE/SW/SE
/// adjacency). Marks the rule active so `_apply_autotile_region` will start substituting tiles.
void rt_tilemap_set_autotile_hi(void *tm,
                                int64_t base_tile,
                                int64_t v8,
                                int64_t v9,
                                int64_t v10,
                                int64_t v11,
                                int64_t v12,
                                int64_t v13,
                                int64_t v14,
                                int64_t v15) {
    if (!tm)
        return;
    autotile_rule *r = find_or_create_rule((rt_tilemap_impl *)tm, base_tile);
    if (!r)
        return;
    r->variants[8] = v8;
    r->variants[9] = v9;
    r->variants[10] = v10;
    r->variants[11] = v11;
    r->variants[12] = v12;
    r->variants[13] = v13;
    r->variants[14] = v14;
    r->variants[15] = v15;
    r->active = 1;
}

/// @brief Clear the autotile of the tilemap.
void rt_tilemap_clear_autotile(void *tm, int64_t base_tile) {
    if (!tm)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        if (tilemap->autotile_rules[i].base_tile == base_tile) {
            tilemap->autotile_rules[i].active = 0;
            return;
        }
    }
}

static autotile_rule *find_rule(rt_tilemap_impl *tilemap, int64_t tile) {
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        if (tilemap->autotile_rules[i].active && tilemap->autotile_rules[i].base_tile == tile)
            return &tilemap->autotile_rules[i];
    }
    return NULL;
}

static int8_t is_same_base(rt_tilemap_impl *tilemap, int64_t tile, int64_t base) {
    if (tile == base)
        return 1;
    // Check if tile is one of the variants
    autotile_rule *r = find_rule(tilemap, base);
    if (!r)
        return 0;
    for (int i = 0; i < 16; i++) {
        if (r->variants[i] == tile)
            return 1;
    }
    return 0;
}

/// @brief Apply the autotile region of the tilemap.
void rt_tilemap_apply_autotile_region(void *tm, int64_t rx, int64_t ry, int64_t rw, int64_t rh) {
    if (!tm)
        return;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;
    if (tilemap->autotile_count == 0)
        return;

    int64_t map_w = rt_tilemap_get_width(tm);
    int64_t map_h = rt_tilemap_get_height(tm);

    for (int64_t y = ry; y < ry + rh; y++) {
        for (int64_t x = rx; x < rx + rw; x++) {
            if (x < 0 || x >= map_w || y < 0 || y >= map_h)
                continue;

            int64_t tile = rt_tilemap_get_tile(tm, x, y);
            autotile_rule *rule = find_rule(tilemap, tile);
            if (!rule) {
                // Check if tile is a variant of some rule
                for (int32_t r = 0; r < tilemap->autotile_count; r++) {
                    if (!tilemap->autotile_rules[r].active)
                        continue;
                    for (int v = 0; v < 16; v++) {
                        if (tilemap->autotile_rules[r].variants[v] == tile) {
                            rule = &tilemap->autotile_rules[r];
                            goto found_rule;
                        }
                    }
                }
            found_rule:
                if (!rule)
                    continue;
            }

            // Compute 4-bit neighbor mask
            int64_t base = rule->base_tile;
            int32_t mask = 0;
            // Up
            if (y > 0 && is_same_base(tilemap, rt_tilemap_get_tile(tm, x, y - 1), base))
                mask |= 1;
            // Right
            if (x < map_w - 1 && is_same_base(tilemap, rt_tilemap_get_tile(tm, x + 1, y), base))
                mask |= 2;
            // Down
            if (y < map_h - 1 && is_same_base(tilemap, rt_tilemap_get_tile(tm, x, y + 1), base))
                mask |= 4;
            // Left
            if (x > 0 && is_same_base(tilemap, rt_tilemap_get_tile(tm, x - 1, y), base))
                mask |= 8;

            rt_tilemap_set_tile(tm, x, y, rule->variants[mask]);
        }
    }
}

/// @brief Apply the autotile of the tilemap.
void rt_tilemap_apply_autotile(void *tm) {
    if (!tm)
        return;
    rt_tilemap_apply_autotile_region(tm, 0, 0, rt_tilemap_get_width(tm), rt_tilemap_get_height(tm));
}

//=============================================================================
// JSON Save/Load
//=============================================================================

static int64_t map_get_i64(void *map, const char *key) {
    return (int64_t)rt_map_get_float(map, rt_const_cstr(key));
}

static void *seq_new_owned(void) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    return seq;
}

static void map_set_string_copy(void *map, const char *key, const char *value) {
    rt_string copy = rt_string_from_bytes(value ? value : "", value ? strlen(value) : 0);
    if (!copy)
        return;
    rt_map_set(map, rt_const_cstr(key), copy);
    if (rt_obj_release_check0(copy))
        rt_obj_free(copy);
}

static void *serialize_pixels_blob(void *pixels) {
    if (!pixels)
        return NULL;

    int64_t width = rt_pixels_width(pixels);
    int64_t height = rt_pixels_height(pixels);
    const uint32_t *raw = rt_pixels_raw_buffer(pixels);
    if (width < 0 || height < 0 || (!raw && width * height > 0))
        return NULL;

    void *blob = rt_map_new();
    void *data = seq_new_owned();
    rt_map_set_int(blob, rt_const_cstr("width"), width);
    rt_map_set_int(blob, rt_const_cstr("height"), height);
    for (int64_t i = 0; i < width * height; i++)
        rt_seq_push(data, rt_box_i64((int64_t)raw[i]));
    rt_map_set(blob, rt_const_cstr("pixels"), data);
    return blob;
}

static void *deserialize_pixels_blob(void *blob) {
    if (!blob)
        return NULL;
    int64_t width = map_get_i64(blob, "width");
    int64_t height = map_get_i64(blob, "height");
    if (width <= 0 || height <= 0)
        return NULL;
    void *pixels = rt_pixels_new(width, height);
    if (!pixels)
        return NULL;
    rt_pixels_impl *impl = (rt_pixels_impl *)pixels;
    uint32_t *dst = impl->data;
    void *data = rt_map_get(blob, rt_const_cstr("pixels"));
    if (!data || rt_seq_len(data) < width * height)
        return pixels;
    for (int64_t i = 0; i < width * height; i++) {
        void *boxed = rt_seq_get(data, i);
        if (boxed)
            dst[i] = (uint32_t)rt_unbox_f64(boxed);
    }
    return pixels;
}

static void assign_base_tileset(rt_tilemap_impl *tm, void *pixels) {
    if (!tm)
        return;
    if (tm->tileset && tm->tileset != pixels)
        rt_heap_release(tm->tileset);
    tm->tileset = pixels;
    tm->tileset_cols = pixels ? rt_pixels_width(pixels) / tm->tile_width : 0;
    tm->tileset_rows = pixels ? rt_pixels_height(pixels) / tm->tile_height : 0;
    tm->tile_count = tm->tileset_cols * tm->tileset_rows;
    tm->layers[0].tileset_cols = tm->tileset_cols;
    tm->layers[0].tileset_rows = tm->tileset_rows;
    tm->layers[0].tile_count = tm->tile_count;
}

static void assign_layer_tileset(rt_tilemap_impl *tm, int64_t layer, void *pixels) {
    if (!tm || layer < 0 || layer >= tm->layer_count)
        return;
    tm_layer *lyr = &tm->layers[layer];
    if (lyr->tileset && lyr->tileset != pixels)
        rt_heap_release(lyr->tileset);
    lyr->tileset = pixels;
    lyr->tileset_cols = pixels ? rt_pixels_width(pixels) / tm->tile_width : 0;
    lyr->tileset_rows = pixels ? rt_pixels_height(pixels) / tm->tile_height : 0;
    lyr->tile_count = lyr->tileset_cols * lyr->tileset_rows;
}

/// @brief Serialize the tilemap to a JSON file at `path`. Includes version (1), dimensions,
/// tile size, every layer's data + tileset reference, tile properties, and autotile rules.
/// Returns 1 on success, 0 on null inputs / missing path / I/O error.
int8_t rt_tilemap_save_to_file(void *tm, rt_string path) {
    if (!tm || !path)
        return 0;
    const char *cpath = rt_string_cstr(path);
    if (!cpath)
        return 0;

    int64_t w = rt_tilemap_get_width(tm);
    int64_t h = rt_tilemap_get_height(tm);
    int64_t tw = rt_tilemap_get_tile_width(tm);
    int64_t th = rt_tilemap_get_tile_height(tm);
    int64_t layer_count = rt_tilemap_get_layer_count(tm);

    // Build JSON object using Map
    void *root = rt_map_new();
    rt_map_set_int(root, rt_const_cstr("version"), 1);
    rt_map_set_int(root, rt_const_cstr("width"), w);
    rt_map_set_int(root, rt_const_cstr("height"), h);
    rt_map_set_int(root, rt_const_cstr("tileWidth"), tw);
    rt_map_set_int(root, rt_const_cstr("tileHeight"), th);
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;
    if (tilemap->tileset) {
        void *tileset_obj = serialize_pixels_blob(tilemap->tileset);
        if (tileset_obj)
            rt_map_set(root, rt_const_cstr("tileset"), tileset_obj);
    }

    // Layers array
    void *layers_arr = seq_new_owned();
    for (int64_t li = 0; li < layer_count; li++) {
        void *layer_obj = rt_map_new();
        // Tile array
        void *tiles_arr = seq_new_owned();
        for (int64_t y = 0; y < h; y++) {
            for (int64_t x = 0; x < w; x++) {
                int64_t t = rt_tilemap_get_tile_layer(tm, li, x, y);
                rt_seq_push(tiles_arr, rt_box_i64(t));
            }
        }
        rt_map_set(layer_obj, rt_const_cstr("tiles"), tiles_arr);
        rt_map_set(
            layer_obj, rt_const_cstr("visible"), rt_box_i64(rt_tilemap_get_layer_visible(tm, li)));
        map_set_string_copy(layer_obj, "name", tilemap->layers[li].name);
        if (li > 0 && tilemap->layers[li].tileset) {
            void *tileset_obj = serialize_pixels_blob(tilemap->layers[li].tileset);
            if (tileset_obj)
                rt_map_set(layer_obj, rt_const_cstr("tileset"), tileset_obj);
        }
        rt_seq_push(layers_arr, layer_obj);
    }
    rt_map_set(root, rt_const_cstr("layers"), layers_arr);

    // Collision info
    void *coll_obj = rt_map_new();
    rt_map_set_int(coll_obj, rt_const_cstr("layer"), rt_tilemap_get_collision_layer(tm));
    void *types_arr = seq_new_owned();
    for (int64_t tile_id = 0; tile_id < MAX_TILE_COLLISION_IDS; tile_id++) {
        int64_t coll_type = rt_tilemap_get_collision(tm, tile_id);
        if (coll_type == RT_TILE_COLLISION_NONE)
            continue;
        void *entry = rt_map_new();
        rt_map_set_int(entry, rt_const_cstr("tile"), tile_id);
        rt_map_set_int(entry, rt_const_cstr("type"), coll_type);
        rt_seq_push(types_arr, entry);
    }
    rt_map_set(coll_obj, rt_const_cstr("types"), types_arr);
    rt_map_set(root, rt_const_cstr("collision"), coll_obj);

    void *props_arr = seq_new_owned();
    for (int64_t tile_id = 0; tile_id < MAX_TILE_PROPS; tile_id++) {
        tile_props *props = &tilemap->tile_props[tile_id];
        if (props->count <= 0)
            continue;
        void *prop_obj = rt_map_new();
        void *entries = seq_new_owned();
        rt_map_set_int(prop_obj, rt_const_cstr("tile"), tile_id);
        for (int32_t i = 0; i < props->count; i++) {
            void *entry = rt_map_new();
            map_set_string_copy(entry, "key", props->entries[i].key);
            rt_map_set_int(entry, rt_const_cstr("value"), props->entries[i].value);
            rt_seq_push(entries, entry);
        }
        rt_map_set(prop_obj, rt_const_cstr("entries"), entries);
        rt_seq_push(props_arr, prop_obj);
    }
    rt_map_set(root, rt_const_cstr("tileProperties"), props_arr);

    void *autotile_arr = seq_new_owned();
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        autotile_rule *rule = &tilemap->autotile_rules[i];
        if (!rule->active)
            continue;
        void *rule_obj = rt_map_new();
        void *variants = seq_new_owned();
        rt_map_set_int(rule_obj, rt_const_cstr("baseTile"), rule->base_tile);
        for (int32_t v = 0; v < 16; v++)
            rt_seq_push(variants, rt_box_i64(rule->variants[v]));
        rt_map_set(rule_obj, rt_const_cstr("variants"), variants);
        rt_seq_push(autotile_arr, rule_obj);
    }
    rt_map_set(root, rt_const_cstr("autotiles"), autotile_arr);

    void *anim_arr = seq_new_owned();
    for (int32_t i = 0; i < tilemap->tile_anim_count; i++) {
        tm_tile_anim *anim = &tilemap->tile_anims[i];
        void *anim_obj = rt_map_new();
        void *frames = seq_new_owned();
        rt_map_set_int(anim_obj, rt_const_cstr("baseTile"), anim->base_tile_id);
        rt_map_set_int(anim_obj, rt_const_cstr("frameCount"), anim->frame_count);
        rt_map_set_int(anim_obj, rt_const_cstr("msPerFrame"), anim->ms_per_frame);
        rt_map_set_int(anim_obj, rt_const_cstr("timer"), anim->timer);
        rt_map_set_int(anim_obj, rt_const_cstr("currentFrame"), anim->current_frame);
        for (int32_t fidx = 0; fidx < anim->frame_count; fidx++)
            rt_seq_push(frames, rt_box_i64(anim->frame_tiles[fidx]));
        rt_map_set(anim_obj, rt_const_cstr("frames"), frames);
        rt_seq_push(anim_arr, anim_obj);
    }
    rt_map_set(root, rt_const_cstr("animations"), anim_arr);

    // Format as JSON
    rt_string json = rt_json_format_pretty(root, 2);
    if (!json)
        return 0;

    const char *json_cstr = rt_string_cstr(json);
    if (!json_cstr)
        return 0;

    FILE *f = fopen(cpath, "w");
    if (!f)
        return 0;
    size_t len = strlen(json_cstr);
    size_t written = fwrite(json_cstr, 1, len, f);
    fclose(f);

    return written == len ? 1 : 0;
}

/// @brief Load a tilemap from a `.vtile` (or compatible) file at `path`. Reads dimensions,
/// layer data, tile properties, and autotile rules. Returns a fresh tilemap handle on success
/// or NULL on I/O / parse failure (file missing, version mismatch, truncated). See
/// `_save_to_file` for the binary layout.
void *rt_tilemap_load_from_file(rt_string path) {
    if (!path)
        return NULL;
    const char *cpath = rt_string_cstr(path);
    if (!cpath)
        return NULL;

    // Read file contents
    FILE *f = fopen(cpath, "r");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)file_size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)file_size, f);
    fclose(f);
    buf[read] = '\0';

    rt_string json_str = rt_string_from_bytes(buf, read);
    free(buf);
    if (!json_str)
        return NULL;

    void *root = rt_json_parse(json_str);
    if (!root)
        return NULL;

    // Extract dimensions (JSON numbers are boxed as f64)
    int64_t w = (int64_t)rt_map_get_float(root, rt_const_cstr("width"));
    int64_t h = (int64_t)rt_map_get_float(root, rt_const_cstr("height"));
    int64_t tw = (int64_t)rt_map_get_float(root, rt_const_cstr("tileWidth"));
    int64_t th = (int64_t)rt_map_get_float(root, rt_const_cstr("tileHeight"));
    if (w <= 0 || h <= 0 || tw <= 0 || th <= 0)
        return NULL;

    void *tm = rt_tilemap_new(w, h, tw, th);
    if (!tm)
        return NULL;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;

    void *tileset_blob = rt_map_get(root, rt_const_cstr("tileset"));
    if (tileset_blob) {
        void *pixels = deserialize_pixels_blob(tileset_blob);
        if (pixels)
            assign_base_tileset(tilemap, pixels);
    }

    // Load layers
    void *layers_arr = rt_map_get(root, rt_const_cstr("layers"));
    if (layers_arr) {
        int64_t lcount = rt_seq_len(layers_arr);
        for (int64_t li = 0; li < lcount; li++) {
            // Add layer if not layer 0
            if (li > 0)
                rt_tilemap_add_layer(tm, rt_const_cstr(""));

            void *layer_obj = rt_seq_get(layers_arr, li);
            if (!layer_obj)
                continue;

            void *tiles_arr = rt_map_get(layer_obj, rt_const_cstr("tiles"));
            if (tiles_arr) {
                int64_t tcount = rt_seq_len(tiles_arr);
                for (int64_t ti = 0; ti < tcount; ti++) {
                    void *tval = rt_seq_get(tiles_arr, ti);
                    if (!tval)
                        continue;
                    int64_t tile = (int64_t)rt_unbox_f64(tval);
                    int64_t tx = ti % w;
                    int64_t ty = ti / w;
                    rt_tilemap_set_tile_layer(tm, li, tx, ty, tile);
                }
            }

            int64_t vis = (int64_t)rt_map_get_float(layer_obj, rt_const_cstr("visible"));
            rt_tilemap_set_layer_visible(tm, li, (int8_t)vis);
            rt_string lname = (rt_string)rt_map_get(layer_obj, rt_const_cstr("name"));
            if (lname) {
                const char *name_cstr = rt_string_cstr(lname);
                if (name_cstr) {
                    memset(tilemap->layers[li].name, 0, sizeof(tilemap->layers[li].name));
                    strncpy(
                        tilemap->layers[li].name, name_cstr, sizeof(tilemap->layers[li].name) - 1);
                }
            }
            if (li > 0) {
                void *layer_tileset = rt_map_get(layer_obj, rt_const_cstr("tileset"));
                if (layer_tileset) {
                    void *pixels = deserialize_pixels_blob(layer_tileset);
                    if (pixels)
                        assign_layer_tileset(tilemap, li, pixels);
                }
            }
        }
    }

    // Load collision
    void *coll = rt_map_get(root, rt_const_cstr("collision"));
    if (coll) {
        int64_t cl = (int64_t)rt_map_get_float(coll, rt_const_cstr("layer"));
        rt_tilemap_set_collision_layer(tm, cl);
        void *types = rt_map_get(coll, rt_const_cstr("types"));
        if (types) {
            for (int64_t i = 0; i < rt_seq_len(types); i++) {
                void *entry = rt_seq_get(types, i);
                if (!entry)
                    continue;
                rt_tilemap_set_collision(
                    tm, map_get_i64(entry, "tile"), map_get_i64(entry, "type"));
            }
        }
    }

    void *props_arr = rt_map_get(root, rt_const_cstr("tileProperties"));
    if (props_arr) {
        for (int64_t i = 0; i < rt_seq_len(props_arr); i++) {
            void *prop_obj = rt_seq_get(props_arr, i);
            if (!prop_obj)
                continue;
            int64_t tile_id = map_get_i64(prop_obj, "tile");
            void *entries = rt_map_get(prop_obj, rt_const_cstr("entries"));
            if (!entries)
                continue;
            for (int64_t j = 0; j < rt_seq_len(entries); j++) {
                void *entry = rt_seq_get(entries, j);
                if (!entry)
                    continue;
                rt_string key = (rt_string)rt_map_get(entry, rt_const_cstr("key"));
                rt_tilemap_set_tile_property(tm, tile_id, key, map_get_i64(entry, "value"));
            }
        }
    }

    void *autotiles = rt_map_get(root, rt_const_cstr("autotiles"));
    if (autotiles) {
        for (int64_t i = 0; i < rt_seq_len(autotiles); i++) {
            void *rule_obj = rt_seq_get(autotiles, i);
            if (!rule_obj)
                continue;
            int64_t base_tile = map_get_i64(rule_obj, "baseTile");
            void *variants = rt_map_get(rule_obj, rt_const_cstr("variants"));
            if (!variants || rt_seq_len(variants) < 16)
                continue;
            rt_tilemap_set_autotile_lo(tm,
                                       base_tile,
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 0)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 1)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 2)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 3)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 4)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 5)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 6)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 7)));
            rt_tilemap_set_autotile_hi(tm,
                                       base_tile,
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 8)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 9)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 10)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 11)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 12)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 13)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 14)),
                                       (int64_t)rt_unbox_f64(rt_seq_get(variants, 15)));
        }
    }

    void *animations = rt_map_get(root, rt_const_cstr("animations"));
    if (animations) {
        for (int64_t i = 0; i < rt_seq_len(animations); i++) {
            void *anim_obj = rt_seq_get(animations, i);
            if (!anim_obj)
                continue;
            int64_t base_tile = map_get_i64(anim_obj, "baseTile");
            int64_t frame_count = map_get_i64(anim_obj, "frameCount");
            int64_t ms_per_frame = map_get_i64(anim_obj, "msPerFrame");
            void *frames = rt_map_get(anim_obj, rt_const_cstr("frames"));
            if (!frames || frame_count <= 0 || frame_count > TM_MAX_ANIM_FRAMES)
                continue;
            rt_tilemap_set_tile_anim(tm, base_tile, frame_count, ms_per_frame);
            for (int64_t fi = 0; fi < frame_count; fi++) {
                void *boxed = rt_seq_get(frames, fi);
                if (boxed)
                    rt_tilemap_set_tile_anim_frame(tm, base_tile, fi, (int64_t)rt_unbox_f64(boxed));
            }
            if (tilemap->tile_anim_count > 0) {
                tm_tile_anim *anim = &tilemap->tile_anims[tilemap->tile_anim_count - 1];
                anim->timer = map_get_i64(anim_obj, "timer");
                anim->current_frame = (int32_t)map_get_i64(anim_obj, "currentFrame");
                if (anim->frame_count > 0)
                    anim->current_frame %= anim->frame_count;
            }
        }
    }

    return tm;
}

//=============================================================================
// CSV Import
//=============================================================================

/// @brief Load a tilemap from a CSV file (`,`-separated tile indices, one row per line). Two-pass
/// reader: first scans for max columns and row count, then allocates a single-layer tilemap of
/// that size and parses values. Empty lines are skipped; lines longer than 16 KiB are truncated.
/// Returns NULL on missing path / empty file / allocation failure.
void *rt_tilemap_load_csv(rt_string path, int64_t tile_w, int64_t tile_h) {
    if (!path)
        return NULL;
    const char *cpath = rt_string_cstr(path);
    if (!cpath)
        return NULL;

    FILE *f = fopen(cpath, "r");
    if (!f)
        return NULL;

    // First pass: count rows and max columns
    int64_t max_cols = 0;
    int64_t rows = 0;
    char line_buf[16384]; /* max CSV line length — rows wider than this are truncated */

    while (fgets(line_buf, sizeof(line_buf), f)) {
        // Count commas + 1 for column count
        int64_t cols = 1;
        for (char *p = line_buf; *p; p++) {
            if (*p == ',')
                cols++;
        }
        // Skip empty lines
        size_t len = strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\n')
            len--;
        if (len == 0)
            continue;

        if (cols > max_cols)
            max_cols = cols;
        rows++;
    }

    if (rows == 0 || max_cols == 0) {
        fclose(f);
        return NULL;
    }

    void *tm = rt_tilemap_new(max_cols, rows, tile_w, tile_h);
    if (!tm) {
        fclose(f);
        return NULL;
    }

    // Second pass: parse tile values
    fseek(f, 0, SEEK_SET);
    int64_t y = 0;

    while (fgets(line_buf, sizeof(line_buf), f) && y < rows) {
        size_t len = strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\n')
            line_buf[--len] = '\0';
        if (len == 0)
            continue;

        int64_t x = 0;
        char *tok = line_buf;
        while (*tok && x < max_cols) {
            // Parse integer
            int64_t val = 0;
            int neg = 0;
            if (*tok == '-') {
                neg = 1;
                tok++;
            }
            while (*tok >= '0' && *tok <= '9') {
                val = val * 10 + (*tok - '0');
                tok++;
            }
            if (neg)
                val = -val;

            rt_tilemap_set_tile(tm, x, y, val);
            x++;

            // Skip comma
            if (*tok == ',')
                tok++;
        }
        y++;
    }

    fclose(f);
    return tm;
}
