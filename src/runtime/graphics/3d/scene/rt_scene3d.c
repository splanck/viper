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
//   - Dirty flag propagates DOWN: changing a parent marks all descendants dirty.
//   - Children array is heap-allocated (not GC-managed); freed in finalizer.
//   - Mesh/material/name and LOD meshes are retained by the node.
//   - Iterative traversal stacks (mark_dirty etc.) avoid recursion stack overflow.
//   - LOD levels are kept sorted by distance ascending so the draw path picks
//     the first matching threshold linearly.
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
#include "rt_scene3d_internal.h"
#include "rt_audio3d.h"
#include "rt_animcontroller3d.h"
#include "rt_box.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_json.h"
#include "rt_map.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_object.h"
#include "rt_pixels_internal.h"
#include "rt_physics3d.h"
#include "rt_quat.h"
#include "rt_seq.h"
#include "rt_skeleton3d_internal.h"
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

#define NODE_INIT_CHILDREN 4
#define SCENE3D_ABS_MAX 1000000000000.0
#define SCENE3D_FLOAT_ABS_MAX 3.40282346638528859812e38

/// @brief Validate @p obj as a Scene3D handle and return its typed pointer (NULL on mismatch).
static rt_scene3d *scene3d_checked(void *obj) {
    return (rt_scene3d *)rt_g3d_checked_or_null(obj, RT_G3D_SCENE3D_CLASS_ID);
}

/// @brief Validate @p obj as a SceneNode3D handle and return its typed pointer (NULL on mismatch).
static rt_scene_node3d *scene_node3d_checked(void *obj) {
    return (rt_scene_node3d *)rt_g3d_checked_or_null(obj, RT_G3D_SCENENODE3D_CLASS_ID);
}

/// @brief Drop the GC reference in `*slot` and null the pointer (refcount-aware free).
static void scene3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
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

/// @brief Clamp `value` into `[-SCENE3D_ABS_MAX, SCENE3D_ABS_MAX]`, substituting `fallback` when not finite.
static double scene3d_clamp_abs_or(double value, double fallback) {
    value = scene3d_finite_or(value, fallback);
    if (value > SCENE3D_ABS_MAX)
        return SCENE3D_ABS_MAX;
    if (value < -SCENE3D_ABS_MAX)
        return -SCENE3D_ABS_MAX;
    return value;
}

