//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d.c
// Purpose: Viper.Graphics3D.Scene3D / SceneNode3D — 3D scene graph with
//   parent-child transform propagation. Each node holds local TRS, and the
//   world matrix is lazily recomputed on access or draw.
//
// Key invariants:
//   - TRS order: world = parent_world * Translate * Rotate * Scale
//   - Dirty state is lazy: changing a parent bumps its world revision, and
//     descendants refresh when their cached parent revision no longer matches.
//   - Children array is heap-allocated (not GC-managed); freed in finalizer.
//   - Mesh/material/name and LOD meshes are retained by the node.
//   - Iterative traversal stacks avoid recursion stack overflow for draw/count
//     walks; transform invalidation itself is allocation-free.
//   - LOD levels are kept sorted by distance ascending so the draw path picks
//     the highest threshold that does not exceed camera distance.
//
// Ownership/Lifetime:
//   - Scene3D / SceneNode3D / NodeAnimation3D / NodeAnimator3D are GC-managed.
//   - Scene3D retains the root subtree; finalizer releases the root.
//   - SceneNode3D retains mesh, material, light, name, animator, body binding,
//     and per-LOD meshes; finalizer releases all of them.
//
// Links: rt_scene3d.h, rt_quat.h, rt_mat4.h, plans/3d/12-scene-graph.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_scene3d.h"
#include "rt_animcontroller3d.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_physics3d.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d_internal.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
#include "rt_sound3d.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include "vgfx3d_frustum.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/// @brief Validate @p obj as a Scene3D handle and return its typed pointer (NULL on mismatch).
rt_scene3d *scene3d_checked(void *obj) {
    return (rt_scene3d *)rt_g3d_checked_or_null(obj, RT_G3D_SCENE3D_CLASS_ID);
}

/// @brief Validate @p obj as a SceneNode3D handle and return its typed pointer (NULL on mismatch).
rt_scene_node3d *scene_node3d_checked(void *obj) {
    return (rt_scene_node3d *)rt_g3d_checked_or_null(obj, RT_G3D_SCENENODE3D_CLASS_ID);
}

static const rt_scene_node3d *scene_node3d_checked_const(const rt_scene_node3d *obj) {
#if defined(RT_G3D_INTERNAL_ASSUME_STRUCT_HANDLE) && RT_G3D_INTERNAL_ASSUME_STRUCT_HANDLE
    return obj;
#else
    void *raw = (void *)(uintptr_t)obj;
    return rt_g3d_has_class(raw, RT_G3D_SCENENODE3D_CLASS_ID) ? obj : NULL;
#endif
}

/// @brief Drop the GC reference in `*slot` and null the pointer (refcount-aware free).
void scene3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a retained Graphics3D slot only if it still has the expected class.
static void scene3d_release_class_ref(void **slot, int64_t class_id) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, class_id)) {
        *slot = NULL;
        return;
    }
    scene3d_release_ref(slot);
}

/// @brief Release a retained Pixels slot only if it still points at Pixels.
static void scene3d_release_pixels_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_pixels_checked_impl_or_null(*slot)) {
        *slot = NULL;
        return;
    }
    scene3d_release_ref(slot);
}

/// @brief Grow a traversal-stack buffer by doubling (min 64 elements), overflow-checked.
/// @return 1 with @p buffer / @p capacity updated, 0 on overflow or allocation failure.
int scene3d_grow_stack_storage(void **buffer, size_t *capacity, size_t elem_size) {
    size_t new_capacity;
    void *grown;
    if (!buffer || !capacity || elem_size == 0)
        return 0;
    new_capacity = *capacity > 0 ? *capacity * 2u : 64u;
    if (new_capacity <= *capacity || new_capacity > SIZE_MAX / elem_size)
        return 0;
    grown = realloc(*buffer, new_capacity * elem_size);
    if (!grown)
        return 0;
    *buffer = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Grow an int32-indexed array to hold at least @p needed elements (floor
///   @p min_capacity, doubling thereafter), optionally zeroing the newly added tail.
/// @return 1 on success (including when already large enough), 0 on bad args or allocation failure.
int scene3d_grow_array_i32(void **buffer,
                           int32_t *capacity,
                           int32_t needed,
                           int32_t min_capacity,
                           size_t elem_size,
                           int zero_new) {
    int32_t old_capacity;
    int32_t new_capacity;
    void *grown;
    if (!buffer || !capacity || needed < 0 || min_capacity <= 0 || elem_size == 0)
        return 0;
    old_capacity = *capacity;
    if (old_capacity >= needed)
        return 1;
    if (old_capacity < 0)
        return 0;
    new_capacity = old_capacity < min_capacity ? min_capacity : old_capacity;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            new_capacity = needed;
        else
            new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / elem_size)
        return 0;
    grown = realloc(*buffer, (size_t)new_capacity * elem_size);
    if (!grown)
        return 0;
    if (zero_new && new_capacity > old_capacity)
        memset((char *)grown + (size_t)old_capacity * elem_size,
               0,
               (size_t)(new_capacity - old_capacity) * elem_size);
    *buffer = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Mark the spatial index fully stale: a topology change forces a full BVH rebuild.
void scene3d_mark_spatial_dirty(rt_scene3d *scene) {
    if (!scene)
        return;
    scene->spatial_index.dirty = 1;
    scene->spatial_index.valid = 0;
    scene->spatial_index.topology_dirty = 1;
}

/// @brief Request a cheaper BVH refit (a node moved but the tree shape is unchanged).
/// @details Only escalates to a full topology rebuild if the index is already invalid.
static void scene3d_mark_spatial_refit_dirty(rt_scene3d *scene) {
    if (!scene)
        return;
    scene->spatial_index.dirty = 1;
    if (!scene->spatial_index.valid)
        scene->spatial_index.topology_dirty = 1;
}

/// @brief Return @p value if it is a finite number, otherwise return @p fallback.
/// @details Used to sanitize every numeric input that comes from external data (glTF
///   assets, caller-supplied transforms) before it can corrupt a matrix multiply or
///   a length calculation with NaN / Inf. The indirection through this helper rather
///   than an inline ternary makes the intent clear at each call site.
/// @param value   Candidate double — may be NaN, +Inf, or -Inf.
/// @param fallback Value to substitute when @p value is not finite.
/// @return @p value when finite, @p fallback otherwise.
static double scene3d_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp `value` into `[-SCENE3D_ABS_MAX, SCENE3D_ABS_MAX]`, substituting `fallback` when
/// not finite.
double scene3d_clamp_abs_or(double value, double fallback) {
    value = scene3d_finite_or(value, fallback);
    if (value > SCENE3D_ABS_MAX)
        return SCENE3D_ABS_MAX;
    if (value < -SCENE3D_ABS_MAX)
        return -SCENE3D_ABS_MAX;
    return value;
}

/// @brief Narrow a double to float, returning 0.0f when non-finite or outside
/// ±SCENE3D_FLOAT_ABS_MAX.
float scene3d_float_or_zero(double value) {
    if (!isfinite(value) || value < -SCENE3D_FLOAT_ABS_MAX || value > SCENE3D_FLOAT_ABS_MAX)
        return 0.0f;
    return (float)value;
}

/// @brief Return @p value if finite, or 1.0 as a safe scale factor.
/// @details Specialisation of `scene3d_finite_or` for scale components where a
///   NaN/Inf value would corrupt transform composition. Finite zero scale is
///   preserved so authored collapse animations and intentionally flattened nodes
///   remain representable; inverse-dependent consumers handle singular matrices
///   at their own boundary.
/// @param value Scale factor candidate — may be NaN or Inf.
/// @return @p value when finite, otherwise 1.0.
double scene3d_scale_or_unit(double value) {
    if (!isfinite(value))
        return 1.0;
    if (value > SCENE3D_ABS_MAX)
        return SCENE3D_ABS_MAX;
    if (value < -SCENE3D_ABS_MAX)
        return -SCENE3D_ABS_MAX;
    return value;
}

/// @brief Clamp a non-negative scene-space distance/radius to the runtime numeric envelope.
double scene3d_distance_or_zero(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0;
    return value > SCENE3D_ABS_MAX ? SCENE3D_ABS_MAX : value;
}

/// @brief Sanitize a raw double[3] coordinate vector in place.
static void scene3d_sanitize_vec3d(double v[3], double fallback) {
    if (!v)
        return;
    v[0] = scene3d_clamp_abs_or(v[0], fallback);
    v[1] = scene3d_clamp_abs_or(v[1], fallback);
    v[2] = scene3d_clamp_abs_or(v[2], fallback);
}

/// @brief Sanitize a raw scale vector in place while preserving finite zero/negative scales.
static void scene3d_sanitize_scale3(double v[3]) {
    if (!v)
        return;
    v[0] = scene3d_scale_or_unit(v[0]);
    v[1] = scene3d_scale_or_unit(v[1]);
    v[2] = scene3d_scale_or_unit(v[2]);
}

/// @brief Robustly normalize a raw double[3] vector without overflowing on huge finite inputs.
int scene3d_normalize_vec3d(double v[3]) {
    double max_abs;
    double x;
    double y;
    double z;
    double len_sq;
    double inv_len;
    if (!v || !isfinite(v[0]) || !isfinite(v[1]) || !isfinite(v[2]))
        return 0;
    max_abs = fmax(fabs(v[0]), fmax(fabs(v[1]), fabs(v[2])));
    if (!isfinite(max_abs) || max_abs < 1e-24)
        return 0;
    x = v[0] / max_abs;
    y = v[1] / max_abs;
    z = v[2] / max_abs;
    len_sq = x * x + y * y + z * z;
    if (!isfinite(len_sq) || len_sq < 1e-24)
        return 0;
    inv_len = 1.0 / sqrt(len_sq);
    v[0] = x * inv_len;
    v[1] = y * inv_len;
    v[2] = z * inv_len;
    return 1;
}

/// @brief Check that every matrix lane is finite.
static int scene3d_matrix_is_finite(const double *m) {
    if (!m)
        return 0;
    for (int i = 0; i < 16; i++) {
        if (!isfinite(m[i]))
            return 0;
    }
    return 1;
}

/// @brief Canonicalize and sanitize a double-precision AABB in place.
void scene3d_canonicalize_aabb_d(double mn[3], double mx[3]) {
    if (!mn || !mx)
        return;
    scene3d_sanitize_vec3d(mn, 0.0);
    scene3d_sanitize_vec3d(mx, 0.0);
    for (int i = 0; i < 3; i++) {
        if (mn[i] > mx[i]) {
            double tmp = mn[i];
            mn[i] = mx[i];
            mx[i] = tmp;
        }
    }
}

/// @brief Restore SceneNode3D local TRS invariants before matrix composition or getters.
static void scene3d_repair_node_transform(rt_scene_node3d *node) {
    if (!node)
        return;
    scene3d_sanitize_vec3d(node->position, 0.0);
    scene3d_sanitize_scale3(node->scale_xyz);
    scene3d_quat_normalize_local(node->rotation);
}

extern void *rt_anim_controller3d_consume_root_motion_rotation(void *obj);

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Build a TRS matrix: Translate * Rotate * Scale (row-major).
/// Quaternion (x,y,z,w) is expanded inline to avoid allocating a Mat4.
static void build_trs_matrix(const double *pos,
                             const double *quat,
                             const double *scl,
                             double *out) {
    double p[3] = {pos ? pos[0] : 0.0, pos ? pos[1] : 0.0, pos ? pos[2] : 0.0};
    double q[4] = {quat ? quat[0] : 0.0, quat ? quat[1] : 0.0, quat ? quat[2] : 0.0,
                   quat ? quat[3] : 1.0};
    double s[3] = {scl ? scl[0] : 1.0, scl ? scl[1] : 1.0, scl ? scl[2] : 1.0};
    scene3d_sanitize_vec3d(p, 0.0);
    scene3d_sanitize_scale3(s);
    scene3d_quat_normalize_local(q);
    double x = q[0], y = q[1], z = q[2], w = q[3];
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;

    double sx = s[0], sy = s[1], sz = s[2];

    /* R * S (rotation columns scaled) */
    out[0] = (1.0 - (yy + zz)) * sx;
    out[1] = (xy - wz) * sy;
    out[2] = (xz + wy) * sz;
    out[3] = p[0];

    out[4] = (xy + wz) * sx;
    out[5] = (1.0 - (xx + zz)) * sy;
    out[6] = (yz - wx) * sz;
    out[7] = p[1];

    out[8] = (xz - wy) * sx;
    out[9] = (yz + wx) * sy;
    out[10] = (1.0 - (xx + yy)) * sz;
    out[11] = p[2];

    out[12] = 0.0;
    out[13] = 0.0;
    out[14] = 0.0;
    out[15] = 1.0;
}

/// @brief Multiply two 4x4 row-major matrices: out = a * b.
static void mat4d_mul(const double *a, const double *b, double *out) {
    double tmp[16];
    if (!out)
        return;
    if (!scene3d_matrix_is_finite(a) || !scene3d_matrix_is_finite(b)) {
        mat4d_identity(out);
        return;
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            double v = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                       a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
            tmp[r * 4 + c] = scene3d_clamp_abs_or(v, r == c ? 1.0 : 0.0);
        }
    }
    memcpy(out, tmp, sizeof(tmp));
}

/// @brief Write an identity 4x4 row-major matrix into @p out.
void mat4d_identity(double *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(double) * 16);
    out[0] = 1.0;
    out[5] = 1.0;
    out[10] = 1.0;
    out[15] = 1.0;
}

/// @brief Invert a row-major 4x4 matrix using the cofactor expansion.
///
/// Returns 0 on success and writes the inverse into `out`. Returns -1
/// if the matrix is singular (`|det| < 1e-12`); `out` is then untouched.
/// Used to derive parent-from-world transforms and bind-pose inverses.
static int mat4d_invert(const double *m, double *out) {
    double inv[16];
    double det;
    double inv_det;
    if (!out || !scene3d_matrix_is_finite(m))
        return -1;
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    for (int i = 0; i < 16; i++) {
        if (!isfinite(inv[i]))
            return -1;
    }
    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!isfinite(det) || fabs(det) < 1e-12)
        return -1;

    inv_det = 1.0 / det;
    for (int i = 0; i < 16; i++) {
        out[i] = inv[i] * inv_det;
        if (!isfinite(out[i]))
            return -1;
    }
    return 0;
}

/// @brief Set `out` to the identity quaternion `(0, 0, 0, 1)` — no rotation.
static void quat_identity(double *out) {
    if (!out)
        return;
    out[0] = 0.0;
    out[1] = 0.0;
    out[2] = 0.0;
    out[3] = 1.0;
}

/// @brief Renormalise `q` so |q|=1; defaults to identity if it's degenerate.
void scene3d_quat_normalize_local(double *q) {
    double max_abs;
    double x;
    double y;
    double z;
    double w;
    double len_sq;
    double inv_len;
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        quat_identity(q);
        return;
    }
    max_abs = fmax(fmax(fabs(q[0]), fabs(q[1])), fmax(fabs(q[2]), fabs(q[3])));
    if (!isfinite(max_abs) || max_abs < 1e-24) {
        quat_identity(q);
        return;
    }
    x = q[0] / max_abs;
    y = q[1] / max_abs;
    z = q[2] / max_abs;
    w = q[3] / max_abs;
    len_sq = x * x + y * y + z * z + w * w;
    if (!isfinite(len_sq) || len_sq < 1e-20) {
        quat_identity(q);
        return;
    }
    inv_len = 1.0 / sqrt(len_sq);
    q[0] = x * inv_len;
    q[1] = y * inv_len;
    q[2] = z * inv_len;
    q[3] = w * inv_len;
}

