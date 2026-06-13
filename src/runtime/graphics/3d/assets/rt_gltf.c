//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_gltf.c
// Purpose: glTF 2.0 (.gltf/.glb) loader implementation.
// Key invariants:
//   - Uses existing rt_json parser for JSON content
//   - Supports .glb binary container (magic 0x46546C67)
//   - Preserves glTF metallic-roughness materials on Material3D's native PBR surface
//   - Mesh primitives support POSITION/NORMAL/TEXCOORD_0 plus COLOR_0, TANGENT,
//     and JOINTS_0/WEIGHTS_0 + JOINTS_1/WEIGHTS_1 vertex attributes
//   - Triangle-list, triangle-strip, and triangle-fan primitives are triangulated
// Ownership/Lifetime:
//   - All extracted objects are GC-managed
// Links: rt_gltf.h, rt_json.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_gltf.h"
#include "rt_asset.h"
#include "rt_asset_error.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_gif.h"
#include "rt_gltf_json.h"
#include "rt_mat4.h"
#include "rt_morphtarget3d.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_quat.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_skeleton3d_internal.h"
#include "rt_string.h"
#include "rt_textureasset3d.h"
#include "rt_untrusted_count.h"
#include "rt_vec3.h"

#include "rt_gltf_internal.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define gltf_fseek(fp, off, whence) _fseeki64((fp), (off), (whence))
#define gltf_ftell(fp) _ftelli64((fp))
#else
#define gltf_fseek(fp, off, whence) fseeko((fp), (off_t)(off), (whence))
#define gltf_ftell(fp) ftello((fp))
#endif

// Forward declarations for runtime JSON and collection APIs
extern void *rt_json_parse_object(rt_string text);
extern void *rt_map_get(void *map, rt_string key);
extern int64_t rt_seq_len(void *seq);
extern void *rt_seq_get(void *seq, int64_t index);
extern int64_t rt_box_type(void *box);
extern int64_t rt_unbox_i64(void *boxed);
extern double rt_unbox_f64(void *boxed);
extern int64_t rt_unbox_i1(void *boxed);
extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
#include "rt_trap.h"
extern void rt_trap_set_recovery(jmp_buf *buf);
extern void rt_trap_clear_recovery(void);
extern void rt_obj_free(void *obj);
extern void *rt_asset_decode_typed(const char *name, const uint8_t *data, size_t size);
extern void *rt_pixels_load(void *path);
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
extern void *rt_material3d_new_pbr(double r, double g, double b);
extern void rt_material3d_set_texture(void *obj, void *pixels);
extern void rt_material3d_set_normal_map(void *obj, void *pixels);
extern void rt_material3d_set_metallic(void *obj, double value);
extern void rt_material3d_set_roughness(void *obj, double value);
extern void rt_material3d_set_ao(void *obj, double value);
extern void rt_material3d_set_emissive_intensity(void *obj, double value);
extern void rt_material3d_set_metallic_roughness_map(void *obj, void *pixels);
extern void rt_material3d_set_ao_map(void *obj, void *pixels);
extern void rt_material3d_set_specular_map(void *obj, void *pixels);
extern void rt_material3d_set_emissive_map(void *obj, void *pixels);
extern void rt_material3d_set_alpha(void *obj, double alpha);
extern void rt_material3d_set_normal_scale(void *obj, double value);
extern void rt_material3d_set_alpha_mode(void *obj, int64_t mode);
extern void rt_material3d_set_double_sided(void *obj, int8_t enabled);
extern void rt_material3d_set_unlit(void *obj, int8_t unlit);
extern void rt_material3d_set_shading_model(void *obj, int64_t model);
extern void rt_material3d_set_reflectivity(void *obj, double value);
extern void rt_material3d_set_custom_param(void *obj, int64_t index, double value);

/// @brief Cast a JSON double to int64 only when the conversion is defined.
static int gltf_double_to_i64_checked(double value, int64_t *out) {
    if (!out || !isfinite(value))
        return 0;
    if (value < (-9223372036854775807.0 - 1.0) || value >= 9223372036854775808.0)
        return 0;
    *out = (int64_t)value;
    return 1;
}

/// @brief Narrow an int64 JSON value to int32 with fallback on range errors.
static int32_t gltf_i32_from_i64_or(int64_t value, int32_t fallback) {
    if (value < (int64_t)INT32_MIN || value > (int64_t)INT32_MAX)
        return fallback;
    return (int32_t)value;
}

extern void rt_material3d_set_import_texture_slot(void *obj,
                                                  int64_t slot,
                                                  int64_t uv_set,
                                                  double offset_u,
                                                  double offset_v,
                                                  double scale_u,
                                                  double scale_v,
                                                  double rotation,
                                                  int64_t wrap_s,
                                                  int64_t wrap_t,
                                                  int64_t filter);

//===----------------------------------------------------------------------===//
// Asset container
//===----------------------------------------------------------------------===//

typedef struct {
    void *root;
    char *name;
    int32_t node_count;
    void **cameras;
    int32_t camera_count;
    int32_t camera_capacity;
} gltf_scene_info_t;

typedef struct {
    void *vptr;
    void **meshes;
    int32_t mesh_count;
    int32_t mesh_capacity;
    void **materials;
    int32_t material_count;
    int32_t material_capacity;
    void **skeletons;
    int32_t skeleton_count;
    int32_t skeleton_capacity;
    void **animations;
    int32_t animation_count;
    int32_t animation_capacity;
    void **node_animations;
    int32_t node_animation_count;
    int32_t node_animation_capacity;
    void **cameras;
    int32_t camera_count;
    int32_t camera_capacity;
    gltf_scene_info_t *scenes;
    int32_t scene_count;
    int32_t scene_capacity;
    void *scene_root;
    int32_t node_count;
} rt_gltf_asset;

/// @brief Validate @p obj as a GLTF asset handle, returning NULL on class mismatch.
static rt_gltf_asset *gltf_asset_checked(void *obj) {
    return (rt_gltf_asset *)rt_g3d_checked_or_null(obj, RT_G3D_GLTF_ASSET_CLASS_ID);
}

typedef struct {
    void *skeleton;
    int32_t *joint_nodes;
    int32_t *joint_to_bone;
    int32_t joint_count;
} gltf_skin_t;

typedef struct {
    int32_t wrap_s;
    int32_t wrap_t;
    int32_t filter;
} gltf_sampler_info_t;

typedef struct {
    int32_t texcoord;
    int8_t has_transform;
    double offset[2];
    double scale[2];
    double rotation;
} gltf_texture_info_t;

typedef struct {
    gltf_texture_info_t slots[RT_MATERIAL3D_TEXTURE_SLOT_COUNT];
} gltf_material_info_t;

static void *jget(void *obj, const char *key);
static const char *jstr(void *obj, const char *key);
static int64_t jvalue_int(void *value, int64_t def);
static int gltf_ascii_ieq_n(const char *a, const char *b, size_t len);

/// @brief Number of skin joints safe to read (clamped to VGFX3D_MAX_BONES); 0 when any backing
///   array is absent.
static int32_t gltf_skin_safe_joint_count(const gltf_skin_t *skin) {
    if (!skin || skin->joint_count <= 0 || !skin->joint_nodes || !skin->joint_to_bone)
        return 0;
    return skin->joint_count < VGFX3D_MAX_BONES ? skin->joint_count : VGFX3D_MAX_BONES;
}

/// @brief Clamp a (count, capacity) pair to a safe element count (0 when invalid, else min).
static int32_t gltf_asset_safe_count(void **items, int32_t count, int32_t capacity) {
    if (!items || count <= 0 || capacity <= 0)
        return 0;
    if (count > capacity)
        return capacity;
    return count;
}

