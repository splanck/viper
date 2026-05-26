//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_frustum.h
// Purpose: View frustum extraction and AABB/sphere intersection tests for
//   culling objects outside the camera's view volume.
//
// Key invariants:
//   - Planes are extracted from the combined VP matrix (Gribb-Hartmann method).
//   - Plane normals point INWARD (positive side = inside frustum).
//   - All planes are normalized after extraction.
//   - Test returns: 0=outside, 1=intersecting, 2=fully inside.
//
// Links: plans/3d/13-frustum-culling.md, rt_scene3d.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef VIPER_ENABLE_GRAPHICS

/// @brief Six frustum planes in Ax+By+Cz+D=0 form (normalized).
/// Order: 0=left, 1=right, 2=bottom, 3=top, 4=near, 5=far.
typedef struct {
    float planes[6][4]; /* [A, B, C, D] per plane */
} vgfx3d_frustum_t;

/// @brief Extract frustum planes from a row-major View*Projection matrix.
void vgfx3d_frustum_extract(vgfx3d_frustum_t *f, const float vp[16]);

/// @brief Test an axis-aligned bounding box against the frustum.
/// @return 0 = fully outside, 1 = intersecting, 2 = fully inside.
int vgfx3d_frustum_test_aabb(const vgfx3d_frustum_t *f, const float min[3], const float max[3]);

/// @brief Test a bounding sphere against the frustum.
/// @return 0 = fully outside, 1 = intersecting, 2 = fully inside.
int vgfx3d_frustum_test_sphere(const vgfx3d_frustum_t *f, const float center[3], float radius);

/// @brief Transform an object-space AABB by a 4x4 row-major matrix,
///        producing a new world-space AABB that encloses the transformed box.
void vgfx3d_transform_aabb(const float obj_min[3],
                           const float obj_max[3],
                           const double world_matrix[16],
                           float out_min[3],
                           float out_max[3]);

/// @brief Compute the AABB of a mesh's vertices.
void vgfx3d_compute_mesh_aabb(const void *vertices,
                              uint32_t vertex_count,
                              uint32_t vertex_stride,
                              float out_min[3],
                              float out_max[3]);

#endif /* VIPER_ENABLE_GRAPHICS */
