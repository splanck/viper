//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_frustum.c
// Purpose: View frustum plane extraction and bounding volume intersection
//   tests. Used by the scene graph (Phase 12) to cull objects outside the
//   camera's view volume before submitting draw calls.
//
// Key invariants:
//   - Gribb-Hartmann method extracts planes from the combined VP matrix.
//   - AABB test uses p-vertex/n-vertex optimization (early-out on first
//     outside plane, tracks intersecting vs fully-inside).
//   - Transform AABB expands the 8 corners and re-fits.
//
// Links: vgfx3d_frustum.h, plans/3d/13-frustum-culling.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx3d_frustum.h"
#include "rt_canvas3d_internal.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

/*==========================================================================
 * Frustum plane extraction (Gribb-Hartmann method)
 *
 * For a row-major VP matrix m[r*4+c], each frustum plane is derived from
 * the sum or difference of two matrix rows. The plane normal points inward
 * (positive half-space = inside the frustum).
 *=========================================================================*/

/// @brief Extract the six view-frustum planes from a view-projection matrix.
/// @details Uses Gribb-Hartmann: each plane equation `ax + by + cz + d = 0` is
///   the sum or difference of two rows of a row-major VP matrix. Plane ordering
///   is [Left, Right, Bottom, Top, Near, Far]; normals point inward so a positive
///   signed distance means "inside the frustum". Each plane is normalized so that
///   subsequent distance tests yield true metric distances (needed for the sphere
///   test, which compares against `radius` directly).
void vgfx3d_frustum_extract(vgfx3d_frustum_t *f, const float vp[16]) {
    /* Left: row3 + row0 */
    f->planes[0][0] = vp[12] + vp[0];
    f->planes[0][1] = vp[13] + vp[1];
    f->planes[0][2] = vp[14] + vp[2];
    f->planes[0][3] = vp[15] + vp[3];

    /* Right: row3 - row0 */
    f->planes[1][0] = vp[12] - vp[0];
    f->planes[1][1] = vp[13] - vp[1];
    f->planes[1][2] = vp[14] - vp[2];
    f->planes[1][3] = vp[15] - vp[3];

    /* Bottom: row3 + row1 */
    f->planes[2][0] = vp[12] + vp[4];
    f->planes[2][1] = vp[13] + vp[5];
    f->planes[2][2] = vp[14] + vp[6];
    f->planes[2][3] = vp[15] + vp[7];

    /* Top: row3 - row1 */
    f->planes[3][0] = vp[12] - vp[4];
    f->planes[3][1] = vp[13] - vp[5];
    f->planes[3][2] = vp[14] - vp[6];
    f->planes[3][3] = vp[15] - vp[7];

    /* Near: row3 + row2 */
    f->planes[4][0] = vp[12] + vp[8];
    f->planes[4][1] = vp[13] + vp[9];
    f->planes[4][2] = vp[14] + vp[10];
    f->planes[4][3] = vp[15] + vp[11];

    /* Far: row3 - row2 */
    f->planes[5][0] = vp[12] - vp[8];
    f->planes[5][1] = vp[13] - vp[9];
    f->planes[5][2] = vp[14] - vp[10];
    f->planes[5][3] = vp[15] - vp[11];

    /* Normalize each plane so distance tests give metric results */
    for (int i = 0; i < 6; i++) {
        float len = sqrtf(f->planes[i][0] * f->planes[i][0] + f->planes[i][1] * f->planes[i][1] +
                          f->planes[i][2] * f->planes[i][2]);
        if (len > 1e-8f) {
            float inv = 1.0f / len;
            f->planes[i][0] *= inv;
            f->planes[i][1] *= inv;
            f->planes[i][2] *= inv;
            f->planes[i][3] *= inv;
        }
    }
}

/*==========================================================================
 * AABB frustum test (p-vertex / n-vertex method)
 *
 * For each plane, the p-vertex is the AABB corner MOST in the direction of
 * the plane normal (farthest along the positive side). If the p-vertex is
 * behind the plane, the entire box is outside. The n-vertex is the opposite
 * corner; if it's behind the plane, the box straddles (intersects).
 *=========================================================================*/

/// @brief Classify an axis-aligned box against the frustum.
/// @details Uses the p-vertex / n-vertex optimization: for each plane we select
///   the box corner that sits farthest along the plane normal (p-vertex). If the
///   p-vertex is behind the plane the entire box is outside — that single test
///   rejects without checking the remaining 7 corners. The opposite corner
///   (n-vertex) disambiguates fully-inside from straddling.
/// @return 0 outside, 1 intersecting, 2 fully inside. The 3-valued result lets
///   callers skip recursive culling when the parent volume is fully inside.
int vgfx3d_frustum_test_aabb(const vgfx3d_frustum_t *f, const float min[3], const float max[3]) {
    int result = 2; /* assume fully inside */
    for (int i = 0; i < 6; i++) {
        /* Select p-vertex: corner most along plane normal */
        float px = f->planes[i][0] >= 0 ? max[0] : min[0];
        float py = f->planes[i][1] >= 0 ? max[1] : min[1];
        float pz = f->planes[i][2] >= 0 ? max[2] : min[2];
        float dist =
            f->planes[i][0] * px + f->planes[i][1] * py + f->planes[i][2] * pz + f->planes[i][3];
        if (dist < 0)
            return 0; /* p-vertex behind plane → entirely outside */

        /* Select n-vertex: corner least along plane normal */
        float nx = f->planes[i][0] >= 0 ? min[0] : max[0];
        float ny = f->planes[i][1] >= 0 ? min[1] : max[1];
        float nz = f->planes[i][2] >= 0 ? min[2] : max[2];
        float ndist =
            f->planes[i][0] * nx + f->planes[i][1] * ny + f->planes[i][2] * nz + f->planes[i][3];
        if (ndist < 0)
            result = 1; /* n-vertex behind → intersecting */
    }
    return result;
}

