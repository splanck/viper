//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_vscn.c
// Purpose: Scene3D .vscn save/load support and asset serialization helpers.
//   Implements the JSON-based scene format that embeds binary asset payloads
//   (textures, mesh buffers) as base64 to stay textual. Pointer-deduplication
//   tables ensure shared assets (same texture used by multiple materials)
//   only emit one copy in the file.
//
// Key invariants:
//   - .vscn is a JSON document with base64-encoded binary embeds.
//   - Asset deduplication: each unique mesh / material / texture / cubemap
//     pointer appears once in the output regardless of how many nodes use it.
//   - Loader rolls back partial state on any error so a half-loaded scene
//     never reaches the caller.
//
// Ownership/Lifetime:
//   - Save / load operate on Scene3D / SceneNode3D objects defined in
//     rt_scene3d.c; this TU owns no GC objects of its own.
//
// Links: rt_scene3d.h, rt_scene3d_internal.h, rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_skeleton3d_internal.h"
#include "rt_trap.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VSCN_MAX_NODE_DEPTH 1024
#define VSCN_ABS_MAX 1.0e12
#define VSCN_MAX_FILE_BYTES (256u * 1024u * 1024u)

/// @brief Count the total number of nodes in the subtree rooted at `node` (inclusive).
/// @details Iterative so adversarially deep loaded hierarchies cannot overflow the C stack.
static int32_t scene3d_count_subtree(const rt_scene_node3d *node) {
    if (!node)
        return 0;

    const rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int32_t total = 0;

    for (;;) {
        if (count >= capacity) {
            size_t new_capacity = capacity > 0 ? capacity * 2u : 64u;
            const rt_scene_node3d **grown;
            if (new_capacity <= capacity || new_capacity > SIZE_MAX / sizeof(*stack)) {
                free(stack);
                return INT32_MAX;
            }
            grown = (const rt_scene_node3d **)realloc((void *)stack, new_capacity * sizeof(*stack));
            if (!grown) {
                free(stack);
                return 0;
            }
            stack = grown;
            capacity = new_capacity;
        }
        stack[count++] = node;
        break;
    }

    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (total == INT32_MAX) {
            free(stack);
            return INT32_MAX;
        }
        total++;
        for (int32_t i = 0, child_count = scene3d_node_child_count(current); i < child_count;
             i++) {
            if (count >= capacity) {
                size_t new_capacity = capacity > 0 ? capacity * 2u : 64u;
                const rt_scene_node3d **grown;
                if (new_capacity <= capacity || new_capacity > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    return INT32_MAX;
                }
                grown =
                    (const rt_scene_node3d **)realloc((void *)stack, new_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return 0;
                }
                stack = grown;
                capacity = new_capacity;
            }
            {
                const rt_scene_node3d *child =
                    (const rt_scene_node3d *)rt_g3d_checked_or_null(
                        current->children[i], RT_G3D_SCENENODE3D_CLASS_ID);
                if (child)
                    stack[count++] = child;
            }
        }
    }
    free(stack);
    return total;
}

// Scene save/load (.vscn JSON format)
//=============================================================================

typedef struct {
    void **items;
    int32_t count;
    int32_t capacity;
} vscn_ptr_table_t;

typedef struct {
    vscn_ptr_table_t meshes;
    vscn_ptr_table_t materials;
    vscn_ptr_table_t textures;
    vscn_ptr_table_t cubemaps;
} vscn_save_context_t;

static const char vscn_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// ---------------------------------------------------------------------------
// VSCN file format helpers — `.vscn` is a JSON document that
// embeds binary asset data (textures, mesh buffers) as base64 to
// stay textual. The helpers here cover the base64 codec, a
// pointer-deduplication table (so two materials sharing a texture
// only emit one copy), and tiny JSON accessors.
// ---------------------------------------------------------------------------

/// @brief Base64 alphabet table used by both encoder and decoder.

/// @brief Decode a single base64 character to its 0-63 value.
/// Returns -2 for `=` (padding sentinel) and -1 for any other invalid byte.
static int vscn_base64_digit_value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    if (c == '=')
        return -2;
    return -1;
}

/// @brief Encode `len` raw bytes to a freshly-allocated NUL-terminated base64 string.
///
/// Output length is `((len + 2) / 3) * 4` characters. Trailing
/// `=` padding is appended for inputs whose length is not a
/// multiple of 3. Caller owns the buffer (`free`); writes the
/// length sans NUL into `*out_len` if non-NULL.
static char *vscn_base64_encode(const uint8_t *data, size_t len, size_t *out_len) {
    if (!data && len > 0)
        return NULL;
    if (len > SIZE_MAX - 2)
        return NULL;
    size_t groups = (len + 2) / 3;
    if (groups > (SIZE_MAX - 1) / 4)
        return NULL;
    size_t olen = groups * 4;
    char *output = (char *)malloc(olen + 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t octet_a = data[i++];
        uint32_t octet_b = (i < len) ? data[i++] : 0;
        uint32_t octet_c = (i < len) ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        output[j++] = vscn_base64_chars[(triple >> 18) & 0x3F];
        output[j++] = vscn_base64_chars[(triple >> 12) & 0x3F];
        output[j++] = vscn_base64_chars[(triple >> 6) & 0x3F];
        output[j++] = vscn_base64_chars[triple & 0x3F];
    }

    {
        size_t padding = (3 - (len % 3)) % 3;
        for (size_t p = 0; p < padding; p++)
            output[j - 1 - p] = '=';
    }

    output[j] = '\0';
    if (out_len)
        *out_len = j;
    return output;
}

/// @brief Raise a Scene3D.Load trap reporting invalid base64 in @p field at byte @p offset.
static void vscn_trap_base64_error(const char *field, size_t offset) {
    char msg[160];
    snprintf(msg,
             sizeof(msg),
             "Scene3D.Load: invalid base64 in %s at byte offset %zu",
             field ? field : "data",
             offset);
    rt_trap(msg);
}

/// @brief Decode a base64 string of `len` characters into raw bytes.
///
/// Strict: rejects inputs whose length isn't a multiple of 4 and
/// any non-alphabet bytes. Honours `=` padding to compute the
/// exact output length. Returns a freshly-allocated buffer
/// (caller `free`s) or NULL on error.
static uint8_t *vscn_base64_decode_ex(const char *data,
                                      size_t len,
                                      size_t *out_len,
                                      size_t *error_offset) {
    if (out_len)
        *out_len = 0;
    if (error_offset)
        *error_offset = SIZE_MAX;
    if (!data)
        return NULL;
    if (len == 0) {
        uint8_t *empty = (uint8_t *)malloc(1);
        if (out_len)
            *out_len = 0;
        return empty;
    }
    if (len % 4 != 0) {
        if (error_offset)
            *error_offset = len;
        return NULL;
    }

    size_t padding = 0;
    if (data[len - 1] == '=')
        padding++;
    if (data[len - 2] == '=')
        padding++;
    for (size_t k = 0; k + padding < len; k++) {
        if (data[k] == '=') {
            if (error_offset)
                *error_offset = k;
            return NULL;
        }
    }
    if ((len / 4) > SIZE_MAX / 3)
        return NULL;

    size_t olen = (len / 4) * 3;
    olen -= padding;

    uint8_t *output = (uint8_t *)malloc(olen > 0 ? olen : 1);
    if (!output)
        return NULL;

    size_t i = 0, j = 0;
    while (i < len) {
        size_t group = i;
        int a = vscn_base64_digit_value(data[i++]);
        int b = vscn_base64_digit_value(data[i++]);
        int c = vscn_base64_digit_value(data[i++]);
        int d = vscn_base64_digit_value(data[i++]);
        int is_last_group = (i == len);

        if (a < 0 || b < 0 || c == -1 || d == -1) {
            if (error_offset) {
                if (a < 0)
                    *error_offset = group;
                else if (b < 0)
                    *error_offset = group + 1u;
                else if (c == -1)
                    *error_offset = group + 2u;
                else
                    *error_offset = group + 3u;
            }
            free(output);
            return NULL;
        }
        if ((c == -2 || d == -2) && !is_last_group) {
            if (error_offset)
                *error_offset = c == -2 ? group + 2u : group + 3u;
            free(output);
            return NULL;
        }
        if (c == -2 && d != -2) {
            if (error_offset)
                *error_offset = group + 3u;
            free(output);
            return NULL;
        }
        if (d == -2 && c != -2 && (c & 0x03) != 0) {
            if (error_offset)
                *error_offset = group + 2u;
            free(output);
            return NULL;
        }
        if (c == -2 && (b & 0x0F) != 0) {
            if (error_offset)
                *error_offset = group + 1u;
            free(output);
            return NULL;
        }

        if (c == -2)
            c = 0;
        if (d == -2)
            d = 0;

        {
            uint32_t triple =
                ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
            if (j < olen)
                output[j++] = (uint8_t)((triple >> 16) & 0xFF);
            if (j < olen)
                output[j++] = (uint8_t)((triple >> 8) & 0xFF);
            if (j < olen)
                output[j++] = (uint8_t)(triple & 0xFF);
        }
    }

    if (out_len)
        *out_len = olen;
    return output;
}

/// @brief Reset a pointer table — free its backing array and zero counts.
static void vscn_free_ptr_table(vscn_ptr_table_t *table) {
    if (!table)
        return;
    free(table->items);
    table->items = NULL;
    table->count = 0;
    table->capacity = 0;
}

/// @brief Drop GC references to all `count` loaded objects, then free the array itself.
///
/// Used by the loader to roll back partially-loaded resources
/// when a later stage of `rt_scene3d_load` fails.
static void vscn_release_loaded_refs(void **items, int count) {
    if (!items)
        return;
    for (int i = 0; i < count; i++) {
        void *tmp = items[i];
        scene3d_release_ref(&tmp);
    }
    free(items);
}

/// @brief Return the index of `item` in the table, inserting it if not present.
///
/// Provides O(n) interning of asset pointers so the saver can
/// emit each shared mesh / material / texture exactly once and
/// reference it by index from elsewhere in the JSON.
/// @return Existing or newly-assigned index, or -1 on alloc failure / NULL inputs.
static int vscn_ptr_table_index_or_add(vscn_ptr_table_t *table, void *item) {
    if (!table || !item)
        return -1;
    for (int32_t i = 0; i < table->count; i++) {
        if (table->items[i] == item)
            return i;
    }
    if (table->count >= table->capacity) {
        if (table->capacity > INT32_MAX / 2)
            return -1;
        int32_t new_cap = table->capacity == 0 ? 8 : table->capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
            return -1;
        void **new_items = (void **)realloc(table->items, (size_t)new_cap * sizeof(void *));
        if (!new_items)
            return -1;
        table->items = new_items;
        table->capacity = new_cap;
    }
    table->items[table->count] = item;
    table->count++;
    return table->count - 1;
}

