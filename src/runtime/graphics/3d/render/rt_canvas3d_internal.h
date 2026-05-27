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
//   - vgfx3d_vertex_t is internal and may evolve with renderer/importer needs.
//
// Links: rt_canvas3d.h, plans/3d/01-software-renderer.md
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_heap.h"
#include "rt_graphics3d_ids.h"
#include "rt_input.h"
#include "vgfx.h"
#include "rt_postfx3d.h"
#include "vgfx3d_frustum.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Vertex format
//=============================================================================

typedef struct {
    float pos[3];            /* object-space position */
    float normal[3];         /* vertex normal */
    float uv[2];             /* TEXCOORD_0 */
    float uv1[2];            /* TEXCOORD_1 (falls back to uv when not authored) */
    float color[4];          /* RGBA vertex color */
    float tangent[4];        /* tangent.xyz + handedness sign in tangent.w */
    uint8_t bone_indices[4]; /* bone palette indices (Phase 14) */
    float bone_weights[4];   /* blend weights (Phase 14) */
} vgfx3d_vertex_t;           /* 92 bytes */

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
    int8_t build_failed;       /* set when a construction/load append fails */
    void *skeleton_ref;         /* attached Skeleton3D (or NULL) */
    void *morph_targets_ref;    /* attached MorphTarget3D (or NULL) */
    uint32_t geometry_revision; /* increments when CPU geometry changes */
} rt_mesh3d;

/// @brief Zero a mesh's cached AABB/bounding-sphere and clear the dirty flag.
static inline void rt_mesh3d_reset_bounds(rt_mesh3d *mesh) {
    if (!mesh)
        return;
    mesh->aabb_min[0] = mesh->aabb_min[1] = mesh->aabb_min[2] = 0.0f;
    mesh->aabb_max[0] = mesh->aabb_max[1] = mesh->aabb_max[2] = 0.0f;
    mesh->bsphere_radius = 0.0f;
    mesh->bounds_dirty = 0;
}

/// @brief Flag a mesh's cached bounds as stale (recomputed lazily on next use).
static inline void rt_mesh3d_mark_bounds_dirty(rt_mesh3d *mesh) {
    if (mesh)
        mesh->bounds_dirty = 1;
}

/// @brief Mark geometry changed: dirties bounds and bumps geometry_revision
///        (wrapping past UINT32_MAX to 1) so GPU buffers know to re-upload.
static inline void rt_mesh3d_touch_geometry(rt_mesh3d *mesh) {
    if (!mesh)
        return;
    mesh->bounds_dirty = 1;
    if (mesh->geometry_revision == UINT32_MAX)
        mesh->geometry_revision = 1;
    else
        mesh->geometry_revision++;
}

/// @brief Recompute a mesh's AABB/bounding sphere if marked dirty.
/// @details No-op when clean; resets to zero bounds when the mesh has no
///          vertices; otherwise calls vgfx3d_compute_mesh_aabb over the
///          vertex buffer and clears the dirty flag.
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
/// @brief Update a camera's cached projection for the given viewport aspect.
void rt_camera3d_sync_render_aspect(void *cam, double aspect);
/// @brief Compute a camera's 4x4 projection matrix into @p out_projection,
///        optionally overriding the aspect ratio (<= 0 keeps the camera's).
void rt_camera3d_get_render_projection(void *cam, double aspect_override, float *out_projection);
/// @brief Internal: advance camera shake by @p dt seconds and refresh the shaken view.
void rt_camera3d_update_shake_for_frame(void *cam, double dt);

//=============================================================================
// Material3D
//=============================================================================

#define RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR 0
#define RT_MATERIAL3D_TEXTURE_SLOT_NORMAL 1
#define RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR 2
#define RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE 3
#define RT_MATERIAL3D_TEXTURE_SLOT_METALLIC_ROUGHNESS 4
#define RT_MATERIAL3D_TEXTURE_SLOT_AO 5
#define RT_MATERIAL3D_TEXTURE_SLOT_COUNT 6

