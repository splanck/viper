//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_gltf.c
// Purpose: glTF 2.0 (.gltf/.glb) loader implementation.
// Key invariants:
//   - Uses existing rt_json parser for JSON content
//   - Supports .glb binary container (magic 0x46546C67)
//   - Preserves glTF metallic-roughness materials on Material3D's native PBR surface
//   - Mesh primitives support POSITION/NORMAL/TEXCOORD_0 plus COLOR_0, TANGENT,
//     and JOINTS_0/WEIGHTS_0 + JOINTS_1/WEIGHTS_1 vertex attributes
//   - Triangle-list, triangle-strip, and triangle-fan primitives are triangulated
// Ownership/Lifetime:
//   - All extracted objects are GC-managed
// Links: rt_gltf.h, rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_gltf.h"
#include "rt_asset.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_gif.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_vec3.h"

#include <float.h>
#include <setjmp.h>
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
extern void rt_trap_set_recovery(jmp_buf *buf);
extern void rt_trap_clear_recovery(void);
extern void rt_obj_free(void *obj);
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);
extern void *rt_pixels_load(void *path);
extern void rt_camera3d_look_at_components(void *obj,
                                           double eye_x,
                                           double eye_y,
                                           double eye_z,
                                           double target_x,
                                           double target_y,
                                           double target_z,
                                           double up_x,
                                           double up_y,
                                           double up_z);
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
extern void rt_material3d_set_unlit(void *obj, int8_t unlit);
extern void rt_material3d_set_shading_model(void *obj, int64_t model);
extern void rt_material3d_set_reflectivity(void *obj, double value);
extern void rt_material3d_set_import_texture_slot(void *obj,
                                                  int64_t slot,
                                                  int64_t uv_set,
                                                  double offset_u,
                                                  double offset_v,
                                                  double scale_u,
                                                  double scale_v,
                                                  double rotation,
                                                  int64_t wrap_s,
                                                  int64_t wrap_t,
                                                  int64_t filter);

//===----------------------------------------------------------------------===//
// Asset container
//===----------------------------------------------------------------------===//

typedef struct {
    void *root;
    char *name;
    int32_t node_count;
    void **cameras;
    int32_t camera_count;
    int32_t camera_capacity;
} gltf_scene_info_t;

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
    void **node_animations;
    int32_t node_animation_count;
    void **cameras;
    int32_t camera_count;
    int32_t camera_capacity;
    gltf_scene_info_t *scenes;
    int32_t scene_count;
    int32_t scene_capacity;
    void *scene_root;
    int32_t node_count;
} rt_gltf_asset;

/// @brief Validate @p obj as a GLTF asset handle, returning NULL on class mismatch.
static rt_gltf_asset *gltf_asset_checked(void *obj) {
    return (rt_gltf_asset *)rt_g3d_checked_or_null(obj, RT_G3D_GLTF_ASSET_CLASS_ID);
}

typedef struct {
    void *skeleton;
    int32_t *joint_nodes;
    int32_t *joint_to_bone;
    int32_t joint_count;
} gltf_skin_t;

typedef struct {
    int32_t wrap_s;
    int32_t wrap_t;
    int32_t filter;
} gltf_sampler_info_t;

typedef struct {
    int32_t texcoord;
    int8_t has_transform;
    double offset[2];
    double scale[2];
    double rotation;
} gltf_texture_info_t;

typedef struct {
    gltf_texture_info_t slots[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
} gltf_material_info_t;

static void *jget(void *obj, const char *key);
static void *jarr(void *obj, const char *key);
static const char *jstr(void *obj, const char *key);
static int64_t jarr_len(void *arr);
static double jvalue_num(void *value, double def);
static int64_t jvalue_int(void *value, int64_t def);
static int gltf_ascii_ieq_n(const char *a, const char *b, size_t len);

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
    if (a->node_animations) {
        for (int32_t i = 0; i < a->node_animation_count; i++) {
            if (a->node_animations[i] && rt_obj_release_check0(a->node_animations[i]))
                rt_obj_free(a->node_animations[i]);
        }
    }
    free(a->node_animations);
    a->node_animations = NULL;
    if (a->cameras) {
        for (int32_t i = 0; i < a->camera_count; i++) {
            if (a->cameras[i] && rt_obj_release_check0(a->cameras[i]))
                rt_obj_free(a->cameras[i]);
        }
    }
    free(a->cameras);
    a->cameras = NULL;
    a->camera_count = 0;
    a->camera_capacity = 0;
    if (a->scenes) {
        for (int32_t i = 0; i < a->scene_count; i++) {
            gltf_scene_info_t *scene = &a->scenes[i];
            if (scene->root && rt_obj_release_check0(scene->root))
                rt_obj_free(scene->root);
            scene->root = NULL;
            free(scene->name);
            scene->name = NULL;
            if (scene->cameras) {
                for (int32_t ci = 0; ci < scene->camera_count; ci++) {
                    if (scene->cameras[ci] && rt_obj_release_check0(scene->cameras[ci]))
                        rt_obj_free(scene->cameras[ci]);
                }
            }
            free(scene->cameras);
            scene->cameras = NULL;
            scene->camera_count = 0;
            scene->camera_capacity = 0;
        }
    }
    free(a->scenes);
    a->scenes = NULL;
    a->scene_count = 0;
    a->scene_capacity = 0;
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

/// @brief Return the best available name for a glTF node, falling back to a synthetic index string.
/// @details First tries the JSON `"name"` field for the node at @p node_index. If that field
///   is absent or empty, a fallback string `"node_N"` is written into @p buffer and returned.
///   The buffer fallback avoids returning NULL to callers that always want a printable label,
///   at the cost of a stack-allocated temporary that must remain live as long as the returned
///   pointer is used. Returns NULL only when both the JSON name is absent and no buffer is
///   provided.
/// @param nodes_arr   JSON array of glTF node objects.
/// @param node_index  Zero-based index into @p nodes_arr.
/// @param buffer      Caller-supplied scratch buffer for the synthetic name; may be NULL.
/// @param buffer_size Capacity of @p buffer including NUL terminator.
/// @return Borrowed pointer to the name string (JSON storage or @p buffer), or NULL.
static const char *gltf_effective_node_name(void *nodes_arr,
                                            int32_t node_index,
                                            char *buffer,
                                            size_t buffer_size) {
    void *node_json;
    const char *name = NULL;
    if (nodes_arr && node_index >= 0 && node_index < jarr_len(nodes_arr)) {
        node_json = rt_seq_get(nodes_arr, node_index);
        name = jstr(node_json, "name");
    }
    if (name && name[0] != '\0')
        return name;
    if (buffer && buffer_size > 0) {
        snprintf(buffer, buffer_size, "node_%d", (int)node_index);
        return buffer;
    }
    return NULL;
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
    double det;
    int flip_axis;
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

    det = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[1] * (m[4] * m[10] - m[6] * m[8]) +
          m[2] * (m[4] * m[9] - m[5] * m[8]);
    if (det < 0.0) {
        flip_axis = 0;
        if (m[0] < 0.0)
            flip_axis = 0;
        else if (m[5] < 0.0)
            flip_axis = 1;
        else if (m[10] < 0.0)
            flip_axis = 2;
        else {
            if (scale[1] > scale[flip_axis])
                flip_axis = 1;
            if (scale[2] > scale[flip_axis])
                flip_axis = 2;
        }
        scale[flip_axis] = -scale[flip_axis];
    }

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

/// @brief Compose a row-major 4×4 transform matrix from separate translation, quaternion, and
/// scale.
/// @details Expands the unit quaternion `(x,y,z,w)` into the 3×3 rotation submatrix using the
///   standard double-angle identities (`xx = x*(2x)`, etc.), then multiplies each column by the
///   corresponding scale component and appends the translation in the rightmost column. The
///   bottom row is `[0, 0, 0, 1]`. This is the inverse of `gltf_matrix_to_trs` — it rebuilds
///   the TRS matrix after decomposition so we can accumulate world-space transforms during
///   the node-graph traversal.
/// @param pos   3-element translation vector `[tx, ty, tz]`.
/// @param quat  4-element unit quaternion `[x, y, z, w]`.
/// @param scale 3-element scale vector `[sx, sy, sz]`.
/// @param out   Caller-supplied 16-element array that receives the row-major matrix.
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

/// @brief Compute the local-space 4×4 matrix for a glTF node.
/// @details A glTF node stores its transform as either a 16-element column-major `"matrix"` array
///   or as separate `"translation"`, `"rotation"`, and `"scale"` arrays. This function handles
///   both forms: when a `"matrix"` field is present it is transposed from glTF's column-major
///   convention to Viper's row-major layout via `gltf_matrix_column_major_to_row_major`; otherwise
///   the three TRS arrays are read (defaulting to identity when absent) and reassembled into a
///   matrix by `gltf_build_trs_matrix`. Returns 1 on success, 0 if the node index is out of
///   range or required data is missing.
/// @param nodes_arr  JSON array of glTF node objects.
/// @param node_idx   Zero-based node index.
/// @param out        Caller-supplied 16-element double array for the row-major result matrix.
/// @return 1 on success, 0 if the node is inaccessible or @p out is NULL.
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

/// @brief Iterative count — returns 1 for `node` plus the sum of all descendants.
static int32_t gltf_count_subtree(const rt_scene_node3d *node) {
    const rt_scene_node3d **stack = NULL;
    int32_t count = 0;
    int32_t capacity = 0;
    int32_t total = 0;
    if (!node)
        return 0;
    while (count >= capacity) {
        int32_t next_capacity = capacity == 0 ? 32 : capacity * 2;
        const rt_scene_node3d **grown;
        if (capacity > INT32_MAX / 2)
            return total;
        grown = (const rt_scene_node3d **)realloc(stack, (size_t)next_capacity * sizeof(*stack));
        if (!grown) {
            free(stack);
            return total;
        }
        stack = grown;
        capacity = next_capacity;
    }
    stack[count++] = node;
    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (!current)
            continue;
        if (total < INT32_MAX)
            total++;
        for (int32_t i = 0; i < current->child_count; ++i) {
            if (count >= capacity) {
                int32_t next_capacity = capacity == 0 ? 32 : capacity * 2;
                const rt_scene_node3d **grown;
                if (capacity > INT32_MAX / 2) {
                    free(stack);
                    return total;
                }
                grown =
                    (const rt_scene_node3d **)realloc(stack,
                                                      (size_t)next_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return total;
                }
                stack = grown;
                capacity = next_capacity;
            }
            stack[count++] = current->children[i];
        }
    }
    free(stack);
    return total;
}

/// @brief Membership test for the set of glTF extensions this loader handles when
///        they appear in `extensionsRequired`.
/// @details The supported list is deliberately small and hardcoded: adding an entry
///          here is a commitment to actually interpret that extension's JSON in
///          material / node / mesh parsing. Listing an extension here without
///          implementing it is worse than not listing it, because assets that require
///          it will load and then silently render wrong.
/// @return 1 if the extension is supported, 0 otherwise (including NULL input).
static int gltf_required_extension_supported(const char *name) {
    if (!name)
        return 0;
    return strcmp(name, "KHR_texture_transform") == 0 ||
           strcmp(name, "KHR_materials_emissive_strength") == 0 ||
           strcmp(name, "KHR_materials_unlit") == 0 || strcmp(name, "KHR_lights_punctual") == 0;
}

/// @brief Enforce the glTF `extensionsRequired` contract.
/// @details If the document declares extensions as REQUIRED (not merely USED), the
///          spec says a loader that can't handle any of them must refuse to load the
///          asset rather than produce a degraded rendering. This function walks the
///          required list and returns 0 on the first unsupported extension so the
///          top-level loader can bail cleanly. Missing or empty `extensionsRequired`
///          is treated as "nothing required" (returns 1).
/// @return 1 when every required extension is supported (or the array is absent);
///         0 when any required extension is unsupported and the load should fail.
static int gltf_validate_required_extensions(void *root) {
    void *required = jarr(root, "extensionsRequired");
    if (!required)
        return 1;
    for (int64_t i = 0; i < jarr_len(required); i++) {
        rt_string name = (rt_string)rt_seq_get(required, i);
        const char *ext = name ? rt_string_cstr(name) : NULL;
        if (!gltf_required_extension_supported(ext))
            return 0;
    }
    return 1;
}

/// @brief Zero-initialise a `gltf_texture_info_t` to identity-transform defaults.
/// @details Sets texcoord=0, has_transform=0, offset=(0,0), scale=(1,1), rotation=0.0 so callers
///   can always call `gltf_read_texture_info` after this without guarding on partial
///   initialisation.
static void gltf_texture_info_init(gltf_texture_info_t *info) {
    if (!info)
        return;
    info->texcoord = 0;
    info->has_transform = 0;
    info->offset[0] = 0.0;
    info->offset[1] = 0.0;
    info->scale[0] = 1.0;
    info->scale[1] = 1.0;
    info->rotation = 0.0;
}

/// @brief Parse a glTF `textureInfo` object (including `KHR_texture_transform` if present).
/// @details Reads the `texCoord` index and, if the `KHR_texture_transform` extension block is
///   present in `extensions`, reads the UV transform (offset, scale, rotation) and sets
///   `has_transform = 1`. The extension's `texCoord` can override the base texcoord index,
///   matching the KHR_texture_transform spec. When no transform block is present the struct
///   retains identity defaults from `gltf_texture_info_init`. Null @p texture_info leaves @p out
///   at defaults so callers need not guard against missing fields.
/// @param texture_info  Parsed glTF `textureInfo` JSON object (may be NULL for default texture
/// slot).
/// @param out           Output struct; always initialised to defaults before filling.
static void gltf_read_texture_info(void *texture_info, gltf_texture_info_t *out) {
    void *extensions;
    void *transform;
    void *offset;
    void *scale;
    if (!out)
        return;
    gltf_texture_info_init(out);
    if (!texture_info)
        return;
    out->texcoord = (int32_t)jint(texture_info, "texCoord", 0);
    if (out->texcoord < 0)
        out->texcoord = 0;
    extensions = jget(texture_info, "extensions");
    transform = extensions ? jget(extensions, "KHR_texture_transform") : NULL;
    if (!transform)
        return;
    out->has_transform = 1;
    out->texcoord = (int32_t)jint(transform, "texCoord", out->texcoord);
    if (out->texcoord < 0)
        out->texcoord = 0;
    offset = jarr(transform, "offset");
    scale = jarr(transform, "scale");
    if (offset && jarr_len(offset) >= 2) {
        out->offset[0] = jvalue_num(rt_seq_get(offset, 0), 0.0);
        out->offset[1] = jvalue_num(rt_seq_get(offset, 1), 0.0);
    }
    if (scale && jarr_len(scale) >= 2) {
        out->scale[0] = jvalue_num(rt_seq_get(scale, 0), 1.0);
        out->scale[1] = jvalue_num(rt_seq_get(scale, 1), 1.0);
    }
    out->rotation = jnum(transform, "rotation", 0.0);
}

/// @brief Map a glTF sampler `wrapS`/`wrapT` integer to a Viper wrap-mode constant.
/// @details glTF uses the original GL enum integers: 33071 = GL_CLAMP_TO_EDGE,
///   33648 = GL_MIRRORED_REPEAT. Anything else (including the default 10497 =
///   GL_REPEAT) maps to `RT_MATERIAL3D_TEXTURE_WRAP_REPEAT`.
/// @param wrap  Raw integer from the glTF sampler JSON.
/// @return One of the `RT_MATERIAL3D_TEXTURE_WRAP_*` constants.
static int32_t gltf_map_sampler_wrap(int64_t wrap) {
    if (wrap == 33071)
        return RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE;
    if (wrap == 33648)
        return RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT;
    return RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
}

/// @brief Map a glTF sampler min/mag filter pair to a Viper filter-mode constant.
/// @details glTF separates min and mag filters; Viper uses a single constant. The mag filter
///   is preferred (it directly controls visible texel appearance). GL nearest constants:
///   9728 = GL_NEAREST, 9984 = GL_NEAREST_MIPMAP_NEAREST, 9986 = GL_NEAREST_MIPMAP_LINEAR.
///   All other values (including linear variants 9729, 9985, 9987) map to LINEAR. When
///   @p mag_filter is -1 (absent in the JSON), falls back to @p min_filter.
/// @param min_filter  Raw `minFilter` value from the glTF sampler JSON, or -1 if absent.
/// @param mag_filter  Raw `magFilter` value, or -1 if absent.
/// @return `RT_MATERIAL3D_TEXTURE_FILTER_NEAREST` or `RT_MATERIAL3D_TEXTURE_FILTER_LINEAR`.
static int32_t gltf_map_sampler_filter(int64_t min_filter, int64_t mag_filter) {
    int64_t filter = mag_filter >= 0 ? mag_filter : min_filter;
    if (filter == 9728 || filter == 9984 || filter == 9986)
        return RT_MATERIAL3D_TEXTURE_FILTER_NEAREST;
    return RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
}

/// @brief Initialise a `gltf_sampler_info_t` to the glTF default sampler state.
/// @details glTF defaults: wrapS = wrapT = REPEAT, filter = LINEAR (per spec §5.24.1).
static void gltf_sampler_info_init(gltf_sampler_info_t *info) {
    if (!info)
        return;
    info->wrap_s = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    info->wrap_t = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    info->filter = RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
}

/// @brief Parse a glTF sampler JSON object into a `gltf_sampler_info_t`.
/// @details Reads `wrapS`, `wrapT`, `minFilter`, and `magFilter` using their glTF
///   default values (10497 = REPEAT for wrap; -1 = absent for filter) and maps them
///   to Viper constants via `gltf_map_sampler_wrap` / `gltf_map_sampler_filter`.
///   A NULL @p sampler_json leaves @p out at the glTF defaults (REPEAT / LINEAR).
/// @param sampler_json  Parsed glTF sampler object JSON; may be NULL for default state.
/// @param out           Output struct; always initialised before filling.
static void gltf_read_sampler_info(void *sampler_json, gltf_sampler_info_t *out) {
    if (!out)
        return;
    gltf_sampler_info_init(out);
    if (!sampler_json)
        return;
    out->wrap_s = gltf_map_sampler_wrap(jint(sampler_json, "wrapS", 10497));
    out->wrap_t = gltf_map_sampler_wrap(jint(sampler_json, "wrapT", 10497));
    out->filter = gltf_map_sampler_filter(jint(sampler_json, "minFilter", -1),
                                          jint(sampler_json, "magFilter", -1));
}

/// @brief DFS helper for cycle detection in the glTF node graph.
/// @details Uses a three-colour state array: 0 = unvisited, 1 = in-progress (grey), 2 = done
/// (black).
///   Returning 0 from a grey node means a back-edge (cycle) was found. Children that are out of
///   range or already grey are also treated as invalid. Called by `gltf_validate_node_graph`
///   once per node. The @p state array must be zero-initialised by the caller before the first
///   call.
/// @param nodes_arr   JSON array of glTF node objects.
/// @param node_count  Total number of nodes (bounds the valid index range).
/// @param node_idx    Node to visit.
/// @param state       Per-node colour byte array of length @p node_count.
/// @return 1 if the subtree is DAG-valid, 0 if a cycle or out-of-bounds child is found.
static int gltf_validate_node_visit(void *nodes_arr,
                                    int32_t node_count,
                                    int32_t node_idx,
                                    uint8_t *state) {
    typedef struct gltf_node_visit_item {
        int32_t node;
        int8_t exit;
    } gltf_node_visit_item;
    gltf_node_visit_item *stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 0;
    if (!nodes_arr || !state || node_idx < 0 || node_idx >= node_count)
        return 0;
    if (state[node_idx] == 1)
        return 0;
    if (state[node_idx] == 2)
        return 1;
    stack_capacity = 32;
    stack = (gltf_node_visit_item *)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack)
        return 0;
    stack[stack_count++] = (gltf_node_visit_item){node_idx, 0};
    while (stack_count > 0) {
        gltf_node_visit_item item = stack[--stack_count];
        if (item.node < 0 || item.node >= node_count) {
            free(stack);
            return 0;
        }
        if (item.exit) {
            state[item.node] = 2;
            continue;
        }
        if (state[item.node] == 2)
            continue;
        if (state[item.node] == 1) {
            free(stack);
            return 0;
        }
        state[item.node] = 1;
        if (stack_count >= stack_capacity) {
            int32_t next_capacity = stack_capacity * 2;
            gltf_node_visit_item *grown;
            if (stack_capacity > INT32_MAX / 2) {
                free(stack);
                return 0;
            }
            grown =
                (gltf_node_visit_item *)realloc(stack, (size_t)next_capacity * sizeof(*stack));
            if (!grown) {
                free(stack);
                return 0;
            }
            stack = grown;
            stack_capacity = next_capacity;
        }
        stack[stack_count++] = (gltf_node_visit_item){item.node, 1};
        {
            void *node_json = rt_seq_get(nodes_arr, item.node);
            void *children = jarr(node_json, "children");
            int64_t child_len = jarr_len(children);
            for (int64_t ci = child_len - 1; ci >= 0; --ci) {
                int64_t child = jvalue_int(rt_seq_get(children, ci), -1);
                if (child < 0 || child >= node_count || state[child] == 1) {
                    free(stack);
                    return 0;
                }
                if (state[child] == 2)
                    continue;
                if (stack_count >= stack_capacity) {
                    int32_t next_capacity = stack_capacity * 2;
                    gltf_node_visit_item *grown;
                    if (stack_capacity > INT32_MAX / 2) {
                        free(stack);
                        return 0;
                    }
                    grown = (gltf_node_visit_item *)realloc(stack,
                                                            (size_t)next_capacity *
                                                                sizeof(*stack));
                    if (!grown) {
                        free(stack);
                        return 0;
                    }
                    stack = grown;
                    stack_capacity = next_capacity;
                }
                stack[stack_count++] = (gltf_node_visit_item){(int32_t)child, 0};
            }
        }
    }
    free(stack);
    return 1;
}

/// @brief Validate the glTF node graph for a well-formed forest (no cycles, unique parents).
/// @details A valid glTF scene node graph must be a directed acyclic forest: each node has at most
///   one parent, there are no back-edges (cycles), and all child indices are in `[0, node_count)`.
///   This function builds a parent array (each entry is -1 for roots or the parent's index), then
///   runs a DFS over every node to detect cycles via `gltf_validate_node_graph`. Returns 0 and
///   frees intermediates if any of these invariants are violated. When @p out_parent is non-NULL
///   and validation succeeds the parent array is returned (caller must `free` it); otherwise it is
///   freed internally. A null or empty nodes array is treated as a trivially valid empty forest.
/// @param nodes_arr   JSON array of glTF node objects.
/// @param node_count  Number of nodes; must match `jarr_len(nodes_arr)`.
/// @param out_parent  If non-NULL, receives the allocated parent-index array on success.
/// @return 1 if the graph is valid, 0 if a cycle, duplicate parent, or OOM is detected.
static int gltf_validate_node_graph(void *nodes_arr, int32_t node_count, int **out_parent) {
    int *parent = NULL;
    uint8_t *state = NULL;
    if (out_parent)
        *out_parent = NULL;
    if (!nodes_arr || node_count <= 0)
        return 1;
    parent = (int *)malloc((size_t)node_count * sizeof(*parent));
    state = (uint8_t *)calloc((size_t)node_count, sizeof(*state));
    if (!parent || !state) {
        free(parent);
        free(state);
        return 0;
    }
    for (int32_t i = 0; i < node_count; i++)
        parent[i] = -1;
    for (int32_t ni = 0; ni < node_count; ni++) {
        void *node_json = rt_seq_get(nodes_arr, ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int64_t child = jvalue_int(rt_seq_get(children, ci), -1);
            if (child < 0 || child >= node_count || parent[child] >= 0) {
                free(parent);
                free(state);
                return 0;
            }
            parent[child] = ni;
        }
    }
    for (int32_t ni = 0; ni < node_count; ni++) {
        if (!gltf_validate_node_visit(nodes_arr, node_count, ni, state)) {
            free(parent);
            free(state);
            return 0;
        }
    }
    free(state);
    if (out_parent)
        *out_parent = parent;
    else
        free(parent);
    return 1;
}

/// @brief Bind a texture index + sampler state + UV-transform to one material texture slot.
/// @details Looks up wrap/filter from the pre-built @p texture_samplers array at @p texture_index
///   (defaulting to REPEAT/LINEAR when the index is out of range), then calls
///   `rt_material3d_set_import_texture_slot` with the full combined parameters. If @p info is NULL
///   an identity `gltf_texture_info_t` (texcoord=0, no transform) is used so callers can pass NULL
///   for textures that have no `KHR_texture_transform` extension block. Slots outside the valid
///   range `[0, RT_MATERIAL3D_TEXTURE_SLOT_COUNT)` are silently ignored.
/// @param texture_samplers  Per-texture sampler state array resolved from the `"samplers"` array.
/// @param texture_count     Length of @p texture_samplers.
/// @param texture_index     glTF texture index into @p texture_samplers (and the image table).
/// @param material          Viper material object to write the slot into.
/// @param slot              Destination slot index in `[0, RT_MATERIAL3D_TEXTURE_SLOT_COUNT)`.
/// @param info              Optional UV-transform + texcoord override; NULL uses identity.
static void gltf_apply_texture_slot(const gltf_sampler_info_t *texture_samplers,
                                    int32_t texture_count,
                                    int64_t texture_index,
                                    void *material,
                                    int64_t slot,
                                    const gltf_texture_info_t *info) {
    gltf_texture_info_t identity;
    const gltf_texture_info_t *texture_info = info;
    int32_t wrap_s = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    int32_t wrap_t = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    int32_t filter = RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
    if (!material || slot < 0 || slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return;
    if (!texture_info) {
        gltf_texture_info_init(&identity);
        texture_info = &identity;
    }
    if (texture_samplers && texture_index >= 0 && texture_index < texture_count) {
        wrap_s = texture_samplers[texture_index].wrap_s;
        wrap_t = texture_samplers[texture_index].wrap_t;
        filter = texture_samplers[texture_index].filter;
    }
    rt_material3d_set_import_texture_slot(material,
                                          slot,
                                          texture_info->texcoord,
                                          texture_info->offset[0],
                                          texture_info->offset[1],
                                          texture_info->scale[0],
                                          texture_info->scale[1],
                                          texture_info->rotation,
                                          wrap_s,
                                          wrap_t,
                                          filter);
}

static int gltf_texture_index_missing_supported_payload(int64_t texture_index,
                                                        int32_t texture_count,
                                                        void **texture_images,
                                                        const uint8_t *texture_supported) {
    return texture_index >= 0 && texture_index < texture_count && texture_supported &&
           texture_supported[texture_index] && (!texture_images || !texture_images[texture_index]);
}

//===----------------------------------------------------------------------===//
// Buffer management
//===----------------------------------------------------------------------===//

typedef struct {
    uint8_t *data;
    size_t len;
} gltf_buffer_t;

typedef enum {
    GLTF_PRELOAD_DEP_BUFFER = 1,
    GLTF_PRELOAD_DEP_IMAGE = 2,
    GLTF_PRELOAD_DEP_IMAGE_RGBA = 3,
    GLTF_PRELOAD_DEP_MESH_POD = 4,
    GLTF_PRELOAD_DEP_MORPH_POD = 5,
    GLTF_PRELOAD_DEP_IMAGE_PIXELS = 6
} gltf_preload_dependency_kind_t;

#define GLTF_PRELOAD_MESH_POD_MAGIC 0x504D4756u /* "VGMP" little-endian */
#define GLTF_PRELOAD_MESH_POD_VERSION 1u
#define GLTF_PRELOAD_MESH_POD_HEADER_SIZE 32u
#define GLTF_PRELOAD_MESH_POD_HAS_NORMALS 0x1u
#define GLTF_PRELOAD_MESH_POD_HAS_UV0 0x2u
#define GLTF_PRELOAD_MESH_POD_HAS_TANGENTS 0x4u
#define GLTF_PRELOAD_MESH_POD_HAS_SKINNING 0x8u

#define GLTF_PRELOAD_MORPH_POD_MAGIC 0x544D4756u /* "VGMT" little-endian */
#define GLTF_PRELOAD_MORPH_POD_VERSION 1u
#define GLTF_PRELOAD_MORPH_POD_HEADER_SIZE 32u
#define GLTF_PRELOAD_MORPH_POD_RECORD_SIZE 32u
#define GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS 0x1u
#define GLTF_PRELOAD_MORPH_POD_HAS_NORMALS 0x2u
#define GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS 0x4u

typedef struct {
    char *path;
    uint8_t *data;
    void *object;
    size_t len;
    size_t prepared;
    gltf_preload_dependency_kind_t kind;
} gltf_preload_dependency_t;

typedef struct {
    const uint8_t *data;
    size_t len;
} gltf_preload_buffer_ref_t;

typedef struct {
    int buffer;
    size_t byte_offset;
    size_t byte_length;
    size_t byte_stride;
    int valid;
} gltf_preload_buffer_view_ref_t;

typedef struct {
    int view;
    size_t byte_offset;
    int comp_type;
    int comp_count;
    int count;
    int8_t normalized;
    int8_t has_sparse;
    int sparse_indices_view;
    size_t sparse_indices_offset;
    int sparse_values_view;
    size_t sparse_values_offset;
    int sparse_count;
    int sparse_index_comp_type;
    int valid;
} gltf_preload_accessor_ref_t;

struct rt_gltf_preload_bundle {
    uint8_t *root_data;
    size_t root_size;
    gltf_preload_dependency_t *dependencies;
    size_t dependency_count;
    size_t dependency_capacity;
};

typedef struct {
    const uint8_t *data;
    int32_t count;
    int32_t stride;
    int32_t comp_type;
    int32_t comp_count;
    int8_t normalized;
    const uint8_t *sparse_indices;
    const uint8_t *sparse_values;
    int32_t sparse_count;
    int32_t sparse_index_comp_type;
    int32_t sparse_index_stride;
    int32_t sparse_value_stride;
} gltf_accessor_view_t;

static void gltf_preload_mesh_key(int mesh_index, int primitive_index, char *out, size_t out_cap);
static void gltf_preload_morph_key(int mesh_index, int primitive_index, char *out, size_t out_cap);
static uint32_t gltf_read_u32_le(const uint8_t *p);
static int gltf_checked_mul_size(size_t a, size_t b, size_t *out);

static char *gltf_strdup_cstr(const char *text) {
    size_t len;
    char *copy;
    if (!text)
        return NULL;
    len = strlen(text);
    copy = (char *)malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static void gltf_preload_release_object(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void gltf_preload_dependency_clear(gltf_preload_dependency_t *dep) {
    if (!dep)
        return;
    free(dep->path);
    free(dep->data);
    gltf_preload_release_object(&dep->object);
    memset(dep, 0, sizeof(*dep));
}

void rt_gltf_preload_bundle_free(rt_gltf_preload_bundle *bundle) {
    if (!bundle)
        return;
    free(bundle->root_data);
    bundle->root_data = NULL;
    for (size_t i = 0; i < bundle->dependency_count; ++i)
        gltf_preload_dependency_clear(&bundle->dependencies[i]);
    free(bundle->dependencies);
    free(bundle);
}

size_t rt_gltf_preload_bundle_dependency_count(const rt_gltf_preload_bundle *bundle) {
    return bundle ? bundle->dependency_count : 0u;
}

size_t rt_gltf_preload_bundle_decoded_image_count(const rt_gltf_preload_bundle *bundle) {
    size_t count = 0;
    if (!bundle)
        return 0u;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        if (bundle->dependencies[i].kind == GLTF_PRELOAD_DEP_IMAGE_RGBA)
            count++;
    }
    return count;
}

size_t rt_gltf_preload_bundle_decoded_image_bytes(const rt_gltf_preload_bundle *bundle) {
    size_t bytes = 0;
    if (!bundle)
        return 0u;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        if (bundle->dependencies[i].kind == GLTF_PRELOAD_DEP_IMAGE_RGBA) {
            size_t len = bundle->dependencies[i].len;
            size_t prepared = bundle->dependencies[i].prepared;
            if (len > 12u)
                len -= 12u;
            else
                len = 0u;
            if (prepared < len)
                len -= prepared;
            else
                len = 0u;
            if (SIZE_MAX - bytes < len)
                return SIZE_MAX;
            bytes += len;
        }
    }
    return bytes;
}

static int gltf_preload_rgba_blob_pixel_bytes(const uint8_t *blob,
                                              size_t len,
                                              uint32_t *out_width,
                                              uint32_t *out_height,
                                              size_t *out_pixel_bytes) {
    uint32_t width;
    uint32_t height;
    size_t pixel_count;
    size_t pixel_bytes;
    if (out_width)
        *out_width = 0;
    if (out_height)
        *out_height = 0;
    if (out_pixel_bytes)
        *out_pixel_bytes = 0;
    if (!blob || len < 12u)
        return 0;
    if (blob[0] != 'V' || blob[1] != 'G' || blob[2] != 'R' || blob[3] != 'A')
        return 0;
    width = gltf_read_u32_le(blob + 4);
    height = gltf_read_u32_le(blob + 8);
    if (width == 0 || height == 0)
        return 0;
    if (!gltf_checked_mul_size((size_t)width, (size_t)height, &pixel_count) ||
        !gltf_checked_mul_size(pixel_count, 4u, &pixel_bytes) ||
        pixel_bytes > len - 12u)
        return 0;
    if (out_width)
        *out_width = width;
    if (out_height)
        *out_height = height;
    if (out_pixel_bytes)
        *out_pixel_bytes = pixel_bytes;
    return 1;
}

size_t rt_gltf_preload_bundle_next_decoded_image_slice_bytes(const rt_gltf_preload_bundle *bundle,
                                                             size_t max_bytes) {
    if (!bundle || max_bytes == 0u)
        return 0u;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        const gltf_preload_dependency_t *dep = &bundle->dependencies[i];
        size_t pixel_bytes;
        size_t remaining;
        size_t slice;
        if (dep->kind != GLTF_PRELOAD_DEP_IMAGE_RGBA)
            continue;
        if (!gltf_preload_rgba_blob_pixel_bytes(dep->data, dep->len, NULL, NULL, &pixel_bytes))
            return 0u;
        if (dep->prepared >= pixel_bytes)
            continue;
        remaining = pixel_bytes - dep->prepared;
        slice = remaining < max_bytes ? remaining : max_bytes;
        if (slice < 4u)
            slice = remaining < 4u ? remaining : 4u;
        return slice - (slice % 4u);
    }
    return 0u;
}