// ---------------------------------------------------------------------------
// `vjson_*` JSON accessor wrappers — these adapt the generic
// rt_json runtime API to the conventions used throughout the
// scene loader (default values for missing keys, return raw cstr
// without allocating, etc.). Each takes a parsed JSON object/array
// and a key/index, and returns the typed value or a caller-supplied default.
// ---------------------------------------------------------------------------

/// @brief Look up a JSON object member by C-string key. Returns NULL if absent.
static int vjson_is_map(void *obj) {
    return obj && !rt_string_is_handle(obj) && rt_heap_is_payload(obj) &&
           rt_obj_class_id(obj) == RT_MAP_CLASS_ID;
}

/// @brief True if @p obj is a parsed-JSON array (a seq payload), not a string/map.
static int vjson_is_seq(void *obj) {
    return obj && !rt_string_is_handle(obj) && rt_heap_is_payload(obj) &&
           rt_obj_class_id(obj) == RT_SEQ_CLASS_ID;
}

/// @brief Look up @p key in a parsed-JSON object; returns the value or NULL if @p obj is
///   not a map or the key is absent.
static void *vjson_get(void *obj, const char *key) {
    if (!obj || !key)
        return NULL;
    if (!vjson_is_map(obj))
        return NULL;
    return rt_map_get(obj, rt_const_cstr(key));
}

/// @brief Length of a JSON array, or 0 for NULL.
static int64_t vjson_len(void *seq) {
    return vjson_is_seq(seq) ? rt_seq_len(seq) : 0;
}

/// @brief Safely coerce a JSON double to int64 without invoking undefined conversion behavior.
static int vjson_double_to_i64_checked(double value, int64_t *out) {
    if (!out || !isfinite(value))
        return 0;
    if (value < (-9223372036854775807.0 - 1.0) || value >= 9223372036854775808.0)
        return 0;
    *out = (int64_t)value;
    return 1;
}

/// @brief Coerce a boxed JSON value to int64. Falls back to `def` for non-numeric or null.
static int64_t vjson_value_i64(void *value, int64_t def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return rt_unbox_i64(value);
        case 1: {
            int64_t coerced;
            return vjson_double_to_i64_checked(rt_unbox_f64(value), &coerced) ? coerced : def;
        }
        case 2:
            return rt_unbox_i1(value);
        default:
            return def;
    }
}

/// @brief Read a JSON numeric value as an exact int64; rejects bools, non-finite doubles, and
///   fractional double values so index/count fields cannot be silently truncated.
static int vjson_value_i64_exact(void *value, int64_t *out) {
    double number;
    if (!value || !out)
        return 0;
    switch (rt_box_type(value)) {
        case 0:
            *out = rt_unbox_i64(value);
            return 1;
        case 1:
            number = rt_unbox_f64(value);
            if (!isfinite(number) || floor(number) != number)
                return 0;
            return vjson_double_to_i64_checked(number, out);
        default:
            return 0;
    }
}

/// @brief Read an object integer property exactly, defaulting only when the key is absent.
static int vjson_i64_exact(void *obj, const char *key, int64_t def, int64_t *out) {
    void *value;
    if (!out)
        return 0;
    *out = def;
    value = vjson_get(obj, key);
    if (!value)
        return 1;
    return vjson_value_i64_exact(value, out);
}

/// @brief Read an array integer element exactly, defaulting only when the index is absent.
static int vjson_arr_i64_exact(void *arr, int64_t index, int64_t def, int64_t *out) {
    if (!out)
        return 0;
    *out = def;
    if (!arr || index < 0 || index >= vjson_len(arr))
        return 1;
    return vjson_value_i64_exact(rt_seq_get(arr, index), out);
}

/// @brief Coerce a boxed JSON value to double. `def` for non-numeric or null.
static double vjson_value_f64(void *value, double def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return (double)rt_unbox_i64(value);
        case 1: {
            double number = rt_unbox_f64(value);
            return isfinite(number) ? number : def;
        }
        case 2:
            return (double)rt_unbox_i1(value);
        default:
            return def;
    }
}

/// @brief Read `obj[key]` as int64, defaulting to `def` if absent / wrong type.
static int64_t vjson_i64(void *obj, const char *key, int64_t def) {
    void *value = vjson_get(obj, key);
    return vjson_value_i64(value, def);
}

/// @brief Read `obj[key]` as double, defaulting to `def` if absent / wrong type.
static double vjson_f64(void *obj, const char *key, double def) {
    void *value = vjson_get(obj, key);
    return vjson_value_f64(value, def);
}

/// @brief Read `obj[key]` as boolean (0/1), defaulting to `def`.
static int8_t vjson_bool(void *obj, const char *key, int8_t def) {
    void *value = vjson_get(obj, key);
    return value ? (vjson_value_i64(value, def) ? 1 : 0) : def;
}

/// @brief Read `obj[key]` as a Viper rt_string. NULL if missing or non-string.
static rt_string vjson_string_value(void *obj, const char *key) {
    void *value = vjson_get(obj, key);
    return rt_string_is_handle(value) ? (rt_string)value : NULL;
}

/// @brief Read `obj[key]` as a borrowed C string. NULL if missing or non-string.
/// The pointer remains valid only as long as the underlying rt_string lives.
static const char *vjson_cstr(void *obj, const char *key) {
    rt_string value = vjson_string_value(obj, key);
    return value ? rt_string_cstr(value) : NULL;
}

/// @brief Array-form `arr[index]` as double with default. Useful for vec3/vec4 unpacking.
static double vjson_arr_f64(void *arr, int64_t index, double def) {
    if (!arr || index < 0 || index >= vjson_len(arr))
        return def;
    return vjson_value_f64(rt_seq_get(arr, index), def);
}

/// @brief Return @p value if finite, else @p fallback. Base sanitizer for loaded JSON numbers.
static double vscn_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Sanitize @p value (fallback if non-finite) and clamp to ±VSCN_ABS_MAX.
static double vscn_clamp_abs_or(double value, double fallback) {
    value = vscn_finite_or(value, fallback);
    if (value > VSCN_ABS_MAX)
        return VSCN_ABS_MAX;
    if (value < -VSCN_ABS_MAX)
        return -VSCN_ABS_MAX;
    return value;
}

/// @brief Sanitize @p value (fallback if non-finite) and clamp to [lo, hi].
static double vscn_clamp_or(double value, double fallback, double lo, double hi) {
    value = vscn_finite_or(value, fallback);
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Sanitize @p value (fallback if non-finite) and clamp negatives to 0.
static double vscn_nonnegative_or(double value, double fallback) {
    value = vscn_finite_or(value, fallback);
    return value < 0.0 ? 0.0 : value;
}

/// @brief Accept a material workflow id (legacy/PBR) from JSON, else use @p fallback.
static int32_t vscn_material_workflow_or(int64_t value, int32_t fallback) {
    if (value == RT_MATERIAL3D_WORKFLOW_LEGACY || value == RT_MATERIAL3D_WORKFLOW_PBR)
        return (int32_t)value;
    return fallback;
}

/// @brief Accept an alpha-mode id (opaque/mask/blend) from JSON, else use @p fallback.
static int32_t vscn_alpha_mode_or(int64_t value, int32_t fallback) {
    if (value >= RT_MATERIAL3D_ALPHA_MODE_OPAQUE && value <= RT_MATERIAL3D_ALPHA_MODE_BLEND)
        return (int32_t)value;
    return fallback;
}

/// @brief Accept a texture-wrap mode (repeat/clamp/mirror) from JSON, else use @p fallback.
static int32_t vscn_wrap_or(int64_t value, int32_t fallback) {
    if (value == RT_MATERIAL3D_TEXTURE_WRAP_REPEAT ||
        value == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE ||
        value == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return (int32_t)value;
    return fallback;
}

/// @brief Accept a texture-filter mode (nearest/linear) from JSON, else use @p fallback.
static int32_t vscn_filter_or(int64_t value, int32_t fallback) {
    if (value == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ||
        value == RT_MATERIAL3D_TEXTURE_FILTER_LINEAR)
        return (int32_t)value;
    return fallback;
}

/// @brief Normalize a loaded quaternion in place; non-finite or near-zero-length values
///   reset to the identity quaternion (0,0,0,1).
static void vscn_normalize_quat(double q[4]) {
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        q[0] = 0.0;
        q[1] = 0.0;
        q[2] = 0.0;
        q[3] = 1.0;
        return;
    }
    double len_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!isfinite(len_sq) || len_sq < 1e-20) {
        q[0] = 0.0;
        q[1] = 0.0;
        q[2] = 0.0;
        q[3] = 1.0;
        return;
    }
    double inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
}

/// @brief Normalize a loaded vec3 in place; a near-zero or non-finite length falls back
///   to the (fx, fy, fz) direction.
static void vscn_normalize_vec3(double v[3], double fx, double fy, double fz) {
    if (!v)
        return;
    double len_sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (!isfinite(len_sq) || len_sq < 1e-20) {
        v[0] = fx;
        v[1] = fy;
        v[2] = fz;
        return;
    }
    double inv_len = 1.0 / sqrt(len_sq);
    v[0] *= inv_len;
    v[1] *= inv_len;
    v[2] *= inv_len;
}

/// @brief Read an optional integer index reference (e.g. a mesh/material index) from
///   @p key into @p out_index. Missing key → -1 and success; a value < -1 → failure (0).
static int vscn_read_index_ref(void *obj, const char *key, int64_t *out_index) {
    void *value;
    int64_t index;
    if (!out_index)
        return 0;
    *out_index = -1;
    value = vjson_get(obj, key);
    if (!value)
        return 1;
    if (!vjson_value_i64_exact(value, &index))
        return 0;
    if (index < -1)
        return 0;
    *out_index = index;
    return 1;
}

// ---------------------------------------------------------------------------
// Output buffer + JSON-formatting helpers used by the saver. The
// growing buffer doubles on overflow; `vscn_append` is a printf-
// style convenience and `vscn_append_json_string` handles the
// usual JSON escapes so user-supplied strings can't break the
// document.
// ---------------------------------------------------------------------------

/// @brief Write `depth` levels of two-space indentation into `indent` (NUL-terminated).
static void vscn_make_indent(char *indent, size_t indent_cap, int depth) {
    size_t count;
    if (!indent || indent_cap == 0)
        return;
    count = (size_t)(depth * 2);
    if (count >= indent_cap)
        count = indent_cap - 1;
    memset(indent, ' ', count);
    indent[count] = '\0';
}

