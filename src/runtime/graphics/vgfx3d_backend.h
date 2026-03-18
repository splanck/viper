//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/vgfx3d_backend.h
// Purpose: Backend abstraction vtable for Viper.Graphics3D. All rendering
//   backends (software, Metal, D3D11, OpenGL) implement this interface.
//   Canvas3D dispatches through the vtable; backend selection is automatic.
//
// Key invariants:
//   - The software backend is always available as a fallback.
//   - GPU backends return non-zero from init() on failure → fallback to software.
//   - All vtable function pointers must be non-NULL.
//   - ctx is an opaque pointer owned by the backend (created in init, freed in destroy).
//
// Links: plans/3d/05-backend-abstraction.md, rt_canvas3d_internal.h
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx.h"
#include "rt_canvas3d_internal.h"
#include <stdint.h>

/*==========================================================================
 * Draw command — submitted between begin_frame/end_frame
 *=========================================================================*/

typedef struct
{
    const vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    float model_matrix[16]; /* row-major float */
    float diffuse_color[4]; /* RGBA material color */
    float specular[3];      /* RGB specular color */
    float shininess;        /* specular exponent */
    int8_t unlit;           /* skip lighting if true */
    const void *texture;    /* Pixels object (rt_pixels_view*) or NULL */
} vgfx3d_draw_cmd_t;

/*==========================================================================
 * Camera parameters — passed to begin_frame
 *=========================================================================*/

typedef struct
{
    float view[16];       /* view matrix, row-major float */
    float projection[16]; /* projection matrix, row-major float */
    float position[3];    /* eye position (for specular) */
} vgfx3d_camera_params_t;

/*==========================================================================
 * Lighting parameters — set before begin_frame
 *=========================================================================*/

typedef struct
{
    int32_t type; /* 0=directional, 1=point, 2=ambient */
    float direction[3];
    float position[3];
    float color[3];
    float intensity;
    float attenuation;
} vgfx3d_light_params_t;

/*==========================================================================
 * Backend vtable
 *=========================================================================*/

typedef struct vgfx3d_backend
{
    const char *name; /* "software", "metal", "d3d11", "opengl" */

    /* Lifecycle */
    void *(*create_ctx)(vgfx_window_t win, int32_t w, int32_t h);
    void (*destroy_ctx)(void *ctx);

    /* Frame */
    void (*clear)(void *ctx, vgfx_window_t win, float r, float g, float b);
    void (*begin_frame)(void *ctx, const vgfx3d_camera_params_t *cam);
    void (*submit_draw)(void *ctx, vgfx_window_t win,
                        const vgfx3d_draw_cmd_t *cmd,
                        const vgfx3d_light_params_t *lights, int32_t light_count,
                        const float *ambient, int8_t wireframe, int8_t backface_cull);
    void (*end_frame)(void *ctx);
} vgfx3d_backend_t;

/*==========================================================================
 * Backend registry
 *=========================================================================*/

extern const vgfx3d_backend_t vgfx3d_software_backend;

/* Select the best available backend. Tries GPU first, falls back to software. */
const vgfx3d_backend_t *vgfx3d_select_backend(void);

#endif /* VIPER_ENABLE_GRAPHICS */
