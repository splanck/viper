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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Keep tilemap file loading large-file safe on platforms where long is 32-bit.
#if defined(_WIN32)
#define tmio_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define tmio_ftell(fp) _ftelli64((fp))
#else
#define tmio_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define tmio_ftell(fp) ftello((fp))
#endif

/// @brief Validate-and-return a Tilemap pointer; NULL for NULL or wrong class.
/// @details Soft check used by every public Tilemap I/O entry point.
static rt_tilemap_impl *tilemap_io_checked(void *tm) {
    if (!tm || rt_obj_class_id(tm) != RT_TILEMAP_CLASS_ID)
        return NULL;
    return (rt_tilemap_impl *)tm;
}

/// @brief Release a retained reference held in @p *slot, free the payload at refcount 0,
///        and clear the slot to NULL.
/// @details The standard ownership-discipline helper used when loading
///          replaces or removes a tile / layer / object reference. Decrements
///          the refcount, frees the payload only when this was the last
///          reference, and writes NULL into @p *slot so a subsequent reload
///          cannot accidentally double-free. NULL @p slot or NULL @c *slot
///          are no-ops.
static void tilemap_io_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Set the tile property of the tilemap.
void rt_tilemap_set_tile_property(void *tm, int64_t tile_index, rt_string key, int64_t value) {
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap || tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return;
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
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap || tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return default_val;
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
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap || tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return 0;
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
    for (int i = 0; i < 16; i++)
        r->variants[i] = base_tile;
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
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap)
        return;
    autotile_rule *r = find_or_create_rule(tilemap, base_tile);
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
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap)
        return;
    autotile_rule *r = find_or_create_rule(tilemap, base_tile);
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
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap)
        return;
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        if (tilemap->autotile_rules[i].base_tile == base_tile) {
            tilemap->autotile_rules[i].active = 0;
            return;
        }
    }
}

/// @brief Find the active autotile rule whose `base_tile` exactly matches `tile`.
/// @return Pointer to the matching rule, or NULL if no rule is registered for this base tile.
static autotile_rule *find_rule(rt_tilemap_impl *tilemap, int64_t tile) {
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        if (tilemap->autotile_rules[i].active && tilemap->autotile_rules[i].base_tile == tile)
            return &tilemap->autotile_rules[i];
    }
    return NULL;
}

/// @brief Test whether @p tile is the same autotile type as @p base — either an
///        exact match or one of the 16 variants registered for @p base's rule.
/// @details Used by the autotile neighbor scan to decide whether adjacent tiles should
///   be counted as connected. Without variant checking, placing any variant (e.g., the
///   corners or edge variants) next to another tile of the same type would break the
///   connectivity mask because the neighbour would no longer equal `base` exactly.
/// @return 1 if the tiles are considered the same autotile type, 0 otherwise.
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

/// @brief Find the autotile rule for `tile`, checking both base tiles and all variant indices.
/// @details First tries an exact base_tile match via find_rule; on failure scans all active
///          rules' 16-slot variant arrays. Used when placing any variant of an autotile set,
///          not just the canonical base tile.
/// @return Pointer to the governing autotile rule, or NULL if tile belongs to no active set.
static autotile_rule *find_rule_for_tile(rt_tilemap_impl *tilemap, int64_t tile) {
    autotile_rule *rule = find_rule(tilemap, tile);
    if (rule)
        return rule;
    for (int32_t r = 0; r < tilemap->autotile_count; r++) {
        if (!tilemap->autotile_rules[r].active)
            continue;
        for (int v = 0; v < 16; v++) {
            if (tilemap->autotile_rules[r].variants[v] == tile)
                return &tilemap->autotile_rules[r];
        }
    }
    return NULL;
}

