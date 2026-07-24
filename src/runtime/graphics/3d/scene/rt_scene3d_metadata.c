//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_metadata.c
// Purpose: Bounded typed gameplay metadata for Graphics3D SceneNode values.
// Key invariants:
//   - Entries remain sorted by exact UTF-8 key bytes for deterministic saves.
//   - Mutations allocate replacement storage before publishing any change.
//   - Keys, strings, entry counts, and floating-point values are bounded.
// Ownership/Lifetime:
//   - Each SceneNode owns its native entry array, keys, and string values.
//   - Returned runtime strings and sequences follow normal caller ownership.
// Links: rt_scene3d.h, rt_scene3d_internal.h,
//   docs/adr/0159-typed-scenenode-metadata-and-vscn-v6.md
//
//===----------------------------------------------------------------------===//

#ifdef ZANNA_ENABLE_GRAPHICS

#include "rt_canvas3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// @brief Borrow one validated SceneNode payload.
static rt_scene_node3d *metadata_node(void *obj) {
    return (rt_scene_node3d *)rt_g3d_checked_or_null(obj, RT_G3D_SCENENODE3D_CLASS_ID);
}

/// @brief Reject structurally corrupt table bounds before indexing native data.
static int metadata_table_valid(const rt_scene_node3d *node) {
    if (!node || node->metadata_count < 0 || node->metadata_capacity < 0 ||
        node->metadata_count > node->metadata_capacity ||
        node->metadata_capacity > RT_SCENE_NODE3D_MAX_METADATA_ENTRIES)
        return 0;
    if (node->metadata_capacity > 0 && !node->metadata)
        return 0;
    return 1;
}

/// @brief Validate one runtime string as bounded, NUL-free UTF-8 bytes.
static int metadata_string_view(
    rt_string value, int32_t maximum, int allow_empty, const char **out_data, int32_t *out_length) {
    const char *data;
    int64_t length;
    if (!value || !rt_string_is_handle(value) || !out_data || !out_length)
        return 0;
    length = rt_str_len(value);
    data = rt_string_cstr(value);
    if (!data || length < (allow_empty ? 0 : 1) || length > maximum ||
        memchr(data, '\0', (size_t)length) != NULL)
        return 0;
    *out_data = data;
    *out_length = (int32_t)length;
    return 1;
}

/// @brief Compare exact byte strings without relying on NUL termination.
static int metadata_key_compare(const char *left,
                                int32_t left_length,
                                const char *right,
                                int32_t right_length) {
    int32_t common = left_length < right_length ? left_length : right_length;
    int compared = common > 0 ? memcmp(left, right, (size_t)common) : 0;
    if (compared != 0)
        return compared;
    if (left_length < right_length)
        return -1;
    if (left_length > right_length)
        return 1;
    return 0;
}

/// @brief Locate a key and its insertion point in the sorted table.
static int32_t metadata_find(const rt_scene_node3d *node,
                             const char *key,
                             int32_t key_length,
                             int *out_found) {
    int32_t low = 0;
    int32_t high = node && node->metadata_count > 0 ? node->metadata_count : 0;
    while (low < high) {
        int32_t middle = low + (high - low) / 2;
        const rt_scene3d_metadata_entry *entry = &node->metadata[middle];
        int compared = metadata_key_compare(entry->key, entry->key_length, key, key_length);
        if (compared < 0)
            low = middle + 1;
        else
            high = middle;
    }
    if (out_found) {
        *out_found =
            node && low < node->metadata_count &&
            metadata_key_compare(
                node->metadata[low].key, node->metadata[low].key_length, key, key_length) == 0;
    }
    return low;
}

/// @brief Allocate one NUL-terminated native byte copy.
static char *metadata_copy_bytes(const char *data, int32_t length) {
    char *copy;
    if (!data || length < 0 || (size_t)length > SIZE_MAX - 1u)
        return NULL;
    copy = (char *)malloc((size_t)length + 1u);
    if (!copy)
        return NULL;
    if (length > 0)
        memcpy(copy, data, (size_t)length);
    copy[length] = '\0';
    return copy;
}