/// @brief Square root helper for quaternion extraction, treating invalid terms as zero.
static double scene3d_sqrt_nonnegative(double value) {
    if (!isfinite(value) || value <= 0.0)
        return 0.0;
    return sqrt(value);
}

/// @brief Quaternion conjugate for unit-rotation inverse.
static void quat_conjugate_local(const double *q, double *out) {
    if (!out)
        return;
    if (!q) {
        quat_identity(out);
        return;
    }
    out[0] = -q[0];
    out[1] = -q[1];
    out[2] = -q[2];
    out[3] = q[3];
}

/// @brief Quaternion product `out = a * b`.
static void quat_mul_local(const double *a, const double *b, double *out) {
    double ax;
    double ay;
    double az;
    double aw;
    double bx;
    double by;
    double bz;
    double bw;
    if (!out)
        return;
    if (!a || !b) {
        quat_identity(out);
        return;
    }
    ax = a[0];
    ay = a[1];
    az = a[2];
    aw = a[3];
    bx = b[0];
    by = b[1];
    bz = b[2];
    bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
    scene3d_quat_normalize_local(out);
}

/// @brief Transform a point by a row-major 4x4 matrix with translation in column 3.
static void mat4d_transform_point(const double *m, const double *point, double *out) {
    double p[3] = {point ? point[0] : 0.0, point ? point[1] : 0.0, point ? point[2] : 0.0};
    double x;
    double y;
    double z;
    if (!out)
        return;
    scene3d_sanitize_vec3d(p, 0.0);
    if (!scene3d_matrix_is_finite(m)) {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
        return;
    }
    x = m[0] * p[0] + m[1] * p[1] + m[2] * p[2] + m[3];
    y = m[4] * p[0] + m[5] * p[1] + m[6] * p[2] + m[7];
    z = m[8] * p[0] + m[9] * p[1] + m[10] * p[2] + m[11];
    out[0] = scene3d_clamp_abs_or(x, 0.0);
    out[1] = scene3d_clamp_abs_or(y, 0.0);
    out[2] = scene3d_clamp_abs_or(z, 0.0);
}

