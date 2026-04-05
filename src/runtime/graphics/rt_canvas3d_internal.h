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
#include "vgfx3d_frustum.h"
#include <float.h>
#include <math.h>
#include <stdint.h>

//=============================================================================
// Vertex format (80 bytes — final layout for all phases)
//=============================================================================

typedef struct {
    float pos[3];            /* object-space position */
    float normal[3];         /* vertex normal */
    float uv[2];             /* texture coordinates */
    float color[4];          /* RGBA vertex color */
    float tangent[3];        /* tangent vector (Phase 9) */
    uint8_t bone_indices[4]; /* bone palette indices (Phase 14) */
    float bone_weights[4];   /* blend weights (Phase 14) */
} vgfx3d_vertex_t;           /* 80 bytes */

//=============================================================================
// Mesh3D
//=============================================================================

typedef struct {
    void *vptr;
    vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    uint32_t *indices;
    uint32_t index_count;
    uint32_t index_capacity;
    /* Transient: set by skinning path before draw, zero otherwise */
    const float *bone_palette;      /* bone_count * 16 floats (4x4 row-major) */
    const float *prev_bone_palette; /* previous-frame palette for motion blur */
    int32_t bone_count;             /* 0 = not skinned */
    /* Transient: set by DrawMeshMorphed GPU path before draw, zero otherwise */
    const float *morph_deltas;        /* shape_count * vertex_count * 3 floats */
    const float *morph_normal_deltas; /* shape_count * vertex_count * 3 floats */
    const float *morph_weights;       /* shape_count floats */
    const float *prev_morph_weights;  /* previous-frame weights for motion blur */
    int32_t morph_shape_count;
    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;
    int8_t bounds_dirty;
    void *morph_targets_ref;    /* attached MorphTarget3D (or NULL) */
    uint32_t geometry_revision; /* increments when CPU geometry changes */
} rt_mesh3d;

static inline void rt_mesh3d_reset_bounds(rt_mesh3d *mesh) {
    if (!mesh)
        return;
    mesh->aabb_min[0] = mesh->aabb_min[1] = mesh->aabb_min[2] = 0.0f;
    mesh->aabb_max[0] = mesh->aabb_max[1] = mesh->aabb_max[2] = 0.0f;
    mesh->bsphere_radius = 0.0f;
    mesh->bounds_dirty = 0;
}

static inline void rt_mesh3d_mark_bounds_dirty(rt_mesh3d *mesh) {
    if (mesh)
        mesh->bounds_dirty = 1;
}

static inline void rt_mesh3d_touch_geometry(rt_mesh3d *mesh) {
    if (!mesh)
        return;
    mesh->bounds_dirty = 1;
    if (mesh->geometry_revision == UINT32_MAX)
        mesh->geometry_revision = 1;
    else
        mesh->geometry_revision++;
}

static inline void rt_mesh3d_refresh_bounds(rt_mesh3d *mesh) {
    if (!mesh || !mesh->bounds_dirty)
        return;
    if (!mesh->vertices || mesh->vertex_count == 0) {
        rt_mesh3d_reset_bounds(mesh);
        return;
    }
    vgfx3d_compute_mesh_aabb(mesh->vertices,
                             mesh->vertex_count,
                             sizeof(vgfx3d_vertex_t),
                             mesh->aabb_min,
                             mesh->aabb_max);
    {
        float dx = mesh->aabb_max[0] - mesh->aabb_min[0];
        float dy = mesh->aabb_max[1] - mesh->aabb_min[1];
        float dz = mesh->aabb_max[2] - mesh->aabb_min[2];
        mesh->bsphere_radius = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
    }
    mesh->bounds_dirty = 0;
}

//=============================================================================
// Camera3D
//=============================================================================

typedef struct {
    void *vptr;
    double view[16];       /* view matrix, row-major */
    double projection[16]; /* projection matrix, row-major */
    double eye[3];         /* camera world position */
    double fov;
    double aspect;
    double near_plane;
    double far_plane;
    double fps_yaw;   /* FPS mode: horizontal rotation (degrees) */
    double fps_pitch; /* FPS mode: vertical rotation (degrees, clamped ±89) */
    /* Camera shake state */
    double shake_intensity;
    double shake_duration;
    double shake_decay;
    double shake_offset[3];
    uint32_t shake_seed;
    int8_t is_ortho;   /* 1 = orthographic projection */
    double ortho_size; /* half-extent of ortho view */
} rt_camera3d;

//=============================================================================
// Material3D
//=============================================================================

typedef struct {
    void *vptr;
    double diffuse[4]; /* RGBA diffuse color */
    double specular[3];
    double shininess;
    void *texture;       /* Pixels (diffuse, slot 0) or NULL */
    void *normal_map;    /* Pixels (normal map, slot 1) or NULL */
    void *specular_map;  /* Pixels (specular map, slot 2) or NULL */
    void *emissive_map;  /* Pixels (emissive map, slot 3) or NULL */
    double emissive[3];  /* emissive color multiplier */
    double alpha;        /* opacity [0.0=invisible, 1.0=opaque], default 1.0 */
    void *env_map;       /* CubeMap3D for environment reflections (or NULL) */
    double reflectivity; /* [0.0=no reflection, 1.0=mirror], default 0.0 */
    int8_t unlit;
    int32_t shading_model;   /* 0=BlinnPhong, 1=Toon, 2=reserved, 3=Unlit, 4=Fresnel, 5=Emissive */
    double custom_params[8]; /* user-defined parameters per shading model */
} rt_material3d;

//=============================================================================
// Light3D
//=============================================================================

