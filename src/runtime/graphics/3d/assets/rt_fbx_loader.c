//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_fbx_loader.c
// Purpose: Dependency-free binary/ASCII FBX parser and complete scene asset extractor for
//   geometry, materials, skeletons, cameras, lights, object animation, and morph animation.
//
// Key invariants:
//   - Supports FBX versions 7100-7700 (both 32-bit and 64-bit offsets).
//   - Array properties with zlib encoding: strip 2-byte header + 4-byte
//     Adler-32 trailer, then call rt_compress_inflate on raw DEFLATE.
//   - Negative polygon indices mark end-of-polygon (bitwise NOT to decode).
//   - Coordinate system correction applied if source is Z-up.
//   - Ear-clipping triangulation for quads/n-gons, with fan fallback only for
//     degenerate projected polygons.
//   - Skinning palette is reduced to the top 4 (bone, weight) influences per
//     vertex and renormalized to sum to 1.
//
// Ownership/Lifetime:
//   - rt_fbx_asset is GC-managed; finalizer releases every owned mesh, material, skeletal/node
//     animation, camera, morph target, skeleton, and scene root.
//   - Parser scratch state (node tree, connection table, binding tables,
//     mesh remaps) is freed before returning from rt_fbx_load.
//   - Texture references loaded from disk are released after assignment to
//     the materials that retain them.
//
// Links: rt_fbx_loader.h, rt_fbx_loader_morph.inc, rt_fbx_loader_nodeanim.inc,
//   plans/3d/15-fbx-loader.md
//
//===----------------------------------------------------------------------===//

#ifdef ZANNA_ENABLE_GRAPHICS

#include "rt_fbx_loader.h"
#include "rt_asset_error.h"
#include "rt_bytes.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_compress.h"
#include "rt_file_stdio.h"
#include "rt_g3d_ref_slots.h"
#include "rt_gif.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_morphtarget3d_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_textureasset3d.h"
#include "rt_trap.h"
#include "rt_untrusted_count.h"
#include "rt_vec3.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void rt_camera3d_look_at_components(void *obj,
                                           double eye_x,
                                           double eye_y,
                                           double eye_z,
                                           double target_x,
                                           double target_y,
                                           double target_z,
                                           double up_x,
                                           double up_y,
                                           double up_z);
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);

#define RT_FBX_HARD_MAX_FILE_BYTES (1024ull * 1024ull * 1024ull)
#define RT_FBX_DEFAULT_MAX_FILE_BYTES (256ull * 1024ull * 1024ull)
#define RT_FBX_DEFAULT_LOAD_BUDGET_BYTES (1024ull * 1024ull * 1024ull)
#define RT_FBX_MAX_TEXTURE_PATH_BYTES (1024u * 1024u)
#define RT_FBX_MAX_TEXTURE_FILE_BYTES (256u * 1024u * 1024u)

#if defined(_MSC_VER)
#define RT_FBX_THREAD_LOCAL __declspec(thread)
#else
#define RT_FBX_THREAD_LOCAL _Thread_local
#endif

/// @brief Thread-local original path used when a temp FBX file should resolve external textures
/// beside the source asset rather than beside the temp spill file.
static RT_FBX_THREAD_LOCAL rt_string g_fbx_texture_base_override = NULL;

/// @brief Per-load accounting and diagnostics shared by binary/ASCII FBX parsing and extraction.
/// @details Every retained allocation class named by ADR 0173 is charged before allocation. The
///          context also accumulates hash/adjacency probe telemetry without introducing global
///          mutable parser state. A context lives only until its load either publishes an asset or
///          rolls back.
typedef struct fbx_load_context {
    uint64_t budget_limit;  ///< Maximum aggregate charged bytes for this load.
    uint64_t budget_used;   ///< Saturating aggregate charged bytes.
    uint64_t lookup_probes; ///< Object-id and connection-endpoint hash probes.
    int budget_exhausted;   ///< Nonzero after overflow or a charge beyond the limit.
} fbx_load_context_t;