/// @brief Ensure `*buf` has at least `needed` bytes of capacity, doubling (starting at
///   4096) until it fits. @return 1 on success, 0 on alloc failure.
static int vscn_reserve(char **buf, size_t *cap, size_t needed) {
    char *nb;
    size_t new_cap;
    if (!buf || !cap)
        return 0;
    if (*cap >= needed)
        return 1;
    new_cap = *cap == 0 ? 4096u : *cap;
    while (new_cap < needed) {
        size_t doubled;
        if (new_cap > SIZE_MAX / 2) {
            new_cap = needed;
            break;
        }
        doubled = new_cap * 2u;
        new_cap = doubled < needed ? needed : doubled;
    }
    nb = (char *)realloc(*buf, new_cap);
    if (!nb)
        return 0;
    *buf = nb;
    *cap = new_cap;
    return 1;
}

/// @brief Append `src_len` raw bytes to the growing `*buf` (reserving capacity first and
///   keeping the buffer NUL-terminated). @return 1 on success, 0 on overflow/alloc failure.
static int vscn_append_raw(char **buf, size_t *len, size_t *cap, const char *src, size_t src_len) {
    if (!buf || !len || !cap || (!src && src_len > 0))
        return 0;
    if (src_len > SIZE_MAX - *len - 1)
        return 0;
    if (!vscn_reserve(buf, cap, *len + src_len + 1))
        return 0;
    if (src_len > 0)
        memcpy(*buf + *len, src, src_len);
    *len += src_len;
    (*buf)[*len] = '\0';
    return 1;
}

/// @brief Append a formatted string to a growable byte buffer.
/// @details Formats directly into the buffer and retries after growing it if
///   the current capacity is too small. Avoids `vsnprintf(NULL, 0)` because
///   that path corrupts floating-point varargs on MSVC ARM64.
/// @return 1 on success, 0 if `vsnprintf` fails or realloc is denied.
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 4, 5)))
#endif
static int vscn_append(char **buf, size_t *len, size_t *cap, const char *fmt, ...) {
    if (!buf || !len || !cap || !fmt)
        return 0;
    for (;;) {
        va_list ap;
        int written;
        size_t available;

        if (*len > SIZE_MAX - 1)
            return 0;
        if (!vscn_reserve(buf, cap, *len + 1))
            return 0;

        available = *cap - *len;
        va_start(ap, fmt);
        written = vsnprintf(*buf + *len, available, fmt, ap);
        va_end(ap);
        if (written < 0)
            return 0;
        if ((size_t)written < available) {
            *len += (size_t)written;
            return 1;
        }

        if ((size_t)written > SIZE_MAX - *len - 1)
            return 0;
        if (!vscn_reserve(buf, cap, *len + (size_t)written + 1))
            return 0;
    }
}

/// @brief Append `text` to the buffer wrapped in JSON quotes with proper escapes.
///
/// Emits the standard backslash escapes (`\"`, `\\`, `\b`, `\f`,
/// `\n`, `\r`, `\t`) for ASCII control characters and `\u00XX`
/// for any other sub-0x20 byte. Bytes >= 0x20 are passed through
/// verbatim — JSON allows raw UTF-8.
static int vscn_append_json_string(char **buf, size_t *len, size_t *cap, const char *text) {
    if (!vscn_append_raw(buf, len, cap, "\"", 1))
        return 0;
    if (text) {
        for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
            char unicode_escape[7];
            switch (*p) {
                case '\"':
                    if (!vscn_append_raw(buf, len, cap, "\\\"", 2))
                        return 0;
                    break;
                case '\\':
                    if (!vscn_append_raw(buf, len, cap, "\\\\", 2))
                        return 0;
                    break;
                case '\b':
                    if (!vscn_append_raw(buf, len, cap, "\\b", 2))
                        return 0;
                    break;
                case '\f':
                    if (!vscn_append_raw(buf, len, cap, "\\f", 2))
                        return 0;
                    break;
                case '\n':
                    if (!vscn_append_raw(buf, len, cap, "\\n", 2))
                        return 0;
                    break;
                case '\r':
                    if (!vscn_append_raw(buf, len, cap, "\\r", 2))
                        return 0;
                    break;
                case '\t':
                    if (!vscn_append_raw(buf, len, cap, "\\t", 2))
                        return 0;
                    break;
                default:
                    if (*p < 0x20u) {
                        snprintf(unicode_escape, sizeof(unicode_escape), "\\u%04x", (unsigned)*p);
                        if (!vscn_append_raw(buf, len, cap, unicode_escape, 6))
                            return 0;
                    } else if (!vscn_append_raw(buf, len, cap, (const char *)p, 1)) {
                        return 0;
                    }
                    break;
            }
        }
    }
    return vscn_append_raw(buf, len, cap, "\"", 1);
}

/// @brief First pass: register every texture / cubemap referenced by `material` in the dedupe
/// tables.
///
/// The save algorithm runs collection over the whole scene before
/// any serialisation so each shared asset gets a stable index
/// referenced from every material that uses it.
static rt_pixels_impl *vscn_material_texture_pixels(void *texture_ref) {
    void *pixels = rt_material3d_resolve_texture_pixels(texture_ref);
    return rt_pixels_checked_impl_or_null(pixels);
}

/// @brief True if a cubemap has all six faces present and square at its declared face size
///   (i.e. it can be serialized into a .vscn asset).
static int vscn_cubemap_is_serializable(rt_cubemap3d *cubemap) {
    if (!rt_g3d_has_class(cubemap, RT_G3D_CUBEMAP3D_CLASS_ID) || cubemap->face_size <= 0 ||
        cubemap->face_size > INT32_MAX)
        return 0;
    for (int i = 0; i < 6; i++) {
        rt_pixels_impl *face = rt_pixels_checked_impl_or_null(cubemap->faces[i]);
        if (!face || face->width != cubemap->face_size || face->height != cubemap->face_size)
            return 0;
    }
    return 1;
}

/// @brief Return a material's environment cubemap only when present and serializable, else NULL.
static rt_cubemap3d *vscn_material_env_map(rt_material3d *material) {
    rt_cubemap3d *cubemap = material ? (rt_cubemap3d *)material->env_map : NULL;
    return vscn_cubemap_is_serializable(cubemap) ? cubemap : NULL;
}

/// @brief Register a material's referenced textures and environment cubemap into the save
///   context's dedup tables; returns 0 on allocation failure, 1 on success (or nothing to do).
static int vscn_collect_material_assets(rt_material3d *material, vscn_save_context_t *ctx) {
    rt_pixels_impl *texture;
    rt_cubemap3d *cubemap;
    if (!material || !ctx)
        return 1;
    texture = vscn_material_texture_pixels(material->texture);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->normal_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->specular_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->emissive_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->metallic_roughness_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    texture = vscn_material_texture_pixels(material->ao_map);
    if (texture && vscn_ptr_table_index_or_add(&ctx->textures, texture) < 0)
        return 0;
    cubemap = vscn_material_env_map(material);
    if (cubemap) {
        if (vscn_ptr_table_index_or_add(&ctx->cubemaps, cubemap) < 0)
            return 0;
        for (int i = 0; i < 6; i++) {
            if (cubemap->faces[i] &&
                vscn_ptr_table_index_or_add(&ctx->textures, cubemap->faces[i]) < 0)
                return 0;
        }
    }
    return 1;
}

/// @brief Collect mesh / material / texture references from a node subtree without recursion.
static int vscn_collect_node_assets(rt_scene_node3d *node, vscn_save_context_t *ctx) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !ctx)
        return 1;
    for (;;) {
        size_t new_capacity = 64u;
        stack = (rt_scene_node3d **)malloc(new_capacity * sizeof(*stack));
        if (!stack)
            return 0;
        capacity = new_capacity;
        stack[count++] = node;
        break;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        if (!current)
            continue;
        if (rt_g3d_has_class(current->mesh, RT_G3D_MESH3D_CLASS_ID) &&
            vscn_ptr_table_index_or_add(&ctx->meshes, current->mesh) < 0) {
            free(stack);
            return 0;
        }
        if (rt_g3d_has_class(current->material, RT_G3D_MATERIAL3D_CLASS_ID)) {
            if (vscn_ptr_table_index_or_add(&ctx->materials, current->material) < 0 ||
                !vscn_collect_material_assets((rt_material3d *)current->material, ctx)) {
                free(stack);
                return 0;
            }
        }
        for (int32_t i = 0, lod_count = scene3d_node_lod_count(current); i < lod_count; i++) {
            if (rt_g3d_has_class(current->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID) &&
                vscn_ptr_table_index_or_add(&ctx->meshes, current->lod_levels[i].mesh) < 0) {
                free(stack);
                return 0;
            }
        }
        for (int32_t i = 0, child_count = scene3d_node_child_count(current); i < child_count;
             i++) {
            rt_scene_node3d *child = scene_node3d_checked(current->children[i]);
            if (!child)
                continue;
            if (count >= capacity) {
                size_t new_capacity = capacity * 2u;
                rt_scene_node3d **grown;
                if (new_capacity <= capacity || new_capacity > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    return 0;
                }
                grown = (rt_scene_node3d **)realloc(stack, new_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return 0;
                }
                stack = grown;
                capacity = new_capacity;
            }
            stack[count++] = child;
        }
    }
    free(stack);
    return 1;
}

/// @brief Emit a Pixels object as a `{"width":…, "height":…, "data":"<base64>"}` JSON entry.
static int vscn_serialize_texture(
    rt_pixels_impl *pixels, char **buf, size_t *len, size_t *cap, int depth) {
    char indent[64];
    size_t pixel_count;
    size_t rgba_len;
    uint8_t *rgba = NULL;
    char *rgba_base64 = NULL;

    if (!pixels)
        return 0;
    if (pixels->width < 0 || pixels->height < 0)
        return 0;
    if ((size_t)pixels->width > SIZE_MAX / (size_t)(pixels->height > 0 ? pixels->height : 1))
        return 0;

    vscn_make_indent(indent, sizeof(indent), depth);
    pixel_count = (size_t)pixels->width * (size_t)pixels->height;
    if (pixel_count > SIZE_MAX / 4)
        return 0;
    if (pixel_count > 0 && !pixels->data)
        return 0;
    rgba_len = pixel_count * 4;
    rgba = (uint8_t *)malloc(rgba_len > 0 ? rgba_len : 1);
    if (!rgba)
        return 0;

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = pixels->data[i];
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    rgba_base64 = vscn_base64_encode(rgba, rgba_len, NULL);
    free(rgba);
    if (!rgba_base64)
        return 0;

    {
        int ok = vscn_append(buf,
                             len,
                             cap,
                             "%s{\"width\": %lld, \"height\": %lld, \"rgbaBase64\": ",
                             indent,
                             (long long)pixels->width,
                             (long long)pixels->height) &&
                 vscn_append_json_string(buf, len, cap, rgba_base64) &&
                 vscn_append(buf, len, cap, "}");
        free(rgba_base64);
        return ok;
    }
}