/// @brief Extract a unit quaternion from the rotation part of a row-major matrix.
///
/// Picks the largest of four possible diagonal terms and uses
/// the corresponding extraction formula — Shepperd's method —
/// for numerical stability across the full sphere of orientations.
static void quat_from_matrix_rows(double m00,
                                  double m01,
                                  double m02,
                                  double m10,
                                  double m11,
                                  double m12,
                                  double m20,
                                  double m21,
                                  double m22,
                                  double *out) {
    double trace;
    if (!out)
        return;
    trace = m00 + m11 + m22;
    if (trace > 0.0) {
        double s = scene3d_sqrt_nonnegative(trace + 1.0) * 2.0;
        if (s <= 1e-12) {
            quat_identity(out);
            return;
        }
        out[3] = 0.25 * s;
        out[0] = (m21 - m12) / s;
        out[1] = (m02 - m20) / s;
        out[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = scene3d_sqrt_nonnegative(1.0 + m00 - m11 - m22) * 2.0;
        if (s <= 1e-12) {
            quat_identity(out);
            return;
        }
        out[3] = (m21 - m12) / s;
        out[0] = 0.25 * s;
        out[1] = (m01 + m10) / s;
        out[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = scene3d_sqrt_nonnegative(1.0 + m11 - m00 - m22) * 2.0;
        if (s <= 1e-12) {
            quat_identity(out);
            return;
        }
        out[3] = (m02 - m20) / s;
        out[0] = (m01 + m10) / s;
        out[1] = 0.25 * s;
        out[2] = (m12 + m21) / s;
    } else {
        double s = scene3d_sqrt_nonnegative(1.0 + m22 - m00 - m11) * 2.0;
        if (s <= 1e-12) {
            quat_identity(out);
            return;
        }
        out[3] = (m10 - m01) / s;
        out[0] = (m02 + m20) / s;
        out[1] = (m12 + m21) / s;
        out[2] = 0.25 * s;
    }
    scene3d_quat_normalize_local(out);
}

/// @brief Strip translation/scale and call `quat_from_matrix_rows` to recover the rotation
/// quaternion.
///
/// Used when reading back world-space orientation after a node's
/// world matrix has been composed from parent transforms — we need
/// the rotation back as a quaternion for further composition.
static int scene_extract_rotation_basis(const double *m, double basis[9]) {
    double rx;
    double ry;
    double rz;
    double ux;
    double uy;
    double uz;
    double fx;
    double fy;
    double fz;
    double rlen;
    double ulen;
    double flen;
    double det;
    if (!m || !basis)
        return 0;
    rx = m[0];
    ry = m[4];
    rz = m[8];
    ux = m[1];
    uy = m[5];
    uz = m[9];
    fx = m[2];
    fy = m[6];
    fz = m[10];
    rlen = sqrt(rx * rx + ry * ry + rz * rz);
    ulen = sqrt(ux * ux + uy * uy + uz * uz);
    flen = sqrt(fx * fx + fy * fy + fz * fz);
    if (!isfinite(rlen) || !isfinite(ulen) || !isfinite(flen) || rlen < 1e-12 || ulen < 1e-12 ||
        flen < 1e-12)
        return 0;
    rx /= rlen;
    ry /= rlen;
    rz /= rlen;
    ux /= ulen;
    uy /= ulen;
    uz /= ulen;
    fx /= flen;
    fy /= flen;
    fz /= flen;
    det = rx * (uy * fz - uz * fy) - ux * (ry * fz - rz * fy) + fx * (ry * uz - rz * uy);
    if (!isfinite(det))
        return 0;
    if (det < 0.0) {
        if (rlen >= ulen && rlen >= flen) {
            rx = -rx;
            ry = -ry;
            rz = -rz;
        } else if (ulen >= flen) {
            ux = -ux;
            uy = -uy;
            uz = -uz;
        } else {
            fx = -fx;
            fy = -fy;
            fz = -fz;
        }
    }
    basis[0] = rx;
    basis[1] = ux;
    basis[2] = fx;
    basis[3] = ry;
    basis[4] = uy;
    basis[5] = fy;
    basis[6] = rz;
    basis[7] = uz;
    basis[8] = fz;
    return 1;
}

/// @brief Extract a unit quaternion from a world matrix's rotation, normalizing the basis
///   first; falls back to identity when the matrix has no recoverable rotation.
static void quat_from_world_matrix(const double *m, double *out) {
    double basis[9];
    if (!out)
        return;
    if (!scene_extract_rotation_basis(m, basis)) {
        quat_identity(out);
        return;
    }
    quat_from_matrix_rows(basis[0],
                          basis[1],
                          basis[2],
                          basis[3],
                          basis[4],
                          basis[5],
                          basis[6],
                          basis[7],
                          basis[8],
                          out);
}

/// @brief Push @p node onto an iterative-traversal node stack, growing it geometrically.
/// @details Used by walk functions that prefer iterative traversal over recursion
///   (avoids stack overflow on deep scene graphs).
///   Returns 1 on success or no-op (NULL node), 0 on overflow / OOM.
int scene_node_stack_push(rt_scene_node3d ***stack,
                          size_t *count,
                          size_t *capacity,
                          rt_scene_node3d *node) {
    rt_scene_node3d *checked;
    if (!stack || !count || !capacity || !node)
        return 1;
    checked = scene_node3d_checked(node);
    if (!checked)
        return 1;
    if (*count >= *capacity) {
        if (!scene3d_grow_stack_storage((void **)stack, capacity, sizeof(**stack)))
            return 0;
    }
    (*stack)[(*count)++] = checked;
    return 1;
}

/// @brief `scene_node_stack_push` for read-only `const` traversals.
/// @details Same growth contract; the const variant is needed because the C type
///   system disallows aliasing a `const T**` as a `T**` even when only reads happen.
static int scene_node_const_stack_push(const rt_scene_node3d ***stack,
                                       size_t *count,
                                       size_t *capacity,
                                       const rt_scene_node3d *node) {
    if (!stack || !count || !capacity || !node)
        return 1;
    if (*count >= *capacity) {
        if (!scene3d_grow_stack_storage((void **)stack, capacity, sizeof(**stack)))
            return 0;
    }
    (*stack)[(*count)++] = node;
    return 1;
}

/// @brief Set @p owner as the owning scene on @p node and every descendant (iterative DFS).
void scene_node_assign_owner_recursive(rt_scene_node3d *node, rt_scene3d *owner) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("Scene3D: owner traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        int32_t child_count;
        current->owner_scene = owner;
        child_count = scene3d_node_child_count(current);
        current->child_count = child_count;
        for (int32_t i = child_count - 1; i >= 0; --i) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D: owner traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief Clear @p owner from @p node and every descendant that still references it (iterative
/// DFS).
void scene_node_clear_owner_recursive(rt_scene_node3d *node, rt_scene3d *owner) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !owner)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("Scene3D: owner traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        int32_t child_count;
        if (current->owner_scene == owner)
            current->owner_scene = NULL;
        child_count = scene3d_node_child_count(current);
        current->child_count = child_count;
        for (int32_t i = child_count - 1; i >= 0; --i) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D: owner traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief Mark a node's local world transform dirty.
///
/// Descendants do not need eager dirty propagation. Each node tracks the parent
/// world-matrix revision it last consumed, so a child lazily notices parent
/// changes during `recompute_world_matrix` without allocating a traversal stack.
void mark_dirty(rt_scene_node3d *node) {
    if (!node)
        return;
    node->world_dirty = 1;
    scene3d_mark_spatial_refit_dirty(node->owner_scene);
}

/// @brief Forward declaration for the recursive node-name search used by the animation
///        channel applier before the function's full definition appears later in the file.


/// @brief Recompute the world matrix if local or parent state changed.
void recompute_world_matrix(rt_scene_node3d *node) {
    double local[16];
    uint32_t parent_revision = 0;
    if (!node)
        return;
    scene3d_repair_node_transform(node);

    if (node->parent) {
        recompute_world_matrix(node->parent);
        parent_revision = node->parent->world_revision;
        if (node->parent_world_revision_seen != parent_revision)
            node->world_dirty = 1;
    }

    if (!node->world_dirty && scene3d_matrix_is_finite(node->world_matrix))
        return;

    build_trs_matrix(node->position, node->rotation, node->scale_xyz, local);
    if (node->parent)
        mat4d_mul(node->parent->world_matrix, local, node->world_matrix);
    else
        memcpy(node->world_matrix, local, sizeof(double) * 16);
    if (!scene3d_matrix_is_finite(node->world_matrix))
        mat4d_identity(node->world_matrix);
    node->parent_world_revision_seen = parent_revision;
    node->world_dirty = 0;
    node->world_revision = node->world_revision == UINT32_MAX ? 1u : node->world_revision + 1u;
}

/// @brief Count nodes in a subtree (including the root).
static int32_t count_subtree(const rt_scene_node3d *node) {
    const rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int32_t total = 0;
    const rt_scene_node3d *root = scene_node3d_checked_const(node);
    if (!root)
        return 0;
    if (!scene_node_const_stack_push(&stack, &count, &capacity, root))
        return INT32_MAX;
    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (total == INT32_MAX) {
            free(stack);
            return INT32_MAX;
        }
        total++;
        for (int32_t i = 0, child_count = scene3d_node_child_count(current); i < child_count;
             i++) {
            const rt_scene_node3d *child = scene_node3d_checked(current->children[i]);
            if (!scene_node_const_stack_push(&stack, &count, &capacity, child)) {
                free(stack);
                return INT32_MAX;
            }
        }
    }
    free(stack);
    return total;
}

/// @brief Compose `node->world_matrix` from its ancestors and read the translation column.
void scene_node_get_world_position(rt_scene_node3d *node, double *x, double *y, double *z) {
    if (!x || !y || !z) {
        return;
    }
    *x = 0.0;
    *y = 0.0;
    *z = 0.0;
    if (!node)
        return;
    recompute_world_matrix(node);
    *x = scene3d_clamp_abs_or(node->world_matrix[3], 0.0);
    *y = scene3d_clamp_abs_or(node->world_matrix[7], 0.0);
    *z = scene3d_clamp_abs_or(node->world_matrix[11], 0.0);
}

/// @brief Read this node's world-space rotation as a quaternion (composing parent rotations).
void scene_node_get_world_rotation(rt_scene_node3d *node, double *out_quat) {
    if (!out_quat) {
        return;
    }
    quat_identity(out_quat);
    if (!node)
        return;
    recompute_world_matrix(node);
    quat_from_world_matrix(node->world_matrix, out_quat);
}

/// @brief Set a node's world-space TRS, working backward through parents to update local TRS.
///
/// Inverts the parent chain's world matrix and applies it to the
/// requested world transform so the resulting local transform
/// produces the requested world pose. Used by physics → scene
/// sync to apply rigid-body world poses to scene nodes.
static void scene_node_set_world_transform(rt_scene_node3d *node,
                                           const double *world_pos,
                                           const double *world_quat) {
    double world_rot[4];
    double inv_parent[16];
    if (!node || !world_pos || !world_quat)
        return;
    world_rot[0] = world_quat[0];
    world_rot[1] = world_quat[1];
    world_rot[2] = world_quat[2];
    world_rot[3] = world_quat[3];
    scene3d_quat_normalize_local(world_rot);

    if (!node->parent) {
        node->position[0] = scene3d_clamp_abs_or(world_pos[0], 0.0);
        node->position[1] = scene3d_clamp_abs_or(world_pos[1], 0.0);
        node->position[2] = scene3d_clamp_abs_or(world_pos[2], 0.0);
        node->rotation[0] = world_rot[0];
        node->rotation[1] = world_rot[1];
        node->rotation[2] = world_rot[2];
        node->rotation[3] = world_rot[3];
        mark_dirty(node);
        return;
    }

    recompute_world_matrix(node->parent);
    {
        double safe_world_pos[3] = {scene3d_clamp_abs_or(world_pos[0], 0.0),
                                    scene3d_clamp_abs_or(world_pos[1], 0.0),
                                    scene3d_clamp_abs_or(world_pos[2], 0.0)};
        if (mat4d_invert(node->parent->world_matrix, inv_parent) == 0) {
            mat4d_transform_point(inv_parent, safe_world_pos, node->position);
        } else {
            memcpy(node->position, safe_world_pos, sizeof(node->position));
        }
    }
    {
        double parent_world_rot[4];
        double inv_parent_rot[4];
        scene_node_get_world_rotation(node->parent, parent_world_rot);
        quat_conjugate_local(parent_world_rot, inv_parent_rot);
        quat_mul_local(inv_parent_rot, world_rot, node->rotation);
    }
    node->position[0] = scene3d_clamp_abs_or(node->position[0], 0.0);
    node->position[1] = scene3d_clamp_abs_or(node->position[1], 0.0);
    node->position[2] = scene3d_clamp_abs_or(node->position[2], 0.0);
    node->scale_xyz[0] = scene3d_scale_or_unit(node->scale_xyz[0]);
    node->scale_xyz[1] = scene3d_scale_or_unit(node->scale_xyz[1]);
    node->scale_xyz[2] = scene3d_scale_or_unit(node->scale_xyz[2]);

    mark_dirty(node);
}

/// @brief Shift an entire root-level subtree by subtracting a world-space delta.
static void scene_node_rebase_root_child(rt_scene_node3d *node, const double delta[3]) {
    double world_pos[3];
    double world_rot[4];
    if (!node || !delta)
        return;
    node = scene_node3d_checked(node);
    if (!node)
        return;
    scene_node_get_world_position(node, &world_pos[0], &world_pos[1], &world_pos[2]);
    scene_node_get_world_rotation(node, world_rot);
    world_pos[0] = scene3d_clamp_abs_or(world_pos[0] - delta[0], 0.0);
    world_pos[1] = scene3d_clamp_abs_or(world_pos[1] - delta[1], 0.0);
    world_pos[2] = scene3d_clamp_abs_or(world_pos[2] - delta[2], 0.0);
    scene_node_set_world_transform(node, world_pos, world_rot);
}

/// @brief Move the node by the per-frame "root motion" delta produced by the bound animator.
///
/// Animations whose root bone moves (walk cycles, jumps) report the
/// per-frame translation/rotation as a delta; here we apply it to
/// the node's local transform so the character actually traverses.
static void scene_node_apply_root_motion(rt_scene_node3d *node) {
    void *delta;
    void *delta_rot;
    void *node_rot;
    void *combined_rot;
    void *animator =
        node ? rt_g3d_checked_or_null(node->bound_animator, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)
             : NULL;
    if (!node || !animator)
        return;
    delta = rt_anim_controller3d_consume_root_motion(animator);
    delta_rot = rt_anim_controller3d_consume_root_motion_rotation(animator);
    if (!delta) {
        scene3d_release_ref(&delta_rot);
        return;
    }
    node->position[0] = scene3d_clamp_abs_or(scene3d_clamp_abs_or(node->position[0], 0.0) +
                                                 scene3d_clamp_abs_or(rt_vec3_x(delta), 0.0),
                                             0.0);
    node->position[1] = scene3d_clamp_abs_or(scene3d_clamp_abs_or(node->position[1], 0.0) +
                                                 scene3d_clamp_abs_or(rt_vec3_y(delta), 0.0),
                                             0.0);
    node->position[2] = scene3d_clamp_abs_or(scene3d_clamp_abs_or(node->position[2], 0.0) +
                                                 scene3d_clamp_abs_or(rt_vec3_z(delta), 0.0),
                                             0.0);
    if (delta_rot) {
        node_rot =
            rt_quat_new(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
        combined_rot = node_rot ? rt_quat_mul(node_rot, delta_rot) : NULL;
        if (combined_rot) {
            node->rotation[0] = rt_quat_x(combined_rot);
            node->rotation[1] = rt_quat_y(combined_rot);
            node->rotation[2] = rt_quat_z(combined_rot);
            node->rotation[3] = rt_quat_w(combined_rot);
            scene3d_quat_normalize_local(node->rotation);
        }
        scene3d_release_ref(&combined_rot);
        scene3d_release_ref(&node_rot);
        scene3d_release_ref(&delta_rot);
    }
    if (rt_obj_release_check0(delta))
        rt_obj_free(delta);
    mark_dirty(node);
}

/// @brief Walk the subtree synchronising node transforms with bound bodies / animators.
///
/// Per node, depending on `sync_mode`:
///   - `BODY_TO_NODE`: copies the rigid body's world pose into the node.
///   - `NODE_TO_BODY`: pushes the node's transform back into the body.
///   - root motion: bumps the local TRS by the animator's per-frame delta.
/// Then recurses into children.
static void scene_node_sync_recursive(rt_scene_node3d *node, double dt) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("Scene3D.SyncBindings: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        int64_t mode;
        int pull_from_body;
        int push_to_body;
        int body_is_kinematic;
        void *node_animator =
            rt_g3d_checked_or_null(current->bound_node_animator, RT_G3D_NODEANIMATOR3D_CLASS_ID);
        void *animator =
            rt_g3d_checked_or_null(current->bound_animator, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);

        if (node_animator)
            node_animator_update((rt_node_animator3d *)node_animator, dt);

        mode = current->sync_mode;
        body_is_kinematic = current->bound_body ? rt_body3d_is_kinematic(current->bound_body) : 0;
        pull_from_body = current->bound_body &&
                         (mode == RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY ||
                          (mode == RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC && !body_is_kinematic));
        push_to_body = current->bound_body &&
                       (mode == RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE ||
                        (mode == RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC && body_is_kinematic));

        if (pull_from_body) {
            double world_pos[3];
            double world_quat[4];
            rt_body3d_get_pose_raw(current->bound_body, world_pos, world_quat, NULL);
            scene_node_set_world_transform(current, world_pos, world_quat);
        } else if (animator &&
                   (mode == RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION || push_to_body)) {
            scene_node_apply_root_motion(current);
        }

        if (push_to_body) {
            double world_pos[3];
            double world_quat[4];
            scene_node_get_world_position(current, &world_pos[0], &world_pos[1], &world_pos[2]);
            scene_node_get_world_rotation(current, world_quat);
            rt_body3d_set_position(current->bound_body, world_pos[0], world_pos[1], world_pos[2]);
            {
                void *quat =
                    rt_quat_new(world_quat[0], world_quat[1], world_quat[2], world_quat[3]);
                rt_body3d_set_orientation(current->bound_body, quat);
                scene3d_release_ref(&quat);
            }
        }

        for (int32_t i = scene3d_node_child_count(current) - 1; i >= 0; i--) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D.SyncBindings: traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief True if `target` appears anywhere in the subtree rooted at `root`.
/// Used to prevent cycles when reparenting (don't re-attach a node under one of its descendants).
int node_contains(const rt_scene_node3d *root, const rt_scene_node3d *target) {
    const rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    root = scene_node3d_checked_const(root);
    target = scene_node3d_checked_const(target);
    if (!root || !target)
        return 0;
    if (!scene_node_const_stack_push(&stack, &count, &capacity, root))
        return root == target;
    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (current == target) {
            free(stack);
            return 1;
        }
        for (int32_t i = 0, child_count = scene3d_node_child_count(current); i < child_count;
             i++) {
            const rt_scene_node3d *child = scene_node3d_checked(current->children[i]);
            if (!scene_node_const_stack_push(&stack, &count, &capacity, child)) {
                rt_trap("SceneNode3D: traversal stack allocation failed");
                free(stack);
                return 0;
            }
        }
    }
    free(stack);
    return 0;
}

/// @brief Compute the local-space AABB of `mesh` by min/maxing every vertex position.
/// Cached on the mesh so subsequent calls are O(1).
void scene_mesh_bounds(rt_mesh3d *mesh, float out_min[3], float out_max[3], float *out_radius) {
    if (!mesh) {
        if (out_min)
            out_min[0] = out_min[1] = out_min[2] = 0.0f;
        if (out_max)
            out_max[0] = out_max[1] = out_max[2] = 0.0f;
        if (out_radius)
            *out_radius = 0.0f;
        return;
    }
    rt_mesh3d_refresh_bounds(mesh);
    if (out_min) {
        out_min[0] = mesh->aabb_min[0];
        out_min[1] = mesh->aabb_min[1];
        out_min[2] = mesh->aabb_min[2];
    }
    if (out_max) {
        out_max[0] = mesh->aabb_max[0];
        out_max[1] = mesh->aabb_max[1];
        out_max[2] = mesh->aabb_max[2];
    }
    if (out_radius)
        *out_radius = mesh->bsphere_radius;
}

/// @brief Multiply a local-space point by the node's world matrix; result lands in `out`.
static void scene_world_point(const double *world_matrix, const float local[3], float out[3]) {
    double p[3];
    double x;
    double y;
    double z;
    if (!world_matrix || !local || !out)
        return;
    if (!scene3d_matrix_is_finite(world_matrix)) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    p[0] = scene3d_clamp_abs_or((double)local[0], 0.0);
    p[1] = scene3d_clamp_abs_or((double)local[1], 0.0);
    p[2] = scene3d_clamp_abs_or((double)local[2], 0.0);
    x = world_matrix[0] * p[0] + world_matrix[1] * p[1] + world_matrix[2] * p[2] +
        world_matrix[3];
    y = world_matrix[4] * p[0] + world_matrix[5] * p[1] + world_matrix[6] * p[2] +
        world_matrix[7];
    z = world_matrix[8] * p[0] + world_matrix[9] * p[1] + world_matrix[10] * p[2] +
        world_matrix[11];
    out[0] = scene3d_float_or_zero(scene3d_clamp_abs_or(x, 0.0));
    out[1] = scene3d_float_or_zero(scene3d_clamp_abs_or(y, 0.0));
    out[2] = scene3d_float_or_zero(scene3d_clamp_abs_or(z, 0.0));
}

/// @brief Normalize a float[3] vector in-place, substituting a caller-supplied fallback
///        direction when the vector is degenerate (zero-length or non-finite magnitude).
/// @details Used to normalize light direction vectors that have been transformed into
///   world space via the node's 3×3 rotation sub-matrix. A scale component in the
///   matrix can inflate or shrink the transformed direction, so re-normalization is
///   always required. The 1e-8 threshold guards against divide-by-near-zero; the
///   (0, 0, -1) fallback used by the caller makes a degenerate light point forward
///   rather than disappear.
/// @param v          float[3] vector to normalize in-place; must not be NULL.
/// @param fallback_x X component of the replacement direction on degenerate input.
/// @param fallback_y Y component of the replacement direction on degenerate input.
/// @param fallback_z Z component of the replacement direction on degenerate input.
static void scene_normalize_f32_vec3(float v[3],
                                     float fallback_x,
                                     float fallback_y,
                                     float fallback_z) {
    float len;
    if (!v)
        return;
    len = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!isfinite(len) || len <= 1e-8f) {
        v[0] = fallback_x;
        v[1] = fallback_y;
        v[2] = fallback_z;
        return;
    }
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

/// @brief Transform a node-attached light into world space for the current draw snapshot.
static void scene_transform_node_light(rt_scene_node3d *node,
                                       const rt_light3d *src,
                                       rt_light3d *dst) {
    float local_pos[3];
    float world_pos[3];
    float world_dir[3];
    double basis[9];
    double local_dir[3];
    if (!node || !src || !dst)
        return;
    recompute_world_matrix(node);
    memcpy(dst, src, sizeof(*dst));
    local_pos[0] = (float)src->position[0];
    local_pos[1] = (float)src->position[1];
    local_pos[2] = (float)src->position[2];
    scene_world_point(node->world_matrix, local_pos, world_pos);
    dst->position[0] = world_pos[0];
    dst->position[1] = world_pos[1];
    dst->position[2] = world_pos[2];

    local_dir[0] = scene3d_finite_or(src->direction[0], 0.0);
    local_dir[1] = scene3d_finite_or(src->direction[1], 0.0);
    local_dir[2] = scene3d_finite_or(src->direction[2], -1.0);
    if (scene_extract_rotation_basis(node->world_matrix, basis)) {
        world_dir[0] =
            (float)(basis[0] * local_dir[0] + basis[1] * local_dir[1] + basis[2] * local_dir[2]);
        world_dir[1] =
            (float)(basis[3] * local_dir[0] + basis[4] * local_dir[1] + basis[5] * local_dir[2]);
        world_dir[2] =
            (float)(basis[6] * local_dir[0] + basis[7] * local_dir[1] + basis[8] * local_dir[2]);
    } else {
        world_dir[0] = (float)local_dir[0];
        world_dir[1] = (float)local_dir[1];
        world_dir[2] = (float)local_dir[2];
    }
    scene_normalize_f32_vec3(world_dir, 0.0f, 0.0f, -1.0f);
    dst->direction[0] = world_dir[0];
    dst->direction[1] = world_dir[1];
    dst->direction[2] = world_dir[2];
}

/// @brief Recursively collect world-space lights from visible nodes into the draw snapshot.
/// @details Traverses the subtree rooted at @p node, skipping invisible nodes entirely
///   (their children are also skipped, consistent with visibility culling elsewhere).
///   For each visible node that carries a light, `scene_transform_node_light` writes
///   the world-space copy into @p storage[*io_count] and adds its address to
///   @p out_lights[*io_count] before incrementing the counter. Collection stops when
///   the hardware light limit `VGFX3D_MAX_LIGHTS` is reached; excess lights are silently
///   dropped, which is the standard GPU behavior for too many dynamic lights.
/// @param node       Root of the subtree to search.
/// @param storage    Pre-allocated array of at least VGFX3D_MAX_LIGHTS rt_light3d structs
///                   used as scratch space for world-space copies.
/// @param out_lights Output pointer array; receives addresses into @p storage.
/// @param io_count   In/out: current light count; incremented for each light found.
static void scene_collect_node_lights(rt_scene_node3d *node,
                                      rt_light3d *storage,
                                      rt_light3d **out_lights,
                                      int32_t *io_count) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !storage || !out_lights || !io_count)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("Scene3D.Draw: light traversal stack allocation failed");
        return;
    }
    while (count > 0 && *io_count < VGFX3D_MAX_LIGHTS) {
        rt_scene_node3d *current = stack[--count];
        if (!current->visible)
            continue;
        if (current->light) {
            int32_t index = *io_count;
            scene_transform_node_light(
                current, (const rt_light3d *)current->light, &storage[index]);
            out_lights[index] = &storage[index];
            *io_count = index + 1;
            if (*io_count >= VGFX3D_MAX_LIGHTS)
                break;
        }
        for (int32_t i = scene3d_node_child_count(current) - 1; i >= 0; i--) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("Scene3D.Draw: light traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief Initialise a min/max pair so subsequent point inserts grow a valid AABB.
void scene_bounds_reset(float out_min[3], float out_max[3]) {
    if (out_min) {
        out_min[0] = FLT_MAX;
        out_min[1] = FLT_MAX;
        out_min[2] = FLT_MAX;
    }
    if (out_max) {
        out_max[0] = -FLT_MAX;
        out_max[1] = -FLT_MAX;
        out_max[2] = -FLT_MAX;
    }
}

/// @brief Reset an AABB to the empty state (min = +DBL_MAX, max = -DBL_MAX) ready for accumulation.
void scene_bounds_reset_d(double out_min[3], double out_max[3]) {
    if (out_min) {
        out_min[0] = DBL_MAX;
        out_min[1] = DBL_MAX;
        out_min[2] = DBL_MAX;
    }
    if (out_max) {
        out_max[0] = -DBL_MAX;
        out_max[1] = -DBL_MAX;
        out_max[2] = -DBL_MAX;
    }
}

/// @brief Expand @p bounds_min/@p bounds_max to include @p point.
static void scene_bounds_include_point(float bounds_min[3],
                                       float bounds_max[3],
                                       const float point[3]) {
    if (!bounds_min || !bounds_max || !point)
        return;
    for (int i = 0; i < 3; i++) {
        if (point[i] < bounds_min[i])
            bounds_min[i] = point[i];
        if (point[i] > bounds_max[i])
            bounds_max[i] = point[i];
    }
}

/// @brief Expand an AABB in place to contain a point.
void scene_bounds_include_point_d(double bounds_min[3],
                                  double bounds_max[3],
                                  const double point[3]) {
    double p[3];
    if (!bounds_min || !bounds_max || !point)
        return;
    p[0] = scene3d_clamp_abs_or(point[0], 0.0);
    p[1] = scene3d_clamp_abs_or(point[1], 0.0);
    p[2] = scene3d_clamp_abs_or(point[2], 0.0);
    for (int i = 0; i < 3; i++) {
        if (p[i] < bounds_min[i])
            bounds_min[i] = p[i];
        if (p[i] > bounds_max[i])
            bounds_max[i] = p[i];
    }
}

/// @brief Transform a local AABB by a matrix into a world AABB by bounding its eight rotated
/// corners.
int scene3d_transform_aabb_d(const float obj_min[3],
                             const float obj_max[3],
                             const double world_matrix[16],
                             double out_min[3],
                             double out_max[3]) {
    if (!out_min || !out_max)
        return 0;
    scene_bounds_reset_d(out_min, out_max);
    if (!obj_min || !obj_max || !world_matrix) {
        out_min[0] = out_min[1] = out_min[2] = 0.0;
        out_max[0] = out_max[1] = out_max[2] = 0.0;
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(obj_min[i]) || !isfinite(obj_max[i])) {
            out_min[0] = out_min[1] = out_min[2] = 0.0;
            out_max[0] = out_max[1] = out_max[2] = 0.0;
            return 0;
        }
    }
    if (!scene3d_matrix_is_finite(world_matrix)) {
        out_min[0] = out_min[1] = out_min[2] = 0.0;
        out_max[0] = out_max[1] = out_max[2] = 0.0;
        return 0;
    }
    float safe_min[3];
    float safe_max[3];
    for (int axis = 0; axis < 3; axis++) {
        if (obj_min[axis] <= obj_max[axis]) {
            safe_min[axis] = obj_min[axis];
            safe_max[axis] = obj_max[axis];
        } else {
            safe_min[axis] = obj_max[axis];
            safe_max[axis] = obj_min[axis];
        }
    }
    for (int corner = 0; corner < 8; corner++) {
        double cx = (corner & 1) ? (double)safe_max[0] : (double)safe_min[0];
        double cy = (corner & 2) ? (double)safe_max[1] : (double)safe_min[1];
        double cz = (corner & 4) ? (double)safe_max[2] : (double)safe_min[2];
        double p[3];
        p[0] = scene3d_clamp_abs_or(
            world_matrix[0] * cx + world_matrix[1] * cy + world_matrix[2] * cz + world_matrix[3],
            0.0);
        p[1] = scene3d_clamp_abs_or(
            world_matrix[4] * cx + world_matrix[5] * cy + world_matrix[6] * cz + world_matrix[7],
            0.0);
        p[2] = scene3d_clamp_abs_or(world_matrix[8] * cx + world_matrix[9] * cy +
                                        world_matrix[10] * cz + world_matrix[11],
                                    0.0);
        scene_bounds_include_point_d(out_min, out_max, p);
    }
    return 1;
}

/// @brief Transform the 8 corners of a local AABB and union them into @p bounds_min/max.
static void scene_bounds_include_aabb(float bounds_min[3],
                                      float bounds_max[3],
                                      const float local_min[3],
                                      const float local_max[3],
                                      const double *local_to_root) {
    float local_corner[3];
    float root_corner[3];
    if (!bounds_min || !bounds_max || !local_min || !local_max || !local_to_root)
        return;
    for (int xi = 0; xi < 2; xi++) {
        for (int yi = 0; yi < 2; yi++) {
            for (int zi = 0; zi < 2; zi++) {
                local_corner[0] = xi ? local_max[0] : local_min[0];
                local_corner[1] = yi ? local_max[1] : local_min[1];
                local_corner[2] = zi ? local_max[2] : local_min[2];
                scene_world_point(local_to_root, local_corner, root_corner);
                scene_bounds_include_point(bounds_min, bounds_max, root_corner);
            }
        }
    }
}

typedef struct {
    rt_scene_node3d *node;
    double node_to_root[16];
} scene_bounds_stack_item_t;

/// @brief Push (node, node→root matrix) onto a growable explicit traversal stack.
/// @details Used instead of recursion so deep scene hierarchies cannot overflow
///          the C stack. Capacity doubles on demand (seeded at 64) via an
///          overflow-guarded realloc.
/// @return 1 on success — and also on NULL/missing arguments, treated as a no-op;
///         0 only when capacity growth overflows or realloc fails.
static int scene_bounds_stack_push(scene_bounds_stack_item_t **stack,
                                   size_t *count,
                                   size_t *capacity,
                                   rt_scene_node3d *node,
                                   const double *node_to_root) {
    rt_scene_node3d *checked;
    if (!stack || !count || !capacity || !node || !node_to_root)
        return 1;
    checked = scene_node3d_checked(node);
    if (!checked)
        return 1;
    if (*count >= *capacity) {
        if (!scene3d_grow_stack_storage((void **)stack, capacity, sizeof(**stack)))
            return 0;
    }
    (*stack)[*count].node = checked;
    memcpy((*stack)[*count].node_to_root, node_to_root, sizeof(double) * 16);
    (*count)++;
    return 1;
}

/// @brief Compute a node subtree's local-space AABB relative to the queried root node.
///
/// @param node Current subtree node.
/// @param node_to_root Row-major matrix mapping @p node local space into the queried root's local
/// space.
/// @param out_min Running subtree minimum.
/// @param out_max Running subtree maximum.
/// @return 1 if the subtree contributed any mesh bounds, otherwise 0.
int scene_node_collect_subtree_bounds(rt_scene_node3d *node,
                                      const double *node_to_root,
                                      float out_min[3],
                                      float out_max[3]) {
    int has_bounds = 0;
    scene_bounds_stack_item_t *stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !node_to_root || !out_min || !out_max)
        return 0;

    if (!scene_bounds_stack_push(&stack, &count, &capacity, node, node_to_root)) {
        rt_trap("SceneNode3D.GetAabb: traversal stack allocation failed");
        return 0;
    }
    while (count > 0) {
        scene_bounds_stack_item_t item = stack[--count];
        void *mesh = rt_g3d_checked_or_null(item.node->mesh, RT_G3D_MESH3D_CLASS_ID);
        if (mesh) {
            float mesh_min[3];
            float mesh_max[3];
            scene_mesh_bounds((rt_mesh3d *)mesh, mesh_min, mesh_max, NULL);
            scene_bounds_include_aabb(out_min, out_max, mesh_min, mesh_max, item.node_to_root);
            has_bounds = 1;
        }
        for (int32_t i = scene3d_node_child_count(item.node) - 1; i >= 0; i--) {
            rt_scene_node3d *child = scene_node3d_checked(item.node->children[i]);
            if (!child)
                continue;
            double child_local[16];
            double child_to_root[16];
            build_trs_matrix(child->position, child->rotation, child->scale_xyz, child_local);
            mat4d_mul(item.node_to_root, child_local, child_to_root);
            if (!scene_bounds_stack_push(&stack, &count, &capacity, child, child_to_root)) {
                rt_trap("SceneNode3D.GetAabb: traversal stack allocation failed");
                free(stack);
                return has_bounds;
            }
        }
    }
    free(stack);

    return has_bounds;
}

/// @brief Depth-first search for a node whose `name` matches `target` (NULL on miss).
rt_scene_node3d *find_by_name(rt_scene_node3d *node, const char *target) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !target)
        return NULL;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("SceneNode3D.Find: traversal stack allocation failed");
        return NULL;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        if (current->name) {
            if (!rt_string_is_handle(current->name)) {
                current->name = NULL;
            } else {
                const char *s = rt_string_cstr(current->name);
                if (s && strcmp(s, target) == 0) {
                    free(stack);
                    return current;
                }
            }
        }
        for (int32_t i = scene3d_node_child_count(current) - 1; i >= 0; i--) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("SceneNode3D.Find: traversal stack allocation failed");
                free(stack);
                return NULL;
            }
        }
    }
    free(stack);
    return NULL;
}

