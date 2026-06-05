//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_skeleton3d.c
// Purpose: Skeleton3D (bone hierarchy + bind pose), Animation3D (keyframe
//   clips), and AnimPlayer3D (playback, sampling, crossfade, palette output).
//
// Key invariants:
//   - Authored bones may arrive in non-topological order; global-pose builders
//     resolve parent chains recursively and break cycles as roots.
//   - Palette computation: local → global (multiply up hierarchy) → * inverse_bind.
//   - Keyframe sampling: binary search for bracket, SLERP rotation, lerp pos/scale.
//   - Crossfade: blend per-bone local transforms between two animations.
//   - GPU vs CPU skinning gated per-backend by bone-count limits.
//
// Ownership/Lifetime:
//   - Skeleton3D / Animation3D / AnimPlayer3D are GC-managed.
//   - Animation keyframe arrays are owned heap allocations freed in the finalizer.
//   - Bone-name strings are retained on assignment.
//
// Links: rt_skeleton3d.h, vgfx3d_skinning.h, plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_skeleton3d.h"
#include "rt_blendtree3d.h"
#include "rt_animcontroller3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_heap.h"
#include "rt_mat4.h"
#include "rt_object.h"
#include "rt_quat.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include "vgfx3d_backend.h"
#include "vgfx3d_skinning.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SKELETON3D_FLOAT_ABS_MAX 3.40282346638528859812e38
#define SKELETON3D_ANIM_ABS_MAX 1.0e12f

/// @brief Heuristic — should we hand bone matrices to the GPU instead of skinning on the CPU?
///
/// GPU backends can skin directly while the active palette fits the backend's
/// shader-visible upload limit. The software backend always returns 0 and uses
/// CPU skinning.
/// The Software backend always returns 0 (CPU-skin path).
static int vgfx3d_backend_prefers_gpu_skinning(const char *backend_name, int32_t bone_count) {
    if (!backend_name || bone_count <= 0)
        return 0;
    if (strcmp(backend_name, "metal") == 0)
        return bone_count <= VGFX3D_MAX_BONES;
    if (strcmp(backend_name, "opengl") == 0)
        return bone_count <= VGFX3D_MAX_BONES;
    if (strcmp(backend_name, "d3d11") == 0)
        return bone_count <= VGFX3D_MAX_BONES;
    return 0;
}

/*==========================================================================
 * Matrix math helpers (float, row-major)
 *=========================================================================*/

/// @brief Multiply two row-major 4×4 float matrices: `out = a * b`.
static void mat4f_mul_local(const float *a, const float *b, float *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

/// @brief Set `m` to the 4×4 identity matrix.
static void mat4f_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/// @brief Resolve one bone's global matrix (global = parent_global × local) by recursing into
///   its parent first, using a tri-state visited array to memoize results and break parent cycles.
static void skeleton3d_compute_global_bone_recursive(const rt_skeleton3d *skeleton,
                                                     const float *locals,
                                                     float *globals,
                                                     uint8_t *state,
                                                     int32_t bone_count,
                                                     int32_t bone) {
    int32_t parent;
    if (!skeleton || !locals || !globals || !state || bone < 0 || bone >= bone_count)
        return;
    if (state[bone] == 2)
        return;
    if (state[bone] == 1) {
        memcpy(&globals[bone * 16], &locals[bone * 16], 16 * sizeof(float));
        state[bone] = 2;
        return;
    }

    state[bone] = 1;
    parent = skeleton3d_valid_parent_index(skeleton, bone, bone_count);
    if (parent >= 0) {
        if (state[parent] == 1) {
            parent = -1;
        } else {
            skeleton3d_compute_global_bone_recursive(
                skeleton, locals, globals, state, bone_count, parent);
            if (state[parent] != 2)
                parent = -1;
        }
    }

    if (parent >= 0)
        mat4f_mul_local(&globals[parent * 16], &locals[bone * 16], &globals[bone * 16]);
    else
        memcpy(&globals[bone * 16], &locals[bone * 16], 16 * sizeof(float));
    state[bone] = 2;
}

/// @brief Compose global bone matrices from per-bone local matrices across the whole skeleton
///   (bounded by the safe bone count), resolving each bone through its parent chain.
void skeleton3d_compute_globals_from_locals(const rt_skeleton3d *skeleton,
                                            const float *locals,
                                            float *globals,
                                            int32_t bone_count) {
    uint8_t state[VGFX3D_MAX_BONES];
    int32_t safe_count;
    if (!skeleton || !locals || !globals)
        return;
    safe_count = skeleton3d_safe_bone_count(skeleton);
    if (bone_count < 0 || bone_count > safe_count)
        bone_count = safe_count;
    if (bone_count <= 0)
        return;
    memset(state, 0, sizeof(state));
    for (int32_t bone = 0; bone < bone_count; bone++) {
        skeleton3d_compute_global_bone_recursive(
            skeleton, locals, globals, state, bone_count, bone);
    }
}

/// @brief Validate @p obj is a heap-allocated Mat4 and return its typed pointer (NULL on mismatch).
static mat4_impl *skeleton3d_mat4_checked(void *obj) {
    if (!obj || !rt_heap_is_payload(obj) || rt_obj_class_id(obj) != RT_MAT4_CLASS_ID)
        return NULL;
    return (mat4_impl *)obj;
}

/// @brief Checked cast of an opaque handle to AnimPlayer3D; NULL on class mismatch.
static rt_anim_player3d *anim_player3d_checked(void *obj) {
    return (rt_anim_player3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMPLAYER3D_CLASS_ID);
}

/// @brief Checked cast of an opaque handle to AnimBlend3D; NULL on class mismatch.
static rt_anim_blend3d *anim_blend3d_checked(void *obj) {
    return (rt_anim_blend3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMBLEND3D_CLASS_ID);
}

/// @brief Checked cast of an opaque handle to Animation3D; NULL on class mismatch.
static rt_animation3d *animation3d_checked(void *obj) {
    return (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Clamp an AnimBlend3D state count without requiring contiguous live animation slots.
static int32_t anim_blend3d_state_slot_limit(const rt_anim_blend3d *blend) {
    if (!blend || blend->state_count <= 0)
        return 0;
    return blend->state_count < RT_ANIM_BLEND3D_MAX_STATES ? blend->state_count
                                                           : RT_ANIM_BLEND3D_MAX_STATES;
}

/// @brief Test whether @p value is finite and fits in float range (no NaN/Inf, no overflow on
/// cast).
static int skeleton3d_value_fits_float(double value) {
    return isfinite(value) && value >= -SKELETON3D_FLOAT_ABS_MAX &&
           value <= SKELETON3D_FLOAT_ABS_MAX;
}

/// @brief Convert a finite double into float range, otherwise use @p fallback.
static float skeleton3d_float_or(double value, float fallback) {
    if (!skeleton3d_value_fits_float(value))
        return fallback;
    return (float)value;
}

/// @brief Clamp a finite double into the full finite float range.
static float skeleton3d_clamp_to_float(double value, float fallback) {
    if (!isfinite(value))
        return fallback;
    if (value > SKELETON3D_FLOAT_ABS_MAX)
        return FLT_MAX;
    if (value < -SKELETON3D_FLOAT_ABS_MAX)
        return -FLT_MAX;
    return (float)value;
}

/// @brief Clamp a non-negative time/duration value into float range.
static float skeleton3d_clamp_nonnegative_float(double value, float fallback) {
    if (!isfinite(value))
        return fallback;
    if (value <= 0.0)
        return 0.0f;
    if (value > SKELETON3D_FLOAT_ABS_MAX)
        return FLT_MAX;
    return (float)value;
}

/// @brief Clamp an interpolation parameter to [0, 1], treating non-finite input as 0.
static float skeleton3d_clamp01f(float value) {
    if (!isfinite(value) || value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
}

/// @brief Compare keyframe times with a small tolerance to collapse float-rounding duplicates.
static int skeleton3d_key_time_equal(float a, float b) {
    return fabsf(a - b) <= 1e-6f;
}

/// @brief Once pose buffers exist, the skeleton topology can no longer grow safely.
static void skeleton3d_freeze(rt_skeleton3d *s) {
    if (s)
        s->frozen = 1;
}

/// @brief Wrap a playback time into `[0, duration)` (looping). Non-finite time or a
///        non-positive/non-finite duration yields 0.0f.
static float animation3d_wrap_time(float time, float duration) {
    if (!isfinite(time))
        return 0.0f;
    if (!isfinite(duration) || duration <= 0.0f)
        return 0.0f;
    time = fmodf(time, duration);
    if (time < 0.0f)
        time += duration;
    if (time >= duration)
        time = 0.0f;
    return time;
}

/// @brief Drop one GC reference held in `*slot` and clear the slot. NULL-safe.
static void mesh3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a retained Skeleton3D slot only if it still points at Skeleton3D.
static void mesh3d_release_skeleton_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_SKELETON3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    mesh3d_release_ref(slot);
}

/// @brief Retain-then-release swap: store @p value into `*slot`, releasing the previous occupant.
/// @details Retain-before-release prevents a self-reassign or shared-owner case from
///   prematurely dropping the refcount to zero during the transition.
static void mesh3d_assign_skeleton_ref(void **slot, void *value) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    mesh3d_release_skeleton_slot(slot);
    *slot = value;
}

/// @brief Invert a 4x4 float matrix. Returns 0 on success, -1 if singular.
static int mat4f_invert(const float *m, float *out) {
    float inv[16];
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

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!isfinite(det) || fabsf(det) < 1e-12f)
        return -1;
    float inv_det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * inv_det;
    return 0;
}

/// @brief Build TRS matrix from float arrays (row-major).
static void build_trs_float(const float *pos, const float *quat, const float *scl, float *out) {
    float x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    float x2 = x + x, y2 = y + y, z2 = z + z;
    float xx = x * x2, xy = x * y2, xz = x * z2;
    float yy = y * y2, yz = y * z2, zz = z * z2;
    float wx = w * x2, wy = w * y2, wz = w * z2;

    out[0] = (1.0f - (yy + zz)) * scl[0];
    out[1] = (xy - wz) * scl[1];
    out[2] = (xz + wy) * scl[2];
    out[3] = pos[0];
    out[4] = (xy + wz) * scl[0];
    out[5] = (1.0f - (xx + zz)) * scl[1];
    out[6] = (yz - wx) * scl[2];
    out[7] = pos[1];
    out[8] = (xz - wy) * scl[0];
    out[9] = (yz + wx) * scl[1];
    out[10] = (1.0f - (xx + yy)) * scl[2];
    out[11] = pos[2];
    out[12] = 0.0f;
    out[13] = 0.0f;
    out[14] = 0.0f;
    out[15] = 1.0f;
}

static void quat_identity_float(float *out);

/// @brief SLERP between two quaternions (float arrays, x,y,z,w).
static void quat_slerp_float(const float *a, const float *b, float t, float *out) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float nb[4] = {b[0], b[1], b[2], b[3]};
    t = skeleton3d_clamp01f(t);
    if (dot < 0.0f) {
        dot = -dot;
        nb[0] = -nb[0];
        nb[1] = -nb[1];
        nb[2] = -nb[2];
        nb[3] = -nb[3];
    }
    if (!isfinite(dot)) {
        quat_identity_float(out);
        return;
    }
    if (dot > 1.0f)
        dot = 1.0f;
    if (dot < -1.0f)
        dot = -1.0f;
    if (dot > 0.9995f) {
        /* Nearly identical: linear interpolation + normalize */
        for (int i = 0; i < 4; i++)
            out[i] = a[i] + t * (nb[i] - a[i]);
    } else {
        float theta = acosf(dot);
        float sin_theta = sinf(theta);
        float wa = sinf((1.0f - t) * theta) / sin_theta;
        float wb = sinf(t * theta) / sin_theta;
        for (int i = 0; i < 4; i++)
            out[i] = wa * a[i] + wb * nb[i];
    }
    /* Normalize */
    float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (isfinite(len) && len > 1e-8f)
        for (int i = 0; i < 4; i++)
            out[i] /= len;
    else
        quat_identity_float(out);
}

/// @brief Write the identity quaternion (0,0,0,1) into `out[4]`.
static void quat_identity_float(float *out) {
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[3] = 1.0f;
}

/// @brief Normalize a quaternion in place, falling back to identity when any lane
///        is non-finite or the length underflows (≤ 1e-8).
static void quat_normalize_float(float *q) {
    float len;
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        quat_identity_float(q);
        return;
    }
    len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(len) || len <= 1e-8f) {
        quat_identity_float(q);
        return;
    }
    for (int i = 0; i < 4; i++)
        q[i] /= len;
}