/// @brief Narrow a double to float, returning 0.0f when non-finite or outside ±SCENE3D_FLOAT_ABS_MAX.
static float scene3d_float_or_zero(double value) {
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
static double scene3d_scale_or_unit(double value) {
    if (!isfinite(value))
        return 1.0;
    if (value > SCENE3D_ABS_MAX)
        return SCENE3D_ABS_MAX;
    if (value < -SCENE3D_ABS_MAX)
        return -SCENE3D_ABS_MAX;
    return value;
}

extern void *rt_anim_controller3d_consume_root_motion_rotation(void *obj);

/*==========================================================================
 * Imported node animation clips
 *=========================================================================*/

/// @brief GC finalizer for a NodeAnimation3D. Releases the clip name reference and
///        frees every channel's target name plus its times / values / in-tangent /
///        out-tangent buffers. Only CUBICSPLINE channels have live tangent buffers;
///        LINEAR / STEP leave those pointers NULL and `free(NULL)` is a no-op.
static void rt_node_animation3d_finalize(void *obj) {
    rt_node_animation3d *anim = (rt_node_animation3d *)obj;
    if (!anim)
        return;
    scene3d_release_ref((void **)&anim->name);
    for (int32_t i = 0; i < anim->channel_count; i++) {
        scene3d_release_ref((void **)&anim->channels[i].target_name);
        free(anim->channels[i].times);
        free(anim->channels[i].values);
        free(anim->channels[i].in_tangents);
        free(anim->channels[i].out_tangents);
    }
    free(anim->channels);
    anim->channels = NULL;
    anim->channel_count = 0;
    anim->channel_capacity = 0;
}

/// @brief Allocate a NodeAnimation3D clip — the container for per-node TRS curves
///        imported from glTF (non-skeletal node animations).
/// @details Retains `name` so the caller can drop their reference safely. Clamps a
///          non-finite or non-positive `duration` to 1.0 so a malformed glTF asset
///          can't hand us a zero-length clip that would divide-by-zero during
///          looping playback. Looping is enabled by default — callers that want a
///          single-shot playback flip `looping` after construction.
/// @return Opaque clip handle, or NULL and traps on allocation failure.
void *rt_node_animation3d_new(rt_string name, double duration) {
    rt_node_animation3d *anim =
        (rt_node_animation3d *)rt_obj_new_i64(RT_G3D_NODEANIMATION3D_CLASS_ID, (int64_t)sizeof(rt_node_animation3d));
    if (!anim) {
        rt_trap("NodeAnimation3D.New: allocation failed");
        return NULL;
    }
    memset(anim, 0, sizeof(*anim));
    rt_obj_set_finalizer(anim, rt_node_animation3d_finalize);
    rt_obj_retain_maybe(name);
    anim->name = name;
    anim->duration = (isfinite(duration) && duration > 0.0) ? duration : 1.0;
    anim->looping = 1;
    return anim;
}

/// @brief Grow the channel array on an animation clip to fit at least `needed` entries.
/// @details Doubling-growth starting at 4 channels on first allocation, with a direct
///          jump to `needed` when the doubled value would still be too small (e.g. a
///          bulk import calling with an exact count). New tail is zero-initialized so
///          uninitialized channel memory can't leak into the finalizer's free path.
///          Leaves the existing array untouched on allocation failure so callers can
///          continue using whatever was already there.
/// @return 1 on success (including no-op), 0 on allocation failure.
static int node_animation_reserve_channels(rt_node_animation3d *anim, int32_t needed) {
    int32_t new_capacity;
    rt_node_anim_channel3d *grown;
    if (!anim || needed < 0)
        return 0;
    if (anim->channel_capacity >= needed)
        return 1;
    if (anim->channel_capacity < 0)
        return 0;
    if (anim->channel_capacity > INT32_MAX / 2)
        new_capacity = needed;
    else
        new_capacity = anim->channel_capacity > 0 ? anim->channel_capacity * 2 : 4;
    if (new_capacity < needed)
        new_capacity = needed;
    if ((size_t)new_capacity > SIZE_MAX / sizeof(*anim->channels))
        return 0;
    grown = (rt_node_anim_channel3d *)realloc(
        anim->channels, (size_t)new_capacity * sizeof(*anim->channels));
    if (!grown)
        return 0;
    memset(grown + anim->channel_capacity,
           0,
           (size_t)(new_capacity - anim->channel_capacity) * sizeof(*grown));
    anim->channels = grown;
    anim->channel_capacity = new_capacity;
    return 1;
}

/// @brief Validate raw channel sample data before it is copied into a clip.
/// @details Enforces the invariants that `node_animation_add_channel_impl` depends on:
///   - `value_width` must match the path's dimensionality: 3 for TRANSLATION/SCALE,
///     4 for ROTATION, at least 1 for WEIGHTS/other (morph weight count varies).
///   - Every time sample must be finite and strictly increasing; glTF allows equal
///     consecutive times only in STEP interpolation, but we reject them here to prevent
///     division by zero in the linear and cubic interpolation paths.
///   - key_count × value_width overflow is checked before iterating so the loop bound
///     is always within size_t range.
///   - All value samples (and, for CUBICSPLINE, both tangent sets) must be finite;
///     non-finite values would produce NaN transforms that corrupt the scene graph.
/// @return 1 if all data passes validation, 0 on any violation.
static int node_animation_validate_channel_data(int64_t path,
                                                int64_t key_count,
                                                int64_t value_width,
                                                const double *times,
                                                const float *values,
                                                const float *in_tangents,
                                                const float *out_tangents,
                                                int cubic) {
    int64_t min_width = 1;
    if (path == RT_NODE_ANIM_PATH_TRANSLATION || path == RT_NODE_ANIM_PATH_SCALE)
        min_width = 3;
    else if (path == RT_NODE_ANIM_PATH_ROTATION)
        min_width = 4;
    if (value_width < min_width)
        return 0;
    for (int64_t i = 0; i < key_count; i++) {
        if (!isfinite(times[i]))
            return 0;
        if (i > 0 && times[i] <= times[i - 1])
            return 0;
    }
    if ((uint64_t)key_count > SIZE_MAX / (uint64_t)value_width)
        return 0;
    size_t value_count = (size_t)key_count * (size_t)value_width;
    for (size_t i = 0; i < value_count; i++) {
        if (!isfinite(values[i]))
            return 0;
        if (cubic && (!isfinite(in_tangents[i]) || !isfinite(out_tangents[i])))
            return 0;
    }
    return 1;
}

/// @brief Add one channel (one animated property on one target node) to an animation
///        clip, taking defensive copies of the sample data.
/// @details Validation the caller doesn't have to repeat:
///          - Rejects a NULL target name, non-positive key count, non-positive value
///            width, or NULL times / values buffer.
///          - CUBICSPLINE channels require both in-tangent and out-tangent buffers.
///          - Path must be a valid TRS-or-weights selector.
///          - `key_count * value_width` is bounds-checked against `SIZE_MAX / sizeof(float)`
///            so a pathological exporter that claims billions of keys can't wrap the
///            multiplication into a small allocation.
///          The implementation deep-copies the times array (as `double`), the values
///          array, and — for CUBICSPLINE channels — the two tangent arrays, so the
///          caller can free the source buffers immediately after this returns.
/// @return The zero-based index of the new channel, or -1 on validation / allocation
///         failure.
static int64_t node_animation_add_channel_impl(void *obj,
                                               rt_string target_name,
                                               int64_t path,
                                               int64_t interpolation,
                                               int64_t key_count,
                                               int64_t value_width,
                                               const double *times,
                                               const float *values,
                                               const float *in_tangents,
                                               const float *out_tangents) {
    rt_node_animation3d *anim =
        (rt_node_animation3d *)rt_g3d_checked_or_null(obj, RT_G3D_NODEANIMATION3D_CLASS_ID);
    rt_node_anim_channel3d *channel;
    size_t time_bytes;
    size_t value_count;
    if (!anim || !target_name || key_count <= 0 || value_width <= 0 || !times || !values)
        return -1;
    if (interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE && (!in_tangents || !out_tangents))
        return -1;
    if (path < RT_NODE_ANIM_PATH_TRANSLATION || path > RT_NODE_ANIM_PATH_WEIGHTS)
        return -1;
    if (key_count > INT32_MAX || value_width > INT32_MAX)
        return -1;
    if (!node_animation_validate_channel_data(path,
                                              key_count,
                                              value_width,
                                              times,
                                              values,
                                              in_tangents,
                                              out_tangents,
                                              interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE))
        return -1;
    value_count = (size_t)key_count * (size_t)value_width;
    if (value_count > SIZE_MAX / sizeof(float))
        return -1;
    if (anim->channel_count == INT32_MAX)
        return -1;
    if (!node_animation_reserve_channels(anim, anim->channel_count + 1))
        return -1;
    channel = &anim->channels[anim->channel_count];
    time_bytes = (size_t)key_count * sizeof(double);
    channel->times = (double *)malloc(time_bytes);
    channel->values = (float *)malloc(value_count * sizeof(float));
    if (interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE) {
        channel->in_tangents = (float *)malloc(value_count * sizeof(float));
        channel->out_tangents = (float *)malloc(value_count * sizeof(float));
    }
    if (!channel->times || !channel->values ||
        (interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE &&
         (!channel->in_tangents || !channel->out_tangents))) {
        free(channel->times);
        free(channel->values);
        free(channel->in_tangents);
        free(channel->out_tangents);
        memset(channel, 0, sizeof(*channel));
        return -1;
    }
    memcpy(channel->times, times, time_bytes);
    memcpy(channel->values, values, value_count * sizeof(float));
    if (interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE) {
        memcpy(channel->in_tangents, in_tangents, value_count * sizeof(float));
        memcpy(channel->out_tangents, out_tangents, value_count * sizeof(float));
    }
    rt_obj_retain_maybe(target_name);
    channel->target_name = target_name;
    channel->path = (int32_t)path;
    if (interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE)
        channel->interpolation = RT_NODE_ANIM_INTERP_CUBICSPLINE;
    else
        channel->interpolation =
            interpolation == RT_NODE_ANIM_INTERP_STEP ? RT_NODE_ANIM_INTERP_STEP
                                                      : RT_NODE_ANIM_INTERP_LINEAR;
    channel->key_count = (int32_t)key_count;
    channel->value_width = (int32_t)value_width;
    return anim->channel_count++;
}

/// @brief Add a STEP or LINEAR channel to an animation clip (no tangent data required).
/// @details Thin wrapper around `node_animation_add_channel_impl` that passes NULL for
///   both tangent arrays. Only CUBICSPLINE interpolation needs tangents; for STEP and
///   LINEAR the tangent pointers are never read so passing NULL is safe and explicit.
///   Callers that want CUBICSPLINE must use `rt_node_animation3d_add_cubic_channel`.
/// @param obj           NodeAnimation3D clip handle.
/// @param target_name   Name of the scene node that this channel drives.
/// @param path          RT_NODE_ANIM_PATH_* constant (TRANSLATION/ROTATION/SCALE/WEIGHTS).
/// @param interpolation RT_NODE_ANIM_INTERP_STEP or RT_NODE_ANIM_INTERP_LINEAR.
/// @param key_count     Number of keyframes.
/// @param value_width   Floats per keyframe (3 for vec3, 4 for quat, N for weights).
/// @param times         Monotonically increasing time samples (seconds), length key_count.
/// @param values        Flattened sample values, length key_count * value_width.
/// @return Zero-based channel index on success, -1 on validation or allocation failure.
int64_t rt_node_animation3d_add_channel(void *obj,
                                        rt_string target_name,
                                        int64_t path,
                                        int64_t interpolation,
                                        int64_t key_count,
                                        int64_t value_width,
                                        const double *times,
                                        const float *values) {
    return node_animation_add_channel_impl(obj,
                                           target_name,
                                           path,
                                           interpolation,
                                           key_count,
                                           value_width,
                                           times,
                                           values,
                                           NULL,
                                           NULL);
}

/// @brief Add a CUBICSPLINE (Catmull-Rom / glTF cubic) channel to an animation clip.
/// @details Thin wrapper that hard-wires RT_NODE_ANIM_INTERP_CUBICSPLINE and passes
///   both tangent arrays through to `node_animation_add_channel_impl`. The tangent
///   arrays must each have the same length as `values` (key_count * value_width floats).
///   Validation inside the impl will reject NULL tangent pointers for cubic channels.
///   This separation from `rt_node_animation3d_add_channel` keeps the non-cubic hot
///   path free of tangent-buffer bookkeeping.
/// @param obj           NodeAnimation3D clip handle.
/// @param target_name   Name of the scene node driven by this channel.
/// @param path          RT_NODE_ANIM_PATH_* constant.
/// @param key_count     Number of keyframes.
/// @param value_width   Floats per keyframe.
/// @param times         Monotonically increasing time samples (seconds).
/// @param values        Flattened output sample values.
/// @param in_tangents   In-tangent per sample (same layout as @p values).
/// @param out_tangents  Out-tangent per sample (same layout as @p values).
/// @return Zero-based channel index on success, -1 on validation or allocation failure.
int64_t rt_node_animation3d_add_cubic_channel(void *obj,
                                              rt_string target_name,
                                              int64_t path,
                                              int64_t key_count,
                                              int64_t value_width,
                                              const double *times,
                                              const float *values,
                                              const float *in_tangents,
                                              const float *out_tangents) {
    return node_animation_add_channel_impl(obj,
                                           target_name,
                                           path,
                                           RT_NODE_ANIM_INTERP_CUBICSPLINE,
                                           key_count,
                                           value_width,
                                           times,
                                           values,
                                           in_tangents,
                                           out_tangents);
}

/// @brief GC finalizer for a NodeAnimator3D — releases retained clip references
///        and frees the animations pointer array.
/// @details Each clip in `animations[]` was retained during construction via
///   `rt_obj_retain_maybe`; the matching release here ensures clips are not freed
///   while the animator is still alive, and are released precisely when the animator
///   itself is collected. The root node pointer is NOT released — the animator borrows
///   that reference from the scene, so releasing it here would cause a double-free.
static void rt_node_animator3d_finalize(void *obj) {
    rt_node_animator3d *animator = (rt_node_animator3d *)obj;
    if (!animator)
        return;
    for (int32_t i = 0; i < animator->animation_count; i++)
        scene3d_release_ref((void **)&animator->animations[i]);
    free(animator->animations);
    animator->animations = NULL;
    animator->animation_count = 0;
    animator->root = NULL;
}

/// @brief Allocate a NodeAnimator3D that owns a set of pre-loaded animation clips.
/// @details Allocates the animator object and a contiguous pointer array sized exactly
///   for @p clip_count entries. Each clip is retained so the animator keeps the clips
///   alive independent of the caller's own references. Defaults to playing back clip 0
///   at speed 1.0 with `playing = 1` — call rt_node_animator3d_set_speed /
///   rt_node_animator3d_play_clip after construction to override. The `root` field is
///   left NULL until the animator is bound to a scene node via
///   `rt_scene_node3d_bind_node_animator`.
/// @param clips      Array of NodeAnimation3D clip handles, must not be NULL.
/// @param clip_count Number of clips; must be in [1, INT32_MAX].
/// @return New animator handle, or NULL (with trap) on allocation failure.
void *rt_node_animator3d_new_from_clips(void **clips, int64_t clip_count) {
    rt_node_animator3d *animator;
    if (!clips || clip_count <= 0 || clip_count > INT32_MAX)
        return NULL;
    for (int32_t i = 0; i < (int32_t)clip_count; i++) {
        if (!rt_g3d_has_class(clips[i], RT_G3D_NODEANIMATION3D_CLASS_ID))
            return NULL;
    }
    animator = (rt_node_animator3d *)rt_obj_new_i64(RT_G3D_NODEANIMATOR3D_CLASS_ID, (int64_t)sizeof(rt_node_animator3d));
    if (!animator) {
        rt_trap("NodeAnimator3D.New: allocation failed");
        return NULL;
    }
    memset(animator, 0, sizeof(*animator));
    rt_obj_set_finalizer(animator, rt_node_animator3d_finalize);
    animator->animations = (rt_node_animation3d **)calloc((size_t)clip_count, sizeof(void *));
    if (!animator->animations) {
        scene3d_release_ref((void **)&animator);
        return NULL;
    }
    for (int32_t i = 0; i < (int32_t)clip_count; i++) {
        rt_obj_retain_maybe(clips[i]);
        animator->animations[i] = (rt_node_animation3d *)clips[i];
    }
    animator->animation_count = (int32_t)clip_count;
    animator->current_animation = 0;
    animator->speed = 1.0;
    animator->playing = 1;
    return animator;
}

/*==========================================================================
 * Helpers
 *=========================================================================*/

/// @brief Build a TRS matrix: Translate * Rotate * Scale (row-major).
/// Quaternion (x,y,z,w) is expanded inline to avoid allocating a Mat4.
static void build_trs_matrix(const double *pos,
                             const double *quat,
                             const double *scl,
                             double *out) {
    double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    double x2 = x + x, y2 = y + y, z2 = z + z;
    double xx = x * x2, xy = x * y2, xz = x * z2;
    double yy = y * y2, yz = y * z2, zz = z * z2;
    double wx = w * x2, wy = w * y2, wz = w * z2;

    double sx = scl[0], sy = scl[1], sz = scl[2];

    /* R * S (rotation columns scaled) */
    out[0] = (1.0 - (yy + zz)) * sx;
    out[1] = (xy - wz) * sy;
    out[2] = (xz + wy) * sz;
    out[3] = pos[0];

    out[4] = (xy + wz) * sx;
    out[5] = (1.0 - (xx + zz)) * sy;
    out[6] = (yz - wx) * sz;
    out[7] = pos[1];

    out[8] = (xz - wy) * sx;
    out[9] = (yz + wx) * sy;
    out[10] = (1.0 - (xx + yy)) * sz;
    out[11] = pos[2];

    out[12] = 0.0;
    out[13] = 0.0;
    out[14] = 0.0;
    out[15] = 1.0;
}

/// @brief Multiply two 4x4 row-major matrices: out = a * b.
static void mat4d_mul(const double *a, const double *b, double *out) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
}