/// @brief Read a boxed Vec3 into a double[3], trapping with @p trap_message on a non-Vec3 handle.
int scene3d_read_vec3d(void *obj, double out[3], const char *trap_message) {
    if (!out)
        return 0;
    if (!rt_g3d_is_vec3(obj)) {
        rt_trap(trap_message);
        return 0;
    }
    out[0] = scene3d_clamp_abs_or(rt_vec3_x(obj), 0.0);
    out[1] = scene3d_clamp_abs_or(rt_vec3_y(obj), 0.0);
    out[2] = scene3d_clamp_abs_or(rt_vec3_z(obj), 0.0);
    return 1;
}

/// @brief Get a node's own mesh AABB in world space. Transform-only nodes return false.
int scene3d_node_world_mesh_aabb(rt_scene_node3d *node,
                                 double world_min[3],
                                 double world_max[3]) {
    float local_min[3];
    float local_max[3];
    if (!node || !node->mesh || !world_min || !world_max)
        return 0;
    recompute_world_matrix(node);
    scene_mesh_bounds((rt_mesh3d *)node->mesh, local_min, local_max, NULL);
    if (!scene3d_transform_aabb_d(local_min, local_max, node->world_matrix, world_min, world_max))
        return 0;
    scene3d_canonicalize_aabb_d(world_min, world_max);
    return 1;
}

/// @brief Whether two AABBs overlap on all three axes.
int scene3d_aabb_intersects_aabb(const double a_min[3],
                                 const double a_max[3],
                                 const double b_min[3],
                                 const double b_max[3]) {
    double amin[3] = {a_min ? a_min[0] : 0.0, a_min ? a_min[1] : 0.0,
                      a_min ? a_min[2] : 0.0};
    double amax[3] = {a_max ? a_max[0] : 0.0, a_max ? a_max[1] : 0.0,
                      a_max ? a_max[2] : 0.0};
    double bmin[3] = {b_min ? b_min[0] : 0.0, b_min ? b_min[1] : 0.0,
                      b_min ? b_min[2] : 0.0};
    double bmax[3] = {b_max ? b_max[0] : 0.0, b_max ? b_max[1] : 0.0,
                      b_max ? b_max[2] : 0.0};
    scene3d_canonicalize_aabb_d(amin, amax);
    scene3d_canonicalize_aabb_d(bmin, bmax);
    return amin[0] <= bmax[0] && amax[0] >= bmin[0] && amin[1] <= bmax[1] &&
           amax[1] >= bmin[1] && amin[2] <= bmax[2] && amax[2] >= bmin[2];
}

/// @brief Whether a sphere overlaps an AABB, via closest-point squared distance vs radius².
int scene3d_aabb_intersects_sphere(const double aabb_min[3],
                                   const double aabb_max[3],
                                   const double center[3],
                                   double radius) {
    double mn[3] = {aabb_min ? aabb_min[0] : 0.0, aabb_min ? aabb_min[1] : 0.0,
                    aabb_min ? aabb_min[2] : 0.0};
    double mx[3] = {aabb_max ? aabb_max[0] : 0.0, aabb_max ? aabb_max[1] : 0.0,
                    aabb_max ? aabb_max[2] : 0.0};
    double c[3] = {center ? center[0] : 0.0, center ? center[1] : 0.0,
                   center ? center[2] : 0.0};
    double dist2 = 0.0;
    scene3d_canonicalize_aabb_d(mn, mx);
    scene3d_sanitize_vec3d(c, 0.0);
    radius = scene3d_distance_or_zero(radius);
    for (int i = 0; i < 3; ++i) {
        double v = c[i];
        if (v < mn[i]) {
            double d = mn[i] - v;
            dist2 += d * d;
        } else if (v > mx[i]) {
            double d = v - mx[i];
            dist2 += d * d;
        }
        if (!isfinite(dist2))
            return 0;
    }
    return dist2 <= radius * radius;
}

/// @brief Ray-vs-AABB slab test with a normalized direction and finite max distance.
int scene3d_ray_intersects_aabb(const double origin[3],
                                const double direction[3],
                                const double aabb_min[3],
                                const double aabb_max[3],
                                double max_distance,
                                double *out_t) {
    double o[3] = {origin ? origin[0] : 0.0, origin ? origin[1] : 0.0,
                   origin ? origin[2] : 0.0};
    double dvec[3] = {direction ? direction[0] : 0.0, direction ? direction[1] : 0.0,
                      direction ? direction[2] : 0.0};
    double mn[3] = {aabb_min ? aabb_min[0] : 0.0, aabb_min ? aabb_min[1] : 0.0,
                    aabb_min ? aabb_min[2] : 0.0};
    double mx[3] = {aabb_max ? aabb_max[0] : 0.0, aabb_max ? aabb_max[1] : 0.0,
                    aabb_max ? aabb_max[2] : 0.0};
    double tmin = 0.0;
    double tmax;
    scene3d_sanitize_vec3d(o, 0.0);
    scene3d_sanitize_vec3d(dvec, 0.0);
    if (!scene3d_normalize_vec3d(dvec))
        return 0;
    scene3d_canonicalize_aabb_d(mn, mx);
    if (!isfinite(max_distance))
        max_distance = SCENE3D_ABS_MAX;
    if (max_distance < 0.0)
        return 0;
    max_distance = scene3d_distance_or_zero(max_distance);
    tmax = max_distance;
    for (int i = 0; i < 3; ++i) {
        double axis_origin = o[i];
        double d = dvec[i];
        if (fabs(d) <= 1e-12) {
            if (axis_origin < mn[i] || axis_origin > mx[i])
                return 0;
            continue;
        }
        double inv = 1.0 / d;
        double t1 = (mn[i] - axis_origin) * inv;
        double t2 = (mx[i] - axis_origin) * inv;
        if (!isfinite(t1) || !isfinite(t2))
            return 0;
        if (t1 > t2) {
            double tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > tmin)
            tmin = t1;
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            return 0;
    }
    if (out_t)
        *out_t = scene3d_distance_or_zero(tmin);
    return tmin <= max_distance;
}

typedef struct {
    uint8_t *visible_zones;
    int32_t zone_count;
    int32_t *culled_count;
    int8_t active;
} scene3d_pvs_context_t;

/// @brief Push a (node, inherited-animator) frame onto the index-build traversal stack, growing it.
/// @return 1 on success (or NULL no-op), 0 on overflow/allocation failure.
int scene_index_build_stack_push(scene_index_build_stack_item_t **stack,
                                 size_t *count,
                                 size_t *capacity,
                                 rt_scene_node3d *node,
                                 void *inherited_animator) {
    rt_scene_node3d *checked;
    if (!stack || !count || !capacity || !node)
        return 1;
    checked = scene_node3d_checked(node);
    if (!checked)
        return 1;
    if (*count >= *capacity) {
        if (!scene3d_grow_stack_storage((void **)stack, capacity, sizeof(**stack)))
            return 0;
    }
    (*stack)[*count].node = checked;
    (*stack)[*count].inherited_animator = inherited_animator;
    (*count)++;
    return 1;
}

/// @brief Ensure the spatial index can hold @p needed leaf entries (doubling growth).