size_t rt_gltf_preload_bundle_prepare_decoded_image_slice(rt_gltf_preload_bundle *bundle,
                                                          size_t max_bytes) {
    if (!bundle || max_bytes == 0u)
        return 0u;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        gltf_preload_dependency_t *dep = &bundle->dependencies[i];
        uint32_t width;
        uint32_t height;
        size_t pixel_bytes;
        size_t remaining;
        size_t copy_bytes;
        size_t first_pixel;
        size_t copy_pixels;
        rt_pixels_impl *pixels;
        if (dep->kind != GLTF_PRELOAD_DEP_IMAGE_RGBA)
            continue;
        if (!gltf_preload_rgba_blob_pixel_bytes(dep->data,
                                                dep->len,
                                                &width,
                                                &height,
                                                &pixel_bytes))
            return 0u;
        if (dep->prepared >= pixel_bytes) {
            dep->prepared = pixel_bytes;
            continue;
        }
        if (!dep->object) {
            dep->object = pixels_alloc((int64_t)width, (int64_t)height);
            if (!dep->object)
                return 0u;
        }
        remaining = pixel_bytes - dep->prepared;
        copy_bytes = remaining < max_bytes ? remaining : max_bytes;
        if (copy_bytes < 4u)
            copy_bytes = remaining < 4u ? remaining : 4u;
        copy_bytes -= copy_bytes % 4u;
        if (copy_bytes == 0u)
            return 0u;
        first_pixel = dep->prepared / 4u;
        copy_pixels = copy_bytes / 4u;
        pixels = (rt_pixels_impl *)dep->object;
        for (size_t pi = 0; pi < copy_pixels; ++pi) {
            const uint8_t *sp = dep->data + 12u + dep->prepared + pi * 4u;
            pixels->data[first_pixel + pi] = ((uint32_t)sp[0] << 24) |
                                             ((uint32_t)sp[1] << 16) |
                                             ((uint32_t)sp[2] << 8) | (uint32_t)sp[3];
        }
        dep->prepared += copy_bytes;
        if (dep->prepared >= pixel_bytes) {
            pixels_touch(pixels);
            free(dep->data);
            dep->data = NULL;
            dep->len = 0u;
            dep->prepared = 0u;
            dep->kind = GLTF_PRELOAD_DEP_IMAGE_PIXELS;
        }
        return copy_bytes;
    }
    return 0u;
}

size_t rt_gltf_preload_bundle_decoded_mesh_count(const rt_gltf_preload_bundle *bundle) {
    size_t count = 0;
    if (!bundle)
        return 0u;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        if (bundle->dependencies[i].kind == GLTF_PRELOAD_DEP_MESH_POD)
            count++;
    }
    return count;
}

static int gltf_preload_bundle_add_dependency(rt_gltf_preload_bundle *bundle,
                                              const char *path,
                                              gltf_preload_dependency_kind_t kind,
                                              uint8_t *data,
                                              size_t len) {
    gltf_preload_dependency_t *grown;
    char *path_copy;
    if (!bundle || !path || !data)
        return 0;
    if (bundle->dependency_count == bundle->dependency_capacity) {
        size_t next_capacity = bundle->dependency_capacity ? bundle->dependency_capacity * 2u : 8u;
        if (next_capacity < bundle->dependency_count)
            return 0;
        grown = (gltf_preload_dependency_t *)realloc(
            bundle->dependencies, next_capacity * sizeof(*bundle->dependencies));
        if (!grown)
            return 0;
        bundle->dependencies = grown;
        bundle->dependency_capacity = next_capacity;
    }
    path_copy = gltf_strdup_cstr(path);
    if (!path_copy)
        return 0;
    bundle->dependencies[bundle->dependency_count].path = path_copy;
    bundle->dependencies[bundle->dependency_count].data = data;
    bundle->dependencies[bundle->dependency_count].object = NULL;
    bundle->dependencies[bundle->dependency_count].len = len;
    bundle->dependencies[bundle->dependency_count].prepared = 0u;
    bundle->dependencies[bundle->dependency_count].kind = kind;
    bundle->dependency_count++;
    return 1;
}

static uint8_t *gltf_preload_bundle_take_dependency(rt_gltf_preload_bundle *bundle,
                                                    const char *path,
                                                    gltf_preload_dependency_kind_t kind,
                                                    size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!bundle || !path)
        return NULL;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        gltf_preload_dependency_t *dep = &bundle->dependencies[i];
        if (dep->kind == kind && dep->path && strcmp(dep->path, path) == 0) {
            uint8_t *data = dep->data;
            if (out_len)
                *out_len = dep->len;
            free(dep->path);
            if (i + 1u < bundle->dependency_count) {
                memmove(dep,
                        dep + 1u,
                        (bundle->dependency_count - i - 1u) * sizeof(*bundle->dependencies));
            }
            bundle->dependency_count--;
            memset(&bundle->dependencies[bundle->dependency_count],
                   0,
                   sizeof(*bundle->dependencies));
            return data;
        }
    }
    return NULL;
}

static void *gltf_preload_bundle_take_object_dependency(rt_gltf_preload_bundle *bundle,
                                                        const char *path,
                                                        gltf_preload_dependency_kind_t kind) {
    if (!bundle || !path)
        return NULL;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        gltf_preload_dependency_t *dep = &bundle->dependencies[i];
        if (dep->kind == kind && dep->path && strcmp(dep->path, path) == 0) {
            void *object = dep->object;
            free(dep->path);
            free(dep->data);
            dep->object = NULL;
            if (i + 1u < bundle->dependency_count) {
                memmove(dep,
                        dep + 1u,
                        (bundle->dependency_count - i - 1u) * sizeof(*bundle->dependencies));
            }
            bundle->dependency_count--;
            memset(&bundle->dependencies[bundle->dependency_count],
                   0,
                   sizeof(*bundle->dependencies));
            return object;
        }
    }
    return NULL;
}

/// @brief Read a little-endian uint32 from an unaligned byte pointer.
/// @details Uses individual byte loads and explicit shifts to avoid undefined behaviour
///   from unaligned access on strict-alignment platforms. Used for parsing the 12-byte
///   GLB (binary glTF) header and chunk-header magic/length fields.
static uint32_t gltf_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Overflow-safe size_t addition: sets *out = a+b and returns 1, or returns 0 on overflow.
/// @details Guards all byte-range arithmetic in the accessor/buffer-view bounds checks so that
///   a malformed glTF with exaggerated offsets or lengths can't wrap around to a valid-looking
///   in-bounds address. Returns 0 also when @p out is NULL.
static int gltf_checked_add_size(size_t a, size_t b, size_t *out) {
    if (!out || a > SIZE_MAX - b)
        return 0;
    *out = a + b;
    return 1;
}

/// @brief Overflow-safe size_t multiplication: sets *out = a*b and returns 1, or returns 0 on
/// overflow.
/// @details Uses the `b > SIZE_MAX / a` guard (avoiding the multiply itself when `a != 0`) to
///   detect the overflow before it happens. Used alongside `gltf_checked_add_size` to
///   safely compute `stride * count` and `comp_size * comp_count` bounds checks.
static int gltf_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (!out)
        return 0;
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

static uint16_t gltf_read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int32_t gltf_read_i32_le(const uint8_t *p) {
    return (int32_t)gltf_read_u32_le(p);
}

static void gltf_write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static float gltf_read_f32_le(const uint8_t *p) {
    uint32_t bits = gltf_read_u32_le(p);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void gltf_write_f32_le(uint8_t *p, float value) {
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    gltf_write_u32_le(p, bits);
}

static int gltf_ascii_has_token_i(const char *text, const char *needle) {
    size_t needle_len;
    if (!text || !needle)
        return 0;
    needle_len = strlen(needle);
    if (needle_len == 0)
        return 1;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (gltf_ascii_ieq_n(text + i, needle, needle_len))
            return 1;
    }
    return 0;
}

static int gltf_preload_image_is_bmp(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/bmp") ||
        gltf_ascii_has_token_i(mime_or_name, "image/x-ms-bmp"))
        return 1;
    len = strlen(mime_or_name);
    return len >= 4u && gltf_ascii_ieq_n(mime_or_name + len - 4u, ".bmp", 4u);
}

static int gltf_preload_image_is_png(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/png"))
        return 1;
    len = strlen(mime_or_name);
    return len >= 4u && gltf_ascii_ieq_n(mime_or_name + len - 4u, ".png", 4u);
}

static int gltf_preload_image_is_jpeg(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/jpeg") ||
        gltf_ascii_has_token_i(mime_or_name, "image/jpg"))
        return 1;
    len = strlen(mime_or_name);
    return (len >= 4u && gltf_ascii_ieq_n(mime_or_name + len - 4u, ".jpg", 4u)) ||
           (len >= 5u && gltf_ascii_ieq_n(mime_or_name + len - 5u, ".jpeg", 5u));
}

static int gltf_preload_image_is_gif(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/gif"))
        return 1;
    len = strlen(mime_or_name);
    return len >= 4u && gltf_ascii_ieq_n(mime_or_name + len - 4u, ".gif", 4u);
}

static int gltf_preload_image_is_supported_format(const char *mime_or_name) {
    return gltf_preload_image_is_png(mime_or_name) ||
           gltf_preload_image_is_bmp(mime_or_name) ||
           gltf_preload_image_is_jpeg(mime_or_name) ||
           gltf_preload_image_is_gif(mime_or_name);
}

static int gltf_decode_bmp_to_rgba_blob(const uint8_t *data,
                                        size_t len,
                                        uint8_t **out_blob,
                                        size_t *out_len) {
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t width_i;
    int32_t height_i;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t width;
    uint32_t height;
    size_t row_stride;
    size_t image_rows;
    size_t pixel_bytes;
    size_t blob_len;
    uint8_t *blob;
    int top_down = 0;

    if (out_blob)
        *out_blob = NULL;
    if (out_len)
        *out_len = 0;
    if (!data || len < 54u || !out_blob || !out_len)
        return 0;
    if (data[0] != 'B' || data[1] != 'M')
        return 0;

    pixel_offset = gltf_read_u32_le(data + 10);
    dib_size = gltf_read_u32_le(data + 14);
    if (dib_size < 40u || pixel_offset >= len)
        return 0;
    width_i = gltf_read_i32_le(data + 18);
    height_i = gltf_read_i32_le(data + 22);
    planes = gltf_read_u16_le(data + 26);
    bit_count = gltf_read_u16_le(data + 28);
    compression = gltf_read_u32_le(data + 30);
    if (width_i <= 0 || height_i == 0 || planes != 1 || compression != 0 ||
        (bit_count != 24 && bit_count != 32))
        return 0;
    if (height_i < 0) {
        if (height_i == INT32_MIN)
            return 0;
        top_down = 1;
        height_i = -height_i;
    }
    width = (uint32_t)width_i;
    height = (uint32_t)height_i;
    if (!gltf_checked_mul_size((size_t)width, (size_t)bit_count, &row_stride))
        return 0;
    if (!gltf_checked_add_size(row_stride, 31u, &row_stride))
        return 0;
    row_stride = (row_stride / 32u) * 4u;
    if (!gltf_checked_mul_size(row_stride, (size_t)height, &image_rows))
        return 0;
    if ((size_t)pixel_offset > len || image_rows > len - (size_t)pixel_offset)
        return 0;
    if (!gltf_checked_mul_size((size_t)width, (size_t)height, &pixel_bytes) ||
        !gltf_checked_mul_size(pixel_bytes, 4u, &pixel_bytes) ||
        !gltf_checked_add_size(12u, pixel_bytes, &blob_len))
        return 0;
    blob = (uint8_t *)malloc(blob_len);
    if (!blob)
        return 0;
    blob[0] = 'V';
    blob[1] = 'G';
    blob[2] = 'R';
    blob[3] = 'A';
    gltf_write_u32_le(blob + 4, width);
    gltf_write_u32_le(blob + 8, height);
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t src_y = top_down ? y : (height - 1u - y);
        const uint8_t *row = data + (size_t)pixel_offset + (size_t)src_y * row_stride;
        uint8_t *dst = blob + 12u + (size_t)y * (size_t)width * 4u;
        for (uint32_t x = 0; x < width; ++x) {
            if (bit_count == 32) {
                const uint8_t *sp = row + (size_t)x * 4u;
                dst[x * 4u + 0u] = sp[2];
                dst[x * 4u + 1u] = sp[1];
                dst[x * 4u + 2u] = sp[0];
                dst[x * 4u + 3u] = sp[3];
            } else {
                const uint8_t *sp = row + (size_t)x * 3u;
                dst[x * 4u + 0u] = sp[2];
                dst[x * 4u + 1u] = sp[1];
                dst[x * 4u + 2u] = sp[0];
                dst[x * 4u + 3u] = 0xFFu;
            }
        }
    }
    *out_blob = blob;
    *out_len = blob_len;
    return 1;
}

static int gltf_rgba32_to_rgba_blob(const uint32_t *pixels,
                                    int64_t width_i,
                                    int64_t height_i,
                                    uint8_t **out_blob,
                                    size_t *out_len) {
    uint32_t width;
    uint32_t height;
    size_t pixel_count;
    size_t pixel_bytes;
    size_t blob_len;
    uint8_t *blob;
    if (out_blob)
        *out_blob = NULL;
    if (out_len)
        *out_len = 0;
    if (!pixels || !out_blob || !out_len || width_i <= 0 || height_i <= 0 ||
        (uint64_t)width_i > UINT32_MAX || (uint64_t)height_i > UINT32_MAX)
        return 0;
    width = (uint32_t)width_i;
    height = (uint32_t)height_i;
    if (!gltf_checked_mul_size((size_t)width, (size_t)height, &pixel_count) ||
        !gltf_checked_mul_size(pixel_count, 4u, &pixel_bytes) ||
        !gltf_checked_add_size(12u, pixel_bytes, &blob_len))
        return 0;
    blob = (uint8_t *)malloc(blob_len);
    if (!blob)
        return 0;
    blob[0] = 'V';
    blob[1] = 'G';
    blob[2] = 'R';
    blob[3] = 'A';
    gltf_write_u32_le(blob + 4, width);
    gltf_write_u32_le(blob + 8, height);
    for (size_t i = 0; i < pixel_count; ++i) {
        uint32_t rgba = pixels[i];
        uint8_t *dst = blob + 12u + i * 4u;
        dst[0] = (uint8_t)((rgba >> 24) & 0xFFu);
        dst[1] = (uint8_t)((rgba >> 16) & 0xFFu);
        dst[2] = (uint8_t)((rgba >> 8) & 0xFFu);
        dst[3] = (uint8_t)(rgba & 0xFFu);
    }
    *out_blob = blob;
    *out_len = blob_len;
    return 1;
}

static int gltf_decode_image_payload_to_rgba_blob(const char *mime_or_name,
                                                  const uint8_t *data,
                                                  size_t data_len,
                                                  uint8_t **out_blob,
                                                  size_t *out_len) {
    if (out_blob)
        *out_blob = NULL;
    if (out_len)
        *out_len = 0;
    if (!mime_or_name || !data || data_len == 0 || !out_blob || !out_len)
        return 0;
    if (gltf_preload_image_is_png(mime_or_name)) {
        uint32_t *rgba32 = NULL;
        int64_t width = 0;
        int64_t height = 0;
        int ok = 0;
        if (rt_png_decode_buffer_rgba32(data, data_len, &rgba32, &width, &height))
            ok = gltf_rgba32_to_rgba_blob(rgba32, width, height, out_blob, out_len);
        free(rgba32);
        return ok;
    }
    if (gltf_preload_image_is_bmp(mime_or_name))
        return gltf_decode_bmp_to_rgba_blob(data, data_len, out_blob, out_len);
    if (gltf_preload_image_is_jpeg(mime_or_name)) {
        uint32_t *rgba32 = NULL;
        int64_t width = 0;
        int64_t height = 0;
        int ok = 0;
        if (rt_jpeg_decode_buffer_rgba32(data, data_len, &rgba32, &width, &height))
            ok = gltf_rgba32_to_rgba_blob(rgba32, width, height, out_blob, out_len);
        free(rgba32);
        return ok;
    }
    if (gltf_preload_image_is_gif(mime_or_name)) {
        uint32_t *rgba32 = NULL;
        int width = 0;
        int height = 0;
        int ok = 0;
        if (rt_gif_decode_memory_first_rgba32(data, data_len, &rgba32, &width, &height))
            ok = gltf_rgba32_to_rgba_blob(
                rgba32, (int64_t)width, (int64_t)height, out_blob, out_len);
        free(rgba32);
        return ok;
    }
    return 0;
}

static void *gltf_pixels_from_rgba_blob(uint8_t *blob, size_t len) {
    uint32_t width;
    uint32_t height;
    size_t pixel_count;
    size_t pixel_bytes;
    rt_pixels_impl *pixels;
    const uint8_t *src;
    if (!blob || len < 12u)
        return NULL;
    if (blob[0] != 'V' || blob[1] != 'G' || blob[2] != 'R' || blob[3] != 'A')
        return NULL;
    width = gltf_read_u32_le(blob + 4);
    height = gltf_read_u32_le(blob + 8);
    if (width == 0 || height == 0)
        return NULL;
    if (!gltf_checked_mul_size((size_t)width, (size_t)height, &pixel_count) ||
        !gltf_checked_mul_size(pixel_count, 4u, &pixel_bytes) ||
        pixel_bytes > len - 12u)
        return NULL;
    pixels = pixels_alloc((int64_t)width, (int64_t)height);
    if (!pixels)
        return NULL;
    src = blob + 12u;
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint8_t *sp = src + i * 4u;
        pixels->data[i] = ((uint32_t)sp[0] << 24) | ((uint32_t)sp[1] << 16) |
                          ((uint32_t)sp[2] << 8) | (uint32_t)sp[3];
    }
    if (pixel_count > 0)
        pixels_touch(pixels);
    return pixels;
}

/// @brief Read a non-negative integer field from a JSON object as a `size_t`.
/// @details Calls `jint` with the supplied default, then rejects negative values (which indicate
///   a malformed glTF or missing field) by returning 0. Used for `byteOffset`, `byteLength`,
///   and `byteStride` fields where negative values would cause pointer arithmetic to underflow.
/// @param obj  JSON object to read from.
/// @param key  Field name.
/// @param def  Default `size_t` value when the field is absent.
/// @param out  Receives the validated value; not written on failure.
/// @return 1 on success, 0 if the value is negative or @p out is NULL.
static int gltf_nonnegative_size(void *obj, const char *key, size_t def, size_t *out) {
    int64_t raw;
    if (!out)
        return 0;
    raw = jint(obj, key, (int64_t)def);
    if (raw < 0)
        return 0;
    *out = (size_t)raw;
    return 1;
}

/// @brief Validate that a decoded URI is a safe relative file path with no directory traversal.
/// @details Rejects URIs that are absolute paths (`/` or `\` prefix), Windows drive paths
///   (`X:` prefix), URLs with a scheme (`://`), and any path containing `.` or `..` segments.
///   Only forward-slash and backslash are treated as separators. This guards against
///   malicious glTF files that try to load assets from outside the intended directory by
///   embedding paths like `../../etc/passwd` or `file:///C:/Windows/System32/...`.
/// @param decoded_uri  Percent-decoded URI string (the raw value from the glTF JSON, already
///                     URL-decoded by the caller).
/// @return 1 if the URI is a safe relative path, 0 if it should be rejected.
static int gltf_safe_relative_uri(const char *decoded_uri) {
    const char *p;
    const char *seg;
    size_t seg_len;
    if (!decoded_uri || decoded_uri[0] == '\0')
        return 0;
    if (decoded_uri[0] == '/' || decoded_uri[0] == '\\')
        return 0;
    if ((decoded_uri[0] && decoded_uri[1] == ':') || strstr(decoded_uri, "://"))
        return 0;
    p = decoded_uri;
    while (*p) {
        while (*p == '/' || *p == '\\')
            p++;
        seg = p;
        while (*p && *p != '/' && *p != '\\')
            p++;
        seg_len = (size_t)(p - seg);
        if ((seg_len == 1 && seg[0] == '.') || (seg_len == 2 && seg[0] == '.' && seg[1] == '.'))
            return 0;
    }
    return 1;
}

static const char gltf_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int gltf_data_uri_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

static int gltf_ascii_isspace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

static int gltf_ascii_ieq_n(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return 0;
    }
    return 1;
}

static int gltf_data_uri_meta_has_base64(const char *meta, size_t len) {
    size_t start = 0;
    while (start < len) {
        size_t end = start;
        while (end < len && meta[end] != ';')
            end++;
        if (end > start && end - start == 6 && gltf_ascii_ieq_n(meta + start, "base64", 6))
            return 1;
        start = end + 1;
    }
    return 0;
}

