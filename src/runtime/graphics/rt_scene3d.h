//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_scene3d.h
// Purpose: Viper.Graphics3D.Scene3D and SceneNode3D — hierarchical scene graph
//   with parent-child transform propagation. Nodes hold local TRS (translate,
//   rotate, scale) and compute world matrices by multiplying up the hierarchy.
//
// Key invariants:
//   - Scene3D always has a root node (created in constructor).
//   - SceneNode3D world_matrix is recomputed lazily when dirty.
//   - Dirty flag propagates to all descendants on any transform change.
//   - Children are stored as a dynamic array (not GC-managed).
//   - Nodes with both mesh and material are drawn; others are transform groups.
//
// Links: rt_canvas3d.h, rt_quat.h, rt_mat4.h, plans/3d/12-scene-graph.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    RT_SCENE_NODE3D_SYNC_NODE_FROM_BODY = 0,
    RT_SCENE_NODE3D_SYNC_BODY_FROM_NODE = 1,
    RT_SCENE_NODE3D_SYNC_NODE_FROM_ANIMATOR_ROOT_MOTION = 2,
    RT_SCENE_NODE3D_SYNC_TWO_WAY_KINEMATIC = 3,
};

/* Scene3D */
void *rt_scene3d_new(void);
void *rt_scene3d_get_root(void *scene);
void rt_scene3d_add(void *scene, void *node);
void rt_scene3d_remove(void *scene, void *node);
void *rt_scene3d_find(void *scene, rt_string name);
void rt_scene3d_draw(void *scene, void *canvas3d, void *camera);
void rt_scene3d_clear(void *scene);
int64_t rt_scene3d_get_node_count(void *scene);
int64_t rt_scene3d_save(void *scene, rt_string path);
void *rt_scene3d_load(rt_string path);
void rt_scene3d_sync_bindings(void *scene, double dt);

/* SceneNode3D */
void *rt_scene_node3d_new(void);
void rt_scene_node3d_set_position(void *node, double x, double y, double z);
void *rt_scene_node3d_get_position(void *node);
void rt_scene_node3d_set_rotation(void *node, void *quat);
void *rt_scene_node3d_get_rotation(void *node);
void rt_scene_node3d_set_scale(void *node, double x, double y, double z);
void *rt_scene_node3d_get_scale(void *node);
void *rt_scene_node3d_get_world_matrix(void *node);
void rt_scene_node3d_add_child(void *node, void *child);
void rt_scene_node3d_remove_child(void *node, void *child);
int64_t rt_scene_node3d_child_count(void *node);
void *rt_scene_node3d_get_child(void *node, int64_t index);
void *rt_scene_node3d_get_parent(void *node);
void *rt_scene_node3d_find(void *node, rt_string name);
void rt_scene_node3d_set_mesh(void *node, void *mesh);
void *rt_scene_node3d_get_mesh(void *node);
void rt_scene_node3d_set_material(void *node, void *material);
void *rt_scene_node3d_get_material(void *node);
void rt_scene_node3d_set_visible(void *node, int8_t visible);
int8_t rt_scene_node3d_get_visible(void *node);
void rt_scene_node3d_set_name(void *node, rt_string name);
rt_string rt_scene_node3d_get_name(void *node);
void *rt_scene_node3d_get_aabb_min(void *node);
void *rt_scene_node3d_get_aabb_max(void *node);
void rt_scene_node3d_bind_body(void *node, void *body);
void rt_scene_node3d_clear_body_binding(void *node);
void *rt_scene_node3d_get_body(void *node);
void rt_scene_node3d_set_sync_mode(void *node, int64_t sync_mode);
int64_t rt_scene_node3d_get_sync_mode(void *node);
void rt_scene_node3d_bind_animator(void *node, void *controller);
void rt_scene_node3d_clear_animator_binding(void *node);
void *rt_scene_node3d_get_animator(void *node);

/* LOD — Level of Detail */
void rt_scene_node3d_add_lod(void *node, double distance, void *mesh);
void rt_scene_node3d_clear_lod(void *node);
int64_t rt_scene_node3d_get_lod_count(void *node);
double rt_scene_node3d_get_lod_distance(void *node, int64_t index);
void *rt_scene_node3d_get_lod_mesh(void *node, int64_t index);

/* Scene3D — frustum culling stats */
int64_t rt_scene3d_get_culled_count(void *scene);
int64_t rt_scene3d_get_node_count(void *scene);

#ifdef __cplusplus
}
#endif
