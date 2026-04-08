//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_model3d.h
// Purpose: High-level imported 3D asset container for meshes, materials,
//   skeletons, animations, and an instantiable template node hierarchy.
//
// Key invariants:
//   - Model3D.Load routes by file extension: .vscn, .fbx, .gltf, .glb.
//   - Imported resources are shared; Instantiate() clones node trees only.
//   - InstantiateScene() creates a fresh Scene3D and attaches cloned top-level
//     nodes below the scene root.
//
// Links: rt_scene3d.h, rt_fbx_loader.h, rt_gltf.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_model3d_load(rt_string path);

int64_t rt_model3d_get_mesh_count(void *obj);
int64_t rt_model3d_get_material_count(void *obj);
int64_t rt_model3d_get_skeleton_count(void *obj);
int64_t rt_model3d_get_animation_count(void *obj);
int64_t rt_model3d_get_node_count(void *obj);

void *rt_model3d_get_mesh(void *obj, int64_t index);
void *rt_model3d_get_material(void *obj, int64_t index);
void *rt_model3d_get_skeleton(void *obj, int64_t index);
void *rt_model3d_get_animation(void *obj, int64_t index);

void *rt_model3d_find_node(void *obj, rt_string name);
void *rt_model3d_instantiate(void *obj);
void *rt_model3d_instantiate_scene(void *obj);

#ifdef __cplusplus
}
#endif