/// @brief One-shot thread-local budget override used only by deterministic CTests.
static RT_FBX_THREAD_LOCAL uint64_t g_fbx_next_load_budget_bytes = 0;
/// @brief Charged bytes observed for the most recent load on the calling thread.
static RT_FBX_THREAD_LOCAL uint64_t g_fbx_last_budget_used_bytes = 0;
/// @brief Hash/adjacency probes observed for the most recent load on the calling thread.
static RT_FBX_THREAD_LOCAL uint64_t g_fbx_last_lookup_probe_count = 0;

/*==========================================================================
 * FBX asset container
 *=========================================================================*/

typedef struct {
    void *vptr;
    void **meshes;
    int32_t mesh_count;
    int32_t mesh_capacity;
    void *skeleton;
    void **animations;
    int32_t animation_count;
    int32_t animation_capacity;
    void **node_animations;
    int32_t node_animation_count;
    int32_t node_animation_capacity;
    void **cameras;
    int32_t camera_count;
    int32_t camera_capacity;
    void **materials;
    int32_t material_count;
    int32_t material_capacity;
    void **morph_targets; // rt_morphtarget3d*[] parallel to meshes[]
    int32_t morph_count;
    int32_t morph_capacity;
    void *scene_root;
} rt_fbx_asset;

typedef struct {
    int32_t *triangle_slots;
    uint32_t triangle_count;
    int32_t slot_count;
    int8_t has_slots;
} fbx_mesh_material_map_t;

typedef struct {
    int64_t id;
    void *mesh;
    fbx_mesh_material_map_t material_map;
} fbx_mesh_binding_t;

typedef struct {
    int64_t id;
    void *material;
} fbx_material_binding_t;

typedef struct {
    int32_t *vertices;
    int32_t count;
    int32_t capacity;
} fbx_vertex_index_list_t;

/// @brief Sparse control-point contribution range for one tessellated surface vertex.
typedef struct {
    uint32_t offset;
    uint32_t count;
} fbx_surface_vertex_remap_t;

typedef struct {
    int64_t id;
    fbx_vertex_index_list_t *control_vertices;
    int32_t control_count;
    fbx_surface_vertex_remap_t *surface_vertices;
    int32_t *surface_control_indices;
    double *surface_control_weights;
    uint32_t surface_vertex_count;
    uint32_t surface_contribution_count;
} fbx_mesh_remap_t;

typedef struct {
    int32_t bone_indices[4];
    double weights[4];
} fbx_skin_influence_t;

typedef struct {
    int64_t model_id;
    int32_t bone_index;
} fbx_bone_binding_t;

/// @brief One FBX BlendShapeChannel mapped into a mesh-local MorphTarget3D shape range.
typedef struct {
    int64_t channel_id;
    int64_t geometry_id;
    int32_t mesh_index;
    int32_t shape_count;
    int32_t *shape_indices;
    double *full_weights;
    double default_percent;
} fbx_morph_channel_binding_t;

#define FBX_NUMERIC_ABS_MAX 1000000000000.0
#define FBX_UV_ABS_MAX 1000000.0
#define FBX_ROTATION_DEG_ABS_MAX 1000000.0
#define FBX_SKIN_WEIGHT_MAX 1000000.0
#define FBX_ANIM_TIME_SECONDS_MAX 100000000.0
#define FBX_ANIM_CURVE_KEYS_MAX 1000000u
/* Keep importer capacity aligned with Skeleton3D. Individual draw palettes remain capped at
 * VGFX3D_MAX_BONES (256) and use per-mesh remapping; the asset-level hierarchy supports 1024. */
#define FBX_MAX_SKELETON_BONES 1024

/// @brief Parse an unsigned decimal byte limit without adding libc conversion dependencies.
/// @details Accepts only ASCII digits, rejects zero, and validates the entire input. Values above
///          @ref RT_FBX_HARD_MAX_FILE_BYTES are reported as that hard cap so administrators can
///          safely provide oversized values without raising the audited maximum.
/// @param text Environment variable text.
/// @param out_limit Receives the parsed and clamped byte limit.
/// @return 1 when @p text is a valid decimal limit, otherwise 0.
static int fbx_parse_file_byte_limit(const char *text, uint64_t *out_limit) {
    if (!text || !*text || !out_limit)
        return 0;

    uint64_t value = 0;
    int clamped = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        uint64_t digit = (uint64_t)(*p - '0');
        if (!clamped) {
            if (value > (RT_FBX_HARD_MAX_FILE_BYTES - digit) / 10u) {
                value = RT_FBX_HARD_MAX_FILE_BYTES;
                clamped = 1;
            } else {
                value = value * 10u + digit;
            }
        }
    }

    if (value == 0)
        return 0;
    *out_limit = value > RT_FBX_HARD_MAX_FILE_BYTES ? RT_FBX_HARD_MAX_FILE_BYTES : value;
    return 1;
}