/*==========================================================================
 * Bounding sphere frustum test
 *=========================================================================*/

/// @brief Classify a bounding sphere against the frustum.
/// @details Relies on planes having been normalized by `vgfx3d_frustum_extract`
///   so the signed distance from `center` to each plane is measured in world
///   units directly comparable against `radius`. A sphere is outside when any
///   single plane distance is < -radius, intersecting when any distance is
///   within [-radius, +radius], otherwise fully inside.
/// @return 0 outside, 1 intersecting, 2 fully inside.
int vgfx3d_frustum_test_sphere(const vgfx3d_frustum_t *f, const float center[3], float radius) {
    int result = 2; /* assume fully inside */
    for (int i = 0; i < 6; i++) {
        float dist = f->planes[i][0] * center[0] + f->planes[i][1] * center[1] +
                     f->planes[i][2] * center[2] + f->planes[i][3];
        if (dist < -radius)
            return 0; /* entirely outside */
        if (dist < radius)
            result = 1; /* intersecting */
    }
    return result;
}

/*==========================================================================
 * AABB transform — expand object-space AABB to world-space AABB
 *
 * Transforms all 8 corners of the object-space AABB by the world matrix,
 * then computes the axis-aligned bounding box of the results.
 *=========================================================================*/

/// @brief Re-fit an object-space AABB into world space through a transform.
/// @details Transforms all eight corners of `[obj_min, obj_max]` by `world_matrix`
///   and returns the axis-aligned bounding box of the resulting point set. This
///   is the correct conservative AABB under arbitrary affine (including rotated
///   and non-uniformly scaled) transforms — simply transforming the two diagonal
///   corners would under-fit the result. The 3x3 translation column in row-major
///   convention is `world_matrix[3,7,11]`; the 4th row is ignored because an
///   affine transform leaves w unchanged.
void vgfx3d_transform_aabb(const float obj_min[3],
                           const float obj_max[3],
                           const double world_matrix[16],
                           float out_min[3],
                           float out_max[3]) {
    out_min[0] = out_min[1] = out_min[2] = FLT_MAX;
    out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;

    /* Iterate all 8 corners of the AABB */
    for (int i = 0; i < 8; i++) {
        float cx = (i & 1) ? obj_max[0] : obj_min[0];
        float cy = (i & 2) ? obj_max[1] : obj_min[1];
        float cz = (i & 4) ? obj_max[2] : obj_min[2];

        /* Transform by row-major 4x4 matrix (column-vector convention) */
        float wx = (float)(world_matrix[0] * cx + world_matrix[1] * cy + world_matrix[2] * cz +
                           world_matrix[3]);
        float wy = (float)(world_matrix[4] * cx + world_matrix[5] * cy + world_matrix[6] * cz +
                           world_matrix[7]);
        float wz = (float)(world_matrix[8] * cx + world_matrix[9] * cy + world_matrix[10] * cz +
                           world_matrix[11]);

        if (wx < out_min[0])
            out_min[0] = wx;
        if (wy < out_min[1])
            out_min[1] = wy;
        if (wz < out_min[2])
            out_min[2] = wz;
        if (wx > out_max[0])
            out_max[0] = wx;
        if (wy > out_max[1])
            out_max[1] = wy;
        if (wz > out_max[2])
            out_max[2] = wz;
    }
}

/*==========================================================================
 * Compute mesh AABB from vertex positions
 *=========================================================================*/

/// @brief Compute the axis-aligned bounding box of a strided vertex array.
/// @details Treats `vertices` as a byte buffer and reads three floats from each
///   stride-sized slot (matching the in-memory layout of `vgfx3d_vertex_t`,
///   where the position occupies the first three floats). Empty or null input
///   yields a zero-sized AABB at the origin rather than inverted sentinels, so
///   downstream code doesn't need a special "empty mesh" branch.
/// @param vertex_stride Byte distance between consecutive vertex position slots.
void vgfx3d_compute_mesh_aabb(const void *vertices,
                              uint32_t vertex_count,
                              uint32_t vertex_stride,
                              float out_min[3],
                              float out_max[3]) {
    out_min[0] = out_min[1] = out_min[2] = FLT_MAX;
    out_max[0] = out_max[1] = out_max[2] = -FLT_MAX;

    if (!vertices || vertex_count == 0) {
        out_min[0] = out_min[1] = out_min[2] = 0.0f;
        out_max[0] = out_max[1] = out_max[2] = 0.0f;
        return;
    }

    const uint8_t *base = (const uint8_t *)vertices;
    for (uint32_t i = 0; i < vertex_count; i++) {
        const float *pos = (const float *)(base + i * vertex_stride);
        for (int j = 0; j < 3; j++) {
            if (pos[j] < out_min[j])
                out_min[j] = pos[j];
            if (pos[j] > out_max[j])
                out_max[j] = pos[j];
        }
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
