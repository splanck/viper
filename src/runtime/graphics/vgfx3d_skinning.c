//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_skinning.c
// Purpose: CPU-side skeletal vertex skinning. Transforms each vertex's
//   position and normal by up to 4 weighted bone matrices from the palette.
//
// Links: vgfx3d_skinning.h, plans/3d/14-skeletal-animation.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx3d_skinning.h"

#include <math.h>
#include <string.h>

/// @brief 3d skin vertices.
void vgfx3d_skin_vertices(const vgfx3d_vertex_t *src,
                          vgfx3d_vertex_t *dst,
                          uint32_t vertex_count,
                          const float *palette,
                          int32_t bone_count) {
    for (uint32_t v = 0; v < vertex_count; v++) {
        float pos[3] = {0, 0, 0};
        float nrm[3] = {0, 0, 0};
        float total_w = 0.0f;

        for (int b = 0; b < 4; b++) {
            float w = src[v].bone_weights[b];
            if (w < 1e-6f)
                continue;
            int idx = (int)src[v].bone_indices[b];
            if (idx >= bone_count)
                continue;

            const float *m = &palette[idx * 16];
            /* pos += w * (M * src_pos) — row-major multiply */
            for (int i = 0; i < 3; i++) {
                pos[i] += w * (m[i * 4 + 0] * src[v].pos[0] + m[i * 4 + 1] * src[v].pos[1] +
                               m[i * 4 + 2] * src[v].pos[2] + m[i * 4 + 3]);
                nrm[i] += w * (m[i * 4 + 0] * src[v].normal[0] + m[i * 4 + 1] * src[v].normal[1] +
                               m[i * 4 + 2] * src[v].normal[2]);
            }
            total_w += w;
        }

        /* Copy all attributes, then overwrite position and normal */
        if (dst != src)
            dst[v] = src[v];

        if (total_w > 1e-6f) {
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
