//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_canvas3d_internal.h
// Purpose: Internal struct definitions for Viper.Graphics3D types.
//   Shared between rt_canvas3d.c, rt_mesh3d.c, rt_camera3d.c, etc.
//
// Key invariants:
//   - These structs are internal to the runtime; never exposed in public headers.
//   - All object pointers received from user code must be cast to these types.
//   - vgfx3d_vertex_t is internal and may evolve with renderer/importer needs.
//
// Ownership/Lifetime:
//   - Runtime objects are GC-managed unless explicitly described as stack fixtures.
//   - Internal scratch buffers are owned by their containing runtime object.
//
// Links: rt_canvas3d.h, plans/3d/01-software-renderer.md
//
//===----------------------------------------------------------------------===//
#pragma once

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_graphics3d_ids.h"
#include "rt_heap.h"
#include "rt_input.h"
#include "rt_postfx3d.h"
#include "rt_string.h"
#include "vgfx.h"
#include "vgfx3d_frustum.h"
#include "vgfx3d_skinning_scratch.h"
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VGFX3D_RENDERTARGET_DIM_MAX 8192

//=============================================================================
// Vertex format
//=============================================================================

/// @brief Interleaved 92-byte vertex: position, normal, two UV sets, RGBA color, tangent
///   (with handedness in .w), and 4 bone index/weight pairs for skinning.
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

/// @brief Mesh3D payload: growable vertex/index arrays, cached AABB/bounding-sphere,
///   a geometry revision counter, and transient skinning/morph pointers set per draw.
typedef struct {
    void *vptr;
    vgfx3d_vertex_t *vertices;
    double *positions64; /* optional authoritative double positions for AddVertex-built meshes */
    uint32_t vertex_count;
    uint32_t vertex_capacity;
    uint32_t *indices;
    uint32_t index_count;
    uint32_t index_capacity;
    double *normal_accum_scratch;       /* reusable vertex_count * 3 normal accumulator */
    size_t normal_accum_scratch_values; /* allocated double count for normal_accum_scratch */
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
    const float *morph_bound_deltas_source; /* source pointer for cached raw morph-bound padding */
    uint32_t morph_bound_revision;     /* geometry_revision for cached raw morph-bound padding */
    uint32_t morph_bound_vertex_count; /* vertex_count used by cached raw morph-bound padding */
    int32_t morph_bound_shape_count;   /* shape_count used by cached raw morph-bound padding */
    double morph_bound_pad;            /* cached max raw morph delta length */
    int8_t morph_bound_valid;
    float aabb_min[3];
    float aabb_max[3];
    float bsphere_radius;
    int8_t bounds_dirty;
    int8_t build_failed;               /* set when a construction/load append fails */
    void *skeleton_ref;                /* attached Skeleton3D (or NULL) */
    void *morph_targets_ref;           /* attached MorphTarget3D (or NULL) */
    uint32_t geometry_revision;        /* increments when CPU geometry changes */
    uint32_t tangent_revision;         /* geometry_revision for cached tangent readiness */
    int8_t tangents_ready;             /* true once tangent presence/generation was resolved */
    uint32_t validated_index_revision; /* geometry_revision for cached index validation */
    uint32_t validated_index_count;    /* complete in-range triangle-list count, 0 = invalid */
    uint32_t
        positions64_rebase_revision;  /* geometry_revision for cached double-position rebase test */
    int8_t positions64_rebase_needed; /* cached result for camera-relative vertex rebasing */
    int8_t resident;                  /* false when stream draw residency should skip this mesh */
    uint8_t geometry_batch_depth;
    int8_t geometry_batch_dirty;
    void *physics_bvh_nodes;           /* rt_physics_mesh_bvh_node[], owned by mesh */
    uint32_t *physics_bvh_tri_indices; /* triangle indices into indices[] / 3 */
    uint32_t physics_bvh_revision;
    int32_t physics_bvh_node_count;
    int32_t physics_bvh_tri_count;
} rt_mesh3d;

/// @brief Allocate a Mesh3D object with all runtime bookkeeping initialized but no default
///   vertex/index storage.
/// @details This internal constructor is for import and clone paths that immediately allocate
///          exact-size buffers. Public code should continue using `rt_mesh3d_new`, which reserves
///          small growable arrays for programmatic `AddVertex` / `AddTriangle` construction.
/// @return GC-managed Mesh3D handle, or NULL on allocation failure.
void *rt_mesh3d_new_empty_storage(void);

/// @brief Vertex count safe to read directly — the live count clamped to capacity, 0 when
///   the vertex buffer is absent or empty.
static inline uint32_t rt_mesh3d_safe_vertex_count(const rt_mesh3d *mesh) {
    if (!mesh || !mesh->vertices || mesh->vertex_count == 0 || mesh->vertex_capacity == 0)
        return 0;
    return mesh->vertex_count < mesh->vertex_capacity ? mesh->vertex_count : mesh->vertex_capacity;
}

/// @brief Index count safe to read directly.
/// @details The live count is clamped to capacity, but otherwise preserved. Callers that require a
///   complete triangle-list count should use rt_mesh3d_validated_index_count() instead.
static inline uint32_t rt_mesh3d_safe_index_count(const rt_mesh3d *mesh) {
    if (!mesh || !mesh->indices || mesh->index_count == 0 || mesh->index_capacity == 0)
        return 0;
    return mesh->index_count < mesh->index_capacity ? mesh->index_count : mesh->index_capacity;
}

/// @brief Clamp a mesh's vertex/index counts to their safe values, marking bounds dirty
///   when either count changed.
static inline void rt_mesh3d_repair_geometry_counts(rt_mesh3d *mesh) {
    uint32_t vertex_count;
    uint32_t index_count;
    if (!mesh)
        return;
    vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    index_count = rt_mesh3d_safe_index_count(mesh);
    if (mesh->vertex_count != vertex_count || mesh->index_count != index_count) {
        mesh->vertex_count = vertex_count;
        mesh->index_count = index_count;
        mesh->bounds_dirty = 1;
        mesh->validated_index_revision = 0;
        mesh->validated_index_count = 0;
    }
}

/// @brief Return a cached complete, in-range triangle-list index count for @p mesh.
/// @details The validation scan is paid once per geometry revision. Backends can trust a draw
///   command carrying this exact count and avoid rescanning every index in every pass. Corrupt
///   indices invalidate the cache with a zero count so consumers skip the draw safely. Revision
///   zero is reserved for stack/transient meshes and is always scanned instead of trusting the
///   zero-initialized cache stamp.
static inline uint32_t rt_mesh3d_validated_index_count(rt_mesh3d *mesh) {
    uint32_t vertex_count;
    uint32_t index_count;
    if (!mesh)
        return 0;
    rt_mesh3d_repair_geometry_counts(mesh);
    if (mesh->geometry_revision != 0 && mesh->validated_index_revision == mesh->geometry_revision)
        return mesh->validated_index_count;
    mesh->validated_index_revision = mesh->geometry_revision ? mesh->geometry_revision : 1u;
    mesh->validated_index_count = 0;
    vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    index_count = rt_mesh3d_safe_index_count(mesh);
    index_count -= index_count % 3u;
    if (!mesh->indices || vertex_count == 0 || index_count < 3u)
        return 0;
    for (uint32_t i = 0; i < index_count; ++i) {
        if (mesh->indices[i] >= vertex_count)
            return 0;
    }
    mesh->validated_index_count = index_count;
    return index_count;
}

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

/// @brief Clamp a double-precision mesh coordinate into the float range used by backend AABBs.
/// @details Meshes authored through the runtime can retain authoritative double positions while
///   their GPU vertices store narrowed floats. Bounds should be derived from the double positions
///   so culling remains conservative for large worlds, then clamped to the backend float domain.
static inline float rt_mesh3d_bounds_f32_from_f64(double value) {
    if (!isfinite(value))
        return 0.0f;
    if (value > (double)FLT_MAX)
        return FLT_MAX;
    if (value < (double)-FLT_MAX)
        return -FLT_MAX;
    return (float)value;
}

