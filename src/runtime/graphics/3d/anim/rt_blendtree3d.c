//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_blendtree3d.c
// Purpose: Parametric 1D/2D BlendTree3D controller layered over AnimBlend3D —
//   maps a continuous (x, y) parameter onto per-clip animation blend weights.
//
// Key invariants:
//   - At most RT_BLENDTREE3D_MAX_SAMPLES (16) animation samples per tree.
//   - 1D trees blend the two samples bracketing param_x; 2D trees use
//     normalized inverse-distance-squared weighting over all samples.
//   - A parameter landing exactly on a sample snaps fully to it (1D shares
//     weight equally among ties; 2D takes the first exact match).
//   - Weights are recomputed eagerly on every add_sample/set_param/update.
//
// Ownership/Lifetime:
//   - BlendTree3D is GC-managed; it owns the underlying AnimBlend3D and the
//     finalizer releases it. Samples are stored inline (no per-sample alloc).
//
// Links: rt_blendtree3d.h, rt_skeleton3d.h (AnimBlend3D backend)
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_blendtree3d.h"

#include "rt_graphics3d_ids.h"
#include "rt_skeleton3d.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT_BLENDTREE3D_MAX_SAMPLES 16
#define RT_BLENDTREE3D_PARAM_ABS_MAX 1000000.0

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);

/// @brief One parameter-space sample: its (x, y) coordinate and the AnimBlend3D
///        state index it drives.
typedef struct {
    double x;
    double y;
    int64_t blend_index;
} rt_blend_tree3d_sample;

/// @brief BlendTree3D state: the owned AnimBlend3D, dimensionality (1 or 2), the
///        current parameter, and the inline fixed-capacity sample table.
typedef struct {
    void *vptr;
    void *blend;
    int32_t dimensions;
    int32_t sample_count;
    double param_x;
    double param_y;
    rt_blend_tree3d_sample samples[RT_BLENDTREE3D_MAX_SAMPLES];
} rt_blend_tree3d;

/// @brief Validate @p obj as a BlendTree3D handle and return its typed pointer (NULL on mismatch).
static rt_blend_tree3d *blend_tree3d_checked(void *obj) {
    return (rt_blend_tree3d *)rt_g3d_checked_or_null(obj, RT_G3D_BLENDTREE3D_CLASS_ID);
}

/// @brief Release a GC-managed reference when this drop is the last one.
static void blend_tree3d_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

/// @brief Release the owned AnimBlend3D only if the private slot still has that class.
static void blend_tree3d_release_blend_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_g3d_has_class(*slot, RT_G3D_ANIMBLEND3D_CLASS_ID)) {
        *slot = NULL;
        return;
    }
    blend_tree3d_release_local(*slot);
    *slot = NULL;
}

/// @brief Return @p value when finite, else 0 — sanitizes parameter inputs.
static double blend_tree3d_finite_or_zero(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value > RT_BLENDTREE3D_PARAM_ABS_MAX)
        return RT_BLENDTREE3D_PARAM_ABS_MAX;
    if (value < -RT_BLENDTREE3D_PARAM_ABS_MAX)
        return -RT_BLENDTREE3D_PARAM_ABS_MAX;
    return value;
}

/// @brief Number of blend-tree samples safe to use: clamped to RT_BLENDTREE3D_MAX_SAMPLES and
///   truncated at the first sample whose blend_index falls outside the blender's state range.
static int32_t blend_tree3d_safe_sample_count(const rt_blend_tree3d *tree) {
    int32_t limit;
    int32_t count = 0;
    int64_t blend_state_count;
    if (!tree || tree->sample_count <= 0)
        return 0;
    blend_state_count = rt_anim_blend3d_state_count(tree->blend);
    if (blend_state_count <= 0)
        return 0;
    limit = tree->sample_count < RT_BLENDTREE3D_MAX_SAMPLES ? tree->sample_count
                                                            : RT_BLENDTREE3D_MAX_SAMPLES;
    while (count < limit && tree->samples[count].blend_index >= 0 &&
           tree->samples[count].blend_index < blend_state_count)
        count++;
    return count;
}

/// @brief Clamp the blend tree's sample_count to its safe value (defensive).
static void blend_tree3d_repair_sample_count(rt_blend_tree3d *tree) {
    if (tree)
        tree->sample_count = blend_tree3d_safe_sample_count(tree);
}

/// @brief Zero every sample's blend weight so a fresh weighting can be written.
static void blend_tree3d_clear_weights(rt_blend_tree3d *tree) {
    int32_t sample_count;
    if (!tree || !tree->blend)
        return;
    sample_count = blend_tree3d_safe_sample_count(tree);
    for (int32_t i = 0; i < sample_count; i++)
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[i].blend_index, 0.0);
}