static int gltf_percent_decode_bytes(const char *src,
                                     size_t len,
                                     uint8_t **out_data,
                                     size_t *out_len) {
    uint8_t *decoded;
    size_t write = 0;
    if (out_data)
        *out_data = NULL;
    if (out_len)
        *out_len = 0;
    if (!src || !out_data || !out_len)
        return 0;
    decoded = (uint8_t *)malloc(len > 0 ? len : 1);
    if (!decoded)
        return 0;
    for (size_t read = 0; read < len; ++read) {
        if (src[read] == '%' && read + 2 < len) {
            int hi = gltf_data_uri_hex_digit(src[read + 1]);
            int lo = gltf_data_uri_hex_digit(src[read + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded[write++] = (uint8_t)((hi << 4) | lo);
                read += 2;
                continue;
            }
        }
        decoded[write++] = (uint8_t)src[read];
    }
    *out_data = decoded;
    *out_len = write;
    return 1;
}

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
    char *compact = NULL;
    const char *src;
    size_t compact_len = 0;
    if (!data)
        return NULL;
    for (size_t i = 0; i < len; ++i) {
        if (!gltf_ascii_isspace(data[i]))
            compact_len++;
    }
    if (compact_len != len) {
        compact = (char *)malloc(compact_len > 0 ? compact_len : 1);
        if (!compact)
            return NULL;
        compact_len = 0;
        for (size_t i = 0; i < len; ++i) {
            if (!gltf_ascii_isspace(data[i]))
                compact[compact_len++] = data[i];
        }
        src = compact;
    } else {
        src = data;
    }
    len = compact_len;
    if (len == 0) {
        uint8_t *empty = (uint8_t *)malloc(1);
        if (out_len)
            *out_len = 0;
        free(compact);
        return empty;
    }
    if (len % 4 != 0) {
        free(compact);
        return NULL;
    }

    size_t olen = (len / 4) * 3;
    if (len > 0 && src[len - 1] == '=')
        olen--;
    if (len > 1 && src[len - 2] == '=')
        olen--;

    uint8_t *output = (uint8_t *)malloc(olen > 0 ? olen : 1);
    if (!output) {
        free(compact);
        return NULL;
    }

    size_t i = 0;
    size_t j = 0;
    while (i < len) {
        int a = gltf_base64_digit_value(src[i++]);
        int b = gltf_base64_digit_value(src[i++]);
        int c = gltf_base64_digit_value(src[i++]);
        int d = gltf_base64_digit_value(src[i++]);
        if (a < 0 || b < 0 || c == -1 || d == -1) {
            free(output);
            free(compact);
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
    free(compact);
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
    uint8_t *decoded_payload = NULL;
    size_t decoded_payload_len = 0;
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
        size_t meta_len = (size_t)(comma - meta);
        const char *semi = memchr(meta, ';', meta_len);
        if (semi) {
            mime_len = (size_t)(semi - meta);
            is_base64 = gltf_data_uri_meta_has_base64(meta, meta_len);
        } else {
            mime_len = meta_len;
        }
        if (mime_buf && mime_buf_cap > 0 && mime_len > 0) {
            if (mime_len >= mime_buf_cap)
                mime_len = mime_buf_cap - 1;
            memcpy(mime_buf, meta, mime_len);
            mime_buf[mime_len] = '\0';
        }
    }

    if (!gltf_percent_decode_bytes(payload, strlen(payload), &decoded_payload, &decoded_payload_len))
        return 0;
    if (is_base64) {
        *out_data = gltf_base64_decode((const char *)decoded_payload, decoded_payload_len, out_len);
        free(decoded_payload);
        return *out_data != NULL;
    }

    *out_data = decoded_payload;
    *out_len = decoded_payload_len;
    return 1;
}

static void gltf_data_uri_copy_mime(const char *uri, char *mime_buf, size_t mime_buf_cap) {
    const char *comma;
    const char *meta;
    const char *semi;
    size_t meta_len;
    size_t mime_len;
    if (mime_buf && mime_buf_cap > 0)
        mime_buf[0] = '\0';
    if (!uri || strncmp(uri, "data:", 5) != 0 || !mime_buf || mime_buf_cap == 0)
        return;
    comma = strchr(uri, ',');
    if (!comma)
        return;
    meta = uri + 5;
    meta_len = (size_t)(comma - meta);
    semi = memchr(meta, ';', meta_len);
    mime_len = semi ? (size_t)(semi - meta) : meta_len;
    if (mime_len == 0)
        return;
    if (mime_len >= mime_buf_cap)
        mime_len = mime_buf_cap - 1u;
    memcpy(mime_buf, meta, mime_len);
    mime_buf[mime_len] = '\0';
}

/// @brief Resolve a buffer-view index to its `(data, length)` slice into the underlying buffer.
///
/// glTF separates buffers (raw bytes) from bufferViews (named
/// offset+length slices). Most accessors reference data through a
/// bufferView, so this helper centralises the indirection.
static const uint8_t *gltf_get_buffer_view_data(
    void *root, int64_t view_idx, gltf_buffer_t *buffers, int buf_count, size_t *out_len) {
    void *views = jarr(root, "bufferViews");
    size_t byte_offset;
    size_t byte_length;
    size_t byte_end;
    if (!views || view_idx < 0 || view_idx >= jarr_len(views))
        return NULL;
    void *view = rt_seq_get(views, view_idx);
    if (!view)
        return NULL;
    int buf_idx = (int)jint(view, "buffer", -1);
    if (buf_idx < 0 || buf_idx >= buf_count)
        return NULL;
    if (!gltf_nonnegative_size(view, "byteOffset", 0, &byte_offset) ||
        !gltf_nonnegative_size(view, "byteLength", 0, &byte_length))
        return NULL;
    if (!gltf_checked_add_size(byte_offset, byte_length, &byte_end))
        return NULL;
    if (byte_end > buffers[buf_idx].len)
        return NULL;
    if (out_len)
        *out_len = byte_length;
    return buffers[buf_idx].data + byte_offset;
}

/// @brief Return the byte size of one glTF accessor component by its GL-enum component type.
/// @details glTF uses the original GL integer constants for component types:
///   5120 = BYTE (1), 5121 = UNSIGNED_BYTE (1), 5122 = SHORT (2), 5123 = UNSIGNED_SHORT (2),
///   5125 = UNSIGNED_INT (4), 5126 = FLOAT (4). Returns 0 for unrecognised values so callers
///   can treat 0 as an error sentinel.
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

/// @brief Return the number of components for a glTF accessor `"type"` string.
/// @details Maps the glTF type name to its component count: "SCALAR"=1, "VEC2"=2, "VEC3"=3,
///   "VEC4"=4, "MAT2"=4, "MAT3"=9, "MAT4"=16. Unrecognised strings return 1 as a safe
///   fallback for optional data this loader does not consume.
static int gltf_component_count(const char *acc_type) {
    if (!acc_type)
        return 1;
    if (strcmp(acc_type, "VEC2") == 0)
        return 2;
    if (strcmp(acc_type, "VEC3") == 0)
        return 3;
    if (strcmp(acc_type, "VEC4") == 0)
        return 4;
    if (strcmp(acc_type, "MAT2") == 0)
        return 4;
    if (strcmp(acc_type, "MAT3") == 0)
        return 9;
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
    int comp_size;
    int comp_count;
    const char *acc_type;
    void *acc;
    int64_t bv_idx;
    size_t byte_offset_acc;
    int comp_type;
    int64_t count_raw;
    if (!accessors || accessor_idx < 0 || accessor_idx >= jarr_len(accessors))
        return 0;
    acc = rt_seq_get(accessors, accessor_idx);
    if (!acc)
        return 0;

    memset(out, 0, sizeof(*out));
    count_raw = jint(acc, "count", 0);
    if (count_raw <= 0 || count_raw > INT32_MAX)
        return 0;
    out->count = (int32_t)count_raw;
    bv_idx = jint(acc, "bufferView", -1);
    if (!gltf_nonnegative_size(acc, "byteOffset", 0, &byte_offset_acc))
        return 0;
    comp_type = (int)jint(acc, "componentType", 5126);
    comp_size = gltf_component_size(comp_type);
    acc_type = jstr(acc, "type");
    comp_count = gltf_component_count(acc_type);
    if (comp_size <= 0 || comp_count <= 0)
        return 0;

    out->stride = comp_size * comp_count;
    out->comp_type = comp_type;
    out->comp_count = comp_count;
    out->normalized = jint(acc, "normalized", 0) ? 1 : 0;

    if (bv_idx >= 0) {
        void *views = jarr(root, "bufferViews");
        void *bv;
        int buf_idx;
        size_t byte_offset_bv;
        size_t byte_stride;
        size_t elem_size;
        size_t offset;
        size_t last_offset;
        size_t last_span;
        size_t required_len;
        if (!views || bv_idx >= jarr_len(views))
            return 0;
        bv = rt_seq_get(views, (int64_t)bv_idx);
        if (!bv)
            return 0;
        buf_idx = (int)jint(bv, "buffer", 0);
        if (!gltf_nonnegative_size(bv, "byteOffset", 0, &byte_offset_bv) ||
            !gltf_nonnegative_size(bv, "byteStride", 0, &byte_stride))
            return 0;
        if (buf_idx < 0 || buf_idx >= buf_count)
            return 0;

        elem_size = (size_t)comp_size * (size_t)comp_count;
        if (byte_stride > 0) {
            if (byte_stride < elem_size || byte_stride > INT32_MAX)
                return 0;
            out->stride = (int32_t)byte_stride;
        } else {
            if (elem_size > INT32_MAX)
                return 0;
            out->stride = (int32_t)elem_size;
        }
        if (!gltf_checked_add_size(byte_offset_bv, byte_offset_acc, &offset))
            return 0;
        if (offset > buffers[buf_idx].len)
            return 0;
        if (!gltf_checked_mul_size((size_t)(out->count - 1), (size_t)out->stride, &last_offset) ||
            !gltf_checked_add_size(offset, last_offset, &last_span) ||
            !gltf_checked_add_size(last_span, elem_size, &required_len))
            return 0;
        if (required_len > buffers[buf_idx].len)
            return 0;
        out->data = buffers[buf_idx].data + offset;
    }

    {
        void *sparse = jget(acc, "sparse");
        int64_t sparse_count_raw = sparse ? jint(sparse, "count", 0) : 0;
        int32_t sparse_count =
            (sparse_count_raw > 0 && sparse_count_raw <= INT32_MAX) ? (int32_t)sparse_count_raw : 0;
        if (sparse && sparse_count > 0) {
            void *indices = jget(sparse, "indices");
            void *values = jget(sparse, "values");
            int64_t indices_view = jint(indices, "bufferView", -1);
            int64_t values_view = jint(values, "bufferView", -1);
            size_t indices_offset;
            size_t values_offset;
            int index_comp_type = (int)jint(indices, "componentType", 0);
            int index_comp_size = gltf_component_size(index_comp_type);
            size_t index_len = 0;
            size_t value_len = 0;
            size_t index_bytes = 0;
            size_t value_bytes = 0;
            size_t index_end = 0;
            size_t value_end = 0;
            const uint8_t *index_data =
                gltf_get_buffer_view_data(root, indices_view, buffers, buf_count, &index_len);
            const uint8_t *value_data =
                gltf_get_buffer_view_data(root, values_view, buffers, buf_count, &value_len);
            if (!index_data || !value_data || index_comp_size <= 0)
                return 0;
            if (index_comp_type != 5121 && index_comp_type != 5123 && index_comp_type != 5125)
                return 0;
            if (!gltf_nonnegative_size(indices, "byteOffset", 0, &indices_offset) ||
                !gltf_nonnegative_size(values, "byteOffset", 0, &values_offset))
                return 0;
            if (indices_offset > index_len || values_offset > value_len)
                return 0;
            if (!gltf_checked_mul_size(
                    (size_t)sparse_count, (size_t)index_comp_size, &index_bytes) ||
                !gltf_checked_add_size(indices_offset, index_bytes, &index_end) ||
                index_end > index_len)
                return 0;
            if (!gltf_checked_mul_size(
                    (size_t)sparse_count, (size_t)comp_size * (size_t)comp_count, &value_bytes) ||
                !gltf_checked_add_size(values_offset, value_bytes, &value_end) ||
                value_end > value_len)
                return 0;
            out->sparse_indices = index_data + indices_offset;
            out->sparse_values = value_data + values_offset;
            out->sparse_count = sparse_count;
            out->sparse_index_comp_type = index_comp_type;
            out->sparse_index_stride = index_comp_size;
            out->sparse_value_stride = comp_size * comp_count;
        }
    }
    return 1;
}

/// @brief Decode one glTF accessor component from raw bytes to a float.
/// @details Handles all six glTF component types. For integer types, when @p normalized is
///   non-zero, the value is linearly mapped to [−1, 1] (signed) or [0, 1] (unsigned) per the
///   glTF spec §3.6.2.4:
///     - INT8:  max(value/127, −1) to preserve the symmetric range at the cost of −128 → −1.
///     - UINT8:  value/255.
///     - INT16:  max(value/32767, −1).
///     - UINT16: value/65535.
///     - UINT32: value/4294967295 (double precision to avoid float precision loss).
///     - FLOAT:  returned as-is regardless of the normalized flag.
///   Used when reading attribute accessors (positions, normals, UVs, joint weights, etc.).
/// @param src        Pointer to the first byte of the component in the buffer.
/// @param comp_type  glTF component type integer (5120–5126).
/// @param normalized Non-zero to apply normalization; 0 to return the raw numeric value.
/// @return The decoded float value, or 0.0f for unknown component types.
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

/// @brief Decode one glTF accessor component from raw bytes to a uint32.
/// @details Used for reading index accessors (SCALAR UNSIGNED_INT/SHORT/BYTE) and joint-index
///   accessors (UNSIGNED_BYTE, UNSIGNED_SHORT). Signed integer types clamp negative values to 0
///   so that malformed data can't produce garbage joint indices or out-of-range triangle indices.
///   FLOAT is converted by truncation toward zero with negative values clamped to 0. Normalization
///   is never applied for uint32 reads — the raw integer value is always returned.
/// @param src        Pointer to the first byte of the component.
/// @param comp_type  glTF component type integer (5120–5126).
/// @return Raw unsigned integer value, or 0u for unknown component types.
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

/// @brief Binary-search the accessor's sparse-index array for @p element_idx.
/// @details glTF sparse accessors store a sorted list of (index, value) pairs that override
///   specific elements in the base buffer. To resolve element N: first check the base buffer
///   data (if present), then binary-search for N in the sparse indices array; if found, the
///   corresponding sparse value overrides the base. This function performs the binary search
///   and returns the position in the sparse array, not the element value itself.
///   Returns -1 when @p element_idx is not among the sparse overrides (i.e. the base data applies).
/// @param view         Accessor view with a non-NULL sparse_indices pointer.
/// @param element_idx  Zero-based logical element index to look up.
/// @return Sparse array slot (≥0) if found, -1 if not overridden.
static int32_t gltf_accessor_sparse_value_index(const gltf_accessor_view_t *view,
                                                int32_t element_idx) {
    int32_t lo;
    int32_t hi;
    if (!view || !view->sparse_indices || view->sparse_count <= 0)
        return -1;
    lo = 0;
    hi = view->sparse_count - 1;
    while (lo <= hi) {
        int32_t i = lo + (hi - lo) / 2;
        uint32_t sparse_index = gltf_decode_component_u32(
            view->sparse_indices + (size_t)i * (size_t)view->sparse_index_stride,
            view->sparse_index_comp_type);
        if (sparse_index == (uint32_t)element_idx)
            return i;
        if (sparse_index < (uint32_t)element_idx)
            lo = i + 1;
        else
            hi = i - 1;
    }
    return -1;
}

/// @brief Read one element from an accessor view into a float array, applying sparse overrides.
/// @details First zeros @p out_components output slots, then — if the base buffer is present —
///   reads up to `min(view->comp_count, out_components)` components from the strided base data
///   via `gltf_decode_component_f32`. Finally, if a sparse override exists for @p element_idx
///   (detected by `gltf_accessor_sparse_value_index`), the sparse values are decoded over the
///   same component slots. The two-phase approach correctly handles the case where the base buffer
///   is NULL (accessor with sparse overrides only, no bufferView).
/// @param view            Accessor view to read from; may have a NULL base data pointer.
/// @param element_idx     Zero-based logical element index.
/// @param out             Output array; receives up to @p out_components floats.
/// @param out_components  Number of float slots in @p out; may be less than view->comp_count.
static void gltf_accessor_read_f32(const gltf_accessor_view_t *view,
                                   int32_t element_idx,
                                   float *out,
                                   int32_t out_components) {
    if (!out || out_components <= 0) {
        return;
    }
    for (int32_t i = 0; i < out_components; i++)
        out[i] = 0.0f;
    if (!view || element_idx < 0 || element_idx >= view->count)
        return;
    int comp_size = gltf_component_size(view->comp_type);
    if (comp_size <= 0)
        return;
    int32_t limit = view->comp_count < out_components ? view->comp_count : out_components;
    if (view->data) {
        const uint8_t *base = view->data + (size_t)element_idx * (size_t)view->stride;
        for (int32_t i = 0; i < limit; i++)
            out[i] = gltf_decode_component_f32(
                base + (size_t)i * (size_t)comp_size, view->comp_type, view->normalized);
    }
    {
        int32_t sparse_slot = gltf_accessor_sparse_value_index(view, element_idx);
        if (sparse_slot >= 0) {
            const uint8_t *sparse =
                view->sparse_values + (size_t)sparse_slot * (size_t)view->sparse_value_stride;
            for (int32_t i = 0; i < limit; i++)
                out[i] = gltf_decode_component_f32(
                    sparse + (size_t)i * (size_t)comp_size, view->comp_type, view->normalized);
        }
    }
}

/// @brief Read one element from an accessor view into a uint32 array, applying sparse overrides.
/// @details Same two-phase (base + sparse-override) strategy as `gltf_accessor_read_f32`, but
///   decodes via `gltf_decode_component_u32` instead. Used for index buffers (SCALAR UINT/USHORT/
///   UBYTE accessor type) and joint-index attributes (UNSIGNED_BYTE/UNSIGNED_SHORT VEC4).
/// @param view            Accessor view to read from.
/// @param element_idx     Zero-based logical element index.
/// @param out             Output array; receives up to @p out_components uint32 values.
/// @param out_components  Number of uint32 slots in @p out.
static void gltf_accessor_read_u32(const gltf_accessor_view_t *view,
                                   int32_t element_idx,
                                   uint32_t *out,
                                   int32_t out_components) {
    if (!out || out_components <= 0) {
        return;
    }
    for (int32_t i = 0; i < out_components; i++)
        out[i] = 0u;
    if (!view || element_idx < 0 || element_idx >= view->count)
        return;
    int comp_size = gltf_component_size(view->comp_type);
    if (comp_size <= 0)
        return;
    int32_t limit = view->comp_count < out_components ? view->comp_count : out_components;
    if (view->data) {
        const uint8_t *base = view->data + (size_t)element_idx * (size_t)view->stride;
        for (int32_t i = 0; i < limit; i++)
            out[i] =
                gltf_decode_component_u32(base + (size_t)i * (size_t)comp_size, view->comp_type);
    }
    {
        int32_t sparse_slot = gltf_accessor_sparse_value_index(view, element_idx);
        if (sparse_slot >= 0) {
            const uint8_t *sparse =
                view->sparse_values + (size_t)sparse_slot * (size_t)view->sparse_value_stride;
            for (int32_t i = 0; i < limit; i++)
                out[i] = gltf_decode_component_u32(sparse + (size_t)i * (size_t)comp_size,
                                                   view->comp_type);
        }
    }
}

/// @brief Insert a `(joint, weight)` influence into a fixed 4-slot vertex influence
///        record, keeping the top four weights.
/// @details GPU skinning palettes cap per-vertex influences at 4 slots, but glTF meshes
///          can carry more influences via JOINTS_0 + JOINTS_1 accessor pairs. This
///          helper is called once per (joint, weight) pair from every contributing
///          accessor, folding all of them into the same 4-tuple. Policy:
///            1. Matching joint already in a slot → sum weights (handles duplicate
///               joints reported across JOINTS_0 and JOINTS_1 for the same bone).
///            2. Empty slot available (weight ~= 0) → fill it.
///            3. All slots full and the incoming weight is larger than the smallest
///               slot → evict the weakest. Otherwise the incoming weight is dropped.
///          Inputs whose joint index exceeds VGFX3D_MAX_BONES or whose weight is
///          negligible (<= 1e-8 in magnitude) are silently dropped so garbage in the
///          source accessors can't corrupt the influence record.
/// @param joint Source joint index from a glTF JOINTS_* accessor.
/// @param weight Source weight from the matching WEIGHTS_* accessor.
/// @param out_joints 4-element joint-index array, mutated in place.
/// @param out_weights 4-element weight array, mutated in place.
static void gltf_add_top_joint_influence(uint32_t joint,
                                         float weight,
                                         uint32_t *out_joints,
                                         float *out_weights) {
    int replace = -1;
    if (!out_joints || !out_weights || joint >= VGFX3D_MAX_BONES || fabsf(weight) <= 1e-8f)
        return;
    for (int i = 0; i < 4; i++) {
        if (out_weights[i] > 1e-8f && out_joints[i] == joint) {
            out_weights[i] += weight;
            return;
        }
        if (replace < 0 && fabsf(out_weights[i]) <= 1e-8f)
            replace = i;
    }
    if (replace < 0) {
        replace = 0;
        for (int i = 1; i < 4; i++) {
            if (out_weights[i] < out_weights[replace])
                replace = i;
        }
        if (weight <= out_weights[replace])
            return;
    }
    out_joints[replace] = joint;
    out_weights[replace] = weight;
}

/// @brief Clamp negatives and renormalize a 4-element vertex weight tuple to sum 1.
/// @details glTF assets are *meant* to ship normalized, but some exporters produce
///          weights that sum to slightly less or more than 1 (accumulated rounding
///          error, or after this loader's own top-4 truncation throws out some
///          influence). Per-vertex weight sums that diverge from 1 cause the skinned
///          mesh to bloat or shrink, so we clean up after the accessor walk:
///            1. Any negative weight (exporter bug or bad sparse override) is clamped
///               to 0 and dropped from the sum.
///            2. Non-zero sum → scale to 1.
///            3. Sum below threshold → leave the tuple alone (a zero-weight vertex is
///               a valid "this vertex is unrigged" signal).
static void gltf_normalize_joint_influences(float *weights) {
    float sum = 0.0f;
    if (!weights)
        return;
    for (int i = 0; i < 4; i++) {
        if (weights[i] > 0.0f)
            sum += weights[i];
        else
            weights[i] = 0.0f;
    }
    if (sum > 1e-8f) {
        for (int i = 0; i < 4; i++)
            weights[i] /= sum;
    }
}

/// @brief True if vertices i0/i1/i2 form a non-degenerate triangle (positive squared
///   cross-product area); rejects out-of-range indices and collinear/zero-area triples.
static int gltf_triangle_positions_form_triangle(const rt_mesh3d *mesh,
                                                 uint32_t i0,
                                                 uint32_t i1,
                                                 uint32_t i2) {
    const float *p0;
    const float *p1;
    const float *p2;
    double e1[3];
    double e2[3];
    double nx;
    double ny;
    double nz;
    double area_sq;
    if (!mesh || !mesh->vertices || i0 >= mesh->vertex_count || i1 >= mesh->vertex_count ||
        i2 >= mesh->vertex_count)
        return 0;
    p0 = mesh->vertices[i0].pos;
    p1 = mesh->vertices[i1].pos;
    p2 = mesh->vertices[i2].pos;
    e1[0] = (double)p1[0] - (double)p0[0];
    e1[1] = (double)p1[1] - (double)p0[1];
    e1[2] = (double)p1[2] - (double)p0[2];
    e2[0] = (double)p2[0] - (double)p0[0];
    e2[1] = (double)p2[1] - (double)p0[1];
    e2[2] = (double)p2[2] - (double)p0[2];
    nx = e1[1] * e2[2] - e1[2] * e2[1];
    ny = e1[2] * e2[0] - e1[0] * e2[2];
    nz = e1[0] * e2[1] - e1[1] * e2[0];
    area_sq = nx * nx + ny * ny + nz * nz;
    return isfinite(area_sq) && area_sq > 1e-20;
}

/// @brief Validate and emit one triangle to the mesh if all three indices form an area.
/// @details Duplicate-index, collinear-position, and out-of-range triangles are silently
///   dropped. This is a safety net for glTF files that include degenerate geometry (exported by
///   tools that strip vertices but leave the index buffer unchanged). Returns 1 if the triangle
///   was emitted, 0 if it was rejected.
static int gltf_emit_triangle(
    void *mesh, uint32_t vertex_count, uint32_t i0, uint32_t i1, uint32_t i2) {
    if (i0 == i1 || i1 == i2 || i0 == i2)
        return 0;
    if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
        return 0;
    if (!gltf_triangle_positions_form_triangle((const rt_mesh3d *)mesh, i0, i1, i2))
        return 0;
    rt_mesh3d_add_triangle(mesh, (int64_t)i0, (int64_t)i1, (int64_t)i2);
    return !((rt_mesh3d *)mesh)->build_failed;
}

/// @brief Read a triangle index from an optional index accessor, or use the element position
/// directly.
/// @details When @p view is NULL the primitive has no index buffer, so the element index itself
///   is used as the vertex index (sequential triangles). This mirrors how GL_TRIANGLES works
///   with `glDrawArrays` vs `glDrawElements`.
static uint32_t gltf_read_index(const gltf_accessor_view_t *view, int32_t element_idx) {
    if (!view)
        return (uint32_t)element_idx;
    uint32_t value = 0;
    gltf_accessor_read_u32(view, element_idx, &value, 1);
    return value;
}

/// @brief Convert glTF primitive topology indices to triangles and append them to a mesh.
/// @details Handles the three glTF triangle-topology modes:
///   - mode 4 = GL_TRIANGLES: every three consecutive indices form one triangle.
///   - mode 5 = GL_TRIANGLE_STRIP: each new vertex extends the strip; winding alternates
///     on odd indices (i+1, i, i+2 instead of i, i+1, i+2) to preserve clockwise order.
///   - mode 6 = GL_TRIANGLE_FAN: a fixed first vertex (the fan center) combined with each
///     consecutive pair of subsequent vertices.
///   Other topology modes (POINTS, LINES, LINE_STRIP, LINE_LOOP — modes 0-3) are not
///   supported and return 0. @p index_view may be NULL for unindexed draws.
/// @param mesh         Viper mesh object to receive the triangles.
/// @param mode         glTF primitive topology mode (4, 5, or 6).
/// @param index_view   Optional index buffer accessor; NULL for non-indexed draw.
/// @param vertex_count Total number of vertices in the primitive (used for bounds-checking).
/// @return 1 if at least one triangle was emitted, 0 on unsupported topology or no data.
static int gltf_append_primitive_indices(void *mesh,
                                         int64_t mode,
                                         const gltf_accessor_view_t *index_view,
                                         int32_t vertex_count) {
    int32_t count = index_view ? index_view->count : vertex_count;
    int32_t emitted = 0;
    if (count <= 0)
        return 0;
    if (mode == 4) {
        for (int32_t i = 0; i + 2 < count; i += 3) {
            emitted += gltf_emit_triangle(mesh,
                                          (uint32_t)vertex_count,
                                          gltf_read_index(index_view, i),
                                          gltf_read_index(index_view, i + 1),
                                          gltf_read_index(index_view, i + 2));
        }
        return emitted > 0;
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
            emitted += gltf_emit_triangle(mesh, (uint32_t)vertex_count, i0, i1, i2);
        }
        return emitted > 0;
    }
    if (mode == 6) {
        uint32_t base = gltf_read_index(index_view, 0);
        for (int32_t i = 1; i + 1 < count; i++) {
            emitted += gltf_emit_triangle(mesh,
                                          (uint32_t)vertex_count,
                                          base,
                                          gltf_read_index(index_view, i),
                                          gltf_read_index(index_view, i + 1));
        }
        return emitted > 0;
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

static void gltf_preload_take_decoded_morph(rt_gltf_preload_bundle *preload_bundle,
                                            int mesh_index,
                                            int primitive_index,
                                            void *mesh_obj,
                                            uint32_t vertex_count) {
    char key[96];
    uint8_t *blob;
    size_t blob_len = 0;
    uint32_t magic;
    uint32_t version;
    uint32_t pod_vertex_count;
    uint32_t shape_count;
    uint32_t record_bytes_u32;
    uint32_t names_bytes_u32;
    uint32_t payload_bytes_u32;
    size_t record_bytes;
    size_t names_bytes;
    size_t payload_bytes;
    size_t records_offset;
    size_t names_offset;
    size_t payload_offset;
    size_t required_len;
    size_t channel_bytes;
    void *morph = NULL;

    if (!preload_bundle || !mesh_obj || vertex_count == 0 || vertex_count > INT32_MAX)
        return;
    gltf_preload_morph_key(mesh_index, primitive_index, key, sizeof(key));
    blob = gltf_preload_bundle_take_dependency(
        preload_bundle, key, GLTF_PRELOAD_DEP_MORPH_POD, &blob_len);
    if (!blob)
        return;
    if (blob_len < GLTF_PRELOAD_MORPH_POD_HEADER_SIZE)
        goto done;
    magic = gltf_read_u32_le(blob + 0);
    version = gltf_read_u32_le(blob + 4);
    pod_vertex_count = gltf_read_u32_le(blob + 8);
    shape_count = gltf_read_u32_le(blob + 12);
    record_bytes_u32 = gltf_read_u32_le(blob + 16);
    names_bytes_u32 = gltf_read_u32_le(blob + 20);
    payload_bytes_u32 = gltf_read_u32_le(blob + 24);
    if (magic != GLTF_PRELOAD_MORPH_POD_MAGIC || version != GLTF_PRELOAD_MORPH_POD_VERSION ||
        pod_vertex_count != vertex_count || shape_count == 0 || shape_count > INT32_MAX)
        goto done;
    if (!gltf_checked_mul_size((size_t)shape_count,
                               GLTF_PRELOAD_MORPH_POD_RECORD_SIZE,
                               &record_bytes) ||
        record_bytes != (size_t)record_bytes_u32)
        goto done;
    names_bytes = (size_t)names_bytes_u32;
    payload_bytes = (size_t)payload_bytes_u32;
    records_offset = GLTF_PRELOAD_MORPH_POD_HEADER_SIZE;
    if (!gltf_checked_add_size(records_offset, record_bytes, &names_offset) ||
        !gltf_checked_add_size(names_offset, names_bytes, &payload_offset) ||
        !gltf_checked_add_size(payload_offset, payload_bytes, &required_len) ||
        required_len > blob_len)
        goto done;
    if (!gltf_checked_mul_size((size_t)vertex_count, 3u * sizeof(float), &channel_bytes))
        goto done;

    morph = rt_morphtarget3d_new((int64_t)vertex_count);
    if (!morph)
        goto done;
    for (uint32_t si = 0; si < shape_count; si++) {
        const uint8_t *record =
            blob + records_offset + (size_t)si * GLTF_PRELOAD_MORPH_POD_RECORD_SIZE;
        uint32_t flags = gltf_read_u32_le(record + 0);
        uint32_t name_offset = gltf_read_u32_le(record + 4);
        uint32_t name_len = gltf_read_u32_le(record + 8);
        float weight = gltf_read_f32_le(record + 12);
        uint32_t pos_offset = gltf_read_u32_le(record + 16);
        uint32_t norm_offset = gltf_read_u32_le(record + 20);
        uint32_t tan_offset = gltf_read_u32_le(record + 24);
        const char *name;
        int64_t shape;
        if (name_len == 0 || (size_t)name_offset + (size_t)name_len > names_bytes)
            continue;
        name = (const char *)(blob + names_offset + name_offset);
        if (name[name_len - 1u] != '\0')
            continue;
        if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS) != 0 &&
            ((size_t)pos_offset > payload_bytes ||
             channel_bytes > payload_bytes - (size_t)pos_offset))
            continue;
        if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0 &&
            ((size_t)norm_offset > payload_bytes ||
             channel_bytes > payload_bytes - (size_t)norm_offset))
            continue;
        if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0 &&
            ((size_t)tan_offset > payload_bytes ||
             channel_bytes > payload_bytes - (size_t)tan_offset))
            continue;
        shape = rt_morphtarget3d_add_shape(morph, rt_const_cstr(name));
        if (shape < 0)
            continue;
        rt_morphtarget3d_set_weight(morph, shape, weight);
        for (uint32_t vi = 0; vi < vertex_count; vi++) {
            if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS) != 0) {
                const uint8_t *src = blob + payload_offset + (size_t)pos_offset +
                                     (size_t)vi * 3u * sizeof(float);
                rt_morphtarget3d_set_delta(morph,
                                           shape,
                                           vi,
                                           gltf_read_f32_le(src + 0),
                                           gltf_read_f32_le(src + 4),
                                           gltf_read_f32_le(src + 8));
            }
            if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0) {
                const uint8_t *src = blob + payload_offset + (size_t)norm_offset +
                                     (size_t)vi * 3u * sizeof(float);
                rt_morphtarget3d_set_normal_delta(morph,
                                                  shape,
                                                  vi,
                                                  gltf_read_f32_le(src + 0),
                                                  gltf_read_f32_le(src + 4),
                                                  gltf_read_f32_le(src + 8));
            }
            if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0) {
                const uint8_t *src = blob + payload_offset + (size_t)tan_offset +
                                     (size_t)vi * 3u * sizeof(float);
                rt_morphtarget3d_set_tangent_delta(morph,
                                                   shape,
                                                   vi,
                                                   gltf_read_f32_le(src + 0),
                                                   gltf_read_f32_le(src + 4),
                                                   gltf_read_f32_le(src + 8));
            }
        }
    }
    if (rt_morphtarget3d_get_shape_count(morph) > 0)
        rt_mesh3d_set_morph_targets(mesh_obj, morph);

done:
    gltf_release_local(morph);
    free(blob);
}

static void *gltf_preload_take_decoded_mesh(rt_gltf_preload_bundle *preload_bundle,
                                            int mesh_index,
                                            int primitive_index,
                                            uint32_t *out_flags) {
    char key[96];
    uint8_t *blob;
    size_t blob_len = 0;
    uint32_t magic;
    uint32_t version;
    uint32_t vertex_count;
    uint32_t index_count;
    uint32_t flags;
    uint32_t vertex_bytes_u32;
    uint32_t index_bytes_u32;
    uint32_t bone_count_u32;
    size_t vertex_bytes;
    size_t index_bytes;
    size_t vertex_offset;
    size_t index_offset;
    size_t required_len;
    vgfx3d_vertex_t *vertices;
    uint32_t *indices;
    void *mesh_obj;
    rt_mesh3d *mesh;

    if (out_flags)
        *out_flags = 0u;
    gltf_preload_mesh_key(mesh_index, primitive_index, key, sizeof(key));
    blob = gltf_preload_bundle_take_dependency(
        preload_bundle, key, GLTF_PRELOAD_DEP_MESH_POD, &blob_len);
    if (!blob)
        return NULL;
    if (blob_len < GLTF_PRELOAD_MESH_POD_HEADER_SIZE)
        goto malformed;
    magic = gltf_read_u32_le(blob + 0);
    version = gltf_read_u32_le(blob + 4);
    vertex_count = gltf_read_u32_le(blob + 8);
    index_count = gltf_read_u32_le(blob + 12);
    flags = gltf_read_u32_le(blob + 16);
    vertex_bytes_u32 = gltf_read_u32_le(blob + 20);
    index_bytes_u32 = gltf_read_u32_le(blob + 24);
    bone_count_u32 = gltf_read_u32_le(blob + 28);
    if (magic != GLTF_PRELOAD_MESH_POD_MAGIC || version != GLTF_PRELOAD_MESH_POD_VERSION ||
        vertex_count == 0 || index_count == 0)
        goto malformed;
    if (bone_count_u32 > VGFX3D_MAX_BONES)
        goto malformed;
    if (!gltf_checked_mul_size((size_t)vertex_count, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !gltf_checked_mul_size((size_t)index_count, sizeof(uint32_t), &index_bytes) ||
        vertex_bytes != (size_t)vertex_bytes_u32 || index_bytes != (size_t)index_bytes_u32)
        goto malformed;
    vertex_offset = GLTF_PRELOAD_MESH_POD_HEADER_SIZE;
    if (!gltf_checked_add_size(vertex_offset, vertex_bytes, &index_offset) ||
        !gltf_checked_add_size(index_offset, index_bytes, &required_len) ||
        required_len > blob_len)
        goto malformed;

    vertices = (vgfx3d_vertex_t *)malloc(vertex_bytes);
    indices = (uint32_t *)malloc(index_bytes);
    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        goto malformed;
    }
    memcpy(vertices, blob + vertex_offset, vertex_bytes);
    memcpy(indices, blob + index_offset, index_bytes);

    mesh_obj = rt_mesh3d_new();
    if (!mesh_obj) {
        free(vertices);
        free(indices);
        goto malformed;
    }
    mesh = (rt_mesh3d *)mesh_obj;
    free(mesh->vertices);
    free(mesh->indices);
    mesh->vertices = vertices;
    mesh->vertex_count = vertex_count;
    mesh->vertex_capacity = vertex_count;
    mesh->indices = indices;
    mesh->index_count = index_count;
    mesh->index_capacity = index_count;
    mesh->bone_count = (int32_t)bone_count_u32;
    mesh->build_failed = 0;
    rt_mesh3d_touch_geometry_now(mesh);
    gltf_preload_take_decoded_morph(preload_bundle, mesh_index, primitive_index, mesh_obj, vertex_count);
    if (out_flags)
        *out_flags = flags;
    free(blob);
    return mesh_obj;

malformed:
    free(blob);
    return NULL;
}

/// @brief Return @p value if it is finite, otherwise return @p fallback.
/// @details Protects downstream arithmetic from NaN and ±Inf values that can appear
///   in glTF fields parsed from malformed JSON numbers.
static double gltf_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Sanitise a double: replace non-finite values with @p fallback, then clamp to [lo, hi].
/// @details Combines `gltf_finite_or` with a bounds clamp in one call. Used for material
///   colour components, emissive strength, metallic/roughness factors, and spot-light angles
///   where both NaN/Inf rejection and range enforcement are required.
static double gltf_clamp_double(double value, double lo, double hi, double fallback) {
    value = gltf_finite_or(value, fallback);
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Normalize @p v, or replace it with @p fallback when it is degenerate.
static void gltf_normalize_vec3_or(double *v, double fx, double fy, double fz) {
    double len;
    if (!v)
        return;
    v[0] = gltf_finite_or(v[0], fx);
    v[1] = gltf_finite_or(v[1], fy);
    v[2] = gltf_finite_or(v[2], fz);
    len = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!isfinite(len) || len <= 1e-12) {
        v[0] = fx;
        v[1] = fy;
        v[2] = fz;
        return;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

/// @brief Read one numeric element from a JSON array by index, with bounds-check and default.
/// @details Combines `jarr_len` bounds-check, `rt_seq_get`, and `jvalue_num` into a single call
///   for safely reading glTF array fields like `"color"` and `"translation"` that may have
///   fewer than the expected number of elements.
static double gltf_arr_num(void *arr, int64_t index, double fallback) {
    if (!arr || index < 0 || index >= jarr_len(arr))
        return fallback;
    return jvalue_num(rt_seq_get(arr, index), fallback);
}

static char *gltf_strdup_or(const char *value, const char *fallback) {
    const char *src = (value && value[0] != '\0') ? value : fallback;
    size_t len;
    char *copy;
    if (!src)
        src = "";
    len = strlen(src);
    if (len == SIZE_MAX) {
        rt_trap("glTF: scene name too long");
        return NULL;
    }
    copy = (char *)malloc(len + 1);
    if (!copy) {
        rt_trap("glTF: scene name allocation failed");
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

/// @brief Append a newly-created Camera3D to the asset-owned active-scene camera list.
/// @details Takes ownership of @p camera on success. On allocation failure the caller still owns
///   the camera and should release it.
static int gltf_append_camera(rt_gltf_asset *asset, void *camera) {
    void **grown;
    int32_t next_capacity;
    if (!asset || !camera)
        return 1;
    if (asset->camera_count >= asset->camera_capacity) {
        if (asset->camera_capacity > INT32_MAX / 2) {
            rt_trap("glTF: too many cameras");
            return 0;
        }
        next_capacity = asset->camera_capacity > 0 ? asset->camera_capacity * 2 : 4;
        grown = (void **)realloc(asset->cameras, (size_t)next_capacity * sizeof(void *));
        if (!grown) {
            rt_trap("glTF: camera list allocation failed");
            return 0;
        }
        asset->cameras = grown;
        asset->camera_capacity = next_capacity;
    }
    asset->cameras[asset->camera_count++] = camera;
    return 1;
}

static int gltf_append_scene_camera(gltf_scene_info_t *scene, void *camera) {
    void **grown;
    int32_t next_capacity;
    if (!scene || !camera)
        return 1;
    if (scene->camera_count >= scene->camera_capacity) {
        if (scene->camera_capacity > INT32_MAX / 2) {
            rt_trap("glTF: too many scene cameras");
            return 0;
        }
        next_capacity = scene->camera_capacity > 0 ? scene->camera_capacity * 2 : 4;
        grown = (void **)realloc(scene->cameras, (size_t)next_capacity * sizeof(void *));
        if (!grown) {
            rt_trap("glTF: scene camera list allocation failed");
            return 0;
        }
        scene->cameras = grown;
        scene->camera_capacity = next_capacity;
    }
    scene->cameras[scene->camera_count++] = camera;
    return 1;
}

static int gltf_append_scene(rt_gltf_asset *asset, void *root, const char *name) {
    gltf_scene_info_t *grown;
    gltf_scene_info_t *scene;
    int32_t next_capacity;
    char fallback[64];
    if (!asset || !root)
        return 0;
    if (asset->scene_count >= asset->scene_capacity) {
        if (asset->scene_capacity > INT32_MAX / 2) {
            rt_trap("glTF: too many scenes");
            return 0;
        }
        next_capacity = asset->scene_capacity > 0 ? asset->scene_capacity * 2 : 4;
        grown = (gltf_scene_info_t *)realloc(asset->scenes,
                                             (size_t)next_capacity * sizeof(*asset->scenes));
        if (!grown) {
            rt_trap("glTF: scene list allocation failed");
            return 0;
        }
        memset(grown + asset->scene_capacity,
               0,
               (size_t)(next_capacity - asset->scene_capacity) * sizeof(*grown));
        asset->scenes = grown;
        asset->scene_capacity = next_capacity;
    }
    snprintf(fallback, sizeof(fallback), asset->scene_count == 0 ? "default" : "scene_%d",
             (int)asset->scene_count);
    scene = &asset->scenes[asset->scene_count];
    memset(scene, 0, sizeof(*scene));
    scene->root = root;
    scene->node_count = gltf_count_subtree((rt_scene_node3d *)root) - 1;
    scene->name = gltf_strdup_or(name, fallback);
    if (!scene->name) {
        memset(scene, 0, sizeof(*scene));
        return 0;
    }
    asset->scene_count++;
    return 1;
}

static rt_scene_node3d *gltf_clone_scene_node_shallow(const rt_scene_node3d *src) {
    rt_scene_node3d *dst;
    if (!src)
        return NULL;
    dst = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!dst)
        return NULL;
    memcpy(dst->position, src->position, sizeof(dst->position));
    memcpy(dst->rotation, src->rotation, sizeof(dst->rotation));
    memcpy(dst->scale_xyz, src->scale_xyz, sizeof(dst->scale_xyz));
    memcpy(dst->aabb_min, src->aabb_min, sizeof(dst->aabb_min));
    memcpy(dst->aabb_max, src->aabb_max, sizeof(dst->aabb_max));
    dst->bsphere_radius = src->bsphere_radius;
    dst->world_dirty = 1;
    dst->visible = src->visible;
    if (src->mesh) {
        rt_obj_retain_maybe(src->mesh);
        dst->mesh = src->mesh;
    }
    if (src->material) {
        rt_obj_retain_maybe(src->material);
        dst->material = src->material;
    }
    if (src->light) {
        rt_obj_retain_maybe(src->light);
        dst->light = src->light;
    }
    if (src->name) {
        rt_obj_retain_maybe(src->name);
        dst->name = src->name;
    }
    return dst;
}

typedef struct {
    const rt_scene_node3d *src;
    rt_scene_node3d *dst;
    int32_t next_child;
} gltf_clone_frame_t;

static rt_scene_node3d *gltf_clone_scene_node(const rt_scene_node3d *src) {
    rt_scene_node3d *root;
    gltf_clone_frame_t *stack;
    int32_t stack_count = 0;
    int32_t stack_capacity = 32;
    if (!src)
        return NULL;
    root = gltf_clone_scene_node_shallow(src);
    if (!root)
        return NULL;
    stack = (gltf_clone_frame_t *)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack) {
        rt_trap("glTF: scene clone stack allocation failed");
        gltf_release_local(root);
        return NULL;
    }
    stack[stack_count++] = (gltf_clone_frame_t){src, root, 0};
    while (stack_count > 0) {
        gltf_clone_frame_t *frame = &stack[stack_count - 1];
        const rt_scene_node3d *src_child;
        rt_scene_node3d *dst_child;
        if (!frame->src || frame->next_child >= frame->src->child_count) {
            stack_count--;
            continue;
        }
        src_child = frame->src->children[frame->next_child++];
        dst_child = gltf_clone_scene_node_shallow(src_child);
        if (!dst_child) {
            free(stack);
            gltf_release_local(root);
            return NULL;
        }
        rt_scene_node3d_add_child(frame->dst, dst_child);
        gltf_release_local(dst_child);
        if (stack_count >= stack_capacity) {
            gltf_clone_frame_t *grown;
            int32_t next_capacity;
            if (stack_capacity > INT32_MAX / 2) {
                free(stack);
                gltf_release_local(root);
                rt_trap("glTF: too many scene nodes to clone");
                return NULL;
            }
            next_capacity = stack_capacity * 2;
            grown = (gltf_clone_frame_t *)realloc(stack, (size_t)next_capacity * sizeof(*stack));
            if (!grown) {
                free(stack);
                gltf_release_local(root);
                rt_trap("glTF: scene clone stack allocation failed");
                return NULL;
            }
            stack = grown;
            stack_capacity = next_capacity;
        }
        stack[stack_count++] = (gltf_clone_frame_t){src_child, dst_child, 0};
    }
    free(stack);
    return root;
}

/// @brief Build a Camera3D from one glTF `cameras[]` JSON entry.
/// @details glTF perspective `yfov` is radians; Viper stores vertical FOV in degrees. glTF
///   orthographic `xmag`/`ymag` are view half-extents, matching Camera3D.NewOrtho's half-height
///   size parameter and aspect ratio.
static void *gltf_make_camera(void *camera_json) {
    static const double pi = 3.14159265358979323846;
    const char *type = jstr(camera_json, "type");
    if (!camera_json || !type)
        return NULL;
    if (strcmp(type, "perspective") == 0) {
        void *persp = jget(camera_json, "perspective");
        double znear = jvalue_num(jget(persp, "znear"), 0.1);
        double zfar = jvalue_num(jget(persp, "zfar"), znear + 1000.0);
        double yfov = jvalue_num(jget(persp, "yfov"), pi / 3.0);
        double aspect = jvalue_num(jget(persp, "aspectRatio"), 1.0);
        return rt_camera3d_new(yfov * (180.0 / pi), aspect, znear, zfar);
    }
    if (strcmp(type, "orthographic") == 0) {
        void *ortho = jget(camera_json, "orthographic");
        double xmag = fabs(jvalue_num(jget(ortho, "xmag"), 1.0));
        double ymag = fabs(jvalue_num(jget(ortho, "ymag"), 1.0));
        double znear = jvalue_num(jget(ortho, "znear"), 0.1);
        double zfar = jvalue_num(jget(ortho, "zfar"), znear + 1000.0);
        double aspect;
        if (!isfinite(xmag) || xmag <= 1e-9)
            xmag = 1.0;
        if (!isfinite(ymag) || ymag <= 1e-9)
            ymag = 1.0;
        aspect = xmag / ymag;
        return rt_camera3d_new_ortho(ymag, aspect, znear, zfar);
    }
    return NULL;
}

/// @brief Apply a glTF camera node's world transform to a standalone Camera3D object.
/// @details glTF cameras look down local -Z with +Y as up. SceneNode3D matrices are row-major,
///   so the local -Z and +Y axes are read from the third and second rotation columns.
static void gltf_apply_camera_node_transform(void *camera, rt_scene_node3d *node) {
    void *world_obj;
    double m[16];
    double eye[3];
    double forward[3];
    double up[3];
    double target[3];
    if (!camera || !node)
        return;
    world_obj = rt_scene_node3d_get_world_matrix(node);
    if (!world_obj)
        return;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m[r * 4 + c] = rt_mat4_get(world_obj, r, c);
    gltf_release_ref(&world_obj);

    eye[0] = m[3];
    eye[1] = m[7];
    eye[2] = m[11];
    forward[0] = -m[2];
    forward[1] = -m[6];
    forward[2] = -m[10];
    up[0] = m[1];
    up[1] = m[5];
    up[2] = m[9];
    gltf_normalize_vec3_or(forward, 0.0, 0.0, -1.0);
    gltf_normalize_vec3_or(up, 0.0, 1.0, 0.0);
    target[0] = eye[0] + forward[0];
    target[1] = eye[1] + forward[1];
    target[2] = eye[2] + forward[2];
    rt_camera3d_look_at_components(camera,
                                   eye[0],
                                   eye[1],
                                   eye[2],
                                   target[0],
                                   target[1],
                                   target[2],
                                   up[0],
                                   up[1],
                                   up[2]);
}

/// @brief Mark @p root_idx and its descendants as members of the active scene.
static int gltf_mark_active_node_subtree(void *nodes_arr,
                                         int32_t node_count,
                                         int32_t root_idx,
                                         uint8_t *active_nodes) {
    int32_t *stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 32;
    if (!nodes_arr || !active_nodes || root_idx < 0 || root_idx >= node_count)
        return 0;
    stack = (int32_t *)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack)
        return 0;
    stack[stack_count++] = root_idx;
    while (stack_count > 0) {
        int32_t node_idx = stack[--stack_count];
        void *node_json;
        void *children;
        if (node_idx < 0 || node_idx >= node_count)
            continue;
        if (active_nodes[node_idx])
            continue;
        active_nodes[node_idx] = 1;
        node_json = rt_seq_get(nodes_arr, node_idx);
        children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int64_t child_idx = jvalue_int(rt_seq_get(children, ci), -1);
            if (child_idx < 0 || child_idx >= node_count)
                continue;
            if (stack_count >= stack_capacity) {
                int32_t next_capacity;
                int32_t *grown;
                if (stack_capacity > INT32_MAX / 2) {
                    free(stack);
                    return 0;
                }
                next_capacity = stack_capacity * 2;
                grown = (int32_t *)realloc(stack, (size_t)next_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return 0;
                }
                stack = grown;
                stack_capacity = next_capacity;
            }
            stack[stack_count++] = (int32_t)child_idx;
        }
    }
    free(stack);
    return 1;
}