/// @brief Number of imported scenes safe to read (live count clamped to capacity).
static int32_t gltf_asset_safe_scene_count(const rt_gltf_asset *asset) {
    if (!asset || !asset->scenes || asset->scene_count <= 0 || asset->scene_capacity <= 0)
        return 0;
    if (asset->scene_count > asset->scene_capacity)
        return asset->scene_capacity;
    return asset->scene_count;
}

/// @brief Release every reference in a glTF ref array (over its safe count) and free the
///   backing storage, resetting the count/capacity to zero.
static void gltf_asset_release_ref_array(void ***items, int32_t *count, int32_t *capacity) {
    void **array = items ? *items : NULL;
    int32_t safe_count = gltf_asset_safe_count(array, count ? *count : 0, capacity ? *capacity : 0);
    if (array) {
        for (int32_t i = 0; i < safe_count; i++) {
            if (array[i] && rt_obj_release_check0(array[i]))
                rt_obj_free(array[i]);
            array[i] = NULL;
        }
        free(array);
    }
    if (items)
        *items = NULL;
    if (count)
        *count = 0;
    if (capacity)
        *capacity = 0;
}

/// @brief GC finalizer for an `rt_gltf_asset` — release every owned mesh / material / scene root.
static void gltf_asset_finalize(void *obj) {
    rt_gltf_asset *a = (rt_gltf_asset *)obj;
    if (!a)
        return;
    gltf_asset_release_ref_array(&a->meshes, &a->mesh_count, &a->mesh_capacity);
    gltf_asset_release_ref_array(&a->materials, &a->material_count, &a->material_capacity);
    gltf_asset_release_ref_array(&a->skeletons, &a->skeleton_count, &a->skeleton_capacity);
    gltf_asset_release_ref_array(&a->animations, &a->animation_count, &a->animation_capacity);
    gltf_asset_release_ref_array(
        &a->node_animations, &a->node_animation_count, &a->node_animation_capacity);
    gltf_asset_release_ref_array(&a->cameras, &a->camera_count, &a->camera_capacity);
    if (a->scenes) {
        int32_t scene_count = gltf_asset_safe_scene_count(a);
        for (int32_t i = 0; i < scene_count; i++) {
            gltf_scene_info_t *scene = &a->scenes[i];
            if (scene->root && rt_obj_release_check0(scene->root))
                rt_obj_free(scene->root);
            scene->root = NULL;
            free(scene->name);
            scene->name = NULL;
            gltf_asset_release_ref_array(
                &scene->cameras, &scene->camera_count, &scene->camera_capacity);
        }
    }
    free(a->scenes);
    a->scenes = NULL;
    a->scene_count = 0;
    a->scene_capacity = 0;
    if (a->scene_root && rt_obj_release_check0(a->scene_root))
        rt_obj_free(a->scene_root);
    a->scene_root = NULL;
    a->node_count = 0;
}

//===----------------------------------------------------------------------===//
// JSON helpers
//===----------------------------------------------------------------------===//

/// @brief Set the name of a scene node from a C string (no-op for empty / NULL input).
static void gltf_set_node_name(rt_scene_node3d *node, const char *name) {
    if (!node || !name || name[0] == '\0')
        return;
    rt_scene_node3d_set_name(node, rt_const_cstr(name));
}

/// @brief Return the best available name for a glTF node, falling back to a synthetic index string.
/// @details First tries the JSON `"name"` field for the node at @p node_index. If that field
///   is absent or empty, a fallback string `"node_N"` is written into @p buffer and returned.
///   The buffer fallback avoids returning NULL to callers that always want a printable label,
///   at the cost of a stack-allocated temporary that must remain live as long as the returned
///   pointer is used. Returns NULL only when both the JSON name is absent and no buffer is
///   provided.
/// @param nodes_arr   JSON array of glTF node objects.
/// @param node_index  Zero-based index into @p nodes_arr.
/// @param buffer      Caller-supplied scratch buffer for the synthetic name; may be NULL.
/// @param buffer_size Capacity of @p buffer including NUL terminator.
/// @return Borrowed pointer to the name string (JSON storage or @p buffer), or NULL.
static const char *gltf_effective_node_name(void *nodes_arr,
                                            int32_t node_index,
                                            char *buffer,
                                            size_t buffer_size) {
    void *node_json;
    const char *name = NULL;
    if (nodes_arr && node_index >= 0 && node_index < jarr_len(nodes_arr)) {
        node_json = rt_seq_get(nodes_arr, node_index);
        name = jstr(node_json, "name");
    }
    if (name && name[0] != '\0')
        return name;
    if (buffer && buffer_size > 0) {
        snprintf(buffer, buffer_size, "node_%d", (int)node_index);
        return buffer;
    }
    return NULL;
}

/// @brief Write an identity transform into the given outputs: zero position, identity
///   quaternion (0,0,0,1), unit scale. NULL outputs are skipped.

// ---------------------------------------------------------------------------
// JSON accessor helpers — adapt the runtime JSON map/array API to
// the glTF parser's conventions (default values for missing keys,
// borrowed cstr returns, etc.).
// ---------------------------------------------------------------------------

/// @brief Look up a JSON object field by key (NULL if absent or `obj` is NULL).
static void *jget(void *obj, const char *key) {
    if (!obj)
        return NULL;
    rt_string k = rt_const_cstr(key);
    return rt_map_get(obj, k);
}

/// @brief Read `obj[key]` as a double; returns `def` if absent or wrong type.
static double jnum(void *obj, const char *key, double def) {
    void *v = jget(obj, key);
    if (!v)
        return def;
    switch (rt_box_type(v)) {
        case 0:
            return (double)rt_unbox_i64(v);
        case 1: {
            double value = rt_unbox_f64(v);
            return isfinite(value) ? value : def;
        }
        case 2:
            return (double)rt_unbox_i1(v);
        default:
            return def;
    }
}

/// @brief Read `obj[key]` as an int64; returns `def` if absent or wrong type.
static int64_t jint(void *obj, const char *key, int64_t def) {
    void *v = jget(obj, key);
    if (!v)
        return def;
    switch (rt_box_type(v)) {
        case 0:
            return rt_unbox_i64(v);
        case 1: {
            int64_t coerced;
            return gltf_double_to_i64_checked(rt_unbox_f64(v), &coerced) ? coerced : def;
        }
        case 2:
            return rt_unbox_i1(v);
        default:
            return def;
    }
}

/// @brief Read `obj[key]` as a borrowed C string (NULL if absent or non-string).
static const char *jstr(void *obj, const char *key) {
    void *v = jget(obj, key);
    if (!rt_string_is_handle(v))
        return NULL;
    return rt_string_cstr((rt_string)v);
}

/// @brief Read `obj[key]` as an array (alias for `jget` — typed for readability).
void *jarr(void *obj, const char *key) {
    return jget(obj, key);
}

/// @brief Length of a JSON array (0 for NULL).
int64_t jarr_len(void *arr) {
    return arr ? rt_seq_len(arr) : 0;
}

/// @brief Length of a JSON array as int32, rejecting counts that cannot be safely indexed.
static int gltf_jarr_len_i32(void *arr, int *out_len) {
    int64_t len;
    if (!out_len)
        return 0;
    len = jarr_len(arr);
    if (len < 0 || len > INT32_MAX)
        return 0;
    *out_len = (int)len;
    return 1;
}

/// @brief Coerce a boxed JSON value to double with default fallback.
double jvalue_num(void *value, double def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return (double)rt_unbox_i64(value);
        case 1: {
            double number = rt_unbox_f64(value);
            return isfinite(number) ? number : def;
        }
        default:
            return def;
    }
}

/// @brief Coerce a boxed JSON value to int64 with default fallback.
static int64_t jvalue_int(void *value, int64_t def) {
    if (!value)
        return def;
    switch (rt_box_type(value)) {
        case 0:
            return rt_unbox_i64(value);
        case 1: {
            int64_t coerced;
            return gltf_double_to_i64_checked(rt_unbox_f64(value), &coerced) ? coerced : def;
        }
        case 2:
            return rt_unbox_i1(value);
        default:
            return def;
    }
}