/// @brief Return the process-configured FBX file-size ceiling in bytes.
/// @details `ZANNA_FBX_MAX_FILE_BYTES` may lower the default ceiling for hosts that process
///          untrusted assets under tighter memory budgets. The hard upper bound remains
///          @c RT_FBX_HARD_MAX_FILE_BYTES so a misconfigured environment cannot raise the
///          loader above its audited allocation limit.
/// @return Maximum readable FBX file size in bytes.
static uint64_t fbx_max_file_bytes(void) {
    const char *env = getenv("ZANNA_FBX_MAX_FILE_BYTES");
    uint64_t parsed = 0;
    if (!env || !*env)
        return RT_FBX_DEFAULT_MAX_FILE_BYTES;
    if (!fbx_parse_file_byte_limit(env, &parsed))
        return RT_FBX_DEFAULT_MAX_FILE_BYTES;
    return parsed;
}

/// @brief Resolve the next load's aggregate memory budget.
/// @details A CTest override is consumed once. Otherwise `ZANNA_FBX_MAX_LOAD_BYTES` may lower, but
///          never raise, the 1 GiB default. Invalid or zero environment text is ignored. Keeping
///          the ceiling independent from the file-size ceiling ensures compressed expansion and
///          parser metadata remain bounded even for a small source file.
/// @return Positive byte ceiling for one FBX load.
static uint64_t fbx_next_load_budget_bytes(void) {
    const char *env;
    uint64_t parsed = 0;
    uint64_t override = g_fbx_next_load_budget_bytes;

    g_fbx_next_load_budget_bytes = 0;
    if (override > 0)
        return override < RT_FBX_DEFAULT_LOAD_BUDGET_BYTES ? override
                                                           : RT_FBX_DEFAULT_LOAD_BUDGET_BYTES;
    env = getenv("ZANNA_FBX_MAX_LOAD_BYTES");
    if (env && *env && fbx_parse_file_byte_limit(env, &parsed) &&
        parsed < RT_FBX_DEFAULT_LOAD_BUDGET_BYTES) {
        return parsed;
    }
    return RT_FBX_DEFAULT_LOAD_BUDGET_BYTES;
}

/// @brief Initialize a per-load budget and reset thread-local test telemetry.
/// @param context Caller-owned load context.
static void fbx_load_context_init(fbx_load_context_t *context) {
    if (!context)
        return;
    memset(context, 0, sizeof(*context));
    context->budget_limit = fbx_next_load_budget_bytes();
    g_fbx_last_budget_used_bytes = 0;
    g_fbx_last_lookup_probe_count = 0;
}

/// @brief Charge a checked element-count allocation to one FBX load before allocating it.
/// @details Both multiplication and addition are overflow checked. Charges are conservative and
///          monotonic: memory released during a failed parse does not restore budget, preventing a
///          malicious file from cycling allocations to bypass the aggregate work/memory ceiling.
/// @param context Active load context.
/// @param count Number of elements to charge.
/// @param element_size Bytes per element.
/// @return Nonzero when the charge fits; zero after marking the context exhausted.
static int fbx_budget_charge(fbx_load_context_t *context, uint64_t count, uint64_t element_size) {
    uint64_t bytes;
    if (!context || context->budget_exhausted)
        return 0;
    if (count != 0 && element_size > UINT64_MAX / count) {
        context->budget_exhausted = 1;
        return 0;
    }
    bytes = count * element_size;
    if (bytes > context->budget_limit || context->budget_used > context->budget_limit - bytes) {
        context->budget_exhausted = 1;
        return 0;
    }
    context->budget_used += bytes;
    g_fbx_last_budget_used_bytes = context->budget_used;
    return 1;
}