/// @brief Convert a 3x3 rotation matrix to a unit quaternion in `out[4]`.
/// @details Shepperd's method: branches on the largest of trace/r00/r11/r22 to
///          keep the `sqrt` argument well away from zero, avoiding the precision
///          loss the naive `sqrt(1+trace)` form suffers near 180° rotations.
///          Non-finite input yields identity; the result is normalized.
static void quat_from_matrix3_float(float r00,
                                    float r01,
                                    float r02,
                                    float r10,
                                    float r11,
                                    float r12,
                                    float r20,
                                    float r21,
                                    float r22,
                                    float *out) {
    float tr;
    if (!out)
        return;
    if (!isfinite(r00) || !isfinite(r01) || !isfinite(r02) || !isfinite(r10) || !isfinite(r11) ||
        !isfinite(r12) || !isfinite(r20) || !isfinite(r21) || !isfinite(r22)) {
        quat_identity_float(out);
        return;
    }
    tr = r00 + r11 + r22;
    if (tr > 0.0f) {
        float s = sqrtf(tr + 1.0f) * 2.0f;
        out[3] = 0.25f * s;
        out[0] = (r21 - r12) / s;
        out[1] = (r02 - r20) / s;
        out[2] = (r10 - r01) / s;
    } else if (r00 > r11 && r00 > r22) {
        float s = sqrtf(1.0f + r00 - r11 - r22) * 2.0f;
        out[3] = (r21 - r12) / s;
        out[0] = 0.25f * s;
        out[1] = (r01 + r10) / s;
        out[2] = (r02 + r20) / s;
    } else if (r11 > r22) {
        float s = sqrtf(1.0f + r11 - r00 - r22) * 2.0f;
        out[3] = (r02 - r20) / s;
        out[0] = (r01 + r10) / s;
        out[1] = 0.25f * s;
        out[2] = (r12 + r21) / s;
    } else {
        float s = sqrtf(1.0f + r22 - r00 - r11) * 2.0f;
        out[3] = (r10 - r01) / s;
        out[0] = (r02 + r20) / s;
        out[1] = (r12 + r21) / s;
        out[2] = 0.25f * s;
    }
    quat_normalize_float(out);
}

/// @brief Decompose a row-major 4x4 float matrix into translation, rotation
///        (quaternion) and scale.
/// @details Scale per axis is the length of each basis column; the basis is
///          divided through by scale before quaternion extraction so the rotation
///          is orthonormal. Non-finite entries degrade to 0/identity/unit.
static void mat4f_decompose_trs(const float *m, float *out_pos, float *out_rot, float *out_scl) {
    float sx;
    float sy;
    float sz;
    float inv_sx;
    float inv_sy;
    float inv_sz;
    if (!m || !out_pos || !out_rot || !out_scl)
        return;
    out_pos[0] = isfinite(m[3]) ? m[3] : 0.0f;
    out_pos[1] = isfinite(m[7]) ? m[7] : 0.0f;
    out_pos[2] = isfinite(m[11]) ? m[11] : 0.0f;
    sx = sqrtf(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    sy = sqrtf(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    sz = sqrtf(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    out_scl[0] = isfinite(sx) && sx > 1e-6f ? sx : 1.0f;
    out_scl[1] = isfinite(sy) && sy > 1e-6f ? sy : 1.0f;
    out_scl[2] = isfinite(sz) && sz > 1e-6f ? sz : 1.0f;
    inv_sx = 1.0f / out_scl[0];
    inv_sy = 1.0f / out_scl[1];
    inv_sz = 1.0f / out_scl[2];
    quat_from_matrix3_float(m[0] * inv_sx,
                            m[1] * inv_sy,
                            m[2] * inv_sz,
                            m[4] * inv_sx,
                            m[5] * inv_sy,
                            m[6] * inv_sz,
                            m[8] * inv_sx,
                            m[9] * inv_sy,
                            m[10] * inv_sz,
                            out_rot);
}

/*==========================================================================
 * Skeleton3D implementation
 *=========================================================================*/

/// @brief GC finalizer for a Skeleton3D — releases the bone array and any name strings.
static void rt_skeleton3d_finalize(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)obj;
    free(s->bones);
    s->bones = NULL;
    s->bone_count = 0;
    s->bone_capacity = 0;
}

/// @brief Copy a runtime string into the fixed bone-name storage used by GPU payloads.
static int skeleton3d_copy_canonical_name(rt_string name, char out[64]) {
    const char *cstr;
    size_t len;
    if (!out)
        return 0;
    out[0] = '\0';
    if (!name)
        return 1;
    cstr = rt_string_cstr(name);
    if (!cstr)
        return 0;
    len = strlen(cstr);
    if (len > 63)
        len = 63;
    memcpy(out, cstr, len);
    out[len] = '\0';
    return 1;
}

/// @brief Allocate an empty Skeleton3D — no bones until `add_bone` is called.
void *rt_skeleton3d_new(void) {
    rt_skeleton3d *s =
        (rt_skeleton3d *)rt_obj_new_i64(RT_G3D_SKELETON3D_CLASS_ID, (int64_t)sizeof(rt_skeleton3d));
    if (!s) {
        rt_trap("Skeleton3D.New: memory allocation failed");
        return NULL;
    }
    s->vptr = NULL;
    s->bones = NULL;
    s->bone_count = 0;
    s->bone_capacity = 0;
    s->frozen = 0;
    rt_obj_set_finalizer(s, rt_skeleton3d_finalize);
    return s;
}

/// @brief Append one bone to the skeleton with its bind-pose matrix and parent reference.
///
/// `parent_index` of -1 marks a root bone. `bind_mat4` is the
/// bone-to-model transform in the rest pose; the inverse-bind
/// (model-to-bone) is computed lazily by
/// `rt_skeleton3d_compute_inverse_bind`.
/// @return The newly-assigned bone index (0-based), or -1 on failure.
int64_t rt_skeleton3d_add_bone(void *obj, rt_string name, int64_t parent_index, void *bind_mat4) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return -1;
    if (s->frozen) {
        rt_trap("Skeleton3D.AddBone: skeleton is already bound to an animation runtime");
        return -1;
    }
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (!s->bones)
        s->bone_capacity = 0;
    if (s->bone_capacity < s->bone_count || s->bone_capacity > VGFX3D_MAX_BONES)
        s->bone_capacity = s->bone_count;
    if (s->bone_count >= VGFX3D_MAX_BONES) {
        rt_trap("Skeleton3D.AddBone: max 256 bones exceeded");
        return -1;
    }
    // parent_index == -1 denotes a root bone. Any other negative value is
    // invalid — previously only the upper bound was checked, so callers could
    // pass -2, -100, etc. which survived validation and corrupted the
    // hierarchy in downstream code.
    if (parent_index < -1 || parent_index >= s->bone_count) {
        rt_trap("Skeleton3D.AddBone: parent_index must be -1 (root) or a valid bone index");
        return -1;
    }

    if (s->bone_count >= s->bone_capacity) {
        int32_t new_capacity = s->bone_capacity > 0 ? s->bone_capacity * 2 : 4;
        vgfx3d_bone_t *nb;
        if (new_capacity < s->bone_count + 1)
            new_capacity = s->bone_count + 1;
        if (new_capacity > VGFX3D_MAX_BONES)
            new_capacity = VGFX3D_MAX_BONES;
        nb = (vgfx3d_bone_t *)realloc(s->bones, (size_t)new_capacity * sizeof(vgfx3d_bone_t));
        if (!nb)
            return -1;
        if (new_capacity > s->bone_capacity) {
            memset(nb + s->bone_capacity,
                   0,
                   (size_t)(new_capacity - s->bone_capacity) * sizeof(*nb));
        }
        s->bones = nb;
        s->bone_capacity = new_capacity;
    }

    vgfx3d_bone_t *bone = &s->bones[s->bone_count];
    memset(bone, 0, sizeof(vgfx3d_bone_t));

    if (!skeleton3d_copy_canonical_name(name, bone->name))
        bone->name[0] = '\0';

    bone->parent_index = (int32_t)parent_index;

    /* Copy bind pose from Mat4 (double → float). Malformed matrices fall back to identity. */
    mat4_impl *bind = skeleton3d_mat4_checked(bind_mat4);
    if (bind) {
        int valid = 1;
        float local[16];
        for (int i = 0; i < 16; i++) {
            double value = bind->m[i];
            if (!skeleton3d_value_fits_float(value)) {
                valid = 0;
                break;
            }
            local[i] = (float)value;
        }
        if (valid)
            memcpy(bone->bind_pose_local, local, sizeof(local));
        else
            mat4f_identity(bone->bind_pose_local);
    } else {
        mat4f_identity(bone->bind_pose_local);
    }
    mat4f_identity(bone->inverse_bind);

    int64_t idx = s->bone_count;
    s->bone_count++;
    return idx;
}

/// @brief Precompute the inverse-bind matrix for every bone (model-space → bone-space).
///
/// Skinning multiplies each bone's animated world matrix by its
/// inverse-bind to displace each vertex from rest space into the
/// animated pose. Call this once after all bones are added.
void rt_skeleton3d_compute_inverse_bind(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    float bind_locals[VGFX3D_MAX_BONES * 16];
    float globals[VGFX3D_MAX_BONES * 16];
    if (!s)
        return;
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (s->bone_count <= 0)
        return;

    for (int32_t i = 0; i < s->bone_count; i++)
        memcpy(&bind_locals[i * 16], s->bones[i].bind_pose_local, 16 * sizeof(float));
    skeleton3d_compute_globals_from_locals(s, bind_locals, globals, s->bone_count);

    for (int32_t i = 0; i < s->bone_count; i++) {
        if (mat4f_invert(&globals[i * 16], s->bones[i].inverse_bind) != 0)
            mat4f_identity(s->bones[i].inverse_bind);
    }
}

/// @brief Number of bones in the skeleton (0 for NULL).
int64_t rt_skeleton3d_get_bone_count(void *obj) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return 0;
    s->bone_count = skeleton3d_safe_bone_count(s);
    return s->bone_count;
}

/// @brief Linear search for a bone by name; returns its index or -1 if not found.
int64_t rt_skeleton3d_find_bone(void *obj, rt_string name) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s || !name)
        return -1;
    s->bone_count = skeleton3d_safe_bone_count(s);
    char target[64];
    if (!skeleton3d_copy_canonical_name(name, target))
        return -1;
    for (int32_t i = 0; i < s->bone_count; i++)
        if (strcmp(s->bones[i].name, target) == 0)
            return i;
    return -1;
}

/// @brief Read the name of bone at `index`. Empty string for out-of-range or NULL.
rt_string rt_skeleton3d_get_bone_name(void *obj, int64_t index) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return rt_const_cstr("");
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (index < 0 || index >= s->bone_count)
        return rt_const_cstr("");
    return rt_const_cstr(s->bones[index].name);
}

/// @brief Read the bind-pose Mat4 of bone at `index` (identity for out-of-range / NULL).
void *rt_skeleton3d_get_bone_bind_pose(void *obj, int64_t index) {
    rt_skeleton3d *s = (rt_skeleton3d *)rt_g3d_checked_or_null(obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!s)
        return NULL;
    s->bone_count = skeleton3d_safe_bone_count(s);
    if (index < 0 || index >= s->bone_count)
        return NULL;
    const float *m = s->bones[index].bind_pose_local;
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/*==========================================================================
 * Animation3D implementation
 *=========================================================================*/

/// @brief GC finalizer for Animation3D — releases all keyframe arrays and the name string.
static void rt_animation3d_finalize(void *obj) {
    rt_animation3d *a = (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
    int32_t channel_count;
    if (!a)
        return;
    channel_count = animation3d_safe_channel_count(a);
    for (int32_t i = 0; i < channel_count; i++)
        free(a->channels[i].keyframes);
    free(a->channels);
    a->channels = NULL;
    a->channel_count = 0;
    a->channel_capacity = 0;
}

/// @brief Release the GC-managed object in @p slot and clear the pointer to NULL.
/// @details Mirrors the `scene3d_release_ref` pattern used in rt_scene3d.c — kept
///   as a local copy so this translation unit has no dependency on scene3d internals.
///   Safe to call with a NULL slot or a slot already holding NULL.
static void animation3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a retained private slot only when it still holds the expected 3D class.
static void animation3d_release_class_ref(void **slot, int64_t class_id) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, class_id)) {
        *slot = NULL;
        return;
    }
    animation3d_release_ref(slot);
}

/// @brief Typed retain/release swap for private animation-runtime object slots.
static void animation3d_assign_class_ref(void **slot, void *value, int64_t class_id) {
    if (!slot || *slot == value)
        return;
    rt_obj_retain_maybe(value);
    animation3d_release_class_ref(slot, class_id);
    *slot = value;
}