/// @brief Iterative count — returns 1 for `node` plus the sum of all descendants.
static int32_t gltf_count_subtree(const rt_scene_node3d *node) {
    const rt_scene_node3d **stack = NULL;
    int32_t count = 0;
    int32_t capacity = 0;
    int32_t total = 0;
    if (!node)
        return 0;
    while (count >= capacity) {
        const rt_scene_node3d **grown;
        int64_t next_capacity64 = capacity == 0 ? 32 : (int64_t)capacity * 2;
        if (next_capacity64 > INT32_MAX) {
            free(stack);
            return total;
        }
        int32_t next_capacity = (int32_t)next_capacity64;
        grown = (const rt_scene_node3d **)realloc(stack, (size_t)next_capacity * sizeof(*stack));
        if (!grown) {
            free(stack);
            return total;
        }
        stack = grown;
        capacity = next_capacity;
    }
    stack[count++] = node;
    while (count > 0) {
        const rt_scene_node3d *current = stack[--count];
        if (!current)
            continue;
        if (total < INT32_MAX)
            total++;
        for (int32_t i = 0; i < current->child_count; ++i) {
            if (count >= capacity) {
                int32_t next_capacity = capacity == 0 ? 32 : capacity * 2;
                const rt_scene_node3d **grown;
                if (capacity > INT32_MAX / 2) {
                    free(stack);
                    return total;
                }
                grown = (const rt_scene_node3d **)realloc(stack,
                                                          (size_t)next_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    return total;
                }
                stack = grown;
                capacity = next_capacity;
            }
            stack[count++] = current->children[i];
        }
    }
    free(stack);
    return total;
}

/// @brief Membership test for the set of glTF extensions this loader handles when
///        they appear in `extensionsRequired`.
/// @details The supported list is deliberately small and hardcoded: adding an entry
///          here is a commitment to actually interpret that extension's JSON in
///          material / node / mesh parsing. Listing an extension here without
///          implementing it is worse than not listing it, because assets that require
///          it will load and then silently render wrong.
/// @return 1 if the extension is supported, 0 otherwise (including NULL input).
static int gltf_required_extension_supported(const char *name) {
    if (!name)
        return 0;
    return strcmp(name, "KHR_texture_transform") == 0 ||
           strcmp(name, "KHR_materials_emissive_strength") == 0 ||
           strcmp(name, "KHR_materials_unlit") == 0 ||
           strcmp(name, "KHR_materials_specular") == 0 || strcmp(name, "KHR_lights_punctual") == 0;
}

/// @brief Enforce the glTF `extensionsRequired` contract.
/// @details If the document declares extensions as REQUIRED (not merely USED), the
///          spec says a loader that can't handle any of them must refuse to load the
///          asset rather than produce a degraded rendering. This function walks the
///          required list and returns 0 on the first unsupported extension so the
///          top-level loader can bail cleanly. Missing or empty `extensionsRequired`
///          is treated as "nothing required" (returns 1).
/// @return 1 when every required extension is supported (or the array is absent);
///         0 when any required extension is unsupported and the load should fail.
static int gltf_validate_required_extensions(void *root) {
    void *required = jarr(root, "extensionsRequired");
    if (!required)
        return 1;
    for (int64_t i = 0; i < jarr_len(required); i++) {
        rt_string name = (rt_string)rt_seq_get(required, i);
        const char *ext = rt_string_is_handle(name) ? rt_string_cstr(name) : NULL;
        if (!gltf_required_extension_supported(ext))
            return 0;
    }
    return 1;
}

/// @brief Zero-initialise a `gltf_texture_info_t` to identity-transform defaults.
/// @details Sets texcoord=0, has_transform=0, offset=(0,0), scale=(1,1), rotation=0.0 so callers
///   can always call `gltf_read_texture_info` after this without guarding on partial
///   initialisation.
static void gltf_texture_info_init(gltf_texture_info_t *info) {
    if (!info)
        return;
    info->texcoord = 0;
    info->has_transform = 0;
    info->offset[0] = 0.0;
    info->offset[1] = 0.0;
    info->scale[0] = 1.0;
    info->scale[1] = 1.0;
    info->rotation = 0.0;
}

/// @brief Parse a glTF `textureInfo` object (including `KHR_texture_transform` if present).
/// @details Reads the `texCoord` index and, if the `KHR_texture_transform` extension block is
///   present in `extensions`, reads the UV transform (offset, scale, rotation) and sets
///   `has_transform = 1`. The extension's `texCoord` can override the base texcoord index,
///   matching the KHR_texture_transform spec. When no transform block is present the struct
///   retains identity defaults from `gltf_texture_info_init`. Null @p texture_info leaves @p out
///   at defaults so callers need not guard against missing fields.
/// @param texture_info  Parsed glTF `textureInfo` JSON object (may be NULL for default texture
/// slot).
/// @param out           Output struct; always initialised to defaults before filling.
static void gltf_read_texture_info(void *texture_info, gltf_texture_info_t *out) {
    void *extensions;
    void *transform;
    void *offset;
    void *scale;
    if (!out)
        return;
    gltf_texture_info_init(out);
    if (!texture_info)
        return;
    out->texcoord = gltf_i32_from_i64_or(jint(texture_info, "texCoord", 0), 0);
    if (out->texcoord < 0)
        out->texcoord = 0;
    extensions = jget(texture_info, "extensions");
    transform = extensions ? jget(extensions, "KHR_texture_transform") : NULL;
    if (!transform)
        return;
    out->has_transform = 1;
    out->texcoord = gltf_i32_from_i64_or(jint(transform, "texCoord", out->texcoord), out->texcoord);
    if (out->texcoord < 0)
        out->texcoord = 0;
    offset = jarr(transform, "offset");
    scale = jarr(transform, "scale");
    if (offset && jarr_len(offset) >= 2) {
        out->offset[0] = jvalue_num(rt_seq_get(offset, 0), 0.0);
        out->offset[1] = jvalue_num(rt_seq_get(offset, 1), 0.0);
    }
    if (scale && jarr_len(scale) >= 2) {
        out->scale[0] = jvalue_num(rt_seq_get(scale, 0), 1.0);
        out->scale[1] = jvalue_num(rt_seq_get(scale, 1), 1.0);
    }
    out->rotation = jnum(transform, "rotation", 0.0);
}

/// @brief Map a glTF sampler `wrapS`/`wrapT` integer to a Viper wrap-mode constant.
/// @details glTF uses the original GL enum integers: 33071 = GL_CLAMP_TO_EDGE,
///   33648 = GL_MIRRORED_REPEAT. Anything else (including the default 10497 =
///   GL_REPEAT) maps to `RT_MATERIAL3D_TEXTURE_WRAP_REPEAT`.
/// @param wrap  Raw integer from the glTF sampler JSON.
/// @return One of the `RT_MATERIAL3D_TEXTURE_WRAP_*` constants.
static int32_t gltf_map_sampler_wrap(int64_t wrap) {
    if (wrap == 33071)
        return RT_MATERIAL3D_TEXTURE_WRAP_CLAMP_TO_EDGE;
    if (wrap == 33648)
        return RT_MATERIAL3D_TEXTURE_WRAP_MIRRORED_REPEAT;
    return RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
}