/// @brief Grow the scene's visibility-zone array to hold at least @p needed zones (doubling
/// growth).
/// @return 1 on success or when capacity already suffices; 0 on overflow or allocation failure.
static int scene3d_visibility_zone_ensure_capacity(rt_scene3d *scene, int32_t needed) {
    int32_t new_capacity;
    rt_scene3d_visibility_zone *grown;
    if (!scene || needed < 0)
        return 0;
    if (needed <= scene->visibility_zone_capacity)
        return 1;
    new_capacity = scene->visibility_zone_capacity < 8 ? 8 : scene->visibility_zone_capacity;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(scene->visibility_zones[0]))
        return 0;
    grown = (rt_scene3d_visibility_zone *)realloc(
        scene->visibility_zones, (size_t)new_capacity * sizeof(scene->visibility_zones[0]));
    if (!grown)
        return 0;
    scene->visibility_zones = grown;
    scene->visibility_zone_capacity = new_capacity;
    return 1;
}

/// @brief Grow the scene's visibility-portal array to hold at least @p needed portals (doubling
/// growth).
/// @return 1 on success or when capacity already suffices; 0 on overflow or allocation failure.
static int scene3d_visibility_portal_ensure_capacity(rt_scene3d *scene, int32_t needed) {
    int32_t new_capacity;
    rt_scene3d_visibility_portal *grown;
    if (!scene || needed < 0)
        return 0;
    if (needed <= scene->visibility_portal_capacity)
        return 1;
    new_capacity = scene->visibility_portal_capacity < 8 ? 8 : scene->visibility_portal_capacity;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(scene->visibility_portals[0]))
        return 0;
    grown = (rt_scene3d_visibility_portal *)realloc(
        scene->visibility_portals, (size_t)new_capacity * sizeof(scene->visibility_portals[0]));
    if (!grown)
        return 0;
    scene->visibility_portals = grown;
    scene->visibility_portal_capacity = new_capacity;
    return 1;
}

static int32_t scene3d_visibility_array_count_or_zero(const void *array,
                                                      int32_t count,
                                                      int32_t capacity) {
    if (!array || count <= 0 || capacity <= 0)
        return 0;
    return count < capacity ? count : capacity;
}

static int32_t scene3d_visibility_zone_count_safe(const rt_scene3d *scene) {
    return scene ? scene3d_visibility_array_count_or_zero(scene->visibility_zones,
                                                          scene->visibility_zone_count,
                                                          scene->visibility_zone_capacity)
                 : 0;
}

static int32_t scene3d_visibility_portal_count_safe(const rt_scene3d *scene) {
    return scene ? scene3d_visibility_array_count_or_zero(scene->visibility_portals,
                                                          scene->visibility_portal_count,
                                                          scene->visibility_portal_capacity)
                 : 0;
}

/// @brief Test whether @p point lies inside the zone's world-space AABB (inclusive bounds).
static int scene3d_visibility_zone_contains_point(const rt_scene3d_visibility_zone *zone,
                                                  const double point[3]) {
    return zone && point && point[0] >= zone->world_min[0] && point[0] <= zone->world_max[0] &&
           point[1] >= zone->world_min[1] && point[1] <= zone->world_max[1] &&
           point[2] >= zone->world_min[2] && point[2] <= zone->world_max[2];
}

/// @brief Find the visibility zone containing the camera eye (including its shake offset).
/// @return Index of the first containing zone, or -1 if the eye is non-finite or outside every
/// zone.
static int scene3d_visibility_find_camera_zone(const rt_scene3d *scene, const rt_camera3d *cam) {
    double eye[3];
    int32_t zone_count;
    if (!scene || !cam)
        return -1;
    zone_count = scene3d_visibility_zone_count_safe(scene);
    eye[0] = cam->eye[0] + cam->shake_offset[0];
    eye[1] = cam->eye[1] + cam->shake_offset[1];
    eye[2] = cam->eye[2] + cam->shake_offset[2];
    if (!isfinite(eye[0]) || !isfinite(eye[1]) || !isfinite(eye[2]))
        return -1;
    for (int32_t i = 0; i < zone_count; ++i) {
        if (scene3d_visibility_zone_contains_point(&scene->visibility_zones[i], eye))
            return i;
    }
    return -1;
}

/// @brief Build the potentially-visible-set for the camera's current zone by portal flood-fill.
/// @details Locates the zone containing the camera, then breadth-first traverses the directed
///          visibility portals to mark every zone reachable from it. The result is a per-zone
///          visibility bitmap consumed by scene3d_pvs_allows_aabb. Left inactive (zeroed) when the
///          scene has no zones, the camera is outside all zones, or allocation fails.
/// @return 1 if an active PVS context was produced; 0 otherwise (with @p out zeroed).
static int scene3d_build_pvs_context(rt_scene3d *scene,
                                     rt_camera3d *cam,
                                     scene3d_pvs_context_t *out) {
    int32_t zone_count;
    int32_t portal_count;
    int32_t camera_zone;
    int32_t *queue = NULL;
    int32_t head = 0;
    int32_t tail = 0;
    if (!scene || !cam || !out)
        return 0;
    zone_count = scene3d_visibility_zone_count_safe(scene);
    portal_count = scene3d_visibility_portal_count_safe(scene);
    if (zone_count <= 0)
        return 0;
    camera_zone = scene3d_visibility_find_camera_zone(scene, cam);
    if (camera_zone < 0)
        return 0;
    out->visible_zones = (uint8_t *)calloc((size_t)zone_count, 1);
    queue = (int32_t *)malloc((size_t)zone_count * sizeof(queue[0]));
    if (!out->visible_zones || !queue) {
        free(out->visible_zones);
        free(queue);
        memset(out, 0, sizeof(*out));
        return 0;
    }
    out->zone_count = zone_count;
    out->active = 1;
    out->visible_zones[camera_zone] = 1;
    queue[tail++] = camera_zone;
    while (head < tail) {
        int32_t zone = queue[head++];
        for (int32_t i = 0; i < portal_count; ++i) {
            const rt_scene3d_visibility_portal *portal = &scene->visibility_portals[i];
            if (portal->from_zone != zone || portal->to_zone < 0 ||
                portal->to_zone >= zone_count)
                continue;
            if (!out->visible_zones[portal->to_zone]) {
                out->visible_zones[portal->to_zone] = 1;
                queue[tail++] = portal->to_zone;
            }
        }
    }
    free(queue);
    return 1;
}

/// @brief Release a PVS context's zone bitmap and reset it to the inactive state. Safe on NULL.
static void scene3d_pvs_context_clear(scene3d_pvs_context_t *pvs) {
    if (!pvs)
        return;
    free(pvs->visible_zones);
    memset(pvs, 0, sizeof(*pvs));
}

/// @brief Test whether an AABB may be visible under the current PVS.
/// @details An AABB passes if it overlaps no authored zone (zones do not cover it) or overlaps at
///          least one zone flagged visible in @p pvs. Returns visible (1) whenever the PVS is
///          inactive, so visibility culling degrades safely to a no-op.
/// @return 1 if the AABB should be considered visible; 0 if every overlapping zone is culled.
static int scene3d_pvs_allows_aabb(const rt_scene3d *scene,
                                   const scene3d_pvs_context_t *pvs,
                                   const double world_min[3],
                                   const double world_max[3]) {
    int matched_zone = 0;
    int32_t zone_count;
    int32_t visible_zone_count;
    if (!scene || !pvs || !pvs->active || !pvs->visible_zones || !world_min || !world_max)
        return 1;
    zone_count = scene3d_visibility_zone_count_safe(scene);
    visible_zone_count = pvs->zone_count < zone_count ? pvs->zone_count : zone_count;
    for (int32_t i = 0; i < visible_zone_count; ++i) {
        const rt_scene3d_visibility_zone *zone = &scene->visibility_zones[i];
        if (!scene3d_aabb_intersects_aabb(world_min, world_max, zone->world_min, zone->world_max))
            continue;
        matched_zone = 1;
        if (pvs->visible_zones[i])
            return 1;
    }
    return matched_zone ? 0 : 1;
}

/// @brief Compute the AABB swept by a ray segment (origin to origin + dir·length) for broadphase
/// culling.
int scene3d_ray_sweep_bounds(const double origin[3],
                             const double direction[3],
                             double max_distance,
                             double out_min[3],
                             double out_max[3]) {
    double o[3] = {origin ? origin[0] : 0.0, origin ? origin[1] : 0.0,
                   origin ? origin[2] : 0.0};
    double d[3] = {direction ? direction[0] : 0.0, direction ? direction[1] : 0.0,
                   direction ? direction[2] : 0.0};
    double end[3];
    if (!origin || !direction || !out_min || !out_max)
        return 0;
    scene3d_sanitize_vec3d(o, 0.0);
    scene3d_sanitize_vec3d(d, 0.0);
    if (!scene3d_normalize_vec3d(d))
        return 0;
    if (!isfinite(max_distance) || max_distance > SCENE3D_ABS_MAX)
        max_distance = SCENE3D_ABS_MAX;
    if (max_distance < 0.0)
        return 0;
    for (int i = 0; i < 3; ++i) {
        double e = o[i] + d[i] * max_distance;
        if (!isfinite(e))
            return 0;
        end[i] = scene3d_clamp_abs_or(e, 0.0);
        out_min[i] = fmin(o[i], end[i]);
        out_max[i] = fmax(o[i], end[i]);
    }
    scene3d_canonicalize_aabb_d(out_min, out_max);
    return 1;
}

/// @brief Compute a world-space AABB enclosing the view frustum of a view-projection matrix.
/// @details Unprojects the eight clip-space corners through the inverse VP and bounds them, giving
/// a
///          coarse AABB the spatial index can use to reject off-screen nodes before exact culling.
static int scene3d_frustum_bounds_from_vp(const float vp[16],
                                          double out_min[3],
                                          double out_max[3]) {
    double m[16];
    double inv[16];
    if (!vp || !out_min || !out_max)
        return 0;
    for (int i = 0; i < 16; ++i) {
        if (!isfinite(vp[i]))
            return 0;
        m[i] = (double)vp[i];
    }
    if (mat4d_invert(m, inv) != 0)
        return 0;
    scene_bounds_reset_d(out_min, out_max);
    for (int xi = 0; xi < 2; ++xi) {
        for (int yi = 0; yi < 2; ++yi) {
            for (int zi = 0; zi < 2; ++zi) {
                double x = xi ? 1.0 : -1.0;
                double y = yi ? 1.0 : -1.0;
                double z = zi ? 1.0 : -1.0;
                double wx = inv[0] * x + inv[1] * y + inv[2] * z + inv[3];
                double wy = inv[4] * x + inv[5] * y + inv[6] * z + inv[7];
                double wz = inv[8] * x + inv[9] * y + inv[10] * z + inv[11];
                double ww = inv[12] * x + inv[13] * y + inv[14] * z + inv[15];
                double point[3];
                if (!isfinite(ww) || fabs(ww) <= 1.0e-12)
                    return 0;
                wx /= ww;
                wy /= ww;
                wz /= ww;
                if (!isfinite(wx) || !isfinite(wy) || !isfinite(wz))
                    return 0;
                point[0] = wx;
                point[1] = wy;
                point[2] = wz;
                scene_bounds_include_point_d(out_min, out_max, point);
            }
        }
    }
    return out_min[0] <= out_max[0] && out_min[1] <= out_max[1] && out_min[2] <= out_max[2];
}

typedef struct {
    rt_scene_node3d *node;
    void *inherited_animator;
} scene_draw_stack_item_t;

static void *scene3d_auto_lod_mesh(rt_scene_node3d *node,
                                   const rt_canvas3d *canvas,
                                   const rt_camera3d *cam,
                                   double radius,
                                   double distance);

/// @brief Return true when a Mesh3D payload is resident and eligible for draw selection.
static int scene3d_mesh_resident(void *mesh) {
    return mesh && rt_mesh3d_get_resident(mesh) != 0;
}

/// @brief Push (node, inherited animator) onto a growable draw-traversal stack.
/// @details Same iterative-traversal rationale and growth/return contract as
///          scene_bounds_stack_push: capacity doubles from 64; returns 1 on
///          success or no-op, 0 on overflow/realloc failure.
static int scene_draw_stack_push(scene_draw_stack_item_t **stack,
                                 size_t *count,
                                 size_t *capacity,
                                 rt_scene_node3d *node,
                                 void *inherited_animator) {
    rt_scene_node3d *checked;
    if (!stack || !count || !capacity || !node)
        return 1;
    checked = scene_node3d_checked(node);
    if (!checked)
        return 1;
    if (*count >= *capacity) {
        if (!scene3d_grow_stack_storage((void **)stack, capacity, sizeof(**stack)))
            return 0;
    }
    (*stack)[*count].node = checked;
    (*stack)[*count].inherited_animator = inherited_animator;
    (*count)++;
    return 1;
}

/// @brief Draw a single node's own geometry (mesh + material) at its world transform.
/// @details Does not recurse into children; the traversal/culling layer calls this per visible
/// node.
/// @brief Resolve which mesh+material a node draws this frame — auto-LOD, manual-LOD,
///        impostor selection, and residency — and compute the chosen mesh's local bounds.
/// @return The mesh to draw, or NULL when the node has no resident drawable mesh.
static void *scene3d_resolve_draw_mesh(rt_scene_node3d *current,
                                       rt_canvas3d *canvas,
                                       rt_camera3d *cam,
                                       const float *cam_pos,
                                       void **out_material,
                                       float out_min[3],
                                       float out_max[3],
                                       float *out_radius) {
    void *draw_mesh = rt_g3d_checked_or_null(current->mesh, RT_G3D_MESH3D_CLASS_ID);
    void *draw_material = rt_g3d_checked_or_null(current->material, RT_G3D_MATERIAL3D_CLASS_ID);
    float camera_distance = 0.0f;
    int has_camera_distance = 0;

    if ((draw_mesh || current->has_impostor) && cam_pos) {
        float local_center[3] = {0.0f, 0.0f, 0.0f};
        float world_center[3];
        float base_min[3] = {0.0f, 0.0f, 0.0f};
        float base_max[3] = {0.0f, 0.0f, 0.0f};
        float base_radius = 0.0f;
        if (draw_mesh) {
            scene_mesh_bounds((rt_mesh3d *)draw_mesh, base_min, base_max, &base_radius);
            local_center[0] = 0.5f * (base_min[0] + base_max[0]);
            local_center[1] = 0.5f * (base_min[1] + base_max[1]);
            local_center[2] = 0.5f * (base_min[2] + base_max[2]);
        }
        scene_world_point(current->world_matrix, local_center, world_center);
        {
            float dx = world_center[0] - cam_pos[0];
            float dy = world_center[1] - cam_pos[1];
            float dz = world_center[2] - cam_pos[2];
            float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            if (!isfinite(dist))
                dist = 0.0f;
            camera_distance = dist;
            has_camera_distance = 1;
            if (draw_mesh && current->auto_lod_enabled && scene3d_node_lod_count(current) > 0) {
                void *auto_mesh = scene3d_auto_lod_mesh(current, canvas, cam, base_radius, dist);
                if (auto_mesh)
                    draw_mesh = auto_mesh;
            }
        }
    }

    if (draw_mesh) {
        int32_t lod_count = scene3d_node_lod_count(current);
        if (!current->auto_lod_enabled && lod_count > 0 && has_camera_distance) {
            float dist = camera_distance;
            for (int32_t l = lod_count - 1; l >= 0; l--) {
                if (dist >= (float)current->lod_levels[l].distance &&
                    scene3d_mesh_resident(current->lod_levels[l].mesh)) {
                    draw_mesh = current->lod_levels[l].mesh;
                    break;
                }
            }
        }
    }

    void *impostor_mesh =
        rt_g3d_checked_or_null(current->impostor_mesh, RT_G3D_MESH3D_CLASS_ID);
    void *impostor_material =
        rt_g3d_checked_or_null(current->impostor_material, RT_G3D_MATERIAL3D_CLASS_ID);
    if (current->has_impostor && impostor_mesh && impostor_material &&
        has_camera_distance && camera_distance >= (float)current->impostor_distance &&
        scene3d_mesh_resident(impostor_mesh)) {
        draw_mesh = impostor_mesh;
        draw_material = impostor_material;
    }

    if (draw_mesh && !scene3d_mesh_resident(draw_mesh))
        draw_mesh = NULL;

    out_min[0] = out_min[1] = out_min[2] = 0.0f;
    out_max[0] = out_max[1] = out_max[2] = 0.0f;
    *out_radius = 0.0f;
    if (draw_mesh)
        scene_mesh_bounds((rt_mesh3d *)draw_mesh, out_min, out_max, out_radius);

    *out_material = draw_material;
    return draw_mesh;
}