/// @brief Borrow a private Skeleton3D slot, clearing it if corruption changed its class.
static rt_skeleton3d *animation3d_skeleton_slot(void **slot) {
    if (!slot || !*slot)
        return NULL;
    if (!rt_g3d_has_class(*slot, RT_G3D_SKELETON3D_CLASS_ID)) {
        *slot = NULL;
        return NULL;
    }
    return (rt_skeleton3d *)*slot;
}

/// @brief Borrow a private Animation3D slot, clearing it if corruption changed its class.
static rt_animation3d *animation3d_clip_slot(void **slot) {
    if (!slot || !*slot)
        return NULL;
    if (!rt_g3d_has_class(*slot, RT_G3D_ANIMATION3D_CLASS_ID)) {
        *slot = NULL;
        return NULL;
    }
    return (rt_animation3d *)*slot;
}

/// @brief Ensure a quaternion keyframe is unit-length; substitute identity on any
///        degenerate or non-finite input so bone transforms stay numerically stable.
/// @details Two separate guards: a NaN/Inf component check (which would make the
///   magnitude computation itself non-finite) and a near-zero length check (which
///   would produce NaN on division). The identity quaternion (0,0,0,1) is used as
///   the fallback so a bad keyframe leaves the bone in its rest pose rather than
///   producing a degenerate skinning matrix that collapses or inverts geometry.
/// @param q float[4] quaternion in (x, y, z, w) order; modified in-place.
static void sanitize_keyframe_quat(float *q) {
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (!isfinite(len) || len <= 1e-8f) {
        q[0] = q[1] = q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    q[0] /= len;
    q[1] /= len;
    q[2] /= len;
    q[3] /= len;
}

/// @brief Clamp one sampled animation lane to a finite range that downstream matrix
///        multiplication can tolerate.
static float animation3d_sanitize_lane(float value, float fallback) {
    if (!isfinite(fallback))
        fallback = 0.0f;
    if (!isfinite(value))
        return fallback;
    if (value > SKELETON3D_ANIM_ABS_MAX)
        return SKELETON3D_ANIM_ABS_MAX;
    if (value < -SKELETON3D_ANIM_ABS_MAX)
        return -SKELETON3D_ANIM_ABS_MAX;
    return value;
}

/// @brief Sanitize sampled TRS data before building local bone matrices.
static void animation3d_sanitize_trs(float *pos,
                                     float *rot,
                                     float *scl,
                                     const float *fallback_pos,
                                     const float *fallback_rot,
                                     const float *fallback_scl) {
    int valid_rot = 1;
    if (pos) {
        for (int32_t i = 0; i < 3; ++i) {
            float fallback = fallback_pos ? fallback_pos[i] : 0.0f;
            pos[i] = animation3d_sanitize_lane(pos[i], fallback);
        }
    }
    if (scl) {
        for (int32_t i = 0; i < 3; ++i) {
            float fallback = fallback_scl ? fallback_scl[i] : 1.0f;
            if (!isfinite(fallback))
                fallback = 1.0f;
            scl[i] = animation3d_sanitize_lane(scl[i], fallback);
        }
    }
    if (!rot)
        return;
    for (int32_t i = 0; i < 4; ++i) {
        if (!isfinite(rot[i])) {
            valid_rot = 0;
            break;
        }
    }
    if (!valid_rot) {
        if (fallback_rot && isfinite(fallback_rot[0]) && isfinite(fallback_rot[1]) &&
            isfinite(fallback_rot[2]) && isfinite(fallback_rot[3])) {
            memcpy(rot, fallback_rot, 4 * sizeof(float));
        } else {
            rot[0] = rot[1] = rot[2] = 0.0f;
            rot[3] = 1.0f;
        }
    }
    sanitize_keyframe_quat(rot);
}

/// @brief Create a new empty animation clip with the given identifier and duration (seconds).
void *rt_animation3d_new(rt_string name, double duration) {
    rt_animation3d *a = (rt_animation3d *)rt_obj_new_i64(RT_G3D_ANIMATION3D_CLASS_ID,
                                                         (int64_t)sizeof(rt_animation3d));
    if (!a) {
        rt_trap("Animation3D.New: memory allocation failed");
        return NULL;
    }
    a->vptr = NULL;
    memset(a->name, 0, 64);
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(a->name, cstr, len);
        }
    }
    a->channels = NULL;
    a->channel_count = 0;
    a->channel_capacity = 0;
    a->duration = (isfinite(duration) && duration > 0.0)
                      ? skeleton3d_clamp_nonnegative_float(duration, 1.0f)
                      : 1.0f;
    a->looping = 0;
    rt_obj_set_finalizer(a, rt_animation3d_finalize);
    return a;
}