typedef struct {
    void *vptr;
    double diffuse[4]; /* RGBA diffuse color */
    double specular[3];
    double shininess;
    int32_t workflow; /* 0=legacy/Blinn-Phong surface, 1=PBR metallic-roughness */
    void *texture;       /* Pixels (diffuse, slot 0) or NULL */
    void *normal_map;    /* Pixels (normal map, slot 1) or NULL */
    void *specular_map;  /* Pixels (specular map, slot 2) or NULL */
    void *emissive_map;  /* Pixels (emissive map, slot 3) or NULL */
    void *metallic_roughness_map; /* Pixels (glTF metallic/roughness map) or NULL */
    void *ao_map;               /* Pixels (ambient occlusion map) or NULL */
    double emissive[3];  /* emissive color multiplier */
    double metallic;     /* [0,1] dielectric->metal */
    double roughness;    /* [0,1] smooth->rough */
    double ao;           /* [0,1] ambient occlusion multiplier */
    double emissive_intensity; /* scalar multiplier applied after emissive color/map */
    double normal_scale; /* scales tangent-space XY perturbation */
    double alpha;        /* opacity [0.0=invisible, 1.0=opaque], default 1.0 */
    double alpha_cutoff; /* alpha-mask cutoff, default 0.5 */
    void *env_map;       /* CubeMap3D for environment reflections (or NULL) */
    double reflectivity; /* [0.0=no reflection, 1.0=mirror], default 0.0 */
    int8_t unlit;
    int8_t double_sided;
    int8_t additive_blend; /* internal-only: route through additive blend state when true */
    int32_t alpha_mode; /* 0=opaque, 1=mask, 2=blend */
    int8_t alpha_mode_auto; /* true when SetAlpha auto-promoted OPAQUE -> BLEND */
    int32_t texture_wrap_s; /* RT_MATERIAL3D_TEXTURE_WRAP_* for imported material textures */
    int32_t texture_wrap_t;
    int32_t texture_filter; /* RT_MATERIAL3D_TEXTURE_FILTER_* */
    int32_t texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    double texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][6];
    int32_t shading_model;   /* 0=BlinnPhong, 1=Toon, 2=reserved, 3=Unlit, 4=Fresnel, 5=Emissive */
    double custom_params[8]; /* user-defined parameters per shading model */
} rt_material3d;

#define RT_MATERIAL3D_TEXTURE_WRAP_REPEAT 0
#define RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE 1
#define RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT 2

#define RT_MATERIAL3D_TEXTURE_FILTER_LINEAR 0
#define RT_MATERIAL3D_TEXTURE_FILTER_NEAREST 1

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
    int8_t enabled;
} rt_light3d;

//=============================================================================
// Canvas3D
//=============================================================================

#define VGFX3D_MAX_LIGHTS 16
#define VGFX3D_MAX_SHADOW_LIGHTS 2

/* Forward declaration — defined in vgfx3d_backend.h */
typedef struct vgfx3d_backend vgfx3d_backend_t;

//=============================================================================
// CubeMap3D — 6-face cube map texture for skybox + reflections
//=============================================================================

typedef struct {
    void *vptr;
    void *faces[6];    /* Pixels objects: +X, -X, +Y, -Y, +Z, -Z */
    int64_t face_size; /* width = height per face (must be square) */
    uint64_t cache_identity; /* stable cache key generation across allocator reuse */
} rt_cubemap3d;

//=============================================================================
// RenderTarget3D — offscreen color + depth buffers
//=============================================================================

typedef struct vgfx3d_rendertarget vgfx3d_rendertarget_t;
typedef int (*vgfx3d_rendertarget_sync_fn)(void *userdata, vgfx3d_rendertarget_t *target);

