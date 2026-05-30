//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_model3d.h
// Purpose: High-level imported 3D asset container for meshes, materials,
//   skeletons, animations, and an instantiable template node hierarchy.
//
// Key invariants:
//   - Model3D.Load routes by file extension: .vscn, .fbx, .gltf, .glb, .obj.
//   - Imported resources are shared, except morph-enabled meshes are cloned per
//     instantiation so mutable blend-shape weights do not leak across instances.
//   - InstantiateScene() creates a fresh Scene3D and attaches cloned top-level
//     nodes below the scene root.
//
// Links: rt_scene3d.h, rt_fbx_loader.h, rt_gltf.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Load a 3D model asset (mesh/material/skeleton/animation graph) from
///        a file. @return New Model3D handle, or NULL on failure.
void *rt_model3d_load(rt_string path);
/// @brief Load a 3D model asset through the runtime asset manager.
/// @details glTF/GLB assets resolve external dependencies relative to the model
///          asset and search mounted/embedded packages before dev filesystem files.
void *rt_model3d_load_asset(rt_string path);
/// @brief Internal async path: build a glTF/GLB model from preloaded root bytes.
/// @details Takes ownership of @p preloaded_data; callers must not reuse it.
void *rt_model3d_load_preloaded_gltf(rt_string path,
                                     uint8_t *preloaded_data,
                                     size_t preloaded_size,
                                     int load_assets);
struct rt_gltf_preload_bundle;
/// @brief Internal async path: load a glTF/GLB Model3D from staged root/dependency bytes.
/// @details Takes ownership of @p bundle.
void *rt_model3d_load_preloaded_gltf_bundle(rt_string path,
                                            struct rt_gltf_preload_bundle *bundle,
                                            int load_assets);

/// @brief Number of meshes contained in the model.
int64_t rt_model3d_get_mesh_count(void *obj);
/// @brief Number of materials contained in the model.
int64_t rt_model3d_get_material_count(void *obj);
/// @brief Number of skeletons contained in the model.
int64_t rt_model3d_get_skeleton_count(void *obj);
/// @brief Number of animations contained in the model.
int64_t rt_model3d_get_animation_count(void *obj);
/// @brief Number of scene-graph nodes contained in the model.
int64_t rt_model3d_get_node_count(void *obj);
/// @brief Number of immutable scenes contained in the model.
int64_t rt_model3d_get_scene_count(void *obj);
/// @brief Number of imported cameras in @p scene_index.
int64_t rt_model3d_get_camera_count(void *obj, int64_t scene_index);

/// @brief Get the mesh at @p index (NULL if out of range).
void *rt_model3d_get_mesh(void *obj, int64_t index);
/// @brief Get the material at @p index (NULL if out of range).
void *rt_model3d_get_material(void *obj, int64_t index);
/// @brief Get the skeleton at @p index (NULL if out of range).
void *rt_model3d_get_skeleton(void *obj, int64_t index);
/// @brief Get the animation at @p index (NULL if out of range).
void *rt_model3d_get_animation(void *obj, int64_t index);
/// @brief Get the imported camera at @p index in @p scene_index (NULL if out of range).
void *rt_model3d_get_camera(void *obj, int64_t scene_index, int64_t index);
/// @brief Get the immutable scene name at @p index (empty string if out of range).
rt_string rt_model3d_get_scene_name(void *obj, int64_t index);

/// @brief Find a scene-graph node by name (NULL if not found).
void *rt_model3d_find_node(void *obj, rt_string name);
/// @brief Instantiate the model's default node hierarchy as a Scene3D node.
void *rt_model3d_instantiate(void *obj);
/// @brief Instantiate the model as a complete standalone Scene3D.
void *rt_model3d_instantiate_scene(void *obj);
/// @brief Instantiate the immutable scene at @p index as a complete standalone Scene3D.
void *rt_model3d_instantiate_scene_at(void *obj, int64_t index);

#ifdef __cplusplus
}
#endif