/// @brief Apply the autotile region of the tilemap.
void rt_tilemap_apply_autotile_region(void *tm, int64_t rx, int64_t ry, int64_t rw, int64_t rh) {
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap)
        return;
    if (tilemap->autotile_count == 0 || rw <= 0 || rh <= 0)
        return;

    int64_t map_w = rt_tilemap_get_width(tm);
    int64_t map_h = rt_tilemap_get_height(tm);
    int64_t rx_end = rt_pixels_add_sat64(rx, rw);
    int64_t ry_end = rt_pixels_add_sat64(ry, rh);
    int64_t start_x = rx < 0 ? 0 : rx;
    int64_t start_y = ry < 0 ? 0 : ry;
    int64_t end_x = rx_end > map_w ? map_w : rx_end;
    int64_t end_y = ry_end > map_h ? map_h : ry_end;
    if (end_x <= start_x || end_y <= start_y)
        return;

    int64_t region_w = end_x - start_x;
    int64_t region_h = end_y - start_y;
    if (region_w > INT64_MAX / region_h)
        return;
    int64_t count = region_w * region_h;
    if (count > INT64_MAX / (int64_t)sizeof(int64_t))
        return;
    int64_t *resolved = (int64_t *)malloc((size_t)count * sizeof(int64_t));
    if (!resolved)
        return;

    for (int64_t y = start_y; y < end_y; y++) {
        for (int64_t x = start_x; x < end_x; x++) {
            int64_t tile = rt_tilemap_get_tile(tm, x, y);
            autotile_rule *rule = find_rule_for_tile(tilemap, tile);
            if (!rule) {
                resolved[(y - start_y) * region_w + (x - start_x)] = tile;
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

            resolved[(y - start_y) * region_w + (x - start_x)] = rule->variants[mask];
        }
    }

    for (int64_t y = start_y; y < end_y; y++) {
        for (int64_t x = start_x; x < end_x; x++)
            rt_tilemap_set_tile(tm, x, y, resolved[(y - start_y) * region_w + (x - start_x)]);
    }
    free(resolved);
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

/// @brief Read a numeric value from a JSON map as int64_t via float coercion.
/// @details JSON numbers are stored as float in the runtime map. The cast to int64_t
///   is safe for the integral values used in tilemap serialization (dimensions, tile
///   indices) because they are all within float's exact-integer range (≤ 2^53).
static int64_t map_get_i64(void *map, const char *key) {
    return (int64_t)rt_map_get_float(map, rt_const_cstr(key));
}

/// @brief Create a Seq that owns its elements so they are released when the Seq
///        is collected. Used for the per-serialized-object pixel data arrays so the
///        boxed integer elements are freed along with the containing sequence.
static void *seq_new_owned(void) {
    void *seq = rt_seq_new();
    rt_seq_set_owns_elements(seq, 1);
    return seq;
}

/// @brief Store @p value into @p map under @p key and drop the caller's local reference.
/// @details The classic "transfer ownership" idiom: rt_map_set retains its own
///          ref to @p value, so the caller's local ref must be released to
///          avoid a leak. tilemap_io_release_ref handles refcount 0 by freeing.
///          NULL @p value is treated as "skip" so the map never contains NULL
///          entries.
static void map_set_owned(void *map, const char *key, void *value) {
    if (!map || !value)
        return;
    rt_map_set(map, rt_const_cstr(key), value);
    tilemap_io_release_ref(&value);
}

/// @brief Append @p value to @p seq and drop the caller's local reference.
/// @details Mirrors map_set_owned but for sequence appends. Used during JSON
///          tilemap load when each parsed tile/layer/object is appended into
///          its parent sequence and the loader's temporary reference must be
///          released afterward.
static void seq_push_owned(void *seq, void *value) {
    if (!seq || !value)
        return;
    rt_seq_push(seq, value);
    tilemap_io_release_ref(&value);
}

/// @brief Store a C string in a runtime map under @p key, releasing the temporary
///        rt_string after the map takes ownership of it.
/// @details `rt_string_from_bytes` allocates a new rt_string; after `rt_map_set` retains
///   it, the caller's reference is released via `release_check0/free`. An empty string
///   is substituted when @p value is NULL so the map always contains a valid entry.
static void map_set_string_copy(void *map, const char *key, const char *value) {
    rt_string copy = rt_string_from_bytes(value ? value : "", value ? strlen(value) : 0);
    if (!copy)
        return;
    rt_map_set(map, rt_const_cstr(key), copy);
    if (rt_obj_release_check0(copy))
        rt_obj_free(copy);
}

/// @brief Serialize a Pixels object to a JSON map blob with "width", "height", and a
///        flat "pixels" Seq of uint32_t RGBA values encoded as boxed integers.
/// @details Used during `rt_tilemap_save` to embed tileset image data directly in the
///   JSON save file, making tilemap files self-contained. Each pixel is stored as an
///   int64_t to stay within JSON's safe integer range. Returns NULL for NULL input or
///   zero-dimension images.
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
        seq_push_owned(data, rt_box_i64((int64_t)raw[i]));
    map_set_owned(blob, "pixels", data);
    return blob;
}

