//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_model3d.c
// Purpose: Model3D high-level asset wrapper over imported scene/resources.
//   Owns a template scene-graph root and per-asset reference arrays for
//   meshes, materials, skeletons, animations, and node animations imported
//   from FBX or glTF. Each `Instantiate()` clones the template root into a
//   fresh scene subtree that can be parented anywhere.
//
// Key invariants:
//   - The template root is a synthetic node — its children represent the
//     authored asset's top-level scene roots.
//   - Reference arrays are dedup'd by pointer at append time so a mesh
//     reused across many nodes only has one retain.
//   - `model_count_subtree` includes the template root; user-visible counts
//     subtract 1.
//
// Ownership/Lifetime:
//   - Model3D is GC-managed; finalizer releases the template root and every
//     entry of the reference arrays.
//   - Cloning a node retains the source's mesh/material; only morph-enabled
//     meshes are deep-cloned for per-instance blend-shape state.
//
// Links: rt_model3d.h, rt_scene3d.h, rt_fbx_loader.h, rt_gltf.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_model3d.h"

#include "rt_animcontroller3d.h"
#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_fbx_loader.h"
#include "rt_gltf.h"
#include "rt_graphics3d_ids.h"
#include "rt_morphtarget3d.h"
#include "rt_numeric.h"
#include "rt_object.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_fbx_get_scene_root(void *fbx);
extern void *rt_pixels_load(void *path);

#define MODEL3D_MAX_WALK_NODES 1048576

typedef struct {
    rt_scene_node3d *root;
    char *name;
    void **cameras;
    int32_t camera_count;
    int32_t camera_capacity;
} model3d_scene_entry;

typedef struct {
    void *vptr;
    rt_scene_node3d *template_root;

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

    model3d_scene_entry *scenes;
    int32_t scene_count;
    int32_t scene_capacity;
} rt_model3d;

/// @brief Validate @p obj as a Model3D handle and return its typed pointer (NULL on mismatch).
static rt_model3d *model3d_checked(void *obj) {
    return (rt_model3d *)rt_g3d_checked_or_null(obj, RT_G3D_MODEL3D_CLASS_ID);
}

/// @brief Clamp a runtime count to its backing capacity and pointer validity.
static int32_t model_clamped_array_count(const void *arr, int32_t count, int32_t capacity) {
    if (count <= 0)
        return 0;
    if (!arr || capacity <= 0)
        return 0;
    return count > capacity ? capacity : count;
}

/// @brief Clamp and persist a private array count before mutating that array.
static int32_t model_repair_array_count(const void *arr, int32_t *count, int32_t capacity) {
    int32_t safe_count = count ? model_clamped_array_count(arr, *count, capacity) : 0;
    if (count)
        *count = safe_count;
    return safe_count;
}

/// @brief Return a safe child count for scene-node walkers.
static int32_t model_node_child_count(const rt_scene_node3d *node) {
    return node ? model_clamped_array_count(node->children, node->child_count, node->child_capacity)
                : 0;
}

/// @brief Return a safe LOD count for scene-node cloning/ref collection.
static int32_t model_node_lod_count(const rt_scene_node3d *node) {
    return node ? model_clamped_array_count(node->lod_levels, node->lod_count, node->lod_capacity)
                : 0;
}

/// @brief Return a safe scene count for public scene accessors.
static int32_t model_scene_count(const rt_model3d *model) {
    return model ? model_clamped_array_count(model->scenes, model->scene_count, model->scene_capacity)
                 : 0;
}

/// @brief Clamp and persist the scene count before mutating the scene table.
static int32_t model_repair_scene_count(rt_model3d *model) {
    int32_t safe_count = model_scene_count(model);
    if (model)
        model->scene_count = safe_count;
    return safe_count;
}

/// @brief Return a C string only when @p string is a valid runtime string handle.
static const char *model_string_cstr_or_null(rt_string string) {
    if (!string || !rt_string_is_handle(string))
        return NULL;
    return rt_string_cstr(string);
}

/// @brief Convert an importer-reported int64 count into a runtime array count.
static int model_count_i64_to_i32(int64_t count, int32_t *out, const char *trap_msg) {
    if (!out)
        return 0;
    if (count <= 0) {
        *out = 0;
        return 1;
    }
    if (count > INT32_MAX) {
        rt_trap(trap_msg ? trap_msg : "Model3D.Load: imported count is too large");
        *out = 0;
        return 0;
    }
    *out = (int32_t)count;
    return 1;
}

/// @brief Release the GC reference at `*slot` and clear the slot. NULL-safe both ways
/// (`slot == NULL` or `*slot == NULL`). Frees the object only when the release drops the
/// retain count to zero — bystander references stay alive.
static void model_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a temporary local reference (e.g. a freshly-cloned scene-node we just
/// re-parented and no longer need). Wrapper around `model_release_ref` that takes a value
/// rather than a slot, so call sites can write `model_release_local(node)` instead of
/// passing `&node` and worrying about the post-release zero.
static void model_release_local(void *obj) {
    void *tmp = obj;
    model_release_ref(&tmp);
}

/// @brief Release every entry in a dynamically-grown reference array, then free the array
/// storage itself and reset the count. Used by the finalizer to tear down `meshes`,
/// `materials`, `skeletons`, and `animations` lists in one call each.
static void model_release_array(void ***arr, int32_t *count, int32_t *capacity) {
    int32_t release_count;
    if (!arr)
        return;
    if (!*arr) {
        if (count)
            *count = 0;
        if (capacity)
            *capacity = 0;
        return;
    }
    release_count = model_clamped_array_count(*arr, count ? *count : 0, capacity ? *capacity : 0);
    if (count) {
        for (int32_t i = 0; i < release_count; i++)
            model_release_ref(&(*arr)[i]);
        *count = 0;
    }
    if (capacity)
        *capacity = 0;
    free(*arr);
    *arr = NULL;
}

/// @brief Release every scene entry's root, name, and camera list (does not free the array itself).
static void model_release_scenes(rt_model3d *model) {
    if (!model)
        return;
    if (!model->scenes) {
        model->scene_count = 0;
        model->scene_capacity = 0;
        return;
    }
    for (int32_t i = 0, scene_count = model_scene_count(model); i < scene_count; i++) {
        model3d_scene_entry *scene = &model->scenes[i];
        model_release_ref((void **)&scene->root);
        free(scene->name);
        scene->name = NULL;
        model_release_array(&scene->cameras, &scene->camera_count, &scene->camera_capacity);
    }
    free(model->scenes);
    model->scenes = NULL;
    model->scene_count = 0;
    model->scene_capacity = 0;
}

/// @brief GC finalizer for `Model3D`. Drops references on the template scene-graph root,
/// then walks each component list (meshes, materials, skeletons, animations) and releases
/// every entry. Underlying assets that other live owners still reference remain alive;
/// solo-owned assets get freed.
static void rt_model3d_finalize(void *obj) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model)
        return;
    model_release_ref((void **)&model->template_root);
    model_release_scenes(model);
    model_release_array(&model->meshes, &model->mesh_count, &model->mesh_capacity);
    model_release_array(&model->materials, &model->material_count, &model->material_capacity);
    model_release_array(&model->skeletons, &model->skeleton_count, &model->skeleton_capacity);
    model_release_array(&model->animations, &model->animation_count, &model->animation_capacity);
    model_release_array(
        &model->node_animations, &model->node_animation_count, &model->node_animation_capacity);
    model_release_array(&model->cameras, &model->camera_count, &model->camera_capacity);
}

/// @brief Allocate a zeroed `rt_model3d` with finalizer wired and a fresh template-root
/// scene node attached. Traps with a user-visible message on either allocation failure;
/// returns NULL only after trapping so the caller's `goto fail` paths can still run.
static rt_model3d *model_new(void) {
    rt_model3d *model =
        (rt_model3d *)rt_obj_new_i64(RT_G3D_MODEL3D_CLASS_ID, (int64_t)sizeof(rt_model3d));
    if (!model) {
        rt_trap("Model3D.Load: allocation failed");
        return NULL;
    }
    memset(model, 0, sizeof(*model));
    rt_obj_set_finalizer(model, rt_model3d_finalize);
    model->template_root = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!model->template_root) {
        rt_trap("Model3D.Load: template root allocation failed");
        model_release_local(model);
        return NULL;
    }
    return model;
}

