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

#include "rt_canvas3d_internal.h"
#include "vgfx.h"
#include <stdint.h>

/*==========================================================================
 * Draw command — submitted between begin_frame/end_frame
 *=========================================================================*/

typedef struct {
    const vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    float model_matrix[16];   /* row-major float */
    float diffuse_color[4];   /* RGBA material color */
    float specular[3];        /* RGB specular color */
    float shininess;          /* specular exponent */
    float alpha;              /* opacity [0.0=invisible, 1.0=opaque] */
    int8_t unlit;             /* skip lighting if true */
    const void *texture;      /* Pixels object (diffuse, slot 0) or NULL */
    const void *normal_map;   /* Pixels (normal map, slot 1) or NULL */
    const void *specular_map; /* Pixels (specular map, slot 2) or NULL */
    const void *emissive_map; /* Pixels (emissive map, slot 3) or NULL */
    float emissive_color[3];  /* emissive color multiplier */
    /* Terrain splat mapping (populated by terrain draw path, NULL otherwise) */
    const void *splat_map;         /* RGBA weight texture (NULL = not terrain) */
    const void *splat_layers[4];   /* Layer textures */
    float splat_layer_scales[4];   /* UV tiling per layer */
    int8_t has_splat;              /* 1 = terrain splat active */
} vgfx3d_draw_cmd_t;

/*==========================================================================
 * Camera parameters — passed to begin_frame
 *=========================================================================*/

typedef struct {
    float view[16];       /* view matrix, row-major float */
    float projection[16]; /* projection matrix, row-major float */
    float position[3];    /* eye position (for specular) */
    /* Distance fog */
    int8_t fog_enabled;
    float fog_near, fog_far;
    float fog_color[3];
} vgfx3d_camera_params_t;

/*==========================================================================
 * Lighting parameters — set before begin_frame
 *=========================================================================*/

typedef struct {
    int32_t type; /* 0=directional, 1=point, 2=ambient, 3=spot */
    float direction[3];
    float position[3];
    float color[3];
    float intensity;
    float attenuation;
    float inner_cos; /* spot: cosine of inner cone angle (full brightness) */
    float outer_cos; /* spot: cosine of outer cone angle (zero brightness) */
} vgfx3d_light_params_t;

/*==========================================================================
 * Backend vtable
 *=========================================================================*/

typedef struct vgfx3d_backend {
    const char *name; /* "software", "metal", "d3d11", "opengl" */

    /* Lifecycle */
    void *(*create_ctx)(vgfx_window_t win, int32_t w, int32_t h);
    void (*destroy_ctx)(void *ctx);

    /* Frame */
    void (*clear)(void *ctx, vgfx_window_t win, float r, float g, float b);
    void (*begin_frame)(void *ctx, const vgfx3d_camera_params_t *cam);
    void (*submit_draw)(void *ctx,
                        vgfx_window_t win,
                        const vgfx3d_draw_cmd_t *cmd,
                        const vgfx3d_light_params_t *lights,
                        int32_t light_count,
                        const float *ambient,
                        int8_t wireframe,
                        int8_t backface_cull);
    void (*end_frame)(void *ctx);

    /* Render target (NULL = render to window) */
    void (*set_render_target)(void *ctx, vgfx3d_rendertarget_t *rt);

    /* Shadow map pass. All three may be NULL if not supported by this backend.
     * shadow_begin: initialize depth buffer, store light VP.
     * shadow_draw: depth-only rasterize one mesh into the shadow map.
     * shadow_end: finalize shadow state for lookup in main pass. */
    void (*shadow_begin)(void *ctx, float *depth_buf, int32_t w, int32_t h,
                         const float *light_vp);
    void (*shadow_draw)(void *ctx, const vgfx3d_draw_cmd_t *cmd);
    void (*shadow_end)(void *ctx, float bias);

    /* Present the final frame to the display. Called once per Flip().
     * For GPU backends, this presents the drawable / swaps the back buffer.
     * NULL = no-op (software backend — vgfx_update handles display). */
    void (*present)(void *ctx);
} vgfx3d_backend_t;

/*==========================================================================
 * Backend registry
 *=========================================================================*/

extern const vgfx3d_backend_t vgfx3d_software_backend;

#if defined(__APPLE__)
extern const vgfx3d_backend_t vgfx3d_metal_backend;
#endif
#if defined(_WIN32)
extern const vgfx3d_backend_t vgfx3d_d3d11_backend;
#endif
#if defined(__linux__)
extern const vgfx3d_backend_t vgfx3d_opengl_backend;
#endif

/* Select the best available backend. Tries GPU first, falls back to software. */
const vgfx3d_backend_t *vgfx3d_select_backend(void);

#endif /* VIPER_ENABLE_GRAPHICS */
