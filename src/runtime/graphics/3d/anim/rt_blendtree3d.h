//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/anim/rt_blendtree3d.h
// Purpose: Parametric 1D/2D animation blend tree layered over AnimBlend3D.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Create a 1D blend tree bound to a Skeleton3D.
void *rt_blend_tree3d_new_1d(void *skeleton);
/// @brief Create a 2D blend tree bound to a Skeleton3D.
void *rt_blend_tree3d_new_2d(void *skeleton);
/// @brief Add an animation sample at parameter coordinate (x, y). Returns sample index or -1.
int64_t rt_blend_tree3d_add_sample(void *tree, void *animation, double x, double y);
/// @brief Set the current blend parameters.
void rt_blend_tree3d_set_param(void *tree, double x, double y);
/// @brief Recompute sample weights and advance the internal AnimBlend3D.
void rt_blend_tree3d_update(void *tree, double dt);
/// @brief Number of samples currently registered.
int64_t rt_blend_tree3d_get_sample_count(void *tree);
/// @brief Internal helper: borrow the underlying AnimBlend3D handle.
void *rt_blend_tree3d_get_blend(void *tree);

/// @brief 2D weighting mode: 0 = freeform-directional (default), 1 = legacy IDW.
void rt_blend_tree3d_set_blend_mode(void *tree, int64_t mode);

/// @brief Current 2D weighting mode.
int64_t rt_blend_tree3d_get_blend_mode(void *tree);

#ifdef __cplusplus
}
#endif