/// @brief Emit a cubemap as six texture-index references (px/nx/py/ny/pz/nz).
static int vscn_serialize_cubemap(rt_cubemap3d *cubemap,
                                  vscn_save_context_t *ctx,
                                  char **buf,
                                  size_t *len,
                                  size_t *cap,
                                  int depth) {
    char indent[64];
    if (!cubemap || !ctx)
        return 0;
    if (!vscn_cubemap_is_serializable(cubemap))
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);
    if (!vscn_append(buf, len, cap, "%s{\"faces\": [", indent))
        return 0;
    for (int i = 0; i < 6; i++) {
        if (!cubemap->faces[i])
            return 0;
        int index = vscn_ptr_table_index_or_add(&ctx->textures, cubemap->faces[i]);
        if (index < 0)
            return 0;
        if (!vscn_append(buf, len, cap, "%s%d", i == 0 ? "" : ", ", index))
            return 0;
    }
    return vscn_append(buf, len, cap, "]}");
}

/// @brief Emit a material as JSON: PBR parameters + texture-index references for each map.
static int vscn_serialize_material(rt_material3d *material,
                                   vscn_save_context_t *ctx,
                                   char **buf,
                                   size_t *len,
                                   size_t *cap,
                                   int depth) {
    char indent[64];
    if (!material || !ctx)
        return 0;
    vscn_make_indent(indent, sizeof(indent), depth);

    const int texture_index =
        vscn_ptr_table_index_or_add(&ctx->textures, vscn_material_texture_pixels(material->texture));
    const int normal_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->normal_map));
    const int specular_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->specular_map));
    const int emissive_index = vscn_ptr_table_index_or_add(
        &ctx->textures, vscn_material_texture_pixels(material->emissive_map));
    const int metallic_roughness_index =
        vscn_ptr_table_index_or_add(&ctx->textures,
                                    vscn_material_texture_pixels(material->metallic_roughness_map));
    const int ao_index =
        vscn_ptr_table_index_or_add(&ctx->textures, vscn_material_texture_pixels(material->ao_map));
    const int env_index =
        vscn_ptr_table_index_or_add(&ctx->cubemaps, vscn_material_env_map(material));

    if (!vscn_append(buf,
                     len,
                     cap,
                     "%s{\"diffuse\": [%.17g, %.17g, %.17g, %.17g], ",
                     indent,
                     vscn_clamp_or(material->diffuse[0], 1.0, 0.0, 1.0),
                     vscn_clamp_or(material->diffuse[1], 1.0, 0.0, 1.0),
                     vscn_clamp_or(material->diffuse[2], 1.0, 0.0, 1.0),
                     vscn_clamp_or(material->diffuse[3], 1.0, 0.0, 1.0)) ||
        !vscn_append(
            buf,
            len,
            cap,
            "\"specular\": [%.17g, %.17g, %.17g], \"shininess\": %.17g, "
            "\"workflow\": %d, ",
            vscn_clamp_or(material->specular[0], 1.0, 0.0, VSCN_ABS_MAX),
            vscn_clamp_or(material->specular[1], 1.0, 0.0, VSCN_ABS_MAX),
            vscn_clamp_or(material->specular[2], 1.0, 0.0, VSCN_ABS_MAX),
            vscn_nonnegative_or(material->shininess, 32.0),
            vscn_material_workflow_or(material->workflow, RT_MATERIAL3D_WORKFLOW_LEGACY)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"emissive\": [%.17g, %.17g, %.17g], \"metallic\": %.17g, "
                     "\"roughness\": %.17g, \"ao\": %.17g, ",
                     vscn_clamp_or(material->emissive[0], 0.0, 0.0, VSCN_ABS_MAX),
                     vscn_clamp_or(material->emissive[1], 0.0, 0.0, VSCN_ABS_MAX),
                     vscn_clamp_or(material->emissive[2], 0.0, 0.0, VSCN_ABS_MAX),
                     vscn_clamp_or(material->metallic, 0.0, 0.0, 1.0),
                     vscn_clamp_or(material->roughness, 0.5, 0.0, 1.0),
                     vscn_clamp_or(material->ao, 1.0, 0.0, 1.0)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"emissiveIntensity\": %.17g, \"normalScale\": %.17g, "
                     "\"alpha\": %.17g, \"alphaMode\": %d, \"alphaCutoff\": %.17g, ",
                     vscn_nonnegative_or(material->emissive_intensity, 1.0),
                     vscn_nonnegative_or(material->normal_scale, 1.0),
                     vscn_clamp_or(material->alpha, 1.0, 0.0, 1.0),
                     vscn_alpha_mode_or(material->alpha_mode, RT_MATERIAL3D_ALPHA_MODE_OPAQUE),
                     vscn_clamp_or(material->alpha_cutoff, 0.5, 0.0, 1.0)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"doubleSided\": %s, \"reflectivity\": %.17g, \"unlit\": %s, "
                     "\"shadingModel\": %d, ",
                     material->double_sided ? "true" : "false",
                     vscn_clamp_or(material->reflectivity, 0.0, 0.0, 1.0),
                     material->unlit ? "true" : "false",
                     (material->shading_model >= 0 && material->shading_model <= 5)
                         ? material->shading_model
                         : 0) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "\"texture\": %d, \"normalMap\": %d, \"specularMap\": %d, "
                     "\"emissiveMap\": %d, \"metallicRoughnessMap\": %d, \"aoMap\": %d, "
                     "\"envMap\": %d, \"customParams\": [",
                     texture_index,
                     normal_index,
                     specular_index,
                     emissive_index,
                     metallic_roughness_index,
                     ao_index,
                     env_index)) {
        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (!vscn_append(buf,
                         len,
                         cap,
                         "%s%.17g",
                         i == 0 ? "" : ", ",
                         vscn_clamp_abs_or(material->custom_params[i], 0.0))) {
            return 0;
        }
    }
    if (!vscn_append(buf, len, cap, "], \"textureSlots\": ["))
        return 0;
    for (int i = 0; i < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; i++) {
        const double *uvm = material->texture_slot_uv_transform[i];
        if (!vscn_append(
                buf,
                len,
                cap,
                "%s{\"uvSet\": %d, \"wrapS\": %d, \"wrapT\": %d, "
                "\"filter\": %d, \"uvTransform\": [%.17g, %.17g, %.17g, %.17g, %.17g, %.17g]}",
                i == 0 ? "" : ", ",
                material->texture_slot_uv_set[i] > 0 ? 1 : 0,
                vscn_wrap_or(material->texture_slot_wrap_s[i], RT_MATERIAL3D_TEXTURE_WRAP_REPEAT),
                vscn_wrap_or(material->texture_slot_wrap_t[i], RT_MATERIAL3D_TEXTURE_WRAP_REPEAT),
                vscn_filter_or(material->texture_slot_filter[i],
                               RT_MATERIAL3D_TEXTURE_FILTER_LINEAR),
                vscn_clamp_abs_or(uvm[0], 1.0),
                vscn_clamp_abs_or(uvm[1], 0.0),
                vscn_clamp_abs_or(uvm[2], 0.0),
                vscn_clamp_abs_or(uvm[3], 1.0),
                vscn_clamp_abs_or(uvm[4], 0.0),
                vscn_clamp_abs_or(uvm[5], 0.0))) {
            return 0;
        }
    }
    return vscn_append(buf, len, cap, "]}");
}

/// @brief Emit a mesh as JSON: vertex/index buffers as base64 + the layout descriptor.
static int vscn_serialize_mesh(rt_mesh3d *mesh, char **buf, size_t *len, size_t *cap, int depth) {
    char indent[64];
    size_t vertex_bytes_len;
    size_t index_bytes_len;
    char *vertex_base64 = NULL;
    char *index_base64 = NULL;

    if (!mesh)
        return 0;
    if (mesh->vertex_count > 0 && !mesh->vertices)
        return 0;
    if (mesh->index_count > 0 && !mesh->indices)
        return 0;
    if ((size_t)mesh->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)mesh->index_count > SIZE_MAX / sizeof(uint32_t))
        return 0;
    vertex_bytes_len = (size_t)mesh->vertex_count * sizeof(vgfx3d_vertex_t);
    index_bytes_len = (size_t)mesh->index_count * sizeof(uint32_t);
    vscn_make_indent(indent, sizeof(indent), depth);
    vertex_base64 = vscn_base64_encode((const uint8_t *)mesh->vertices, vertex_bytes_len, NULL);
    index_base64 = vscn_base64_encode((const uint8_t *)mesh->indices, index_bytes_len, NULL);
    if (!vertex_base64 || !index_base64) {
        free(vertex_base64);
        free(index_base64);
        return 0;
    }

    {
        int ok = vscn_append(buf,
                             len,
                             cap,
                             "%s{\"vertexFormat\": \"vgfx3d_vertex_le_v2\", "
                             "\"vertexCount\": %u, "
                             "\"indexCount\": %u, "
                             "\"boneCount\": %d, "
                             "\"resident\": %s, "
                             "\"verticesBase64\": ",
                             indent,
                             mesh->vertex_count,
                             mesh->index_count,
                             mesh->bone_count,
                             rt_mesh3d_get_resident(mesh) ? "true" : "false") &&
                 vscn_append_json_string(buf, len, cap, vertex_base64) &&
                 vscn_append(buf, len, cap, ", \"indicesBase64\": ") &&
                 vscn_append_json_string(buf, len, cap, index_base64) &&
                 vscn_append(buf, len, cap, "}");
        free(vertex_base64);
        free(index_base64);
        return ok;
    }
}

