//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gltf.h
// Purpose: glTF 2.0 (.gltf/.glb) asset loader.
// Key invariants:
//   - Supports .gltf (JSON + external files) and .glb (binary container)
//   - PBR metallic-roughness materials mapped to Blinn-Phong
//   - Embedded textures (base64 in .gltf, binary chunk in .glb) supported
//   - Skeleton, animation, and morph targets extracted when present
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

void *rt_gltf_load(rt_string path);
int64_t rt_gltf_mesh_count(void *asset);
void *rt_gltf_get_mesh(void *asset, int64_t index);
int64_t rt_gltf_material_count(void *asset);
void *rt_gltf_get_material(void *asset, int64_t index);

#ifdef __cplusplus
}
#endif
