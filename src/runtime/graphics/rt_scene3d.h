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
/// @brief Create an empty 3D scene with a single root node.
void *rt_scene3d_new(void);
/// @brief Get the implicit root node so callers can attach children directly.
void *rt_scene3d_get_root(void *scene);
/// @brief Convenience: attach @p node beneath the scene root.
void rt_scene3d_add(void *scene, void *node);
/// @brief Convenience: detach @p node from the scene root.
void rt_scene3d_remove(void *scene, void *node);
/// @brief Recursively search the scene for a node whose name matches @p name (NULL if none).
void *rt_scene3d_find(void *scene, rt_string name);
/// @brief Render every visible drawable node in the scene from @p camera onto @p canvas3d.
void rt_scene3d_draw(void *scene, void *canvas3d, void *camera);
/// @brief Detach all children of the root (does not delete the root itself).
void rt_scene3d_clear(void *scene);
/// @brief Total number of nodes in the scene (counts root + every descendant).
int64_t rt_scene3d_get_node_count(void *scene);
/// @brief Serialize the scene to a .vscn file. Returns 1 on success, 0 on failure.
int64_t rt_scene3d_save(void *scene, rt_string path);
/// @brief Deserialize a scene from a .vscn / .gltf / .glb / .fbx file. NULL on failure.
void *rt_scene3d_load(rt_string path);
/// @brief Push physics body, animator root-motion, and other bindings into node transforms.
/// Call once per frame after physics step but before draw.
void rt_scene3d_sync_bindings(void *scene, double dt);

/* SceneNode3D */
/// @brief Create a SceneNode3D at the origin with identity rotation, unit scale, no mesh.
void *rt_scene_node3d_new(void);
/// @brief Set the node's local-space position (relative to its parent).
void rt_scene_node3d_set_position(void *node, double x, double y, double z);
/// @brief Get the node's local-space position as a Vec3.
void *rt_scene_node3d_get_position(void *node);
/// @brief Set the node's local-space orientation from a Quaternion.
void rt_scene_node3d_set_rotation(void *node, void *quat);
/// @brief Get the node's local-space orientation as a Quaternion.
void *rt_scene_node3d_get_rotation(void *node);
/// @brief Set the node's local-space scale (independent x/y/z factors).
void rt_scene_node3d_set_scale(void *node, double x, double y, double z);
/// @brief Get the node's local-space scale as a Vec3.
void *rt_scene_node3d_get_scale(void *node);
/// @brief Get the node's world-space transform as a Mat4 (lazily recomputed when dirty).
void *rt_scene_node3d_get_world_matrix(void *node);
/// @brief Attach @p child as a child of @p node (detaches from any prior parent).
void rt_scene_node3d_add_child(void *node, void *child);
/// @brief Detach @p child from @p node (no-op if not actually a child).
void rt_scene_node3d_remove_child(void *node, void *child);
/// @brief Number of direct children of @p node.
int64_t rt_scene_node3d_child_count(void *node);
/// @brief Get the @p index-th child (NULL if out of range).
void *rt_scene_node3d_get_child(void *node, int64_t index);
/// @brief Get the node's parent (NULL if root or detached).
void *rt_scene_node3d_get_parent(void *node);
/// @brief Recursively search this subtree for a node whose name matches @p name.
void *rt_scene_node3d_find(void *node, rt_string name);
/// @brief Bind a Mesh3D for rendering at this node's world transform.
void rt_scene_node3d_set_mesh(void *node, void *mesh);
/// @brief Get the bound Mesh3D (NULL if this is a transform-only node).
void *rt_scene_node3d_get_mesh(void *node);
/// @brief Bind a Material3D for the node's mesh draw.
void rt_scene_node3d_set_material(void *node, void *material);
/// @brief Get the bound Material3D (NULL if none).
void *rt_scene_node3d_get_material(void *node);
/// @brief Toggle visibility (hidden nodes and their descendants are skipped during draw).
void rt_scene_node3d_set_visible(void *node, int8_t visible);
/// @brief Get the visibility flag.
int8_t rt_scene_node3d_get_visible(void *node);
/// @brief Set the node's name (used by `_find` lookups).
void rt_scene_node3d_set_name(void *node, rt_string name);
/// @brief Get the node's name (empty string if unset).
rt_string rt_scene_node3d_get_name(void *node);
/// @brief Get the AABB minimum corner for the node subtree in this node's local space.
void *rt_scene_node3d_get_aabb_min(void *node);
/// @brief Get the AABB maximum corner for the node subtree in this node's local space.
void *rt_scene_node3d_get_aabb_max(void *node);
/// @brief Bind a physics Body3D so its transform syncs with this node's transform.
void rt_scene_node3d_bind_body(void *node, void *body);
/// @brief Remove the body binding (transform updates stop affecting the body).
void rt_scene_node3d_clear_body_binding(void *node);
/// @brief Get the bound Body3D (NULL if none).
void *rt_scene_node3d_get_body(void *node);
/// @brief Choose how node↔body sync flows (one of RT_SCENE_NODE3D_SYNC_*).
void rt_scene_node3d_set_sync_mode(void *node, int64_t sync_mode);
/// @brief Get the current sync-mode constant.
int64_t rt_scene_node3d_get_sync_mode(void *node);
/// @brief Bind an AnimController3D so its evaluated bones drive this node's child meshes.
void rt_scene_node3d_bind_animator(void *node, void *controller);
/// @brief Remove the animator binding.
void rt_scene_node3d_clear_animator_binding(void *node);
/// @brief Get the bound AnimController3D (NULL if none).
void *rt_scene_node3d_get_animator(void *node);

/* LOD — Level of Detail */
/// @brief Add a level-of-detail mesh swap: when camera distance exceeds @p distance, use @p mesh.
void rt_scene_node3d_add_lod(void *node, double distance, void *mesh);
/// @brief Remove all LOD entries (revert to the base mesh at all distances).
void rt_scene_node3d_clear_lod(void *node);
/// @brief Number of LOD entries registered on this node.
int64_t rt_scene_node3d_get_lod_count(void *node);
/// @brief Get the camera distance threshold for the @p index-th LOD entry.
double rt_scene_node3d_get_lod_distance(void *node, int64_t index);
/// @brief Get the mesh used for the @p index-th LOD entry.
void *rt_scene_node3d_get_lod_mesh(void *node, int64_t index);

/* Scene3D — frustum culling stats */
/// @brief Number of nodes culled by the frustum during the most recent Draw (perf metric).
int64_t rt_scene3d_get_culled_count(void *scene);
int64_t rt_scene3d_get_node_count(void *scene);

#ifdef __cplusplus
}
#endif
