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
#include "rt_platform.h"
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
    uint32_t validated_index_count; /* non-zero when Canvas3D has already range-validated indices */
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
    int32_t texture_anisotropy;
    int32_t texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_anisotropy[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    float texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][6];
    const void *metallic_roughness_map; /* Pixels fallback (glTF metallic/roughness map) or NULL */
    const void *ao_map;                 /* Pixels fallback (ambient occlusion map) or NULL */
    void *metallic_roughness_map_asset;
    void *ao_map_asset;
    const void *env_map;       /* CubeMap3D (environment reflections) or NULL */
    float reflectivity;        /* [0.0=no reflection, 1.0=mirror] */
    int8_t ibl_env;            /* env_map is the canvas IBL environment: PBR draws use
                                  SH irradiance + prefiltered specular instead of the
                                  flat ambient + legacy reflectivity mix */
    uint32_t lights_revision;  /* monotonic stamp of the light+ambient snapshot this
                                  draw was queued with; consecutive draws sharing a
                                  stamp let backends skip re-uploading scene/light
                                  constants (0 = unknown, always upload) */
    const void *cluster_table; /* vgfx3d_cluster_table_t built for this draw's light
                                  snapshot, or NULL for the flat light loop. Backends
                                  re-upload cluster buffers under the same
                                  lights_revision gate as the light constants. */
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
    /* Plan 10: soft-particle fade distance in world units (0 = hard edges).
     * Blend-mode fragments fade out as they approach the opaque depth
     * snapshot; backends without resolve_opaque_targets ignore it. */
    float soft_particle_fade;
    /* Plan 10: material opts into screen-space reflections (mask written to
     * the motion target's blue channel for the SSR post pass). */
    int8_t ssr_enabled;
    float depth_bias; /* constant depth offset; negative pulls coplanar draws forward */
    float slope_scaled_depth_bias; /* slope-proportional depth offset for steep coplanar polygons */
    int8_t has_alpha_texture;      /* draw-time texture scan found non-opaque alpha */
} vgfx3d_draw_cmd_t;

#define VGFX3D_MAX_DRAW_INDEX_COUNT ((uint32_t)INT_MAX)
/* Renderer depth-bias units are NDC-scale offsets (the software rasterizer adds them to the
 * interpolated depth directly). GPU constant-bias units are multiples of the depth buffer's
 * smallest resolvable step — 2^-24 for D24 and, for float depth in the typical [0.5, 1) scene
 * range, effectively the same magnitude. Scaling by 2^24 therefore makes one renderer unit
 * ~one NDC unit on every backend; the previous 65536 scale left GPU biases ~256x weaker than
 * the software reference, so decal/material biases behaved differently per platform. */
#define VGFX3D_DEPTH_BIAS_CONSTANT_SCALE 16777216.0f

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
    if (cmd->validated_index_count == cmd->index_count)
        return cmd->index_count;
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

/// @brief True if the command needs standard (non-additive) alpha blending.
/// @details Honors the draw command's resolved alpha mode first. Material classification promotes
///   decoded fractional-alpha textures to BLEND and binary cutouts to MASK before commands reach
///   this backend helper; a remaining `has_alpha_texture` with OPAQUE mode is treated as masked for
///   conservative depth/shadow behavior. Scalar material alpha still infers blending.
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