/// @brief Record one or more load-local lookup probes with saturation.
/// @param context Active load context; NULL is ignored.
/// @param count Probe increment.
static void fbx_record_lookup_probes(fbx_load_context_t *context, uint64_t count) {
    if (!context)
        return;
    if (UINT64_MAX - context->lookup_probes < count)
        context->lookup_probes = UINT64_MAX;
    else
        context->lookup_probes += count;
    g_fbx_last_lookup_probe_count = context->lookup_probes;
}

/// @brief Lower the aggregate budget of the next FBX load on this thread for CTest injection.
/// @details Zero clears the override. Values above the production default are clamped so this hook
///          cannot weaken the runtime resource limit.
/// @param bytes Requested one-shot byte budget, or zero to restore normal selection.
void rt_fbx_test_set_load_budget_bytes(uint64_t bytes) {
    g_fbx_next_load_budget_bytes = bytes;
}

/// @brief Return aggregate bytes charged by the most recent FBX load on this thread.
uint64_t rt_fbx_test_get_last_budget_used_bytes(void) {
    return g_fbx_last_budget_used_bytes;
}

/// @brief Return object-id and connection-endpoint hash probes from the most recent FBX load.
uint64_t rt_fbx_test_get_last_lookup_probe_count(void) {
    return g_fbx_last_lookup_probe_count;
}

static void fbx_release_ref(void **slot);

