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
#include "rt_gltf_json.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_textureasset3d.h"
#include "rt_vec3.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
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
extern void rt_material3d_set_custom_param(void *obj, int64_t index, double value);

/// @brief Cast a JSON double to int64 only when the conversion is defined.
static int gltf_double_to_i64_checked(double value, int64_t *out) {
    if (!out || !isfinite(value))
        return 0;
    if (value < (-9223372036854775807.0 - 1.0) || value >= 9223372036854775808.0)
        return 0;
    *out = (int64_t)value;
    return 1;
}

/// @brief Narrow an int64 JSON value to int32 with fallback on range errors.
static int32_t gltf_i32_from_i64_or(int64_t value, int32_t fallback) {
    if (value < (int64_t)INT32_MIN || value > (int64_t)INT32_MAX)
        return fallback;
    return (int32_t)value;
}

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
    int32_t mesh_capacity;
    void **materials;
    int32_t material_count;
    int32_t material_capacity;
    void **skeletons;
    int32_t skeleton_count;
    int32_t skeleton_capacity;
    void **animations;
    int32_t animation_count;
    int32_t animation_capacity;
    void **node_animations;
    int32_t node_animation_count;
    int32_t node_animation_capacity;
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

/// @brief Number of skin joints safe to read (clamped to VGFX3D_MAX_BONES); 0 when any backing
///   array is absent.
static int32_t gltf_skin_safe_joint_count(const gltf_skin_t *skin) {
    if (!skin || skin->joint_count <= 0 || !skin->joint_nodes || !skin->joint_to_bone)
        return 0;
    return skin->joint_count < VGFX3D_MAX_BONES ? skin->joint_count : VGFX3D_MAX_BONES;
}

/// @brief Clamp a (count, capacity) pair to a safe element count (0 when invalid, else min).
static int32_t gltf_asset_safe_count(void **items, int32_t count, int32_t capacity) {
    if (!items || count <= 0 || capacity <= 0)
        return 0;
    if (count > capacity)
        return capacity;
    return count;
}

/// @brief Number of imported scenes safe to read (live count clamped to capacity).
static int32_t gltf_asset_safe_scene_count(const rt_gltf_asset *asset) {
    if (!asset || !asset->scenes || asset->scene_count <= 0 || asset->scene_capacity <= 0)
        return 0;
    if (asset->scene_count > asset->scene_capacity)
        return asset->scene_capacity;
    return asset->scene_count;
}

/// @brief Release every reference in a glTF ref array (over its safe count) and free the
///   backing storage, resetting the count/capacity to zero.
static void gltf_asset_release_ref_array(void ***items, int32_t *count, int32_t *capacity) {
    void **array = items ? *items : NULL;
    int32_t safe_count =
        gltf_asset_safe_count(array, count ? *count : 0, capacity ? *capacity : 0);
    if (array) {
        for (int32_t i = 0; i < safe_count; i++) {
            if (array[i] && rt_obj_release_check0(array[i]))
                rt_obj_free(array[i]);
            array[i] = NULL;
        }
        free(array);
    }
    if (items)
        *items = NULL;
    if (count)
        *count = 0;
    if (capacity)
        *capacity = 0;
}