/// @brief SSR mask strength for a draw (0 = not reflective in screen space).
/// @details Written to the motion target's alpha channel; the SSR post pass
///          scales its contribution by this per-pixel value. Materials with an
///          explicit reflectivity reuse it; SSR-only surfaces default to 0.5.
static inline float vgfx3d_draw_cmd_ssr_mask(const vgfx3d_draw_cmd_t *cmd) {
    if (!cmd || !cmd->ssr_enabled)
        return 0.0f;
    return cmd->reflectivity > 0.001f ? cmd->reflectivity : 0.5f;
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
    /* Exponential height fog (Track E doc 07): density scales by
     * exp(-(worldY - base) * falloff); combines with distance fog via
     * combined transmittance, weighted by blend. Shares fog_color. */
    int8_t height_fog_enabled;
    float height_fog_base;
    float height_fog_falloff;
    float height_fog_density;
    float height_fog_blend;
    /* Secondary passes can preserve the previous scene color while resetting
     * depth for overlays or UI. */
    int8_t load_existing_color;
    int8_t load_existing_depth;
    /* Image-based lighting (canvas environment). When ibl_enabled is nonzero,
     * ibl_sh carries SH-9 RGB irradiance coefficients (Ramamoorthi-Hanrahan
     * order, cosine-convolved, 1/pi folded) and PBR draws flagged ibl_env
     * replace the flat ambient term with SH diffuse + prefiltered specular. */
    int8_t ibl_enabled;
    float ibl_intensity;
    float ibl_sh[27];
    /* Shadow filtering/bias parameters (Plan 06). shadow_strength is how dark a
     * fully-occluded texel gets (0 = no darkening, 1 = black); shadow_slope_bias
     * scales the per-texel slope-proportional compare bias; shadow_quality picks
     * the PCF tier (0 = 4 taps, 1 = 8, 2 = 16 rotated-Poisson taps). */
    float shadow_strength;
    float shadow_slope_bias;
    int32_t shadow_quality;
    /* Plan 10: camera clip planes for shader-side depth linearization (soft
     * particles, SSR). Zero/invalid values disable depth-dependent effects. */
    float znear;
    float zfar;
} vgfx3d_camera_params_t;

/*==========================================================================
 * Lighting parameters — set before begin_frame
 *=========================================================================*/

#define VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC 0
#define VGFX3D_SHADOW_PROJECTION_PERSPECTIVE 1
/* Omnidirectional point-light shadow: shadow_index is the FIRST of six
 * consecutive slots (atlas tiles), one 90-degree perspective face per axis in
 * the order +X,-X,+Y,-Y,+Z,-Z. Shaders pick the face by the dominant axis of
 * light->fragment and then sample it exactly like a perspective slot. */
#define VGFX3D_SHADOW_PROJECTION_CUBE 2
#define VGFX3D_SHADOW_CUBE_FACES 6

/*==========================================================================
 * Clustered forward+ light culling (Plan 07)
 *=========================================================================*/

#define VGFX3D_CLUSTER_DIM_X 16
#define VGFX3D_CLUSTER_DIM_Y 9
#define VGFX3D_CLUSTER_DIM_Z 24
#define VGFX3D_CLUSTER_COUNT (VGFX3D_CLUSTER_DIM_X * VGFX3D_CLUSTER_DIM_Y * VGFX3D_CLUSTER_DIM_Z)
#define VGFX3D_MAX_CLUSTER_LIGHT_INDICES 8192

/// @brief CPU-binned froxel light table consumed by GPU backends.
/// @details Built by the canvas per light-snapshot revision (the same stamp that
///   gates scene/light constant uploads): the flattened light array is sorted so
///   directional/ambient lights form a global prefix of length
///   `global_light_count`, and every point/spot light's bounding sphere is
///   conservatively rasterized into a 16x9x24 view froxel grid with exponential
///   Z slicing over [znear, zfar]. `offsets` holds prefix sums into `indices`
///   (light-array indices) per cluster in X-major, then Y, then Z order:
///   cluster = x + y*DIM_X + z*DIM_X*DIM_Y. Per-cluster truncation on index
///   overflow is order-stable and counted in `overflow_count` (never UB).
typedef struct {
    uint32_t lights_revision;   /* snapshot revision this table matches (0 = invalid) */
    int32_t global_light_count; /* directional/ambient prefix length in the light array */
    int32_t binned_light_count; /* point/spot lights considered for binning */
    int32_t overflow_count;     /* dropped cluster entries (diagnostics; 0 in practice) */
    float znear;
    float zfar;
    uint16_t offsets[VGFX3D_CLUSTER_COUNT + 1];
    uint16_t indices[VGFX3D_MAX_CLUSTER_LIGHT_INDICES];
} vgfx3d_cluster_table_t;

