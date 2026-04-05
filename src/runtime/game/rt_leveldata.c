//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_leveldata.c
// Purpose: JSON level loader. Parses level file with tilemap data + objects.
//
// Level JSON format:
//   { "width": N, "height": N, "tileWidth": N, "tileHeight": N,
//     "properties": { "theme": "...", "playerStartX": N, "playerStartY": N },
//     "layers": [{ "name": "terrain", "type": "tiles", "data": [0,1,...] }],
//     "objects": [{ "type": "enemy", "id": "slime", "x": N, "y": N }, ...] }
//
//===----------------------------------------------------------------------===//

#include "rt_leveldata.h"
#include "rt_object.h"
#include "rt_string.h"

#include <stdlib.h>
#include <string.h>

// JSON and file I/O externs
extern void *rt_json_parse(rt_string text);
extern rt_string rt_io_file_read_all_text(rt_string path);
extern void *rt_jsonpath_get(void *root, rt_string path);
extern rt_string rt_jsonpath_get_str(void *root, rt_string path);
extern int64_t rt_jsonpath_get_int(void *root, rt_string path);
extern rt_string rt_const_cstr(const char *s);
extern rt_string rt_string_from_bytes(const char *data, size_t len);

// Map/Seq accessors
extern void *rt_map_get(void *map, void *key);
extern int64_t rt_seq_len(void *seq);
extern void *rt_seq_get(void *seq, int64_t idx);
extern int64_t rt_unbox_i64(void *obj);
extern double rt_unbox_f64(void *obj);
extern int64_t rt_box_type(void *obj);

// Safe integer extraction from JSON value (may be boxed as i64 or f64)
static int64_t json_val_to_i64(void *val) {
    if (!val)
        return 0;
    int64_t tag = rt_box_type(val);
    if (tag == 0)
        return rt_unbox_i64(val); // RT_BOX_I64 = 0
    if (tag == 1)
        return (int64_t)rt_unbox_f64(val); // RT_BOX_F64 = 1
    return 0;
}

// Tilemap creation
extern void *rt_tilemap_new(int64_t w, int64_t h, int64_t tw, int64_t th);
extern void rt_tilemap_set_tile(void *tm, int64_t x, int64_t y, int64_t tile);

#define LEVEL_MAX_OBJECTS 512

typedef struct {
    char type[32];
    char id[32];
    int64_t x, y;
} level_object_t;

typedef struct {
    void *tilemap;
    level_object_t objects[LEVEL_MAX_OBJECTS];
    int32_t object_count;
    int64_t player_start_x;
    int64_t player_start_y;
    char theme[32];
} leveldata_impl;

static leveldata_impl *get(void *level) {
    return (leveldata_impl *)level;
}