/// @brief Append one keyframe (time, position, rotation, scale) to a bone's track.
///
/// Keyframes are sorted by time within a track so the player can
/// binary-search to find the correct interpolation interval.
void rt_animation3d_add_keyframe(
    void *obj, int64_t bone_index, double time, void *position, void *rotation, void *scale) {
    rt_animation3d *a = (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
    if (!a || bone_index < 0 || bone_index >= VGFX3D_MAX_BONES || !isfinite(time) ||
        fabs(time) > FLT_MAX)
        return;
    if (time < 0.0)
        time = 0.0;
    if ((position && !rt_g3d_is_vec3(position)) || (rotation && !rt_g3d_is_quat(rotation)) ||
        (scale && !rt_g3d_is_vec3(scale)))
        return;
    vgfx3d_keyframe_t new_kf;
    memset(&new_kf, 0, sizeof(new_kf));
    new_kf.time = (float)time;
    if (position) {
        double px = rt_vec3_x(position);
        double py = rt_vec3_y(position);
        double pz = rt_vec3_z(position);
        if (skeleton3d_value_fits_float(px)) {
            new_kf.position[0] = (float)px;
            new_kf.position_mask |= 1u;
        }
        if (skeleton3d_value_fits_float(py)) {
            new_kf.position[1] = (float)py;
            new_kf.position_mask |= 2u;
        }
        if (skeleton3d_value_fits_float(pz)) {
            new_kf.position[2] = (float)pz;
            new_kf.position_mask |= 4u;
        }
    }
    if (rotation) {
        double qx = rt_quat_x(rotation);
        double qy = rt_quat_y(rotation);
        double qz = rt_quat_z(rotation);
        double qw = rt_quat_w(rotation);
        if (skeleton3d_value_fits_float(qx) && skeleton3d_value_fits_float(qy) &&
            skeleton3d_value_fits_float(qz) && skeleton3d_value_fits_float(qw)) {
            new_kf.rotation[0] = (float)qx;
            new_kf.rotation[1] = (float)qy;
            new_kf.rotation[2] = (float)qz;
            new_kf.rotation[3] = (float)qw;
            new_kf.rotation_mask = 0x0Fu;
        } else {
            new_kf.rotation[3] = 1.0f;
        }
    } else {
        new_kf.rotation[3] = 1.0f;
    }
    sanitize_keyframe_quat(new_kf.rotation);
    new_kf.scale_xyz[0] = 1.0f;
    new_kf.scale_xyz[1] = 1.0f;
    new_kf.scale_xyz[2] = 1.0f;
    if (scale) {
        double sx = rt_vec3_x(scale);
        double sy = rt_vec3_y(scale);
        double sz = rt_vec3_z(scale);
        if (skeleton3d_value_fits_float(sx)) {
            new_kf.scale_xyz[0] = (float)sx;
            new_kf.scale_mask |= 1u;
        }
        if (skeleton3d_value_fits_float(sy)) {
            new_kf.scale_xyz[1] = (float)sy;
            new_kf.scale_mask |= 2u;
        }
        if (skeleton3d_value_fits_float(sz)) {
            new_kf.scale_xyz[2] = (float)sz;
            new_kf.scale_mask |= 4u;
        }
    }

    a->channel_count = animation3d_safe_channel_count(a);
    if (!a->channels)
        a->channel_capacity = 0;

    /* Find or create channel for this bone */
    vgfx3d_anim_channel_t *ch = NULL;
    for (int32_t i = 0; i < a->channel_count; i++)
        if (a->channels[i].bone_index == (int32_t)bone_index) {
            ch = &a->channels[i];
            break;
        }

    if (!ch) {
        if (a->channel_count >= RT_ANIMATION3D_MAX_CHANNELS)
            return;
        if (a->channel_count >= a->channel_capacity) {
            if (a->channel_capacity > INT32_MAX / 2)
                return;
            int32_t new_cap = a->channel_capacity == 0 ? 8 : a->channel_capacity * 2;
            if (new_cap > RT_ANIMATION3D_MAX_CHANNELS)
                new_cap = RT_ANIMATION3D_MAX_CHANNELS;
            if (new_cap <= a->channel_count)
                return;
            if ((size_t)new_cap > SIZE_MAX / sizeof(vgfx3d_anim_channel_t))
                return;
            vgfx3d_anim_channel_t *nc = (vgfx3d_anim_channel_t *)realloc(
                a->channels, (size_t)new_cap * sizeof(vgfx3d_anim_channel_t));
            if (!nc)
                return;
            a->channels = nc;
            a->channel_capacity = new_cap;
        }
        ch = &a->channels[a->channel_count++];
        memset(ch, 0, sizeof(vgfx3d_anim_channel_t));
        ch->bone_index = (int32_t)bone_index;
    }

    ch->keyframe_count = animation3d_safe_keyframe_count(ch);
    if (!ch->keyframes)
        ch->keyframe_capacity = 0;
    if (ch->keyframe_count >= ch->keyframe_capacity) {
        if (ch->keyframe_count >= RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL)
            return;
        if (ch->keyframe_capacity > INT32_MAX / 2)
            return;
        int32_t new_cap = ch->keyframe_capacity == 0 ? 16 : ch->keyframe_capacity * 2;
        if (new_cap > RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL)
            new_cap = RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL;
        if (new_cap <= ch->keyframe_count)
            return;
        if ((size_t)new_cap > SIZE_MAX / sizeof(vgfx3d_keyframe_t))
            return;
        vgfx3d_keyframe_t *nk = (vgfx3d_keyframe_t *)realloc(
            ch->keyframes, (size_t)new_cap * sizeof(vgfx3d_keyframe_t));
        if (!nk)
            return;
        ch->keyframes = nk;
        ch->keyframe_capacity = new_cap;
    }

    int32_t insert = ch->keyframe_count;
    if (insert > 0 && ch->keyframes[insert - 1].time <= new_kf.time) {
        if (skeleton3d_key_time_equal(ch->keyframes[insert - 1].time, new_kf.time)) {
            ch->keyframes[insert - 1] = new_kf;
            return;
        }
    } else {
        while (insert > 0 &&
               ch->keyframes[insert - 1].time > new_kf.time + 1e-6f)
            insert--;
    }
    if (insert < ch->keyframe_count &&
        skeleton3d_key_time_equal(ch->keyframes[insert].time, new_kf.time)) {
        ch->keyframes[insert] = new_kf;
        return;
    }
    if (insert < ch->keyframe_count) {
        memmove(&ch->keyframes[insert + 1],
                &ch->keyframes[insert],
                (size_t)(ch->keyframe_count - insert) * sizeof(ch->keyframes[0]));
    }
    ch->keyframes[insert] = new_kf;
    ch->keyframe_count++;
}

/// @brief Mark this clip as looping (wraps back to t=0 at end) or one-shot (stops at end).
void rt_animation3d_set_looping(void *obj, int8_t loop) {
    rt_animation3d *a = (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
    if (a)
        a->looping = loop ? 1 : 0;
}

/// @brief Read the looping flag (0 = one-shot, 1 = loops).
int8_t rt_animation3d_get_looping(void *obj) {
    rt_animation3d *a = (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
    return a && a->looping ? 1 : 0;
}

/// @brief Total length of the clip in seconds (0.0 for NULL).
double rt_animation3d_get_duration(void *obj) {
    rt_animation3d *a = (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
    return (a && isfinite(a->duration) && a->duration > 0.0f) ? a->duration : 0.0;
}

/// @brief The clip's display / lookup name (empty string for NULL).
rt_string rt_animation3d_get_name(void *obj) {
    rt_animation3d *a = (rt_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMATION3D_CLASS_ID);
    return a ? rt_const_cstr(a->name) : rt_const_cstr("");
}

/// @brief Infer a canonical humanoid-joint id from a bone name so a clip can retarget across
///   skeletons that use different naming conventions (mixamo, Unreal, Blender). @details Lowercases
///   the name, drops separators and a `mixamorig` prefix, detects side (left/right, incl. a
///   trailing l/r on limbs), and matches a base joint by keyword. Returns 0 for unrecognized names
///   (caller falls back to exact-name then index matching). Bare `leg`/`arm` follow mixamo, where
///   `Leg` is the calf and `Arm` the upper arm (`UpLeg`/`ForeArm` disambiguate the others).
static int animation3d_humanoid_role(const char *raw) {
    char b[64];
    int n = 0;
    const char *s;
    int side = 0; /* 0 center, 1 left, 2 right */
    int base = 0;
    if (!raw || !raw[0])
        return 0;
    for (const char *p = raw; *p && n < 63; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c == '_' || c == ' ' || c == '.' || c == '-' || c == ':')
            continue;
        b[n++] = c;
    }
    b[n] = '\0';
    s = b;
    if (strncmp(s, "mixamorig", 9) == 0)
        s += 9;
    if (strstr(s, "left"))
        side = 1;
    else if (strstr(s, "right"))
        side = 2;
    if (strstr(s, "hip") || strstr(s, "pelvis"))
        base = 1;
    else if (strstr(s, "upperchest"))
        base = 4;
    else if (strstr(s, "chest"))
        base = 3;
    else if (strstr(s, "spine"))
        base = 2;
    else if (strstr(s, "neck"))
        base = 5;
    else if (strstr(s, "head"))
        base = 6;
    else if (strstr(s, "shoulder") || strstr(s, "clavicle"))
        base = 7;
    else if (strstr(s, "forearm") || strstr(s, "lowerarm"))
        base = 9;
    else if (strstr(s, "upperarm") || strstr(s, "uparm") || strstr(s, "arm"))
        base = 8;
    else if (strstr(s, "hand"))
        base = 10;
    else if (strstr(s, "upperleg") || strstr(s, "upleg") || strstr(s, "thigh"))
        base = 11;
    else if (strstr(s, "lowerleg") || strstr(s, "calf") || strstr(s, "shin") || strstr(s, "leg"))
        base = 12;
    else if (strstr(s, "foot") || strstr(s, "ankle"))
        base = 13;
    else if (strstr(s, "toe") || strstr(s, "ball"))
        base = 14;
    if (base == 0)
        return 0;
    if (side == 0 && base >= 7) { /* limb without an explicit side word: trailing l/r */
        char last = n > 0 ? b[n - 1] : '\0';
        if (last == 'l')
            side = 1;
        else if (last == 'r')
            side = 2;
    }
    return base * 4 + side;
}

/// @brief Map a source-skeleton bone to the best-matching destination bone for retargeting.
/// @details Prefers an exact name match, then falls back to a humanoid-role match, then to the same
///          index if in range. Returns -1 when no correspondence exists.
static int32_t *animation3d_build_humanoid_role_cache(const rt_skeleton3d *skel) {
    int32_t *roles;
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    if (bone_count <= 0)
        return NULL;
    if ((size_t)bone_count > SIZE_MAX / sizeof(*roles))
        return NULL;
    roles = (int32_t *)malloc((size_t)bone_count * sizeof(*roles));
    if (!roles)
        return NULL;
    for (int32_t i = 0; i < bone_count; ++i)
        roles[i] = animation3d_humanoid_role(skel->bones[i].name);
    return roles;
}

/// @brief Map a source-skeleton bone to the best destination bone for retargeting: exact
///   name match first, then humanoid-role mapping (which handles cross-convention skeletons).
/// @return Destination bone index, or -1 if @p src_bone is invalid or has no match.
static int32_t animation3d_retarget_find_bone(const rt_skeleton3d *src,
                                              const rt_skeleton3d *dst,
                                              const int32_t *src_roles,
                                              const int32_t *dst_roles,
                                              int32_t src_bone) {
    int32_t src_count = skeleton3d_safe_bone_count(src);
    int32_t dst_count = skeleton3d_safe_bone_count(dst);
    if (!src || !dst || src_bone < 0 || src_bone >= src_count)
        return -1;
    const char *name = src->bones[src_bone].name;
    int role;
    if (name && name[0] != '\0') {
        for (int32_t i = 0; i < dst_count; ++i)
            if (strcmp(dst->bones[i].name, name) == 0)
                return i;
    }
    /* No exact-name match: try humanoid role mapping (handles cross-convention skeletons). */
    role = src_roles ? src_roles[src_bone] : animation3d_humanoid_role(name);
    if (role != 0) {
        for (int32_t i = 0; i < dst_count; ++i)
            if ((dst_roles ? dst_roles[i] : animation3d_humanoid_role(dst->bones[i].name)) == role)
                return i;
    }
    return src_bone < dst_count ? src_bone : -1;
}

/// @brief Whether @p anim already has an animation channel targeting @p bone.
/// @details Guards retargeting against two source bones that map to the same destination
///          bone both emitting channels — the first mapping wins, avoiding duplicates.
static int animation3d_has_channel_for_bone(const rt_animation3d *anim, int32_t bone) {
    int32_t channel_count;
    if (!anim)
        return 0;
    channel_count = animation3d_safe_channel_count(anim);
    for (int32_t i = 0; i < channel_count; ++i)
        if (anim->channels[i].bone_index == bone)
            return 1;
    return 0;
}

/// @brief Bone length = magnitude of the bind-pose local translation (offset
///   from its parent). Used to scale retargeted translations between skeletons
///   of different proportions. Row-major translation lives at m[3]/m[7]/m[11].
static float animation3d_bone_bind_length(const rt_skeleton3d *skel, int32_t bone) {
    const float *m;
    if (!skel || bone < 0 || bone >= skeleton3d_safe_bone_count(skel))
        return 0.0f;
    m = skel->bones[bone].bind_pose_local;
    {
        float len = sqrtf(m[3] * m[3] + m[7] * m[7] + m[11] * m[11]);
        return isfinite(len) ? len : 0.0f;
    }
}

/// @brief Copy an animation channel onto @p dst_bone, scaling its translation keys by @p pos_scale.
/// @details Duplicates the source keyframes; translation keys are multiplied by the bone-length
/// ratio
///          (rotations/scales transfer unchanged) so motion fits a differently-proportioned
///          skeleton.
/// @return 1 on success, 0 if the channel table is full or a keyframe copy fails.
static int animation3d_retarget_copy_channel(rt_animation3d *dst,
                                             const vgfx3d_anim_channel_t *src,
                                             int32_t dst_bone,
                                             float pos_scale) {
    static const float fallback_pos[3] = {0.0f, 0.0f, 0.0f};
    static const float fallback_rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    static const float fallback_scl[3] = {1.0f, 1.0f, 1.0f};
    int32_t keyframe_count = animation3d_safe_keyframe_count(src);
    if (!dst || !src)
        return 0;
    dst->channel_count = animation3d_safe_channel_count(dst);
    if (dst->channel_count >= dst->channel_capacity)
        return 0;
    if (keyframe_count > 0 && !src->keyframes)
        return 0;
    if (!isfinite(pos_scale) || pos_scale <= 0.0f)
        pos_scale = 1.0f;
    vgfx3d_anim_channel_t *out = &dst->channels[dst->channel_count++];
    memset(out, 0, sizeof(*out));
    out->bone_index = dst_bone;
    out->keyframe_count = keyframe_count;
    out->keyframe_capacity = keyframe_count;
    if (keyframe_count == 0)
        return 1;
    out->keyframes =
        (vgfx3d_keyframe_t *)malloc((size_t)keyframe_count * sizeof(vgfx3d_keyframe_t));
    if (!out->keyframes) {
        out->keyframe_count = 0;
        out->keyframe_capacity = 0;
        return 0;
    }
    memcpy(out->keyframes, src->keyframes, (size_t)keyframe_count * sizeof(*out->keyframes));
    /* Proportional retarget: scale translation keys by the bone-length ratio so a
     * clip authored on one skeleton fits a differently-proportioned one.
     * Rotations and scales transfer unchanged. */
    for (int32_t k = 0; k < out->keyframe_count; ++k) {
        out->keyframes[k].time =
            skeleton3d_clamp_nonnegative_float(out->keyframes[k].time, 0.0f);
        if (pos_scale != 1.0f) {
            out->keyframes[k].position[0] =
                skeleton3d_clamp_to_float((double)out->keyframes[k].position[0] * pos_scale, 0.0f);
            out->keyframes[k].position[1] =
                skeleton3d_clamp_to_float((double)out->keyframes[k].position[1] * pos_scale, 0.0f);
            out->keyframes[k].position[2] =
                skeleton3d_clamp_to_float((double)out->keyframes[k].position[2] * pos_scale, 0.0f);
        }
        animation3d_sanitize_trs(out->keyframes[k].position,
                                 out->keyframes[k].rotation,
                                 out->keyframes[k].scale_xyz,
                                 fallback_pos,
                                 fallback_rot,
                                 fallback_scl);
    }
    return 1;
}

/// @brief Retarget an animation clip from a source skeleton onto a destination skeleton.
/// @details Builds a new clip preserving duration/looping, then for each source channel
///          maps its bone to the destination (by humanoid role or index), scales translation
///          keys by the bone-length ratio so the motion fits differing proportions, and
///          carries rotations/scales over unchanged. Bones with no mapping are skipped.
/// @return New Animation3D handle, or NULL on invalid input or allocation failure.
void *rt_animation3d_retarget(void *animation, void *src_skeleton, void *dst_skeleton) {
    rt_animation3d *src_anim = animation3d_checked(animation);
    rt_skeleton3d *src_skel =
        (rt_skeleton3d *)rt_g3d_checked_or_null(src_skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    rt_skeleton3d *dst_skel =
        (rt_skeleton3d *)rt_g3d_checked_or_null(dst_skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    void *out_obj;
    rt_animation3d *out;
    int32_t *src_roles = NULL;
    int32_t *dst_roles = NULL;
    int32_t src_channel_count;
    if (!src_anim || !src_skel || !dst_skel)
        return NULL;
    out_obj = rt_animation3d_new(rt_const_cstr(src_anim->name), src_anim->duration);
    out = animation3d_checked(out_obj);
    if (!out)
        return NULL;
    out->looping = src_anim->looping ? 1 : 0;
    src_channel_count = animation3d_safe_channel_count(src_anim);
    if (src_channel_count <= 0)
        return out_obj;
    if ((size_t)src_channel_count > SIZE_MAX / sizeof(vgfx3d_anim_channel_t)) {
        animation3d_release_ref(&out_obj);
        return NULL;
    }
    out->channels =
        (vgfx3d_anim_channel_t *)calloc((size_t)src_channel_count, sizeof(*out->channels));
    if (!out->channels) {
        animation3d_release_ref(&out_obj);
        return NULL;
    }
    out->channel_capacity = src_channel_count;
    src_roles = animation3d_build_humanoid_role_cache(src_skel);
    dst_roles = animation3d_build_humanoid_role_cache(dst_skel);
    for (int32_t i = 0; i < src_channel_count; ++i) {
        const vgfx3d_anim_channel_t *src_ch = &src_anim->channels[i];
        int32_t dst_bone =
            animation3d_retarget_find_bone(src_skel, dst_skel, src_roles, dst_roles, src_ch->bone_index);
        float src_len, dst_len, pos_scale;
        if (dst_bone < 0 || animation3d_has_channel_for_bone(out, dst_bone))
            continue;
        src_len = animation3d_bone_bind_length(src_skel, src_ch->bone_index);
        dst_len = animation3d_bone_bind_length(dst_skel, dst_bone);
        pos_scale = (src_len > 1e-6f && dst_len > 1e-6f) ? (dst_len / src_len) : 1.0f;
        if (!animation3d_retarget_copy_channel(out, src_ch, dst_bone, pos_scale)) {
            free(src_roles);
            free(dst_roles);
            animation3d_release_ref(&out_obj);
            return NULL;
        }
    }
    free(src_roles);
    free(dst_roles);
    return out_obj;
}

/*==========================================================================
 * Keyframe sampling
 *=========================================================================*/

/// @brief Find the channel for @p bone_index in an animation clip.
static const vgfx3d_anim_channel_t *animation3d_find_channel(const rt_animation3d *anim,
                                                             int32_t bone_index) {
    if (!anim || bone_index < 0)
        return NULL;
    for (int32_t i = 0, channel_count = animation3d_safe_channel_count(anim); i < channel_count;
         i++) {
        if (anim->channels[i].bone_index == bone_index)
            return &anim->channels[i];
    }
    return NULL;
}

/// @brief Resolve a keyframe's optional components against bind-pose fallback TRS.
static void keyframe_effective_trs(const vgfx3d_keyframe_t *key,
                                   const float *fallback_pos,
                                   const float *fallback_rot,
                                   const float *fallback_scl,
                                   float *out_pos,
                                   float *out_rot,
                                   float *out_scl) {
    if (!key || !fallback_pos || !fallback_rot || !fallback_scl)
        return;
    for (int32_t i = 0; i < 3; i++) {
        uint8_t bit = (uint8_t)(1u << i);
        out_pos[i] = (key->position_mask & bit) ? key->position[i] : fallback_pos[i];
        out_scl[i] = (key->scale_mask & bit) ? key->scale_xyz[i] : fallback_scl[i];
    }
    if ((key->rotation_mask & 0x0Fu) == 0x0Fu) {
        memcpy(out_rot, key->rotation, 4 * sizeof(float));
    } else {
        memcpy(out_rot, fallback_rot, 4 * sizeof(float));
    }
    animation3d_sanitize_trs(out_pos, out_rot, out_scl, fallback_pos, fallback_rot, fallback_scl);
}

/// @brief Sample a channel at time t, returning separate TRS components.
static void sample_channel_trs_with_fallback(const vgfx3d_anim_channel_t *ch,
                                             float t,
                                             const float *fallback_pos,
                                             const float *fallback_rot,
                                             const float *fallback_scl,
                                             float *out_pos,
                                             float *out_rot,
                                             float *out_scl) {
    int32_t keyframe_count = animation3d_safe_keyframe_count(ch);
    if (!isfinite(t))
        t = 0.0f;
    if (!ch || keyframe_count <= 0 || !ch->keyframes) {
        memcpy(out_pos, fallback_pos, 3 * sizeof(float));
        memcpy(out_rot, fallback_rot, 4 * sizeof(float));
        memcpy(out_scl, fallback_scl, 3 * sizeof(float));
        animation3d_sanitize_trs(out_pos, out_rot, out_scl, fallback_pos, fallback_rot, fallback_scl);
        return;
    }
    if (keyframe_count == 1) {
        keyframe_effective_trs(
            &ch->keyframes[0], fallback_pos, fallback_rot, fallback_scl, out_pos, out_rot, out_scl);
        return;
    }

    if (t <= ch->keyframes[0].time) {
        keyframe_effective_trs(
            &ch->keyframes[0], fallback_pos, fallback_rot, fallback_scl, out_pos, out_rot, out_scl);
        return;
    }
    if (t >= ch->keyframes[keyframe_count - 1].time) {
        keyframe_effective_trs(&ch->keyframes[keyframe_count - 1],
                               fallback_pos,
                               fallback_rot,
                               fallback_scl,
                               out_pos,
                               out_rot,
                               out_scl);
        return;
    }

    /* Find bracketing keyframes */
    int k0 = 0, k1 = keyframe_count - 1;
    while (k1 - k0 > 1) {
        int mid = k0 + (k1 - k0) / 2;
        if (ch->keyframes[mid].time <= t)
            k0 = mid;
        else
            k1 = mid;
    }

    float t0 = ch->keyframes[k0].time;
    float t1 = ch->keyframes[k1].time;
    float alpha = skeleton3d_clamp01f((t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f);

    const vgfx3d_keyframe_t *key0 = &ch->keyframes[k0];
    const vgfx3d_keyframe_t *key1 = &ch->keyframes[k1];
    float pos0[3], rot0[4], scl0[3];
    float pos1[3], rot1[4], scl1[3];
    keyframe_effective_trs(key0, fallback_pos, fallback_rot, fallback_scl, pos0, rot0, scl0);
    keyframe_effective_trs(key1, fallback_pos, fallback_rot, fallback_scl, pos1, rot1, scl1);

    for (int i = 0; i < 3; i++) {
        out_pos[i] = pos0[i] + alpha * (pos1[i] - pos0[i]);
        out_scl[i] = scl0[i] + alpha * (scl1[i] - scl0[i]);
    }
    quat_slerp_float(rot0, rot1, alpha, out_rot);
    animation3d_sanitize_trs(out_pos, out_rot, out_scl, fallback_pos, fallback_rot, fallback_scl);
}

/// @brief Sample a channel at time t, producing a local TRS matrix.
static void sample_channel_with_fallback(const vgfx3d_anim_channel_t *ch,
                                         float t,
                                         const float *fallback_local,
                                         float *out_local) {
    float fallback_pos[3], fallback_rot[4], fallback_scl[3];
    float pos[3], rot[4], scl[3];
    mat4f_decompose_trs(fallback_local, fallback_pos, fallback_rot, fallback_scl);
    sample_channel_trs_with_fallback(
        ch, t, fallback_pos, fallback_rot, fallback_scl, pos, rot, scl);
    build_trs_float(pos, rot, scl, out_local);
}

/// @brief Sample an animation's bone channel, falling back to bind pose when absent.
static void sample_animation_trs_for_bone(const rt_animation3d *anim,
                                          int32_t bone_index,
                                          float time,
                                          const float *fallback_local,
                                          float *out_pos,
                                          float *out_rot,
                                          float *out_scl) {
    float fallback_pos[3], fallback_rot[4], fallback_scl[3];
    mat4f_decompose_trs(fallback_local, fallback_pos, fallback_rot, fallback_scl);
    sample_channel_trs_with_fallback(animation3d_find_channel(anim, bone_index),
                                     time,
                                     fallback_pos,
                                     fallback_rot,
                                     fallback_scl,
                                     out_pos,
                                     out_rot,
                                     out_scl);
}

/*==========================================================================
 * AnimPlayer3D implementation
 *=========================================================================*/

/// @brief GC finalizer for AnimPlayer3D — release the bound skeleton/animation refs and the bone
/// palette buffer.
static void rt_anim_player3d_finalize(void *obj) {
    rt_anim_player3d *p = (rt_anim_player3d *)obj;
    animation3d_release_class_ref((void **)&p->skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    animation3d_release_class_ref((void **)&p->current, RT_G3D_ANIMATION3D_CLASS_ID);
    animation3d_release_class_ref((void **)&p->crossfade_from, RT_G3D_ANIMATION3D_CLASS_ID);
    free(p->bone_palette);
    p->bone_palette = NULL;
    free(p->prev_bone_palette);
    p->prev_bone_palette = NULL;
    free(p->motion_palette_snapshot);
    p->motion_palette_snapshot = NULL;
    free(p->local_transforms);
    p->local_transforms = NULL;
    free(p->globals_buf);
    p->globals_buf = NULL;
}

static void compute_bone_palette(rt_anim_player3d *p);
static void compute_player_bind_pose(rt_anim_player3d *p);
static int8_t anim_player_current_looping(rt_anim_player3d *p);

/// @brief Create an animation player bound to a target skeleton.
///
/// The player holds the current playback time, the active and
/// fading-out clip, and a per-bone palette buffer (3-frame ring
/// for motion-blur previous-pose lookup).
void *rt_anim_player3d_new(void *skeleton) {
    rt_skeleton3d *skel =
        (rt_skeleton3d *)rt_g3d_checked_or_null(skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    if (!skel) {
        rt_trap("AnimPlayer3D.New: null skeleton");
        return NULL;
    }
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    skel->bone_count = bone_count;

    rt_anim_player3d *p = (rt_anim_player3d *)rt_obj_new_i64(RT_G3D_ANIMPLAYER3D_CLASS_ID,
                                                             (int64_t)sizeof(rt_anim_player3d));
    if (!p) {
        rt_trap("AnimPlayer3D.New: memory allocation failed");
        return NULL;
    }
    p->vptr = NULL;
    p->skeleton = skel;
    rt_obj_retain_maybe(skeleton);
    p->current = NULL;
    p->crossfade_from = NULL;
    p->current_time = 0.0f;
    p->crossfade_time = 0.0f;
    p->crossfade_duration = 0.0f;
    p->crossfade_from_time = 0.0f;
    p->crossfade_from_speed = 1.0f;
    p->speed = 1.0f;
    p->playing = 0;
    p->loop_override_enabled = 0;
    p->loop_override_value = 0;
    p->crossfade_from_looping = 0;
    p->last_motion_frame = 0;
    p->has_prev_motion_palette = 0;

    size_t palette_size = (size_t)VGFX3D_MAX_BONES * 16 * sizeof(float);
    p->bone_palette = (float *)calloc(1, palette_size);
    p->prev_bone_palette = (float *)calloc(1, palette_size);
    p->motion_palette_snapshot = (float *)calloc(1, palette_size);
    p->local_transforms = (float *)calloc(1, palette_size);
    p->globals_buf = (float *)calloc(1, palette_size);
    if (!p->bone_palette || !p->prev_bone_palette || !p->motion_palette_snapshot ||
        !p->local_transforms || !p->globals_buf) {
        rt_anim_player3d_finalize(p);
        if (rt_obj_release_check0(p))
            rt_obj_free(p);
        rt_trap("AnimPlayer3D.New: memory allocation failed");
        return NULL;
    }

    skeleton3d_freeze(skel);
    compute_player_bind_pose(p);
    if (palette_size > 0) {
        memcpy(p->prev_bone_palette, p->bone_palette, palette_size);
        memcpy(p->motion_palette_snapshot, p->bone_palette, palette_size);
    }

    rt_obj_set_finalizer(p, rt_anim_player3d_finalize);
    return p;
}

/// @brief Snap-cut to a new animation clip, resetting playback time to 0.
/// For smooth transitions, prefer `rt_anim_player3d_crossfade`.
void rt_anim_player3d_play(void *obj, void *animation) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    rt_animation3d *anim = animation ? animation3d_checked(animation) : NULL;
    if (!p || (animation && !anim))
        return;
    animation3d_assign_class_ref((void **)&p->current, anim, RT_G3D_ANIMATION3D_CLASS_ID);
    p->current_time = 0.0f;
    p->playing = anim ? 1 : 0;
    animation3d_assign_class_ref((void **)&p->crossfade_from, NULL, RT_G3D_ANIMATION3D_CLASS_ID);
    p->crossfade_time = 0.0f;
    p->crossfade_duration = 0.0f;
    p->crossfade_from_time = 0.0f;
    p->has_prev_motion_palette = 0;
    p->last_motion_frame = 0;
    compute_bone_palette(p);
}

/// @brief Cross-fade smoothly into a new animation clip over `duration` seconds.
///
/// Both clips run in parallel during the fade; per-bone TRS is
/// linearly blended (lerp + slerp) by the elapsed-fade ratio.
void rt_anim_player3d_crossfade(void *obj, void *animation, double duration) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    rt_animation3d *anim = animation3d_checked(animation);
    rt_animation3d *current;
    if (!p || !anim)
        return;
    if (!isfinite(duration) || duration <= 0.0) {
        rt_anim_player3d_play(obj, anim);
        return;
    }
    current = animation3d_clip_slot((void **)&p->current);
    animation3d_assign_class_ref(
        (void **)&p->crossfade_from, current, RT_G3D_ANIMATION3D_CLASS_ID);
    p->crossfade_from_time = p->current_time;
    p->crossfade_from_speed = isfinite(p->speed) ? p->speed : 1.0f;
    p->crossfade_from_looping = anim_player_current_looping(p);
    animation3d_assign_class_ref((void **)&p->current, anim, RT_G3D_ANIMATION3D_CLASS_ID);
    p->current_time = 0.0f;
    p->crossfade_time = 0.0f;
    p->crossfade_duration = skeleton3d_clamp_nonnegative_float(duration, FLT_MAX);
    p->playing = 1;
    p->has_prev_motion_palette = 0;
    p->last_motion_frame = 0;
    compute_bone_palette(p);
}

/// @brief Stop playback and return the output pose to the skeleton bind pose.
void rt_anim_player3d_stop(void *obj) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    if (!p)
        return;
    p->playing = 0;
    animation3d_assign_class_ref((void **)&p->crossfade_from, NULL, RT_G3D_ANIMATION3D_CLASS_ID);
    p->crossfade_time = 0.0f;
    p->crossfade_duration = 0.0f;
    p->crossfade_from_time = 0.0f;
    p->has_prev_motion_palette = 0;
    p->last_motion_frame = 0;
    compute_player_bind_pose(p);
}

/// @brief Build global matrices and skinning palette from local bone transforms.
static void compute_palette_from_locals(const rt_skeleton3d *skel,
                                        const float *locals,
                                        float *globals,
                                        float *palette) {
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    if (!skel || !locals || !globals || !palette || bone_count <= 0)
        return;

    skeleton3d_compute_globals_from_locals(skel, locals, globals, bone_count);
    for (int32_t i = 0; i < bone_count; i++)
        mat4f_mul_local(&globals[i * 16], skel->bones[i].inverse_bind, &palette[i * 16]);
}

/// @brief Force a player to the skeleton bind pose in both world and skinning buffers.
static void compute_player_bind_pose(rt_anim_player3d *p) {
    rt_skeleton3d *skel = p ? animation3d_skeleton_slot((void **)&p->skeleton) : NULL;
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    if (!p || !skel || bone_count <= 0 || !p->local_transforms || !p->globals_buf ||
        !p->bone_palette)
        return;
    skel->bone_count = bone_count;
    for (int32_t i = 0; i < bone_count; i++)
        memcpy(&p->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));
    compute_palette_from_locals(skel, p->local_transforms, p->globals_buf, p->bone_palette);
}

/// @brief Compute the bone palette from the current animation state.
static void compute_bone_palette(rt_anim_player3d *p) {
    rt_skeleton3d *skel = p ? animation3d_skeleton_slot((void **)&p->skeleton) : NULL;
    rt_animation3d *current = p ? animation3d_clip_slot((void **)&p->current) : NULL;
    rt_animation3d *crossfade_from =
        p ? animation3d_clip_slot((void **)&p->crossfade_from) : NULL;
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    if (!p || !skel || bone_count == 0 || !p->local_transforms || !p->globals_buf ||
        !p->bone_palette)
        return;
    skel->bone_count = bone_count;

    /* Start with bind pose for all bones */
    for (int32_t i = 0; i < bone_count; i++)
        memcpy(&p->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

    /* Override with animated transforms from current animation */
    if (current) {
        int32_t channel_count = animation3d_safe_channel_count(current);
        current->channel_count = channel_count;
        for (int32_t c = 0; c < channel_count; c++) {
            int32_t bone = current->channels[c].bone_index;
            if (bone >= 0 && bone < bone_count) {
                sample_channel_with_fallback(&current->channels[c],
                                             p->current_time,
                                             skel->bones[bone].bind_pose_local,
                                             &p->local_transforms[bone * 16]);
            }
        }
    }

    /* Crossfade over every bone so target-only and source-only channels blend
     * against bind pose instead of popping at transition boundaries. */
    if (crossfade_from && current && p->crossfade_duration > 0.0f) {
        float factor = skeleton3d_clamp01f(p->crossfade_time / p->crossfade_duration);

        for (int32_t bone = 0; bone < bone_count; bone++) {
            float from_pos[3], from_rot[4], from_scl[3];
            float to_pos[3], to_rot[4], to_scl[3];
            float blend_pos[3], blend_rot[4], blend_scl[3];
            sample_animation_trs_for_bone(crossfade_from,
                                          bone,
                                          p->crossfade_from_time,
                                          skel->bones[bone].bind_pose_local,
                                          from_pos,
                                          from_rot,
                                          from_scl);
            sample_animation_trs_for_bone(current,
                                          bone,
                                          p->current_time,
                                          skel->bones[bone].bind_pose_local,
                                          to_pos,
                                          to_rot,
                                          to_scl);
            for (int i = 0; i < 3; i++) {
                blend_pos[i] = from_pos[i] + factor * (to_pos[i] - from_pos[i]);
                blend_scl[i] = from_scl[i] + factor * (to_scl[i] - from_scl[i]);
            }
            quat_slerp_float(from_rot, to_rot, factor, blend_rot);
            animation3d_sanitize_trs(blend_pos, blend_rot, blend_scl, from_pos, from_rot, from_scl);
            build_trs_float(blend_pos, blend_rot, blend_scl, &p->local_transforms[bone * 16]);
        }
    }

    compute_palette_from_locals(skel, p->local_transforms, p->globals_buf, p->bone_palette);
}

/// @brief True if the player's currently active clip is set to loop.
static int8_t anim_player_current_looping(rt_anim_player3d *p) {
    rt_animation3d *current = p ? animation3d_clip_slot((void **)&p->current) : NULL;
    if (!current)
        return 0;
    if (p->loop_override_enabled)
        return p->loop_override_value ? 1 : 0;
    return current->looping ? 1 : 0;
}

/// @brief Advance playback by `delta_time` seconds and refresh the bone palette.
///
/// Handles wrap-around for looping clips, fade-in / fade-out for
/// active cross-fades, and copies the new bone palette into the
/// motion-blur ring buffer for the next-frame previous-pose lookup.
void rt_anim_player3d_update(void *obj, double delta_time) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    rt_animation3d *current;
    rt_animation3d *crossfade_from;
    if (!p)
        return;
    current = animation3d_clip_slot((void **)&p->current);
    if (!p->playing || !current)
        return;
    if (!isfinite(delta_time) || delta_time < 0.0)
        return;
    if (!isfinite(p->speed))
        p->speed = 1.0f;

    p->current_time += skeleton3d_clamp_to_float(delta_time * (double)p->speed, 0.0f);
    if (!isfinite(p->current_time))
        p->current_time = 0.0f;

    /* Handle looping / end */
    if (anim_player_current_looping(p)) {
        if (current->duration > 0.0f)
            p->current_time = animation3d_wrap_time(p->current_time, current->duration);
    } else {
        if (p->current_time >= current->duration) {
            p->current_time = current->duration;
            p->playing = 0;
        } else if (p->current_time <= 0.0f && p->speed < 0.0f) {
            p->current_time = 0.0f;
            p->playing = 0;
        }
    }

    /* Update crossfade */
    crossfade_from = animation3d_clip_slot((void **)&p->crossfade_from);
    if (crossfade_from) {
        p->crossfade_time += skeleton3d_clamp_to_float(delta_time, 0.0f);
        p->crossfade_from_time +=
            skeleton3d_clamp_to_float(delta_time * (double)p->crossfade_from_speed, 0.0f);
        if (!isfinite(p->crossfade_time))
            p->crossfade_time = 0.0f;
        if (!isfinite(p->crossfade_from_time))
            p->crossfade_from_time = 0.0f;
        if (crossfade_from->duration > 0.0f) {
            if (p->crossfade_from_looping)
                p->crossfade_from_time =
                    animation3d_wrap_time(p->crossfade_from_time, crossfade_from->duration);
            else if (p->crossfade_from_time > crossfade_from->duration)
                p->crossfade_from_time = crossfade_from->duration;
            else if (p->crossfade_from_time < 0.0f)
                p->crossfade_from_time = 0.0f;
        }
        if (p->crossfade_time >= p->crossfade_duration) {
            animation3d_assign_class_ref(
                (void **)&p->crossfade_from, NULL, RT_G3D_ANIMATION3D_CLASS_ID);
            p->crossfade_time = 0.0f;
            p->crossfade_duration = 0.0f;
            p->crossfade_from_time = 0.0f;
            p->crossfade_from_speed = 1.0f;
            p->crossfade_from_looping = 0;
        }
    }

    compute_bone_palette(p);
}

/// @brief Time-scale factor applied per `update` call (1.0 = real time, 0.5 = slow-mo, etc.).
void rt_anim_player3d_set_speed(void *obj, double speed) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    if (p) {
        if (!isfinite(speed))
            speed = 1.0;
        p->speed = skeleton3d_clamp_to_float(speed, 1.0f);
    }
}

/// @brief Current speed multiplier (1.0 if `obj` is NULL).
double rt_anim_player3d_get_speed(void *obj) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    return (p && isfinite(p->speed)) ? p->speed : 1.0;
}

/// @brief True if the player is currently advancing time on `update` calls.
int8_t rt_anim_player3d_is_playing(void *obj) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    return p && p->playing ? 1 : 0;
}

/// @brief Current playback time within the active clip (seconds).
double rt_anim_player3d_get_time(void *obj) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    return (p && isfinite(p->current_time)) ? p->current_time : 0.0;
}