/// @brief GC finalizer for an `rt_gltf_asset` — release every owned mesh / material / scene root.
static void gltf_asset_finalize(void *obj) {
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (!a)
        return;
    gltf_asset_release_ref_array(&a->meshes, &a->mesh_count, &a->mesh_capacity);
    gltf_asset_release_ref_array(&a->materials, &a->material_count, &a->material_capacity);
    gltf_asset_release_ref_array(&a->skeletons, &a->skeleton_count, &a->skeleton_capacity);
    gltf_asset_release_ref_array(&a->animations, &a->animation_count, &a->animation_capacity);
    gltf_asset_release_ref_array(
        &a->node_animations, &a->node_animation_count, &a->node_animation_capacity);
    gltf_asset_release_ref_array(&a->cameras, &a->camera_count, &a->camera_capacity);
    if (a->scenes) {
        int32_t scene_count = gltf_asset_safe_scene_count(a);
        for (int32_t i = 0; i < scene_count; i++) {
            gltf_scene_info_t *scene = &a->scenes[i];
            if (scene->root && rt_obj_release_check0(scene->root))
                rt_obj_free(scene->root);
            scene->root = NULL;
            free(scene->name);
            scene->name = NULL;
            gltf_asset_release_ref_array(
                &scene->cameras, &scene->camera_count, &scene->camera_capacity);
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

/// @brief Write an identity transform into the given outputs: zero position, identity
///   quaternion (0,0,0,1), unit scale. NULL outputs are skipped.
static void gltf_write_identity_trs(double *pos, double *quat, double *scale) {
    if (pos) {
        pos[0] = 0.0;
        pos[1] = 0.0;
        pos[2] = 0.0;
    }
    if (quat) {
        quat[0] = 0.0;
        quat[1] = 0.0;
        quat[2] = 0.0;
        quat[3] = 1.0;
    }
    if (scale) {
        scale[0] = 1.0;
        scale[1] = 1.0;
        scale[2] = 1.0;
    }
}

/// @brief Square root guarded against non-finite or negative input (returns 0.0 for those).
static double gltf_sqrt_nonnegative(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0;
    return sqrt(value);
}

/// @brief Dot product of two 3-component double vectors.
static double gltf_vec3_dot_local(const double *a, const double *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/// @brief Normalize a 3-vector in place.
/// @return 1 on success; 0 (leaving @p v unchanged) for NULL, non-finite, or near-zero length.
static int gltf_vec3_normalize_local(double *v) {
    double len;
    if (!v)
        return 0;
    if (!isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2]))
        return 0;
    len = sqrt(gltf_vec3_dot_local(v, v));
    if (!isfinite(len) || len <= 1e-12)
        return 0;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return 1;
}

/// @brief Cross product out = a × b of two 3-component double vectors.
static void gltf_vec3_cross_local(const double *a, const double *b, double *out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

/// @brief Gram-Schmidt orthonormalize three basis columns in place, substituting canonical
///   axes for any degenerate (non-normalizable) column so the result is always orthonormal.
static void gltf_orthonormalize_columns(double *c0, double *c1, double *c2) {
    double dot01;
    if (!gltf_vec3_normalize_local(c0)) {
        c0[0] = 1.0;
        c0[1] = 0.0;
        c0[2] = 0.0;
    }
    dot01 = gltf_vec3_dot_local(c0, c1);
    c1[0] -= dot01 * c0[0];
    c1[1] -= dot01 * c0[1];
    c1[2] -= dot01 * c0[2];
    if (!gltf_vec3_normalize_local(c1)) {
        if (fabs(c0[0]) < 0.9) {
            c1[0] = 1.0;
            c1[1] = 0.0;
            c1[2] = 0.0;
        } else {
            c1[0] = 0.0;
            c1[1] = 1.0;
            c1[2] = 0.0;
        }
        dot01 = gltf_vec3_dot_local(c0, c1);
        c1[0] -= dot01 * c0[0];
        c1[1] -= dot01 * c0[1];
        c1[2] -= dot01 * c0[2];
        if (!gltf_vec3_normalize_local(c1)) {
            c1[0] = 0.0;
            c1[1] = 1.0;
            c1[2] = 0.0;
        }
    }
    gltf_vec3_cross_local(c0, c1, c2);
    if (!gltf_vec3_normalize_local(c2)) {
        c2[0] = 0.0;
        c2[1] = 0.0;
        c2[2] = 1.0;
    }
}

/// @brief Decompose a row-major 4x4 transform matrix into separate position, quaternion, and scale.
///
/// glTF nodes can store either a 16-element matrix or explicit
/// TRS — this helper converts the matrix form so the runtime
/// always works with TRS internally. Uses Shepperd's method
/// (largest-trace pivot) for the rotation extraction. Sheared authoring matrices
/// are reduced to the closest orthonormal rotation basis by Gram-Schmidt so
/// unsupported shear does not appear as a spurious node rotation.
static void gltf_matrix_to_trs(const double *m, double *pos, double *quat, double *scale) {
    double r00, r01, r02;
    double r10, r11, r12;
    double r20, r21, r22;
    double trace, s;
    double det;
    int flip_axis;
    if (!m || !pos || !quat || !scale)
        return;
    for (int i = 0; i < 16; i++) {
        if (!isfinite(m[i])) {
            gltf_write_identity_trs(pos, quat, scale);
            return;
        }
    }

    pos[0] = m[3];
    pos[1] = m[7];
    pos[2] = m[11];

    scale[0] = gltf_sqrt_nonnegative(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    scale[1] = gltf_sqrt_nonnegative(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    scale[2] = gltf_sqrt_nonnegative(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    if (!isfinite(scale[0]) || scale[0] <= 1e-12)
        scale[0] = 1.0;
    if (!isfinite(scale[1]) || scale[1] <= 1e-12)
        scale[1] = 1.0;
    if (!isfinite(scale[2]) || scale[2] <= 1e-12)
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
    {
        double c0[3] = {r00, r10, r20};
        double c1[3] = {r01, r11, r21};
        double c2[3] = {r02, r12, r22};
        gltf_orthonormalize_columns(c0, c1, c2);
        r00 = c0[0];
        r10 = c0[1];
        r20 = c0[2];
        r01 = c1[0];
        r11 = c1[1];
        r21 = c1[2];
        r02 = c2[0];
        r12 = c2[1];
        r22 = c2[2];
    }

    trace = r00 + r11 + r22;
    if (trace > 0.0) {
        s = gltf_sqrt_nonnegative(trace + 1.0) * 2.0;
        if (s <= 1e-12) {
            quat[0] = quat[1] = quat[2] = 0.0;
            quat[3] = 1.0;
            return;
        }
        quat[3] = 0.25 * s;
        quat[0] = (r21 - r12) / s;
        quat[1] = (r02 - r20) / s;
        quat[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        s = gltf_sqrt_nonnegative(1.0 + r00 - r11 - r22) * 2.0;
        if (s <= 1e-12) {
            quat[0] = quat[1] = quat[2] = 0.0;
            quat[3] = 1.0;
            return;
        }
        quat[3] = (r21 - r12) / s;
        quat[0] = 0.25 * s;
        quat[1] = (r01 + r10) / s;
        quat[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        s = gltf_sqrt_nonnegative(1.0 + r11 - r00 - r22) * 2.0;
        if (s <= 1e-12) {
            quat[0] = quat[1] = quat[2] = 0.0;
            quat[3] = 1.0;
            return;
        }
        quat[3] = (r02 - r20) / s;
        quat[0] = (r01 + r10) / s;
        quat[1] = 0.25 * s;
        quat[2] = (r12 + r21) / s;
    } else {
        s = gltf_sqrt_nonnegative(1.0 + r22 - r00 - r11) * 2.0;
        if (s <= 1e-12) {
            quat[0] = quat[1] = quat[2] = 0.0;
            quat[3] = 1.0;
            return;
        }
        quat[3] = (r10 - r01) / s;
        quat[0] = (r02 + r20) / s;
        quat[1] = (r12 + r21) / s;
        quat[2] = 0.25 * s;
    }
    {
        double qlen =
            sqrt(quat[0] * quat[0] + quat[1] * quat[1] + quat[2] * quat[2] + quat[3] * quat[3]);
        if (!isfinite(qlen) || qlen <= 1e-12) {
            quat[0] = quat[1] = quat[2] = 0.0;
            quat[3] = 1.0;
        } else {
            quat[0] /= qlen;
            quat[1] /= qlen;
            quat[2] /= qlen;
            quat[3] /= qlen;
        }
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
    double qlen = sqrt(x * x + y * y + z * z + w * w);
    if (!isfinite(qlen) || qlen <= 1e-12) {
        x = y = z = 0.0;
        w = 1.0;
    } else {
        x /= qlen;
        y /= qlen;
        z /= qlen;
        w /= qlen;
    }
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
        case 1: {
            double value = rt_unbox_f64(v);
            return isfinite(value) ? value : def;
        }
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
        case 1: {
            int64_t coerced;
            return gltf_double_to_i64_checked(rt_unbox_f64(v), &coerced) ? coerced : def;
        }
        case 2:
            return rt_unbox_i1(v);
        default:
            return def;
    }
}

/// @brief Read `obj[key]` as a borrowed C string (NULL if absent or non-string).
static const char *jstr(void *obj, const char *key) {
    void *v = jget(obj, key);
    if (!rt_string_is_handle(v))
        return NULL;
    return rt_string_cstr((rt_string)v);
}

/// @brief Read `obj[key]` as an array (alias for `jget` — typed for readability).
static void *jarr(void *obj, const char *key) {
    return jget(obj, key);
}

/// @brief Length of a JSON array (0 for NULL).
static int64_t jarr_len(void *arr) {
    return arr ? rt_seq_len(arr) : 0;
}

/// @brief Length of a JSON array as int32, rejecting counts that cannot be safely indexed.
static int gltf_jarr_len_i32(void *arr, int *out_len) {
    int64_t len;
    if (!out_len)
        return 0;
    len = jarr_len(arr);
    if (len < 0 || len > INT32_MAX)
        return 0;
    *out_len = (int)len;
    return 1;
}

/// @brief Coerce a boxed JSON value to double with default fallback.
static double jvalue_num(void *value, double def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return (double)rt_unbox_i64(value);
        case 1: {
            double number = rt_unbox_f64(value);
            return isfinite(number) ? number : def;
        }
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
        case 1: {
            int64_t coerced;
            return gltf_double_to_i64_checked(rt_unbox_f64(value), &coerced) ? coerced : def;
        }
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
                grown = (const rt_scene_node3d **)realloc(stack,
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
           strcmp(name, "KHR_materials_unlit") == 0 ||
           strcmp(name, "KHR_materials_specular") == 0 ||
           strcmp(name, "KHR_lights_punctual") == 0;
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
        const char *ext = rt_string_is_handle(name) ? rt_string_cstr(name) : NULL;
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
    out->texcoord = gltf_i32_from_i64_or(jint(texture_info, "texCoord", 0), 0);
    if (out->texcoord < 0)
        out->texcoord = 0;
    extensions = jget(texture_info, "extensions");
    transform = extensions ? jget(extensions, "KHR_texture_transform") : NULL;
    if (!transform)
        return;
    out->has_transform = 1;
    out->texcoord = gltf_i32_from_i64_or(jint(transform, "texCoord", out->texcoord), out->texcoord);
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
            grown = (gltf_node_visit_item *)realloc(stack, (size_t)next_capacity * sizeof(*stack));
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
                                                            (size_t)next_capacity * sizeof(*stack));
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

/// @brief True if a texture is a supported format but its decoded image is still missing.
/// @details Flags an in-range, supported-format texture whose image slot is NULL, i.e. one the
///          caller still needs to decode/stage before the material can reference it.
static int gltf_texture_index_missing_supported_payload(int64_t texture_index,
                                                        int32_t texture_count,
                                                        void **texture_images,
                                                        const uint8_t *texture_supported) {
    return texture_index >= 0 && texture_index < texture_count && texture_supported &&
           texture_supported[texture_index] && (!texture_images || !texture_images[texture_index]);
}

/// @brief Resolve the image source for a parsed glTF texture, including KHR_texture_basisu.
static int64_t gltf_texture_source_index(void *texture_json) {
    void *extensions;
    void *basisu;
    int64_t source_idx = jint(texture_json, "source", -1);
    extensions = jget(texture_json, "extensions");
    basisu = extensions ? jget(extensions, "KHR_texture_basisu") : NULL;
    int64_t basisu_source_idx = jint(basisu, "source", -1);
    if (basisu_source_idx >= 0)
        return basisu_source_idx;
    return source_idx;
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

/// @brief One staged preload dependency: an owned key (path), raw bytes and/or a decoded
///        runtime object, how many bytes have been incrementally prepared, and its kind tag.
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
static int gltf_preload_rgba_blob_pixel_bytes(const uint8_t *blob,
                                              size_t len,
                                              uint32_t *out_width,
                                              uint32_t *out_height,
                                              size_t *out_pixel_bytes);

/// @brief Compute a checked geometric capacity for preload staging arrays.
static int gltf_preload_next_capacity_size(
    size_t current, size_t required, size_t initial, size_t elem_size, size_t *out_capacity) {
    size_t next;
    if (out_capacity)
        *out_capacity = current;
    if (!out_capacity || elem_size == 0u || required == 0u)
        return 0;
    if (current >= required)
        return 1;
    next = current > 0u ? current : initial;
    if (next == 0u)
        next = 1u;
    while (next < required) {
        size_t prev = next;
        if (next > SIZE_MAX / 2u)
            next = required;
        else
            next *= 2u;
        if (next <= prev && next < required)
            return 0;
    }
    if (next > SIZE_MAX / elem_size)
        return 0;
    *out_capacity = next;
    return 1;
}

/// @brief Int-capacity wrapper for JSON-indexed preload ref tables.
static int gltf_preload_next_capacity_int(
    int current, int required, int initial, size_t elem_size, int *out_capacity) {
    size_t next = 0u;
    if (out_capacity)
        *out_capacity = current;
    if (!out_capacity || current < 0 || required <= 0)
        return 0;
    if (current >= required)
        return 1;
    if (!gltf_preload_next_capacity_size(
            (size_t)current, (size_t)required, (size_t)initial, elem_size, &next) ||
        next > (size_t)INT_MAX)
        return 0;
    *out_capacity = (int)next;
    return 1;
}

/// @brief Duplicate a NUL-terminated string into a malloc'd copy (NULL in -> NULL out).
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

/// @brief Release a GC reference held in @p *slot if this is its last drop, then NULL it.
static void gltf_preload_release_object(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Free one dependency's owned path, raw bytes, and decoded object, then zero it.
static void gltf_preload_dependency_clear(gltf_preload_dependency_t *dep) {
    if (!dep)
        return;
    free(dep->path);
    free(dep->data);
    gltf_preload_release_object(&dep->object);
    memset(dep, 0, sizeof(*dep));
}

/// @brief Free a preload bundle: its root JSON bytes, every staged dependency, and the bundle.
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

/// @brief Number of staged dependencies in the bundle (0 if NULL).
size_t rt_gltf_preload_bundle_dependency_count(const rt_gltf_preload_bundle *bundle) {
    return bundle ? bundle->dependency_count : 0u;
}

/// @brief Count staged dependencies that are decoded RGBA images.
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

/// @brief Total still-unprepared RGBA pixel bytes across decoded-image dependencies.
/// @details Excludes the 12-byte "VGRA" header and any bytes already prepared via the slice
///          API; saturates at SIZE_MAX. Lets a caller budget remaining main-thread upload work.
size_t rt_gltf_preload_bundle_decoded_image_bytes(const rt_gltf_preload_bundle *bundle) {
    size_t bytes = 0;
    if (!bundle)
        return 0u;
    for (size_t i = 0; i < bundle->dependency_count; ++i) {
        if (bundle->dependencies[i].kind == GLTF_PRELOAD_DEP_IMAGE_RGBA) {
            size_t len = 0u;
            size_t prepared = bundle->dependencies[i].prepared;
            if (!gltf_preload_rgba_blob_pixel_bytes(
                    bundle->dependencies[i].data, bundle->dependencies[i].len, NULL, NULL, &len))
                continue;
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

/// @brief Validate a "VGRA" blob header and report its width, height, and pixel byte count.
/// @return 1 with the out-params set, or 0 on a malformed/too-short blob (out-params zeroed).
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
        !gltf_checked_mul_size(pixel_count, 4u, &pixel_bytes) || pixel_bytes > len - 12u)
        return 0;
    if (out_width)
        *out_width = width;
    if (out_height)
        *out_height = height;
    if (out_pixel_bytes)
        *out_pixel_bytes = pixel_bytes;
    return 1;
}

/// @brief Size (<= @p max_bytes) of the next decoded-image slice that would be prepared.
/// @details Returns a 4-byte-aligned (whole-pixel) byte count for the first decoded-RGBA
///          dependency with work remaining, letting the caller pace upload without mutating
///          state. Returns 0 when nothing is pending or a blob is malformed.
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

/// @brief Incrementally upload up to @p max_bytes of the next decoded image into its Pixels object.
/// @details Lazily allocates the destination Pixels on first slice, copies a whole-pixel-aligned
///          run of RGBA texels, and advances the prepared cursor. When a blob finishes it marks the
///          surface dirty, frees the staging bytes, and retags the dependency as IMAGE_PIXELS. This
///          spreads large texture uploads across frames to avoid a single main-thread stall.
/// @return Bytes prepared this call (0 when nothing remains or allocation fails).
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
        if (!gltf_preload_rgba_blob_pixel_bytes(dep->data, dep->len, &width, &height, &pixel_bytes))
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
            pixels->data[first_pixel + pi] = ((uint32_t)sp[0] << 24) | ((uint32_t)sp[1] << 16) |
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

/// @brief Count staged dependencies that are packed mesh POD blobs.
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

/// @brief Append a dependency (taking ownership of @p data) under key @p path, growing the array.
/// @details Doubles capacity as needed and copies the key string. On any failure the caller
///          retains ownership of @p data (it is not stored).
/// @return 1 on success, 0 on invalid args or allocation failure.
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
        size_t next_capacity;
        if (!gltf_preload_next_capacity_size(bundle->dependency_capacity,
                                             bundle->dependency_count + 1u,
                                             8u,
                                             sizeof(*bundle->dependencies),
                                             &next_capacity))
            return 0;
        grown = (gltf_preload_dependency_t *)realloc(bundle->dependencies,
                                                     next_capacity * sizeof(*bundle->dependencies));
        if (!grown)
            return 0;
        memset(grown + bundle->dependency_capacity,
               0,
               (next_capacity - bundle->dependency_capacity) * sizeof(*grown));
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

/// @brief Find a dependency by (kind, path), remove it, and hand its raw bytes to the caller.
/// @details Transfers ownership of the returned buffer (caller frees) and compacts the array.
/// @return The dependency's data with @p out_len set, or NULL if no match.
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
            memset(
                &bundle->dependencies[bundle->dependency_count], 0, sizeof(*bundle->dependencies));
            return data;
        }
    }
    return NULL;
}

/// @brief Find a dependency by (kind, path), remove it, and hand its decoded object to the caller.
/// @details Frees the dependency's raw bytes and key, compacts the array, and transfers ownership
///          of the returned runtime object. Returns NULL if no match.
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
            memset(
                &bundle->dependencies[bundle->dependency_count], 0, sizeof(*bundle->dependencies));
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

/// @brief Read a little-endian uint16 from @p p.
static uint16_t gltf_read_u16_le(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/// @brief Read a little-endian int32 from @p p (two's-complement reinterpretation of the u32).
static int32_t gltf_read_i32_le(const uint8_t *p) {
    return (int32_t)gltf_read_u32_le(p);
}

/// @brief Write @p v as a little-endian uint32 into @p p.
static void gltf_write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/// @brief Read a little-endian IEEE-754 float32 from @p p (bit-copied, no aliasing).
static float gltf_read_f32_le(const uint8_t *p) {
    uint32_t bits = gltf_read_u32_le(p);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/// @brief Write @p value as a little-endian IEEE-754 float32 into @p p.
static void gltf_write_f32_le(uint8_t *p, float value) {
    uint32_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    gltf_write_u32_le(p, bits);
}

/// @brief Case-insensitive substring search: is @p needle present anywhere in @p text?
/// @details An empty needle matches; used to spot a MIME token within a longer type string.
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

/// @brief Whether @p mime_or_name denotes a BMP image (by MIME token or .bmp extension).
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

/// @brief Whether @p mime_or_name denotes a PNG image (by MIME token or .png extension).
static int gltf_preload_image_is_png(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/png"))
        return 1;
    len = strlen(mime_or_name);
    return len >= 4u && gltf_ascii_ieq_n(mime_or_name + len - 4u, ".png", 4u);
}

/// @brief Whether @p mime_or_name denotes a JPEG image (by MIME token or .jpg/.jpeg extension).
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

/// @brief Whether @p mime_or_name denotes a GIF image (by MIME token or .gif extension).
static int gltf_preload_image_is_gif(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/gif"))
        return 1;
    len = strlen(mime_or_name);
    return len >= 4u && gltf_ascii_ieq_n(mime_or_name + len - 4u, ".gif", 4u);
}

/// @brief Whether the image format is one the preloader can decode (PNG/BMP/JPEG/GIF).
static int gltf_preload_image_is_supported_format(const char *mime_or_name) {
    return gltf_preload_image_is_png(mime_or_name) || gltf_preload_image_is_bmp(mime_or_name) ||
           gltf_preload_image_is_jpeg(mime_or_name) || gltf_preload_image_is_gif(mime_or_name);
}

/// @brief Whether @p mime_or_name denotes a KTX2 texture asset.
static int gltf_image_is_ktx2(const char *mime_or_name) {
    size_t len;
    if (!mime_or_name)
        return 0;
    if (gltf_ascii_has_token_i(mime_or_name, "image/ktx2") ||
        gltf_ascii_has_token_i(mime_or_name, "model/ktx2") ||
        gltf_ascii_has_token_i(mime_or_name, "application/ktx2"))
        return 1;
    len = strlen(mime_or_name);
    return len >= 5u && gltf_ascii_ieq_n(mime_or_name + len - 5u, ".ktx2", 5u);
}

/// @brief Decode KTX2 payloads under trap recovery so malformed textures fail the import cleanly.
static void *gltf_decode_ktx2_payload(const uint8_t *data, size_t data_len) {
    void *asset = NULL;
    jmp_buf recovery;
    if (!data || data_len == 0)
        return NULL;
    rt_trap_set_recovery(&recovery);
    if (setjmp(recovery) == 0)
        asset = rt_textureasset3d_load_ktx2_memory(data, (uint64_t)data_len);
    rt_trap_clear_recovery();
    return asset;
}

/// @brief Decode an uncompressed 24/32-bpp BMP into an owned "VGRA" RGBA blob.
/// @details Parses the BMP/DIB headers, handles bottom-up vs. top-down (negative height) row
///          order and the 4-byte row-stride padding, and swizzles BGR(A) to RGBA. Every size
///          computation is overflow-checked. The output blob is `V`,`G`,`R`,`A` + LE width +
///          LE height + tightly-packed RGBA8 rows (caller frees @p out_blob).
/// @return 1 on success, 0 on a malformed/unsupported BMP or allocation failure.
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

/// @brief Pack a 0xRRGGBBAA pixel array into an owned "VGRA" RGBA blob (overflow-checked).
/// @details Shared sink for the PNG/JPEG/GIF decoders, which all emit packed 32-bit pixels.
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

/// @brief Decode an encoded image payload (PNG/BMP/JPEG/GIF) into an owned "VGRA" RGBA blob.
/// @details Dispatches on @p mime_or_name to the matching codec; PNG/JPEG/GIF go through the
///          shared rt_*_decode helpers then gltf_rgba32_to_rgba_blob, BMP uses the inline decoder.
/// @return 1 on success, 0 for an unsupported format or any decode failure.
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

/// @brief Build a runtime Pixels object from a "VGRA" RGBA blob (inverse of the blob encoders).
/// @details Validates the magic and header, bounds-checks the payload, then packs the RGBA
///          bytes into the Pixels 0xRRGGBBAA words and marks the surface dirty.
/// @return New Pixels handle, or NULL on a malformed blob or allocation failure.
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
        !gltf_checked_mul_size(pixel_count, 4u, &pixel_bytes) || pixel_bytes > len - 12u)
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

/// @brief Sanitize a decoded glTF resource URI into a safe relative path in @p out.
/// @details Path-traversal guard for external buffers/images: rejects absolute paths,
///   Windows drive letters (`X:`), URI scheme separators, forbids any `..` segment,
///   and collapses `.` and redundant separators, emitting forward-slash-joined segments.
/// @return Non-zero if a non-empty safe path was written; 0 (with out[0]='\0') if the URI
///   is empty, unsafe, or would overflow @p out_cap.
static int gltf_normalize_relative_uri(const char *decoded_uri, char *out, size_t out_cap) {
    const char *p;
    size_t out_len = 0;
    int wrote_segment = 0;
    if (!out || out_cap == 0)
        return 0;
    out[0] = '\0';
    if (!decoded_uri || decoded_uri[0] == '\0')
        return 0;
    if (decoded_uri[0] == '/' || decoded_uri[0] == '\\')
        return 0;
    if (strchr(decoded_uri, ':') || strstr(decoded_uri, "://"))
        return 0;

    p = decoded_uri;
    while (*p) {
        const char *seg;
        size_t seg_len;
        while (*p == '/' || *p == '\\')
            p++;
        seg = p;
        while (*p && *p != '/' && *p != '\\')
            p++;
        seg_len = (size_t)(p - seg);
        if (seg_len == 0 || (seg_len == 1 && seg[0] == '.'))
            continue;
        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.')
            return 0;
        if (wrote_segment) {
            if (out_len + 1 >= out_cap)
                return 0;
            out[out_len++] = '/';
        }
        if (seg_len >= out_cap || out_len > out_cap - 1 - seg_len)
            return 0;
        memcpy(out + out_len, seg, seg_len);
        out_len += seg_len;
        out[out_len] = '\0';
        wrote_segment = 1;
    }
    return wrote_segment;
}

static const char gltf_base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// @brief Value of a hex digit (0-15), or -1 if @p ch is not a hex digit. Used for %XX URI escapes.
static int gltf_data_uri_hex_digit(char ch) {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

/// @brief ASCII whitespace test (space, tab, CR, LF, form-feed, vertical-tab); locale-independent.
static int gltf_ascii_isspace(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '\f' || ch == '\v';
}

/// @brief Case-insensitive compare of the first @p len bytes of @p a and @p b (ASCII only).
/// @return 1 if equal ignoring ASCII case, 0 otherwise.
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

/// @brief Whether a data-URI metadata segment contains a ";base64" token.
/// @details Scans the semicolon-separated parameters between the MIME type and the comma,
///          matching the literal "base64" parameter case-insensitively.
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

/// @brief Percent-decode (%XX) a URI string into a freshly allocated byte buffer (caller frees).
/// @details Decodes %XX escapes to bytes and copies all other characters verbatim. Malformed
///          escapes reject the URI instead of leaving a literal `%` in the payload. The buffer is
///          sized to the input length (an upper bound, since decoding only shrinks).
/// @return 1 with @p out_data / @p out_len set, or 0 on bad args or allocation failure.
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
        if (src[read] == '%') {
            int hi;
            int lo;
            if (read + 2 >= len) {
                free(decoded);
                return 0;
            }
            hi = gltf_data_uri_hex_digit(src[read + 1]);
            lo = gltf_data_uri_hex_digit(src[read + 2]);
            if (hi < 0 || lo < 0) {
                free(decoded);
                return 0;
            }
            decoded[write++] = (uint8_t)((hi << 4) | lo);
            read += 2;
            continue;
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
    if (out_len)
        *out_len = 0;
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
        if ((c == -2 && d != -2) || ((c == -2 || d == -2) && i < len)) {
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
/// @return 1 on success (writes decoded bytes + MIME type), 0 on malformed URI.
static int gltf_parse_data_uri(
    const char *uri, char *mime_buf, size_t mime_buf_cap, uint8_t **out_data, size_t *out_len) {
    const char *comma;
    const char *payload;
    uint8_t *decoded_payload = NULL;
    size_t decoded_payload_len = 0;
    size_t mime_len = 0;
    int is_base64 = 0;
    if (out_data)
        *out_data = NULL;
    if (out_len)
        *out_len = 0;
    if (mime_buf && mime_buf_cap > 0)
        mime_buf[0] = '\0';
    if (!out_data || !out_len)
        return 0;
    if (!uri || strncmp(uri, "data:", 5) != 0)
        return 0;
    comma = strchr(uri, ',');
    if (!comma)
        return 0;
    payload = comma + 1;

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

    if (!gltf_percent_decode_bytes(
            payload, strlen(payload), &decoded_payload, &decoded_payload_len))
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

/// @brief Copy a data-URI's MIME type (the part between "data:" and the first ';' or ',') into
///        @p mime_buf, NUL-terminated and truncated to @p mime_buf_cap. Empties the buffer if
///        absent.
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
    static const uint8_t empty_buffer_view_byte = 0;
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
    if (byte_length == 0)
        return buffers[buf_idx].data ? buffers[buf_idx].data + byte_offset : &empty_buffer_view_byte;
    if (!buffers[buf_idx].data)
        return NULL;
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
///   error sentinel so malformed accessors are rejected instead of being treated as SCALAR.
static int gltf_component_count(const char *acc_type) {
    if (!acc_type)
        return 0;
    if (strcmp(acc_type, "SCALAR") == 0)
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
    return 0;
}

static int gltf_validate_sparse_indices(const gltf_accessor_view_t *view);

static int gltf_accessor_init_view(void *acc,
                                   gltf_accessor_view_t *out,
                                   int64_t *out_bv_idx,
                                   size_t *out_byte_offset_acc,
                                   int *out_comp_size) {
    int comp_type;
    int comp_size;
    int comp_count;
    const char *acc_type;
    int64_t count_raw;
    memset(out, 0, sizeof(*out));
    count_raw = jint(acc, "count", 0);
    if (count_raw <= 0 || count_raw > INT32_MAX)
        return 0;
    if (!gltf_nonnegative_size(acc, "byteOffset", 0, out_byte_offset_acc))
        return 0;
    comp_type = (int)jint(acc, "componentType", 5126);
    comp_size = gltf_component_size(comp_type);
    acc_type = jstr(acc, "type");
    comp_count = gltf_component_count(acc_type);
    if (comp_size <= 0 || comp_count <= 0)
        return 0;
    out->count = (int32_t)count_raw;
    out->stride = comp_size * comp_count;
    out->comp_type = comp_type;
    out->comp_count = comp_count;
    out->normalized = jint(acc, "normalized", 0) ? 1 : 0;
    *out_bv_idx = jint(acc, "bufferView", -1);
    *out_comp_size = comp_size;
    return 1;
}

static int gltf_accessor_bind_buffer_view(void *root,
                                          int64_t bv_idx,
                                          size_t byte_offset_acc,
                                          gltf_buffer_t *buffers,
                                          int buf_count,
                                          int comp_size,
                                          int comp_count,
                                          gltf_accessor_view_t *out) {
    void *views = jarr(root, "bufferViews");
    void *bv;
    int buf_idx;
    size_t byte_offset_bv;
    size_t byte_length_bv;
    size_t byte_stride;
    size_t elem_size;
    size_t offset;
    size_t view_end;
    size_t last_offset;
    size_t last_span;
    size_t required_view_len;
    size_t required_len;
    if (bv_idx < 0)
        return 1;
    if (!views || bv_idx >= jarr_len(views))
        return 0;
    bv = rt_seq_get(views, (int64_t)bv_idx);
    if (!bv)
        return 0;
    buf_idx = (int)jint(bv, "buffer", 0);
    if (!gltf_nonnegative_size(bv, "byteOffset", 0, &byte_offset_bv) ||
        !gltf_nonnegative_size(bv, "byteLength", 0, &byte_length_bv) ||
        !gltf_nonnegative_size(bv, "byteStride", 0, &byte_stride))
        return 0;
    if (!buffers || buf_count <= 0 || buf_idx < 0 || buf_idx >= buf_count)
        return 0;
    if (!gltf_checked_add_size(byte_offset_bv, byte_length_bv, &view_end) ||
        view_end > buffers[buf_idx].len)
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
    if (byte_offset_acc > byte_length_bv || offset > buffers[buf_idx].len)
        return 0;
    if (!gltf_checked_mul_size((size_t)(out->count - 1), (size_t)out->stride, &last_offset) ||
        !gltf_checked_add_size(byte_offset_acc, last_offset, &last_span) ||
        !gltf_checked_add_size(last_span, elem_size, &required_view_len))
        return 0;
    if (required_view_len > byte_length_bv)
        return 0;
    if (!gltf_checked_add_size(byte_offset_bv, required_view_len, &required_len) ||
        required_len > buffers[buf_idx].len)
        return 0;
    out->data = buffers[buf_idx].data + offset;
    return 1;
}

static int gltf_accessor_bind_sparse(void *root,
                                     void *acc,
                                     gltf_buffer_t *buffers,
                                     int buf_count,
                                     int comp_size,
                                     int comp_count,
                                     gltf_accessor_view_t *out) {
    void *sparse = jget(acc, "sparse");
    int64_t sparse_count_raw = sparse ? jint(sparse, "count", 0) : 0;
    int32_t sparse_count =
        (sparse_count_raw > 0 && sparse_count_raw <= INT32_MAX) ? (int32_t)sparse_count_raw : 0;
    if (!sparse)
        return 1;
    if (sparse_count_raw <= 0 || sparse_count_raw > INT32_MAX || sparse_count_raw > out->count)
        return 0;
    if (sparse_count <= 0)
        return 1;
    {
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
        if (!gltf_checked_mul_size((size_t)sparse_count, (size_t)index_comp_size, &index_bytes) ||
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
        return gltf_validate_sparse_indices(out);
    }
}

/// @brief Resolve a glTF accessor into a typed byte view.
static int gltf_get_accessor_view(void *root,
                                  int64_t accessor_idx,
                                  gltf_buffer_t *buffers,
                                  int buf_count,
                                  gltf_accessor_view_t *out) {
    void *accessors = jarr(root, "accessors");
    void *acc;
    int64_t bv_idx;
    size_t byte_offset_acc;
    int comp_size;
    if (!out || !accessors || accessor_idx < 0 || accessor_idx >= jarr_len(accessors))
        return 0;
    acc = rt_seq_get(accessors, accessor_idx);
    if (!acc)
        return 0;
    if (!gltf_accessor_init_view(acc, out, &bv_idx, &byte_offset_acc, &comp_size))
        return 0;
    if (!gltf_accessor_bind_buffer_view(
            root, bv_idx, byte_offset_acc, buffers, buf_count, comp_size, out->comp_count, out))
        return 0;
    return gltf_accessor_bind_sparse(root, acc, buffers, buf_count, comp_size, out->comp_count, out);
}

/// @brief Validate sparse accessor metadata globally before importers decide to skip primitives.
static int gltf_validate_sparse_accessors(void *root, gltf_buffer_t *buffers, int buf_count) {
    void *accessors = jarr(root, "accessors");
    int64_t accessor_count = jarr_len(accessors);
    for (int64_t i = 0; i < accessor_count; i++) {
        void *acc = rt_seq_get(accessors, i);
        void *sparse = jget(acc, "sparse");
        if (sparse && jint(sparse, "count", 0) > 0) {
            gltf_accessor_view_t view;
            if (!gltf_get_accessor_view(root, i, buffers, buf_count, &view))
                return 0;
        }
    }
    return 1;
}

/// @brief Read a little-endian uint16 from the first 2 bytes of @p src.
static uint16_t gltf_read_le_u16(const uint8_t *src) {
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

/// @brief Read a little-endian int16 from the first 2 bytes of @p src.
static int16_t gltf_read_le_i16(const uint8_t *src) {
    return (int16_t)gltf_read_le_u16(src);
}

/// @brief Read a little-endian uint32 from the first 4 bytes of @p src.
static uint32_t gltf_read_le_u32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

/// @brief Read a little-endian IEEE-754 float from the first 4 bytes of @p src (bit copy).
static float gltf_read_le_f32(const uint8_t *src) {
    uint32_t bits = gltf_read_le_u32(src);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
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
    if (!src)
        return 0.0f;
    switch (comp_type) {
        case 5120: {
            int8_t value = (int8_t)src[0];
            if (!normalized)
                return (float)value;
            if (value == INT8_MIN)
                return -1.0f;
            return (float)value / 127.0f;
        }
        case 5121: {
            uint8_t value = src[0];
            return normalized ? (float)value / 255.0f : (float)value;
        }
        case 5122: {
            int16_t value = gltf_read_le_i16(src);
            if (!normalized)
                return (float)value;
            if (value == INT16_MIN)
                return -1.0f;
            return (float)value / 32767.0f;
        }
        case 5123: {
            uint16_t value = gltf_read_le_u16(src);
            return normalized ? (float)value / 65535.0f : (float)value;
        }
        case 5125: {
            uint32_t value = gltf_read_le_u32(src);
            return normalized ? (float)((double)value / 4294967295.0) : (float)value;
        }
        case 5126: {
            return gltf_read_le_f32(src);
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
    if (!src)
        return 0u;
    switch (comp_type) {
        case 5120: {
            int8_t value = (int8_t)src[0];
            return value < 0 ? 0u : (uint32_t)value;
        }
        case 5121: {
            uint8_t value = src[0];
            return (uint32_t)value;
        }
        case 5122: {
            int16_t value = gltf_read_le_i16(src);
            return value < 0 ? 0u : (uint32_t)value;
        }
        case 5123: {
            uint16_t value = gltf_read_le_u16(src);
            return (uint32_t)value;
        }
        case 5125: {
            return gltf_read_le_u32(src);
        }
        case 5126: {
            float value = gltf_read_le_f32(src);
            if (!isfinite(value) || value <= 0.0f)
                return 0u;
            if (value >= (float)UINT32_MAX)
                return UINT32_MAX;
            return (uint32_t)value;
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

/// @brief Validate sparse accessor indices before sparse reads rely on binary search.
/// @details glTF requires sparse indices to be strictly increasing and in accessor range. Without
///   this guard duplicate or unsorted indices silently produce inconsistent overrides because the
///   runtime lookup performs a binary search over the sparse index table.
static int gltf_validate_sparse_indices(const gltf_accessor_view_t *view) {
    uint32_t previous = 0u;
    if (!view || !view->sparse_indices || view->sparse_count <= 0)
        return 1;
    for (int32_t i = 0; i < view->sparse_count; i++) {
        uint32_t sparse_index = gltf_decode_component_u32(
            view->sparse_indices + (size_t)i * (size_t)view->sparse_index_stride,
            view->sparse_index_comp_type);
        if (sparse_index >= (uint32_t)view->count)
            return 0;
        if (i > 0 && sparse_index <= previous)
            return 0;
        previous = sparse_index;
    }
    return 1;
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
///          negligible/non-finite (<= 1e-8) are silently dropped so garbage in the
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
    if (!out_joints || !out_weights || joint >= VGFX3D_MAX_BONES || !isfinite(weight) ||
        weight <= 1e-8f)
        return;
    for (int i = 0; i < 4; i++) {
        if (out_weights[i] > 1e-8f && out_joints[i] == joint) {
            out_weights[i] += weight;
            return;
        }
        if (replace < 0 && out_weights[i] <= 1e-8f)
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
        if (isfinite(weights[i]) && weights[i] > 0.0f)
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

/// @brief True if the accessor view has dense data or a complete sparse substitution set.
static int gltf_accessor_has_payload(const gltf_accessor_view_t *view) {
    return view &&
           (view->data || (view->sparse_count > 0 && view->sparse_indices && view->sparse_values));
}

/// @brief True if the accessor is FLOAT (comp_type 5126) with a component count in
///   [@p min_components, @p max_components].
static int gltf_accessor_is_f32_components(const gltf_accessor_view_t *view,
                                           int min_components,
                                           int max_components) {
    return view && view->comp_type == 5126 && view->comp_count >= min_components &&
           view->comp_count <= max_components;
}

/// @brief True if the accessor is a scalar unsigned index stream (u8/u16/u32 component types).
static int gltf_accessor_is_indices(const gltf_accessor_view_t *view) {
    return view && view->comp_count == 1 &&
           (view->comp_type == 5121 || view->comp_type == 5123 || view->comp_type == 5125);
}

/// @brief True if the accessor is a valid UV stream (≥2 components, FLOAT or normalized u8/u16).
static int gltf_accessor_is_texcoord(const gltf_accessor_view_t *view) {
    return view && view->comp_count >= 2 &&
           (view->comp_type == 5126 ||
            (view->normalized && (view->comp_type == 5121 || view->comp_type == 5123)));
}

/// @brief True if the accessor is a valid vertex-color stream (3-4 components, FLOAT or
///   normalized u8/u16).
static int gltf_accessor_is_color(const gltf_accessor_view_t *view) {
    return view && view->comp_count >= 3 && view->comp_count <= 4 &&
           (view->comp_type == 5126 ||
            (view->normalized && (view->comp_type == 5121 || view->comp_type == 5123)));
}

/// @brief True if the accessor is a joint-index stream (≥4 components, u8 or u16).
static int gltf_accessor_is_joints(const gltf_accessor_view_t *view) {
    return view && view->comp_count >= 4 && (view->comp_type == 5121 || view->comp_type == 5123);
}

/// @brief True if the accessor is a skin-weight stream (≥4 components, FLOAT or normalized u8/u16).
static int gltf_accessor_is_weights(const gltf_accessor_view_t *view) {
    return view && view->comp_count >= 4 &&
           (view->comp_type == 5126 ||
            (view->normalized && (view->comp_type == 5121 || view->comp_type == 5123)));
}

/// @brief True if all @p count floats are finite; false for NULL or negative count.
static int gltf_f32_array_is_finite(const float *values, int32_t count) {
    if (!values || count < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (!isfinite(values[i]))
            return 0;
    }
    return 1;
}

/// @brief Normalize a finite vec3 in place.
/// @return 1 when the vector had a useful finite length, 0 for zero/NaN/Inf input.
static int gltf_normalize_vec3f_in_place(float *v) {
    double len;
    if (!v || !isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2]))
        return 0;
    len = sqrt((double)v[0] * (double)v[0] + (double)v[1] * (double)v[1] +
               (double)v[2] * (double)v[2]);
    if (!isfinite(len) || len <= 1e-12)
        return 0;
    v[0] = (float)((double)v[0] / len);
    v[1] = (float)((double)v[1] / len);
    v[2] = (float)((double)v[2] / len);
    return 1;
}

/// @brief Normalize a glTF tangent vec4 xyz and reduce handedness to ±1.
/// @return 1 when xyz was usable; 0 when tangent space should be recomputed.
static int gltf_sanitize_tangent4(float *tangent) {
    if (!tangent || !isfinite(tangent[3]))
        return 0;
    tangent[3] = tangent[3] < 0.0f ? -1.0f : 1.0f;
    return gltf_normalize_vec3f_in_place(tangent);
}

/// @brief Triangle count yielded by a glTF primitive topology: mode 4 = TRIANGLES
///   (count/3), 5/6 = TRIANGLE_STRIP/FAN (count-2). Returns 0 for too few indices and
///   -1 for non-triangle modes (points/lines).
static int32_t gltf_primitive_triangle_count(int64_t mode, int32_t index_count) {
    if (index_count <= 0)
        return 0;
    if (mode == 4)
        return index_count / 3;
    if (mode == 5 || mode == 6)
        return index_count >= 3 ? index_count - 2 : 0;
    return -1;
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

/// @brief Unpack a primitive's preloaded "VGMT" morph-target POD blob and attach it to @p mesh_obj.
/// @details Looks up the morph dependency by mesh/primitive key, validates the header (magic,
///          version, matching vertex count) and every record/name/payload offset against the blob
///          length, then rebuilds an rt_morphtarget3d — adding each named shape with its weight and
///          per-vertex position/normal/tangent deltas (only the flagged channels). No-op if the
///          blob is missing or malformed; always frees the blob and the transient morph reference.
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
    if (!gltf_checked_mul_size(
            (size_t)shape_count, GLTF_PRELOAD_MORPH_POD_RECORD_SIZE, &record_bytes) ||
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
                const uint8_t *src =
                    blob + payload_offset + (size_t)pos_offset + (size_t)vi * 3u * sizeof(float);
                rt_morphtarget3d_set_delta(morph,
                                           shape,
                                           vi,
                                           gltf_read_f32_le(src + 0),
                                           gltf_read_f32_le(src + 4),
                                           gltf_read_f32_le(src + 8));
            }
            if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_NORMALS) != 0) {
                const uint8_t *src =
                    blob + payload_offset + (size_t)norm_offset + (size_t)vi * 3u * sizeof(float);
                rt_morphtarget3d_set_normal_delta(morph,
                                                  shape,
                                                  vi,
                                                  gltf_read_f32_le(src + 0),
                                                  gltf_read_f32_le(src + 4),
                                                  gltf_read_f32_le(src + 8));
            }
            if ((flags & GLTF_PRELOAD_MORPH_POD_HAS_TANGENTS) != 0) {
                const uint8_t *src =
                    blob + payload_offset + (size_t)tan_offset + (size_t)vi * 3u * sizeof(float);
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

/// @brief Unpack a primitive's preloaded "VGMP" mesh POD blob into a new Mesh3D.
/// @details Looks up the mesh dependency by key, validates the header and that the declared
///          vertex/index byte spans fit the blob, then installs the deserialized vertex and index
///          arrays directly into a fresh mesh's internals (a fast path that bypasses per-vertex
///          appends), attaches any morph targets, and returns the primitive's flags via @p
///          out_flags.
/// @return New Mesh3D handle, or NULL if the blob is missing or malformed.
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
        !gltf_checked_add_size(index_offset, index_bytes, &required_len) || required_len > blob_len)
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
    free(mesh->positions64);
    free(mesh->indices);
    mesh->vertices = vertices;
    mesh->positions64 = (double *)calloc((size_t)vertex_count * 3u, sizeof(double));
    if (mesh->positions64) {
        for (uint32_t vi = 0; vi < vertex_count; vi++) {
            mesh->positions64[(size_t)vi * 3u + 0] = (double)vertices[vi].pos[0];
            mesh->positions64[(size_t)vi * 3u + 1] = (double)vertices[vi].pos[1];
            mesh->positions64[(size_t)vi * 3u + 2] = (double)vertices[vi].pos[2];
        }
    }
    mesh->vertex_count = vertex_count;
    mesh->vertex_capacity = vertex_count;
    mesh->indices = indices;
    mesh->index_count = index_count;
    mesh->index_capacity = index_count;
    mesh->bone_count = (int32_t)bone_count_u32;
    mesh->build_failed = 0;
    rt_mesh3d_touch_geometry_now(mesh);
    gltf_preload_take_decoded_morph(
        preload_bundle, mesh_index, primitive_index, mesh_obj, vertex_count);
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

/// @brief Sanitize a quaternion in place: replace non-finite components (defaulting w to 1),
///   then normalize; falls back to identity (0,0,0,1) when the length is near zero.
static void gltf_normalize_quat_or_identity(double *q) {
    double len;
    if (!q)
        return;
    q[0] = gltf_finite_or(q[0], 0.0);
    q[1] = gltf_finite_or(q[1], 0.0);
    q[2] = gltf_finite_or(q[2], 0.0);
    q[3] = gltf_finite_or(q[3], 1.0);
    len = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(len) || len <= 1e-12) {
        q[0] = 0.0;
        q[1] = 0.0;
        q[2] = 0.0;
        q[3] = 1.0;
        return;
    }
    q[0] /= len;
    q[1] /= len;
    q[2] /= len;
    q[3] /= len;
}

/// @brief Sanitize a TRS triple in place: non-finite position components become 0, the
///   quaternion is normalized to identity-on-degenerate, and non-finite scale components
///   become 1. NULL outputs are skipped.
static void gltf_sanitize_trs(double *pos, double *quat, double *scale) {
    if (pos) {
        pos[0] = gltf_finite_or(pos[0], 0.0);
        pos[1] = gltf_finite_or(pos[1], 0.0);
        pos[2] = gltf_finite_or(pos[2], 0.0);
    }
    gltf_normalize_quat_or_identity(quat);
    if (scale) {
        scale[0] = gltf_finite_or(scale[0], 1.0);
        scale[1] = gltf_finite_or(scale[1], 1.0);
        scale[2] = gltf_finite_or(scale[2], 1.0);
    }
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

/// @brief Duplicate @p value, or @p fallback when @p value is NULL/empty (traps on alloc failure).
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
        memset(grown + asset->camera_capacity,
               0,
               (size_t)(next_capacity - asset->camera_capacity) * sizeof(*grown));
        asset->cameras = grown;
        asset->camera_capacity = next_capacity;
    }
    asset->cameras[asset->camera_count++] = camera;
    return 1;
}

/// @brief Append a Camera3D to one scene's camera list, growing it as needed (takes ownership).
/// @details Per-scene analogue of gltf_append_camera. Returns 1 on success (or no-op for NULL
///          args); returns 0 and traps on overflow or allocation failure.
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
        memset(grown + scene->camera_capacity,
               0,
               (size_t)(next_capacity - scene->camera_capacity) * sizeof(*grown));
        scene->cameras = grown;
        scene->camera_capacity = next_capacity;
    }
    scene->cameras[scene->camera_count++] = camera;
    return 1;
}

/// @brief Register a scene (its root node + name) on the asset, growing the scene list.
/// @details Counts the root's descendants for node_count and synthesizes a name ("default" for
///          the first scene, else "scene_N") when @p name is empty. Returns 0 and traps on overflow
///          or allocation failure; the root is borrowed (the asset references it, not copies it).
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
    snprintf(fallback,
             sizeof(fallback),
             asset->scene_count == 0 ? "default" : "scene_%d",
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

/// @brief Clone a scene node's own transform/bounds/attachments without its children.
/// @details Copies the local TRS, AABB, and bounding sphere, and shares (retains) the mesh,
///          material, light, and name handles. Marks world transform dirty. Children are not
///          copied — gltf_clone_scene_node walks the hierarchy using this as the per-node step.
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
    dst->import_index = src->import_index;
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

/// @brief One explicit-stack frame for the iterative scene-node clone: the source/dest node
///        pair and the index of the next child still to be cloned.
typedef struct {
    const rt_scene_node3d *src;
    rt_scene_node3d *dst;
    int32_t next_child;
} gltf_clone_frame_t;

/// @brief Deep-clone a scene node and its entire subtree (shallow-clone per node + shared assets).
/// @details Uses an explicit growable stack rather than recursion, so arbitrarily deep glTF
///          hierarchies cannot overflow the C call stack. On any allocation failure it releases
///          the partially-built clone and traps. Returns the cloned root (NULL on failure).
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
        if (!rt_scene_node3d_try_add_child(frame->dst, dst_child)) {
            gltf_release_local(dst_child);
            free(stack);
            gltf_release_local(root);
            return NULL;
        }
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
        double znear = gltf_clamp_double(jvalue_num(jget(persp, "znear"), 0.1), 1e-4, DBL_MAX, 0.1);
        double zfar = gltf_finite_or(jvalue_num(jget(persp, "zfar"), znear + 1000.0), znear + 1000.0);
        double yfov = gltf_clamp_double(
            jvalue_num(jget(persp, "yfov"), pi / 3.0), pi / 180.0, 179.0 * pi / 180.0, pi / 3.0);
        double aspect =
            gltf_clamp_double(jvalue_num(jget(persp, "aspectRatio"), 1.0), 1e-6, DBL_MAX, 1.0);
        if (zfar <= znear + 1e-4)
            zfar = znear + 1000.0;
        return rt_camera3d_new(yfov * (180.0 / pi), aspect, znear, zfar);
    }
    if (strcmp(type, "orthographic") == 0) {
        void *ortho = jget(camera_json, "orthographic");
        double xmag = fabs(jvalue_num(jget(ortho, "xmag"), 1.0));
        double ymag = fabs(jvalue_num(jget(ortho, "ymag"), 1.0));
        double znear = gltf_clamp_double(jvalue_num(jget(ortho, "znear"), 0.1), 1e-4, DBL_MAX, 0.1);
        double zfar = gltf_finite_or(jvalue_num(jget(ortho, "zfar"), znear + 1000.0), znear + 1000.0);
        double aspect;
        if (!isfinite(xmag) || xmag <= 1e-9)
            xmag = 1.0;
        if (!isfinite(ymag) || ymag <= 1e-9)
            ymag = 1.0;
        if (zfar <= znear + 1e-4)
            zfar = znear + 1000.0;
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
    rt_camera3d_look_at_components(
        camera, eye[0], eye[1], eye[2], target[0], target[1], target[2], up[0], up[1], up[2]);
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

/// @brief Mirror the first parsed scene onto the asset's legacy single-scene fields.
/// @details Back-compat shim: retains scene 0's root as the asset's scene_root, copies its node
///          count, and re-registers its cameras on the asset so older single-scene accessors keep
///          working. Returns 0 and releases the camera on append failure.
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
        if (has_pos && (!gltf_accessor_has_payload(&pos_view) ||
                        !gltf_accessor_is_f32_components(&pos_view, 3, 3)))
            has_pos = 0;
        if (has_norm && (!gltf_accessor_has_payload(&norm_view) ||
                         !gltf_accessor_is_f32_components(&norm_view, 3, 3)))
            has_norm = 0;
        if (has_tangent && (!gltf_accessor_has_payload(&tangent_view) ||
                            !gltf_accessor_is_f32_components(&tangent_view, 3, 4)))
            has_tangent = 0;
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
    if (limit < 0)
        return;
    if (limit > shape_count)
        limit = shape_count;
    for (int64_t i = 0; i < limit; i++)
        rt_morphtarget3d_set_weight(
            mesh->morph_targets_ref, i, jvalue_num(rt_seq_get(weights_arr, i), 0.0));
    for (int64_t i = limit; i < shape_count; i++)
        rt_morphtarget3d_set_weight(mesh->morph_targets_ref, i, 0.0);
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
///          or `?` (query), and NUL-terminates @p out. Rejects malformed escapes and any control
///          character (< 0x20, including NUL) or DEL (0x7F), whether literal or percent-decoded.
///          Does not handle scheme prefixes (file://, http://) — caller rejects those after decode.
static int gltf_decode_uri_path(const char *uri, char *out, size_t out_cap) {
    size_t oi = 0;
    if (!out || out_cap == 0)
        return 0;
    out[0] = '\0';
    if (!uri)
        return 0;
    while (*uri) {
        unsigned char decoded;
        if (*uri == '#' || *uri == '?')
            break;
        if (oi + 1 >= out_cap)
            return 0;
        if (*uri == '%') {
            int hi = gltf_hex_digit(uri[1]);
            int lo = gltf_hex_digit(uri[2]);
            if (hi >= 0 && lo >= 0) {
                decoded = (unsigned char)((hi << 4) | lo);
                if (decoded == 0 || decoded < 0x20u || decoded == 0x7Fu)
                    return 0;
                out[oi++] = (char)decoded;
                uri += 3;
                continue;
            }
            return 0;
        }
        if (*uri == '\0' || (unsigned char)*uri < 0x20u || (unsigned char)*uri == 0x7Fu)
            return 0;
        out[oi++] = *uri++;
    }
    out[oi] = '\0';
    return oi > 0;
}

/// @brief Combine a glTF document's base path with a relative URI to produce an absolute path.
/// @details Decodes the URI via `gltf_decode_uri_path`, normalizes safe `.` path segments,
///          rejects absolute paths / `..` traversal / decoded NULs / overlong output, then
///          prepends the directory component of @p base_path. Both `/` and `\` separators are
///          recognised for cross-platform support. Writes an empty string to @p out on failure.
static void gltf_resolve_relative_path(const char *base_path,
                                       const char *uri,
                                       char *out,
                                       size_t out_cap) {
    char decoded_uri[1024];
    char normalized_uri[1024];
    const char *last_sep;
    const char *last_bsep;
    size_t dir_len;
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    if (!uri)
        return;
    if (!gltf_decode_uri_path(uri, decoded_uri, sizeof(decoded_uri)))
        return;
    if (!gltf_normalize_relative_uri(decoded_uri, normalized_uri, sizeof(normalized_uri)))
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
        if (strlen(normalized_uri) >= out_cap - dir_len) {
            out[0] = '\0';
            return;
        }
        strncat(out, normalized_uri, out_cap - dir_len - 1);
    } else {
        if (strlen(normalized_uri) >= out_cap)
            return;
        strncpy(out, normalized_uri, out_cap - 1);
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

/// @brief Take a preloaded image for @p key as a Pixels object, decoding from RGBA if needed.
/// @details Prefers an already-finished IMAGE_PIXELS dependency; otherwise consumes the staged
///          IMAGE_RGBA blob and builds Pixels from it. Returns NULL if neither is present.
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
    int is_ktx2 = gltf_image_is_ktx2(resource_path);
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
        void *decoded = is_ktx2 ? gltf_decode_ktx2_payload(staged, staged_len)
                                : rt_asset_decode_typed(resource_path, staged, staged_len);
        free(staged);
        if (decoded)
            return decoded;
    }
    if (load_assets) {
        size_t data_len = 0;
        uint8_t *data = rt_asset_load_raw(rt_const_cstr(resource_path), &data_len);
        if (data) {
            void *decoded = is_ktx2 ? gltf_decode_ktx2_payload(data, data_len)
                                    : rt_asset_decode_typed(resource_path, data, data_len);
            free(data);
            if (decoded)
                return decoded;
        }
        if (gltf_is_asset_uri(resource_path))
            return NULL;
    }
    if (is_ktx2) {
        size_t data_len = 0;
        uint8_t *data = gltf_read_file_bytes(resource_path, &data_len);
        void *decoded = data ? gltf_decode_ktx2_payload(data, data_len) : NULL;
        free(data);
        return decoded;
    }
    return rt_pixels_load(rt_const_cstr(resource_path));
}

/// @brief Write a NUL-terminated error message into the caller's buffer (no-op if buffer absent).
static void gltf_preload_set_error(char *error, size_t error_cap, const char *message) {
    if (error && error_cap > 0) {
        snprintf(error, error_cap, "%s", message ? message : "failed to stage glTF preload");
    }
}

/// @brief Resolve a raw JSON texture object's source, including KHR_texture_basisu.
static int gltf_json_texture_source_index(const char *json,
                                          size_t len,
                                          size_t obj_start,
                                          size_t obj_end) {
    size_t extensions_start;
    size_t extensions_end;
    size_t basisu_start;
    size_t basisu_end;
    int source = gltf_json_object_get_int(json, len, obj_start, obj_end, "source", -1);
    if (!gltf_json_object_find_value(
            json, len, obj_start, obj_end, "extensions", &extensions_start, &extensions_end))
        return source;
    extensions_start = gltf_json_skip_ws(json, len, extensions_start);
    if (extensions_start >= extensions_end || json[extensions_start] != '{')
        return source;
    if (!gltf_json_object_find_value(json,
                                     len,
                                     extensions_start,
                                     extensions_end,
                                     "KHR_texture_basisu",
                                     &basisu_start,
                                     &basisu_end))
        return source;
    basisu_start = gltf_json_skip_ws(json, len, basisu_start);
    if (basisu_start >= basisu_end || json[basisu_start] != '{')
        return source;
    {
        int basisu_source =
            gltf_json_object_get_int(json, len, basisu_start, basisu_end, "source", -1);
        return basisu_source >= 0 ? basisu_source : source;
    }
}

/// @brief Extract the glTF JSON text from raw bytes, handling both .gltf and binary .glb.
/// @details For GLB (magic "glTF", version 2) it validates the chunk table and copies the first
///          (JSON) chunk; for plain .gltf it copies the bytes verbatim. Returns a NUL-terminated
///          owned copy (caller frees) with @p out_len set, or NULL on a malformed container.
static char *gltf_json_copy_from_root_bytes(const uint8_t *data, size_t len, size_t *out_len) {
    char *json_copy = NULL;
    if (out_len)
        *out_len = 0;
    if (!data || len == 0)
        return NULL;
    if (len > (size_t)UINT32_MAX)
        return NULL;
    if (len >= 12 && data[0] == 0x67 && data[1] == 0x6C && data[2] == 0x54 && data[3] == 0x46) {
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
    if (len > SIZE_MAX - 1u)
        return NULL;
    json_copy = (char *)malloc(len + 1u);
    if (!json_copy)
        return NULL;
    memcpy(json_copy, data, len);
    json_copy[len] = '\0';
    if (out_len)
        *out_len = len;
    return json_copy;
}

/// @brief Stage an image payload under @p key, decoding to RGBA up front when possible.
/// @details Takes ownership of @p data. If it decodes to an RGBA blob the dependency is tagged
///          IMAGE_RGBA (ready for streaming upload); otherwise the raw bytes are kept as IMAGE.
///          When @p required and a supported format fails to decode, sets @p error and fails.
/// @return 1 on success (including the no-op for empty data), 0 on a required decode/stage failure.
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

/// @brief Resolve and stage an external (non-data-URI) image referenced by @p uri.
/// @details Resolves @p uri relative to @p model_path, loads the bytes (from the asset system or
///          disk), and stages them via add_image_payload keyed by the resolved path. data: URIs and
///          unreadable optional images are skipped; an unreadable required image sets @p error.
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

/// @brief Format the dependency key for inline buffer @p index ("<gltf:inline-buffer:N>").
static void gltf_preload_buffer_key(int index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:inline-buffer:%d>", index);
}

/// @brief Format the dependency key for inline image @p index, with an extension chosen by MIME
/// type.
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

/// @brief Format the mesh-POD dependency key for a (mesh, primitive) pair.
static void gltf_preload_mesh_key(int mesh_index, int primitive_index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:mesh-pod:%d:%d>", mesh_index, primitive_index);
}

/// @brief Format the morph-POD dependency key for a (mesh, primitive) pair.
static void gltf_preload_morph_key(int mesh_index, int primitive_index, char *out, size_t out_cap) {
    if (!out || out_cap == 0)
        return;
    snprintf(out, out_cap, "<gltf:morph-pod:%d:%d>", mesh_index, primitive_index);
}

/// @brief Locate the binary (BIN) chunk inside a GLB container.
/// @details Validates the "glTF" magic/version/length and walks the chunk table (the JSON chunk
///          must come first) to find the 0x004E4942 ("BIN\0") chunk.
/// @return 1 with @p out_bin / @p out_bin_len set, 0 for a plain .gltf or a malformed/absent BIN.
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
    if (len > (size_t)UINT32_MAX)
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

/// @brief Ensure the buffer-ref array holds at least @p required_count entries, zero-filling
/// growth.
/// @return 1 on success, 0 on bad args or reallocation failure.
static int gltf_preload_grow_buffer_refs(gltf_preload_buffer_ref_t **refs,
                                         int *capacity,
                                         int required_count) {
    gltf_preload_buffer_ref_t *grown;
    int next_capacity;
    if (!refs || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    if (!gltf_preload_next_capacity_int(
            *capacity, required_count, 8, sizeof(**refs), &next_capacity))
        return 0;
    grown = (gltf_preload_buffer_ref_t *)realloc(*refs, (size_t)next_capacity * sizeof(**refs));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *refs = grown;
    *capacity = next_capacity;
    return 1;
}

static void gltf_preload_set_buffer_ref(gltf_preload_buffer_ref_t *refs,
                                        int index,
                                        const uint8_t *data,
                                        size_t len) {
    refs[index].data = data;
    refs[index].len = len;
}

static int gltf_preload_stage_inline_buffer(rt_gltf_preload_bundle *bundle,
                                            int index,
                                            const char *uri,
                                            size_t required_len,
                                            gltf_preload_buffer_ref_t *refs) {
    char key[64];
    char mime_type[64];
    uint8_t *data = NULL;
    size_t data_len = 0;
    if (!gltf_parse_data_uri(uri, mime_type, sizeof(mime_type), &data, &data_len) ||
        data_len < required_len) {
        free(data);
        return required_len == 0u;
    }
    gltf_preload_buffer_key(index, key, sizeof(key));
    if (!gltf_preload_bundle_add_dependency(bundle, key, GLTF_PRELOAD_DEP_BUFFER, data, data_len)) {
        free(data);
        return 0;
    }
    gltf_preload_set_buffer_ref(refs, index, data, data_len);
    return 1;
}

static int gltf_preload_stage_external_buffer(rt_gltf_preload_bundle *bundle,
                                              const char *model_path,
                                              int index,
                                              const char *uri,
                                              int load_assets,
                                              size_t required_len,
                                              gltf_preload_buffer_ref_t *refs) {
    char resource_path[1024];
    uint8_t *data = NULL;
    size_t data_len = 0;
    gltf_resolve_relative_path(model_path, uri, resource_path, sizeof(resource_path));
    if (resource_path[0] != '\0') {
        data = gltf_load_dependency_bytes(resource_path, load_assets, required_len, NULL, &data_len);
        if (data) {
            if (!gltf_preload_bundle_add_dependency(
                    bundle, resource_path, GLTF_PRELOAD_DEP_BUFFER, data, data_len)) {
                free(data);
                return 0;
            }
            gltf_preload_set_buffer_ref(refs, index, data, data_len);
        }
    }
    return data || required_len == 0u;
}

static int gltf_preload_stage_buffer_uri(rt_gltf_preload_bundle *bundle,
                                         const char *model_path,
                                         int index,
                                         const char *uri,
                                         int load_assets,
                                         size_t required_len,
                                         gltf_preload_buffer_ref_t *refs) {
    if (strncmp(uri, "data:", 5) == 0)
        return gltf_preload_stage_inline_buffer(bundle, index, uri, required_len, refs);
    return gltf_preload_stage_external_buffer(
        bundle, model_path, index, uri, load_assets, required_len, refs);
}

static int gltf_preload_bind_glb_buffer(int index,
                                        const uint8_t *glb_bin,
                                        size_t glb_bin_len,
                                        size_t required_len,
                                        gltf_preload_buffer_ref_t *refs) {
    if (index != 0 || !glb_bin || required_len > glb_bin_len || required_len > SIZE_MAX - 3u ||
        glb_bin_len > required_len + 3u)
        return 0;
    gltf_preload_set_buffer_ref(refs, index, glb_bin, required_len);
    return 1;
}

/// @brief Stage every glTF buffer and build the buffer-ref table the views/accessors resolve
/// against.
/// @details Walks the top-level "buffers" array: a data: URI is decoded inline, an external URI is
///          resolved/loaded, and an absent URI binds to the GLB BIN chunk. Each ref records the
///          bytes so later stages can slice them. Sets @p error and returns 0 on a staging failure.
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
    gltf_root_find_glb_bin(
        bundle ? bundle->root_data : NULL, bundle ? bundle->root_size : 0, &glb_bin, &glb_bin_len);
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
                int ok = gltf_preload_stage_buffer_uri(
                    bundle, model_path, index, uri, load_assets, required_len, refs);
                free(uri);
                if (!ok) {
                    free(refs);
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
            } else {
                if (!gltf_preload_bind_glb_buffer(index, glb_bin, glb_bin_len, required_len, refs)) {
                    free(refs);
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
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

/// @brief Ensure the buffer-view-ref array holds at least @p required_count entries (zero-filled
/// growth).
static int gltf_preload_grow_view_refs(gltf_preload_buffer_view_ref_t **views,
                                       int *capacity,
                                       int required_count) {
    gltf_preload_buffer_view_ref_t *grown;
    int next_capacity;
    if (!views || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    if (!gltf_preload_next_capacity_int(
            *capacity, required_count, 8, sizeof(**views), &next_capacity))
        return 0;
    grown =
        (gltf_preload_buffer_view_ref_t *)realloc(*views, (size_t)next_capacity * sizeof(**views));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *views = grown;
    *capacity = next_capacity;
    return 1;
}

/// @brief Parse the top-level "bufferViews" array into a ref table (buffer, offset, length,
/// stride).
/// @details A view is marked valid when it names a buffer and has a positive length. Returns the
///          table via @p out_views / @p out_count; on allocation failure sets @p error and returns
///          0.
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

/// @brief Verify one accessor's element range lies entirely within its buffer view and buffer.
/// @details Computes element size from componentType x type, derives the stride (explicit or
///          tightly-packed), and overflow-checks that the last element's end offset fits both the
///          view's byteLength and the backing buffer. This is the security gate that stops a
///          malformed accessor from inducing out-of-bounds reads during mesh packing.
/// @return 1 if valid (or count 0 / no bufferView), 0 if any bound is exceeded or types are bad.
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

/// @brief Validate every accessor in the top-level "accessors" array against its view/buffer
/// bounds.
/// @details Fails fast (setting @p error) on the first out-of-range accessor. Returns 1 if all
/// pass.
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

/// @brief Ensure the accessor-ref array holds at least @p required_count entries (zero-filled
/// growth).
static int gltf_preload_grow_accessor_refs(gltf_preload_accessor_ref_t **accessors,
                                           int *capacity,
                                           int required_count) {
    gltf_preload_accessor_ref_t *grown;
    int next_capacity;
    if (!accessors || !capacity || required_count <= 0)
        return 0;
    if (*capacity >= required_count)
        return 1;
    if (!gltf_preload_next_capacity_int(
            *capacity, required_count, 8, sizeof(**accessors), &next_capacity))
        return 0;
    grown = (gltf_preload_accessor_ref_t *)realloc(*accessors,
                                                   (size_t)next_capacity * sizeof(**accessors));
    if (!grown)
        return 0;
    memset(grown + *capacity, 0, (size_t)(next_capacity - *capacity) * sizeof(*grown));
    *accessors = grown;
    *capacity = next_capacity;
    return 1;
}

/// @brief Parse the top-level "accessors" array (including sparse substitutions) into a ref table.
/// @details Records component type/count, element count, normalization, and any sparse
///          indices/values sub-views. Marks each accessor valid only with a positive count and
///          known component size. Returns the table via @p out_accessors / @p out_count.
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

/// @brief Resolve an accessor index to a concrete, bounds-checked view over buffer memory.
/// @details Computes element size and stride, validates the dense range fits the view and buffer,
///          and — for sparse accessors — validates the index/value sub-views and that sparse
///          indices are strictly increasing and in-range. Fills @p out with raw pointers the
///          packers read.
/// @return 1 with @p out populated, or 0 if the accessor is invalid or any bound/ordering check
/// fails.
static int gltf_preload_resolve_accessor_view(const gltf_preload_accessor_ref_t *accessors,
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
        if (!view || !view->valid || view->buffer < 0 || view->buffer >= buffer_count || !buffers ||
            !buffers[view->buffer].data)
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
            (accessor->sparse_index_comp_type != 5121 && accessor->sparse_index_comp_type != 5123 &&
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
            !gltf_checked_mul_size((size_t)accessor->sparse_count, element_size, &value_bytes) ||
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

/// @brief Whether all @p count floats are finite (no NaN/Inf); false for NULL or non-positive
/// count.
static int gltf_preload_floats_are_finite(const float *values, int count) {
    if (!values || count <= 0)
        return 0;
    for (int i = 0; i < count; i++) {
        if (!isfinite(values[i]))
            return 0;
    }
    return 1;
}

/// @brief Whether three vertices form a non-degenerate triangle (positive area).
/// @details Computes the squared area from the cross product of two edges and rejects values
///          below 1e-20, dropping zero-area triangles that would produce invalid normals.
static int gltf_preload_pod_positions_form_triangle(
    const vgfx3d_vertex_t *vertices, uint32_t vertex_count, uint32_t i0, uint32_t i1, uint32_t i2) {
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

/// @brief Append a triangle's three indices if it is non-degenerate (distinct, positive-area).
/// @return 1 if emitted, 0 if the triangle was rejected (degenerate or repeated indices).
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

/// @brief Read an index from the index accessor, or use @p element_index directly for non-indexed
/// geometry.
static uint32_t gltf_preload_read_index_or_vertex(const gltf_accessor_view_t *view,
                                                  int32_t element_index) {
    uint32_t value = (uint32_t)element_index;
    if (view)
        gltf_accessor_read_u32(view, element_index, &value, 1);
    return value;
}

/// @brief Compute the triangle-list index capacity needed to triangulate a primitive topology.
/// @details mode 4 = triangles (count/3 tris), 5/6 = strip/fan (count-2 tris); each yields 3
///          indices per triangle. Overflow-checked. Returns 0 for unsupported topologies.
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
    if (!gltf_checked_mul_size(tri_count, 3u, &index_capacity) || index_capacity > UINT32_MAX) {
        return 0;
    }
    *out_capacity = (uint32_t)index_capacity;
    return 1;
}

/// @brief Write a vertex's 4 skinning joints/weights, normalizing the weights to sum to 1.
/// @details Out-of-range joints and non-positive weights are zeroed; weights are renormalized by
///          their total (or all zeroed if the total is ~0).
/// @return One past the highest referenced bone index (i.e. a bone-count hint), or 0 if none.
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
            if ((int32_t)joints[i] > max_bone_index)
                max_bone_index = (int32_t)joints[i];
        } else {
            vertex->bone_weights[i] = 0.0f;
        }
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

/// @brief Serialize a packed mesh primitive into a "VGMP" POD blob and stage it as a dependency.
/// @details Writes the 32-byte header (magic, version, vertex/index counts, flags, byte spans, bone
///          count) followed by the raw vertex and index arrays, all overflow-checked, then stores
///          it under the mesh/primitive key for gltf_preload_take_decoded_mesh to consume later.
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
        !gltf_checked_add_size(GLTF_PRELOAD_MESH_POD_HEADER_SIZE, vertex_bytes, &payload_offset) ||
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
    if (!gltf_preload_bundle_add_dependency(
            bundle, key, GLTF_PRELOAD_DEP_MESH_POD, blob, blob_len)) {
        free(blob);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

/// @brief One decoded morph shape staged for packing: its name, base weight, present-channel
///        flags, and owned per-vertex position/normal/tangent delta arrays.
typedef struct {
    char *name;
    float weight;
    uint32_t flags;
    float *pos_deltas;
    float *normal_deltas;
    float *tangent_deltas;
} gltf_preload_morph_shape_t;

/// @brief Free an array of morph shapes and every owned name/delta buffer inside it.
static void gltf_preload_free_morph_shapes(gltf_preload_morph_shape_t *shapes, size_t shape_count) {
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

/// @brief Resolve a morph target's display name, falling back to "target_N".
/// @details Reads the optional mesh `extras.targetNames[target_index]`; if absent or empty,
///          synthesizes a name from the index. Returns an owned string (caller frees).
static char *gltf_preload_target_name_alloc(
    const char *json, size_t json_len, size_t mesh_start, size_t mesh_end, int target_index) {
    size_t extras_start;
    size_t extras_end;
    size_t names_start;
    size_t names_end;
    char fallback[64];
    char *name = NULL;
    if (gltf_json_object_find_value(
            json, json_len, mesh_start, mesh_end, "extras", &extras_start, &extras_end) &&
        extras_start < extras_end && json[extras_start] == '{' &&
        gltf_json_object_find_value(
            json, json_len, extras_start, extras_end, "targetNames", &names_start, &names_end) &&
        names_start < names_end && json[names_start] == '[') {
        name =
            gltf_json_array_get_string_alloc(json, json_len, names_start, names_end, target_index);
    }
    if (name && name[0] != '\0')
        return name;
    free(name);
    snprintf(fallback, sizeof(fallback), "target_%d", target_index);
    return gltf_strdup_cstr(fallback);
}

/// @brief Read a morph target's default weight from the mesh "weights" array (0.0 if absent).
static double gltf_preload_target_weight(
    const char *json, size_t json_len, size_t mesh_start, size_t mesh_end, int target_index) {
    size_t weights_start;
    size_t weights_end;
    double weight;
    if (!gltf_json_object_find_value(
            json, json_len, mesh_start, mesh_end, "weights", &weights_start, &weights_end) ||
        weights_start >= weights_end || json[weights_start] != '[')
        return 0.0;
    weight = gltf_json_array_get_number(
        json, json_len, weights_start, weights_end, target_index, 0.0);
    if (!isfinite(weight))
        return 0.0;
    if (weight < -1.0)
        return -1.0;
    if (weight > 1.0)
        return 1.0;
    return weight;
}

/// @brief Copy one morph channel (vec3 deltas) from an accessor view into an owned float array.
/// @details Allocates vertex_count*3 floats, samples up to the accessor's count (zero-padding the
///          rest), and sanitizes non-finite components to 0. NULL view yields a NULL out (no-op).
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

/// @brief Serialize a primitive's morph shapes into a "VGMT" POD blob and stage it as a dependency.
/// @details Lays out a header, fixed-size per-shape records, a packed names region, and a deltas
///          payload (position/normal/tangent channels per the flags), all overflow-checked, then
///          stores it under the morph key for gltf_preload_take_decoded_morph to consume later.
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
        size_t name_len = shapes[i].name ? strlen(shapes[i].name) : 0u;
        if (name_len == SIZE_MAX)
            return 1;
        name_len += 1u;
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
        uint8_t *record =
            blob + GLTF_PRELOAD_MORPH_POD_HEADER_SIZE + i * GLTF_PRELOAD_MORPH_POD_RECORD_SIZE;
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

/// @brief Decode a primitive's morph targets and stage them as a morph POD dependency.
/// @details Parses the primitive's "targets" JSON array; for each entry resolves the optional
///          POSITION/NORMAL/TANGENT accessor views, copies their per-vertex deltas into a
///          growing morph-shape list (tagging present channels via GLTF_PRELOAD_MORPH_POD_HAS_*
///          and recording each target's name and morph weight), then hands the collected shapes
///          to gltf_preload_pack_morph_pod so gltf_preload_take_decoded_morph can rehydrate them
///          at load time. A primitive without a "targets" array is a successful no-op.
/// @return 1 on success or when there is nothing to stage; 0 on allocation/staging failure
///         (with @p error populated).
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
            pos_acc =
                gltf_json_object_get_int(json, json_len, target_pos, target_end, "POSITION", -1);
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
                      pos_view.comp_type == 5126 && pos_view.comp_count >= 3;
            has_norm = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          norm_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &norm_view) &&
                       norm_view.comp_type == 5126 && norm_view.comp_count >= 3;
            has_tangent = gltf_preload_resolve_accessor_view(accessors,
                                                             accessor_count,
                                                             tangent_acc,
                                                             buffers,
                                                             buffer_count,
                                                             views,
                                                             view_count,
                                                             &tangent_view) &&
                          tangent_view.comp_type == 5126 && tangent_view.comp_count >= 3;
            if (has_pos || has_norm || has_tangent) {
                if (shape_count >= SIZE_MAX / sizeof(*shapes)) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                gltf_preload_morph_shape_t *grown = (gltf_preload_morph_shape_t *)realloc(
                    shapes, (shape_count + 1u) * sizeof(*shapes));
                if (!grown) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    ok = 0;
                    goto done;
                }
                shapes = grown;
                memset(&shapes[shape_count], 0, sizeof(shapes[shape_count]));
                shapes[shape_count].name = gltf_preload_target_name_alloc(
                    json, json_len, mesh_start, mesh_end, target_index);
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

/// @brief Decode one mesh primitive into interleaved vertices + indices and stage it as a mesh POD
/// dependency.
/// @details Reads the primitive's "attributes" (POSITION required; NORMAL, TEXCOORD_0/1, COLOR_0,
///          TANGENT, JOINTS_0/1, WEIGHTS_0/1 optional) and "mode" (4=triangle list, 5=strip,
///          6=fan), resolves each accessor view, and assembles a vgfx3d_vertex_t array. Up to
///          eight bone influences (JOINTS_0 + JOINTS_1) are merged down to the top four and
///          renormalized, with out-of-range bone indices dropped. The source topology is
///          triangulated into a 32-bit index buffer, channel-presence flags are recorded, and the
///          result is packed via gltf_preload_pack_mesh_pod and then
///          gltf_preload_stage_morph_targets for any blend shapes. Non-triangle modes and
///          position-less primitives are skipped.
/// @return 1 on success or skip; 0 on allocation/staging failure (with @p error populated).
/// @brief Resolved POD-staging state for one glTF mesh primitive: every attribute's
///        accessor view + presence flag, plus derived vertex/index counts and topology.
typedef struct {
    int mode;
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
    int has_normals;
    int has_uv0;
    int has_uv1;
    int has_colors;
    int has_tangents;
    int has_joints;
    int has_weights;
    int has_joints1;
    int has_weights1;
    int has_skinning_attrs;
    int has_indices;
    int32_t vertex_count_i;
    int32_t source_index_count;
    uint32_t index_capacity_count;
} gltf_primitive_attribs_t;

/// @brief Resolve every attribute/index accessor view of a primitive into @p out.
/// @return 1 when the primitive is stageable, 0 to skip it (unsupported mode/attrs/topology).
static int gltf_preload_resolve_primitive_attribs(const char *json,
                                                  size_t json_len,
                                                  size_t prim_start,
                                                  size_t prim_end,
                                                  const gltf_preload_buffer_ref_t *buffers,
                                                  int buffer_count,
                                                  const gltf_preload_buffer_view_ref_t *views,
                                                  int view_count,
                                                  const gltf_preload_accessor_ref_t *accessors,
                                                  int accessor_count,
                                                  gltf_primitive_attribs_t *out) {
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

    memset(out, 0, sizeof(*out));
    if (!gltf_json_object_find_value(
            json, json_len, prim_start, prim_end, "attributes", &attrs_start, &attrs_end) ||
        attrs_start >= attrs_end || json[attrs_start] != '{')
        return 0;
    out->mode = gltf_json_object_get_int(json, json_len, prim_start, prim_end, "mode", 4);
    if (out->mode != 4 && out->mode != 5 && out->mode != 6)
        return 0;

    pos_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "POSITION", -1);
    norm_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "NORMAL", -1);
    if (pos_acc < 0)
        return 0;

    if (!gltf_preload_resolve_accessor_view(accessors,
                                            accessor_count,
                                            pos_acc,
                                            buffers,
                                            buffer_count,
                                            views,
                                            view_count,
                                            &out->pos_view))
        return 0;
    out->has_normals = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          norm_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->norm_view) &&
                       out->norm_view.comp_count >= 3 &&
                       out->norm_view.count >= out->pos_view.count;
    if (out->pos_view.comp_count < 3 || out->pos_view.count <= 0)
        return 0;

    uv0_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TEXCOORD_0", -1);
    uv1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TEXCOORD_1", -1);
    color_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "COLOR_0", -1);
    tangent_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "TANGENT", -1);
    joints_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "JOINTS_0", -1);
    weights_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "WEIGHTS_0", -1);
    joints1_acc = gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "JOINTS_1", -1);
    weights1_acc =
        gltf_json_object_get_int(json, json_len, attrs_start, attrs_end, "WEIGHTS_1", -1);
    idx_acc = gltf_json_object_get_int(json, json_len, prim_start, prim_end, "indices", -1);

    out->has_uv0 = gltf_preload_resolve_accessor_view(accessors,
                                                      accessor_count,
                                                      uv0_acc,
                                                      buffers,
                                                      buffer_count,
                                                      views,
                                                      view_count,
                                                      &out->uv0_view) &&
                   out->uv0_view.comp_count >= 2 && out->uv0_view.count >= out->pos_view.count;
    out->has_uv1 = gltf_preload_resolve_accessor_view(accessors,
                                                      accessor_count,
                                                      uv1_acc,
                                                      buffers,
                                                      buffer_count,
                                                      views,
                                                      view_count,
                                                      &out->uv1_view) &&
                   out->uv1_view.comp_count >= 2 && out->uv1_view.count >= out->pos_view.count;
    out->has_colors = gltf_preload_resolve_accessor_view(accessors,
                                                         accessor_count,
                                                         color_acc,
                                                         buffers,
                                                         buffer_count,
                                                         views,
                                                         view_count,
                                                         &out->color_view) &&
                      out->color_view.comp_count >= 3 &&
                      out->color_view.count >= out->pos_view.count;
    out->has_tangents = gltf_preload_resolve_accessor_view(accessors,
                                                           accessor_count,
                                                           tangent_acc,
                                                           buffers,
                                                           buffer_count,
                                                           views,
                                                           view_count,
                                                           &out->tangent_view) &&
                        out->tangent_view.comp_count >= 3 &&
                        out->tangent_view.count >= out->pos_view.count;
    out->has_joints = gltf_preload_resolve_accessor_view(accessors,
                                                         accessor_count,
                                                         joints_acc,
                                                         buffers,
                                                         buffer_count,
                                                         views,
                                                         view_count,
                                                         &out->joints_view) &&
                      out->joints_view.comp_count >= 4 &&
                      out->joints_view.count >= out->pos_view.count;
    out->has_weights = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          weights_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->weights_view) &&
                       out->weights_view.comp_count >= 4 &&
                       out->weights_view.count >= out->pos_view.count;
    out->has_joints1 = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          joints1_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->joints1_view) &&
                       out->joints1_view.comp_count >= 4 &&
                       out->joints1_view.count >= out->pos_view.count;
    out->has_weights1 = gltf_preload_resolve_accessor_view(accessors,
                                                           accessor_count,
                                                           weights1_acc,
                                                           buffers,
                                                           buffer_count,
                                                           views,
                                                           view_count,
                                                           &out->weights1_view) &&
                        out->weights1_view.comp_count >= 4 &&
                        out->weights1_view.count >= out->pos_view.count;
    out->has_skinning_attrs =
        out->has_joints || out->has_weights || out->has_joints1 || out->has_weights1;
    out->has_indices = gltf_preload_resolve_accessor_view(accessors,
                                                          accessor_count,
                                                          idx_acc,
                                                          buffers,
                                                          buffer_count,
                                                          views,
                                                          view_count,
                                                          &out->idx_view) &&
                       out->idx_view.comp_count == 1 &&
                       (out->idx_view.comp_type == 5121 || out->idx_view.comp_type == 5123 ||
                        out->idx_view.comp_type == 5125);
    out->vertex_count_i = out->pos_view.count;
    out->source_index_count = out->has_indices ? out->idx_view.count : out->vertex_count_i;
    if (out->vertex_count_i < 3 || out->source_index_count < 3)
        return 0;
    if (!gltf_preload_topology_index_capacity(
            out->mode, out->source_index_count, &out->index_capacity_count) ||
        out->index_capacity_count == 0)
        return 0;
    return 1;
}

/// @brief Decode each vertex's attributes into @p vertices, normalizing skin influences.
/// @return 1 on success, 0 to skip the primitive (non-finite source data). Sets @p out_bone_count.
static int gltf_preload_build_primitive_vertices(const gltf_primitive_attribs_t *a,
                                                 vgfx3d_vertex_t *vertices,
                                                 uint32_t *out_bone_count,
                                                 int *out_valid_normals,
                                                 int *out_valid_tangents) {
    uint32_t bone_count = 0u;
    int valid_normals = a && a->has_normals;
    int valid_tangents = a && a->has_tangents;
    if (out_valid_normals)
        *out_valid_normals = 0;
    if (out_valid_tangents)
        *out_valid_tangents = 0;
    if (!a || !vertices || !out_bone_count)
        return 0;
    for (int32_t vi = 0; vi < a->vertex_count_i; vi++) {
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
        gltf_accessor_read_f32(&a->pos_view, vi, pos, 3);
        if (a->has_normals)
            gltf_accessor_read_f32(&a->norm_view, vi, nrm, 3);
        if (!gltf_preload_floats_are_finite(pos, 3) ||
            (a->has_normals && !gltf_preload_floats_are_finite(nrm, 3)))
            return 0;
        if (a->has_normals && !gltf_normalize_vec3f_in_place(nrm)) {
            valid_normals = 0;
            nrm[0] = 0.0f;
            nrm[1] = 0.0f;
            nrm[2] = 0.0f;
        }
        if (a->has_uv0) {
            gltf_accessor_read_f32(&a->uv0_view, vi, uv, 2);
            if (!gltf_preload_floats_are_finite(uv, 2))
                return 0;
        }
        if (a->has_uv1) {
            gltf_accessor_read_f32(&a->uv1_view, vi, uv1, 2);
            if (!gltf_preload_floats_are_finite(uv1, 2))
                return 0;
        } else {
            uv1[0] = uv[0];
            uv1[1] = uv[1];
        }
        if (a->has_colors) {
            gltf_accessor_read_f32(&a->color_view, vi, color, 4);
            if (a->color_view.comp_count < 4)
                color[3] = 1.0f;
            if (!gltf_preload_floats_are_finite(color, 4))
                return 0;
        }
        if (a->has_tangents) {
            gltf_accessor_read_f32(&a->tangent_view, vi, tangent, 4);
            if (a->tangent_view.comp_count < 4)
                tangent[3] = 1.0f;
            if (!gltf_preload_floats_are_finite(tangent, 4))
                return 0;
            if (!gltf_sanitize_tangent4(tangent)) {
                valid_tangents = 0;
                tangent[0] = 1.0f;
                tangent[1] = 0.0f;
                tangent[2] = 0.0f;
                tangent[3] = 1.0f;
            }
        }
        memcpy(vertices[vi].pos, pos, sizeof(vertices[vi].pos));
        memcpy(vertices[vi].normal, nrm, sizeof(vertices[vi].normal));
        memcpy(vertices[vi].uv, uv, sizeof(vertices[vi].uv));
        memcpy(vertices[vi].uv1, uv1, sizeof(vertices[vi].uv1));
        memcpy(vertices[vi].color, color, sizeof(vertices[vi].color));
        memcpy(vertices[vi].tangent, tangent, sizeof(vertices[vi].tangent));
        if (a->has_skinning_attrs) {
            uint32_t vertex_bone_count;
            if (a->has_joints)
                gltf_accessor_read_u32(&a->joints_view, vi, joints, 4);
            if (a->has_weights)
                gltf_accessor_read_f32(&a->weights_view, vi, weights, 4);
            if (a->has_joints1)
                gltf_accessor_read_u32(&a->joints1_view, vi, joints1, 4);
            if (a->has_weights1)
                gltf_accessor_read_f32(&a->weights1_view, vi, weights1, 4);
            if (a->has_joints1 && a->has_weights1) {
                uint32_t merged_joints[4] = {0u, 0u, 0u, 0u};
                float merged_weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                if (a->has_joints && a->has_weights) {
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
    *out_bone_count = bone_count;
    if (out_valid_normals)
        *out_valid_normals = valid_normals;
    if (out_valid_tangents)
        *out_valid_tangents = valid_tangents;
    return 1;
}

/// @brief Triangulate the primitive's topology (modes 4/5/6) into @p indices.
static void gltf_preload_triangulate_primitive(const gltf_primitive_attribs_t *a,
                                               vgfx3d_vertex_t *vertices,
                                               uint32_t *indices,
                                               uint32_t *out_index_count) {
    const gltf_accessor_view_t *idx = a->has_indices ? &a->idx_view : NULL;
    uint32_t index_count = 0;
    if (a->mode == 4) {
        for (int32_t ii = 0; ii + 2 < a->source_index_count; ii += 3) {
            gltf_preload_emit_pod_triangle(vertices,
                                           (uint32_t)a->vertex_count_i,
                                           gltf_preload_read_index_or_vertex(idx, ii),
                                           gltf_preload_read_index_or_vertex(idx, ii + 1),
                                           gltf_preload_read_index_or_vertex(idx, ii + 2),
                                           indices,
                                           &index_count);
        }
    } else if (a->mode == 5) {
        for (int32_t ii = 0; ii + 2 < a->source_index_count; ii++) {
            uint32_t i0 = gltf_preload_read_index_or_vertex(idx, ii);
            uint32_t i1 = gltf_preload_read_index_or_vertex(idx, ii + 1);
            uint32_t i2 = gltf_preload_read_index_or_vertex(idx, ii + 2);
            if ((ii & 1) != 0) {
                uint32_t tmp = i0;
                i0 = i1;
                i1 = tmp;
            }
            gltf_preload_emit_pod_triangle(
                vertices, (uint32_t)a->vertex_count_i, i0, i1, i2, indices, &index_count);
        }
    } else {
        uint32_t base = gltf_preload_read_index_or_vertex(idx, 0);
        for (int32_t ii = 1; ii + 1 < a->source_index_count; ii++) {
            gltf_preload_emit_pod_triangle(vertices,
                                           (uint32_t)a->vertex_count_i,
                                           base,
                                           gltf_preload_read_index_or_vertex(idx, ii),
                                           gltf_preload_read_index_or_vertex(idx, ii + 1),
                                           indices,
                                           &index_count);
        }
    }
    *out_index_count = index_count;
}

/// @brief Stage one mesh primitive during the preload pass: resolve its vertex attributes
///   and indices from the glTF JSON and buffer views, then record the built geometry into
///   @p bundle keyed by (@p mesh_index, @p primitive_index) for later mesh assembly. Writes a
///   message into @p error (capacity @p error_cap) on a fatal failure.
/// @return Non-zero to continue staging (primitive staged or harmlessly skipped); 0 on a
///   fatal error such as an allocation failure.
static int gltf_preload_stage_mesh_primitive(rt_gltf_preload_bundle *bundle,
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
    gltf_primitive_attribs_t attribs;
    uint32_t flags = 0u;
    uint32_t bone_count = 0u;
    int valid_normals = 0;
    int valid_tangents = 0;
    vgfx3d_vertex_t *vertices = NULL;
    uint32_t *indices = NULL;
    uint32_t index_count = 0;
    size_t vertex_bytes;
    size_t index_bytes;
    int ok = 1;

    if (!bundle || !json || !accessors)
        return 1;
    if (!gltf_preload_resolve_primitive_attribs(json,
                                                json_len,
                                                prim_start,
                                                prim_end,
                                                buffers,
                                                buffer_count,
                                                views,
                                                view_count,
                                                accessors,
                                                accessor_count,
                                                &attribs))
        return 1;
    if (!gltf_checked_mul_size(
            (size_t)attribs.vertex_count_i, sizeof(vgfx3d_vertex_t), &vertex_bytes) ||
        !gltf_checked_mul_size(
            (size_t)attribs.index_capacity_count, sizeof(uint32_t), &index_bytes))
        return 1;
    if (vertex_bytes == 0 || index_bytes == 0)
        return 1;

    vertices = (vgfx3d_vertex_t *)calloc((size_t)attribs.vertex_count_i, sizeof(*vertices));
    indices = (uint32_t *)malloc(index_bytes);
    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }

    if (!gltf_preload_build_primitive_vertices(
            &attribs, vertices, &bone_count, &valid_normals, &valid_tangents))
        goto done;

    gltf_preload_triangulate_primitive(&attribs, vertices, indices, &index_count);
    if (index_count == 0)
        goto done;
    if (valid_normals)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_NORMALS;
    if (attribs.has_uv0)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_UV0;
    if (valid_tangents)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_TANGENTS;
    if (attribs.has_skinning_attrs)
        flags |= GLTF_PRELOAD_MESH_POD_HAS_SKINNING;
    ok = gltf_preload_pack_mesh_pod(bundle,
                                    mesh_index,
                                    primitive_index,
                                    flags,
                                    vertices,
                                    (uint32_t)attribs.vertex_count_i,
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
                                              attribs.vertex_count_i,
                                              error,
                                              error_cap);
    }

done:
    free(vertices);
    free(indices);
    return ok;
}

/// @brief Stage every mesh primitive as a packed POD blob (and its morph targets) off the main
/// thread.
/// @details Walks "meshes"[].primitives[], resolves each attribute/index accessor, triangulates the
///          topology, normalizes skinning, and emits "VGMP"/"VGMT" dependencies so the main-thread
///          load can build meshes without re-parsing. Sets @p error and returns 0 on a staging
///          failure.
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
                        size_t prim_end =
                            gltf_json_find_matching(json, json_len, prim_pos, '{', '}');
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

/// @brief Stage an inline image's bytes under its image-index key (decoding to RGBA when possible).
static int gltf_preload_stage_image_bytes(rt_gltf_preload_bundle *bundle,
                                          int image_index,
                                          const char *mime_type,
                                          uint8_t *data,
                                          size_t data_len,
                                          int required,
                                          char *error,
                                          size_t error_cap) {
    char key[96];
    if (!data || data_len == 0) {
        free(data);
        if (required) {
            gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
            return 0;
        }
        return 1;
    }
    gltf_preload_image_key(image_index, mime_type, key, sizeof(key));
    return gltf_preload_bundle_add_image_payload(
        bundle, key, mime_type ? mime_type : key, data, data_len, required, error, error_cap);
}

/// @brief Mark which image sources are actually referenced by a texture, in the @p required bitmap.
/// @details Scans the top-level "textures" array and sets required[source] for each texture's image
///          index, so only images a material can use are treated as mandatory to stage.
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
            source = gltf_json_texture_source_index(json, json_len, pos, object_end);
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

static int gltf_json_count_objects_in_array(const char *json,
                                            size_t json_len,
                                            size_t array_start,
                                            size_t array_end) {
    size_t pos = array_start + 1u;
    int count = 0;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            count++;
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
    return count;
}

static int gltf_preload_prepare_required_images(const char *json,
                                                size_t json_len,
                                                size_t array_start,
                                                size_t array_end,
                                                int *out_image_count,
                                                uint8_t **out_required,
                                                char *error,
                                                size_t error_cap) {
    int image_count = gltf_json_count_objects_in_array(json, json_len, array_start, array_end);
    uint8_t *required = NULL;
    if (out_image_count)
        *out_image_count = image_count;
    if (out_required)
        *out_required = NULL;
    if (image_count <= 0)
        return 1;
    required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
    if (!required) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF preload");
        return 0;
    }
    gltf_preload_mark_required_images(json, json_len, required, image_count);
    if (out_required)
        *out_required = required;
    else
        free(required);
    return 1;
}

/// @brief Verify that every texture-referenced data-URI image decodes to a supported format.
/// @details Marks required images (via gltf_preload_mark_required_images), then checks each
///          referenced inline data-URI image is a decodable format, rejecting the model otherwise.
/// @return 1 if all required inline images are valid, 0 if any is unsupported/corrupt.
static int gltf_validate_required_data_uri_images(const char *json, size_t json_len) {
    size_t array_start;
    size_t array_end;
    size_t pos;
    int image_count;
    int index = 0;
    uint8_t *required = NULL;
    if (!gltf_json_find_top_level_array(json, json_len, "images", &array_start, &array_end))
        return 1;

    image_count = gltf_json_count_objects_in_array(json, json_len, array_start, array_end);
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
            int required_image;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            uri = gltf_json_object_get_string(json, json_len, pos, object_end, "uri");
            mime_type = gltf_json_object_get_string(json, json_len, pos, object_end, "mimeType");
            required_image = index < image_count && required[index] != 0;
            if (required_image && uri && strncmp(uri, "data:", 5) == 0) {
                char parsed_mime[64];
                const char *image_type;
                uint8_t *data = NULL;
                uint8_t *rgba_blob = NULL;
                size_t data_len = 0;
                size_t rgba_len = 0;
                int ok = 1;
                gltf_data_uri_copy_mime(uri, parsed_mime, sizeof(parsed_mime));
                image_type = mime_type ? mime_type : parsed_mime;
                if (gltf_image_is_ktx2(image_type)) {
                    void *ktx_asset = NULL;
                    ok = gltf_parse_data_uri(
                        uri, parsed_mime, sizeof(parsed_mime), &data, &data_len);
                    if (ok)
                        ktx_asset = gltf_decode_ktx2_payload(data, data_len);
                    ok = ktx_asset != NULL;
                    if (ktx_asset && rt_obj_release_check0(ktx_asset))
                        rt_obj_free(ktx_asset);
                    free(data);
                } else if (gltf_preload_image_is_supported_format(image_type)) {
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

static int gltf_preload_stage_data_uri_image(rt_gltf_preload_bundle *bundle,
                                             int image_index,
                                             const char *mime_type,
                                             const char *uri,
                                             int required_image,
                                             char *error,
                                             size_t error_cap) {
    char parsed_mime[64];
    uint8_t *data = NULL;
    size_t data_len = 0;
    parsed_mime[0] = '\0';
    if (gltf_parse_data_uri(uri, parsed_mime, sizeof(parsed_mime), &data, &data_len))
        return gltf_preload_stage_image_bytes(bundle,
                                              image_index,
                                              mime_type ? mime_type : parsed_mime,
                                              data,
                                              data_len,
                                              required_image,
                                              error,
                                              error_cap);
    if (required_image && gltf_preload_image_is_supported_format(mime_type)) {
        gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
        return 0;
    }
    return 1;
}

static int gltf_preload_stage_image_uri(rt_gltf_preload_bundle *bundle,
                                        const char *model_path,
                                        int image_index,
                                        const char *mime_type,
                                        const char *uri,
                                        int load_assets,
                                        int required_image,
                                        char *error,
                                        size_t error_cap) {
    if (strncmp(uri, "data:", 5) == 0)
        return gltf_preload_stage_data_uri_image(
            bundle, image_index, mime_type, uri, required_image, error, error_cap);
    return gltf_preload_stage_external_image(
        bundle, model_path, uri, load_assets, required_image, error, error_cap);
}

static int gltf_preload_stage_buffer_view_image(rt_gltf_preload_bundle *bundle,
                                                const char *json,
                                                size_t json_len,
                                                int image_index,
                                                size_t object_start,
                                                size_t object_end,
                                                const char *mime_type,
                                                int required_image,
                                                const gltf_preload_buffer_ref_t *buffers,
                                                int buffer_count,
                                                const gltf_preload_buffer_view_ref_t *views,
                                                int view_count,
                                                char *error,
                                                size_t error_cap) {
    int view_index =
        gltf_json_object_get_int(json, json_len, object_start, object_end, "bufferView", -1);
    int staged = 0;
    if (view_index >= 0 && view_index < view_count && views && views[view_index].valid) {
        const gltf_preload_buffer_view_ref_t *view = &views[view_index];
        if (view->buffer >= 0 && view->buffer < buffer_count && buffers &&
            buffers[view->buffer].data) {
            size_t end;
            if (view->byte_length == 0 && required_image) {
                gltf_preload_set_error(error, error_cap, "invalid glTF image payload");
                return 0;
            }
            if (view->byte_length > 0 &&
                gltf_checked_add_size(view->byte_offset, view->byte_length, &end) &&
                end <= buffers[view->buffer].len) {
                uint8_t *copy = (uint8_t *)malloc(view->byte_length);
                if (!copy) {
                    gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
                    return 0;
                }
                memcpy(copy, buffers[view->buffer].data + view->byte_offset, view->byte_length);
                staged = 1;
                if (!gltf_preload_stage_image_bytes(bundle,
                                                    image_index,
                                                    mime_type,
                                                    copy,
                                                    view->byte_length,
                                                    required_image,
                                                    error,
                                                    error_cap))
                    return 0;
            }
        }
    }
    if (!staged && required_image) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF dependency");
        return 0;
    }
    return 1;
}

static int gltf_preload_stage_image_object(rt_gltf_preload_bundle *bundle,
                                           const char *model_path,
                                           const char *json,
                                           size_t json_len,
                                           int image_index,
                                           size_t object_start,
                                           size_t object_end,
                                           int load_assets,
                                           int required_image,
                                           const gltf_preload_buffer_ref_t *buffers,
                                           int buffer_count,
                                           const gltf_preload_buffer_view_ref_t *views,
                                           int view_count,
                                           char *error,
                                           size_t error_cap) {
    char *uri = gltf_json_object_get_string(json, json_len, object_start, object_end, "uri");
    char *mime_type =
        gltf_json_object_get_string(json, json_len, object_start, object_end, "mimeType");
    int ok;
    if (uri) {
        ok = gltf_preload_stage_image_uri(bundle,
                                          model_path,
                                          image_index,
                                          mime_type,
                                          uri,
                                          load_assets,
                                          required_image,
                                          error,
                                          error_cap);
    } else {
        ok = gltf_preload_stage_buffer_view_image(bundle,
                                                  json,
                                                  json_len,
                                                  image_index,
                                                  object_start,
                                                  object_end,
                                                  mime_type,
                                                  required_image,
                                                  buffers,
                                                  buffer_count,
                                                  views,
                                                  view_count,
                                                  error,
                                                  error_cap);
    }
    free(uri);
    free(mime_type);
    return ok;
}

/// @brief Stage every glTF image (inline data-URI, bufferView-embedded, or external file).
/// @details Walks the "images" array, marking texture-referenced images required, and stages each
///          image's bytes (decoding to RGBA when possible) under its image key. A required image
///          that cannot be staged sets @p error and fails; optional images are skipped on error.
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
    int image_count;
    int index = 0;
    uint8_t *required = NULL;
    if (!gltf_json_find_top_level_array(json, json_len, "images", &array_start, &array_end))
        return 1;

    if (!gltf_preload_prepare_required_images(
            json, json_len, array_start, array_end, &image_count, &required, error, error_cap))
        return 0;

    pos = array_start + 1u;
    while (pos < array_end) {
        pos = gltf_json_skip_ws(json, json_len, pos);
        if (pos >= array_end || json[pos] == ']')
            break;
        if (json[pos] == '{') {
            size_t object_end = gltf_json_find_matching(json, json_len, pos, '{', '}');
            int required_image;
            if (object_end == SIZE_MAX || object_end > array_end)
                break;
            if (index >= image_count) {
                free(required);
                gltf_preload_set_error(error, error_cap, "invalid glTF image list");
                return 0;
            }
            required_image = required ? required[index] != 0 : 0;
            if (!gltf_preload_stage_image_object(bundle,
                                                 model_path,
                                                 json,
                                                 json_len,
                                                 index,
                                                 pos,
                                                 object_end,
                                                 load_assets,
                                                 required_image,
                                                 buffers,
                                                 buffer_count,
                                                 views,
                                                 view_count,
                                                 error,
                                                 error_cap)) {
                free(required);
                return 0;
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

static rt_gltf_preload_bundle *gltf_preload_bundle_alloc_with_root(uint8_t *root_data,
                                                                   size_t root_size,
                                                                   char *error,
                                                                   size_t error_cap) {
    rt_gltf_preload_bundle *bundle = (rt_gltf_preload_bundle *)calloc(1, sizeof(*bundle));
    if (!bundle) {
        gltf_preload_set_error(error, error_cap, "failed to stage glTF preload");
        return NULL;
    }
    bundle->root_data = root_data;
    bundle->root_size = root_size;
    return bundle;
}

static int gltf_preload_stage_bundle_json(rt_gltf_preload_bundle *bundle,
                                          const char *model_path,
                                          char *json,
                                          size_t json_len,
                                          int load_assets,
                                          char *error,
                                          size_t error_cap) {
    gltf_preload_buffer_ref_t *buffer_refs = NULL;
    int buffer_ref_count = 0;
    gltf_preload_buffer_view_ref_t *view_refs = NULL;
    int view_ref_count = 0;
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
    return ok;
}

/// @brief Build an off-main-thread preload bundle from a C path and raw glTF/GLB bytes.
/// @details Extracts the JSON, validates accessors, and stages all dependencies so the subsequent
///          rt_gltf_load_preloaded_bundle call can build runtime objects on the main thread with no
///          file I/O or re-parsing. Takes ownership of @p root_data. On failure writes @p error,
///          frees partial work, and returns NULL.
/// @return A populated preload bundle (caller frees via rt_gltf_preload_bundle_free), or NULL.
rt_gltf_preload_bundle *rt_gltf_preload_bundle_create_cstr(const char *model_path,
                                                           uint8_t *root_data,
                                                           size_t root_size,
                                                           int load_assets,
                                                           char *error,
                                                           size_t error_cap) {
    rt_gltf_preload_bundle *bundle;
    char *json = NULL;
    size_t json_len = 0;
    if (error && error_cap > 0)
        error[0] = '\0';
    if (!root_data || root_size == 0 || !model_path) {
        free(root_data);
        gltf_preload_set_error(error, error_cap, "failed to load model");
        return NULL;
    }
    bundle = gltf_preload_bundle_alloc_with_root(root_data, root_size, error, error_cap);
    if (!bundle) {
        free(root_data);
        return NULL;
    }
    json = gltf_json_copy_from_root_bytes(root_data, root_size, &json_len);
    if (json) {
        int ok = gltf_preload_stage_bundle_json(
            bundle, model_path, json, json_len, load_assets, error, error_cap);
        free(json);
        if (!ok) {
            rt_gltf_preload_bundle_free(bundle);
            return NULL;
        }
    }
    return bundle;
}

/// @brief Build an off-main-thread preload bundle from a runtime string path.
/// @details Thin compatibility wrapper around `rt_gltf_preload_bundle_create_cstr`; callers that run
///          on worker threads should prefer the C-string variant with an already-snapshotted path.
rt_gltf_preload_bundle *rt_gltf_preload_bundle_create(rt_string path,
                                                      uint8_t *root_data,
                                                      size_t root_size,
                                                      int load_assets,
                                                      char *error,
                                                      size_t error_cap) {
    return rt_gltf_preload_bundle_create_cstr(path ? rt_string_cstr(path) : NULL,
                                              root_data,
                                              root_size,
                                              load_assets,
                                              error,
                                              error_cap);
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
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    if (!skin)
        return -1;
    for (int32_t i = 0; i < joint_count; i++)
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
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    if (!skin || !skin->skeleton || !nodes_arr || joint_local < 0 ||
        joint_local >= joint_count)
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
        int32_t child_node = gltf_i32_from_i64_or(jvalue_int(rt_seq_get(children, ci), -1), -1);
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

/// @brief Release Skeleton3D handles stored in skin scratch entries and clear the slots.
static void gltf_release_skin_skeletons(gltf_skin_t *skins, int32_t skin_count) {
    if (!skins)
        return;
    for (int32_t i = 0; i < skin_count; i++)
        gltf_release_ref(&skins[i].skeleton);
}

static int gltf_inverse_bind_accessor_valid(const gltf_accessor_view_t *view,
                                            int32_t joint_count);

static void gltf_parse_skins_clear_asset(rt_gltf_asset *asset) {
    if (!asset)
        return;
    asset->skeleton_count = 0;
    free(asset->skeletons);
    asset->skeletons = NULL;
    asset->skeleton_capacity = 0;
}

static void gltf_parse_skins_fail(rt_gltf_asset *asset,
                                  gltf_skin_t *skins,
                                  int32_t skin_count,
                                  int32_t *node_parents,
                                  int *hard_error) {
    if (hard_error)
        *hard_error = 1;
    gltf_release_skin_skeletons(skins, skin_count);
    gltf_parse_skins_clear_asset(asset);
    free(node_parents);
    gltf_free_skins(skins, skin_count);
}

static int32_t *gltf_build_node_parent_table(void *nodes_arr, int32_t node_count) {
    int32_t *node_parents = (int32_t *)malloc((size_t)node_count * sizeof(*node_parents));
    if (!node_parents)
        return NULL;
    for (int32_t i = 0; i < node_count; i++)
        node_parents[i] = -1;
    for (int32_t ni = 0; ni < node_count; ni++) {
        void *node_json = rt_seq_get(nodes_arr, ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int32_t child = gltf_i32_from_i64_or(jvalue_int(rt_seq_get(children, ci), -1), -1);
            if (child >= 0 && child < node_count)
                node_parents[child] = ni;
        }
    }
    return node_parents;
}

static int gltf_parse_skins_alloc_state(rt_gltf_asset *asset,
                                        void *nodes_arr,
                                        int32_t skin_count,
                                        int32_t node_count,
                                        int32_t **out_node_parents,
                                        gltf_skin_t **out_skins) {
    int32_t *node_parents = gltf_build_node_parent_table(nodes_arr, node_count);
    gltf_skin_t *skins = NULL;
    *out_node_parents = NULL;
    *out_skins = NULL;
    if (!node_parents)
        return 0;
    skins = (gltf_skin_t *)calloc((size_t)skin_count, sizeof(*skins));
    asset->skeletons = (void **)calloc((size_t)skin_count, sizeof(void *));
    asset->skeleton_count = 0;
    asset->skeleton_capacity = asset->skeletons ? skin_count : 0;
    if (!skins || !asset->skeletons) {
        free(node_parents);
        gltf_free_skins(skins, skin_count);
        free(asset->skeletons);
        asset->skeletons = NULL;
        asset->skeleton_count = 0;
        asset->skeleton_capacity = 0;
        return 0;
    }
    *out_node_parents = node_parents;
    *out_skins = skins;
    return 1;
}

static int gltf_skin_init_entry(gltf_skin_t *skin, int32_t joint_count) {
    skin->joint_nodes = (int32_t *)calloc((size_t)joint_count, sizeof(int32_t));
    skin->joint_to_bone = (int32_t *)malloc((size_t)joint_count * sizeof(int32_t));
    skin->joint_count = joint_count;
    skin->skeleton = rt_skeleton3d_new();
    if (!skin->joint_nodes || !skin->joint_to_bone || !skin->skeleton) {
        if (skin->skeleton)
            gltf_release_ref(&skin->skeleton);
        free(skin->joint_nodes);
        free(skin->joint_to_bone);
        skin->joint_nodes = NULL;
        skin->joint_to_bone = NULL;
        skin->joint_count = 0;
        return 0;
    }
    return 1;
}

static int gltf_skin_read_joint_nodes(gltf_skin_t *skin,
                                      void *joints,
                                      int32_t joint_count,
                                      int32_t node_count) {
    for (int32_t ji = 0; ji < joint_count; ji++) {
        int32_t joint_node = gltf_i32_from_i64_or(jvalue_int(rt_seq_get(joints, ji), -1), -1);
        if (joint_node < 0 || joint_node >= node_count)
            return 0;
        for (int32_t prev = 0; prev < ji; prev++) {
            if (skin->joint_nodes[prev] == joint_node)
                return 0;
        }
        skin->joint_nodes[ji] = joint_node;
        skin->joint_to_bone[ji] = -1;
    }
    return 1;
}

static void gltf_skin_register_joints(gltf_skin_t *skin,
                                      void *nodes_arr,
                                      const int32_t *node_parents,
                                      int32_t node_count) {
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    for (int32_t ji = 0; ji < joint_count; ji++) {
        int32_t parent_node =
            skin->joint_nodes[ji] >= 0 && skin->joint_nodes[ji] < node_count
                ? node_parents[skin->joint_nodes[ji]]
                : -1;
        if (gltf_skin_find_joint(skin, parent_node) < 0)
            gltf_add_skin_joint_recursive(skin, nodes_arr, ji, -1);
    }
    for (int32_t ji = 0; ji < joint_count; ji++) {
        if (skin->joint_to_bone[ji] < 0)
            gltf_add_skin_joint_recursive(skin, nodes_arr, ji, -1);
    }
}

static void gltf_skin_apply_inverse_bind(void *root,
                                         void *skin_json,
                                         gltf_buffer_t *buffers,
                                         int buf_count,
                                         gltf_skin_t *skin) {
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    int64_t ibm_acc = jint(skin_json, "inverseBindMatrices", -1);
    gltf_accessor_view_t ibm_view;
    if (!gltf_get_accessor_view(root, ibm_acc, buffers, buf_count, &ibm_view) ||
        !gltf_inverse_bind_accessor_valid(&ibm_view, joint_count))
        return;
    {
        rt_skeleton3d *skel = (rt_skeleton3d *)skin->skeleton;
        int32_t bone_count = skeleton3d_safe_bone_count(skel);
        for (int32_t ji = 0; ji < joint_count; ji++) {
            int32_t bone = skin->joint_to_bone[ji];
            float col_major[16];
            if (bone < 0 || bone >= bone_count)
                continue;
            gltf_accessor_read_f32(&ibm_view, ji, col_major, 16);
            for (int row = 0; row < 4; row++)
                for (int col = 0; col < 4; col++)
                    skel->bones[bone].inverse_bind[row * 4 + col] = col_major[col * 4 + row];
        }
    }
}

static int gltf_parse_skin_entry(rt_gltf_asset *asset,
                                 void *root,
                                 void *nodes_arr,
                                 void *skin_json,
                                 gltf_buffer_t *buffers,
                                 int buf_count,
                                 const int32_t *node_parents,
                                 int32_t node_count,
                                 gltf_skin_t *skin) {
    void *joints = jarr(skin_json, "joints");
    int64_t joint_count64 = jarr_len(joints);
    int32_t joint_count;
    if (joint_count64 <= 0)
        return 0;
    if (joint_count64 > INT32_MAX || joint_count64 > VGFX3D_MAX_BONES)
        return -1;
    joint_count = (int32_t)joint_count64;
    if (!gltf_skin_init_entry(skin, joint_count))
        return 0;
    if (!gltf_skin_read_joint_nodes(skin, joints, joint_count, node_count))
        return -1;
    gltf_skin_register_joints(skin, nodes_arr, node_parents, node_count);
    rt_skeleton3d_compute_inverse_bind(skin->skeleton);
    gltf_skin_apply_inverse_bind(root, skin_json, buffers, buf_count, skin);
    asset->skeletons[asset->skeleton_count++] = skin->skeleton;
    return 1;
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
///          On allocation failure — or a malformed skin (out-of-range or duplicate joint
///          index), which also sets *hard_error — the partial state is rolled back
///          (node_parents freed, skins freed, asset->skeletons cleared) so the asset is never
///          left in a half-built state.
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
    int64_t skin_count64 = jarr_len(skins_arr);
    int64_t node_count64 = jarr_len(nodes_arr);
    int32_t skin_count = 0;
    int32_t node_count = 0;
    int32_t *node_parents = NULL;
    gltf_skin_t *skins = NULL;
    if (hard_error)
        *hard_error = 0;
    if (out_skins)
        *out_skins = NULL;
    if (out_skin_count)
        *out_skin_count = 0;
    if (!asset || !skins_arr || skin_count64 <= 0 || skin_count64 > INT32_MAX || !nodes_arr ||
        node_count64 <= 0 || node_count64 > INT32_MAX)
        return;
    skin_count = (int32_t)skin_count64;
    node_count = (int32_t)node_count64;

    if (!gltf_parse_skins_alloc_state(
            asset, nodes_arr, skin_count, node_count, &node_parents, &skins))
        return;

    for (int32_t si = 0; si < skin_count; si++) {
        void *skin_json = rt_seq_get(skins_arr, si);
        int skin_result = gltf_parse_skin_entry(asset,
                                                root,
                                                nodes_arr,
                                                skin_json,
                                                buffers,
                                                buf_count,
                                                node_parents,
                                                node_count,
                                                &skins[si]);
        if (skin_result < 0) {
            gltf_parse_skins_fail(asset, skins, skin_count, node_parents, hard_error);
            return;
        }
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
    int32_t max_bone = -1;
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    if (!mesh || !mesh->vertices || joint_count <= 0)
        return;
    for (uint32_t vi = 0; vi < mesh->vertex_count; vi++) {
        vgfx3d_vertex_t *v = &mesh->vertices[vi];
        int64_t bones[4] = {0, 0, 0, 0};
        double weights[4] = {0.0, 0.0, 0.0, 0.0};
        double sum = 0.0;
        for (int i = 0; i < 4; i++) {
            uint8_t joint = v->bone_indices[i];
            if (joint < joint_count && skin->joint_to_bone[joint] >= 0 &&
                isfinite(v->bone_weights[i]) && v->bone_weights[i] > 0.0f) {
                bones[i] = skin->joint_to_bone[joint];
                weights[i] = v->bone_weights[i];
                sum += weights[i];
            }
        }
        if (sum <= 0.0) {
            memset(v->bone_indices, 0, sizeof(v->bone_indices));
            memset(v->bone_weights, 0, sizeof(v->bone_weights));
            continue;
        }
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
    mesh->bone_count = max_bone + 1;
    rt_mesh3d_set_skeleton(mesh, skin->skeleton);
}

/// @brief Deep-clone a mesh for per-node variant use.
/// @details `rt_mesh3d_clone` already clones attached morph targets, so this wrapper
///          deliberately avoids a second clone/reattach pass.
static void *gltf_clone_mesh_variant(void *source_mesh) {
    if (!source_mesh)
        return NULL;
    return rt_mesh3d_clone(source_mesh);
}

/// @brief Return the mesh to attach to a glTF scene node, creating a per-node variant when needed.
/// @details Returns the shared asset mesh directly when neither skinning nor morph weights require
///          per-instance state. When a skin or a weights array is present, clones the mesh via
///          `gltf_clone_mesh_variant`, applies the skin's inverse-bind matrices, and writes the
///          initial morph weights. Returns NULL on clone failure so callers can fail
///          the import rather than silently binding mutable state to a shared mesh.
static void *gltf_make_node_mesh_variant(rt_gltf_asset *asset,
                                         int32_t mesh_index,
                                         int64_t skin_ref,
                                         void *weights_arr,
                                         const gltf_skin_t *skins,
                                         int32_t skin_count) {
    int has_skin = skin_ref >= 0 && skin_ref < skin_count;
    int needs_variant = has_skin || weights_arr != NULL;
    void *source_mesh;
    void *variant;
    if (!asset || mesh_index < 0 || mesh_index >= asset->mesh_count)
        return NULL;
    if (!needs_variant)
        return asset->meshes[mesh_index];
    source_mesh = asset->meshes[mesh_index];
    variant = gltf_clone_mesh_variant(source_mesh);
    if (!variant)
        return NULL;
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

/// @brief Return non-zero if a skeletal animation curve already targets this bone/path.
/// @details glTF leaves duplicate animation channels for the same target undefined. The
///          runtime stores a single TRS curve per bone property, so importing later duplicates
///          would otherwise mix their key times while sampling only one curve.
static int gltf_anim_curve_already_targets(const gltf_anim_curve_t *curves,
                                           int32_t count,
                                           int32_t bone_idx,
                                           int32_t path) {
    if (!curves || count <= 0 || bone_idx < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (curves[i].valid && curves[i].bone_idx == bone_idx && curves[i].path == path)
            return 1;
    }
    return 0;
}

/// @brief Return non-zero if a node animation channel already owns this node/path pair.
static int gltf_node_anim_channel_seen(const int32_t *nodes,
                                       const int32_t *paths,
                                       int32_t count,
                                       int32_t node_idx,
                                       int32_t path) {
    if (!nodes || !paths || count <= 0 || node_idx < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (nodes[i] == node_idx && paths[i] == path)
            return 1;
    }
    return 0;
}

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
    if (!times || !count || !capacity || *count < 0 || *capacity < 0 || *count > *capacity ||
        (*count > 0 && !*times) || !isfinite(value) || value < 0.0)
        return 0;
    while (pos < *count && (*times)[pos] < value - 1e-6)
        pos++;
    if (pos < *count && fabs((*times)[pos] - value) <= 1e-6)
        return 1;
    if (*count >= *capacity) {
        int32_t new_capacity;
        double *grown;
        if (*capacity > INT32_MAX / 2)
            new_capacity = *count + 1;
        else
            new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        if (new_capacity <= *capacity || (size_t)new_capacity > SIZE_MAX / sizeof(*grown))
            return 0;
        grown = (double *)realloc(*times, (size_t)new_capacity * sizeof(*grown));
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

/// @brief Return a pre-next-key hold time for importing STEP curves into linear Animation3D tracks.
/// @details Animation3D has linear/slerp playback only. Adding a hold key just before the next
///          authored key preserves glTF STEP semantics closely enough for skeletal clips while
///          avoiding duplicate-time replacement in Animation3D.
static double gltf_anim_step_hold_time(double current, double next) {
    double gap = next - current;
    double eps;
    if (!isfinite(current) || !isfinite(next) || gap <= 4e-6)
        return -1.0;
    eps = gap * 0.25;
    if (eps > 1e-4)
        eps = 1e-4;
    if (eps <= 1e-6)
        return -1.0;
    return next - eps;
}

/// @brief Insert all sample times for a skeletal curve, expanding STEP curves with hold keys.
static int gltf_anim_insert_curve_times(double **times,
                                        int32_t *count,
                                        int32_t *capacity,
                                        const gltf_anim_curve_t *curve) {
    int step;
    if (!curve || !curve->valid)
        return 1;
    step = curve->interpolation && strcmp(curve->interpolation, "STEP") == 0;
    for (int32_t ti = 0; ti < curve->input.count; ti++) {
        double t = gltf_curve_time(&curve->input, ti);
        if (!gltf_anim_insert_time(times, count, capacity, t) ||
            *count > RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL)
            return 0;
        if (step && ti + 1 < curve->input.count) {
            double hold = gltf_anim_step_hold_time(t, gltf_curve_time(&curve->input, ti + 1));
            if (hold >= 0.0 &&
                (!gltf_anim_insert_time(times, count, capacity, hold) ||
                 *count > RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL))
                return 0;
        }
    }
    return 1;
}

/// @brief Convert a keyframe index into the real output-accessor offset for the curve.
/// @details glTF CUBICSPLINE samplers store three floats-per-component per keyframe in
///          the order [in-tangent, value, out-tangent]. So for N keyframes the output
///          accessor holds 3N entries and the actual "value" element we want is
///          `key_index * 3 + 1`. STEP and LINEAR samplers store one entry per key and
///          map identity.
static int32_t gltf_curve_output_index(const gltf_anim_curve_t *curve, int32_t key_index) {
    if (key_index < 0)
        return -1;
    if (curve && curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0) {
        if (key_index > (INT32_MAX - 1) / 3)
            return -1;
        return key_index * 3 + 1;
    }
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
    if (!curve || !out || components <= 0 || components > 4 || output_index < 0 ||
        output_index >= curve->output.count)
        return;
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
    if (comp_count > (int32_t)(sizeof(tmp) / sizeof(tmp[0])))
        return 0.0f;
    element = flat_index / comp_count;
    component = flat_index % comp_count;
    if (component < 0 || element < 0 || element >= view->count)
        return 0.0f;
    gltf_accessor_read_f32(view, element, tmp, comp_count);
    return tmp[component];
}

/// @brief Return non-zero when a float accessor region contains only finite values.
static int gltf_accessor_f32_values_are_finite(const gltf_accessor_view_t *view,
                                               int32_t element_count,
                                               int32_t components) {
    float tmp[16];
    if (!view || element_count < 0 || components <= 0 ||
        components > (int32_t)(sizeof(tmp) / sizeof(tmp[0])) || element_count > view->count)
        return 0;
    for (int32_t i = 0; i < element_count; i++) {
        gltf_accessor_read_f32(view, i, tmp, components);
        for (int32_t c = 0; c < components; c++) {
            if (!isfinite(tmp[c]))
                return 0;
        }
    }
    return 1;
}

enum {
    GLTF_ANIM_INTERP_INVALID = -1,
    GLTF_ANIM_INTERP_LINEAR = 0,
    GLTF_ANIM_INTERP_STEP = 1,
    GLTF_ANIM_INTERP_CUBICSPLINE = 2
};

/// @brief Validate glTF animation sampler input times (SCALAR FLOAT, finite, sorted).
static int gltf_animation_input_accessor_valid(const gltf_accessor_view_t *input,
                                               int32_t interpolation_mode) {
    double previous = -1.0;
    int step = interpolation_mode == GLTF_ANIM_INTERP_STEP;
    if (!input || input->comp_type != 5126 || input->comp_count != 1 || input->count <= 0)
        return 0;
    for (int32_t i = 0; i < input->count; i++) {
        double t = gltf_curve_time(input, i);
        if (!isfinite(t) || t < 0.0)
            return 0;
        if (i > 0 && (t < previous || (!step && t <= previous + 1e-6)))
            return 0;
        previous = t;
    }
    return 1;
}

#define GLTF_NODE_ANIM_KEY_COUNT_MAX 1000000
#define GLTF_NODE_ANIM_VALUE_WIDTH_MAX 4096
#define GLTF_NODE_ANIM_VALUE_COUNT_MAX 4000000
#define GLTF_SKIN_ANIM_KEY_COUNT_MAX RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL

/// @brief Decode a glTF sampler interpolation string, applying only the spec's absent-field default.
static int32_t gltf_animation_interpolation_mode(const char *interpolation) {
    if (!interpolation || interpolation[0] == '\0' || strcmp(interpolation, "LINEAR") == 0)
        return GLTF_ANIM_INTERP_LINEAR;
    if (strcmp(interpolation, "STEP") == 0)
        return GLTF_ANIM_INTERP_STEP;
    if (strcmp(interpolation, "CUBICSPLINE") == 0)
        return GLTF_ANIM_INTERP_CUBICSPLINE;
    return GLTF_ANIM_INTERP_INVALID;
}

/// @brief Validate translation/rotation/scale animation sampler outputs.
static int gltf_animation_trs_output_accessor_valid(const gltf_accessor_view_t *input,
                                                    const gltf_accessor_view_t *output,
                                                    int32_t components,
                                                    int cubic) {
    int64_t required_count;
    if (!input || !output || components <= 0 || output->comp_type != 5126 ||
        output->comp_count != components)
        return 0;
    required_count = (int64_t)input->count * (int64_t)(cubic ? 3 : 1);
    if (required_count <= 0 || required_count > INT32_MAX || output->count != required_count)
        return 0;
    return gltf_accessor_f32_values_are_finite(output, (int32_t)required_count, components);
}

/// @brief Validate inverseBindMatrices accessors before overriding computed bind data.
static int gltf_inverse_bind_accessor_valid(const gltf_accessor_view_t *view,
                                            int32_t joint_count) {
    if (!view || joint_count <= 0 || view->comp_type != 5126 || view->comp_count != 16 ||
        view->count != joint_count)
        return 0;
    return gltf_accessor_f32_values_are_finite(view, joint_count, 16);
}

/// @brief Normalize @p out in-place when @p components == 4 (quaternion interpolation result).
/// @details glTF CUBICSPLINE and linear interpolation can drift the magnitude of rotation
///          quaternions away from unit length; renormalization prevents visual artifacts.
///          Non-quaternion paths (translation/scale with components ≠ 4) are left unchanged.
static void gltf_normalize_sample_if_quat(float *out, int32_t components) {
    if (!out)
        return;
    if (components == 4) {
        float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (isfinite(len) && len > 1e-6f) {
            for (int c = 0; c < 4; c++)
                out[c] /= len;
        } else {
            out[0] = 0.0f;
            out[1] = 0.0f;
            out[2] = 0.0f;
            out[3] = 1.0f;
        }
    }
}

/// @brief Replace non-finite sampled components with the caller's pre-sample fallback.
static void gltf_sanitize_sample_or(float *out, const float *fallback, int32_t components) {
    if (!out || components <= 0 || components > 4)
        return;
    for (int32_t c = 0; c < components; c++) {
        if (!isfinite(out[c]))
            out[c] = fallback && isfinite(fallback[c]) ? fallback[c]
                                                       : (components == 4 && c == 3 ? 1.0f : 0.0f);
    }
    gltf_normalize_sample_if_quat(out, components);
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
    if (!isfinite(alpha) || alpha < 0.0)
        alpha = 0.0;
    else if (alpha > 1.0)
        alpha = 1.0;
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
    if (!isfinite(dot)) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    if (dot > 1.0)
        dot = 1.0;
    if (dot < -1.0)
        dot = -1.0;
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
    int32_t lo;
    int32_t hi;
    double t0;
    double t1;
    double alpha;
    float fallback[4] = {0.0f, 0.0f, 0.0f, components == 4 ? 1.0f : 0.0f};
    if (!curve || !curve->valid || !out || components <= 0 || curve->input.count <= 0 ||
        components > 4 || !isfinite(time))
        return;
    for (int32_t c = 0; c < components; c++)
        fallback[c] = isfinite(out[c]) ? out[c] : fallback[c];
    key_count = curve->input.count;
    if (curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0) {
        if (key_count > INT32_MAX / 3)
            key_count = curve->output.count / 3;
        else if (curve->output.count < key_count * 3)
            key_count = curve->output.count / 3;
    } else if (curve->output.count < key_count)
        key_count = curve->output.count;
    if (key_count <= 0)
        return;
    if (curve->interpolation && strcmp(curve->interpolation, "STEP") == 0) {
        int32_t upper_lo = 0;
        int32_t upper_hi = key_count;
        int32_t sample_index;
        while (upper_lo < upper_hi) {
            int32_t mid = upper_lo + (upper_hi - upper_lo) / 2;
            if (gltf_curve_time(&curve->input, mid) <= time)
                upper_lo = mid + 1;
            else
                upper_hi = mid;
        }
        sample_index = upper_lo - 1;
        if (sample_index < 0)
            sample_index = 0;
        if (sample_index >= key_count)
            sample_index = key_count - 1;
        gltf_curve_read_value(curve, sample_index, out, components);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
    if (time <= gltf_curve_time(&curve->input, 0)) {
        gltf_curve_read_value(curve, 0, out, components);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
    if (time >= gltf_curve_time(&curve->input, key_count - 1)) {
        gltf_curve_read_value(curve, key_count - 1, out, components);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
    lo = 0;
    hi = key_count - 1;
    while (hi - lo > 1) {
        int32_t mid = lo + (hi - lo) / 2;
        if (gltf_curve_time(&curve->input, mid) <= time)
            lo = mid;
        else
            hi = mid;
    }
    t0 = gltf_curve_time(&curve->input, lo);
    t1 = gltf_curve_time(&curve->input, hi);
    alpha = t1 > t0 ? (time - t0) / (t1 - t0) : 0.0;
    if (!isfinite(alpha) || alpha < 0.0)
        alpha = 0.0;
    if (alpha > 1.0)
        alpha = 1.0;
    {
        float a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float b[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        gltf_curve_read_value(curve, lo, a, components);
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
            if (lo > (INT32_MAX - 2) / 3 || hi > INT32_MAX / 3)
                return;
            gltf_curve_read_output_value(curve, lo * 3 + 2, out_tangent0, components);
            gltf_curve_read_output_value(curve, hi * 3, in_tangent1, components);
            gltf_curve_read_value(curve, hi, b, components);
            if (components == 4) {
                double dot;
                gltf_normalize_sample_if_quat(a, 4);
                gltf_normalize_sample_if_quat(b, 4);
                dot = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2] +
                      (double)a[3] * b[3];
                if (isfinite(dot) && dot < 0.0) {
                    for (int c = 0; c < 4; c++) {
                        b[c] = -b[c];
                        in_tangent1[c] = -in_tangent1[c];
                    }
                }
            }
            if (!isfinite(dt))
                dt = 0.0;
            for (int c = 0; c < components; c++) {
                out[c] = (float)(h00 * a[c] + h10 * dt * out_tangent0[c] + h01 * b[c] +
                                 h11 * dt * in_tangent1[c]);
            }
            gltf_sanitize_sample_or(out, fallback, components);
            return;
        }
        gltf_curve_read_value(curve, hi, b, components);
        if (components == 4) {
            gltf_slerp_quat(a, b, alpha, out);
            return;
        }
        for (int c = 0; c < components; c++)
            out[c] = (float)(a[c] + (b[c] - a[c]) * alpha);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
}

static int32_t gltf_skin_anim_path_index(const char *path) {
    if (!path)
        return -1;
    if (strcmp(path, "translation") == 0)
        return 0;
    if (strcmp(path, "rotation") == 0)
        return 1;
    if (strcmp(path, "scale") == 0)
        return 2;
    return -1;
}

static void gltf_skin_anim_resolve_curve(void *root,
                                         void *nodes_arr,
                                         void *samplers,
                                         void *channels,
                                         int32_t channel_index,
                                         gltf_buffer_t *buffers,
                                         int buf_count,
                                         const gltf_skin_t *skin,
                                         gltf_anim_curve_t *curves,
                                         double *duration_io) {
    void *channel = rt_seq_get(channels, channel_index);
    void *target = jget(channel, "target");
    const char *path = jstr(target, "path");
    int64_t sampler_idx = jint(channel, "sampler", -1);
    int64_t node_idx = jint(target, "node", -1);
    void *sampler;
    int32_t interpolation_mode;
    int32_t bone_idx;
    if (sampler_idx < 0 || sampler_idx >= jarr_len(samplers) || node_idx < 0 ||
        node_idx > INT32_MAX || node_idx >= jarr_len(nodes_arr))
        return;
    bone_idx = gltf_skin_bone_for_node(skin, (int32_t)node_idx);
    if (bone_idx < 0)
        return;
    sampler = rt_seq_get(samplers, sampler_idx);
    curves[channel_index].path = gltf_skin_anim_path_index(path);
    if (curves[channel_index].path < 0)
        return;
    if (gltf_anim_curve_already_targets(
            curves, channel_index, bone_idx, curves[channel_index].path))
        return;
    curves[channel_index].interpolation = jstr(sampler, "interpolation");
    interpolation_mode = gltf_animation_interpolation_mode(curves[channel_index].interpolation);
    if (interpolation_mode == GLTF_ANIM_INTERP_INVALID)
        return;
    if (!gltf_get_accessor_view(root,
                                jint(sampler, "input", -1),
                                buffers,
                                buf_count,
                                &curves[channel_index].input) ||
        !gltf_get_accessor_view(root,
                                jint(sampler, "output", -1),
                                buffers,
                                buf_count,
                                &curves[channel_index].output))
        return;
    if (curves[channel_index].input.count > GLTF_SKIN_ANIM_KEY_COUNT_MAX ||
        !gltf_animation_input_accessor_valid(&curves[channel_index].input, interpolation_mode))
        return;
    if (!gltf_animation_trs_output_accessor_valid(
            &curves[channel_index].input,
            &curves[channel_index].output,
            curves[channel_index].path == 1 ? 4 : 3,
            interpolation_mode == GLTF_ANIM_INTERP_CUBICSPLINE))
        return;
    curves[channel_index].valid = 1;
    curves[channel_index].node_idx = (int32_t)node_idx;
    curves[channel_index].bone_idx = bone_idx;
    for (int32_t ti = 0; ti < curves[channel_index].input.count; ti++) {
        double t = gltf_curve_time(&curves[channel_index].input, ti);
        if (t > *duration_io)
            *duration_io = t;
    }
}

static gltf_anim_curve_t *gltf_skin_anim_build_curves(void *root,
                                                     void *nodes_arr,
                                                     void *samplers,
                                                     void *channels,
                                                     int32_t channel_count,
                                                     gltf_buffer_t *buffers,
                                                     int buf_count,
                                                     const gltf_skin_t *skin,
                                                     double *duration_io) {
    gltf_anim_curve_t *curves = (gltf_anim_curve_t *)calloc((size_t)channel_count, sizeof(*curves));
    if (!curves)
        return NULL;
    for (int32_t ci = 0; ci < channel_count; ci++)
        gltf_skin_anim_resolve_curve(root,
                                     nodes_arr,
                                     samplers,
                                     channels,
                                     ci,
                                     buffers,
                                     buf_count,
                                     skin,
                                     curves,
                                     duration_io);
    return curves;
}

static int gltf_skin_anim_collect_bone_curves(const gltf_anim_curve_t *curves,
                                              int32_t channel_count,
                                              int32_t bone,
                                              double **out_times,
                                              int32_t *out_time_count,
                                              int32_t *out_node_idx,
                                              const gltf_anim_curve_t **out_curve_t,
                                              const gltf_anim_curve_t **out_curve_r,
                                              const gltf_anim_curve_t **out_curve_s) {
    double *times = NULL;
    int32_t time_count = 0;
    int32_t time_capacity = 0;
    int32_t node_idx = -1;
    const gltf_anim_curve_t *curve_t = NULL;
    const gltf_anim_curve_t *curve_r = NULL;
    const gltf_anim_curve_t *curve_s = NULL;
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
        if (!gltf_anim_insert_curve_times(&times, &time_count, &time_capacity, &curves[ci])) {
            free(times);
            return 0;
        }
    }
    if (time_count <= 0 || node_idx < 0) {
        free(times);
        return 0;
    }
    *out_times = times;
    *out_time_count = time_count;
    *out_node_idx = node_idx;
    *out_curve_t = curve_t;
    *out_curve_r = curve_r;
    *out_curve_s = curve_s;
    return 1;
}

static int gltf_skin_anim_emit_bone_samples(void *anim,
                                            void *nodes_arr,
                                            int32_t bone,
                                            int32_t node_idx,
                                            const gltf_anim_curve_t *curve_t,
                                            const gltf_anim_curve_t *curve_r,
                                            const gltf_anim_curve_t *curve_s,
                                            const double *times,
                                            int32_t time_count) {
    double local[16];
    double pos_d[3];
    double rot_d[4];
    double scl_d[3];
    int emitted = 0;
    if (!gltf_node_local_matrix(nodes_arr, node_idx, local))
        return 0;
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
        if (pos_obj && rot_obj && scl_obj) {
            rt_animation3d_add_keyframe(anim, bone, times[ti], pos_obj, rot_obj, scl_obj);
            emitted = 1;
        }
        gltf_release_ref(&pos_obj);
        gltf_release_ref(&rot_obj);
        gltf_release_ref(&scl_obj);
    }
    return emitted;
}

static int gltf_skin_anim_emit_bone_track(void *anim,
                                          void *nodes_arr,
                                          const gltf_anim_curve_t *curves,
                                          int32_t channel_count,
                                          int32_t bone) {
    double *times = NULL;
    int32_t time_count = 0;
    int32_t node_idx = -1;
    const gltf_anim_curve_t *curve_t = NULL;
    const gltf_anim_curve_t *curve_r = NULL;
    const gltf_anim_curve_t *curve_s = NULL;
    int emitted;
    if (!gltf_skin_anim_collect_bone_curves(curves,
                                            channel_count,
                                            bone,
                                            &times,
                                            &time_count,
                                            &node_idx,
                                            &curve_t,
                                            &curve_r,
                                            &curve_s))
        return 0;
    emitted = gltf_skin_anim_emit_bone_samples(
        anim, nodes_arr, bone, node_idx, curve_t, curve_r, curve_s, times, time_count);
    free(times);
    return emitted;
}

static int gltf_skin_anim_emit_clip_bones(void *anim,
                                          void *nodes_arr,
                                          const gltf_anim_curve_t *curves,
                                          int32_t channel_count,
                                          int32_t bone_limit) {
    int emitted_any = 0;
    for (int32_t bone = 0; bone < bone_limit; bone++) {
        if (gltf_skin_anim_emit_bone_track(anim, nodes_arr, curves, channel_count, bone))
            emitted_any = 1;
    }
    return emitted_any;
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
    int64_t anim_count64 = jarr_len(anims_arr);
    int32_t anim_count;
    if (!asset || !anims_arr || anim_count64 <= 0 || !skins || skin_count <= 0)
        return;
    if (anim_count64 <= 0 || anim_count64 > INT32_MAX)
        return;
    anim_count = (int32_t)anim_count64;
    if ((int64_t)anim_count * (int64_t)skin_count > INT32_MAX ||
        (size_t)anim_count > SIZE_MAX / (size_t)skin_count ||
        (size_t)anim_count * (size_t)skin_count > SIZE_MAX / sizeof(void *))
        return;
    asset->animations = (void **)calloc((size_t)anim_count * (size_t)skin_count, sizeof(void *));
    asset->animation_capacity = asset->animations ? anim_count * skin_count : 0;
    if (!asset->animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int channel_count_tmp = 0;
        int32_t channel_count;
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        if (!gltf_jarr_len_i32(channels, &channel_count_tmp))
            continue;
        channel_count = (int32_t)channel_count_tmp;
        if (channel_count <= 0 || !samplers)
            continue;
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "animation_%d", (int)ai);
            name = generated_name;
        }
        for (int32_t si = 0; si < skin_count; si++) {
            rt_skeleton3d *skin_skel = (rt_skeleton3d *)skins[si].skeleton;
            int32_t bone_limit;
            gltf_anim_curve_t *curves;
            double duration = 0.0;
            int emitted_any = 0;
            void *anim;
            char skin_name[128];
            const char *clip_name = name;
            if (!skin_skel || !skins[si].joint_to_bone ||
                skeleton3d_safe_bone_count(skin_skel) <= 0)
                continue;
            bone_limit = skeleton3d_safe_bone_count(skin_skel);
            curves = gltf_skin_anim_build_curves(root,
                                                 nodes_arr,
                                                 samplers,
                                                 channels,
                                                 channel_count,
                                                 buffers,
                                                 buf_count,
                                                 &skins[si],
                                                 &duration);
            if (!curves)
                continue;
            if (skin_count > 1) {
                snprintf(skin_name, sizeof(skin_name), "%s_skin_%d", name, (int)si);
                clip_name = skin_name;
            }
            anim = rt_animation3d_new(rt_const_cstr(clip_name), duration > 0.0 ? duration : 1.0);
            if (!anim) {
                free(curves);
                continue;
            }
            emitted_any = gltf_skin_anim_emit_clip_bones(
                anim, nodes_arr, curves, channel_count, bone_limit);
            free(curves);
            if (!emitted_any) {
                gltf_release_ref(&anim);
                continue;
            }
            rt_animation3d_set_looping(anim, 1);
            if (asset->animation_count < 0 ||
                asset->animation_count >= asset->animation_capacity) {
                gltf_release_ref(&anim);
                continue;
            }
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
    if (!path)
        return -1;
    if (strcmp(path, "translation") == 0)
        return RT_NODE_ANIM_PATH_TRANSLATION;
    if (strcmp(path, "rotation") == 0)
        return RT_NODE_ANIM_PATH_ROTATION;
    if (strcmp(path, "scale") == 0)
        return RT_NODE_ANIM_PATH_SCALE;
    if (strcmp(path, "weights") == 0)
        return RT_NODE_ANIM_PATH_WEIGHTS;
    return -1;
}

/// @brief Return the morph-target count addressed by a node weights animation.
/// @details glTF `weights` animation output width must equal the target mesh's morph-target count.
///          The importer represents multi-primitive meshes as a parent node plus child primitive
///          nodes, then applies animated weights to the subtree, so every morphed primitive with
///          targets must agree on the same count.
static int32_t gltf_node_weights_target_count(void *root, int32_t node_idx) {
    void *nodes_arr = jarr(root, "nodes");
    void *meshes_arr = jarr(root, "meshes");
    void *node_json;
    void *mesh_json;
    void *primitives;
    void *mesh_weights;
    int64_t node_count = jarr_len(nodes_arr);
    int64_t mesh_count = jarr_len(meshes_arr);
    int64_t mesh_ref;
    int64_t primitive_count;
    int32_t target_count = -1;
    if (!root || node_idx < 0 || node_idx >= node_count)
        return 0;
    node_json = rt_seq_get(nodes_arr, node_idx);
    mesh_ref = jint(node_json, "mesh", -1);
    if (mesh_ref < 0 || mesh_ref >= mesh_count)
        return 0;
    mesh_json = rt_seq_get(meshes_arr, mesh_ref);
    primitives = jarr(mesh_json, "primitives");
    primitive_count = jarr_len(primitives);
    for (int64_t pi = 0; pi < primitive_count; pi++) {
        void *targets = jarr(rt_seq_get(primitives, pi), "targets");
        int64_t count = jarr_len(targets);
        if (count <= 0)
            continue;
        if (count > GLTF_NODE_ANIM_VALUE_WIDTH_MAX)
            return 0;
        if (target_count < 0)
            target_count = (int32_t)count;
        else if (target_count != (int32_t)count)
            return 0;
    }
    if (target_count <= 0)
        return 0;
    mesh_weights = jarr(mesh_json, "weights");
    if (mesh_weights) {
        int64_t weight_count = jarr_len(mesh_weights);
        if (weight_count > 0 && weight_count != target_count)
            return 0;
    }
    return target_count;
}

/// @brief Determine how many float components are stored per keyframe for a given animation path.
/// @details For TRANSLATION/SCALE the width is 3 (XYZ); for ROTATION it is 4 (XYZW quaternion).
///          For the WEIGHTS path the width is derived by dividing the total output components by
///          the number of input keyframes (× 3 for CUBICSPLINE). Returns 0 for degenerate data.
static int32_t gltf_node_anim_width_for_path(int32_t path,
                                             const gltf_accessor_view_t *input,
                                             const gltf_accessor_view_t *output,
                                             int32_t interpolation_mode,
                                             int cubic) {
    int64_t total_components;
    int64_t divisor;
    if (!gltf_animation_input_accessor_valid(input, interpolation_mode) || !output ||
        output->count <= 0 || output->comp_type != 5126)
        return 0;
    if (path == RT_NODE_ANIM_PATH_TRANSLATION || path == RT_NODE_ANIM_PATH_SCALE) {
        return gltf_animation_trs_output_accessor_valid(input, output, 3, cubic) ? 3 : 0;
    }
    if (path == RT_NODE_ANIM_PATH_ROTATION) {
        return gltf_animation_trs_output_accessor_valid(input, output, 4, cubic) ? 4 : 0;
    }
    total_components = (int64_t)output->count * (int64_t)output->comp_count;
    divisor = (int64_t)input->count * (cubic ? 3 : 1);
    if (divisor <= 0 || total_components <= 0 || total_components % divisor != 0)
        return 0;
    if (total_components / divisor > INT32_MAX)
        return 0;
    if (total_components / divisor > GLTF_NODE_ANIM_VALUE_WIDTH_MAX)
        return 0;
    if (!gltf_accessor_f32_values_are_finite(output, output->count, output->comp_count))
        return 0;
    return (int32_t)(total_components / divisor);
}

typedef struct {
    int32_t path;
    int32_t node_idx;
    const char *interpolation;
    int32_t interpolation_mode;
    int cubic;
    gltf_accessor_view_t input;
    gltf_accessor_view_t output;
    int32_t width;
    int64_t value_count;
} gltf_node_anim_channel_t;

static int gltf_node_anim_resolve_channel(void *root,
                                          void *nodes_arr,
                                          void *samplers,
                                          void *channel,
                                          gltf_buffer_t *buffers,
                                          int buf_count,
                                          const gltf_skin_t *skins,
                                          int32_t skin_count,
                                          const int32_t *seen_nodes,
                                          const int32_t *seen_paths,
                                          int32_t seen_count,
                                          gltf_node_anim_channel_t *out) {
    void *target = jget(channel, "target");
    const char *path_str = jstr(target, "path");
    int32_t path = gltf_node_anim_path(path_str);
    int64_t sampler_idx = jint(channel, "sampler", -1);
    int64_t node_idx = jint(target, "node", -1);
    void *sampler;
    int32_t interpolation_mode;
    int cubic;
    gltf_accessor_view_t input;
    gltf_accessor_view_t output;
    int32_t width;
    int32_t expected_weight_width = 0;
    int64_t value_count;

    memset(out, 0, sizeof(*out));
    if (path < 0 || sampler_idx < 0 || sampler_idx >= jarr_len(samplers) || node_idx < 0 ||
        node_idx > INT32_MAX || node_idx >= jarr_len(nodes_arr))
        return 0;
    if (path != RT_NODE_ANIM_PATH_WEIGHTS &&
        gltf_node_is_skin_joint(skins, skin_count, (int32_t)node_idx))
        return 0;
    if (path == RT_NODE_ANIM_PATH_WEIGHTS) {
        expected_weight_width = gltf_node_weights_target_count(root, (int32_t)node_idx);
        if (expected_weight_width <= 0)
            return 0;
    }
    if (gltf_node_anim_channel_seen(
            seen_nodes, seen_paths, seen_count, (int32_t)node_idx, path))
        return 0;
    sampler = rt_seq_get(samplers, sampler_idx);
    out->interpolation = jstr(sampler, "interpolation");
    interpolation_mode = gltf_animation_interpolation_mode(out->interpolation);
    if (interpolation_mode == GLTF_ANIM_INTERP_INVALID)
        return 0;
    cubic = interpolation_mode == GLTF_ANIM_INTERP_CUBICSPLINE;
    if (!gltf_get_accessor_view(root, jint(sampler, "input", -1), buffers, buf_count, &input) ||
        !gltf_get_accessor_view(root, jint(sampler, "output", -1), buffers, buf_count, &output))
        return 0;
    if (input.count <= 0 || input.count > GLTF_NODE_ANIM_KEY_COUNT_MAX ||
        !gltf_animation_input_accessor_valid(&input, interpolation_mode))
        return 0;
    width = gltf_node_anim_width_for_path(path, &input, &output, interpolation_mode, cubic);
    if (width <= 0)
        return 0;
    if (path == RT_NODE_ANIM_PATH_WEIGHTS && width != expected_weight_width)
        return 0;
    if (width > GLTF_NODE_ANIM_VALUE_WIDTH_MAX)
        return 0;
    value_count = (int64_t)input.count * (int64_t)width;
    if (value_count <= 0 || value_count > INT32_MAX ||
        value_count > GLTF_NODE_ANIM_VALUE_COUNT_MAX)
        return 0;
    if (cubic && value_count > INT32_MAX / 3)
        return 0;
    if ((size_t)input.count > SIZE_MAX / sizeof(double) ||
        (size_t)value_count > SIZE_MAX / sizeof(float))
        return 0;
    out->path = path;
    out->node_idx = (int32_t)node_idx;
    out->interpolation_mode = interpolation_mode;
    out->cubic = cubic;
    out->input = input;
    out->output = output;
    out->width = width;
    out->value_count = value_count;
    return 1;
}

static void gltf_node_anim_free_samples(double *times,
                                        float *values,
                                        float *in_tangents,
                                        float *out_tangents) {
    free(times);
    free(values);
    free(in_tangents);
    free(out_tangents);
}

static int gltf_node_anim_alloc_samples(const gltf_node_anim_channel_t *channel,
                                        double **out_times,
                                        float **out_values,
                                        float **out_in_tangents,
                                        float **out_out_tangents) {
    double *times = NULL;
    float *values = NULL;
    float *in_tangents = NULL;
    float *out_tangents = NULL;
    *out_times = NULL;
    *out_values = NULL;
    *out_in_tangents = NULL;
    *out_out_tangents = NULL;
    times = (double *)malloc((size_t)channel->input.count * sizeof(double));
    values = (float *)malloc((size_t)channel->value_count * sizeof(float));
    if (channel->cubic) {
        in_tangents = (float *)malloc((size_t)channel->value_count * sizeof(float));
        out_tangents = (float *)malloc((size_t)channel->value_count * sizeof(float));
    }
    if (!times || !values || (channel->cubic && (!in_tangents || !out_tangents))) {
        gltf_node_anim_free_samples(times, values, in_tangents, out_tangents);
        return 0;
    }
    *out_times = times;
    *out_values = values;
    *out_in_tangents = in_tangents;
    *out_out_tangents = out_tangents;
    return 1;
}

static void gltf_node_anim_fill_weight_key(const gltf_node_anim_channel_t *channel,
                                           int32_t key_index,
                                           float *values,
                                           float *in_tangents,
                                           float *out_tangents) {
    int32_t width = channel->width;
    int32_t in_base = channel->cubic ? (key_index * 3) * width : key_index * width;
    int32_t base = channel->cubic ? (key_index * 3 + 1) * width : key_index * width;
    int32_t out_base = channel->cubic ? (key_index * 3 + 2) * width : key_index * width;
    for (int32_t wi = 0; wi < width; wi++)
        values[(size_t)key_index * (size_t)width + (size_t)wi] =
            gltf_accessor_read_flat_f32(&channel->output, base + wi);
    if (channel->cubic) {
        for (int32_t wi = 0; wi < width; wi++) {
            in_tangents[(size_t)key_index * (size_t)width + (size_t)wi] =
                gltf_accessor_read_flat_f32(&channel->output, in_base + wi);
            out_tangents[(size_t)key_index * (size_t)width + (size_t)wi] =
                gltf_accessor_read_flat_f32(&channel->output, out_base + wi);
        }
    }
}

static void gltf_node_anim_fill_trs_key(const gltf_node_anim_channel_t *channel,
                                        int32_t key_index,
                                        int32_t source_key,
                                        float *values,
                                        float *in_tangents,
                                        float *out_tangents) {
    float tmp[4] = {
        0.0f, 0.0f, 0.0f, channel->path == RT_NODE_ANIM_PATH_ROTATION ? 1.0f : 0.0f};
    gltf_accessor_read_f32(&channel->output, source_key, tmp, channel->width);
    if (channel->path == RT_NODE_ANIM_PATH_ROTATION)
        gltf_normalize_sample_if_quat(tmp, 4);
    memcpy(&values[(size_t)key_index * (size_t)channel->width],
           tmp,
           (size_t)channel->width * sizeof(float));
    if (channel->cubic) {
        float in_tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float out_tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gltf_accessor_read_f32(&channel->output, key_index * 3, in_tmp, channel->width);
        gltf_accessor_read_f32(&channel->output, key_index * 3 + 2, out_tmp, channel->width);
        memcpy(&in_tangents[(size_t)key_index * (size_t)channel->width],
               in_tmp,
               (size_t)channel->width * sizeof(float));
        memcpy(&out_tangents[(size_t)key_index * (size_t)channel->width],
               out_tmp,
               (size_t)channel->width * sizeof(float));
    }
}

static void gltf_node_anim_fill_samples(const gltf_node_anim_channel_t *channel,
                                        double *times,
                                        float *values,
                                        float *in_tangents,
                                        float *out_tangents,
                                        double *duration_io) {
    for (int32_t ki = 0; ki < channel->input.count; ki++) {
        int32_t source_key = channel->cubic ? (ki <= (INT32_MAX - 1) / 3 ? ki * 3 + 1 : -1) : ki;
        /* Initialize the time sample before any early-continue so a skipped
         * cubic key never leaves an uninitialized slot, and coerce a non-finite
         * curve time to 0 so it can't poison the clip duration or interpolation. */
        double tval = gltf_curve_time(&channel->input, ki);
        times[ki] = isfinite(tval) ? tval : 0.0;
        if (source_key < 0)
            continue;
        if (times[ki] > *duration_io)
            *duration_io = times[ki];
        if (channel->path == RT_NODE_ANIM_PATH_WEIGHTS)
            gltf_node_anim_fill_weight_key(channel, ki, values, in_tangents, out_tangents);
        else
            gltf_node_anim_fill_trs_key(
                channel, ki, source_key, values, in_tangents, out_tangents);
    }
}

static int gltf_node_anim_emit_channel(void *node_anim,
                                       void *nodes_arr,
                                       const gltf_node_anim_channel_t *channel,
                                       double *times,
                                       float *values,
                                       float *in_tangents,
                                       float *out_tangents,
                                       int32_t *seen_nodes,
                                       int32_t *seen_paths,
                                       int32_t *seen_count,
                                       int32_t channel_count) {
    char fallback_name[64];
    const char *target_name =
        gltf_effective_node_name(nodes_arr, channel->node_idx, fallback_name, sizeof(fallback_name));
    int64_t channel_index;
    if (!target_name)
        return 0;
    if (channel->cubic) {
        channel_index = rt_node_animation3d_add_cubic_channel(node_anim,
                                                              rt_const_cstr(target_name),
                                                              channel->path,
                                                              channel->input.count,
                                                              channel->width,
                                                              times,
                                                              values,
                                                              in_tangents,
                                                              out_tangents);
    } else {
        channel_index = rt_node_animation3d_add_channel(
            node_anim,
            rt_const_cstr(target_name),
            channel->path,
            channel->interpolation_mode == GLTF_ANIM_INTERP_STEP ? RT_NODE_ANIM_INTERP_STEP
                                                                 : RT_NODE_ANIM_INTERP_LINEAR,
            channel->input.count,
            channel->width,
            times,
            values);
    }
    if (channel_index < 0)
        return 0;
    rt_node_animation3d_set_channel_target_node_index(node_anim, channel_index, channel->node_idx);
    if (*seen_count < channel_count) {
        seen_nodes[*seen_count] = channel->node_idx;
        seen_paths[*seen_count] = channel->path;
        (*seen_count)++;
    }
    return 1;
}

static int gltf_node_anim_process_channel(void *node_anim,
                                          void *root,
                                          void *nodes_arr,
                                          void *samplers,
                                          void *channel_json,
                                          gltf_buffer_t *buffers,
                                          int buf_count,
                                          const gltf_skin_t *skins,
                                          int32_t skin_count,
                                          int32_t *seen_nodes,
                                          int32_t *seen_paths,
                                          int32_t *seen_count,
                                          int32_t channel_count,
                                          double *duration_io) {
    gltf_node_anim_channel_t channel;
    double *times = NULL;
    float *values = NULL;
    float *in_tangents = NULL;
    float *out_tangents = NULL;
    int emitted;
    if (!gltf_node_anim_resolve_channel(root,
                                        nodes_arr,
                                        samplers,
                                        channel_json,
                                        buffers,
                                        buf_count,
                                        skins,
                                        skin_count,
                                        seen_nodes,
                                        seen_paths,
                                        *seen_count,
                                        &channel))
        return 0;
    if (!gltf_node_anim_alloc_samples(&channel, &times, &values, &in_tangents, &out_tangents))
        return 0;
    gltf_node_anim_fill_samples(&channel, times, values, in_tangents, out_tangents, duration_io);
    emitted = gltf_node_anim_emit_channel(node_anim,
                                          nodes_arr,
                                          &channel,
                                          times,
                                          values,
                                          in_tangents,
                                          out_tangents,
                                          seen_nodes,
                                          seen_paths,
                                          seen_count,
                                          channel_count);
    gltf_node_anim_free_samples(times, values, in_tangents, out_tangents);
    return emitted;
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
    int64_t anim_count64 = jarr_len(anims_arr);
    int32_t anim_count;
    if (!asset || !anims_arr || anim_count64 <= 0 || anim_count64 > INT32_MAX)
        return;
    anim_count = (int32_t)anim_count64;
    asset->node_animations = (void **)calloc((size_t)anim_count, sizeof(void *));
    asset->node_animation_capacity = asset->node_animations ? anim_count : 0;
    if (!asset->node_animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int channel_count_tmp = 0;
        int32_t channel_count;
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        void *node_anim;
        int32_t *seen_nodes;
        int32_t *seen_paths;
        int32_t seen_count = 0;
        double duration = 0.0;
        int emitted_any = 0;
        if (!gltf_jarr_len_i32(channels, &channel_count_tmp))
            continue;
        channel_count = (int32_t)channel_count_tmp;
        if (channel_count <= 0 || !samplers)
            continue;
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "node_animation_%d", (int)ai);
            name = generated_name;
        }
        node_anim = rt_node_animation3d_new(rt_const_cstr(name), 1.0);
        if (!node_anim)
            continue;
        if ((size_t)channel_count > SIZE_MAX / sizeof(*seen_nodes) ||
            (size_t)channel_count > SIZE_MAX / sizeof(*seen_paths)) {
            gltf_release_ref(&node_anim);
            continue;
        }
        seen_nodes = (int32_t *)calloc((size_t)channel_count, sizeof(*seen_nodes));
        seen_paths = (int32_t *)calloc((size_t)channel_count, sizeof(*seen_paths));
        if (!seen_nodes || !seen_paths) {
            free(seen_nodes);
            free(seen_paths);
            gltf_release_ref(&node_anim);
            continue;
        }
        for (int32_t ci = 0; ci < channel_count; ci++) {
            if (gltf_node_anim_process_channel(node_anim,
                                               root,
                                               nodes_arr,
                                               samplers,
                                               rt_seq_get(channels, ci),
                                               buffers,
                                               buf_count,
                                               skins,
                                               skin_count,
                                               seen_nodes,
                                               seen_paths,
                                               &seen_count,
                                               channel_count,
                                               &duration))
                emitted_any = 1;
        }
        free(seen_nodes);
        free(seen_paths);
        ((rt_node_animation3d *)node_anim)->duration = duration > 0.0 ? duration : 1.0;
        if (!emitted_any) {
            gltf_release_ref(&node_anim);
            continue;
        }
        if (asset->node_animation_count < 0 ||
            asset->node_animation_count >= asset->node_animation_capacity) {
            gltf_release_ref(&node_anim);
            continue;
        }
        asset->node_animations[asset->node_animation_count++] = node_anim;
    }
}

//===----------------------------------------------------------------------===//
// Loader phase helpers (extracted from rt_gltf_load_impl)
//===----------------------------------------------------------------------===//

typedef struct {
    const char *mime_type;
    uint8_t *owned_data;
    const uint8_t *image_data;
    size_t image_len;
    char parsed_mime[64];
} gltf_image_load_state_t;

static void gltf_mark_required_image_sources(void *textures_arr,
                                             int texture_count,
                                             int image_count,
                                             uint8_t *image_required) {
    if (!textures_arr || texture_count <= 0 || image_count <= 0 || !image_required)
        return;
    for (int i = 0; i < texture_count; i++) {
        void *texture_json = rt_seq_get(textures_arr, (int64_t)i);
        int64_t source_idx = gltf_texture_source_index(texture_json);
        if (source_idx >= 0 && source_idx < image_count)
            image_required[source_idx] = 1u;
    }
}

static int gltf_prepare_image_arrays(void *images_arr,
                                     void *textures_arr,
                                     int *out_image_count,
                                     int *out_texture_count,
                                     void ***out_images,
                                     uint8_t **out_image_required) {
    int image_count = 0;
    int texture_count = 0;
    void **images = NULL;
    uint8_t *image_required = NULL;
    *out_image_count = 0;
    *out_texture_count = 0;
    *out_images = NULL;
    *out_image_required = NULL;
    if (!gltf_jarr_len_i32(images_arr, &image_count) ||
        !gltf_jarr_len_i32(textures_arr, &texture_count))
        return 0;
    if (image_count > 0) {
        images = (void **)calloc((size_t)image_count, sizeof(void *));
        if (!images)
            return 0;
    }
    if (image_count > 0 && texture_count > 0) {
        image_required = (uint8_t *)calloc((size_t)image_count, sizeof(uint8_t));
        if (!image_required) {
            free(images);
            return 0;
        }
        gltf_mark_required_image_sources(textures_arr, texture_count, image_count, image_required);
    }
    *out_image_count = image_count;
    *out_texture_count = texture_count;
    *out_images = images;
    *out_image_required = image_required;
    return 1;
}

static int gltf_load_inline_image_data(void **image_slot,
                                       rt_gltf_preload_bundle *preload_bundle,
                                       int image_index,
                                       const char *uri,
                                       int required_image,
                                       gltf_image_load_state_t *state) {
    char preload_key[96];
    gltf_data_uri_copy_mime(uri, state->parsed_mime, sizeof(state->parsed_mime));
    gltf_preload_image_key(image_index,
                           state->mime_type ? state->mime_type : state->parsed_mime,
                           preload_key,
                           sizeof(preload_key));
    *image_slot = gltf_preload_take_decoded_image(preload_bundle, preload_key);
    if (*image_slot)
        return 1;
    state->owned_data = gltf_preload_bundle_take_dependency(
        preload_bundle, preload_key, GLTF_PRELOAD_DEP_IMAGE, &state->image_len);
    if (state->owned_data) {
        state->image_data = state->owned_data;
        if (!state->mime_type && state->parsed_mime[0] != '\0')
            state->mime_type = state->parsed_mime;
        return 1;
    }
    if (gltf_parse_data_uri(
            uri, state->parsed_mime, sizeof(state->parsed_mime), &state->owned_data, &state->image_len)) {
        state->image_data = state->owned_data;
        if (!state->mime_type && state->parsed_mime[0] != '\0')
            state->mime_type = state->parsed_mime;
        return 1;
    }
    return !required_image ||
           !gltf_preload_image_is_supported_format(
               state->mime_type ? state->mime_type : state->parsed_mime);
}

static int gltf_load_external_image_ref(void **image_slot,
                                        const char *filepath,
                                        int load_assets,
                                        rt_gltf_preload_bundle *preload_bundle,
                                        const char *uri,
                                        int required_image) {
    char image_path[1024];
    gltf_resolve_relative_path(filepath, uri, image_path, sizeof(image_path));
    if (image_path[0] != '\0')
        *image_slot = gltf_load_dependency_image(image_path, load_assets, preload_bundle);
    if (!*image_slot && required_image &&
        gltf_preload_image_is_supported_format(image_path[0] != '\0' ? image_path : uri)) {
        if (load_assets)
            gltf_trap_asset_dependency(filepath, image_path, "image");
        return 0;
    }
    return 1;
}

static int gltf_load_buffer_view_image_data(void **image_slot,
                                            void *root,
                                            rt_gltf_preload_bundle *preload_bundle,
                                            gltf_buffer_t *buffers,
                                            int buf_count,
                                            int image_index,
                                            void *image_json,
                                            int required_image,
                                            gltf_image_load_state_t *state) {
    int64_t view_idx = jint(image_json, "bufferView", -1);
    char preload_key[96];
    gltf_preload_image_key(image_index, state->mime_type, preload_key, sizeof(preload_key));
    *image_slot = gltf_preload_take_decoded_image(preload_bundle, preload_key);
    if (!*image_slot) {
        state->owned_data = gltf_preload_bundle_take_dependency(
            preload_bundle, preload_key, GLTF_PRELOAD_DEP_IMAGE, &state->image_len);
        if (state->owned_data)
            state->image_data = state->owned_data;
        else
            state->image_data =
                gltf_get_buffer_view_data(root, view_idx, buffers, buf_count, &state->image_len);
    }
    return *image_slot || (state->image_data && state->image_len > 0) || !required_image ||
           !gltf_preload_image_is_supported_format(state->mime_type);
}

static int gltf_decode_loaded_image(void **image_slot,
                                    const gltf_image_load_state_t *state,
                                    int required_image) {
    char image_name[64];
    const char *image_type;
    if (*image_slot || !state->image_data || state->image_len == 0)
        return 1;
    gltf_build_embedded_name(state->mime_type, ".bin", image_name, sizeof(image_name));
    image_type = state->mime_type ? state->mime_type : image_name;
    if (gltf_image_is_ktx2(image_type)) {
        *image_slot = gltf_decode_ktx2_payload(state->image_data, state->image_len);
        return *image_slot || !required_image;
    }
    if (gltf_preload_image_is_supported_format(image_type)) {
        uint8_t *rgba_blob = NULL;
        size_t rgba_len = 0;
        if (gltf_decode_image_payload_to_rgba_blob(
                image_type, state->image_data, state->image_len, &rgba_blob, &rgba_len)) {
            *image_slot = gltf_pixels_from_rgba_blob(rgba_blob, rgba_len);
            free(rgba_blob);
            return *image_slot || !required_image;
        }
        return !required_image;
    }
    *image_slot = rt_asset_decode_typed(image_name, state->image_data, state->image_len);
    return 1;
}

static int gltf_load_image_at_index(void *root,
                                    const char *filepath,
                                    int load_assets,
                                    rt_gltf_preload_bundle *preload_bundle,
                                    gltf_buffer_t *buffers,
                                    int buf_count,
                                    void **images,
                                    void *images_arr,
                                    const uint8_t *image_required,
                                    int image_index) {
    void *image_json = rt_seq_get(images_arr, (int64_t)image_index);
    const char *uri = jstr(image_json, "uri");
    gltf_image_load_state_t state;
    int required_image = image_required ? image_required[image_index] != 0 : 0;
    int ok;
    memset(&state, 0, sizeof(state));
    state.mime_type = jstr(image_json, "mimeType");
    if (uri && strncmp(uri, "data:", 5) == 0) {
        ok = gltf_load_inline_image_data(
            &images[image_index], preload_bundle, image_index, uri, required_image, &state);
    } else if (uri) {
        ok = gltf_load_external_image_ref(
            &images[image_index], filepath, load_assets, preload_bundle, uri, required_image);
    } else {
        ok = gltf_load_buffer_view_image_data(&images[image_index],
                                              root,
                                              preload_bundle,
                                              buffers,
                                              buf_count,
                                              image_index,
                                              image_json,
                                              required_image,
                                              &state);
    }
    if (ok)
        ok = gltf_decode_loaded_image(&images[image_index], &state, required_image);
    free(state.owned_data);
    return ok;
}

static int gltf_alloc_texture_tables(int texture_count,
                                     void ***out_texture_images,
                                     uint8_t **out_texture_supported,
                                     gltf_sampler_info_t **out_texture_samplers) {
    void **texture_images = NULL;
    uint8_t *texture_supported = NULL;
    gltf_sampler_info_t *texture_samplers = NULL;
    *out_texture_images = NULL;
    *out_texture_supported = NULL;
    *out_texture_samplers = NULL;
    if (texture_count <= 0)
        return 1;
    texture_images = (void **)calloc((size_t)texture_count, sizeof(void *));
    texture_supported = (uint8_t *)calloc((size_t)texture_count, sizeof(uint8_t));
    texture_samplers =
        (gltf_sampler_info_t *)calloc((size_t)texture_count, sizeof(*texture_samplers));
    if (!texture_images || !texture_supported || !texture_samplers) {
        free(texture_images);
        free(texture_supported);
        free(texture_samplers);
        return 0;
    }
    *out_texture_images = texture_images;
    *out_texture_supported = texture_supported;
    *out_texture_samplers = texture_samplers;
    return 1;
}

static int gltf_image_json_supported_for_texture(void *image_json) {
    const char *image_uri = jstr(image_json, "uri");
    const char *image_mime = jstr(image_json, "mimeType");
    char parsed_mime[64];
    const char *image_type;
    parsed_mime[0] = '\0';
    if (image_uri && strncmp(image_uri, "data:", 5) == 0)
        gltf_data_uri_copy_mime(image_uri, parsed_mime, sizeof(parsed_mime));
    image_type = image_mime ? image_mime : (parsed_mime[0] != '\0' ? parsed_mime : image_uri);
    return gltf_preload_image_is_supported_format(image_type) || gltf_image_is_ktx2(image_type);
}

static void gltf_populate_texture_tables(void *root,
                                         void *images_arr,
                                         int image_count,
                                         void **images,
                                         void *textures_arr,
                                         int texture_count,
                                         void **texture_images,
                                         uint8_t *texture_supported,
                                         gltf_sampler_info_t *texture_samplers) {
    void *samplers_arr = jarr(root, "samplers");
    for (int i = 0; i < texture_count && texture_images; i++) {
        void *texture_json = rt_seq_get(textures_arr, (int64_t)i);
        int64_t source_idx = gltf_texture_source_index(texture_json);
        int64_t sampler_idx = jint(texture_json, "sampler", -1);
        if (texture_samplers) {
            void *sampler_json = sampler_idx >= 0 && sampler_idx < jarr_len(samplers_arr)
                                     ? rt_seq_get(samplers_arr, sampler_idx)
                                     : NULL;
            gltf_read_sampler_info(sampler_json, &texture_samplers[i]);
        }
        if (source_idx >= 0 && source_idx < image_count) {
            void *image_json = rt_seq_get(images_arr, source_idx);
            if (texture_supported && gltf_image_json_supported_for_texture(image_json))
                texture_supported[i] = 1u;
            texture_images[i] = images[source_idx];
        }
    }
}

/// @brief Decode glTF images and resolve textures (sampler info + image binding)
///        into the parallel texture tables consumed by material resolution.
///        Extracted post-asset phase of rt_gltf_load_impl; never early-returns.
/// @note All seven out-params (images/image_count/image_required and the three
///       texture tables + texture_count) are heap state owned by the caller's
///       cleanup. load_failed_io is set when a required image fails to decode.
static void gltf_load_images_and_textures(void *root,
                                          const char *filepath,
                                          int load_assets,
                                          rt_gltf_preload_bundle *preload_bundle,
                                          gltf_buffer_t *buffers,
                                          int buf_count,
                                          void ***out_images,
                                          int *out_image_count,
                                          uint8_t **out_image_required,
                                          void ***out_texture_images,
                                          uint8_t **out_texture_supported,
                                          gltf_sampler_info_t **out_texture_samplers,
                                          int *out_texture_count,
                                          int *load_failed_io) {
    int load_failed = *load_failed_io;
    void **images = NULL;
    void **texture_images = NULL;
    uint8_t *texture_supported = NULL;
    gltf_sampler_info_t *texture_samplers = NULL;
    void *images_arr = jarr(root, "images");
    int image_count = 0;
    void *textures_arr = jarr(root, "textures");
    int texture_count = 0;
    uint8_t *image_required = NULL;
    if (!gltf_prepare_image_arrays(images_arr,
                                   textures_arr,
                                   &image_count,
                                   &texture_count,
                                   &images,
                                   &image_required)) {
        load_failed = 1;
        goto finish;
    }

    for (int i = 0; i < image_count && images; i++) {
        if (!gltf_load_image_at_index(root,
                                      filepath,
                                      load_assets,
                                      preload_bundle,
                                      buffers,
                                      buf_count,
                                      images,
                                      images_arr,
                                      image_required,
                                      i)) {
            load_failed = 1;
            break;
        }
    }
    if (load_failed)
        goto finish;

    if (!gltf_alloc_texture_tables(
            texture_count, &texture_images, &texture_supported, &texture_samplers)) {
        load_failed = 1;
        goto finish;
    }
    gltf_populate_texture_tables(root,
                                 images_arr,
                                 image_count,
                                 images,
                                 textures_arr,
                                 texture_count,
                                 texture_images,
                                 texture_supported,
                                 texture_samplers);
finish:
    *out_images = images;
    *out_image_count = image_count;
    *out_image_required = image_required;
    *out_texture_images = texture_images;
    *out_texture_supported = texture_supported;
    *out_texture_samplers = texture_samplers;
    *out_texture_count = texture_count;
    *load_failed_io = load_failed;
}

typedef void (*gltf_material_texture_setter_fn)(void *material, void *texture);

static gltf_texture_info_t *gltf_material_slot_info(gltf_material_info_t *material_infos,
                                                    int32_t material_index,
                                                    int32_t slot) {
    if (!material_infos || material_index < 0 || slot < 0 ||
        slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return NULL;
    return &material_infos[material_index].slots[slot];
}

static int gltf_material_bind_texture(void *texture_json,
                                      void *material,
                                      int32_t slot,
                                      void **texture_images,
                                      const uint8_t *texture_supported,
                                      const gltf_sampler_info_t *texture_samplers,
                                      int32_t texture_count,
                                      gltf_texture_info_t *slot_info,
                                      gltf_material_texture_setter_fn setter) {
    int64_t tex_idx = jint(texture_json, "index", -1);
    if (texture_json && slot_info)
        gltf_read_texture_info(texture_json, slot_info);
    if (tex_idx >= 0 && tex_idx < texture_count && texture_images && texture_images[tex_idx]) {
        if (setter)
            setter(material, texture_images[tex_idx]);
    } else if (gltf_texture_index_missing_supported_payload(
                   tex_idx, texture_count, texture_images, texture_supported)) {
        return 1;
    }
    gltf_apply_texture_slot(texture_samplers, texture_count, tex_idx, material, slot, slot_info);
    return 0;
}

static void gltf_material_read_pbr_base_color(void *pbr, double rgba[4]) {
    void *bcf = jarr(pbr, "baseColorFactor");
    rgba[0] = 1.0;
    rgba[1] = 1.0;
    rgba[2] = 1.0;
    rgba[3] = 1.0;
    if (bcf && jarr_len(bcf) >= 3) {
        rgba[0] = gltf_clamp_double(jvalue_num(rt_seq_get(bcf, 0), rgba[0]), 0.0, 1.0, rgba[0]);
        rgba[1] = gltf_clamp_double(jvalue_num(rt_seq_get(bcf, 1), rgba[1]), 0.0, 1.0, rgba[1]);
        rgba[2] = gltf_clamp_double(jvalue_num(rt_seq_get(bcf, 2), rgba[2]), 0.0, 1.0, rgba[2]);
        if (jarr_len(bcf) >= 4)
            rgba[3] =
                gltf_clamp_double(jvalue_num(rt_seq_get(bcf, 3), rgba[3]), 0.0, 1.0, rgba[3]);
    }
}

static void *gltf_material_create_pbr_from_json(void *pbr) {
    double rgba[4];
    double metallic;
    double roughness;
    void *mat;
    if (!pbr)
        return NULL;
    gltf_material_read_pbr_base_color(pbr, rgba);
    metallic = gltf_clamp_double(jnum(pbr, "metallicFactor", 1.0), 0.0, 1.0, 1.0);
    roughness = gltf_clamp_double(jnum(pbr, "roughnessFactor", 1.0), 0.0, 1.0, 1.0);
    mat = rt_material3d_new_pbr(rgba[0], rgba[1], rgba[2]);
    if (mat) {
        rt_material3d_set_metallic(mat, metallic);
        rt_material3d_set_roughness(mat, roughness);
        if (rgba[3] < 1.0)
            rt_material3d_set_alpha(mat, rgba[3]);
    }
    return mat;
}

static void *gltf_material_create_default_pbr(void) {
    void *mat = rt_material3d_new_pbr(1.0, 1.0, 1.0);
    if (mat) {
        rt_material3d_set_metallic(mat, 1.0);
        rt_material3d_set_roughness(mat, 1.0);
    }
    return mat;
}

static int gltf_material_apply_pbr_textures(void *pbr,
                                            void *material,
                                            int32_t material_index,
                                            void **texture_images,
                                            const uint8_t *texture_supported,
                                            const gltf_sampler_info_t *texture_samplers,
                                            int32_t texture_count,
                                            gltf_material_info_t *material_infos) {
    int load_failed = 0;
    void *base_tex = jget(pbr, "baseColorTexture");
    void *mr_tex = jget(pbr, "metallicRoughnessTexture");
    load_failed |= gltf_material_bind_texture(
        base_tex,
        material,
        RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count,
        gltf_material_slot_info(material_infos, material_index, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR),
        rt_material3d_set_texture);
    load_failed |= gltf_material_bind_texture(
        mr_tex,
        material,
        RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count,
        gltf_material_slot_info(
            material_infos, material_index, RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS),
        rt_material3d_set_metallic_roughness_map);
    return load_failed;
}

static void gltf_material_apply_emissive_factor(void *mat_json, void *material) {
    void *ef = jarr(mat_json, "emissiveFactor");
    if (ef && jarr_len(ef) >= 3) {
        double er = gltf_clamp_double(jvalue_num(rt_seq_get(ef, 0), 0.0), 0.0, 1.0, 0.0);
        double eg = gltf_clamp_double(jvalue_num(rt_seq_get(ef, 1), 0.0), 0.0, 1.0, 0.0);
        double eb = gltf_clamp_double(jvalue_num(rt_seq_get(ef, 2), 0.0), 0.0, 1.0, 0.0);
        rt_material3d_set_emissive_color(material, er, eg, eb);
    }
}

static int gltf_material_apply_specular_extension(void *specular,
                                                  void *material,
                                                  int32_t material_index,
                                                  void **texture_images,
                                                  const uint8_t *texture_supported,
                                                  const gltf_sampler_info_t *texture_samplers,
                                                  int32_t texture_count,
                                                  gltf_material_info_t *material_infos) {
    void *spec_color;
    void *spec_tex;
    void *spec_color_tex;
    void *chosen_spec_tex;
    if (!specular || !material)
        return 0;
    spec_color = jarr(specular, "specularColorFactor");
    spec_tex = jget(specular, "specularTexture");
    spec_color_tex = jget(specular, "specularColorTexture");
    chosen_spec_tex = spec_color_tex ? spec_color_tex : spec_tex;
    ((rt_material3d *)material)->specular[0] =
        gltf_clamp_double(jnum(specular, "specularFactor", ((rt_material3d *)material)->specular[0]),
                          0.0,
                          1.0,
                          ((rt_material3d *)material)->specular[0]);
    ((rt_material3d *)material)->specular[1] = ((rt_material3d *)material)->specular[0];
    ((rt_material3d *)material)->specular[2] = ((rt_material3d *)material)->specular[0];
    if (spec_color && jarr_len(spec_color) >= 3) {
        ((rt_material3d *)material)->specular[0] = gltf_clamp_double(
            jvalue_num(rt_seq_get(spec_color, 0), ((rt_material3d *)material)->specular[0]),
            0.0,
            1.0,
            ((rt_material3d *)material)->specular[0]);
        ((rt_material3d *)material)->specular[1] = gltf_clamp_double(
            jvalue_num(rt_seq_get(spec_color, 1), ((rt_material3d *)material)->specular[1]),
            0.0,
            1.0,
            ((rt_material3d *)material)->specular[1]);
        ((rt_material3d *)material)->specular[2] = gltf_clamp_double(
            jvalue_num(rt_seq_get(spec_color, 2), ((rt_material3d *)material)->specular[2]),
            0.0,
            1.0,
            ((rt_material3d *)material)->specular[2]);
    }
    return gltf_material_bind_texture(
        chosen_spec_tex,
        material,
        RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count,
        gltf_material_slot_info(material_infos, material_index, RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR),
        rt_material3d_set_specular_map);
}

static int gltf_material_validate_extension_texture(void *texture_json,
                                                    void *material,
                                                    void **texture_images,
                                                    const uint8_t *texture_supported,
                                                    const gltf_sampler_info_t *texture_samplers,
                                                    int32_t texture_count) {
    return gltf_material_bind_texture(texture_json,
                                      material,
                                      -1,
                                      texture_images,
                                      texture_supported,
                                      texture_samplers,
                                      texture_count,
                                      NULL,
                                      NULL);
}

static int gltf_material_apply_clearcoat_extension(void *clearcoat,
                                                   void *material,
                                                   void **texture_images,
                                                   const uint8_t *texture_supported,
                                                   const gltf_sampler_info_t *texture_samplers,
                                                   int32_t texture_count) {
    double clearcoat_factor;
    double clearcoat_roughness;
    int load_failed = 0;
    if (!clearcoat || !material)
        return 0;
    clearcoat_factor =
        gltf_clamp_double(jnum(clearcoat, "clearcoatFactor", 0.0), 0.0, 1.0, 0.0);
    clearcoat_roughness = gltf_clamp_double(
        jnum(clearcoat, "clearcoatRoughnessFactor", 0.0), 0.0, 1.0, 0.0);
    if (clearcoat_factor > ((rt_material3d *)material)->reflectivity)
        rt_material3d_set_reflectivity(material, clearcoat_factor);
    rt_material3d_set_custom_param(material, 1, clearcoat_factor);
    rt_material3d_set_custom_param(material, 2, clearcoat_roughness);
    load_failed |= gltf_material_validate_extension_texture(jget(clearcoat, "clearcoatTexture"),
                                                            material,
                                                            texture_images,
                                                            texture_supported,
                                                            texture_samplers,
                                                            texture_count);
    load_failed |= gltf_material_validate_extension_texture(
        jget(clearcoat, "clearcoatRoughnessTexture"),
        material,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count);
    load_failed |= gltf_material_validate_extension_texture(jget(clearcoat, "clearcoatNormalTexture"),
                                                            material,
                                                            texture_images,
                                                            texture_supported,
                                                            texture_samplers,
                                                            texture_count);
    return load_failed;
}

static int gltf_material_apply_transmission_extension(void *transmission,
                                                      void *material,
                                                      void **texture_images,
                                                      const uint8_t *texture_supported,
                                                      const gltf_sampler_info_t *texture_samplers,
                                                      int32_t texture_count) {
    double transmission_factor;
    if (!transmission || !material)
        return 0;
    transmission_factor =
        gltf_clamp_double(jnum(transmission, "transmissionFactor", 0.0), 0.0, 1.0, 0.0);
    if (transmission_factor > 0.0) {
        if (transmission_factor > ((rt_material3d *)material)->reflectivity)
            rt_material3d_set_reflectivity(material, transmission_factor);
        rt_material3d_set_custom_param(material, 3, transmission_factor);
    }
    return gltf_material_validate_extension_texture(jget(transmission, "transmissionTexture"),
                                                    material,
                                                    texture_images,
                                                    texture_supported,
                                                    texture_samplers,
                                                    texture_count);
}

static int gltf_material_apply_extensions(void *mat_json,
                                          void *material,
                                          int32_t material_index,
                                          void **texture_images,
                                          const uint8_t *texture_supported,
                                          const gltf_sampler_info_t *texture_samplers,
                                          int32_t texture_count,
                                          gltf_material_info_t *material_infos) {
    void *extensions = jget(mat_json, "extensions");
    void *emissive_strength = extensions ? jget(extensions, "KHR_materials_emissive_strength") : NULL;
    void *unlit = extensions ? jget(extensions, "KHR_materials_unlit") : NULL;
    void *specular = extensions ? jget(extensions, "KHR_materials_specular") : NULL;
    void *clearcoat = extensions ? jget(extensions, "KHR_materials_clearcoat") : NULL;
    void *transmission = extensions ? jget(extensions, "KHR_materials_transmission") : NULL;
    int load_failed = 0;
    if (emissive_strength)
        rt_material3d_set_emissive_intensity(
            material,
            gltf_clamp_double(jnum(emissive_strength, "emissiveStrength", 1.0), 0.0, DBL_MAX, 1.0));
    if (unlit) {
        rt_material3d_set_unlit(material, 1);
        rt_material3d_set_shading_model(material, 3);
    }
    load_failed |= gltf_material_apply_specular_extension(specular,
                                                          material,
                                                          material_index,
                                                          texture_images,
                                                          texture_supported,
                                                          texture_samplers,
                                                          texture_count,
                                                          material_infos);
    load_failed |= gltf_material_apply_clearcoat_extension(clearcoat,
                                                           material,
                                                           texture_images,
                                                           texture_supported,
                                                           texture_samplers,
                                                           texture_count);
    load_failed |= gltf_material_apply_transmission_extension(transmission,
                                                              material,
                                                              texture_images,
                                                              texture_supported,
                                                              texture_samplers,
                                                              texture_count);
    return load_failed;
}

static int gltf_material_apply_standard_textures(void *mat_json,
                                                 void *material,
                                                 int32_t material_index,
                                                 void **texture_images,
                                                 const uint8_t *texture_supported,
                                                 const gltf_sampler_info_t *texture_samplers,
                                                 int32_t texture_count,
                                                 gltf_material_info_t *material_infos) {
    int load_failed = 0;
    void *normal_tex = jget(mat_json, "normalTexture");
    void *occlusion_tex = jget(mat_json, "occlusionTexture");
    void *emissive_tex = jget(mat_json, "emissiveTexture");
    load_failed |= gltf_material_bind_texture(
        normal_tex,
        material,
        RT_MATERIAL3D_TEXTURE_SLOT_NORMAL,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count,
        gltf_material_slot_info(material_infos, material_index, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL),
        rt_material3d_set_normal_map);
    if (normal_tex)
        rt_material3d_set_normal_scale(material, gltf_finite_or(jnum(normal_tex, "scale", 1.0), 1.0));

    load_failed |= gltf_material_bind_texture(
        occlusion_tex,
        material,
        RT_MATERIAL3D_TEXTURE_SLOT_AO,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count,
        gltf_material_slot_info(material_infos, material_index, RT_MATERIAL3D_TEXTURE_SLOT_AO),
        rt_material3d_set_ao_map);
    if (occlusion_tex)
        rt_material3d_set_ao(
            material, gltf_clamp_double(jnum(occlusion_tex, "strength", 1.0), 0.0, 1.0, 1.0));

    load_failed |= gltf_material_bind_texture(
        emissive_tex,
        material,
        RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE,
        texture_images,
        texture_supported,
        texture_samplers,
        texture_count,
        gltf_material_slot_info(material_infos, material_index, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE),
        rt_material3d_set_emissive_map);
    return load_failed;
}

static void gltf_material_apply_alpha_and_sided(void *mat_json, void *material) {
    const char *alpha_mode = jstr(mat_json, "alphaMode");
    if (alpha_mode && strcmp(alpha_mode, "MASK") == 0) {
        rt_material3d_set_alpha_mode(material, RT_MATERIAL3D_ALPHA_MODE_MASK);
        ((rt_material3d *)material)->alpha_cutoff =
            gltf_clamp_double(jnum(mat_json, "alphaCutoff", 0.5), 0.0, 1.0, 0.5);
    } else if (alpha_mode && strcmp(alpha_mode, "BLEND") == 0) {
        rt_material3d_set_alpha_mode(material, RT_MATERIAL3D_ALPHA_MODE_BLEND);
    } else {
        rt_material3d_set_alpha_mode(material, RT_MATERIAL3D_ALPHA_MODE_OPAQUE);
    }
    rt_material3d_set_double_sided(material, jint(mat_json, "doubleSided", 0) ? 1 : 0);
}

/// @brief Resolve glTF materials (PBR factors, KHR material extensions, and the
///        normal/occlusion/emissive/specular texture slots) into Material3D
///        objects on the asset. Extracted post-asset phase of rt_gltf_load_impl;
///        never early-returns.
/// @note out_material_infos returns the per-material texture-slot metadata table
///       (heap, owned by the caller's cleanup). load_failed_io is read on entry
///       and set when a referenced texture has a missing-but-required payload.
static void gltf_load_materials(rt_gltf_asset *asset,
                                void *root,
                                void **texture_images,
                                uint8_t *texture_supported,
                                gltf_sampler_info_t *texture_samplers,
                                int texture_count,
                                gltf_material_info_t **out_material_infos,
                                int *load_failed_io) {
    int load_failed = *load_failed_io;
    gltf_material_info_t *material_infos = NULL;
    // Extract materials
    void *mats_arr = jarr(root, "materials");
    int mat_count = 0;
    int material_capacity = 1;
    if (!gltf_jarr_len_i32(mats_arr, &mat_count) || mat_count > INT32_MAX - 1) {
        load_failed = 1;
        goto finish;
    }
    material_capacity = mat_count > 0 ? mat_count + 1 : 1;
    asset->materials = (void **)calloc((size_t)material_capacity, sizeof(void *));
    asset->material_capacity = asset->materials ? material_capacity : 0;
    material_infos =
        (gltf_material_info_t *)calloc((size_t)material_capacity, sizeof(gltf_material_info_t));
    if (!asset->materials || !material_infos) {
        asset->material_capacity = 0;
        load_failed = 1;
        goto finish;
    }
    if (material_infos) {
        for (int i = 0; i < material_capacity; i++)
            for (int slot = 0; slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; slot++)
                gltf_texture_info_init(&material_infos[i].slots[slot]);
    }
    if (mat_count > 0 && asset->materials) {
        for (int i = 0; i < mat_count && asset->materials; i++) {
            void *mat_json = rt_seq_get(mats_arr, (int64_t)i);
            void *mat = NULL;
            void *pbr = jget(mat_json, "pbrMetallicRoughness");

            if (pbr) {
                mat = gltf_material_create_pbr_from_json(pbr);
                load_failed |= gltf_material_apply_pbr_textures(pbr,
                                                                mat,
                                                                i,
                                                                texture_images,
                                                                texture_supported,
                                                                texture_samplers,
                                                                texture_count,
                                                                material_infos);
            }
            if (!mat)
                mat = gltf_material_create_default_pbr();
            if (!mat)
                continue;

            gltf_material_apply_emissive_factor(mat_json, mat);
            load_failed |= gltf_material_apply_extensions(mat_json,
                                                          mat,
                                                          i,
                                                          texture_images,
                                                          texture_supported,
                                                          texture_samplers,
                                                          texture_count,
                                                          material_infos);
            load_failed |= gltf_material_apply_standard_textures(mat_json,
                                                                 mat,
                                                                 i,
                                                                 texture_images,
                                                                 texture_supported,
                                                                 texture_samplers,
                                                                 texture_count,
                                                                 material_infos);
            gltf_material_apply_alpha_and_sided(mat_json, mat);

            if (load_failed) {
                gltf_release_local(mat);
                break;
            }
            asset->materials[i] = mat;
            asset->material_count = i + 1;
        }
    }
finish:
    *out_material_infos = material_infos;
    *load_failed_io = load_failed;
}

/// @brief Append one imported vertex to @p mesh during glTF import, taking position, normal,
///   UV0/UV1, color, and tangent (any pointer may be NULL to use that attribute's default).
/// @return 1 on success, 0 if the mesh vertex storage cannot grow.
static int gltf_mesh_append_import_vertex(rt_mesh3d *mesh,
                                          const float *pos,
                                          const float *nrm,
                                          const float *uv0,
                                          const float *uv1,
                                          const float *color,
                                          const float *tangent) {
    vgfx3d_vertex_t *vertex;
    if (!mesh || mesh->build_failed || !pos || !nrm || !uv0 || !uv1 || !color || !tangent)
        return 0;
    if (mesh->vertex_count >= mesh->vertex_capacity || mesh->vertex_count == UINT32_MAX)
        return 0;
    vertex = &mesh->vertices[mesh->vertex_count++];
    memset(vertex, 0, sizeof(*vertex));
    vertex->pos[0] = pos[0];
    vertex->pos[1] = pos[1];
    vertex->pos[2] = pos[2];
    if (mesh->positions64) {
        mesh->positions64[(size_t)(mesh->vertex_count - 1u) * 3u + 0] = (double)pos[0];
        mesh->positions64[(size_t)(mesh->vertex_count - 1u) * 3u + 1] = (double)pos[1];
        mesh->positions64[(size_t)(mesh->vertex_count - 1u) * 3u + 2] = (double)pos[2];
    }
    vertex->normal[0] = nrm[0];
    vertex->normal[1] = nrm[1];
    vertex->normal[2] = nrm[2];
    vertex->uv[0] = uv0[0];
    vertex->uv[1] = uv0[1];
    vertex->uv1[0] = uv1[0];
    vertex->uv1[1] = uv1[1];
    memcpy(vertex->color, color, sizeof(vertex->color));
    memcpy(vertex->tangent, tangent, sizeof(vertex->tangent));
    return 1;
}

typedef struct {
    gltf_accessor_view_t pos;
    gltf_accessor_view_t norm;
    gltf_accessor_view_t uv0;
    gltf_accessor_view_t uv1;
    gltf_accessor_view_t color;
    gltf_accessor_view_t tangent;
    gltf_accessor_view_t joints;
    gltf_accessor_view_t weights;
    gltf_accessor_view_t joints1;
    gltf_accessor_view_t weights1;
    gltf_accessor_view_t idx;
    int has_normals;
    int has_uv0;
    int has_uv1;
    int has_colors;
    int has_tangents;
    int has_joints;
    int has_weights;
    int has_joints1;
    int has_weights1;
    int has_indices;
    int has_skin0;
    int has_skin1;
    int32_t triangle_estimate;
} gltf_mesh_primitive_views_t;

static void *gltf_mesh_resolve_primitive_material(rt_gltf_asset *asset,
                                                  int64_t material_idx,
                                                  int32_t material_capacity,
                                                  void **default_material_io) {
    void *prim_material = NULL;
    if (!asset)
        return NULL;
    if (material_idx >= 0 && material_idx < asset->material_count)
        prim_material = asset->materials[material_idx];
    if (!prim_material && asset->materials) {
        if (default_material_io && !*default_material_io &&
            asset->material_count < material_capacity) {
            *default_material_io = gltf_material_create_default_pbr();
            if (*default_material_io)
                asset->materials[asset->material_count++] = *default_material_io;
        }
        prim_material = default_material_io ? *default_material_io : NULL;
    }
    return prim_material;
}

static int gltf_mesh_take_preloaded_primitive(rt_gltf_asset *asset,
                                              rt_gltf_preload_bundle *preload_bundle,
                                              int32_t mesh_json_index,
                                              int32_t primitive_index,
                                              void *primitive_material,
                                              void **primitive_materials,
                                              int32_t *mesh_idx_io) {
    uint32_t decoded_mesh_flags = 0u;
    void *decoded_mesh =
        gltf_preload_take_decoded_mesh(preload_bundle, mesh_json_index, primitive_index, &decoded_mesh_flags);
    if (!decoded_mesh)
        return 0;
    if ((decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_NORMALS) == 0)
        rt_mesh3d_recalc_normals(decoded_mesh);
    if ((decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_TANGENTS) == 0 &&
        (decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_UV0) != 0) {
        rt_material3d *tangent_material = (rt_material3d *)primitive_material;
        if (tangent_material && tangent_material->normal_map)
            rt_mesh3d_calc_tangents(decoded_mesh);
    }
    asset->meshes[*mesh_idx_io] = decoded_mesh;
    (*mesh_idx_io)++;
    asset->mesh_count = *mesh_idx_io;
    if (primitive_materials)
        primitive_materials[*mesh_idx_io - 1] = primitive_material;
    return 1;
}

static int gltf_mesh_validate_skin_views(gltf_mesh_primitive_views_t *views,
                                         int32_t vertex_count) {
    if (!views)
        return 0;
    if ((views->has_joints != views->has_weights) || (views->has_joints1 != views->has_weights1))
        return 0;
    if (views->has_joints &&
        (!gltf_accessor_has_payload(&views->joints) || !gltf_accessor_has_payload(&views->weights) ||
         !gltf_accessor_is_joints(&views->joints) || !gltf_accessor_is_weights(&views->weights) ||
         views->joints.count < vertex_count || views->weights.count < vertex_count))
        return 0;
    if (views->has_joints1 &&
        (!gltf_accessor_has_payload(&views->joints1) ||
         !gltf_accessor_has_payload(&views->weights1) ||
         !gltf_accessor_is_joints(&views->joints1) ||
         !gltf_accessor_is_weights(&views->weights1) || views->joints1.count < vertex_count ||
         views->weights1.count < vertex_count))
        return 0;
    return 1;
}

static int gltf_mesh_read_primitive_views(void *root,
                                          gltf_buffer_t *buffers,
                                          int32_t buf_count,
                                          void *attrs,
                                          void *prim,
                                          int64_t mode,
                                          gltf_mesh_primitive_views_t *views,
                                          int *load_failed_io) {
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
    int32_t draw_count;
    if (!views)
        return 0;
    memset(views, 0, sizeof(*views));
    if (pos_acc < 0)
        return 0;

    views->has_normals = gltf_get_accessor_view(root, norm_acc, buffers, buf_count, &views->norm);
    views->has_uv0 = gltf_get_accessor_view(root, uv0_acc, buffers, buf_count, &views->uv0);
    views->has_uv1 = gltf_get_accessor_view(root, uv1_acc, buffers, buf_count, &views->uv1);
    views->has_colors = gltf_get_accessor_view(root, color_acc, buffers, buf_count, &views->color);
    views->has_tangents =
        gltf_get_accessor_view(root, tangent_acc, buffers, buf_count, &views->tangent);
    views->has_joints = gltf_get_accessor_view(root, joints_acc, buffers, buf_count, &views->joints);
    views->has_weights =
        gltf_get_accessor_view(root, weights_acc, buffers, buf_count, &views->weights);
    views->has_joints1 =
        gltf_get_accessor_view(root, joints1_acc, buffers, buf_count, &views->joints1);
    views->has_weights1 =
        gltf_get_accessor_view(root, weights1_acc, buffers, buf_count, &views->weights1);
    views->has_indices = gltf_get_accessor_view(root, idx_acc, buffers, buf_count, &views->idx);

    if (!gltf_get_accessor_view(root, pos_acc, buffers, buf_count, &views->pos) ||
        views->pos.count == 0 || !gltf_accessor_has_payload(&views->pos) ||
        !gltf_accessor_is_f32_components(&views->pos, 3, 3)) {
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    if (views->has_normals &&
        (!gltf_accessor_has_payload(&views->norm) ||
         !gltf_accessor_is_f32_components(&views->norm, 3, 3) ||
         views->norm.count < views->pos.count))
        views->has_normals = 0;
    if (views->has_uv0 &&
        (!gltf_accessor_has_payload(&views->uv0) || !gltf_accessor_is_texcoord(&views->uv0) ||
         views->uv0.count < views->pos.count))
        views->has_uv0 = 0;
    if (views->has_uv1 &&
        (!gltf_accessor_has_payload(&views->uv1) || !gltf_accessor_is_texcoord(&views->uv1) ||
         views->uv1.count < views->pos.count))
        views->has_uv1 = 0;
    if (views->has_colors &&
        (!gltf_accessor_has_payload(&views->color) || !gltf_accessor_is_color(&views->color) ||
         views->color.count < views->pos.count))
        views->has_colors = 0;
    if (views->has_tangents &&
        (!gltf_accessor_has_payload(&views->tangent) ||
         !gltf_accessor_is_f32_components(&views->tangent, 4, 4) ||
         views->tangent.count < views->pos.count))
        views->has_tangents = 0;
    if (views->has_indices &&
        (!gltf_accessor_has_payload(&views->idx) || !gltf_accessor_is_indices(&views->idx))) {
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    if (!gltf_mesh_validate_skin_views(views, views->pos.count)) {
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }

    draw_count = views->has_indices ? views->idx.count : views->pos.count;
    views->triangle_estimate = gltf_primitive_triangle_count(mode, draw_count);
    if (views->triangle_estimate <= 0)
        return 0;
    views->has_skin0 = views->has_joints && views->has_weights;
    views->has_skin1 = views->has_joints1 && views->has_weights1;
    return 1;
}

typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
    float uv1[2];
    float color[4];
    float tangent[4];
    float weights[4];
    uint32_t joints[4];
    float weights1[4];
    uint32_t joints1[4];
} gltf_mesh_import_vertex_t;

static void gltf_mesh_import_vertex_init(gltf_mesh_import_vertex_t *vertex) {
    memset(vertex, 0, sizeof(*vertex));
    vertex->color[0] = 1.0f;
    vertex->color[1] = 1.0f;
    vertex->color[2] = 1.0f;
    vertex->color[3] = 1.0f;
    vertex->tangent[3] = 1.0f;
}

static int gltf_mesh_import_vertex_is_finite(const gltf_mesh_primitive_views_t *views,
                                             const gltf_mesh_import_vertex_t *vertex) {
    return gltf_f32_array_is_finite(vertex->pos, 3) &&
           gltf_f32_array_is_finite(vertex->nrm, 3) &&
           gltf_f32_array_is_finite(vertex->uv, 2) &&
           gltf_f32_array_is_finite(vertex->uv1, 2) &&
           gltf_f32_array_is_finite(vertex->color, 4) &&
           gltf_f32_array_is_finite(vertex->tangent, 4) &&
           (!views->has_skin0 || gltf_f32_array_is_finite(vertex->weights, 4)) &&
           (!views->has_skin1 || gltf_f32_array_is_finite(vertex->weights1, 4));
}

static void gltf_mesh_merge_extra_joint_influences(gltf_mesh_import_vertex_t *vertex,
                                                   int has_primary_skin) {
    uint32_t merged_joints[4] = {0u, 0u, 0u, 0u};
    float merged_weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (has_primary_skin) {
        for (int j = 0; j < 4; j++)
            gltf_add_top_joint_influence(
                vertex->joints[j], vertex->weights[j], merged_joints, merged_weights);
    }
    for (int j = 0; j < 4; j++)
        gltf_add_top_joint_influence(
            vertex->joints1[j], vertex->weights1[j], merged_joints, merged_weights);
    gltf_normalize_joint_influences(merged_weights);
    memcpy(vertex->joints, merged_joints, sizeof(vertex->joints));
    memcpy(vertex->weights, merged_weights, sizeof(vertex->weights));
}

static void gltf_mesh_clip_joint_influences(gltf_mesh_import_vertex_t *vertex) {
    for (int j = 0; j < 4; j++) {
        if (vertex->joints[j] >= VGFX3D_MAX_BONES) {
            vertex->joints[j] = 0u;
            vertex->weights[j] = 0.0f;
        }
    }
}

static int gltf_mesh_read_import_vertex(const gltf_mesh_primitive_views_t *views,
                                        int32_t vertex_index,
                                        gltf_mesh_import_vertex_t *vertex,
                                        int *invalid_authored_normals_io,
                                        int *invalid_authored_tangents_io) {
    gltf_mesh_import_vertex_init(vertex);
    gltf_accessor_read_f32(&views->pos, vertex_index, vertex->pos, 3);
    if (views->has_normals)
        gltf_accessor_read_f32(&views->norm, vertex_index, vertex->nrm, 3);
    if (views->has_uv0)
        gltf_accessor_read_f32(&views->uv0, vertex_index, vertex->uv, 2);
    if (views->has_uv1)
        gltf_accessor_read_f32(&views->uv1, vertex_index, vertex->uv1, 2);
    else {
        vertex->uv1[0] = vertex->uv[0];
        vertex->uv1[1] = vertex->uv[1];
    }
    if (views->has_colors) {
        gltf_accessor_read_f32(&views->color, vertex_index, vertex->color, 4);
        if (views->color.comp_count < 4)
            vertex->color[3] = 1.0f;
    }
    if (views->has_tangents) {
        gltf_accessor_read_f32(&views->tangent, vertex_index, vertex->tangent, 4);
        if (views->tangent.comp_count < 4)
            vertex->tangent[3] = 1.0f;
    }
    if (views->has_joints)
        gltf_accessor_read_u32(&views->joints, vertex_index, vertex->joints, 4);
    if (views->has_weights)
        gltf_accessor_read_f32(&views->weights, vertex_index, vertex->weights, 4);
    if (views->has_joints1)
        gltf_accessor_read_u32(&views->joints1, vertex_index, vertex->joints1, 4);
    if (views->has_weights1)
        gltf_accessor_read_f32(&views->weights1, vertex_index, vertex->weights1, 4);
    if (!gltf_mesh_import_vertex_is_finite(views, vertex))
        return 0;
    if (views->has_normals && !gltf_normalize_vec3f_in_place(vertex->nrm)) {
        if (invalid_authored_normals_io)
            *invalid_authored_normals_io = 1;
        vertex->nrm[0] = 0.0f;
        vertex->nrm[1] = 0.0f;
        vertex->nrm[2] = 0.0f;
    }
    if (views->has_tangents && !gltf_sanitize_tangent4(vertex->tangent)) {
        if (invalid_authored_tangents_io)
            *invalid_authored_tangents_io = 1;
        vertex->tangent[0] = 1.0f;
        vertex->tangent[1] = 0.0f;
        vertex->tangent[2] = 0.0f;
        vertex->tangent[3] = 1.0f;
    }
    if (views->has_skin1)
        gltf_mesh_merge_extra_joint_influences(vertex, views->has_skin0);
    else if (views->has_skin0) {
        gltf_mesh_clip_joint_influences(vertex);
        gltf_normalize_joint_influences(vertex->weights);
    }
    return 1;
}

static void gltf_mesh_apply_vertex_skin(rt_mesh3d *mesh,
                                        int32_t vertex_index,
                                        const gltf_mesh_import_vertex_t *vertex) {
    rt_mesh3d_set_bone_weights(mesh,
                               vertex_index,
                               (int64_t)vertex->joints[0],
                               vertex->weights[0],
                               (int64_t)vertex->joints[1],
                               vertex->weights[1],
                               (int64_t)vertex->joints[2],
                               vertex->weights[2],
                               (int64_t)vertex->joints[3],
                               vertex->weights[3]);
    for (int j = 0; j < 4; j++) {
        if (vertex->weights[j] > 0.0001f && vertex->joints[j] < VGFX3D_MAX_BONES &&
            (int32_t)(vertex->joints[j] + 1u) > mesh->bone_count) {
            mesh->bone_count = (int32_t)(vertex->joints[j] + 1u);
        }
    }
}

static int gltf_mesh_import_vertices(rt_mesh3d *mesh,
                                     const gltf_mesh_primitive_views_t *views,
                                     int *invalid_authored_normals_io,
                                     int *invalid_authored_tangents_io) {
    for (int32_t vi = 0; vi < views->pos.count; vi++) {
        gltf_mesh_import_vertex_t vertex;
        if (!gltf_mesh_read_import_vertex(
                views, vi, &vertex, invalid_authored_normals_io, invalid_authored_tangents_io))
            return 0;
        if (!gltf_mesh_append_import_vertex(
                mesh, vertex.pos, vertex.nrm, vertex.uv, vertex.uv1, vertex.color, vertex.tangent))
            return 0;
        if (views->has_skin0 || views->has_skin1)
            gltf_mesh_apply_vertex_skin(mesh, vi, &vertex);
    }
    return 1;
}

static int gltf_mesh_finalize_imported_primitive(rt_gltf_asset *asset,
                                                 void *root,
                                                 gltf_buffer_t *buffers,
                                                 int32_t buf_count,
                                                 void *mesh_json,
                                                 void *prim,
                                                 void *mesh,
                                                 int64_t mode,
                                                 const gltf_mesh_primitive_views_t *views,
                                                 int64_t material_idx,
                                                 void *primitive_material,
                                                 int invalid_authored_normals,
                                                 int invalid_authored_tangents,
                                                 int *load_failed_io) {
    if (!gltf_append_primitive_indices(
            mesh, mode, views->has_indices ? &views->idx : NULL, views->pos.count)) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    if ((!views->has_normals || invalid_authored_normals) &&
        ((rt_mesh3d *)mesh)->vertex_count > 0)
        rt_mesh3d_recalc_normals(mesh);
    if ((!views->has_tangents || invalid_authored_tangents) && views->has_uv0 &&
        material_idx >= 0 && material_idx < asset->material_count) {
        rt_material3d *tangent_material = (rt_material3d *)primitive_material;
        if (tangent_material && tangent_material->normal_map)
            rt_mesh3d_calc_tangents(mesh);
    }
    gltf_import_primitive_morph_targets(
        root, buffers, buf_count, mesh_json, prim, mesh, views->pos.count);
    return 1;
}

typedef struct {
    rt_gltf_asset *asset;
    void *root;
    gltf_buffer_t *buffers;
    int buf_count;
    rt_gltf_preload_bundle *preload_bundle;
    int material_capacity;
    void **primitive_materials;
    void **default_material_io;
    int *load_failed_io;
} gltf_mesh_load_context_t;

static int gltf_mesh_count_total_primitives(void *meshes_arr,
                                            int mesh_json_count,
                                            int *out_total_prims,
                                            int *load_failed_io) {
    int total_prims = 0;
    for (int i = 0; i < mesh_json_count; i++) {
        void *mesh_json = rt_seq_get(meshes_arr, (int64_t)i);
        void *prims = jarr(mesh_json, "primitives");
        int64_t prim_len = jarr_len(prims);
        if (prim_len > INT32_MAX || total_prims > INT32_MAX - prim_len) {
            if (load_failed_io)
                *load_failed_io = 1;
            return 0;
        }
        total_prims += (int)prim_len;
    }
    *out_total_prims = total_prims;
    return 1;
}

static int gltf_mesh_alloc_output_tables(rt_gltf_asset *asset,
                                         int mesh_json_count,
                                         int total_prims,
                                         int **out_mesh_prim_start,
                                         int **out_mesh_prim_count,
                                         void ***out_primitive_materials,
                                         int *load_failed_io) {
    int *mesh_prim_start = (int *)calloc((size_t)mesh_json_count, sizeof(int));
    int *mesh_prim_count = (int *)calloc((size_t)mesh_json_count, sizeof(int));
    void **primitive_materials = (void **)calloc((size_t)total_prims, sizeof(void *));
    asset->meshes = (void **)calloc((size_t)total_prims, sizeof(void *));
    asset->mesh_capacity = asset->meshes ? total_prims : 0;
    if (!mesh_prim_start || !mesh_prim_count || !primitive_materials || !asset->meshes) {
        free(mesh_prim_start);
        free(mesh_prim_count);
        free(primitive_materials);
        free(asset->meshes);
        asset->meshes = NULL;
        asset->mesh_capacity = 0;
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    *out_mesh_prim_start = mesh_prim_start;
    *out_mesh_prim_count = mesh_prim_count;
    *out_primitive_materials = primitive_materials;
    return 1;
}

static void gltf_mesh_import_primitive(gltf_mesh_load_context_t *ctx,
                                       void *mesh_json,
                                       void *prim,
                                       int mesh_json_index,
                                       int primitive_index,
                                       int *mesh_idx_io) {
    void *attrs = jget(prim, "attributes");
    int64_t material_idx = jint(prim, "material", -1);
    int64_t mode = jint(prim, "mode", 4);
    void *prim_material = NULL;
    gltf_mesh_primitive_views_t views;
    void *mesh;
    int primitive_failed = 0;
    int invalid_authored_normals = 0;
    int invalid_authored_tangents = 0;
    if (!attrs)
        return;
    prim_material = gltf_mesh_resolve_primitive_material(
        ctx->asset, material_idx, ctx->material_capacity, ctx->default_material_io);
    if (gltf_mesh_take_preloaded_primitive(ctx->asset,
                                           ctx->preload_bundle,
                                           mesh_json_index,
                                           primitive_index,
                                           prim_material,
                                           ctx->primitive_materials,
                                           mesh_idx_io))
        return;
    if (!gltf_mesh_read_primitive_views(ctx->root,
                                        ctx->buffers,
                                        ctx->buf_count,
                                        attrs,
                                        prim,
                                        mode,
                                        &views,
                                        ctx->load_failed_io))
        return;

    mesh = rt_mesh3d_new();
    if (!mesh)
        return;
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);
    rt_mesh3d_reserve(mesh, views.pos.count, views.triangle_estimate);
    if (((rt_mesh3d *)mesh)->build_failed) {
        gltf_release_local(mesh);
        if (ctx->load_failed_io)
            *ctx->load_failed_io = 1;
        return;
    }

    primitive_failed = !gltf_mesh_import_vertices((rt_mesh3d *)mesh,
                                                  &views,
                                                  &invalid_authored_normals,
                                                  &invalid_authored_tangents);
    if (primitive_failed) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        gltf_release_local(mesh);
        if (ctx->load_failed_io)
            *ctx->load_failed_io = 1;
        return;
    }

    if (!gltf_mesh_finalize_imported_primitive(ctx->asset,
                                               ctx->root,
                                               ctx->buffers,
                                               ctx->buf_count,
                                               mesh_json,
                                               prim,
                                               mesh,
                                               mode,
                                               &views,
                                               material_idx,
                                               prim_material,
                                               invalid_authored_normals,
                                               invalid_authored_tangents,
                                               ctx->load_failed_io)) {
        gltf_release_local(mesh);
        return;
    }

    ctx->asset->meshes[*mesh_idx_io] = mesh;
    (*mesh_idx_io)++;
    ctx->asset->mesh_count = *mesh_idx_io;
    if (ctx->primitive_materials)
        ctx->primitive_materials[*mesh_idx_io - 1] = prim_material;
}

static void gltf_mesh_import_mesh_primitives(gltf_mesh_load_context_t *ctx,
                                             void *meshes_arr,
                                             int mesh_json_index,
                                             int *mesh_prim_start,
                                             int *mesh_prim_count,
                                             int *mesh_idx_io) {
    void *mesh_json = rt_seq_get(meshes_arr, (int64_t)mesh_json_index);
    void *prims = jarr(mesh_json, "primitives");
    int prim_count = (int)jarr_len(prims);
    int mesh_start = *mesh_idx_io;
    if (mesh_prim_start)
        mesh_prim_start[mesh_json_index] = mesh_start;
    if (mesh_prim_count)
        mesh_prim_count[mesh_json_index] = 0;
    for (int pi = 0; pi < prim_count; pi++)
        gltf_mesh_import_primitive(ctx,
                                   mesh_json,
                                   rt_seq_get(prims, (int64_t)pi),
                                   mesh_json_index,
                                   pi,
                                   mesh_idx_io);
    if (mesh_prim_count)
        mesh_prim_count[mesh_json_index] = *mesh_idx_io - mesh_start;
}

/// @brief Decode glTF meshes/primitives into Mesh3D objects on the asset.
///        Extracted post-asset phase of rt_gltf_load_impl. Malformed primitives set
///        load_failed_io so corrupt assets do not render as partial/empty scenes.
/// @note out_mesh_prim_start/out_mesh_prim_count map a glTF mesh index to its
///       owning asset->meshes range; out_primitive_materials parallels
///       asset->meshes. All three are heap arrays owned by the caller's cleanup.
static void gltf_load_meshes(rt_gltf_asset *asset,
                             void *root,
                             gltf_buffer_t *buffers,
                             int buf_count,
                             rt_gltf_preload_bundle *preload_bundle,
                             int **out_mesh_prim_start,
                             int **out_mesh_prim_count,
                             void ***out_primitive_materials,
                             int *load_failed_io) {
    int *mesh_prim_start = NULL;
    int *mesh_prim_count = NULL;
    void **primitive_materials = NULL;
    void *default_material = NULL;
    int64_t gltf_material_count_raw = jarr_len(jarr(root, "materials"));
    int gltf_material_count =
        gltf_material_count_raw > INT32_MAX ? INT32_MAX : (int)gltf_material_count_raw;
    int material_capacity = 1;
    // Extract meshes
    void *meshes_arr = jarr(root, "meshes");
    int64_t mesh_json_count_raw = jarr_len(meshes_arr);
    int mesh_json_count = mesh_json_count_raw > INT32_MAX ? INT32_MAX : (int)mesh_json_count_raw;
    if (gltf_material_count_raw > INT32_MAX - 1 || mesh_json_count_raw > INT32_MAX) {
        if (load_failed_io)
            *load_failed_io = 1;
        goto finish;
    }
    material_capacity = gltf_material_count > 0 ? gltf_material_count + 1 : 1;
    if (mesh_json_count > 0) {
        int total_prims = 0;
        if (!gltf_mesh_count_total_primitives(
                meshes_arr, mesh_json_count, &total_prims, load_failed_io))
            goto finish;

        if (total_prims > 0) {
            gltf_mesh_load_context_t ctx;
            if (!gltf_mesh_alloc_output_tables(asset,
                                               mesh_json_count,
                                               total_prims,
                                               &mesh_prim_start,
                                               &mesh_prim_count,
                                               &primitive_materials,
                                               load_failed_io))
                goto finish;
            ctx.asset = asset;
            ctx.root = root;
            ctx.buffers = buffers;
            ctx.buf_count = buf_count;
            ctx.preload_bundle = preload_bundle;
            ctx.material_capacity = material_capacity;
            ctx.primitive_materials = primitive_materials;
            ctx.default_material_io = &default_material;
            ctx.load_failed_io = load_failed_io;
            int mesh_idx = 0;
            for (int mi = 0; mi < mesh_json_count && asset->meshes; mi++) {
                gltf_mesh_import_mesh_primitives(
                    &ctx, meshes_arr, mi, mesh_prim_start, mesh_prim_count, &mesh_idx);
            }
        }
    }
finish:
    *out_mesh_prim_start = mesh_prim_start;
    *out_mesh_prim_count = mesh_prim_count;
    *out_primitive_materials = primitive_materials;
}

static void gltf_node_read_transform(void *node_json, rt_scene_node3d *node) {
    void *matrix_arr = jarr(node_json, "matrix");
    if (matrix_arr && jarr_len(matrix_arr) >= 16) {
        double m[16];
        double row_major[16];
        for (int i = 0; i < 16; i++)
            m[i] = jvalue_num(rt_seq_get(matrix_arr, (int64_t)i), i % 5 == 0 ? 1.0 : 0.0);
        gltf_matrix_column_major_to_row_major(m, row_major);
        gltf_matrix_to_trs(row_major, node->position, node->rotation, node->scale_xyz);
    } else {
        void *translation = jarr(node_json, "translation");
        void *rotation = jarr(node_json, "rotation");
        void *scale_arr = jarr(node_json, "scale");
        node->position[0] = gltf_arr_num(translation, 0, 0.0);
        node->position[1] = gltf_arr_num(translation, 1, 0.0);
        node->position[2] = gltf_arr_num(translation, 2, 0.0);
        node->rotation[0] = gltf_arr_num(rotation, 0, 0.0);
        node->rotation[1] = gltf_arr_num(rotation, 1, 0.0);
        node->rotation[2] = gltf_arr_num(rotation, 2, 0.0);
        node->rotation[3] = gltf_arr_num(rotation, 3, 1.0);
        node->scale_xyz[0] = gltf_arr_num(scale_arr, 0, 1.0);
        node->scale_xyz[1] = gltf_arr_num(scale_arr, 1, 1.0);
        node->scale_xyz[2] = gltf_arr_num(scale_arr, 2, 1.0);
    }
    gltf_sanitize_trs(node->position, node->rotation, node->scale_xyz);
    node->world_dirty = 1;
}

static int gltf_node_bind_light(rt_scene_node3d *node,
                                int64_t light_ref,
                                void **imported_lights,
                                int32_t imported_light_count) {
    if (light_ref < 0)
        return 1;
    if (light_ref < imported_light_count && imported_lights && imported_lights[light_ref]) {
        rt_scene_node3d_set_light(node, imported_lights[light_ref]);
        return 1;
    }
    return 0;
}

static void gltf_node_set_primitive_material(rt_scene_node3d *node,
                                             void **primitive_materials,
                                             int32_t primitive_index) {
    if (primitive_materials && primitive_index >= 0 && primitive_materials[primitive_index])
        rt_scene_node3d_set_material(node, primitive_materials[primitive_index]);
}

static int gltf_node_attach_primitive_mesh(rt_gltf_asset *asset,
                                           rt_scene_node3d *node,
                                           int32_t mesh_index,
                                           int64_t skin_ref,
                                           void *weights_arr,
                                           const gltf_skin_t *skins,
                                           int32_t skin_count) {
    void *node_mesh;
    if (!asset || !node || mesh_index < 0 || mesh_index >= asset->mesh_count)
        return 0;
    node_mesh =
        gltf_make_node_mesh_variant(asset, mesh_index, skin_ref, weights_arr, skins, skin_count);
    if (!node_mesh)
        return 0;
    rt_scene_node3d_set_mesh(node, node_mesh);
    if (node_mesh != asset->meshes[mesh_index])
        gltf_release_local(node_mesh);
    return 1;
}

static int gltf_node_attach_extra_primitive(rt_gltf_asset *asset,
                                            rt_scene_node3d *node,
                                            int32_t mesh_index,
                                            int64_t skin_ref,
                                            void *weights_arr,
                                            void **primitive_materials,
                                            const gltf_skin_t *skins,
                                            int32_t skin_count) {
    rt_scene_node3d *prim_node = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!prim_node)
        return 0;
    if (!gltf_node_attach_primitive_mesh(
            asset, prim_node, mesh_index, skin_ref, weights_arr, skins, skin_count)) {
        gltf_release_local(prim_node);
        return 0;
    }
    gltf_node_set_primitive_material(prim_node, primitive_materials, mesh_index);
    if (!rt_scene_node3d_try_add_child(node, prim_node)) {
        gltf_release_local(prim_node);
        return 0;
    }
    gltf_release_local(prim_node);
    return 1;
}

static int gltf_node_attach_mesh_primitives(rt_gltf_asset *asset,
                                            rt_scene_node3d *node,
                                            int64_t mesh_ref,
                                            int64_t skin_ref,
                                            void *weights_arr,
                                            int32_t mesh_json_count,
                                            int *mesh_prim_start,
                                            int *mesh_prim_count,
                                            void **primitive_materials,
                                            const gltf_skin_t *skins,
                                            int32_t skin_count) {
    int32_t prim_start;
    int32_t prim_count;
    if (mesh_ref < 0)
        return 1;
    /* Reject an out-of-range mesh reference unconditionally — including when the
     * whole document declares zero primitives, in which case the prim tables are
     * NULL. This bounds check is what makes GLTF.Load reject a node pointing at a
     * non-existent mesh; gating it behind the prim-table NULL check (below) let
     * such nodes load silently. */
    if (mesh_ref >= mesh_json_count)
        return 0;
    if (!mesh_prim_count || !mesh_prim_start)
        return 1;
    prim_start = mesh_prim_start[mesh_ref];
    prim_count = mesh_prim_count[mesh_ref];
    if (prim_count <= 0)
        return 1;
    if (prim_start < 0 || prim_start > asset->mesh_count ||
        prim_count > asset->mesh_count - prim_start)
        return 0;
    if (!gltf_node_attach_primitive_mesh(asset,
                                         node,
                                         prim_start,
                                         skin_ref,
                                         weights_arr,
                                         skins,
                                         skin_count))
        return 0;
    gltf_node_set_primitive_material(node, primitive_materials, prim_start);
    for (int32_t pi = 1; pi < prim_count; pi++) {
        if (!gltf_node_attach_extra_primitive(asset,
                                             node,
                                             prim_start + pi,
                                             skin_ref,
                                             weights_arr,
                                             primitive_materials,
                                             skins,
                                             skin_count))
            return 0;
    }
    return 1;
}

static int gltf_build_template_node(rt_gltf_asset *asset,
                                    void *nodes_arr,
                                    int32_t node_index,
                                    int32_t mesh_json_count,
                                    int *mesh_prim_start,
                                    int *mesh_prim_count,
                                    void **primitive_materials,
                                    gltf_skin_t *skins,
                                    int32_t skin_count,
                                    void **imported_lights,
                                    int32_t imported_light_count,
                                    rt_scene_node3d **out_node) {
    void *node_json = rt_seq_get(nodes_arr, (int64_t)node_index);
    rt_scene_node3d *node = (rt_scene_node3d *)rt_scene_node3d_new();
    const char *name = jstr(node_json, "name");
    char fallback_name[64];
    void *weights_arr = jarr(node_json, "weights");
    void *node_extensions = jget(node_json, "extensions");
    void *node_punctual_light =
        node_extensions ? jget(node_extensions, "KHR_lights_punctual") : NULL;
    int64_t mesh_ref = jint(node_json, "mesh", -1);
    int64_t skin_ref = jint(node_json, "skin", -1);
    int64_t light_ref = jint(node_punctual_light, "light", -1);

    *out_node = node;
    if (!node)
        return 0;
    node->import_index = node_index;
    if (!name || name[0] == '\0')
        name =
            gltf_effective_node_name(nodes_arr, node_index, fallback_name, sizeof(fallback_name));
    gltf_set_node_name(node, name);
    if (mesh_ref < -1 || skin_ref < -1 || light_ref < -1)
        return 0;
    if (!gltf_node_bind_light(node, light_ref, imported_lights, imported_light_count))
        return 0;
    gltf_node_read_transform(node_json, node);
    if (skin_ref >= 0 && skin_ref >= skin_count)
        return 0;
    return gltf_node_attach_mesh_primitives(asset,
                                           node,
                                           mesh_ref,
                                           skin_ref,
                                           weights_arr,
                                           mesh_json_count,
                                           mesh_prim_start,
                                           mesh_prim_count,
                                           primitive_materials,
                                           skins,
                                           skin_count);
}

static int gltf_link_template_node_children(void *nodes_arr,
                                            int32_t node_json_count,
                                            rt_scene_node3d **nodes) {
    for (int32_t ni = 0; ni < node_json_count && nodes; ni++) {
        void *node_json = rt_seq_get(nodes_arr, (int64_t)ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int64_t child_idx = jvalue_int(rt_seq_get(children, ci), -1);
            if (child_idx >= 0 && child_idx < node_json_count && nodes[ni] && nodes[child_idx]) {
                if (!rt_scene_node3d_try_add_child(nodes[ni], nodes[child_idx]))
                    return 0;
            }
        }
    }
    return 1;
}

static int64_t gltf_scene_order_index(int64_t order, int64_t active_scene, int active_valid) {
    if (!active_valid)
        return order;
    return order == 0 ? active_scene : (order <= active_scene ? order - 1 : order);
}

static int gltf_scene_roots_are_valid(void *scene_nodes,
                                      int32_t node_json_count,
                                      const int *node_parent,
                                      int *scene_seen) {
    for (int64_t i = 0; i < jarr_len(scene_nodes); i++) {
        int64_t node_idx = jvalue_int(rt_seq_get(scene_nodes, i), -1);
        if (node_idx < 0 || node_idx >= node_json_count || scene_seen[node_idx] ||
            (node_parent && node_parent[node_idx] >= 0))
            return 0;
        scene_seen[node_idx] = 1;
    }
    return 1;
}

static int gltf_clone_scene_root_into(rt_scene_node3d *scene_root,
                                      void *nodes_arr,
                                      int32_t node_json_count,
                                      rt_scene_node3d **nodes,
                                      int32_t node_idx,
                                      uint8_t *scene_active) {
    rt_scene_node3d *clone;
    if (node_idx < 0 || node_idx >= node_json_count || !nodes[node_idx])
        return 1;
    clone = gltf_clone_scene_node(nodes[node_idx]);
    if (!clone)
        return 0;
    if (!rt_scene_node3d_try_add_child(scene_root, clone)) {
        gltf_release_local(clone);
        return 0;
    }
    gltf_release_local(clone);
    return gltf_mark_active_node_subtree(nodes_arr, node_json_count, node_idx, scene_active);
}

static int gltf_import_scene_from_json(rt_gltf_asset *asset,
                                       void *root,
                                       void *nodes_arr,
                                       rt_scene_node3d **nodes,
                                       int32_t node_json_count,
                                       const int *node_parent,
                                       void *scene_json,
                                       int64_t scene_index,
                                       int64_t order,
                                       int *built_any_io) {
    void *scene_nodes = jarr(scene_json, "nodes");
    int *scene_seen = (int *)calloc((size_t)node_json_count, sizeof(int));
    uint8_t *scene_active = (uint8_t *)calloc((size_t)node_json_count, sizeof(*scene_active));
    rt_scene_node3d *scene_root = NULL;
    int ok = scene_seen != NULL && scene_active != NULL;
    char fallback_scene_name[64];
    const char *scene_name = jstr(scene_json, "name");
    if (ok)
        ok = gltf_scene_roots_are_valid(scene_nodes, node_json_count, node_parent, scene_seen);
    if (ok) {
        scene_root = (rt_scene_node3d *)rt_scene_node3d_new();
        ok = scene_root != NULL;
    }
    for (int64_t i = 0; ok && i < jarr_len(scene_nodes); i++) {
        int64_t node_idx = jvalue_int(rt_seq_get(scene_nodes, i), -1);
        ok = gltf_clone_scene_root_into(
            scene_root, nodes_arr, node_json_count, nodes, (int32_t)node_idx, scene_active);
    }
    if (ok && scene_root) {
        snprintf(fallback_scene_name,
                 sizeof(fallback_scene_name),
                 order == 0 ? "default" : "scene_%d",
                 (int)scene_index);
        ok = gltf_append_scene(asset,
                               scene_root,
                               (scene_name && scene_name[0] != '\0') ? scene_name
                                                                      : fallback_scene_name);
        if (ok) {
            gltf_scene_info_t *scene = &asset->scenes[asset->scene_count - 1];
            ok =
                gltf_import_scene_cameras(scene, root, nodes_arr, nodes, node_json_count, scene_active);
            scene_root = NULL;
            *built_any_io = 1;
        }
    }
    if (scene_root)
        gltf_release_local(scene_root);
    free(scene_active);
    free(scene_seen);
    return ok;
}

static int gltf_import_fallback_scene(rt_gltf_asset *asset,
                                      void *root,
                                      void *nodes_arr,
                                      rt_scene_node3d **nodes,
                                      int32_t node_json_count,
                                      const int *node_parent) {
    rt_scene_node3d *scene_root = (rt_scene_node3d *)rt_scene_node3d_new();
    uint8_t *scene_active = (uint8_t *)calloc((size_t)node_json_count, sizeof(*scene_active));
    int attached_any = 0;
    int ok = scene_root != NULL && scene_active != NULL;
    for (int32_t i = 0; ok && i < node_json_count; i++) {
        if (node_parent && node_parent[i] < 0 && nodes[i]) {
            ok = gltf_clone_scene_root_into(
                scene_root, nodes_arr, node_json_count, nodes, i, scene_active);
            if (ok)
                attached_any = 1;
        }
    }
    if (ok && attached_any && gltf_append_scene(asset, scene_root, "default")) {
        gltf_scene_info_t *scene = &asset->scenes[asset->scene_count - 1];
        ok = gltf_import_scene_cameras(scene, root, nodes_arr, nodes, node_json_count, scene_active);
        scene_root = NULL;
    } else if (ok) {
        ok = 0;
    }
    if (scene_root)
        gltf_release_local(scene_root);
    free(scene_active);
    return ok;
}

static int gltf_import_node_scenes(rt_gltf_asset *asset,
                                   void *root,
                                   void *nodes_arr,
                                   rt_scene_node3d **nodes,
                                   int32_t node_json_count,
                                   const int *node_parent) {
    void *scenes_arr = jarr(root, "scenes");
    int64_t active_scene = jint(root, "scene", 0);
    int64_t scene_count = jarr_len(scenes_arr);
    int built_any = 0;
    int active_valid =
        scenes_arr && scene_count > 0 && active_scene >= 0 && active_scene < scene_count;
    if (scenes_arr && scene_count > 0) {
        for (int64_t order = 0; order < scene_count; order++) {
            int64_t scene_index = gltf_scene_order_index(order, active_scene, active_valid);
            if (!gltf_import_scene_from_json(asset,
                                             root,
                                             nodes_arr,
                                             nodes,
                                             node_json_count,
                                             node_parent,
                                             rt_seq_get(scenes_arr, scene_index),
                                             scene_index,
                                             order,
                                             &built_any))
                return 0;
        }
    }
    if (!built_any &&
        !gltf_import_fallback_scene(asset, root, nodes_arr, nodes, node_json_count, node_parent))
        return 0;
    return gltf_install_active_scene_compat(asset);
}

/// @brief Build the reusable template node hierarchy and per-scene roots for a
///        glTF asset. Extracted post-asset phase of rt_gltf_load_impl; operates
///        only on already-decoded meshes/materials/skins and never early-returns.
/// @return Non-zero (load_failed) on a fatal allocation/clone failure.
static int gltf_build_node_hierarchy(rt_gltf_asset *asset,
                                     void *root,
                                     int *mesh_prim_start,
                                     int *mesh_prim_count,
                                     void **primitive_materials,
                                     gltf_skin_t *skins,
                                     int32_t skin_count,
                                     void **imported_lights,
                                     int32_t imported_light_count) {
    int load_failed = 0;
    int mesh_json_count = (int)jarr_len(jarr(root, "meshes"));
    {
        void *nodes_arr = jarr(root, "nodes");
        int node_json_count = (int)jarr_len(nodes_arr);
        if (node_json_count > 0) {
            int *node_parent = NULL;
            int graph_valid = gltf_validate_node_graph(nodes_arr, node_json_count, &node_parent);
            rt_scene_node3d **nodes =
                graph_valid ? (rt_scene_node3d **)calloc((size_t)node_json_count, sizeof(*nodes))
                            : NULL;
            if (!graph_valid || !nodes)
                load_failed = 1;
            for (int ni = 0; ni < node_json_count && nodes; ni++) {
                if (!gltf_build_template_node(asset,
                                              nodes_arr,
                                              ni,
                                              mesh_json_count,
                                              mesh_prim_start,
                                              mesh_prim_count,
                                              primitive_materials,
                                              skins,
                                              skin_count,
                                              imported_lights,
                                              imported_light_count,
                                              &nodes[ni])) {
                    load_failed = 1;
                    continue;
                }
            }

            if (!load_failed && !gltf_link_template_node_children(nodes_arr, node_json_count, nodes))
                load_failed = 1;
            if (!load_failed && graph_valid && nodes &&
                !gltf_import_node_scenes(asset, root, nodes_arr, nodes, node_json_count, node_parent))
                load_failed = 1;

            for (int i = 0; i < node_json_count && nodes; i++)
                gltf_release_ref((void **)&nodes[i]);
            free(nodes);
            free(node_parent);
        }
    }
    return load_failed;
}

//===----------------------------------------------------------------------===//
// Main loader
//===----------------------------------------------------------------------===//

static void gltf_free_buffers(gltf_buffer_t *buffers, int buf_count, const uint8_t *bin_chunk) {
    if (!buffers)
        return;
    for (int i = 0; i < buf_count; i++) {
        if (buffers[i].data != bin_chunk)
            free(buffers[i].data);
    }
    free(buffers);
}

static int gltf_load_buffer_uri(const char *filepath,
                                int load_assets,
                                rt_gltf_preload_bundle *preload_bundle,
                                int buffer_index,
                                const char *uri,
                                size_t byte_length,
                                gltf_buffer_t *buffer) {
    if (strncmp(uri, "data:", 5) == 0) {
        char mime_type[64];
        uint8_t *decoded = NULL;
        size_t decoded_len = 0;
        char preload_key[64];
        gltf_preload_buffer_key(buffer_index, preload_key, sizeof(preload_key));
        decoded = gltf_preload_bundle_take_dependency(
            preload_bundle, preload_key, GLTF_PRELOAD_DEP_BUFFER, &decoded_len);
        if (decoded ||
            gltf_parse_data_uri(uri, mime_type, sizeof(mime_type), &decoded, &decoded_len)) {
            if (decoded_len < byte_length) {
                free(decoded);
                return 0;
            }
            buffer->data = decoded;
            buffer->len = byte_length;
            return 1;
        }
        return byte_length == 0;
    }

    char buf_path[1024];
    gltf_resolve_relative_path(filepath, uri, buf_path, sizeof(buf_path));
    if (buf_path[0] == '\0')
        return 0;
    buffer->data = gltf_load_dependency_bytes(buf_path, load_assets, byte_length, preload_bundle, NULL);
    if (buffer->data)
        buffer->len = byte_length;
    if (byte_length > 0 && (!buffer->data || buffer->len < byte_length)) {
        if (load_assets)
            gltf_trap_asset_dependency(filepath, buf_path, "buffer");
        return 0;
    }
    return 1;
}

static int gltf_load_buffers(void *root,
                             const char *filepath,
                             int load_assets,
                             rt_gltf_preload_bundle *preload_bundle,
                             uint8_t *bin_chunk,
                             size_t bin_chunk_len,
                             gltf_buffer_t **out_buffers,
                             int *out_buf_count) {
    void *buffers_arr = jarr(root, "buffers");
    int buf_count = (int)jarr_len(buffers_arr);
    gltf_buffer_t *buffers =
        (gltf_buffer_t *)calloc((size_t)(buf_count + 1), sizeof(gltf_buffer_t));
    if (!buffers)
        return 0;
    for (int i = 0; i < buf_count; i++) {
        void *buf_obj = rt_seq_get(buffers_arr, (int64_t)i);
        int64_t byte_length_raw = jint(buf_obj, "byteLength", -1);
        const char *uri = jstr(buf_obj, "uri");
        size_t byte_length;
        if (byte_length_raw < 0) {
            gltf_free_buffers(buffers, buf_count, bin_chunk);
            return 0;
        }
        byte_length = (size_t)byte_length_raw;
        if (i == 0 && bin_chunk && !uri) {
            if (byte_length > bin_chunk_len || byte_length > SIZE_MAX - 3u ||
                bin_chunk_len > byte_length + 3u) {
                gltf_free_buffers(buffers, buf_count, bin_chunk);
                return 0;
            }
            buffers[i].data = bin_chunk;
            buffers[i].len = byte_length;
        } else if (uri) {
            if (!gltf_load_buffer_uri(
                    filepath, load_assets, preload_bundle, i, uri, byte_length, &buffers[i])) {
                gltf_free_buffers(buffers, buf_count, bin_chunk);
                return 0;
            }
        } else if (byte_length > 0) {
            gltf_free_buffers(buffers, buf_count, bin_chunk);
            return 0;
        }
    }
    *out_buffers = buffers;
    *out_buf_count = buf_count;
    return 1;
}

static rt_gltf_asset *gltf_asset_new_empty(void) {
    rt_gltf_asset *asset =
        (rt_gltf_asset *)rt_obj_new_i64(RT_G3D_GLTF_ASSET_CLASS_ID, (int64_t)sizeof(rt_gltf_asset));
    if (!asset)
        return NULL;
    asset->vptr = NULL;
    asset->meshes = NULL;
    asset->mesh_count = 0;
    asset->mesh_capacity = 0;
    asset->materials = NULL;
    asset->material_count = 0;
    asset->material_capacity = 0;
    asset->skeletons = NULL;
    asset->skeleton_count = 0;
    asset->skeleton_capacity = 0;
    asset->animations = NULL;
    asset->animation_count = 0;
    asset->animation_capacity = 0;
    asset->node_animations = NULL;
    asset->node_animation_count = 0;
    asset->node_animation_capacity = 0;
    asset->cameras = NULL;
    asset->camera_count = 0;
    asset->camera_capacity = 0;
    asset->scenes = NULL;
    asset->scene_count = 0;
    asset->scene_capacity = 0;
    asset->scene_root = NULL;
    asset->node_count = 0;
    rt_obj_set_finalizer(asset, gltf_asset_finalize);
    return asset;
}

static int gltf_extract_json_document(uint8_t *file_data,
                                      size_t file_size,
                                      char **out_json_str,
                                      uint8_t **out_bin_chunk,
                                      size_t *out_bin_chunk_len) {
    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;
    int parse_error = 0;
    *out_json_str = NULL;
    *out_bin_chunk = NULL;
    *out_bin_chunk_len = 0;

    if (file_size >= 12 && file_data[0] == 0x67 && file_data[1] == 0x6C &&
        file_data[2] == 0x54 && file_data[3] == 0x46) {
        uint32_t version = gltf_read_u32_le(file_data + 4);
        uint32_t declared_len = gltf_read_u32_le(file_data + 8);
        size_t pos = 12;
        int chunk_index = 0;
        if (file_size > (size_t)UINT32_MAX || version != 2 || declared_len != (uint32_t)file_size)
            parse_error = 1;
        while (!parse_error && pos + 8 <= file_size) {
            uint32_t chunk_len = gltf_read_u32_le(file_data + pos);
            uint32_t chunk_type = gltf_read_u32_le(file_data + pos + 4);
            pos += 8;
            if ((chunk_len & 3u) != 0 || chunk_len > file_size - pos) {
                parse_error = 1;
                break;
            }
            if (chunk_index == 0 && chunk_type != 0x4E4F534A) {
                parse_error = 1;
                break;
            }
            if (chunk_type == 0x4E4F534A) {
                if (json_str || chunk_len == 0) {
                    parse_error = 1;
                    break;
                }
                json_str = (char *)malloc((size_t)chunk_len + 1u);
                if (!json_str) {
                    parse_error = 1;
                    break;
                }
                memcpy(json_str, file_data + pos, chunk_len);
                json_str[chunk_len] = '\0';
            } else if (chunk_type == 0x004E4942) {
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
        if (!parse_error && pos != file_size)
            parse_error = 1;
    } else {
        json_str = (char *)malloc(file_size + 1u);
        if (!json_str)
            return 0;
        memcpy(json_str, file_data, file_size);
        json_str[file_size] = '\0';
    }

    if (parse_error || !json_str) {
        free(json_str);
        return 0;
    }
    *out_json_str = json_str;
    *out_bin_chunk = bin_chunk;
    *out_bin_chunk_len = bin_chunk_len;
    return 1;
}

static void *gltf_parse_validated_root_json(char *json_str) {
    rt_string json_rts;
    void *root = NULL;
    jmp_buf json_recovery;
    if (!json_str)
        return NULL;
    if (!gltf_validate_required_data_uri_images(json_str, strlen(json_str))) {
        free(json_str);
        return NULL;
    }
    json_rts = rt_const_cstr(json_str);
    rt_trap_set_recovery(&json_recovery);
    if (setjmp(json_recovery) == 0)
        root = rt_json_parse_object(json_rts);
    rt_trap_clear_recovery();
    free(json_str);
    if (!root)
        return NULL;
    {
        void *asset_json = jget(root, "asset");
        const char *version = jstr(asset_json, "version");
        if (!version || strncmp(version, "2.", 2) != 0) {
            gltf_release_local(root);
            return NULL;
        }
    }
    if (!gltf_validate_required_extensions(root)) {
        gltf_release_local(root);
        return NULL;
    }
    return root;
}

typedef struct {
    void **images;
    int image_count;
    uint8_t *image_required;
    void **texture_images;
    uint8_t *texture_supported;
    gltf_sampler_info_t *texture_samplers;
    int texture_count;
    int *mesh_prim_start;
    int *mesh_prim_count;
    void **primitive_materials;
    gltf_material_info_t *material_infos;
    gltf_skin_t *skins;
    int32_t skin_count;
    void **imported_lights;
    int32_t imported_light_count;
} gltf_load_scratch_t;

static void gltf_load_scratch_cleanup(gltf_load_scratch_t *scratch) {
    if (!scratch)
        return;
    if (scratch->images) {
        for (int i = 0; i < scratch->image_count; i++)
            gltf_release_ref(&scratch->images[i]);
    }
    free(scratch->images);
    free(scratch->image_required);
    free(scratch->texture_images);
    free(scratch->texture_supported);
    free(scratch->texture_samplers);
    free(scratch->mesh_prim_start);
    free(scratch->mesh_prim_count);
    free(scratch->primitive_materials);
    free(scratch->material_infos);
    if (scratch->imported_lights) {
        for (int32_t i = 0; i < scratch->imported_light_count; i++)
            gltf_release_ref(&scratch->imported_lights[i]);
    }
    free(scratch->imported_lights);
    gltf_free_skins(scratch->skins, scratch->skin_count);
}

static int gltf_load_asset_payload(rt_gltf_asset *asset,
                                   void *root,
                                   const char *filepath,
                                   int load_assets,
                                   rt_gltf_preload_bundle *preload_bundle,
                                   gltf_buffer_t *buffers,
                                   int buf_count,
                                   gltf_load_scratch_t *scratch) {
    int load_failed = 0;
    gltf_load_images_and_textures(root,
                                  filepath,
                                  load_assets,
                                  preload_bundle,
                                  buffers,
                                  buf_count,
                                  &scratch->images,
                                  &scratch->image_count,
                                  &scratch->image_required,
                                  &scratch->texture_images,
                                  &scratch->texture_supported,
                                  &scratch->texture_samplers,
                                  &scratch->texture_count,
                                  &load_failed);
    gltf_load_materials(asset,
                        root,
                        scratch->texture_images,
                        scratch->texture_supported,
                        scratch->texture_samplers,
                        scratch->texture_count,
                        &scratch->material_infos,
                        &load_failed);
    gltf_parse_punctual_lights(root, &scratch->imported_lights, &scratch->imported_light_count);
    gltf_load_meshes(asset,
                     root,
                     buffers,
                     buf_count,
                     preload_bundle,
                     &scratch->mesh_prim_start,
                     &scratch->mesh_prim_count,
                     &scratch->primitive_materials,
                     &load_failed);
    gltf_parse_skins(
        asset, root, buffers, buf_count, &scratch->skins, &scratch->skin_count, &load_failed);
    if (!load_failed) {
        gltf_parse_animations(asset, root, buffers, buf_count, scratch->skins, scratch->skin_count);
        gltf_parse_node_animations(
            asset, root, buffers, buf_count, scratch->skins, scratch->skin_count);
        load_failed = gltf_build_node_hierarchy(asset,
                                                root,
                                                scratch->mesh_prim_start,
                                                scratch->mesh_prim_count,
                                                scratch->primitive_materials,
                                                scratch->skins,
                                                scratch->skin_count,
                                                scratch->imported_lights,
                                                scratch->imported_light_count);
    }
    return load_failed;
}

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
    if (file_size > (size_t)LONG_MAX) {
        if (!preloaded_data)
            free(file_data);
        return NULL;
    }

    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;
    if (!gltf_extract_json_document(file_data, file_size, &json_str, &bin_chunk, &bin_chunk_len)) {
        free(file_data);
        return NULL;
    }

    void *root = gltf_parse_validated_root_json(json_str);
    if (!root) {
        free(file_data);
        return NULL;
    }

    int buf_count = 0;
    gltf_buffer_t *buffers = NULL;
    gltf_load_scratch_t scratch;
    int load_failed = 0;
    memset(&scratch, 0, sizeof(scratch));
    if (!gltf_load_buffers(root,
                           filepath,
                           load_assets,
                           preload_bundle,
                           bin_chunk,
                           bin_chunk_len,
                           &buffers,
                           &buf_count)) {
        gltf_release_local(root);
        free(file_data);
        return NULL;
    }

    if (!gltf_validate_sparse_accessors(root, buffers, buf_count)) {
        gltf_free_buffers(buffers, buf_count, bin_chunk);
        gltf_release_local(root);
        free(file_data);
        return NULL;
    }

    rt_gltf_asset *asset = gltf_asset_new_empty();
    if (!asset) {
        gltf_free_buffers(buffers, buf_count, bin_chunk);
        gltf_release_local(root);
        free(file_data);
        return NULL;
    }

    load_failed = gltf_load_asset_payload(
        asset, root, filepath, load_assets, preload_bundle, buffers, buf_count, &scratch);
    gltf_load_scratch_cleanup(&scratch);

    gltf_free_buffers(buffers, buf_count, bin_chunk);
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

/// @brief Build a glTF asset on the main thread from a previously-staged preload bundle.
/// @details Consumes the bundle's staged dependencies (no file I/O), falling back to a normal
///          rt_gltf_load/rt_gltf_load_asset when @p bundle is NULL. Always frees @p bundle.
/// @return The loaded glTF asset handle, or NULL on failure.
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
    return a ? gltf_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity) : 0;
}

/// @brief Get a mesh by index from the loaded GLTF asset.
void *rt_gltf_get_mesh(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t mesh_count = gltf_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity);
    if (index < 0 || index >= mesh_count)
        return NULL;
    return rt_g3d_checked_or_null(a->meshes[index], RT_G3D_MESH3D_CLASS_ID);
}

/// @brief Get the number of materials extracted from the GLTF file.
int64_t rt_gltf_material_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->materials, a->material_count, a->material_capacity) : 0;
}

/// @brief Get a material by index from the loaded GLTF asset.
void *rt_gltf_get_material(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t material_count =
        gltf_asset_safe_count(a->materials, a->material_count, a->material_capacity);
    if (index < 0 || index >= material_count)
        return NULL;
    return rt_g3d_checked_or_null(a->materials[index], RT_G3D_MATERIAL3D_CLASS_ID);
}

/// @brief Number of skeletons extracted from the loaded glTF asset.
int64_t rt_gltf_skeleton_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->skeletons, a->skeleton_count, a->skeleton_capacity) : 0;
}

/// @brief Get a skeleton by index from the loaded glTF asset.
void *rt_gltf_get_skeleton(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t skeleton_count =
        gltf_asset_safe_count(a->skeletons, a->skeleton_count, a->skeleton_capacity);
    if (index < 0 || index >= skeleton_count)
        return NULL;
    return rt_g3d_checked_or_null(a->skeletons[index], RT_G3D_SKELETON3D_CLASS_ID);
}

/// @brief Number of animation clips extracted from the loaded glTF asset.
int64_t rt_gltf_animation_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->animations, a->animation_count, a->animation_capacity) : 0;
}

/// @brief Get an Animation3D clip by index from the loaded glTF asset.
void *rt_gltf_get_animation(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t animation_count =
        gltf_asset_safe_count(a->animations, a->animation_count, a->animation_capacity);
    if (index < 0 || index >= animation_count)
        return NULL;
    return rt_g3d_checked_or_null(a->animations[index], RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Return the number of node animations (AnimationClip-style tracks) in the glTF asset.
int64_t rt_gltf_node_animation_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(
                   a->node_animations, a->node_animation_count, a->node_animation_capacity)
             : 0;
}

/// @brief Get a node-animation track by index from the loaded glTF asset.
void *rt_gltf_get_node_animation(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t node_animation_count =
        gltf_asset_safe_count(
            a->node_animations, a->node_animation_count, a->node_animation_capacity);
    if (index < 0 || index >= node_animation_count)
        return NULL;
    return rt_g3d_checked_or_null(a->node_animations[index],
                                  RT_G3D_NODEANIMATION3D_CLASS_ID);
}

/// @brief Return the number of cameras imported from the active scene.
int64_t rt_gltf_camera_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->cameras, a->camera_count, a->camera_capacity) : 0;
}

/// @brief Borrow the i-th active-scene Camera3D imported from glTF.
void *rt_gltf_get_camera(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t camera_count = gltf_asset_safe_count(a->cameras, a->camera_count, a->camera_capacity);
    if (index < 0 || index >= camera_count)
        return NULL;
    return rt_g3d_checked_or_null(a->cameras[index], RT_G3D_CAMERA3D_CLASS_ID);
}

/// @brief Number of immutable scenes in the glTF asset.
int64_t rt_gltf_scene_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return gltf_asset_safe_scene_count(a);
}

/// @brief Return the imported scene name, or an empty string for invalid indices.
rt_string rt_gltf_get_scene_name(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || index < 0 || index >= scene_count || !a->scenes[index].name)
        return rt_const_cstr("");
    return rt_const_cstr(a->scenes[index].name);
}

/// @brief Borrow the root SceneNode3D for immutable scene @p index.
void *rt_gltf_get_scene_root_at(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || index < 0 || index >= scene_count)
        return NULL;
    return rt_g3d_checked_or_null(a->scenes[index].root, RT_G3D_SCENENODE3D_CLASS_ID);
}

/// @brief Number of cameras reachable from immutable scene @p scene_index.
int64_t rt_gltf_scene_camera_count(void *obj, int64_t scene_index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || scene_index < 0 || scene_index >= scene_count)
        return 0;
    return gltf_asset_safe_count(a->scenes[scene_index].cameras,
                                 a->scenes[scene_index].camera_count,
                                 a->scenes[scene_index].camera_capacity);
}

/// @brief Borrow a Camera3D from immutable scene @p scene_index.
void *rt_gltf_get_scene_camera(void *obj, int64_t scene_index, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    gltf_scene_info_t *scene;
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || scene_index < 0 || scene_index >= scene_count)
        return NULL;
    scene = &a->scenes[scene_index];
    int32_t camera_count =
        gltf_asset_safe_count(scene->cameras, scene->camera_count, scene->camera_capacity);
    if (index < 0 || index >= camera_count)
        return NULL;
    return rt_g3d_checked_or_null(scene->cameras[index], RT_G3D_CAMERA3D_CLASS_ID);
}

/// @brief Number of nodes in the loaded glTF scene tree (0 for NULL).
int64_t rt_gltf_node_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a || a->node_count <= 0 ||
        !rt_g3d_has_class(a->scene_root, RT_G3D_SCENENODE3D_CLASS_ID))
        return 0;
    return a->node_count;
}

/// @brief Return the scene-root SceneNode of the loaded asset (NULL if not loaded / NULL).
void *rt_gltf_get_scene_root(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? rt_g3d_checked_or_null(a->scene_root, RT_G3D_SCENENODE3D_CLASS_ID) : NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