/// @brief Write an identity 4x4 row-major matrix into @p out.
static void mat4d_identity(double *out) {
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
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!isfinite(det) || fabs(det) < 1e-12)
        return -1;

    inv_det = 1.0 / det;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] * inv_det;
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
static void quat_normalize_local(double *q) {
    double len_sq;
    double inv_len;
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        quat_identity(q);
        return;
    }
    len_sq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!isfinite(len_sq) || len_sq < 1e-20) {
        quat_identity(q);
        return;
    }
    inv_len = 1.0 / sqrt(len_sq);
    q[0] *= inv_len;
    q[1] *= inv_len;
    q[2] *= inv_len;
    q[3] *= inv_len;
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
        double s = sqrt(trace + 1.0) * 2.0;
        out[3] = 0.25 * s;
        out[0] = (m21 - m12) / s;
        out[1] = (m02 - m20) / s;
        out[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = sqrt(1.0 + m00 - m11 - m22) * 2.0;
        out[3] = (m21 - m12) / s;
        out[0] = 0.25 * s;
        out[1] = (m01 + m10) / s;
        out[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = sqrt(1.0 + m11 - m00 - m22) * 2.0;
        out[3] = (m02 - m20) / s;
        out[0] = (m01 + m10) / s;
        out[1] = 0.25 * s;
        out[2] = (m12 + m21) / s;
    } else {
        double s = sqrt(1.0 + m22 - m00 - m11) * 2.0;
        out[3] = (m10 - m01) / s;
        out[0] = (m02 + m20) / s;
        out[1] = (m12 + m21) / s;
        out[2] = 0.25 * s;
    }
    quat_normalize_local(out);
}

/// @brief Strip translation/scale and call `quat_from_matrix_rows` to recover the rotation quaternion.
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
    if (!isfinite(rlen) || !isfinite(ulen) || !isfinite(flen) || rlen < 1e-12 ||
        ulen < 1e-12 || flen < 1e-12)
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
    det = rx * (uy * fz - uz * fy) - ux * (ry * fz - rz * fy) +
          fx * (ry * uz - rz * uy);
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
    quat_from_matrix_rows(basis[0], basis[1], basis[2], basis[3], basis[4], basis[5], basis[6], basis[7], basis[8], out);
}

/// @brief Decompose a row-major TRS matrix into translation, rotation, and scale.
static void decompose_trs_matrix(const double *m, double *pos, double *quat, double *scale) {
    double rx;
    double ry;
    double rz;
    double ux;
    double uy;
    double uz;
    double fx;
    double fy;
    double fz;
    double sx;
    double sy;
    double sz;
    double det;
    if (!m)
        return;
    if (pos) {
        pos[0] = m[3];
        pos[1] = m[7];
        pos[2] = m[11];
    }
    rx = m[0];
    ry = m[4];
    rz = m[8];
    ux = m[1];
    uy = m[5];
    uz = m[9];
    fx = m[2];
    fy = m[6];
    fz = m[10];
    sx = sqrt(rx * rx + ry * ry + rz * rz);
    sy = sqrt(ux * ux + uy * uy + uz * uz);
    sz = sqrt(fx * fx + fy * fy + fz * fz);
    {
        double sx_norm = sx > 1e-12 ? sx : 1.0;
        double sy_norm = sy > 1e-12 ? sy : 1.0;
        double sz_norm = sz > 1e-12 ? sz : 1.0;
        rx /= sx_norm;
        ry /= sx_norm;
        rz /= sx_norm;
        ux /= sy_norm;
        uy /= sy_norm;
        uz /= sy_norm;
        fx /= sz_norm;
        fy /= sz_norm;
        fz /= sz_norm;
    }

    det = rx * (uy * fz - uz * fy) - ux * (ry * fz - rz * fy) +
          fx * (ry * uz - rz * uy);
    if (det < 0.0) {
        if (sx >= sy && sx >= sz) {
            sx = -sx;
            rx = -rx;
            ry = -ry;
            rz = -rz;
        } else if (sy >= sz) {
            sy = -sy;
            ux = -ux;
            uy = -uy;
            uz = -uz;
        } else {
            sz = -sz;
            fx = -fx;
            fy = -fy;
            fz = -fz;
        }
    }
    if (scale) {
        scale[0] = scene3d_scale_or_unit(sx);
        scale[1] = scene3d_scale_or_unit(sy);
        scale[2] = scene3d_scale_or_unit(sz);
    }
    if (quat)
        quat_from_matrix_rows(rx, ux, fx, ry, uy, fy, rz, uz, fz, quat);
}

/// @brief Push @p node onto an iterative-traversal node stack, growing it geometrically.
/// @details Used by `mark_dirty` and other walk functions that prefer iterative
///   traversal over recursion (avoids stack overflow on deep scene graphs).
///   Returns 1 on success or no-op (NULL node), 0 on overflow / OOM.
static int scene_node_stack_push(rt_scene_node3d ***stack,
                                 size_t *count,
                                 size_t *capacity,
                                 rt_scene_node3d *node) {
    rt_scene_node3d **grown;
    size_t new_capacity;
    if (!stack || !count || !capacity || !node)
        return 1;
    if (*count >= *capacity) {
        new_capacity = *capacity > 0 ? *capacity * 2u : 64u;
        if (new_capacity <= *capacity)
            return 0;
        if (new_capacity > SIZE_MAX / sizeof(**stack))
            return 0;
        grown = (rt_scene_node3d **)realloc(*stack, new_capacity * sizeof(**stack));
        if (!grown)
            return 0;
        *stack = grown;
        *capacity = new_capacity;
    }
    (*stack)[(*count)++] = node;
    return 1;
}

/// @brief `scene_node_stack_push` for read-only `const` traversals.
/// @details Same growth contract; the const variant is needed because the C type
///   system disallows aliasing a `const T**` as a `T**` even when only reads happen.
static int scene_node_const_stack_push(const rt_scene_node3d ***stack,
                                       size_t *count,
                                       size_t *capacity,
                                       const rt_scene_node3d *node) {
    const rt_scene_node3d **grown;
    size_t new_capacity;
    if (!stack || !count || !capacity || !node)
        return 1;
    if (*count >= *capacity) {
        new_capacity = *capacity > 0 ? *capacity * 2u : 64u;
        if (new_capacity <= *capacity)
            return 0;
        if (new_capacity > SIZE_MAX / sizeof(**stack))
            return 0;
        grown = (const rt_scene_node3d **)realloc((void *)*stack, new_capacity * sizeof(**stack));
        if (!grown)
            return 0;
        *stack = grown;
        *capacity = new_capacity;
    }
    (*stack)[(*count)++] = node;
    return 1;
}

