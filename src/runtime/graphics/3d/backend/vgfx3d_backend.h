//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend.h
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

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_postfx3d.h"
#include "vgfx.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>

/*==========================================================================
 * Draw command — submitted between begin_frame/end_frame
 *=========================================================================*/

/// @brief One renderable submission: geometry (with an optional stable cache key),
///   model/previous-model matrices, the full resolved material (colors, PBR factors,
///   texture slots + samplers, alpha/cull state), and optional GPU skinning, morph,
///   instancing, and terrain-splat payloads. Built by Canvas3D, consumed by submit_draw.
typedef struct {
    const vgfx3d_vertex_t *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
    /* Stable identity for backend-side static geometry caches. NULL means the
     * geometry is transient and should use the streaming upload path. */
    const void *geometry_key;
    uint32_t geometry_revision;
    float model_matrix[16];      /* row-major float */
    float prev_model_matrix[16]; /* previous-frame row-major float */
    float diffuse_color[4];      /* RGBA material color */
    float specular[3];           /* RGB specular color */
    float shininess;             /* specular exponent */
    float alpha;                 /* opacity [0.0=invisible, 1.0=opaque] */
    int8_t unlit;                /* skip lighting if true */
    int8_t disable_depth_test;   /* screen-space overlays bypass depth test/write */
    const void *texture;         /* Pixels fallback (diffuse, slot 0) or NULL */
    const void *normal_map;      /* Pixels fallback (normal map, slot 1) or NULL */
    const void *specular_map;    /* Pixels fallback (specular map, slot 2) or NULL */
    const void *emissive_map;    /* Pixels fallback (emissive map, slot 3) or NULL */
    void *texture_asset;         /* TextureAsset3D native source (diffuse, slot 0) or NULL */
    void *normal_map_asset;
    void *specular_map_asset;
    void *emissive_map_asset;
    uint64_t texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int64_t texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int64_t texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    float emissive_color[3];  /* emissive color multiplier */
    float metallic;           /* [0,1] dielectric->metal */
    float roughness;          /* [0,1] smooth->rough */
    float ao;                 /* [0,1] ambient occlusion multiplier */
    float emissive_intensity; /* scalar multiplier applied after emissive color/map */
    float normal_scale;       /* scales tangent-space XY perturbation */
    int8_t additive_blend;    /* use additive blending instead of standard alpha */
    int32_t workflow;         /* RT_MATERIAL3D_WORKFLOW_* */
    int32_t alpha_mode;       /* RT_MATERIAL3D_ALPHA_MODE_* */
    float alpha_cutoff;       /* alpha-mask cutoff */
    int32_t shadow_mode;      /* RT_MATERIAL3D_SHADOW_MODE_* */
    int32_t double_sided;     /* culling disabled when true */
    int32_t texture_wrap_s;   /* RT_MATERIAL3D_TEXTURE_WRAP_* */
    int32_t texture_wrap_t;   /* RT_MATERIAL3D_TEXTURE_WRAP_* */
    int32_t texture_filter;   /* RT_MATERIAL3D_TEXTURE_FILTER_* */
    int32_t texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    float texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][6];
    const void *metallic_roughness_map; /* Pixels fallback (glTF metallic/roughness map) or NULL */
    const void *ao_map;                 /* Pixels fallback (ambient occlusion map) or NULL */
    void *metallic_roughness_map_asset;
    void *ao_map_asset;
    const void *env_map; /* CubeMap3D (environment reflections) or NULL */
    float reflectivity;  /* [0.0=no reflection, 1.0=mirror] */
    /* Terrain splat mapping (populated by terrain draw path, NULL otherwise) */
    const void *splat_map;       /* RGBA weight texture (NULL = not terrain) */
    const void *splat_layers[4]; /* Layer textures */
    float splat_layer_scales[4]; /* UV tiling per layer */
    int8_t has_splat;            /* 1 = terrain splat active */
    /* GPU skeletal skinning (MTL-09): set by rt_skeleton3d.c for GPU path */
    const float *bone_palette;      /* bone_count * 16 floats (4x4 row-major) */
    const float *prev_bone_palette; /* previous-frame palette or NULL */
    int32_t bone_count;             /* number of bones (0 = no skinning) */
    /* GPU morph targets (MTL-10): set by rt_morphtarget3d.c for GPU path */
    const float *morph_deltas;           /* shape_count * vertex_count * 3 floats */
    const float *morph_normal_deltas;    /* shape_count * vertex_count * 3 floats or NULL */
    const float *morph_weights;          /* shape_count floats */
    const float *prev_morph_weights;     /* previous-frame shape_count floats or NULL */
    int32_t morph_shape_count;           /* number of active morph shapes (0 = none) */
    const void *morph_key;               /* stable identity for backend morph-payload caches */
    uint64_t morph_revision;             /* bumps when morph delta payload changes */
    const float *prev_instance_matrices; /* N * 16 floats for instanced motion blur */
    int8_t has_prev_model_matrix;        /* 1 when prev_model_matrix is valid */
    int8_t has_prev_instance_matrices;   /* 1 when prev_instance_matrices matches instance_count */
    int32_t shading_model;  /* 0=BlinnPhong, 1=Toon, 2=PBR, 3=Unlit, 4=Fresnel, 5=Emissive */
    float custom_params[8]; /* user-defined shader parameters */
    float depth_bias;       /* constant depth offset; negative pulls coplanar draws forward */
    float slope_scaled_depth_bias; /* slope-proportional depth offset for steep coplanar polygons */
    int8_t has_alpha_texture;       /* draw-time texture scan found non-opaque alpha */
} vgfx3d_draw_cmd_t;