/// @brief Immediately mark geometry changed: dirties bounds, bumps geometry_revision (wrapping
///        past UINT32_MAX to 1), and invalidates cached tangents. Bypasses batch deferral.
static inline void rt_mesh3d_touch_geometry_now(rt_mesh3d *mesh) {
    if (!mesh)
        return;
    mesh->resident = 1;
    mesh->bounds_dirty = 1;
    if (mesh->geometry_revision == UINT32_MAX)
        mesh->geometry_revision = 1;
    else
        mesh->geometry_revision++;
    mesh->tangents_ready = 0;
    mesh->tangent_revision = 0;
    mesh->validated_index_revision = 0;
    mesh->validated_index_count = 0;
    mesh->positions64_rebase_revision = 0;
    mesh->positions64_rebase_needed = 0;
    mesh->morph_bound_deltas_source = NULL;
    mesh->morph_bound_revision = 0;
    mesh->morph_bound_vertex_count = 0;
    mesh->morph_bound_shape_count = 0;
    mesh->morph_bound_pad = 0.0;
    mesh->morph_bound_valid = 0;
}

/// @brief Mark geometry changed: dirties bounds and bumps geometry_revision
///        (wrapping past UINT32_MAX to 1) so GPU buffers know to re-upload.
static inline void rt_mesh3d_touch_geometry(rt_mesh3d *mesh) {
    if (!mesh)
        return;
    if (mesh->geometry_batch_depth > 0) {
        mesh->geometry_batch_dirty = 1;
        return;
    }
    rt_mesh3d_touch_geometry_now(mesh);
}

/// @brief Open a geometry-edit batch: defers per-edit revision bumps until the batch ends.
/// @details Re-entrant via a depth counter (saturates at UINT8_MAX), so bulk vertex edits trigger
///          a single re-upload instead of one per change. Pair with rt_mesh3d_end_geometry_batch.
static inline void rt_mesh3d_begin_geometry_batch(rt_mesh3d *mesh) {
    if (!mesh || mesh->geometry_batch_depth == UINT8_MAX)
        return;
    mesh->geometry_batch_depth++;
}

/// @brief Close a geometry-edit batch; when the outermost batch closes and edits occurred,
///        applies a single deferred geometry touch.
static inline void rt_mesh3d_end_geometry_batch(rt_mesh3d *mesh) {
    if (!mesh || mesh->geometry_batch_depth == 0)
        return;
    mesh->geometry_batch_depth--;
    if (mesh->geometry_batch_depth == 0 && mesh->geometry_batch_dirty) {
        mesh->geometry_batch_dirty = 0;
        rt_mesh3d_touch_geometry_now(mesh);
    }
}

/// @brief Recompute a mesh's AABB/bounding sphere if marked dirty.
/// @details No-op when clean; resets to zero bounds when the mesh has no
///          vertices; otherwise calls vgfx3d_compute_mesh_aabb over the
///          vertex buffer and clears the dirty flag.
static inline void rt_mesh3d_refresh_bounds(rt_mesh3d *mesh) {
    uint32_t vertex_count;
    if (!mesh || !mesh->bounds_dirty)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    if (!mesh->vertices || vertex_count == 0) {
        rt_mesh3d_reset_bounds(mesh);
        return;
    }
    if (mesh->positions64) {
        double minv[3] = {DBL_MAX, DBL_MAX, DBL_MAX};
        double maxv[3] = {-DBL_MAX, -DBL_MAX, -DBL_MAX};
        for (uint32_t i = 0; i < vertex_count; i++) {
            const double *p = &mesh->positions64[(size_t)i * 3u];
            for (int axis = 0; axis < 3; axis++) {
                double value = isfinite(p[axis]) ? p[axis] : 0.0;
                if (value < minv[axis])
                    minv[axis] = value;
                if (value > maxv[axis])
                    maxv[axis] = value;
            }
        }
        for (int axis = 0; axis < 3; axis++) {
            mesh->aabb_min[axis] = rt_mesh3d_bounds_f32_from_f64(minv[axis]);
            mesh->aabb_max[axis] = rt_mesh3d_bounds_f32_from_f64(maxv[axis]);
        }
    } else {
        vgfx3d_compute_mesh_aabb(
            mesh->vertices, vertex_count, sizeof(vgfx3d_vertex_t), mesh->aabb_min, mesh->aabb_max);
    }
    if (!isfinite(mesh->aabb_min[0]) || !isfinite(mesh->aabb_min[1]) ||
        !isfinite(mesh->aabb_min[2]) || !isfinite(mesh->aabb_max[0]) ||
        !isfinite(mesh->aabb_max[1]) || !isfinite(mesh->aabb_max[2])) {
        rt_mesh3d_reset_bounds(mesh);
        return;
    }
    {
        double dx = (double)mesh->aabb_max[0] - (double)mesh->aabb_min[0];
        double dy = (double)mesh->aabb_max[1] - (double)mesh->aabb_min[1];
        double dz = (double)mesh->aabb_max[2] - (double)mesh->aabb_min[2];
        double radius = 0.5 * sqrt(dx * dx + dy * dy + dz * dz);
        if (!isfinite(radius) || radius < 0.0)
            radius = (double)FLT_MAX;
        mesh->bsphere_radius = radius > (double)FLT_MAX ? FLT_MAX : (float)radius;
    }
    mesh->bounds_dirty = 0;
}

//=============================================================================
// Camera3D
//=============================================================================

/// @brief Camera3D payload: cached view/projection matrices, eye position, perspective
///   or ortho parameters, FPS yaw/pitch, and camera-shake state.
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
    int64_t last_shake_update_token; /* renderer timing token for one shake advance per frame */
    int8_t is_ortho;                 /* 1 = orthographic projection */
    double ortho_size;               /* half-extent of ortho view */
    int8_t pick_cache_valid;
    int8_t pick_cache_is_ortho;
    double pick_cache_aspect;
    double pick_cache_fov;
    double pick_cache_near;
    double pick_cache_far;
    double pick_cache_ortho_size;
    double pick_cache_view[16];
    double pick_cache_inv_vp[16];
} rt_camera3d;

/// @brief Update a camera's cached projection for the given viewport aspect.
void rt_camera3d_sync_render_aspect(void *cam, double aspect);
/// @brief Compute a camera's 4x4 projection matrix into @p out_projection,
///        optionally overriding the aspect ratio (<= 0 keeps the camera's).
void rt_camera3d_get_render_projection(void *cam, double aspect_override, float *out_projection);
/// @brief Internal: advance camera shake by @p dt seconds and refresh the shaken view.
void rt_camera3d_update_shake_for_frame(void *cam, double dt);
/// @brief Internal: advance camera shake at most once for a renderer timing token.
void rt_camera3d_update_shake_for_frame_token(void *cam, double dt, int64_t frame_token);

/// @brief Internal Mesh3D tangent generator for already-validated mesh storage.
void rt_mesh3d_calc_tangents_impl(rt_mesh3d *mesh);

typedef struct {
    char *material_name;
    void *mesh;
} rt_mesh3d_obj_group_t;

int rt_mesh3d_from_obj_groups(rt_string path,
                              rt_mesh3d_obj_group_t **out_groups,
                              int32_t *out_count);
void rt_mesh3d_obj_groups_free(rt_mesh3d_obj_group_t *groups, int32_t count);

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