/// @brief Seek to an absolute time in the current clip.
/// Resets the motion-blur previous-pose snapshot so blur doesn't span the discontinuity.
void rt_anim_player3d_set_time(void *obj, double time) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    if (p) {
        rt_animation3d *current = animation3d_clip_slot((void **)&p->current);
        if (!isfinite(time))
            time = 0.0;
        if (current && current->duration > 0.0f && anim_player_current_looping(p)) {
            p->current_time =
                animation3d_wrap_time(skeleton3d_clamp_to_float(time, 0.0f), current->duration);
        } else {
            if (time < 0.0)
                time = 0.0;
            if (current && current->duration > 0.0f && time > current->duration)
                time = current->duration;
            p->current_time = skeleton3d_float_or(time, 0.0f);
        }
        p->has_prev_motion_palette = 0;
        p->last_motion_frame = 0;
        if (current)
            compute_bone_palette(p);
        else
            compute_player_bind_pose(p);
    }
}

/// @brief Promote last frame's pose into the "previous" slot for motion-blur shaders.
///
/// Called at the start of each frame's draw — promotes the last
/// snapshot to `prev_bone_palette` and snapshots the current pose
/// into `motion_palette_snapshot` for the *next* frame to consume.
/// Returns the previous palette (or NULL if no prior frame yet).
static const float *anim_player_prepare_prev_palette(rt_anim_player3d *p, int64_t frame_serial) {
    rt_skeleton3d *skel;
    if (!p || !p->bone_palette)
        return NULL;
    skel = animation3d_skeleton_slot((void **)&p->skeleton);
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    if (skel)
        skel->bone_count = bone_count;
    size_t palette_size = (size_t)bone_count * 16 * sizeof(float);
    if (p->last_motion_frame != frame_serial) {
        if (p->prev_bone_palette && p->motion_palette_snapshot && bone_count > 0 &&
            p->last_motion_frame != 0) {
            memcpy(p->prev_bone_palette, p->motion_palette_snapshot, palette_size);
            p->has_prev_motion_palette = 1;
        } else if (!p->prev_bone_palette) {
            p->has_prev_motion_palette = 0;
        }
        if (p->motion_palette_snapshot && bone_count > 0)
            memcpy(p->motion_palette_snapshot, p->bone_palette, palette_size);
        p->last_motion_frame = frame_serial;
    }
    return p->has_prev_motion_palette ? p->prev_bone_palette : NULL;
}