#define VGFX3D_MAX_DRAW_INDEX_COUNT ((uint32_t)INT_MAX)
#define VGFX3D_DEPTH_BIAS_CONSTANT_SCALE 65536.0f

/// @brief Return the triangle-list index count accepted by all backends, or 0 if invalid.
/// @details GPU APIs do not share the software backend's per-triangle out-of-range guard. This
///          helper defines a single conservative contract: commands must contain complete
///          triangles, fit APIs with signed draw-count parameters, and every referenced vertex
///          must be inside the submitted vertex buffer. Rejecting bad commands before upload avoids
///          undefined GPU fetches that can surface as flickering or exploding triangles.
static inline uint32_t vgfx3d_draw_cmd_validated_index_count(const vgfx3d_draw_cmd_t *cmd) {
    uint32_t count;
    if (!cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 || cmd->index_count < 3)
        return 0;
    if (cmd->index_count > VGFX3D_MAX_DRAW_INDEX_COUNT)
        return 0;
    if ((cmd->index_count % 3u) != 0u)
        return 0;
    count = cmd->index_count;
    for (uint32_t i = 0; i < count; i++) {
        if (cmd->indices[i] >= cmd->vertex_count)
            return 0;
    }
    return count;
}

/// @brief True when @p cmd contains a complete, in-range indexed triangle list.
static inline int vgfx3d_draw_cmd_has_valid_triangles(const vgfx3d_draw_cmd_t *cmd) {
    return vgfx3d_draw_cmd_validated_index_count(cmd) != 0u;
}

/// @brief Convert renderer depth-bias units to backend constant-bias units.
/// @details Material3D exposes a small float bias in renderer units. Backends with
///          implementation-specific constant-bias scales use this conversion so decal/shadow
///          offsets remain comparable across GL, D3D11, Metal, and software.
static inline float vgfx3d_depth_bias_constant_units(float bias) {
    double scaled;
    if (!isfinite(bias))
        return 0.0f;
    scaled = (double)bias * (double)VGFX3D_DEPTH_BIAS_CONSTANT_SCALE;
    if (scaled > (double)INT_MAX)
        return (float)INT_MAX;
    if (scaled < (double)INT_MIN)
        return (float)INT_MIN;
    return (float)scaled;
}

/// @brief Convert renderer depth-bias units to D3D11's integer DepthBias field.
static inline int32_t vgfx3d_depth_bias_d3d11_units(float bias) {
    float scaled = vgfx3d_depth_bias_constant_units(bias);
    if (scaled >= (float)INT_MAX)
        return INT_MAX;
    if (scaled <= (float)INT_MIN)
        return INT_MIN;
    return (int32_t)lrintf(scaled);
}

/// @brief True if the command needs standard (non-additive) alpha blending. Honors an
///   explicit alpha_mode (BLEND/MASK) first, treats alpha-bearing texture content as masked
///   unless the caller explicitly requested blending, and otherwise infers transparency from a
///   sub-1.0 alpha or diffuse alpha.
static inline int vgfx3d_draw_cmd_uses_alpha_blend(const vgfx3d_draw_cmd_t *cmd) {
    if (!cmd)
        return 0;
    if (cmd->additive_blend)
        return 0;
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_BLEND)
        return 1;
    if (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK)
        return 0;
    if (cmd->has_alpha_texture)
        return 0;
    return (cmd->alpha < 0.999f || cmd->diffuse_color[3] < 0.999f) ? 1 : 0;
}

