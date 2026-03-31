//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_fbx_loader.h
// Purpose: FBX binary file loader — extracts Mesh3D, Skeleton3D, Animation3D,
//   and Material3D from .fbx files. Supports both v<7500 (32-bit offsets) and
//   v>=7500 (64-bit offsets). Uses rt_compress_inflate for zlib decompression.
//
// Key invariants:
//   - Zero external dependencies (uses existing rt_compress for zlib).
//   - Returns an FBX asset container with arrays of extracted objects.
//   - All extracted objects (meshes, skeleton, etc.) are GC-managed.
//   - Handles Blender Z-up → Y-up coordinate system conversion.
//
// Links: plans/3d/15-fbx-loader.md, rt_skeleton3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *rt_fbx_load(rt_string path);
int64_t rt_fbx_mesh_count(void *fbx);
void *rt_fbx_get_mesh(void *fbx, int64_t index);
void *rt_fbx_get_skeleton(void *fbx);
int64_t rt_fbx_animation_count(void *fbx);
void *rt_fbx_get_animation(void *fbx, int64_t index);
rt_string rt_fbx_get_animation_name(void *fbx, int64_t index);
int64_t rt_fbx_material_count(void *fbx);
void *rt_fbx_get_material(void *fbx, int64_t index);
void *rt_fbx_get_morph_target(void *fbx, int64_t mesh_index);

#ifdef __cplusplus
}
#endif