/// @brief Read the current world matrix for one bone as a Mat4 (NULL on out-of-range).
void *rt_anim_player3d_get_bone_matrix(void *obj, int64_t bone_index) {
    rt_anim_player3d *p = anim_player3d_checked(obj);
    rt_skeleton3d *skel;
    int32_t bone_count;
    if (!p)
        return NULL;
    skel = animation3d_skeleton_slot((void **)&p->skeleton);
    if (!skel)
        return NULL;
    bone_count = skeleton3d_safe_bone_count(skel);
    skel->bone_count = bone_count;
    if (bone_index < 0 || bone_index >= bone_count)
        return NULL;
    if (!p->globals_buf)
        compute_bone_palette(p);
    if (!p->globals_buf)
        return NULL;
    const float *m = &p->globals_buf[bone_index * 16];
    return rt_mat4_new(m[0],
                       m[1],
                       m[2],
                       m[3],
                       m[4],
                       m[5],
                       m[6],
                       m[7],
                       m[8],
                       m[9],
                       m[10],
                       m[11],
                       m[12],
                       m[13],
                       m[14],
                       m[15]);
}

/*==========================================================================
 * Mesh3D extensions
 *=========================================================================*/

/// @brief Recompute the highest positively weighted bone referenced by a mesh.
static int32_t mesh3d_recompute_weight_bone_count(const rt_mesh3d *m) {
    int32_t max_bone = -1;
    uint32_t vertex_count = rt_mesh3d_safe_vertex_count(m);
    if (!m || !m->vertices)
        return 0;
    for (uint32_t vi = 0; vi < vertex_count; vi++) {
        const vgfx3d_vertex_t *v = &m->vertices[vi];
        for (int i = 0; i < 4; i++) {
            if (v->bone_weights[i] > 0.0f && v->bone_indices[i] > max_bone)
                max_bone = v->bone_indices[i];
        }
    }
    return max_bone >= 0 ? max_bone + 1 : 0;
}