/// @brief Load a level from a JSON file, creating a tilemap and extracting objects.
/// @details Parses a JSON level file with "width", "height", "tileWidth", "tileHeight",
///          "layers" (tile data), "objects" (entity spawn points), and "properties"
///          (playerStartX/Y, theme). Creates a tilemap from tile layer data and stores
///          up to 256 named objects with type, id, and position.
void *rt_leveldata_load(void *path) {
    if (!path)
        return NULL;

    // Read file
    rt_string text = rt_io_file_read_all_text((rt_string)path);
    if (!text)
        return NULL;

    // Parse JSON
    void *root = rt_json_parse(text);
    if (!root)
        return NULL;

    // Read dimensions
    int64_t w = rt_jsonpath_get_int(root, rt_const_cstr("width"));
    int64_t h = rt_jsonpath_get_int(root, rt_const_cstr("height"));
    int64_t tw = rt_jsonpath_get_int(root, rt_const_cstr("tileWidth"));
    int64_t th = rt_jsonpath_get_int(root, rt_const_cstr("tileHeight"));
    if (w <= 0 || h <= 0)
        return NULL;
    if (tw <= 0)
        tw = 32;
    if (th <= 0)
        th = 32;

    // Create level data
    leveldata_impl *ld = (leveldata_impl *)rt_obj_new_i64(0, (int64_t)sizeof(leveldata_impl));
    if (!ld)
        return NULL;
    memset(ld, 0, sizeof(leveldata_impl));

    // Read properties
    void *props = rt_jsonpath_get(root, rt_const_cstr("properties"));
    if (props) {
        rt_string theme = rt_jsonpath_get_str(props, rt_const_cstr("theme"));
        if (theme) {
            const char *ct = rt_string_cstr(theme);
            if (ct) {
                strncpy(ld->theme, ct, 31);
                ld->theme[31] = '\0';
            }
        }
        ld->player_start_x = rt_jsonpath_get_int(props, rt_const_cstr("playerStartX"));
        ld->player_start_y = rt_jsonpath_get_int(props, rt_const_cstr("playerStartY"));
    }

    // Create tilemap
    ld->tilemap = rt_tilemap_new(w, h, tw, th);

    // Read tile layers
    void *layers = rt_jsonpath_get(root, rt_const_cstr("layers"));
    if (layers) {
        int64_t layerCount = rt_seq_len(layers);
        for (int64_t li = 0; li < layerCount; li++) {
            void *layer = rt_seq_get(layers, li);
            if (!layer)
                continue;
            rt_string layerType = rt_jsonpath_get_str(layer, rt_const_cstr("type"));
            if (!layerType)
                continue;
            const char *typeStr = rt_string_cstr(layerType);
            if (!typeStr || strcmp(typeStr, "tiles") != 0)
                continue;

            void *data = rt_jsonpath_get(layer, rt_const_cstr("data"));
            if (!data)
                continue;
            int64_t dataLen = rt_seq_len(data);
            for (int64_t i = 0; i < dataLen && i < w * h; i++) {
                void *val = rt_seq_get(data, i);
                if (val) {
                    int64_t tile = json_val_to_i64(val);
                    rt_tilemap_set_tile(ld->tilemap, i % w, i / w, tile);
                }
            }
        }
    }

    // Read objects
    void *objects = rt_jsonpath_get(root, rt_const_cstr("objects"));
    if (objects) {
        int64_t objCount = rt_seq_len(objects);
        for (int64_t i = 0; i < objCount && ld->object_count < LEVEL_MAX_OBJECTS; i++) {
            void *obj = rt_seq_get(objects, i);
            if (!obj)
                continue;

            level_object_t *lo = &ld->objects[ld->object_count];
            memset(lo, 0, sizeof(level_object_t));

            rt_string otype = rt_jsonpath_get_str(obj, rt_const_cstr("type"));
            rt_string oid = rt_jsonpath_get_str(obj, rt_const_cstr("id"));
            if (otype) {
                const char *s = rt_string_cstr(otype);
                if (s) {
                    strncpy(lo->type, s, 31);
                    lo->type[31] = '\0';
                }
            }
            if (oid) {
                const char *s = rt_string_cstr(oid);
                if (s) {
                    strncpy(lo->id, s, 31);
                    lo->id[31] = '\0';
                }
            }
            lo->x = rt_jsonpath_get_int(obj, rt_const_cstr("x"));
            lo->y = rt_jsonpath_get_int(obj, rt_const_cstr("y"));
            ld->object_count++;
        }
    }

    return ld;
}

/// @brief Get the tilemap created from the level's tile layers.
void *rt_leveldata_get_tilemap(void *level) {
    return level ? get(level)->tilemap : NULL;
}

/// @brief Get the number of objects (entity spawn points) in the level.
int64_t rt_leveldata_object_count(void *level) {
    return level ? get(level)->object_count : 0;
}

/// @brief Get the type string of an object at the given index (e.g., "enemy", "item").
rt_string rt_leveldata_object_type(void *level, int64_t index) {
    if (!level || index < 0 || index >= get(level)->object_count)
        return rt_const_cstr("");
    return rt_const_cstr(get(level)->objects[index].type);
}

/// @brief Get the ID string of an object at the given index.
rt_string rt_leveldata_object_id(void *level, int64_t index) {
    if (!level || index < 0 || index >= get(level)->object_count)
        return rt_const_cstr("");
    return rt_const_cstr(get(level)->objects[index].id);
}

/// @brief Get the X position of an object at the given index.
int64_t rt_leveldata_object_x(void *level, int64_t index) {
    if (!level || index < 0 || index >= get(level)->object_count)
        return 0;
    return get(level)->objects[index].x;
}

/// @brief Get the Y position of an object at the given index.
int64_t rt_leveldata_object_y(void *level, int64_t index) {
    if (!level || index < 0 || index >= get(level)->object_count)
        return 0;
    return get(level)->objects[index].y;
}

/// @brief Get the player's starting X position from the level properties.
int64_t rt_leveldata_player_start_x(void *level) {
    return level ? get(level)->player_start_x : 0;
}

/// @brief Get the player's starting Y position from the level properties.
int64_t rt_leveldata_player_start_y(void *level) {
    return level ? get(level)->player_start_y : 0;
}

/// @brief Get the theme name from the level properties (e.g., "forest", "cave").
rt_string rt_leveldata_get_theme(void *level) {
    if (!level)
        return rt_const_cstr("");
    return rt_const_cstr(get(level)->theme);
}