/// @brief Frustum + PVS visibility test for a node's chosen mesh.
/// @return 1 when the node should be drawn, 0 when culled (bumping the cull counters).
static int scene3d_node_cull_test(rt_scene_node3d *current,
                                  const vgfx3d_frustum_t *frustum,
                                  const scene3d_pvs_context_t *pvs,
                                  void *draw_mesh,
                                  const float draw_min[3],
                                  const float draw_max[3],
                                  float draw_radius,
                                  void *effective_animator,
                                  int32_t *culled) {
    if ((frustum || (pvs && pvs->active)) && draw_mesh && draw_radius > 0.0f) {
        rt_mesh3d *draw_mesh_impl = (rt_mesh3d *)draw_mesh;
        int has_dynamic_deformation =
            scene3d_mesh_has_dynamic_deformation(draw_mesh_impl, effective_animator);
        float cull_min[3] = {draw_min[0], draw_min[1], draw_min[2]};
        float cull_max[3] = {draw_max[0], draw_max[1], draw_max[2]};
        float world_min[3], world_max[3];
        double world_min_d[3], world_max_d[3];
        if (has_dynamic_deformation) {
            float pad = (float)scene3d_mesh_dynamic_bound_pad(
                draw_mesh_impl, effective_animator, (double)draw_radius);
            cull_min[0] -= pad;
            cull_min[1] -= pad;
            cull_min[2] -= pad;
            cull_max[0] += pad;
            cull_max[1] += pad;
            cull_max[2] += pad;
        }
        vgfx3d_transform_aabb(cull_min, cull_max, current->world_matrix, world_min, world_max);
        world_min_d[0] = (double)world_min[0];
        world_min_d[1] = (double)world_min[1];
        world_min_d[2] = (double)world_min[2];
        world_max_d[0] = (double)world_max[0];
        world_max_d[1] = (double)world_max[1];
        world_max_d[2] = (double)world_max[2];
        if (pvs && pvs->active &&
            !scene3d_pvs_allows_aabb(current->owner_scene, pvs, world_min_d, world_max_d)) {
            if (culled)
                (*culled)++;
            if (pvs->culled_count)
                (*pvs->culled_count)++;
            return 0;
        } else if (frustum && vgfx3d_frustum_test_aabb(frustum, world_min, world_max) == 0) {
            if (culled)
                (*culled)++;
            return 0;
        }
    }
    return 1;
}

/// @brief Cull-test a node using its cached world-space AABB against the PVS and view frustum,
///   incrementing @p culled when the node is rejected.
/// @return Non-zero if the node is culled (the caller should skip it); 0 if potentially visible.
static int scene3d_cached_world_bounds_cull_test(rt_scene_node3d *current,
                                                 const vgfx3d_frustum_t *frustum,
                                                 const scene3d_pvs_context_t *pvs,
                                                 const double world_min_d[3],
                                                 const double world_max_d[3],
                                                 int32_t *culled) {
    float world_min[3];
    float world_max[3];
    if (!current || !world_min_d || !world_max_d)
        return 1;
    if (pvs && pvs->active &&
        !scene3d_pvs_allows_aabb(current->owner_scene, pvs, world_min_d, world_max_d)) {
        if (culled)
            (*culled)++;
        if (pvs->culled_count)
            (*pvs->culled_count)++;
        return 0;
    }
    if (!frustum)
        return 1;
    for (int i = 0; i < 3; ++i) {
        world_min[i] = scene3d_float_or_zero(world_min_d[i]);
        world_max[i] = scene3d_float_or_zero(world_max_d[i]);
    }
    if (vgfx3d_frustum_test_aabb(frustum, world_min, world_max) == 0) {
        if (culled)
            (*culled)++;
        return 0;
    }
    return 1;
}

/// @brief Submit a node's resolved mesh to the canvas (skinned when an animator palette exists).
static void scene3d_submit_node_draw(rt_scene_node3d *current,
                                     void *canvas3d,
                                     void *draw_mesh,
                                     void *draw_material,
                                     void *effective_animator,
                                     int32_t *visible_nodes) {
    draw_mesh = rt_g3d_checked_or_null(draw_mesh, RT_G3D_MESH3D_CLASS_ID);
    draw_material = rt_g3d_checked_or_null(draw_material, RT_G3D_MATERIAL3D_CLASS_ID);
    if (!(draw_mesh && draw_material))
        return;
    if (visible_nodes)
        (*visible_nodes)++;
    const float *anim_palette = NULL;
    const float *anim_prev_palette = NULL;
    int32_t anim_bone_count = 0;
    int32_t anim_prev_bone_count = 0;
    int32_t mesh_bone_count = ((rt_mesh3d *)draw_mesh)->bone_count;

    if (effective_animator) {
        void *anim_skeleton = rt_anim_controller3d_get_skeleton(effective_animator);
        rt_mesh3d *mesh_impl = (rt_mesh3d *)draw_mesh;
        if (!mesh_impl->skeleton_ref || mesh_impl->skeleton_ref == anim_skeleton) {
            anim_palette =
                rt_anim_controller3d_get_final_palette_data(effective_animator, &anim_bone_count);
            anim_prev_palette =
                rt_anim_controller3d_get_previous_palette_data(effective_animator,
                                                               &anim_prev_bone_count);
            if (anim_prev_bone_count <= 0 || anim_prev_bone_count < anim_bone_count)
                anim_prev_palette = NULL;
        }
    }
    if (anim_palette && anim_bone_count > 0 && mesh_bone_count > 0) {
        int32_t draw_bone_count =
            anim_bone_count < mesh_bone_count ? anim_bone_count : mesh_bone_count;
        if (rt_canvas3d_add_temp_object(canvas3d, effective_animator)) {
            rt_canvas3d_draw_mesh_matrix_skinned_keyed(canvas3d,
                                                       draw_mesh,
                                                       current->world_matrix,
                                                       draw_material,
                                                       current,
                                                       anim_palette,
                                                       anim_prev_palette,
                                                       draw_bone_count);
        }
    } else {
        rt_canvas3d_draw_mesh_matrix_keyed(
            canvas3d, draw_mesh, current->world_matrix, draw_material, current, NULL, NULL);
    }
}

/// @brief Draw a single node: resolve its mesh+material, cull-test it, then submit it.
static void scene3d_draw_node_self(rt_scene_node3d *current,
                                   void *canvas3d,
                                   rt_canvas3d *canvas,
                                   rt_camera3d *cam,
                                   const vgfx3d_frustum_t *frustum,
                                   const scene3d_pvs_context_t *pvs,
                                   int32_t *culled,
                                   int32_t *visible_nodes,
                                   const float *cam_pos,
                                   void *effective_animator) {
    void *draw_mesh;
    void *draw_material;
    float draw_min[3];
    float draw_max[3];
    float draw_radius;

    if (!current)
        return;
    recompute_world_matrix(current);

    draw_mesh = scene3d_resolve_draw_mesh(
        current, canvas, cam, cam_pos, &draw_material, draw_min, draw_max, &draw_radius);

    if (!scene3d_node_cull_test(current,
                                frustum,
                                pvs,
                                draw_mesh,
                                draw_min,
                                draw_max,
                                draw_radius,
                                effective_animator,
                                culled))
        return;

    scene3d_submit_node_draw(
        current, canvas3d, draw_mesh, draw_material, effective_animator, visible_nodes);
}

/// @brief Draw one spatial-index entry's node through @p canvas with the active camera,
///   applying frustum/PVS culling (updating @p culled / @p visible_nodes) and resolving the
///   node's effective animator.
static void scene3d_draw_spatial_entry(rt_scene3d_spatial_entry *entry,
                                       void *canvas3d,
                                       rt_canvas3d *canvas,
                                       rt_camera3d *cam,
                                       const vgfx3d_frustum_t *frustum,
                                       const scene3d_pvs_context_t *pvs,
                                       int32_t *culled,
                                       int32_t *visible_nodes,
                                       const float *cam_pos) {
    rt_scene_node3d *current;
    void *effective_animator;
    void *draw_mesh;
    void *draw_material;
    float draw_min[3];
    float draw_max[3];
    float draw_radius;
    int use_cached_bounds;
    if (!entry || !entry->node)
        return;
    current = entry->node;
    effective_animator = scene3d_effective_animator(current);
    recompute_world_matrix(current);
    draw_mesh = scene3d_resolve_draw_mesh(
        current, canvas, cam, cam_pos, &draw_material, draw_min, draw_max, &draw_radius);
    use_cached_bounds =
        draw_mesh == current->mesh &&
        !scene3d_mesh_has_dynamic_deformation((rt_mesh3d *)draw_mesh, effective_animator);
    if (use_cached_bounds) {
        if (!scene3d_cached_world_bounds_cull_test(
                current, frustum, pvs, entry->world_min, entry->world_max, culled))
            return;
    } else if (!scene3d_node_cull_test(current,
                                       frustum,
                                       pvs,
                                       draw_mesh,
                                       draw_min,
                                       draw_max,
                                       draw_radius,
                                       effective_animator,
                                       culled)) {
        return;
    }
    scene3d_submit_node_draw(
        current, canvas3d, draw_mesh, draw_material, effective_animator, visible_nodes);
}