/// @brief Create Camera3D objects for camera nodes that are reachable from one immutable scene.
static int gltf_import_scene_cameras(gltf_scene_info_t *scene,
                                     void *root,
                                     void *nodes_arr,
                                     rt_scene_node3d **nodes,
                                     int32_t node_count,
                                     const uint8_t *active_nodes) {
    void *cameras_arr = jarr(root, "cameras");
    int64_t camera_def_count = jarr_len(cameras_arr);
    if (!scene || !nodes_arr || !nodes || !active_nodes || camera_def_count <= 0)
        return 1;
    for (int32_t ni = 0; ni < node_count; ni++) {
        void *node_json;
        int64_t camera_ref;
        void *camera;
        if (!active_nodes[ni] || !nodes[ni])
            continue;
        node_json = rt_seq_get(nodes_arr, ni);
        camera_ref = jvalue_int(jget(node_json, "camera"), -1);
        if (camera_ref < 0 || camera_ref >= camera_def_count)
            continue;
        camera = gltf_make_camera(rt_seq_get(cameras_arr, camera_ref));
        if (!camera)
            continue;
        gltf_apply_camera_node_transform(camera, nodes[ni]);
        if (!gltf_append_scene_camera(scene, camera)) {
            gltf_release_ref(&camera);
            return 0;
        }
    }
    return 1;
}

static int gltf_install_active_scene_compat(rt_gltf_asset *asset) {
    gltf_scene_info_t *scene;
    if (!asset || asset->scene_count <= 0)
        return 1;
    scene = &asset->scenes[0];
    if (scene->root) {
        rt_obj_retain_maybe(scene->root);
        asset->scene_root = scene->root;
        asset->node_count = scene->node_count;
    }
    for (int32_t i = 0; i < scene->camera_count; i++) {
        void *camera = scene->cameras[i];
        if (!camera)
            continue;
        rt_obj_retain_maybe(camera);
        if (!gltf_append_camera(asset, camera)) {
            gltf_release_ref(&camera);
            return 0;
        }
    }
    return 1;
}

/// @brief Construct a `rt_light3d` from a `KHR_lights_punctual` extension light JSON object.
/// @details Reads `type` (directional/point/spot), `color`, `intensity`, and `range` from
///   the light JSON. Intensity is stored directly; range is converted to an inverse-square
///   attenuation factor (`1 / range²`), or 0 for unbounded lights (range = 0 per spec).
///   For spot lights, `innerConeAngle` and `outerConeAngle` are read from the nested `"spot"`
///   object, clamped to `(0, π/2]`, and stored as their cosines — the convention expected by
///   the shader. A minimum angular gap (`spot_eps`) is enforced between inner and outer angles
///   to prevent the penumbra from collapsing to zero width. The default direction `(0, 0, −1)`
///   matches the glTF spec's local-space forward vector; the actual world-space direction is
///   applied when the node's transform is combined with the light during scene-graph traversal.
/// @param light_json  Parsed KHR_lights_punctual light JSON object; must be non-NULL.
/// @return Newly allocated `rt_light3d` on success; NULL if the type field is absent or unknown.
static void *gltf_new_punctual_light(void *light_json) {
    static const double pi = 3.14159265358979323846;
    const char *type = jstr(light_json, "type");
    void *color_arr = jarr(light_json, "color");
    void *spot = jget(light_json, "spot");
    double range = jnum(light_json, "range", 0.0);
    double inner_angle = spot ? jnum(spot, "innerConeAngle", 0.0) : 0.0;
    double outer_angle = spot ? jnum(spot, "outerConeAngle", pi / 4.0) : pi / 4.0;
    rt_light3d *light;
    if (!type)
        return NULL;
    light = (rt_light3d *)rt_obj_new_i64(RT_G3D_LIGHT3D_CLASS_ID, (int64_t)sizeof(rt_light3d));
    if (!light)
        return NULL;
    memset(light, 0, sizeof(*light));
    light->color[0] = gltf_clamp_double(gltf_arr_num(color_arr, 0, 1.0), 0.0, 1.0, 1.0);
    light->color[1] = gltf_clamp_double(gltf_arr_num(color_arr, 1, 1.0), 0.0, 1.0, 1.0);
    light->color[2] = gltf_clamp_double(gltf_arr_num(color_arr, 2, 1.0), 0.0, 1.0, 1.0);
    light->intensity = gltf_clamp_double(jnum(light_json, "intensity", 1.0), 0.0, DBL_MAX, 1.0);
    light->attenuation = range > 1e-6 && isfinite(range) ? 1.0 / (range * range) : 0.0;
    light->direction[0] = 0.0;
    light->direction[1] = 0.0;
    light->direction[2] = -1.0;
    light->enabled = 1;
    light->casts_shadows = 1;

    if (strcmp(type, "directional") == 0) {
        light->type = 0;
        return light;
    }
    if (strcmp(type, "point") == 0) {
        light->type = 1;
        return light;
    }
    if (strcmp(type, "spot") == 0) {
        const double max_spot_angle = pi * 0.5;
        const double spot_eps = 1e-4;
        light->type = 3;
        inner_angle = gltf_clamp_double(inner_angle, 0.0, max_spot_angle - spot_eps, 0.0);
        outer_angle = gltf_clamp_double(outer_angle, 0.0, max_spot_angle, pi / 4.0);
        if (outer_angle <= inner_angle + spot_eps) {
            if (inner_angle > max_spot_angle - spot_eps)
                inner_angle = max_spot_angle - spot_eps;
            outer_angle = inner_angle + spot_eps;
        }
        if (outer_angle > max_spot_angle) {
            outer_angle = max_spot_angle;
            if (inner_angle >= outer_angle)
                inner_angle = outer_angle - spot_eps;
        }
        light->inner_cos = cos(inner_angle);
        light->outer_cos = cos(outer_angle);
        return light;
    }

    gltf_release_local(light);
    return NULL;
}

/// @brief Parse the KHR_lights_punctual extension block from the glTF root and build a light array.
/// @details Walks `root.extensions.KHR_lights_punctual.lights`, calls `gltf_new_punctual_light`
///          for each entry, and writes the resulting pointer array and count to the output params.
///          On failure or missing extension both outputs are left at NULL/0.
static void gltf_parse_punctual_lights(void *root, void ***out_lights, int32_t *out_count) {
    void *extensions;
    void *punctual;
    void *lights_arr;
    int64_t count64;
    void **lights;
    if (out_lights)
        *out_lights = NULL;
    if (out_count)
        *out_count = 0;
    if (!root || !out_lights || !out_count)
        return;
    extensions = jget(root, "extensions");
    punctual = extensions ? jget(extensions, "KHR_lights_punctual") : NULL;
    lights_arr = punctual ? jarr(punctual, "lights") : NULL;
    count64 = jarr_len(lights_arr);
    if (!lights_arr || count64 <= 0 || count64 > INT32_MAX)
        return;
    lights = (void **)calloc((size_t)count64, sizeof(void *));
    if (!lights)
        return;
    for (int64_t i = 0; i < count64; i++)
        lights[i] = gltf_new_punctual_light(rt_seq_get(lights_arr, i));
    *out_lights = lights;
    *out_count = (int32_t)count64;
}

/// @brief Retrieve a morph-target name from the mesh's `extras.targetNames` array.
/// @details If no name is present (missing extras, out-of-range index, or empty string),
///          fills @p fallback with `"target_N"` and returns it so the caller always
///          gets a non-NULL, non-empty string.
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

/// @brief Read all morph targets from a glTF primitive's `targets` array into a MorphTarget3D
/// object.
/// @details Creates a `rt_morphtarget3d` with @p vertex_count slots, iterates each target
///          entry, reads POSITION / NORMAL / TANGENT accessor deltas, and registers them as
///          named shapes. Initial weights come from the mesh-level `weights` array when present.
///          The morph target is attached to @p mesh_obj via `rt_mesh3d_set_morph_targets`.
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
        int64_t tangent_acc = jint(target, "TANGENT", -1);
        gltf_accessor_view_t pos_view;
        gltf_accessor_view_t norm_view;
        gltf_accessor_view_t tangent_view;
        int has_pos = gltf_get_accessor_view(root, pos_acc, buffers, buf_count, &pos_view);
        int has_norm = gltf_get_accessor_view(root, norm_acc, buffers, buf_count, &norm_view);
        int has_tangent =
            gltf_get_accessor_view(root, tangent_acc, buffers, buf_count, &tangent_view);
        char fallback_name[64];
        const char *name;
        int64_t shape;
        if (!has_pos && !has_norm && !has_tangent)
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
        if (has_tangent) {
            int32_t limit = tangent_view.count < vertex_count ? tangent_view.count : vertex_count;
            for (int32_t vi = 0; vi < limit; vi++) {
                float delta[3] = {0.0f, 0.0f, 0.0f};
                gltf_accessor_read_f32(&tangent_view, vi, delta, 3);
                rt_morphtarget3d_set_tangent_delta(morph, shape, vi, delta[0], delta[1], delta[2]);
            }
        }
    }

    if (rt_morphtarget3d_get_shape_count(morph) > 0)
        rt_mesh3d_set_morph_targets(mesh_obj, morph);
    gltf_release_local(morph);
}

/// @brief Write per-shape weights from a glTF node's `weights` array into the mesh's morph target.
/// @details Iterates up to `min(shape_count, weights_arr length)` entries and calls
///          `rt_morphtarget3d_set_weight` for each. No-op when the mesh has no morph targets
///          or when @p weights_arr is NULL.
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

/// @brief Combine a glTF document's directory with a relative URI to get an absolute filesystem
/// path.
///
/// External buffers and textures in glTF are referenced via paths
/// relative to the .gltf file itself; this prepends the document's
/// directory so the loader can open them.
static int gltf_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

/// @brief Decode a percent-encoded URI path component into a plain filesystem path string.
/// @details Converts `%XX` escape sequences to their byte values, stops at `#` (fragment)
///          or `?` (query), and NUL-terminates @p out. Handles malformed escapes by copying
///          them verbatim. Does not handle scheme prefixes (file://, http://) — caller must
///          strip those before passing here.
static void gltf_decode_uri_path(const char *uri, char *out, size_t out_cap) {
    size_t oi = 0;
    if (!out || out_cap == 0)
        return;
    out[0] = '\0';
    if (!uri)
        return;
    while (*uri && oi + 1 < out_cap) {
        if (*uri == '#' || *uri == '?')
            break;
        if (*uri == '%' && uri[1] && uri[2]) {
            int hi = gltf_hex_digit(uri[1]);
            int lo = gltf_hex_digit(uri[2]);
            if (hi >= 0 && lo >= 0) {
                out[oi++] = (char)((hi << 4) | lo);
                uri += 3;
                continue;
            }
        }
        out[oi++] = *uri++;
    }
    out[oi] = '\0';
}

/// @brief Combine a glTF document's base path with a relative URI to produce an absolute path.
/// @details Decodes the URI via `gltf_decode_uri_path`, validates it with `gltf_safe_relative_uri`
///          (rejects absolute paths, `..` traversal, etc.), then prepends the directory component
///          of @p base_path. Both `/` and `\` separators are recognised for cross-platform support.
///          Writes an empty string to @p out on any validation failure.
static void gltf_resolve_relative_path(const char *base_path,
                                       const char *uri,
                                       char *out,
                                       size_t out_cap) {
    char decoded_uri[1024];
    const char *last_sep;
    const char *last_bsep;
    size_t dir_len;
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!uri)
        return;
    gltf_decode_uri_path(uri, decoded_uri, sizeof(decoded_uri));
    if (!gltf_safe_relative_uri(decoded_uri))
        return;
    last_sep = strrchr(base_path ? base_path : "", '/');
    last_bsep = strrchr(base_path ? base_path : "", '\\');
    if (last_bsep && (!last_sep || last_bsep > last_sep))
        last_sep = last_bsep;
    if (last_sep) {
        dir_len = (size_t)(last_sep - base_path + 1);
        if (dir_len >= out_cap)
            dir_len = out_cap - 1;
        memcpy(out, base_path, dir_len);
        out[dir_len] = '\0';
        strncat(out, decoded_uri, out_cap - dir_len - 1);
    } else {
        strncpy(out, decoded_uri, out_cap - 1);
        out[out_cap - 1] = '\0';
    }
}

/// @brief True if @p path uses the `asset://` scheme (a packed/mounted asset URI).
static int gltf_is_asset_uri(const char *path) {
    return path && strncmp(path, "asset://", 8) == 0;
}

/// @brief Read an entire file into a freshly malloc'd buffer (caller frees), capping at
///   256 MiB; returns NULL and zeroes @p out_size on any error.
static uint8_t *gltf_read_file_bytes(const char *filepath, size_t *out_size) {
    if (out_size)
        *out_size = 0;
    if (!filepath)
        return NULL;
    FILE *f = fopen(filepath, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long fsize = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
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
    if (out_size)
        *out_size = (size_t)fsize;
    return file_data;
}

/// @brief Trap with a formatted message reporting that a model's @p kind dependency
///   (buffer/image) at @p dependency_path could not be loaded.
static void gltf_trap_asset_dependency(const char *model_path,
                                       const char *dependency_path,
                                       const char *kind) {
    char msg[2048];
    snprintf(msg,
             sizeof(msg),
             "GLTF.LoadAsset: failed to load %s dependency '%s' for model '%s'",
             kind ? kind : "asset",
             dependency_path ? dependency_path : "",
             model_path ? model_path : "");
    rt_trap(msg);
}

/// @brief Load the root .gltf/.glb bytes: when @p load_assets, try the asset manager
///   first (and stop there for `asset://` URIs), otherwise fall back to the filesystem.
static uint8_t *gltf_load_root_bytes(rt_string path,
                                     const char *filepath,
                                     int load_assets,
                                     size_t *out_size) {
    uint8_t *data = NULL;
    if (out_size)
        *out_size = 0;
    if (load_assets) {
        data = rt_asset_load_raw(path, out_size);
        if (data || gltf_is_asset_uri(filepath))
            return data;
    }
    return gltf_read_file_bytes(filepath, out_size);
}

/// @brief Load an external dependency's bytes (asset manager when @p load_assets, else
///   filesystem), failing if fewer than @p required_len bytes are available.
static uint8_t *gltf_load_dependency_bytes(const char *resource_path,
                                           int load_assets,
                                           size_t required_len,
                                           rt_gltf_preload_bundle *preload_bundle,
                                           size_t *out_size) {
    uint8_t *data = NULL;
    size_t len = 0;
    if (out_size)
        *out_size = 0;
    if (!resource_path)
        return NULL;
    data = gltf_preload_bundle_take_dependency(
        preload_bundle, resource_path, GLTF_PRELOAD_DEP_BUFFER, &len);
    if (data)
        goto done;
    if (load_assets) {
        data = rt_asset_load_raw(rt_const_cstr(resource_path), &len);
        if (data || gltf_is_asset_uri(resource_path))
            goto done;
    }
    data = gltf_read_file_bytes(resource_path, &len);

done:
    if (!data)
        return NULL;
    if (len < required_len) {
        free(data);
        return NULL;
    }
    if (out_size)
        *out_size = len;
    return data;
}

static void *gltf_preload_take_decoded_image(rt_gltf_preload_bundle *preload_bundle,
                                             const char *key) {
    uint8_t *rgba_blob;
    size_t rgba_len = 0;
    void *pixels;
    pixels = gltf_preload_bundle_take_object_dependency(
        preload_bundle, key, GLTF_PRELOAD_DEP_IMAGE_PIXELS);
    if (pixels)
        return pixels;
    rgba_blob = gltf_preload_bundle_take_dependency(
        preload_bundle, key, GLTF_PRELOAD_DEP_IMAGE_RGBA, &rgba_len);
    if (!rgba_blob)
        return NULL;
    pixels = gltf_pixels_from_rgba_blob(rgba_blob, rgba_len);
    free(rgba_blob);
    return pixels;
}

/// @brief Load and decode an external image dependency into a Pixels object: asset
///   manager (decoded by type) when @p load_assets, otherwise rt_pixels_load from disk.
static void *gltf_load_dependency_image(const char *resource_path,
                                        int load_assets,
                                        rt_gltf_preload_bundle *preload_bundle) {
    uint8_t *staged = NULL;
    size_t staged_len = 0;
    if (!resource_path || resource_path[0] == '\0')
        return NULL;
    {
        void *decoded = gltf_preload_take_decoded_image(preload_bundle, resource_path);
        if (decoded)
            return decoded;
    }
    staged = gltf_preload_bundle_take_dependency(
        preload_bundle, resource_path, GLTF_PRELOAD_DEP_IMAGE, &staged_len);
    if (staged) {
        void *decoded = rt_asset_decode_typed(resource_path, staged, staged_len);
        free(staged);
        if (decoded)
            return decoded;
    }
    if (load_assets) {
        size_t data_len = 0;
        uint8_t *data = rt_asset_load_raw(rt_const_cstr(resource_path), &data_len);
        if (data) {
            void *decoded = rt_asset_decode_typed(resource_path, data, data_len);
            free(data);
            if (decoded)
                return decoded;
        }
        if (gltf_is_asset_uri(resource_path))
            return NULL;
    }
    return rt_pixels_load(rt_const_cstr(resource_path));
}

static void gltf_preload_set_error(char *error, size_t error_cap, const char *message) {
    if (error && error_cap > 0) {
        snprintf(error, error_cap, "%s", message ? message : "failed to stage glTF preload");
    }
}

static size_t gltf_json_skip_ws(const char *json, size_t len, size_t pos) {
    while (pos < len) {
        char c = json[pos];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        pos++;
    }
    return pos;
}

static size_t gltf_json_skip_string_raw(const char *json, size_t len, size_t pos) {
    if (!json || pos >= len || json[pos] != '"')
        return SIZE_MAX;
    pos++;
    while (pos < len) {
        char c = json[pos++];
        if (c == '"')
            return pos;
        if (c == '\\') {
            if (pos >= len)
                return SIZE_MAX;
            pos++;
        }
    }
    return SIZE_MAX;
}

static char *gltf_json_read_string_alloc(const char *json,
                                         size_t len,
                                         size_t pos,
                                         size_t *out_next) {
    char *out;
    size_t cap;
    size_t count = 0;
    if (out_next)
        *out_next = SIZE_MAX;
    if (!json || pos >= len || json[pos] != '"')
        return NULL;
    cap = len - pos + 1u;
    out = (char *)malloc(cap);
    if (!out)
        return NULL;
    pos++;
    while (pos < len) {
        char c = json[pos++];
        if (c == '"') {
            out[count] = '\0';
            if (out_next)
                *out_next = pos;
            return out;
        }
        if (c == '\\') {
            if (pos >= len)
                break;
            c = json[pos++];
            switch (c) {
            case '"':
            case '\\':
            case '/':
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'n':
                c = '\n';
                break;
            case 'r':
                c = '\r';
                break;
            case 't':
                c = '\t';
                break;
            default:
                free(out);
                return NULL;
            }
        }
        out[count++] = c;
    }
    free(out);
    return NULL;
}

static int gltf_json_key_matches(const char *json,
                                 size_t len,
                                 size_t pos,
                                 const char *key,
                                 size_t *out_next) {
    char *decoded = gltf_json_read_string_alloc(json, len, pos, out_next);
    int matches = decoded && key && strcmp(decoded, key) == 0;
    free(decoded);
    return matches;
}

static size_t gltf_json_skip_value(const char *json, size_t len, size_t pos) {
    int object_depth = 0;
    int array_depth = 0;
    pos = gltf_json_skip_ws(json, len, pos);
    if (pos >= len)
        return SIZE_MAX;
    if (json[pos] == '"')
        return gltf_json_skip_string_raw(json, len, pos);
    if (json[pos] != '{' && json[pos] != '[') {
        while (pos < len && json[pos] != ',' && json[pos] != '}' && json[pos] != ']')
            pos++;
        return pos;
    }
    do {
        char c = json[pos];
        if (c == '"') {
            pos = gltf_json_skip_string_raw(json, len, pos);
            if (pos == SIZE_MAX)
                return SIZE_MAX;
            continue;
        }
        if (c == '{')
            object_depth++;
        else if (c == '}')
            object_depth--;
        else if (c == '[')
            array_depth++;
        else if (c == ']')
            array_depth--;
        pos++;
    } while (pos < len && (object_depth > 0 || array_depth > 0));
    return object_depth == 0 && array_depth == 0 ? pos : SIZE_MAX;
}

static size_t gltf_json_find_matching(const char *json,
                                      size_t len,
                                      size_t pos,
                                      char open_ch,
                                      char close_ch) {
    int depth = 0;
    if (!json || pos >= len || json[pos] != open_ch)
        return SIZE_MAX;
    while (pos < len) {
        char c = json[pos];
        if (c == '"') {
            pos = gltf_json_skip_string_raw(json, len, pos);
            if (pos == SIZE_MAX)
                return SIZE_MAX;
            continue;
        }
        if (c == open_ch)
            depth++;
        else if (c == close_ch) {
            depth--;
            if (depth == 0)
                return pos + 1u;
        }
        pos++;
    }
    return SIZE_MAX;
}

static int gltf_json_find_top_level_array(const char *json,
                                          size_t len,
                                          const char *key,
                                          size_t *out_start,
                                          size_t *out_end) {
    int object_depth = 0;
    int array_depth = 0;
    size_t pos = 0;
    if (out_start)
        *out_start = SIZE_MAX;
    if (out_end)
        *out_end = SIZE_MAX;
    while (pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_top_key = 0;
            if (next == SIZE_MAX)
                return 0;
            if (object_depth == 1 && array_depth == 0)
                at_top_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_top_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t start;
                size_t end;
                if (colon < len && json[colon] == ':') {
                    start = gltf_json_skip_ws(json, len, colon + 1u);
                    if (start < len && json[start] == '[') {
                        end = gltf_json_find_matching(json, len, start, '[', ']');
                        if (end != SIZE_MAX) {
                            if (out_start)
                                *out_start = start;
                            if (out_end)
                                *out_end = end;
                            return 1;
                        }
                    }
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            object_depth++;
        else if (c == '}')
            object_depth--;
        else if (c == '[')
            array_depth++;
        else if (c == ']')
            array_depth--;
        pos++;
    }
    return 0;
}

static char *gltf_json_object_get_string(const char *json,
                                         size_t len,
                                         size_t obj_start,
                                         size_t obj_end,
                                         const char *key) {
    int depth = 0;
    size_t pos = obj_start;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return NULL;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t value;
                if (colon < obj_end && json[colon] == ':') {
                    value = gltf_json_skip_ws(json, len, colon + 1u);
                    if (value < obj_end && json[value] == '"')
                        return gltf_json_read_string_alloc(json, len, value, NULL);
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return NULL;
            continue;
        }
        pos++;
    }
    return NULL;
}

static size_t gltf_json_object_get_size(const char *json,
                                        size_t len,
                                        size_t obj_start,
                                        size_t obj_end,
                                        const char *key,
                                        size_t fallback) {
    int depth = 0;
    size_t pos = obj_start;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return fallback;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t value;
                size_t result = 0;
                if (colon < obj_end && json[colon] == ':') {
                    value = gltf_json_skip_ws(json, len, colon + 1u);
                    if (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                        while (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                            size_t digit = (size_t)(json[value] - '0');
                            if (result > (SIZE_MAX - digit) / 10u)
                                return fallback;
                            result = result * 10u + digit;
                            value++;
                        }
                        return result;
                    }
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return fallback;
            continue;
        }
        pos++;
    }
    return fallback;
}

static int gltf_json_object_get_int(const char *json,
                                    size_t len,
                                    size_t obj_start,
                                    size_t obj_end,
                                    const char *key,
                                    int fallback) {
    int depth = 0;
    size_t pos = obj_start;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return fallback;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                size_t value;
                int sign = 1;
                int result = 0;
                if (colon < obj_end && json[colon] == ':') {
                    value = gltf_json_skip_ws(json, len, colon + 1u);
                    if (value < obj_end && json[value] == '-') {
                        sign = -1;
                        value++;
                    }
                    if (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                        while (value < obj_end && json[value] >= '0' && json[value] <= '9') {
                            int digit = (int)(json[value] - '0');
                            if (result > (INT_MAX - digit) / 10)
                                return fallback;
                            result = result * 10 + digit;
                            value++;
                        }
                        return sign * result;
                    }
                }
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return fallback;
            continue;
        }
        pos++;
    }
    return fallback;
}

static int gltf_json_object_find_value(const char *json,
                                       size_t len,
                                       size_t obj_start,
                                       size_t obj_end,
                                       const char *key,
                                       size_t *out_start,
                                       size_t *out_end) {
    int depth = 0;
    size_t pos = obj_start;
    if (out_start)
        *out_start = SIZE_MAX;
    if (out_end)
        *out_end = SIZE_MAX;
    while (pos < obj_end && pos < len) {
        char c = json[pos];
        if (c == '"') {
            size_t next = gltf_json_skip_string_raw(json, len, pos);
            int at_key = 0;
            if (next == SIZE_MAX)
                return 0;
            if (depth == 1)
                at_key = gltf_json_key_matches(json, len, pos, key, NULL);
            if (at_key) {
                size_t colon = gltf_json_skip_ws(json, len, next);
                if (colon < obj_end && json[colon] == ':') {
                    size_t value = gltf_json_skip_ws(json, len, colon + 1u);
                    size_t end = gltf_json_skip_value(json, len, value);
                    if (end != SIZE_MAX && end <= obj_end) {
                        if (out_start)
                            *out_start = value;
                        if (out_end)
                            *out_end = end;
                        return 1;
                    }
                }
                return 0;
            }
            pos = next;
            continue;
        }
        if (c == '{')
            depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0)
                break;
        } else if (depth == 1 && c == ':') {
            pos = gltf_json_skip_value(json, len, pos + 1u);
            if (pos == SIZE_MAX)
                return 0;
            continue;
        }
        pos++;
    }
    return 0;
}

