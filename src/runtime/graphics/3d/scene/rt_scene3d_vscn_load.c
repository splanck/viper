//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_vscn_load.c
// Purpose: Scene3D .vscn deserialization (load). Parses the JSON scene
//   document, decodes base64 asset payloads, and rebuilds the Scene3D /
//   SceneNode3D tree. Rolls back partial state on any error so a half-loaded
//   scene never reaches the caller.
//
// Key invariants:
//   - Loader validates structure and clamps numeric values before use.
//   - On any failure, all partially-loaded resources are released.
//
// Ownership/Lifetime:
//   - Produces Scene3D / SceneNode3D objects defined in rt_scene3d.c;
//     this TU owns no GC objects of its own once load completes.
//
// Links: rt_scene3d.h, rt_scene3d_internal.h, rt_scene3d_vscn_internal.h,
//        rt_scene3d_vscn_material_parse.inc (material parsers),
//        rt_scene3d_vscn_save.c (inverse: save), rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#if !defined(_WIN32)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE
#endif
#endif

#include "rt_asset_error.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_file_stdio.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_scene3d_vscn_internal.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_untrusted_count.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define vscn_fseek(fp, off, whence) rt_file_stdio_seek64((fp), (off), (whence))
#define vscn_ftell(fp) rt_file_stdio_tell64((fp))

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
            if (new_capacity <= capacity || new_capacity > SIZE_MAX / sizeof(rt_scene_node3d *)) {
                free(stack);
                return INT32_MAX;
            }
            grown = (const rt_scene_node3d **)realloc((void *)stack,
                                                      new_capacity * sizeof(rt_scene_node3d *));
            if (!grown) {
                free(stack);
                return -1;
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
        for (int32_t i = 0, child_count = scene3d_node_child_count(current); i < child_count; i++) {
            if (count >= capacity) {
                size_t new_capacity = capacity > 0 ? capacity * 2u : 64u;
                const rt_scene_node3d **grown;
                if (new_capacity <= capacity ||
                    new_capacity > SIZE_MAX / sizeof(rt_scene_node3d *)) {
                    free(stack);
                    return INT32_MAX;
                }
                grown = (const rt_scene_node3d **)realloc((void *)stack,
                                                          new_capacity * sizeof(rt_scene_node3d *));
                if (!grown) {
                    free(stack);
                    return -1;
                }
                stack = grown;
                capacity = new_capacity;
            }
            {
                const rt_scene_node3d *child = (const rt_scene_node3d *)rt_g3d_checked_or_null(
                    current->children[i], RT_G3D_SCENENODE3D_CLASS_ID);
                if (child)
                    stack[count++] = child;
            }
        }
    }
    free(stack);
    return total;
}

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

