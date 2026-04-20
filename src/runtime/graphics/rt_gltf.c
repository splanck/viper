//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gltf.c
// Purpose: glTF 2.0 (.gltf/.glb) loader implementation.
// Key invariants:
//   - Uses existing rt_json parser for JSON content
//   - Supports .glb binary container (magic 0x46546C67)
//   - Preserves glTF metallic-roughness materials on Material3D's native PBR surface
//   - Mesh primitives support POSITION/NORMAL/TEXCOORD_0 plus COLOR_0, TANGENT,
//     and JOINTS_0/WEIGHTS_0 vertex attributes
//   - Triangle-list, triangle-strip, and triangle-fan primitives are triangulated
// Ownership/Lifetime:
//   - All extracted objects are GC-managed
// Links: rt_gltf.h, rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_gltf.h"
#include "rt_scene3d_internal.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_pixels.h"
#include "rt_quat.h"
#include "rt_skeleton3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include "rt_vec3.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Forward declarations for runtime JSON and collection APIs
extern void *rt_json_parse_object(rt_string text);
extern void *rt_map_get(void *map, rt_string key);
extern int64_t rt_seq_len(void *seq);
extern void *rt_seq_get(void *seq, int64_t index);
extern int64_t rt_box_type(void *box);
extern int64_t rt_unbox_i64(void *boxed);
extern double rt_unbox_f64(void *boxed);
extern int64_t rt_unbox_i1(void *boxed);
extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
#include "rt_trap.h"
extern int64_t rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);
extern void *rt_pixels_load(void *path);
extern void *rt_material3d_new_pbr(double r, double g, double b);
extern void rt_material3d_set_texture(void *obj, void *pixels);
extern void rt_material3d_set_normal_map(void *obj, void *pixels);
extern void rt_material3d_set_metallic(void *obj, double value);
extern void rt_material3d_set_roughness(void *obj, double value);
extern void rt_material3d_set_ao(void *obj, double value);
extern void rt_material3d_set_emissive_intensity(void *obj, double value);
extern void rt_material3d_set_metallic_roughness_map(void *obj, void *pixels);
extern void rt_material3d_set_ao_map(void *obj, void *pixels);
extern void rt_material3d_set_specular_map(void *obj, void *pixels);
extern void rt_material3d_set_emissive_map(void *obj, void *pixels);
extern void rt_material3d_set_alpha(void *obj, double alpha);
extern void rt_material3d_set_normal_scale(void *obj, double value);
extern void rt_material3d_set_alpha_mode(void *obj, int64_t mode);
extern void rt_material3d_set_double_sided(void *obj, int8_t enabled);

//===----------------------------------------------------------------------===//
// Asset container
//===----------------------------------------------------------------------===//

typedef struct {
    void *vptr;
    void **meshes;
    int32_t mesh_count;
    void **materials;
    int32_t material_count;
    void **skeletons;
    int32_t skeleton_count;
    void **animations;
    int32_t animation_count;
    void *scene_root;
    int32_t node_count;
} rt_gltf_asset;

typedef struct {
    void *skeleton;
    int32_t *joint_nodes;
    int32_t *joint_to_bone;
    int32_t joint_count;
} gltf_skin_t;

static void *jget(void *obj, const char *key);
static void *jarr(void *obj, const char *key);
static const char *jstr(void *obj, const char *key);
static int64_t jarr_len(void *arr);
static double jvalue_num(void *value, double def);
static int64_t jvalue_int(void *value, int64_t def);

/// @brief GC finalizer for an `rt_gltf_asset` — release every owned mesh / material / scene root.
static void gltf_asset_finalize(void *obj) {
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (a->meshes) {
        for (int32_t i = 0; i < a->mesh_count; i++) {
            if (a->meshes[i] && rt_obj_release_check0(a->meshes[i]))
                rt_obj_free(a->meshes[i]);
        }
    }
    free(a->meshes);
    a->meshes = NULL;
    if (a->materials) {
        for (int32_t i = 0; i < a->material_count; i++) {
            if (a->materials[i] && rt_obj_release_check0(a->materials[i]))
                rt_obj_free(a->materials[i]);
        }
    }
    free(a->materials);
    a->materials = NULL;
    if (a->skeletons) {
        for (int32_t i = 0; i < a->skeleton_count; i++) {
            if (a->skeletons[i] && rt_obj_release_check0(a->skeletons[i]))
                rt_obj_free(a->skeletons[i]);
        }
    }
    free(a->skeletons);
    a->skeletons = NULL;
    if (a->animations) {
        for (int32_t i = 0; i < a->animation_count; i++) {
            if (a->animations[i] && rt_obj_release_check0(a->animations[i]))
                rt_obj_free(a->animations[i]);
        }
    }
    free(a->animations);
    a->animations = NULL;
    if (a->scene_root && rt_obj_release_check0(a->scene_root))
        rt_obj_free(a->scene_root);
    a->scene_root = NULL;
    a->node_count = 0;
}

//===----------------------------------------------------------------------===//
// JSON helpers
//===----------------------------------------------------------------------===//

/// @brief Set the name of a scene node from a C string (no-op for empty / NULL input).
static void gltf_set_node_name(rt_scene_node3d *node, const char *name) {
    if (!node || !name || name[0] == '\0')
        return;
    rt_scene_node3d_set_name(node, rt_const_cstr(name));
}

/// @brief Decompose a row-major 4x4 transform matrix into separate position, quaternion, and scale.
///
/// glTF nodes can store either a 16-element matrix or explicit
/// TRS — this helper converts the matrix form so the runtime
/// always works with TRS internally. Uses Shepperd's method
/// (largest-trace pivot) for the rotation extraction.
static void gltf_matrix_to_trs(const double *m, double *pos, double *quat, double *scale) {
    double r00, r01, r02;
    double r10, r11, r12;
    double r20, r21, r22;
    double trace, s;
    if (!m || !pos || !quat || !scale)
        return;

    pos[0] = m[3];
    pos[1] = m[7];
    pos[2] = m[11];

    scale[0] = sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    scale[1] = sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    scale[2] = sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    if (scale[0] <= 1e-12)
        scale[0] = 1.0;
    if (scale[1] <= 1e-12)
        scale[1] = 1.0;
    if (scale[2] <= 1e-12)
        scale[2] = 1.0;

    r00 = m[0] / scale[0];
    r10 = m[4] / scale[0];
    r20 = m[8] / scale[0];
    r01 = m[1] / scale[1];
    r11 = m[5] / scale[1];
    r21 = m[9] / scale[1];
    r02 = m[2] / scale[2];
    r12 = m[6] / scale[2];
    r22 = m[10] / scale[2];

    trace = r00 + r11 + r22;
    if (trace > 0.0) {
        s = sqrt(trace + 1.0) * 2.0;
        quat[3] = 0.25 * s;
        quat[0] = (r21 - r12) / s;
        quat[1] = (r02 - r20) / s;
        quat[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        s = sqrt(1.0 + r00 - r11 - r22) * 2.0;
        quat[3] = (r21 - r12) / s;
        quat[0] = 0.25 * s;
        quat[1] = (r01 + r10) / s;
        quat[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        s = sqrt(1.0 + r11 - r00 - r22) * 2.0;
        quat[3] = (r02 - r20) / s;
        quat[0] = (r01 + r10) / s;
        quat[1] = 0.25 * s;
        quat[2] = (r12 + r21) / s;
    } else {
        s = sqrt(1.0 + r22 - r00 - r11) * 2.0;
        quat[3] = (r10 - r01) / s;
        quat[0] = (r02 + r20) / s;
        quat[1] = (r12 + r21) / s;
        quat[2] = 0.25 * s;
    }
}

/// @brief Convert a glTF column-major matrix array into Viper's row-major matrix layout.
static void gltf_matrix_column_major_to_row_major(const double *src, double *dst) {
    if (!src || !dst)
        return;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++)
            dst[row * 4 + col] = src[col * 4 + row];
    }
}

static void gltf_build_trs_matrix(const double *pos,
                                  const double *quat,
                                  const double *scale,
                                  double *out) {
    double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;
    out[0] = (1.0 - (yy + zz)) * scale[0];
    out[1] = (xy - wz) * scale[1];
    out[2] = (xz + wy) * scale[2];
    out[3] = pos[0];
    out[4] = (xy + wz) * scale[0];
    out[5] = (1.0 - (xx + zz)) * scale[1];
    out[6] = (yz - wx) * scale[2];
    out[7] = pos[1];
    out[8] = (xz - wy) * scale[0];
    out[9] = (yz + wx) * scale[1];
    out[10] = (1.0 - (xx + yy)) * scale[2];
    out[11] = pos[2];
    out[12] = 0.0;
    out[13] = 0.0;
    out[14] = 0.0;
    out[15] = 1.0;
}

static int gltf_node_local_matrix(void *nodes_arr, int32_t node_idx, double *out) {
    void *node_json;
    void *matrix_arr;
    void *translation;
    void *rotation;
    void *scale_arr;
    double pos[3] = {0.0, 0.0, 0.0};
    double quat[4] = {0.0, 0.0, 0.0, 1.0};
    double scale[3] = {1.0, 1.0, 1.0};
    if (!nodes_arr || !out || node_idx < 0 || node_idx >= jarr_len(nodes_arr))
        return 0;
    node_json = rt_seq_get(nodes_arr, node_idx);
    if (!node_json)
        return 0;
    matrix_arr = jarr(node_json, "matrix");
    if (matrix_arr && jarr_len(matrix_arr) >= 16) {
        double m[16];
        for (int i = 0; i < 16; i++)
            m[i] = jvalue_num(rt_seq_get(matrix_arr, (int64_t)i), i % 5 == 0 ? 1.0 : 0.0);
        gltf_matrix_column_major_to_row_major(m, out);
        return 1;
    }
    translation = jarr(node_json, "translation");
    rotation = jarr(node_json, "rotation");
    scale_arr = jarr(node_json, "scale");
    for (int i = 0; i < 3; i++) {
        if (translation && jarr_len(translation) > i)
            pos[i] = jvalue_num(rt_seq_get(translation, i), pos[i]);
        if (scale_arr && jarr_len(scale_arr) > i)
            scale[i] = jvalue_num(rt_seq_get(scale_arr, i), scale[i]);
    }
    for (int i = 0; i < 4; i++) {
        if (rotation && jarr_len(rotation) > i)
            quat[i] = jvalue_num(rt_seq_get(rotation, i), quat[i]);
    }
    gltf_build_trs_matrix(pos, quat, scale, out);
    return 1;
}

// ---------------------------------------------------------------------------
// JSON accessor helpers — adapt the runtime JSON map/array API to
// the glTF parser's conventions (default values for missing keys,
// borrowed cstr returns, etc.).
// ---------------------------------------------------------------------------

/// @brief Look up a JSON object field by key (NULL if absent or `obj` is NULL).
static void *jget(void *obj, const char *key) {
    if (!obj)
        return NULL;
    rt_string k = rt_const_cstr(key);
    return rt_map_get(obj, k);
}

/// @brief Read `obj[key]` as a double; returns `def` if absent or wrong type.
static double jnum(void *obj, const char *key, double def) {
    void *v = jget(obj, key);
    if (!v)
        return def;
    switch (rt_box_type(v)) {
        case 0:
            return (double)rt_unbox_i64(v);
        case 1:
            return rt_unbox_f64(v);
        case 2:
            return (double)rt_unbox_i1(v);
        default:
            return def;
    }
}

/// @brief Read `obj[key]` as an int64; returns `def` if absent or wrong type.
static int64_t jint(void *obj, const char *key, int64_t def) {
    void *v = jget(obj, key);
    if (!v)
        return def;
    switch (rt_box_type(v)) {
        case 0:
            return rt_unbox_i64(v);
        case 1:
            return (int64_t)rt_unbox_f64(v);
        case 2:
            return rt_unbox_i1(v);
        default:
            return def;
    }
}