/// @brief Ensure `*arr` can hold at least `need` entries, doubling capacity (initial 4)
/// when it grows. Returns 1 on success, 0 on realloc failure (existing array untouched so
/// the caller can safely trap and bail).
static int model_grow_array(void ***arr, int32_t *cap, int32_t need) {
    int32_t new_cap;
    if (need < 0 || !cap || !arr)
        return 0;
    if (*cap < 0)
        *cap = 0;
    if (*cap >= need && *arr)
        return 1;
    if (!*arr)
        *cap = 0;
    new_cap = *cap > 0 ? *cap : 4;
    while (new_cap < need) {
        if (new_cap > INT32_MAX / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }
    if (new_cap < need)
        return 0;
    if ((size_t)new_cap > SIZE_MAX / sizeof(void *))
        return 0;
    void **grown = (void **)realloc(*arr, (size_t)new_cap * sizeof(void *));
    if (!grown)
        return 0;
    *arr = grown;
    *cap = new_cap;
    return 1;
}

static int model_append_ref(
    void ***arr, int32_t *count, int32_t *cap, void *obj, const char *trap_msg);

/// @brief Duplicate @p value, or @p fallback when @p value is NULL/empty (traps on alloc failure).
static char *model_strdup_or(const char *value, const char *fallback) {
    const char *src = (value && value[0] != '\0') ? value : fallback;
    size_t len;
    char *copy;
    if (!src)
        src = "";
    len = strlen(src);
    if (len == SIZE_MAX) {
        rt_trap("Model3D.Load: scene name too long");
        return NULL;
    }
    copy = (char *)malloc(len + 1);
    if (!copy) {
        rt_trap("Model3D.Load: scene name allocation failed");
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

/// @brief Ensure the model's scene array holds at least @p need entries (zero-filled growth).
/// @return 1 on success, 0 on overflow or reallocation failure.
static int model_grow_scenes(rt_model3d *model, int32_t need) {
    model3d_scene_entry *grown;
    int32_t old_capacity;
    int32_t new_cap;
    if (!model || need < 0)
        return 0;
    if (model->scene_capacity < 0)
        model->scene_capacity = 0;
    if (model->scene_capacity >= need && model->scenes)
        return 1;
    if (!model->scenes)
        model->scene_capacity = 0;
    old_capacity = model->scene_capacity;
    new_cap = old_capacity > 0 ? old_capacity : 4;
    while (new_cap < need) {
        if (new_cap > INT32_MAX / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }
    if (new_cap < need)
        return 0;
    if ((size_t)new_cap > SIZE_MAX / sizeof(*model->scenes))
        return 0;
    grown = (model3d_scene_entry *)realloc(model->scenes, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    memset(grown + old_capacity, 0, (size_t)(new_cap - old_capacity) * sizeof(*grown));
    model->scenes = grown;
    model->scene_capacity = new_cap;
    return 1;
}

/// @brief Append a scene (retained @p root + name) to the model, synthesizing a name if empty.
/// @details Uses "default" for the first scene, else "scene_N". Reports the new index via
///          @p out_index. Returns 0 and traps on growth or name-allocation failure.
static int model_append_scene_entry(rt_model3d *model,
                                    const char *name,
                                    rt_scene_node3d *root,
                                    int32_t *out_index) {
    model3d_scene_entry *scene;
    char fallback[64];
    int32_t scene_count;
    if (!model || !root)
        return 0;
    scene_count = model_repair_scene_count(model);
    if (scene_count >= INT32_MAX || !model_grow_scenes(model, scene_count + 1)) {
        rt_trap("Model3D.Load: scene list allocation failed");
        return 0;
    }
    snprintf(fallback,
             sizeof(fallback),
             scene_count == 0 ? "default" : "scene_%d",
             (int)scene_count);
    scene = &model->scenes[scene_count];
    memset(scene, 0, sizeof(*scene));
    rt_obj_retain_maybe(root);
    scene->root = root;
    scene->name = model_strdup_or(name, fallback);
    if (!scene->name) {
        model_release_ref((void **)&scene->root);
        memset(scene, 0, sizeof(*scene));
        return 0;
    }
    if (out_index)
        *out_index = scene_count;
    model->scene_count = scene_count + 1;
    return 1;
}

/// @brief Append a Camera3D to a scene entry's camera list (no-op for NULL args).
static int model_append_scene_camera(model3d_scene_entry *scene, void *camera) {
    if (!scene || !camera)
        return 1;
    return model_append_ref(&scene->cameras,
                            &scene->camera_count,
                            &scene->camera_capacity,
                            camera,
                            "Model3D.Load: scene camera list allocation failed");
}

/// @brief Append a retained reference to `obj` at the tail of `*arr`. Grows storage if
/// needed; traps with `trap_msg` and returns 0 on allocation failure. Always retains `obj`
/// (NULL-safe via `rt_obj_retain_maybe`) so the model owns a stable count for the lifetime
/// of the entry.
static int model_append_ref(
    void ***arr, int32_t *count, int32_t *cap, void *obj, const char *trap_msg) {
    int32_t safe_count;
    if (!obj)
        return 1;
    if (!arr || !count || !cap)
        return 0;
    safe_count = model_repair_array_count(*arr, count, *cap);
    if (safe_count >= INT32_MAX)
        return 0;
    if (!model_grow_array(arr, cap, safe_count + 1)) {
        rt_trap(trap_msg);
        return 0;
    }
    rt_obj_retain_maybe(obj);
    (*arr)[safe_count] = obj;
    *count = safe_count + 1;
    return 1;
}

/// @brief Append `obj` only if the array does not already contain that exact pointer.
/// Used for collecting unique mesh / material references off a scene-graph walk where the
/// same asset may be referenced from many nodes. NULL `obj` is treated as success-noop.
static int model_append_unique_ref(
    void ***arr, int32_t *count, int32_t *cap, void *obj, const char *trap_msg) {
    int32_t safe_count;
    if (!obj)
        return 1;
    if (!arr || !count || !cap)
        return 0;
    safe_count = model_repair_array_count(*arr, count, *cap);
    for (int32_t i = 0; i < safe_count; i++) {
        if ((*arr)[i] == obj)
            return 1;
    }
    return model_append_ref(arr, count, cap, obj, trap_msg);
}

/// @brief Recursively count nodes in the subtree rooted at `node`, including `node`
/// itself. Used by `rt_model3d_get_node_count` (which subtracts 1 to exclude the synthetic
/// template root from the user-visible count).
static int32_t model_count_subtree(const rt_scene_node3d *node) {
    const rt_scene_node3d **stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 0;
    int32_t total = 0;
    if (!node)
        return 0;
    stack_capacity = 32;
    stack = (const rt_scene_node3d **)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack) {
        rt_trap("Model3D: node-count stack allocation failed");
        return 0;
    }
    stack[stack_count++] = node;
    while (stack_count > 0) {
        const rt_scene_node3d *current = stack[--stack_count];
        if (!current)
            continue;
        if (total >= MODEL3D_MAX_WALK_NODES || total == INT32_MAX) {
            free(stack);
            rt_trap("Model3D: too many nodes");
            return 0;
        }
        total++;
        int32_t child_count = model_node_child_count(current);
        for (int32_t i = 0; i < child_count; i++) {
            if (stack_count >= stack_capacity) {
                if (stack_capacity > INT32_MAX / 2 ||
                    (size_t)(stack_capacity * 2) > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    rt_trap("Model3D: too many nodes");
                    return 0;
                }
                int32_t new_capacity = stack_capacity * 2;
                const rt_scene_node3d **grown =
                    (const rt_scene_node3d **)realloc(stack, (size_t)new_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    rt_trap("Model3D: node-count stack allocation failed");
                    return 0;
                }
                stack = grown;
                stack_capacity = new_capacity;
            }
            stack[stack_count++] = current->children[i];
        }
    }
    free(stack);
    return total;
}

/// @brief Derive a friendly name for the template root from the source asset's file path
/// — strips the directory prefix (handles both `/` and `\` separators so a Windows-authored
/// path works on macOS/Linux) and the file extension. Falls back to a no-op when the
/// resulting name would be empty. Truncates at 127 bytes to fit the local stack buffer.
static void model_set_root_name(rt_scene_node3d *root, const char *path_cstr) {
    const char *name = path_cstr;
    const char *slash;
    const char *dot;
    size_t len;
    char buf[128];

    if (!root || !path_cstr)
        return;

    slash = strrchr(path_cstr, '/');
    if (!slash)
        slash = strrchr(path_cstr, '\\');
    if (slash && slash[1] != '\0')
        name = slash + 1;

    dot = strrchr(name, '.');
    len = dot && dot > name ? (size_t)(dot - name) : strlen(name);
    if (len == 0)
        return;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, name, len);
    buf[len] = '\0';
    rt_scene_node3d_set_name(root, rt_const_cstr(buf));
}

/// @brief Recursive deep-copy of a scene-node subtree. Clones TRS, world-dirty flag,
/// visibility, AABB / bounding sphere, and per-LOD entries; *retains* (does not clone)
/// shared assets — `mesh`, `material`, `name`, and per-LOD meshes — so the cloned subtree
/// shares static geometry with the template instead of duplicating GPU resources. When
/// requested, morph-enabled meshes are cloned with independent MorphTarget3D state so
/// blend-shape weights can diverge per instance. Children are cloned recursively and
/// parented to the clone via `rt_scene_node3d_try_add_child`, then the caller's local reference
/// is released so the parent owns the only retain.
static int model_clone_mutable_mesh(void *mesh, void **out_mesh) {
    rt_mesh3d *src = (rt_mesh3d *)rt_g3d_checked_or_null(mesh, RT_G3D_MESH3D_CLASS_ID);
    void *mesh_clone;
    if (out_mesh)
        *out_mesh = NULL;
    if (!src || !src->morph_targets_ref)
        return 1;
    mesh_clone = rt_mesh3d_clone(mesh);
    if (!mesh_clone)
        return 0;
    rt_mesh3d *cloned = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_clone, RT_G3D_MESH3D_CLASS_ID);
    if (!cloned || !cloned->morph_targets_ref) {
        model_release_local(mesh_clone);
        return 0;
    }
    if (out_mesh)
        *out_mesh = mesh_clone;
    else
        model_release_local(mesh_clone);
    return 1;
}

static float model_finite_float_or(float value, float fallback) {
    return isfinite(value) ? value : fallback;
}

static double model_nonnegative_double_or(double value, double fallback) {
    if (!isfinite(value) || value < 0.0)
        return fallback;
    return value;
}

static void model_clone_sanitize_bounds(rt_scene_node3d *dst, const rt_scene_node3d *src) {
    int valid = 1;
    if (!dst || !src)
        return;
    for (int32_t i = 0; i < 3; i++) {
        if (!isfinite(src->aabb_min[i]) || !isfinite(src->aabb_max[i]) ||
            src->aabb_min[i] > src->aabb_max[i]) {
            valid = 0;
            break;
        }
    }
    if (!valid) {
        memset(dst->aabb_min, 0, sizeof(dst->aabb_min));
        memset(dst->aabb_max, 0, sizeof(dst->aabb_max));
        dst->bsphere_radius = 0.0f;
        return;
    }
    for (int32_t i = 0; i < 3; i++) {
        dst->aabb_min[i] = model_finite_float_or(src->aabb_min[i], 0.0f);
        dst->aabb_max[i] = model_finite_float_or(src->aabb_max[i], dst->aabb_min[i]);
    }
    dst->bsphere_radius =
        (isfinite(src->bsphere_radius) && src->bsphere_radius >= 0.0f) ? src->bsphere_radius : 0.0f;
}

/// @brief Clone one node without its children. Returns NULL and releases partial state on OOM.
static rt_scene_node3d *model_clone_node_shallow(const rt_scene_node3d *src,
                                                 int clone_mutable_meshes) {
    rt_scene_node3d *dst;
    if (!src)
        return NULL;

    dst = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!dst)
        return NULL;

    dst->position[0] = scene3d_clamp_abs_or(src->position[0], 0.0);
    dst->position[1] = scene3d_clamp_abs_or(src->position[1], 0.0);
    dst->position[2] = scene3d_clamp_abs_or(src->position[2], 0.0);
    dst->rotation[0] = src->rotation[0];
    dst->rotation[1] = src->rotation[1];
    dst->rotation[2] = src->rotation[2];
    dst->rotation[3] = src->rotation[3];
    scene3d_quat_normalize_local(dst->rotation);
    dst->scale_xyz[0] = scene3d_scale_or_unit(src->scale_xyz[0]);
    dst->scale_xyz[1] = scene3d_scale_or_unit(src->scale_xyz[1]);
    dst->scale_xyz[2] = scene3d_scale_or_unit(src->scale_xyz[2]);
    dst->world_dirty = 1;
    dst->import_index = src->import_index;
    dst->visible = src->visible ? 1 : 0;
    dst->auto_lod_enabled = src->auto_lod_enabled ? 1 : 0;
    dst->auto_lod_screen_error_px = model_nonnegative_double_or(src->auto_lod_screen_error_px, 8.0);
    if (dst->auto_lod_screen_error_px < 1.0)
        dst->auto_lod_screen_error_px = 1.0;
    if (dst->auto_lod_screen_error_px > 1000000.0)
        dst->auto_lod_screen_error_px = 1000000.0;

    model_clone_sanitize_bounds(dst, src);

    if (src->mesh) {
        void *src_mesh = rt_g3d_checked_or_null(src->mesh, RT_G3D_MESH3D_CLASS_ID);
        void *mesh_clone = NULL;
        if (src_mesh && clone_mutable_meshes && !model_clone_mutable_mesh(src_mesh, &mesh_clone)) {
            model_release_local(dst);
            return NULL;
        }
        if (mesh_clone) {
            dst->mesh = mesh_clone;
        } else if (src_mesh) {
            rt_obj_retain_maybe(src_mesh);
            dst->mesh = src_mesh;
        }
    }
    if (src->material) {
        rt_obj_retain_maybe(src->material);
        dst->material = src->material;
    }
    if (src->light) {
        rt_obj_retain_maybe(src->light);
        dst->light = src->light;
    }
    if (src->name) {
        rt_obj_retain_maybe(src->name);
        dst->name = src->name;
    }

    {
        int32_t lod_count = model_node_lod_count(src);
        if (lod_count <= 0)
            goto after_lods;
        if ((size_t)lod_count > SIZE_MAX / sizeof(*dst->lod_levels)) {
            rt_trap("Model3D: lod allocation overflow");
            model_release_local(dst);
            return NULL;
        }
        dst->lod_levels = calloc((size_t)lod_count, sizeof(*dst->lod_levels));
        if (!dst->lod_levels) {
            rt_trap("Model3D: lod allocation failed");
            model_release_local(dst);
            return NULL;
        }
        dst->lod_capacity = lod_count;
        for (int32_t i = 0; i < lod_count; i++) {
            void *lod_mesh_clone = NULL;
            void *lod_mesh =
                rt_g3d_checked_or_null(src->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID);
            double distance = model_nonnegative_double_or(src->lod_levels[i].distance, 0.0);
            if (dst->lod_count > 0 && distance < dst->lod_levels[dst->lod_count - 1].distance)
                distance = dst->lod_levels[dst->lod_count - 1].distance;
            if (!lod_mesh)
                continue;
            if (clone_mutable_meshes &&
                !model_clone_mutable_mesh(lod_mesh, &lod_mesh_clone)) {
                model_release_local(dst);
                return NULL;
            }
            dst->lod_levels[dst->lod_count].distance = distance;
            if (lod_mesh_clone) {
                dst->lod_levels[dst->lod_count].mesh = lod_mesh_clone;
            } else {
                dst->lod_levels[dst->lod_count].mesh = lod_mesh;
                rt_obj_retain_maybe(dst->lod_levels[dst->lod_count].mesh);
            }
            dst->lod_count++;
        }
    }
after_lods:

    if (src->has_impostor && src->impostor_pixels) {
        double impostor_distance = model_nonnegative_double_or(src->impostor_distance, 0.0);
        rt_scene_node3d_set_impostor(dst, impostor_distance, src->impostor_pixels);
    }

    return dst;
}

typedef struct model_clone_frame {
    const rt_scene_node3d *src;
    rt_scene_node3d *dst;
    int32_t next_child;
} model_clone_frame;

/// @brief Iterative deep-copy of a scene-node subtree.
static rt_scene_node3d *model_clone_node(const rt_scene_node3d *src, int clone_mutable_meshes) {
    rt_scene_node3d *root;
    model_clone_frame *stack;
    int32_t stack_count = 0;
    int32_t stack_capacity = 32;
    int32_t visited_count = 1;
    if (!src)
        return NULL;
    root = model_clone_node_shallow(src, clone_mutable_meshes);
    if (!root)
        return NULL;
    stack = (model_clone_frame *)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack) {
        rt_trap("Model3D: clone stack allocation failed");
        model_release_local(root);
        return NULL;
    }
    stack[stack_count++] = (model_clone_frame){src, root, 0};
    while (stack_count > 0) {
        model_clone_frame *frame = &stack[stack_count - 1];
        int32_t child_count = model_node_child_count(frame->src);
        if (!frame->src || frame->next_child >= child_count) {
            stack_count--;
            continue;
        }
        const rt_scene_node3d *src_child = frame->src->children[frame->next_child++];
        if (!src_child)
            continue;
        if (visited_count >= MODEL3D_MAX_WALK_NODES) {
            free(stack);
            model_release_local(root);
            rt_trap("Model3D: too many nodes to clone");
            return NULL;
        }
        visited_count++;
        rt_scene_node3d *dst_child = model_clone_node_shallow(src_child, clone_mutable_meshes);
        if (!dst_child) {
            free(stack);
            model_release_local(root);
            return NULL;
        }
        if (!rt_scene_node3d_try_add_child(frame->dst, dst_child)) {
            model_release_local(dst_child);
            free(stack);
            model_release_local(root);
            return NULL;
        }
        model_release_local(dst_child);
        if (stack_count >= stack_capacity) {
            if (stack_capacity > INT32_MAX / 2 ||
                (size_t)(stack_capacity * 2) > SIZE_MAX / sizeof(*stack)) {
                free(stack);
                model_release_local(root);
                rt_trap("Model3D: too many nodes to clone");
                return NULL;
            }
            int32_t new_capacity = stack_capacity * 2;
            model_clone_frame *grown =
                (model_clone_frame *)realloc(stack, (size_t)new_capacity * sizeof(*stack));
            if (!grown) {
                free(stack);
                model_release_local(root);
                rt_trap("Model3D: clone stack allocation failed");
                return NULL;
            }
            stack = grown;
            stack_capacity = new_capacity;
        }
        stack[stack_count++] = (model_clone_frame){src_child, dst_child, 0};
    }
    free(stack);
    return root;
}