/// @brief Mark a node and all descendants as dirty without recursive stack growth.
static void mark_dirty(rt_scene_node3d *node) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        node->world_dirty = 1;
        return;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        current->world_dirty = 1;
        for (int32_t i = 0; i < current->child_count; i++) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("SceneNode3D: traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief Forward declaration for the recursive node-name search used by the animation
///        channel applier before the function's full definition appears later in the file.
static rt_scene_node3d *find_by_name(rt_scene_node3d *node, const char *target);

/// @brief Normalize a float[4] quaternion in-place; substitute the identity quaternion
///        if the magnitude is too small to normalize safely.
/// @details A degenerate quaternion (all-zero or near-zero) would produce NaN after
///   division. The identity quaternion (0, 0, 0, 1) is substituted instead so a bad
///   asset frame leaves the node in its rest orientation rather than exploding the
///   scene graph. The 1e-8 threshold is smaller than any numerically meaningful
///   quaternion from a non-degenerate rotation.
/// @param q float[4] quaternion in (x, y, z, w) order; modified in-place.
static void node_anim_normalize_quat(float *q) {
    float len;
    if (!q)
        return;
    if (!isfinite(q[0]) || !isfinite(q[1]) || !isfinite(q[2]) || !isfinite(q[3])) {
        q[0] = 0.0f;
        q[1] = 0.0f;
        q[2] = 0.0f;
        q[3] = 1.0f;
        return;
    }
    len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (len > 1e-8f) {
        q[0] /= len;
        q[1] /= len;
        q[2] /= len;
        q[3] /= len;
    } else {
        q[0] = 0.0f;
        q[1] = 0.0f;
        q[2] = 0.0f;
        q[3] = 1.0f;
    }
}

/// @brief Spherical-linear interpolation between two unit quaternions along the
///        shortest arc on the 4-sphere.
/// @details Both inputs are normalized before use so callers need not pre-normalize.
///   The dot product is computed in double precision and the sign of @p b is flipped
///   when `dot < 0` to guarantee the shortest-path interpolation (without this, a
///   270° spin would be taken instead of a 90° spin for near-antipodal quaternions).
///   When `dot > 0.9995` (quaternions nearly identical) the implementation falls back
///   to normalized LERP to avoid the `acos(1.0)` domain-error that produces NaN at
///   the identity separation angle.
/// @param a     Source quaternion (x, y, z, w), must not be NULL.
/// @param b     Target quaternion (x, y, z, w), must not be NULL.
/// @param alpha Interpolation parameter in [0, 1]; 0 returns @p a, 1 returns @p b.
/// @param out   float[4] output quaternion, must not be NULL.
static void node_anim_slerp_quat(const float *a, const float *b, double alpha, float *out) {
    float q0[4];
    float q1[4];
    double dot;
    if (!a || !b || !out)
        return;
    memcpy(q0, a, sizeof(q0));
    memcpy(q1, b, sizeof(q1));
    node_anim_normalize_quat(q0);
    node_anim_normalize_quat(q1);
    dot = (double)q0[0] * q1[0] + (double)q0[1] * q1[1] + (double)q0[2] * q1[2] +
          (double)q0[3] * q1[3];
    if (dot < 0.0) {
        dot = -dot;
        for (int i = 0; i < 4; i++)
            q1[i] = -q1[i];
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
        for (int i = 0; i < 4; i++)
            out[i] = (float)((double)q0[i] + ((double)q1[i] - (double)q0[i]) * alpha);
        node_anim_normalize_quat(out);
        return;
    }
    {
        double theta0 = acos(dot);
        double theta = theta0 * alpha;
        double sin_theta = sin(theta);
        double sin_theta0 = sin(theta0);
        double s0 = cos(theta) - dot * sin_theta / sin_theta0;
        double s1 = sin_theta / sin_theta0;
        for (int i = 0; i < 4; i++)
            out[i] = (float)(s0 * q0[i] + s1 * q1[i]);
        node_anim_normalize_quat(out);
    }
}

/// @brief Sample an animation channel at the given time and write interpolated values
///        to @p out_values, dispatching over STEP / LINEAR / CUBICSPLINE interpolation.
/// @details The implementation binary-searches for the bracketing keyframe interval
///   [lo, hi], then computes alpha = (time - t0) / (t1 - t0) clamped to [0, 1].
///   Dispatch:
///   - STEP: copies the `lo` keyframe values unchanged.
///   - CUBICSPLINE: evaluates the glTF cubic Hermite spline using the precomputed
///     h00/h10/h01/h11 basis polynomials, scaling tangents by the interval length dt.
///     Rotation channels are re-normalized after cubic evaluation.
///   - LINEAR (default): component-wise lerp, with SLERP for ROTATION channels to
///     maintain unit-quaternion properties across the interval.
///   Time clamping at the clip endpoints avoids out-of-range array access.
/// @param channel    Fully validated channel with at least one keyframe.
/// @param time       Playback time in seconds.
/// @param out_values Caller-allocated buffer of at least channel->value_width floats.
static void node_anim_sample_channel(const rt_node_anim_channel3d *channel,
                                     double time,
                                     float *out_values) {
    int32_t lo;
    int32_t hi;
    double t0;
    double t1;
    double alpha;
    if (!channel || !out_values || channel->key_count <= 0 || channel->value_width <= 0)
        return;
    if (time <= channel->times[0]) {
        memcpy(out_values, channel->values, (size_t)channel->value_width * sizeof(float));
        return;
    }
    if (time >= channel->times[channel->key_count - 1]) {
        memcpy(out_values,
               &channel->values[(size_t)(channel->key_count - 1) * (size_t)channel->value_width],
               (size_t)channel->value_width * sizeof(float));
        return;
    }
    lo = 0;
    hi = channel->key_count - 1;
    while (hi - lo > 1) {
        int32_t mid = lo + (hi - lo) / 2;
        if (channel->times[mid] <= time)
            lo = mid;
        else
            hi = mid;
    }
    t0 = channel->times[lo];
    t1 = channel->times[hi];
    alpha = (t1 > t0 && channel->interpolation != RT_NODE_ANIM_INTERP_STEP) ? (time - t0) / (t1 - t0)
                                                                            : 0.0;
    if (alpha < 0.0)
        alpha = 0.0;
    else if (alpha > 1.0)
        alpha = 1.0;
    if (channel->interpolation == RT_NODE_ANIM_INTERP_STEP) {
        memcpy(out_values,
               &channel->values[(size_t)lo * (size_t)channel->value_width],
               (size_t)channel->value_width * sizeof(float));
        return;
    }
    if (channel->interpolation == RT_NODE_ANIM_INTERP_CUBICSPLINE && channel->in_tangents &&
        channel->out_tangents) {
        double dt = t1 - t0;
        double u2 = alpha * alpha;
        double u3 = u2 * alpha;
        double h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
        double h10 = u3 - 2.0 * u2 + alpha;
        double h01 = -2.0 * u3 + 3.0 * u2;
        double h11 = u3 - u2;
        for (int32_t i = 0; i < channel->value_width; i++) {
            size_t ai = (size_t)lo * (size_t)channel->value_width + (size_t)i;
            size_t bi = (size_t)hi * (size_t)channel->value_width + (size_t)i;
            out_values[i] = (float)(h00 * channel->values[ai] +
                                    h10 * dt * channel->out_tangents[ai] +
                                    h01 * channel->values[bi] +
                                    h11 * dt * channel->in_tangents[bi]);
        }
        if (channel->path == RT_NODE_ANIM_PATH_ROTATION && channel->value_width >= 4)
            node_anim_normalize_quat(out_values);
        return;
    }
    if (channel->path == RT_NODE_ANIM_PATH_ROTATION && channel->value_width >= 4) {
        const float *a = &channel->values[(size_t)lo * (size_t)channel->value_width];
        const float *b = &channel->values[(size_t)hi * (size_t)channel->value_width];
        node_anim_slerp_quat(a, b, alpha, out_values);
        return;
    }
    for (int32_t i = 0; i < channel->value_width; i++) {
        float a = channel->values[(size_t)lo * (size_t)channel->value_width + (size_t)i];
        float b = channel->values[(size_t)hi * (size_t)channel->value_width + (size_t)i];
        out_values[i] = (float)((double)a + ((double)b - (double)a) * alpha);
    }
}

/// @brief Propagate morph-target weights from a WEIGHTS channel through a node subtree.
/// @details glTF WEIGHTS path targets a specific node but the weights are conceptually
///   inherited by all mesh nodes in the subtree that share the same morph target set.
///   This recursive walk sets weights on every mesh that has a morph-targets object,
///   clamping the applied count to `min(shape_count, weight_count)` so a channel with
///   fewer weights than shapes leaves the excess shapes at their current weights rather
///   than clearing them, and a channel with more weights than shapes does not overrun
///   the morph target array.
/// @param node         Root of the subtree to drive.
/// @param weights      Array of weight values from the sampled WEIGHTS channel.
/// @param weight_count Number of values in @p weights.
static void node_anim_apply_weights_recursive(rt_scene_node3d *node,
                                              const float *weights,
                                              int32_t weight_count) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!node || !weights || weight_count <= 0)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("NodeAnimation3D: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        rt_mesh3d *mesh = (rt_mesh3d *)current->mesh;
        if (mesh && mesh->morph_targets_ref) {
            int64_t shape_count = rt_morphtarget3d_get_shape_count(mesh->morph_targets_ref);
            int32_t limit = (int32_t)((shape_count < weight_count) ? shape_count : weight_count);
            for (int32_t i = 0; i < limit; i++)
                rt_morphtarget3d_set_weight(mesh->morph_targets_ref, i, weights[i]);
        }
        for (int32_t i = current->child_count - 1; i >= 0; i--) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("NodeAnimation3D: traversal stack allocation failed");
                free(stack);
                return;
            }
        }
    }
    free(stack);
}

/// @brief Resolve a channel's target node by name, sample the channel, and write the
///        TRS or weight result onto the target.
/// @details Resolution uses `find_by_name` on the animator root subtree at every call,
///   which is an O(n) tree walk. For clips with many channels this is called once per
///   channel per frame; the expectation is that scene graphs are small enough that the
///   linear search dominates only for very large skeletal hierarchies, which in practice
///   use the skeleton system rather than node animation.
///   A stack buffer of 16 floats covers all standard TRS channels (max width 4 for
///   quaternion); only WEIGHTS channels with more than 16 targets spill to a heap
///   allocation. The heap buffer is always freed before returning.
///   SCALE values are sanitised through `scene3d_scale_or_unit` to avoid degenerate
///   inverse matrices when an asset provides a near-zero scale keyframe.
/// @param root    Root of the scene subtree searched for the target node.
/// @param channel Channel to sample; must have a valid target_name.
/// @param time    Playback time in seconds.
static void node_anim_apply_channel(rt_scene_node3d *root,
                                    const rt_node_anim_channel3d *channel,
                                    double time) {
    const char *target_name;
    rt_scene_node3d *target;
    float stack_values[16];
    float *values = stack_values;
    int32_t width;
    if (!root || !channel || !channel->target_name)
        return;
    width = channel->value_width;
    if (width <= 0)
        return;
    if (width > (int32_t)(sizeof(stack_values) / sizeof(stack_values[0]))) {
        values = (float *)calloc((size_t)width, sizeof(float));
        if (!values)
            return;
    } else {
        memset(values, 0, sizeof(stack_values));
    }
    target_name = rt_string_cstr(channel->target_name);
    target = target_name ? find_by_name(root, target_name) : NULL;
    if (!target) {
        if (values != stack_values)
            free(values);
        return;
    }
    node_anim_sample_channel(channel, time, values);
    switch (channel->path) {
        case RT_NODE_ANIM_PATH_TRANSLATION:
            if (width >= 3) {
                target->position[0] = values[0];
                target->position[1] = values[1];
                target->position[2] = values[2];
                mark_dirty(target);
            }
            break;
        case RT_NODE_ANIM_PATH_ROTATION:
            if (width >= 4) {
                target->rotation[0] = values[0];
                target->rotation[1] = values[1];
                target->rotation[2] = values[2];
                target->rotation[3] = values[3];
                quat_normalize_local(target->rotation);
                mark_dirty(target);
            }
            break;
        case RT_NODE_ANIM_PATH_SCALE:
            if (width >= 3) {
                target->scale_xyz[0] = scene3d_scale_or_unit(values[0]);
                target->scale_xyz[1] = scene3d_scale_or_unit(values[1]);
                target->scale_xyz[2] = scene3d_scale_or_unit(values[2]);
                mark_dirty(target);
            }
            break;
        case RT_NODE_ANIM_PATH_WEIGHTS:
            node_anim_apply_weights_recursive(target, values, width);
            break;
    }
    if (values != stack_values)
        free(values);
}

/// @brief Advance an animator's playback time and apply all channels of the current
///        clip to the bound scene subtree.
/// @details Time advance is: `time += dt * speed` clamped to finite values.
///   After advance, looping clips wrap via `fmod` into [0, duration); one-shot clips
///   clamp to `duration` and set `playing = 0` so callers can detect clip end.
///   Out-of-range `current_animation` is reset to 0 defensively (can happen when the
///   clip set is hot-swapped). A NULL `root` node is a fast-out — the animator must
///   have been bound via `rt_scene_node3d_bind_node_animator` before updates have any
///   effect. All channels are applied in index order with no blending; to blend two
///   clips the caller must manage two animators and lerp the results at the scene node.
/// @param animator Animator to advance; silently no-ops on NULL or non-playing state.
/// @param dt       Delta time in seconds since the last update; non-finite values are
///                 ignored so a stalled timer cannot corrupt the playback position.
static void node_animator_update(rt_node_animator3d *animator, double dt) {
    rt_node_animation3d *clip;
    if (!animator || !animator->playing || animator->animation_count <= 0 || !animator->root)
        return;
    if (animator->current_animation < 0 || animator->current_animation >= animator->animation_count)
        animator->current_animation = 0;
    clip = animator->animations[animator->current_animation];
    if (!clip)
        return;
    if (isfinite(dt) && dt > 0.0)
        animator->time += dt * (isfinite(animator->speed) ? animator->speed : 1.0);
    if (clip->duration > 0.0) {
        if (clip->looping) {
            animator->time = fmod(animator->time, clip->duration);
            if (animator->time < 0.0)
                animator->time += clip->duration;
        } else if (animator->time > clip->duration) {
            animator->time = clip->duration;
            animator->playing = 0;
        }
    }
    for (int32_t i = 0; i < clip->channel_count; i++)
        node_anim_apply_channel(animator->root, &clip->channels[i], animator->time);
}

/// @brief Recompute the world matrix if dirty, walking ancestors iteratively.
static void recompute_world_matrix(rt_scene_node3d *node) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    rt_scene_node3d *current = node;
    if (!node || !node->world_dirty)
        return;

    while (current && current->world_dirty) {
        if (!scene_node_stack_push(&stack, &count, &capacity, current)) {
            rt_trap("SceneNode3D: traversal stack allocation failed");
            free(stack);
            return;
        }
        current = current->parent;
    }

    while (count > 0) {
        double local[16];
        current = stack[--count];
        build_trs_matrix(current->position, current->rotation, current->scale_xyz, local);
        if (current->parent)
            mat4d_mul(current->parent->world_matrix, local, current->world_matrix);
        else
            memcpy(current->world_matrix, local, sizeof(double) * 16);
        current->world_dirty = 0;
    }
    free(stack);
}

