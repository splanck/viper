//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_model3d.c
// Purpose: Model3D high-level asset wrapper over imported scene/resources.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_model3d.h"

#include "rt_fbx_loader.h"
#include "rt_gltf.h"
#include "rt_scene3d.h"
#include "rt_scene3d_internal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern rt_string rt_const_cstr(const char *s);
extern const char *rt_string_cstr(rt_string s);
#include "rt_trap.h"

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
} rt_model3d;

static void model_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void model_release_local(void *obj) {
    void *tmp = obj;
    model_release_ref(&tmp);
}

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

static void rt_model3d_finalize(void *obj) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model)
        return;
    model_release_ref((void **)&model->template_root);
    model_release_array(&model->meshes, &model->mesh_count);
    model_release_array(&model->materials, &model->material_count);
    model_release_array(&model->skeletons, &model->skeleton_count);
    model_release_array(&model->animations, &model->animation_count);
}

static rt_model3d *model_new(void) {
    rt_model3d *model = (rt_model3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_model3d));
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

static int model_grow_array(void ***arr, int32_t *cap, int32_t need) {
    if (*cap >= need)
        return 1;
    int32_t new_cap = *cap > 0 ? *cap * 2 : 4;
    if (new_cap < need)
        new_cap = need;
    void **grown = (void **)realloc(*arr, (size_t)new_cap * sizeof(void *));
    if (!grown)
        return 0;
    *arr = grown;
    *cap = new_cap;
    return 1;
}

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

static int32_t model_count_subtree(const rt_scene_node3d *node) {
    int32_t total = 1;
    if (!node)
        return 0;
    for (int32_t i = 0; i < node->child_count; i++)
        total += model_count_subtree(node->children[i]);
    return total;
}

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

static rt_scene_node3d *model_clone_node(const rt_scene_node3d *src) {
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

    memcpy(dst->aabb_min, src->aabb_min, sizeof(dst->aabb_min));
    memcpy(dst->aabb_max, src->aabb_max, sizeof(dst->aabb_max));
    dst->bsphere_radius = src->bsphere_radius;

    if (src->mesh) {
        rt_obj_retain_maybe(src->mesh);
        dst->mesh = src->mesh;
    }
    if (src->material) {
        rt_obj_retain_maybe(src->material);
        dst->material = src->material;
    }
    if (src->name) {
        rt_obj_retain_maybe(src->name);
        dst->name = src->name;
    }

    if (src->lod_count > 0) {
        dst->lod_levels = calloc((size_t)src->lod_count, sizeof(*dst->lod_levels));
        if (!dst->lod_levels) {
            rt_trap("Model3D: lod allocation failed");
            return dst;
        }
        dst->lod_capacity = src->lod_count;
        dst->lod_count = src->lod_count;
        for (int32_t i = 0; i < src->lod_count; i++) {
            dst->lod_levels[i].distance = src->lod_levels[i].distance;
            dst->lod_levels[i].mesh = src->lod_levels[i].mesh;
            rt_obj_retain_maybe(dst->lod_levels[i].mesh);
        }
    }

    for (int32_t i = 0; i < src->child_count; i++) {
        rt_scene_node3d *child = model_clone_node(src->children[i]);
        if (child) {
            rt_scene_node3d_add_child(dst, child);
            model_release_local(child);
        }
    }

    return dst;
}

static void model_clone_children_to_root(rt_scene_node3d *dst_root, const rt_scene_node3d *src_root) {
    if (!dst_root || !src_root)
        return;
    for (int32_t i = 0; i < src_root->child_count; i++) {
        rt_scene_node3d *child = model_clone_node(src_root->children[i]);
        if (child) {
            rt_scene_node3d_add_child(dst_root, child);
            model_release_local(child);
        }
    }
}