/// @brief Clone each child of `src_root` and attach to `dst_root`. Used by `Model3D.Load`
/// to graft the imported scene's top-level children under the synthesized template root
/// *without* copying the import's own root (which often carries loader-specific metadata
/// we don't want to leak into the user-visible tree).
static int model_clone_children_to_root(rt_scene_node3d *dst_root,
                                        const rt_scene_node3d *src_root) {
    if (!dst_root || !src_root)
        return 0;
    for (int32_t i = 0, child_count = model_node_child_count(src_root); i < child_count; i++) {
        if (!src_root->children[i])
            continue;
        rt_scene_node3d *child = model_clone_node(src_root->children[i], 0);
        if (!child)
            return 0;
        if (!rt_scene_node3d_try_add_child(dst_root, child)) {
            model_release_local(child);
            return 0;
        }
        model_release_local(child);
    }
    return 1;
}

static int model_collect_scene_refs(rt_model3d *model, const rt_scene_node3d *node);

/// @brief Collect mesh/material references from every top-level child of the template root.
/// @details Delegates to `model_collect_scene_refs` for each immediate child so the template
///   root node itself is not registered (it is a synthetic container, not a content node).
///   Returns 1 on success, 0 if any registration step fails due to an allocation error.
static int model_collect_template_refs(rt_model3d *model) {
    if (!model || !model->template_root)
        return 1;
    for (int32_t i = 0, child_count = model_node_child_count(model->template_root); i < child_count;
         i++) {
        if (!model_collect_scene_refs(model, model->template_root->children[i]))
            return 0;
    }
    return 1;
}

/// @brief Wire a default skeletal AnimationController3D onto `root` when one is present in the
/// model.
/// @details Creates an AnimController3D backed by the first skeleton, adds one state per loaded
///   animation clip (naming it after its rt_animation3d name, with a "animation_<i>" fallback for
///   unnamed clips), then plays the first state so the model arrives in a live pose immediately
///   after instantiation.  No-op when the root already has a bound animator, there are no
///   skeletons, or there are no animation clips to add.  The controller's local reference is
///   released after binding because the root's `bound_animator` slot owns the retain.
static void model_bind_default_animator(rt_model3d *model, rt_scene_node3d *root) {
    void *controller;
    int added_any = 0;
    void *skeleton = NULL;
    int32_t valid_skeleton_count = 0;
    int32_t skeleton_count =
        model ? model_clamped_array_count(model->skeletons, model->skeleton_count, model->skeleton_capacity)
              : 0;
    int32_t animation_count =
        model ? model_clamped_array_count(
                    model->animations, model->animation_count, model->animation_capacity)
              : 0;
    if (!model || !root || root->bound_animator || skeleton_count <= 0 || animation_count <= 0)
        return;
    for (int32_t i = 0; i < skeleton_count; i++) {
        void *candidate = rt_g3d_checked_or_null(model->skeletons[i], RT_G3D_SKELETON3D_CLASS_ID);
        if (!candidate)
            continue;
        skeleton = candidate;
        valid_skeleton_count++;
        if (valid_skeleton_count > 1)
            return;
    }
    if (valid_skeleton_count != 1 || !skeleton)
        return;
    controller = rt_anim_controller3d_new(skeleton);
    if (!controller)
        return;
    for (int32_t i = 0; i < animation_count; i++) {
        rt_string anim_name;
        const char *name;
        char fallback[64];
        char unique_fallback[64];
        rt_string state_name;
        int64_t state_index;
        int64_t before_count;
        void *animation = rt_g3d_checked_or_null(model->animations[i], RT_G3D_ANIMATION3D_CLASS_ID);
        if (!animation)
            continue;
        anim_name = rt_animation3d_get_name(animation);
        name = anim_name ? rt_string_cstr(anim_name) : NULL;
        if (!name || name[0] == '\0') {
            snprintf(fallback, sizeof(fallback), "animation_%d", (int)i);
            name = fallback;
        }
        state_name = rt_const_cstr(name);
        before_count = rt_anim_controller3d_get_state_count(controller);
        state_index = rt_anim_controller3d_add_state(controller, state_name, animation);
        if (state_index >= 0 && rt_anim_controller3d_get_state_count(controller) == before_count &&
            state_index < before_count) {
            snprintf(unique_fallback, sizeof(unique_fallback), "animation_%d", (int)i);
            if (strcmp(unique_fallback, name) != 0) {
                state_name = rt_const_cstr(unique_fallback);
                state_index = rt_anim_controller3d_add_state(controller, state_name, animation);
                name = unique_fallback;
            }
        }
        if (state_index >= 0 && !added_any) {
            rt_anim_controller3d_play(controller, state_name);
            added_any = 1;
        }
    }
    if (added_any)
        rt_scene_node3d_bind_animator(root, controller);
    model_release_local(controller);
}