/// @brief Map a glTF sampler min/mag filter pair to a Viper filter-mode constant.
/// @details glTF separates min and mag filters; Viper uses a single constant. The mag filter
///   is preferred (it directly controls visible texel appearance). GL nearest constants:
///   9728 = GL_NEAREST, 9984 = GL_NEAREST_MIPMAP_NEAREST, 9986 = GL_NEAREST_MIPMAP_LINEAR.
///   All other values (including linear variants 9729, 9985, 9987) map to LINEAR. When
///   @p mag_filter is -1 (absent in the JSON), falls back to @p min_filter.
/// @param min_filter  Raw `minFilter` value from the glTF sampler JSON, or -1 if absent.
/// @param mag_filter  Raw `magFilter` value, or -1 if absent.
/// @return `RT_MATERIAL3D_TEXTURE_FILTER_NEAREST` or `RT_MATERIAL3D_TEXTURE_FILTER_LINEAR`.
static int32_t gltf_map_sampler_filter(int64_t min_filter, int64_t mag_filter) {
    int64_t filter = mag_filter >= 0 ? mag_filter : min_filter;
    if (filter == 9728 || filter == 9984 || filter == 9986)
        return RT_MATERIAL3D_TEXTURE_FILTER_NEAREST;
    return RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
}

/// @brief Initialise a `gltf_sampler_info_t` to the glTF default sampler state.
/// @details glTF defaults: wrapS = wrapT = REPEAT, filter = LINEAR (per spec §5.24.1).
static void gltf_sampler_info_init(gltf_sampler_info_t *info) {
    if (!info)
        return;
    info->wrap_s = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    info->wrap_t = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    info->filter = RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
}

/// @brief Parse a glTF sampler JSON object into a `gltf_sampler_info_t`.
/// @details Reads `wrapS`, `wrapT`, `minFilter`, and `magFilter` using their glTF
///   default values (10497 = REPEAT for wrap; -1 = absent for filter) and maps them
///   to Viper constants via `gltf_map_sampler_wrap` / `gltf_map_sampler_filter`.
///   A NULL @p sampler_json leaves @p out at the glTF defaults (REPEAT / LINEAR).
/// @param sampler_json  Parsed glTF sampler object JSON; may be NULL for default state.
/// @param out           Output struct; always initialised before filling.
static void gltf_read_sampler_info(void *sampler_json, gltf_sampler_info_t *out) {
    if (!out)
        return;
    gltf_sampler_info_init(out);
    if (!sampler_json)
        return;
    out->wrap_s = gltf_map_sampler_wrap(jint(sampler_json, "wrapS", 10497));
    out->wrap_t = gltf_map_sampler_wrap(jint(sampler_json, "wrapT", 10497));
    out->filter = gltf_map_sampler_filter(jint(sampler_json, "minFilter", -1),
                                          jint(sampler_json, "magFilter", -1));
}

/// @brief DFS helper for cycle detection in the glTF node graph.
/// @details Uses a three-colour state array: 0 = unvisited, 1 = in-progress (grey), 2 = done
/// (black).
///   Returning 0 from a grey node means a back-edge (cycle) was found. Children that are out of
///   range or already grey are also treated as invalid. Called by `gltf_validate_node_graph`
///   once per node. The @p state array must be zero-initialised by the caller before the first
///   call.
/// @param nodes_arr   JSON array of glTF node objects.
/// @param node_count  Total number of nodes (bounds the valid index range).
/// @param node_idx    Node to visit.
/// @param state       Per-node colour byte array of length @p node_count.
/// @return 1 if the subtree is DAG-valid, 0 if a cycle or out-of-bounds child is found.
static int gltf_validate_node_visit(void *nodes_arr,
                                    int32_t node_count,
                                    int32_t node_idx,
                                    uint8_t *state) {
    typedef struct gltf_node_visit_item {
        int32_t node;
        int8_t exit;
    } gltf_node_visit_item;

    gltf_node_visit_item *stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 0;
    if (!nodes_arr || !state || node_idx < 0 || node_idx >= node_count)
        return 0;
    if (state[node_idx] == 1)
        return 0;
    if (state[node_idx] == 2)
        return 1;
    stack_capacity = 32;
    stack = (gltf_node_visit_item *)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack)
        return 0;
    stack[stack_count++] = (gltf_node_visit_item){node_idx, 0};
    while (stack_count > 0) {
        gltf_node_visit_item item = stack[--stack_count];
        if (item.node < 0 || item.node >= node_count) {
            free(stack);
            return 0;
        }
        if (item.exit) {
            state[item.node] = 2;
            continue;
        }
        if (state[item.node] == 2)
            continue;
        if (state[item.node] == 1) {
            free(stack);
            return 0;
        }
        state[item.node] = 1;
        if (stack_count >= stack_capacity) {
            int32_t next_capacity = stack_capacity * 2;
            gltf_node_visit_item *grown;
            if (stack_capacity > INT32_MAX / 2) {
                free(stack);
                return 0;
            }
            grown = (gltf_node_visit_item *)realloc(stack, (size_t)next_capacity * sizeof(*stack));
            if (!grown) {
                free(stack);
                return 0;
            }
            stack = grown;
            stack_capacity = next_capacity;
        }
        stack[stack_count++] = (gltf_node_visit_item){item.node, 1};
        {
            void *node_json = rt_seq_get(nodes_arr, item.node);
            void *children = jarr(node_json, "children");
            int64_t child_len = jarr_len(children);
            for (int64_t ci = child_len - 1; ci >= 0; --ci) {
                int64_t child = jvalue_int(rt_seq_get(children, ci), -1);
                if (child < 0 || child >= node_count || state[child] == 1) {
                    free(stack);
                    return 0;
                }
                if (state[child] == 2)
                    continue;
                if (stack_count >= stack_capacity) {
                    int32_t next_capacity = stack_capacity * 2;
                    gltf_node_visit_item *grown;
                    if (stack_capacity > INT32_MAX / 2) {
                        free(stack);
                        return 0;
                    }
                    grown = (gltf_node_visit_item *)realloc(stack,
                                                            (size_t)next_capacity * sizeof(*stack));
                    if (!grown) {
                        free(stack);
                        return 0;
                    }
                    stack = grown;
                    stack_capacity = next_capacity;
                }
                stack[stack_count++] = (gltf_node_visit_item){(int32_t)child, 0};
            }
        }
    }
    free(stack);
    return 1;
}

/// @brief Validate the glTF node graph for a well-formed forest (no cycles, unique parents).
/// @details A valid glTF scene node graph must be a directed acyclic forest: each node has at most
///   one parent, there are no back-edges (cycles), and all child indices are in `[0, node_count)`.
///   This function builds a parent array (each entry is -1 for roots or the parent's index), then
///   runs a DFS over every node to detect cycles via `gltf_validate_node_graph`. Returns 0 and
///   frees intermediates if any of these invariants are violated. When @p out_parent is non-NULL
///   and validation succeeds the parent array is returned (caller must `free` it); otherwise it is
///   freed internally. A null or empty nodes array is treated as a trivially valid empty forest.
/// @param nodes_arr   JSON array of glTF node objects.
/// @param node_count  Number of nodes; must match `jarr_len(nodes_arr)`.
/// @param out_parent  If non-NULL, receives the allocated parent-index array on success.
/// @return 1 if the graph is valid, 0 if a cycle, duplicate parent, or OOM is detected.
static int gltf_validate_node_graph(void *nodes_arr, int32_t node_count, int **out_parent) {
    int *parent = NULL;
    uint8_t *state = NULL;
    if (out_parent)
        *out_parent = NULL;
    if (!nodes_arr || node_count <= 0)
        return 1;
    parent = (int *)malloc((size_t)node_count * sizeof(*parent));
    state = (uint8_t *)calloc((size_t)node_count, sizeof(*state));
    if (!parent || !state) {
        free(parent);
        free(state);
        return 0;
    }
    for (int32_t i = 0; i < node_count; i++)
        parent[i] = -1;
    for (int32_t ni = 0; ni < node_count; ni++) {
        void *node_json = rt_seq_get(nodes_arr, ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int64_t child = jvalue_int(rt_seq_get(children, ci), -1);
            if (child < 0 || child >= node_count || parent[child] >= 0) {
                free(parent);
                free(state);
                return 0;
            }
            parent[child] = ni;
        }
    }
    for (int32_t ni = 0; ni < node_count; ni++) {
        if (!gltf_validate_node_visit(nodes_arr, node_count, ni, state)) {
            free(parent);
            free(state);
            return 0;
        }
    }
    free(state);
    if (out_parent)
        *out_parent = parent;
    else
        free(parent);
    return 1;
}