/// @brief Count nodes in a subtree (including the root).
static int32_t count_subtree(const rt_scene_node3d *node) {
    const rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int32_t total = 0;
    if (!node)
        return 0;
    if (!scene_node_const_stack_push(&stack, &count, &capacity, node))
        return INT32_MAX;
    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (total == INT32_MAX) {
            free(stack);
            return INT32_MAX;
        }
        total++;
        for (int32_t i = 0; i < current->child_count; i++) {
            if (!scene_node_const_stack_push(&stack, &count, &capacity, current->children[i])) {
                free(stack);
                return INT32_MAX;
            }
        }
    }
    free(stack);
    return total;
}

/// @brief Compose `node->world_matrix` from its ancestors and read the translation column.
static void scene_node_get_world_position(rt_scene_node3d *node, double *x, double *y, double *z) {
    if (!x || !y || !z) {
        return;
    }
    *x = 0.0;
    *y = 0.0;
    *z = 0.0;
    if (!node)
        return;
    recompute_world_matrix(node);
    *x = node->world_matrix[3];
    *y = node->world_matrix[7];
    *z = node->world_matrix[11];
}

/// @brief Read this node's world-space rotation as a quaternion (composing parent rotations).
static void scene_node_get_world_rotation(rt_scene_node3d *node, double *out_quat) {
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
    double desired_world[16];
    double local_matrix[16];
    double inv_parent[16];
    if (!node || !world_pos || !world_quat)
        return;
    world_rot[0] = world_quat[0];
    world_rot[1] = world_quat[1];
    world_rot[2] = world_quat[2];
    world_rot[3] = world_quat[3];
    quat_normalize_local(world_rot);

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
        build_trs_matrix(safe_world_pos, world_rot, node->scale_xyz, desired_world);
    }
    if (mat4d_invert(node->parent->world_matrix, inv_parent) == 0) {
        mat4d_mul(inv_parent, desired_world, local_matrix);
    } else {
        memcpy(local_matrix, desired_world, sizeof(local_matrix));
    }
    decompose_trs_matrix(local_matrix, node->position, node->rotation, node->scale_xyz);
    node->position[0] = scene3d_clamp_abs_or(node->position[0], 0.0);
    node->position[1] = scene3d_clamp_abs_or(node->position[1], 0.0);
    node->position[2] = scene3d_clamp_abs_or(node->position[2], 0.0);
    node->scale_xyz[0] = scene3d_scale_or_unit(node->scale_xyz[0]);
    node->scale_xyz[1] = scene3d_scale_or_unit(node->scale_xyz[1]);
    node->scale_xyz[2] = scene3d_scale_or_unit(node->scale_xyz[2]);

    mark_dirty(node);
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
    if (!node || !node->bound_animator)
        return;
    delta = rt_anim_controller3d_consume_root_motion(node->bound_animator);
    delta_rot = rt_anim_controller3d_consume_root_motion_rotation(node->bound_animator);
    if (!delta) {
        scene3d_release_ref(&delta_rot);
        return;
    }
    node->position[0] = scene3d_clamp_abs_or(
        scene3d_clamp_abs_or(node->position[0], 0.0) +
            scene3d_clamp_abs_or(rt_vec3_x(delta), 0.0),
        0.0);
    node->position[1] = scene3d_clamp_abs_or(
        scene3d_clamp_abs_or(node->position[1], 0.0) +
            scene3d_clamp_abs_or(rt_vec3_y(delta), 0.0),
        0.0);
    node->position[2] = scene3d_clamp_abs_or(
        scene3d_clamp_abs_or(node->position[2], 0.0) +
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
            quat_normalize_local(node->rotation);
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

        if (current->bound_node_animator)
            node_animator_update((rt_node_animator3d *)current->bound_node_animator, dt);

        mode = current->sync_mode;
        body_is_kinematic =
            current->bound_body ? rt_body3d_is_kinematic(current->bound_body) : 0;
        pull_from_body =
            current->bound_body &&
            (mode == RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY ||
             (mode == RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC && !body_is_kinematic));
        push_to_body =
            current->bound_body &&
            (mode == RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE ||
             (mode == RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC && body_is_kinematic));

        if (pull_from_body) {
            double world_pos[3];
            double world_quat[4];
            void *pos = rt_body3d_get_position(current->bound_body);
            void *quat = rt_body3d_get_orientation(current->bound_body);
            world_pos[0] = pos ? rt_vec3_x(pos) : 0.0;
            world_pos[1] = pos ? rt_vec3_y(pos) : 0.0;
            world_pos[2] = pos ? rt_vec3_z(pos) : 0.0;
            world_quat[0] = quat ? rt_quat_x(quat) : 0.0;
            world_quat[1] = quat ? rt_quat_y(quat) : 0.0;
            world_quat[2] = quat ? rt_quat_z(quat) : 0.0;
            world_quat[3] = quat ? rt_quat_w(quat) : 1.0;
            scene_node_set_world_transform(current, world_pos, world_quat);
            scene3d_release_ref(&pos);
            scene3d_release_ref(&quat);
        } else if (current->bound_animator &&
                   (mode == RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION ||
                    push_to_body)) {
            scene_node_apply_root_motion(current);
        }

        if (push_to_body) {
            double world_pos[3];
            double world_quat[4];
            scene_node_get_world_position(
                current, &world_pos[0], &world_pos[1], &world_pos[2]);
            scene_node_get_world_rotation(current, world_quat);
            rt_body3d_set_position(
                current->bound_body, world_pos[0], world_pos[1], world_pos[2]);
            {
                void *quat =
                    rt_quat_new(world_quat[0], world_quat[1], world_quat[2], world_quat[3]);
                rt_body3d_set_orientation(current->bound_body, quat);
                scene3d_release_ref(&quat);
            }
        }

        for (int32_t i = current->child_count - 1; i >= 0; i--) {
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
static int node_contains(const rt_scene_node3d *root, const rt_scene_node3d *target) {
    const rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    if (!root)
        return 0;
    if (!scene_node_const_stack_push(&stack, &count, &capacity, root))
        return root == target;
    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (current == target) {
            free(stack);
            return 1;
        }
        for (int32_t i = 0; i < current->child_count; i++) {
            if (!scene_node_const_stack_push(&stack, &count, &capacity, current->children[i])) {
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
static void scene_mesh_bounds(rt_mesh3d *mesh,
                              float out_min[3],
                              float out_max[3],
                              float *out_radius) {
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
    if (!world_matrix || !local || !out)
        return;
    out[0] = scene3d_float_or_zero(world_matrix[0] * (double)local[0] +
                                   world_matrix[1] * (double)local[1] +
                                   world_matrix[2] * (double)local[2] + world_matrix[3]);
    out[1] = scene3d_float_or_zero(world_matrix[4] * (double)local[0] +
                                   world_matrix[5] * (double)local[1] +
                                   world_matrix[6] * (double)local[2] + world_matrix[7]);
    out[2] = scene3d_float_or_zero(world_matrix[8] * (double)local[0] +
                                   world_matrix[9] * (double)local[1] +
                                   world_matrix[10] * (double)local[2] + world_matrix[11]);
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
static void scene_normalize_f32_vec3(float v[3], float fallback_x, float fallback_y, float fallback_z) {
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
static void scene_transform_node_light(rt_scene_node3d *node, const rt_light3d *src, rt_light3d *dst) {
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
            scene_transform_node_light(current, (const rt_light3d *)current->light, &storage[index]);
            out_lights[index] = &storage[index];
            *io_count = index + 1;
            if (*io_count >= VGFX3D_MAX_LIGHTS)
                break;
        }
        for (int32_t i = current->child_count - 1; i >= 0; i--) {
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
static void scene_bounds_reset(float out_min[3], float out_max[3]) {
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

/// @brief Expand @p bounds_min/@p bounds_max to include @p point.
static void scene_bounds_include_point(float bounds_min[3], float bounds_max[3], const float point[3]) {
    if (!bounds_min || !bounds_max || !point)
        return;
    for (int i = 0; i < 3; i++) {
        if (point[i] < bounds_min[i])
            bounds_min[i] = point[i];
        if (point[i] > bounds_max[i])
            bounds_max[i] = point[i];
    }
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
    scene_bounds_stack_item_t *grown;
    size_t new_capacity;
    if (!stack || !count || !capacity || !node || !node_to_root)
        return 1;
    if (*count >= *capacity) {
        new_capacity = *capacity > 0 ? *capacity * 2u : 64u;
        if (new_capacity <= *capacity || new_capacity > SIZE_MAX / sizeof(**stack))
            return 0;
        grown = (scene_bounds_stack_item_t *)realloc(*stack, new_capacity * sizeof(**stack));
        if (!grown)
            return 0;
        *stack = grown;
        *capacity = new_capacity;
    }
    (*stack)[*count].node = node;
    memcpy((*stack)[*count].node_to_root, node_to_root, sizeof(double) * 16);
    (*count)++;
    return 1;
}

/// @brief Compute a node subtree's local-space AABB relative to the queried root node.
///
/// @param node Current subtree node.
/// @param node_to_root Row-major matrix mapping @p node local space into the queried root's local space.
/// @param out_min Running subtree minimum.
/// @param out_max Running subtree maximum.
/// @return 1 if the subtree contributed any mesh bounds, otherwise 0.
static int scene_node_collect_subtree_bounds(rt_scene_node3d *node,
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
        if (item.node->mesh) {
            float mesh_min[3];
            float mesh_max[3];
            scene_mesh_bounds((rt_mesh3d *)item.node->mesh, mesh_min, mesh_max, NULL);
            scene_bounds_include_aabb(out_min, out_max, mesh_min, mesh_max, item.node_to_root);
            has_bounds = 1;
        }
        for (int32_t i = item.node->child_count - 1; i >= 0; i--) {
            rt_scene_node3d *child = item.node->children[i];
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
static rt_scene_node3d *find_by_name(rt_scene_node3d *node, const char *target) {
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
            const char *s = rt_string_cstr(current->name);
            if (s && strcmp(s, target) == 0) {
                free(stack);
                return current;
            }
        }
        for (int32_t i = current->child_count - 1; i >= 0; i--) {
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

typedef struct {
    rt_scene_node3d *node;
    void *inherited_animator;
} scene_draw_stack_item_t;

/// @brief Push (node, inherited animator) onto a growable draw-traversal stack.
/// @details Same iterative-traversal rationale and growth/return contract as
///          scene_bounds_stack_push: capacity doubles from 64; returns 1 on
///          success or no-op, 0 on overflow/realloc failure.
static int scene_draw_stack_push(scene_draw_stack_item_t **stack,
                                 size_t *count,
                                 size_t *capacity,
                                 rt_scene_node3d *node,
                                 void *inherited_animator) {
    scene_draw_stack_item_t *grown;
    size_t new_capacity;
    if (!stack || !count || !capacity || !node)
        return 1;
    if (*count >= *capacity) {
        new_capacity = *capacity > 0 ? *capacity * 2u : 64u;
        if (new_capacity <= *capacity || new_capacity > SIZE_MAX / sizeof(**stack))
            return 0;
        grown = (scene_draw_stack_item_t *)realloc(*stack, new_capacity * sizeof(**stack));
        if (!grown)
            return 0;
        *stack = grown;
        *capacity = new_capacity;
    }
    (*stack)[*count].node = node;
    (*stack)[*count].inherited_animator = inherited_animator;
    (*count)++;
    return 1;
}

/// @brief Draw traversal: depth-first, skip invisible nodes, frustum-cull meshes.
/// Children are ALWAYS traversed even if the parent mesh is culled, because
/// child transforms may place them inside the frustum independently.
static void draw_node(rt_scene_node3d *node,
                      void *canvas3d,
                      const vgfx3d_frustum_t *frustum,
                      int32_t *culled,
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
        int draw_self = 1;
        void *draw_mesh;
        float draw_min[3] = {0.0f, 0.0f, 0.0f};
        float draw_max[3] = {0.0f, 0.0f, 0.0f};
        float draw_radius = 0.0f;

        if (!current->visible)
            continue;

        recompute_world_matrix(current);
        effective_animator =
            current->bound_animator ? current->bound_animator : item.inherited_animator;
        draw_mesh = current->mesh;

        if (draw_mesh) {
            if (current->lod_count > 0 && cam_pos) {
                float local_center[3];
                float world_center[3];
                scene_mesh_bounds((rt_mesh3d *)current->mesh, draw_min, draw_max, &draw_radius);
                local_center[0] = 0.5f * (draw_min[0] + draw_max[0]);
                local_center[1] = 0.5f * (draw_min[1] + draw_max[1]);
                local_center[2] = 0.5f * (draw_min[2] + draw_max[2]);
                scene_world_point(current->world_matrix, local_center, world_center);
                {
                    float dx = world_center[0] - cam_pos[0];
                    float dy = world_center[1] - cam_pos[1];
                    float dz = world_center[2] - cam_pos[2];
                    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                    if (!isfinite(dist))
                        dist = 0.0f;
                    for (int32_t l = current->lod_count - 1; l >= 0; l--) {
                        if (dist >= (float)current->lod_levels[l].distance) {
                            draw_mesh = current->lod_levels[l].mesh;
                            break;
                        }
                    }
                }
            }
            scene_mesh_bounds((rt_mesh3d *)draw_mesh, draw_min, draw_max, &draw_radius);
        }

        if (frustum && draw_mesh && draw_radius > 0.0f) {
            rt_mesh3d *draw_mesh_impl = (rt_mesh3d *)draw_mesh;
            int has_dynamic_deformation =
                (effective_animator != NULL || draw_mesh_impl->morph_targets_ref != NULL ||
                 draw_mesh_impl->morph_deltas != NULL || draw_mesh_impl->morph_weights != NULL ||
                 draw_mesh_impl->morph_shape_count > 0);
            if (!has_dynamic_deformation) {
                float cull_min[3] = {draw_min[0], draw_min[1], draw_min[2]};
                float cull_max[3] = {draw_max[0], draw_max[1], draw_max[2]};
                float world_min[3], world_max[3];
                vgfx3d_transform_aabb(cull_min, cull_max, current->world_matrix, world_min, world_max);
                if (vgfx3d_frustum_test_aabb(frustum, world_min, world_max) == 0) {
                    draw_self = 0;
                    if (culled)
                        (*culled)++;
                }
            }
        }

        if (draw_self && draw_mesh && current->material) {
            const float *anim_palette = NULL;
            const float *anim_prev_palette = NULL;
            int32_t anim_bone_count = 0;
            int32_t mesh_bone_count = ((rt_mesh3d *)draw_mesh)->bone_count;

            if (effective_animator) {
                anim_palette =
                    rt_anim_controller3d_get_final_palette_data(effective_animator, &anim_bone_count);
                anim_prev_palette =
                    rt_anim_controller3d_get_previous_palette_data(effective_animator,
                                                                   &anim_bone_count);
            }
            if (anim_palette && anim_bone_count > 0 && mesh_bone_count > 0) {
                int32_t draw_bone_count =
                    anim_bone_count < mesh_bone_count ? anim_bone_count : mesh_bone_count;
                if (rt_canvas3d_add_temp_object(canvas3d, effective_animator)) {
                    rt_canvas3d_draw_mesh_matrix_skinned_keyed(canvas3d,
                                                               draw_mesh,
                                                               current->world_matrix,
                                                               current->material,
                                                               current,
                                                               anim_palette,
                                                               anim_prev_palette,
                                                               draw_bone_count);
                }
            } else {
                rt_canvas3d_draw_mesh_matrix_keyed(canvas3d,
                                                   draw_mesh,
                                                   current->world_matrix,
                                                   current->material,
                                                   current,
                                                   NULL,
                                                   NULL);
            }
        }

        for (int32_t i = current->child_count - 1; i >= 0; i--) {
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

/*==========================================================================
 * SceneNode3D — lifecycle
 *=========================================================================*/

/// @brief GC finalizer for a SceneNode — release mesh/material/animator/body refs and the children array.
static void rt_scene_node3d_finalize(void *obj) {
    rt_scene_node3d *node = (rt_scene_node3d *)obj;
    if (!node)
        return;

    for (int32_t i = 0; i < node->child_count; i++) {
        if (node->children[i])
            node->children[i]->parent = NULL;
        scene3d_release_ref((void **)&node->children[i]);
    }
    free(node->children);
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;
    for (int32_t i = 0; i < node->lod_count; i++)
        scene3d_release_ref(&node->lod_levels[i].mesh);
    free(node->lod_levels);
    node->lod_levels = NULL;
    node->lod_count = 0;
    node->lod_capacity = 0;
    scene3d_release_ref(&node->mesh);
    scene3d_release_ref(&node->material);
    scene3d_release_ref(&node->light);
    scene3d_release_ref(&node->bound_body);
    scene3d_release_ref(&node->bound_animator);
    if (node->bound_node_animator)
        ((rt_node_animator3d *)node->bound_node_animator)->root = NULL;
    scene3d_release_ref(&node->bound_node_animator);
    scene3d_release_ref((void **)&node->name);
}

// ===========================================================================
// SceneNode public API
//
// A SceneNode is one transformable element in the scene graph: it
// carries a TRS (position / rotation / scale), an optional mesh +
// material to draw, an optional rigid body to drive physics from,
// an optional animator, and a list of child nodes. Each accessor
// is null-safe; setters skip if `obj` is NULL, getters return zero
// / identity / NULL.
// ===========================================================================

/// @brief Create an empty SceneNode at the origin (identity rotation, scale 1).
void *rt_scene_node3d_new(void) {
    rt_scene_node3d *node = (rt_scene_node3d *)rt_obj_new_i64(RT_G3D_SCENENODE3D_CLASS_ID, (int64_t)sizeof(rt_scene_node3d));
    if (!node) {
        rt_trap("SceneNode3D.New: memory allocation failed");
        return NULL;
    }
    node->vptr = NULL;
    node->position[0] = node->position[1] = node->position[2] = 0.0;
    node->rotation[0] = node->rotation[1] = node->rotation[2] = 0.0;
    node->rotation[3] = 1.0; /* identity quaternion (0,0,0,1) */
    node->scale_xyz[0] = node->scale_xyz[1] = node->scale_xyz[2] = 1.0;

    /* Identity world matrix */
    memset(node->world_matrix, 0, sizeof(double) * 16);
    node->world_matrix[0] = node->world_matrix[5] = 1.0;
    node->world_matrix[10] = node->world_matrix[15] = 1.0;
    node->world_dirty = 1;

    node->parent = NULL;
    node->children = NULL;
    node->child_count = 0;
    node->child_capacity = 0;

    node->mesh = NULL;
    node->material = NULL;
    node->light = NULL;
    node->bound_body = NULL;
    node->bound_animator = NULL;
    node->bound_node_animator = NULL;
    node->sync_mode = RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
    node->visible = 1;
    node->name = NULL;

    memset(node->aabb_min, 0, sizeof(float) * 3);
    memset(node->aabb_max, 0, sizeof(float) * 3);
    node->bsphere_radius = 0.0f;

    node->lod_levels = NULL;
    node->lod_count = 0;
    node->lod_capacity = 0;

    rt_obj_set_finalizer(node, rt_scene_node3d_finalize);
    return node;
}

/*==========================================================================
 * SceneNode3D — transform
 *=========================================================================*/

/// @brief Set the local-space position component of the node's TRS.
void rt_scene_node3d_set_position(void *obj, double x, double y, double z) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return;
    n->position[0] = scene3d_clamp_abs_or(x, 0.0);
    n->position[1] = scene3d_clamp_abs_or(y, 0.0);
    n->position[2] = scene3d_clamp_abs_or(z, 0.0);
    mark_dirty(n);
}

/// @brief Read the local position as a Vec3 (origin if `obj` is NULL).
void *rt_scene_node3d_get_position(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(0, 0, 0);
    return rt_vec3_new(n->position[0], n->position[1], n->position[2]);
}

/// @brief Replace the local rotation with the given Quat (re-normalised on store).
void rt_scene_node3d_set_rotation(void *obj, void *quat) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n || !rt_g3d_is_quat(quat))
        return;
    n->rotation[0] = rt_quat_x(quat);
    n->rotation[1] = rt_quat_y(quat);
    n->rotation[2] = rt_quat_z(quat);
    n->rotation[3] = rt_quat_w(quat);
    quat_normalize_local(n->rotation);
    mark_dirty(n);
}

/// @brief Read the local rotation as a Quat (identity if `obj` is NULL).
void *rt_scene_node3d_get_rotation(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_quat_new(0, 0, 0, 1);
    return rt_quat_new(n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]);
}

/// @brief Set the per-axis scale (uniform or non-uniform).
void rt_scene_node3d_set_scale(void *obj, double x, double y, double z) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return;
    n->scale_xyz[0] = scene3d_scale_or_unit(x);
    n->scale_xyz[1] = scene3d_scale_or_unit(y);
    n->scale_xyz[2] = scene3d_scale_or_unit(z);
    mark_dirty(n);
}

/// @brief Read the local scale as a Vec3 (1,1,1 if `obj` is NULL).
void *rt_scene_node3d_get_scale(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(1, 1, 1);
    return rt_vec3_new(n->scale_xyz[0], n->scale_xyz[1], n->scale_xyz[2]);
}

/// @brief Compose this node's local TRS with all ancestors and return the world matrix as a Mat4.
void *rt_scene_node3d_get_world_matrix(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return NULL;
    recompute_world_matrix(n);
    const double *m = n->world_matrix;
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

/// @brief Read the world-space translation as a Vec3.
void *rt_scene_node3d_get_world_position(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    scene_node_get_world_position(n, &x, &y, &z);
    return rt_vec3_new(x, y, z);
}

/// @brief Read world-space scale magnitudes from the composed matrix basis vectors.
void *rt_scene_node3d_get_world_scale(void *obj) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    const double *m;
    double sx;
    double sy;
    double sz;
    if (!n)
        return rt_vec3_new(1.0, 1.0, 1.0);
    recompute_world_matrix(n);
    m = n->world_matrix;
    sx = sqrt(m[0] * m[0] + m[4] * m[4] + m[8] * m[8]);
    sy = sqrt(m[1] * m[1] + m[5] * m[5] + m[9] * m[9]);
    sz = sqrt(m[2] * m[2] + m[6] * m[6] + m[10] * m[10]);
    return rt_vec3_new(sx, sy, sz);
}

/*==========================================================================
 * SceneNode3D — hierarchy
 *=========================================================================*/

/// @brief Reparent `child` under `obj`. Detaches from previous parent first; rejects cycles.
void rt_scene_node3d_add_child(void *obj, void *child_obj) {
    rt_scene_node3d *parent = scene_node3d_checked(obj);
    rt_scene_node3d *child = scene_node3d_checked(child_obj);
    if (!parent || !child || parent == child)
        return;
    if (child->parent == parent)
        return;

    /* Reject cycle formation: parent may not already be inside child's subtree. */
    if (node_contains(child, parent))
        return;

    /* Grow children array if needed */
    if (parent->child_count >= parent->child_capacity) {
        int32_t new_cap;
        if (parent->child_capacity < 0 || parent->child_capacity > INT32_MAX / 2) {
            rt_trap("SceneNode3D.AddChild: too many children");
            return;
        }
        new_cap = parent->child_capacity == 0 ? NODE_INIT_CHILDREN : parent->child_capacity * 2;
        if ((size_t)new_cap > SIZE_MAX / sizeof(rt_scene_node3d *)) {
            rt_trap("SceneNode3D.AddChild: too many children");
            return;
        }
        rt_scene_node3d **nc = (rt_scene_node3d **)realloc(
            parent->children, (size_t)new_cap * sizeof(rt_scene_node3d *));
        if (!nc) {
            rt_trap("SceneNode3D.AddChild: allocation failed");
            return;
        }
        parent->children = nc;
        parent->child_capacity = new_cap;
    }

    rt_obj_retain_maybe(child);
    /* Detach from previous parent if any. The temporary retain above becomes
       the new parent's ownership after the old parent releases its reference. */
    if (child->parent)
        rt_scene_node3d_remove_child(child->parent, child);

    parent->children[parent->child_count++] = child;
    child->parent = parent;
    mark_dirty(child);
}

/// @brief Detach `child` from `obj`. Decrements the GC refcount. No-op if not actually a child.
void rt_scene_node3d_remove_child(void *obj, void *child_obj) {
    rt_scene_node3d *parent = scene_node3d_checked(obj);
    rt_scene_node3d *child = scene_node3d_checked(child_obj);
    if (!parent || !child)
        return;

    for (int32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            /* Shift remaining children down */
            for (int32_t j = i; j < parent->child_count - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->child_count--;
            parent->children[parent->child_count] = NULL;
            child->parent = NULL;
            mark_dirty(child);
            if (rt_obj_release_check0(child))
                rt_obj_free(child);
            return;
        }
    }
}

/// @brief Number of immediate (non-recursive) children attached to this node.
int64_t rt_scene_node3d_child_count(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->child_count : 0;
}

/// @brief Return the `index`-th child handle (NULL on out-of-range or NULL `obj`).
void *rt_scene_node3d_get_child(void *obj, int64_t index) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return NULL;
    if (index < 0 || index >= n->child_count)
        return NULL;
    return n->children[index];
}

/// @brief Parent node handle (NULL for root or detached nodes).
void *rt_scene_node3d_get_parent(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->parent : NULL;
}

/// @brief Recursive depth-first search of the subtree for a node with the given name.
void *rt_scene_node3d_find(void *obj, rt_string name) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node || !name)
        return NULL;
    const char *s = rt_string_cstr(name);
    if (!s)
        return NULL;
    return find_by_name(node, s);
}

/*==========================================================================
 * SceneNode3D — renderable / visibility / name
 *=========================================================================*/

/// @brief Bind a mesh to this node (replaces previous; null clears).
/// The mesh is referenced (not copied) so multiple nodes can share it.
void rt_scene_node3d_set_mesh(void *obj, void *mesh) {
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return;
    if (mesh && !rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    if (n->mesh == mesh) {
        if (mesh)
            scene_mesh_bounds((rt_mesh3d *)mesh, n->aabb_min, n->aabb_max, &n->bsphere_radius);
        return;
    }
    rt_obj_retain_maybe(mesh);
    scene3d_release_ref(&n->mesh);
    n->mesh = mesh;

    /* Compute object-space AABB from mesh vertices */
    if (mesh) {
        scene_mesh_bounds((rt_mesh3d *)mesh, n->aabb_min, n->aabb_max, &n->bsphere_radius);
    } else {
        n->aabb_min[0] = n->aabb_min[1] = n->aabb_min[2] = 0.0f;
        n->aabb_max[0] = n->aabb_max[1] = n->aabb_max[2] = 0.0f;
        n->bsphere_radius = 0.0f;
    }
}

/// @brief Currently bound mesh handle (NULL if none).
void *rt_scene_node3d_get_mesh(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->mesh : NULL;
}

/// @brief Bind a material to this node (replaces previous; null clears).
void rt_scene_node3d_set_material(void *obj, void *material) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (material && !rt_g3d_has_class(material, RT_G3D_MATERIAL3D_CLASS_ID))
        return;
    if (node->material == material)
        return;
    rt_obj_retain_maybe(material);
    scene3d_release_ref(&node->material);
    node->material = material;
}

/// @brief Currently bound material handle (NULL if none).
void *rt_scene_node3d_get_material(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->material : NULL;
}

/// @brief Attach a Light3D to this node; Scene3D.Draw transforms it by the node world pose.
void rt_scene_node3d_set_light(void *obj, void *light) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (light && !rt_g3d_has_class(light, RT_G3D_LIGHT3D_CLASS_ID))
        return;
    if (node->light == light)
        return;
    rt_obj_retain_maybe(light);
    scene3d_release_ref(&node->light);
    node->light = light;
}

/// @brief Currently attached Light3D handle (NULL if this node has no imported/local light).
void *rt_scene_node3d_get_light(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->light : NULL;
}

/// @brief Toggle whether this node participates in rendering.
void rt_scene_node3d_set_visible(void *obj, int8_t visible) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (node)
        node->visible = visible ? 1 : 0;
}

/// @brief Read the visibility flag (0 or 1; 0 if `obj` is NULL).
int8_t rt_scene_node3d_get_visible(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->visible : 0;
}

/// @brief Set the node's identifier name (used by `rt_scene_node3d_find`).
void rt_scene_node3d_set_name(void *obj, rt_string name) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (!name)
        name = rt_const_cstr("");
    if (node->name == name)
        return;
    rt_obj_retain_maybe(name);
    scene3d_release_ref((void **)&node->name);
    node->name = name;
}

/// @brief Read the node's name (empty string if unset or `obj` is NULL).
rt_string rt_scene_node3d_get_name(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (node && node->name)
        return node->name;
    return rt_const_cstr("");
}

/// @brief Local-space minimum corner of this node subtree's AABB (origin if empty).
void *rt_scene_node3d_get_aabb_min(void *obj) {
    double identity[16];
    float bounds_min[3];
    float bounds_max[3];
    int has_bounds;
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(0, 0, 0);
    scene_bounds_reset(bounds_min, bounds_max);
    mat4d_identity(identity);
    has_bounds = scene_node_collect_subtree_bounds(n, identity, bounds_min, bounds_max);
    if (!has_bounds) {
        bounds_min[0] = bounds_min[1] = bounds_min[2] = 0.0f;
        bounds_max[0] = bounds_max[1] = bounds_max[2] = 0.0f;
    }
    return rt_vec3_new(bounds_min[0], bounds_min[1], bounds_min[2]);
}

/// @brief Local-space maximum corner of this node subtree's AABB.
void *rt_scene_node3d_get_aabb_max(void *obj) {
    double identity[16];
    float bounds_min[3];
    float bounds_max[3];
    int has_bounds;
    rt_scene_node3d *n = scene_node3d_checked(obj);
    if (!n)
        return rt_vec3_new(0, 0, 0);
    scene_bounds_reset(bounds_min, bounds_max);
    mat4d_identity(identity);
    has_bounds = scene_node_collect_subtree_bounds(n, identity, bounds_min, bounds_max);
    if (!has_bounds) {
        bounds_min[0] = bounds_min[1] = bounds_min[2] = 0.0f;
        bounds_max[0] = bounds_max[1] = bounds_max[2] = 0.0f;
    }
    return rt_vec3_new(bounds_max[0], bounds_max[1], bounds_max[2]);
}

/// @brief Link a physics rigid body to this node so transforms stay in sync (see `set_sync_mode`).
void rt_scene_node3d_bind_body(void *obj, void *body) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (body && !rt_g3d_has_class(body, RT_G3D_BODY3D_CLASS_ID))
        return;
    if (node->bound_body == body)
        return;
    rt_obj_retain_maybe(body);
    scene3d_release_ref(&node->bound_body);
    node->bound_body = body;
}

/// @brief Detach any bound rigid body. Subsequent `sync` calls on this node become no-ops.
void rt_scene_node3d_clear_body_binding(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    scene3d_release_ref(&node->bound_body);
}

/// @brief Currently bound rigid body handle (NULL if none).
void *rt_scene_node3d_get_body(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->bound_body : NULL;
}

/// @brief Choose how this node and its bound body stay in sync each frame.
///
/// Modes: `NODE_FROM_BODY` (default — rigid-body sim drives the node),
/// `BODY_FROM_NODE` (kinematic — node animates the body),
/// `NODE_FROM_ANIMATOR_ROOT_MOTION` (root-motion driven), or
/// `TWO_WAY_KINEMATIC` (sync both directions per frame).
void rt_scene_node3d_set_sync_mode(void *obj, int64_t sync_mode) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    switch (sync_mode) {
    case RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY:
    case RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE:
    case RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION:
    case RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC:
        node->sync_mode = (int32_t)sync_mode;
        break;
    default:
        node->sync_mode = RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
        break;
    }
}

/// @brief Current node/body sync mode (`NODE_FROM_BODY` if `obj` is NULL).
int64_t rt_scene_node3d_get_sync_mode(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->sync_mode : RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY;
}

/// @brief Bind an animation controller to drive this node's transform / skeleton.
void rt_scene_node3d_bind_animator(void *obj, void *controller) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    if (controller && !rt_g3d_has_class(controller, RT_G3D_ANIMCONTROLLER3D_CLASS_ID))
        return;
    if (node->bound_animator == controller)
        return;
    rt_obj_retain_maybe(controller);
    scene3d_release_ref(&node->bound_animator);
    node->bound_animator = controller;
}