/// @brief Read `obj[key]` as a borrowed C string (NULL if absent or non-string).
static const char *jstr(void *obj, const char *key) {
    void *v = jget(obj, key);
    rt_string s = (rt_string)v;
    return s ? rt_string_cstr(s) : NULL;
}

/// @brief Read `obj[key]` as an array (alias for `jget` — typed for readability).
static void *jarr(void *obj, const char *key) {
    return jget(obj, key);
}

/// @brief Length of a JSON array (0 for NULL).
static int64_t jarr_len(void *arr) {
    return arr ? rt_seq_len(arr) : 0;
}

/// @brief Coerce a boxed JSON value to double with default fallback.
static double jvalue_num(void *value, double def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return (double)rt_unbox_i64(value);
        case 1:
            return rt_unbox_f64(value);
        default:
            return def;
    }
}

/// @brief Coerce a boxed JSON value to int64 with default fallback.
static int64_t jvalue_int(void *value, int64_t def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return rt_unbox_i64(value);
        case 1:
            return (int64_t)rt_unbox_f64(value);
        case 2:
            return rt_unbox_i1(value);
        default:
            return def;
    }
}

/// @brief Recursive count — returns 1 for `node` plus the sum of all descendants.
static int32_t gltf_count_subtree(const rt_scene_node3d *node) {
    int32_t total = 1;
    if (!node)
        return 0;
    for (int32_t i = 0; i < node->child_count; i++)
        total += gltf_count_subtree(node->children[i]);
    return total;
}

//===----------------------------------------------------------------------===//
// Buffer management
//===----------------------------------------------------------------------===//

typedef struct {
    uint8_t *data;
    size_t len;
} gltf_buffer_t;

typedef struct {
    const uint8_t *data;
    int32_t count;
    int32_t stride;
    int32_t comp_type;
    int32_t comp_count;
    int8_t normalized;
} gltf_accessor_view_t;

static const char gltf_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Decode a single base64 character to its 0-63 value (-2 for '=', -1 for invalid).
static int gltf_base64_digit_value(char c) {
    const char *p = strchr(gltf_base64_chars, c);
    if (p)
        return (int)(p - gltf_base64_chars);
    if (c == '=')
        return -2;
    return -1;
}