static int gltf_json_array_item_range(const char *json,
                                      size_t len,
                                      size_t array_start,
                                      size_t array_end,
                                      int item_index,
                                      size_t *out_start,
                                      size_t *out_end) {
    size_t pos;
    int index = 0;
    if (out_start)
        *out_start = SIZE_MAX;
    if (out_end)
        *out_end = SIZE_MAX;
    if (!json || item_index < 0 || array_start >= array_end || array_end > len ||
        json[array_start] != '[')
        return 0;
    pos = array_start + 1u;
    while (pos < array_end) {
        size_t value_start;
        size_t value_end;
        pos = gltf_json_skip_ws(json, len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        value_start = pos;
        value_end = gltf_json_skip_value(json, len, value_start);
        if (value_end == SIZE_MAX || value_end > array_end)
            return 0;
        if (index == item_index) {
            if (out_start)
                *out_start = value_start;
            if (out_end)
                *out_end = value_end;
            return 1;
        }
        index++;
        pos = gltf_json_skip_ws(json, len, value_end);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    return 0;
}

static double gltf_json_array_get_number(const char *json,
                                         size_t len,
                                         size_t array_start,
                                         size_t array_end,
                                         int item_index,
                                         double fallback) {
    size_t value_start;
    size_t value_end;
    size_t text_len;
    char *text;
    char *endptr;
    double value;
    if (!gltf_json_array_item_range(
            json, len, array_start, array_end, item_index, &value_start, &value_end))
        return fallback;
    value_start = gltf_json_skip_ws(json, len, value_start);
    if (value_start >= value_end || json[value_start] == '"' || json[value_start] == '{' ||
        json[value_start] == '[')
        return fallback;
    text_len = value_end - value_start;
    text = (char *)malloc(text_len + 1u);
    if (!text)
        return fallback;
    memcpy(text, json + value_start, text_len);
    text[text_len] = '\0';
    endptr = NULL;
    value = strtod(text, &endptr);
    if (endptr == text || !isfinite(value))
        value = fallback;
    free(text);
    return value;
}

static char *gltf_json_array_get_string_alloc(const char *json,
                                              size_t len,
                                              size_t array_start,
                                              size_t array_end,
                                              int item_index) {
    size_t value_start;
    size_t value_end;
    if (!gltf_json_array_item_range(
            json, len, array_start, array_end, item_index, &value_start, &value_end))
        return NULL;
    value_start = gltf_json_skip_ws(json, len, value_start);
    if (value_start >= value_end || json[value_start] != '"')
        return NULL;
    return gltf_json_read_string_alloc(json, len, value_start, NULL);
}

static int gltf_json_object_get_boolish(const char *json,
                                        size_t len,
                                        size_t obj_start,
                                        size_t obj_end,
                                        const char *key,
                                        int fallback) {
    size_t value_start;
    size_t value_end;
    size_t value;
    if (!gltf_json_object_find_value(
            json, len, obj_start, obj_end, key, &value_start, &value_end))
        return fallback;
    value = gltf_json_skip_ws(json, len, value_start);
    if (value + 4u <= value_end && strncmp(json + value, "true", 4u) == 0)
        return 1;
    if (value + 5u <= value_end && strncmp(json + value, "false", 5u) == 0)
        return 0;
    return gltf_json_object_get_int(json, len, obj_start, obj_end, key, fallback) ? 1 : 0;
}

static char *gltf_json_copy_from_root_bytes(const uint8_t *data, size_t len, size_t *out_len) {
    char *json_copy = NULL;
    if (out_len)
        *out_len = 0;
    if (!data || len == 0)
        return NULL;
    if (len >= 12 && data[0] == 0x67 && data[1] == 0x6C && data[2] == 0x54 &&
        data[3] == 0x46) {
        uint32_t version = gltf_read_u32_le(data + 4);
        uint32_t declared_len = gltf_read_u32_le(data + 8);
        size_t pos = 12;
        int chunk_index = 0;
        if (version != 2 || declared_len != (uint32_t)len)
            return NULL;
        while (pos + 8u <= len) {
            uint32_t chunk_len = gltf_read_u32_le(data + pos);
            uint32_t chunk_type = gltf_read_u32_le(data + pos + 4u);
            pos += 8u;
            if ((chunk_len & 3u) != 0 || chunk_len > len - pos)
                return NULL;
            if (chunk_index == 0 && chunk_type != 0x4E4F534A)
                return NULL;
            if (chunk_type == 0x4E4F534A) {
                json_copy = (char *)malloc((size_t)chunk_len + 1u);
                if (!json_copy)
                    return NULL;
                memcpy(json_copy, data + pos, (size_t)chunk_len);
                json_copy[chunk_len] = '\0';
                if (out_len)
                    *out_len = (size_t)chunk_len;
                return json_copy;
            }
            pos += (size_t)chunk_len;
            chunk_index++;
        }
        return NULL;
    }
    json_copy = (char *)malloc(len + 1u);
    if (!json_copy)
        return NULL;
    memcpy(json_copy, data, len);
    json_copy[len] = '\0';
    if (out_len)
        *out_len = len;
    return json_copy;
}

static int gltf_preload_bundle_add_image_payload(rt_gltf_preload_bundle *bundle,
                                                 const char *key,
                                                 const char *mime_or_name,
                                                 uint8_t *data,
                                                 size_t data_len,
                                                 int required,
                                                 char *error,
                                                 size_t error_cap) {
    uint8_t *rgba_blob = NULL;
    size_t rgba_len = 0;
    gltf_preload_dependency_kind_t kind = GLTF_PRELOAD_DEP_IMAGE;
    if (!data || data_len == 0)
        return 1;
    if (gltf_decode_image_payload_to_rgba_blob(
            mime_or_name, data, data_len, &rgba_blob, &rgba_len)) {
        free(data);
        data = rgba_blob;
        data_len = rgba_len;
        kind = GLTF_PRELOAD_DEP_IMAGE_RGBA;
    }
    if (required && kind != GLTF_PRELOAD_DEP_IMAGE_RGBA &&
        gltf_preload_image_is_supported_format(mime_or_name)) {
        free(data);
        gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
        return 0;
    }
    if (!gltf_preload_bundle_add_dependency(bundle, key, kind, data, data_len)) {
        free(data);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

static int gltf_preload_stage_external_image(rt_gltf_preload_bundle *bundle,
                                             const char *model_path,
                                             const char *uri,
                                             int load_assets,
                                             int required,
                                             char *error,
                                             size_t error_cap) {
    char resource_path[1024];
    uint8_t *data;
    size_t data_len = 0;
    if (!uri || strncmp(uri, "data:", 5) == 0)
        return 1;
    gltf_resolve_relative_path(model_path, uri, resource_path, sizeof(resource_path));
    if (resource_path[0] == '\0')
        return 1;
    data = gltf_load_dependency_bytes(resource_path, load_assets, 0u, NULL, &data_len);
    if (!data) {
        if (required && gltf_preload_image_is_supported_format(resource_path)) {
            gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
            return 0;
        }
        return 1;
    }
    return gltf_preload_bundle_add_image_payload(
        bundle, resource_path, resource_path, data, data_len, required, error, error_cap);
}

static void gltf_preload_buffer_key(int index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:inline-buffer:%d>", index);
}

static void gltf_preload_image_key(int index, const char *mime_type, char *out, size_t out_cap) {
    const char *ext = ".bin";
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
    snprintf(out, out_cap, "inline-image-%d%s", index, ext);
}

static void gltf_preload_mesh_key(int mesh_index, int primitive_index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:mesh-pod:%d:%d>", mesh_index, primitive_index);
}

static void gltf_preload_morph_key(int mesh_index, int primitive_index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:morph-pod:%d:%d>", mesh_index, primitive_index);
}

static int gltf_root_find_glb_bin(const uint8_t *data,
                                  size_t len,
                                  const uint8_t **out_bin,
                                  size_t *out_bin_len) {
    size_t pos = 12;
    int chunk_index = 0;
    if (out_bin)
        *out_bin = NULL;
    if (out_bin_len)
        *out_bin_len = 0;
    if (!data || len < 12 || data[0] != 0x67 || data[1] != 0x6C || data[2] != 0x54 ||
        data[3] != 0x46)
        return 0;
    if (gltf_read_u32_le(data + 4) != 2 || gltf_read_u32_le(data + 8) != (uint32_t)len)
        return 0;
    while (pos + 8u <= len) {
        uint32_t chunk_len = gltf_read_u32_le(data + pos);
        uint32_t chunk_type = gltf_read_u32_le(data + pos + 4u);
        pos += 8u;
        if ((chunk_len & 3u) != 0 || chunk_len > len - pos)
            return 0;
        if (chunk_index == 0 && chunk_type != 0x4E4F534A)
            return 0;
        if (chunk_type == 0x004E4942) {
            if (out_bin)
                *out_bin = data + pos;
            if (out_bin_len)
                *out_bin_len = (size_t)chunk_len;
            return 1;
        }
        pos += (size_t)chunk_len;
        chunk_index++;
    }
    return 0;
}

static int gltf_preload_grow_buffer_refs(gltf_preload_buffer_ref_t **refs,
                                         int *capacity,
                                         int required_count) {
    gltf_preload_buffer_ref_t *grown;
    int next_capacity;
    if (!refs || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    next_capacity = *capacity ? *capacity * 2 : 8;
    while (next_capacity < required_count)
        next_capacity *= 2;
    grown = (gltf_preload_buffer_ref_t *)realloc(
        *refs, (size_t)next_capacity * sizeof(**refs));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *refs = grown;
    *capacity = next_capacity;
    return 1;
}

static int gltf_preload_stage_buffers(rt_gltf_preload_bundle *bundle,
                                      const char *model_path,
                                      const char *json,
                                      size_t json_len,
                                      int load_assets,
                                      gltf_preload_buffer_ref_t **out_refs,
                                      int *out_count,
                                      char *error,
                                      size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    const uint8_t *glb_bin = NULL;
    size_t glb_bin_len = 0;
    gltf_preload_buffer_ref_t *refs = NULL;
    int refs_capacity = 0;
    int index = 0;
    if (out_refs)
        *out_refs = NULL;
    if (out_count)
        *out_count = 0;
    gltf_root_find_glb_bin(bundle ? bundle->root_data : NULL,
                           bundle ? bundle->root_size : 0,
                           &glb_bin,
                           &glb_bin_len);
    if (!gltf_json_find_top_level_array(json, json_len, "buffers", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *uri;
            size_t required_len = 0;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!gltf_preload_grow_buffer_refs(&refs, &refs_capacity, index + 1)) {
                free(refs);
                gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                return 0;
            }
            uri = gltf_json_object_get_string(json, json_len, pos, object_end, "uri");
            required_len =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteLength", 0u);
            if (uri) {
                int ok = 1;
                if (strncmp(uri, "data:", 5) == 0) {
                    char key[64];
                    char mime_type[64];
                    uint8_t *data = NULL;
                    size_t data_len = 0;
                    if (!gltf_parse_data_uri(
                            uri, mime_type, sizeof(mime_type), &data, &data_len) ||
                        data_len < required_len) {
                        free(data);
                        ok = required_len == 0u;
                    } else {
                        gltf_preload_buffer_key(index, key, sizeof(key));
                        ok = gltf_preload_bundle_add_dependency(
                            bundle, key, GLTF_PRELOAD_DEP_BUFFER, data, data_len);
                        if (ok) {
                            refs[index].data = data;
                            refs[index].len = data_len;
                        } else {
                            free(data);
                        }
                    }
                } else {
                    char resource_path[1024];
                    uint8_t *data = NULL;
                    size_t data_len = 0;
                    gltf_resolve_relative_path(model_path, uri, resource_path, sizeof(resource_path));
                    if (resource_path[0] != '\0') {
                        data = gltf_load_dependency_bytes(
                            resource_path, load_assets, required_len, NULL, &data_len);
                        if (data) {
                            ok = gltf_preload_bundle_add_dependency(
                                bundle, resource_path, GLTF_PRELOAD_DEP_BUFFER, data, data_len);
                            if (ok) {
                                refs[index].data = data;
                                refs[index].len = data_len;
                            } else {
                                free(data);
                            }
                        }
                    }
                    if (!data && required_len > 0u)
                        ok = 0;
                }
                free(uri);
                if (!ok) {
                    free(refs);
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
            } else if (index == 0 && glb_bin && required_len <= glb_bin_len &&
                       required_len <= SIZE_MAX - 3u && glb_bin_len <= required_len + 3u) {
                refs[index].data = glb_bin;
                refs[index].len = required_len;
            }
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (out_refs)
        *out_refs = refs;
    else
        free(refs);
    if (out_count)
        *out_count = index;
    return 1;
}

static int gltf_preload_grow_view_refs(gltf_preload_buffer_view_ref_t **views,
                                       int *capacity,
                                       int required_count) {
    gltf_preload_buffer_view_ref_t *grown;
    int next_capacity;
    if (!views || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    next_capacity = *capacity ? *capacity * 2 : 8;
    while (next_capacity < required_count)
        next_capacity *= 2;
    grown = (gltf_preload_buffer_view_ref_t *)realloc(
        *views, (size_t)next_capacity * sizeof(**views));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *views = grown;
    *capacity = next_capacity;
    return 1;
}

static int gltf_preload_parse_buffer_views(const char *json,
                                           size_t json_len,
                                           gltf_preload_buffer_view_ref_t **out_views,
                                           int *out_count,
                                           char *error,
                                           size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    gltf_preload_buffer_view_ref_t *views = NULL;
    int views_capacity = 0;
    int index = 0;
    if (out_views)
        *out_views = NULL;
    if (out_count)
        *out_count = 0;
    if (!gltf_json_find_top_level_array(json, json_len, "bufferViews", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!gltf_preload_grow_view_refs(&views, &views_capacity, index + 1)) {
                free(views);
                gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                return 0;
            }
            views[index].buffer =
                gltf_json_object_get_int(json, json_len, pos, object_end, "buffer", -1);
            views[index].byte_offset =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteOffset", 0u);
            views[index].byte_length =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteLength", 0u);
            views[index].byte_stride =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteStride", 0u);
            views[index].valid = views[index].buffer >= 0 && views[index].byte_length > 0;
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (out_views)
        *out_views = views;
    else
        free(views);
    if (out_count)
        *out_count = index;
    return 1;
}

static int gltf_preload_validate_accessor(const char *json,
                                          size_t json_len,
                                          size_t obj_start,
                                          size_t obj_end,
                                          const gltf_preload_buffer_ref_t *buffers,
                                          int buffer_count,
                                          const gltf_preload_buffer_view_ref_t *views,
                                          int view_count) {
    int view_index;
    int component_type;
    int component_size;
    int component_count;
    int count;
    size_t accessor_offset;
    size_t element_size;
    size_t stride;
    size_t last_offset;
    size_t accessor_last;
    size_t accessor_end;
    size_t buffer_end;
    char *type;
    const gltf_preload_buffer_view_ref_t *view;

    count = gltf_json_object_get_int(json, json_len, obj_start, obj_end, "count", -1);
    if (count < 0)
        return 0;
    if (count == 0)
        return 1;

    component_type =
        gltf_json_object_get_int(json, json_len, obj_start, obj_end, "componentType", 0);
    component_size = gltf_component_size(component_type);
    type = gltf_json_object_get_string(json, json_len, obj_start, obj_end, "type");
    component_count = gltf_component_count(type);
    free(type);
    if (component_size <= 0 || component_count <= 0)
        return 0;
    if (!gltf_checked_mul_size((size_t)component_size, (size_t)component_count, &element_size))
        return 0;

    view_index = gltf_json_object_get_int(json, json_len, obj_start, obj_end, "bufferView", -1);
    if (view_index < 0)
        return 1;
    if (!views || view_index >= view_count || !views[view_index].valid)
        return 0;
    view = &views[view_index];
    if (view->buffer < 0 || view->buffer >= buffer_count || !buffers || !buffers[view->buffer].data)
        return 0;

    accessor_offset =
        gltf_json_object_get_size(json, json_len, obj_start, obj_end, "byteOffset", 0u);
    stride = view->byte_stride > 0u ? view->byte_stride : element_size;
    if (stride < element_size)
        return 0;
    if (!gltf_checked_mul_size((size_t)(count - 1), stride, &last_offset) ||
        !gltf_checked_add_size(accessor_offset, last_offset, &accessor_last) ||
        !gltf_checked_add_size(accessor_last, element_size, &accessor_end))
        return 0;
    if (accessor_end > view->byte_length)
        return 0;
    if (!gltf_checked_add_size(view->byte_offset, accessor_end, &buffer_end))
        return 0;
    return buffer_end <= buffers[view->buffer].len;
}

static int gltf_preload_validate_accessors(const char *json,
                                           size_t json_len,
                                           const gltf_preload_buffer_ref_t *buffers,
                                           int buffer_count,
                                           const gltf_preload_buffer_view_ref_t *views,
                                           int view_count,
                                           char *error,
                                           size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    if (!gltf_json_find_top_level_array(json, json_len, "accessors", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!gltf_preload_validate_accessor(
                    json, json_len, pos, object_end, buffers, buffer_count, views, view_count)) {
                gltf_preload_set_error(error, error_cap, "invalid glTF accessor range");
                return 0;
            }
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    return 1;
}

static int gltf_preload_grow_accessor_refs(gltf_preload_accessor_ref_t **accessors,
                                           int *capacity,
                                           int required_count) {
    gltf_preload_accessor_ref_t *grown;
    int next_capacity;
    if (!accessors || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    next_capacity = *capacity ? *capacity * 2 : 8;
    while (next_capacity < required_count)
        next_capacity *= 2;
    grown = (gltf_preload_accessor_ref_t *)realloc(
        *accessors, (size_t)next_capacity * sizeof(**accessors));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *accessors = grown;
    *capacity = next_capacity;
    return 1;
}

static int gltf_preload_parse_accessors(const char *json,
                                        size_t json_len,
                                        gltf_preload_accessor_ref_t **out_accessors,
                                        int *out_count,
                                        char *error,
                                        size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    gltf_preload_accessor_ref_t *accessors = NULL;
    int accessors_capacity = 0;
    int index = 0;
    if (out_accessors)
        *out_accessors = NULL;
    if (out_count)
        *out_count = 0;
    if (!gltf_json_find_top_level_array(json, json_len, "accessors", &array_start, &array_end))
        return 1;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *type;
            size_t sparse_start;
            size_t sparse_end;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (!gltf_preload_grow_accessor_refs(&accessors, &accessors_capacity, index + 1)) {
                free(accessors);
                gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                return 0;
            }
            type = gltf_json_object_get_string(json, json_len, pos, object_end, "type");
            accessors[index].view =
                gltf_json_object_get_int(json, json_len, pos, object_end, "bufferView", -1);
            accessors[index].sparse_indices_view = -1;
            accessors[index].sparse_values_view = -1;
            accessors[index].byte_offset =
                gltf_json_object_get_size(json, json_len, pos, object_end, "byteOffset", 0u);
            accessors[index].comp_type =
                gltf_json_object_get_int(json, json_len, pos, object_end, "componentType", 0);
            accessors[index].comp_count = gltf_component_count(type);
            accessors[index].count =
                gltf_json_object_get_int(json, json_len, pos, object_end, "count", 0);
            accessors[index].normalized = (int8_t)gltf_json_object_get_boolish(
                json, json_len, pos, object_end, "normalized", 0);
            if (gltf_json_object_find_value(
                    json, json_len, pos, object_end, "sparse", &sparse_start, &sparse_end) &&
                sparse_start < sparse_end && json[sparse_start] == '{') {
                size_t indices_start;
                size_t indices_end;
                size_t values_start;
                size_t values_end;
                accessors[index].sparse_count =
                    gltf_json_object_get_int(json, json_len, sparse_start, sparse_end, "count", 0);
                if (accessors[index].sparse_count > 0 &&
                    gltf_json_object_find_value(json,
                                                json_len,
                                                sparse_start,
                                                sparse_end,
                                                "indices",
                                                &indices_start,
                                                &indices_end) &&
                    gltf_json_object_find_value(json,
                                                json_len,
                                                sparse_start,
                                                sparse_end,
                                                "values",
                                                &values_start,
                                                &values_end) &&
                    indices_start < indices_end && values_start < values_end &&
                    json[indices_start] == '{' && json[values_start] == '{') {
                    accessors[index].has_sparse = 1;
                    accessors[index].sparse_indices_view = gltf_json_object_get_int(
                        json, json_len, indices_start, indices_end, "bufferView", -1);
                    accessors[index].sparse_indices_offset = gltf_json_object_get_size(
                        json, json_len, indices_start, indices_end, "byteOffset", 0u);
                    accessors[index].sparse_index_comp_type = gltf_json_object_get_int(
                        json, json_len, indices_start, indices_end, "componentType", 0);
                    accessors[index].sparse_values_view = gltf_json_object_get_int(
                        json, json_len, values_start, values_end, "bufferView", -1);
                    accessors[index].sparse_values_offset = gltf_json_object_get_size(
                        json, json_len, values_start, values_end, "byteOffset", 0u);
                }
            }
            accessors[index].valid = accessors[index].count > 0 &&
                                     gltf_component_size(accessors[index].comp_type) > 0 &&
                                     accessors[index].comp_count > 0;
            free(type);
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (out_accessors)
        *out_accessors = accessors;
    else
        free(accessors);
    if (out_count)
        *out_count = index;
    return 1;
}

static int gltf_preload_resolve_accessor_view(
    const gltf_preload_accessor_ref_t *accessors,
    int accessor_count,
    int accessor_index,
    const gltf_preload_buffer_ref_t *buffers,
    int buffer_count,
    const gltf_preload_buffer_view_ref_t *views,
    int view_count,
    gltf_accessor_view_t *out) {
    const gltf_preload_accessor_ref_t *accessor;
    const gltf_preload_buffer_view_ref_t *view;
    size_t element_size;
    size_t stride;
    size_t last_offset;
    size_t accessor_last;
    size_t accessor_end;
    size_t buffer_offset;
    size_t buffer_end;
    int comp_size;
    if (!accessors || accessor_index < 0 || accessor_index >= accessor_count || !out)
        return 0;
    accessor = &accessors[accessor_index];
    if (!accessor->valid)
        return 0;
    comp_size = gltf_component_size(accessor->comp_type);
    if (comp_size <= 0 || accessor->comp_count <= 0 || accessor->count <= 0)
        return 0;
    if (!gltf_checked_mul_size((size_t)comp_size, (size_t)accessor->comp_count, &element_size))
        return 0;
    if (element_size > INT32_MAX)
        return 0;
    memset(out, 0, sizeof(*out));
    out->count = (int32_t)accessor->count;
    out->stride = (int32_t)element_size;
    out->comp_type = accessor->comp_type;
    out->comp_count = accessor->comp_count;
    out->normalized = accessor->normalized;
    if (accessor->view >= 0) {
        if (accessor->view >= view_count)
            return 0;
        view = views ? &views[accessor->view] : NULL;
        if (!view || !view->valid || view->buffer < 0 || view->buffer >= buffer_count ||
            !buffers || !buffers[view->buffer].data)
            return 0;
        stride = view->byte_stride > 0u ? view->byte_stride : element_size;
        if (stride < element_size || stride > INT32_MAX)
            return 0;
        if (!gltf_checked_mul_size((size_t)(accessor->count - 1), stride, &last_offset) ||
            !gltf_checked_add_size(accessor->byte_offset, last_offset, &accessor_last) ||
            !gltf_checked_add_size(accessor_last, element_size, &accessor_end))
            return 0;
        if (accessor_end > view->byte_length)
            return 0;
        if (!gltf_checked_add_size(view->byte_offset, accessor->byte_offset, &buffer_offset) ||
            !gltf_checked_add_size(view->byte_offset, accessor_end, &buffer_end))
            return 0;
        if (buffer_end > buffers[view->buffer].len)
            return 0;
        out->data = buffers[view->buffer].data + buffer_offset;
        out->stride = (int32_t)stride;
    } else if (!accessor->has_sparse) {
        return 0;
    }
    if (accessor->has_sparse) {
        const gltf_preload_buffer_view_ref_t *indices_view;
        const gltf_preload_buffer_view_ref_t *values_view;
        int index_comp_size = gltf_component_size(accessor->sparse_index_comp_type);
        size_t index_bytes;
        size_t value_bytes;
        size_t index_end;
        size_t value_end;
        size_t index_buffer_offset;
        size_t value_buffer_offset;
        uint32_t previous_sparse_index = 0;
        if (accessor->sparse_count <= 0 ||
            (accessor->sparse_index_comp_type != 5121 &&
             accessor->sparse_index_comp_type != 5123 &&
             accessor->sparse_index_comp_type != 5125) ||
            index_comp_size <= 0 || accessor->sparse_indices_view < 0 ||
            accessor->sparse_indices_view >= view_count || accessor->sparse_values_view < 0 ||
            accessor->sparse_values_view >= view_count || !views || !buffers)
            return 0;
        indices_view = &views[accessor->sparse_indices_view];
        values_view = &views[accessor->sparse_values_view];
        if (!indices_view->valid || !values_view->valid || indices_view->buffer < 0 ||
            values_view->buffer < 0 || indices_view->buffer >= buffer_count ||
            values_view->buffer >= buffer_count || !buffers[indices_view->buffer].data ||
            !buffers[values_view->buffer].data)
            return 0;
        if (!gltf_checked_mul_size(
                (size_t)accessor->sparse_count, (size_t)index_comp_size, &index_bytes) ||
            !gltf_checked_mul_size(
                (size_t)accessor->sparse_count, element_size, &value_bytes) ||
            !gltf_checked_add_size(accessor->sparse_indices_offset, index_bytes, &index_end) ||
            !gltf_checked_add_size(accessor->sparse_values_offset, value_bytes, &value_end))
            return 0;
        if (index_end > indices_view->byte_length || value_end > values_view->byte_length)
            return 0;
        if (!gltf_checked_add_size(
                indices_view->byte_offset, accessor->sparse_indices_offset, &index_buffer_offset) ||
            !gltf_checked_add_size(
                values_view->byte_offset, accessor->sparse_values_offset, &value_buffer_offset) ||
            !gltf_checked_add_size(index_buffer_offset, index_bytes, &buffer_end) ||
            buffer_end > buffers[indices_view->buffer].len ||
            !gltf_checked_add_size(value_buffer_offset, value_bytes, &buffer_end) ||
            buffer_end > buffers[values_view->buffer].len)
            return 0;
        out->sparse_indices = buffers[indices_view->buffer].data + index_buffer_offset;
        out->sparse_values = buffers[values_view->buffer].data + value_buffer_offset;
        out->sparse_count = (int32_t)accessor->sparse_count;
        out->sparse_index_comp_type = accessor->sparse_index_comp_type;
        out->sparse_index_stride = index_comp_size;
        out->sparse_value_stride = (int32_t)element_size;
        for (int32_t si = 0; si < out->sparse_count; si++) {
            uint32_t sparse_index = gltf_decode_component_u32(
                out->sparse_indices + (size_t)si * (size_t)out->sparse_index_stride,
                out->sparse_index_comp_type);
            if (sparse_index >= (uint32_t)out->count ||
                (si > 0 && sparse_index <= previous_sparse_index))
                return 0;
            previous_sparse_index = sparse_index;
        }
    }
    return 1;
}

static int gltf_preload_floats_are_finite(const float *values, int count) {
    if (!values || count <= 0)
        return 0;
    for (int i = 0; i < count; i++) {
        if (!isfinite(values[i]))
            return 0;
    }
    return 1;
}

static int gltf_preload_pod_positions_form_triangle(const vgfx3d_vertex_t *vertices,
                                                    uint32_t vertex_count,
                                                    uint32_t i0,
                                                    uint32_t i1,
                                                    uint32_t i2) {
    const float *p0;
    const float *p1;
    const float *p2;
    double e1[3];
    double e2[3];
    double nx;
    double ny;
    double nz;
    double area_sq;
    if (!vertices || i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
        return 0;
    p0 = vertices[i0].pos;
    p1 = vertices[i1].pos;
    p2 = vertices[i2].pos;
    e1[0] = (double)p1[0] - (double)p0[0];
    e1[1] = (double)p1[1] - (double)p0[1];
    e1[2] = (double)p1[2] - (double)p0[2];
    e2[0] = (double)p2[0] - (double)p0[0];
    e2[1] = (double)p2[1] - (double)p0[1];
    e2[2] = (double)p2[2] - (double)p0[2];
    nx = e1[1] * e2[2] - e1[2] * e2[1];
    ny = e1[2] * e2[0] - e1[0] * e2[2];
    nz = e1[0] * e2[1] - e1[1] * e2[0];
    area_sq = nx * nx + ny * ny + nz * nz;
    return isfinite(area_sq) && area_sq > 1e-20;
}

static int gltf_preload_emit_pod_triangle(const vgfx3d_vertex_t *vertices,
                                          uint32_t vertex_count,
                                          uint32_t i0,
                                          uint32_t i1,
                                          uint32_t i2,
                                          uint32_t *indices,
                                          uint32_t *index_count) {
    if (!indices || !index_count || i0 == i1 || i1 == i2 || i0 == i2)
        return 0;
    if (!gltf_preload_pod_positions_form_triangle(vertices, vertex_count, i0, i1, i2))
        return 0;
    indices[(*index_count)++] = i0;
    indices[(*index_count)++] = i1;
    indices[(*index_count)++] = i2;
    return 1;
}

static uint32_t gltf_preload_read_index_or_vertex(const gltf_accessor_view_t *view,
                                                  int32_t element_index) {
    uint32_t value = (uint32_t)element_index;
    if (view)
        gltf_accessor_read_u32(view, element_index, &value, 1);
    return value;
}

static int gltf_preload_topology_index_capacity(int mode,
                                                int32_t source_index_count,
                                                uint32_t *out_capacity) {
    size_t tri_count;
    size_t index_capacity;
    if (out_capacity)
        *out_capacity = 0;
    if (source_index_count < 3 || !out_capacity)
        return 1;
    if (mode == 4)
        tri_count = (size_t)(source_index_count / 3);
    else if (mode == 5 || mode == 6)
        tri_count = (size_t)(source_index_count - 2);
    else
        return 0;
    if (!gltf_checked_mul_size(tri_count, 3u, &index_capacity) ||
        index_capacity > UINT32_MAX) {
        return 0;
    }
    *out_capacity = (uint32_t)index_capacity;
    return 1;
}

static uint32_t gltf_preload_write_vertex_bone_weights(vgfx3d_vertex_t *vertex,
                                                       const uint32_t joints[4],
                                                       const float weights[4]) {
    double sum = 0.0;
    int32_t max_bone_index = -1;
    if (!vertex || !joints || !weights)
        return 0u;
    for (int i = 0; i < 4; i++) {
        if (joints[i] >= VGFX3D_MAX_BONES) {
            vertex->bone_indices[i] = 0u;
            vertex->bone_weights[i] = 0.0f;
            continue;
        }
        vertex->bone_indices[i] = (uint8_t)joints[i];
        if (isfinite(weights[i]) && weights[i] > 0.0f) {
            vertex->bone_weights[i] = weights[i];
            sum += (double)weights[i];
        } else {
            vertex->bone_weights[i] = 0.0f;
        }
        if ((int32_t)joints[i] > max_bone_index)
            max_bone_index = (int32_t)joints[i];
    }
    if (sum > 1e-12) {
        for (int i = 0; i < 4; i++)
            vertex->bone_weights[i] = (float)((double)vertex->bone_weights[i] / sum);
    } else {
        for (int i = 0; i < 4; i++)
            vertex->bone_weights[i] = 0.0f;
    }
    return max_bone_index >= 0 ? (uint32_t)(max_bone_index + 1) : 0u;
}

static int gltf_preload_pack_mesh_pod(rt_gltf_preload_bundle *bundle,
                                      int mesh_index,
                                      int primitive_index,
                                      uint32_t flags,
                                      vgfx3d_vertex_t *vertices,
                                      uint32_t vertex_count,
                                      uint32_t *indices,
                                      uint32_t index_count,
                                      uint32_t bone_count,
                                      char *error,
                                      size_t error_cap) {
    uint8_t *blob;
    size_t vertex_bytes;
    size_t index_bytes;
    size_t payload_offset;
    size_t blob_len;
    char key[96];
    if (!vertices || !indices || vertex_count == 0 || index_count == 0)
        return 1;
    if (bone_count > VGFX3D_MAX_BONES)
        return 1;
    if (!gltf_checked_mul_size((size_t)vertex_count, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !gltf_checked_mul_size((size_t)index_count, sizeof(uint32_t), &index_bytes) ||
        !gltf_checked_add_size(
            GLTF_PRELOAD_MESH_POD_HEADER_SIZE, vertex_bytes, &payload_offset) ||
        !gltf_checked_add_size(payload_offset, index_bytes, &blob_len) ||
        vertex_bytes > UINT32_MAX || index_bytes > UINT32_MAX) {
        return 1;
    }
    blob = (uint8_t *)malloc(blob_len);
    if (!blob) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    memset(blob, 0, GLTF_PRELOAD_MESH_POD_HEADER_SIZE);
    gltf_write_u32_le(blob + 0, GLTF_PRELOAD_MESH_POD_MAGIC);
    gltf_write_u32_le(blob + 4, GLTF_PRELOAD_MESH_POD_VERSION);
    gltf_write_u32_le(blob + 8, vertex_count);
    gltf_write_u32_le(blob + 12, index_count);
    gltf_write_u32_le(blob + 16, flags);
    gltf_write_u32_le(blob + 20, (uint32_t)vertex_bytes);
    gltf_write_u32_le(blob + 24, (uint32_t)index_bytes);
    gltf_write_u32_le(blob + 28, bone_count);
    memcpy(blob + GLTF_PRELOAD_MESH_POD_HEADER_SIZE, vertices, vertex_bytes);
    memcpy(blob + payload_offset, indices, index_bytes);
    gltf_preload_mesh_key(mesh_index, primitive_index, key, sizeof(key));
    if (!gltf_preload_bundle_add_dependency(bundle, key, GLTF_PRELOAD_DEP_MESH_POD, blob, blob_len)) {
        free(blob);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

typedef struct {
    char *name;
    float weight;
    uint32_t flags;
    float *pos_deltas;
    float *normal_deltas;
    float *tangent_deltas;
} gltf_preload_morph_shape_t;

static void gltf_preload_free_morph_shapes(gltf_preload_morph_shape_t *shapes,
                                           size_t shape_count) {
    if (!shapes)
        return;
    for (size_t i = 0; i < shape_count; i++) {
        free(shapes[i].name);
        free(shapes[i].pos_deltas);
        free(shapes[i].normal_deltas);
        free(shapes[i].tangent_deltas);
    }
    free(shapes);
}

static char *gltf_preload_target_name_alloc(const char *json,
                                            size_t json_len,
                                            size_t mesh_start,
                                            size_t mesh_end,
                                            int target_index) {
    size_t extras_start;
    size_t extras_end;
    size_t names_start;
    size_t names_end;
    char fallback[64];
    char *name = NULL;
    if (gltf_json_object_find_value(
            json, json_len, mesh_start, mesh_end, "extras", &extras_start, &extras_end) &&
        extras_start < extras_end && json[extras_start] == '{' &&
        gltf_json_object_find_value(json,
                                    json_len,
                                    extras_start,
                                    extras_end,
                                    "targetNames",
                                    &names_start,
                                    &names_end) &&
        names_start < names_end && json[names_start] == '[') {
        name = gltf_json_array_get_string_alloc(json, json_len, names_start, names_end, target_index);
    }
    if (name && name[0] != '\0')
        return name;
    free(name);
    snprintf(fallback, sizeof(fallback), "target_%d", target_index);
    return gltf_strdup_cstr(fallback);
}

static double gltf_preload_target_weight(const char *json,
                                         size_t json_len,
                                         size_t mesh_start,
                                         size_t mesh_end,
                                         int target_index) {
    size_t weights_start;
    size_t weights_end;
    if (!gltf_json_object_find_value(
            json, json_len, mesh_start, mesh_end, "weights", &weights_start, &weights_end) ||
        weights_start >= weights_end || json[weights_start] != '[')
        return 0.0;
    return gltf_json_array_get_number(json, json_len, weights_start, weights_end, target_index, 0.0);
}

static int gltf_preload_copy_morph_channel(const gltf_accessor_view_t *view,
                                           int32_t vertex_count,
                                           float **out_deltas) {
    size_t bytes;
    float *deltas;
    int32_t limit;
    if (out_deltas)
        *out_deltas = NULL;
    if (!view || !out_deltas || vertex_count <= 0)
        return 1;
    if (!gltf_checked_mul_size((size_t)vertex_count, 3u * sizeof(float), &bytes))
        return 0;
    deltas = (float *)calloc(1, bytes);
    if (!deltas)
        return 0;
    limit = view->count < vertex_count ? view->count : vertex_count;
    for (int32_t vi = 0; vi < limit; vi++) {
        float sample[3] = {0.0f, 0.0f, 0.0f};
        gltf_accessor_read_f32(view, vi, sample, 3);
        for (int c = 0; c < 3; c++)
            deltas[(size_t)vi * 3u + (size_t)c] = isfinite(sample[c]) ? sample[c] : 0.0f;
    }
    *out_deltas = deltas;
    return 1;
}

static int gltf_preload_pack_morph_pod(rt_gltf_preload_bundle *bundle,
                                       int mesh_index,
                                       int primitive_index,
                                       const gltf_preload_morph_shape_t *shapes,
                                       size_t shape_count,
                                       uint32_t vertex_count,
                                       char *error,
                                       size_t error_cap) {
    size_t channel_bytes;
    size_t record_bytes;
    size_t names_bytes = 0;
    size_t payload_bytes = 0;
    size_t names_offset;
    size_t payload_offset;
    size_t blob_len;
    size_t name_cursor = 0;
    size_t payload_cursor = 0;
    uint8_t *blob;
    char key[96];
    if (!bundle || !shapes || shape_count == 0 || vertex_count == 0)
        return 1;
    if (shape_count > INT32_MAX || vertex_count > INT32_MAX)
        return 1;
    if (!gltf_checked_mul_size((size_t)vertex_count, 3u * sizeof(float), &channel_bytes) ||
        !gltf_checked_mul_size(shape_count, GLTF_PRELOAD_MORPH_POD_RECORD_SIZE, &record_bytes))
        return 1;
    for (size_t i = 0; i < shape_count; i++) {
        size_t name_len = shapes[i].name ? strlen(shapes[i].name) + 1u : 1u;
        if (!gltf_checked_add_size(names_bytes, name_len, &names_bytes))
            return 1;
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS) != 0 &&
            !gltf_checked_add_size(payload_bytes, channel_bytes, &payload_bytes))
            return 1;
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0 &&
            !gltf_checked_add_size(payload_bytes, channel_bytes, &payload_bytes))
            return 1;
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0 &&
            !gltf_checked_add_size(payload_bytes, channel_bytes, &payload_bytes))
            return 1;
    }
    if (!gltf_checked_add_size(GLTF_PRELOAD_MORPH_POD_HEADER_SIZE, record_bytes, &names_offset) ||
        !gltf_checked_add_size(names_offset, names_bytes, &payload_offset) ||
        !gltf_checked_add_size(payload_offset, payload_bytes, &blob_len) ||
        record_bytes > UINT32_MAX || names_bytes > UINT32_MAX || payload_bytes > UINT32_MAX)
        return 1;
    blob = (uint8_t *)calloc(1, blob_len);
    if (!blob) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    gltf_write_u32_le(blob + 0, GLTF_PRELOAD_MORPH_POD_MAGIC);
    gltf_write_u32_le(blob + 4, GLTF_PRELOAD_MORPH_POD_VERSION);
    gltf_write_u32_le(blob + 8, vertex_count);
    gltf_write_u32_le(blob + 12, (uint32_t)shape_count);
    gltf_write_u32_le(blob + 16, (uint32_t)record_bytes);
    gltf_write_u32_le(blob + 20, (uint32_t)names_bytes);
    gltf_write_u32_le(blob + 24, (uint32_t)payload_bytes);
    gltf_write_u32_le(blob + 28, 0u);

    for (size_t i = 0; i < shape_count; i++) {
        uint8_t *record = blob + GLTF_PRELOAD_MORPH_POD_HEADER_SIZE +
                          i * GLTF_PRELOAD_MORPH_POD_RECORD_SIZE;
        const char *name = shapes[i].name ? shapes[i].name : "";
        size_t name_len = strlen(name) + 1u;
        uint32_t pos_offset = 0u;
        uint32_t norm_offset = 0u;
        uint32_t tan_offset = 0u;
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS) != 0) {
            pos_offset = (uint32_t)payload_cursor;
            memcpy(blob + payload_offset + payload_cursor, shapes[i].pos_deltas, channel_bytes);
            payload_cursor += channel_bytes;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0) {
            norm_offset = (uint32_t)payload_cursor;
            memcpy(blob + payload_offset + payload_cursor, shapes[i].normal_deltas, channel_bytes);
            payload_cursor += channel_bytes;
        }
        if ((shapes[i].flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0) {
            tan_offset = (uint32_t)payload_cursor;
            memcpy(blob + payload_offset + payload_cursor, shapes[i].tangent_deltas, channel_bytes);
            payload_cursor += channel_bytes;
        }
        memcpy(blob + names_offset + name_cursor, name, name_len);
        gltf_write_u32_le(record + 0, shapes[i].flags);
        gltf_write_u32_le(record + 4, (uint32_t)name_cursor);
        gltf_write_u32_le(record + 8, (uint32_t)name_len);
        gltf_write_f32_le(record + 12, shapes[i].weight);
        gltf_write_u32_le(record + 16, pos_offset);
        gltf_write_u32_le(record + 20, norm_offset);
        gltf_write_u32_le(record + 24, tan_offset);
        gltf_write_u32_le(record + 28, 0u);
        name_cursor += name_len;
    }

    gltf_preload_morph_key(mesh_index, primitive_index, key, sizeof(key));
    if (!gltf_preload_bundle_add_dependency(
            bundle, key, GLTF_PRELOAD_DEP_MORPH_POD, blob, blob_len)) {
        free(blob);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

static int gltf_preload_stage_morph_targets(rt_gltf_preload_bundle *bundle,
                                            const char *json,
                                            size_t json_len,
                                            int mesh_index,
                                            int primitive_index,
                                            size_t mesh_start,
                                            size_t mesh_end,
                                            size_t prim_start,
                                            size_t prim_end,
                                            const gltf_preload_buffer_ref_t *buffers,
                                            int buffer_count,
                                            const gltf_preload_buffer_view_ref_t *views,
                                            int view_count,
                                            const gltf_preload_accessor_ref_t *accessors,
                                            int accessor_count,
                                            int32_t vertex_count,
                                            char *error,
                                            size_t error_cap) {
    size_t targets_start;
    size_t targets_end;
    size_t target_pos;
    gltf_preload_morph_shape_t *shapes = NULL;
    size_t shape_count = 0;
    int target_index = 0;
    int ok = 1;
    if (!bundle || !json || vertex_count <= 0)
        return 1;
    if (!gltf_json_object_find_value(
            json, json_len, prim_start, prim_end, "targets", &targets_start, &targets_end) ||
        targets_start >= targets_end || json[targets_start] != '[')
        return 1;
    target_pos = targets_start + 1u;
    while (target_pos < targets_end) {
        target_pos = gltf_json_skip_ws(json, json_len, target_pos);
        if (target_pos >= targets_end || json[target_pos] == ']')
            break;
        if (json[target_pos] == '{') {
            size_t target_end = gltf_json_find_matching(json, json_len, target_pos, '{', '}');
            int pos_acc;
            int norm_acc;
            int tangent_acc;
            int has_pos;
            int has_norm;
            int has_tangent;
            gltf_accessor_view_t pos_view;
            gltf_accessor_view_t norm_view;
            gltf_accessor_view_t tangent_view;
            if (target_end == SIZE_MAX || target_end > targets_end)
                break;
            pos_acc = gltf_json_object_get_int(
                json, json_len, target_pos, target_end, "POSITION", -1);
            norm_acc =
                gltf_json_object_get_int(json, json_len, target_pos, target_end, "NORMAL", -1);
            tangent_acc =
                gltf_json_object_get_int(json, json_len, target_pos, target_end, "TANGENT", -1);
            has_pos = gltf_preload_resolve_accessor_view(accessors,
                                                         accessor_count,
                                                         pos_acc,
                                                         buffers,
                                                         buffer_count,
                                                         views,
                                                         view_count,
                                                         &pos_view) &&
                      pos_view.comp_count >= 3;
            has_norm = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          norm_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &norm_view) &&
                       norm_view.comp_count >= 3;
            has_tangent = gltf_preload_resolve_accessor_view(accessors,
                                                             accessor_count,
                                                             tangent_acc,
                                                             buffers,
                                                             buffer_count,
                                                             views,
                                                             view_count,
                                                             &tangent_view) &&
                          tangent_view.comp_count >= 3;
            if (has_pos || has_norm || has_tangent) {
                gltf_preload_morph_shape_t *grown =
                    (gltf_preload_morph_shape_t *)realloc(
                        shapes, (shape_count + 1u) * sizeof(*shapes));
                if (!grown) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                shapes = grown;
                memset(&shapes[shape_count], 0, sizeof(shapes[shape_count]));
                shapes[shape_count].name =
                    gltf_preload_target_name_alloc(json, json_len, mesh_start, mesh_end, target_index);
                if (!shapes[shape_count].name) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                shapes[shape_count].weight = (float)gltf_preload_target_weight(
                    json, json_len, mesh_start, mesh_end, target_index);
                if (has_pos) {
                    if (!gltf_preload_copy_morph_channel(
                            &pos_view, vertex_count, &shapes[shape_count].pos_deltas)) {
                        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                        ok = 0;
                        goto done;
                    }
                    shapes[shape_count].flags |= GLTF_PRELOAD_MORPH_POD_HAS_POSITIONS;
                }
                if (has_norm) {
                    if (!gltf_preload_copy_morph_channel(
                            &norm_view, vertex_count, &shapes[shape_count].normal_deltas)) {
                        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                        ok = 0;
                        goto done;
                    }
                    shapes[shape_count].flags |= GLTF_PRELOAD_MORPH_POD_HAS_NORMALS;
                }
                if (has_tangent) {
                    if (!gltf_preload_copy_morph_channel(
                            &tangent_view, vertex_count, &shapes[shape_count].tangent_deltas)) {
                        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                        ok = 0;
                        goto done;
                    }
                    shapes[shape_count].flags |= GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS;
                }
                shape_count++;
            }
            target_index++;
            target_pos = target_end;
        } else {
            target_pos = gltf_json_skip_value(json, json_len, target_pos);
            if (target_pos == SIZE_MAX)
                break;
        }
        target_pos = gltf_json_skip_ws(json, json_len, target_pos);
        if (target_pos < targets_end && json[target_pos] == ',')
            target_pos++;
    }
    ok = gltf_preload_pack_morph_pod(bundle,
                                     mesh_index,
                                     primitive_index,
                                     shapes,
                                     shape_count,
                                     (uint32_t)vertex_count,
                                     error,
                                     error_cap);

done:
    gltf_preload_free_morph_shapes(shapes, shape_count);
    return ok;
}

static int gltf_preload_stage_mesh_primitive(
    rt_gltf_preload_bundle *bundle,
    const char *json,
    size_t json_len,
    int mesh_index,
    int primitive_index,
    size_t mesh_start,
    size_t mesh_end,
    size_t prim_start,
    size_t prim_end,
    const gltf_preload_buffer_ref_t *buffers,
    int buffer_count,
    const gltf_preload_buffer_view_ref_t *views,
    int view_count,
    const gltf_preload_accessor_ref_t *accessors,
    int accessor_count,
    char *error,
    size_t error_cap) {
    size_t attrs_start;
    size_t attrs_end;
    int pos_acc;
    int norm_acc;
    int uv0_acc;
    int uv1_acc;
    int color_acc;
    int tangent_acc;
    int joints_acc;
    int weights_acc;
    int joints1_acc;
    int weights1_acc;
    int idx_acc;
    int mode;
    int has_normals = 0;
    int has_uv0 = 0;
    int has_uv1 = 0;
    int has_colors = 0;
    int has_tangents = 0;
    int has_joints = 0;
    int has_weights = 0;
    int has_joints1 = 0;
    int has_weights1 = 0;
    int has_skinning_attrs = 0;
    uint32_t flags = 0u;
    uint32_t bone_count = 0u;
    gltf_accessor_view_t pos_view;
    gltf_accessor_view_t norm_view;
    gltf_accessor_view_t uv0_view;
    gltf_accessor_view_t uv1_view;
    gltf_accessor_view_t color_view;
    gltf_accessor_view_t tangent_view;
    gltf_accessor_view_t joints_view;
    gltf_accessor_view_t weights_view;
    gltf_accessor_view_t joints1_view;
    gltf_accessor_view_t weights1_view;
    gltf_accessor_view_t idx_view;
    int has_indices = 0;
    int32_t vertex_count_i;
    int32_t source_index_count;
    vgfx3d_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t index_count = 0;
    uint32_t index_capacity_count = 0;
    size_t vertex_bytes;
    size_t index_bytes;
    int ok = 1;

    if (!bundle || !json || !accessors)
        return 1;
    if (!gltf_json_object_find_value(
            json, json_len, prim_start, prim_end, "attributes", &attrs_start, &attrs_end) ||
        attrs_start >= attrs_end || json[attrs_start] != '{')
        return 1;
    mode = gltf_json_object_get_int(json, json_len, prim_start, prim_end, "mode", 4);
    if (mode != 4 && mode != 5 && mode != 6)
        return 1;

    pos_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "POSITION", -1);
    norm_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "NORMAL", -1);
    if (pos_acc < 0)
        return 1;

    if (!gltf_preload_resolve_accessor_view(accessors,
                                            accessor_count,
                                            pos_acc,
                                            buffers,
                                            buffer_count,
                                            views,
                                            view_count,
                                            &pos_view))
        return 1;
    has_normals = gltf_preload_resolve_accessor_view(accessors,
                                                     accessor_count,
                                                     norm_acc,
                                                     buffers,
                                                     buffer_count,
                                                     views,
                                                     view_count,
                                                     &norm_view) &&
                  norm_view.comp_count >= 3 && norm_view.count >= pos_view.count;
    if (pos_view.comp_count < 3 || pos_view.count <= 0)
        return 1;

    uv0_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TEXCOORD_0", -1);
    uv1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TEXCOORD_1", -1);
    color_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "COLOR_0", -1);
    tangent_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TANGENT", -1);
    joints_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "JOINTS_0", -1);
    weights_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "WEIGHTS_0", -1);
    joints1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "JOINTS_1", -1);
    weights1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "WEIGHTS_1", -1);
    idx_acc = gltf_json_object_get_int(json, json_len, prim_start, prim_end, "indices", -1);

    has_uv0 = gltf_preload_resolve_accessor_view(accessors,
                                                 accessor_count,
                                                 uv0_acc,
                                                 buffers,
                                                 buffer_count,
                                                 views,
                                                 view_count,
                                                 &uv0_view) &&
              uv0_view.comp_count >= 2 && uv0_view.count >= pos_view.count;
    has_uv1 = gltf_preload_resolve_accessor_view(accessors,
                                                 accessor_count,
                                                 uv1_acc,
                                                 buffers,
                                                 buffer_count,
                                                 views,
                                                 view_count,
                                                 &uv1_view) &&
              uv1_view.comp_count >= 2 && uv1_view.count >= pos_view.count;
    has_colors = gltf_preload_resolve_accessor_view(accessors,
                                                   accessor_count,
                                                   color_acc,
                                                   buffers,
                                                   buffer_count,
                                                   views,
                                                   view_count,
                                                   &color_view) &&
                 color_view.comp_count >= 3 && color_view.count >= pos_view.count;
    has_tangents = gltf_preload_resolve_accessor_view(accessors,
                                                     accessor_count,
                                                     tangent_acc,
                                                     buffers,
                                                     buffer_count,
                                                     views,
                                                     view_count,
                                                     &tangent_view) &&
                   tangent_view.comp_count >= 3 && tangent_view.count >= pos_view.count;
    has_joints = gltf_preload_resolve_accessor_view(accessors,
                                                    accessor_count,
                                                    joints_acc,
                                                    buffers,
                                                    buffer_count,
                                                    views,
                                                    view_count,
                                                    &joints_view) &&
                 joints_view.comp_count >= 4 && joints_view.count >= pos_view.count;
    has_weights = gltf_preload_resolve_accessor_view(accessors,
                                                     accessor_count,
                                                     weights_acc,
                                                     buffers,
                                                     buffer_count,
                                                     views,
                                                     view_count,
                                                     &weights_view) &&
                  weights_view.comp_count >= 4 && weights_view.count >= pos_view.count;
    has_joints1 = gltf_preload_resolve_accessor_view(accessors,
                                                     accessor_count,
                                                     joints1_acc,
                                                     buffers,
                                                     buffer_count,
                                                     views,
                                                     view_count,
                                                     &joints1_view) &&
                  joints1_view.comp_count >= 4 && joints1_view.count >= pos_view.count;
    has_weights1 = gltf_preload_resolve_accessor_view(accessors,
                                                      accessor_count,
                                                      weights1_acc,
                                                      buffers,
                                                      buffer_count,
                                                      views,
                                                      view_count,
                                                      &weights1_view) &&
                   weights1_view.comp_count >= 4 && weights1_view.count >= pos_view.count;
    has_skinning_attrs = has_joints || has_weights || has_joints1 || has_weights1;
    has_indices = gltf_preload_resolve_accessor_view(accessors,
                                                    accessor_count,
                                                    idx_acc,
                                                    buffers,
                                                    buffer_count,
                                                    views,
                                                    view_count,
                                                    &idx_view) &&
                  idx_view.comp_count == 1 &&
                  (idx_view.comp_type == 5121 || idx_view.comp_type == 5123 ||
                   idx_view.comp_type == 5125);
    vertex_count_i = pos_view.count;
    source_index_count = has_indices ? idx_view.count : vertex_count_i;
    if (vertex_count_i < 3 || source_index_count < 3)
        return 1;
    if (!gltf_preload_topology_index_capacity(mode, source_index_count, &index_capacity_count) ||
        index_capacity_count == 0)
        return 1;
    if (!gltf_checked_mul_size((size_t)vertex_count_i, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !gltf_checked_mul_size((size_t)index_capacity_count, sizeof(uint32_t), &index_bytes))
        return 1;
    if (vertex_bytes == 0 || index_bytes == 0)
        return 1;

    vertices = (vgfx3d_vertex_t *)calloc((size_t)vertex_count_i, sizeof(*vertices));
    indices = (uint32_t *)malloc(index_bytes);
    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }

    for (int32_t vi = 0; vi < vertex_count_i; vi++) {
        float pos[3] = {0.0f, 0.0f, 0.0f};
        float nrm[3] = {0.0f, 0.0f, 0.0f};
        float uv[2] = {0.0f, 0.0f};
        float uv1[2] = {0.0f, 0.0f};
        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        float tangent[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t joints[4] = {0u, 0u, 0u, 0u};
        float weights1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        uint32_t joints1[4] = {0u, 0u, 0u, 0u};
        gltf_accessor_read_f32(&pos_view, vi, pos, 3);
        if (has_normals)
            gltf_accessor_read_f32(&norm_view, vi, nrm, 3);
        if (!gltf_preload_floats_are_finite(pos, 3) ||
            (has_normals && !gltf_preload_floats_are_finite(nrm, 3))) {
            ok = 1;
            goto done;
        }
        if (has_uv0) {
            gltf_accessor_read_f32(&uv0_view, vi, uv, 2);
            if (!gltf_preload_floats_are_finite(uv, 2)) {
                ok = 1;
                goto done;
            }
        }
        if (has_uv1) {
            gltf_accessor_read_f32(&uv1_view, vi, uv1, 2);
            if (!gltf_preload_floats_are_finite(uv1, 2)) {
                ok = 1;
                goto done;
            }
        } else {
            uv1[0] = uv[0];
            uv1[1] = uv[1];
        }
        if (has_colors) {
            gltf_accessor_read_f32(&color_view, vi, color, 4);
            if (color_view.comp_count < 4)
                color[3] = 1.0f;
            if (!gltf_preload_floats_are_finite(color, 4)) {
                ok = 1;
                goto done;
            }
        }
        if (has_tangents) {
            gltf_accessor_read_f32(&tangent_view, vi, tangent, 4);
            if (tangent_view.comp_count < 4)
                tangent[3] = 1.0f;
            if (!gltf_preload_floats_are_finite(tangent, 4)) {
                ok = 1;
                goto done;
            }
        }
        memcpy(vertices[vi].pos, pos, sizeof(vertices[vi].pos));
        memcpy(vertices[vi].normal, nrm, sizeof(vertices[vi].normal));
        memcpy(vertices[vi].uv, uv, sizeof(vertices[vi].uv));
        memcpy(vertices[vi].uv1, uv1, sizeof(vertices[vi].uv1));
        memcpy(vertices[vi].color, color, sizeof(vertices[vi].color));
        memcpy(vertices[vi].tangent, tangent, sizeof(vertices[vi].tangent));
        if (has_skinning_attrs) {
            uint32_t vertex_bone_count;
            if (has_joints)
                gltf_accessor_read_u32(&joints_view, vi, joints, 4);
            if (has_weights)
                gltf_accessor_read_f32(&weights_view, vi, weights, 4);
            if (has_joints1)
                gltf_accessor_read_u32(&joints1_view, vi, joints1, 4);
            if (has_weights1)
                gltf_accessor_read_f32(&weights1_view, vi, weights1, 4);
            if (has_joints1 && has_weights1) {
                uint32_t merged_joints[4] = {0u, 0u, 0u, 0u};
                float merged_weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                if (has_joints && has_weights) {
                    for (int j = 0; j < 4; j++)
                        gltf_add_top_joint_influence(
                            joints[j], weights[j], merged_joints, merged_weights);
                }
                for (int j = 0; j < 4; j++)
                    gltf_add_top_joint_influence(
                        joints1[j], weights1[j], merged_joints, merged_weights);
                gltf_normalize_joint_influences(merged_weights);
                memcpy(joints, merged_joints, sizeof(joints));
                memcpy(weights, merged_weights, sizeof(weights));
            } else {
                for (int j = 0; j < 4; j++) {
                    if (joints[j] >= VGFX3D_MAX_BONES)
                        weights[j] = 0.0f;
                }
            }
            vertex_bone_count =
                gltf_preload_write_vertex_bone_weights(&vertices[vi], joints, weights);
            if (vertex_bone_count > bone_count)
                bone_count = vertex_bone_count;
        }
    }

    if (mode == 4) {
        for (int32_t ii = 0; ii + 2 < source_index_count; ii += 3) {
            gltf_preload_emit_pod_triangle(
                vertices,
                (uint32_t)vertex_count_i,
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii),
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii + 1),
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii + 2),
                indices,
                &index_count);
        }
    } else if (mode == 5) {
        for (int32_t ii = 0; ii + 2 < source_index_count; ii++) {
            uint32_t i0 =
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii);
            uint32_t i1 =
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii + 1);
            uint32_t i2 =
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii + 2);
            if ((ii & 1) != 0) {
                uint32_t tmp = i0;
                i0 = i1;
                i1 = tmp;
            }
            gltf_preload_emit_pod_triangle(
                vertices, (uint32_t)vertex_count_i, i0, i1, i2, indices, &index_count);
        }
    } else {
        uint32_t base = gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, 0);
        for (int32_t ii = 1; ii + 1 < source_index_count; ii++) {
            gltf_preload_emit_pod_triangle(
                vertices,
                (uint32_t)vertex_count_i,
                base,
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii),
                gltf_preload_read_index_or_vertex(has_indices ? &idx_view : NULL, ii + 1),
                indices,
                &index_count);
        }
    }
    if (index_count == 0)
        goto done;
    if (has_normals)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_NORMALS;
    if (has_uv0)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_UV0;
    if (has_tangents)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_TANGENTS;
    if (has_skinning_attrs)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_SKINNING;
    ok = gltf_preload_pack_mesh_pod(bundle,
                                    mesh_index,
                                    primitive_index,
                                    flags,
                                    vertices,
                                    (uint32_t)vertex_count_i,
                                    indices,
                                    index_count,
                                    bone_count,
                                    error,
                                    error_cap);
    if (ok) {
        ok = gltf_preload_stage_morph_targets(bundle,
                                              json,
                                              json_len,
                                              mesh_index,
                                              primitive_index,
                                              mesh_start,
                                              mesh_end,
                                              prim_start,
                                              prim_end,
                                              buffers,
                                              buffer_count,
                                              views,
                                              view_count,
                                              accessors,
                                              accessor_count,
                                              vertex_count_i,
                                              error,
                                              error_cap);
    }

