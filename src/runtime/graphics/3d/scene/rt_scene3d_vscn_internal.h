//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/scene/rt_scene3d_vscn_internal.h
// Purpose: Shared limits and value sanitizers for the Scene3D .vscn save/load
//   pair. The serializer (rt_scene3d_vscn_save.c) and loader
//   (rt_scene3d_vscn_load.c, including its material-parse .inc) both clamp
//   floating-point values and validate material enum ids, so those pure
//   helpers live here as static inline functions (one internal-linkage copy
//   per translation unit: no exported symbol, no source duplication).
//
// Key invariants:
//   - Helpers are pure: finite/range clamping, vector normalization, and
//     enum-id validation only.
//   - VSCN_ABS_MAX bounds serialized/parsed coordinate magnitudes.
//
// Ownership/Lifetime:
//   - No state; operates only on caller-provided values.
//
// Links: rt_scene3d_vscn_save.c, rt_scene3d_vscn_load.c, rt_canvas3d.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_canvas3d.h" // RT_MATERIAL3D_* enum ids used by the validators below

#include <math.h>
#include <stdint.h>

/* A VSCN node adds an object and, except at the leaf, a children array to the shared JSON parser's
 * 200-level nesting budget. Ninety-eight node levels leave room for the document root plus the
 * deepest node's optional light/LOD/auto-LOD objects. Save and load enforce this same limit. */
#define VSCN_MAX_NODE_DEPTH 98
#define VSCN_ABS_MAX 1.0e12
#define VSCN_MAX_FILE_BYTES (256u * 1024u * 1024u)

/// @brief Return @p value if finite, else @p fallback. Base sanitizer for loaded JSON numbers.
static inline double vscn_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Sanitize @p value (fallback if non-finite) and clamp to ±VSCN_ABS_MAX.
static inline double vscn_clamp_abs_or(double value, double fallback) {
    value = vscn_finite_or(value, fallback);
    if (value > VSCN_ABS_MAX)
        return VSCN_ABS_MAX;
    if (value < -VSCN_ABS_MAX)
        return -VSCN_ABS_MAX;
    return value;
}

/// @brief Sanitize @p value (fallback if non-finite) and clamp to [lo, hi].
static inline double vscn_clamp_or(double value, double fallback, double lo, double hi) {
    value = vscn_finite_or(value, fallback);
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

/// @brief Sanitize @p value (fallback if non-finite) and clamp negatives to 0.
static inline double vscn_nonnegative_or(double value, double fallback) {
    value = vscn_finite_or(value, fallback);
    return value < 0.0 ? 0.0 : value;
}

/// @brief Accept a material workflow id (legacy/PBR) from JSON, else use @p fallback.
static inline int32_t vscn_material_workflow_or(int64_t value, int32_t fallback) {
    if (value == RT_MATERIAL3D_WORKFLOW_LEGACY || value == RT_MATERIAL3D_WORKFLOW_PBR)
        return (int32_t)value;
    return fallback;
}

/// @brief Accept an alpha-mode id (opaque/mask/blend) from JSON, else use @p fallback.
static inline int32_t vscn_alpha_mode_or(int64_t value, int32_t fallback) {
    if (value >= RT_MATERIAL3D_ALPHA_MODE_OPAQUE && value <= RT_MATERIAL3D_ALPHA_MODE_BLEND)
        return (int32_t)value;
    return fallback;
}

/// @brief Accept a texture-wrap mode (repeat/clamp/mirror) from JSON, else use @p fallback.
static inline int32_t vscn_wrap_or(int64_t value, int32_t fallback) {
    if (value == RT_MATERIAL3D_TEXTURE_WRAP_REPEAT ||
        value == RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE ||
        value == RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT)
        return (int32_t)value;
    return fallback;
}

/// @brief Accept a texture-filter mode (nearest/linear) from JSON, else use @p fallback.
static inline int32_t vscn_filter_or(int64_t value, int32_t fallback) {
    if (value == RT_MATERIAL3D_TEXTURE_FILTER_NEAREST ||
        value == RT_MATERIAL3D_TEXTURE_FILTER_LINEAR)
        return (int32_t)value;
    return fallback;
}

/// @brief Normalize a loaded quaternion in place; non-finite or near-zero-length values
///   reset to the identity quaternion (0,0,0,1).
static inline void vscn_normalize_quat(double q[4]) {
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
static inline void vscn_normalize_vec3(double v[3], double fx, double fy, double fz) {
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