/// @brief Bind a NodeAnimator3D to this scene node so its clip channels are applied
///        each frame during `rt_scene3d_update`.
/// @details Retains the new animator and releases the old one. Crucially, the old
///   animator's `root` pointer is cleared to NULL before release so it cannot hold
///   a dangling reference to this node after the swap. The new animator's `root` is
///   set to this node immediately so `node_animator_update` can navigate the subtree
///   on the very next update tick. Passing NULL detaches the current animator and is
///   equivalent to calling `rt_scene_node3d_clear_node_animator_binding`.
/// @param obj      Scene node to drive; no-op if NULL.
/// @param animator NodeAnimator3D handle, or NULL to detach.
void rt_scene_node3d_bind_node_animator(void *obj, void *animator) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    rt_node_animator3d *node_animator;
    if (!node)
        return;
    if (animator && !rt_g3d_has_class(animator, RT_G3D_NODEANIMATOR3D_CLASS_ID))
        return;
    if (node->bound_node_animator == animator)
        return;
    rt_obj_retain_maybe(animator);
    node_animator = (rt_node_animator3d *)animator;
    if (node_animator && node_animator->root && node_animator->root != node) {
        rt_scene_node3d *old_root = node_animator->root;
        if (old_root->bound_node_animator == animator)
            scene3d_release_ref(&old_root->bound_node_animator);
        else
            node_animator->root = NULL;
    }
    if (node->bound_node_animator)
        ((rt_node_animator3d *)node->bound_node_animator)->root = NULL;
    scene3d_release_ref(&node->bound_node_animator);
    node->bound_node_animator = animator;
    if (node_animator)
        node_animator->root = node;
}