/// @brief Release only the value payload of one entry.
static void metadata_release_value(rt_scene3d_metadata_entry *entry) {
    if (!entry)
        return;
    if (entry->kind == RT_SCENE3D_METADATA_STRING) {
        free(entry->value.string_value.data);
        entry->value.string_value.data = NULL;
        entry->value.string_value.length = 0;
    }
}

/// @brief Grow the bounded table without publishing a partial allocation.
static int metadata_reserve(rt_scene_node3d *node, int32_t needed) {
    int32_t capacity;
    rt_scene3d_metadata_entry *grown;
    if (!node || needed < 0 || needed > RT_SCENE_NODE3D_MAX_METADATA_ENTRIES)
        return 0;
    if (node->metadata_capacity >= needed)
        return 1;
    capacity = node->metadata_capacity > 0 ? node->metadata_capacity * 2 : 8;
    if (capacity < needed)
        capacity = needed;
    if (capacity > RT_SCENE_NODE3D_MAX_METADATA_ENTRIES)
        capacity = RT_SCENE_NODE3D_MAX_METADATA_ENTRIES;
    grown = (rt_scene3d_metadata_entry *)realloc(
        node->metadata, (size_t)capacity * sizeof(rt_scene3d_metadata_entry));
    if (!grown)
        return 0;
    node->metadata = grown;
    node->metadata_capacity = capacity;
    return 1;
}

/// @brief Publish a validated scalar as an insert or transactional replacement.
static int8_t metadata_set(rt_scene_node3d *node,
                           const char *key,
                           int32_t key_length,
                           rt_scene3d_metadata_kind kind,
                           int8_t bool_value,
                           int64_t int_value,
                           double float_value,
                           const char *string_value,
                           int32_t string_length) {
    int found = 0;
    int32_t index;
    char *new_key = NULL;
    char *new_string = NULL;
    rt_scene3d_metadata_entry replacement;
    if (!metadata_table_valid(node) || !key || key_length <= 0 ||
        key_length > RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES)
        return 0;
    if (kind == RT_SCENE3D_METADATA_FLOAT && !isfinite(float_value))
        return 0;
    if (kind == RT_SCENE3D_METADATA_STRING) {
        if (!string_value || string_length < 0 ||
            string_length > RT_SCENE_NODE3D_MAX_METADATA_STRING_BYTES)
            return 0;
        new_string = metadata_copy_bytes(string_value, string_length);
        if (!new_string)
            return 0;
    }

    index = metadata_find(node, key, key_length, &found);
    memset(&replacement, 0, sizeof(replacement));
    replacement.kind = kind;
    if (kind == RT_SCENE3D_METADATA_BOOL)
        replacement.value.bool_value = bool_value ? 1 : 0;
    else if (kind == RT_SCENE3D_METADATA_INT)
        replacement.value.int_value = int_value;
    else if (kind == RT_SCENE3D_METADATA_FLOAT)
        replacement.value.float_value = float_value;
    else if (kind == RT_SCENE3D_METADATA_STRING) {
        replacement.value.string_value.data = new_string;
        replacement.value.string_value.length = string_length;
    }

    if (found) {
        replacement.key = node->metadata[index].key;
        replacement.key_length = node->metadata[index].key_length;
        metadata_release_value(&node->metadata[index]);
        node->metadata[index] = replacement;
        return 1;
    }
    if (node->metadata_count >= RT_SCENE_NODE3D_MAX_METADATA_ENTRIES ||
        !metadata_reserve(node, node->metadata_count + 1)) {
        free(new_string);
        return 0;
    }
    new_key = metadata_copy_bytes(key, key_length);
    if (!new_key) {
        free(new_string);
        return 0;
    }
    if (index < node->metadata_count) {
        memmove(&node->metadata[index + 1],
                &node->metadata[index],
                (size_t)(node->metadata_count - index) * sizeof(rt_scene3d_metadata_entry));
    }
    replacement.key = new_key;
    replacement.key_length = key_length;
    node->metadata[index] = replacement;
    node->metadata_count++;
    return 1;
}

