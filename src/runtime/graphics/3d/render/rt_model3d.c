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
#include "rt_object.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"
#include "rt_skeleton3d.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_fbx_get_scene_root(void *fbx);

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
static void model_release_array(void ***arr, int32_t *count) {
    if (!arr || !*arr)
        return;
    if (count) {
        for (int32_t i = 0; i < *count; i++)
            model_release_ref(&(*arr)[i]);
        *count = 0;
    }
    free(*arr);
    *arr = NULL;
}

static void model_release_scenes(rt_model3d *model) {
    if (!model || !model->scenes)
        return;
    for (int32_t i = 0; i < model->scene_count; i++) {
        model3d_scene_entry *scene = &model->scenes[i];
        model_release_ref((void **)&scene->root);
        free(scene->name);
        scene->name = NULL;
        model_release_array(&scene->cameras, &scene->camera_count);
        scene->camera_capacity = 0;
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
    model_release_array(&model->meshes, &model->mesh_count);
    model_release_array(&model->materials, &model->material_count);
    model_release_array(&model->skeletons, &model->skeleton_count);
    model_release_array(&model->animations, &model->animation_count);
    model_release_array(&model->node_animations, &model->node_animation_count);
    model_release_array(&model->cameras, &model->camera_count);
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
    if (*cap >= need)
        return 1;
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

static char *model_strdup_or(const char *value, const char *fallback) {
    const char *src = (value && value[0] != '\0') ? value : fallback;
    size_t len;
    char *copy;
    if (!src)
        src = "";
    len = strlen(src);
    copy = (char *)malloc(len + 1);
    if (!copy) {
        rt_trap("Model3D.Load: scene name allocation failed");
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

static int model_grow_scenes(rt_model3d *model, int32_t need) {
    model3d_scene_entry *grown;
    int32_t new_cap;
    if (!model || need < 0)
        return 0;
    if (model->scene_capacity >= need)
        return 1;
    new_cap = model->scene_capacity > 0 ? model->scene_capacity : 4;
    while (new_cap < need) {
        if (new_cap > INT32_MAX / 2) {
            new_cap = need;
            break;
        }
        new_cap *= 2;
    }
    if ((size_t)new_cap > SIZE_MAX / sizeof(*model->scenes))
        return 0;
    grown = (model3d_scene_entry *)realloc(model->scenes, (size_t)new_cap * sizeof(*grown));
    if (!grown)
        return 0;
    memset(grown + model->scene_capacity,
           0,
           (size_t)(new_cap - model->scene_capacity) * sizeof(*grown));
    model->scenes = grown;
    model->scene_capacity = new_cap;
    return 1;
}

static int model_append_scene_entry(rt_model3d *model,
                                    const char *name,
                                    rt_scene_node3d *root,
                                    int32_t *out_index) {
    model3d_scene_entry *scene;
    char fallback[64];
    if (!model || !root)
        return 0;
    if (!model_grow_scenes(model, model->scene_count + 1)) {
        rt_trap("Model3D.Load: scene list allocation failed");
        return 0;
    }
    snprintf(fallback, sizeof(fallback), model->scene_count == 0 ? "default" : "scene_%d",
             (int)model->scene_count);
    scene = &model->scenes[model->scene_count];
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
        *out_index = model->scene_count;
    model->scene_count++;
    return 1;
}

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
    if (!model_grow_array(arr, cap, *count + 1)) {
        rt_trap(trap_msg);
        return 0;
    }
    rt_obj_retain_maybe(obj);
    (*arr)[(*count)++] = obj;
    return 1;
}

/// @brief Append `obj` only if the array does not already contain that exact pointer.
/// Used for collecting unique mesh / material references off a scene-graph walk where the
/// same asset may be referenced from many nodes. NULL `obj` is treated as success-noop.
static int model_append_unique_ref(
    void ***arr, int32_t *count, int32_t *cap, void *obj, const char *trap_msg) {
    if (!obj)
        return 1;
    for (int32_t i = 0; i < *count; i++) {
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
        if (total == INT32_MAX) {
            free(stack);
            rt_trap("Model3D: too many nodes");
            return 0;
        }
        total++;
        for (int32_t i = 0; i < current->child_count; i++) {
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
/// parented to the clone via `rt_scene_node3d_add_child`, then the caller's local reference
/// is released so the parent owns the only retain.
static void *model_clone_mutable_mesh(void *mesh) {
    rt_mesh3d *src = (rt_mesh3d *)mesh;
    void *mesh_clone;
    void *morph_clone;
    if (!src || !src->morph_targets_ref)
        return NULL;
    mesh_clone = rt_mesh3d_clone(mesh);
    if (!mesh_clone)
        return NULL;
    morph_clone = rt_morphtarget3d_clone(src->morph_targets_ref);
    if (!morph_clone) {
        model_release_local(mesh_clone);
        return NULL;
    }
    rt_mesh3d_set_morph_targets(mesh_clone, morph_clone);
    model_release_local(morph_clone);
    return mesh_clone;
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

    dst->position[0] = src->position[0];
    dst->position[1] = src->position[1];
    dst->position[2] = src->position[2];
    dst->rotation[0] = src->rotation[0];
    dst->rotation[1] = src->rotation[1];
    dst->rotation[2] = src->rotation[2];
    dst->rotation[3] = src->rotation[3];
    dst->scale_xyz[0] = src->scale_xyz[0];
    dst->scale_xyz[1] = src->scale_xyz[1];
    dst->scale_xyz[2] = src->scale_xyz[2];
    dst->world_dirty = 1;
    dst->visible = src->visible;
    dst->auto_lod_enabled = src->auto_lod_enabled;
    dst->auto_lod_screen_error_px = src->auto_lod_screen_error_px;

    memcpy(dst->aabb_min, src->aabb_min, sizeof(dst->aabb_min));
    memcpy(dst->aabb_max, src->aabb_max, sizeof(dst->aabb_max));
    dst->bsphere_radius = src->bsphere_radius;

    if (src->mesh) {
        void *mesh_clone = clone_mutable_meshes ? model_clone_mutable_mesh(src->mesh) : NULL;
        if (mesh_clone) {
            dst->mesh = mesh_clone;
        } else {
            rt_obj_retain_maybe(src->mesh);
            dst->mesh = src->mesh;
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

    if (src->lod_count > 0) {
        dst->lod_levels = calloc((size_t)src->lod_count, sizeof(*dst->lod_levels));
        if (!dst->lod_levels) {
            rt_trap("Model3D: lod allocation failed");
            model_release_local(dst);
            return NULL;
        }
        dst->lod_capacity = src->lod_count;
        dst->lod_count = src->lod_count;
        for (int32_t i = 0; i < src->lod_count; i++) {
            void *lod_mesh_clone =
                clone_mutable_meshes ? model_clone_mutable_mesh(src->lod_levels[i].mesh) : NULL;
            dst->lod_levels[i].distance = src->lod_levels[i].distance;
            if (lod_mesh_clone) {
                dst->lod_levels[i].mesh = lod_mesh_clone;
            } else {
                dst->lod_levels[i].mesh = src->lod_levels[i].mesh;
                rt_obj_retain_maybe(dst->lod_levels[i].mesh);
            }
        }
    }

    if (src->has_impostor && src->impostor_pixels)
        rt_scene_node3d_set_impostor(dst, src->impostor_distance, src->impostor_pixels);

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
        if (!frame->src || frame->next_child >= frame->src->child_count) {
            stack_count--;
            continue;
        }
        const rt_scene_node3d *src_child = frame->src->children[frame->next_child++];
        rt_scene_node3d *dst_child = model_clone_node_shallow(src_child, clone_mutable_meshes);
        if (!dst_child) {
            free(stack);
            model_release_local(root);
            return NULL;
        }
        rt_scene_node3d_add_child(frame->dst, dst_child);
        if (rt_scene_node3d_get_parent(dst_child) != frame->dst) {
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
static void model_clone_children_to_root(rt_scene_node3d *dst_root,
                                         const rt_scene_node3d *src_root) {
    if (!dst_root || !src_root)
        return;
    for (int32_t i = 0; i < src_root->child_count; i++) {
        rt_scene_node3d *child = model_clone_node(src_root->children[i], 0);
        if (child) {
            rt_scene_node3d_add_child(dst_root, child);
            model_release_local(child);
        }
    }
}

static int model_collect_scene_refs(rt_model3d *model, const rt_scene_node3d *node);

/// @brief Collect mesh/material references from every top-level child of the template root.
/// @details Delegates to `model_collect_scene_refs` for each immediate child so the template
///   root node itself is not registered (it is a synthetic container, not a content node).
///   Returns 1 on success, 0 if any registration step fails due to an allocation error.
static int model_collect_template_refs(rt_model3d *model) {
    if (!model || !model->template_root)
        return 1;
    for (int32_t i = 0; i < model->template_root->child_count; i++) {
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
    char first_state[64];
    int added_any = 0;
    if (!model || !root || root->bound_animator || model->skeleton_count <= 0 ||
        model->animation_count <= 0)
        return;
    controller = rt_anim_controller3d_new(model->skeletons[0]);
    if (!controller)
        return;
    first_state[0] = '\0';
    for (int32_t i = 0; i < model->animation_count; i++) {
        rt_string anim_name = rt_animation3d_get_name(model->animations[i]);
        const char *name = anim_name ? rt_string_cstr(anim_name) : NULL;
        char fallback[64];
        rt_string state_name;
        int64_t state_index;
        if (!name || name[0] == '\0') {
            snprintf(fallback, sizeof(fallback), "animation_%d", (int)i);
            name = fallback;
        }
        state_name = rt_const_cstr(name);
        state_index = rt_anim_controller3d_add_state(controller, state_name, model->animations[i]);
        if (state_index >= 0 && !added_any) {
            size_t len = strlen(name);
            if (len >= sizeof(first_state))
                len = sizeof(first_state) - 1;
            memcpy(first_state, name, len);
            first_state[len] = '\0';
            added_any = 1;
        }
    }
    if (added_any)
        rt_anim_controller3d_play(controller, rt_const_cstr(first_state));
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
    if (!model || !root || root->bound_node_animator || model->node_animation_count <= 0)
        return;
    animator =
        rt_node_animator3d_new_from_clips(model->node_animations, model->node_animation_count);
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
        if (!current)
            continue;
        if (!model_append_unique_ref(&model->meshes,
                                     &model->mesh_count,
                                     &model->mesh_capacity,
                                     current->mesh,
                                     "Model3D.Load: mesh list allocation failed")) {
            free(stack);
            return 0;
        }
        if (!model_append_unique_ref(&model->materials,
                                     &model->material_count,
                                     &model->material_capacity,
                                     current->material,
                                     "Model3D.Load: material list allocation failed")) {
            free(stack);
            return 0;
        }
        for (int32_t i = 0; i < current->lod_count; i++) {
            if (!model_append_unique_ref(&model->meshes,
                                         &model->mesh_count,
                                         &model->mesh_capacity,
                                         current->lod_levels[i].mesh,
                                         "Model3D.Load: mesh list allocation failed")) {
                free(stack);
                return 0;
            }
        }
        for (int32_t i = 0; i < current->child_count; i++) {
            if (stack_count >= stack_capacity) {
                if (stack_capacity > INT32_MAX / 2 ||
                    (size_t)(stack_capacity * 2) > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    rt_trap("Model3D.Load: too many scene nodes");
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
            stack[stack_count++] = current->children[i];
        }
    }
    free(stack);
    return 1;
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
static void model_build_synth_mesh_nodes(rt_model3d *model) {
    char name[64];
    if (!model || !model->template_root || model->template_root->child_count > 0)
        return;

    for (int32_t i = 0; i < model->mesh_count; i++) {
        rt_scene_node3d *node = (rt_scene_node3d *)rt_scene_node3d_new();
        void *material = NULL;
        if (!node)
            continue;
        snprintf(name, sizeof(name), "mesh_%d", (int)i);
        rt_scene_node3d_set_name(node, rt_const_cstr(name));
        rt_scene_node3d_set_mesh(node, model->meshes[i]);
        if (model->material_count == 1)
            material = model->materials[0];
        else if (i < model->material_count)
            material = model->materials[i];
        if (material)
            rt_scene_node3d_set_material(node, material);
        rt_scene_node3d_add_child(model->template_root, node);
        model_release_local(node);
    }
}

/// @brief Load a 3D model from disk into a `rt_model3d` template — auto-detects format by file
/// extension (.vscn, .gltf/.glb, .fbx). Collects meshes, materials, skeletons, animations, and
/// the scene-node template tree from the source asset, retaining each component for the model's
/// lifetime. Returns NULL (with a trap message) for invalid path / unsupported extension /
/// underlying loader failure. Use `_instantiate` to spawn a clone in a live scene.
static void *rt_model3d_load_impl(rt_string path, int load_assets) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    const char *api_name = load_assets ? "Model3D.LoadAsset" : "Model3D.Load";
    rt_model3d *model;

    if (!path || !path_cstr) {
        rt_trap(load_assets ? "Model3D.LoadAsset: invalid path" : "Model3D.Load: invalid path");
        return NULL;
    }

    model = model_new();
    if (!model)
        return NULL;
    model_set_root_name(model->template_root, path_cstr);

    if (model_has_ext(path_cstr, ".vscn")) {
        rt_scene3d *scene = (rt_scene3d *)rt_scene3d_load(path);
        if (!scene)
            goto fail;
        model_clone_children_to_root(model->template_root, scene->root);
        for (int32_t i = 0; i < model->template_root->child_count; i++) {
            if (!model_collect_scene_refs(model, model->template_root->children[i])) {
                model_release_local(scene);
                goto fail;
            }
        }
        model_release_local(scene);
    } else if (model_has_ext(path_cstr, ".gltf") || model_has_ext(path_cstr, ".glb")) {
        void *asset = load_assets ? rt_gltf_load_asset(path) : rt_gltf_load(path);
        int64_t mesh_count;
        int64_t material_count;
        int64_t skeleton_count;
        int64_t animation_count;
        int64_t node_animation_count;
        int64_t camera_count;
        int64_t gltf_scene_count;
        void *scene_root;
        if (!asset)
            goto fail;
        mesh_count = rt_gltf_mesh_count(asset);
        material_count = rt_gltf_material_count(asset);
        skeleton_count = rt_gltf_skeleton_count(asset);
        animation_count = rt_gltf_animation_count(asset);
        node_animation_count = rt_gltf_node_animation_count(asset);
        camera_count = rt_gltf_camera_count(asset);
        gltf_scene_count = rt_gltf_scene_count(asset);
        for (int64_t i = 0; i < mesh_count; i++) {
            if (!model_append_ref(&model->meshes,
                                  &model->mesh_count,
                                  &model->mesh_capacity,
                                  rt_gltf_get_mesh(asset, i),
                                  "Model3D.Load: mesh list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        for (int64_t i = 0; i < material_count; i++) {
            if (!model_append_ref(&model->materials,
                                  &model->material_count,
                                  &model->material_capacity,
                                  rt_gltf_get_material(asset, i),
                                  "Model3D.Load: material list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        for (int64_t i = 0; i < skeleton_count; i++) {
            if (!model_append_ref(&model->skeletons,
                                  &model->skeleton_count,
                                  &model->skeleton_capacity,
                                  rt_gltf_get_skeleton(asset, i),
                                  "Model3D.Load: skeleton list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        for (int64_t i = 0; i < animation_count; i++) {
            if (!model_append_ref(&model->animations,
                                  &model->animation_count,
                                  &model->animation_capacity,
                                  rt_gltf_get_animation(asset, i),
                                  "Model3D.Load: animation list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        for (int64_t i = 0; i < node_animation_count; i++) {
            if (!model_append_ref(&model->node_animations,
                                  &model->node_animation_count,
                                  &model->node_animation_capacity,
                                  rt_gltf_get_node_animation(asset, i),
                                  "Model3D.Load: node animation list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        for (int64_t i = 0; i < camera_count; i++) {
            if (!model_append_ref(&model->cameras,
                                  &model->camera_count,
                                  &model->camera_capacity,
                                  rt_gltf_get_camera(asset, i),
                                  "Model3D.Load: camera list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        scene_root = rt_gltf_get_scene_root(asset);
        if (scene_root) {
            model_clone_children_to_root(model->template_root, (const rt_scene_node3d *)scene_root);
            if (!model_collect_template_refs(model)) {
                model_release_local(asset);
                goto fail;
            }
        }
        model_build_synth_mesh_nodes(model);
        if (gltf_scene_count > 0) {
            for (int64_t si = 0; si < gltf_scene_count; si++) {
                rt_string scene_name = rt_gltf_get_scene_name(asset, si);
                const char *scene_name_cstr = scene_name ? rt_string_cstr(scene_name) : NULL;
                rt_scene_node3d *scene_template = NULL;
                int32_t model_scene_index = -1;
                if (si == 0) {
                    scene_template = model->template_root;
                } else {
                    void *gltf_scene_root = rt_gltf_get_scene_root_at(asset, si);
                    scene_template = (rt_scene_node3d *)rt_scene_node3d_new();
                    if (!scene_template) {
                        model_release_local(asset);
                        goto fail;
                    }
                    model_clone_children_to_root(scene_template,
                                                 (const rt_scene_node3d *)gltf_scene_root);
                }
                if (!model_append_scene_entry(model,
                                              scene_name_cstr,
                                              scene_template,
                                              &model_scene_index)) {
                    if (si != 0)
                        model_release_local(scene_template);
                    model_release_local(asset);
                    goto fail;
                }
                for (int64_t ci = 0; ci < rt_gltf_scene_camera_count(asset, si); ci++) {
                    if (!model_append_scene_camera(
                            &model->scenes[model_scene_index],
                            rt_gltf_get_scene_camera(asset, si, ci))) {
                        if (si != 0)
                            model_release_local(scene_template);
                        model_release_local(asset);
                        goto fail;
                    }
                }
                if (si != 0)
                    model_release_local(scene_template);
            }
        }
        model_release_local(asset);
    } else if (model_has_ext(path_cstr, ".fbx")) {
        void *asset = rt_fbx_load(path);
        int64_t mesh_count;
        int64_t material_count;
        int64_t animation_count;
        void *skeleton;
        void *scene_root;
        if (!asset)
            goto fail;
        mesh_count = rt_fbx_mesh_count(asset);
        material_count = rt_fbx_material_count(asset);
        animation_count = rt_fbx_animation_count(asset);
        skeleton = rt_fbx_get_skeleton(asset);
        scene_root = rt_fbx_get_scene_root(asset);

        for (int64_t i = 0; i < mesh_count; i++) {
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
                goto fail;
            }
        }
        for (int64_t i = 0; i < material_count; i++) {
            if (!model_append_ref(&model->materials,
                                  &model->material_count,
                                  &model->material_capacity,
                                  rt_fbx_get_material(asset, i),
                                  "Model3D.Load: material list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        if (skeleton) {
            if (!model_append_ref(&model->skeletons,
                                  &model->skeleton_count,
                                  &model->skeleton_capacity,
                                  skeleton,
                                  "Model3D.Load: skeleton list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        for (int64_t i = 0; i < animation_count; i++) {
            if (!model_append_ref(&model->animations,
                                  &model->animation_count,
                                  &model->animation_capacity,
                                  rt_fbx_get_animation(asset, i),
                                  "Model3D.Load: animation list allocation failed")) {
                model_release_local(asset);
                goto fail;
            }
        }
        if (scene_root) {
            model_clone_children_to_root(model->template_root, (const rt_scene_node3d *)scene_root);
            if (!model_collect_template_refs(model)) {
                model_release_local(asset);
                goto fail;
            }
        }
        model_build_synth_mesh_nodes(model);
        model_release_local(asset);
    } else if (model_has_ext(path_cstr, ".obj")) {
        void *mesh = rt_mesh3d_from_obj(path);
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
        model_build_synth_mesh_nodes(model);
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
    model_release_local(model);
    return NULL;
}

/// @brief Load a model from the filesystem (no asset-manager resolution). See header.
void *rt_model3d_load(rt_string path) {
    return rt_model3d_load_impl(path, 0);
}

/// @brief Load a model through the asset manager (mounted/embedded + dev fallback). See header.
void *rt_model3d_load_asset(rt_string path) {
    return rt_model3d_load_impl(path, 1);
}

/// @brief Number of meshes loaded into this model.
int64_t rt_model3d_get_mesh_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model->mesh_count : 0;
}

/// @brief Number of materials loaded into this model.
int64_t rt_model3d_get_material_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model->material_count : 0;
}

/// @brief Number of skeletons (bone hierarchies) loaded into this model.
int64_t rt_model3d_get_skeleton_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model->skeleton_count : 0;
}

/// @brief Number of animation clips loaded into this model.
int64_t rt_model3d_get_animation_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model->animation_count : 0;
}

/// @brief Number of scene nodes in the template subtree (excludes the synthetic root).
int64_t rt_model3d_get_node_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || !model->template_root)
        return 0;
    return model_count_subtree(model->template_root) - 1;
}

/// @brief Number of immutable scenes addressable by this Model3D.
/// @details glTF imports order scenes with the active/default scene at index 0 and any secondary
///   scene roots after it. Other importers expose one default scene.
int64_t rt_model3d_get_scene_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model->scene_count : 0;
}

/// @brief Number of cameras imported for @p scene_index.
/// @details glTF cameras are retained per immutable scene. Invalid scene indices and importers
///   without authored cameras return zero.
int64_t rt_model3d_get_camera_count(void *obj, int64_t scene_index) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || scene_index < 0 || scene_index >= model->scene_count)
        return 0;
    return model->scenes[scene_index].camera_count;
}

/// @brief Borrow the i-th Mesh3D (NULL on out-of-range). Caller must NOT release; the model owns
/// it.
void *rt_model3d_get_mesh(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || index < 0 || index >= model->mesh_count)
        return NULL;
    return model->meshes[index];
}

/// @brief Borrow the i-th Material3D (NULL on out-of-range).
void *rt_model3d_get_material(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || index < 0 || index >= model->material_count)
        return NULL;
    return model->materials[index];
}

/// @brief Borrow the i-th Skeleton3D (NULL on out-of-range).
void *rt_model3d_get_skeleton(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || index < 0 || index >= model->skeleton_count)
        return NULL;
    return model->skeletons[index];
}

/// @brief Borrow the i-th Animation3D clip (NULL on out-of-range).
void *rt_model3d_get_animation(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || index < 0 || index >= model->animation_count)
        return NULL;
    return model->animations[index];
}

/// @brief Borrow an imported Camera3D from @p scene_index (NULL on out-of-range).
void *rt_model3d_get_camera(void *obj, int64_t scene_index, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    model3d_scene_entry *scene;
    if (!model || scene_index < 0 || scene_index >= model->scene_count)
        return NULL;
    scene = &model->scenes[scene_index];
    if (index < 0 || index >= scene->camera_count)
        return NULL;
    return scene->cameras[index];
}

/// @brief Return the immutable scene name for @p index.
/// @details Invalid model handles or indices return the empty string.
rt_string rt_model3d_get_scene_name(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || index < 0 || index >= model->scene_count || !model->scenes[index].name)
        return rt_const_cstr("");
    return rt_const_cstr(model->scenes[index].name);
}

/// @brief Locate a node in the template subtree by exact name match. NULL if not found.
void *rt_model3d_find_node(void *obj, rt_string name) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || !model->template_root)
        return NULL;
    return rt_scene_node3d_find(model->template_root, name);
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
    if (!model || index < 0 || index >= model->scene_count)
        return NULL;
    entry = &model->scenes[index];
    if (!entry->root)
        return NULL;
    scene = (rt_scene3d *)rt_scene3d_new();
    if (!scene)
        return NULL;
    for (int32_t i = 0; i < entry->root->child_count; i++) {
        rt_scene_node3d *child = model_clone_node(entry->root->children[i], 1);
        if (child) {
            rt_scene3d_add(scene, child);
            model_release_local(child);
        }
    }
    model_bind_default_animator(model, scene->root);
    model_bind_default_node_animator(model, scene->root);
    return scene;
}

#else
typedef int rt_model3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