/// @brief Draw traversal: depth-first, skip invisible nodes, frustum-cull meshes.
/// Children are ALWAYS traversed even if the parent mesh is culled, because
/// child transforms may place them inside the frustum independently.
static void draw_node(rt_scene_node3d *node,
                      void *canvas3d,
                      rt_canvas3d *canvas,
                      rt_camera3d *cam,
                      const vgfx3d_frustum_t *frustum,
                      const scene3d_pvs_context_t *pvs,
                      int32_t *culled,
                      int32_t *visible_nodes,
                      const float *cam_pos,
                      void *inherited_animator) {
    scene_draw_stack_item_t *stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node)
        return;
    if (!scene_draw_stack_push(&stack, &count, &capacity, node, inherited_animator)) {
        rt_trap("Scene3D.Draw: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        scene_draw_stack_item_t item = stack[--count];
        rt_scene_node3d *current = item.node;
        void *effective_animator;

        if (!current->visible)
            continue;

        effective_animator =
            rt_g3d_checked_or_null(current->bound_animator, RT_G3D_ANIMCONTROLLER3D_CLASS_ID);
        if (!effective_animator)
            effective_animator = item.inherited_animator;
        scene3d_draw_node_self(current,
                               canvas3d,
                               canvas,
                               cam,
                               frustum,
                               pvs,
                               culled,
                               visible_nodes,
                               cam_pos,
                               effective_animator);

        for (int32_t i = scene3d_node_child_count(current) - 1; i >= 0; i--) {
            if (!scene_draw_stack_push(
                    &stack, &count, &capacity, current->children[i], effective_animator)) {
                rt_trap("Scene3D.Draw: traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief Draw the scene using the spatial index for frustum culling.
/// @details Gathers candidate nodes whose bounds intersect the camera frustum via the BVH, sorts
/// them
///          back into scene traversal order for stable draw ordering, and draws each. Falls back to
///          a full traversal when the index is unavailable.
static int draw_node_spatial(rt_scene3d *scene,
                             void *canvas3d,
                             rt_canvas3d *canvas,
                             rt_camera3d *cam,
                             const vgfx3d_frustum_t *frustum,
                             const scene3d_pvs_context_t *pvs,
                             const float vp[16],
                             int32_t *culled,
                             int32_t *visible_nodes,
                             const float *cam_pos) {
    scene3d_spatial_candidate_list_t candidates = {0};
    double frustum_min[3];
    double frustum_max[3];
    int ok;
    if (!scene || !scene->use_spatial_index)
        return 0;
    if (scene3d_frustum_bounds_from_vp(vp, frustum_min, frustum_max))
        ok = scene3d_spatial_collect_aabb(scene, frustum_min, frustum_max, &candidates, 1);
    else
        ok = scene3d_spatial_collect_all(scene, &candidates);
    if (!ok) {
        free(candidates.items);
        return 0;
    }
    if (culled)
        *culled += scene->spatial_index.last_prefiltered_count;
    for (int32_t i = 0; i < candidates.count; ++i) {
        scene3d_draw_spatial_entry(candidates.items[i],
                                   canvas3d,
                                   canvas,
                                   cam,
                                   frustum,
                                   pvs,
                                   culled,
                                   visible_nodes,
                                   cam_pos);
    }
    free(candidates.items);
    return 1;
}

/*==========================================================================
 * Scene3D
 *=========================================================================*/

/// @brief GC finalizer for a Scene3D — releases the root node and any post-processing context.
static void rt_scene3d_finalize(void *obj) {
    rt_scene3d *scene = (rt_scene3d *)obj;
    rt_scene_node3d *root;
    int32_t visibility_zone_count;
    if (!scene)
        return;
    root = scene_node3d_checked(scene->root);
    if (root) {
        scene_node_clear_owner_recursive(root, scene);
        root->parent = NULL;
    }
    scene3d_release_ref((void **)&scene->root);
    free(scene->spatial_index.entries);
    free(scene->spatial_index.entry_indices);
    free(scene->spatial_index.nodes);
    scene->spatial_index.entries = NULL;
    scene->spatial_index.entry_indices = NULL;
    scene->spatial_index.nodes = NULL;
    scene->spatial_index.count = 0;
    scene->spatial_index.capacity = 0;
    scene->spatial_index.entry_index_capacity = 0;
    scene->spatial_index.node_count = 0;
    scene->spatial_index.node_capacity = 0;
    scene->spatial_index.root_node = -1;
    visibility_zone_count = scene3d_visibility_zone_count_safe(scene);
    for (int32_t i = 0; i < visibility_zone_count; ++i)
        scene3d_release_ref((void **)&scene->visibility_zones[i].name);
    free(scene->visibility_zones);
    free(scene->visibility_portals);
    free(scene->query_candidates);
    scene->visibility_zones = NULL;
    scene->visibility_portals = NULL;
    scene->query_candidates = NULL;
    scene->visibility_zone_count = 0;
    scene->visibility_zone_capacity = 0;
    scene->visibility_portal_count = 0;
    scene->visibility_portal_capacity = 0;
    scene->query_candidate_capacity = 0;
}

/// @brief Allocate a fresh Scene3D with an empty root node and no lights or skybox.
void *rt_scene3d_new(void) {
    rt_scene3d *s =
        (rt_scene3d *)rt_obj_new_i64(RT_G3D_SCENE3D_CLASS_ID, (int64_t)sizeof(rt_scene3d));
    if (!s) {
        rt_trap("Scene3D.New: memory allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->root = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!s->root) {
        if (rt_obj_release_check0(s))
            rt_obj_free(s);
        rt_trap("Scene3D.New: root node allocation failed");
        return NULL;
    }
    s->root->owner_scene = s;
    s->node_count = 1; /* root */
    s->last_culled_count = 0;
    s->last_visible_node_count = 0;
    s->last_pvs_culled_count = 0;
    s->use_spatial_index = 1;
    memset(&s->spatial_index, 0, sizeof(s->spatial_index));
    s->spatial_index.root_node = -1;
    s->spatial_index.dirty = 1;
    s->spatial_index.topology_dirty = 1;
    rt_obj_set_finalizer(s, rt_scene3d_finalize);
    return s;
}

/// @brief Return the implicit root node — every other node lives somewhere in its subtree.
void *rt_scene3d_get_root(void *obj) {
    rt_scene3d *s = scene3d_checked(obj);
    return s ? scene_node3d_checked(s->root) : NULL;
}

/// @brief Convenience: add `node` as a direct child of the scene's root node.
int8_t rt_scene3d_try_add(void *obj, void *node) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *n = scene_node3d_checked(node);
    rt_scene_node3d *root = s ? scene_node3d_checked(s->root) : NULL;
    if (!s || !root || !n)
        return 0;
    if (!rt_scene_node3d_try_add_child(root, n))
        return 0;
    s->node_count = count_subtree(root);
    return n->parent == root ? 1 : 0;
}

/// @brief Add a top-level node to the scene (under its root), taking ownership and dirtying the
/// index.
void rt_scene3d_add(void *obj, void *node) {
    (void)rt_scene3d_try_add(obj, node);
}

/// @brief `Scene3D.RebaseOrigin(dx, dy, dz)` — shift scene content by -delta.
void rt_scene3d_rebase_origin(void *obj, double dx, double dy, double dz) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *root = s ? scene_node3d_checked(s->root) : NULL;
    double delta[3] = {
        scene3d_finite_or(dx, 0.0), scene3d_finite_or(dy, 0.0), scene3d_finite_or(dz, 0.0)};
    if (!s || !root)
        return;
    if (delta[0] == 0.0 && delta[1] == 0.0 && delta[2] == 0.0)
        return;
    for (int32_t i = 0, child_count = scene3d_node_child_count(root); i < child_count; ++i)
        scene_node_rebase_root_child(root->children[i], delta);
}

/// @brief Convenience: remove `node` from the scene root's children.
void rt_scene3d_remove(void *obj, void *node) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *n = scene_node3d_checked(node);
    rt_scene_node3d *root = s ? scene_node3d_checked(s->root) : NULL;
    if (!s || !root || !n)
        return;
    if (node_contains(root, n) && n->parent)
        rt_scene_node3d_remove_child(n->parent, n);
    s->node_count = count_subtree(root);
}

/// @brief Locate a node by name via depth-first traversal from the scene root.
/// @return Pointer to the first matching node or NULL. Ownership is not
///   transferred — callers must not release the returned pointer directly.
void *rt_scene3d_find(void *obj, rt_string name) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *root = s ? scene_node3d_checked(s->root) : NULL;
    if (!s || !name)
        return NULL;
    if (!root)
        return NULL;
    if (!rt_string_is_handle(name))
        return NULL;
    const char *str = rt_string_cstr(name);
    if (!str)
        return NULL;
    return find_by_name(root, str);
}

/// @brief Add an authored PVS visibility zone AABB; returns its zero-based index, or -1.
int64_t rt_scene3d_add_visibility_zone(void *obj, rt_string name, void *min_obj, void *max_obj) {
    rt_scene3d *s = scene3d_checked(obj);
    int32_t zone_count;
    double a[3];
    double b[3];
    rt_scene3d_visibility_zone *zone;
    if (!s)
        return -1;
    if (!scene3d_read_vec3d(min_obj, a, "Scene3D.AddVisibilityZone: min must be Vec3") ||
        !scene3d_read_vec3d(max_obj, b, "Scene3D.AddVisibilityZone: max must be Vec3"))
        return -1;
    zone_count = scene3d_visibility_zone_count_safe(s);
    s->visibility_zone_count = zone_count;
    if (zone_count == INT32_MAX ||
        !scene3d_visibility_zone_ensure_capacity(s, zone_count + 1)) {
        rt_trap("Scene3D.AddVisibilityZone: allocation failed");
        return -1;
    }
    zone = &s->visibility_zones[zone_count];
    memset(zone, 0, sizeof(*zone));
    for (int i = 0; i < 3; ++i) {
        zone->world_min[i] = fmin(a[i], b[i]);
        zone->world_max[i] = fmax(a[i], b[i]);
    }
    if (!name)
        name = rt_const_cstr("");
    rt_obj_retain_maybe(name);
    zone->name = name;
    s->visibility_zone_count = zone_count + 1;
    return zone_count;
}

/// @brief Append one directed visibility portal (@p from_zone -> @p to_zone) to the scene.
/// @details Validates both zone indices against the current zone count and grows the portal array
///          as needed before recording the edge.
/// @return 1 on success; 0 on invalid indices, overflow, or allocation failure.
static int scene3d_add_visibility_portal_directed(rt_scene3d *s,
                                                  int32_t from_zone,
                                                  int32_t to_zone) {
    int32_t zone_count;
    int32_t portal_count;
    rt_scene3d_visibility_portal *portal;
    if (!s)
        return 0;
    zone_count = scene3d_visibility_zone_count_safe(s);
    portal_count = scene3d_visibility_portal_count_safe(s);
    s->visibility_zone_count = zone_count;
    s->visibility_portal_count = portal_count;
    if (from_zone < 0 || to_zone < 0 || from_zone >= zone_count || to_zone >= zone_count)
        return 0;
    if (portal_count == INT32_MAX ||
        !scene3d_visibility_portal_ensure_capacity(s, portal_count + 1))
        return 0;
    portal = &s->visibility_portals[portal_count];
    portal->from_zone = from_zone;
    portal->to_zone = to_zone;
    s->visibility_portal_count = portal_count + 1;
    return 1;
}

/// @brief Add a directed or bidirectional visibility portal between authored zones.
int64_t rt_scene3d_add_visibility_portal(void *obj,
                                         int64_t from_zone,
                                         int64_t to_zone,
                                         int8_t bidirectional) {
    rt_scene3d *s = scene3d_checked(obj);
    int32_t from_i;
    int32_t to_i;
    int32_t zone_count;
    int32_t portal_count;
    int32_t first_index;
    if (!s || from_zone < 0 || to_zone < 0 || from_zone > INT32_MAX || to_zone > INT32_MAX)
        return -1;
    zone_count = scene3d_visibility_zone_count_safe(s);
    portal_count = scene3d_visibility_portal_count_safe(s);
    s->visibility_zone_count = zone_count;
    s->visibility_portal_count = portal_count;
    from_i = (int32_t)from_zone;
    to_i = (int32_t)to_zone;
    if (from_i >= zone_count || to_i >= zone_count)
        return -1;
    first_index = portal_count;
    if (!scene3d_add_visibility_portal_directed(s, from_i, to_i)) {
        rt_trap("Scene3D.AddVisibilityPortal: allocation failed");
        return -1;
    }
    if (bidirectional && !scene3d_add_visibility_portal_directed(s, to_i, from_i)) {
        rt_trap("Scene3D.AddVisibilityPortal: allocation failed");
        return -1;
    }
    return first_index;
}

/// @brief Helper to convert double[16] to float[16] for frustum extraction.
static void mat4_d2f_local(const double *src, float *dst) {
    for (int i = 0; i < 16; i++)
        dst[i] = scene3d_float_or_zero(src[i]);
}

/// @brief Compute the actual output aspect ratio for projection matrix construction.
/// @details Priority: render-target dimensions > canvas window dimensions > camera's
///   stored aspect ratio. Using the render target's size when one is active ensures
///   the projection matches an off-screen FBO rather than the window, which matters
///   for shadow maps, reflection probes, and post-process passes that render at a
///   different resolution than the display. Falls back to `cam->aspect` (set by the
///   caller when no canvas is available) or 1.0 as the last resort.
/// @param canvas Active canvas, may be NULL.
/// @param cam    Active camera, may be NULL.
/// @return Width / height aspect ratio, always positive.
static double scene3d_active_output_aspect(const rt_canvas3d *canvas, const rt_camera3d *cam) {
    int32_t w = 0;
    int32_t h = 0;
    if (canvas) {
        if (canvas->render_target) {
            w = canvas->render_target->width;
            h = canvas->render_target->height;
        } else {
            w = canvas->width;
            h = canvas->height;
        }
    }
    if (w > 0 && h > 0)
        return (double)w / (double)h;
    return cam ? cam->aspect : 1.0;
}

/// @brief Return active output height in pixels for screen-error LOD estimates.
static double scene3d_active_output_height(const rt_canvas3d *canvas) {
    int32_t h = 0;
    if (canvas) {
        if (canvas->render_target)
            h = canvas->render_target->height;
        else
            h = canvas->height;
    }
    return h > 0 ? (double)h : 720.0;
}

/// @brief Estimate how large a bounding sphere appears on screen in pixels.
static double scene3d_projected_diameter_px(const rt_canvas3d *canvas,
                                            const rt_camera3d *cam,
                                            double radius,
                                            double distance) {
    double output_h;
    if (!cam || !isfinite(radius) || radius <= 0.0)
        return DBL_MAX;
    output_h = scene3d_active_output_height(canvas);
    if (cam->is_ortho) {
        double half_height =
            isfinite(cam->ortho_size) && cam->ortho_size > 0.0 ? cam->ortho_size : 1.0;
        return (2.0 * radius) * (output_h / (2.0 * half_height));
    }
    {
        double fov_rad = cam->fov * SCENE3D_PI / 180.0;
        double tan_half;
        if (!isfinite(fov_rad) || fov_rad <= 0.0 || fov_rad >= SCENE3D_PI)
            fov_rad = 60.0 * SCENE3D_PI / 180.0;
        tan_half = tan(fov_rad * 0.5);
        if (!isfinite(distance) || distance <= 1e-4)
            distance = 1e-4;
        if (!isfinite(tan_half) || tan_half <= 1e-6)
            tan_half = tan(30.0 * SCENE3D_PI / 180.0);
        return (2.0 * radius) * (output_h / (2.0 * tan_half * distance));
    }
}

/// @brief Pick an authored LOD mesh using projected screen size instead of distance thresholds.
static void *scene3d_auto_lod_mesh(rt_scene_node3d *node,
                                   const rt_canvas3d *canvas,
                                   const rt_camera3d *cam,
                                   double radius,
                                   double distance) {
    double projected_px;
    double screen_error_px;
    int32_t lod_count;
    if (!node)
        return NULL;
    lod_count = scene3d_node_lod_count(node);
    if (lod_count <= 0)
        return NULL;
    if (!node->auto_lod_enabled)
        return NULL;
    projected_px = scene3d_projected_diameter_px(canvas, cam, radius, distance);
    screen_error_px = scene3d_finite_or(node->auto_lod_screen_error_px, 8.0);
    if (screen_error_px < 1.0)
        screen_error_px = 1.0;
    if (screen_error_px > 1000000.0)
        screen_error_px = 1000000.0;
    for (int32_t i = lod_count - 1; i >= 0; --i) {
        double threshold = screen_error_px * (double)(lod_count - i);
        if (projected_px <= threshold && scene3d_mesh_resident(node->lod_levels[i].mesh))
            return node->lod_levels[i].mesh;
    }
    return NULL;
}

/// @brief Build the view-projection matrix used for frustum-culling this frame.
/// @details When the canvas is already inside a 3D frame, the VP matrix cached in
///   `canvas->cached_vp` is reused to guarantee all visibility tests in the same
///   frame use identical frustum planes, even when multiple scenes are drawn in one
///   `Begin3D/End3D` block. Otherwise the matrix is computed fresh by multiplying
///   the camera view matrix (double→float converted) by the perspective/ortho
///   projection returned by `rt_camera3d_get_render_projection`, which uses the
///   aspect ratio from `scene3d_active_output_aspect` to match the actual output.
/// @param canvas Canvas in use; may be NULL (falls back to camera-only projection).
/// @param cam    Camera providing view and projection parameters; may be NULL (produces
///               an identity VP so no culling occurs).
/// @param vp     float[16] output matrix in row-major order; must not be NULL.
static void scene3d_build_culling_vp(rt_canvas3d *canvas, rt_camera3d *cam, float *vp) {
    float vf[16];
    float pf[16];

    if (!vp)
        return;
    if (canvas && canvas->in_frame && !canvas->frame_is_2d) {
        memcpy(vp, canvas->cached_vp, sizeof(canvas->cached_vp));
        return;
    }
    memset(vp, 0, sizeof(float) * 16);
    if (!cam)
        return;

    mat4_d2f_local(cam->view, vf);
    rt_camera3d_get_render_projection(cam, scene3d_active_output_aspect(canvas, cam), pf);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            vp[r * 4 + c] = pf[r * 4 + 0] * vf[0 * 4 + c] + pf[r * 4 + 1] * vf[1 * 4 + c] +
                            pf[r * 4 + 2] * vf[2 * 4 + c] + pf[r * 4 + 3] * vf[3 * 4 + c];
}

/// @brief Traverse the scene and submit visible nodes for rendering.
/// @details Builds the view-projection matrix from the camera, extracts the
///   six frustum planes, then recursively draws the scene tree. Nodes whose
///   world-space bounds fall outside the frustum are skipped and counted in
///   `last_culled_count`, queryable via `rt_scene3d_get_culled_count`. If no
///   frame is currently active on the canvas the scene opens/closes one on
///   behalf of the caller; otherwise it draws into the existing frame so
///   multiple scenes can share a single Begin/End pair. Traps when invoked
///   inside a Begin2D/End block because 2D and 3D passes use incompatible
///   pipeline state.
void rt_scene3d_draw(void *obj, void *canvas3d, void *camera) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_canvas3d *canvas = rt_canvas3d_checked_or_stack(canvas3d);
    rt_camera3d *cam = rt_camera3d_checked_or_stack(camera);
    int8_t started_frame = 0;
    rt_light3d *scene_light_ptrs[VGFX3D_MAX_LIGHTS];
    rt_light3d *prev_scene_lights[VGFX3D_MAX_LIGHTS];
    rt_light3d prev_scene_light_storage[VGFX3D_MAX_LIGHTS];
    int32_t scene_light_count = 0;
    int32_t prev_scene_light_count;
    float vp[16];
    vgfx3d_frustum_t frustum;
    int32_t culled = 0;
    int32_t visible_nodes = 0;
    int32_t pvs_culled = 0;
    scene3d_pvs_context_t pvs = {0};

    if (!s || !canvas || !cam)
        return;
    if (canvas->in_frame) {
        if (canvas->frame_is_2d) {
            rt_trap("Scene3D.Draw: cannot draw a 3D scene during Begin2D/End");
            return;
        }
    } else {
        rt_canvas3d_begin(canvas3d, camera);
        if (!canvas->in_frame || canvas->frame_is_2d)
            return;
        started_frame = 1;
    }
    scene3d_build_culling_vp(canvas, cam, vp);
    vgfx3d_frustum_extract(&frustum, vp);
    if (scene3d_build_pvs_context(s, cam, &pvs))
        pvs.culled_count = &pvs_culled;

    float cam_pos[3] = {scene3d_float_or_zero(cam->eye[0]),
                        scene3d_float_or_zero(cam->eye[1]),
                        scene3d_float_or_zero(cam->eye[2])};
    prev_scene_light_count = canvas->scene_light_count;
    memcpy(prev_scene_lights, canvas->scene_lights, sizeof(prev_scene_lights));
    memcpy(prev_scene_light_storage, canvas->scene_light_storage, sizeof(prev_scene_light_storage));
    memset(scene_light_ptrs, 0, sizeof(scene_light_ptrs));
    scene_collect_node_lights(
        s->root, canvas->scene_light_storage, scene_light_ptrs, &scene_light_count);
    canvas->scene_light_count = scene_light_count;
    memset(canvas->scene_lights, 0, sizeof(canvas->scene_lights));
    memcpy(canvas->scene_lights,
           scene_light_ptrs,
           (size_t)scene_light_count * sizeof(scene_light_ptrs[0]));
    if (!draw_node_spatial(
            s, canvas3d, canvas, cam, &frustum, &pvs, vp, &culled, &visible_nodes, cam_pos)) {
        draw_node(
            s->root, canvas3d, canvas, cam, &frustum, &pvs, &culled, &visible_nodes, cam_pos, NULL);
    }
    if (started_frame)
        rt_canvas3d_end(canvas3d);
    canvas->scene_light_count = prev_scene_light_count;
    memcpy(canvas->scene_lights, prev_scene_lights, sizeof(prev_scene_lights));
    memcpy(canvas->scene_light_storage, prev_scene_light_storage, sizeof(prev_scene_light_storage));
    s->last_culled_count = culled;
    s->last_visible_node_count = visible_nodes;
    s->last_pvs_culled_count = pvs_culled;
    canvas->last_occluded_draw_count += culled;
    scene3d_pvs_context_clear(&pvs);
}

/// @brief Detach and release every child of the root so the scene is empty.
/// @details Preserves the root node itself — callers can continue adding
///   children afterwards without re-instantiating the scene. Each detached
///   subtree's retain count is decremented; subtrees held by other strong
///   refs outside the scene graph survive and remain usable.
void rt_scene3d_clear(void *obj) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *root;
    if (!s)
        return;
    root = scene_node3d_checked(s->root);
    if (!root) {
        s->node_count = 0;
        scene3d_mark_spatial_dirty(s);
        return;
    }
    /* Detach all children from root */
    for (int32_t i = 0, child_count = scene3d_node_child_count(root); i < child_count; i++) {
        rt_scene_node3d *child = scene_node3d_checked(root->children[i]);
        if (!child) {
            root->children[i] = NULL;
            continue;
        }
        child->parent = NULL;
        scene_node_clear_owner_recursive(child, s);
        scene3d_release_ref((void **)&root->children[i]);
    }
    root->child_count = 0;
    s->node_count = 1; /* just root */
    scene3d_mark_spatial_dirty(s);
}