/// @brief Bind a texture index + sampler state + UV-transform to one material texture slot.
/// @details Looks up wrap/filter from the pre-built @p texture_samplers array at @p texture_index
///   (defaulting to REPEAT/LINEAR when the index is out of range), then calls
///   `rt_material3d_set_import_texture_slot` with the full combined parameters. If @p info is NULL
///   an identity `gltf_texture_info_t` (texcoord=0, no transform) is used so callers can pass NULL
///   for textures that have no `KHR_texture_transform` extension block. Slots outside the valid
///   range `[0, RT_MATERIAL3D_TEXTURE_SLOT_COUNT)` are silently ignored.
/// @param texture_samplers  Per-texture sampler state array resolved from the `"samplers"` array.
/// @param texture_count     Length of @p texture_samplers.
/// @param texture_index     glTF texture index into @p texture_samplers (and the image table).
/// @param material          Viper material object to write the slot into.
/// @param slot              Destination slot index in `[0, RT_MATERIAL3D_TEXTURE_SLOT_COUNT)`.
/// @param info              Optional UV-transform + texcoord override; NULL uses identity.
static void gltf_apply_texture_slot(const gltf_sampler_info_t *texture_samplers,
                                    int32_t texture_count,
                                    int64_t texture_index,
                                    void *material,
                                    int64_t slot,
                                    const gltf_texture_info_t *info) {
    gltf_texture_info_t identity;
    const gltf_texture_info_t *texture_info = info;
    int32_t wrap_s = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    int32_t wrap_t = RT_MATERIAL3D_TEXTURE_WRAP_REPEAT;
    int32_t filter = RT_MATERIAL3D_TEXTURE_FILTER_LINEAR;
    if (!material || slot < 0 || slot >= RT_MATERIAL3D_TEXTURE_SLOT_COUNT)
        return;
    if (!texture_info) {
        gltf_texture_info_init(&identity);
        texture_info = &identity;
    }
    if (texture_samplers && texture_index >= 0 && texture_index < texture_count) {
        wrap_s = texture_samplers[texture_index].wrap_s;
        wrap_t = texture_samplers[texture_index].wrap_t;
        filter = texture_samplers[texture_index].filter;
    }
    rt_material3d_set_import_texture_slot(material,
                                          slot,
                                          texture_info->texcoord,
                                          texture_info->offset[0],
                                          texture_info->offset[1],
                                          texture_info->scale[0],
                                          texture_info->scale[1],
                                          texture_info->rotation,
                                          wrap_s,
                                          wrap_t,
                                          filter);
}

/// @brief True if a texture is a supported format but its decoded image is still missing.
/// @details Flags an in-range, supported-format texture whose image slot is NULL, i.e. one the
///          caller still needs to decode/stage before the material can reference it.
static int gltf_texture_index_missing_supported_payload(int64_t texture_index,
                                                        int32_t texture_count,
                                                        void **texture_images,
                                                        const uint8_t *texture_supported) {
    return texture_index >= 0 && texture_index < texture_count && texture_supported &&
           texture_supported[texture_index] && (!texture_images || !texture_images[texture_index]);
}

/// @brief Resolve the image source for a parsed glTF texture, including KHR_texture_basisu.
static int64_t gltf_texture_source_index(void *texture_json) {
    void *extensions;
    void *basisu;
    int64_t source_idx = jint(texture_json, "source", -1);
    extensions = jget(texture_json, "extensions");
    basisu = extensions ? jget(extensions, "KHR_texture_basisu") : NULL;
    int64_t basisu_source_idx = jint(basisu, "source", -1);
    if (basisu_source_idx >= 0)
        return basisu_source_idx;
    return source_idx;
}

//===----------------------------------------------------------------------===//
// Implementation split across cohesive .inc units compiled as one translation unit.
//===----------------------------------------------------------------------===//
// clang-format off
#include "rt_gltf_codec.inc"
#include "rt_gltf_accessor.inc"
#include "rt_gltf_import.inc"
#include "rt_gltf_preload.inc"
#include "rt_gltf_anim.inc"
#include "rt_gltf_material.inc"
#include "rt_gltf_mesh.inc"
#include "rt_gltf_scene.inc"
// clang-format on

//===----------------------------------------------------------------------===//
// Main loader
//===----------------------------------------------------------------------===//

static void gltf_free_buffers(gltf_buffer_t *buffers, int buf_count, const uint8_t *bin_chunk) {
    if (!buffers)
        return;
    for (int i = 0; i < buf_count; i++) {
        if (buffers[i].data != bin_chunk)
            free(buffers[i].data);
    }
    free(buffers);
}

static int gltf_load_buffer_uri(const char *filepath,
                                int load_assets,
                                rt_gltf_preload_bundle *preload_bundle,
                                int buffer_index,
                                const char *uri,
                                size_t byte_length,
                                gltf_buffer_t *buffer) {
    if (strncmp(uri, "data:", 5) == 0) {
        char mime_type[64];
        uint8_t *decoded = NULL;
        size_t decoded_len = 0;
        char preload_key[64];
        gltf_preload_buffer_key(buffer_index, preload_key, sizeof(preload_key));
        decoded = gltf_preload_bundle_take_dependency(
            preload_bundle, preload_key, GLTF_PRELOAD_DEP_BUFFER, &decoded_len);
        if (decoded ||
            gltf_parse_data_uri(uri, mime_type, sizeof(mime_type), &decoded, &decoded_len)) {
            if (decoded_len < byte_length) {
                free(decoded);
                return 0;
            }
            buffer->data = decoded;
            buffer->len = byte_length;
            return 1;
        }
        return byte_length == 0;
    }

    char buf_path[1024];
    gltf_resolve_relative_path(filepath, uri, buf_path, sizeof(buf_path));
    if (buf_path[0] == '\0') {
        rt_asset_error_setf(RT_ASSET_ERROR_UNSUPPORTED,
                            "GLTF.Load: unsupported buffer dependency '%s' for '%s'",
                            uri ? uri : "",
                            filepath ? filepath : "");
        return 0;
    }
    buffer->data =
        gltf_load_dependency_bytes(buf_path, load_assets, byte_length, preload_bundle, NULL);
    if (buffer->data)
        buffer->len = byte_length;
    if (byte_length > 0 && (!buffer->data || buffer->len < byte_length)) {
        rt_asset_error_setf(RT_ASSET_ERROR_NOT_FOUND,
                            "GLTF.Load: failed to load buffer dependency '%s' for '%s'",
                            buf_path,
                            filepath ? filepath : "");
        return 0;
    }
    return 1;
}

static int gltf_load_buffers(void *root,
                             const char *filepath,
                             int load_assets,
                             rt_gltf_preload_bundle *preload_bundle,
                             uint8_t *bin_chunk,
                             size_t bin_chunk_len,
                             size_t root_size,
                             gltf_buffer_t **out_buffers,
                             int *out_buf_count) {
    void *buffers_arr = jarr(root, "buffers");
    int64_t buf_count64 = jarr_len(buffers_arr);
    if (buf_count64 < 0 || buf_count64 > INT32_MAX ||
        !rt_untrusted_count_ok(buf_count64, 1u, root_size))
        return 0;
    int buf_count = (int)buf_count64;
    gltf_buffer_t *buffers =
        (gltf_buffer_t *)calloc((size_t)(buf_count + 1), sizeof(gltf_buffer_t));
    if (!buffers)
        return 0;
    for (int i = 0; i < buf_count; i++) {
        void *buf_obj = rt_seq_get(buffers_arr, (int64_t)i);
        int64_t byte_length_raw = jint(buf_obj, "byteLength", -1);
        const char *uri = jstr(buf_obj, "uri");
        size_t byte_length;
        if (byte_length_raw < 0) {
            gltf_free_buffers(buffers, buf_count, bin_chunk);
            return 0;
        }
        byte_length = (size_t)byte_length_raw;
        if (i == 0 && bin_chunk && !uri) {
            if (byte_length > bin_chunk_len || byte_length > SIZE_MAX - 3u ||
                bin_chunk_len > byte_length + 3u) {
                gltf_free_buffers(buffers, buf_count, bin_chunk);
                return 0;
            }
            buffers[i].data = bin_chunk;
            buffers[i].len = byte_length;
        } else if (uri) {
            if (!gltf_load_buffer_uri(
                    filepath, load_assets, preload_bundle, i, uri, byte_length, &buffers[i])) {
                gltf_free_buffers(buffers, buf_count, bin_chunk);
                return 0;
            }
        } else if (byte_length > 0) {
            gltf_free_buffers(buffers, buf_count, bin_chunk);
            return 0;
        }
    }
    *out_buffers = buffers;
    *out_buf_count = buf_count;
    return 1;
}