/// @brief Detach any bound animator. Subsequent frames stop applying its motion.
void rt_scene_node3d_clear_animator_binding(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    scene3d_release_ref(&node->bound_animator);
}

/// @brief Currently bound animation controller handle (NULL if none).
void *rt_scene_node3d_get_animator(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->bound_animator : NULL;
}

/*==========================================================================
 * Scene3D
 *=========================================================================*/

/// @brief GC finalizer for a Scene3D — releases the root node and any post-processing context.
static void rt_scene3d_finalize(void *obj) {
    rt_scene3d *scene = (rt_scene3d *)obj;
    if (!scene)
        return;
    if (scene->root)
        scene->root->parent = NULL;
    scene3d_release_ref((void **)&scene->root);
}

/// @brief Allocate a fresh Scene3D with an empty root node and no lights or skybox.
void *rt_scene3d_new(void) {
    rt_scene3d *s = (rt_scene3d *)rt_obj_new_i64(RT_G3D_SCENE3D_CLASS_ID, (int64_t)sizeof(rt_scene3d));
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
    s->node_count = 1; /* root */
    s->last_culled_count = 0;
    rt_obj_set_finalizer(s, rt_scene3d_finalize);
    return s;
}

/// @brief Return the implicit root node — every other node lives somewhere in its subtree.
void *rt_scene3d_get_root(void *obj) {
    rt_scene3d *s = scene3d_checked(obj);
    return s ? s->root : NULL;
}