/// @brief Wire a default NodeAnimator3D onto `root` using any node-animation clips in the model.
/// @details Creates a NodeAnimator3D from the full `node_animations` array so scene-node TRS
///   animations (camera paths, object tracks) play automatically after instantiation, mirroring
///   the same auto-play convenience as `model_bind_default_animator` for skeletal animations.
///   No-op when a node animator is already bound or when no node-animation clips were loaded.
///   The animator's local reference is released after binding.
static void model_bind_default_node_animator(rt_model3d *model, rt_scene_node3d *root) {
    void *animator;
    void **clips = NULL;
    int32_t live_count = 0;
    int32_t node_animation_count =
        model ? model_clamped_array_count(model->node_animations,
                                          model->node_animation_count,
                                          model->node_animation_capacity)
              : 0;
    if (!model || !root || root->bound_node_animator || node_animation_count <= 0)
        return;
    for (int32_t i = 0; i < node_animation_count; i++) {
        if (rt_g3d_checked_or_null(model->node_animations[i], RT_G3D_NODEANIMATION3D_CLASS_ID))
            live_count++;
    }
    if (live_count <= 0)
        return;
    if (live_count == node_animation_count) {
        animator = rt_node_animator3d_new_from_clips(model->node_animations, node_animation_count);
    } else {
        if ((size_t)live_count > SIZE_MAX / sizeof(*clips)) {
            rt_trap("Model3D.Instantiate: node-animation list too large");
            return;
        }
        clips = (void **)malloc((size_t)live_count * sizeof(*clips));
        if (!clips) {
            rt_trap("Model3D.Instantiate: node-animation list allocation failed");
            return;
        }
        for (int32_t i = 0, out_i = 0; i < node_animation_count; i++) {
            void *clip =
                rt_g3d_checked_or_null(model->node_animations[i], RT_G3D_NODEANIMATION3D_CLASS_ID);
            if (clip)
                clips[out_i++] = clip;
        }
        animator = rt_node_animator3d_new_from_clips(clips, live_count);
        free(clips);
    }
    if (!animator)
        return;
    rt_scene_node3d_bind_node_animator(root, animator);
    model_release_local(animator);
}

/// @brief Walk the scene subtree at `node` and register every unique `mesh` / `material`
/// reference (including those stored on LOD entries) into the model's component arrays.
/// Used after cloning imported scene-graph nodes so consumers can enumerate the assets
/// without re-walking the tree. Recurses into every child; duplicates are skipped by
/// `model_append_unique_ref`.
static int model_collect_scene_refs(rt_model3d *model, const rt_scene_node3d *node) {
    const rt_scene_node3d **stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 32;
    int32_t visited_count = 1;
    static const char *too_many_msg = "Model3D.Load: too many scene nodes";
    if (!model || !node)
        return 1;
    stack = (const rt_scene_node3d **)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack) {
        rt_trap("Model3D.Load: scene-ref walk allocation failed");
        return 0;
    }
    stack[stack_count++] = node;
    while (stack_count > 0) {
        const rt_scene_node3d *current = stack[--stack_count];
        void *mesh;
        void *material;
        if (!current)
            continue;
        mesh = rt_g3d_checked_or_null(current->mesh, RT_G3D_MESH3D_CLASS_ID);
        material = rt_g3d_checked_or_null(current->material, RT_G3D_MATERIAL3D_CLASS_ID);
        if (!model_append_unique_ref(&model->meshes,
                                     &model->mesh_count,
                                     &model->mesh_capacity,
                                     mesh,
                                     "Model3D.Load: mesh list allocation failed")) {
            free(stack);
            return 0;
        }
        if (!model_append_unique_ref(&model->materials,
                                     &model->material_count,
                                     &model->material_capacity,
                                     material,
                                     "Model3D.Load: material list allocation failed")) {
            free(stack);
            return 0;
        }
        int32_t lod_count = model_node_lod_count(current);
        int32_t child_count = model_node_child_count(current);
        for (int32_t i = 0; i < lod_count; i++) {
            void *lod_mesh =
                rt_g3d_checked_or_null(current->lod_levels[i].mesh, RT_G3D_MESH3D_CLASS_ID);
            if (!model_append_unique_ref(&model->meshes,
                                         &model->mesh_count,
                                         &model->mesh_capacity,
                                         lod_mesh,
                                         "Model3D.Load: mesh list allocation failed")) {
                free(stack);
                return 0;
            }
        }
        for (int32_t i = 0; i < child_count; i++) {
            const rt_scene_node3d *child = current->children[i];
            if (!child)
                continue;
            if (visited_count >= MODEL3D_MAX_WALK_NODES) {
                free(stack);
                rt_trap(too_many_msg);
                return 0;
            }
            if (stack_count >= stack_capacity) {
                if (stack_capacity > INT32_MAX / 2 ||
                    (size_t)(stack_capacity * 2) > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    rt_trap(too_many_msg);
                    return 0;
                }
                int32_t new_capacity = stack_capacity * 2;
                const rt_scene_node3d **grown =
                    (const rt_scene_node3d **)realloc(stack, (size_t)new_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    rt_trap("Model3D.Load: scene-ref walk allocation failed");
                    return 0;
                }
                stack = grown;
                stack_capacity = new_capacity;
            }
            stack[stack_count++] = child;
            visited_count++;
        }
    }
    free(stack);
    return 1;
}

/// @brief Find a named node using Model3D's clamped child-count walker.
static rt_scene_node3d *model_find_node_safe(rt_scene_node3d *root, rt_string name) {
    rt_scene_node3d **stack = NULL;
    int32_t stack_count = 0;
    int32_t stack_capacity = 32;
    int32_t visited_count = 1;
    const char *needle = model_string_cstr_or_null(name);
    if (!root || !needle)
        return NULL;
    stack = (rt_scene_node3d **)malloc((size_t)stack_capacity * sizeof(*stack));
    if (!stack) {
        rt_trap("Model3D.FindNode: search stack allocation failed");
        return NULL;
    }
    stack[stack_count++] = root;
    while (stack_count > 0) {
        rt_scene_node3d *current = stack[--stack_count];
        int32_t child_count;
        if (!current)
            continue;
        if (current->name) {
            const char *node_name = model_string_cstr_or_null(current->name);
            if (node_name && strcmp(node_name, needle) == 0) {
                free(stack);
                return current;
            }
        }
        child_count = model_node_child_count(current);
        for (int32_t i = 0; i < child_count; i++) {
            rt_scene_node3d *child = current->children[i];
            if (!child)
                continue;
            if (visited_count >= MODEL3D_MAX_WALK_NODES) {
                free(stack);
                rt_trap("Model3D.FindNode: too many nodes");
                return NULL;
            }
            if (stack_count >= stack_capacity) {
                if (stack_capacity > INT32_MAX / 2 ||
                    (size_t)(stack_capacity * 2) > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    rt_trap("Model3D.FindNode: too many nodes");
                    return NULL;
                }
                int32_t new_capacity = stack_capacity * 2;
                rt_scene_node3d **grown =
                    (rt_scene_node3d **)realloc(stack, (size_t)new_capacity * sizeof(*stack));
                if (!grown) {
                    free(stack);
                    rt_trap("Model3D.FindNode: search stack allocation failed");
                    return NULL;
                }
                stack = grown;
                stack_capacity = new_capacity;
            }
            stack[stack_count++] = child;
            visited_count++;
        }
    }
    free(stack);
    return NULL;
}

/// @brief Case-insensitive extension match. Returns 1 when `path_cstr` ends with `ext`
/// (which must start with `.`), ignoring ASCII case. Used by `Model3D.Load` to pick the
/// right importer (.vscn / .gltf / .glb / .fbx) without relying on platform-dependent
/// filename normalization.
static int model_has_ext(const char *path_cstr, const char *ext) {
    const char *dot;
    if (!path_cstr || !ext)
        return 0;
    dot = strrchr(path_cstr, '.');
    if (!dot)
        return 0;
    while (*dot && *ext) {
        if (tolower((unsigned char)*dot) != tolower((unsigned char)*ext))
            return 0;
        ++dot;
        ++ext;
    }
    return *dot == '\0' && *ext == '\0';
}

/// @brief Synthesize a minimal scene-graph when the imported asset only exposes a bare
/// mesh/material list (no native scene hierarchy). One `mesh_<i>` child per mesh gets
/// parented under the template root, with material assignment: single shared material
/// if the asset has exactly one, otherwise parallel indexing by mesh slot. No-op when
/// the template root already has children (i.e. the importer gave us a real hierarchy).
static int model_build_synth_mesh_nodes(rt_model3d *model) {
    char name[64];
    if (!model || !model->template_root)
        return 0;
    if (model_node_child_count(model->template_root) > 0)
        return 1;

    int32_t mesh_count =
        model_clamped_array_count(model->meshes, model->mesh_count, model->mesh_capacity);
    int32_t material_count =
        model_clamped_array_count(model->materials, model->material_count, model->material_capacity);
    for (int32_t i = 0; i < mesh_count; i++) {
        rt_scene_node3d *node = (rt_scene_node3d *)rt_scene_node3d_new();
        void *material = NULL;
        if (!model->meshes[i])
            continue;
        if (!node)
            return 0;
        snprintf(name, sizeof(name), "mesh_%d", (int)i);
        rt_scene_node3d_set_name(node, rt_const_cstr(name));
        rt_scene_node3d_set_mesh(node, model->meshes[i]);
        if (material_count == 1)
            material = model->materials[0];
        else if (i < material_count)
            material = model->materials[i];
        if (material)
            rt_scene_node3d_set_material(node, material);
        if (!rt_scene_node3d_try_add_child(model->template_root, node)) {
            model_release_local(node);
            return 0;
        }
        model_release_local(node);
    }
    return 1;
}

typedef struct {
    char *name;
    void *material;
} model_obj_material_entry;

typedef struct {
    model_obj_material_entry *entries;
    int32_t count;
    int32_t capacity;
} model_obj_material_table;

/// @brief Safe live entry count for the OBJ material table.
static int32_t model_obj_material_table_count(const model_obj_material_table *table) {
    return table ? model_clamped_array_count(table->entries, table->count, table->capacity) : 0;
}

/// @brief Clamp and persist the OBJ material table count before mutation.
static int32_t model_obj_material_table_repair_count(model_obj_material_table *table) {
    int32_t count = model_obj_material_table_count(table);
    if (table)
        table->count = count;
    return count;
}

/// @brief Duplicate the substring [@p start, @p end), trimming leading/trailing ASCII
///   whitespace. Returns NULL on allocation failure.
static char *model_strdup_range(const char *start, const char *end) {
    size_t len;
    char *copy;
    if (!start)
        start = "";
    if (!end)
        end = start + strlen(start);
    while (start < end && (*start == ' ' || *start == '\t'))
        start++;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;
    len = (size_t)(end - start);
    copy = (char *)malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, start, len);
    copy[len] = '\0';
    return copy;
}

/// @brief Free an OBJ material table: release each entry's name and material reference, then
///   the entry array, and zero the table.
static void model_obj_material_table_free(model_obj_material_table *table) {
    if (!table)
        return;
    if (!table->entries) {
        table->count = 0;
        table->capacity = 0;
        return;
    }
    for (int32_t i = 0, count = model_obj_material_table_count(table); i < count; i++) {
        free(table->entries[i].name);
        model_release_ref(&table->entries[i].material);
    }
    free(table->entries);
    memset(table, 0, sizeof(*table));
}