typedef enum {
    VGFX3D_RENDERTARGET_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_rendertarget_color_format_t;

struct vgfx3d_rendertarget {
    uint8_t *color_buf; /* RGBA pixels (software path) */
    float *hdr_color_buf; /* linear RGBA32F CPU mirror for HDR GPU readback */
    float *depth_buf;   /* float depth buffer */
    int32_t width;
    int32_t height;
    int32_t stride; /* width * 4 */
    int32_t color_format;
    int8_t color_dirty;
    int8_t hdr_color_valid;
    vgfx3d_rendertarget_sync_fn sync_color;
    void *sync_color_userdata;
};

/// @brief True if the render target uses the HDR (16-bit float) color format.
static inline int vgfx3d_rendertarget_is_hdr(const vgfx3d_rendertarget_t *target) {
    return target && target->color_format == VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F;
}

/// @brief Lazily allocate the 8-bit LDR color buffer (zero-filled).
/// @details No-op if already allocated; fails on invalid dims or overflow.
/// @return 1 if the buffer is available, 0 on failure.
static inline int vgfx3d_rendertarget_ensure_color(vgfx3d_rendertarget_t *target) {
    size_t bytes;
    if (!target)
        return 0;
    if (target->color_buf)
        return 1;
    if (target->width <= 0 || target->height <= 0 || target->stride <= 0)
        return 0;
    if ((size_t)target->height > SIZE_MAX / (size_t)target->stride)
        return 0;
    bytes = (size_t)target->height * (size_t)target->stride;
    target->color_buf = (uint8_t *)malloc(bytes);
    if (!target->color_buf)
        return 0;
    memset(target->color_buf, 0, bytes);
    return 1;
}

/// @brief Lazily allocate the RGBA float HDR color buffer (zero-filled).
/// @details Only valid for HDR-format targets; fails otherwise or on overflow.
/// @return 1 if the HDR buffer is available, 0 on failure.
static inline int vgfx3d_rendertarget_ensure_hdr_color(vgfx3d_rendertarget_t *target) {
    size_t pixel_count;
    size_t float_count;
    if (!vgfx3d_rendertarget_is_hdr(target))
        return 0;
    if (target->hdr_color_buf)
        return 1;
    if (target->width <= 0 || target->height <= 0)
        return 0;
    if ((size_t)target->width > SIZE_MAX / (size_t)target->height)
        return 0;
    pixel_count = (size_t)target->width * (size_t)target->height;
    if (pixel_count > SIZE_MAX / (sizeof(float) * 4u))
        return 0;
    float_count = pixel_count * 4u;
    target->hdr_color_buf = (float *)calloc(float_count, sizeof(float));
    return target->hdr_color_buf != NULL;
}

/// @brief Lazily allocate the float depth buffer, initialized to FLT_MAX.
/// @return 1 if the depth buffer is available, 0 on failure.
static inline int vgfx3d_rendertarget_ensure_depth(vgfx3d_rendertarget_t *target) {
    size_t pixel_count;
    if (!target)
        return 0;
    if (target->depth_buf)
        return 1;
    if (target->width <= 0 || target->height <= 0)
        return 0;
    if ((size_t)target->width > SIZE_MAX / (size_t)target->height)
        return 0;
    pixel_count = (size_t)target->width * (size_t)target->height;
    if (pixel_count > SIZE_MAX / sizeof(float))
        return 0;
    target->depth_buf = (float *)malloc(pixel_count * sizeof(float));
    if (!target->depth_buf)
        return 0;
    for (size_t i = 0; i < pixel_count; i++)
        target->depth_buf[i] = FLT_MAX;
    return 1;
}

/// @brief Pull the latest GPU/backend color into the CPU buffer if dirty.
/// @details Invokes the registered sync_color callback; clears color_dirty on
///          success. @return 1 if color is up to date, 0 on failure.
static inline int vgfx3d_rendertarget_sync_color_if_needed(vgfx3d_rendertarget_t *target) {
    if (!target)
        return 0;
    if (!target->color_dirty)
        return 1;
    if (!target->sync_color)
        return 0;
    if (!target->sync_color(target->sync_color_userdata, target))
        return 0;
    target->color_dirty = 0;
    return 1;
}

/// @brief Detach any color-sync callback and clear dirty/HDR-valid flags
///        (used when a target stops being backed by a live GPU surface).
static inline void vgfx3d_rendertarget_clear_sync(vgfx3d_rendertarget_t *target) {
    if (!target)
        return;
    target->color_dirty = 0;
    target->hdr_color_valid = 0;
    target->sync_color = NULL;
    target->sync_color_userdata = NULL;
}

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
    float cached_cam_forward[3]; /* forward vector cached for skybox + ortho shading */
    int8_t cached_cam_is_ortho;
    float last_scene_vp[16]; /* most recent 3D VP matrix (preserved across 2D passes) */
    float last_scene_cam_pos[3];
    int8_t has_last_scene_vp;

    /* Deferred draw command queue (for transparency sorting) */
    void *draw_cmds; /* dynamic array of deferred_draw_t */
    int32_t draw_count;
    int32_t draw_capacity;
    void *trans_cmds; /* reusable transparent draw scratch buffer */
    int32_t trans_capacity;
    void *final_overlay_cmds; /* dynamic array of deferred_draw_t, replayed after post-FX */
    int32_t final_overlay_count;
    int32_t final_overlay_capacity;
    void **final_overlay_temp_buffers;
    int32_t final_overlay_temp_buf_count;
    int32_t final_overlay_temp_buf_capacity;
    int8_t final_overlay_recording;
    int8_t frame_finalized;
    int8_t frame_presented_by_finalize;

    /* Render target (NULL = render to window) */
    vgfx3d_rendertarget_t *render_target;
    rt_rendertarget3d *render_target_owner; /* retained wrapper for active target */

    /* Lighting */
    rt_light3d *lights[VGFX3D_MAX_LIGHTS];
    rt_light3d *scene_lights[VGFX3D_MAX_LIGHTS];
    int32_t scene_light_count; /* transient, not retained: populated by Scene3D.Draw */
    float ambient[3];

    /* Skybox */
    rt_cubemap3d *skybox; /* CubeMap3D for background (or NULL) */
    uint8_t *skybox_cpu_cache; /* tightly packed RGBA8 fallback skybox */
    int32_t skybox_cpu_cache_w;
    int32_t skybox_cpu_cache_h;
    uint64_t skybox_cpu_cache_generation;
    int8_t skybox_cpu_cache_is_ortho;
    float skybox_cpu_cache_vp[16];
    float skybox_cpu_cache_cam_pos[3];
    float skybox_cpu_cache_forward[3];

    /* Post-processing effect chain (NULL = disabled) */
    void *postfx;
    vgfx3d_postfx_chain_t frame_postfx_chain;
    int8_t frame_gpu_postfx_enabled;
    int8_t frame_postfx_state_latched;
    int32_t quality_requested;
    int32_t quality_active;
    int32_t quality_fallback_reason;
    int8_t quality_fallback;

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
    int32_t shadow_count;
    vgfx3d_rendertarget_t *shadow_rts[VGFX3D_MAX_SHADOW_LIGHTS];
    float shadow_light_vps[VGFX3D_MAX_SHADOW_LIGHTS][16];

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
    int64_t delta_time_us;
    int64_t delta_time_ms;
    int64_t dt_max_ms;
    int32_t input_source;
    int32_t clock_source;
    int64_t synthetic_dt_us;
    int64_t synthetic_key_keys[64];
    int8_t synthetic_key_downs[64];
    int32_t synthetic_key_count;
    uint8_t synthetic_key_state[VIPER_KEY_MAX];
    double synthetic_mouse_dx;
    double synthetic_mouse_dy;
    double synthetic_mouse_wheel_y;
    int64_t synthetic_mouse_buttons;
    int8_t synthetic_mouse_has_buttons;
    uint8_t synthetic_mouse_button_state[VIPER_MOUSE_BUTTON_MAX];
    int8_t should_close;

    /* Previous-frame transform history for motion blur */
    void *motion_history;
    int32_t motion_history_count;
    int32_t motion_history_capacity;
} rt_canvas3d;

