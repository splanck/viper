//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_canvas3d_internal.h
// Purpose: Internal struct definitions for Viper.Graphics3D types.
//   Shared between rt_canvas3d.c, rt_mesh3d.c, rt_camera3d.c, etc.
//
// Key invariants:
//   - These structs are internal to the runtime; never exposed in public headers.
//   - All object pointers received from user code must be cast to these types.
//   - vgfx3d_vertex_t is 80 bytes, defined upfront for all phases.
//
// Links: rt_canvas3d.h, plans/3d/01-software-renderer.md
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "vgfx.h"
#include <float.h>
#include <stdint.h>

//=============================================================================
// Vertex format (80 bytes — final layout for all phases)
//=============================================================================

typedef struct
{
    float pos[3];            /* object-space position */
    float normal[3];         /* vertex normal */
    float uv[2];             /* texture coordinates */
    float color[4];          /* RGBA vertex color */
    float tangent[3];        /* tangent vector (Phase 9) */
    uint8_t bone_indices[4]; /* bone palette indices (Phase 14) */
    float bone_weights[4];   /* blend weights (Phase 14) */
} vgfx3d_vertex_t; /* 80 bytes */

//=============================================================================
// Mesh3D
//=============================================================================

typedef struct
{
    void *vptr;
    vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    uint32_t *indices;
    uint32_t index_count;
    uint32_t index_capacity;
} rt_mesh3d;

//=============================================================================
// Camera3D
//=============================================================================

typedef struct
{
    void *vptr;
    double view[16];       /* view matrix, row-major */
    double projection[16]; /* projection matrix, row-major */
    double eye[3];         /* camera world position */
    double fov;
    double aspect;
    double near_plane;
    double far_plane;
} rt_camera3d;

//=============================================================================
// Material3D
//=============================================================================

typedef struct
{
    void *vptr;
    double diffuse[4]; /* RGBA diffuse color */
    double specular[3];
    double shininess;
    void *texture; /* Pixels object or NULL */
    int8_t unlit;
} rt_material3d;

//=============================================================================
// Light3D
//=============================================================================

typedef struct
{
    void *vptr;
    int32_t type; /* 0=directional, 1=point, 2=ambient */
    double direction[3];
    double position[3];
    double color[3];
    double intensity;
    double attenuation;
} rt_light3d;

//=============================================================================
// Canvas3D
//=============================================================================

#define VGFX3D_MAX_LIGHTS 8

/* Forward declaration — defined in vgfx3d_backend.h */
typedef struct vgfx3d_backend vgfx3d_backend_t;

//=============================================================================
// RenderTarget3D — offscreen color + depth buffers
//=============================================================================

typedef struct
{
    uint8_t *color_buf; /* RGBA pixels (software path) */
    float *depth_buf;   /* float depth buffer */
    int32_t width;
    int32_t height;
    int32_t stride; /* width * 4 */
} vgfx3d_rendertarget_t;

typedef struct
{
    void *vptr;
    vgfx3d_rendertarget_t *target;
    int64_t width;
    int64_t height;
} rt_rendertarget3d;

typedef struct
{
    void *vptr;
    vgfx_window_t gfx_win; /* underlying vgfx window (owns framebuffer) */
    int32_t width;
    int32_t height;

    /* Backend dispatch */
    const vgfx3d_backend_t *backend; /* vtable (software, metal, d3d11, opengl) */
    void *backend_ctx;               /* opaque backend state */

    /* Frame state */
    int8_t in_frame; /* 1 = between Begin/End */

    /* Render target (NULL = render to window) */
    vgfx3d_rendertarget_t *render_target;
    const vgfx3d_backend_t *render_target_saved_backend; /* saved GPU backend during RTT */
    void *render_target_saved_ctx;                        /* saved GPU context during RTT */

    /* Lighting */
    rt_light3d *lights[VGFX3D_MAX_LIGHTS];
    float ambient[3];

    /* Rendering options */
    int8_t wireframe;
    int8_t backface_cull;

    /* Timing */
    int64_t last_flip_us;
    int64_t delta_time_ms;
    int64_t dt_max_ms;
    int8_t should_close;
} rt_canvas3d;

//=============================================================================
// Mat4 internal access (matches rt_mat4.c layout)
//=============================================================================

typedef struct
{
    double m[16]; /* row-major: m[r*4+c] */
} mat4_impl;

#endif /* VIPER_ENABLE_GRAPHICS */