done:
    free(vertices);
    free(indices);
    return ok;
}

static int gltf_preload_stage_meshes(rt_gltf_preload_bundle *bundle,
                                     const char *json,
                                     size_t json_len,
                                     const gltf_preload_buffer_ref_t *buffers,
                                     int buffer_count,
                                     const gltf_preload_buffer_view_ref_t *views,
                                     int view_count,
                                     char *error,
                                     size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    gltf_preload_accessor_ref_t *accessors = NULL;
    int accessor_count = 0;
    int mesh_index = 0;
    if (!gltf_json_find_top_level_array(json, json_len, "meshes", &array_start, &array_end))
        return 1;
    if (!gltf_preload_parse_accessors(
            json, json_len, &accessors, &accessor_count, error, error_cap))
        return 0;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            size_t prims_start;
            size_t prims_end;
            int primitive_index = 0;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (gltf_json_object_find_value(
                    json, json_len, pos, object_end, "primitives", &prims_start, &prims_end) &&
                prims_start < prims_end && json[prims_start] == '[') {
                size_t prim_pos = prims_start + 1u;
                while (prim_pos < prims_end) {
                    prim_pos = gltf_json_skip_ws(json, json_len, prim_pos);
                    if (prim_pos >= prims_end || json[prim_pos] == ']')
                        break;
                    if (json[prim_pos] == '{') {
                        size_t prim_end = gltf_json_find_matching(
                            json, json_len, prim_pos, '{', '}');
                        if (prim_end == SIZE_MAX || prim_end > prims_end)
                            break;
                        if (!gltf_preload_stage_mesh_primitive(bundle,
                                                               json,
                                                               json_len,
                                                               mesh_index,
                                                               primitive_index,
                                                               pos,
                                                               object_end,
                                                               prim_pos,
                                                               prim_end,
                                                               buffers,
                                                               buffer_count,
                                                               views,
                                                               view_count,
                                                               accessors,
                                                               accessor_count,
                                                               error,
                                                               error_cap)) {
                            free(accessors);
                            return 0;
                        }
                        primitive_index++;
                        prim_pos = prim_end;
                    } else {
                        prim_pos = gltf_json_skip_value(json, json_len, prim_pos);
                        if (prim_pos == SIZE_MAX)
                            break;
                    }
                    prim_pos = gltf_json_skip_ws(json, json_len, prim_pos);
                    if (prim_pos < prims_end && json[prim_pos] == ',')
                        prim_pos++;
                }
            }
            mesh_index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    free(accessors);
    return 1;
}

static int gltf_preload_stage_image_bytes(rt_gltf_preload_bundle *bundle,
                                          int image_index,
                                          const char *mime_type,
                                          uint8_t *data,
                                          size_t data_len,
                                          int required,
                                          char *error,
                                          size_t error_cap) {
    char key[96];
    if (!data || data_len == 0)
        return 1;
    gltf_preload_image_key(image_index, mime_type, key, sizeof(key));
    return gltf_preload_bundle_add_image_payload(
        bundle, key, mime_type ? mime_type : key, data, data_len, required, error, error_cap);
}

static void gltf_preload_mark_required_images(const char *json,
                                              size_t json_len,
                                              uint8_t *required,
                                              int required_count) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    if (!json || !required || required_count <= 0)
        return;
    if (!gltf_json_find_top_level_array(json, json_len, "textures", &array_start, &array_end))
        return;
    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            int source;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            source = gltf_json_object_get_int(json, json_len, pos, object_end, "source", -1);
            if (source >= 0 && source < required_count)
                required[source] = 1u;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
}

static int gltf_validate_required_data_uri_images(const char *json, size_t json_len) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    int image_count = 0;
    int index = 0;
    uint8_t *required = NULL;
    if (!gltf_json_find_top_level_array(json, json_len, "images", &array_start, &array_end))
        return 1;

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            image_count++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (image_count <= 0)
        return 1;

    required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
    if (!required)
        return 0;
    gltf_preload_mark_required_images(json, json_len, required, image_count);

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *uri;
            char *mime_type;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            uri = gltf_json_object_get_string(json, json_len, pos, object_end, "uri");
            mime_type = gltf_json_object_get_string(json, json_len, pos, object_end, "mimeType");
            if (required[index] && uri && strncmp(uri, "data:", 5) == 0) {
                char parsed_mime[64];
                const char *image_type;
                uint8_t *data = NULL;
                uint8_t *rgba_blob = NULL;
                size_t data_len = 0;
                size_t rgba_len = 0;
                int ok = 1;
                gltf_data_uri_copy_mime(uri, parsed_mime, sizeof(parsed_mime));
                image_type = mime_type ? mime_type : parsed_mime;
                if (gltf_preload_image_is_supported_format(image_type)) {
                    ok = gltf_parse_data_uri(
                             uri, parsed_mime, sizeof(parsed_mime), &data, &data_len) &&
                         gltf_decode_image_payload_to_rgba_blob(
                             image_type, data, data_len, &rgba_blob, &rgba_len);
                    free(data);
                    free(rgba_blob);
                }
                if (!ok) {
                    free(uri);
                    free(mime_type);
                    free(required);
                    return 0;
                }
            }
            free(uri);
            free(mime_type);
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    free(required);
    return 1;
}

static int gltf_preload_stage_images(rt_gltf_preload_bundle *bundle,
                                     const char *model_path,
                                     const char *json,
                                     size_t json_len,
                                     int load_assets,
                                     const gltf_preload_buffer_ref_t *buffers,
                                     int buffer_count,
                                     const gltf_preload_buffer_view_ref_t *views,
                                     int view_count,
                                     char *error,
                                     size_t error_cap) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    int image_count = 0;
    int index = 0;
    uint8_t *required = NULL;
    if (!gltf_json_find_top_level_array(json, json_len, "images", &array_start, &array_end))
        return 1;

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            image_count++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    if (image_count > 0) {
        required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
        if (required)
            gltf_preload_mark_required_images(json, json_len, required, image_count);
    }

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            char *uri;
            char *mime_type;
            int required_image = required ? required[index] != 0 : 0;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            uri = gltf_json_object_get_string(json, json_len, pos, object_end, "uri");
            mime_type = gltf_json_object_get_string(json, json_len, pos, object_end, "mimeType");
            if (uri) {
                int ok = 1;
                if (strncmp(uri, "data:", 5) == 0) {
                    char parsed_mime[64];
                    uint8_t *data = NULL;
                    size_t data_len = 0;
                    parsed_mime[0] = '\0';
                    if (gltf_parse_data_uri(uri, parsed_mime, sizeof(parsed_mime), &data, &data_len)) {
                        ok = gltf_preload_stage_image_bytes(bundle,
                                                            index,
                                                            mime_type ? mime_type : parsed_mime,
                                                            data,
                                                            data_len,
                                                            required_image,
                                                            error,
                                                            error_cap);
                    } else if (required_image &&
                               gltf_preload_image_is_supported_format(mime_type)) {
                        gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
                        ok = 0;
                    }
                } else {
                    ok = gltf_preload_stage_external_image(bundle,
                                                           model_path,
                                                           uri,
                                                           load_assets,
                                                           required_image,
                                                           error,
                                                           error_cap);
                }
                free(uri);
                free(mime_type);
                if (!ok) {
                    free(required);
                    return 0;
                }
            } else {
                int view_index =
                    gltf_json_object_get_int(json, json_len, pos, object_end, "bufferView", -1);
                int staged = 0;
                if (view_index >= 0 && view_index < view_count && views && views[view_index].valid) {
                    const gltf_preload_buffer_view_ref_t *view = &views[view_index];
                    if (view->buffer >= 0 && view->buffer < buffer_count && buffers &&
                        buffers[view->buffer].data) {
                        size_t end;
                        if (gltf_checked_add_size(view->byte_offset, view->byte_length, &end) &&
                            end <= buffers[view->buffer].len) {
                            uint8_t *copy = (uint8_t *)malloc(view->byte_length);
                            if (!copy) {
                                free(mime_type);
                                free(required);
                                gltf_preload_set_error(
                                    error, error_cap, "failed to stage glTF dependency");
                                return 0;
                            }
                            memcpy(copy,
                                   buffers[view->buffer].data + view->byte_offset,
                                   view->byte_length);
                            staged = 1;
                            if (!gltf_preload_stage_image_bytes(bundle,
                                                                 index,
                                                                 mime_type,
                                                                 copy,
                                                                 view->byte_length,
                                                                 required_image,
                                                                 error,
                                                                 error_cap)) {
                                free(mime_type);
                                free(required);
                                return 0;
                            }
                        }
                    }
                }
                if (!staged && required_image) {
                    free(mime_type);
                    free(required);
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
                free(mime_type);
            }
            index++;
            pos = object_end;
        } else {
            pos = gltf_json_skip_value(json, json_len, pos);
            if (pos == SIZE_MAX)
                break;
        }
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos < array_end && json[pos] == ',')
            pos++;
    }
    free(required);
    return 1;
}