/// @brief Recursively emit a scene node and its children as nested JSON objects.
///
/// Each node carries name, transform (TRS), mesh-index +
/// material-index references, and a children array. References
/// use the dedupe-table indices populated by the asset-collection pass.
static int vscn_serialize_node(rt_scene_node3d *node,
                               vscn_save_context_t *ctx,
                               char **buf,
                               size_t *len,
                               size_t *cap,
                               int depth) {
    if (!node)
        return 1;
    if (depth > VSCN_MAX_NODE_DEPTH * 2)
        return 0;
    char indent[64];
    vscn_make_indent(indent, sizeof(indent), depth);

    const char *name = node->name ? rt_string_cstr(node->name) : "node";
    if (!name)
        name = "node";
    double rotation[4] = {
        node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]};
    vscn_normalize_quat(rotation);
    int mesh_index = node->mesh ? vscn_ptr_table_index_or_add(&ctx->meshes, node->mesh) : -1;
    int material_index =
        node->material ? vscn_ptr_table_index_or_add(&ctx->materials, node->material) : -1;
    if ((node->mesh && mesh_index < 0) || (node->material && material_index < 0))
        return 0;

    if (!vscn_append(buf, len, cap, "%s{\n", indent) ||
        !vscn_append(buf, len, cap, "%s  \"name\": ", indent) ||
        !vscn_append_json_string(buf, len, cap, name) ||
        !vscn_append(buf,
                     len,
                     cap,
                     ",\n%s  \"position\": [%.17g, %.17g, %.17g],\n",
                     indent,
                     vscn_clamp_abs_or(node->position[0], 0.0),
                     vscn_clamp_abs_or(node->position[1], 0.0),
                     vscn_clamp_abs_or(node->position[2], 0.0)) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"rotation\": [%.17g, %.17g, %.17g, %.17g],\n",
                     indent,
                     rotation[0],
                     rotation[1],
                     rotation[2],
                     rotation[3]) ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"scale\": [%.17g, %.17g, %.17g],\n",
                     indent,
                     vscn_clamp_abs_or(node->scale_xyz[0], 1.0),
                     vscn_clamp_abs_or(node->scale_xyz[1], 1.0),
                     vscn_clamp_abs_or(node->scale_xyz[2], 1.0)) ||
        !vscn_append(
            buf, len, cap, "%s  \"visible\": %s,\n", indent, node->visible ? "true" : "false") ||
        !vscn_append(
            buf, len, cap, "%s  \"hasMesh\": %s,\n", indent, node->mesh ? "true" : "false") ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"hasMaterial\": %s,\n",
                     indent,
                     node->material ? "true" : "false") ||
        !vscn_append(buf,
                     len,
                     cap,
                     "%s  \"mesh\": %d,\n%s  \"material\": %d",
                     indent,
                     mesh_index,
                     indent,
                     material_index)) {
        return 0;
    }

    if (node->light) {
        rt_light3d *light = (rt_light3d *)node->light;
        double light_dir[3] = {vscn_clamp_abs_or(light->direction[0], 0.0),
                               vscn_clamp_abs_or(light->direction[1], 0.0),
                               vscn_clamp_abs_or(light->direction[2], -1.0)};
        vscn_normalize_vec3(light_dir, 0.0, 0.0, -1.0);
        if (!vscn_append(buf,
                         len,
                         cap,
                         ",\n%s  \"light\": {\"type\": %d, "
                         "\"direction\": [%.17g, %.17g, %.17g], "
                         "\"position\": [%.17g, %.17g, %.17g], "
                         "\"color\": [%.17g, %.17g, %.17g], "
                         "\"intensity\": %.17g, \"attenuation\": %.17g, "
                         "\"innerCos\": %.17g, \"outerCos\": %.17g, "
                         "\"castsShadows\": %s}",
                         indent,
                         (light->type >= 0 && light->type <= 3) ? light->type : 1,
                         light_dir[0],
                         light_dir[1],
                         light_dir[2],
                         vscn_clamp_abs_or(light->position[0], 0.0),
                         vscn_clamp_abs_or(light->position[1], 0.0),
                         vscn_clamp_abs_or(light->position[2], 0.0),
                         vscn_clamp_or(light->color[0], 1.0, 0.0, VSCN_ABS_MAX),
                         vscn_clamp_or(light->color[1], 1.0, 0.0, VSCN_ABS_MAX),
                         vscn_clamp_or(light->color[2], 1.0, 0.0, VSCN_ABS_MAX),
                         vscn_nonnegative_or(light->intensity, 1.0),
                         vscn_nonnegative_or(light->attenuation, 0.0),
                         vscn_clamp_or(light->inner_cos, 1.0, 0.0, 1.0),
                         vscn_clamp_or(light->outer_cos, 0.7071067811865476, 0.0, 1.0),
                         light->casts_shadows ? "true" : "false")) {
            return 0;
        }
    }

    int32_t lod_count = scene3d_node_lod_count(node);
    if (lod_count > 0) {
        if (!vscn_append(buf, len, cap, ",\n%s  \"lod\": [\n", indent))
            return 0;
        for (int32_t i = 0; i < lod_count; i++) {
            void *lod_mesh = rt_g3d_has_class(node->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID)
                                 ? node->lod_levels[i].mesh
                                 : NULL;
            int lod_mesh_index =
                lod_mesh ? vscn_ptr_table_index_or_add(&ctx->meshes, lod_mesh) : -1;
            if (lod_mesh && lod_mesh_index < 0)
                return 0;
            if (!vscn_append(buf,
                             len,
                             cap,
                             "%s    {\"distance\": %.17g, \"hasMesh\": %s, \"mesh\": %d}%s\n",
                             indent,
                             vscn_nonnegative_or(node->lod_levels[i].distance, 0.0),
                             lod_mesh ? "true" : "false",
                             lod_mesh_index,
                             i + 1 < lod_count ? "," : "")) {
                return 0;
            }
        }
        if (!vscn_append(buf, len, cap, "%s  ]", indent))
            return 0;
    }

    if (node->auto_lod_enabled) {
        if (!vscn_append(buf,
                         len,
                         cap,
                         ",\n%s  \"autoLOD\": {\"enabled\": true, \"screenErrorPx\": %.17g}",
                         indent,
                         vscn_nonnegative_or(node->auto_lod_screen_error_px, 8.0))) {
            return 0;
        }
    }

    int32_t child_count = scene3d_node_child_count(node);
    if (child_count > 0) {
        int32_t emitted = 0;
        if (!vscn_append(buf, len, cap, ",\n%s  \"children\": [\n", indent))
            return 0;
        for (int32_t i = 0; i < child_count; i++) {
            rt_scene_node3d *child = scene_node3d_checked(node->children[i]);
            if (!child)
                continue;
            if (emitted > 0 && !vscn_append(buf, len, cap, ","))
                return 0;
            if (!vscn_serialize_node(child, ctx, buf, len, cap, depth + 2))
                return 0;
            emitted++;
            if (!vscn_append(buf, len, cap, "\n"))
                return 0;
        }
        if (!vscn_append(buf, len, cap, "%s  ]", indent))
            return 0;
    }

    return vscn_append(buf, len, cap, "\n%s}", indent);
}

/// @brief Reverse of `vscn_serialize_texture` — build a Pixels object from a JSON entry.
static rt_pixels_impl *vscn_parse_texture(void *texture_obj) {
    int64_t width;
    int64_t height;
    const char *rgba_b64;
    size_t rgba_len = 0;
    size_t rgba_error = SIZE_MAX;
    uint8_t *rgba = NULL;
    rt_pixels_impl *pixels = NULL;

    if (!vjson_is_map(texture_obj))
        return NULL;
    if (!vjson_i64_exact(texture_obj, "width", 0, &width) ||
        !vjson_i64_exact(texture_obj, "height", 0, &height))
        return NULL;
    rgba_b64 = vjson_cstr(texture_obj, "rgbaBase64");
    if (width <= 0 || height <= 0)
        return NULL;
    if ((uint64_t)width > SIZE_MAX || (uint64_t)height > SIZE_MAX)
        return NULL;
    if ((size_t)width > SIZE_MAX / (size_t)(height > 0 ? height : 1))
        return NULL;
    if ((size_t)width * (size_t)height > SIZE_MAX / 4)
        return NULL;
    if (!rgba_b64)
        rgba_b64 = "";

    rgba = vscn_base64_decode_ex(rgba_b64, strlen(rgba_b64), &rgba_len, &rgba_error);
    if (!rgba) {
        if (rgba_error != SIZE_MAX)
            vscn_trap_base64_error("texture.rgbaBase64", rgba_error);
        return NULL;
    }
    if (rgba_len != (size_t)width * (size_t)height * 4) {
        free(rgba);
        return NULL;
    }

    pixels = (rt_pixels_impl *)rt_pixels_new(width, height);
    if (!pixels) {
        free(rgba);
        return NULL;
    }

    for (size_t i = 0; i < (size_t)width * (size_t)height; i++) {
        pixels->data[i] = ((uint32_t)rgba[i * 4 + 0] << 24) | ((uint32_t)rgba[i * 4 + 1] << 16) |
                          ((uint32_t)rgba[i * 4 + 2] << 8) | (uint32_t)rgba[i * 4 + 3];
    }
    free(rgba);
    pixels_touch(pixels);
    return pixels;
}

/// @brief Reverse of `vscn_serialize_cubemap` — assemble a cubemap from texture-index references.
static rt_cubemap3d *vscn_parse_cubemap(void *cubemap_obj,
                                        rt_pixels_impl **textures,
                                        int tex_count) {
    void *faces_arr;
    void *faces[6];

    if (!vjson_is_map(cubemap_obj))
        return NULL;
    faces_arr = vjson_get(cubemap_obj, "faces");
    if (!faces_arr || vjson_len(faces_arr) < 6)
        return NULL;

    for (int i = 0; i < 6; i++) {
        int64_t index;
        if (!vjson_arr_i64_exact(faces_arr, i, -1, &index))
            return NULL;
        if (index < 0 || index >= tex_count || !textures[index])
            return NULL;
        faces[i] = textures[index];
    }

    return (rt_cubemap3d *)rt_cubemap3d_new(
        faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]);
}

#include "rt_scene3d_vscn_material_parse.inc"

/// @brief Reverse of `vscn_serialize_material` — restore PBR parameters and bind texture refs.
static rt_material3d *vscn_parse_material(void *material_obj,
                                          rt_pixels_impl **textures,
                                          int tex_count,
                                          rt_cubemap3d **cubemaps,
                                          int cubemap_count) {
    rt_material3d *material;

    if (!vjson_is_map(material_obj))
        return NULL;
    material = (rt_material3d *)rt_material3d_new();
    if (!material)
        return NULL;

    vscn_parse_material_color4(material_obj, "diffuse", material->diffuse, 1.0);
    vscn_parse_material_color3(material_obj, "specular", material->specular, VSCN_ABS_MAX);
    vscn_parse_material_color3(material_obj, "emissive", material->emissive, VSCN_ABS_MAX);
    vscn_parse_material_scalars(material_obj, material);
    vscn_parse_material_custom_params(material_obj, material);
    if (!vscn_parse_material_texture_slots(material_obj, material) ||
        !vscn_bind_material_refs(
            material_obj, material, textures, tex_count, cubemaps, cubemap_count)) {
        scene3d_release_ref((void **)&material);
        return NULL;
    }

    return material;
}

/// @brief Validate raw vertex payloads loaded from VSCN before they reach bounds, skinning, or
///   backend upload code.
static int vscn_vertex_payload_is_valid(const vgfx3d_vertex_t *vertices, uint32_t vertex_count) {
    if (!vertices && vertex_count > 0)
        return 0;
    for (uint32_t vi = 0; vi < vertex_count; vi++) {
        const vgfx3d_vertex_t *v = &vertices[vi];
        float weight_sum = 0.0f;
        for (int i = 0; i < 3; i++) {
            if (!isfinite((double)v->pos[i]) || !isfinite((double)v->normal[i]))
                return 0;
        }
        for (int i = 0; i < 2; i++) {
            if (!isfinite((double)v->uv[i]) || !isfinite((double)v->uv1[i]))
                return 0;
        }
        for (int i = 0; i < 4; i++) {
            if (!isfinite((double)v->color[i]) || !isfinite((double)v->tangent[i]) ||
                !isfinite((double)v->bone_weights[i]) || v->bone_weights[i] < 0.0f ||
                v->bone_weights[i] > 1.0f)
                return 0;
            weight_sum += v->bone_weights[i];
        }
        if (weight_sum > 1.0001f)
            return 0;
    }
    return 1;
}

