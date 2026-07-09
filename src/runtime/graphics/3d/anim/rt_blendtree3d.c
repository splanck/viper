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

#include "rt_g3d_ref_slots.h"
#include "rt_graphics3d_ids.h"
#include "rt_skeleton3d.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RT_BLENDTREE3D_MAX_SAMPLES 16
#define RT_BLENDTREE3D_PARAM_ABS_MAX 1000000.0
/* Delaunay of <= 16 points has at most 2n - 2 - b < 30 triangles; 32 is a
 * comfortable inline bound (Bowyer-Watson transiently uses a few more). */
#define RT_BLENDTREE3D_MAX_TRIS 64

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
    /* 2D blend mode: 0 = freeform-directional (Delaunay + barycentric,
     * default), 1 = legacy inverse-distance-squared. */
    int32_t blend_mode_2d;
    /* Cached Delaunay triangulation of the sample points (freeform mode).
     * Rebuilt lazily whenever samples change; tri_count == 0 with
     * tris_dirty == 0 marks a degenerate layout (collinear samples) that
     * falls back to the legacy weighting. */
    int32_t tris[RT_BLENDTREE3D_MAX_TRIS * 3];
    int32_t tri_count;
    int8_t tris_dirty;
} rt_blend_tree3d;

/// @brief Validate @p obj as a BlendTree3D handle and return its typed pointer (NULL on mismatch).
static rt_blend_tree3d *blend_tree3d_checked(void *obj) {
    return (rt_blend_tree3d *)rt_g3d_checked_or_null(obj, RT_G3D_BLENDTREE3D_CLASS_ID);
}

/// @brief Return true when the owned backend still points at a live AnimBlend3D.
static int blend_tree3d_blend_valid(const rt_blend_tree3d *tree) {
    return tree && rt_g3d_has_class(tree->blend, RT_G3D_ANIMBLEND3D_CLASS_ID);
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
        rt_g3d_ref_slot_clear_unowned(slot);
        return;
    }
    rt_g3d_ref_slot_release(slot);
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
    if (!blend_tree3d_blend_valid(tree))
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
    if (!tree)
        return;
    if (!blend_tree3d_blend_valid(tree))
        blend_tree3d_release_blend_ref(&tree->blend);
    tree->sample_count = blend_tree3d_safe_sample_count(tree);
}