rt_gltf_preload_bundle *rt_gltf_preload_bundle_create(rt_string path,
                                                      uint8_t *root_data,
                                                      size_t root_size,
                                                      int load_assets,
                                                      char *error,
                                                      size_t error_cap) {
    const char *model_path = path ? rt_string_cstr(path) : NULL;
    rt_gltf_preload_bundle *bundle;
    char *json = NULL;
    size_t json_len = 0;
    gltf_preload_buffer_ref_t *buffer_refs = NULL;
    int buffer_ref_count = 0;
    gltf_preload_buffer_view_ref_t *view_refs = NULL;
    int view_ref_count = 0;
    if (error && error_cap > 0)
        error[0] = '\0';
    if (!root_data || root_size == 0 || !model_path) {
        free(root_data);
        gltf_preload_set_error(error, error_cap, "failed to load model");
        return NULL;
    }
    bundle = (rt_gltf_preload_bundle *)calloc(1, sizeof(*bundle));
    if (!bundle) {
        free(root_data);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF preload");
        return NULL;
    }
    bundle->root_data = root_data;
    bundle->root_size = root_size;
    json = gltf_json_copy_from_root_bytes(root_data, root_size, &json_len);
    if (json) {
        int ok = gltf_preload_stage_buffers(bundle,
                                            model_path,
                                            json,
                                            json_len,
                                            load_assets,
                                            &buffer_refs,
                                            &buffer_ref_count,
                                            error,
                                            error_cap);
        if (ok) {
            ok = gltf_preload_parse_buffer_views(
                json, json_len, &view_refs, &view_ref_count, error, error_cap);
        }
        if (ok) {
            ok = gltf_preload_validate_accessors(json,
                                                 json_len,
                                                 buffer_refs,
                                                 buffer_ref_count,
                                                 view_refs,
                                                 view_ref_count,
                                                 error,
                                                 error_cap);
        }
        if (ok) {
            ok = gltf_preload_stage_meshes(bundle,
                                           json,
                                           json_len,
                                           buffer_refs,
                                           buffer_ref_count,
                                           view_refs,
                                           view_ref_count,
                                           error,
                                           error_cap);
        }
        if (ok) {
            ok = gltf_preload_stage_images(bundle,
                                           model_path,
                                           json,
                                           json_len,
                                           load_assets,
                                           buffer_refs,
                                           buffer_ref_count,
                                           view_refs,
                                           view_ref_count,
                                           error,
                                           error_cap);
        }
        free(buffer_refs);
        free(view_refs);
        free(json);
        if (!ok) {
            rt_gltf_preload_bundle_free(bundle);
            return NULL;
        }
    }
    return bundle;
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
                             int32_t *out_skin_count,
                             int *hard_error) {
    void *skins_arr = jarr(root, "skins");
    void *nodes_arr = jarr(root, "nodes");
    int32_t skin_count = (int32_t)jarr_len(skins_arr);
    int32_t node_count = (int32_t)jarr_len(nodes_arr);
    int32_t *node_parents = NULL;
    gltf_skin_t *skins = NULL;
    if (hard_error)
        *hard_error = 0;
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
        if (joint_count > VGFX3D_MAX_BONES) {
            if (hard_error)
                *hard_error = 1;
            for (int32_t i = 0; i < asset->skeleton_count; i++)
                gltf_release_ref(&asset->skeletons[i]);
            asset->skeleton_count = 0;
            free(asset->skeletons);
            asset->skeletons = NULL;
            free(node_parents);
            gltf_free_skins(skins, skin_count);
            return;
        }
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
            int32_t parent_node =
                skins[si].joint_nodes[ji] >= 0 && skins[si].joint_nodes[ji] < node_count
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

/// @brief Deep-clone a mesh and its morph targets for per-node variant use.
/// @details Calls `rt_mesh3d_clone` for the geometry, then clones the morph target
///          separately (so each node instance gets independent weights) and re-attaches
///          it via `rt_mesh3d_set_morph_targets`. Returns NULL on allocation failure.
static void *gltf_clone_mesh_variant(void *source_mesh) {
    rt_mesh3d *variant;
    rt_mesh3d *mesh;
    void *morph_clone;
    if (!source_mesh)
        return NULL;
    variant = (rt_mesh3d *)rt_mesh3d_clone(source_mesh);
    if (!variant)
        return NULL;
    mesh = (rt_mesh3d *)variant;
    if (mesh->morph_targets_ref) {
        morph_clone = rt_morphtarget3d_clone(mesh->morph_targets_ref);
        if (morph_clone) {
            rt_mesh3d_set_morph_targets(mesh, morph_clone);
            gltf_release_local(morph_clone);
        }
    }
    return variant;
}

/// @brief Return the mesh to attach to a glTF scene node, creating a per-node variant when needed.
/// @details Returns the shared asset mesh directly when neither skinning nor morph weights require
///          per-instance state. When a skin or a weights array is present, clones the mesh via
///          `gltf_clone_mesh_variant`, applies the skin's inverse-bind matrices, and writes the
///          initial morph weights. Falls back to the shared mesh on clone failure.
static void *gltf_make_node_mesh_variant(rt_gltf_asset *asset,
                                         int32_t mesh_index,
                                         int64_t skin_ref,
                                         void *weights_arr,
                                         const gltf_skin_t *skins,
                                         int32_t skin_count,
                                         void **mesh_variant_sources) {
    int has_skin = skin_ref >= 0 && skin_ref < skin_count;
    int needs_variant = has_skin || weights_arr != NULL;
    void *source_mesh;
    void *variant;
    if (!asset || mesh_index < 0 || mesh_index >= asset->mesh_count)
        return NULL;
    if (!needs_variant)
        return asset->meshes[mesh_index];
    source_mesh = mesh_variant_sources && mesh_variant_sources[mesh_index]
                      ? mesh_variant_sources[mesh_index]
                      : asset->meshes[mesh_index];
    variant = gltf_clone_mesh_variant(source_mesh);
    if (!variant)
        return asset->meshes[mesh_index];
    if (has_skin)
        gltf_apply_skin_to_mesh(variant, &skins[skin_ref]);
    if (weights_arr)
        gltf_apply_node_morph_weights(variant, weights_arr);
    return variant;
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

/// @brief Look up the engine bone index for a glTF node in a specific skin.
/// @return Engine bone index, or -1 if the node is not a joint in this skin.
static int32_t gltf_skin_bone_for_node(const gltf_skin_t *skin, int32_t node_idx) {
    int32_t joint;
    if (!skin || !skin->joint_to_bone)
        return -1;
    joint = gltf_skin_find_joint(skin, node_idx);
    if (joint >= 0 && skin->joint_to_bone[joint] >= 0)
        return skin->joint_to_bone[joint];
    return -1;
}

/// @brief Read the time value at @p index from a glTF animation input accessor as a double.
/// @details glTF animation times are stored as float32 scalars in the input accessor;
///          this function reads one float and promotes it to double for precision.
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
static int gltf_anim_insert_time(double **times, int32_t *count, int32_t *capacity, double value) {
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

/// @brief Read @p components floats from the curve's output accessor at an already-computed index.
/// @details Thin wrapper around `gltf_accessor_read_f32` that accepts a pre-multiplied index
///          (CUBICSPLINE callers pass `key * 3 + 1` for the value sample; linear callers pass
///          `key`).
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

/// @brief Read a single scalar float from an accessor by flat component index.
/// @details Converts a flat index (element_index * comp_count + component) into the correct
///          (element, component) pair, reads a full element into a temporary buffer, then
///          returns the requested component. Used to read per-vertex weight scalars from
///          multi-component accessor views.
/// @return The float value at @p flat_index, or 0.0 on out-of-range or null view.
static float gltf_accessor_read_flat_f32(const gltf_accessor_view_t *view, int32_t flat_index) {
    float tmp[16];
    int32_t comp_count;
    int32_t element;
    int32_t component;
    if (!view || flat_index < 0 || view->comp_count <= 0)
        return 0.0f;
    comp_count = view->comp_count;
    element = flat_index / comp_count;
    component = flat_index % comp_count;
    if (component < 0 || component >= (int32_t)(sizeof(tmp) / sizeof(tmp[0])))
        return 0.0f;
    gltf_accessor_read_f32(view, element, tmp, comp_count);
    return tmp[component];
}

/// @brief Normalize @p out in-place when @p components == 4 (quaternion interpolation result).
/// @details glTF CUBICSPLINE and linear interpolation can drift the magnitude of rotation
///          quaternions away from unit length; renormalization prevents visual artifacts.
///          Non-quaternion paths (translation/scale with components ≠ 4) are left unchanged.
static void gltf_normalize_sample_if_quat(float *out, int32_t components) {
    if (components == 4) {
        float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (len > 1e-6f) {
            for (int c = 0; c < 4; c++)
                out[c] /= len;
        }
    }
}

/// @brief Spherically interpolate two quaternions by @p alpha ∈ [0, 1].
/// @details Both inputs are normalized before interpolation. When the dot product is
///          negative, `q1` is negated to ensure slerp travels the shortest arc (< 180°).
///          When the quaternions are nearly parallel (dot > 0.9995), plain nlerp is used
///          to avoid division-by-zero in the sin/cos path. Result is always renormalized.
static void gltf_slerp_quat(const float *a, const float *b, double alpha, float *out) {
    float q0[4];
    float q1[4];
    double dot;
    if (!a || !b || !out)
        return;
    memcpy(q0, a, sizeof(q0));
    memcpy(q1, b, sizeof(q1));
    gltf_normalize_sample_if_quat(q0, 4);
    gltf_normalize_sample_if_quat(q1, 4);
    dot = (double)q0[0] * q1[0] + (double)q0[1] * q1[1] + (double)q0[2] * q1[2] +
          (double)q0[3] * q1[3];
    if (dot < 0.0) {
        dot = -dot;
        for (int c = 0; c < 4; c++)
            q1[c] = -q1[c];
    }
    if (dot > 0.9995) {
        for (int c = 0; c < 4; c++)
            out[c] = (float)((double)q0[c] + ((double)q1[c] - (double)q0[c]) * alpha);
        gltf_normalize_sample_if_quat(out, 4);
        return;
    }
    {
        double theta0 = acos(dot);
        double theta = theta0 * alpha;
        double sin_theta = sin(theta);
        double sin_theta0 = sin(theta0);
        double s0 = cos(theta) - dot * sin_theta / sin_theta0;
        double s1 = sin_theta / sin_theta0;
        for (int c = 0; c < 4; c++)
            out[c] = (float)(s0 * q0[c] + s1 * q1[c]);
        gltf_normalize_sample_if_quat(out, 4);
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
                    out[c] = (float)(h00 * a[c] + h10 * dt * out_tangent0[c] + h01 * b[c] +
                                     h11 * dt * in_tangent1[c]);
                }
                gltf_normalize_sample_if_quat(out, components);
                return;
            }
            gltf_curve_read_value(curve, i, b, components);
            if (components == 4) {
                gltf_slerp_quat(a, b, alpha, out);
                return;
            }
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
    asset->animations = (void **)calloc((size_t)anim_count * (size_t)skin_count, sizeof(void *));
    if (!asset->animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int32_t channel_count = (int32_t)jarr_len(channels);
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        if (channel_count <= 0 || !samplers)
            continue;
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "animation_%d", (int)ai);
            name = generated_name;
        }
        for (int32_t si = 0; si < skin_count; si++) {
            gltf_anim_curve_t *curves =
                (gltf_anim_curve_t *)calloc((size_t)channel_count, sizeof(*curves));
            double duration = 0.0;
            int emitted_any = 0;
            void *anim;
            char skin_name[128];
            const char *clip_name = name;
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
                bone_idx = gltf_skin_bone_for_node(&skins[si], (int32_t)node_idx);
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
                if (!gltf_get_accessor_view(
                        root, jint(sampler, "input", -1), buffers, buf_count, &curves[ci].input) ||
                    !gltf_get_accessor_view(
                        root, jint(sampler, "output", -1), buffers, buf_count, &curves[ci].output))
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
            if (skin_count > 1) {
                snprintf(skin_name, sizeof(skin_name), "%s_skin_%d", name, (int)si);
                clip_name = skin_name;
            }
            anim = rt_animation3d_new(rt_const_cstr(clip_name), duration > 0.0 ? duration : 1.0);
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
                        gltf_anim_insert_time(&times,
                                              &time_count,
                                              &time_capacity,
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
                    float rot[4] = {
                        (float)rot_d[0], (float)rot_d[1], (float)rot_d[2], (float)rot_d[3]};
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
}

/// @brief Return non-zero if @p node_idx is a joint in any of the @p skin_count skins.
/// @details Used during scene-graph construction to decide whether a node should be excluded
///          from the scene hierarchy (skin joints are already represented inside the skeleton).
static int gltf_node_is_skin_joint(const gltf_skin_t *skins, int32_t skin_count, int32_t node_idx) {
    if (!skins || skin_count <= 0)
        return 0;
    for (int32_t si = 0; si < skin_count; si++) {
        if (gltf_skin_bone_for_node(&skins[si], node_idx) >= 0)
            return 1;
    }
    return 0;
}

/// @brief Map a glTF animation channel path string to the engine's RT_NODE_ANIM_PATH_* constant.
/// @return RT_NODE_ANIM_PATH_TRANSLATION / ROTATION / SCALE / WEIGHTS, or -1 for unknown paths.
static int32_t gltf_node_anim_path(const char *path) {
    if (!path || strcmp(path, "translation") == 0)
        return RT_NODE_ANIM_PATH_TRANSLATION;
    if (strcmp(path, "rotation") == 0)
        return RT_NODE_ANIM_PATH_ROTATION;
    if (strcmp(path, "scale") == 0)
        return RT_NODE_ANIM_PATH_SCALE;
    if (strcmp(path, "weights") == 0)
        return RT_NODE_ANIM_PATH_WEIGHTS;
    return -1;
}

/// @brief Determine how many float components are stored per keyframe for a given animation path.
/// @details For TRANSLATION/SCALE the width is 3 (XYZ); for ROTATION it is 4 (XYZW quaternion).
///          For the WEIGHTS path the width is derived by dividing the total output components by
///          the number of input keyframes (× 3 for CUBICSPLINE). Returns 0 for degenerate data.
static int32_t gltf_node_anim_width_for_path(int32_t path,
                                             const gltf_accessor_view_t *input,
                                             const gltf_accessor_view_t *output,
                                             int cubic) {
    int64_t total_components;
    int64_t divisor;
    if (!input || !output || input->count <= 0 || output->count <= 0)
        return 0;
    if (path == RT_NODE_ANIM_PATH_TRANSLATION || path == RT_NODE_ANIM_PATH_SCALE)
        return (int64_t)output->count >= (int64_t)input->count * (cubic ? 3 : 1) ? 3 : 0;
    if (path == RT_NODE_ANIM_PATH_ROTATION)
        return (int64_t)output->count >= (int64_t)input->count * (cubic ? 3 : 1) ? 4 : 0;
    total_components = (int64_t)output->count * (int64_t)output->comp_count;
    divisor = (int64_t)input->count * (cubic ? 3 : 1);
    if (divisor <= 0 || total_components <= 0 || total_components % divisor != 0)
        return 0;
    if (total_components / divisor > INT32_MAX)
        return 0;
    return (int32_t)(total_components / divisor);
}

/// @brief Parse all glTF node animations and attach them to the asset.
/// @details Iterates the `animations` array, builds one `rt_node_animation3d` per animation,
///          then for each channel resolves the sampler, maps the target node to a bone (skin
///          joint) or scene node index, unions the time range across all channels to compute
///          the animation duration, and emits per-track keyframe data. Animations with no
///          valid channels are skipped. Named or auto-named (`"node_animation_N"`) animations
///          are stored in `asset->node_animations` for retrieval by the caller.
static void gltf_parse_node_animations(rt_gltf_asset *asset,
                                       void *root,
                                       gltf_buffer_t *buffers,
                                       int buf_count,
                                       const gltf_skin_t *skins,
                                       int32_t skin_count) {
    void *anims_arr = jarr(root, "animations");
    void *nodes_arr = jarr(root, "nodes");
    int32_t anim_count = (int32_t)jarr_len(anims_arr);
    if (!asset || !anims_arr || anim_count <= 0)
        return;
    asset->node_animations = (void **)calloc((size_t)anim_count, sizeof(void *));
    if (!asset->node_animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int32_t channel_count = (int32_t)jarr_len(channels);
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        void *node_anim;
        double duration = 0.0;
        int emitted_any = 0;
        if (channel_count <= 0 || !samplers)
            continue;
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "node_animation_%d", (int)ai);
            name = generated_name;
        }
        node_anim = rt_node_animation3d_new(rt_const_cstr(name), 1.0);
        if (!node_anim)
            continue;
        for (int32_t ci = 0; ci < channel_count; ci++) {
            void *channel = rt_seq_get(channels, ci);
            void *target = jget(channel, "target");
            const char *path_str = jstr(target, "path");
            int32_t path = gltf_node_anim_path(path_str);
            int64_t sampler_idx = jint(channel, "sampler", -1);
            int64_t node_idx = jint(target, "node", -1);
            void *sampler;
            const char *interpolation;
            int cubic;
            gltf_accessor_view_t input;
            gltf_accessor_view_t output;
            int32_t width;
            double *times = NULL;
            float *values = NULL;
            float *in_tangents = NULL;
            float *out_tangents = NULL;
            char fallback_name[64];
            const char *target_name;
            int64_t value_count;
            if (path < 0 || sampler_idx < 0 || sampler_idx >= jarr_len(samplers) || node_idx < 0)
                continue;
            if (path != RT_NODE_ANIM_PATH_WEIGHTS &&
                gltf_node_is_skin_joint(skins, skin_count, (int32_t)node_idx))
                continue;
            sampler = rt_seq_get(samplers, sampler_idx);
            interpolation = jstr(sampler, "interpolation");
            cubic = interpolation && strcmp(interpolation, "CUBICSPLINE") == 0;
            if (!gltf_get_accessor_view(
                    root, jint(sampler, "input", -1), buffers, buf_count, &input) ||
                !gltf_get_accessor_view(
                    root, jint(sampler, "output", -1), buffers, buf_count, &output) ||
                input.count <= 0)
                continue;
            width = gltf_node_anim_width_for_path(path, &input, &output, cubic);
            if (width <= 0)
                continue;
            value_count = (int64_t)input.count * (int64_t)width;
            if (value_count <= 0 || value_count > INT32_MAX)
                continue;
            times = (double *)malloc((size_t)input.count * sizeof(double));
            values = (float *)malloc((size_t)value_count * sizeof(float));
            if (cubic) {
                in_tangents = (float *)malloc((size_t)value_count * sizeof(float));
                out_tangents = (float *)malloc((size_t)value_count * sizeof(float));
            }
            if (!times || !values || (cubic && (!in_tangents || !out_tangents))) {
                free(times);
                free(values);
                free(in_tangents);
                free(out_tangents);
                continue;
            }
            for (int32_t ki = 0; ki < input.count; ki++) {
                int32_t source_key = cubic ? ki * 3 + 1 : ki;
                times[ki] = gltf_curve_time(&input, ki);
                if (times[ki] > duration)
                    duration = times[ki];
                if (path == RT_NODE_ANIM_PATH_WEIGHTS) {
                    int32_t in_base = cubic ? (ki * 3) * width : ki * width;
                    int32_t base = (cubic ? (ki * 3 + 1) * width : ki * width);
                    int32_t out_base = cubic ? (ki * 3 + 2) * width : ki * width;
                    for (int32_t wi = 0; wi < width; wi++)
                        values[(size_t)ki * (size_t)width + (size_t)wi] =
                            gltf_accessor_read_flat_f32(&output, base + wi);
                    if (cubic) {
                        for (int32_t wi = 0; wi < width; wi++) {
                            in_tangents[(size_t)ki * (size_t)width + (size_t)wi] =
                                gltf_accessor_read_flat_f32(&output, in_base + wi);
                            out_tangents[(size_t)ki * (size_t)width + (size_t)wi] =
                                gltf_accessor_read_flat_f32(&output, out_base + wi);
                        }
                    }
                } else {
                    float tmp[4] = {
                        0.0f, 0.0f, 0.0f, path == RT_NODE_ANIM_PATH_ROTATION ? 1.0f : 0.0f};
                    gltf_accessor_read_f32(&output, source_key, tmp, width);
                    if (path == RT_NODE_ANIM_PATH_ROTATION)
                        gltf_normalize_sample_if_quat(tmp, 4);
                    memcpy(&values[(size_t)ki * (size_t)width], tmp, (size_t)width * sizeof(float));
                    if (cubic) {
                        float in_tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        float out_tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        gltf_accessor_read_f32(&output, ki * 3, in_tmp, width);
                        gltf_accessor_read_f32(&output, ki * 3 + 2, out_tmp, width);
                        memcpy(&in_tangents[(size_t)ki * (size_t)width],
                               in_tmp,
                               (size_t)width * sizeof(float));
                        memcpy(&out_tangents[(size_t)ki * (size_t)width],
                               out_tmp,
                               (size_t)width * sizeof(float));
                    }
                }
            }
            target_name = gltf_effective_node_name(
                nodes_arr, (int32_t)node_idx, fallback_name, sizeof(fallback_name));
            if (target_name) {
                int64_t channel_index;
                if (cubic) {
                    channel_index =
                        rt_node_animation3d_add_cubic_channel(node_anim,
                                                              rt_const_cstr(target_name),
                                                              path,
                                                              input.count,
                                                              width,
                                                              times,
                                                              values,
                                                              in_tangents,
                                                              out_tangents);
                } else {
                    channel_index = rt_node_animation3d_add_channel(
                        node_anim,
                        rt_const_cstr(target_name),
                        path,
                        (interpolation && strcmp(interpolation, "STEP") == 0)
                            ? RT_NODE_ANIM_INTERP_STEP
                            : RT_NODE_ANIM_INTERP_LINEAR,
                        input.count,
                        width,
                        times,
                        values);
                }
                if (channel_index >= 0)
                    emitted_any = 1;
            }
            free(times);
            free(values);
            free(in_tangents);
            free(out_tangents);
        }
        ((rt_node_animation3d *)node_anim)->duration = duration > 0.0 ? duration : 1.0;
        if (!emitted_any) {
            gltf_release_ref(&node_anim);
            continue;
        }
        asset->node_animations[asset->node_animation_count++] = node_anim;
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
static void *rt_gltf_load_impl(rt_string path,
                               int load_assets,
                               uint8_t *preloaded_data,
                               size_t preloaded_size,
                               rt_gltf_preload_bundle *preload_bundle) {
    if (!path) {
        free(preloaded_data);
        return NULL;
    }
    const char *filepath = rt_string_cstr(path);
    if (!filepath) {
        free(preloaded_data);
        return NULL;
    }

    size_t file_size = 0;
    uint8_t *file_data = preloaded_data;
    if (file_data) {
        file_size = preloaded_size;
    } else {
        file_data = gltf_load_root_bytes(path, filepath, load_assets, &file_size);
    }
    if (!file_data) {
        if (load_assets)
            gltf_trap_asset_dependency(filepath, filepath, "model");
        return NULL;
    }
    long fsize = (long)file_size;

    // Detect .glb vs .gltf
    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;
    int parse_error = 0;

    if ((size_t)fsize >= 12 && file_data[0] == 0x67 && file_data[1] == 0x6C &&
        file_data[2] == 0x54 && file_data[3] == 0x46) {
        // GLB binary container
        uint32_t version = gltf_read_u32_le(file_data + 4);
        uint32_t declared_len = gltf_read_u32_le(file_data + 8);
        int chunk_index = 0;
        if (version != 2 || declared_len != (uint32_t)fsize)
            parse_error = 1;

        // Parse chunks
        size_t pos = 12;
        while (!parse_error && pos + 8 <= (size_t)fsize) {
            uint32_t chunk_len = gltf_read_u32_le(file_data + pos);
            uint32_t chunk_type = gltf_read_u32_le(file_data + pos + 4);
            pos += 8;
            if ((chunk_len & 3u) != 0 || chunk_len > (size_t)fsize - pos) {
                parse_error = 1;
                break;
            }
            if (chunk_index == 0 && chunk_type != 0x4E4F534A) {
                parse_error = 1;
                break;
            }

            if (chunk_type == 0x4E4F534A) {
                // JSON chunk
                if (json_str || chunk_len == 0) {
                    parse_error = 1;
                    break;
                }
                json_str = (char *)malloc(chunk_len + 1);
                if (json_str) {
                    memcpy(json_str, file_data + pos, chunk_len);
                    json_str[chunk_len] = '\0';
                } else {
                    parse_error = 1;
                    break;
                }
            } else if (chunk_type == 0x004E4942) {
                // BIN chunk
                if (bin_chunk) {
                    parse_error = 1;
                    break;
                }
                bin_chunk = file_data + pos;
                bin_chunk_len = chunk_len;
            }
            pos += chunk_len;
            chunk_index++;
        }
        if (!parse_error && pos != (size_t)fsize)
            parse_error = 1;
    } else {
        // Text .gltf
        json_str = (char *)malloc((size_t)fsize + 1);
        if (json_str) {
            memcpy(json_str, file_data, (size_t)fsize);
            json_str[fsize] = '\0';
        }
    }

    if (parse_error) {
        free(json_str);
        free(file_data);
        return NULL;
    }

    if (!json_str) {
        free(file_data);
        return NULL;
    }
    if (!gltf_validate_required_data_uri_images(json_str, strlen(json_str))) {
        free(json_str);
        free(file_data);
        return NULL;
    }

    // Parse JSON
    rt_string json_rts = rt_const_cstr(json_str);
    void *root = NULL;
    jmp_buf json_recovery;
    rt_trap_set_recovery(&json_recovery);
    if (setjmp(json_recovery) == 0)
        root = rt_json_parse_object(json_rts);
    rt_trap_clear_recovery();
    free(json_str);
    if (!root) {
        free(file_data);
        return NULL;
    }
    {
        void *asset_json = jget(root, "asset");
        const char *version = jstr(asset_json, "version");
        if (!version || strncmp(version, "2.", 2) != 0) {
            gltf_release_local(root);
            free(file_data);
            return NULL;
        }
    }
    if (!gltf_validate_required_extensions(root)) {
        gltf_release_local(root);
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
    void **mesh_variant_sources = NULL;
    gltf_material_info_t *material_infos = NULL;
    gltf_skin_t *skins = NULL;
    int32_t skin_count = 0;
    void **imported_lights = NULL;
    int32_t imported_light_count = 0;
    int32_t *mesh_applied_skin = NULL;
    int load_failed = 0;
    if (!buffers) {
        gltf_release_local(root);
        free(file_data);
        return NULL;
    }

    for (int i = 0; i < buf_count; i++) {
        void *buf_obj = rt_seq_get(buffers_arr, (int64_t)i);
        int64_t byte_length_raw = jint(buf_obj, "byteLength", -1);
        size_t byte_length = 0;
        const char *uri = jstr(buf_obj, "uri");
        if (byte_length_raw < 0) {
            load_failed = 1;
            break;
        }
        byte_length = (size_t)byte_length_raw;

        if (i == 0 && bin_chunk && !uri) {
            // GLB: buffer 0 is the BIN chunk
            if (byte_length > bin_chunk_len || byte_length > SIZE_MAX - 3u ||
                bin_chunk_len > byte_length + 3u) {
                load_failed = 1;
                break;
            }
            buffers[i].data = bin_chunk;
            buffers[i].len = byte_length;
        } else if (uri) {
            if (strncmp(uri, "data:", 5) == 0) {
                char mime_type[64];
                uint8_t *decoded = NULL;
                size_t decoded_len = 0;
                char preload_key[64];
                gltf_preload_buffer_key(i, preload_key, sizeof(preload_key));
                decoded = gltf_preload_bundle_take_dependency(
                    preload_bundle, preload_key, GLTF_PRELOAD_DEP_BUFFER, &decoded_len);
                if (decoded ||
                    gltf_parse_data_uri(
                        uri, mime_type, sizeof(mime_type), &decoded, &decoded_len)) {
                    if (decoded_len < byte_length) {
                        free(decoded);
                        load_failed = 1;
                        break;
                    }
                    buffers[i].data = decoded;
                    buffers[i].len = byte_length;
                } else if (byte_length > 0) {
                    load_failed = 1;
                    break;
                }
            } else {
                // External file — resolve relative to .gltf directory
                char buf_path[1024];
                gltf_resolve_relative_path(filepath, uri, buf_path, sizeof(buf_path));
                if (buf_path[0] == '\0') {
                    load_failed = 1;
                    break;
                }
                buffers[i].data =
                    gltf_load_dependency_bytes(
                        buf_path, load_assets, byte_length, preload_bundle, NULL);
                if (buffers[i].data)
                    buffers[i].len = byte_length;
                if (byte_length > 0 && (!buffers[i].data || buffers[i].len < byte_length)) {
                    if (load_assets)
                        gltf_trap_asset_dependency(filepath, buf_path, "buffer");
                    load_failed = 1;
                    break;
                }
            }
        } else if (byte_length > 0) {
            load_failed = 1;
            break;
        }
    }
    if (load_failed) {
        for (int i = 0; i < buf_count; i++) {
            if (buffers[i].data != bin_chunk)
                free(buffers[i].data);
        }
        free(buffers);
        gltf_release_local(root);
        free(file_data);
        return NULL;
    }

    // Create asset
    rt_gltf_asset *asset =
        (rt_gltf_asset *)rt_obj_new_i64(RT_G3D_GLTF_ASSET_CLASS_ID, (int64_t)sizeof(rt_gltf_asset));
    if (!asset) {
        for (int i = 0; i < buf_count; i++) {
            if (buffers[i].data != bin_chunk)
                free(buffers[i].data);
        }
        free(mesh_prim_start);
        free(mesh_prim_count);
        free(primitive_materials);
        free(buffers);
        gltf_release_local(root);
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
    asset->node_animations = NULL;
    asset->node_animation_count = 0;
    asset->cameras = NULL;
    asset->camera_count = 0;
    asset->camera_capacity = 0;
    asset->scenes = NULL;
    asset->scene_count = 0;
    asset->scene_capacity = 0;
    asset->scene_root = NULL;
    asset->node_count = 0;
    rt_obj_set_finalizer(asset, gltf_asset_finalize);

    void **images = NULL;
    void **texture_images = NULL;
    uint8_t *texture_supported = NULL;
    gltf_sampler_info_t *texture_samplers = NULL;
    void *images_arr = jarr(root, "images");
    int image_count = (int)jarr_len(images_arr);
    void *textures_arr = jarr(root, "textures");
    int texture_count = (int)jarr_len(textures_arr);
    uint8_t *image_required = NULL;
    if (image_count > 0)
        images = (void **)calloc((size_t)image_count, sizeof(void *));
    if (image_count > 0 && texture_count > 0) {
        image_required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
        if (image_required) {
            for (int i = 0; i < texture_count; i++) {
                void *texture_json = rt_seq_get(textures_arr, (int64_t)i);
                int64_t source_idx = jint(texture_json, "source", -1);
                if (source_idx >= 0 && source_idx < image_count)
                    image_required[source_idx] = 1u;
            }
        }
    }

    for (int i = 0; i < image_count && images; i++) {
        void *image_json = rt_seq_get(images_arr, (int64_t)i);
        const char *uri = jstr(image_json, "uri");
        const char *mime_type = jstr(image_json, "mimeType");
        uint8_t *owned_data = NULL;
        const uint8_t *image_data = NULL;
        size_t image_len = 0;
        char image_name[64];
        char parsed_mime[64];
        int required_image = image_required ? image_required[i] != 0 : 0;
        parsed_mime[0] = '\0';

        if (uri && strncmp(uri, "data:", 5) == 0) {
            char preload_key[96];
            gltf_data_uri_copy_mime(uri, parsed_mime, sizeof(parsed_mime));
            gltf_preload_image_key(
                i, mime_type ? mime_type : parsed_mime, preload_key, sizeof(preload_key));
            images[i] = gltf_preload_take_decoded_image(preload_bundle, preload_key);
            if (!images[i]) {
                owned_data = gltf_preload_bundle_take_dependency(
                    preload_bundle, preload_key, GLTF_PRELOAD_DEP_IMAGE, &image_len);
                if (owned_data) {
                    image_data = owned_data;
                    if (!mime_type && parsed_mime[0] != '\0')
                        mime_type = parsed_mime;
                } else if (gltf_parse_data_uri(
                               uri, parsed_mime, sizeof(parsed_mime), &owned_data, &image_len)) {
                    image_data = owned_data;
                    if (!mime_type && parsed_mime[0] != '\0')
                        mime_type = parsed_mime;
                } else if (required_image &&
                           gltf_preload_image_is_supported_format(mime_type ? mime_type
                                                                            : parsed_mime)) {
                    load_failed = 1;
                    break;
                }
            }
        } else if (uri) {
            char image_path[1024];
            gltf_resolve_relative_path(filepath, uri, image_path, sizeof(image_path));
            if (image_path[0] != '\0')
                images[i] = gltf_load_dependency_image(image_path, load_assets, preload_bundle);
            if (!images[i] && required_image &&
                gltf_preload_image_is_supported_format(image_path)) {
                if (load_assets)
                    gltf_trap_asset_dependency(filepath, image_path, "image");
                load_failed = 1;
                break;
            }
        } else {
            int64_t view_idx = jint(image_json, "bufferView", -1);
            char preload_key[96];
            gltf_preload_image_key(i, mime_type, preload_key, sizeof(preload_key));
            images[i] = gltf_preload_take_decoded_image(preload_bundle, preload_key);
            if (!images[i]) {
                owned_data = gltf_preload_bundle_take_dependency(
                    preload_bundle, preload_key, GLTF_PRELOAD_DEP_IMAGE, &image_len);
                if (owned_data)
                    image_data = owned_data;
                else
                    image_data =
                        gltf_get_buffer_view_data(root, view_idx, buffers, buf_count, &image_len);
            }
            if (!images[i] && (!image_data || image_len == 0) && required_image &&
                gltf_preload_image_is_supported_format(mime_type)) {
                load_failed = 1;
                break;
            }
        }

        if (!images[i] && image_data && image_len > 0) {
            const char *image_type;
            gltf_build_embedded_name(mime_type, ".bin", image_name, sizeof(image_name));
            image_type = mime_type ? mime_type : image_name;
            if (gltf_preload_image_is_supported_format(image_type)) {
                uint8_t *rgba_blob = NULL;
                size_t rgba_len = 0;
                if (gltf_decode_image_payload_to_rgba_blob(
                        image_type, image_data, image_len, &rgba_blob, &rgba_len)) {
                    images[i] = gltf_pixels_from_rgba_blob(rgba_blob, rgba_len);
                    free(rgba_blob);
                    if (!images[i] && required_image) {
                        free(owned_data);
                        load_failed = 1;
                        break;
                    }
                } else if (required_image) {
                    free(owned_data);
                    load_failed = 1;
                    break;
                }
            } else {
                images[i] = rt_asset_decode_typed(image_name, image_data, image_len);
            }
        }
        free(owned_data);
    }

    if (texture_count > 0)
        texture_images = (void **)calloc((size_t)texture_count, sizeof(void *));
    if (texture_count > 0)
        texture_supported = (uint8_t *)calloc((size_t)texture_count, sizeof(uint8_t));
    if (texture_count > 0)
        texture_samplers =
            (gltf_sampler_info_t *)calloc((size_t)texture_count, sizeof(*texture_samplers));
    for (int i = 0; i < texture_count && texture_images; i++) {
        void *texture_json = rt_seq_get(textures_arr, (int64_t)i);
        int64_t source_idx = jint(texture_json, "source", -1);
        int64_t sampler_idx = jint(texture_json, "sampler", -1);
        if (texture_samplers) {
            void *samplers_arr = jarr(root, "samplers");
            void *sampler_json = sampler_idx >= 0 && sampler_idx < jarr_len(samplers_arr)
                                     ? rt_seq_get(samplers_arr, sampler_idx)
                                     : NULL;
            gltf_read_sampler_info(sampler_json, &texture_samplers[i]);
        }
        if (source_idx >= 0 && source_idx < image_count) {
            void *image_json = rt_seq_get(images_arr, source_idx);
            const char *image_uri = jstr(image_json, "uri");
            const char *image_mime = jstr(image_json, "mimeType");
            char parsed_mime[64];
            parsed_mime[0] = '\0';
            if (image_uri && strncmp(image_uri, "data:", 5) == 0)
                gltf_data_uri_copy_mime(image_uri, parsed_mime, sizeof(parsed_mime));
            if (texture_supported &&
                gltf_preload_image_is_supported_format(image_mime
                                                           ? image_mime
                                                           : (parsed_mime[0] != '\0' ? parsed_mime
                                                                                      : image_uri)))
                texture_supported[i] = 1u;
            texture_images[i] = images[source_idx];
        }
    }

    // Extract materials
    void *mats_arr = jarr(root, "materials");
    int mat_count = (int)jarr_len(mats_arr);
    int material_capacity = mat_count > 0 ? mat_count + 1 : 1;
    void *default_material = NULL;
    asset->materials = (void **)calloc((size_t)material_capacity, sizeof(void *));
    material_infos =
        (gltf_material_info_t *)calloc((size_t)material_capacity, sizeof(gltf_material_info_t));
    if (material_infos) {
        for (int i = 0; i < material_capacity; i++)
            for (int slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++)
                gltf_texture_info_init(&material_infos[i].slots[slot]);
    }
    if (mat_count > 0 && asset->materials) {
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
                    if (base_tex && material_infos) {
                        gltf_read_texture_info(
                            base_tex,
                            &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]);
                    }
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_texture(mat, texture_images[tex_idx]);
                    else if (gltf_texture_index_missing_supported_payload(
                                 tex_idx, texture_count, texture_images, texture_supported))
                        load_failed = 1;
                    gltf_apply_texture_slot(
                        texture_samplers,
                        texture_count,
                        tex_idx,
                        mat,
                        RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR,
                        material_infos
                            ? &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]
                            : NULL);
                }
                {
                    void *mr_tex = jget(pbr, "metallicRoughnessTexture");
                    int64_t tex_idx = jint(mr_tex, "index", -1);
                    if (mr_tex && material_infos) {
                        gltf_read_texture_info(
                            mr_tex,
                            &material_infos[i]
                                 .slots[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS]);
                    }
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_metallic_roughness_map(mat, texture_images[tex_idx]);
                    else if (gltf_texture_index_missing_supported_payload(
                                 tex_idx, texture_count, texture_images, texture_supported))
                        load_failed = 1;
                    gltf_apply_texture_slot(
                        texture_samplers,
                        texture_count,
                        tex_idx,
                        mat,
                        RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
                        material_infos ? &material_infos[i]
                                              .slots[RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS]
                                       : NULL);
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
                void *unlit = extensions ? jget(extensions, "KHR_materials_unlit") : NULL;
                void *specular = extensions ? jget(extensions, "KHR_materials_specular") : NULL;
                void *clearcoat = extensions ? jget(extensions, "KHR_materials_clearcoat") : NULL;
                void *transmission =
                    extensions ? jget(extensions, "KHR_materials_transmission") : NULL;
                if (emissive_strength)
                    rt_material3d_set_emissive_intensity(
                        mat, jnum(emissive_strength, "emissiveStrength", 1.0));
                if (unlit) {
                    rt_material3d_set_unlit(mat, 1);
                    rt_material3d_set_shading_model(mat, 3);
                }
                if (specular) {
                    void *spec_color = jarr(specular, "specularColorFactor");
                    void *spec_tex = jget(specular, "specularTexture");
                    int64_t tex_idx = jint(spec_tex, "index", -1);
                    ((rt_material3d *)mat)->specular[0] =
                        jnum(specular, "specularFactor", ((rt_material3d *)mat)->specular[0]);
                    ((rt_material3d *)mat)->specular[1] = ((rt_material3d *)mat)->specular[0];
                    ((rt_material3d *)mat)->specular[2] = ((rt_material3d *)mat)->specular[0];
                    if (spec_color && jarr_len(spec_color) >= 3) {
                        ((rt_material3d *)mat)->specular[0] = jvalue_num(
                            rt_seq_get(spec_color, 0), ((rt_material3d *)mat)->specular[0]);
                        ((rt_material3d *)mat)->specular[1] = jvalue_num(
                            rt_seq_get(spec_color, 1), ((rt_material3d *)mat)->specular[1]);
                        ((rt_material3d *)mat)->specular[2] = jvalue_num(
                            rt_seq_get(spec_color, 2), ((rt_material3d *)mat)->specular[2]);
                    }
                    if (spec_tex && material_infos) {
                        gltf_read_texture_info(
                            spec_tex,
                            &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR]);
                    }
                    if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                        texture_images[tex_idx])
                        rt_material3d_set_specular_map(mat, texture_images[tex_idx]);
                    else if (gltf_texture_index_missing_supported_payload(
                                 tex_idx, texture_count, texture_images, texture_supported))
                        load_failed = 1;
                    gltf_apply_texture_slot(
                        texture_samplers,
                        texture_count,
                        tex_idx,
                        mat,
                        RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR,
                        material_infos
                            ? &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR]
                            : NULL);
                }
                if (clearcoat) {
                    double clearcoat_factor = jnum(clearcoat, "clearcoatFactor", 0.0);
                    if (clearcoat_factor > ((rt_material3d *)mat)->reflectivity)
                        rt_material3d_set_reflectivity(mat, clearcoat_factor);
                }
                if (transmission) {
                    double transmission_factor = jnum(transmission, "transmissionFactor", 0.0);
                    if (transmission_factor > 0.0) {
                        rt_material3d_set_reflectivity(mat, transmission_factor);
                        rt_material3d_set_alpha(mat, 1.0 - transmission_factor);
                        rt_material3d_set_alpha_mode(mat, RT_MATERIAL3D_ALPHA_MODE_BLEND);
                    }
                }
            }

            {
                void *normal_tex = jget(mat_json, "normalTexture");
                int64_t tex_idx = jint(normal_tex, "index", -1);
                if (normal_tex && material_infos) {
                    gltf_read_texture_info(
                        normal_tex, &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL]);
                }
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_normal_map(mat, texture_images[tex_idx]);
                else if (gltf_texture_index_missing_supported_payload(
                             tex_idx, texture_count, texture_images, texture_supported))
                    load_failed = 1;
                gltf_apply_texture_slot(
                    texture_samplers,
                    texture_count,
                    tex_idx,
                    mat,
                    RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
                    material_infos ? &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_NORMAL]
                                   : NULL);
                if (normal_tex)
                    rt_material3d_set_normal_scale(mat, jnum(normal_tex, "scale", 1.0));
            }
            {
                void *occlusion_tex = jget(mat_json, "occlusionTexture");
                int64_t tex_idx = jint(occlusion_tex, "index", -1);
                if (occlusion_tex && material_infos) {
                    gltf_read_texture_info(occlusion_tex,
                                           &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_AO]);
                }
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_ao_map(mat, texture_images[tex_idx]);
                else if (gltf_texture_index_missing_supported_payload(
                             tex_idx, texture_count, texture_images, texture_supported))
                    load_failed = 1;
                gltf_apply_texture_slot(
                    texture_samplers,
                    texture_count,
                    tex_idx,
                    mat,
                    RT_MATERIAL3D_TEXTURE_SLOT_AO,
                    material_infos ? &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_AO]
                                   : NULL);
                if (occlusion_tex)
                    rt_material3d_set_ao(mat, jnum(occlusion_tex, "strength", 1.0));
            }
            {
                void *emissive_tex = jget(mat_json, "emissiveTexture");
                int64_t tex_idx = jint(emissive_tex, "index", -1);
                if (emissive_tex && material_infos) {
                    gltf_read_texture_info(
                        emissive_tex,
                        &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE]);
                }
                if (tex_idx >= 0 && tex_idx < texture_count && texture_images &&
                    texture_images[tex_idx])
                    rt_material3d_set_emissive_map(mat, texture_images[tex_idx]);
                else if (gltf_texture_index_missing_supported_payload(
                             tex_idx, texture_count, texture_images, texture_supported))
                    load_failed = 1;
                gltf_apply_texture_slot(
                    texture_samplers,
                    texture_count,
                    tex_idx,
                    mat,
                    RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE,
                    material_infos ? &material_infos[i].slots[RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE]
                                   : NULL);
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

            if (load_failed) {
                gltf_release_local(mat);
                break;
            }
            asset->materials[i] = mat;
            asset->material_count = i + 1;
        }
    }

    gltf_parse_punctual_lights(root, &imported_lights, &imported_light_count);

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
                int mesh_start = mesh_idx;
                if (mesh_prim_start)
                    mesh_prim_start[mi] = mesh_start;
                if (mesh_prim_count)
                    mesh_prim_count[mi] = 0;

                for (int pi = 0; pi < prim_count; pi++) {
                    void *prim = rt_seq_get(prims, (int64_t)pi);
                    void *attrs = jget(prim, "attributes");
                    int64_t material_idx = jint(prim, "material", -1);
                    if (!attrs)
                        continue;

                    // Get accessor indices
                    int64_t pos_acc = jint(attrs, "POSITION", -1);
                    int64_t norm_acc = jint(attrs, "NORMAL", -1);
                    int64_t uv0_acc = jint(attrs, "TEXCOORD_0", -1);
                    int64_t uv1_acc = jint(attrs, "TEXCOORD_1", -1);
                    int64_t color_acc = jint(attrs, "COLOR_0", -1);
                    int64_t tangent_acc = jint(attrs, "TANGENT", -1);
                    int64_t joints_acc = jint(attrs, "JOINTS_0", -1);
                    int64_t weights_acc = jint(attrs, "WEIGHTS_0", -1);
                    int64_t joints1_acc = jint(attrs, "JOINTS_1", -1);
                    int64_t weights1_acc = jint(attrs, "WEIGHTS_1", -1);
                    int64_t idx_acc = jint(prim, "indices", -1);
                    int64_t mode = jint(prim, "mode", 4);
                    void *prim_material = NULL;

                    if (pos_acc < 0)
                        continue;
                    if (material_idx >= 0 && material_idx < asset->material_count)
                        prim_material = asset->materials[material_idx];
                    if (!prim_material && asset->materials) {
                        if (!default_material && asset->material_count < material_capacity) {
                            default_material = rt_material3d_new_pbr(1.0, 1.0, 1.0);
                            if (default_material) {
                                rt_material3d_set_metallic(default_material, 1.0);
                                rt_material3d_set_roughness(default_material, 1.0);
                                asset->materials[asset->material_count++] = default_material;
                            }
                        }
                        prim_material = default_material;
                    }

                    {
                        uint32_t decoded_mesh_flags = 0u;
                        void *decoded_mesh =
                            gltf_preload_take_decoded_mesh(preload_bundle, mi, pi, &decoded_mesh_flags);
                        if (decoded_mesh) {
                            if ((decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_NORMALS) == 0)
                                rt_mesh3d_recalc_normals(decoded_mesh);
                            if ((decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_TANGENTS) == 0 &&
                                (decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_UV0) != 0) {
                                rt_material3d *tangent_material = (rt_material3d *)prim_material;
                                if (tangent_material && tangent_material->normal_map)
                                    rt_mesh3d_calc_tangents(decoded_mesh);
                            }
                            asset->meshes[mesh_idx++] = decoded_mesh;
                            asset->mesh_count = mesh_idx;
                            if (primitive_materials)
                                primitive_materials[mesh_idx - 1] = prim_material;
                            continue;
                        }
                    }

                    gltf_accessor_view_t pos_view;
                    gltf_accessor_view_t norm_view;
                    gltf_accessor_view_t uv0_view;
                    gltf_accessor_view_t uv1_view;
                    gltf_accessor_view_t color_view;
                    gltf_accessor_view_t tangent_view;
                    gltf_accessor_view_t joints_view;
                    gltf_accessor_view_t weights_view;
                    gltf_accessor_view_t joints1_view;
                    gltf_accessor_view_t weights1_view;
                    gltf_accessor_view_t idx_view;
                    int has_normals =
                        gltf_get_accessor_view(root, norm_acc, buffers, buf_count, &norm_view);
                    int has_uv0 =
                        gltf_get_accessor_view(root, uv0_acc, buffers, buf_count, &uv0_view);
                    int has_uv1 =
                        gltf_get_accessor_view(root, uv1_acc, buffers, buf_count, &uv1_view);
                    int has_colors =
                        gltf_get_accessor_view(root, color_acc, buffers, buf_count, &color_view);
                    int has_tangents = gltf_get_accessor_view(
                        root, tangent_acc, buffers, buf_count, &tangent_view);
                    int has_joints =
                        gltf_get_accessor_view(root, joints_acc, buffers, buf_count, &joints_view);
                    int has_weights = gltf_get_accessor_view(
                        root, weights_acc, buffers, buf_count, &weights_view);
                    int has_joints1 = gltf_get_accessor_view(
                        root, joints1_acc, buffers, buf_count, &joints1_view);
                    int has_weights1 = gltf_get_accessor_view(
                        root, weights1_acc, buffers, buf_count, &weights1_view);
                    int has_indices =
                        gltf_get_accessor_view(root, idx_acc, buffers, buf_count, &idx_view);

                    if (!gltf_get_accessor_view(root, pos_acc, buffers, buf_count, &pos_view) ||
                        pos_view.count == 0)
                        continue;

                    // Create mesh and populate vertices
                    void *mesh = rt_mesh3d_new();
                    int primitive_failed = 0;
                    if (!mesh)
                        continue;

                    for (int32_t vi = 0; vi < pos_view.count; vi++) {
                        float pos[3] = {0.0f, 0.0f, 0.0f};
                        float nrm[3] = {0.0f, 0.0f, 0.0f};
                        float uv[2] = {0.0f, 0.0f};
                        float uv1[2] = {0.0f, 0.0f};
                        float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                        float tangent[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                        float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        uint32_t joints[4] = {0u, 0u, 0u, 0u};
                        float weights1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                        uint32_t joints1[4] = {0u, 0u, 0u, 0u};
                        gltf_accessor_read_f32(&pos_view, vi, pos, 3);
                        if (has_normals)
                            gltf_accessor_read_f32(&norm_view, vi, nrm, 3);
                        if (has_uv0)
                            gltf_accessor_read_f32(&uv0_view, vi, uv, 2);
                        if (has_uv1)
                            gltf_accessor_read_f32(&uv1_view, vi, uv1, 2);
                        else {
                            uv1[0] = uv[0];
                            uv1[1] = uv[1];
                        }
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
                        if (has_joints1)
                            gltf_accessor_read_u32(&joints1_view, vi, joints1, 4);
                        if (has_weights1)
                            gltf_accessor_read_f32(&weights1_view, vi, weights1, 4);
                        if (has_joints1 && has_weights1) {
                            uint32_t merged_joints[4] = {0u, 0u, 0u, 0u};
                            float merged_weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                            if (has_joints && has_weights) {
                                for (int j = 0; j < 4; j++)
                                    gltf_add_top_joint_influence(
                                        joints[j], weights[j], merged_joints, merged_weights);
                            }
                            for (int j = 0; j < 4; j++)
                                gltf_add_top_joint_influence(
                                    joints1[j], weights1[j], merged_joints, merged_weights);
                            gltf_normalize_joint_influences(merged_weights);
                            memcpy(joints, merged_joints, sizeof(joints));
                            memcpy(weights, merged_weights, sizeof(weights));
                        } else {
                            for (int j = 0; j < 4; j++) {
                                if (joints[j] >= VGFX3D_MAX_BONES)
                                    weights[j] = 0.0f;
                            }
                        }

                        rt_mesh3d_add_vertex(
                            mesh, pos[0], pos[1], pos[2], nrm[0], nrm[1], nrm[2], uv[0], uv[1]);
                        if (((rt_mesh3d *)mesh)->build_failed ||
                            ((rt_mesh3d *)mesh)->vertex_count <= (uint32_t)vi) {
                            primitive_failed = 1;
                            break;
                        }
                        vgfx3d_vertex_t *vertex = &((rt_mesh3d *)mesh)->vertices[vi];
                        vertex->uv1[0] = uv1[0];
                        vertex->uv1[1] = uv1[1];
                        memcpy(vertex->color, color, sizeof(vertex->color));
                        memcpy(vertex->tangent, tangent, sizeof(vertex->tangent));
                        if (has_joints || has_weights || has_joints1 || has_weights1) {
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
                                if (weights[j] > 0.0001f && joints[j] < VGFX3D_MAX_BONES &&
                                    (int32_t)(joints[j] + 1u) > ((rt_mesh3d *)mesh)->bone_count) {
                                    ((rt_mesh3d *)mesh)->bone_count = (int32_t)(joints[j] + 1u);
                                }
                            }
                        }
                    }
                    if (primitive_failed) {
                        gltf_release_local(mesh);
                        continue;
                    }

                    if (!gltf_append_primitive_indices(
                            mesh, mode, has_indices ? &idx_view : NULL, pos_view.count)) {
                        gltf_release_local(mesh);
                        continue;
                    }

                    // Recalc normals if none provided
                    if (!has_normals && ((rt_mesh3d *)mesh)->vertex_count > 0)
                        rt_mesh3d_recalc_normals(mesh);
                    if (!has_tangents && has_uv0 && material_idx >= 0 &&
                        material_idx < asset->material_count) {
                        rt_material3d *tangent_material = (rt_material3d *)prim_material;
                        if (tangent_material && tangent_material->normal_map)
                            rt_mesh3d_calc_tangents(mesh);
                    }
                    gltf_import_primitive_morph_targets(
                        root, buffers, buf_count, mesh_json, prim, mesh, pos_view.count);

                    asset->meshes[mesh_idx++] = mesh;
                    asset->mesh_count = mesh_idx;
                    if (primitive_materials)
                        primitive_materials[mesh_idx - 1] = prim_material;
                }
                if (mesh_prim_count)
                    mesh_prim_count[mi] = mesh_idx - mesh_start;
            }
        }
    }

    if (asset->mesh_count > 0) {
        mesh_variant_sources = (void **)calloc((size_t)asset->mesh_count, sizeof(void *));
        if (mesh_variant_sources) {
            for (int32_t i = 0; i < asset->mesh_count; i++)
                mesh_variant_sources[i] = rt_mesh3d_clone(asset->meshes[i]);
        }
    }

    gltf_parse_skins(asset, root, buffers, buf_count, &skins, &skin_count, &load_failed);
    if (!load_failed) {
        gltf_parse_animations(asset, root, buffers, buf_count, skins, skin_count);
        gltf_parse_node_animations(asset, root, buffers, buf_count, skins, skin_count);
        if (asset->mesh_count > 0) {
            mesh_applied_skin =
                (int32_t *)malloc((size_t)asset->mesh_count * sizeof(*mesh_applied_skin));
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
                int *node_parent = NULL;
                int graph_valid =
                    gltf_validate_node_graph(nodes_arr, node_json_count, &node_parent);
                rt_scene_node3d **nodes =
                    graph_valid
                        ? (rt_scene_node3d **)calloc((size_t)node_json_count, sizeof(*nodes))
                        : NULL;
                for (int ni = 0; ni < node_json_count && nodes; ni++) {
                    void *node_json = rt_seq_get(nodes_arr, (int64_t)ni);
                    rt_scene_node3d *node = (rt_scene_node3d *)rt_scene_node3d_new();
                    const char *name = jstr(node_json, "name");
                    char fallback_name[64];
                    void *translation = jarr(node_json, "translation");
                    void *rotation = jarr(node_json, "rotation");
                    void *scale_arr = jarr(node_json, "scale");
                    void *matrix_arr = jarr(node_json, "matrix");
                    void *weights_arr = jarr(node_json, "weights");
                    void *node_extensions = jget(node_json, "extensions");
                    void *node_punctual_light =
                        node_extensions ? jget(node_extensions, "KHR_lights_punctual") : NULL;
                    int64_t mesh_ref = jint(node_json, "mesh", -1);
                    int64_t skin_ref = jint(node_json, "skin", -1);
                    int64_t light_ref = jint(node_punctual_light, "light", -1);
                    nodes[ni] = node;
                    if (!node)
                        continue;

                    if (!name || name[0] == '\0')
                        name = gltf_effective_node_name(
                            nodes_arr, ni, fallback_name, sizeof(fallback_name));
                    gltf_set_node_name(node, name);
                    if (light_ref >= 0 && light_ref < imported_light_count && imported_lights &&
                        imported_lights[light_ref])
                        rt_scene_node3d_set_light(node, imported_lights[light_ref]);

                    if (matrix_arr && jarr_len(matrix_arr) >= 16) {
                        double m[16];
                        for (int i = 0; i < 16; i++)
                            m[i] = jvalue_num(rt_seq_get(matrix_arr, (int64_t)i),
                                              i % 5 == 0 ? 1.0 : 0.0);
                        double row_major[16];
                        gltf_matrix_column_major_to_row_major(m, row_major);
                        gltf_matrix_to_trs(
                            row_major, node->position, node->rotation, node->scale_xyz);
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

                    if (mesh_ref >= 0 && mesh_ref < mesh_json_count && mesh_prim_count &&
                        mesh_prim_start) {
                        int prim_start = mesh_prim_start[mesh_ref];
                        int prim_count = mesh_prim_count[mesh_ref];
                        if (prim_count > 0) {
                            int mesh_index = prim_start;
                            void *node_mesh = NULL;
                            if (mesh_index >= 0 && mesh_index < asset->mesh_count) {
                                if (skin_ref >= 0 && skin_ref < skin_count && mesh_applied_skin &&
                                    mesh_applied_skin[mesh_index] < 0) {
                                    gltf_apply_skin_to_mesh(asset->meshes[mesh_index],
                                                            &skins[skin_ref]);
                                    mesh_applied_skin[mesh_index] = (int32_t)skin_ref;
                                }
                                node_mesh = gltf_make_node_mesh_variant(asset,
                                                                        mesh_index,
                                                                        skin_ref,
                                                                        weights_arr,
                                                                        skins,
                                                                        skin_count,
                                                                        mesh_variant_sources);
                            }
                            rt_scene_node3d_set_mesh(node, node_mesh);
                            if (node_mesh && node_mesh != asset->meshes[mesh_index])
                                gltf_release_local(node_mesh);
                            if (primitive_materials && primitive_materials[prim_start])
                                rt_scene_node3d_set_material(node, primitive_materials[prim_start]);
                            for (int pi = 1; pi < prim_count; pi++) {
                                void *prim_mesh = NULL;
                                int child_mesh_index = prim_start + pi;
                                rt_scene_node3d *prim_node =
                                    (rt_scene_node3d *)rt_scene_node3d_new();
                                if (!prim_node)
                                    continue;
                                if (child_mesh_index >= 0 && child_mesh_index < asset->mesh_count) {
                                    if (skin_ref >= 0 && skin_ref < skin_count &&
                                        mesh_applied_skin &&
                                        mesh_applied_skin[child_mesh_index] < 0) {
                                        gltf_apply_skin_to_mesh(asset->meshes[child_mesh_index],
                                                                &skins[skin_ref]);
                                        mesh_applied_skin[child_mesh_index] = (int32_t)skin_ref;
                                    }
                                    prim_mesh = gltf_make_node_mesh_variant(asset,
                                                                            child_mesh_index,
                                                                            skin_ref,
                                                                            weights_arr,
                                                                            skins,
                                                                            skin_count,
                                                                            mesh_variant_sources);
                                }
                                rt_scene_node3d_set_mesh(prim_node, prim_mesh);
                                if (prim_mesh && prim_mesh != asset->meshes[child_mesh_index])
                                    gltf_release_local(prim_mesh);
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
                        if (child_idx >= 0 && child_idx < node_json_count && nodes[ni] &&
                            nodes[child_idx]) {
                            rt_scene_node3d_add_child(nodes[ni], nodes[child_idx]);
                        }
                    }
                }

                if (graph_valid && nodes) {
                    void *scenes_arr = jarr(root, "scenes");
                    int64_t active_scene = jint(root, "scene", 0);
                    int64_t scene_count = jarr_len(scenes_arr);
                    int built_any = 0;
                    int active_valid = scenes_arr && scene_count > 0 && active_scene >= 0 &&
                                       active_scene < scene_count;
                    if (scenes_arr && scene_count > 0) {
                        for (int64_t order = 0; order < scene_count && !load_failed; order++) {
                            int64_t scene_index =
                                active_valid ? (order == 0 ? active_scene
                                                           : (order <= active_scene ? order - 1
                                                                                    : order))
                                             : order;
                            void *scene_json = rt_seq_get(scenes_arr, scene_index);
                            void *scene_nodes = jarr(scene_json, "nodes");
                            int *scene_seen =
                                (int *)calloc((size_t)node_json_count, sizeof(int));
                            uint8_t *scene_active =
                                (uint8_t *)calloc((size_t)node_json_count, sizeof(*scene_active));
                            rt_scene_node3d *scene_root = NULL;
                            int scene_roots_valid = scene_seen != NULL && scene_active != NULL;
                            int attached_any = 0;
                            char fallback_scene_name[64];
                            const char *scene_name = jstr(scene_json, "name");
                            for (int i = 0; scene_roots_valid && i < jarr_len(scene_nodes); i++) {
                                int64_t node_idx =
                                    jvalue_int(rt_seq_get(scene_nodes, (int64_t)i), -1);
                                if (node_idx < 0 || node_idx >= node_json_count ||
                                    scene_seen[node_idx] ||
                                    (node_parent && node_parent[node_idx] >= 0)) {
                                    scene_roots_valid = 0;
                                    break;
                                }
                                scene_seen[node_idx] = 1;
                            }
                            if (scene_roots_valid) {
                                scene_root = (rt_scene_node3d *)rt_scene_node3d_new();
                                if (!scene_root) {
                                    load_failed = 1;
                                } else {
                                    for (int i = 0; i < jarr_len(scene_nodes); i++) {
                                        int64_t node_idx =
                                            jvalue_int(rt_seq_get(scene_nodes, (int64_t)i), -1);
                                        if (node_idx >= 0 && node_idx < node_json_count &&
                                            nodes[node_idx]) {
                                            rt_scene_node3d *clone =
                                                gltf_clone_scene_node(nodes[node_idx]);
                                            if (!clone) {
                                                load_failed = 1;
                                                break;
                                            }
                                            rt_scene_node3d_add_child(scene_root, clone);
                                            gltf_release_local(clone);
                                            if (!gltf_mark_active_node_subtree(nodes_arr,
                                                                               node_json_count,
                                                                               (int32_t)node_idx,
                                                                               scene_active)) {
                                                load_failed = 1;
                                                break;
                                            }
                                            attached_any = 1;
                                        }
                                    }
                                }
                            }
                            if (!load_failed && scene_root && attached_any) {
                                snprintf(fallback_scene_name,
                                         sizeof(fallback_scene_name),
                                         order == 0 ? "default" : "scene_%d",
                                         (int)scene_index);
                                if (!gltf_append_scene(asset,
                                                       scene_root,
                                                       (scene_name && scene_name[0] != '\0')
                                                           ? scene_name
                                                           : fallback_scene_name)) {
                                    load_failed = 1;
                                } else {
                                    gltf_scene_info_t *scene = &asset->scenes[asset->scene_count - 1];
                                    if (!gltf_import_scene_cameras(scene,
                                                                   root,
                                                                   nodes_arr,
                                                                   nodes,
                                                                   node_json_count,
                                                                   scene_active))
                                        load_failed = 1;
                                    scene_root = NULL;
                                    built_any = 1;
                                }
                            }
                            if (scene_root)
                                gltf_release_local(scene_root);
                            free(scene_active);
                            free(scene_seen);
                        }
                    }
                    if (!built_any && !load_failed) {
                        rt_scene_node3d *scene_root = (rt_scene_node3d *)rt_scene_node3d_new();
                        uint8_t *scene_active =
                            (uint8_t *)calloc((size_t)node_json_count, sizeof(*scene_active));
                        int attached_any = 0;
                        if (!scene_root || !scene_active) {
                            if (scene_root)
                                gltf_release_local(scene_root);
                            free(scene_active);
                            load_failed = 1;
                        } else {
                            for (int i = 0; i < node_json_count; i++) {
                                if (node_parent && node_parent[i] < 0 && nodes[i]) {
                                    rt_scene_node3d *clone = gltf_clone_scene_node(nodes[i]);
                                    if (!clone) {
                                        load_failed = 1;
                                        break;
                                    }
                                    rt_scene_node3d_add_child(scene_root, clone);
                                    gltf_release_local(clone);
                                    if (!gltf_mark_active_node_subtree(
                                            nodes_arr, node_json_count, i, scene_active)) {
                                        load_failed = 1;
                                        break;
                                    }
                                    attached_any = 1;
                                }
                            }
                            if (!load_failed && attached_any &&
                                gltf_append_scene(asset, scene_root, "default")) {
                                gltf_scene_info_t *scene = &asset->scenes[asset->scene_count - 1];
                                if (!gltf_import_scene_cameras(scene,
                                                               root,
                                                               nodes_arr,
                                                               nodes,
                                                               node_json_count,
                                                               scene_active))
                                    load_failed = 1;
                                scene_root = NULL;
                            } else if (!load_failed) {
                                load_failed = 1;
                            }
                            if (scene_root)
                                gltf_release_local(scene_root);
                            free(scene_active);
                        }
                    }
                    if (!load_failed && !gltf_install_active_scene_compat(asset))
                        load_failed = 1;
                }

                for (int i = 0; i < node_json_count && nodes; i++)
                    gltf_release_ref((void **)&nodes[i]);
                free(nodes);
                free(node_parent);
            }
        }
    }

    if (images) {
        for (int i = 0; i < image_count; i++)
            gltf_release_ref(&images[i]);
    }
    free(images);
    free(image_required);
    free(texture_images);
    free(texture_supported);
    free(texture_samplers);
    free(mesh_prim_start);
    free(mesh_prim_count);
    free(primitive_materials);
    free(material_infos);
    if (imported_lights) {
        for (int32_t i = 0; i < imported_light_count; i++)
            gltf_release_ref(&imported_lights[i]);
    }
    free(imported_lights);
    if (mesh_variant_sources) {
        for (int32_t i = 0; i < asset->mesh_count; i++)
            gltf_release_ref(&mesh_variant_sources[i]);
    }
    free(mesh_variant_sources);
    free(mesh_applied_skin);
    gltf_free_skins(skins, skin_count);

    // Cleanup buffers (except BIN chunk which is part of file_data)
    for (int i = 0; i < buf_count; i++) {
        if (buffers[i].data != bin_chunk)
            free(buffers[i].data);
    }
    free(buffers);
    gltf_release_local(root);
    free(file_data);

    if (load_failed) {
        gltf_release_ref((void **)&asset);
        return NULL;
    }
    return asset;
}

/// @brief Load a glTF/GLB from the filesystem (no asset-manager resolution). See header.
void *rt_gltf_load(rt_string path) {
    return rt_gltf_load_impl(path, 0, NULL, 0, NULL);
}

/// @brief Load a glTF/GLB through the asset manager (mounted/embedded + dev fallback). See header.
void *rt_gltf_load_asset(rt_string path) {
    return rt_gltf_load_impl(path, 1, NULL, 0, NULL);
}

/// @brief Internal async path: build a glTF/GLB asset from worker-staged root bytes.
/// @details Takes ownership of @p preloaded_data and frees it on all paths.
void *rt_gltf_load_preloaded(rt_string path,
                             uint8_t *preloaded_data,
                             size_t preloaded_size,
                             int load_assets) {
    return rt_gltf_load_impl(path, load_assets ? 1 : 0, preloaded_data, preloaded_size, NULL);
}

void *rt_gltf_load_preloaded_bundle(rt_string path,
                                    rt_gltf_preload_bundle *bundle,
                                    int load_assets) {
    uint8_t *root_data;
    size_t root_size;
    void *asset;
    if (!bundle)
        return load_assets ? rt_gltf_load_asset(path) : rt_gltf_load(path);
    root_data = bundle->root_data;
    root_size = bundle->root_size;
    bundle->root_data = NULL;
    bundle->root_size = 0;
    asset = rt_gltf_load_impl(path, load_assets ? 1 : 0, root_data, root_size, bundle);
    rt_gltf_preload_bundle_free(bundle);
    return asset;
}

/// @brief Get the number of meshes extracted from the GLTF file.
int64_t rt_gltf_mesh_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->mesh_count : 0;
}