/// @brief Reverse of `vscn_serialize_mesh` — decode base64 buffers and rebuild the mesh.
static rt_mesh3d *vscn_parse_mesh(void *mesh_obj) {
    rt_mesh3d *mesh;
    const char *vertex_format;
    const char *vertices_b64;
    const char *indices_b64;
    int64_t vertex_count_i64;
    int64_t index_count_i64;
    uint32_t vertex_count;
    uint32_t index_count;
    size_t vertices_len = 0;
    size_t indices_len = 0;
    size_t vertices_error = SIZE_MAX;
    size_t indices_error = SIZE_MAX;
    uint8_t *vertices_raw = NULL;
    uint8_t *indices_raw = NULL;

    if (!vjson_is_map(mesh_obj))
        return NULL;

    vertex_format = vjson_cstr(mesh_obj, "vertexFormat");
    if (vertex_format && strcmp(vertex_format, "vgfx3d_vertex_le_v1") != 0 &&
        strcmp(vertex_format, "vgfx3d_vertex_le_v2") != 0)
        return NULL;

    if (!vjson_i64_exact(mesh_obj, "vertexCount", 0, &vertex_count_i64) ||
        !vjson_i64_exact(mesh_obj, "indexCount", 0, &index_count_i64))
        return NULL;
    if (vertex_count_i64 < 0 || index_count_i64 < 0 || vertex_count_i64 > UINT32_MAX ||
        index_count_i64 > UINT32_MAX)
        return NULL;
    vertex_count = (uint32_t)vertex_count_i64;
    index_count = (uint32_t)index_count_i64;
    if (index_count % 3u != 0)
        return NULL;
    if ((size_t)vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)vertex_count > SIZE_MAX / 84u || (size_t)index_count > SIZE_MAX / sizeof(uint32_t))
        return NULL;
    vertices_b64 = vjson_cstr(mesh_obj, "verticesBase64");
    indices_b64 = vjson_cstr(mesh_obj, "indicesBase64");
    if (!vertices_b64)
        vertices_b64 = "";
    if (!indices_b64)
        indices_b64 = "";

    vertices_raw =
        vscn_base64_decode_ex(vertices_b64, strlen(vertices_b64), &vertices_len, &vertices_error);
    indices_raw =
        vscn_base64_decode_ex(indices_b64, strlen(indices_b64), &indices_len, &indices_error);
    if (!vertices_raw || !indices_raw) {
        if (!vertices_raw && vertices_error != SIZE_MAX)
            vscn_trap_base64_error("mesh.verticesBase64", vertices_error);
        else if (!indices_raw && indices_error != SIZE_MAX)
            vscn_trap_base64_error("mesh.indicesBase64", indices_error);
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }
    if ((vertices_len != (size_t)vertex_count * sizeof(vgfx3d_vertex_t) &&
         vertices_len != (size_t)vertex_count * 84u) ||
        indices_len != (size_t)index_count * sizeof(uint32_t)) {
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }

    mesh = (rt_mesh3d *)rt_mesh3d_new();
    if (!mesh) {
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }

    if (vertex_count > 0) {
        vgfx3d_vertex_t *vertices =
            (vgfx3d_vertex_t *)malloc((size_t)vertex_count * sizeof(vgfx3d_vertex_t));
        if (!vertices) {
            free(vertices_raw);
            free(indices_raw);
            scene3d_release_ref((void **)&mesh);
            return NULL;
        }
        if (vertices_len == (size_t)vertex_count * sizeof(vgfx3d_vertex_t)) {
            memcpy(vertices, vertices_raw, (size_t)vertex_count * sizeof(vgfx3d_vertex_t));
        } else {
            typedef struct {
                float pos[3];
                float normal[3];
                float uv[2];
                float color[4];
                float tangent[4];
                uint8_t bone_indices[4];
                float bone_weights[4];
            } vgfx3d_vertex_legacy84_t;

            const vgfx3d_vertex_legacy84_t *legacy = (const vgfx3d_vertex_legacy84_t *)vertices_raw;
            for (uint32_t vi = 0; vi < vertex_count; vi++) {
                memset(&vertices[vi], 0, sizeof(vertices[vi]));
                memcpy(vertices[vi].pos, legacy[vi].pos, sizeof(vertices[vi].pos));
                memcpy(vertices[vi].normal, legacy[vi].normal, sizeof(vertices[vi].normal));
                memcpy(vertices[vi].uv, legacy[vi].uv, sizeof(vertices[vi].uv));
                memcpy(vertices[vi].uv1, legacy[vi].uv, sizeof(vertices[vi].uv1));
                memcpy(vertices[vi].color, legacy[vi].color, sizeof(vertices[vi].color));
                memcpy(vertices[vi].tangent, legacy[vi].tangent, sizeof(vertices[vi].tangent));
                memcpy(vertices[vi].bone_indices,
                       legacy[vi].bone_indices,
                       sizeof(vertices[vi].bone_indices));
                memcpy(vertices[vi].bone_weights,
                       legacy[vi].bone_weights,
                       sizeof(vertices[vi].bone_weights));
            }
        }
        if (!vscn_vertex_payload_is_valid(vertices, vertex_count)) {
            free(vertices);
            free(vertices_raw);
            free(indices_raw);
            scene3d_release_ref((void **)&mesh);
            return NULL;
        }
        free(mesh->vertices);
        free(mesh->positions64);
        mesh->vertices = vertices;
        mesh->positions64 = NULL;
        mesh->vertex_count = vertex_count;
        mesh->vertex_capacity = vertex_count;
    } else {
        mesh->vertex_count = 0;
    }

    if (index_count > 0) {
        uint32_t *indices = (uint32_t *)malloc((size_t)index_count * sizeof(uint32_t));
        if (!indices) {
            free(vertices_raw);
            free(indices_raw);
            scene3d_release_ref((void **)&mesh);
            return NULL;
        }
        memcpy(indices, indices_raw, (size_t)index_count * sizeof(uint32_t));
        for (uint32_t i = 0; i < index_count; i++) {
            if (indices[i] >= vertex_count) {
                free(indices);
                free(vertices_raw);
                free(indices_raw);
                scene3d_release_ref((void **)&mesh);
                return NULL;
            }
        }
        free(mesh->indices);
        mesh->indices = indices;
        mesh->index_count = index_count;
        mesh->index_capacity = index_count;
    } else {
        mesh->index_count = 0;
    }

    {
        int64_t bone_count = 0;
        if (!vjson_i64_exact(mesh_obj, "boneCount", 0, &bone_count) ||
            bone_count > VGFX3D_MAX_BONES) {
            scene3d_release_ref((void **)&mesh);
            return NULL;
        }
        mesh->bone_count = bone_count > 0 ? (int32_t)bone_count : 0;
    }
    mesh->bone_palette = NULL;
    mesh->prev_bone_palette = NULL;
    mesh->morph_deltas = NULL;
    mesh->morph_normal_deltas = NULL;
    mesh->morph_weights = NULL;
    mesh->prev_morph_weights = NULL;
    mesh->morph_shape_count = 0;
    mesh->morph_targets_ref = NULL;
    mesh->geometry_revision = 1;
    mesh->bounds_dirty = 1;
    rt_mesh3d_set_resident(mesh, vjson_bool(mesh_obj, "resident", 1));
    rt_mesh3d_refresh_bounds(mesh);

    free(vertices_raw);
    free(indices_raw);
    return mesh;
}

/// @brief Parse a JSON light object from a VSCN file into an `rt_light3d` struct.
/// @details Reads type, direction, color, intensity, attenuation, and spot-cone
///          cosines from the vjson object. Defaults to a point light (type 1) when
///          the type field is absent or out of range [0–3].
static rt_light3d *vscn_parse_light(void *light_obj) {
    rt_light3d *light;
    void *arr;
    if (!vjson_is_map(light_obj))
        return NULL;
    light = (rt_light3d *)rt_obj_new_i64(RT_G3D_LIGHT3D_CLASS_ID, (int64_t)sizeof(rt_light3d));
    if (!light)
        return NULL;
    memset(light, 0, sizeof(*light));
    light->type = (int32_t)vjson_i64(light_obj, "type", 1);
    if (light->type < 0 || light->type > 3)
        light->type = 1;
    light->direction[2] = -1.0;
    light->color[0] = light->color[1] = light->color[2] = 1.0;
    light->enabled = 1;
    light->casts_shadows = vjson_bool(light_obj, "castsShadows", light->type != 2);
    light->intensity = vscn_nonnegative_or(vjson_f64(light_obj, "intensity", 1.0), 1.0);
    light->attenuation = vscn_nonnegative_or(vjson_f64(light_obj, "attenuation", 0.0), 0.0);
    light->inner_cos = vjson_f64(light_obj, "innerCos", 1.0);
    light->outer_cos = vjson_f64(light_obj, "outerCos", 0.7071067811865476);
    if (!isfinite(light->inner_cos) || light->inner_cos < 0.0 || light->inner_cos > 1.0)
        light->inner_cos = 1.0;
    if (!isfinite(light->outer_cos) || light->outer_cos < 0.0 || light->outer_cos > 1.0)
        light->outer_cos = 0.7071067811865476;
    if (light->type == 3 && light->inner_cos <= light->outer_cos + 1e-6) {
        if (light->inner_cos <= 1e-6)
            light->inner_cos = 1.0;
        light->outer_cos = light->inner_cos - 1e-6;
        if (light->outer_cos < 0.0)
            light->outer_cos = 0.0;
    }

    arr = vjson_get(light_obj, "direction");
    if (arr && vjson_len(arr) >= 3) {
        light->direction[0] = vscn_clamp_abs_or(vjson_arr_f64(arr, 0, light->direction[0]), 0.0);
        light->direction[1] = vscn_clamp_abs_or(vjson_arr_f64(arr, 1, light->direction[1]), 0.0);
        light->direction[2] = vscn_clamp_abs_or(vjson_arr_f64(arr, 2, light->direction[2]), -1.0);
    }
    vscn_normalize_vec3(light->direction, 0.0, 0.0, -1.0);
    arr = vjson_get(light_obj, "position");
    if (arr && vjson_len(arr) >= 3) {
        light->position[0] = vscn_clamp_abs_or(vjson_arr_f64(arr, 0, light->position[0]), 0.0);
        light->position[1] = vscn_clamp_abs_or(vjson_arr_f64(arr, 1, light->position[1]), 0.0);
        light->position[2] = vscn_clamp_abs_or(vjson_arr_f64(arr, 2, light->position[2]), 0.0);
    }
    arr = vjson_get(light_obj, "color");
    if (arr && vjson_len(arr) >= 3) {
        light->color[0] =
            vscn_clamp_or(vjson_arr_f64(arr, 0, light->color[0]), 1.0, 0.0, VSCN_ABS_MAX);
        light->color[1] =
            vscn_clamp_or(vjson_arr_f64(arr, 1, light->color[1]), 1.0, 0.0, VSCN_ABS_MAX);
        light->color[2] =
            vscn_clamp_or(vjson_arr_f64(arr, 2, light->color[2]), 1.0, 0.0, VSCN_ABS_MAX);
    }
    return light;
}