/// @brief Reconstruct a Pixels object from a serialized blob map (inverse of
///        `serialize_pixels_blob`).
/// @details Reads "width" and "height" from the blob, allocates a new Pixels, then
///   copies each element of the "pixels" Seq by unboxing it as float64 and casting to
///   uint32_t. If the Seq length does not match `width * height` the partially
///   constructed Pixels is released and NULL is returned to signal a corrupt save file.
static void *deserialize_pixels_blob(void *blob) {
    if (!blob)
        return NULL;
    int64_t width = map_get_i64(blob, "width");
    int64_t height = map_get_i64(blob, "height");
    if (width <= 0 || height <= 0)
        return NULL;
    if (width > INT64_MAX / height)
        return NULL;
    void *pixels = rt_pixels_new(width, height);
    if (!pixels)
        return NULL;
    rt_pixels_impl *impl = (rt_pixels_impl *)pixels;
    uint32_t *dst = impl->data;
    void *data = rt_map_get(blob, rt_const_cstr("pixels"));
    int64_t expected = width * height;
    if (!data || rt_seq_len(data) != expected) {
        tilemap_io_release_ref(&pixels);
        return NULL;
    }
    for (int64_t i = 0; i < expected; i++) {
        void *boxed = rt_seq_get(data, i);
        if (boxed)
            dst[i] = (uint32_t)rt_unbox_f64(boxed);
    }
    return pixels;
}

/// @brief Replace the tilemap's default tileset with @p pixels, releasing the old one
///        and recomputing derived metrics (cols, rows, tile_count) and syncing layer 0.
/// @details The tileset metrics (tileset_cols/rows, tile_count) are derived from the
///   image dimensions divided by the tile size, so they must be recalculated whenever
///   the tileset changes. Layer 0 mirrors the base tileset, so its copy is updated too.
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

/// @brief Replace a specific layer's tileset override with @p pixels, releasing the old
///        one and recomputing that layer's cols/rows/tile_count.
/// @details Per-layer tilesets allow different layers to use different tile graphics
///   (e.g., background layer on a larger tileset, foreground on a smaller one).
///   A NULL @p pixels clears the per-layer override so the layer reverts to the
///   tilemap's default tileset during rendering.
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

/// @brief Linear-search @p tm's tile-animation table for the entry whose base tile is @p base_tile.
/// @details Tile animations are keyed by their first-frame tile id. The
///          table is small (typically < 32 entries) so a linear scan is
///          cheaper than maintaining a hash. Returns NULL if no matching
///          animation exists or if @p tm is NULL.
static tm_tile_anim *find_tile_anim(rt_tilemap_impl *tm, int64_t base_tile) {
    if (!tm)
        return NULL;
    for (int32_t i = 0; i < tm->tile_anim_count; i++) {
        if (tm->tile_anims[i].base_tile_id == base_tile)
            return &tm->tile_anims[i];
    }
    return NULL;
}