/// @brief Get a mesh by index from the loaded GLTF asset.
void *rt_gltf_get_mesh(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    if (index < 0 || index >= a->mesh_count)
        return NULL;
    return a->meshes[index];
}

/// @brief Get the number of materials extracted from the GLTF file.
int64_t rt_gltf_material_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->material_count : 0;
}

/// @brief Get a material by index from the loaded GLTF asset.
void *rt_gltf_get_material(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    if (index < 0 || index >= a->material_count)
        return NULL;
    return a->materials[index];
}

/// @brief Number of skeletons extracted from the loaded glTF asset.
int64_t rt_gltf_skeleton_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->skeleton_count : 0;
}

/// @brief Get a skeleton by index from the loaded glTF asset.
void *rt_gltf_get_skeleton(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    if (index < 0 || index >= a->skeleton_count)
        return NULL;
    return a->skeletons[index];
}

/// @brief Number of animation clips extracted from the loaded glTF asset.
int64_t rt_gltf_animation_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->animation_count : 0;
}

/// @brief Get an Animation3D clip by index from the loaded glTF asset.
void *rt_gltf_get_animation(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    if (index < 0 || index >= a->animation_count)
        return NULL;
    return a->animations[index];
}

/// @brief Return the number of node animations (AnimationClip-style tracks) in the glTF asset.
int64_t rt_gltf_node_animation_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->node_animation_count : 0;
}

/// @brief Get a node-animation track by index from the loaded glTF asset.
void *rt_gltf_get_node_animation(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    if (index < 0 || index >= a->node_animation_count)
        return NULL;
    return a->node_animations[index];
}

/// @brief Return the number of cameras imported from the active scene.
int64_t rt_gltf_camera_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->camera_count : 0;
}

/// @brief Borrow the i-th active-scene Camera3D imported from glTF.
void *rt_gltf_get_camera(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    if (index < 0 || index >= a->camera_count)
        return NULL;
    return a->cameras[index];
}

/// @brief Number of immutable scenes in the glTF asset.
int64_t rt_gltf_scene_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->scene_count : 0;
}

/// @brief Return the imported scene name, or an empty string for invalid indices.
rt_string rt_gltf_get_scene_name(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a || index < 0 || index >= a->scene_count || !a->scenes[index].name)
        return rt_const_cstr("");
    return rt_const_cstr(a->scenes[index].name);
}

/// @brief Borrow the root SceneNode3D for immutable scene @p index.
void *rt_gltf_get_scene_root_at(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a || index < 0 || index >= a->scene_count)
        return NULL;
    return a->scenes[index].root;
}

/// @brief Number of cameras reachable from immutable scene @p scene_index.
int64_t rt_gltf_scene_camera_count(void *obj, int64_t scene_index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a || scene_index < 0 || scene_index >= a->scene_count)
        return 0;
    return a->scenes[scene_index].camera_count;
}

/// @brief Borrow a Camera3D from immutable scene @p scene_index.
void *rt_gltf_get_scene_camera(void *obj, int64_t scene_index, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    gltf_scene_info_t *scene;
    if (!a || scene_index < 0 || scene_index >= a->scene_count)
        return NULL;
    scene = &a->scenes[scene_index];
    if (index < 0 || index >= scene->camera_count)
        return NULL;
    return scene->cameras[index];
}

/// @brief Number of nodes in the loaded glTF scene tree (0 for NULL).
int64_t rt_gltf_node_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->node_count : 0;
}

/// @brief Return the scene-root SceneNode of the loaded asset (NULL if not loaded / NULL).
void *rt_gltf_get_scene_root(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? a->scene_root : NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
