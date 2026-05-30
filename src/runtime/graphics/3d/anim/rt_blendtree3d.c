//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_blendtree3d.c
// Purpose: Parametric BlendTree3D controller implemented over AnimBlend3D.
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

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);

typedef struct {
    double x;
    double y;
    int64_t blend_index;
} rt_blend_tree3d_sample;

typedef struct {
    void *vptr;
    void *blend;
    int32_t dimensions;
    int32_t sample_count;
    double param_x;
    double param_y;
    rt_blend_tree3d_sample samples[RT_BLENDTREE3D_MAX_SAMPLES];
} rt_blend_tree3d;

static rt_blend_tree3d *blend_tree3d_checked(void *obj) {
    return (rt_blend_tree3d *)rt_g3d_checked_or_null(obj, RT_G3D_BLENDTREE3D_CLASS_ID);
}

static void blend_tree3d_release_local(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

static double blend_tree3d_finite_or_zero(double value) {
    return isfinite(value) ? value : 0.0;
}

static void blend_tree3d_clear_weights(rt_blend_tree3d *tree) {
    if (!tree || !tree->blend)
        return;
    for (int32_t i = 0; i < tree->sample_count; i++)
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[i].blend_index, 0.0);
}

static void blend_tree3d_apply_1d(rt_blend_tree3d *tree) {
    int32_t lower = -1;
    int32_t upper = -1;
    int32_t exact_count = 0;
    double x;
    if (!tree || !tree->blend || tree->sample_count <= 0)
        return;
    x = tree->param_x;
    blend_tree3d_clear_weights(tree);
    if (tree->sample_count == 1) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[0].blend_index, 1.0);
        return;
    }
    for (int32_t i = 0; i < tree->sample_count; i++) {
        double sx = tree->samples[i].x;
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
        for (int32_t i = 0; i < tree->sample_count; i++) {
            if (fabs(tree->samples[i].x - x) <= 1e-9)
                rt_anim_blend3d_set_weight(tree->blend, tree->samples[i].blend_index, w);
        }
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

static void blend_tree3d_apply_2d(rt_blend_tree3d *tree) {
    double raw[RT_BLENDTREE3D_MAX_SAMPLES];
    double total = 0.0;
    int32_t exact = -1;
    if (!tree || !tree->blend || tree->sample_count <= 0)
        return;
    blend_tree3d_clear_weights(tree);
    if (tree->sample_count == 1) {
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[0].blend_index, 1.0);
        return;
    }
    for (int32_t i = 0; i < tree->sample_count; i++) {
        double dx = tree->param_x - tree->samples[i].x;
        double dy = tree->param_y - tree->samples[i].y;
        double d2 = dx * dx + dy * dy;
        raw[i] = 0.0;
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
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[0].blend_index, 1.0);
        return;
    }
    for (int32_t i = 0; i < tree->sample_count; i++)
        rt_anim_blend3d_set_weight(tree->blend, tree->samples[i].blend_index, raw[i] / total);
}

static void blend_tree3d_apply_weights(rt_blend_tree3d *tree) {
    if (!tree)
        return;
    if (tree->dimensions == 2)
        blend_tree3d_apply_2d(tree);
    else
        blend_tree3d_apply_1d(tree);
}

static void blend_tree3d_finalize(void *obj) {
    rt_blend_tree3d *tree = (rt_blend_tree3d *)obj;
    if (!tree)
        return;
    blend_tree3d_release_local(tree->blend);
    tree->blend = NULL;
}

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
    rt_obj_set_finalizer(tree, blend_tree3d_finalize);
    return tree;
}

void *rt_blend_tree3d_new_1d(void *skeleton) {
    return blend_tree3d_new(skeleton, 1);
}

void *rt_blend_tree3d_new_2d(void *skeleton) {
    return blend_tree3d_new(skeleton, 2);
}

int64_t rt_blend_tree3d_add_sample(void *obj, void *animation, double x, double y) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    int64_t blend_index;
    if (!tree || !rt_g3d_has_class(animation, RT_G3D_ANIMATION3D_CLASS_ID))
        return -1;
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

void rt_blend_tree3d_set_param(void *obj, double x, double y) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    if (!tree)
        return;
    tree->param_x = blend_tree3d_finite_or_zero(x);
    tree->param_y = blend_tree3d_finite_or_zero(y);
    blend_tree3d_apply_weights(tree);
}

void rt_blend_tree3d_update(void *obj, double dt) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    if (!tree || !tree->blend)
        return;
    blend_tree3d_apply_weights(tree);
    rt_anim_blend3d_update(tree->blend, dt);
}

int64_t rt_blend_tree3d_get_sample_count(void *obj) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    return tree ? tree->sample_count : 0;
}

void *rt_blend_tree3d_get_blend(void *obj) {
    rt_blend_tree3d *tree = blend_tree3d_checked(obj);
    return tree ? tree->blend : NULL;
}

#else
typedef int rt_blendtree3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