/// @brief Return @p value when finite, else @p fallback (scalar sanitizer).
static double fbx_finite_or(double value, double fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp @p value into [lo, hi], substituting @p fallback when non-finite.
static double fbx_clamp_double(double value, double lo, double hi, double fallback) {
    value = fbx_finite_or(value, fallback);
    if (value < lo)
        value = lo;
    if (value > hi)
        value = hi;
    return value;
}

/// @brief Clamp @p value into [-limit, limit], substituting @p fallback when non-finite.
static double fbx_clamp_abs_or(double value, double fallback, double limit) {
    value = fbx_finite_or(value, fallback);
    if (value > limit)
        value = limit;
    if (value < -limit)
        value = -limit;
    return value;
}

/// @brief Sanitize a scale factor to a finite, bounded value, replacing ~zero magnitudes with 1.0.
static double fbx_scale_or_unit(double value) {
    value = fbx_clamp_abs_or(value, 1.0, FBX_NUMERIC_ABS_MAX);
    if (fabs(value) < 1e-12)
        value = 1.0;
    return value;
}

/// @brief Clamp a position triple into the FBX numeric bound; returns 0 (leaving the values
///   untouched) when any lane is non-finite.
static int fbx_sanitize_position3(double *x, double *y, double *z) {
    if (!x || !y || !z || !isfinite(*x) || !isfinite(*y) || !isfinite(*z))
        return 0;
    *x = fbx_clamp_abs_or(*x, 0.0, FBX_NUMERIC_ABS_MAX);
    *y = fbx_clamp_abs_or(*y, 0.0, FBX_NUMERIC_ABS_MAX);
    *z = fbx_clamp_abs_or(*z, 0.0, FBX_NUMERIC_ABS_MAX);
    return 1;
}

/// @brief Normalize a normal triple in place, falling back to +Y when it is non-finite or
///   of ~zero length.
static void fbx_sanitize_normal3(double *x, double *y, double *z) {
    double len2;
    double inv_len;
    if (!x || !y || !z || !isfinite(*x) || !isfinite(*y) || !isfinite(*z)) {
        if (x)
            *x = 0.0;
        if (y)
            *y = 1.0;
        if (z)
            *z = 0.0;
        return;
    }
    len2 = (*x) * (*x) + (*y) * (*y) + (*z) * (*z);
    if (!isfinite(len2) || len2 <= 1e-20) {
        *x = 0.0;
        *y = 1.0;
        *z = 0.0;
        return;
    }
    inv_len = 1.0 / sqrt(len2);
    *x *= inv_len;
    *y *= inv_len;
    *z *= inv_len;
}

/// @brief Clamp a rotation angle in degrees to ±FBX_ROTATION_DEG_ABS_MAX (non-finite → 0).
static double fbx_sanitize_rotation_degrees(double value) {
    return fbx_clamp_abs_or(value, 0.0, FBX_ROTATION_DEG_ABS_MAX);
}

/// @brief Clamp a (count, capacity) pair to a safe element count (0 when invalid, else min).
static int32_t fbx_asset_safe_count(void **items, int32_t count, int32_t capacity) {
    if (!items || count <= 0 || capacity <= 0)
        return 0;
    if (count > capacity)
        return capacity;
    return count;
}

/// @brief Ensure a growable reference array holds @p needed slots, doubling capacity and
///   zero-filling the new slots; returns 0 on overflow or allocation failure.
static int fbx_asset_reserve_ref_array(void ***items, int32_t *capacity, int32_t needed) {
    int32_t old_capacity;
    int32_t new_capacity;
    void **grown;
    if (!items || !capacity || needed < 0)
        return 0;
    if (!*items && *capacity > 0)
        *capacity = 0;
    if (*capacity < 0)
        *capacity = 0;
    if (needed <= *capacity)
        return 1;
    old_capacity = *capacity;
    new_capacity = old_capacity > 0 ? old_capacity : 4;
    while (new_capacity < needed) {
        if (new_capacity > INT32_MAX / 2)
            return 0;
        new_capacity *= 2;
    }
    if ((size_t)new_capacity > SIZE_MAX / sizeof(void *))
        return 0;
    grown = (void **)realloc(*items, (size_t)new_capacity * sizeof(void *));
    if (!grown)
        return 0;
    if (new_capacity > old_capacity)
        memset(grown + old_capacity, 0, (size_t)(new_capacity - old_capacity) * sizeof(*grown));
    *items = grown;
    *capacity = new_capacity;
    return 1;
}

/// @brief Release every reference in a ref array (over its safe count) and free the backing
///   storage, resetting the count/capacity to zero.
static void fbx_asset_release_ref_array(void ***items, int32_t *count, int32_t *capacity) {
    void **array = items ? *items : NULL;
    int32_t safe_count = fbx_asset_safe_count(array, count ? *count : 0, capacity ? *capacity : 0);
    if (array) {
        for (int32_t i = 0; i < safe_count; i++)
            fbx_release_ref(&array[i]);
        free(array);
    }
    if (items)
        *items = NULL;
    if (count)
        *count = 0;
    if (capacity)
        *capacity = 0;
}

// clang-format off
typedef struct fbx_constraint_pose fbx_constraint_pose_t;
static const double *fbx_constraint_static_global_for_id(const fbx_constraint_pose_t *pose,
                                                         int64_t model_id);

#include "rt_fbx_loader_parse.inc"
#include "rt_fbx_loader_ascii.inc"
#include "rt_fbx_loader_geometry.inc"
#include "rt_fbx_loader_scene.inc"
#include "rt_fbx_loader_constraints.inc"
#include "rt_fbx_loader_skeleton.inc"
#include "rt_fbx_loader_anim.inc"
#include "rt_fbx_loader_nodeanim.inc"
#include "rt_fbx_loader_loader.inc"
// clang-format on

/*==========================================================================
 * FBX asset accessors
 *=========================================================================*/

/// @brief Get the number of meshes extracted from the FBX file.
int64_t rt_fbx_mesh_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity) : 0;
}

/// @brief Get a mesh by index from the loaded FBX asset.
void *rt_fbx_get_mesh(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t mesh_count = fbx_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity);
    if (index < 0 || index >= mesh_count)
        return NULL;
    return rt_g3d_checked_or_null(a->meshes[index], RT_G3D_MESH3D_CLASS_ID);
}

/// @brief Get the skeleton extracted from the FBX file (NULL if no skeleton).
void *rt_fbx_get_skeleton(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? rt_g3d_checked_or_null(a->skeleton, RT_G3D_SKELETON3D_CLASS_ID) : NULL;
}