/// @brief Reverse of `vscn_serialize_node` — recursively rebuild a node subtree from JSON.
static rt_scene_node3d *vscn_parse_node(void *node_obj,
                                        rt_mesh3d **meshes,
                                        int mesh_count,
                                        rt_material3d **materials,
                                        int material_count,
                                        int *io_error,
                                        int depth) {
    rt_scene_node3d *node;
    void *arr;
    rt_string name;

    if (!vjson_is_map(node_obj)) {
        if (io_error)
            *io_error = 1;
        return NULL;
    }
    if (depth > VSCN_MAX_NODE_DEPTH) {
        if (io_error)
            *io_error = 1;
        return NULL;
    }
    node = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!node) {
        if (io_error)
            *io_error = 1;
        return NULL;
    }

    name = vjson_string_value(node_obj, "name");
    if (name)
        rt_scene_node3d_set_name(node, name);

    arr = vjson_get(node_obj, "position");
    if (arr && !vjson_is_seq(arr)) {
        if (io_error)
            *io_error = 1;
        scene3d_release_ref((void **)&node);
        return NULL;
    }
    if (arr && vjson_len(arr) >= 3) {
        node->position[0] = vscn_clamp_abs_or(vjson_arr_f64(arr, 0, node->position[0]), 0.0);
        node->position[1] = vscn_clamp_abs_or(vjson_arr_f64(arr, 1, node->position[1]), 0.0);
        node->position[2] = vscn_clamp_abs_or(vjson_arr_f64(arr, 2, node->position[2]), 0.0);
    }

    arr = vjson_get(node_obj, "rotation");
    if (arr && !vjson_is_seq(arr)) {
        if (io_error)
            *io_error = 1;
        scene3d_release_ref((void **)&node);
        return NULL;
    }
    if (arr && vjson_len(arr) >= 4) {
        node->rotation[0] = vjson_arr_f64(arr, 0, node->rotation[0]);
        node->rotation[1] = vjson_arr_f64(arr, 1, node->rotation[1]);
        node->rotation[2] = vjson_arr_f64(arr, 2, node->rotation[2]);
        node->rotation[3] = vjson_arr_f64(arr, 3, node->rotation[3]);
        vscn_normalize_quat(node->rotation);
    }

    arr = vjson_get(node_obj, "scale");
    if (arr && !vjson_is_seq(arr)) {
        if (io_error)
            *io_error = 1;
        scene3d_release_ref((void **)&node);
        return NULL;
    }
    if (arr && vjson_len(arr) >= 3) {
        node->scale_xyz[0] = vscn_clamp_abs_or(vjson_arr_f64(arr, 0, node->scale_xyz[0]), 1.0);
        node->scale_xyz[1] = vscn_clamp_abs_or(vjson_arr_f64(arr, 1, node->scale_xyz[1]), 1.0);
        node->scale_xyz[2] = vscn_clamp_abs_or(vjson_arr_f64(arr, 2, node->scale_xyz[2]), 1.0);
    }

    node->visible = vjson_bool(node_obj, "visible", 1);
    node->world_dirty = 1;

    {
        int64_t mesh_index;
        if (!vscn_read_index_ref(node_obj, "mesh", &mesh_index) ||
            (mesh_index >= 0 && (mesh_index >= mesh_count || !meshes || !meshes[mesh_index]))) {
            if (io_error)
                *io_error = 1;
            scene3d_release_ref((void **)&node);
            return NULL;
        }
        if (mesh_index >= 0 && mesh_index < mesh_count && meshes[mesh_index])
            rt_scene_node3d_set_mesh(node, meshes[mesh_index]);
    }
    {
        int64_t material_index;
        if (!vscn_read_index_ref(node_obj, "material", &material_index) ||
            (material_index >= 0 &&
             (material_index >= material_count || !materials || !materials[material_index]))) {
            if (io_error)
                *io_error = 1;
            scene3d_release_ref((void **)&node);
            return NULL;
        }
        if (material_index >= 0 && material_index < material_count && materials[material_index])
            rt_scene_node3d_set_material(node, materials[material_index]);
    }
    {
        void *light_obj = vjson_get(node_obj, "light");
        rt_light3d *light = vscn_parse_light(light_obj);
        if (light_obj && !light) {
            if (io_error)
                *io_error = 1;
            scene3d_release_ref((void **)&node);
            return NULL;
        }
        if (light) {
            rt_scene_node3d_set_light(node, light);
            {
                void *tmp = light;
                scene3d_release_ref(&tmp);
            }
        }
    }

    arr = vjson_get(node_obj, "lod");
    if (arr) {
        if (!vjson_is_seq(arr)) {
            if (io_error)
                *io_error = 1;
            scene3d_release_ref((void **)&node);
            return NULL;
        }
        for (int64_t i = 0; i < vjson_len(arr); i++) {
            void *lod_obj = rt_seq_get(arr, i);
            int64_t mesh_index;
            if (!lod_obj || !vscn_read_index_ref(lod_obj, "mesh", &mesh_index) ||
                (mesh_index >= 0 && (mesh_index >= mesh_count || !meshes || !meshes[mesh_index]))) {
                if (io_error)
                    *io_error = 1;
                scene3d_release_ref((void **)&node);
                return NULL;
            }
            if (mesh_index >= 0 && mesh_index < mesh_count && meshes[mesh_index]) {
                rt_scene_node3d_add_lod(
                    node,
                    vscn_nonnegative_or(vjson_f64(lod_obj, "distance", 0.0), 0.0),
                    meshes[mesh_index]);
            }
        }
    }

    {
        void *auto_lod = vjson_get(node_obj, "autoLOD");
        if (auto_lod && vjson_is_map(auto_lod)) {
            rt_scene_node3d_set_auto_lod(
                node,
                vjson_bool(auto_lod, "enabled", 0),
                vscn_nonnegative_or(vjson_f64(auto_lod, "screenErrorPx", 8.0), 8.0));
        }
    }

    arr = vjson_get(node_obj, "children");
    if (arr) {
        if (!vjson_is_seq(arr)) {
            if (io_error)
                *io_error = 1;
            scene3d_release_ref((void **)&node);
            return NULL;
        }
        for (int64_t i = 0; i < vjson_len(arr); i++) {
            rt_scene_node3d *child = vscn_parse_node(rt_seq_get(arr, i),
                                                     meshes,
                                                     mesh_count,
                                                     materials,
                                                     material_count,
                                                     io_error,
                                                     depth + 1);
            if (io_error && *io_error) {
                scene3d_release_ref((void **)&node);
                return NULL;
            }
            if (child) {
                if (!rt_scene_node3d_try_add_child(node, child)) {
                    if (io_error)
                        *io_error = 1;
                    scene3d_release_ref((void **)&child);
                    scene3d_release_ref((void **)&node);
                    return NULL;
                }
                {
                    void *tmp = child;
                    scene3d_release_ref(&tmp);
                }
            }
        }
    }

    return node;
}

/// @brief Public API: save a Scene3D to a `.vscn` file at `path`.
///
/// Two-pass algorithm: (1) walk the scene to populate the mesh /
/// material / texture / cubemap dedupe tables, (2) emit the JSON
/// document with shared assets first, then nodes referencing them
/// by index. Output is pretty-printed for diff-friendliness.
/// @return 1 on success, 0 on any failure (open, alloc, write).
/// @brief Free the four shared-asset pointer tables collected for a save (every failure exit).
static void vscn_save_free_ctx(vscn_save_context_t *ctx) {
    vscn_free_ptr_table(&ctx->meshes);
    vscn_free_ptr_table(&ctx->materials);
    vscn_free_ptr_table(&ctx->textures);
    vscn_free_ptr_table(&ctx->cubemaps);
}

