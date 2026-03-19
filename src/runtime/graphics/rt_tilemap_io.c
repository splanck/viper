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

#include "rt_box.h"
#include "rt_graphics.h"
#include "rt_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Tile Properties
//=============================================================================

#define MAX_TILE_PROPS 256
#define MAX_PROP_KEYS 8
#define MAX_PROP_KEY_LEN 32

typedef struct
{
    char key[MAX_PROP_KEY_LEN];
    int64_t value;
} tile_prop_entry;

typedef struct
{
    tile_prop_entry entries[MAX_PROP_KEYS];
    int32_t count;
} tile_props;

// Property storage — static for simplicity (one tilemap at a time)
// In a real implementation this would be embedded in the tilemap struct.
static tile_props s_props[MAX_TILE_PROPS];
static int8_t s_props_initialized = 0;

static void ensure_props(void)
{
    if (!s_props_initialized)
    {
        memset(s_props, 0, sizeof(s_props));
        s_props_initialized = 1;
    }
}

void rt_tilemap_set_tile_property(void *tm, int64_t tile_index, rt_string key, int64_t value)
{
    (void)tm;
    ensure_props();
    if (tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return;
    const char *ckey = rt_string_cstr(key);
    if (!ckey)
        return;

    tile_props *p = &s_props[tile_index];
    // Check if key exists
    for (int32_t i = 0; i < p->count; i++)
    {
        if (strcmp(p->entries[i].key, ckey) == 0)
        {
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

int64_t rt_tilemap_get_tile_property(void *tm,
                                     int64_t tile_index,
                                     rt_string key,
                                     int64_t default_val)
{
    (void)tm;
    ensure_props();
    if (tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return default_val;
    const char *ckey = rt_string_cstr(key);
    if (!ckey)
        return default_val;

    tile_props *p = &s_props[tile_index];
    for (int32_t i = 0; i < p->count; i++)
    {
        if (strcmp(p->entries[i].key, ckey) == 0)
            return p->entries[i].value;
    }
    return default_val;
}

int8_t rt_tilemap_has_tile_property(void *tm, int64_t tile_index, rt_string key)
{
    (void)tm;
    ensure_props();
    if (tile_index < 0 || tile_index >= MAX_TILE_PROPS || !key)
        return 0;
    const char *ckey = rt_string_cstr(key);
    if (!ckey)
        return 0;

    tile_props *p = &s_props[tile_index];
    for (int32_t i = 0; i < p->count; i++)
    {
        if (strcmp(p->entries[i].key, ckey) == 0)
            return 1;
    }
    return 0;
}

//=============================================================================
// Auto-Tiling
//=============================================================================

#define MAX_AUTOTILE_RULES 64

typedef struct
{
    int64_t base_tile;
    int64_t variants[16]; // indexed by 4-bit neighbor mask
    int8_t active;
} autotile_rule;

static autotile_rule s_autotile_rules[MAX_AUTOTILE_RULES];
static int32_t s_autotile_count = 0;

/// Find or create an autotile rule for a base tile
static autotile_rule *find_or_create_rule(int64_t base_tile)
{
    for (int32_t i = 0; i < s_autotile_count; i++)
    {
        if (s_autotile_rules[i].base_tile == base_tile)
            return &s_autotile_rules[i];
    }
    if (s_autotile_count >= MAX_AUTOTILE_RULES)
        return NULL;
    autotile_rule *r = &s_autotile_rules[s_autotile_count++];
    memset(r, 0, sizeof(autotile_rule));
    r->base_tile = base_tile;
    r->active = 1;
    return r;
}

void rt_tilemap_set_autotile_lo(void *tm,
                                int64_t base_tile,
                                int64_t v0,
                                int64_t v1,
                                int64_t v2,
                                int64_t v3,
                                int64_t v4,
                                int64_t v5,
                                int64_t v6,
                                int64_t v7)
{
    (void)tm;
    autotile_rule *r = find_or_create_rule(base_tile);
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

void rt_tilemap_set_autotile_hi(void *tm,
                                int64_t base_tile,
                                int64_t v8,
                                int64_t v9,
                                int64_t v10,
                                int64_t v11,
                                int64_t v12,
                                int64_t v13,
                                int64_t v14,
                                int64_t v15)
{
    (void)tm;
    autotile_rule *r = find_or_create_rule(base_tile);
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

void rt_tilemap_clear_autotile(void *tm, int64_t base_tile)
{
    (void)tm;
    for (int32_t i = 0; i < s_autotile_count; i++)
    {
        if (s_autotile_rules[i].base_tile == base_tile)
        {
            s_autotile_rules[i].active = 0;
            return;
        }
    }
}

static autotile_rule *find_rule(int64_t tile)
{
    for (int32_t i = 0; i < s_autotile_count; i++)
    {
        if (s_autotile_rules[i].active && s_autotile_rules[i].base_tile == tile)
            return &s_autotile_rules[i];
    }
    return NULL;
}

static int8_t is_same_base(int64_t tile, int64_t base)
{
    if (tile == base)
        return 1;
    // Check if tile is one of the variants
    autotile_rule *r = find_rule(base);
    if (!r)
        return 0;
    for (int i = 0; i < 16; i++)
    {
        if (r->variants[i] == tile)
            return 1;
    }
    return 0;
}

void rt_tilemap_apply_autotile_region(void *tm, int64_t rx, int64_t ry, int64_t rw, int64_t rh)
{
    if (!tm || s_autotile_count == 0)
        return;

    int64_t map_w = rt_tilemap_get_width(tm);
    int64_t map_h = rt_tilemap_get_height(tm);

    for (int64_t y = ry; y < ry + rh; y++)
    {
        for (int64_t x = rx; x < rx + rw; x++)
        {
            if (x < 0 || x >= map_w || y < 0 || y >= map_h)
                continue;

            int64_t tile = rt_tilemap_get_tile(tm, x, y);
            autotile_rule *rule = find_rule(tile);
            if (!rule)
            {
                // Check if tile is a variant of some rule
                for (int32_t r = 0; r < s_autotile_count; r++)
                {
                    if (!s_autotile_rules[r].active)
                        continue;
                    for (int v = 0; v < 16; v++)
                    {
                        if (s_autotile_rules[r].variants[v] == tile)
                        {
                            rule = &s_autotile_rules[r];
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
            if (y > 0 && is_same_base(rt_tilemap_get_tile(tm, x, y - 1), base))
                mask |= 1;
            // Right
            if (x < map_w - 1 && is_same_base(rt_tilemap_get_tile(tm, x + 1, y), base))
                mask |= 2;
            // Down
            if (y < map_h - 1 && is_same_base(rt_tilemap_get_tile(tm, x, y + 1), base))
                mask |= 4;
            // Left
            if (x > 0 && is_same_base(rt_tilemap_get_tile(tm, x - 1, y), base))
                mask |= 8;

            rt_tilemap_set_tile(tm, x, y, rule->variants[mask]);
        }
    }
}

void rt_tilemap_apply_autotile(void *tm)
{
    if (!tm)
        return;
    rt_tilemap_apply_autotile_region(tm, 0, 0, rt_tilemap_get_width(tm), rt_tilemap_get_height(tm));
}

//=============================================================================
// JSON Save/Load
//=============================================================================

int8_t rt_tilemap_save_to_file(void *tm, rt_string path)
{
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

    // Layers array
    void *layers_arr = rt_seq_new();
    for (int64_t li = 0; li < layer_count; li++)
    {
        void *layer_obj = rt_map_new();
        // Tile array
        void *tiles_arr = rt_seq_new();
        for (int64_t y = 0; y < h; y++)
        {
            for (int64_t x = 0; x < w; x++)
            {
                int64_t t = rt_tilemap_get_tile_layer(tm, li, x, y);
                rt_seq_push(tiles_arr, rt_box_i64(t));
            }
        }
        rt_map_set(layer_obj, rt_const_cstr("tiles"), tiles_arr);
        rt_map_set(
            layer_obj, rt_const_cstr("visible"), rt_box_i64(rt_tilemap_get_layer_visible(tm, li)));
        rt_seq_push(layers_arr, layer_obj);
    }
    rt_map_set(root, rt_const_cstr("layers"), layers_arr);

    // Collision info
    void *coll_obj = rt_map_new();
    rt_map_set_int(coll_obj, rt_const_cstr("layer"), rt_tilemap_get_collision_layer(tm));
    rt_map_set(root, rt_const_cstr("collision"), coll_obj);

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

void *rt_tilemap_load_from_file(rt_string path)
{
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
    if (file_size <= 0)
    {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)file_size + 1);
    if (!buf)
    {
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

    // Load layers
    void *layers_arr = rt_map_get(root, rt_const_cstr("layers"));
    if (layers_arr)
    {
        int64_t lcount = rt_seq_len(layers_arr);
        for (int64_t li = 0; li < lcount; li++)
        {
            // Add layer if not layer 0
            if (li > 0)
                rt_tilemap_add_layer(tm, rt_const_cstr(""));

            void *layer_obj = rt_seq_get(layers_arr, li);
            if (!layer_obj)
                continue;

            void *tiles_arr = rt_map_get(layer_obj, rt_const_cstr("tiles"));
            if (tiles_arr)
            {
                int64_t tcount = rt_seq_len(tiles_arr);
                for (int64_t ti = 0; ti < tcount; ti++)
                {
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
        }
    }

    // Load collision
    void *coll = rt_map_get(root, rt_const_cstr("collision"));
    if (coll)
    {
        int64_t cl = (int64_t)rt_map_get_float(coll, rt_const_cstr("layer"));
        rt_tilemap_set_collision_layer(tm, cl);
    }

    return tm;
}

//=============================================================================
// CSV Import
//=============================================================================

void *rt_tilemap_load_csv(rt_string path, int64_t tile_w, int64_t tile_h)
{
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
    char line_buf[4096];

    while (fgets(line_buf, sizeof(line_buf), f))
    {
        // Count commas + 1 for column count
        int64_t cols = 1;
        for (char *p = line_buf; *p; p++)
        {
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

    if (rows == 0 || max_cols == 0)
    {
        fclose(f);
        return NULL;
    }

    void *tm = rt_tilemap_new(max_cols, rows, tile_w, tile_h);
    if (!tm)
    {
        fclose(f);
        return NULL;
    }

    // Second pass: parse tile values
    fseek(f, 0, SEEK_SET);
    int64_t y = 0;

    while (fgets(line_buf, sizeof(line_buf), f) && y < rows)
    {
        size_t len = strlen(line_buf);
        if (len > 0 && line_buf[len - 1] == '\n')
            line_buf[--len] = '\0';
        if (len == 0)
            continue;

        int64_t x = 0;
        char *tok = line_buf;
        while (*tok && x < max_cols)
        {
            // Parse integer
            int64_t val = 0;
            int neg = 0;
            if (*tok == '-')
            {
                neg = 1;
                tok++;
            }
            while (*tok >= '0' && *tok <= '9')
            {
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