static rt_gltf_asset *gltf_asset_new_empty(void) {
    rt_gltf_asset *asset =
        (rt_gltf_asset *)rt_obj_new_i64(RT_G3D_GLTF_ASSET_CLASS_ID, (int64_t)sizeof(rt_gltf_asset));
    if (!asset)
        return NULL;
    asset->vptr = NULL;
    asset->meshes = NULL;
    asset->mesh_count = 0;
    asset->mesh_capacity = 0;
    asset->materials = NULL;
    asset->material_count = 0;
    asset->material_capacity = 0;
    asset->skeletons = NULL;
    asset->skeleton_count = 0;
    asset->skeleton_capacity = 0;
    asset->animations = NULL;
    asset->animation_count = 0;
    asset->animation_capacity = 0;
    asset->node_animations = NULL;
    asset->node_animation_count = 0;
    asset->node_animation_capacity = 0;
    asset->cameras = NULL;
    asset->camera_count = 0;
    asset->camera_capacity = 0;
    asset->scenes = NULL;
    asset->scene_count = 0;
    asset->scene_capacity = 0;
    asset->scene_root = NULL;
    asset->node_count = 0;
    rt_obj_set_finalizer(asset, gltf_asset_finalize);
    return asset;
}

static int gltf_extract_json_document(uint8_t *file_data,
                                      size_t file_size,
                                      char **out_json_str,
                                      uint8_t **out_bin_chunk,
                                      size_t *out_bin_chunk_len) {
    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;
    int parse_error = 0;
    *out_json_str = NULL;
    *out_bin_chunk = NULL;
    *out_bin_chunk_len = 0;

    if (file_size >= 12 && file_data[0] == 0x67 && file_data[1] == 0x6C && file_data[2] == 0x54 &&
        file_data[3] == 0x46) {
        uint32_t version = gltf_read_u32_le(file_data + 4);
        uint32_t declared_len = gltf_read_u32_le(file_data + 8);
        size_t pos = 12;
        int chunk_index = 0;
        if (file_size > (size_t)UINT32_MAX || version != 2 || declared_len != (uint32_t)file_size)
            parse_error = 1;
        while (!parse_error && pos + 8 <= file_size) {
            uint32_t chunk_len = gltf_read_u32_le(file_data + pos);
            uint32_t chunk_type = gltf_read_u32_le(file_data + pos + 4);
            pos += 8;
            if ((chunk_len & 3u) != 0 || chunk_len > file_size - pos) {
                parse_error = 1;
                break;
            }
            if (chunk_index == 0 && chunk_type != 0x4E4F534A) {
                parse_error = 1;
                break;
            }
            if (chunk_type == 0x4E4F534A) {
                if (json_str || chunk_len == 0) {
                    parse_error = 1;
                    break;
                }
                json_str = (char *)malloc((size_t)chunk_len + 1u);
                if (!json_str) {
                    parse_error = 1;
                    break;
                }
                memcpy(json_str, file_data + pos, chunk_len);
                json_str[chunk_len] = '\0';
            } else if (chunk_type == 0x004E4942) {
                if (bin_chunk) {
                    parse_error = 1;
                    break;
                }
                bin_chunk = file_data + pos;
                bin_chunk_len = chunk_len;
            }
            pos += chunk_len;
            chunk_index++;
        }
        if (!parse_error && pos != file_size)
            parse_error = 1;
    } else {
        json_str = (char *)malloc(file_size + 1u);
        if (!json_str)
            return 0;
        memcpy(json_str, file_data, file_size);
        json_str[file_size] = '\0';
    }

    if (parse_error || !json_str) {
        free(json_str);
        return 0;
    }
    *out_json_str = json_str;
    *out_bin_chunk = bin_chunk;
    *out_bin_chunk_len = bin_chunk_len;
    return 1;
}

static void *gltf_parse_validated_root_json(char *json_str) {
    rt_string json_rts;
    void *root = NULL;
    jmp_buf json_recovery;
    if (!json_str)
        return NULL;
    if (!gltf_validate_required_data_uri_images(json_str, strlen(json_str))) {
        free(json_str);
        return NULL;
    }
    json_rts = rt_const_cstr(json_str);
    rt_trap_set_recovery(&json_recovery);
    if (setjmp(json_recovery) == 0)
        root = rt_json_parse_object(json_rts);
    rt_trap_clear_recovery();
    free(json_str);
    if (!root)
        return NULL;
    {
        void *asset_json = jget(root, "asset");
        const char *version = jstr(asset_json, "version");
        if (!version || strncmp(version, "2.", 2) != 0) {
            gltf_release_local(root);
            return NULL;
        }
    }
    if (!gltf_validate_required_extensions(root)) {
        gltf_release_local(root);
        return NULL;
    }
    return root;
}

typedef struct {
    void **images;
    int image_count;
    uint8_t *image_required;
    void **texture_images;
    uint8_t *texture_supported;
    gltf_sampler_info_t *texture_samplers;
    int texture_count;
    int *mesh_prim_start;
    int *mesh_prim_count;
    void **primitive_materials;
    gltf_material_info_t *material_infos;
    gltf_skin_t *skins;
    int32_t skin_count;
    void **imported_lights;
    int32_t imported_light_count;
} gltf_load_scratch_t;

static void gltf_load_scratch_cleanup(gltf_load_scratch_t *scratch) {
    if (!scratch)
        return;
    if (scratch->images) {
        for (int i = 0; i < scratch->image_count; i++)
            gltf_release_ref(&scratch->images[i]);
    }
    free(scratch->images);
    free(scratch->image_required);
    free(scratch->texture_images);
    free(scratch->texture_supported);
    free(scratch->texture_samplers);
    free(scratch->mesh_prim_start);
    free(scratch->mesh_prim_count);
    free(scratch->primitive_materials);
    free(scratch->material_infos);
    if (scratch->imported_lights) {
        for (int32_t i = 0; i < scratch->imported_light_count; i++)
            gltf_release_ref(&scratch->imported_lights[i]);
    }
    free(scratch->imported_lights);
    gltf_free_skins(scratch->skins, scratch->skin_count);
}