/// @brief Validate a Canvas3D handle while optionally preserving internal stack fixtures.
/// @details Production handles must carry the Canvas3D class id. Backend/unit tests
///   may opt into plain stack rt_canvas3d fixtures by compiling graphics runtime code
///   with `RT_G3D_ALLOW_STACK_FIXTURES=1`; production builds should leave it unset so
///   arbitrary non-heap pointers are rejected before any Canvas3D fields are read.
static inline rt_canvas3d *rt_canvas3d_checked_or_stack(void *obj) {
    rt_canvas3d *c = (rt_canvas3d *)rt_g3d_checked_or_null(obj, RT_G3D_CANVAS3D_CLASS_ID);
    if (c)
        return c;
#if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (obj && !rt_heap_is_payload(obj))
        return (rt_canvas3d *)obj;
#endif
    return NULL;
}

/// @brief Validate a Camera3D handle while optionally preserving internal stack fixtures.
static inline rt_camera3d *rt_camera3d_checked_or_stack(void *obj) {
    rt_camera3d *cam = (rt_camera3d *)rt_g3d_checked_or_null(obj, RT_G3D_CAMERA3D_CLASS_ID);
    if (cam)
        return cam;
#if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (obj && !rt_heap_is_payload(obj))
        return (rt_camera3d *)obj;
#endif
    return NULL;
}

