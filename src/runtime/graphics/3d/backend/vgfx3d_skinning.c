//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_skinning.c
// Purpose: CPU-side skeletal vertex skinning. Transforms each vertex's
//   position and normal by up to 4 weighted bone matrices from the palette.
//
// Links: vgfx3d_skinning.h, plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx3d_skinning.h"
#include "vgfx3d_backend_utils.h"

#include <math.h>
#include <string.h>

/// @brief Apply 4-influence linear blend skinning on the CPU.
/// @details For every vertex, sums `weight[i] * palette[bone_index[i]] * v` over the
///   four bone influences carried in `src[v].bone_weights` / `src[v].bone_indices`.
///   Positions take the full affine row (translation column included); normals use
///   each bone's inverse-transpose normal matrix and are re-normalized. Influences
///   with weight below 1e-6 or whose bone index exceeds `bone_count` are skipped.
///   Non-position/normal
///   attributes are passed through by copying `src[v]` into `dst[v]` first when the
///   buffers differ (in-place skinning is supported by leaving `dst == src`).
///   Vertices with zero total weight fall through unchanged so unskinned geometry
///   is preserved for meshes that mix rigid and skinned verts.
/// @param src Source vertex array (read-only).
/// @param dst Destination vertex array; may alias `src`.
/// @param vertex_count Number of vertices to transform.
/// @param palette Row-major 4x4 matrix palette laid out as `bone_count` matrices
///   stored back-to-back (16 floats per matrix).
/// @param bone_count Upper bound on valid indices in `palette`.
void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src,
                          vgfx3d_vertex_t *dst,
                          uint32_t vertex_count,
                          const float *palette,
                          int32_t bone_count) {
    if (!src || !dst || vertex_count == 0)
        return;
    if (!palette || bone_count <= 0) {
        if (dst != src)
            memcpy(dst, src, (size_t)vertex_count * sizeof(*dst));
        return;
    }

    for (uint32_t v = 0; v < vertex_count; v++) {
        float pos[3] = {0, 0, 0};
        float nrm[3] = {0, 0, 0};
        float total_w = 0.0f;

        for (int b = 0; b < 4; b++) {
            float w = src[v].bone_weights[b];
            if (!isfinite(w) || w <= 1e-6f)
                continue;
            int idx = (int)src[v].bone_indices[b];
            if (idx >= bone_count)
                continue;

            const float *m = &palette[idx * 16];
            float nm[16];
            vgfx3d_compute_normal_matrix4(m, nm);
            /* pos += w * (M * src_pos) — row-major multiply */
            for (int i = 0; i < 3; i++) {
                pos[i] += w * (m[i * 4 + 0] * src[v].pos[0] + m[i * 4 + 1] * src[v].pos[1] +
                               m[i * 4 + 2] * src[v].pos[2] + m[i * 4 + 3]);
                nrm[i] += w * (nm[i * 4 + 0] * src[v].normal[0] + nm[i * 4 + 1] * src[v].normal[1] +
                               nm[i * 4 + 2] * src[v].normal[2]);
            }
            total_w += w;
        }

        /* Copy all attributes, then overwrite position and normal */
        if (dst != src)
            dst[v] = src[v];

        if (total_w > 1e-6f) {
            float inv_w = 1.0f / total_w;
            pos[0] *= inv_w;
            pos[1] *= inv_w;
            pos[2] *= inv_w;
            nrm[0] *= inv_w;
            nrm[1] *= inv_w;
            nrm[2] *= inv_w;
            memcpy(dst[v].pos, pos, sizeof(float) * 3);
            /* Normalize skinned normal */
            float len = sqrtf(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
            if (len > 1e-8f) {
                nrm[0] /= len;
                nrm[1] /= len;
                nrm[2] /= len;
            }
            memcpy(dst[v].normal, nrm, sizeof(float) * 3);
        }
    }
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