/// @brief One light passed to submit_draw: kind (directional/point/ambient/spot), an
///   optional shadow-map slot index, direction/position/color/intensity/attenuation, and
///   spot inner/outer cone cosines.
typedef struct {
    int32_t type;                   /* 0=directional, 1=point, 2=ambient, 3=spot */
    int32_t shadow_index;           /* -1 = unshadowed, otherwise [0, VGFX3D_MAX_SHADOW_LIGHTS) */
    int32_t shadow_cascade_count;   /* >1 means shadow_index is the first cascade slot */
    int32_t shadow_projection_type; /* VGFX3D_SHADOW_PROJECTION_* */
    int32_t casts_shadows;
    uintptr_t identity; /* stable CPU light identity used to match shadow slots after packing */
    float direction[3];
    float position[3];
    float color[3];
    float shadow_cascade_splits[VGFX3D_CSM_SLOTS]; /* consumed as one float4 by shaders */
    float intensity;
    float attenuation;
    float inner_cos; /* spot: cosine of inner cone angle (full brightness) */
    float outer_cos; /* spot: cosine of outer cone angle (zero brightness) */
} vgfx3d_light_params_t;

/// @brief Optional backend telemetry snapshot for renderer diagnostics.
/// @details All counters are monotonic for the lifetime of the backend context unless otherwise
///          stated. Backends that do not implement a field should leave it zero. The structure is
///          intentionally plain integers so Canvas3D can expose selected values through stable
///          runtime properties without allocating or depending on backend-private headers.
typedef struct {
    uint64_t draw_calls;        ///< Successful backend draw calls emitted.
    uint64_t dropped_draws;     ///< Draw commands rejected by the backend before issuing API calls.
    uint64_t mesh_cache_hits;   ///< Static mesh VBO/IBO cache hits.
    uint64_t mesh_cache_misses; ///< Static mesh VBO/IBO cache misses or refreshes.
    uint64_t mesh_stream_uploads;    ///< Transient mesh VBO/IBO uploads.
    uint64_t texture_fallback_binds; ///< Times a fallback texture was bound while real payload was
                                     ///< absent.
    uint64_t direct_presents;    ///< Presents through the native GPU swapchain/default framebuffer.
    uint64_t offscreen_presents; ///< Presents resolved through an offscreen/readback path.
    int32_t present_path;        ///< 0 unknown, 1 direct, 2 offscreen.
    int32_t default_framebuffer_writable; ///< Last default framebuffer writability decision.
} vgfx3d_backend_stats_t;

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

    /* 1 = the backend can render AND sample shadow slots beyond
     * VGFX3D_CSM_SLOTS (its internal atlas tiles). Canvas3D clamps its
     * per-frame shadow slot usage to VGFX3D_CSM_SLOTS when 0, so lights never
     * receive slot indices the backend's shaders cannot resolve. */
    int8_t shadow_atlas_slots;

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

    /* Shadow slots beyond VGFX3D_CSM_SLOTS are "atlas slots": Canvas3D still
     * drives them through shadow_begin/draw/end with per-slot CPU depth
     * buffers, but GPU backends store them as tiles of one internal depth
     * atlas (static 4x2 grid keyed by slot - VGFX3D_CSM_SLOTS) so the general
     * shadow budget can grow without consuming more texture bind points. */

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

    /* Plan 10: snapshot the opaque-pass depth (and any SSR inputs) into
     * sampleable textures at the opaque->transparent seam. Optional; NULL means
     * soft particles/SSR are unsupported and blend draws keep hard edges. */
    void (*resolve_opaque_targets)(void *ctx);

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
    /* Optional backend texture capability bits. Return a mask using
     * RT_CANVAS3D_BACKEND_CAP_BC1/BC3/BC4/BC5/BC7/ASTC/ETC2 for native block
     * upload and RT_CANVAS3D_BACKEND_CAP_ANISOTROPY when sampler anisotropy is
     * supported. */
    int64_t (*get_native_texture_caps)(void *ctx);

    /* Optional backend feature capability bits. Return a mask using
     * RT_CANVAS3D_BACKEND_CAP_* values for features that depend on the concrete
     * device/context rather than only vtable hooks. Canvas3D ORs these bits into
     * BackendCapabilities after checking its generic software and hook-based
     * fallbacks. */
    int64_t (*get_feature_caps)(void *ctx);

    /// @brief Optional backend diagnostics hook.
    /// @details Copies a point-in-time telemetry snapshot into @p out_stats. NULL means the
    ///          backend has no private telemetry beyond Canvas3D's existing frame counters.
    void (*get_backend_stats)(void *ctx, vgfx3d_backend_stats_t *out_stats);

    /* Reversed-Z depth: when non-zero, Canvas3D negates the projection's z row for
     * every pass it sends this backend (scene, view-model, 2D/overlay ortho), and the
     * backend clears depth to 0 with Greater-style compares. Float depth precision then
     * concentrates where standard-Z starves it, eliminating distant z-fighting shimmer.
     * Shadow-map rendering keeps the standard convention on every backend (light VPs are
     * CPU-built and sampled with LessEqual compares). The software backend stays
     * standard as the deterministic golden reference. */
    int8_t reversed_z;

    /* Optional present pacing control. Non-zero synchronizes presentation to the
     * display's vertical blank (the default on every backend); zero presents
     * immediately for lowest latency. NULL = the platform default is fixed. */
    void (*set_vsync)(void *ctx, int8_t enabled);

    /* Optional render-scale control: window-backed scene rendering happens at
     * `scale` times the output size ([0.25, 1]) and is upscaled at presentation.
     * Returns non-zero when the scale was applied. NULL = fixed 1:1 rendering
     * (Canvas3D.TrySetRenderScale reports unsupported). */
    int8_t (*set_render_scale)(void *ctx, float scale);

    /* Optional scene-depth probes for occlusion-aware effects (lens flares).
     * queue_depth_probe registers one NDC point (x, y in [-1, 1]) during frame
     * building and returns its slot id, or -1 when unsupported/full. Slot ids
     * restart at 0 each frame, so a stable per-frame request order yields stable
     * slots. read_depth_probe returns the scene window depth ([0, 1], larger =
     * farther) captured for that slot on a PREVIOUS completed frame, or a
     * negative value while no result is available. GPU backends read the depth
     * back asynchronously (typically one frame of latency, never a pipeline
     * stall); the software backend answers from its CPU z-buffer. */
    int32_t (*queue_depth_probe)(void *ctx, float ndc_x, float ndc_y);
    float (*read_depth_probe)(void *ctx, int32_t slot);
} vgfx3d_backend_t;