/// @brief Material3D payload: diffuse/specular/emissive colors, PBR metallic-roughness
///   factors, six texture-map slots with per-slot sampler/UV state, alpha/blend mode,
///   environment reflectivity, shading model, and custom shader params.
typedef struct {
    void *vptr;
    double diffuse[4]; /* RGBA diffuse color */
    double specular[3];
    double shininess;
    int32_t workflow; /* 0=legacy/Blinn-Phong surface, 1=PBR metallic-roughness */
    void *texture;    /* Pixels, TextureAsset3D, or RenderTarget3D source (diffuse, slot 0) */
    void *normal_map; /* Pixels, TextureAsset3D, or RenderTarget3D source (normal map, slot 1) */
    void
        *specular_map; /* Pixels, TextureAsset3D, or RenderTarget3D source (specular map, slot 2) */
    void
        *emissive_map; /* Pixels, TextureAsset3D, or RenderTarget3D source (emissive map, slot 3) */
    void *metallic_roughness_map; /* Pixels, TextureAsset3D, or RenderTarget3D source (glTF
                                     metallic/roughness map) */
    void *ao_map; /* Pixels, TextureAsset3D, or RenderTarget3D source (ambient occlusion map) */
    double emissive[3];        /* emissive color multiplier */
    double metallic;           /* [0,1] dielectric->metal */
    double roughness;          /* [0,1] smooth->rough */
    double ao;                 /* [0,1] ambient occlusion multiplier */
    double emissive_intensity; /* scalar multiplier applied after emissive color/map */
    double normal_scale;       /* scales tangent-space XY perturbation */
    double alpha;              /* opacity [0.0=invisible, 1.0=opaque], default 1.0 */
    double alpha_cutoff;       /* alpha-mask cutoff, default 0.5 */
    void *env_map;             /* CubeMap3D for environment reflections (or NULL) */
    double reflectivity;       /* [0.0=no reflection, 1.0=mirror], default 0.0 */
    int8_t unlit;
    int8_t double_sided;
    int8_t additive_blend;  /* internal-only: route through additive blend state when true */
    int32_t alpha_mode;     /* 0=opaque, 1=mask, 2=blend */
    int8_t alpha_mode_auto; /* true when SetAlpha auto-promoted OPAQUE -> BLEND */
    int32_t shadow_mode;    /* 0=auto, 1=none, 2=cast even when alpha-blended */
    int32_t texture_wrap_s; /* RT_MATERIAL3D_TEXTURE_WRAP_* for imported material textures */
    int32_t texture_wrap_t;
    int32_t texture_filter; /* RT_MATERIAL3D_TEXTURE_FILTER_* */
    int32_t anisotropy;     /* 1=off, otherwise clamped to [1,16] */
    int32_t texture_slot_wrap_s[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_wrap_t[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_filter[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_anisotropy[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    int32_t texture_slot_uv_set[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
    double texture_slot_uv_transform[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][6];
    int32_t shading_model;   /* 0=BlinnPhong, 1=Toon, 2=PBR, 3=Unlit, 4=Fresnel, 5=Emissive */
    double custom_params[8]; /* user-defined parameters per shading model */
    double depth_bias;       /* constant depth offset; negative pulls coplanar geometry forward */
    double slope_scaled_depth_bias; /* additional slope-scaled depth offset for decals/overlays */
    double soft_fade;               /* soft-particle fade distance in world units (0 = off) */
    int8_t ssr_enabled;             /* screen-space reflections opt-in (Plan 10) */
} rt_material3d;

/// @brief Resolve a Material3D texture slot source to the currently resident Pixels fallback.
void *rt_material3d_resolve_texture_pixels(void *texture_ref);
/// @brief Resolve a Material3D texture slot source to a native TextureAsset3D, if any.
void *rt_material3d_resolve_texture_native_asset(void *texture_ref);

#define RT_MATERIAL3D_TEXTURE_WRAP_REPEAT 0
#define RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE 1
#define RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT 2

#define RT_MATERIAL3D_TEXTURE_FILTER_LINEAR 0
#define RT_MATERIAL3D_TEXTURE_FILTER_NEAREST 1

//=============================================================================
// Light3D
//=============================================================================

/// @brief Light3D payload: light kind, direction/position, color/intensity/attenuation,
///   spot cone cosines, an enabled flag, and whether it may claim shadow-map slots.
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
    int8_t casts_shadows;
} rt_light3d;

/* Minimum point/spot attenuation used by constructors and importers to avoid unbounded local
 * lights. Directional and ambient lights keep zero attenuation because they are not distance
 * falloff lights. */
#define RT_LIGHT3D_DEFAULT_ATTENUATION 0.001

//=============================================================================
// Canvas3D
//=============================================================================

#define VGFX3D_FORWARD_LIGHT_LIMIT 16
#define VGFX3D_MAX_LIGHTS 64
/* Total shadow slots: 0..3 are per-texture slots (CSM cascades and/or the
 * highest-priority lights); 4..11 are tiles of the GPU backends' internal
 * shadow atlas (software keeps per-slot buffers). */
#define VGFX3D_MAX_SHADOW_LIGHTS 12
/* CSM cascade slot count. Cascade-semantic sizes (split arrays, cascade
 * clamps) key on this, NOT on the total shadow-slot count, so growing the
 * general shadow budget cannot silently widen the float4 cascade-split
 * payload the shaders consume. */
#define VGFX3D_CSM_SLOTS 4
#define RT_CANVAS3D_EVENT_QUEUE_CAPACITY 128
#define RT_CANVAS3D_MESH_SNAPSHOT_FRAME_BYTE_BUDGET (256ull * 1024ull * 1024ull)
#define RT_CANVAS3D_WORLD_BOUNDS_CACHE_SIZE 1024

typedef struct {
    void *source;
    uint32_t geometry_revision;
    uint32_t vertex_count;
    uint32_t index_count;
    vgfx3d_vertex_t *vertices;
    uint32_t *indices;
    int8_t tangents_generated;
} rt_canvas3d_mesh_snapshot_entry;

/// @brief Per-frame cache entry for copied float payloads used by deferred draw commands.
/// @details Morph weights, raw morph deltas, and bone palettes are still validated against their
/// source arrays on every queued draw, but identical source/count/content tuples in one frame can
/// share a single temp-buffer snapshot. This removes repeated heap copies when a skinned or morphed
/// mesh is submitted more than once before frame cleanup.
typedef struct {
    const float *source;
    size_t count;
    uint64_t content_hash;
    float *snapshot;
} rt_canvas3d_float_snapshot_entry;

/// @brief CPU occlusion history entry keyed by stable draw identity.
/// @details Coarse occlusion culling is intentionally delayed for objects that have only just
///          become covered. Requiring repeated covered results prevents one-frame projected-AABB
///          mistakes from blinking visible triangles out of the scene.
typedef struct {
    uintptr_t key;
    int32_t covered_streak;
    int64_t last_frame_seen;
} rt_canvas3d_occlusion_history_entry;

/// @brief Per-frame duplicate counter for queued draws sharing one occlusion fingerprint.
/// @details CPU occlusion history needs distinct keys for identical repeated submissions. The
///          deferred draw queue is finalized in one pass, and this table tracks how many times each
///          fingerprint has already appeared in the current frame.
typedef struct {
    uintptr_t fingerprint;
    int32_t count;
} rt_canvas3d_occlusion_duplicate_entry;

/// @brief One fixed-slot per-frame world-AABB cache entry.
/// @details The hash narrows lookup, but hits still compare the full mesh pointer plus all
///          sixteen source double matrix components so collisions cannot affect correctness.
typedef struct {
    const void *mesh_key;
    uint64_t matrix_hash;
    double matrix_key[16];
    float world_min[3];
    float world_max[3];
    int8_t occupied;
} rt_canvas3d_world_bounds_cache_entry;

/* Forward declaration — defined in vgfx3d_backend.h */
typedef struct vgfx3d_backend vgfx3d_backend_t;

//=============================================================================
// CubeMap3D — 6-face cube map texture for skybox + reflections
//=============================================================================

/// @brief Number of prefiltered specular mip levels retained for image-based
///   lighting (base 128 halving to 4: 128/64/32/16/8/4).
#define RT_CUBEMAP3D_IBL_MAX_MIPS 6

/// @brief CubeMap3D payload: six square Pixels faces (±X, ±Y, ±Z) plus a cache identity
///   used as a stable GPU-upload key across allocator reuse. When image-based
///   lighting has been prepared (rt_cubemap3d_ensure_ibl), the payload also
///   carries SH-9 irradiance coefficients and a GGX-prefiltered specular mip
///   chain stored as additional Pixels faces.
typedef struct {
    void *vptr;
    void *faces[6];          /* Pixels objects: +X, -X, +Y, -Y, +Z, -Z */
    int64_t face_size;       /* width = height per face (must be square) */
    uint64_t cache_identity; /* stable cache key generation across allocator reuse */
    /* --- IBL payload (valid only when ibl_ready != 0) --- */
    float ibl_sh[27]; /* SH-9 RGB irradiance, cosine-convolved, 1/pi folded */
    void *ibl_mips[RT_CUBEMAP3D_IBL_MAX_MIPS][6]; /* prefiltered Pixels faces  */
    int32_t ibl_mip_count;                        /* 0 until rt_cubemap3d_ensure_ibl */
    int32_t ibl_base_size;                        /* face size of prefiltered mip 0 */
    int8_t ibl_ready;
    uint64_t ibl_identity; /* distinct GPU cache key for the prefiltered chain */
} rt_cubemap3d;

/// @brief Return 1 when @p cubemap is a live CubeMap3D with all six matching square faces.
#ifdef __cplusplus
extern "C" {
#endif
int rt_cubemap3d_is_complete(void *cubemap);
#ifdef __cplusplus
}
#endif

//=============================================================================
// RenderTarget3D — offscreen color + depth buffers
//=============================================================================

typedef struct vgfx3d_rendertarget vgfx3d_rendertarget_t;
typedef int (*vgfx3d_rendertarget_sync_fn)(void *userdata, vgfx3d_rendertarget_t *target);

/// @brief Render-target color format: 8-bit UNORM (LDR) or 16-bit float (HDR).
typedef enum {
    VGFX3D_RENDERTARGET_COLOR_FORMAT_UNORM8 = 0,
    VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F = 1,
} vgfx3d_rendertarget_color_format_t;

/// @brief Offscreen render target: lazily-allocated LDR/HDR color and depth buffers,
///   dimensions/stride/format, dirty flags, and a backend color-sync callback that
///   refreshes the CPU mirror from a GPU surface on readback.
struct vgfx3d_rendertarget {
    uint8_t *color_buf;   /* RGBA pixels (software path) */
    float *hdr_color_buf; /* linear RGBA32F CPU mirror for HDR GPU readback */
    float *depth_buf;     /* float depth buffer */
    int32_t width;
    int32_t height;
    int32_t stride; /* width * 4 */
    int32_t color_format;
    uint64_t cache_identity;  /* stable key generation across allocator pointer reuse */
    uint64_t estimated_bytes; /* color + depth footprint reserved against RT budget */
    int8_t color_dirty;
    int8_t hdr_color_valid;
    /* Bumped when a Canvas3D frame that rendered into this target ends; lets
     * RT-as-material-texture mirrors refresh only on real content changes. */
    uint64_t content_revision;
    vgfx3d_rendertarget_sync_fn sync_color;
    void *sync_color_userdata;
};

/// @brief True if the render target uses the HDR (16-bit float) color format.
static inline int vgfx3d_rendertarget_is_hdr(const vgfx3d_rendertarget_t *target) {
    return target && target->color_format == VGFX3D_RENDERTARGET_COLOR_FORMAT_HDR16F;
}

/// @brief Validate the render target's dimensions and compute its pixel count into
///   *out_pixel_count; returns 0 (count 0) on degenerate, oversized, or overflowing sizes.
static inline int vgfx3d_rendertarget_valid_pixels(const vgfx3d_rendertarget_t *target,
                                                   size_t *out_pixel_count) {
    size_t pixels;
    if (out_pixel_count)
        *out_pixel_count = 0u;
    if (!target || target->width <= 0 || target->height <= 0)
        return 0;
    if (target->width > VGFX3D_RENDERTARGET_DIM_MAX || target->height > VGFX3D_RENDERTARGET_DIM_MAX)
        return 0;
    if ((size_t)target->width > SIZE_MAX / (size_t)target->height)
        return 0;
    pixels = (size_t)target->width * (size_t)target->height;
    if (out_pixel_count)
        *out_pixel_count = pixels;
    return 1;
}

/// @brief Validate the render target's color stride against its width/format and compute the
///   color buffer byte size into *out_bytes; returns 0 on an invalid or overflowing layout.
static inline int vgfx3d_rendertarget_valid_color_layout(const vgfx3d_rendertarget_t *target,
                                                         size_t *out_bytes) {
    size_t min_stride;
    size_t bytes;
    if (out_bytes)
        *out_bytes = 0u;
    if (!target || target->stride <= 0)
        return 0;
    if (!vgfx3d_rendertarget_valid_pixels(target, NULL))
        return 0;
    if ((size_t)target->width > SIZE_MAX / 4u)
        return 0;
    min_stride = (size_t)target->width * 4u;
    if ((size_t)target->stride < min_stride)
        return 0;
    if ((size_t)target->height > SIZE_MAX / (size_t)target->stride)
        return 0;
    bytes = (size_t)target->height * (size_t)target->stride;
    if (out_bytes)
        *out_bytes = bytes;
    return 1;
}

/// @brief Lazily allocate the 8-bit LDR color buffer (zero-filled).
/// @details No-op if already allocated; fails on invalid dims or overflow.
/// @return 1 if the buffer is available, 0 on failure.
static inline int vgfx3d_rendertarget_ensure_color(vgfx3d_rendertarget_t *target) {
    size_t bytes;
    if (!target)
        return 0;
    if (!vgfx3d_rendertarget_valid_color_layout(target, &bytes))
        return 0;
    if (target->color_buf)
        return 1;
    target->color_buf = (uint8_t *)calloc(bytes, 1u);
    return target->color_buf != NULL;
}

/// @brief Lazily allocate the RGBA float HDR color buffer (zero-filled).
/// @details Only valid for HDR-format targets; fails otherwise or on overflow.
/// @return 1 if the HDR buffer is available, 0 on failure.
static inline int vgfx3d_rendertarget_ensure_hdr_color(vgfx3d_rendertarget_t *target) {
    size_t pixel_count;
    size_t float_count;
    if (!vgfx3d_rendertarget_is_hdr(target))
        return 0;
    if (!vgfx3d_rendertarget_valid_pixels(target, &pixel_count))
        return 0;
    if (pixel_count > SIZE_MAX / (sizeof(float) * 4u))
        return 0;
    if (target->hdr_color_buf)
        return 1;
    float_count = pixel_count * 4u;
    target->hdr_color_buf = (float *)calloc(float_count, sizeof(float));
    return target->hdr_color_buf != NULL;
}

/// @brief Flood a depth buffer with FLT_MAX (the "infinitely far" clear value) using
///   exponential-doubling memcpy — write one float, then repeatedly copy the filled
///   prefix over the rest, doubling each pass (memset can't write a 4-byte pattern).
static inline void vgfx3d_rendertarget_fill_depth_max(float *depth, size_t pixel_count) {
    size_t filled = 1u;
    if (!depth || pixel_count == 0)
        return;
    depth[0] = FLT_MAX;
    while (filled < pixel_count) {
        size_t copy_count = filled;
        if (copy_count > pixel_count - filled)
            copy_count = pixel_count - filled;
        memcpy(depth + filled, depth, copy_count * sizeof(float));
        filled += copy_count;
    }
}

/// @brief Lazily allocate the float depth buffer and initialize it to FLT_MAX.
/// @details No-op if already allocated; fails on invalid dims or size overflow.
/// @return 1 if the depth buffer is available, 0 on failure.
static inline int vgfx3d_rendertarget_ensure_depth(vgfx3d_rendertarget_t *target) {
    size_t pixel_count;
    if (!target)
        return 0;
    if (!vgfx3d_rendertarget_valid_pixels(target, &pixel_count))
        return 0;
    if (pixel_count > SIZE_MAX / sizeof(float))
        return 0;
    if (target->depth_buf)
        return 1;
    target->depth_buf = (float *)malloc(pixel_count * sizeof(float));
    if (!target->depth_buf)
        return 0;
    vgfx3d_rendertarget_fill_depth_max(target->depth_buf, pixel_count);
    return 1;
}

/// @brief Pull the latest GPU/backend color into the CPU buffer if dirty.
/// @details Invokes the registered sync_color callback; clears color_dirty on
///          success. @return 1 if color is up to date, 0 on failure.
static inline int vgfx3d_rendertarget_sync_color_if_needed(vgfx3d_rendertarget_t *target) {
    if (!target)
        return 0;
    if (!vgfx3d_rendertarget_valid_color_layout(target, NULL))
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

/// @brief Detach the backend sync callback while preserving the current dirty bit.
/// @details Used when a backend resource is about to be destroyed or reused after a failed
/// readback.
///   The CPU mirror is no longer trustworthy, but clearing `color_dirty` would make `AsPixels`
///   return stale bytes. Leaving dirty set with no callback makes readback fail explicitly.
static inline void vgfx3d_rendertarget_detach_sync_preserve_dirty(vgfx3d_rendertarget_t *target) {
    int8_t dirty;
    if (!target)
        return;
    dirty = target->color_dirty;
    vgfx3d_rendertarget_clear_sync(target);
    target->color_dirty = dirty;
}

/// @brief RenderTarget3D payload: a GC wrapper holding the backing render target plus
///   its width/height and a lazily-created Pixels mirror for material binding.
typedef struct {
    void *vptr;
    vgfx3d_rendertarget_t *target;
    int64_t width;
    int64_t height;
    void *material_pixels;             /* cached Pixels mirror for RT-as-texture binding */
    uint64_t material_pixels_revision; /* content_revision the mirror was refreshed at */
} rt_rendertarget3d;

/// @brief Resolve a RenderTarget3D handle to its material-binding Pixels mirror,
///   refreshing the mirror only when the target's content changed since the last
///   completed frame into it. Returns NULL for invalid handles.
void *rt_rendertarget3d_material_pixels(void *obj);

/// @brief Canvas3D payload — the central 3D rendering context. Holds the window and
///   selected backend vtable+ctx, per-frame state and the deferred draw-command queues
///   (opaque/transparent/overlay) used for transparency sorting, retained lights/skybox/
///   post-FX chain, fog and shadow-map state, pending terrain-splat inputs, per-frame
///   temp buffers/objects, frame timing plus synthetic input/clock state for deterministic
///   runs, and motion-blur transform history.
typedef struct {
    void *vptr;
    vgfx_window_t gfx_win;      /* underlying vgfx window (owns framebuffer) */
    int32_t width;              /* public/logical coordinate width */
    int32_t height;             /* public/logical coordinate height */
    int32_t framebuffer_width;  /* physical backing-pixel width */
    int32_t framebuffer_height; /* physical backing-pixel height */

    /* Backend dispatch */
    const vgfx3d_backend_t *backend;     /* vtable (software, metal, d3d11, opengl) */
    void *backend_ctx;                   /* opaque backend state */
    const char *backend_requested_name;  /* backend selected before runtime fallback */
    int8_t backend_fallback;             /* 1 when Canvas3D fell back to software at creation */
    const char *backend_fallback_reason; /* empty unless backend_fallback is true */

    /* Frame state */
    int8_t in_frame;                       /* 1 = between Begin/End */
    int8_t frame_is_2d;                    /* 1 = active frame uses orthographic 2D projection */
    int8_t frame_is_view_model;            /* 1 = secondary camera-space pass over a fresh depth
                                              buffer (weapon view models); skips skybox + shadows */
    int64_t last_instanced_fallback_count; /* instances routed through the per-draw
                                              fallback (blend/rebase) this frame */
    int64_t last_instanced_fallback_dropped_count; /* instances skipped because the bounded
                                                      fallback queue could not accept them */
    /* Exponential height fog (shares fog_color with distance fog). */
    int8_t height_fog_enabled;
    float height_fog_base;
    float height_fog_falloff;
    float height_fog_density;
    float height_fog_blend;
    float cached_vp[16];            /* VP matrix cached in begin_frame for debug drawing */
    float cached_cam_pos[3];        /* camera position cached for sort key computation */
    double cached_world_cam_pos[3]; /* unre-based world camera position for diagnostics/safety */
    float cached_render_cam_pos[3]; /* camera position in backend render space */
    float cached_cam_forward[3];    /* forward vector cached for skybox + ortho shading */
    float cached_cam_near; /* active camera near clip distance, for stable cascade splits */
    float cached_cam_far;  /* active camera far clip distance, for stable cascade splits */
    int8_t cached_cam_is_ortho;
    int8_t camera_relative_upload;
    double camera_relative_origin[3];
    float last_scene_vp[16]; /* most recent 3D VP matrix (preserved across 2D passes) */
    float last_scene_cam_pos[3];
    int8_t has_last_scene_vp;

    /* Deferred draw command queue (for transparency sorting) */
    void *draw_cmds; /* dynamic array of deferred_draw_t */
    int32_t draw_count;
    int32_t draw_capacity;
    void *trans_cmds; /* reusable transparent draw scratch buffer */
    int32_t trans_capacity;
    void *sort_cmds; /* reusable deferred stable-sort scratch buffer */
    int32_t sort_capacity;
    void *final_overlay_cmds; /* dynamic array of deferred_draw_t, replayed after post-FX */
    int32_t final_overlay_count;
    int32_t final_overlay_capacity;
    void **final_overlay_temp_buffers;
    int32_t final_overlay_temp_buf_count;
    int32_t final_overlay_temp_buf_capacity;
    void **final_overlay_temp_objects; /* GC objs (materials/textures) kept alive until overlay
                                          replay */
    int32_t final_overlay_temp_obj_count;
    int32_t final_overlay_temp_obj_capacity;
    uint8_t *final_overlay_arena; /* Stable vertex/index arena for common final-overlay draws */
    size_t final_overlay_arena_capacity;
    size_t final_overlay_arena_used;
    size_t final_overlay_arena_peak;
    int8_t final_overlay_recording;
    int8_t frame_finalized;
    int8_t frame_presented_by_finalize;

    /* Render target (NULL = render to window) */
    vgfx3d_rendertarget_t *render_target;
    rt_rendertarget3d *render_target_owner; /* retained wrapper for active target */

    /* Lighting */
    rt_light3d *lights[VGFX3D_MAX_LIGHTS];
    rt_light3d *scene_lights[VGFX3D_MAX_LIGHTS];
    rt_light3d scene_light_storage[VGFX3D_MAX_LIGHTS];
    int32_t scene_light_count; /* transient, not retained: populated by Scene3D.Draw */
    float ambient[3];

    /* Image-based lighting: when enabled and the skybox has a prepared IBL
     * payload, PBR draws without an explicit material env map light their
     * ambient term from SH irradiance + the prefiltered specular chain. */
    int8_t ibl_enabled;
    float ibl_intensity;

    /* Light-snapshot revisioning: queued draws are stamped with a monotonic
     * revision; the stamp only advances when the flattened light set or
     * ambient color actually changed since the previous queued draw, letting
     * backends skip re-uploading scene/light constants for runs of draws. */
    uint32_t lights_revision;
    void *last_light_snapshot; /* vgfx3d_light_params_t[VGFX3D_MAX_LIGHTS], lazily allocated */
    float last_light_snapshot_ambient[3];
    int32_t last_light_snapshot_count;
    int8_t last_light_snapshot_valid;

    /* Skybox */
    rt_cubemap3d *skybox;      /* CubeMap3D for background (or NULL) */
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
    void **temp_buffer_set;
    int32_t temp_buffer_set_capacity;
    rt_canvas3d_float_snapshot_entry *float_snapshots;
    int32_t float_snapshot_count;
    int32_t float_snapshot_capacity;
    rt_canvas3d_mesh_snapshot_entry *mesh_snapshots;
    int32_t mesh_snapshot_count;
    int32_t mesh_snapshot_capacity;
    int32_t *mesh_snapshot_hash;
    int32_t mesh_snapshot_hash_capacity;
    /* 0 when the hash table indexes every snapshot (the common case); set to 1 if a
     * hash rebuild fails (OOM) so lookups fall back to the linear scan for safety. */
    int8_t mesh_snapshot_hash_dirty;
    size_t mesh_snapshot_bytes;
    int64_t last_mesh_snapshot_bytes;         /* snapshot bytes copied by the latest ended frame */
    int64_t last_mesh_snapshot_drop_count;    /* snapshot allocations/budget denials this frame */
    int64_t last_mesh_snapshot_dropped_bytes; /* requested snapshot bytes denied this frame */
    vgfx3d_skinning_scratch_t skinning_scratch;

    /* Temporary runtime objects retained until end of frame */
    void **temp_objects;
    int32_t temp_obj_count;
    int32_t temp_obj_capacity;
    void **temp_object_set;
    int32_t temp_object_set_capacity;

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
    int32_t shadow_cascade_count;
    float shadow_slope_bias;
    /* Plan 06: occlusion darkening (0..1; 0.85 reproduces the legacy 0.15 lit floor)
     * and PCF tier (0 = 4 taps, 1 = 8, 2 = 16 rotated-Poisson taps). */
    float shadow_strength;
    int32_t shadow_quality;
    vgfx3d_rendertarget_t *shadow_rts[VGFX3D_MAX_SHADOW_LIGHTS];
    int8_t shadow_rt_owned[VGFX3D_MAX_SHADOW_LIGHTS]; /* slots allocated by Canvas3D itself */
    float shadow_light_vps[VGFX3D_MAX_SHADOW_LIGHTS][16];

    /* Plan 08: overlay 2D clip rect (enqueue-time CPU clipping — applies to
     * screen-space rect/line/image/text queueing while active; backend-neutral). */
    int8_t overlay_clip_active;
    float overlay_clip_x;
    float overlay_clip_y;
    float overlay_clip_w;
    float overlay_clip_h;

    /* Pending terrain splat data (consumed by next draw_mesh call, then cleared) */
    int8_t pending_has_splat;
    const void *pending_splat_map;
    const void *pending_splat_layers[4];
    float pending_splat_layer_scales[4];

    /* Rendering options */
    int8_t wireframe;
    int8_t backface_cull;
    int8_t frustum_culling;
    int8_t occlusion_culling;
    int8_t opaque_depth_sorting;
    float occlusion_depth_margin;
    int32_t occlusion_rect_expand_cells;
    int8_t clustered_lighting;
    /* Plan 07: revision-keyed froxel table ring (vgfx3d_cluster_table_t[count],
     * lazily allocated; entries are invalidated at frame Begin because binning
     * is camera-dependent). */
    void *cluster_tables;
    int32_t cluster_table_count;
    int32_t cluster_table_cursor;
    int64_t cluster_overflow_total;   /* lifetime truncated cluster entries (diagnostics) */
    int32_t cluster_light_budget;     /* per-cluster light-index capacity (8..64) */
    int32_t last_dropped_light_count; /* forward-path lights truncated by the active limit */
    int32_t shadow_budget;            /* general shadow-light slots (1..VGFX3D_MAX_SHADOW_LIGHTS) */
    int32_t last_shadow_slots_used; /* shadow slots rendered in the latest frame (incl. cascades) */
    int32_t last_shadow_requests_dropped; /* shadow-requesting lights denied a slot this frame */
    int32_t last_draw_count;
    int32_t last_occluded_draw_count;
    int32_t last_frustum_culled_draw_count;
    int32_t last_cpu_occluded_draw_count;
    int32_t last_occlusion_candidate_count;
    int64_t last_texture_upload_bytes;
    int64_t last_frame_gpu_time_us;
    int64_t frame_draws_submitted;
    int64_t frame_aabb_transforms;
    int64_t frame_sort_passes;
    int64_t frame_backend_state_changes;
    uint64_t frame_last_backend_state_key;
    int8_t frame_has_backend_state_key;
    rt_canvas3d_world_bounds_cache_entry world_bounds_cache[RT_CANVAS3D_WORLD_BOUNDS_CACHE_SIZE];

    /* Timing */
    int64_t frame_serial;
    int64_t last_flip_us;
    int64_t delta_time_us;
    int64_t delta_time_ms;
    int64_t fps_sample_us[32];
    int64_t fps_sample_total_us;
    int32_t fps_sample_index;
    int32_t fps_sample_count;
    int64_t dt_max_ms;
    int64_t timing_serial;
    int8_t frame_timing_updated_by_poll;
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
    /* Relative (raw) mouse mode applied to the platform window; reconciled
     * against rt_mouse_get_relative_mode() each poll so the runtime input
     * layer stays window-handle-free. */
    int8_t relative_mouse_applied;
    int8_t should_close;
    int64_t last_event_type;
    int64_t event_type_queue[RT_CANVAS3D_EVENT_QUEUE_CAPACITY];
    int32_t event_type_head;
    int32_t event_type_count;
    int64_t event_type_dropped_count; /* lifetime window/input events dropped from the ring */

    /* Previous-frame transform history for motion blur */
    void *motion_history;
    int32_t motion_history_count;
    int32_t motion_history_capacity;
    int32_t *motion_history_hash;
    int32_t motion_history_hash_capacity;
    int32_t motion_history_retention_frames;
    rt_canvas3d_occlusion_history_entry *occlusion_history;
    int32_t occlusion_history_count;
    int32_t occlusion_history_capacity;
    int32_t *occlusion_history_hash;
    int32_t occlusion_history_hash_capacity;
    rt_canvas3d_occlusion_duplicate_entry *occlusion_duplicate_counts;
    int32_t occlusion_duplicate_count_capacity;
    int8_t occlusion_state_valid;
    void *occlusion_last_render_target;
    int32_t occlusion_last_output_width;
    int32_t occlusion_last_output_height;
    double occlusion_last_world_cam_pos[3];
    float occlusion_last_cam_forward[3];
    float occlusion_last_near;
    float occlusion_last_far;
    int8_t occlusion_last_is_ortho;
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

/// @brief Internal Mat4 layout (row-major m[r*4+c]) matching rt_mat4.c, used to read a
///   Mat4 object's matrix without going through the public accessor API.
typedef struct {
    double m[16]; /* row-major: m[r*4+c] */
} mat4_impl;

/// @brief Internal: submit a mesh draw with an explicit row-major Mat4 (skips Mat4 wrapper alloc).
void rt_canvas3d_draw_mesh_matrix(void *obj,
                                  void *mesh_obj,
                                  const double *model_matrix,
                                  void *material_obj);
/// @brief Internal: submit a mesh draw with motion-key + previous bone/morph data for TAA + motion
/// blur.
void rt_canvas3d_draw_mesh_matrix_keyed(void *obj,
                                        void *mesh_obj,
                                        const double *model_matrix,
                                        void *material_obj,
                                        const void *motion_key,
                                        const float *prev_bone_palette,
                                        const float *prev_morph_weights);
/// @brief Internal: submit a mesh draw with explicit local bounds and occlusion safety metadata.
/// @details The explicit bounds path is used by Scene3D/extras that already computed conservative
///   local bounds for dynamic deformation or chunking. `conservative_bounds` preserves frustum
///   culling while disabling exact-coverage assumptions in CPU occlusion. `disable_occlusion`
///   skips both CPU occlusion testing and occluder writes for draws whose coverage is not a solid
///   projection of their AABB. `culling_pad` expands only CPU frustum tests, which is useful for
///   terrain chunks whose edge triangles must survive small CPU/GPU precision differences.
void rt_canvas3d_draw_mesh_matrix_keyed_bounds(void *obj,
                                               void *mesh_obj,
                                               const double *model_matrix,
                                               void *material_obj,
                                               const void *motion_key,
                                               const float *prev_bone_palette,
                                               const float *prev_morph_weights,
                                               const float *local_bounds_min,
                                               const float *local_bounds_max,
                                               int8_t conservative_bounds,
                                               int8_t disable_occlusion,
                                               float culling_pad);
/// @brief Internal: enable camera-relative float upload for large-world Game3D frames.
void rt_canvas3d_set_camera_relative_upload(void *obj, int8_t enabled);
/// @brief Internal: copy the active camera-relative origin; returns 1 when upload rebasing is on.
int rt_canvas3d_get_camera_relative_origin(void *obj, double out_origin[3]);
/// @brief Internal: invalidate and release the cached CPU skybox fallback image.
void rt_canvas3d_invalidate_skybox_cache(rt_canvas3d *canvas);
/// @brief Internal: submit a mesh draw after applying morph targets.
void rt_canvas3d_draw_mesh_matrix_morphed(void *canvas,
                                          void *mesh,
                                          const double *model_matrix,
                                          void *material,
                                          const void *motion_key,
                                          void *morph_targets);
/// @brief Internal: submit a morph-target draw while preserving explicit conservative bounds.
/// @details Scene3D precomputes expanded local bounds for animated/morphed geometry. This variant
///          carries those bounds plus occlusion safety flags through the attached-MorphTarget fast
///          path so morphed vertices are not culled or CPU-occluded against the static mesh AABB.
void rt_canvas3d_draw_mesh_matrix_morphed_bounds(void *canvas,
                                                 void *mesh,
                                                 const double *model_matrix,
                                                 void *material,
                                                 const void *motion_key,
                                                 void *morph_targets,
                                                 const float *local_bounds_min,
                                                 const float *local_bounds_max,
                                                 int8_t conservative_bounds,
                                                 int8_t disable_occlusion);
/// @brief Internal: retain a GC-managed object until the current frame is fully submitted.
int rt_canvas3d_add_temp_object(void *obj, void *value);
/// @brief Internal: sample a cubemap direction into linear RGB components.
void rt_cubemap_sample(
    const rt_cubemap3d *cm, float dx, float dy, float dz, float *out_r, float *out_g, float *out_b);
/// @brief Internal: sample a cubemap reflection with a roughness-dependent blur kernel.
void rt_cubemap_sample_roughness(const rt_cubemap3d *cm,
                                 float dx,
                                 float dy,
                                 float dz,
                                 float roughness,
                                 float *out_r,
                                 float *out_g,
                                 float *out_b);
/// @brief Internal: lazily compute the cubemap's IBL payload (SH-9 irradiance +
///   GGX-prefiltered specular mip chain). Idempotent; returns 1 when ready.
int rt_cubemap3d_ensure_ibl(void *cubemap);
/// @brief Internal: sample the prefiltered specular chain (trilinear across
///   roughness levels). Falls back to rt_cubemap_sample_roughness when the IBL
///   payload has not been prepared.
void rt_cubemap_sample_ibl(const rt_cubemap3d *cm,
                           float dx,
                           float dy,
                           float dz,
                           float roughness,
                           float *out_r,
                           float *out_g,
                           float *out_b);
/// @brief Internal: evaluate SH-9 irradiance coefficients (as stored in
///   rt_cubemap3d.ibl_sh) along a unit normal. Output is linear RGB with the
///   Lambertian 1/pi already folded in (a constant environment of color C
///   evaluates to C for every normal).
void rt_sh9_eval_irradiance(const float sh[27], float nx, float ny, float nz, float *out_rgb);
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
/// @brief Internal: queue an instanced batch with explicit local bounds and occlusion flags.
void rt_canvas3d_queue_instanced_batch_bounds(void *canvas_obj,
                                              void *mesh_obj,
                                              void *material_obj,
                                              const float *instance_matrices,
                                              int32_t instance_count,
                                              const float *prev_instance_matrices,
                                              int8_t has_prev_instance_matrices,
                                              const float *local_bounds_min,
                                              const float *local_bounds_max,
                                              int8_t conservative_bounds,
                                              int8_t disable_occlusion);
/// @brief Internal: get the monotonic per-frame counter (used to seed motion-blur history).
int64_t rt_canvas3d_get_frame_serial(void *obj);
/// @brief Internal: begin a HUD/overlay sub-pass; @p preserve_existing_color skips the initial
/// clear.
int canvas3d_begin_overlay_frame(rt_canvas3d *c, int8_t preserve_existing_color);
/// @brief Internal: borrow the most recently used scene VP matrix for billboard alignment.
const float *canvas3d_active_scene_vp(const rt_canvas3d *c);
/// @brief Internal: queue a 2D rect into the overlay pass at clip-space position with RGBA color.
int canvas3d_queue_screen_rect(
    rt_canvas3d *c, float x, float y, float w, float h, float r, float g, float b, float a);
/// @brief Internal: queue a screen-space textured quad sampling `pixels` over UV (0,0)-(1,1).
int canvas3d_queue_screen_image(rt_canvas3d *c, float x, float y, float w, float h, void *pixels);
/// @brief Internal: register the Canvas3D Game.UI widget draw-ops binding (ADR 0065).
void canvas3d_register_gameui_ops(void);
/* Plan 07: clustered forward+ binning — declarations live in
 * rt_canvas3d_clusters.h (they use backend types this header stays free of). */
/// @brief Internal: queue a screen-space rounded rectangle as a triangle fan (Plan 08).
int canvas3d_queue_screen_round_rect(rt_canvas3d *c,
                                     float x,
                                     float y,
                                     float w,
                                     float h,
                                     float radius,
                                     float r,
                                     float g,
                                     float b,
                                     float a);
/// @brief Internal: queue a screen-space textured quad sampling `pixels` over an explicit
/// normalized UV sub-rect (u0,v0)-(u1,v1). Plan 08 region blits build on this.
int canvas3d_queue_screen_image_uv(rt_canvas3d *c,
                                   float x,
                                   float y,
                                   float w,
                                   float h,
                                   void *pixels,
                                   float u0,
                                   float v0,
                                   float u1,
                                   float v1);
/// @brief Internal: retain a GC object referenced by a final-overlay draw until the overlay
/// replays.
int canvas3d_track_final_overlay_temp_object(rt_canvas3d *c, void *obj);
/// @brief Internal: untrack and release a final-overlay temp object after a queueing failure.
void canvas3d_release_tracked_final_overlay_temp_object(rt_canvas3d *c, void *obj);
/// @brief Internal: apply a height-weighted XZ wind sway to a mesh's vertices in place.
/// @details Base vertices (lowest local-Y) stay planted; displacement scales with
///   (normalized height)^2 along (dir_x, dir_z), modulated by sin(phase). Marks geometry
///   dirty. NULL/degenerate-safe. Exposed (non-static) so wind deformation is unit-testable.
void canvas3d_deform_mesh_wind(
    rt_mesh3d *mesh, double dir_x, double dir_z, double strength, double phase);
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

/// @brief Per-object previous-frame transforms for motion-vector derivation.
typedef struct {
    uintptr_t key;
    float current_model[16];
    float prev_model[16];
    int64_t last_frame_seen;
    int8_t has_current;
    int8_t has_prev;
} canvas_motion_history_t;

// Motion-history (rt_canvas3d_motion.c) + the shared open-addressing hash-table
// utilities, also used by the transient-resource tracker.
uint32_t canvas3d_hash_u64(uintptr_t value);
int32_t canvas3d_next_power_of_two_i32(int32_t value);
/// @brief Drop all retained previous-model matrices after an external coordinate-space shift.
/// @details Floating-origin rebases change every world transform by the same delta. Retaining
///   history across that discontinuity would compare matrices from different origins, producing
///   bogus motion vectors and temporal shimmer.
void canvas3d_clear_motion_history(rt_canvas3d *c);
/// @brief Drop all CPU occlusion covered-streak history after a visibility-space discontinuity.
/// @details CPU occlusion culling is deliberately history-gated. Floating-origin rebases, camera
///          cuts, projection/viewport changes, and render-target switches invalidate the prior
///          projected coverage relation, so retaining covered streaks across them can hide visible
///          triangles for one or more frames.
void canvas3d_clear_occlusion_history(rt_canvas3d *c);
void canvas3d_prune_motion_history(rt_canvas3d *c);
void canvas3d_resolve_previous_model(rt_canvas3d *c,
                                     uintptr_t motion_key,
                                     const float *current_model,
                                     float *out_prev_model,
                                     int8_t *out_has_prev);
uintptr_t canvas3d_mesh_transform_motion_key(const void *mesh_obj,
                                             const void *material_obj,
                                             const void *transform_obj);
uintptr_t canvas3d_instance_motion_key(const void *mesh_obj,
                                       const void *material_obj,
                                       const void *batch_obj,
                                       int32_t instance_count,
                                       int32_t index);

// Shared finite-range check (rt_canvas3d.c) used by the geometry snapshotter.
int canvas3d_double_fits_float(double value);

// Mesh-geometry snapshotting (rt_canvas3d_snapshot.c).
int canvas3d_snapshot_mesh_geometry(rt_canvas3d *c,
                                    const rt_mesh3d *mesh,
                                    vgfx3d_vertex_t **out_vertices,
                                    uint32_t **out_indices);
void canvas3d_compute_vertices_aabb(const vgfx3d_vertex_t *vertices,
                                    uint32_t vertex_count,
                                    float out_min[3],
                                    float out_max[3]);
int canvas3d_snapshot_mesh_geometry_rebased(rt_canvas3d *c,
                                            const rt_mesh3d *mesh,
                                            const double origin[3],
                                            vgfx3d_vertex_t **out_vertices,
                                            uint32_t **out_indices);
int canvas3d_reserve_mesh_snapshot_cache(rt_canvas3d *c, int32_t needed);
void canvas3d_mesh_snapshot_hash_clear(rt_canvas3d *c);
int canvas3d_snapshot_mesh_geometry_cached(rt_canvas3d *c,
                                           const rt_mesh3d *mesh,
                                           void *mesh_obj,
                                           vgfx3d_vertex_t **out_vertices,
                                           uint32_t **out_indices);
int canvas3d_snapshot_mesh_geometry_with_tangents_cached(rt_canvas3d *c,
                                                         const rt_mesh3d *mesh,
                                                         void *mesh_obj,
                                                         vgfx3d_vertex_t **out_vertices,
                                                         uint32_t **out_indices);
int canvas3d_should_snapshot_geometry(const rt_mesh3d *mesh, void *mesh_obj);

// Shared pixel utilities (rt_canvas3d.c) used by the CPU skybox.
uint8_t canvas3d_clamp01_to_u8(float value);
int canvas3d_rgba8_stride_valid(int32_t w, int32_t h, int32_t stride);

// CPU skybox fallback (rt_canvas3d_skybox.c).
int canvas3d_ensure_skybox_cpu_cache(rt_canvas3d *c, int32_t w, int32_t h);
void canvas3d_blit_skybox_cpu_cache(
    rt_canvas3d *c, uint8_t *dst_pixels, int32_t dst_w, int32_t dst_h, int32_t dst_stride);

// Value-sanitizing utilities (rt_canvas3d.c) used by the light flattener.
float canvas3d_clamp01_f64(double value);
int canvas3d_uses_camera_relative_upload(const rt_canvas3d *c);
float canvas3d_sanitize_nonnegative_f64(double value, float fallback);
float canvas3d_sanitize_f64_to_float(double value, float fallback);
float canvas3d_clamp_f64_to_float(double value, double lo, double hi, float fallback);

// Light flattening (rt_canvas3d_lighting.c).
int32_t canvas3d_active_light_limit(rt_canvas3d *c);

// Per-frame transient-resource tracking (rt_canvas3d_tempmgr.c): temp buffers,
// final-overlay temp buffers, and the GC-managed transient-object hash set.
int canvas3d_track_temp_buffer(rt_canvas3d *c, void *buffer);
int canvas3d_untrack_temp_buffer(rt_canvas3d *c, void *buffer);
void canvas3d_release_tracked_temp_buffer(rt_canvas3d *c, void *buffer);
void canvas3d_release_tracked_mesh_snapshot(
    rt_canvas3d *c, void *vertices, size_t vertex_bytes, void *indices, size_t index_bytes);
int canvas3d_track_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer);
int canvas3d_untrack_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer);
void canvas3d_release_tracked_final_overlay_temp_buffer(rt_canvas3d *c, void *buffer);
/// @brief Allocate stable storage from the retained final-overlay vertex/index arena.
/// @details The arena is only used for HUD commands that replay after the normal frame temp
/// cleanup. Returned memory remains valid until @ref canvas3d_clear_final_overlay resets the arena,
/// and the helper deliberately avoids moving existing storage while a final overlay is being
/// recorded so previously queued draw commands keep valid pointers.
/// @param c Canvas that owns the final-overlay arena.
/// @param bytes Number of payload bytes to reserve.
/// @param alignment Power-of-two byte alignment for the returned pointer.
/// @return Pointer to arena storage, or NULL when the request cannot be satisfied without moving
/// existing queued geometry.
void *canvas3d_alloc_final_overlay_arena(rt_canvas3d *c, size_t bytes, size_t alignment);
/// @brief Reset retained final-overlay arena state after overlay replay.
/// @details This does not normally free the arena; it drops the used byte count so the next overlay
/// can reuse the same stable memory. Oversized arenas are released to avoid retaining large
/// transient captures after unusual frames.
/// @param c Canvas whose final-overlay arena should be reset.
void canvas3d_reset_final_overlay_arena(rt_canvas3d *c);
void canvas3d_temp_object_set_clear(rt_canvas3d *c);
int canvas3d_ensure_temp_object_set(rt_canvas3d *c, int32_t count_hint);
int canvas3d_temp_object_set_contains(rt_canvas3d *c, void *obj);
int canvas3d_temp_object_set_insert(rt_canvas3d *c, void *obj);
void canvas3d_rebuild_temp_object_set(rt_canvas3d *c);
int canvas3d_track_temp_object(rt_canvas3d *c, void *obj);
void canvas3d_release_tracked_temp_object(rt_canvas3d *c, void *obj);
void canvas3d_clear_temp_buffers(rt_canvas3d *c);
void canvas3d_clear_temp_objects(rt_canvas3d *c);

#endif /* VIPER_ENABLE_GRAPHICS */