/// @brief Get the `SceneNode3D` root of the imported scene graph — the tree of models
/// the FBX author created, with their world transforms and mesh/material bindings.
/// Returned reference is borrowed; the asset owns the lifetime. Distinct from the flat
/// `mesh_count` / `material_count` lists which expose every shared resource the scene
/// uses, regardless of whether it's actually attached to a node.
void *rt_fbx_get_scene_root(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? rt_g3d_checked_or_null(a->scene_root, RT_G3D_SCENENODE3D_CLASS_ID) : NULL;
}

/// @brief Get the number of animation clips in the FBX file.
int64_t rt_fbx_animation_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->animations, a->animation_count, a->animation_capacity) : 0;
}

/// @brief Get the number of object/morph animation clips in the FBX file.
int64_t rt_fbx_node_animation_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(
                   a->node_animations, a->node_animation_count, a->node_animation_capacity)
             : 0;
}

/// @brief Get an object/morph animation clip by index from the loaded FBX asset.
void *rt_fbx_get_node_animation(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    int32_t count;
    if (!a)
        return NULL;
    count = fbx_asset_safe_count(
        a->node_animations, a->node_animation_count, a->node_animation_capacity);
    if (index < 0 || index >= count)
        return NULL;
    return rt_g3d_checked_or_null(a->node_animations[index], RT_G3D_NODEANIMATION3D_CLASS_ID);
}

/// @brief Get the name of an object/morph animation clip by index.
rt_string rt_fbx_get_node_animation_name(void *obj, int64_t index) {
    void *animation = rt_fbx_get_node_animation(obj, index);
    return animation ? rt_node_animation3d_get_name(animation) : rt_const_cstr("");
}

/// @brief Get the number of cameras extracted from the FBX file.
int64_t rt_fbx_camera_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->cameras, a->camera_count, a->camera_capacity) : 0;
}

/// @brief Get a camera by index from the loaded FBX asset.
void *rt_fbx_get_camera(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    int32_t count;
    if (!a)
        return NULL;
    count = fbx_asset_safe_count(a->cameras, a->camera_count, a->camera_capacity);
    if (index < 0 || index >= count)
        return NULL;
    return rt_g3d_checked_or_null(a->cameras[index], RT_G3D_CAMERA3D_CLASS_ID);
}

/// @brief Get an animation clip by index from the loaded FBX asset.
void *rt_fbx_get_animation(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t animation_count =
        fbx_asset_safe_count(a->animations, a->animation_count, a->animation_capacity);
    if (index < 0 || index >= animation_count)
        return NULL;
    return rt_g3d_checked_or_null(a->animations[index], RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Get the name of an animation clip by index.
rt_string rt_fbx_get_animation_name(void *obj, int64_t index) {
    void *anim = rt_fbx_get_animation(obj, index);
    if (!anim)
        return rt_const_cstr("");
    return rt_animation3d_get_name(anim);
}

/// @brief Get the number of materials extracted from the FBX file.
int64_t rt_fbx_material_count(void *obj) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    return a ? fbx_asset_safe_count(a->materials, a->material_count, a->material_capacity) : 0;
}

/// @brief Get a material by index from the loaded FBX asset.
void *rt_fbx_get_material(void *obj, int64_t index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t material_count =
        fbx_asset_safe_count(a->materials, a->material_count, a->material_capacity);
    if (index < 0 || index >= material_count)
        return NULL;
    return rt_g3d_checked_or_null(a->materials[index], RT_G3D_MATERIAL3D_CLASS_ID);
}

/// @brief Get the morph target data for a mesh by its index in the FBX asset.
void *rt_fbx_get_morph_target(void *obj, int64_t mesh_index) {
    rt_fbx_asset *a = (rt_fbx_asset *)rt_g3d_checked_or_null(obj, RT_G3D_FBX_ASSET_CLASS_ID);
    if (!a)
        return NULL;
    int32_t morph_count = fbx_asset_safe_count(a->morph_targets, a->morph_count, a->morph_capacity);
    if (mesh_index < 0 || mesh_index >= morph_count)
        return NULL;
    return rt_g3d_checked_or_null(a->morph_targets[mesh_index], RT_G3D_MORPHTARGET3D_CLASS_ID);
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* ZANNA_ENABLE_GRAPHICS */