/// @brief Maximum scene-depth probes per frame across all users of the hook.
#define VGFX3D_DEPTH_PROBE_MAX 64

/*==========================================================================
 * Backend registry
 *=========================================================================*/

/// @brief The always-available CPU software backend (fallback of last resort).
extern const vgfx3d_backend_t vgfx3d_software_backend;

/// @brief Software backend: read-only NDC depth buffer view for CPU post-FX
///   (NULL when unavailable). Only valid for the software backend's context.
const float *vgfx3d_sw_get_zbuf(void *ctx, int32_t *out_w, int32_t *out_h);

#if RT_PLATFORM_MACOS
/// @brief The Metal GPU backend (macOS only).
extern const vgfx3d_backend_t vgfx3d_metal_backend;
#endif
#if RT_PLATFORM_WINDOWS
/// @brief The Direct3D 11 GPU backend (Windows only).
extern const vgfx3d_backend_t vgfx3d_d3d11_backend;
#endif
#if RT_PLATFORM_LINUX
/// @brief The OpenGL GPU backend (Linux only).
extern const vgfx3d_backend_t vgfx3d_opengl_backend;
#endif

typedef enum {
    VGFX3D_BACKEND_PLATFORM_OTHER = 0,
    VGFX3D_BACKEND_PLATFORM_MACOS = 1,
    VGFX3D_BACKEND_PLATFORM_WINDOWS = 2,
    VGFX3D_BACKEND_PLATFORM_WINDOWS_ARM64 = 3,
    VGFX3D_BACKEND_PLATFORM_LINUX = 4,
} vgfx3d_backend_platform_t;

/// @brief Return the default backend name for a platform policy bucket.
static inline const char *vgfx3d_default_backend_name_for_platform(
    vgfx3d_backend_platform_t platform) {
    switch (platform) {
        case VGFX3D_BACKEND_PLATFORM_MACOS:
            return "metal";
        case VGFX3D_BACKEND_PLATFORM_WINDOWS:
            return "d3d11";
        case VGFX3D_BACKEND_PLATFORM_WINDOWS_ARM64:
            return "software";
        case VGFX3D_BACKEND_PLATFORM_LINUX:
            return "software";
        case VGFX3D_BACKEND_PLATFORM_OTHER:
        default:
            return "software";
    }
}

/// @brief Select the best available backend: try the platform GPU backend first, then
///   fall back to the software backend. Returns a borrowed pointer to a static vtable.
const vgfx3d_backend_t *vgfx3d_select_backend(void);

#endif /* VIPER_ENABLE_GRAPHICS */
