//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_gltf.h
// Purpose: glTF 2.0 (.gltf/.glb) asset loader.
// Key invariants:
//   - Supports .gltf (JSON + external files) and .glb (binary container)
//   - PBR metallic-roughness materials mapped to Blinn-Phong
//   - Embedded base64 buffers/images and GLB bufferView images are supported
//   - Extracts meshes, materials, node-attached lights, skeletons, animations,
//     and the active-scene node hierarchy
// Ownership/Lifetime:
//   - Caller owns the returned asset and all objects within it
// Links: rt_mesh3d.c, rt_material3d.c, rt_skeleton3d.c
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Load a glTF/GLB asset from @p path. @return an asset handle, or NULL.
void *rt_gltf_load(rt_string path);
/// @brief Load a glTF/GLB asset through the runtime asset manager.
/// @details Supports mounted/embedded assets plus dev filesystem fallback. External
///          .gltf buffers/images resolve relative to the parent model asset.
void *rt_gltf_load_asset(rt_string path);
/// @brief Number of meshes in the asset.
int64_t rt_gltf_mesh_count(void *asset);
/// @brief Get the mesh at @p index (NULL if out of range).
void *rt_gltf_get_mesh(void *asset, int64_t index);
/// @brief Number of materials in the asset.
int64_t rt_gltf_material_count(void *asset);
/// @brief Get the material at @p index (NULL if out of range).
void *rt_gltf_get_material(void *asset, int64_t index);
/// @brief Number of skeletons (skins) in the asset.
int64_t rt_gltf_skeleton_count(void *asset);
/// @brief Get the skeleton at @p index (NULL if out of range).
void *rt_gltf_get_skeleton(void *asset, int64_t index);
/// @brief Number of skeletal animation clips.
int64_t rt_gltf_animation_count(void *asset);
/// @brief Get the skeletal animation at @p index (NULL if out of range).
void *rt_gltf_get_animation(void *asset, int64_t index);
/// @brief Number of node (transform) animation clips.
int64_t rt_gltf_node_animation_count(void *asset);
/// @brief Get the node animation at @p index (NULL if out of range).
void *rt_gltf_get_node_animation(void *asset, int64_t index);
/// @brief Number of imported cameras in the active scene.
int64_t rt_gltf_camera_count(void *asset);
/// @brief Get the imported camera at @p index (NULL if out of range).
void *rt_gltf_get_camera(void *asset, int64_t index);
/// @brief Number of immutable scenes in the asset. Index 0 is the active/default scene.
int64_t rt_gltf_scene_count(void *asset);
/// @brief Get the immutable scene name at @p index (empty string if out of range).
rt_string rt_gltf_get_scene_name(void *asset, int64_t index);
/// @brief Get the scene-graph root for immutable scene @p index (NULL if out of range).
void *rt_gltf_get_scene_root_at(void *asset, int64_t index);
/// @brief Number of imported cameras reachable from immutable scene @p scene_index.
int64_t rt_gltf_scene_camera_count(void *asset, int64_t scene_index);
/// @brief Get an imported camera from immutable scene @p scene_index (NULL if out of range).
void *rt_gltf_get_scene_camera(void *asset, int64_t scene_index, int64_t index);
/// @brief Number of nodes in the asset's scene graph.
int64_t rt_gltf_node_count(void *asset);
/// @brief Get the asset's scene-graph root node (NULL if none).
void *rt_gltf_get_scene_root(void *asset);

#ifdef __cplusplus
}
#endif