/// @brief Compute 1D blend weights for the current param_x and push them to AnimBlend3D.
/// @details Locates the nearest sample below (`lower`) and above (`upper`) param_x and
///          linearly interpolates their two weights by the fractional position between
///          them. Samples within 1e-9 of param_x are treated as exact and share the full
///          weight equally; when param_x falls outside the sample range, the single
///          bracketing sample receives weight 1.0.
static void blend_tree3d_apply_1d(rt_blend_tree3d *tree) {
    int32_t lower = -1;
    int32_t upper = -1;
    int32_t exact_count = 0;
    int32_t sample_count;
    double x;
    sample_count = blend_tree3d_safe_sample_count(tree);
    if (!tree || !tree->blend || sample_count <= 0)
        return;
    x = blend_tree3d_finite_or_zero(tree->param_x);
    blend_tree3d_clear_weights(tree);
    if (sample_count == 1) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[0].blend_index, 1.0);
        return;
    }
    for (int32_t i = 0; i < sample_count; i++) {
        double sx = tree->samples[i].x;
        if (!isfinite(sx))
            continue;
        if (fabs(sx - x) <= 1e-9) {
            exact_count++;
            continue;
        }
        if (sx < x && (lower < 0 || sx > tree->samples[lower].x))
            lower = i;
        if (sx > x && (upper < 0 || sx < tree->samples[upper].x))
            upper = i;
    }
    if (exact_count > 0) {
        double w = 1.0 / (double)exact_count;
        for (int32_t i = 0; i < sample_count; i++) {
            if (fabs(tree->samples[i].x - x) <= 1e-9)
                rt_anim_blend3d_set_weight(tree->blend, tree->samples[i].blend_index, w);
        }
        return;
    }
    if (lower < 0 && upper < 0) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[0].blend_index, 1.0);
        return;
    }
    if (lower < 0) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[upper].blend_index, 1.0);
        return;
    }
    if (upper < 0) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[lower].blend_index, 1.0);
        return;
    }
    {
        double span = tree->samples[upper].x - tree->samples[lower].x;
        double t = span > 1e-12 ? (x - tree->samples[lower].x) / span : 0.0;
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[lower].blend_index, 1.0 - t);
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[upper].blend_index, t);
    }
}

/// @brief Compute 2D blend weights for (param_x, param_y) and push them to AnimBlend3D.
/// @details Uses normalized inverse-distance-squared weighting: each sample contributes
///          1/d² of its parameter-space distance, then all weights are normalized to sum
///          to 1. A parameter landing on a sample (d² ≤ 1e-12) snaps fully to it; a
///          degenerate total (non-finite or near zero) falls back to weighting sample 0.
static void blend_tree3d_apply_2d(rt_blend_tree3d *tree) {
    double raw[RT_BLENDTREE3D_MAX_SAMPLES];
    double total = 0.0;
    int32_t exact = -1;
    int32_t nearest = 0;
    double nearest_d2 = DBL_MAX;
    int32_t sample_count = blend_tree3d_safe_sample_count(tree);
    if (!tree || !tree->blend || sample_count <= 0)
        return;
    blend_tree3d_clear_weights(tree);
    if (sample_count == 1) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[0].blend_index, 1.0);
        return;
    }
    for (int32_t i = 0; i < sample_count; i++) {
        double dx = tree->param_x - tree->samples[i].x;
        double dy = tree->param_y - tree->samples[i].y;
        double d2 = dx * dx + dy * dy;
        raw[i] = 0.0;
        if (!isfinite(d2))
            continue;
        if (d2 < nearest_d2) {
            nearest_d2 = d2;
            nearest = i;
        }
        if (d2 <= 1e-12) {
            exact = i;
            break;
        }
        raw[i] = 1.0 / d2;
        total += raw[i];
    }
    if (exact >= 0) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[exact].blend_index, 1.0);
        return;
    }
    if (total <= DBL_MIN || !isfinite(total)) {
        /* Degenerate inverse-distance weighting (every sample astronomically far
         * or non-finite): snap to the geometrically nearest sample rather than
         * blindly defaulting to sample 0, which could be the farthest one. */
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[nearest].blend_index, 1.0);
        return;
    }
    for (int32_t i = 0; i < sample_count; i++)
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[i].blend_index, raw[i] / total);
}

/// @brief Dispatch to the 1D or 2D weighting routine based on the tree's dimensionality.
static void blend_tree3d_apply_weights(rt_blend_tree3d *tree) {
    if (!tree)
        return;
    if (tree->dimensions == 2)
        blend_tree3d_apply_2d(tree);
    else
        blend_tree3d_apply_1d(tree);
}