/// @brief True if the command is transparent in any way — additive OR alpha-blended —
///   i.e. it must be drawn in the transparent (depth-sorted, non-depth-writing) pass.
static inline int vgfx3d_draw_cmd_uses_transparent_blend(const vgfx3d_draw_cmd_t *cmd) {
    if (!cmd)
        return 0;
    return cmd->additive_blend || vgfx3d_draw_cmd_uses_alpha_blend(cmd);
}

/*==========================================================================
 * Camera parameters — passed to begin_frame
 *=========================================================================*/

/// @brief Per-frame camera state passed to begin_frame: view/projection matrices, eye
///   position/forward, ortho flag, optional distance fog, and load-existing color/depth
///   flags for overlay/secondary passes.
typedef struct {
    float view[16];       /* view matrix, row-major float */
    float projection[16]; /* projection matrix, row-major float */
    float position[3];    /* eye position (for specular) */
    float forward[3];     /* forward/view direction in world space */
    int8_t is_ortho;      /* 1 = orthographic projection */
    /* Distance fog */
    int8_t fog_enabled;
    float fog_near, fog_far;
    float fog_color[3];
    /* Secondary passes can preserve the previous scene color while resetting
     * depth for overlays or UI. */
    int8_t load_existing_color;
    int8_t load_existing_depth;
} vgfx3d_camera_params_t;

/*==========================================================================
 * Lighting parameters — set before begin_frame
 *=========================================================================*/

/// @brief One light passed to submit_draw: kind (directional/point/ambient/spot), an
///   optional shadow-map slot index, direction/position/color/intensity/attenuation, and
///   spot inner/outer cone cosines.
typedef struct {
    int32_t type;                 /* 0=directional, 1=point, 2=ambient, 3=spot */
    int32_t shadow_index;         /* -1 = unshadowed, otherwise [0, VGFX3D_MAX_SHADOW_LIGHTS) */
    int32_t shadow_cascade_count; /* >1 means shadow_index is the first cascade slot */
    int32_t casts_shadows;
    uintptr_t identity; /* stable CPU light identity used to match shadow slots after packing */
    float direction[3];
    float position[3];
    float color[3];
    float shadow_cascade_splits[VGFX3D_MAX_SHADOW_LIGHTS];
    float intensity;
    float attenuation;
    float inner_cos; /* spot: cosine of inner cone angle (full brightness) */
    float outer_cos; /* spot: cosine of outer cone angle (zero brightness) */
} vgfx3d_light_params_t;

/*==========================================================================
 * Backend vtable
 *=========================================================================*/