/// @brief Zero every sample's blend weight so a fresh weighting can be written.
static void blend_tree3d_clear_weights(rt_blend_tree3d *tree) {
    int32_t sample_count;
    if (!tree || !blend_tree3d_blend_valid(tree))
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
    if (!tree || !blend_tree3d_blend_valid(tree) || sample_count <= 0)
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
    if (!tree || !blend_tree3d_blend_valid(tree) || sample_count <= 0)
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

//===----------------------------------------------------------------------===//
// Freeform-directional 2D blending (Delaunay + barycentric)
//===----------------------------------------------------------------------===//

/// @brief Twice the signed area of triangle (a, b, c) in parameter space.
static double blend_tree3d_signed_area2(
    double ax, double ay, double bx, double by, double cx, double cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

/// @brief Rebuild the Delaunay triangulation of the tree's sample points
///        (Bowyer–Watson with a super-triangle). Leaves tri_count == 0 for
///        degenerate (collinear / duplicate-heavy) layouts.
static void blend_tree3d_triangulate(rt_blend_tree3d *tree) {
    tree->tri_count = 0;
    tree->tris_dirty = 0;
    int32_t n = blend_tree3d_safe_sample_count(tree);
    if (n < 3)
        return;

    /* Working points: samples plus a super-triangle enclosing everything. */
    double px[RT_BLENDTREE3D_MAX_SAMPLES + 3];
    double py[RT_BLENDTREE3D_MAX_SAMPLES + 3];
    double mnx = DBL_MAX, mny = DBL_MAX, mxx = -DBL_MAX, mxy = -DBL_MAX;
    for (int32_t i = 0; i < n; i++) {
        px[i] = blend_tree3d_finite_or_zero(tree->samples[i].x);
        py[i] = blend_tree3d_finite_or_zero(tree->samples[i].y);
        if (px[i] < mnx)
            mnx = px[i];
        if (py[i] < mny)
            mny = py[i];
        if (px[i] > mxx)
            mxx = px[i];
        if (py[i] > mxy)
            mxy = py[i];
    }
    double span = (mxx - mnx) > (mxy - mny) ? (mxx - mnx) : (mxy - mny);
    if (span < 1e-12)
        return; /* all samples coincide */
    double cx = (mnx + mxx) * 0.5;
    double cy = (mny + mxy) * 0.5;
    int32_t s0 = n, s1 = n + 1, s2 = n + 2;
    px[s0] = cx - 20.0 * span;
    py[s0] = cy - 10.0 * span;
    px[s1] = cx + 20.0 * span;
    py[s1] = cy - 10.0 * span;
    px[s2] = cx;
    py[s2] = cy + 20.0 * span;

    /* Triangle scratch (indices into px/py). */
    int32_t tv[RT_BLENDTREE3D_MAX_TRIS * 3];
    int8_t alive[RT_BLENDTREE3D_MAX_TRIS];
    int32_t tri_count = 0;
    tv[0] = s0;
    tv[1] = s1;
    tv[2] = s2;
    alive[0] = 1;
    tri_count = 1;

    /* Cavity boundary edge buffer. */
    int32_t ea[RT_BLENDTREE3D_MAX_TRIS * 3];
    int32_t eb[RT_BLENDTREE3D_MAX_TRIS * 3];

    for (int32_t ip = 0; ip < n; ip++) {
        int32_t edge_count = 0;
        for (int32_t t = 0; t < tri_count; t++) {
            if (!alive[t])
                continue;
            int32_t a = tv[t * 3], b = tv[t * 3 + 1], c = tv[t * 3 + 2];
            double area2 = blend_tree3d_signed_area2(px[a], py[a], px[b], py[b], px[c], py[c]);
            if (area2 < 0.0) {
                int32_t tmp = b;
                b = c;
                c = tmp;
                area2 = -area2;
            }
            if (area2 < 1e-18)
                continue; /* sliver: skip circumcircle math */
            double adx = px[a] - px[ip], ady = py[a] - py[ip];
            double bdx = px[b] - px[ip], bdy = py[b] - py[ip];
            double cdx = px[c] - px[ip], cdy = py[c] - py[ip];
            double det = (adx * adx + ady * ady) * (bdx * cdy - cdx * bdy) -
                         (bdx * bdx + bdy * bdy) * (adx * cdy - cdx * ady) +
                         (cdx * cdx + cdy * cdy) * (adx * bdy - bdx * ady);
            if (det <= 0.0)
                continue; /* point outside this triangle's circumcircle */
            /* Triangle dies; its edges join the cavity boundary (an edge
             * appearing twice is interior and cancels). */
            alive[t] = 0;
            int32_t evs[3][2] = {{tv[t * 3], tv[t * 3 + 1]},
                                 {tv[t * 3 + 1], tv[t * 3 + 2]},
                                 {tv[t * 3 + 2], tv[t * 3]}};
            for (int32_t e = 0; e < 3; e++) {
                int found = -1;
                for (int32_t k = 0; k < edge_count; k++) {
                    if ((ea[k] == evs[e][0] && eb[k] == evs[e][1]) ||
                        (ea[k] == evs[e][1] && eb[k] == evs[e][0])) {
                        found = k;
                        break;
                    }
                }
                if (found >= 0) {
                    ea[found] = ea[edge_count - 1];
                    eb[found] = eb[edge_count - 1];
                    edge_count--;
                } else if (edge_count < RT_BLENDTREE3D_MAX_TRIS * 3) {
                    ea[edge_count] = evs[e][0];
                    eb[edge_count] = evs[e][1];
                    edge_count++;
                }
            }
        }
        /* Compact dead triangles, then fan new ones from the cavity edges. */
        int32_t w = 0;
        for (int32_t t = 0; t < tri_count; t++) {
            if (!alive[t])
                continue;
            tv[w * 3] = tv[t * 3];
            tv[w * 3 + 1] = tv[t * 3 + 1];
            tv[w * 3 + 2] = tv[t * 3 + 2];
            alive[w] = 1;
            w++;
        }
        tri_count = w;
        for (int32_t k = 0; k < edge_count; k++) {
            if (tri_count >= RT_BLENDTREE3D_MAX_TRIS)
                return; /* overflow: stay degenerate (falls back to IDW) */
            tv[tri_count * 3] = ea[k];
            tv[tri_count * 3 + 1] = eb[k];
            tv[tri_count * 3 + 2] = ip;
            alive[tri_count] = 1;
            tri_count++;
        }
    }

    /* Keep only triangles free of super-triangle vertices. */
    for (int32_t t = 0; t < tri_count; t++) {
        int32_t a = tv[t * 3], b = tv[t * 3 + 1], c = tv[t * 3 + 2];
        if (a >= n || b >= n || c >= n)
            continue;
        tree->tris[tree->tri_count * 3] = a;
        tree->tris[tree->tri_count * 3 + 1] = b;
        tree->tris[tree->tri_count * 3 + 2] = c;
        tree->tri_count++;
    }
}

/// @brief Freeform-directional 2D weighting: barycentric inside the containing
///        Delaunay triangle (at most 3 non-zero weights), nearest-hull-edge
///        projection outside the hull (2 weights). Degenerate triangulations
///        fall back to the legacy inverse-distance weighting.
static void blend_tree3d_apply_2d_freeform(rt_blend_tree3d *tree) {
    int32_t sample_count = blend_tree3d_safe_sample_count(tree);
    if (!tree || !blend_tree3d_blend_valid(tree) || sample_count <= 0)
        return;
    if (tree->tris_dirty)
        blend_tree3d_triangulate(tree);
    if (tree->tri_count <= 0) {
        blend_tree3d_apply_2d(tree); /* collinear layout: legacy weighting */
        return;
    }

    double x = blend_tree3d_finite_or_zero(tree->param_x);
    double y = blend_tree3d_finite_or_zero(tree->param_y);

    /* Interior: barycentric weights over the containing triangle. */
    for (int32_t t = 0; t < tree->tri_count; t++) {
        int32_t a = tree->tris[t * 3], b = tree->tris[t * 3 + 1], c = tree->tris[t * 3 + 2];
        double ax = tree->samples[a].x, ay = tree->samples[a].y;
        double bx = tree->samples[b].x, by = tree->samples[b].y;
        double cx = tree->samples[c].x, cy = tree->samples[c].y;
        double area2 = blend_tree3d_signed_area2(ax, ay, bx, by, cx, cy);
        if (fabs(area2) < 1e-18)
            continue;
        double wa = blend_tree3d_signed_area2(x, y, bx, by, cx, cy) / area2;
        double wb = blend_tree3d_signed_area2(ax, ay, x, y, cx, cy) / area2;
        double wc = 1.0 - wa - wb;
        const double tol = -1e-9;
        if (wa >= tol && wb >= tol && wc >= tol) {
            blend_tree3d_clear_weights(tree);
            rt_anim_blend3d_set_weight(
                tree->blend, tree->samples[a].blend_index, wa < 0.0 ? 0.0 : wa);
            rt_anim_blend3d_set_weight(
                tree->blend, tree->samples[b].blend_index, wb < 0.0 ? 0.0 : wb);
            rt_anim_blend3d_set_weight(
                tree->blend, tree->samples[c].blend_index, wc < 0.0 ? 0.0 : wc);
            return;
        }
    }

    /* Exterior: project onto the nearest hull (boundary) edge and lerp its
     * two clips. A boundary edge belongs to exactly one triangle. */
    double best_d2 = DBL_MAX;
    int32_t best_a = tree->tris[0];
    int32_t best_b = tree->tris[1];
    double best_t = 0.0;
    for (int32_t t = 0; t < tree->tri_count; t++) {
        for (int32_t e = 0; e < 3; e++) {
            int32_t a = tree->tris[t * 3 + e];
            int32_t b = tree->tris[t * 3 + (e + 1) % 3];
            int shared = 0;
            for (int32_t u = 0; u < tree->tri_count && !shared; u++) {
                if (u == t)
                    continue;
                for (int32_t e2 = 0; e2 < 3; e2++) {
                    int32_t ua = tree->tris[u * 3 + e2];
                    int32_t ub = tree->tris[u * 3 + (e2 + 1) % 3];
                    if ((ua == a && ub == b) || (ua == b && ub == a)) {
                        shared = 1;
                        break;
                    }
                }
            }
            if (shared)
                continue; /* interior edge */
            double ax = tree->samples[a].x, ay = tree->samples[a].y;
            double bx = tree->samples[b].x, by = tree->samples[b].y;
            double ex = bx - ax, ey = by - ay;
            double len2 = ex * ex + ey * ey;
            double proj = len2 > 1e-18 ? ((x - ax) * ex + (y - ay) * ey) / len2 : 0.0;
            if (proj < 0.0)
                proj = 0.0;
            if (proj > 1.0)
                proj = 1.0;
            double qx = ax + ex * proj, qy = ay + ey * proj;
            double d2 = (x - qx) * (x - qx) + (y - qy) * (y - qy);
            if (d2 < best_d2) {
                best_d2 = d2;
                best_a = a;
                best_b = b;
                best_t = proj;
            }
        }
    }
    blend_tree3d_clear_weights(tree);
    rt_anim_blend3d_set_weight(tree->blend, tree->samples[best_a].blend_index, 1.0 - best_t);
    rt_anim_blend3d_set_weight(tree->blend, tree->samples[best_b].blend_index, best_t);
}

/// @brief Dispatch to the 1D or 2D weighting routine based on the tree's dimensionality.
static void blend_tree3d_apply_weights(rt_blend_tree3d *tree) {
    if (!tree)
        return;
    if (tree->dimensions == 2) {
        if (tree->blend_mode_2d == 1)
            blend_tree3d_apply_2d(tree);
        else
            blend_tree3d_apply_2d_freeform(tree);
    } else {
        blend_tree3d_apply_1d(tree);
    }
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
    if (!tree || !blend_tree3d_blend_valid(tree) ||
        !rt_g3d_has_class(animation, RT_G3D_ANIMATION3D_CLASS_ID))
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
    tree->tris_dirty = 1;
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
    if (!tree || !blend_tree3d_blend_valid(tree))
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
    return blend_tree3d_blend_valid(tree) ? tree->blend : NULL;
}

/// @brief Select the 2D weighting mode: 0 = freeform-directional (Delaunay +
///        barycentric, the default — at most 3 non-zero weights, hull-edge
///        projection outside), 1 = legacy inverse-distance-squared (kept for
///        content authored against the old soft blending). No effect on 1D
///        trees. Weights are recomputed immediately.
void rt_blend_tree3d_set_blend_mode(void *obj, int64_t mode) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    if (!tree)
        return;
    tree->blend_mode_2d = mode == 1 ? 1 : 0;
    blend_tree3d_repair_sample_count(tree);
    blend_tree3d_apply_weights(tree);
}

/// @brief Current 2D weighting mode (0 = freeform, 1 = legacy IDW).
int64_t rt_blend_tree3d_get_blend_mode(void *obj) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    return tree ? tree->blend_mode_2d : 0;
}

#else
typedef int rt_blendtree3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