/// @brief Serialize the tilemap to a JSON file at `path`. Includes version (1), dimensions,
/// tile size, every layer's data + tileset reference, tile properties, and autotile rules.
/// Returns 1 on success, 0 on null inputs / missing path / I/O error.
int8_t rt_tilemap_save_to_file(void *tm, rt_string path) {
    rt_tilemap_impl *tilemap = tilemap_io_checked(tm);
    if (!tilemap || !path)
        return 0;
    const char *cpath = rt_string_cstr(path);
    if (!cpath)
        return 0;

    int64_t w = rt_tilemap_get_width(tm);
    int64_t h = rt_tilemap_get_height(tm);
    int64_t tw = rt_tilemap_get_tile_width(tm);
    int64_t th = rt_tilemap_get_tile_height(tm);
    int64_t layer_count = rt_tilemap_get_layer_count(tm);

    int8_t result = 0;

    // Build JSON object using Map
    void *root = rt_map_new();
    rt_string json = NULL;
    if (!root)
        return 0;
    rt_map_set_int(root, rt_const_cstr("version"), 1);
    rt_map_set_int(root, rt_const_cstr("width"), w);
    rt_map_set_int(root, rt_const_cstr("height"), h);
    rt_map_set_int(root, rt_const_cstr("tileWidth"), tw);
    rt_map_set_int(root, rt_const_cstr("tileHeight"), th);
    if (tilemap->tileset) {
        void *tileset_obj = serialize_pixels_blob(tilemap->tileset);
        if (tileset_obj)
            map_set_owned(root, "tileset", tileset_obj);
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
                seq_push_owned(tiles_arr, rt_box_i64(t));
            }
        }
        map_set_owned(layer_obj, "tiles", tiles_arr);
        rt_map_set_int(layer_obj, rt_const_cstr("visible"), rt_tilemap_get_layer_visible(tm, li));
        map_set_string_copy(layer_obj, "name", tilemap->layers[li].name);
        if (li > 0 && tilemap->layers[li].tileset) {
            void *tileset_obj = serialize_pixels_blob(tilemap->layers[li].tileset);
            if (tileset_obj)
                map_set_owned(layer_obj, "tileset", tileset_obj);
        }
        seq_push_owned(layers_arr, layer_obj);
    }
    map_set_owned(root, "layers", layers_arr);

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
        seq_push_owned(types_arr, entry);
    }
    map_set_owned(coll_obj, "types", types_arr);
    map_set_owned(root, "collision", coll_obj);

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
            seq_push_owned(entries, entry);
        }
        map_set_owned(prop_obj, "entries", entries);
        seq_push_owned(props_arr, prop_obj);
    }
    map_set_owned(root, "tileProperties", props_arr);

    void *autotile_arr = seq_new_owned();
    for (int32_t i = 0; i < tilemap->autotile_count; i++) {
        autotile_rule *rule = &tilemap->autotile_rules[i];
        if (!rule->active)
            continue;
        void *rule_obj = rt_map_new();
        void *variants = seq_new_owned();
        rt_map_set_int(rule_obj, rt_const_cstr("baseTile"), rule->base_tile);
        for (int32_t v = 0; v < 16; v++)
            seq_push_owned(variants, rt_box_i64(rule->variants[v]));
        map_set_owned(rule_obj, "variants", variants);
        seq_push_owned(autotile_arr, rule_obj);
    }
    map_set_owned(root, "autotiles", autotile_arr);

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
            seq_push_owned(frames, rt_box_i64(anim->frame_tiles[fidx]));
        map_set_owned(anim_obj, "frames", frames);
        seq_push_owned(anim_arr, anim_obj);
    }
    map_set_owned(root, "animations", anim_arr);

    // Format as JSON
    json = rt_json_format_pretty(root, 2);
    if (!json)
        goto cleanup;

    const char *json_cstr = rt_string_cstr(json);
    if (!json_cstr)
        goto cleanup;

    FILE *f = fopen(cpath, "wb");
    if (!f)
        goto cleanup;
    size_t len = strlen(json_cstr);
    size_t written = fwrite(json_cstr, 1, len, f);
    fclose(f);

    result = written == len ? 1 : 0;