/// @brief Renderer abstraction vtable. Each backend (software/Metal/D3D11/OpenGL) fills
///   in these function pointers; Canvas3D dispatches through them without knowing the
///   concrete backend. Core lifecycle/frame/draw hooks must be non-NULL; optional hooks
///   (shadows, skybox, instancing, present, readback, GPU post-FX, layer show/hide) may
///   be NULL, signaling Canvas3D to use its software fallback for that capability.
typedef struct vgfx3d_backend {
    const char *name; /* "software", "metal", "d3d11", "opengl" */

    /* Lifecycle */
    void *(*create_ctx)(vgfx_window_t win, int32_t w, int32_t h);
    void (*destroy_ctx)(void *ctx);

    /* Frame */
    void (*clear)(void *ctx, vgfx_window_t win, float r, float g, float b);
    void (*resize)(void *ctx, int32_t w, int32_t h);
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
     * shadow_begin: initialize the indexed depth target and store that light VP.
     * shadow_draw: depth-only rasterize one mesh into the shadow map.
     * shadow_end: finalize that slot for lookup in the main pass. */
    void (*shadow_begin)(
        void *ctx, int32_t slot, float *depth_buf, int32_t w, int32_t h, const float *light_vp);
    void (*shadow_draw)(void *ctx, const vgfx3d_draw_cmd_t *cmd);
    void (*shadow_end)(void *ctx, int32_t slot, float bias);

    /* Optional skybox pass. When non-NULL, Canvas3D may delegate cubemap skybox
     * rendering to the backend instead of rasterizing it into the software
     * framebuffer. */
    void (*draw_skybox)(void *ctx, const void *cubemap);

    /* Instanced rendering (MTL-13): draw multiple instances in one GPU call.
     * NULL = fallback to N individual submit_draw() calls (software path). */
    void (*submit_draw_instanced)(void *ctx,
                                  vgfx_window_t win,
                                  const vgfx3d_draw_cmd_t *cmd,
                                  const float *instance_matrices, /* N * 16 floats */
                                  int32_t instance_count,
                                  const vgfx3d_light_params_t *lights,
                                  int32_t light_count,
                                  const float *ambient,
                                  int8_t wireframe,
                                  int8_t backface_cull);

    /* Present the final frame to the display. Called once per Flip().
     * For GPU backends, this presents the drawable / swaps the back buffer.
     * NULL = no-op (software backend — vgfx_update handles display). */
    void (*present)(void *ctx);

    /* Optional readback hook for window-backed rendering. When non-NULL, Canvas3D
     * may request the current scene color in RGBA row-major layout. */
    int (*readback_rgba)(void *ctx, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride);

    /* Optional GPU post-processing presentation hook. When non-NULL, Canvas3D
     * skips the CPU postfx pass and lets the backend own the final onscreen
     * composite for the supplied ordered effect chain. */
    void (*present_postfx)(void *ctx, const vgfx3d_postfx_chain_t *postfx);

    /* Optional split GPU post-processing hook. When non-NULL, Canvas3D may ask
     * the backend to composite post-FX into the current presentation target,
     * replay final overlays on top, and then call present(). Backends without
     * this hook keep the legacy present_postfx path. */
    void (*apply_postfx)(void *ctx, const vgfx3d_postfx_chain_t *postfx);

    /* Optional per-frame hint for backends that need to know whether the
     * current window-backed frame will be presented through GPU postfx. */
    void (*set_gpu_postfx_enabled)(void *ctx, int8_t enabled);

    /* Optional latched postfx chain for GPU backends that need the current
     * frame's effect settings outside the immediate present_postfx call, for
     * example to service screenshots from the backend-owned presentation path. */
    void (*set_gpu_postfx_snapshot)(void *ctx, const vgfx3d_postfx_chain_t *postfx);

    /* Show/hide GPU layer. Called from Canvas3D.Begin/End to toggle
     * visibility of the GPU rendering layer (e.g., CAMetalLayer).
     * NULL = no-op (software backend has no GPU layer). */
    void (*show_gpu_layer)(void *ctx);
    void (*hide_gpu_layer)(void *ctx);

    /* Optional streaming/upload controls. Budget is in texture payload bytes
     * uploaded to backend storage per frame; UINT64_MAX means unlimited. */
    void (*set_texture_upload_budget)(void *ctx, uint64_t bytes);
    uint64_t (*get_texture_upload_pending_bytes)(void *ctx);
    /* Optional streaming/upload telemetry. Returns the texture payload bytes
     * uploaded to backend storage during the current frame. NULL = unsupported. */
    uint64_t (*get_texture_upload_bytes)(void *ctx);
    /* Optional GPU timing telemetry. Returns the latest completed backend GPU
     * frame time in microseconds, or 0 when unsupported/not yet available. */
    uint64_t (*get_frame_gpu_time_us)(void *ctx);
    /* Optional native compressed TextureAsset3D capability bits. Return a mask
     * using RT_CANVAS3D_BACKEND_CAP_BC7 / ASTC / ETC2 when native block upload is
     * wired and supported by the active device. */
    int64_t (*get_native_texture_caps)(void *ctx);
} vgfx3d_backend_t;

/*==========================================================================
 * Backend registry
 *=========================================================================*/

/// @brief The always-available CPU software backend (fallback of last resort).
extern const vgfx3d_backend_t vgfx3d_software_backend;

#if defined(__APPLE__)
/// @brief The Metal GPU backend (macOS only).
extern const vgfx3d_backend_t vgfx3d_metal_backend;
#endif
#if defined(_WIN32)
/// @brief The Direct3D 11 GPU backend (Windows only).
extern const vgfx3d_backend_t vgfx3d_d3d11_backend;
#endif
#if defined(__linux__)
/// @brief The OpenGL GPU backend (Linux only).
extern const vgfx3d_backend_t vgfx3d_opengl_backend;
#endif

/// @brief Select the best available backend: try the platform GPU backend first, then
///   fall back to the software backend. Returns a borrowed pointer to a static vtable.
const vgfx3d_backend_t *vgfx3d_select_backend(void);

#endif /* VIPER_ENABLE_GRAPHICS */