/// @brief Convenience: add `node` as a direct child of the scene's root node.
void rt_scene3d_add(void *obj, void *node) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *n = scene_node3d_checked(node);
    if (!s || !n)
        return;
    rt_scene_node3d_add_child(s->root, n);
    s->node_count = count_subtree(s->root);
}

/// @brief Convenience: remove `node` from the scene root's children.
void rt_scene3d_remove(void *obj, void *node) {
    rt_scene3d *s = scene3d_checked(obj);
    rt_scene_node3d *n = scene_node3d_checked(node);
    if (!s || !n)
        return;
    if (node_contains(s->root, n) && n->parent)
        rt_scene_node3d_remove_child(n->parent, n);
    s->node_count = count_subtree(s->root);
}

/// @brief Locate a node by name via depth-first traversal from the scene root.
/// @return Pointer to the first matching node or NULL. Ownership is not
///   transferred — callers must not release the returned pointer directly.
void *rt_scene3d_find(void *obj, rt_string name) {
    rt_scene3d *s = scene3d_checked(obj);
    if (!s || !name)
        return NULL;
    const char *str = rt_string_cstr(name);
    if (!str)
        return NULL;
    return find_by_name(s->root, str);
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
    rt_light3d scene_light_storage[VGFX3D_MAX_LIGHTS];
    rt_light3d *scene_light_ptrs[VGFX3D_MAX_LIGHTS];
    rt_light3d *prev_scene_lights[VGFX3D_MAX_LIGHTS];
    int32_t scene_light_count = 0;
    int32_t prev_scene_light_count;
    float vp[16];
    vgfx3d_frustum_t frustum;
    int32_t culled = 0;

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

    float cam_pos[3] = {scene3d_float_or_zero(cam->eye[0]),
                        scene3d_float_or_zero(cam->eye[1]),
                        scene3d_float_or_zero(cam->eye[2])};
    prev_scene_light_count = canvas->scene_light_count;
    memcpy(prev_scene_lights, canvas->scene_lights, sizeof(prev_scene_lights));
    memset(scene_light_ptrs, 0, sizeof(scene_light_ptrs));
    scene_collect_node_lights(s->root, scene_light_storage, scene_light_ptrs, &scene_light_count);
    canvas->scene_light_count = scene_light_count;
    memset(canvas->scene_lights, 0, sizeof(canvas->scene_lights));
    memcpy(canvas->scene_lights, scene_light_ptrs, (size_t)scene_light_count * sizeof(scene_light_ptrs[0]));
    draw_node(s->root, canvas3d, &frustum, &culled, cam_pos, NULL);
    if (started_frame)
        rt_canvas3d_end(canvas3d);
    canvas->scene_light_count = prev_scene_light_count;
    memcpy(canvas->scene_lights, prev_scene_lights, sizeof(prev_scene_lights));
    s->last_culled_count = culled;
}

/// @brief Detach and release every child of the root so the scene is empty.
/// @details Preserves the root node itself — callers can continue adding
///   children afterwards without re-instantiating the scene. Each detached
///   subtree's retain count is decremented; subtrees held by other strong
///   refs outside the scene graph survive and remain usable.
void rt_scene3d_clear(void *obj) {
    rt_scene3d *s = scene3d_checked(obj);
    if (!s)
        return;
    /* Detach all children from root */
    for (int32_t i = 0; i < s->root->child_count; i++) {
        s->root->children[i]->parent = NULL;
        scene3d_release_ref((void **)&s->root->children[i]);
    }
    s->root->child_count = 0;
    s->node_count = 1; /* just root */
}

/// @brief Propagate node transforms out to bound systems (audio, etc.) for one tick.
/// @details Walks the tree once to let each node publish its world transform to
///   attached subsystems (e.g. 3D audio sources following a node), then ticks
///   the audio graph using `dt` so Doppler / attenuation stay in lockstep with
///   the scene's own integration timestep. Callers typically invoke this once
///   per simulation step, before submitting the scene for drawing.
void rt_scene3d_sync_bindings(void *obj, double dt) {
    rt_scene3d *scene = scene3d_checked(obj);
    if (!scene || !scene->root)
        return;
    scene_node_sync_recursive(scene->root, dt);
    rt_audio3d_sync_bindings(dt);
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

/// @brief Number of nodes culled by the most recent `rt_scene3d_draw` call.
/// @details Zero until the first draw. Useful as a coarse telemetry signal to
///   verify that culling is actually rejecting off-screen geometry.
int64_t rt_scene3d_get_culled_count(void *obj) {
    rt_scene3d *scene = scene3d_checked(obj);
    return scene ? scene->last_culled_count : 0;
}

/*==========================================================================
 * LOD — Level of Detail per SceneNode3D
 *=========================================================================*/

/// @brief Register a mesh LOD to swap in at or beyond a given camera distance.
/// @details Grows the LOD array on demand (doubling, min 4 slots) and keeps
///   entries sorted ascending by `distance` so the draw path can linearly pick
///   the first level whose threshold exceeds the current view distance. The
///   mesh is retained here and released by `rt_scene_node3d_clear_lod` so
///   callers may drop their local reference immediately after adding.
void rt_scene_node3d_add_lod(void *obj, double distance, void *mesh) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node || !mesh)
        return;
    if (!rt_g3d_has_class(mesh, RT_G3D_MESH3D_CLASS_ID))
        return;
    distance = scene3d_finite_or(distance, 0.0);
    if (distance < 0.0)
        distance = 0.0;

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
        if (!tmp)
            return;
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
}

/// @brief Release every registered LOD mesh on this node and reset the count.
/// @details Preserves the underlying `lod_levels` allocation so subsequent
///   `add_lod` calls can reuse it without reallocating.
void rt_scene_node3d_clear_lod(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return;
    for (int32_t i = 0; i < node->lod_count; i++)
        scene3d_release_ref(&node->lod_levels[i].mesh);
    node->lod_count = 0;
}

/// @brief Number of LOD levels currently registered on this node.
int64_t rt_scene_node3d_get_lod_count(void *obj) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    return node ? node->lod_count : 0;
}

/// @brief Distance threshold (in world units) for the LOD at `index`.
/// @return Ascending-sorted threshold, or 0.0 for an out-of-range index or
///   null node; 0.0 is a safe sentinel because LOD 0 (if present) would
///   normally specify a non-zero distance anyway.
double rt_scene_node3d_get_lod_distance(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return 0.0;
    if (index < 0 || index >= node->lod_count)
        return 0.0;
    return node->lod_levels[index].distance;
}

/// @brief Borrowed pointer to the mesh registered at LOD `index`.
/// @return The mesh or NULL; ownership stays with the scene node.
void *rt_scene_node3d_get_lod_mesh(void *obj, int64_t index) {
    rt_scene_node3d *node = scene_node3d_checked(obj);
    if (!node)
        return NULL;
    if (index < 0 || index >= node->lod_count)
        return NULL;
    return node->lod_levels[index].mesh;
}

//=============================================================================

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