static void model_collect_scene_refs(rt_model3d *model, const rt_scene_node3d *node) {
    if (!model || !node)
        return;

    model_append_unique_ref(
        &model->meshes, &model->mesh_count, &model->mesh_capacity, node->mesh,
        "Model3D.Load: mesh list allocation failed");
    model_append_unique_ref(&model->materials,
                            &model->material_count,
                            &model->material_capacity,
                            node->material,
                            "Model3D.Load: material list allocation failed");
    for (int32_t i = 0; i < node->lod_count; i++) {
        model_append_unique_ref(&model->meshes,
                                &model->mesh_count,
                                &model->mesh_capacity,
                                node->lod_levels[i].mesh,
                                "Model3D.Load: mesh list allocation failed");
    }
    for (int32_t i = 0; i < node->child_count; i++)
        model_collect_scene_refs(model, node->children[i]);
}

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
void *rt_model3d_load(rt_string path) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    rt_model3d *model;

    if (!path || !path_cstr) {
        rt_trap("Model3D.Load: invalid path");
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
        for (int32_t i = 0; i < model->template_root->child_count; i++)
            model_collect_scene_refs(model, model->template_root->children[i]);
        model_release_local(scene);
    } else if (model_has_ext(path_cstr, ".gltf") || model_has_ext(path_cstr, ".glb")) {
        void *asset = rt_gltf_load(path);
        int64_t mesh_count;
        int64_t material_count;
        void *scene_root;
        if (!asset)
            goto fail;
        mesh_count = rt_gltf_mesh_count(asset);
        material_count = rt_gltf_material_count(asset);
        for (int64_t i = 0; i < mesh_count; i++) {
            model_append_ref(&model->meshes,
                             &model->mesh_count,
                             &model->mesh_capacity,
                             rt_gltf_get_mesh(asset, i),
                             "Model3D.Load: mesh list allocation failed");
        }
        for (int64_t i = 0; i < material_count; i++) {
            model_append_ref(&model->materials,
                             &model->material_count,
                             &model->material_capacity,
                             rt_gltf_get_material(asset, i),
                             "Model3D.Load: material list allocation failed");
        }
        scene_root = rt_gltf_get_scene_root(asset);
        if (scene_root)
            model_clone_children_to_root(model->template_root, (const rt_scene_node3d *)scene_root);
        model_build_synth_mesh_nodes(model);
        model_release_local(asset);
    } else if (model_has_ext(path_cstr, ".fbx")) {
        void *asset = rt_fbx_load(path);
        int64_t mesh_count;
        int64_t material_count;
        int64_t animation_count;
        void *skeleton;
        if (!asset)
            goto fail;
        mesh_count = rt_fbx_mesh_count(asset);
        material_count = rt_fbx_material_count(asset);
        animation_count = rt_fbx_animation_count(asset);
        skeleton = rt_fbx_get_skeleton(asset);

        for (int64_t i = 0; i < mesh_count; i++) {
            model_append_ref(&model->meshes,
                             &model->mesh_count,
                             &model->mesh_capacity,
                             rt_fbx_get_mesh(asset, i),
                             "Model3D.Load: mesh list allocation failed");
        }
        for (int64_t i = 0; i < material_count; i++) {
            model_append_ref(&model->materials,
                             &model->material_count,
                             &model->material_capacity,
                             rt_fbx_get_material(asset, i),
                             "Model3D.Load: material list allocation failed");
        }
        if (skeleton) {
            model_append_ref(&model->skeletons,
                             &model->skeleton_count,
                             &model->skeleton_capacity,
                             skeleton,
                             "Model3D.Load: skeleton list allocation failed");
        }
        for (int64_t i = 0; i < animation_count; i++) {
            model_append_ref(&model->animations,
                             &model->animation_count,
                             &model->animation_capacity,
                             rt_fbx_get_animation(asset, i),
                             "Model3D.Load: animation list allocation failed");
        }
        model_build_synth_mesh_nodes(model);
        model_release_local(asset);
    } else {
        rt_trap("Model3D.Load: unsupported file extension");
        goto fail;
    }

    return model;

fail:
    model_release_local(model);
    return NULL;
}

/// @brief Number of meshes loaded into this model.
int64_t rt_model3d_get_mesh_count(void *obj) {
    return obj ? ((rt_model3d *)obj)->mesh_count : 0;
}

/// @brief Number of materials loaded into this model.
int64_t rt_model3d_get_material_count(void *obj) {
    return obj ? ((rt_model3d *)obj)->material_count : 0;
}

/// @brief Number of skeletons (bone hierarchies) loaded into this model.
int64_t rt_model3d_get_skeleton_count(void *obj) {
    return obj ? ((rt_model3d *)obj)->skeleton_count : 0;
}

/// @brief Number of animation clips loaded into this model.
int64_t rt_model3d_get_animation_count(void *obj) {
    return obj ? ((rt_model3d *)obj)->animation_count : 0;
}

/// @brief Number of scene nodes in the template subtree (excludes the synthetic root).
int64_t rt_model3d_get_node_count(void *obj) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || !model->template_root)
        return 0;
    return model_count_subtree(model->template_root) - 1;
}

/// @brief Borrow the i-th Mesh3D (NULL on out-of-range). Caller must NOT release; the model owns it.
void *rt_model3d_get_mesh(void *obj, int64_t index) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || index < 0 || index >= model->mesh_count)
        return NULL;
    return model->meshes[index];
}

/// @brief Borrow the i-th Material3D (NULL on out-of-range).
void *rt_model3d_get_material(void *obj, int64_t index) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || index < 0 || index >= model->material_count)
        return NULL;
    return model->materials[index];
}

/// @brief Borrow the i-th Skeleton3D (NULL on out-of-range).
void *rt_model3d_get_skeleton(void *obj, int64_t index) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || index < 0 || index >= model->skeleton_count)
        return NULL;
    return model->skeletons[index];
}

/// @brief Borrow the i-th Animation3D clip (NULL on out-of-range).
void *rt_model3d_get_animation(void *obj, int64_t index) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || index < 0 || index >= model->animation_count)
        return NULL;
    return model->animations[index];
}

/// @brief Locate a node in the template subtree by exact name match. NULL if not found.
void *rt_model3d_find_node(void *obj, rt_string name) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || !model->template_root)
        return NULL;
    return rt_scene_node3d_find(model->template_root, name);
}

/// @brief Clone the template into an independent SceneNode3D subtree (with synthetic root).
/// Each instantiation creates a fresh deep-copy of the node hierarchy, suitable for adding to
/// a live scene without disturbing the template. Underlying meshes/materials are shared.
void *rt_model3d_instantiate(void *obj) {
    rt_model3d *model = (rt_model3d *)obj;
    if (!model || !model->template_root)
        return NULL;
    return model_clone_node(model->template_root);
}

/// @brief Build a fresh Scene3D from the template's children, cloning each one. Use when the
/// caller wants the model exposed as a top-level scene rather than a node subtree.
void *rt_model3d_instantiate_scene(void *obj) {
    rt_model3d *model = (rt_model3d *)obj;
    rt_scene3d *scene;
    if (!model || !model->template_root)
        return NULL;
    scene = (rt_scene3d *)rt_scene3d_new();
    if (!scene)
        return NULL;
    for (int32_t i = 0; i < model->template_root->child_count; i++) {
        rt_scene_node3d *child = model_clone_node(model->template_root->children[i]);
        if (child) {
            rt_scene3d_add(scene, child);
            model_release_local(child);
        }
    }
    return scene;
}

#else
typedef int rt_model3d_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