/// @brief Validate a public metadata key and find it.
static const rt_scene3d_metadata_entry *metadata_get_entry(void *obj,
                                                           rt_string key,
                                                           rt_scene_node3d **out_node,
                                                           int32_t *out_index) {
    rt_scene_node3d *node = metadata_node(obj);
    const char *key_data;
    int32_t key_length;
    int found = 0;
    int32_t index;
    if (out_node)
        *out_node = node;
    if (!metadata_table_valid(node) ||
        !metadata_string_view(
            key, RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES, 0, &key_data, &key_length))
        return NULL;
    index = metadata_find(node, key_data, key_length, &found);
    if (out_index)
        *out_index = index;
    return found ? &node->metadata[index] : NULL;
}

/// @brief Release every native allocation owned by one node metadata table.
void rt_scene_node3d_metadata_clear_internal(rt_scene_node3d *node) {
    if (!node)
        return;
    int32_t count = metadata_table_valid(node) ? node->metadata_count : 0;
    for (int32_t index = 0; index < count; ++index) {
        free(node->metadata[index].key);
        node->metadata[index].key = NULL;
        metadata_release_value(&node->metadata[index]);
    }
    free(node->metadata);
    node->metadata = NULL;
    node->metadata_count = 0;
    node->metadata_capacity = 0;
}

void *rt_scene_node3d_metadata_keys(void *obj) {
    rt_scene_node3d *node = metadata_node(obj);
    void *keys = rt_seq_new_owned();
    if (!metadata_table_valid(node))
        return keys;
    for (int32_t index = 0; index < node->metadata_count; ++index) {
        rt_string key = rt_string_from_bytes(node->metadata[index].key,
                                             (size_t)node->metadata[index].key_length);
        if (!key)
            continue;
        rt_seq_push(keys, key);
        rt_string_unref(key);
    }
    return keys;
}

rt_string rt_scene_node3d_metadata_kind(void *obj, rt_string key) {
    const rt_scene3d_metadata_entry *entry = metadata_get_entry(obj, key, NULL, NULL);
    if (!entry)
        return rt_const_cstr("");
    switch (entry->kind) {
        case RT_SCENE3D_METADATA_NULL:
            return rt_const_cstr("null");
        case RT_SCENE3D_METADATA_BOOL:
            return rt_const_cstr("bool");
        case RT_SCENE3D_METADATA_INT:
            return rt_const_cstr("int");
        case RT_SCENE3D_METADATA_FLOAT:
            return rt_const_cstr("float");
        case RT_SCENE3D_METADATA_STRING:
            return rt_const_cstr("string");
    }
    return rt_const_cstr("");
}

int8_t rt_scene_node3d_metadata_has(void *obj, rt_string key) {
    return metadata_get_entry(obj, key, NULL, NULL) ? 1 : 0;
}

int64_t rt_scene_node3d_metadata_get_int(void *obj, rt_string key, int64_t def) {
    const rt_scene3d_metadata_entry *entry = metadata_get_entry(obj, key, NULL, NULL);
    return entry && entry->kind == RT_SCENE3D_METADATA_INT ? entry->value.int_value : def;
}

double rt_scene_node3d_metadata_get_float(void *obj, rt_string key, double def) {
    const rt_scene3d_metadata_entry *entry = metadata_get_entry(obj, key, NULL, NULL);
    if (!entry)
        return def;
    if (entry->kind == RT_SCENE3D_METADATA_FLOAT)
        return entry->value.float_value;
    if (entry->kind == RT_SCENE3D_METADATA_INT)
        return (double)entry->value.int_value;
    return def;
}

int8_t rt_scene_node3d_metadata_get_bool(void *obj, rt_string key, int8_t def) {
    const rt_scene3d_metadata_entry *entry = metadata_get_entry(obj, key, NULL, NULL);
    return entry && entry->kind == RT_SCENE3D_METADATA_BOOL ? (entry->value.bool_value ? 1 : 0)
                                                            : (def ? 1 : 0);
}