//=============================================================================
// Mat4 internal access (matches rt_mat4.c layout)
//=============================================================================

typedef struct {
    double m[16]; /* row-major: m[r*4+c] */
} mat4_impl;

/// @brief Internal: submit a mesh draw with an explicit row-major Mat4 (skips Mat4 wrapper alloc).
void rt_canvas3d_draw_mesh_matrix(void *obj,
                                  void *mesh_obj,
                                  const double *model_matrix,
                                  void *material_obj);
/// @brief Internal: submit a mesh draw with motion-key + previous bone/morph data for TAA + motion blur.
void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                        void *mesh_obj,
                                        const double *model_matrix,
                                        void *material_obj,
                                        const void *motion_key,
                                        const float *prev_bone_palette,
                                        const float *prev_morph_weights);
/// @brief Internal: invalidate and release the cached CPU skybox fallback image.
void rt_canvas3d_invalidate_skybox_cache(rt_canvas3d *canvas);
/// @brief Internal: submit a mesh draw after applying morph targets.
void rt_canvas3d_draw_mesh_matrix_morphed(void *canvas,
                                          void *mesh,
                                          const double *model_matrix,
                                          void *material,
                                          const void *motion_key,
                                          void *morph_targets);
/// @brief Internal: retain a GC-managed object until the current frame is fully submitted.
int rt_canvas3d_add_temp_object(void *obj, void *value);
/// @brief Internal: sample a cubemap direction into linear RGB components.
void rt_cubemap_sample(const rt_cubemap3d *cm,
                       float dx,
                       float dy,
                       float dz,
                       float *out_r,
                       float *out_g,
                       float *out_b);
/// @brief Internal: sample a cubemap reflection with a roughness-dependent blur kernel.
void rt_cubemap_sample_roughness(const rt_cubemap3d *cm,
                                 float dx,
                                 float dy,
                                 float dz,
                                 float roughness,
                                 float *out_r,
                                 float *out_g,
                                 float *out_b);
/// @brief Internal: apply a canvas's active post-processing chain.
void rt_postfx3d_apply_to_canvas(void *canvas);
/// @brief Internal: inject a mouse delta without changing absolute position.
void rt_mouse_force_delta(int64_t dx, int64_t dy);
/// @brief Internal: queue an instanced batch (one draw call rendering many transforms of @p mesh).
void rt_canvas3d_queue_instanced_batch(void *canvas_obj,
                                       void *mesh_obj,
                                       void *material_obj,
                                       const float *instance_matrices,
                                       int32_t instance_count,
                                       const float *prev_instance_matrices,
                                       int8_t has_prev_instance_matrices);
/// @brief Internal: get the monotonic per-frame counter (used to seed motion-blur history).
int64_t rt_canvas3d_get_frame_serial(void *obj);
/// @brief Internal: begin a HUD/overlay sub-pass; @p preserve_existing_color skips the initial clear.
int canvas3d_begin_overlay_frame(rt_canvas3d *c, int8_t preserve_existing_color);
/// @brief Internal: borrow the most recently used scene VP matrix for billboard alignment.
const float *canvas3d_active_scene_vp(const rt_canvas3d *c);
/// @brief Internal: queue a 2D rect into the overlay pass at clip-space position with RGBA color.
int canvas3d_queue_screen_rect(
    rt_canvas3d *c, float x, float y, float w, float h, float r, float g, float b, float a);
/// @brief Internal: queue a 2D line into the overlay pass with thickness and RGBA color.
int canvas3d_queue_screen_line(rt_canvas3d *c,
                               float x0,
                               float y0,
                               float x1,
                               float y1,
                               float thickness,
                               float r,
                               float g,
                               float b,
                               float a);
/// @brief Internal: discard recorded final-overlay commands and their temp buffers.
void canvas3d_clear_final_overlay(rt_canvas3d *c);

#endif /* VIPER_ENABLE_GRAPHICS */