cleanup:
    tilemap_io_release_ref((void **)&json);
    tilemap_io_release_ref(&root);
    return result;
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

    rt_string json_str = NULL;
    void *root = NULL;
    void *tm = NULL;
    void *result = NULL;

    // Read file contents
    FILE *f = fopen(cpath, "rb");
    if (!f)
        return NULL;
    if (tmio_fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    int64_t file_size = (int64_t)tmio_ftell(f);
    if (tmio_fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if (file_size <= 0) {
        fclose(f);
        return NULL;
    }
    if ((uint64_t)file_size > SIZE_MAX - 1u) {
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
    if (read != (size_t)file_size) {
        free(buf);
        return NULL;
    }
    buf[read] = '\0';

    json_str = rt_string_from_bytes(buf, read);
    free(buf);
    if (!json_str)
        goto cleanup;

    root = rt_json_parse(json_str);
    if (!root)
        goto cleanup;

    // Extract dimensions (JSON numbers are boxed as f64)
    int64_t w = (int64_t)rt_map_get_float(root, rt_const_cstr("width"));
    int64_t h = (int64_t)rt_map_get_float(root, rt_const_cstr("height"));
    int64_t tw = (int64_t)rt_map_get_float(root, rt_const_cstr("tileWidth"));
    int64_t th = (int64_t)rt_map_get_float(root, rt_const_cstr("tileHeight"));
    if (w <= 0 || h <= 0 || tw <= 0 || th <= 0)
        goto cleanup;
    if (w > INT64_MAX / h)
        goto cleanup;
    int64_t expected_tiles = w * h;

    tm = rt_tilemap_new(w, h, tw, th);
    if (!tm)
        goto cleanup;
    rt_tilemap_impl *tilemap = (rt_tilemap_impl *)tm;

    void *tileset_blob = rt_map_get(root, rt_const_cstr("tileset"));
    if (tileset_blob) {
        void *pixels = deserialize_pixels_blob(tileset_blob);
        if (!pixels) {
            goto cleanup;
        }
        assign_base_tileset(tilemap, pixels);
    }

    // Load layers
    void *layers_arr = rt_map_get(root, rt_const_cstr("layers"));
    if (layers_arr) {
        int64_t lcount = rt_seq_len(layers_arr);
        if (lcount > TM_MAX_LAYERS)
            lcount = TM_MAX_LAYERS;
        for (int64_t li = 0; li < lcount; li++) {
            int64_t layer_index = li;
            // Add layer if not layer 0
            if (li > 0) {
                layer_index = rt_tilemap_add_layer(tm, rt_const_cstr(""));
                if (layer_index < 0)
                    continue;
            }

            void *layer_obj = rt_seq_get(layers_arr, li);
            if (!layer_obj)
                continue;

            void *tiles_arr = rt_map_get(layer_obj, rt_const_cstr("tiles"));
            if (tiles_arr) {
                int64_t tcount = rt_seq_len(tiles_arr);
                if (tcount != expected_tiles) {
                    goto cleanup;
                }
                for (int64_t ti = 0; ti < tcount; ti++) {
                    void *tval = rt_seq_get(tiles_arr, ti);
                    if (!tval)
                        continue;
                    int64_t tile = (int64_t)rt_unbox_f64(tval);
                    int64_t tx = ti % w;
                    int64_t ty = ti / w;
                    rt_tilemap_set_tile_layer(tm, layer_index, tx, ty, tile);
                }
            }

            int64_t vis = (int64_t)rt_map_get_float(layer_obj, rt_const_cstr("visible"));
            rt_tilemap_set_layer_visible(tm, layer_index, (int8_t)vis);
            rt_string lname = (rt_string)rt_map_get(layer_obj, rt_const_cstr("name"));
            if (lname) {
                const char *name_cstr = rt_string_cstr(lname);
                if (name_cstr) {
                    memset(tilemap->layers[layer_index].name,
                           0,
                           sizeof(tilemap->layers[layer_index].name));
                    strncpy(tilemap->layers[layer_index].name,
                            name_cstr,
                            sizeof(tilemap->layers[layer_index].name) - 1);
                }
            }
            if (layer_index > 0) {
                void *layer_tileset = rt_map_get(layer_obj, rt_const_cstr("tileset"));
                if (layer_tileset) {
                    void *pixels = deserialize_pixels_blob(layer_tileset);
                    if (!pixels) {
                        goto cleanup;
                    }
                    assign_layer_tileset(tilemap, layer_index, pixels);
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
            if (rt_seq_len(frames) < frame_count)
                continue;
            rt_tilemap_set_tile_anim(tm, base_tile, frame_count, ms_per_frame);
            for (int64_t fi = 0; fi < frame_count; fi++) {
                void *boxed = rt_seq_get(frames, fi);
                if (boxed)
                    rt_tilemap_set_tile_anim_frame(tm, base_tile, fi, (int64_t)rt_unbox_f64(boxed));
            }
            {
                tm_tile_anim *anim = find_tile_anim(tilemap, base_tile);
                if (!anim)
                    continue;
                anim->timer = map_get_i64(anim_obj, "timer");
                int64_t current = map_get_i64(anim_obj, "currentFrame");
                if (anim->frame_count > 0) {
                    current %= anim->frame_count;
                    if (current < 0)
                        current += anim->frame_count;
                    anim->current_frame = (int32_t)current;
                } else {
                    anim->current_frame = 0;
                }
            }
        }
    }

    result = tm;
    tm = NULL;

cleanup:
    tilemap_io_release_ref(&tm);
    tilemap_io_release_ref(&root);
    tilemap_io_release_ref((void **)&json_str);
    return result;
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
            int64_t val = 0;
            errno = 0;
            char *end = tok;
            long long parsed = strtoll(tok, &end, 10);
            if (end != tok) {
                if (errno == ERANGE)
                    val = parsed < 0 ? INT64_MIN : INT64_MAX;
                else
                    val = (int64_t)parsed;
                tok = end;
            }

            rt_tilemap_set_tile(tm, x, y, val);
            x++;

            while (*tok && *tok != ',')
                tok++;
            if (*tok == ',')
                tok++;
        }
        y++;
    }

    fclose(f);
    return tm;
}