typedef struct {
    void *vptr;
    int32_t type; /* 0=directional, 1=point, 2=ambient, 3=spot */
    double direction[3];
    double position[3];
    double color[3];
    double intensity;
    double attenuation;
    double inner_cos; /* spot light inner cone cosine (full brightness inside) */
    double outer_cos; /* spot light outer cone cosine (zero brightness outside) */
} rt_light3d;

//=============================================================================
// Canvas3D
//=============================================================================

#define VGFX3D_MAX_LIGHTS 8

/* Forward declaration — defined in vgfx3d_backend.h */
typedef struct vgfx3d_backend vgfx3d_backend_t;

//=============================================================================
// CubeMap3D — 6-face cube map texture for skybox + reflections
//=============================================================================

typedef struct {
    void *vptr;
    void *faces[6];    /* Pixels objects: +X, -X, +Y, -Y, +Z, -Z */
    int64_t face_size; /* width = height per face (must be square) */
} rt_cubemap3d;

//=============================================================================
// RenderTarget3D — offscreen color + depth buffers
//=============================================================================

typedef struct {
    uint8_t *color_buf; /* RGBA pixels (software path) */
    float *depth_buf;   /* float depth buffer */
    int32_t width;
    int32_t height;
    int32_t stride; /* width * 4 */
} vgfx3d_rendertarget_t;

typedef struct {
    void *vptr;
    vgfx3d_rendertarget_t *target;
    int64_t width;
    int64_t height;
} rt_rendertarget3d;

typedef struct {
    void *vptr;
    vgfx_window_t gfx_win; /* underlying vgfx window (owns framebuffer) */
    int32_t width;
    int32_t height;

    /* Backend dispatch */
    const vgfx3d_backend_t *backend; /* vtable (software, metal, d3d11, opengl) */
    void *backend_ctx;               /* opaque backend state */

    /* Frame state */
    int8_t in_frame;         /* 1 = between Begin/End */
    int8_t frame_is_2d;      /* 1 = active frame uses orthographic 2D projection */
    float cached_vp[16];     /* VP matrix cached in begin_frame for debug drawing */
    float cached_cam_pos[3]; /* camera position cached for sort key computation */
    float last_scene_vp[16]; /* most recent 3D VP matrix (preserved across 2D passes) */
    float last_scene_cam_pos[3];
    int8_t has_last_scene_vp;

    /* Deferred draw command queue (for transparency sorting) */
    void *draw_cmds; /* dynamic array of deferred_draw_t */
    int32_t draw_count;
    int32_t draw_capacity;
    void *trans_cmds; /* reusable transparent draw scratch buffer */
    int32_t trans_capacity;

    /* Render target (NULL = render to window) */
    vgfx3d_rendertarget_t *render_target;
    rt_rendertarget3d *render_target_owner; /* retained wrapper for active target */

    /* Lighting */
    rt_light3d *lights[VGFX3D_MAX_LIGHTS];
    float ambient[3];

    /* Skybox */
    rt_cubemap3d *skybox; /* CubeMap3D for background (or NULL) */

    /* Post-processing effect chain (NULL = disabled) */
    void *postfx;

    /* Temporary raw buffers freed at end of frame (e.g., skinned vertex data) */
    void **temp_buffers;
    int32_t temp_buf_count;
    int32_t temp_buf_capacity;

    /* Temporary runtime objects retained until end of frame */
    void **temp_objects;
    int32_t temp_obj_count;
    int32_t temp_obj_capacity;

    /* Reusable text rendering scratch buffers */
    vgfx3d_vertex_t *text_vertices;
    int32_t text_vertex_capacity;
    uint32_t *text_indices;
    int32_t text_index_capacity;

    /* Distance fog */
    int8_t fog_enabled;
    float fog_near;
    float fog_far;
    float fog_color[3];

    /* Shadow mapping */
    int8_t shadows_enabled;
    int32_t shadow_resolution;
    float shadow_bias;
    vgfx3d_rendertarget_t *shadow_rt;
    float shadow_light_vp[16];

    /* Pending terrain splat data (consumed by next draw_mesh call, then cleared) */
    int8_t pending_has_splat;
    const void *pending_splat_map;
    const void *pending_splat_layers[4];
    float pending_splat_layer_scales[4];

    /* Rendering options */
    int8_t wireframe;
    int8_t backface_cull;
    int8_t occlusion_culling;

    /* Timing */
    int64_t frame_serial;
    int64_t last_flip_us;
    int64_t delta_time_ms;
    int64_t dt_max_ms;
    int8_t should_close;

    /* Previous-frame transform history for motion blur */
    void *motion_history;
    int32_t motion_history_count;
    int32_t motion_history_capacity;
} rt_canvas3d;

//=============================================================================
// Mat4 internal access (matches rt_mat4.c layout)
//=============================================================================

typedef struct {
    double m[16]; /* row-major: m[r*4+c] */
} mat4_impl;

void rt_canvas3d_draw_mesh_matrix(void *obj,
                                  void *mesh_obj,
                                  const double *model_matrix,
                                  void *material_obj);
void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                        void *mesh_obj,
                                        const double *model_matrix,
                                        void *material_obj,
                                        const void *motion_key,
                                        const float *prev_bone_palette,
                                        const float *prev_morph_weights);
void rt_canvas3d_queue_instanced_batch(void *canvas_obj,
                                       void *mesh_obj,
                                       void *material_obj,
                                       const float *instance_matrices,
                                       int32_t instance_count,
                                       const float *prev_instance_matrices,
                                       int8_t has_prev_instance_matrices);
int64_t rt_canvas3d_get_frame_serial(void *obj);

#endif /* VIPER_ENABLE_GRAPHICS */