/// @brief Bind a skeleton to a mesh so its per-vertex `bone_indices/weights` can drive skinning.
void rt_mesh3d_set_skeleton(void *mesh, void *skeleton) {
    rt_mesh3d *m = (rt_mesh3d *)rt_g3d_checked_or_null(mesh, RT_G3D_MESH3D_CLASS_ID);
    rt_skeleton3d *s =
        (rt_skeleton3d *)rt_g3d_checked_or_null(skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    if (!m)
        return;
    if (m->skeleton_ref && !rt_g3d_has_class(m->skeleton_ref, RT_G3D_SKELETON3D_CLASS_ID))
        mesh3d_release_skeleton_slot(&m->skeleton_ref);
    if (skeleton && !s)
        return;
    mesh3d_assign_skeleton_ref(&m->skeleton_ref, s);
    if (s) {
        skeleton3d_freeze(s);
        m->bone_count = s->bone_count > VGFX3D_MAX_BONES ? VGFX3D_MAX_BONES : s->bone_count;
    } else {
        m->bone_count = 0;
    }
    rt_mesh3d_touch_geometry(m);
}

/// @brief Attach the per-vertex bone influence data (4 indices + 4 weights per vertex) to a mesh.
///
/// Required for any skinned draw call. The arrays are referenced
/// (not copied), so the caller must keep them alive for the
/// lifetime of the mesh.
void rt_mesh3d_set_bone_weights(void *obj,
                                int64_t vertex_index,
                                int64_t b0,
                                double w0,
                                int64_t b1,
                                double w1,
                                int64_t b2,
                                double w2,
                                int64_t b3,
                                double w3) {
    rt_mesh3d *m = (rt_mesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_MESH3D_CLASS_ID);
    if (!m)
        return;
    rt_mesh3d_repair_geometry_counts(m);
    if (vertex_index < 0 || vertex_index >= m->vertex_count)
        return;

    // Bone indices are stored as uint8_t. A raw int64 -> uint8 cast silently
    // wraps values outside [0, 255] to bogus valid-looking indices, then the
    // skinning palette applies the wrong transform. Preserve valid authored
    // indices even when their weight is zero so importers can round-trip JOINTS
    // attributes, but only positive finite weights contribute to deformation.
    int64_t idx[4] = {b0, b1, b2, b3};
    double wt[4] = {w0, w1, w2, w3};
    vgfx3d_vertex_t *v = &m->vertices[vertex_index];
    double sum = 0.0;
    int64_t max_bone_index = -1;
    int64_t old_max_bone_index = -1;
    for (int i = 0; i < 4; i++) {
        if (v->bone_weights[i] > 0.0f && v->bone_indices[i] > old_max_bone_index)
            old_max_bone_index = v->bone_indices[i];
    }
    for (int i = 0; i < 4; i++) {
        if (idx[i] < 0 || idx[i] >= VGFX3D_MAX_BONES) {
            v->bone_indices[i] = 0;
            v->bone_weights[i] = 0.0f;
        } else {
            v->bone_indices[i] = (uint8_t)idx[i];
            if (isfinite(wt[i]) && wt[i] > 0.0) {
                v->bone_weights[i] = (float)wt[i];
                sum += wt[i];
                if (idx[i] > max_bone_index)
                    max_bone_index = idx[i];
            } else {
                v->bone_weights[i] = 0.0f;
            }
        }
    }
    if (sum > 1e-12) {
        for (int i = 0; i < 4; i++)
            v->bone_weights[i] = (float)((double)v->bone_weights[i] / sum);
    } else {
        for (int i = 0; i < 4; i++)
            v->bone_weights[i] = 0.0f;
    }
    if (max_bone_index >= 0 && max_bone_index + 1 > m->bone_count)
        m->bone_count = (int32_t)(max_bone_index + 1);
    else if (!m->skeleton_ref && old_max_bone_index >= 0 &&
             old_max_bone_index + 1 >= m->bone_count &&
             (max_bone_index < 0 || max_bone_index + 1 < m->bone_count))
        m->bone_count = mesh3d_recompute_weight_bone_count(m);
    rt_mesh3d_touch_geometry(m);
}

/*==========================================================================
 * Canvas3D extension — DrawMeshSkinned
 *=========================================================================*/

/// @brief Queue a skinned mesh draw with per-frame motion-vector continuity.
/// @details Chooses between GPU and CPU skinning at the start:
///   `vgfx3d_backend_prefers_gpu_skinning` asks the backend whether it wants
///   to consume the palette directly (for D3D11/OpenGL with enough uniform
///   space); if yes, we hand the palettes through to the normal draw path
///   via a shallow-copy `rt_mesh3d` so the backend's vertex shader does the
///   matrix blend. Otherwise we `vgfx3d_skin_vertices` on the CPU into a
///   scratch buffer, tracked as a temp allocation so it survives until the
///   frame is submitted and flushed. `motion_key` threads through for
///   temporal effects (TAA, motion blur) so the engine can correlate this
///   draw with the matching one in the previous frame. `prev_bone_palette`
///   supplies the previous-frame palette for per-vertex motion vectors —
///   without it, skinned motion blur falls back to object-space smearing.
void rt_canvas3d_draw_mesh_matrix_skinned_keyed_bounds(void *canvas,
                                                       void *mesh_obj,
                                                       const double *model_matrix,
                                                       void *material,
                                                       const void *motion_key,
                                                       const float *bone_palette,
                                                       const float *prev_bone_palette,
                                                       int32_t bone_count,
                                                       const float *local_bounds_min,
                                                       const float *local_bounds_max,
                                                       int8_t conservative_bounds,
                                                       int8_t disable_occlusion) {
    rt_mesh3d *mesh;
    rt_canvas3d *c;

    if (!canvas || !mesh_obj || !model_matrix || !material || !bone_palette || bone_count <= 0)
        return;
    if (bone_count > VGFX3D_MAX_BONES)
        bone_count = VGFX3D_MAX_BONES;
    mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
#if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (!mesh && mesh_obj && !rt_heap_is_payload(mesh_obj))
        mesh = (rt_mesh3d *)mesh_obj;
#endif
    if (!mesh)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    if (mesh->vertex_count == 0)
        return;

    c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    if (rt_heap_is_payload(mesh_obj) && !rt_canvas3d_add_temp_object(canvas, mesh_obj))
        return;
    if (c && c->backend && vgfx3d_backend_prefers_gpu_skinning(c->backend->name, bone_count)) {
        rt_mesh3d tmp = *mesh;
        tmp.bone_palette = bone_palette;
        tmp.prev_bone_palette = prev_bone_palette;
        tmp.bone_count = bone_count;
        rt_canvas3d_draw_mesh_matrix_keyed_bounds(canvas,
                                                  &tmp,
                                                  model_matrix,
                                                  material,
                                                  motion_key,
                                                  prev_bone_palette,
                                                  NULL,
                                                  local_bounds_min,
                                                  local_bounds_max,
                                                  conservative_bounds,
                                                  disable_occlusion);
        return;
    }

    if ((size_t)mesh->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t))
        return;
    vgfx3d_vertex_t *skinned =
        (vgfx3d_vertex_t *)malloc((size_t)mesh->vertex_count * sizeof(vgfx3d_vertex_t));
    if (!skinned)
        return;

    vgfx3d_skin_vertices(mesh->vertices, skinned, mesh->vertex_count, bone_palette, bone_count);

    rt_mesh3d tmp = *mesh;
    tmp.vertices = skinned;
    tmp.positions64 = NULL;
    tmp.bone_palette = NULL;
    tmp.prev_bone_palette = NULL;
    tmp.bone_count = 0;
    if (!rt_canvas3d_add_temp_buffer(canvas, skinned)) {
        free(skinned);
        return;
    }
    rt_canvas3d_draw_mesh_matrix_keyed_bounds(canvas,
                                              &tmp,
                                              model_matrix,
                                              material,
                                              motion_key,
                                              NULL,
                                              NULL,
                                              local_bounds_min,
                                              local_bounds_max,
                                              conservative_bounds,
                                              disable_occlusion);
}

/// @brief Draw a skinned mesh using an explicit model matrix and keyed bone palette.
void rt_canvas3d_draw_mesh_matrix_skinned_keyed(void *canvas,
                                                void *mesh_obj,
                                                const double *model_matrix,
                                                void *material,
                                                const void *motion_key,
                                                const float *bone_palette,
                                                const float *prev_bone_palette,
                                                int32_t bone_count) {
    rt_canvas3d_draw_mesh_matrix_skinned_keyed_bounds(canvas,
                                                      mesh_obj,
                                                      model_matrix,
                                                      material,
                                                      motion_key,
                                                      bone_palette,
                                                      prev_bone_palette,
                                                      bone_count,
                                                      NULL,
                                                      NULL,
                                                      0,
                                                      0);
}

/// @brief Draw a skinned mesh — applies an animator's bone palette before rasterising.
/// @details Accepts either a raw `AnimPlayer3D` or an `AnimController3D`. For a
///   controller, the state-machine's computed final/previous bone palettes and
///   skeleton drive the skinning, so idle/walk states and crossfades render directly.
void rt_canvas3d_draw_mesh_skinned(
    void *canvas, void *mesh, void *transform, void *material, void *anim_player) {
    rt_mesh3d *m;
    rt_anim_player3d *p;
    rt_skeleton3d *skel;
    const float *prev_palette;
    mat4_impl *transform_mat;
    if (!canvas || !mesh || !transform || !material || !anim_player)
        return;
    if (!rt_canvas3d_checked_or_stack(canvas))
        return;
    transform_mat = skeleton3d_mat4_checked(transform);
    if (!transform_mat)
        return;
    m = (rt_mesh3d *)rt_g3d_checked_or_null(mesh, RT_G3D_MESH3D_CLASS_ID);
    if (!m)
        return;
    rt_mesh3d_repair_geometry_counts(m);
    if (m->vertex_count == 0)
        return;

    if (rt_g3d_has_class(anim_player, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        int32_t ctrl_bone_count = 0;
        const float *ctrl_palette =
            rt_anim_controller3d_get_final_palette_data(anim_player, &ctrl_bone_count);
        const float *ctrl_prev =
            rt_anim_controller3d_get_previous_palette_data(anim_player, NULL);
        void *ctrl_skel = rt_anim_controller3d_get_skeleton(anim_player);
        if (!ctrl_palette || ctrl_bone_count <= 0 || !ctrl_skel)
            return;
        if (!rt_canvas3d_add_temp_object(canvas, anim_player))
            return;
        rt_canvas3d_draw_mesh_matrix_skinned_keyed(
            canvas, mesh, transform_mat->m, material, transform, ctrl_palette, ctrl_prev,
            skeleton3d_safe_bone_count((rt_skeleton3d *)ctrl_skel));
        return;
    }

    p = (rt_anim_player3d *)rt_g3d_checked_or_null(anim_player, RT_G3D_ANIMPLAYER3D_CLASS_ID);
    if (!p)
        return;
    skel = animation3d_skeleton_slot((void **)&p->skeleton);
    if (!skel || !p->bone_palette)
        return;
    if (!rt_canvas3d_add_temp_object(canvas, anim_player))
        return;
    prev_palette = anim_player_prepare_prev_palette(p, rt_canvas3d_get_frame_serial(canvas));
    rt_canvas3d_draw_mesh_matrix_skinned_keyed(canvas,
                                               mesh,
                                               transform_mat->m,
                                               material,
                                               transform,
                                               p->bone_palette,
                                               prev_palette,
                                               skeleton3d_safe_bone_count(skel));
}

/*==========================================================================
 * AnimBlend3D — multi-state animation blending
 *=========================================================================*/

/// @brief GC finalizer for AnimBlend3D — releases per-state animation refs and the bone palette.
static void anim_blend3d_finalizer(void *obj) {
    rt_anim_blend3d *b = (rt_anim_blend3d *)obj;
    for (int32_t i = 0, count = anim_blend3d_state_slot_limit(b); i < count; i++) {
        animation3d_release_class_ref(
            (void **)&b->states[i].animation, RT_G3D_ANIMATION3D_CLASS_ID);
    }
    animation3d_release_class_ref((void **)&b->skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    free(b->bone_palette);
    free(b->prev_bone_palette);
    free(b->motion_palette_snapshot);
    free(b->local_transforms);
    free(b->temp_state_local);
    free(b->globals_buf);
    b->bone_palette = b->prev_bone_palette = b->motion_palette_snapshot = NULL;
    b->local_transforms = b->temp_state_local = b->globals_buf = NULL;
    b->state_count = 0;
}

/// @brief Create a multi-clip blender that mixes several animations on the same skeleton.
///
/// Useful for blendspaces (e.g. mixing walk + run by speed) and
/// additive layering. Each registered "state" carries its own
/// time, speed, and weight.
void *rt_anim_blend3d_new(void *skel_obj) {
    rt_skeleton3d *skel =
        (rt_skeleton3d *)rt_g3d_checked_or_null(skel_obj, RT_G3D_SKELETON3D_CLASS_ID);
    if (!skel)
        return NULL;
    rt_anim_blend3d *b = (rt_anim_blend3d *)rt_obj_new_i64(RT_G3D_ANIMBLEND3D_CLASS_ID,
                                                           (int64_t)sizeof(rt_anim_blend3d));
    if (!b) {
        rt_trap("AnimBlend3D.New: allocation failed");
        return NULL;
    }
    b->vptr = NULL;
    b->skeleton = skel;
    rt_obj_retain_maybe(skel);
    b->state_count = 0;
    memset(b->states, 0, sizeof(b->states));
    b->last_motion_frame = 0;
    b->has_prev_motion_palette = 0;

    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    size_t buf_sz = (size_t)VGFX3D_MAX_BONES * 16 * sizeof(float);
    b->bone_palette = (float *)calloc(1, buf_sz);
    b->prev_bone_palette = (float *)calloc(1, buf_sz);
    b->motion_palette_snapshot = (float *)calloc(1, buf_sz);
    b->local_transforms = (float *)calloc(1, buf_sz);
    b->temp_state_local = (float *)calloc(1, buf_sz);
    b->globals_buf = (float *)calloc(1, buf_sz);
    if (!b->bone_palette || !b->prev_bone_palette || !b->motion_palette_snapshot ||
        !b->local_transforms || !b->temp_state_local || !b->globals_buf) {
        anim_blend3d_finalizer(b);
        if (rt_obj_release_check0(b))
            rt_obj_free(b);
        rt_trap("AnimBlend3D.New: allocation failed");
        return NULL;
    }

    skeleton3d_freeze(skel);
    if (buf_sz > 0) {
        for (int32_t i = 0; i < bone_count; i++)
            memcpy(
                &b->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));
        compute_palette_from_locals(skel, b->local_transforms, b->globals_buf, b->bone_palette);
        memcpy(b->prev_bone_palette, b->bone_palette, buf_sz);
        memcpy(b->motion_palette_snapshot, b->bone_palette, buf_sz);
    }

    rt_obj_set_finalizer(b, anim_blend3d_finalizer);
    return b;
}

/// @brief Register a new blend state by name and animation; returns its index.
int64_t rt_anim_blend3d_add_state(void *obj, rt_string name, void *anim_obj) {
    rt_anim_blend3d *b =
        (rt_anim_blend3d *)rt_g3d_checked_or_null(obj, RT_G3D_ANIMBLEND3D_CLASS_ID);
    rt_animation3d *anim =
        (rt_animation3d *)rt_g3d_checked_or_null(anim_obj, RT_G3D_ANIMATION3D_CLASS_ID);
    rt_skeleton3d *skel;
    if (!b || !anim)
        return -1;
    skel = animation3d_skeleton_slot((void **)&b->skeleton);
    if (!animation3d_channels_fit_skeleton(anim, skel))
        return -1;
    b->state_count = animblend3d_safe_state_count(b);
    if (b->state_count >= RT_ANIM_BLEND3D_MAX_STATES)
        return -1;

    anim_blend_state_t *st = &b->states[b->state_count];
    memset(st, 0, sizeof(anim_blend_state_t));
    if (name) {
        const char *cstr = rt_string_cstr(name);
        if (cstr) {
            size_t len = strlen(cstr);
            if (len > 63)
                len = 63;
            memcpy(st->name, cstr, len);
            st->name[len] = '\0';
        }
    }
    st->animation = anim;
    rt_obj_retain_maybe(anim);
    st->weight = 0.0f;
    st->anim_time = 0.0f;
    st->speed = 1.0f;
    st->looping = anim->looping ? 1 : 0;
    return b->state_count++;
}

/// @brief Set state `state`'s blend weight; the blender renormalises across all states each frame.
void rt_anim_blend3d_set_weight(void *obj, int64_t state, double weight) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b)
        return;
    if (state < 0 || state >= animblend3d_safe_state_count(b))
        return;
    if (!isfinite(weight))
        weight = 0.0;
    if (weight < 0.0)
        weight = 0.0;
    if (weight > 1.0)
        weight = 1.0;
    b->states[state].weight = (float)weight;
}