static int gltf_load_asset_payload(rt_gltf_asset *asset,
                                   void *root,
                                   const char *filepath,
                                   int load_assets,
                                   rt_gltf_preload_bundle *preload_bundle,
                                   gltf_buffer_t *buffers,
                                   int buf_count,
                                   gltf_load_scratch_t *scratch) {
    int load_failed = 0;
    gltf_load_images_and_textures(root,
                                  filepath,
                                  load_assets,
                                  preload_bundle,
                                  buffers,
                                  buf_count,
                                  &scratch->images,
                                  &scratch->image_count,
                                  &scratch->image_required,
                                  &scratch->texture_images,
                                  &scratch->texture_supported,
                                  &scratch->texture_samplers,
                                  &scratch->texture_count,
                                  &load_failed);
    gltf_load_materials(asset,
                        root,
                        scratch->texture_images,
                        scratch->texture_supported,
                        scratch->texture_samplers,
                        scratch->texture_count,
                        &scratch->material_infos,
                        &load_failed);
    gltf_parse_punctual_lights(root, &scratch->imported_lights, &scratch->imported_light_count);
    gltf_load_meshes(asset,
                     root,
                     buffers,
                     buf_count,
                     preload_bundle,
                     &scratch->mesh_prim_start,
                     &scratch->mesh_prim_count,
                     &scratch->primitive_materials,
                     &load_failed);
    gltf_parse_skins(
        asset, root, buffers, buf_count, &scratch->skins, &scratch->skin_count, &load_failed);
    if (!load_failed) {
        gltf_parse_animations(asset, root, buffers, buf_count, scratch->skins, scratch->skin_count);
        gltf_parse_node_animations(
            asset, root, buffers, buf_count, scratch->skins, scratch->skin_count);
        load_failed = gltf_build_node_hierarchy(asset,
                                                root,
                                                scratch->mesh_prim_start,
                                                scratch->mesh_prim_count,
                                                scratch->primitive_materials,
                                                scratch->skins,
                                                scratch->skin_count,
                                                scratch->imported_lights,
                                                scratch->imported_light_count);
    }
    return load_failed;
}

/// @brief Load a glTF 2.0 asset from disk and build the engine representation.
/// @details Top-level entry point that orchestrates the entire load pipeline. The
///          high-level stages are:
///            1. Read the whole file (hard cap 256 MiB to avoid denial-of-service
///              on pathological inputs).
///            2. Detect format: the 4-byte magic `glTF` signals a binary GLB
///              container, otherwise treat the bytes as a JSON `.gltf`.
///            3. For GLB: walk the chunk list, extracting the JSON chunk and the
///              embedded BIN chunk (which substitutes for buffer index 0).
///            4. Parse buffers (data-URIs and external files), bufferViews,
///              accessors, and textures into a `gltf_buffer_t[]` scratch table.
///            5. Walk the first scene: for every mesh primitive, decode
///              POSITION / NORMAL / TEXCOORD_0 / TANGENT / JOINTS_0 / WEIGHTS_0
///              attributes through their accessor views, plus the index buffer,
///              and build a Mesh3D.
///            6. Materials: baseColorFactor + baseColorTexture resolved to
///              Material3D. Missing materials fall back to white.
///            7. If skins are present, `gltf_parse_skins` builds Skeleton3D objects
///              and `gltf_apply_skin_to_mesh` retargets the vertex bone indices
///              onto engine bones.
///            8. `gltf_parse_animations` converts glTF sampler/channel data into
///              bone-oriented Animation3D.
///            9. Release scratch buffers (decoded binary data freed; accessor
///               views point into buffers freed at end).
///
///          Failure paths: any I/O error, JSON parse error, or allocation failure
///          returns NULL. Partial state is rolled back via `gltf_release_ref` so
///          the caller never sees a half-built asset.
/// @param path File path to `.gltf` or `.glb`.
/// @return Opaque rt_gltf_asset*, or NULL on failure.
static void *rt_gltf_load_impl(rt_string path,
                               int load_assets,
                               uint8_t *preloaded_data,
                               size_t preloaded_size,
                               rt_gltf_preload_bundle *preload_bundle) {
    if (!path) {
        free(preloaded_data);
        return NULL;
    }
    const char *filepath = rt_string_cstr(path);
    if (!filepath) {
        free(preloaded_data);
        return NULL;
    }

    size_t file_size = 0;
    uint8_t *file_data = preloaded_data;
    if (file_data) {
        file_size = preloaded_size;
    } else {
        file_data = gltf_load_root_bytes(path, filepath, load_assets, &file_size);
    }
    if (!file_data) {
        rt_asset_error_setf(RT_ASSET_ERROR_NOT_FOUND, "GLTF.Load: '%s' not found", filepath);
        return NULL;
    }
    if (file_size > (size_t)LONG_MAX) {
        if (!preloaded_data)
            free(file_data);
        rt_asset_error_setf(RT_ASSET_ERROR_TOO_LARGE, "GLTF.Load: '%s' is too large", filepath);
        return NULL;
    }

    char *json_str = NULL;
    uint8_t *bin_chunk = NULL;
    size_t bin_chunk_len = 0;
    if (!gltf_extract_json_document(file_data, file_size, &json_str, &bin_chunk, &bin_chunk_len)) {
        free(file_data);
        rt_asset_error_setf(
            RT_ASSET_ERROR_BAD_MAGIC, "GLTF.Load: '%s' is not a supported glTF/GLB", filepath);
        return NULL;
    }

    void *root = gltf_parse_validated_root_json(json_str);
    if (!root) {
        free(file_data);
        rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT, "GLTF.Load: '%s' has invalid JSON", filepath);
        return NULL;
    }

    int buf_count = 0;
    gltf_buffer_t *buffers = NULL;
    gltf_load_scratch_t scratch;
    int load_failed = 0;
    memset(&scratch, 0, sizeof(scratch));
    if (!gltf_load_buffers(root,
                           filepath,
                           load_assets,
                           preload_bundle,
                           bin_chunk,
                           bin_chunk_len,
                           file_size,
                           &buffers,
                           &buf_count)) {
        gltf_release_local(root);
        free(file_data);
        rt_asset_error_setf_if_empty(
            RT_ASSET_ERROR_CORRUPT, "GLTF.Load: '%s' has invalid buffers", filepath);
        return NULL;
    }

    if (!gltf_validate_sparse_accessors(root, buffers, buf_count)) {
        gltf_free_buffers(buffers, buf_count, bin_chunk);
        gltf_release_local(root);
        free(file_data);
        rt_asset_error_setf(
            RT_ASSET_ERROR_CORRUPT, "GLTF.Load: '%s' has invalid sparse accessors", filepath);
        return NULL;
    }

    rt_gltf_asset *asset = gltf_asset_new_empty();
    if (!asset) {
        gltf_free_buffers(buffers, buf_count, bin_chunk);
        gltf_release_local(root);
        free(file_data);
        return NULL;
    }

    load_failed = gltf_load_asset_payload(
        asset, root, filepath, load_assets, preload_bundle, buffers, buf_count, &scratch);
    gltf_load_scratch_cleanup(&scratch);

    gltf_free_buffers(buffers, buf_count, bin_chunk);
    gltf_release_local(root);
    free(file_data);

    if (load_failed) {
        gltf_release_ref((void **)&asset);
        rt_asset_error_setf_if_empty(RT_ASSET_ERROR_CORRUPT,
                                     "GLTF.Load: '%s' contains unsupported or corrupt data",
                                     filepath);
        return NULL;
    }
    return asset;
}

/// @brief Load a glTF/GLB from the filesystem (no asset-manager resolution). See header.
void *rt_gltf_load(rt_string path) {
    rt_asset_error_begin_load();
    if (!path) {
        rt_asset_error_end_load_failure();
        rt_trap("GLTF.Load: path must not be null");
        return NULL;
    }
    if (!rt_string_cstr(path)) {
        rt_asset_error_end_load_failure();
        rt_trap("GLTF.Load: invalid path");
        return NULL;
    }
    void *asset = rt_gltf_load_impl(path, 0, NULL, 0, NULL);
    if (asset) {
        rt_asset_error_end_load_success();
    } else {
        rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT, "GLTF.Load: failed to load glTF");
        rt_asset_error_end_load_failure();
    }
    return asset;
}