/// @brief Propagate node transforms out to bound systems (audio, etc.) for one tick.
/// @details Walks the tree once to let each node publish its world transform to
///   attached subsystems (e.g. 3D audio sources following a node), then ticks
///   the audio graph using `dt` so Doppler / attenuation stay in lockstep with
///   the scene's own integration timestep. Callers typically invoke this once
///   per simulation step, before submitting the scene for drawing.
void rt_scene3d_sync_bindings(void *obj, double dt) {
    rt_scene3d *scene = scene3d_checked(obj);
    rt_scene_node3d *root = scene ? scene_node3d_checked(scene->root) : NULL;
    if (!scene || !root)
        return;
    scene_node_sync_recursive(root, dt);
    rt_sound3d_sync_bindings(dt);
}

/// @brief Count every node in the scene, including the implicit root.
/// @details Re-walks the tree rather than trusting a cached value so the result
///   stays correct after direct child-list mutation. The cached `node_count`
///   field is refreshed as a side effect.
int64_t rt_scene3d_get_node_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    if (!scene)
        return 0;
    scene->node_count = count_subtree(scene->root);
    return scene->node_count;
}

static int64_t scene3d_counter_or_zero(int32_t value) {
    return value > 0 ? (int64_t)value : 0;
}

/// @brief Number of nodes culled by the most recent `rt_scene3d_draw` call.
/// @details Zero until the first draw. Useful as a coarse telemetry signal to
///   verify that culling is actually rejecting off-screen geometry.
int64_t rt_scene3d_get_culled_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    return scene ? scene3d_counter_or_zero(scene->last_culled_count) : 0;
}

/// @brief Number of drawable mesh nodes submitted by the most recent draw.
int64_t rt_scene3d_get_visible_node_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    return scene ? scene3d_counter_or_zero(scene->last_visible_node_count) : 0;
}

/// @brief Number of drawable mesh nodes skipped by portal/PVS visibility in the most recent draw.
int64_t rt_scene3d_get_pvs_culled_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    return scene ? scene3d_counter_or_zero(scene->last_pvs_culled_count) : 0;
}

/// @brief Number of authored visibility zones in the scene.
int64_t rt_scene3d_get_visibility_zone_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    return scene ? (int64_t)scene3d_visibility_zone_count_safe(scene) : 0;
}

/// @brief Number of directed visibility portal links in the scene.
int64_t rt_scene3d_get_visibility_portal_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    return scene ? (int64_t)scene3d_visibility_portal_count_safe(scene) : 0;
}

/*==========================================================================
 * LOD — Level of Detail per SceneNode3D
 *=========================================================================*/

/// @brief Register a mesh LOD to swap in at or beyond a given camera distance.
/// @details Grows the LOD array on demand (doubling, min 4 slots) and keeps
///   entries sorted ascending by `distance` so the draw path can pick the
///   highest threshold that does not exceed the current view distance. Duplicate
///   thresholds replace the existing mesh instead of creating ambiguous ties. The
///   mesh is retained here and released by `rt_scene_node3d_clear_lod` so
///   callers may drop their local reference immediately after adding.
void rt_scene_node3d_add_lod(void *obj, double distance, void *mesh) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node || !mesh)
        return;
    if (!rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    distance = scene3d_finite_or(distance, 0.0);
    if (distance < 0.0)
        distance = 0.0;
    node->lod_count = scene3d_node_lod_count(node);
    if (!node->lod_levels)
        node->lod_capacity = 0;
    lod_count = node->lod_count;

    for (int32_t i = 0; i < lod_count; i++) {
        if (node->lod_levels[i].distance == distance) {
            rt_obj_retain_maybe(mesh);
            scene3d_release_class_ref(&node->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID);
            node->lod_levels[i].mesh = mesh;
            scene3d_mark_spatial_dirty(node->owner_scene);
            return;
        }
    }

    if (node->lod_count >= node->lod_capacity) {
        if (node->lod_capacity >= INT32_MAX / 2) {
            rt_trap("SceneNode3D.AddLOD: too many LOD levels");
            return;
        }
        int32_t new_cap = node->lod_capacity < 4 ? 4 : node->lod_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(node->lod_levels[0])) {
            rt_trap("SceneNode3D.AddLOD: LOD allocation overflow");
            return;
        }
        void *tmp = realloc(node->lod_levels, (size_t)new_cap * sizeof(node->lod_levels[0]));
        if (!tmp) {
            rt_trap("SceneNode3D.AddLOD: LOD allocation failed");
            return;
        }
        node->lod_levels = tmp;
        node->lod_capacity = new_cap;
    }

    /* Insert sorted by distance ascending */
    int32_t pos = node->lod_count;
    for (int32_t i = 0; i < node->lod_count; i++) {
        if (distance < node->lod_levels[i].distance) {
            pos = i;
            break;
        }
    }
    /* Shift elements right */
    for (int32_t i = node->lod_count; i > pos; i--)
        node->lod_levels[i] = node->lod_levels[i - 1];

    node->lod_levels[pos].distance = distance;
    node->lod_levels[pos].mesh = mesh;
    rt_obj_retain_maybe(mesh);
    node->lod_count++;
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Enable or disable screen-error selection over authored LOD entries.
void rt_scene_node3d_set_auto_lod(void *obj, int8_t enabled, double screen_error_px) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    node->auto_lod_enabled = enabled ? 1 : 0;
    if (!isfinite(screen_error_px))
        screen_error_px = 8.0;
    if (screen_error_px < 1.0)
        screen_error_px = 1.0;
    if (screen_error_px > 1000000.0)
        screen_error_px = 1000000.0;
    node->auto_lod_screen_error_px = screen_error_px;
}

/// @brief Build a camera-facing local XY quad used by SetImpostor.
static void *scene3d_new_impostor_mesh(rt_scene_node3d *node, void *pixels) {
    double aspect = 1.0;
    double diameter = 1.0;
    double width;
    double height;
    void *mesh;
    int64_t pw;
    int64_t ph;
    if (!pixels)
        return NULL;
    pw = rt_pixels_width(pixels);
    ph = rt_pixels_height(pixels);
    if (pw > 0 && ph > 0)
        aspect = (double)pw / (double)ph;
    if (node && isfinite(node->bsphere_radius) && node->bsphere_radius > 0.0f)
        diameter = (double)node->bsphere_radius * 2.0;
    if (!isfinite(aspect) || aspect <= 0.0)
        aspect = 1.0;
    if (aspect > 128.0)
        aspect = 128.0;
    if (aspect < 1.0 / 128.0)
        aspect = 1.0 / 128.0;
    height = diameter;
    width = height * aspect;
    if (!isfinite(width) || width <= 0.0)
        width = 1.0;
    if (!isfinite(height) || height <= 0.0)
        height = 1.0;

    mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;
    rt_mesh3d_reserve(mesh, 4, 2);
    rt_mesh3d_add_vertex(mesh, -width * 0.5, -height * 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0);
    rt_mesh3d_add_vertex(mesh, width * 0.5, -height * 0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
    rt_mesh3d_add_vertex(mesh, width * 0.5, height * 0.5, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0);
    rt_mesh3d_add_vertex(mesh, -width * 0.5, height * 0.5, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
    rt_mesh3d_add_triangle(mesh, 0, 1, 2);
    rt_mesh3d_add_triangle(mesh, 0, 2, 3);
    return mesh;
}

/// @brief Bind or clear a generated textured impostor proxy for distant draws.
void rt_scene_node3d_set_impostor(void *obj, double distance, void *pixels) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    void *mesh;
    void *material;
    if (!node)
        return;
    if (!pixels) {
        node->has_impostor = 0;
        node->impostor_distance = 0.0;
        scene3d_release_pixels_ref(&node->impostor_pixels);
        scene3d_release_class_ref(&node->impostor_mesh, RT_G3D_MESH3D_CLASS_ID);
        scene3d_release_class_ref(&node->impostor_material, RT_G3D_MATERIAL3D_CLASS_ID);
        scene3d_mark_spatial_dirty(node->owner_scene);
        return;
    }
    if (!rt_pixels_checked_impl_or_null(pixels)) {
        rt_trap("SceneNode3D.SetImpostor: pixels must be Pixels");
        return;
    }
    distance = scene3d_finite_or(distance, 0.0);
    if (distance < 0.0)
        distance = 0.0;

    mesh = scene3d_new_impostor_mesh(node, pixels);
    material = rt_material3d_new();
    if (!mesh || !material) {
        scene3d_release_ref(&mesh);
        scene3d_release_ref(&material);
        rt_trap("SceneNode3D.SetImpostor: proxy allocation failed");
        return;
    }
    rt_material3d_set_texture(material, pixels);
    rt_material3d_set_unlit(material, 1);
    rt_material3d_set_double_sided(material, 1);

    rt_obj_retain_maybe(pixels);
    scene3d_release_pixels_ref(&node->impostor_pixels);
    scene3d_release_class_ref(&node->impostor_mesh, RT_G3D_MESH3D_CLASS_ID);
    scene3d_release_class_ref(&node->impostor_material, RT_G3D_MATERIAL3D_CLASS_ID);
    node->impostor_pixels = pixels;
    node->impostor_mesh = mesh;
    node->impostor_material = material;
    node->impostor_distance = distance;
    node->has_impostor = 1;
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Release every registered LOD mesh on this node and reset the count.
/// @details Preserves the underlying `lod_levels` allocation so subsequent
///   `add_lod` calls can reuse it without reallocating.
void rt_scene_node3d_clear_lod(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return;
    lod_count = scene3d_node_lod_count(node);
    for (int32_t i = 0; i < lod_count; i++)
        scene3d_release_class_ref(&node->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID);
    node->lod_count = 0;
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Number of LOD levels currently registered on this node.
int64_t rt_scene_node3d_get_lod_count(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return 0;
    node->lod_count = scene3d_node_lod_count(node);
    return node->lod_count;
}

/// @brief Distance threshold (in world units) for the LOD at `index`.
/// @return Ascending-sorted threshold, or 0.0 for an out-of-range index or
///   null node; 0.0 is a safe sentinel because LOD 0 (if present) would
///   normally specify a non-zero distance anyway.
double rt_scene_node3d_get_lod_distance(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return 0.0;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return 0.0;
    {
        double distance = scene3d_finite_or(node->lod_levels[index].distance, 0.0);
        return distance < 0.0 ? 0.0 : distance;
    }
}

/// @brief Borrowed pointer to the mesh registered at LOD `index`.
/// @return The mesh or NULL; ownership stays with the scene node.
void *rt_scene_node3d_get_lod_mesh(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return NULL;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return NULL;
    return rt_g3d_checked_or_null(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID);
}

/// @brief Toggle resident state for the mesh backing the @p index-th LOD entry.
void rt_scene_node3d_set_lod_resident(void *obj, int64_t index, int8_t resident) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return;
    if (!rt_g3d_has_class(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    rt_mesh3d_set_resident(node->lod_levels[index].mesh, resident);
    scene3d_mark_spatial_dirty(node->owner_scene);
}

/// @brief Return whether the @p index-th LOD mesh payload is resident.
int8_t rt_scene_node3d_get_lod_resident(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return 0;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return 0;
    if (!rt_g3d_has_class(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID))
        return 0;
    return rt_mesh3d_get_resident(node->lod_levels[index].mesh);
}

/// @brief Estimated resident payload bytes for the @p index-th LOD mesh.
int64_t rt_scene_node3d_get_lod_resident_bytes(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    int32_t lod_count;
    if (!node)
        return 0;
    lod_count = scene3d_node_lod_count(node);
    node->lod_count = lod_count;
    if (index < 0 || index >= lod_count)
        return 0;
    if (!rt_g3d_has_class(node->lod_levels[index].mesh, RT_G3D_MESH3D_CLASS_ID))
        return 0;
    return rt_mesh3d_get_resident_bytes(node->lod_levels[index].mesh);
}

//=============================================================================

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