/// @brief GC finalizer: release the owned AnimBlend3D backend.
static void blend_tree3d_finalize(void *obj) {
    rt_blend_tree3d *tree = (rt_blend_tree3d *)obj;
    if (!tree)
        return;
    blend_tree3d_release_blend_ref(&tree->blend);
}

/// @brief Shared constructor for 1D/2D trees: wrap a new AnimBlend3D bound to @p skeleton.
/// @details @p dimensions is clamped to 1 or 2. Returns NULL if @p skeleton is not a
///          Skeleton3D or if either the AnimBlend3D or the tree allocation fails.
static void *blend_tree3d_new(void *skeleton, int32_t dimensions) {
    rt_blend_tree3d *tree;
    void *blend;
    if (!rt_g3d_has_class(skeleton, RT_G3D_SKELETON3D_CLASS_ID))
        return NULL;
    blend = rt_anim_blend3d_new(skeleton);
    if (!blend)
        return NULL;
    tree = (rt_blend_tree3d *)rt_obj_new_i64(RT_G3D_BLENDTREE3D_CLASS_ID,
                                             (int64_t)sizeof(rt_blend_tree3d));
    if (!tree) {
        blend_tree3d_release_local(blend);
        return NULL;
    }
    memset(tree, 0, sizeof(*tree));
    tree->dimensions = dimensions == 2 ? 2 : 1;
    tree->blend = blend;
    for (int32_t i = 0; i < RT_BLENDTREE3D_MAX_SAMPLES; i++)
        tree->samples[i].blend_index = -1;
    rt_obj_set_finalizer(tree, blend_tree3d_finalize);
    return tree;
}

/// @brief Create a 1D blend tree bound to @p skeleton.
void *rt_blend_tree3d_new_1d(void *skeleton) {
    return blend_tree3d_new(skeleton, 1);
}

/// @brief Create a 2D blend tree bound to @p skeleton.
void *rt_blend_tree3d_new_2d(void *skeleton) {
    return blend_tree3d_new(skeleton, 2);
}

/// @brief Register an animation sample at parameter coordinate (x, y) and reblend.
/// @details Non-finite coordinates are sanitized to 0. Returns the new sample's index,
///          or -1 if the handle/animation is invalid or the sample table is full.
int64_t rt_blend_tree3d_add_sample(void *obj, void *animation, double x, double y) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    int64_t blend_index;
    if (!tree || !tree->blend || !rt_g3d_has_class(animation, RT_G3D_ANIMATION3D_CLASS_ID))
        return -1;
    blend_tree3d_repair_sample_count(tree);
    if (tree->sample_count >= RT_BLENDTREE3D_MAX_SAMPLES)
        return -1;
    blend_index = rt_anim_blend3d_add_state(tree->blend, NULL, animation);
    if (blend_index < 0)
        return -1;
    tree->samples[tree->sample_count].x = blend_tree3d_finite_or_zero(x);
    tree->samples[tree->sample_count].y = blend_tree3d_finite_or_zero(y);
    tree->samples[tree->sample_count].blend_index = blend_index;
    tree->sample_count++;
    blend_tree3d_apply_weights(tree);
    return tree->sample_count - 1;
}

/// @brief Set the current blend parameters (sanitized to finite) and recompute weights.
void rt_blend_tree3d_set_param(void *obj, double x, double y) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    if (!tree)
        return;
    blend_tree3d_repair_sample_count(tree);
    tree->param_x = blend_tree3d_finite_or_zero(x);
    tree->param_y = blend_tree3d_finite_or_zero(y);
    blend_tree3d_apply_weights(tree);
}

/// @brief Recompute sample weights and advance the underlying AnimBlend3D by @p dt seconds.
void rt_blend_tree3d_update(void *obj, double dt) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    if (!tree || !tree->blend)
        return;
    blend_tree3d_repair_sample_count(tree);
    if (!isfinite(dt) || dt < 0.0)
        dt = 0.0;
    blend_tree3d_apply_weights(tree);
    rt_anim_blend3d_update(tree->blend, dt);
}

/// @brief Number of samples currently registered (0 for an invalid handle).
int64_t rt_blend_tree3d_get_sample_count(void *obj) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    return tree ? blend_tree3d_safe_sample_count(tree) : 0;
}

/// @brief Borrow the underlying AnimBlend3D handle (not retained; NULL if invalid).
void *rt_blend_tree3d_get_blend(void *obj) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    return tree ? tree->blend : NULL;
}

#else
typedef int rt_blendtree3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