rt_string rt_scene_node3d_metadata_get_string(void *obj, rt_string key, rt_string def) {
    const rt_scene3d_metadata_entry *entry = metadata_get_entry(obj, key, NULL, NULL);
    if (entry && entry->kind == RT_SCENE3D_METADATA_STRING)
        return rt_string_from_bytes(entry->value.string_value.data,
                                    (size_t)entry->value.string_value.length);
    if (def && rt_string_is_handle(def))
        return rt_string_ref(def);
    return rt_const_cstr("");
}

int8_t rt_scene_node3d_metadata_set_null(void *obj, rt_string key) {
    rt_scene_node3d *node = metadata_node(obj);
    const char *key_data;
    int32_t key_length;
    if (!node || !metadata_string_view(
                     key, RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES, 0, &key_data, &key_length))
        return 0;
    return metadata_set(node, key_data, key_length, RT_SCENE3D_METADATA_NULL, 0, 0, 0.0, NULL, 0);
}

int8_t rt_scene_node3d_metadata_set_int(void *obj, rt_string key, int64_t value) {
    rt_scene_node3d *node = metadata_node(obj);
    const char *key_data;
    int32_t key_length;
    if (!node || !metadata_string_view(
                     key, RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES, 0, &key_data, &key_length))
        return 0;
    return metadata_set(
        node, key_data, key_length, RT_SCENE3D_METADATA_INT, 0, value, 0.0, NULL, 0);
}

int8_t rt_scene_node3d_metadata_set_float(void *obj, rt_string key, double value) {
    rt_scene_node3d *node = metadata_node(obj);
    const char *key_data;
    int32_t key_length;
    if (!node || !isfinite(value) ||
        !metadata_string_view(
            key, RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES, 0, &key_data, &key_length))
        return 0;
    return metadata_set(
        node, key_data, key_length, RT_SCENE3D_METADATA_FLOAT, 0, 0, value, NULL, 0);
}

int8_t rt_scene_node3d_metadata_set_bool(void *obj, rt_string key, int8_t value) {
    rt_scene_node3d *node = metadata_node(obj);
    const char *key_data;
    int32_t key_length;
    if (!node || !metadata_string_view(
                     key, RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES, 0, &key_data, &key_length))
        return 0;
    return metadata_set(
        node, key_data, key_length, RT_SCENE3D_METADATA_BOOL, value, 0, 0.0, NULL, 0);
}

int8_t rt_scene_node3d_metadata_set_string(void *obj, rt_string key, rt_string value) {
    rt_scene_node3d *node = metadata_node(obj);
    const char *key_data;
    const char *value_data;
    int32_t key_length;
    int32_t value_length;
    if (!node ||
        !metadata_string_view(
            key, RT_SCENE_NODE3D_MAX_METADATA_KEY_BYTES, 0, &key_data, &key_length) ||
        !metadata_string_view(
            value, RT_SCENE_NODE3D_MAX_METADATA_STRING_BYTES, 1, &value_data, &value_length))
        return 0;
    return metadata_set(node,
                        key_data,
                        key_length,
                        RT_SCENE3D_METADATA_STRING,
                        0,
                        0,
                        0.0,
                        value_data,
                        value_length);
}

int8_t rt_scene_node3d_metadata_remove(void *obj, rt_string key) {
    rt_scene_node3d *node = NULL;
    int32_t index = 0;
    const rt_scene3d_metadata_entry *entry = metadata_get_entry(obj, key, &node, &index);
    if (!node || !entry)
        return 0;
    free(node->metadata[index].key);
    metadata_release_value(&node->metadata[index]);
    if (index + 1 < node->metadata_count) {
        memmove(&node->metadata[index],
                &node->metadata[index + 1],
                (size_t)(node->metadata_count - index - 1) * sizeof(rt_scene3d_metadata_entry));
    }
    node->metadata_count--;
    memset(&node->metadata[node->metadata_count], 0, sizeof(rt_scene3d_metadata_entry));
    return 1;
}

#endif /* ZANNA_ENABLE_GRAPHICS */