/// @brief Load a glTF/GLB through the asset manager (mounted/embedded + dev fallback). See header.
void *rt_gltf_load_asset(rt_string path) {
    rt_asset_error_begin_load();
    if (!path) {
        rt_asset_error_end_load_failure();
        rt_trap("GLTF.LoadAsset: path must not be null");
        return NULL;
    }
    if (!rt_string_cstr(path)) {
        rt_asset_error_end_load_failure();
        rt_trap("GLTF.LoadAsset: invalid path");
        return NULL;
    }
    void *asset = rt_gltf_load_impl(path, 1, NULL, 0, NULL);
    if (asset) {
        rt_asset_error_end_load_success();
    } else {
        rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT, "GLTF.LoadAsset: failed to load glTF");
        rt_asset_error_end_load_failure();
    }
    return asset;
}

/// @brief Internal async path: build a glTF/GLB asset from worker-staged root bytes.
/// @details Takes ownership of @p preloaded_data and frees it on all paths.
void *rt_gltf_load_preloaded(rt_string path,
                             uint8_t *preloaded_data,
                             size_t preloaded_size,
                             int load_assets) {
    return rt_gltf_load_impl(path, load_assets ? 1 : 0, preloaded_data, preloaded_size, NULL);
}

/// @brief Build a glTF asset on the main thread from a previously-staged preload bundle.
/// @details Consumes the bundle's staged dependencies (no file I/O), falling back to a normal
///          rt_gltf_load/rt_gltf_load_asset when @p bundle is NULL. Always frees @p bundle.
/// @return The loaded glTF asset handle, or NULL on failure.
void *rt_gltf_load_preloaded_bundle(rt_string path,
                                    rt_gltf_preload_bundle *bundle,
                                    int load_assets) {
    uint8_t *root_data;
    size_t root_size;
    void *asset;
    if (!bundle)
        return load_assets ? rt_gltf_load_asset(path) : rt_gltf_load(path);
    root_data = bundle->root_data;
    root_size = bundle->root_size;
    bundle->root_data = NULL;
    bundle->root_size = 0;
    asset = rt_gltf_load_impl(path, load_assets ? 1 : 0, root_data, root_size, bundle);
    rt_gltf_preload_bundle_free(bundle);
    return asset;
}

/// @brief Get the number of meshes extracted from the GLTF file.
int64_t rt_gltf_mesh_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity) : 0;
}

/// @brief Get a mesh by index from the loaded GLTF asset.
void *rt_gltf_get_mesh(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t mesh_count = gltf_asset_safe_count(a->meshes, a->mesh_count, a->mesh_capacity);
    if (index < 0 || index >= mesh_count)
        return NULL;
    return rt_g3d_checked_or_null(a->meshes[index], RT_G3D_MESH3D_CLASS_ID);
}

/// @brief Get the number of materials extracted from the GLTF file.
int64_t rt_gltf_material_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->materials, a->material_count, a->material_capacity) : 0;
}

/// @brief Get a material by index from the loaded GLTF asset.
void *rt_gltf_get_material(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t material_count =
        gltf_asset_safe_count(a->materials, a->material_count, a->material_capacity);
    if (index < 0 || index >= material_count)
        return NULL;
    return rt_g3d_checked_or_null(a->materials[index], RT_G3D_MATERIAL3D_CLASS_ID);
}

/// @brief Number of skeletons extracted from the loaded glTF asset.
int64_t rt_gltf_skeleton_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->skeletons, a->skeleton_count, a->skeleton_capacity) : 0;
}

/// @brief Get a skeleton by index from the loaded glTF asset.
void *rt_gltf_get_skeleton(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t skeleton_count =
        gltf_asset_safe_count(a->skeletons, a->skeleton_count, a->skeleton_capacity);
    if (index < 0 || index >= skeleton_count)
        return NULL;
    return rt_g3d_checked_or_null(a->skeletons[index], RT_G3D_SKELETON3D_CLASS_ID);
}

/// @brief Number of animation clips extracted from the loaded glTF asset.
int64_t rt_gltf_animation_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->animations, a->animation_count, a->animation_capacity) : 0;
}

/// @brief Get an Animation3D clip by index from the loaded glTF asset.
void *rt_gltf_get_animation(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t animation_count =
        gltf_asset_safe_count(a->animations, a->animation_count, a->animation_capacity);
    if (index < 0 || index >= animation_count)
        return NULL;
    return rt_g3d_checked_or_null(a->animations[index], RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Return the number of node animations (AnimationClip-style tracks) in the glTF asset.
int64_t rt_gltf_node_animation_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(
                   a->node_animations, a->node_animation_count, a->node_animation_capacity)
             : 0;
}

/// @brief Get a node-animation track by index from the loaded glTF asset.
void *rt_gltf_get_node_animation(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t node_animation_count = gltf_asset_safe_count(
        a->node_animations, a->node_animation_count, a->node_animation_capacity);
    if (index < 0 || index >= node_animation_count)
        return NULL;
    return rt_g3d_checked_or_null(a->node_animations[index], RT_G3D_NODEANIMATION3D_CLASS_ID);
}

/// @brief Return the number of cameras imported from the active scene.
int64_t rt_gltf_camera_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? gltf_asset_safe_count(a->cameras, a->camera_count, a->camera_capacity) : 0;
}

/// @brief Borrow the i-th active-scene Camera3D imported from glTF.
void *rt_gltf_get_camera(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a)
        return NULL;
    int32_t camera_count = gltf_asset_safe_count(a->cameras, a->camera_count, a->camera_capacity);
    if (index < 0 || index >= camera_count)
        return NULL;
    return rt_g3d_checked_or_null(a->cameras[index], RT_G3D_CAMERA3D_CLASS_ID);
}

/// @brief Number of immutable scenes in the glTF asset.
int64_t rt_gltf_scene_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return gltf_asset_safe_scene_count(a);
}

/// @brief Return the imported scene name, or an empty string for invalid indices.
rt_string rt_gltf_get_scene_name(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || index < 0 || index >= scene_count || !a->scenes[index].name)
        return rt_const_cstr("");
    return rt_const_cstr(a->scenes[index].name);
}

/// @brief Borrow the root SceneNode3D for immutable scene @p index.
void *rt_gltf_get_scene_root_at(void *obj, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || index < 0 || index >= scene_count)
        return NULL;
    return rt_g3d_checked_or_null(a->scenes[index].root, RT_G3D_SCENENODE3D_CLASS_ID);
}

/// @brief Number of cameras reachable from immutable scene @p scene_index.
int64_t rt_gltf_scene_camera_count(void *obj, int64_t scene_index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || scene_index < 0 || scene_index >= scene_count)
        return 0;
    return gltf_asset_safe_count(a->scenes[scene_index].cameras,
                                 a->scenes[scene_index].camera_count,
                                 a->scenes[scene_index].camera_capacity);
}

/// @brief Borrow a Camera3D from immutable scene @p scene_index.
void *rt_gltf_get_scene_camera(void *obj, int64_t scene_index, int64_t index) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    gltf_scene_info_t *scene;
    int32_t scene_count = gltf_asset_safe_scene_count(a);
    if (!a || scene_index < 0 || scene_index >= scene_count)
        return NULL;
    scene = &a->scenes[scene_index];
    int32_t camera_count =
        gltf_asset_safe_count(scene->cameras, scene->camera_count, scene->camera_capacity);
    if (index < 0 || index >= camera_count)
        return NULL;
    return rt_g3d_checked_or_null(scene->cameras[index], RT_G3D_CAMERA3D_CLASS_ID);
}

/// @brief Number of nodes in the loaded glTF scene tree (0 for NULL).
int64_t rt_gltf_node_count(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    if (!a || a->node_count <= 0 || !rt_g3d_has_class(a->scene_root, RT_G3D_SCENENODE3D_CLASS_ID))
        return 0;
    return a->node_count;
}

/// @brief Return the scene-root SceneNode of the loaded asset (NULL if not loaded / NULL).
void *rt_gltf_get_scene_root(void *obj) {
    rt_gltf_asset *a = gltf_asset_checked(obj);
    return a ? rt_g3d_checked_or_null(a->scene_root, RT_G3D_SCENENODE3D_CLASS_ID) : NULL;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