/// @brief Append a (name, material) entry, de-duplicating by name (on a duplicate, frees the
///   passed @p name and releases @p material). Takes ownership of @p name and the material
///   reference and grows the table as needed.
/// @return 1 on success or a harmless duplicate; 0 on allocation failure.
static int model_obj_material_table_append(model_obj_material_table *table,
                                           char *name,
                                           void *material) {
    model_obj_material_entry *grown;
    int32_t count;
    int32_t old_capacity;
    if (!table || !name || !material)
        return 0;
    count = model_obj_material_table_repair_count(table);
    for (int32_t i = 0; i < count; i++) {
        if (strcmp(table->entries[i].name ? table->entries[i].name : "", name) == 0) {
            free(name);
            model_release_local(material);
            return 1;
        }
    }
    if (!table->entries)
        table->capacity = 0;
    if (table->capacity < 0)
        table->capacity = 0;
    old_capacity = table->capacity;
    if (count >= table->capacity) {
        int32_t new_capacity = old_capacity > 0 ? old_capacity * 2 : 8;
        if (old_capacity > INT32_MAX / 2 ||
            (size_t)new_capacity > SIZE_MAX / sizeof(*table->entries))
            return 0;
        grown = (model_obj_material_entry *)realloc(table->entries,
                                                    (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        memset(grown + old_capacity, 0, (size_t)(new_capacity - old_capacity) * sizeof(*grown));
        table->entries = grown;
        table->capacity = new_capacity;
    }
    table->entries[count].name = name;
    table->entries[count].material = material;
    table->count = count + 1;
    return 1;
}

/// @brief Find a material by name in the OBJ material table; NULL if absent.
static void *model_obj_material_lookup(const model_obj_material_table *table, const char *name) {
    if (!table || !name || !*name)
        return NULL;
    for (int32_t i = 0, count = model_obj_material_table_count(table); i < count; i++) {
        if (strcmp(table->entries[i].name ? table->entries[i].name : "", name) == 0)
            return table->entries[i].material;
    }
    return NULL;
}

/// @brief Write the parent-directory portion of @p path (up to the last '/' or '\\') into
///   @p out; empty if @p path has no separator.
static void model_parent_dir(char *out, size_t out_size, const char *path) {
    const char *last;
    size_t len;
    if (!out || out_size == 0)
        return;
    out[0] = '\0';
    if (!path)
        return;
    {
        const char *slash = strrchr(path, '/');
        const char *backslash = strrchr(path, '\\');
        last = (!slash || (backslash && backslash > slash)) ? backslash : slash;
    }
    if (!last)
        return;
    len = (size_t)(last - path);
    if (len == 0 && (path[0] == '/' || path[0] == '\\'))
        len = 1;
    if (len >= out_size)
        len = out_size - 1u;
    memcpy(out, path, len);
    out[len] = '\0';
}

/// @brief True if @p path is absolute: a leading '/' or '\\', or a Windows drive letter ("C:").
static int model_is_absolute_path(const char *path) {
    if (!path || !*path)
        return 0;
    if (path[0] == '/' || path[0] == '\\')
        return 1;
    return isalpha((unsigned char)path[0]) && path[1] == ':';
}

/// @brief Sanitize an asset reference into a safe relative path in @p out (path-traversal
///   guard): converts '\\' to '/', then rejects absolute paths, URI scheme separators, and any
///   ".." segment, collapsing "." and redundant slashes.
/// @return Non-zero if a non-empty safe path was written; 0 if empty, unsafe, or overflowing.
static int model_normalize_relative_asset_ref(const char *src, char *out, size_t out_size) {
    char normalized[1024];
    const char *p;
    size_t ni = 0;
    size_t out_len = 0;
    int wrote_segment = 0;
    if (!src || !*src || !out || out_size == 0)
        return 0;
    while (*src && ni + 1u < sizeof(normalized)) {
        char ch = *src++;
        unsigned char uch = (unsigned char)ch;
        if (ch == ':')
            return 0;
        if (uch < 0x20u || uch == 0x7fu)
            return 0;
        normalized[ni++] = ch == '\\' ? '/' : ch;
    }
    if (*src)
        return 0;
    normalized[ni] = '\0';
    if (model_is_absolute_path(normalized) || strstr(normalized, "://"))
        return 0;
    p = normalized;
    out[0] = '\0';
    while (*p) {
        const char *seg;
        size_t seg_len;
        while (*p == '/')
            p++;
        seg = p;
        while (*p && *p != '/')
            p++;
        seg_len = (size_t)(p - seg);
        if (seg_len == 0 || (seg_len == 1 && seg[0] == '.'))
            continue;
        if (seg_len == 2 && seg[0] == '.' && seg[1] == '.')
            return 0;
        if (wrote_segment) {
            if (out_len + 1u >= out_size)
                return 0;
            out[out_len++] = '/';
        }
        if (seg_len > out_size - 1u - out_len)
            return 0;
        memcpy(out + out_len, seg, seg_len);
        out_len += seg_len;
        out[out_len] = '\0';
        wrote_segment = 1;
    }
    return wrote_segment;
}

/// @brief Join @p dir and @p leaf with a '/' separator into @p out (just @p leaf when @p dir
///   is empty), bounds-checked. @return 1 on success, 0 on bad input or overflow.
static int model_join_path(char *out, size_t out_size, const char *dir, const char *leaf) {
    size_t dir_len;
    size_t leaf_len;
    if (!out || out_size == 0 || !leaf || !*leaf)
        return 0;
    leaf_len = strlen(leaf);
    if (!dir || !*dir) {
        if (leaf_len >= out_size)
            return 0;
        memcpy(out, leaf, leaf_len);
        out[leaf_len] = '\0';
        return 1;
    }
    dir_len = strlen(dir);
    if (dir_len > 0 && (dir[dir_len - 1u] == '/' || dir[dir_len - 1u] == '\\')) {
        if (dir_len + leaf_len >= out_size)
            return 0;
        memcpy(out, dir, dir_len);
        memcpy(out + dir_len, leaf, leaf_len);
        out[dir_len + leaf_len] = '\0';
        return 1;
    }
    if (dir_len + 1u + leaf_len >= out_size)
        return 0;
    memcpy(out, dir, dir_len);
    out[dir_len++] = '/';
    memcpy(out + dir_len, leaf, leaf_len);
    out[dir_len + leaf_len] = '\0';
    return 1;
}

/// @brief Advance past leading spaces and tabs in @p p (NULL-safe).
/// @return The first non-blank character, or @p p unchanged when it is NULL.
static const char *model_obj_skip_ws(const char *p) {
    while (p && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

/// @brief Test whether the remainder of @p p (after leading blanks) carries no further tokens —
///   only trailing whitespace, a newline, or an OBJ/MTL "#" comment follows.
/// @return Non-zero when the rest of the line is empty.
static int model_obj_line_tail_done(const char *p) {
    p = model_obj_skip_ws(p);
    return !p || *p == '\0' || *p == '\n' || *p == '\r' || *p == '#';
}

/// @brief Parse one whitespace-delimited numeric token from *@p cursor into @p out, advancing
///   @p cursor past it. Rejects tokens that fill the 128-byte scratch buffer or parse to a
///   non-finite value.
/// @return Non-zero on success; 0 at end-of-line, on a "#" comment, or on a malformed/oversized token.
static int model_obj_parse_double_token(const char **cursor, double *out) {
    char token[128];
    const char *start;
    size_t len;
    if (!cursor || !*cursor || !out)
        return 0;
    start = model_obj_skip_ws(*cursor);
    if (!start || *start == '\0' || *start == '\n' || *start == '\r' || *start == '#')
        return 0;
    *cursor = start;
    while (**cursor && **cursor != ' ' && **cursor != '\t' && **cursor != '\n' &&
           **cursor != '\r' && **cursor != '#')
        (*cursor)++;
    len = (size_t)(*cursor - start);
    if (len == 0 || len >= sizeof(token))
        return 0;
    memcpy(token, start, len);
    token[len] = '\0';
    return rt_parse_double(token, out) == 0 && isfinite(*out);
}

/// @brief Parse a line carrying exactly one double (e.g. an MTL scalar such as "Ns"/"d"): one
///   token followed only by blanks or a comment.
/// @return Non-zero when @p a was read and nothing but trailing whitespace remains.
static int model_obj_parse_double1(const char *p, double *a) {
    const char *cursor = p;
    return model_obj_parse_double_token(&cursor, a) && model_obj_line_tail_done(cursor);
}

/// @brief Parse a line carrying exactly three doubles (e.g. an OBJ "v"/"vn" position/normal or an
///   MTL "Kd"/"Ka"/"Ks" color triple).
/// @return Non-zero when @p a, @p b and @p c were all read and nothing but trailing whitespace remains.
static int model_obj_parse_double3(const char *p, double *a, double *b, double *c) {
    const char *cursor = p;
    return model_obj_parse_double_token(&cursor, a) && model_obj_parse_double_token(&cursor, b) &&
           model_obj_parse_double_token(&cursor, c) && model_obj_line_tail_done(cursor);
}

/// @brief Clamp @p value into the [0, 1] range, mapping non-finite inputs to 0 (used for MTL
///   color/factor components that must stay normalized).
/// @return The clamped value; 0.0 for NaN or infinity.
static double model_obj_clamp01(double value) {
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

/// @brief Extract the texture filename from an MTL map directive value, taking the last
///   whitespace-delimited token (after trimming) so leading option flags (e.g. "-o 1 1") are
///   skipped. @return A malloc'd copy of the path, or NULL.
static char *model_obj_extract_map_path(const char *value) {
    const char *start;
    const char *end;
    const char *last;
    if (!value)
        return NULL;
    start = value;
    while (*start == ' ' || *start == '\t')
        start++;
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r'))
        end--;
    if (end > start && (*start == '"' || *start == '\'')) {
        char quote = *start++;
        const char *quoted_end = start;
        while (quoted_end < end && *quoted_end != quote)
            quoted_end++;
        if (quoted_end < end)
            return model_strdup_range(start, quoted_end);
    }
    last = end;
    while (last > start) {
        const char *token_start = last;
        while (token_start > start && token_start[-1] != ' ' && token_start[-1] != '\t')
            token_start--;
        if (token_start < end && (*token_start == '"' || *token_start == '\'')) {
            char quote = *token_start;
            const char *quoted_end = token_start + 1;
            while (quoted_end < end && *quoted_end != quote)
                quoted_end++;
            if (quoted_end < end)
                return model_strdup_range(token_start + 1, quoted_end);
        }
        if (token_start == start)
            break;
        last = token_start - 1;
        while (last > start && (last[-1] == ' ' || last[-1] == '\t'))
            last--;
    }
    last = end;
    while (last > start && last[-1] != ' ' && last[-1] != '\t')
        last--;
    return model_strdup_range(last, end);
}

/// @brief Extract the next token from *@p cursor — honoring single/double quotes so paths or
///   material names containing spaces survive — and advance @p cursor past it (and any closing quote).
/// @return A malloc'd copy of the token, or NULL at end-of-line / on a comment.
static char *model_obj_next_path_token(const char **cursor) {
    const char *start;
    const char *end;
    char quote = '\0';
    if (!cursor || !*cursor)
        return NULL;
    while (**cursor == ' ' || **cursor == '\t')
        (*cursor)++;
    if (**cursor == '\0' || **cursor == '\n' || **cursor == '\r' || **cursor == '#')
        return NULL;
    start = *cursor;
    if (*start == '"' || *start == '\'') {
        quote = *start;
        start++;
        end = start;
        while (*end && *end != quote && *end != '\n' && *end != '\r')
            end++;
        *cursor = (*end == quote) ? end + 1 : end;
        return model_strdup_range(start, end);
    }
    end = start;
    while (*end && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r' && *end != '#')
        end++;
    *cursor = end;
    return model_strdup_range(start, end);
}

/// @brief Load the texture named by an MTL map directive (sanitized via the path-traversal
///   guard and joined to @p mtl_dir) and assign it to @p material's @p slot
///   (normal/specular/emissive, else base color). No-op on a missing/unsafe/unloadable path.
static void model_obj_set_texture_from_map(void *material,
                                           const char *mtl_dir,
                                           const char *map_value,
                                           int32_t slot) {
    char *map_ref;
    char safe_ref[1024];
    char path[1024];
    void *pixels;
    if (!material)
        return;
    map_ref = model_obj_extract_map_path(map_value);
    if (!map_ref)
        return;
    if (!model_normalize_relative_asset_ref(map_ref, safe_ref, sizeof(safe_ref)) ||
        !model_join_path(path, sizeof(path), mtl_dir, safe_ref)) {
        free(map_ref);
        return;
    }
    free(map_ref);
    pixels = rt_pixels_load(rt_const_cstr(path));
    if (!pixels)
        return;
    switch (slot) {
        case RT_MATERIAL3D_TEXTURE_SLOT_NORMAL:
            rt_material3d_set_normal_map(material, pixels);
            break;
        case RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR:
            rt_material3d_set_specular_map(material, pixels);
            break;
        case RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE:
            rt_material3d_set_emissive_map(material, pixels);
            break;
        default:
            rt_material3d_set_texture(material, pixels);
            break;
    }
    model_release_local(pixels);
}

/// @brief Parse the .mtl library referenced by @p mtllib_ref (relative to @p obj_dir), adding
///   each "newmtl" material — with its colors and map_* textures — to @p table.
static void model_obj_parse_mtl_file(model_obj_material_table *table,
                                     const char *obj_dir,
                                     const char *mtllib_ref) {
    char safe_ref[1024];
    char path[1024];
    char mtl_dir[1024];
    FILE *f;
    char line[1024];
    char *current_name = NULL;
    void *current_material = NULL;
    if (!table || !model_normalize_relative_asset_ref(mtllib_ref, safe_ref, sizeof(safe_ref)))
        return;
    if (!model_join_path(path, sizeof(path), obj_dir, safe_ref))
        return;
    model_parent_dir(mtl_dir, sizeof(mtl_dir), path);
    f = fopen(path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\0' || *p == '\n' || *p == '\r')
            continue;
        if (strncmp(p, "newmtl ", 7) == 0) {
            const char *cursor = p + 7;
            if (current_name && current_material &&
                !model_obj_material_table_append(table, current_name, current_material)) {
                free(current_name);
                model_release_local(current_material);
            }
            current_name = model_obj_next_path_token(&cursor);
            current_material = rt_material3d_new();
            if (!current_name || !current_material) {
                free(current_name);
                model_release_local(current_material);
                current_name = NULL;
                current_material = NULL;
            }
        } else if (current_material && strncmp(p, "Kd ", 3) == 0) {
            double r, g, b;
            p += 3;
            if (model_obj_parse_double3(p, &r, &g, &b))
                rt_material3d_set_color(current_material, r, g, b);
        } else if (current_material && strncmp(p, "Ks ", 3) == 0) {
            double r, g, b;
            p += 3;
            if (model_obj_parse_double3(p, &r, &g, &b)) {
                ((rt_material3d *)current_material)->specular[0] = model_obj_clamp01(r);
                ((rt_material3d *)current_material)->specular[1] = model_obj_clamp01(g);
                ((rt_material3d *)current_material)->specular[2] = model_obj_clamp01(b);
            }
        } else if (current_material && strncmp(p, "Ke ", 3) == 0) {
            double r, g, b;
            p += 3;
            if (model_obj_parse_double3(p, &r, &g, &b))
                rt_material3d_set_emissive_color(current_material, r, g, b);
        } else if (current_material && strncmp(p, "Ns ", 3) == 0) {
            double ns;
            p += 3;
            if (model_obj_parse_double1(p, &ns))
                rt_material3d_set_shininess(current_material, ns);
        } else if (current_material && strncmp(p, "map_Kd ", 7) == 0) {
            model_obj_set_texture_from_map(
                current_material, mtl_dir, p + 7, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR);
        } else if (current_material && strncmp(p, "map_Ks ", 7) == 0) {
            model_obj_set_texture_from_map(
                current_material, mtl_dir, p + 7, RT_MATERIAL3D_TEXTURE_SLOT_SPECULAR);
        } else if (current_material && strncmp(p, "map_Ke ", 7) == 0) {
            model_obj_set_texture_from_map(
                current_material, mtl_dir, p + 7, RT_MATERIAL3D_TEXTURE_SLOT_EMISSIVE);
        } else if (current_material &&
                   (strncmp(p, "map_Bump ", 9) == 0 || strncmp(p, "map_bump ", 9) == 0 ||
                    strncmp(p, "bump ", 5) == 0 || strncmp(p, "norm ", 5) == 0)) {
            const char *map_value = (p[0] == 'b' || p[0] == 'n') ? p + 5 : p + 9;
            model_obj_set_texture_from_map(
                current_material, mtl_dir, map_value, RT_MATERIAL3D_TEXTURE_SLOT_NORMAL);
        } else if (current_material && (strncmp(p, "d ", 2) == 0 || strncmp(p, "Tr ", 3) == 0)) {
            double a;
            const char *value = p + (p[0] == 'T' ? 3 : 2);
            if (model_obj_parse_double1(value, &a)) {
                if (p[0] == 'T')
                    a = 1.0 - a;
                rt_material3d_set_alpha(current_material, a);
            }
        }
    }
    fclose(f);
    if (current_name && current_material &&
        !model_obj_material_table_append(table, current_name, current_material)) {
        free(current_name);
        model_release_local(current_material);
    }
}

/// @brief Scan the OBJ file at @p obj_path for "mtllib" directives and parse each referenced
///   .mtl library into @p table.
static void model_obj_load_material_libraries(const char *obj_path,
                                              model_obj_material_table *table) {
    char obj_dir[1024];
    FILE *f;
    char line[1024];
    if (!obj_path || !table)
        return;
    model_parent_dir(obj_dir, sizeof(obj_dir), obj_path);
    f = fopen(obj_path, "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "mtllib ", 7) == 0) {
            const char *cursor = p + 7;
            for (;;) {
                char *name = model_obj_next_path_token(&cursor);
                if (!name)
                    break;
                if (name) {
                    model_obj_parse_mtl_file(table, obj_dir, name);
                    free(name);
                }
            }
        }
    }
    fclose(f);
}

/// @brief Load a Wavefront OBJ file into @p model: parse vertices/normals/UVs and faces
///   (grouped by material), resolve materials from referenced .mtl libraries, and build the
///   model's per-material meshes. @return 1 on success, 0 on failure.
static int model3d_load_from_obj(rt_model3d *model, rt_string path, const char *path_cstr) {
    rt_mesh3d_obj_group_t *groups = NULL;
    int32_t group_count = 0;
    model_obj_material_table materials;
    void *default_material = NULL;
    memset(&materials, 0, sizeof(materials));
    if (!rt_mesh3d_from_obj_groups(path, &groups, &group_count) || group_count <= 0)
        return 0;
    model_obj_load_material_libraries(path_cstr, &materials);
    default_material = rt_material3d_new();
    if (!default_material) {
        model_obj_material_table_free(&materials);
        rt_mesh3d_obj_groups_free(groups, group_count);
        return 0;
    }
    for (int32_t i = 0; i < group_count; i++) {
        void *material = model_obj_material_lookup(&materials, groups[i].material_name);
        rt_scene_node3d *node;
        char fallback_name[64];
        const char *node_name = groups[i].material_name && groups[i].material_name[0] != '\0'
                                    ? groups[i].material_name
                                    : NULL;
        if (!material)
            material = default_material;
        if (!model_append_unique_ref(&model->meshes,
                                     &model->mesh_count,
                                     &model->mesh_capacity,
                                     groups[i].mesh,
                                     "Model3D.Load: mesh list allocation failed") ||
            !model_append_unique_ref(&model->materials,
                                     &model->material_count,
                                     &model->material_capacity,
                                     material,
                                     "Model3D.Load: material list allocation failed")) {
            model_release_local(default_material);
            model_obj_material_table_free(&materials);
            rt_mesh3d_obj_groups_free(groups, group_count);
            return 0;
        }
        node = (rt_scene_node3d *)rt_scene_node3d_new();
        if (!node) {
            model_release_local(default_material);
            model_obj_material_table_free(&materials);
            rt_mesh3d_obj_groups_free(groups, group_count);
            return 0;
        }
        if (!node_name) {
            snprintf(fallback_name, sizeof(fallback_name), "mesh_%d", (int)i);
            node_name = fallback_name;
        }
        rt_scene_node3d_set_name(node, rt_const_cstr(node_name));
        rt_scene_node3d_set_mesh(node, groups[i].mesh);
        rt_scene_node3d_set_material(node, material);
        if (!rt_scene_node3d_try_add_child(model->template_root, node)) {
            model_release_local(node);
            model_release_local(default_material);
            model_obj_material_table_free(&materials);
            rt_mesh3d_obj_groups_free(groups, group_count);
            return 0;
        }
        model_release_local(node);
    }
    model_release_local(default_material);
    model_obj_material_table_free(&materials);
    rt_mesh3d_obj_groups_free(groups, group_count);
    return 1;
}

/// @brief .gltf/.glb branch of rt_model3d_load_impl: decode the asset and collect its
///   meshes/materials/skeletons/animations/cameras/scene templates onto `model`.
///   Consumes the preloaded gltf inputs. @return 1 on success, 0 on failure (the local
///   asset is released; the caller cleans up `model`).
static int model3d_load_from_gltf(rt_model3d *model,
                                  rt_string path,
                                  int load_assets,
                                  uint8_t *preloaded_gltf_data,
                                  size_t preloaded_gltf_size,
                                  struct rt_gltf_preload_bundle *preloaded_gltf_bundle) {
    void *asset = NULL;
    int32_t mesh_count;
    int32_t material_count;
    int32_t skeleton_count;
    int32_t animation_count;
    int32_t node_animation_count;
    int32_t camera_count;
    int32_t gltf_scene_count;
    void *scene_root;
    if (preloaded_gltf_bundle) {
        asset = rt_gltf_load_preloaded_bundle(path, preloaded_gltf_bundle, load_assets);
        preloaded_gltf_bundle = NULL;
    } else if (preloaded_gltf_data) {
        asset = rt_gltf_load_preloaded(path, preloaded_gltf_data, preloaded_gltf_size, load_assets);
        preloaded_gltf_data = NULL;
    } else {
        asset = load_assets ? rt_gltf_load_asset(path) : rt_gltf_load(path);
    }
    if (!asset)
        return 0;
    if (!model_count_i64_to_i32(
            rt_gltf_mesh_count(asset), &mesh_count, "Model3D.Load: too many glTF meshes") ||
        !model_count_i64_to_i32(rt_gltf_material_count(asset),
                                &material_count,
                                "Model3D.Load: too many glTF materials") ||
        !model_count_i64_to_i32(rt_gltf_skeleton_count(asset),
                                &skeleton_count,
                                "Model3D.Load: too many glTF skeletons") ||
        !model_count_i64_to_i32(rt_gltf_animation_count(asset),
                                &animation_count,
                                "Model3D.Load: too many glTF animations") ||
        !model_count_i64_to_i32(rt_gltf_node_animation_count(asset),
                                &node_animation_count,
                                "Model3D.Load: too many glTF node animations") ||
        !model_count_i64_to_i32(
            rt_gltf_camera_count(asset), &camera_count, "Model3D.Load: too many glTF cameras") ||
        !model_count_i64_to_i32(rt_gltf_scene_count(asset),
                                &gltf_scene_count,
                                "Model3D.Load: too many glTF scenes")) {
        model_release_local(asset);
        return 0;
    }
    for (int32_t i = 0; i < mesh_count; i++) {
        if (!model_append_ref(&model->meshes,
                              &model->mesh_count,
                              &model->mesh_capacity,
                              rt_gltf_get_mesh(asset, i),
                              "Model3D.Load: mesh list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < material_count; i++) {
        if (!model_append_ref(&model->materials,
                              &model->material_count,
                              &model->material_capacity,
                              rt_gltf_get_material(asset, i),
                              "Model3D.Load: material list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < skeleton_count; i++) {
        if (!model_append_ref(&model->skeletons,
                              &model->skeleton_count,
                              &model->skeleton_capacity,
                              rt_gltf_get_skeleton(asset, i),
                              "Model3D.Load: skeleton list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < animation_count; i++) {
        if (!model_append_ref(&model->animations,
                              &model->animation_count,
                              &model->animation_capacity,
                              rt_gltf_get_animation(asset, i),
                              "Model3D.Load: animation list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < node_animation_count; i++) {
        if (!model_append_ref(&model->node_animations,
                              &model->node_animation_count,
                              &model->node_animation_capacity,
                              rt_gltf_get_node_animation(asset, i),
                              "Model3D.Load: node animation list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < camera_count; i++) {
        if (!model_append_ref(&model->cameras,
                              &model->camera_count,
                              &model->camera_capacity,
                              rt_gltf_get_camera(asset, i),
                              "Model3D.Load: camera list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    scene_root = rt_gltf_get_scene_root(asset);
    if (scene_root) {
        if (!model_clone_children_to_root(model->template_root,
                                          (const rt_scene_node3d *)scene_root)) {
            model_release_local(asset);
            return 0;
        }
        if (!model_collect_template_refs(model)) {
            model_release_local(asset);
            return 0;
        }
    }
    if (!scene_root && !model_build_synth_mesh_nodes(model)) {
        model_release_local(asset);
        return 0;
    }
    if (gltf_scene_count > 0) {
        for (int32_t si = 0; si < gltf_scene_count; si++) {
            rt_string scene_name = rt_gltf_get_scene_name(asset, si);
            const char *scene_name_cstr = scene_name ? rt_string_cstr(scene_name) : NULL;
            rt_scene_node3d *scene_template = NULL;
            int32_t model_scene_index = -1;
            int32_t scene_camera_count = 0;
            if (si == 0) {
                scene_template = model->template_root;
            } else {
                void *gltf_scene_root = rt_gltf_get_scene_root_at(asset, si);
                scene_template = (rt_scene_node3d *)rt_scene_node3d_new();
                if (!gltf_scene_root || !scene_template) {
                    if (scene_template)
                        model_release_local(scene_template);
                    model_release_local(asset);
                    return 0;
                }
                if (!model_clone_children_to_root(scene_template,
                                                  (const rt_scene_node3d *)gltf_scene_root)) {
                    model_release_local(scene_template);
                    model_release_local(asset);
                    return 0;
                }
            }
            if (!model_append_scene_entry(
                    model, scene_name_cstr, scene_template, &model_scene_index)) {
                if (si != 0)
                    model_release_local(scene_template);
                model_release_local(asset);
                return 0;
            }
            if (!model_count_i64_to_i32(rt_gltf_scene_camera_count(asset, si),
                                        &scene_camera_count,
                                        "Model3D.Load: too many glTF scene cameras")) {
                if (si != 0)
                    model_release_local(scene_template);
                model_release_local(asset);
                return 0;
            }
            for (int32_t ci = 0; ci < scene_camera_count; ci++) {
                if (!model_append_scene_camera(&model->scenes[model_scene_index],
                                               rt_gltf_get_scene_camera(asset, si, ci))) {
                    if (si != 0)
                        model_release_local(scene_template);
                    model_release_local(asset);
                    return 0;
                }
            }
            if (si != 0)
                model_release_local(scene_template);
        }
    }
    model_release_local(asset);
    return 1;
}

/// @brief .fbx branch of rt_model3d_load_impl: decode the asset and collect its meshes/
///   materials/skeleton/animations/scene template onto `model`. @return 1 on success,
///   0 on failure (local asset released; caller cleans up `model`).
static int model3d_load_from_fbx(rt_model3d *model, rt_string path) {
    void *asset = rt_fbx_load(path);
    int32_t mesh_count;
    int32_t material_count;
    int32_t animation_count;
    void *skeleton;
    void *scene_root;
    if (!asset)
        return 0;
    if (!model_count_i64_to_i32(
            rt_fbx_mesh_count(asset), &mesh_count, "Model3D.Load: too many FBX meshes") ||
        !model_count_i64_to_i32(rt_fbx_material_count(asset),
                                &material_count,
                                "Model3D.Load: too many FBX materials") ||
        !model_count_i64_to_i32(rt_fbx_animation_count(asset),
                                &animation_count,
                                "Model3D.Load: too many FBX animations")) {
        model_release_local(asset);
        return 0;
    }
    skeleton = rt_fbx_get_skeleton(asset);
    scene_root = rt_fbx_get_scene_root(asset);

    for (int32_t i = 0; i < mesh_count; i++) {
        void *mesh = rt_fbx_get_mesh(asset, i);
        void *morph_targets = rt_fbx_get_morph_target(asset, i);
        if (morph_targets)
            rt_mesh3d_set_morph_targets(mesh, morph_targets);
        if (!model_append_ref(&model->meshes,
                              &model->mesh_count,
                              &model->mesh_capacity,
                              mesh,
                              "Model3D.Load: mesh list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < material_count; i++) {
        if (!model_append_ref(&model->materials,
                              &model->material_count,
                              &model->material_capacity,
                              rt_fbx_get_material(asset, i),
                              "Model3D.Load: material list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    if (skeleton) {
        if (!model_append_ref(&model->skeletons,
                              &model->skeleton_count,
                              &model->skeleton_capacity,
                              skeleton,
                              "Model3D.Load: skeleton list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    for (int32_t i = 0; i < animation_count; i++) {
        if (!model_append_ref(&model->animations,
                              &model->animation_count,
                              &model->animation_capacity,
                              rt_fbx_get_animation(asset, i),
                              "Model3D.Load: animation list allocation failed")) {
            model_release_local(asset);
            return 0;
        }
    }
    if (scene_root) {
        if (!model_clone_children_to_root(model->template_root,
                                          (const rt_scene_node3d *)scene_root)) {
            model_release_local(asset);
            return 0;
        }
        if (!model_collect_template_refs(model)) {
            model_release_local(asset);
            return 0;
        }
    }
    if (!model_build_synth_mesh_nodes(model)) {
        model_release_local(asset);
        return 0;
    }
    model_release_local(asset);
    return 1;
}

/// @brief Load a 3D model from disk into a `rt_model3d` template — auto-detects format by file
/// extension (.vscn, .gltf/.glb, .fbx, .obj, .stl). Collects meshes, materials, skeletons,
/// animations, and the scene-node template tree from the source asset, retaining each component for
/// the model's lifetime. Returns NULL (with a trap message) for invalid path / unsupported
/// extension / underlying loader failure. Use `_instantiate` to spawn a clone in a live scene.
static void *rt_model3d_load_impl(rt_string path,
                                  int load_assets,
                                  uint8_t *preloaded_gltf_data,
                                  size_t preloaded_gltf_size,
                                  struct rt_gltf_preload_bundle *preloaded_gltf_bundle) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    const char *api_name = load_assets ? "Model3D.LoadAsset" : "Model3D.Load";
    rt_model3d *model;

    if (!path || !path_cstr) {
        free(preloaded_gltf_data);
        rt_gltf_preload_bundle_free(preloaded_gltf_bundle);
        rt_trap(load_assets ? "Model3D.LoadAsset: invalid path" : "Model3D.Load: invalid path");
        return NULL;
    }

    model = model_new();
    if (!model) {
        free(preloaded_gltf_data);
        rt_gltf_preload_bundle_free(preloaded_gltf_bundle);
        return NULL;
    }
    model_set_root_name(model->template_root, path_cstr);

    if (model_has_ext(path_cstr, ".vscn")) {
        rt_scene3d *scene = (rt_scene3d *)rt_scene3d_load(path);
        if (!scene)
            goto fail;
        if (!model_clone_children_to_root(model->template_root, scene->root) ||
            !model_collect_template_refs(model)) {
            model_release_local(scene);
            goto fail;
        }
        model_release_local(scene);
    } else if (model_has_ext(path_cstr, ".gltf") || model_has_ext(path_cstr, ".glb")) {
        int gltf_ok = model3d_load_from_gltf(model,
                                             path,
                                             load_assets,
                                             preloaded_gltf_data,
                                             preloaded_gltf_size,
                                             preloaded_gltf_bundle);
        preloaded_gltf_data = NULL;
        preloaded_gltf_bundle = NULL;
        if (!gltf_ok)
            goto fail;
    } else if (model_has_ext(path_cstr, ".fbx")) {
        if (!model3d_load_from_fbx(model, path))
            goto fail;
    } else if (model_has_ext(path_cstr, ".obj")) {
        if (!model3d_load_from_obj(model, path, path_cstr))
            goto fail;
    } else if (model_has_ext(path_cstr, ".stl")) {
        void *mesh = rt_mesh3d_from_stl(path);
        void *material = NULL;
        if (!mesh)
            goto fail;
        material = rt_material3d_new();
        if (!material) {
            model_release_local(mesh);
            goto fail;
        }
        if (!model_append_ref(&model->meshes,
                              &model->mesh_count,
                              &model->mesh_capacity,
                              mesh,
                              "Model3D.Load: mesh list allocation failed")) {
            model_release_local(material);
            model_release_local(mesh);
            goto fail;
        }
        if (!model_append_ref(&model->materials,
                              &model->material_count,
                              &model->material_capacity,
                              material,
                              "Model3D.Load: material list allocation failed")) {
            model_release_local(material);
            model_release_local(mesh);
            goto fail;
        }
        model_release_local(material);
        model_release_local(mesh);
        if (!model_build_synth_mesh_nodes(model))
            goto fail;
    } else {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: unsupported file extension", api_name);
        rt_trap(msg);
        goto fail;
    }

    if (model->scene_count == 0 && model->template_root) {
        if (!model_append_scene_entry(model, "default", model->template_root, NULL))
            goto fail;
    }

    return model;

fail:
    free(preloaded_gltf_data);
    rt_gltf_preload_bundle_free(preloaded_gltf_bundle);
    model_release_local(model);
    return NULL;
}

/// @brief Load a model from the filesystem (no asset-manager resolution). See header.
void *rt_model3d_load(rt_string path) {
    return rt_model3d_load_impl(path, 0, NULL, 0, NULL);
}

/// @brief Load a model through the asset manager (mounted/embedded + dev fallback). See header.
void *rt_model3d_load_asset(rt_string path) {
    return rt_model3d_load_impl(path, 1, NULL, 0, NULL);
}

/// @brief Internal async path: build a glTF/GLB Model3D from worker-staged root bytes.
/// @details Takes ownership of @p preloaded_data. Non-glTF paths free it and use the normal
///   loader so conservative async callers do not leak when falling back to main-thread loading.
void *rt_model3d_load_preloaded_gltf(rt_string path,
                                     uint8_t *preloaded_data,
                                     size_t preloaded_size,
                                     int load_assets) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    if (!path_cstr || !(model_has_ext(path_cstr, ".gltf") || model_has_ext(path_cstr, ".glb"))) {
        free(preloaded_data);
        return load_assets ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    }
    return rt_model3d_load_impl(path, load_assets ? 1 : 0, preloaded_data, preloaded_size, NULL);
}

/// @brief Build a Model3D from a glTF asset that was staged off-thread into @p bundle.
/// @details Loads the glTF on the main thread from the preload bundle (no file I/O), then wraps the
///          resulting asset's meshes/materials/scenes as a Model3D. Falls back to a normal load
///          when
///          @p bundle is NULL.
/// @return New Model3D handle, or NULL on failure.
void *rt_model3d_load_preloaded_gltf_bundle(rt_string path,
                                            struct rt_gltf_preload_bundle *bundle,
                                            int load_assets) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    if (!path_cstr || !(model_has_ext(path_cstr, ".gltf") || model_has_ext(path_cstr, ".glb"))) {
        rt_gltf_preload_bundle_free(bundle);
        return load_assets ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    }
    return rt_model3d_load_impl(path, load_assets ? 1 : 0, NULL, 0, bundle);
}

/// @brief Number of meshes loaded into this model.
int64_t rt_model3d_get_mesh_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model_clamped_array_count(model->meshes, model->mesh_count, model->mesh_capacity)
                 : 0;
}

/// @brief Number of materials loaded into this model.
int64_t rt_model3d_get_material_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model_clamped_array_count(
                       model->materials, model->material_count, model->material_capacity)
                 : 0;
}

/// @brief Number of skeletons (bone hierarchies) loaded into this model.
int64_t rt_model3d_get_skeleton_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model_clamped_array_count(
                       model->skeletons, model->skeleton_count, model->skeleton_capacity)
                 : 0;
}

/// @brief Number of animation clips loaded into this model.
int64_t rt_model3d_get_animation_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model_clamped_array_count(
                       model->animations, model->animation_count, model->animation_capacity)
                 : 0;
}

/// @brief Number of scene nodes in the template subtree (excludes the synthetic root).
int64_t rt_model3d_get_node_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || !model->template_root)
        return 0;
    {
        int32_t count = model_count_subtree(model->template_root);
        return count > 0 ? (int64_t)(count - 1) : 0;
    }
}

/// @brief Number of immutable scenes addressable by this Model3D.
/// @details glTF imports order scenes with the active/default scene at index 0 and any secondary
///   scene roots after it. Other importers expose one default scene.
int64_t rt_model3d_get_scene_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model_scene_count(model);
}

/// @brief Number of cameras imported for @p scene_index.
/// @details glTF cameras are retained per immutable scene. Invalid scene indices and importers
///   without authored cameras return zero.
int64_t rt_model3d_get_camera_count(void *obj, int64_t scene_index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t scene_count = model_scene_count(model);
    if (!model || scene_index < 0 || scene_index >= scene_count)
        return 0;
    return model_clamped_array_count(model->scenes[scene_index].cameras,
                                     model->scenes[scene_index].camera_count,
                                     model->scenes[scene_index].camera_capacity);
}

/// @brief Borrow the i-th Mesh3D (NULL on out-of-range). Caller must NOT release; the model owns
/// it.
void *rt_model3d_get_mesh(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t mesh_count =
        model ? model_clamped_array_count(model->meshes, model->mesh_count, model->mesh_capacity)
              : 0;
    if (!model || index < 0 || index >= mesh_count)
        return NULL;
    return rt_g3d_checked_or_null(model->meshes[index], RT_G3D_MESH3D_CLASS_ID);
}

/// @brief Borrow the i-th Material3D (NULL on out-of-range).
void *rt_model3d_get_material(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t material_count =
        model ? model_clamped_array_count(
                    model->materials, model->material_count, model->material_capacity)
              : 0;
    if (!model || index < 0 || index >= material_count)
        return NULL;
    return rt_g3d_checked_or_null(model->materials[index], RT_G3D_MATERIAL3D_CLASS_ID);
}

/// @brief Borrow the i-th Skeleton3D (NULL on out-of-range).
void *rt_model3d_get_skeleton(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t skeleton_count =
        model ? model_clamped_array_count(
                    model->skeletons, model->skeleton_count, model->skeleton_capacity)
              : 0;
    if (!model || index < 0 || index >= skeleton_count)
        return NULL;
    return rt_g3d_checked_or_null(model->skeletons[index], RT_G3D_SKELETON3D_CLASS_ID);
}

/// @brief Borrow the i-th Animation3D clip (NULL on out-of-range).
void *rt_model3d_get_animation(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t animation_count =
        model ? model_clamped_array_count(
                    model->animations, model->animation_count, model->animation_capacity)
              : 0;
    if (!model || index < 0 || index >= animation_count)
        return NULL;
    return rt_g3d_checked_or_null(model->animations[index], RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Borrow an imported Camera3D from @p scene_index (NULL on out-of-range).
void *rt_model3d_get_camera(void *obj, int64_t scene_index, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    model3d_scene_entry *scene;
    int32_t scene_count = model_scene_count(model);
    int32_t camera_count;
    if (!model || scene_index < 0 || scene_index >= scene_count)
        return NULL;
    scene = &model->scenes[scene_index];
    camera_count =
        model_clamped_array_count(scene->cameras, scene->camera_count, scene->camera_capacity);
    if (index < 0 || index >= camera_count)
        return NULL;
    return rt_g3d_checked_or_null(scene->cameras[index], RT_G3D_CAMERA3D_CLASS_ID);
}

/// @brief Return the immutable scene name for @p index.
/// @details Invalid model handles or indices return the empty string.
rt_string rt_model3d_get_scene_name(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t scene_count = model_scene_count(model);
    if (!model || index < 0 || index >= scene_count || !model->scenes[index].name)
        return rt_const_cstr("");
    return rt_const_cstr(model->scenes[index].name);
}

/// @brief Locate a node in the template subtree by exact name match. NULL if not found.
void *rt_model3d_find_node(void *obj, rt_string name) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || !model->template_root)
        return NULL;
    return model_find_node_safe(model->template_root, name);
}

/// @brief Clone the template into an independent SceneNode3D subtree (with synthetic root).
/// Each instantiation creates a fresh deep-copy of the node hierarchy, suitable for adding to
/// a live scene without disturbing the template. Static meshes/materials are shared; meshes
/// with attached morph targets are cloned so their blend-shape weights are instance-local.
void *rt_model3d_instantiate(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    rt_scene_node3d *root;
    if (!model || !model->template_root)
        return NULL;
    root = model_clone_node(model->template_root, 1);
    model_bind_default_animator(model, root);
    model_bind_default_node_animator(model, root);
    return root;
}

/// @brief Build a fresh Scene3D from the template's children, cloning each one. Use when the
/// caller wants the model exposed as a top-level scene rather than a node subtree.
void *rt_model3d_instantiate_scene(void *obj) {
    return rt_model3d_instantiate_scene_at(obj, 0);
}

/// @brief Build a fresh Scene3D for immutable scene @p index.
void *rt_model3d_instantiate_scene_at(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    rt_scene3d *scene;
    model3d_scene_entry *entry;
    int32_t scene_count = model_scene_count(model);
    if (!model || index < 0 || index >= scene_count)
        return NULL;
    entry = &model->scenes[index];
    if (!entry->root)
        return NULL;
    scene = (rt_scene3d *)rt_scene3d_new();
    if (!scene)
        return NULL;
    for (int32_t i = 0, child_count = model_node_child_count(entry->root); i < child_count; i++) {
        if (!entry->root->children[i])
            continue;
        rt_scene_node3d *child = model_clone_node(entry->root->children[i], 1);
        if (child) {
            if (!rt_scene3d_try_add(scene, child)) {
                model_release_local(child);
                model_release_local(scene);
                return NULL;
            }
            model_release_local(child);
        } else {
            model_release_local(scene);
            return NULL;
        }
    }
    model_bind_default_animator(model, scene->root);
    model_bind_default_node_animator(model, scene->root);
    return scene;
}

#else
typedef int rt_model3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
