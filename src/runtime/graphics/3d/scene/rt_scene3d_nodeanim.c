//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_nodeanim.c
// Purpose: Node-animation subsystem (glTF-style channel animation) for Scene3D —
//   NodeAnimation3D clips, NodeAnimator3D playback, channel sampling/application,
//   and morph-weight propagation. Split out of rt_scene3d.c; shares private
//   structs/helpers via rt_scene3d_internal.h.
// Links: rt_scene3d_internal.h, rt_morphtarget3d.h, rt_quat.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

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
#include "rt_scene3d.h"
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
    rt_node_animation3d *anim = (rt_node_animation3d *)rt_obj_new_i64(
        RT_G3D_NODEANIMATION3D_CLASS_ID, (int64_t)sizeof(rt_node_animation3d));
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
    if (!anim || needed < 0)
        return 0;
    return scene3d_grow_array_i32((void **)&anim->channels,
                                  &anim->channel_capacity,
                                  needed,
                                  4,
                                  sizeof(*anim->channels),
                                  1);
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
        channel->interpolation = interpolation == RT_NODE_ANIM_INTERP_STEP
                                     ? RT_NODE_ANIM_INTERP_STEP
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
    return node_animation_add_channel_impl(
        obj, target_name, path, interpolation, key_count, value_width, times, values, NULL, NULL);
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
    animator = (rt_node_animator3d *)rt_obj_new_i64(RT_G3D_NODEANIMATOR3D_CLASS_ID,
                                                    (int64_t)sizeof(rt_node_animator3d));
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
    alpha = (t1 > t0 && channel->interpolation != RT_NODE_ANIM_INTERP_STEP)
                ? (time - t0) / (t1 - t0)
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
            out_values[i] =
                (float)(h00 * channel->values[ai] + h10 * dt * channel->out_tangents[ai] +
                        h01 * channel->values[bi] + h11 * dt * channel->in_tangents[bi]);
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

/// @brief Propagate morph-target weights from a WEIGHTS channel through a matching subtree.
/// @details glTF WEIGHTS targets one node's morph set. Imported multi-primitive nodes may
///   represent that set on child mesh nodes, so this walk first picks the target subtree's
///   first morph-target object and then applies weights only to meshes sharing that exact
///   object. Unrelated morphed descendants are left untouched.
/// @param node         Root of the subtree to drive.
/// @param weights      Array of weight values from the sampled WEIGHTS channel.
/// @param weight_count Number of values in @p weights.
static void *node_anim_find_first_morph_targets(rt_scene_node3d *node) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    void *result = NULL;
    if (!node)
        return NULL;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("NodeAnimation3D: traversal stack allocation failed");
        return NULL;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        rt_mesh3d *mesh = (rt_mesh3d *)current->mesh;
        if (mesh && mesh->morph_targets_ref) {
            result = mesh->morph_targets_ref;
            break;
        }
        for (int32_t i = current->child_count - 1; i >= 0; i--) {
            if (!scene_node_stack_push(&stack, &count, &capacity, current->children[i])) {
                rt_trap("NodeAnimation3D: traversal stack allocation failed");
                free(stack);
                return NULL;
            }
        }
    }
    free(stack);
    return result;
}

/// @brief Apply morph-target weights from an animation down a node subtree (recursive).
static void node_anim_apply_weights_recursive(rt_scene_node3d *node,
                                              const float *weights,
                                              int32_t weight_count) {
    rt_scene_node3d **stack = NULL;
    size_t count = 0;
    size_t capacity = 0;
    void *target_morphs;
    if (!node || !weights || weight_count <= 0)
        return;
    target_morphs = node_anim_find_first_morph_targets(node);
    if (!target_morphs)
        return;
    if (!scene_node_stack_push(&stack, &count, &capacity, node)) {
        rt_trap("NodeAnimation3D: traversal stack allocation failed");
        return;
    }
    while (count > 0) {
        rt_scene_node3d *current = stack[--count];
        rt_mesh3d *mesh = (rt_mesh3d *)current->mesh;
        if (mesh && mesh->morph_targets_ref == target_morphs) {
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

/// @brief Whether @p node lies in @p root's subtree (walks parent links up to root).
static int scene_node_is_descendant_of(rt_scene_node3d *root, rt_scene_node3d *node) {
    while (node) {
        if (node == root)
            return 1;
        node = node->parent;
    }
    return 0;
}

/// @brief Resolve an animation channel's target node within @p root's subtree, by name.
/// @details Caches the resolved node on the channel keyed by the target name, so repeated frames
/// skip
///          the by-name search unless the name changes.
static rt_scene_node3d *node_anim_resolve_target(rt_scene_node3d *root,
                                                 rt_node_anim_channel3d *channel) {
    const char *target_name;
    const char *cached_name;
    if (!root || !channel || !channel->target_name)
        return NULL;
    target_name = rt_string_cstr(channel->target_name);
    if (!target_name)
        return NULL;
    if (channel->cached_root == root && channel->cached_target &&
        scene_node_is_descendant_of(root, channel->cached_target)) {
        cached_name =
            channel->cached_target->name ? rt_string_cstr(channel->cached_target->name) : "";
        if (cached_name && strcmp(cached_name, target_name) == 0)
            return channel->cached_target;
    }
    channel->cached_target = find_by_name(root, target_name);
    channel->cached_root = channel->cached_target ? root : NULL;
    return channel->cached_target;
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
                                    rt_node_anim_channel3d *channel,
                                    double time) {
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
    target = node_anim_resolve_target(root, channel);
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
                scene3d_quat_normalize_local(target->rotation);
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
void node_animator_update(rt_node_animator3d *animator, double dt) {
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

#endif /* VIPER_ENABLE_GRAPHICS */