/// @brief Convenience: look the state up by name and call `set_weight`.
void rt_anim_blend3d_set_weight_by_name(void *obj, rt_string name, double weight) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    char target_buf[64];
    if (!b || !name)
        return;
    const char *target = rt_string_cstr(name);
    if (!target)
        return;
    size_t len = strlen(target);
    if (len >= sizeof(target_buf))
        len = sizeof(target_buf) - 1u;
    memcpy(target_buf, target, len);
    target_buf[len] = '\0';
    if (target_buf[0] == '\0')
        return;
    for (int32_t i = 0, count = animblend3d_safe_state_count(b); i < count; i++) {
        if (strcmp(b->states[i].name, target_buf) == 0) {
            if (!isfinite(weight))
                weight = 0.0;
            if (weight < 0.0)
                weight = 0.0;
            if (weight > 1.0)
                weight = 1.0;
            b->states[i].weight = (float)weight;
            return;
        }
    }
}

/// @brief Read a state's current blend weight (0.0 for out-of-range or NULL).
double rt_anim_blend3d_get_weight(void *obj, int64_t state) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b)
        return 0.0;
    if (state < 0 || state >= animblend3d_safe_state_count(b))
        return 0.0;
    if (!isfinite(b->states[state].weight) || b->states[state].weight <= 0.0f)
        return 0.0;
    if (b->states[state].weight >= 1.0f)
        return 1.0;
    return (double)b->states[state].weight;
}

/// @brief Set the per-state time-scale (independent of the others — each clip can run at its own
/// rate).
void rt_anim_blend3d_set_speed(void *obj, int64_t state, double speed) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b)
        return;
    if (state < 0 || state >= animblend3d_safe_state_count(b))
        return;
    if (!isfinite(speed))
        speed = 1.0;
    b->states[state].speed = skeleton3d_clamp_to_float(speed, 1.0f);
}

/// @brief Advance every contributing state by `dt` seconds and recompute the blended bone palette.
///
/// For each bone, samples per-state TRS at that state's current
/// time, weights them by the normalised state weights, and lerps
/// (positions / scales) and slerps (rotations) to produce the
/// final pose. Output palette is then ready for skinning.
void rt_anim_blend3d_update(void *obj, double dt) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    if (!b || dt < 0 || !isfinite(dt))
        return;
    rt_skeleton3d *skel = animation3d_skeleton_slot((void **)&b->skeleton);
    int32_t bc = skeleton3d_safe_bone_count(skel);
    int32_t state_count = anim_blend3d_state_slot_limit(b);
    if (!skel || bc == 0)
        return;
    if (!b->local_transforms || !b->temp_state_local || !b->globals_buf || !b->bone_palette)
        return;

    /* Advance all state timers */
    for (int32_t s = 0; s < state_count; s++) {
        anim_blend_state_t *st = &b->states[s];
        rt_animation3d *anim = animation3d_clip_slot((void **)&st->animation);
        if (!anim)
            continue;
        if (!isfinite(st->speed))
            st->speed = 1.0f;
        st->anim_time += skeleton3d_clamp_to_float(dt * (double)st->speed, 0.0f);
        if (!isfinite(st->anim_time))
            st->anim_time = 0.0f;
        if (st->looping && anim->duration > 0)
            st->anim_time = animation3d_wrap_time(st->anim_time, anim->duration);
        else {
            if (st->anim_time > anim->duration)
                st->anim_time = anim->duration;
            else if (st->anim_time < 0.0f)
                st->anim_time = 0.0f;
        }
    }

    /* Start with bind pose */
    for (int32_t i = 0; i < bc; i++)
        memcpy(&b->local_transforms[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

    /* Blend active states */
    float total_weight = 0.0f;
    for (int32_t s = 0; s < state_count; s++) {
        anim_blend_state_t *st = &b->states[s];
        rt_animation3d *anim = animation3d_clip_slot((void **)&st->animation);
        int32_t channel_count;
        if (!anim || !isfinite(st->weight) || st->weight < 1e-6f)
            continue;

        /* Sample this state's channels into temp */
        for (int32_t i = 0; i < bc; i++)
            memcpy(
                &b->temp_state_local[i * 16], skel->bones[i].bind_pose_local, 16 * sizeof(float));

        channel_count = animation3d_safe_channel_count(anim);
        for (int32_t c = 0; c < channel_count; c++) {
            int32_t bone = anim->channels[c].bone_index;
            if (bone < 0 || bone >= bc)
                continue;
            sample_channel_with_fallback(&anim->channels[c],
                                         st->anim_time,
                                         skel->bones[bone].bind_pose_local,
                                         &b->temp_state_local[bone * 16]);
        }

        /* Weighted TRS blend into local_transforms. */
        float w = st->weight;
        total_weight += w;
        float blend_t = skeleton3d_clamp01f((total_weight > 1e-6f) ? w / total_weight : 1.0f);

        for (int32_t bone = 0; bone < bc; bone++) {
            float from_pos[3], from_rot[4], from_scl[3];
            float to_pos[3], to_rot[4], to_scl[3];
            float blend_pos[3], blend_rot[4], blend_scl[3];
            mat4f_decompose_trs(&b->local_transforms[bone * 16], from_pos, from_rot, from_scl);
            mat4f_decompose_trs(&b->temp_state_local[bone * 16], to_pos, to_rot, to_scl);
            for (int32_t i = 0; i < 3; i++) {
                blend_pos[i] = from_pos[i] + (to_pos[i] - from_pos[i]) * blend_t;
                blend_scl[i] = from_scl[i] + (to_scl[i] - from_scl[i]) * blend_t;
            }
            quat_slerp_float(from_rot, to_rot, blend_t, blend_rot);
            animation3d_sanitize_trs(blend_pos, blend_rot, blend_scl, from_pos, from_rot, from_scl);
            build_trs_float(blend_pos, blend_rot, blend_scl, &b->local_transforms[bone * 16]);
        }
    }

    compute_palette_from_locals(skel, b->local_transforms, b->globals_buf, b->bone_palette);
}

/// @brief Number of registered states (0 for NULL).
int64_t rt_anim_blend3d_state_count(void *obj) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    return b ? animblend3d_safe_state_count(b) : 0;
}

/// @brief Borrow the skeleton handle this blender was constructed with.
void *rt_anim_blend3d_get_skeleton(void *obj) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    return b ? animation3d_skeleton_slot((void **)&b->skeleton) : NULL;
}

/// @brief Borrow the current blended local transform buffer for controller integration.
const float *rt_anim_blend3d_get_local_transform_data(void *obj, int32_t *bone_count) {
    rt_anim_blend3d *b = anim_blend3d_checked(obj);
    rt_skeleton3d *skel;
    if (bone_count)
        *bone_count = 0;
    if (!b || !b->local_transforms)
        return NULL;
    skel = animation3d_skeleton_slot((void **)&b->skeleton);
    if (!skel)
        return NULL;
    if (bone_count)
        *bone_count = skeleton3d_safe_bone_count(skel);
    return b->local_transforms;
}

/// @brief Same role as `anim_player_prepare_prev_palette` but for the blender — see that function.
static const float *anim_blend_prepare_prev_palette(rt_anim_blend3d *b, int64_t frame_serial) {
    rt_skeleton3d *skel;
    if (!b || !b->bone_palette)
        return NULL;
    skel = animation3d_skeleton_slot((void **)&b->skeleton);
    int32_t bone_count = skeleton3d_safe_bone_count(skel);
    size_t palette_size = (size_t)bone_count * 16 * sizeof(float);
    if (b->last_motion_frame != frame_serial) {
        if (b->prev_bone_palette && b->motion_palette_snapshot && bone_count > 0 &&
            b->last_motion_frame != 0) {
            memcpy(b->prev_bone_palette, b->motion_palette_snapshot, palette_size);
            b->has_prev_motion_palette = 1;
        } else if (!b->prev_bone_palette) {
            b->has_prev_motion_palette = 0;
        }
        if (b->motion_palette_snapshot && bone_count > 0)
            memcpy(b->motion_palette_snapshot, b->bone_palette, palette_size);
        b->last_motion_frame = frame_serial;
    }
    return b->has_prev_motion_palette ? b->prev_bone_palette : NULL;
}

/// @brief Draw a skinned mesh using a blended animation pose.
/// @details Thin convenience layer that pulls the final bone palette and the
///   matching previous-frame palette out of the `rt_anim_blend3d` wrapper
///   (built by the animation controller from one or more playing clips) and
///   forwards to `rt_canvas3d_draw_mesh_matrix_skinned_keyed`. The transform
///   object supplies both the model matrix (via its `mat4_impl`) and a
///   stable identity pointer used as the motion key for temporal effects.
void rt_canvas3d_draw_mesh_blended(
    void *canvas, void *mesh_obj, void *transform, void *material, void *blend_obj) {
    if (!canvas || !mesh_obj || !transform || !material || !blend_obj)
        return;
    if (!rt_canvas3d_checked_or_stack(canvas))
        return;
    mat4_impl *transform_mat = skeleton3d_mat4_checked(transform);
    if (!transform_mat)
        return;
    rt_anim_blend3d *b =
        (rt_anim_blend3d *)rt_g3d_checked_or_null(blend_obj, RT_G3D_ANIMBLEND3D_CLASS_ID);
    if (!b)
        b = (rt_anim_blend3d *)rt_blend_tree3d_get_blend(blend_obj);
    if (!b)
        return;
    rt_skeleton3d *skel = animation3d_skeleton_slot((void **)&b->skeleton);
    if (!skel || skeleton3d_safe_bone_count(skel) == 0)
        return;

    rt_mesh3d *mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
    if (!mesh)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    if (mesh->vertex_count == 0)
        return;

    const float *prev_palette =
        anim_blend_prepare_prev_palette(b, rt_canvas3d_get_frame_serial(canvas));
    if (!rt_canvas3d_add_temp_object(canvas, blend_obj))
        return;
    rt_canvas3d_draw_mesh_matrix_skinned_keyed(canvas,
                                               mesh,
                                               transform_mat->m,
                                               material,
                                               transform,
                                               b->bone_palette,
                                               prev_palette,
                                               skeleton3d_safe_bone_count(skel));
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