/// @brief Emit the `"textures": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_textures(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"textures\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->textures.count; i++) {
        if (!vscn_serialize_texture((rt_pixels_impl *)ctx->textures.items[i], buf, len, cap, 2) ||
            (i < ctx->textures.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the `"cubemaps": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_cubemaps(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"cubemaps\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->cubemaps.count; i++) {
        if (!vscn_serialize_cubemap((rt_cubemap3d *)ctx->cubemaps.items[i], ctx, buf, len, cap, 2) ||
            (i < ctx->cubemaps.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the `"materials": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_materials(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"materials\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->materials.count; i++) {
        if (!vscn_serialize_material(
                (rt_material3d *)ctx->materials.items[i], ctx, buf, len, cap, 2) ||
            (i < ctx->materials.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the `"meshes": [ ... ],` array. @return 1 on success, 0 on append failure.
static int vscn_save_emit_meshes(char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx) {
    if (!vscn_append(buf, len, cap, "  \"meshes\": [\n"))
        return 0;
    for (int32_t i = 0; i < ctx->meshes.count; i++) {
        if (!vscn_serialize_mesh((rt_mesh3d *)ctx->meshes.items[i], buf, len, cap, 2) ||
            (i < ctx->meshes.count - 1 && !vscn_append(buf, len, cap, ",")) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
    }
    return vscn_append(buf, len, cap, "  ],\n");
}

/// @brief Emit the closing `"nodes": [ ... ]` array (root's children; root is implicit).
/// @return 1 on success, 0 on append failure.
static int vscn_save_emit_nodes(
    char **buf, size_t *len, size_t *cap, vscn_save_context_t *ctx, rt_scene_node3d *root) {
    int32_t child_count = scene3d_node_child_count(root);
    int32_t emitted = 0;
    if (!vscn_append(buf, len, cap, "  \"nodes\": [\n"))
        return 0;
    for (int32_t i = 0; i < child_count; i++) {
        if (!root->children[i])
            continue;
        rt_scene_node3d *child = scene_node3d_checked(root->children[i]);
        if (!child)
            continue;
        if (emitted > 0 && !vscn_append(buf, len, cap, ","))
            return 0;
        if (!vscn_serialize_node(child, ctx, buf, len, cap, 2) ||
            !vscn_append(buf, len, cap, "\n"))
            return 0;
        emitted++;
    }
    return vscn_append(buf, len, cap, "  ]\n");
}

/// @brief Serialize the scene to a .vscn file. @return 1 on success, 0 on failure.
int64_t rt_scene3d_save(void *scene_obj, rt_string path) {
    if (!scene_obj || !path)
        return 0;
    rt_scene3d *scene = (rt_scene3d *)rt_g3d_checked_or_null(scene_obj, RT_G3D_SCENE3D_CLASS_ID);
    if (!scene)
        return 0;
    if (!scene->root)
        return 0;

    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return 0;

    vscn_save_context_t ctx = {0};
    char *buf = NULL;
    size_t len = 0, cap = 0;
    FILE *f;
    size_t written = 0;

    for (int32_t i = 0, child_count = scene3d_node_child_count(scene->root); i < child_count; i++) {
        if (!scene->root->children[i])
            continue;
        rt_scene_node3d *child = scene_node3d_checked(scene->root->children[i]);
        if (!child)
            continue;
        if (!vscn_collect_node_assets(child, &ctx)) {
            vscn_save_free_ctx(&ctx);
            return 0;
        }
    }

    if (!vscn_append(&buf, &len, &cap, "{\n") ||
        !vscn_append(&buf, &len, &cap, "  \"format\": \"vscn\",\n") ||
        !vscn_append(&buf, &len, &cap, "  \"version\": 2,\n") ||
        !vscn_save_emit_textures(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_cubemaps(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_materials(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_meshes(&buf, &len, &cap, &ctx) ||
        !vscn_save_emit_nodes(&buf, &len, &cap, &ctx, scene->root) ||
        !vscn_append(&buf, &len, &cap, "}\n")) {
        vscn_save_free_ctx(&ctx);
        free(buf);
        return 0;
    }

    f = fopen(filepath, "wb");
    if (!f) {
        vscn_save_free_ctx(&ctx);
        free(buf);
        return 0;
    }
    while (written < len) {
        size_t chunk = fwrite(buf + written, 1, len - written, f);
        if (chunk == 0) {
            fclose(f);
            vscn_save_free_ctx(&ctx);
            free(buf);
            return 0;
        }
        written += chunk;
    }
    if (fflush(f) != 0 || fclose(f) != 0) {
        vscn_save_free_ctx(&ctx);
        free(buf);
        return 0;
    }
    vscn_save_free_ctx(&ctx);
    free(buf);
    return 1;
}

/// @brief Read an entire file into a newly-malloc'd, NUL-terminated buffer.
/// @return The buffer (caller frees) with its byte length in @p out_size, or NULL on I/O error or
///   when the file exceeds VSCN_MAX_FILE_BYTES (256 MiB).
static char *vscn_read_file(const char *filepath, long *out_size) {
    FILE *f = fopen(filepath, "rb");
    long file_size;
    char *json;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    file_size = ftell(f);
    if (file_size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    if ((uint64_t)file_size > VSCN_MAX_FILE_BYTES || (uint64_t)file_size > SIZE_MAX - 1) {
        fclose(f);
        return NULL;
    }
    json = (char *)malloc((size_t)file_size + 1);
    if (!json) {
        fclose(f);
        return NULL;
    }
    if (file_size > 0 && fread(json, 1, (size_t)file_size, f) != (size_t)file_size) {
        fclose(f);
        free(json);
        return NULL;
    }
    fclose(f);
    json[file_size] = '\0';
    *out_size = file_size;
    return json;
}

/// @brief Parse `nodes_arr` into scene-graph children of @p scene's root (no-op when array absent).
/// @return 1 on success, 0 on a node parse failure (the offending node is released).
static int vscn_load_nodes(rt_scene3d *scene,
                           void *nodes_arr,
                           rt_mesh3d **meshes,
                           int mesh_count,
                           rt_material3d **materials,
                           int material_count) {
    if (!nodes_arr)
        return 1;
    for (int64_t i = 0; i < vjson_len(nodes_arr); i++) {
        int parse_error = 0;
        rt_scene_node3d *node = vscn_parse_node(
            rt_seq_get(nodes_arr, i), meshes, mesh_count, materials, material_count, &parse_error, 0);
        if (parse_error || !node) {
            scene3d_release_ref((void **)&node);
            return 0;
        }
        if (!rt_scene_node3d_try_add_child(scene->root, node)) {
            scene3d_release_ref((void **)&node);
            return 0;
        }
        {
            void *tmp = node;
            scene3d_release_ref(&tmp);
        }
    }
    return 1;
}

/// @brief Deserialize a Scene3D from a `.vscn` (JSON) file; returns NULL on failure.
/// @details Inverts `rt_scene3d_save`: parses the JSON, rebuilds the shared-asset arrays in
///   dependency order (textures, then cubemaps, then materials, then meshes), and finally walks the
///   node tree wiring index references back to the freshly-loaded objects. All partially-loaded
///   refs are released on any failure. glTF/FBX scenes load through rt_gltf_load / rt_fbx_load.
void *rt_scene3d_load(rt_string path) {
    const char *filepath;
    char *json = NULL;
    rt_string json_text = NULL;
    long file_size;
    void *root = NULL;
    void *textures_arr;
    void *cubemaps_arr;
    void *materials_arr;
    void *meshes_arr;
    void *nodes_arr;
    int tex_count = 0;
    int cubemap_count = 0;
    int material_count = 0;
    int mesh_count = 0;
    rt_pixels_impl **textures = NULL;
    rt_cubemap3d **cubemaps = NULL;
    rt_material3d **materials = NULL;
    rt_mesh3d **meshes = NULL;
    rt_scene3d *scene = NULL;

    if (!path)
        return NULL;
    filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    json = vscn_read_file(filepath, &file_size);
    if (!json)
        return NULL;

    json_text = rt_string_from_bytes(json, (size_t)file_size);
    free(json);
    json = NULL;
    if (!json_text)
        return NULL;
    {
        const char *json_cstr = rt_string_cstr(json_text);
        while (json_cstr && (*json_cstr == ' ' || *json_cstr == '\n' || *json_cstr == '\r' ||
                             *json_cstr == '\t'))
            json_cstr++;
        if (!json_cstr || *json_cstr != '{') {
            rt_string_unref(json_text);
            return NULL;
        }
    }
    if (rt_json_is_valid(json_text) != 1) {
        rt_string_unref(json_text);
        return NULL;
    }
    root = rt_json_parse_object(json_text);
    rt_string_unref(json_text);
    json_text = NULL;
    if (!root)
        return NULL;

    textures_arr = vjson_get(root, "textures");
    cubemaps_arr = vjson_get(root, "cubemaps");
    materials_arr = vjson_get(root, "materials");
    meshes_arr = vjson_get(root, "meshes");
    nodes_arr = vjson_get(root, "nodes");

    {
        const char *format = vjson_cstr(root, "format");
        void *version_value = vjson_get(root, "version");
        int64_t version = 1;
        if (version_value && !vjson_value_i64_exact(version_value, &version))
            goto fail;
        if ((format && strcmp(format, "vscn") != 0) || version < 1 || version > 2)
            goto fail;
        if ((textures_arr && !vjson_is_seq(textures_arr)) ||
            (cubemaps_arr && !vjson_is_seq(cubemaps_arr)) ||
            (materials_arr && !vjson_is_seq(materials_arr)) ||
            (meshes_arr && !vjson_is_seq(meshes_arr)) || (nodes_arr && !vjson_is_seq(nodes_arr)))
            goto fail;
    }

    if (vjson_len(textures_arr) > INT32_MAX || vjson_len(cubemaps_arr) > INT32_MAX ||
        vjson_len(materials_arr) > INT32_MAX || vjson_len(meshes_arr) > INT32_MAX)
        goto fail;
    tex_count = (int)vjson_len(textures_arr);
    cubemap_count = (int)vjson_len(cubemaps_arr);
    material_count = (int)vjson_len(materials_arr);
    mesh_count = (int)vjson_len(meshes_arr);

    if (tex_count > 0)
        textures = (rt_pixels_impl **)calloc((size_t)tex_count, sizeof(rt_pixels_impl *));
    if (cubemap_count > 0)
        cubemaps = (rt_cubemap3d **)calloc((size_t)cubemap_count, sizeof(rt_cubemap3d *));
    if (material_count > 0)
        materials = (rt_material3d **)calloc((size_t)material_count, sizeof(rt_material3d *));
    if (mesh_count > 0)
        meshes = (rt_mesh3d **)calloc((size_t)mesh_count, sizeof(rt_mesh3d *));
    if ((tex_count > 0 && !textures) || (cubemap_count > 0 && !cubemaps) ||
        (material_count > 0 && !materials) || (mesh_count > 0 && !meshes)) {
        free(textures);
        free(cubemaps);
        free(materials);
        free(meshes);
        scene3d_release_ref(&root);
        return NULL;
    }

    for (int i = 0; i < tex_count; i++) {
        textures[i] = vscn_parse_texture(rt_seq_get(textures_arr, (int64_t)i));
        if (!textures[i])
            goto fail;
    }
    for (int i = 0; i < cubemap_count; i++) {
        cubemaps[i] = vscn_parse_cubemap(rt_seq_get(cubemaps_arr, (int64_t)i), textures, tex_count);
        if (!cubemaps[i])
            goto fail;
    }
    for (int i = 0; i < material_count; i++) {
        materials[i] = vscn_parse_material(
            rt_seq_get(materials_arr, (int64_t)i), textures, tex_count, cubemaps, cubemap_count);
        if (!materials[i])
            goto fail;
    }
    for (int i = 0; i < mesh_count; i++) {
        meshes[i] = vscn_parse_mesh(rt_seq_get(meshes_arr, (int64_t)i));
        if (!meshes[i])
            goto fail;
    }

    scene = (rt_scene3d *)rt_scene3d_new();
    if (!scene)
        goto fail;

    if (!vscn_load_nodes(scene, nodes_arr, meshes, mesh_count, materials, material_count))
        goto fail;
    scene->node_count = scene3d_count_subtree(scene->root);
    if (scene->node_count <= 0)
        goto fail;
    scene->last_culled_count = 0;

    vscn_release_loaded_refs((void **)meshes, mesh_count);
    vscn_release_loaded_refs((void **)materials, material_count);
    vscn_release_loaded_refs((void **)cubemaps, cubemap_count);
    vscn_release_loaded_refs((void **)textures, tex_count);
    scene3d_release_ref(&root);
    return scene;

fail:
    if (json_text)
        rt_string_unref(json_text);
    free(json);
    vscn_release_loaded_refs((void **)meshes, mesh_count);
    vscn_release_loaded_refs((void **)materials, material_count);
    vscn_release_loaded_refs((void **)cubemaps, cubemap_count);
    vscn_release_loaded_refs((void **)textures, tex_count);
    scene3d_release_ref((void **)&scene);
    scene3d_release_ref(&root);
    return NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