/// @brief Record invalid base64 in @p field at byte @p offset.
static void vscn_set_base64_error(const char *field, size_t offset) {
    rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT,
                        "Scene3D.Load: invalid base64 in %s at byte offset %zu",
                        field ? field : "data",
                        offset);
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
    if ((size_t)width > SIZE_MAX / (size_t)height)
        return NULL;
    if ((size_t)width * (size_t)height > SIZE_MAX / 4)
        return NULL;
    if (!rgba_b64)
        rgba_b64 = "";

    rgba = vscn_base64_decode_ex(rgba_b64, strlen(rgba_b64), &rgba_len, &rgba_error);
    if (!rgba) {
        if (rgba_error != SIZE_MAX)
            vscn_set_base64_error("texture.rgbaBase64", rgba_error);
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

// Material-parse helpers (color/scalar/texture-slot parsing + texture/cubemap
// ref binding) live in a textual fragment, included here — after the vjson_*
// accessors and numeric sanitizers they depend on, and before the material
// parser that calls them.
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
    int64_t bone_count_i64 = 0;
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
        index_count_i64 > UINT32_MAX) {
        rt_asset_error_set(RT_ASSET_ERROR_TOO_LARGE, "Scene3D.Load: mesh count exceeds range");
        return NULL;
    }
    if (!vjson_i64_exact(mesh_obj, "boneCount", 0, &bone_count_i64) || bone_count_i64 < 0 ||
        bone_count_i64 > VGFX3D_MAX_BONES) {
        rt_asset_error_set(bone_count_i64 > VGFX3D_MAX_BONES ? RT_ASSET_ERROR_TOO_LARGE
                                                             : RT_ASSET_ERROR_CORRUPT,
                           "Scene3D.Load: mesh bone count is invalid");
        return NULL;
    }
    vertex_count = (uint32_t)vertex_count_i64;
    index_count = (uint32_t)index_count_i64;
    if (index_count % 3u != 0) {
        rt_asset_error_set(RT_ASSET_ERROR_CORRUPT,
                           "Scene3D.Load: mesh index count is not a triangle list");
        return NULL;
    }
    if ((size_t)vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t) ||
        (size_t)vertex_count > SIZE_MAX / 84u ||
        (size_t)index_count > SIZE_MAX / sizeof(uint32_t)) {
        rt_asset_error_set(RT_ASSET_ERROR_TOO_LARGE, "Scene3D.Load: mesh payload is too large");
        return NULL;
    }
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
            vscn_set_base64_error("mesh.verticesBase64", vertices_error);
        else if (!indices_raw && indices_error != SIZE_MAX)
            vscn_set_base64_error("mesh.indicesBase64", indices_error);
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }
    if (!rt_untrusted_count_ok(vertex_count_i64, 84u, vertices_len) ||
        !rt_untrusted_count_ok(index_count_i64, sizeof(uint32_t), indices_len)) {
        rt_asset_error_set(RT_ASSET_ERROR_CORRUPT,
                           "Scene3D.Load: mesh payload count exceeds source bytes");
        free(vertices_raw);
        free(indices_raw);
        return NULL;
    }
    if ((vertices_len != (size_t)vertex_count * sizeof(vgfx3d_vertex_t) &&
         vertices_len != (size_t)vertex_count * 84u) ||
        indices_len != (size_t)index_count * sizeof(uint32_t)) {
        rt_asset_error_set(RT_ASSET_ERROR_CORRUPT,
                           "Scene3D.Load: mesh payload size does not match counts");
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

    mesh->bone_count = bone_count_i64 > 0 ? (int32_t)bone_count_i64 : 0;
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

/// @brief Read an entire file into a newly-malloc'd, NUL-terminated buffer.
///
/// @details Uses the platform's 64-bit seek/tell API and rejects files larger
///   than @c VSCN_MAX_FILE_BYTES before allocating. The extra trailing NUL is
///   for the JSON parser convenience and is not included in @p out_size.
///
/// @param filepath Path to the UTF-8 scene file to read.
/// @param out_size Receives the exact byte length of the file on success.
/// @return The buffer (caller frees) with its byte length in @p out_size, or NULL on I/O error or
///   when the file exceeds VSCN_MAX_FILE_BYTES (256 MiB).
static char *vscn_read_file(const char *filepath, size_t *out_size) {
    if (!out_size)
        return NULL;
    FILE *f = rt_file_stdio_open_utf8(filepath, "rb");
    int64_t file_size;
    char *json;
    *out_size = 0;
    if (!f) {
        rt_asset_error_setf(RT_ASSET_ERROR_NOT_FOUND, "Scene3D.Load: '%s' not found", filepath);
        return NULL;
    }
    if (vscn_fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNREADABLE, "Scene3D.Load: failed to seek '%s'", filepath);
        return NULL;
    }
    file_size = (int64_t)vscn_ftell(f);
    if (file_size < 0 || vscn_fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNREADABLE, "Scene3D.Load: failed to read '%s'", filepath);
        return NULL;
    }
    if ((uint64_t)file_size > VSCN_MAX_FILE_BYTES || (uint64_t)file_size > SIZE_MAX - 1) {
        fclose(f);
        rt_asset_error_setf(RT_ASSET_ERROR_TOO_LARGE, "Scene3D.Load: '%s' is too large", filepath);
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
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNREADABLE, "Scene3D.Load: failed to read '%s'", filepath);
        return NULL;
    }
    fclose(f);
    json[(size_t)file_size] = '\0';
    *out_size = (size_t)file_size;
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
        rt_scene_node3d *node = vscn_parse_node(rt_seq_get(nodes_arr, i),
                                                meshes,
                                                mesh_count,
                                                materials,
                                                material_count,
                                                &parse_error,
                                                0);
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
static void *rt_scene3d_load_impl(rt_string path) {
    const char *filepath;
    char *json = NULL;
    rt_string json_text = NULL;
    size_t file_size;
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

    json_text = rt_string_from_bytes(json, file_size);
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
            rt_asset_error_setf(
                RT_ASSET_ERROR_BAD_MAGIC, "Scene3D.Load: '%s' is not a .vscn JSON file", filepath);
            return NULL;
        }
    }
    if (rt_json_is_valid(json_text) != 1) {
        rt_string_unref(json_text);
        rt_asset_error_setf(
            RT_ASSET_ERROR_CORRUPT, "Scene3D.Load: '%s' has invalid JSON", filepath);
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
    if (scene->node_count < 0) {
        rt_asset_error_set(RT_ASSET_ERROR_TOO_LARGE,
                           "Scene3D.Load: scene node hierarchy is too large");
        goto fail;
    }
    if (scene->node_count == INT32_MAX) {
        rt_asset_error_set(RT_ASSET_ERROR_TOO_LARGE, "Scene3D.Load: too many nodes");
        goto fail;
    }
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

void *rt_scene3d_load(rt_string path) {
    rt_asset_error_begin_load();
    if (!path) {
        rt_asset_error_end_load_failure();
        rt_trap("Scene3D.Load: path must not be null");
        return NULL;
    }
    if (!rt_string_cstr(path)) {
        rt_asset_error_end_load_failure();
        rt_trap("Scene3D.Load: invalid path");
        return NULL;
    }
    void *scene = rt_scene3d_load_impl(path);
    if (scene) {
        rt_asset_error_end_load_success();
    } else {
        rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT, "Scene3D.Load: failed to load scene");
        rt_asset_error_end_load_failure();
    }
    return scene;
}

#endif // VIPER_ENABLE_GRAPHICS