/// @brief Decode a base64 string of `len` characters into raw bytes (caller `free`s).
static uint8_t *gltf_base64_decode(const char *data, size_t len, size_t *out_len) {
    if (!data)
        return NULL;
    if (len == 0) {
        uint8_t *empty = (uint8_t *)malloc(1);
        if (out_len)
            *out_len = 0;
        return empty;
    }
    if (len % 4 != 0)
        return NULL;

    size_t olen = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=')
        olen--;
    if (len > 1 && data[len - 2] == '=')
        olen--;

    uint8_t *output = (uint8_t *)malloc(olen > 0 ? olen : 1);
    if (!output)
        return NULL;

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        int a = gltf_base64_digit_value(data[i++]);
        int b = gltf_base64_digit_value(data[i++]);
        int c = gltf_base64_digit_value(data[i++]);
        int d = gltf_base64_digit_value(data[i++]);
        if (a < 0 || b < 0 || c == -1 || d == -1) {
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

/// @brief Parse a `data:[<mediatype>][;base64],<data>` URI inline-embedded in glTF JSON.
///
/// Recognises the base64 marker and strips the MIME-type prefix.
/// Used for inline texture / buffer data so a single .gltf file
/// can be self-contained without separate .bin sidecars.
/// @return 0 on success (writes decoded bytes + MIME type), -1 on malformed URI.
static int gltf_parse_data_uri(
    const char *uri, char *mime_buf, size_t mime_buf_cap, uint8_t **out_data, size_t *out_len) {
    const char *comma;
    const char *payload;
    size_t mime_len = 0;
    int is_base64 = 0;
    if (!uri || strncmp(uri, "data:", 5) != 0)
        return 0;
    comma = strchr(uri, ',');
    if (!comma)
        return 0;
    payload = comma + 1;

    if (mime_buf && mime_buf_cap > 0)
        mime_buf[0] = '\0';
    {
        const char *meta = uri + 5;
        const char *semi = memchr(meta, ';', (size_t)(comma - meta));
        if (semi) {
            mime_len = (size_t)(semi - meta);
            if (strstr(semi, ";base64"))
                is_base64 = 1;
        } else {
            mime_len = (size_t)(comma - meta);
        }
        if (mime_buf && mime_buf_cap > 0 && mime_len > 0) {
            if (mime_len >= mime_buf_cap)
                mime_len = mime_buf_cap - 1;
            memcpy(mime_buf, meta, mime_len);
            mime_buf[mime_len] = '\0';
        }
    }

    if (is_base64) {
        *out_data = gltf_base64_decode(payload, strlen(payload), out_len);
        return *out_data != NULL;
    }

    *out_len = strlen(payload);
    *out_data = (uint8_t *)malloc(*out_len > 0 ? *out_len : 1);
    if (!*out_data)
        return 0;
    if (*out_len > 0)
        memcpy(*out_data, payload, *out_len);
    return 1;
}

/// @brief Resolve a buffer-view index to its `(data, length)` slice into the underlying buffer.
///
/// glTF separates buffers (raw bytes) from bufferViews (named
/// offset+length slices). Most accessors reference data through a
/// bufferView, so this helper centralises the indirection.
static const uint8_t *gltf_get_buffer_view_data(
    void *root, int64_t view_idx, gltf_buffer_t *buffers, int buf_count, size_t *out_len) {
    void *views = jarr(root, "bufferViews");
    if (!views || view_idx < 0 || view_idx >= jarr_len(views))
        return NULL;
    void *view = rt_seq_get(views, view_idx);
    if (!view)
        return NULL;
    int buf_idx = (int)jint(view, "buffer", -1);
    size_t byte_offset = (size_t)jint(view, "byteOffset", 0);
    size_t byte_length = (size_t)jint(view, "byteLength", 0);
    if (buf_idx < 0 || buf_idx >= buf_count)
        return NULL;
    if (byte_offset + byte_length > buffers[buf_idx].len)
        return NULL;
    if (out_len)
        *out_len = byte_length;
    return buffers[buf_idx].data + byte_offset;
}

static int gltf_component_size(int comp_type) {
    switch (comp_type) {
        case 5120:
        case 5121:
            return 1;
        case 5122:
        case 5123:
            return 2;
        case 5125:
        case 5126:
            return 4;
        default:
            return 0;
    }
}

static int gltf_component_count(const char *acc_type) {
    if (!acc_type)
        return 1;
    if (strcmp(acc_type, "VEC2") == 0)
        return 2;
    if (strcmp(acc_type, "VEC3") == 0)
        return 3;
    if (strcmp(acc_type, "VEC4") == 0)
        return 4;
    if (strcmp(acc_type, "MAT4") == 0)
        return 16;
    return 1;
}

/// @brief Resolve a glTF accessor into a typed byte view.
static int gltf_get_accessor_view(void *root,
                                  int64_t accessor_idx,
                                  gltf_buffer_t *buffers,
                                  int buf_count,
                                  gltf_accessor_view_t *out) {
    void *accessors = jarr(root, "accessors");
    size_t required_len;
    int comp_size;
    int comp_count;
    const char *acc_type;
    if (!accessors || accessor_idx < 0 || accessor_idx >= jarr_len(accessors))
        return 0;
    void *acc = rt_seq_get(accessors, accessor_idx);
    if (!acc)
        return 0;

    memset(out, 0, sizeof(*out));
    out->count = (int32_t)jint(acc, "count", 0);
    int bv_idx = (int)jint(acc, "bufferView", -1);
    int byte_offset_acc = (int)jint(acc, "byteOffset", 0);
    int comp_type = (int)jint(acc, "componentType", 5126); // 5126=FLOAT

    void *views = jarr(root, "bufferViews");
    if (!views || bv_idx < 0 || bv_idx >= (int)jarr_len(views))
        return 0;
    void *bv = rt_seq_get(views, (int64_t)bv_idx);
    if (!bv)
        return 0;

    int buf_idx = (int)jint(bv, "buffer", 0);
    int byte_offset_bv = (int)jint(bv, "byteOffset", 0);
    int byte_stride = (int)jint(bv, "byteStride", 0);

    if (buf_idx < 0 || buf_idx >= buf_count)
        return 0;

    comp_size = gltf_component_size(comp_type);
    acc_type = jstr(acc, "type");
    comp_count = gltf_component_count(acc_type);
    if (comp_size <= 0 || comp_count <= 0)
        return 0;

    out->stride = byte_stride > 0 ? byte_stride : comp_size * comp_count;
    out->comp_type = comp_type;
    out->comp_count = comp_count;
    out->normalized = jint(acc, "normalized", 0) ? 1 : 0;

    size_t offset = (size_t)byte_offset_bv + (size_t)byte_offset_acc;
    if (offset > buffers[buf_idx].len)
        return 0;
    if (out->count <= 0)
        return 0;
    required_len = offset + (size_t)(out->count - 1) * (size_t)out->stride +
                   (size_t)comp_size * (size_t)comp_count;
    if (required_len > buffers[buf_idx].len)
        return 0;

    out->data = buffers[buf_idx].data + offset;
    return 1;
}

static float gltf_decode_component_f32(const uint8_t *src, int comp_type, int normalized) {
    switch (comp_type) {
        case 5120: {
            int8_t value = 0;
            memcpy(&value, src, sizeof(value));
            if (!normalized)
                return (float)value;
            if (value == INT8_MIN)
                return -1.0f;
            return (float)value / 127.0f;
        }
        case 5121: {
            uint8_t value = 0;
            memcpy(&value, src, sizeof(value));
            return normalized ? (float)value / 255.0f : (float)value;
        }
        case 5122: {
            int16_t value = 0;
            memcpy(&value, src, sizeof(value));
            if (!normalized)
                return (float)value;
            if (value == INT16_MIN)
                return -1.0f;
            return (float)value / 32767.0f;
        }
        case 5123: {
            uint16_t value = 0;
            memcpy(&value, src, sizeof(value));
            return normalized ? (float)value / 65535.0f : (float)value;
        }
        case 5125: {
            uint32_t value = 0;
            memcpy(&value, src, sizeof(value));
            return normalized ? (float)((double)value / 4294967295.0) : (float)value;
        }
        case 5126: {
            float value = 0.0f;
            memcpy(&value, src, sizeof(value));
            return value;
        }
        default:
            return 0.0f;
    }
}

static uint32_t gltf_decode_component_u32(const uint8_t *src, int comp_type) {
    switch (comp_type) {
        case 5120: {
            int8_t value = 0;
            memcpy(&value, src, sizeof(value));
            return value < 0 ? 0u : (uint32_t)value;
        }
        case 5121: {
            uint8_t value = 0;
            memcpy(&value, src, sizeof(value));
            return (uint32_t)value;
        }
        case 5122: {
            int16_t value = 0;
            memcpy(&value, src, sizeof(value));
            return value < 0 ? 0u : (uint32_t)value;
        }
        case 5123: {
            uint16_t value = 0;
            memcpy(&value, src, sizeof(value));
            return (uint32_t)value;
        }
        case 5125: {
            uint32_t value = 0;
            memcpy(&value, src, sizeof(value));
            return value;
        }
        case 5126: {
            float value = 0.0f;
            memcpy(&value, src, sizeof(value));
            return value < 0.0f ? 0u : (uint32_t)value;
        }
        default:
            return 0u;
    }
}

static void gltf_accessor_read_f32(const gltf_accessor_view_t *view,
                                   int32_t element_idx,
                                   float *out,
                                   int32_t out_components) {
    if (!out || out_components <= 0) {
        return;
    }
    for (int32_t i = 0; i < out_components; i++)
        out[i] = 0.0f;
    if (!view || !view->data || element_idx < 0 || element_idx >= view->count)
        return;
    int comp_size = gltf_component_size(view->comp_type);
    if (comp_size <= 0)
        return;
    const uint8_t *base = view->data + (size_t)element_idx * (size_t)view->stride;
    int32_t limit = view->comp_count < out_components ? view->comp_count : out_components;
    for (int32_t i = 0; i < limit; i++)
        out[i] = gltf_decode_component_f32(base + (size_t)i * (size_t)comp_size,
                                           view->comp_type,
                                           view->normalized);
}

static void gltf_accessor_read_u32(const gltf_accessor_view_t *view,
                                   int32_t element_idx,
                                   uint32_t *out,
                                   int32_t out_components) {
    if (!out || out_components <= 0) {
        return;
    }
    for (int32_t i = 0; i < out_components; i++)
        out[i] = 0u;
    if (!view || !view->data || element_idx < 0 || element_idx >= view->count)
        return;
    int comp_size = gltf_component_size(view->comp_type);
    if (comp_size <= 0)
        return;
    const uint8_t *base = view->data + (size_t)element_idx * (size_t)view->stride;
    int32_t limit = view->comp_count < out_components ? view->comp_count : out_components;
    for (int32_t i = 0; i < limit; i++)
        out[i] = gltf_decode_component_u32(base + (size_t)i * (size_t)comp_size, view->comp_type);
}

static void gltf_emit_triangle(void *mesh, uint32_t i0, uint32_t i1, uint32_t i2) {
    if (i0 == i1 || i1 == i2 || i0 == i2)
        return;
    rt_mesh3d_add_triangle(mesh, (int64_t)i0, (int64_t)i1, (int64_t)i2);
}

static uint32_t gltf_read_index(const gltf_accessor_view_t *view, int32_t element_idx) {
    if (!view)
        return (uint32_t)element_idx;
    uint32_t value = 0;
    gltf_accessor_read_u32(view, element_idx, &value, 1);
    return value;
}

static int gltf_append_primitive_indices(void *mesh,
                                         int64_t mode,
                                         const gltf_accessor_view_t *index_view,
                                         int32_t vertex_count) {
    int32_t count = index_view ? index_view->count : vertex_count;
    if (count <= 0)
        return 0;
    if (mode == 4) {
        for (int32_t i = 0; i + 2 < count; i += 3) {
            gltf_emit_triangle(
                mesh, gltf_read_index(index_view, i), gltf_read_index(index_view, i + 1), gltf_read_index(index_view, i + 2));
        }
        return 1;
    }
    if (mode == 5) {
        for (int32_t i = 0; i + 2 < count; i++) {
            uint32_t i0 = gltf_read_index(index_view, i + 0);
            uint32_t i1 = gltf_read_index(index_view, i + 1);
            uint32_t i2 = gltf_read_index(index_view, i + 2);
            if ((i & 1) != 0) {
                uint32_t tmp = i0;
                i0 = i1;
                i1 = tmp;
            }
            gltf_emit_triangle(mesh, i0, i1, i2);
        }
        return 1;
    }
    if (mode == 6) {
        uint32_t base = gltf_read_index(index_view, 0);
        for (int32_t i = 1; i + 1 < count; i++) {
            gltf_emit_triangle(
                mesh, base, gltf_read_index(index_view, i), gltf_read_index(index_view, i + 1));
        }
        return 1;
    }
    return 0;
}

/// @brief Refcount-aware free of `*slot`; nulls the pointer afterwards.
static void gltf_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Drop a transient GC reference taken during the load (e.g. parsed JSON map).
static void gltf_release_local(void *obj) {
    void *tmp = obj;
    gltf_release_ref(&tmp);
}

static const char *gltf_target_name(void *mesh_json,
                                    int32_t target_index,
                                    char *fallback,
                                    size_t fallback_cap) {
    void *extras;
    void *names;
    rt_string name;
    const char *cstr;
    if (fallback && fallback_cap > 0) {
        snprintf(fallback, fallback_cap, "target_%d", (int)target_index);
        fallback[fallback_cap - 1] = '\0';
    }
    extras = jget(mesh_json, "extras");
    names = extras ? jarr(extras, "targetNames") : NULL;
    if (!names || target_index < 0 || target_index >= jarr_len(names))
        return fallback;
    name = (rt_string)rt_seq_get(names, target_index);
    cstr = name ? rt_string_cstr(name) : NULL;
    return cstr && cstr[0] != '\0' ? cstr : fallback;
}

static void gltf_import_primitive_morph_targets(void *root,
                                                gltf_buffer_t *buffers,
                                                int buf_count,
                                                void *mesh_json,
                                                void *prim,
                                                void *mesh_obj,
                                                int32_t vertex_count) {
    void *targets;
    void *mesh_weights;
    void *morph = NULL;
    int64_t target_count;
    if (!root || !prim || !mesh_obj || vertex_count <= 0)
        return;
    targets = jarr(prim, "targets");
    target_count = jarr_len(targets);
    if (!targets || target_count <= 0)
        return;

    morph = rt_morphtarget3d_new(vertex_count);
    if (!morph)
        return;
    mesh_weights = mesh_json ? jarr(mesh_json, "weights") : NULL;

    for (int64_t ti = 0; ti < target_count; ti++) {
        void *target = rt_seq_get(targets, ti);
        int64_t pos_acc = jint(target, "POSITION", -1);
        int64_t norm_acc = jint(target, "NORMAL", -1);
        gltf_accessor_view_t pos_view;
        gltf_accessor_view_t norm_view;
        int has_pos = gltf_get_accessor_view(root, pos_acc, buffers, buf_count, &pos_view);
        int has_norm = gltf_get_accessor_view(root, norm_acc, buffers, buf_count, &norm_view);
        char fallback_name[64];
        const char *name;
        int64_t shape;
        if (!has_pos && !has_norm)
            continue;

        name = gltf_target_name(mesh_json, (int32_t)ti, fallback_name, sizeof(fallback_name));
        shape = rt_morphtarget3d_add_shape(morph, rt_const_cstr(name));
        if (shape < 0)
            continue;

        if (mesh_weights && ti < jarr_len(mesh_weights))
            rt_morphtarget3d_set_weight(
                morph, shape, jvalue_num(rt_seq_get(mesh_weights, ti), 0.0));

        if (has_pos) {
            int32_t limit = pos_view.count < vertex_count ? pos_view.count : vertex_count;
            for (int32_t vi = 0; vi < limit; vi++) {
                float delta[3] = {0.0f, 0.0f, 0.0f};
                gltf_accessor_read_f32(&pos_view, vi, delta, 3);
                rt_morphtarget3d_set_delta(morph, shape, vi, delta[0], delta[1], delta[2]);
            }
        }
        if (has_norm) {
            int32_t limit = norm_view.count < vertex_count ? norm_view.count : vertex_count;
            for (int32_t vi = 0; vi < limit; vi++) {
                float delta[3] = {0.0f, 0.0f, 0.0f};
                gltf_accessor_read_f32(&norm_view, vi, delta, 3);
                rt_morphtarget3d_set_normal_delta(morph, shape, vi, delta[0], delta[1], delta[2]);
            }
        }
    }

    if (rt_morphtarget3d_get_shape_count(morph) > 0)
        rt_mesh3d_set_morph_targets(mesh_obj, morph);
    gltf_release_local(morph);
}

static void gltf_apply_node_morph_weights(void *mesh_obj, void *weights_arr) {
    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    int64_t shape_count;
    int64_t limit;
    if (!mesh || !mesh->morph_targets_ref || !weights_arr)
        return;
    shape_count = rt_morphtarget3d_get_shape_count(mesh->morph_targets_ref);
    limit = jarr_len(weights_arr);
    if (limit > shape_count)
        limit = shape_count;
    for (int64_t i = 0; i < limit; i++)
        rt_morphtarget3d_set_weight(
            mesh->morph_targets_ref, i, jvalue_num(rt_seq_get(weights_arr, i), 0.0));
}

/// @brief Combine a glTF document's directory with a relative URI to get an absolute filesystem path.
///
/// External buffers and textures in glTF are referenced via paths
/// relative to the .gltf file itself; this prepends the document's
/// directory so the loader can open them.
static void gltf_resolve_relative_path(const char *base_path,
                                       const char *uri,
                                       char *out,
                                       size_t out_cap) {
    const char *last_sep;
    const char *last_bsep;
    size_t dir_len;
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!uri)
        return;
    last_sep = strrchr(base_path ? base_path : "", '/');
    last_bsep = strrchr(base_path ? base_path : "", '\\');
    if (last_bsep > last_sep)
        last_sep = last_bsep;
    if (last_sep) {
        dir_len = (size_t)(last_sep - base_path + 1);
        if (dir_len >= out_cap)
            dir_len = out_cap - 1;
        memcpy(out, base_path, dir_len);
        out[dir_len] = '\0';
        strncat(out, uri, out_cap - dir_len - 1);
    } else {
        strncpy(out, uri, out_cap - 1);
        out[out_cap - 1] = '\0';
    }
}

/// @brief Synthesize a name for an embedded resource (e.g. `inline-image-3.png`).
/// Used by the asset system to give meaningful names to inline base64 buffers.
static void gltf_build_embedded_name(const char *mime_type,
                                     const char *fallback_ext,
                                     char *out,
                                     size_t out_cap) {
    const char *ext = fallback_ext ? fallback_ext : ".bin";
    if (!out || out_cap == 0)
        return;
    if (mime_type) {
        if (strstr(mime_type, "png"))
            ext = ".png";
        else if (strstr(mime_type, "jpeg") || strstr(mime_type, "jpg"))
            ext = ".jpg";
        else if (strstr(mime_type, "bmp"))
            ext = ".bmp";
        else if (strstr(mime_type, "gif"))
            ext = ".gif";
    }
    snprintf(out, out_cap, "embedded%s", ext);
}

/// @brief Reverse-lookup: given a glTF node index, return the local joint slot within
///        this skin (or -1 if the node isn't part of it). Linear scan is fine because
///        typical skins have fewer than ~64 joints.
static int gltf_skin_find_joint(const gltf_skin_t *skin, int32_t node_idx) {
    if (!skin)
        return -1;
    for (int32_t i = 0; i < skin->joint_count; i++)
        if (skin->joint_nodes[i] == node_idx)
            return i;
    return -1;
}

/// @brief Depth-first registration of a glTF joint into the engine's skeleton.
/// @details Converts one joint (plus all of its joint-space descendants) into engine
///          bones, preserving the parent/child hierarchy. Each node's local TRS is
///          folded into a bind matrix via `gltf_node_local_matrix` and registered with
///          `rt_skeleton3d_add_bone`.
///
///          `skin->joint_to_bone[joint_local]` doubles as a cycle-break tri-state:
///            -1 = not visited, -2 = currently on the recursion stack, >=0 = bone idx.
///          A self-referential or cyclic joint graph (malformed glTF) will hit the -2
///          guard and bail instead of overflowing the stack. Unresolved joints stay at
///          -1 so callers can retry them as roots on a second pass.
///
///          Children are discovered through the JSON "children" array of the node, not
///          through a prebuilt parent table — this lets one joint be skipped (e.g. when
///          its own parent-joint lookup failed) and still be picked up later as a root.
/// @param skin Skin context; its `joint_to_bone` array is mutated in place.
/// @param nodes_arr JSON "nodes" array from the glTF root (used for traversal).
/// @param joint_local Zero-based index into `skin->joint_nodes`.
/// @param parent_bone Engine bone index of the parent, or -1 to make a root bone.
/// @return The newly added bone index, or -1 on failure / cycle.
static int64_t gltf_add_skin_joint_recursive(gltf_skin_t *skin,
                                             void *nodes_arr,
                                             int32_t joint_local,
                                             int64_t parent_bone) {
    int32_t node_idx;
    void *node_json;
    const char *name;
    char generated_name[64];
    double local[16];
    void *bind_mat;
    int64_t bone_idx;
    void *children;
    if (!skin || !skin->skeleton || !nodes_arr || joint_local < 0 ||
        joint_local >= skin->joint_count)
        return -1;
    if (skin->joint_to_bone[joint_local] >= 0)
        return skin->joint_to_bone[joint_local];
    if (skin->joint_to_bone[joint_local] == -2)
        return -1;
    skin->joint_to_bone[joint_local] = -2;
    node_idx = skin->joint_nodes[joint_local];
    if (!gltf_node_local_matrix(nodes_arr, node_idx, local)) {
        skin->joint_to_bone[joint_local] = -1;
        return -1;
    }
    node_json = rt_seq_get(nodes_arr, node_idx);
    name = jstr(node_json, "name");
    if (!name || name[0] == '\0') {
        snprintf(generated_name, sizeof(generated_name), "joint_%d", (int)joint_local);
        name = generated_name;
    }
    bind_mat = rt_mat4_new(local[0],
                           local[1],
                           local[2],
                           local[3],
                           local[4],
                           local[5],
                           local[6],
                           local[7],
                           local[8],
                           local[9],
                           local[10],
                           local[11],
                           local[12],
                           local[13],
                           local[14],
                           local[15]);
    bone_idx = rt_skeleton3d_add_bone(skin->skeleton, rt_const_cstr(name), parent_bone, bind_mat);
    gltf_release_ref(&bind_mat);
    if (bone_idx < 0) {
        skin->joint_to_bone[joint_local] = -1;
        return -1;
    }
    skin->joint_to_bone[joint_local] = (int32_t)bone_idx;

    children = jarr(node_json, "children");
    for (int64_t ci = 0; ci < jarr_len(children); ci++) {
        int32_t child_node = (int32_t)jvalue_int(rt_seq_get(children, ci), -1);
        int32_t child_joint = gltf_skin_find_joint(skin, child_node);
        if (child_joint >= 0)
            gltf_add_skin_joint_recursive(skin, nodes_arr, child_joint, bone_idx);
    }
    return bone_idx;
}

/// @brief Release the per-skin scratch buffers held in an array produced by
///        `gltf_parse_skins`. Does NOT release the skeleton object itself — that is
///        owned by `asset->skeletons` and lives beyond the parse step.
static void gltf_free_skins(gltf_skin_t *skins, int32_t skin_count) {
    if (!skins)
        return;
    for (int32_t i = 0; i < skin_count; i++) {
        free(skins[i].joint_nodes);
        free(skins[i].joint_to_bone);
    }
    free(skins);
}

/// @brief Parse every skin in a glTF document into engine `Skeleton3D` objects.
/// @details Walks the top-level "skins" array and, for each skin, builds a Skeleton3D
///          whose bones mirror the skin's joint hierarchy. The pipeline per skin is:
///            1. Build a `node_parents[]` lookup table (single O(N) pass over nodes)
///               so we can identify root joints (parent-of-joint not in same skin).
///            2. For each skin, allocate `joint_nodes` / `joint_to_bone` scratch.
///            3. First pass: register only root joints (parent outside the skin) so
///               they become root bones.
///            4. Second pass: any joint still unresolved (disconnected subtree, joint
///               order shenanigans) is added as its own root.
///            5. `rt_skeleton3d_compute_inverse_bind` computes the fallback inverse
///               bind matrices from local TRS, then if the skin provides
///               "inverseBindMatrices" the values are read and transposed from
///               glTF column-major into the engine's row-major layout.
///
///          On allocation failure the partial state is rolled back (node_parents freed,
///          skins freed, asset->skeletons cleared) so the asset is never left in a
///          half-built state.
///
///          Output `out_skins` is optional; when NULL the caller indicates they only
///          want the skeletons populated on `asset` and the helper table is freed.
/// @param asset Target asset — `skeletons` is allocated and populated.
/// @param root glTF JSON root object.
/// @param buffers Parsed buffer table (indexed by bufferView.buffer).
/// @param buf_count Number of entries in `buffers`.
/// @param out_skins Optional; receives a per-skin scratch array (caller frees via
///                  `gltf_free_skins`). Needed later to map vertex bone indices.
/// @param out_skin_count Optional; receives the count that was written.
static void gltf_parse_skins(rt_gltf_asset *asset,
                             void *root,
                             gltf_buffer_t *buffers,
                             int buf_count,
                             gltf_skin_t **out_skins,
                             int32_t *out_skin_count) {
    void *skins_arr = jarr(root, "skins");
    void *nodes_arr = jarr(root, "nodes");
    int32_t skin_count = (int32_t)jarr_len(skins_arr);
    int32_t node_count = (int32_t)jarr_len(nodes_arr);
    int32_t *node_parents = NULL;
    gltf_skin_t *skins = NULL;
    if (out_skins)
        *out_skins = NULL;
    if (out_skin_count)
        *out_skin_count = 0;
    if (!asset || !skins_arr || skin_count <= 0 || !nodes_arr || node_count <= 0)
        return;

    node_parents = (int32_t *)malloc((size_t)node_count * sizeof(*node_parents));
    skins = (gltf_skin_t *)calloc((size_t)skin_count, sizeof(*skins));
    asset->skeletons = (void **)calloc((size_t)skin_count, sizeof(void *));
    if (!node_parents || !skins || !asset->skeletons) {
        free(node_parents);
        gltf_free_skins(skins, skin_count);
        free(asset->skeletons);
        asset->skeletons = NULL;
        return;
    }
    for (int32_t i = 0; i < node_count; i++)
        node_parents[i] = -1;
    for (int32_t ni = 0; ni < node_count; ni++) {
        void *node_json = rt_seq_get(nodes_arr, ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int32_t child = (int32_t)jvalue_int(rt_seq_get(children, ci), -1);
            if (child >= 0 && child < node_count)
                node_parents[child] = ni;
        }
    }

    for (int32_t si = 0; si < skin_count; si++) {
        void *skin_json = rt_seq_get(skins_arr, si);
        void *joints = jarr(skin_json, "joints");
        int32_t joint_count = (int32_t)jarr_len(joints);
        if (joint_count <= 0)
            continue;
        skins[si].joint_nodes = (int32_t *)calloc((size_t)joint_count, sizeof(int32_t));
        skins[si].joint_to_bone = (int32_t *)malloc((size_t)joint_count * sizeof(int32_t));
        skins[si].joint_count = joint_count;
        skins[si].skeleton = rt_skeleton3d_new();
        if (!skins[si].joint_nodes || !skins[si].joint_to_bone || !skins[si].skeleton) {
            if (skins[si].skeleton)
                gltf_release_ref(&skins[si].skeleton);
            free(skins[si].joint_nodes);
            free(skins[si].joint_to_bone);
            skins[si].joint_nodes = NULL;
            skins[si].joint_to_bone = NULL;
            skins[si].joint_count = 0;
            continue;
        }
        for (int32_t ji = 0; ji < joint_count; ji++) {
            skins[si].joint_nodes[ji] = (int32_t)jvalue_int(rt_seq_get(joints, ji), -1);
            skins[si].joint_to_bone[ji] = -1;
        }
        for (int32_t ji = 0; ji < joint_count; ji++) {
            int32_t parent_node = skins[si].joint_nodes[ji] >= 0 &&
                                          skins[si].joint_nodes[ji] < node_count
                                      ? node_parents[skins[si].joint_nodes[ji]]
                                      : -1;
            if (gltf_skin_find_joint(&skins[si], parent_node) < 0)
                gltf_add_skin_joint_recursive(&skins[si], nodes_arr, ji, -1);
        }
        for (int32_t ji = 0; ji < joint_count; ji++) {
            if (skins[si].joint_to_bone[ji] < 0)
                gltf_add_skin_joint_recursive(&skins[si], nodes_arr, ji, -1);
        }
        rt_skeleton3d_compute_inverse_bind(skins[si].skeleton);
        {
            int64_t ibm_acc = jint(skin_json, "inverseBindMatrices", -1);
            gltf_accessor_view_t ibm_view;
            if (gltf_get_accessor_view(root, ibm_acc, buffers, buf_count, &ibm_view)) {
                rt_skeleton3d *skel = (rt_skeleton3d *)skins[si].skeleton;
                int32_t limit = joint_count < ibm_view.count ? joint_count : ibm_view.count;
                for (int32_t ji = 0; ji < limit; ji++) {
                    int32_t bone = skins[si].joint_to_bone[ji];
                    float col_major[16];
                    if (bone < 0 || bone >= skel->bone_count)
                        continue;
                    gltf_accessor_read_f32(&ibm_view, ji, col_major, 16);
                    for (int row = 0; row < 4; row++)
                        for (int col = 0; col < 4; col++)
                            skel->bones[bone].inverse_bind[row * 4 + col] =
                                col_major[col * 4 + row];
                }
            }
        }
        asset->skeletons[asset->skeleton_count++] = skins[si].skeleton;
    }
    free(node_parents);
    if (out_skins)
        *out_skins = skins;
    else
        gltf_free_skins(skins, skin_count);
    if (out_skin_count)
        *out_skin_count = skin_count;
}

/// @brief Retarget a mesh's raw glTF joint indices onto the skin's engine bone indices.
/// @details After vertex data is loaded, each vertex carries its original glTF joint
///          indices (`bone_indices[0..3]`) and four weights. This function:
///            - Translates each joint through `skin->joint_to_bone[]` to the matching
///              engine bone index, dropping influences where the mapping is invalid or
///              the weight is zero.
///            - Renormalizes the surviving weights so they sum to 1 (glTF assets are
///              meant to ship normalized but some exporters lie; one influence being
///              dropped by the mapping would otherwise skew the result).
///            - Writes the fixed-up (bone, weight) pairs back through
///              `rt_mesh3d_set_bone_weights`.
///          Tracks `max_bone` so the mesh's advertised bone count matches what the
///          vertex data actually references — this is what the skinning shader uses to
///          size the palette upload.
/// @param mesh_obj Target mesh (mutated in place).
/// @param skin Skin whose `joint_to_bone` table drives the remap.
static void gltf_apply_skin_to_mesh(void *mesh_obj, const gltf_skin_t *skin) {
    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    int32_t max_bone = 0;
    if (!mesh || !skin || !skin->joint_to_bone)
        return;
    for (uint32_t vi = 0; vi < mesh->vertex_count; vi++) {
        vgfx3d_vertex_t *v = &mesh->vertices[vi];
        int64_t bones[4] = {0, 0, 0, 0};
        double weights[4] = {0.0, 0.0, 0.0, 0.0};
        double sum = 0.0;
        for (int i = 0; i < 4; i++) {
            uint8_t joint = v->bone_indices[i];
            if (joint < skin->joint_count && skin->joint_to_bone[joint] >= 0 &&
                v->bone_weights[i] > 0.0f) {
                bones[i] = skin->joint_to_bone[joint];
                weights[i] = v->bone_weights[i];
                sum += weights[i];
            }
        }
        if (sum <= 0.0)
            continue;
        for (int i = 0; i < 4; i++) {
            weights[i] /= sum;
            if (weights[i] > 0.0 && bones[i] + 1 > max_bone)
                max_bone = (int32_t)(bones[i] + 1);
        }
        rt_mesh3d_set_bone_weights(mesh,
                                   vi,
                                   bones[0],
                                   weights[0],
                                   bones[1],
                                   weights[1],
                                   bones[2],
                                   weights[2],
                                   bones[3],
                                   weights[3]);
    }
    mesh->bone_count = max_bone;
}

typedef struct {
    int8_t valid;
    int32_t path; /* 0=translation, 1=rotation, 2=scale */
    int32_t node_idx;
    int32_t bone_idx;
    const char *interpolation;
    gltf_accessor_view_t input;
    gltf_accessor_view_t output;
} gltf_anim_curve_t;

static int32_t gltf_find_bone_for_node(const gltf_skin_t *skins,
                                       int32_t skin_count,
                                       int32_t node_idx) {
    for (int32_t si = 0; si < skin_count; si++) {
        int32_t joint = gltf_skin_find_joint(&skins[si], node_idx);
        if (joint >= 0 && skins[si].joint_to_bone && skins[si].joint_to_bone[joint] >= 0)
            return skins[si].joint_to_bone[joint];
    }
    return -1;
}

static double gltf_curve_time(const gltf_accessor_view_t *view, int32_t index) {
    float t = 0.0f;
    gltf_accessor_read_f32(view, index, &t, 1);
    return (double)t;
}

/// @brief Insert `value` into a sorted-unique time list, growing the backing buffer as
///        needed. Ignores values within 1e-6 of an existing entry (de-dup tolerance
///        mirrors typical DCC exporter precision). The buffer doubles on growth
///        starting at 16 entries so amortized cost is O(N log N) total.
/// @return 1 on success, 0 on allocation failure (caller must free `*times` later).
static int gltf_anim_insert_time(double **times,
                                 int32_t *count,
                                 int32_t *capacity,
                                 double value) {
    int32_t pos = 0;
    while (pos < *count && (*times)[pos] < value - 1e-6)
        pos++;
    if (pos < *count && fabs((*times)[pos] - value) <= 1e-6)
        return 1;
    if (*count >= *capacity) {
        int32_t new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        double *grown = (double *)realloc(*times, (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        *times = grown;
        *capacity = new_capacity;
    }
    memmove(&(*times)[pos + 1], &(*times)[pos], (size_t)(*count - pos) * sizeof(**times));
    (*times)[pos] = value;
    (*count)++;
    return 1;
}

/// @brief Convert a keyframe index into the real output-accessor offset for the curve.
/// @details glTF CUBICSPLINE samplers store three floats-per-component per keyframe in
///          the order [in-tangent, value, out-tangent]. So for N keyframes the output
///          accessor holds 3N entries and the actual "value" element we want is
///          `key_index * 3 + 1`. STEP and LINEAR samplers store one entry per key and
///          map identity.
static int32_t gltf_curve_output_index(const gltf_anim_curve_t *curve, int32_t key_index) {
    if (curve && curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0)
        return key_index * 3 + 1;
    return key_index;
}

static void gltf_curve_read_output_value(const gltf_anim_curve_t *curve,
                                         int32_t output_index,
                                         float *out,
                                         int32_t components) {
    gltf_accessor_read_f32(&curve->output, output_index, out, components);
}

/// @brief Read `components` floats for one logical keyframe from the curve's output
///        accessor, handling the CUBICSPLINE stride quirk transparently.
static void gltf_curve_read_value(const gltf_anim_curve_t *curve,
                                  int32_t key_index,
                                  float *out,
                                  int32_t components) {
    gltf_curve_read_output_value(curve, gltf_curve_output_index(curve, key_index), out, components);
}

static void gltf_normalize_sample_if_quat(float *out, int32_t components) {
    if (components == 4) {
        float len =
            sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (len > 1e-6f) {
            for (int c = 0; c < 4; c++)
                out[c] /= len;
        }
    }
}

/// @brief Sample an animation curve at an arbitrary time, respecting its interpolation.
/// @details Handles clamping (before first / after last key), STEP interpolation (hold
///          previous value), LINEAR interpolation, and glTF CUBICSPLINE Hermite sampling
///          using the exported in/out tangents.
///
///          When sampling a rotation curve (`components == 4`) the blended quaternion
///          is renormalized; linear quaternion blends otherwise drift below unit length
///          on long playback and produce subtle scale creep.
///
///          The key-count is clamped to the smaller of input.count and output.count
///          (or output.count/3 for CUBICSPLINE) so truncated/corrupt asset data can't
///          run us past the accessor view.
/// @param curve Source curve (must be marked valid; no-op otherwise).
/// @param time Sample time in seconds.
/// @param out Destination buffer of `components` floats.
/// @param components 3 for translation/scale, 4 for rotation quaternions.
static void gltf_sample_curve(const gltf_anim_curve_t *curve,
                              double time,
                              float *out,
                              int32_t components) {
    int32_t key_count;
    if (!curve || !curve->valid || !out || components <= 0 || curve->input.count <= 0)
        return;
    key_count = curve->input.count;
    if (curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0 &&
        curve->output.count < key_count * 3)
        key_count = curve->output.count / 3;
    else if (curve->output.count < key_count)
        key_count = curve->output.count;
    if (key_count <= 0)
        return;
    if (time <= gltf_curve_time(&curve->input, 0)) {
        gltf_curve_read_value(curve, 0, out, components);
        gltf_normalize_sample_if_quat(out, components);
        return;
    }
    for (int32_t i = 1; i < key_count; i++) {
        double t0 = gltf_curve_time(&curve->input, i - 1);
        double t1 = gltf_curve_time(&curve->input, i);
        if (time <= t1 + 1e-6) {
            float a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            float b[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            double alpha = t1 > t0 ? (time - t0) / (t1 - t0) : 0.0;
            if (alpha < 0.0)
                alpha = 0.0;
            if (alpha > 1.0)
                alpha = 1.0;
            gltf_curve_read_value(curve, i - 1, a, components);
            if (curve->interpolation && strcmp(curve->interpolation, "STEP") == 0) {
                memcpy(out, a, (size_t)components * sizeof(float));
                return;
            }
            if (curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0) {
                float out_tangent0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float in_tangent1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                double dt = t1 - t0;
                double t2 = alpha * alpha;
                double t3 = t2 * alpha;
                double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
                double h10 = t3 - 2.0 * t2 + alpha;
                double h01 = -2.0 * t3 + 3.0 * t2;
                double h11 = t3 - t2;
                gltf_curve_read_output_value(curve, (i - 1) * 3 + 2, out_tangent0, components);
                gltf_curve_read_output_value(curve, i * 3, in_tangent1, components);
                gltf_curve_read_value(curve, i, b, components);
                for (int c = 0; c < components; c++) {
                    out[c] = (float)(h00 * a[c] + h10 * dt * out_tangent0[c] +
                                     h01 * b[c] + h11 * dt * in_tangent1[c]);
                }
                gltf_normalize_sample_if_quat(out, components);
                return;
            }
            gltf_curve_read_value(curve, i, b, components);
            for (int c = 0; c < components; c++)
                out[c] = (float)(a[c] + (b[c] - a[c]) * alpha);
            gltf_normalize_sample_if_quat(out, components);
            return;
        }
    }
    gltf_curve_read_value(curve, key_count - 1, out, components);
    gltf_normalize_sample_if_quat(out, components);
}

/// @brief Parse the glTF "animations" array into engine `Animation3D` objects.
/// @details glTF animation data is channel-oriented: each channel pairs a sampler
///          (keyframe data) with a target (node + property path: translation /
///          rotation / scale). The engine, in contrast, is bone-oriented: one
///          Animation3D holds per-bone keyframes where each keyframe records T/R/S
///          atomically.
///
///          This conversion works in two passes per glTF animation:
///            1. Build a `curves[]` table where each entry resolves (node, path,
///               sampler data, interpolation mode). Unknown paths, missing accessors,
///               or non-skinned targets are dropped here. `duration` is accumulated
///               as the max input-time across all valid channels.
///            2. For each engine bone in [0, VGFX3D_MAX_BONES):
///                 - Collect every time stamp from every curve targeting that bone,
///                   deduplicated and sorted by `gltf_anim_insert_time`.
///                 - Seed T/R/S from the node's bind pose so bones without a channel
///                   keep their rest value.
///                 - At each time stamp sample all three curves and emit a merged
///                   keyframe. Missing curves inherit the bind-pose seed.
///                 - Release the Vec3/Quat temporaries immediately (the animation
///                   retains its own references internally).
///          Animations that produce no keyframes (e.g. all targets missed the skin)
///          are released rather than filed — a NULL-keyframe animation would just
///          waste a slot.
///
///          Animations are flagged looping by default; callers can override via
///          `rt_animation3d_set_looping` after load.
/// @param asset Receiver for the populated `animations` array.
/// @param root glTF JSON root.
/// @param buffers Parsed buffer table used by `gltf_get_accessor_view`.
/// @param buf_count Length of `buffers`.
/// @param skins Scratch skins produced by `gltf_parse_skins`, used to resolve
///              (node → bone index) without rebuilding a lookup.
/// @param skin_count Length of `skins`.
static void gltf_parse_animations(rt_gltf_asset *asset,
                                  void *root,
                                  gltf_buffer_t *buffers,
                                  int buf_count,
                                  const gltf_skin_t *skins,
                                  int32_t skin_count) {
    void *anims_arr = jarr(root, "animations");
    void *nodes_arr = jarr(root, "nodes");
    int32_t anim_count = (int32_t)jarr_len(anims_arr);
    if (!asset || !anims_arr || anim_count <= 0 || !skins || skin_count <= 0)
        return;
    asset->animations = (void **)calloc((size_t)anim_count, sizeof(void *));
    if (!asset->animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int32_t channel_count = (int32_t)jarr_len(channels);
        gltf_anim_curve_t *curves;
        double duration = 0.0;
        int emitted_any = 0;
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        void *anim;
        if (channel_count <= 0 || !samplers)
            continue;
        curves = (gltf_anim_curve_t *)calloc((size_t)channel_count, sizeof(*curves));
        if (!curves)
            continue;
        for (int32_t ci = 0; ci < channel_count; ci++) {
            void *channel = rt_seq_get(channels, ci);
            void *target = jget(channel, "target");
            const char *path = jstr(target, "path");
            int64_t sampler_idx = jint(channel, "sampler", -1);
            int64_t node_idx = jint(target, "node", -1);
            void *sampler;
            int32_t bone_idx;
            if (sampler_idx < 0 || sampler_idx >= jarr_len(samplers) || node_idx < 0)
                continue;
            bone_idx = gltf_find_bone_for_node(skins, skin_count, (int32_t)node_idx);
            if (bone_idx < 0)
                continue;
            sampler = rt_seq_get(samplers, sampler_idx);
            if (!path || strcmp(path, "translation") == 0)
                curves[ci].path = 0;
            else if (strcmp(path, "rotation") == 0)
                curves[ci].path = 1;
            else if (strcmp(path, "scale") == 0)
                curves[ci].path = 2;
            else
                continue;
            if (!gltf_get_accessor_view(root, jint(sampler, "input", -1), buffers, buf_count, &curves[ci].input) ||
                !gltf_get_accessor_view(root, jint(sampler, "output", -1), buffers, buf_count, &curves[ci].output))
                continue;
            curves[ci].valid = 1;
            curves[ci].node_idx = (int32_t)node_idx;
            curves[ci].bone_idx = bone_idx;
            curves[ci].interpolation = jstr(sampler, "interpolation");
            for (int32_t ti = 0; ti < curves[ci].input.count; ti++) {
                double t = gltf_curve_time(&curves[ci].input, ti);
                if (t > duration)
                    duration = t;
            }
        }
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "animation_%d", (int)ai);
            name = generated_name;
        }
        anim = rt_animation3d_new(rt_const_cstr(name), duration > 0.0 ? duration : 1.0);
        if (!anim) {
            free(curves);
            continue;
        }
        for (int32_t bone = 0; bone < VGFX3D_MAX_BONES; bone++) {
            double *times = NULL;
            int32_t time_count = 0;
            int32_t time_capacity = 0;
            int32_t node_idx = -1;
            const gltf_anim_curve_t *curve_t = NULL;
            const gltf_anim_curve_t *curve_r = NULL;
            const gltf_anim_curve_t *curve_s = NULL;
            double local[16];
            double pos_d[3];
            double rot_d[4];
            double scl_d[3];
            for (int32_t ci = 0; ci < channel_count; ci++) {
                if (!curves[ci].valid || curves[ci].bone_idx != bone)
                    continue;
                node_idx = curves[ci].node_idx;
                if (curves[ci].path == 0)
                    curve_t = &curves[ci];
                else if (curves[ci].path == 1)
                    curve_r = &curves[ci];
                else if (curves[ci].path == 2)
                    curve_s = &curves[ci];
                for (int32_t ti = 0; ti < curves[ci].input.count; ti++)
                    gltf_anim_insert_time(&times, &time_count, &time_capacity,
                                          gltf_curve_time(&curves[ci].input, ti));
            }
            if (time_count <= 0 || node_idx < 0) {
                free(times);
                continue;
            }
            if (!gltf_node_local_matrix(nodes_arr, node_idx, local)) {
                free(times);
                continue;
            }
            gltf_matrix_to_trs(local, pos_d, rot_d, scl_d);
            for (int32_t ti = 0; ti < time_count; ti++) {
                float pos[3] = {(float)pos_d[0], (float)pos_d[1], (float)pos_d[2]};
                float rot[4] = {(float)rot_d[0], (float)rot_d[1], (float)rot_d[2], (float)rot_d[3]};
                float scl[3] = {(float)scl_d[0], (float)scl_d[1], (float)scl_d[2]};
                void *pos_obj;
                void *rot_obj;
                void *scl_obj;
                gltf_sample_curve(curve_t, times[ti], pos, 3);
                gltf_sample_curve(curve_r, times[ti], rot, 4);
                gltf_sample_curve(curve_s, times[ti], scl, 3);
                pos_obj = rt_vec3_new(pos[0], pos[1], pos[2]);
                rot_obj = rt_quat_new(rot[0], rot[1], rot[2], rot[3]);
                scl_obj = rt_vec3_new(scl[0], scl[1], scl[2]);
                rt_animation3d_add_keyframe(anim, bone, times[ti], pos_obj, rot_obj, scl_obj);
                gltf_release_ref(&pos_obj);
                gltf_release_ref(&rot_obj);
                gltf_release_ref(&scl_obj);
                emitted_any = 1;
            }
            free(times);
        }
        free(curves);
        if (!emitted_any) {
            gltf_release_ref(&anim);
            continue;
        }
        rt_animation3d_set_looping(anim, 1);
        asset->animations[asset->animation_count++] = anim;
    }
}

//===----------------------------------------------------------------------===//
// Main loader
//===----------------------------------------------------------------------===//

/// @brief Load a glTF 2.0 asset from disk and build the engine representation.
/// @details Top-level entry point that orchestrates the entire load pipeline. The
///          high-level stages are:
///            1. Read the whole file (hard cap 256 MiB to avoid denial-of-service
///              on pathological inputs).
///            2. Detect format: the 4-byte magic `glTF` signals a binary GLB
///              container, otherwise treat the bytes as a JSON `.gltf`.
///            3. For GLB: walk the chunk list, extracting the JSON chunk and the
///              embedded BIN chunk (which substitutes for buffer index 0).
///            4. Parse buffers (data-URIs and external files), bufferViews,
///              accessors, and textures into a `gltf_buffer_t[]` scratch table.
///            5. Walk the first scene: for every mesh primitive, decode
///              POSITION / NORMAL / TEXCOORD_0 / TANGENT / JOINTS_0 / WEIGHTS_0
///              attributes through their accessor views, plus the index buffer,
///              and build a Mesh3D.
///            6. Materials: baseColorFactor + baseColorTexture resolved to
///              Material3D. Missing materials fall back to white.
///            7. If skins are present, `gltf_parse_skins` builds Skeleton3D objects
///              and `gltf_apply_skin_to_mesh` retargets the vertex bone indices
///              onto engine bones.
///            8. `gltf_parse_animations` converts glTF sampler/channel data into
///              bone-oriented Animation3D.
///            9. Release scratch buffers (decoded binary data freed; accessor
///               views point into buffers freed at end).
///
///          Failure paths: any I/O error, JSON parse error, or allocation failure
///          returns NULL. Partial state is rolled back via `gltf_release_ref` so
///          the caller never sees a half-built asset.
/// @param path File path to `.gltf` or `.glb`.
/// @return Opaque rt_gltf_asset*, or NULL on failure.
void *rt_gltf_load(rt_string path) {
    if (!path)
        return NULL;
    const char *filepath = rt_string_cstr(path);
    if (!filepath)
        return NULL;

    // Read file
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0 || fsize > 256 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    uint8_t *file_data = (uint8_t *)malloc((size_t)fsize);
    if (!file_data) {
        fclose(f);
        return NULL;
    }
    if (fread(file_data, 1, (size_t)fsize, f) != (size_t)fsize) {
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    // Detect .glb vs .gltf
    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;

    if ((size_t)fsize >= 12 && file_data[0] == 0x67 && file_data[1] == 0x6C &&
        file_data[2] == 0x54 && file_data[3] == 0x46) {
        // GLB binary container
        uint32_t version = file_data[4] | ((uint32_t)file_data[5] << 8) |
                           ((uint32_t)file_data[6] << 16) | ((uint32_t)file_data[7] << 24);
        (void)version;

        // Parse chunks
        size_t pos = 12;
        while (pos + 8 <= (size_t)fsize) {
            uint32_t chunk_len = file_data[pos] | ((uint32_t)file_data[pos + 1] << 8) |
                                 ((uint32_t)file_data[pos + 2] << 16) |
                                 ((uint32_t)file_data[pos + 3] << 24);
            uint32_t chunk_type = file_data[pos + 4] | ((uint32_t)file_data[pos + 5] << 8) |
                                  ((uint32_t)file_data[pos + 6] << 16) |
                                  ((uint32_t)file_data[pos + 7] << 24);
            pos += 8;
            if (pos + chunk_len > (size_t)fsize)
                break;

            if (chunk_type == 0x4E4F534A) {
                // JSON chunk
                json_str = (char *)malloc(chunk_len + 1);
                if (json_str) {
                    memcpy(json_str, file_data + pos, chunk_len);
                    json_str[chunk_len] = '\0';
                }
            } else if (chunk_type == 0x004E4942) {
                // BIN chunk
                bin_chunk = file_data + pos;
                bin_chunk_len = chunk_len;
            }
            pos += chunk_len;
        }
    } else {
        // Text .gltf
        json_str = (char *)malloc((size_t)fsize + 1);
        if (json_str) {
            memcpy(json_str, file_data, (size_t)fsize);
            json_str[fsize] = '\0';
        }
    }

    if (!json_str) {
        free(file_data);
        return NULL;
    }

    // Parse JSON
    rt_string json_rts = rt_const_cstr(json_str);
    void *root = rt_json_parse_object(json_rts);
    free(json_str);
    if (!root) {
        free(file_data);
        return NULL;
    }

    // Load buffers
    void *buffers_arr = jarr(root, "buffers");
    int buf_count = (int)jarr_len(buffers_arr);
    gltf_buffer_t *buffers =
        (gltf_buffer_t *)calloc((size_t)(buf_count + 1), sizeof(gltf_buffer_t));
    int *mesh_prim_start = NULL;
    int *mesh_prim_count = NULL;
    void **primitive_materials = NULL;
    gltf_skin_t *skins = NULL;
    int32_t skin_count = 0;
    int32_t *mesh_applied_skin = NULL;
    if (!buffers) {
        free(file_data);
        return NULL;
    }

    for (int i = 0; i < buf_count; i++) {
        void *buf_obj = rt_seq_get(buffers_arr, (int64_t)i);
        int64_t byte_length = jint(buf_obj, "byteLength", 0);
        const char *uri = jstr(buf_obj, "uri");

        if (i == 0 && bin_chunk && !uri) {
            // GLB: buffer 0 is the BIN chunk
            buffers[i].data = bin_chunk;
            buffers[i].len = bin_chunk_len;
        } else if (uri) {
            if (strncmp(uri, "data:", 5) == 0) {
                char mime_type[64];
                uint8_t *decoded = NULL;
                size_t decoded_len = 0;
                if (gltf_parse_data_uri(
                        uri, mime_type, sizeof(mime_type), &decoded, &decoded_len)) {
                    buffers[i].data = decoded;
                    buffers[i].len = decoded_len;
                }
            } else {
                // External file — resolve relative to .gltf directory
                char buf_path[1024];
                gltf_resolve_relative_path(filepath, uri, buf_path, sizeof(buf_path));
                FILE *bf = fopen(buf_path, "rb");
                if (bf) {
                    fseek(bf, 0, SEEK_END);
                    long blen = ftell(bf);
                    fseek(bf, 0, SEEK_SET);
                    if (blen > 0 && blen <= byte_length + 4) {
                        buffers[i].data = (uint8_t *)malloc((size_t)blen);
                        if (buffers[i].data) {
                            if (fread(buffers[i].data, 1, (size_t)blen, bf) == (size_t)blen)
                                buffers[i].len = (size_t)blen;
                        }
                    }
                    fclose(bf);
                }
            }
        }
    }

    // Create asset
    rt_gltf_asset *asset = (rt_gltf_asset *)rt_obj_new_i64(0, (int64_t)sizeof(rt_gltf_asset));
    if (!asset) {
        for (int i = 0; i < buf_count; i++) {
            if (buffers[i].data != bin_chunk)
                free(buffers[i].data);
        }
        free(mesh_prim_start);
        free(mesh_prim_count);
        free(primitive_materials);
        free(buffers);
        free(file_data);
        return NULL;
    }
    asset->vptr = NULL;
    asset->meshes = NULL;
    asset->mesh_count = 0;
    asset->materials = NULL;
    asset->material_count = 0;
    asset->skeletons = NULL;
    asset->skeleton_count = 0;
    asset->animations = NULL;
    asset->animation_count = 0;
    asset->scene_root = NULL;
    asset->node_count = 0;
    rt_obj_set_finalizer(asset, gltf_asset_finalize);

    void **images = NULL;
    void **texture_images = NULL;
    void *images_arr = jarr(root, "images");
    int image_count = (int)jarr_len(images_arr);
    if (image_count > 0)
        images = (void **)calloc((size_t)image_count, sizeof(void *));

    for (int i = 0; i < image_count && images; i++) {
        void *image_json = rt_seq_get(images_arr, (int64_t)i);
        const char *uri = jstr(image_json, "uri");
        const char *mime_type = jstr(image_json, "mimeType");
        uint8_t *owned_data = NULL;
        const uint8_t *image_data = NULL;
        size_t image_len = 0;
        char image_name[64];

        if (uri && strncmp(uri, "data:", 5) == 0) {
            char parsed_mime[64];
            if (gltf_parse_data_uri(
                    uri, parsed_mime, sizeof(parsed_mime), &owned_data, &image_len)) {
                image_data = owned_data;
                if (!mime_type && parsed_mime[0] != '\0')
                    mime_type = parsed_mime;
            }
        } else if (uri) {
            char image_path[1024];
            gltf_resolve_relative_path(filepath, uri, image_path, sizeof(image_path));
            images[i] = rt_pixels_load(rt_const_cstr(image_path));
        } else {
            int64_t view_idx = jint(image_json, "bufferView", -1);
            image_data = gltf_get_buffer_view_data(root, view_idx, buffers, buf_count, &image_len);
        }

        if (!images[i] && image_data && image_len > 0) {
            gltf_build_embedded_name(mime_type, ".bin", image_name, sizeof(image_name));
            images[i] = rt_asset_decode_typed(image_name, image_data, image_len);
        }
        free(owned_data);
    }

    void *textures_arr = jarr(root, "textures");
    int texture_count = (int)jarr_len(textures_arr);
    if (texture_count > 0)
        texture_images = (void **)calloc((size_t)texture_count, sizeof(void *));
    for (int i = 0; i < texture_count && texture_images; i++) {
        void *texture_json = rt_seq_get(textures_arr, (int64_t)i);
        int64_t source_idx = jint(texture_json, "source", -1);
        if (source_idx >= 0 && source_idx < image_count)
            texture_images[i] = images[source_idx];
    }

    // Extract materials
    void *mats_arr = jarr(root, "materials");
    int mat_count = (int)jarr_len(mats_arr);
    if (mat_count > 0) {
        asset->materials = (void **)calloc((size_t)mat_count, sizeof(void *));
        for (int i = 0; i < mat_count && asset->materials; i++) {
            void *mat_json = rt_seq_get(mats_arr, (int64_t)i);
            void *mat = NULL;

            // PBR metallic-roughness
            void *pbr = jget(mat_json, "pbrMetallicRoughness");
            if (pbr) {
                double base_r = 1.0;
                double base_g = 1.0;
                double base_b = 1.0;
                double base_a = 1.0;
                double metallic = jnum(pbr, "metallicFactor", 1.0);
                double roughness = jnum(pbr, "roughnessFactor", 1.0);
                void *bcf = jarr(pbr, "baseColorFactor");
                if (bcf && jarr_len(bcf) >= 3) {
                    base_r = jvalue_num(rt_seq_get(bcf, 0), base_r);
                    base_g = jvalue_num(rt_seq_get(bcf, 1), base_g);
                    base_b = jvalue_num(rt_seq_get(bcf, 2), base_b);
                    if (jarr_len(bcf) >= 4) {
                        base_a = jvalue_num(rt_seq_get(bcf, 3), base_a);
                    }
                }
                {
                    void *pbr_mat = rt_material3d_new_pbr(base_r, base_g, base_b);
                    if (pbr_mat) {
                        mat = pbr_mat;
                        rt_material3d_set_metallic(mat, metallic);
                        rt_material3d_set_roughness(mat, roughness);
                        if (base_a < 1.0)
                            rt_material3d_set_alpha(mat, base_a);
                    }
                }
                {
                    void *base_tex = jget(pbr, "baseColorTexture");
                    int64_t tex_idx = jint(base_tex, "index", -1);
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_texture(mat, texture_images[tex_idx]);
                }
                {
                    void *mr_tex = jget(pbr, "metallicRoughnessTexture");
                    int64_t tex_idx = jint(mr_tex, "index", -1);
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_metallic_roughness_map(mat, texture_images[tex_idx]);
                }
            }
            if (!mat)
                mat = rt_material3d_new();
            if (!mat)
                continue;

            // Emissive
            void *ef = jarr(mat_json, "emissiveFactor");
            if (ef && jarr_len(ef) >= 3) {
                double er = jvalue_num(rt_seq_get(ef, 0), 0.0);
                double eg = jvalue_num(rt_seq_get(ef, 1), 0.0);
                double eb = jvalue_num(rt_seq_get(ef, 2), 0.0);
                rt_material3d_set_emissive_color(mat, er, eg, eb);
            }
            {
                void *extensions = jget(mat_json, "extensions");
                void *emissive_strength =
                    extensions ? jget(extensions, "KHR_materials_emissive_strength") : NULL;
                if (emissive_strength)
                    rt_material3d_set_emissive_intensity(
                        mat, jnum(emissive_strength, "emissiveStrength", 1.0));
            }

            {
                void *normal_tex = jget(mat_json, "normalTexture");
                int64_t tex_idx = jint(normal_tex, "index", -1);
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_normal_map(mat, texture_images[tex_idx]);
                if (normal_tex)
                    rt_material3d_set_normal_scale(mat, jnum(normal_tex, "scale", 1.0));
            }
            {
                void *occlusion_tex = jget(mat_json, "occlusionTexture");
                int64_t tex_idx = jint(occlusion_tex, "index", -1);
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_ao_map(mat, texture_images[tex_idx]);
                if (occlusion_tex)
                    rt_material3d_set_ao(mat, jnum(occlusion_tex, "strength", 1.0));
            }
            {
                void *emissive_tex = jget(mat_json, "emissiveTexture");
                int64_t tex_idx = jint(emissive_tex, "index", -1);
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_emissive_map(mat, texture_images[tex_idx]);
            }
            {
                const char *alpha_mode = jstr(mat_json, "alphaMode");
                if (alpha_mode && strcmp(alpha_mode, "MASK") == 0) {
                    rt_material3d_set_alpha_mode(mat, RT_MATERIAL3D_ALPHA_MODE_MASK);
                    ((rt_material3d *)mat)->alpha_cutoff = jnum(mat_json, "alphaCutoff", 0.5);
                } else if (alpha_mode && strcmp(alpha_mode, "BLEND") == 0) {
                    rt_material3d_set_alpha_mode(mat, RT_MATERIAL3D_ALPHA_MODE_BLEND);
                } else {
                    rt_material3d_set_alpha_mode(mat, RT_MATERIAL3D_ALPHA_MODE_OPAQUE);
                }
            }
            rt_material3d_set_double_sided(mat, (int8_t)jint(mat_json, "doubleSided", 0));

            asset->materials[i] = mat;
            asset->material_count = i + 1;
        }
    }

    // Extract meshes
    void *meshes_arr = jarr(root, "meshes");
    int mesh_json_count = (int)jarr_len(meshes_arr);
    if (mesh_json_count > 0) {
        // Count total primitives (each primitive becomes a mesh)
        int total_prims = 0;
        for (int i = 0; i < mesh_json_count; i++) {
            void *mesh_json = rt_seq_get(meshes_arr, (int64_t)i);
            void *prims = jarr(mesh_json, "primitives");
            total_prims += (int)jarr_len(prims);
        }

        if (total_prims > 0) {
            mesh_prim_start = (int *)calloc((size_t)mesh_json_count, sizeof(int));
            mesh_prim_count = (int *)calloc((size_t)mesh_json_count, sizeof(int));
            primitive_materials = (void **)calloc((size_t)total_prims, sizeof(void *));
            asset->meshes = (void **)calloc((size_t)total_prims, sizeof(void *));
            int mesh_idx = 0;

            for (int mi = 0; mi < mesh_json_count && asset->meshes; mi++) {
                void *mesh_json = rt_seq_get(meshes_arr, (int64_t)mi);
                void *prims = jarr(mesh_json, "primitives");
                int prim_count = (int)jarr_len(prims);
                if (mesh_prim_start)
                    mesh_prim_start[mi] = mesh_idx;
                if (mesh_prim_count)
                    mesh_prim_count[mi] = prim_count;

                for (int pi = 0; pi < prim_count; pi++) {
                    void *prim = rt_seq_get(prims, (int64_t)pi);
                    void *attrs = jget(prim, "attributes");
                    int64_t material_idx = jint(prim, "material", -1);
                    if (!attrs)
                        continue;

                    // Get accessor indices
                    int64_t pos_acc = jint(attrs, "POSITION", -1);
                    int64_t norm_acc = jint(attrs, "NORMAL", -1);
                    int64_t uv_acc = jint(attrs, "TEXCOORD_0", -1);
                    int64_t color_acc = jint(attrs, "COLOR_0", -1);
                    int64_t tangent_acc = jint(attrs, "TANGENT", -1);
                    int64_t joints_acc = jint(attrs, "JOINTS_0", -1);
                    int64_t weights_acc = jint(attrs, "WEIGHTS_0", -1);
                    int64_t idx_acc = jint(prim, "indices", -1);
                    int64_t mode = jint(prim, "mode", 4);

                    if (pos_acc < 0)
                        continue;

                    gltf_accessor_view_t pos_view;
                    gltf_accessor_view_t norm_view;
                    gltf_accessor_view_t uv_view;
                    gltf_accessor_view_t color_view;
                    gltf_accessor_view_t tangent_view;
                    gltf_accessor_view_t joints_view;
                    gltf_accessor_view_t weights_view;
                    gltf_accessor_view_t idx_view;
                    int has_normals = gltf_get_accessor_view(root, norm_acc, buffers, buf_count, &norm_view);
                    int has_uvs = gltf_get_accessor_view(root, uv_acc, buffers, buf_count, &uv_view);
                    int has_colors =
                        gltf_get_accessor_view(root, color_acc, buffers, buf_count, &color_view);
                    int has_tangents =
                        gltf_get_accessor_view(root, tangent_acc, buffers, buf_count, &tangent_view);
                    int has_joints =
                        gltf_get_accessor_view(root, joints_acc, buffers, buf_count, &joints_view);
                    int has_weights =
                        gltf_get_accessor_view(root, weights_acc, buffers, buf_count, &weights_view);
                    int has_indices =
                        gltf_get_accessor_view(root, idx_acc, buffers, buf_count, &idx_view);

                    if (!gltf_get_accessor_view(root, pos_acc, buffers, buf_count, &pos_view) ||
                        pos_view.count == 0)
                        continue;

                    // Create mesh and populate vertices
                    void *mesh = rt_mesh3d_new();
                    if (!mesh)
                        continue;

                    for (int32_t vi = 0; vi < pos_view.count; vi++) {
                        float pos[3] = {0.0f, 0.0f, 0.0f};
                        float nrm[3] = {0.0f, 0.0f, 0.0f};
                        float uv[2] = {0.0f, 0.0f};
                        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                        float tangent[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                        float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        uint32_t joints[4] = {0u, 0u, 0u, 0u};
                        gltf_accessor_read_f32(&pos_view, vi, pos, 3);
                        if (has_normals)
                            gltf_accessor_read_f32(&norm_view, vi, nrm, 3);
                        if (has_uvs)
                            gltf_accessor_read_f32(&uv_view, vi, uv, 2);
                        if (has_colors) {
                            gltf_accessor_read_f32(&color_view, vi, color, 4);
                            if (color_view.comp_count < 4)
                                color[3] = 1.0f;
                        }
                        if (has_tangents) {
                            gltf_accessor_read_f32(&tangent_view, vi, tangent, 4);
                            if (tangent_view.comp_count < 4)
                                tangent[3] = 1.0f;
                        }
                        if (has_joints)
                            gltf_accessor_read_u32(&joints_view, vi, joints, 4);
                        if (has_weights)
                            gltf_accessor_read_f32(&weights_view, vi, weights, 4);

                        rt_mesh3d_add_vertex(
                            mesh, pos[0], pos[1], pos[2], nrm[0], nrm[1], nrm[2], uv[0], uv[1]);
                        vgfx3d_vertex_t *vertex = &((rt_mesh3d *)mesh)->vertices[vi];
                        memcpy(vertex->color, color, sizeof(vertex->color));
                        memcpy(vertex->tangent, tangent, sizeof(vertex->tangent));
                        if (has_joints || has_weights) {
                            rt_mesh3d_set_bone_weights(mesh,
                                                       vi,
                                                       (int64_t)joints[0],
                                                       weights[0],
                                                       (int64_t)joints[1],
                                                       weights[1],
                                                       (int64_t)joints[2],
                                                       weights[2],
                                                       (int64_t)joints[3],
                                                       weights[3]);
                            for (int j = 0; j < 4; j++) {
                                if (weights[j] > 0.0001f &&
                                    (int32_t)(joints[j] + 1u) > ((rt_mesh3d *)mesh)->bone_count) {
                                    ((rt_mesh3d *)mesh)->bone_count = (int32_t)(joints[j] + 1u);
                                }
                            }
                        }
                    }

                    if (!gltf_append_primitive_indices(
                            mesh, mode, has_indices ? &idx_view : NULL, pos_view.count)) {
                        gltf_release_local(mesh);
                        continue;
                    }

                    // Recalc normals if none provided
                    if (!has_normals && ((rt_mesh3d *)mesh)->vertex_count > 0)
                        rt_mesh3d_recalc_normals(mesh);
                    if (!has_tangents && has_uvs && material_idx >= 0 &&
                        material_idx < asset->material_count) {
                        rt_material3d *prim_material = (rt_material3d *)asset->materials[material_idx];
                        if (prim_material && prim_material->normal_map)
                            rt_mesh3d_calc_tangents(mesh);
                    }
                    gltf_import_primitive_morph_targets(
                        root, buffers, buf_count, mesh_json, prim, mesh, pos_view.count);

                    asset->meshes[mesh_idx++] = mesh;
                    asset->mesh_count = mesh_idx;
                    if (primitive_materials && material_idx >= 0 && material_idx < asset->material_count)
                        primitive_materials[mesh_idx - 1] = asset->materials[material_idx];
                }
            }
        }
    }

    gltf_parse_skins(asset, root, buffers, buf_count, &skins, &skin_count);
    gltf_parse_animations(asset, root, buffers, buf_count, skins, skin_count);
    if (asset->mesh_count > 0) {
        mesh_applied_skin = (int32_t *)malloc((size_t)asset->mesh_count * sizeof(*mesh_applied_skin));
        if (mesh_applied_skin) {
            for (int32_t i = 0; i < asset->mesh_count; i++)
                mesh_applied_skin[i] = -1;
        }
    }

    // Build a reusable template node hierarchy from the active glTF scene.
    {
        void *nodes_arr = jarr(root, "nodes");
        int node_json_count = (int)jarr_len(nodes_arr);
        if (node_json_count > 0) {
            rt_scene_node3d **nodes = (rt_scene_node3d **)calloc((size_t)node_json_count, sizeof(*nodes));
            int *has_parent = (int *)calloc((size_t)node_json_count, sizeof(int));
            asset->scene_root = rt_scene_node3d_new();

            for (int ni = 0; ni < node_json_count && nodes; ni++) {
                void *node_json = rt_seq_get(nodes_arr, (int64_t)ni);
                rt_scene_node3d *node = (rt_scene_node3d *)rt_scene_node3d_new();
                const char *name = jstr(node_json, "name");
                void *translation = jarr(node_json, "translation");
                void *rotation = jarr(node_json, "rotation");
                void *scale_arr = jarr(node_json, "scale");
                void *matrix_arr = jarr(node_json, "matrix");
                void *weights_arr = jarr(node_json, "weights");
                int64_t mesh_ref = jint(node_json, "mesh", -1);
                int64_t skin_ref = jint(node_json, "skin", -1);
                nodes[ni] = node;
                if (!node)
                    continue;

                if (name)
                    gltf_set_node_name(node, name);

                if (matrix_arr && jarr_len(matrix_arr) >= 16) {
                    double m[16];
                    for (int i = 0; i < 16; i++)
                        m[i] = jvalue_num(rt_seq_get(matrix_arr, (int64_t)i), i % 5 == 0 ? 1.0 : 0.0);
                    double row_major[16];
                    gltf_matrix_column_major_to_row_major(m, row_major);
                    gltf_matrix_to_trs(row_major, node->position, node->rotation, node->scale_xyz);
                } else {
                    node->position[0] = translation && jarr_len(translation) > 0
                                            ? jvalue_num(rt_seq_get(translation, 0), 0.0)
                                            : 0.0;
                    node->position[1] = translation && jarr_len(translation) > 1
                                            ? jvalue_num(rt_seq_get(translation, 1), 0.0)
                                            : 0.0;
                    node->position[2] = translation && jarr_len(translation) > 2
                                            ? jvalue_num(rt_seq_get(translation, 2), 0.0)
                                            : 0.0;

                    node->rotation[0] = rotation && jarr_len(rotation) > 0
                                            ? jvalue_num(rt_seq_get(rotation, 0), 0.0)
                                            : 0.0;
                    node->rotation[1] = rotation && jarr_len(rotation) > 1
                                            ? jvalue_num(rt_seq_get(rotation, 1), 0.0)
                                            : 0.0;
                    node->rotation[2] = rotation && jarr_len(rotation) > 2
                                            ? jvalue_num(rt_seq_get(rotation, 2), 0.0)
                                            : 0.0;
                    node->rotation[3] = rotation && jarr_len(rotation) > 3
                                            ? jvalue_num(rt_seq_get(rotation, 3), 1.0)
                                            : 1.0;

                    node->scale_xyz[0] = scale_arr && jarr_len(scale_arr) > 0
                                             ? jvalue_num(rt_seq_get(scale_arr, 0), 1.0)
                                             : 1.0;
                    node->scale_xyz[1] = scale_arr && jarr_len(scale_arr) > 1
                                             ? jvalue_num(rt_seq_get(scale_arr, 1), 1.0)
                                             : 1.0;
                    node->scale_xyz[2] = scale_arr && jarr_len(scale_arr) > 2
                                             ? jvalue_num(rt_seq_get(scale_arr, 2), 1.0)
                                             : 1.0;
                }

                node->world_dirty = 1;

                if (mesh_ref >= 0 && mesh_ref < mesh_json_count && mesh_prim_count && mesh_prim_start) {
                    int prim_start = mesh_prim_start[mesh_ref];
                    int prim_count = mesh_prim_count[mesh_ref];
                    if (skin_ref >= 0 && skin_ref < skin_count && mesh_applied_skin) {
                        for (int pi = 0; pi < prim_count; pi++) {
                            int mesh_index = prim_start + pi;
                            if (mesh_index >= 0 && mesh_index < asset->mesh_count &&
                                mesh_applied_skin[mesh_index] < 0) {
                                gltf_apply_skin_to_mesh(asset->meshes[mesh_index], &skins[skin_ref]);
                                mesh_applied_skin[mesh_index] = (int32_t)skin_ref;
                            }
                        }
                    }
                    if (weights_arr) {
                        for (int pi = 0; pi < prim_count; pi++) {
                            int mesh_index = prim_start + pi;
                            if (mesh_index >= 0 && mesh_index < asset->mesh_count)
                                gltf_apply_node_morph_weights(asset->meshes[mesh_index], weights_arr);
                        }
                    }
                    if (prim_count > 0) {
                        rt_scene_node3d_set_mesh(node, asset->meshes[prim_start]);
                        if (primitive_materials && primitive_materials[prim_start])
                            rt_scene_node3d_set_material(node, primitive_materials[prim_start]);
                        for (int pi = 1; pi < prim_count; pi++) {
                            rt_scene_node3d *prim_node =
                                (rt_scene_node3d *)rt_scene_node3d_new();
                            if (!prim_node)
                                continue;
                            rt_scene_node3d_set_mesh(prim_node, asset->meshes[prim_start + pi]);
                            if (primitive_materials && primitive_materials[prim_start + pi])
                                rt_scene_node3d_set_material(
                                    prim_node, primitive_materials[prim_start + pi]);
                            rt_scene_node3d_add_child(node, prim_node);
                            gltf_release_local(prim_node);
                        }
                    }
                }
            }

            for (int ni = 0; ni < node_json_count && nodes; ni++) {
                void *node_json = rt_seq_get(nodes_arr, (int64_t)ni);
                void *children = jarr(node_json, "children");
                for (int ci = 0; ci < jarr_len(children); ci++) {
                    int64_t child_idx = jvalue_int(rt_seq_get(children, (int64_t)ci), -1);
                    if (child_idx >= 0 && child_idx < node_json_count && nodes[ni] && nodes[child_idx]) {
                        rt_scene_node3d_add_child(nodes[ni], nodes[child_idx]);
                        has_parent[child_idx] = 1;
                    }
                }
            }

            if (asset->scene_root) {
                void *scenes_arr = jarr(root, "scenes");
                int64_t active_scene = jint(root, "scene", 0);
                int attached_any = 0;
                if (scenes_arr && jarr_len(scenes_arr) > 0 && active_scene >= 0 &&
                    active_scene < jarr_len(scenes_arr)) {
                    void *scene_json = rt_seq_get(scenes_arr, active_scene);
                    void *scene_nodes = jarr(scene_json, "nodes");
                    for (int i = 0; i < jarr_len(scene_nodes); i++) {
                        int64_t node_idx = jvalue_int(rt_seq_get(scene_nodes, (int64_t)i), -1);
                        if (node_idx >= 0 && node_idx < node_json_count && nodes[node_idx]) {
                            rt_scene_node3d_add_child(asset->scene_root, nodes[node_idx]);
                            attached_any = 1;
                        }
                    }
                }
                if (!attached_any) {
                    for (int i = 0; i < node_json_count; i++) {
                        if (!has_parent[i] && nodes[i])
                            rt_scene_node3d_add_child(asset->scene_root, nodes[i]);
                    }
                }
                if (asset->scene_root)
                    asset->node_count = gltf_count_subtree(asset->scene_root) - 1;
                for (int i = 0; i < node_json_count; i++)
                    gltf_release_ref((void **)&nodes[i]);
            }

            free(nodes);
            free(has_parent);
        }
    }

    if (images) {
        for (int i = 0; i < image_count; i++)
            gltf_release_ref(&images[i]);
    }
    free(images);
    free(texture_images);
    free(mesh_prim_start);
    free(mesh_prim_count);
    free(primitive_materials);
    free(mesh_applied_skin);
    gltf_free_skins(skins, skin_count);

    // Cleanup buffers (except BIN chunk which is part of file_data)
    for (int i = 0; i < buf_count; i++) {
        if (buffers[i].data != bin_chunk)
            free(buffers[i].data);
    }
    free(buffers);
    free(file_data);

    return asset;
}

/// @brief Get the number of meshes extracted from the GLTF file.
int64_t rt_gltf_mesh_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->mesh_count : 0;
}

/// @brief Get a mesh by index from the loaded GLTF asset.
void *rt_gltf_get_mesh(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (index < 0 || index >= a->mesh_count)
        return NULL;
    return a->meshes[index];
}

/// @brief Get the number of materials extracted from the GLTF file.
int64_t rt_gltf_material_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->material_count : 0;
}

/// @brief Get a material by index from the loaded GLTF asset.
void *rt_gltf_get_material(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (index < 0 || index >= a->material_count)
        return NULL;
    return a->materials[index];
}

/// @brief Number of skeletons extracted from the loaded glTF asset.
int64_t rt_gltf_skeleton_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->skeleton_count : 0;
}

/// @brief Get a skeleton by index from the loaded glTF asset.
void *rt_gltf_get_skeleton(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (index < 0 || index >= a->skeleton_count)
        return NULL;
    return a->skeletons[index];
}

/// @brief Number of animation clips extracted from the loaded glTF asset.
int64_t rt_gltf_animation_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->animation_count : 0;
}

/// @brief Get an Animation3D clip by index from the loaded glTF asset.
void *rt_gltf_get_animation(void *obj, int64_t index) {
    if (!obj)
        return NULL;
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (index < 0 || index >= a->animation_count)
        return NULL;
    return a->animations[index];
}

/// @brief Number of nodes in the loaded glTF scene tree (0 for NULL).
int64_t rt_gltf_node_count(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->node_count : 0;
}

/// @brief Return the scene-root SceneNode of the loaded asset (NULL if not loaded / NULL).
void *rt_gltf_get_scene_root(void *obj) {
    return obj ? ((rt_gltf_asset *)obj)->scene_root : NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
