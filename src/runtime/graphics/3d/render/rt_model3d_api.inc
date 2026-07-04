//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/render/rt_model3d_api.inc  (#included from rt_model3d.c; not a
// standalone translation unit) Purpose: Model3D public load API, accessors, and instantiation
//===----------------------------------------------------------------------===//
/// @brief Convert a SceneAsset load return value into a Result.
/// @details Successful values are retained by `Result.Ok` and then released from the local
///          loader reference. Failures use the thread-local asset diagnostic message captured by
///          the compatibility APIs, falling back to @p fallback when no message is available.
/// @param value Loader return value, or NULL on recoverable failure.
/// @param fallback Fallback error message for empty diagnostics.
/// @return Owned `Viper.Result` carrying @p value or an error string.
static void *model3d_load_value_to_result(void *value, const char *fallback) {
    if (value) {
        void *result = rt_result_ok(value);
        model_release_local(value);
        return result;
    }

    const char *message = rt_asset_error_get_message();
    if (!message || message[0] == '\0')
        return rt_result_err_str(rt_const_cstr(fallback ? fallback : "SceneAsset load failed"));

    rt_string err = rt_string_from_bytes(message, strlen(message));
    void *result = rt_result_err_str(err);
    rt_str_release_maybe(err);
    return result;
}

/// @brief Load a model from the filesystem (no asset-manager resolution). See header.
void *rt_model3d_load(rt_string path) {
    rt_asset_error_begin_load();
    void *model = rt_model3d_load_impl(path, 0, NULL, 0, NULL, NULL, 0);
    if (model) {
        rt_asset_error_end_load_success();
    } else {
        rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT, "Model3D.Load: failed to load model");
        rt_asset_error_end_load_failure();
    }
    return model;
}

/// @brief Load a model from the filesystem as `Result.Ok(SceneAsset)` or `Result.Err(String)`.
/// @param path Filesystem path to load.
/// @return Owned Viper.Result value.
void *rt_model3d_load_result(rt_string path) {
    return model3d_load_value_to_result(rt_model3d_load(path), "SceneAsset.Load failed");
}

/// @brief Load a model with explicit import options (Plan 09).
/// @details `force_tangents` generates tangents for every UV0-mapped glTF primitive even
///   when its material has no normal map at load time — for assets whose materials gain
///   normal maps after import. FBX ignores the option. Options scope thread-locally
///   around the load, so the default entry points are unchanged.
void *rt_model3d_load_with_options(rt_string path, int8_t force_tangents) {
    rt_gltf_load_options opts = rt_gltf_load_options_default();
    rt_gltf_load_options saved;
    void *model;

    opts.force_tangents = force_tangents ? 1 : 0;
    saved = rt_gltf_set_thread_load_options(&opts);
    model = rt_model3d_load(path);
    (void)rt_gltf_set_thread_load_options(&saved);
    return model;
}

/// @brief Options-aware Result variant of `SceneAsset.Load` (Plan 09).
void *rt_model3d_load_result_with_options(rt_string path, int8_t force_tangents) {
    return model3d_load_value_to_result(rt_model3d_load_with_options(path, force_tangents),
                                        "SceneAsset.LoadWithOptions failed");
}

/// @brief Load a model through the asset manager (mounted/embedded + dev fallback). See header.
void *rt_model3d_load_asset(rt_string path) {
    rt_asset_error_begin_load();
    void *model = rt_model3d_load_impl(path, 1, NULL, 0, NULL, NULL, 0);
    if (model) {
        rt_asset_error_end_load_success();
    } else {
        rt_asset_error_set_if_empty(RT_ASSET_ERROR_CORRUPT,
                                    "Model3D.LoadAsset: failed to load model");
        rt_asset_error_end_load_failure();
    }
    return model;
}

/// @brief Load a model through the asset manager as `Result.Ok(SceneAsset)` or `Result.Err`.
/// @param path Asset path or URI to load.
/// @return Owned Viper.Result value.
void *rt_model3d_load_asset_result(rt_string path) {
    return model3d_load_value_to_result(rt_model3d_load_asset(path), "SceneAsset.LoadAsset failed");
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
    return rt_model3d_load_impl(
        path, load_assets ? 1 : 0, preloaded_data, preloaded_size, NULL, NULL, 0);
}

/// @brief Internal async path: build an FBX Model3D from worker-staged root bytes.
/// @details Takes ownership of @p preloaded_data. Non-FBX paths free it and use the normal
///   loader so conservative async callers do not leak when falling back to main-thread loading.
void *rt_model3d_load_preloaded_fbx(rt_string path,
                                    uint8_t *preloaded_data,
                                    size_t preloaded_size,
                                    int load_assets) {
    const char *path_cstr = path ? rt_string_cstr(path) : NULL;
    if (!path_cstr || !model_has_ext(path_cstr, ".fbx")) {
        free(preloaded_data);
        return load_assets ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    }
    return rt_model3d_load_impl(
        path, load_assets ? 1 : 0, NULL, 0, NULL, preloaded_data, preloaded_size);
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
    return rt_model3d_load_impl(path, load_assets ? 1 : 0, NULL, 0, bundle, NULL, 0);
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

/// @brief Number of node/object/morph/camera animation clips loaded into this model.
int64_t rt_model3d_get_node_animation_count(void *obj) {
    rt_model3d *model = model3d_checked(obj);
    return model ? model_clamped_array_count(model->node_animations,
                                             model->node_animation_count,
                                             model->node_animation_capacity)
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
    int32_t material_count = model ? model_clamped_array_count(model->materials,
                                                               model->material_count,
                                                               model->material_capacity)
                                   : 0;
    if (!model || index < 0 || index >= material_count)
        return NULL;
    return rt_g3d_checked_or_null(model->materials[index], RT_G3D_MATERIAL3D_CLASS_ID);
}

/// @brief Borrow the i-th Skeleton3D (NULL on out-of-range).
void *rt_model3d_get_skeleton(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t skeleton_count = model ? model_clamped_array_count(model->skeletons,
                                                               model->skeleton_count,
                                                               model->skeleton_capacity)
                                   : 0;
    if (!model || index < 0 || index >= skeleton_count)
        return NULL;
    return rt_g3d_checked_or_null(model->skeletons[index], RT_G3D_SKELETON3D_CLASS_ID);
}

/// @brief Borrow the i-th Animation3D clip (NULL on out-of-range).
void *rt_model3d_get_animation(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t animation_count = model ? model_clamped_array_count(model->animations,
                                                                model->animation_count,
                                                                model->animation_capacity)
                                    : 0;
    if (!model || index < 0 || index >= animation_count)
        return NULL;
    return rt_g3d_checked_or_null(model->animations[index], RT_G3D_ANIMATION3D_CLASS_ID);
}

/// @brief Borrow the i-th NodeAnimation3D clip (NULL on out-of-range).
void *rt_model3d_get_node_animation(void *obj, int64_t index) {
    rt_model3d *model = model3d_checked(obj);
    int32_t animation_count = model ? model_clamped_array_count(model->node_animations,
                                                                model->node_animation_count,
                                                                model->node_animation_capacity)
                                    : 0;
    if (!model || index < 0 || index >= animation_count)
        return NULL;
    return rt_g3d_checked_or_null(model->node_animations[index], RT_G3D_NODEANIMATION3D_CLASS_ID);
}

/// @brief Borrow the i-th NodeAnimation3D name, or an empty string on out-of-range.
rt_string rt_model3d_get_node_animation_name(void *obj, int64_t index) {
    return rt_node_animation3d_get_name(rt_model3d_get_node_animation(obj, index));
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

/// @brief Load a temporary Model3D and return one retained clip from it.
static void *model3d_load_clip_from_model(rt_string path,
                                          int64_t index,
                                          int asset_path,
                                          int node_animation) {
    void *model = asset_path ? rt_model3d_load_asset(path) : rt_model3d_load(path);
    void *clip = NULL;
    if (!model)
        return NULL;
    clip = node_animation ? rt_model3d_get_node_animation(model, index)
                          : rt_model3d_get_animation(model, index);
    if (clip)
        rt_obj_retain_maybe(clip);
    model_release_local(model);
    return clip;
}

/// @brief Load a model file and return a retained skeletal Animation3D clip by index.
void *rt_model3d_load_animation(rt_string path, int64_t index) {
    return model3d_load_clip_from_model(path, index, 0, 0);
}

/// @brief Load a skeletal animation clip as a Result.
/// @param path Filesystem model path.
/// @param index Zero-based skeletal animation index.
/// @return Owned Viper.Result carrying Animation3D or an error string.
void *rt_model3d_load_animation_result(rt_string path, int64_t index) {
    return model3d_load_value_to_result(rt_model3d_load_animation(path, index),
                                        "SceneAsset.LoadAnimation failed");
}

/// @brief Load a packed model asset and return a retained skeletal Animation3D clip by index.
void *rt_model3d_load_animation_asset(rt_string path, int64_t index) {
    return model3d_load_clip_from_model(path, index, 1, 0);
}

/// @brief Load a skeletal animation clip from a packed asset as a Result.
/// @param path Asset path or URI.
/// @param index Zero-based skeletal animation index.
/// @return Owned Viper.Result carrying Animation3D or an error string.
void *rt_model3d_load_animation_asset_result(rt_string path, int64_t index) {
    return model3d_load_value_to_result(rt_model3d_load_animation_asset(path, index),
                                        "SceneAsset.LoadAnimationAsset failed");
}

/// @brief Load a model file and return a retained NodeAnimation3D clip by index.
void *rt_model3d_load_node_animation(rt_string path, int64_t index) {
    return model3d_load_clip_from_model(path, index, 0, 1);
}

/// @brief Load a node/object animation clip as a Result.
/// @param path Filesystem model path.
/// @param index Zero-based node animation index.
/// @return Owned Viper.Result carrying NodeAnimation3D or an error string.
void *rt_model3d_load_node_animation_result(rt_string path, int64_t index) {
    return model3d_load_value_to_result(rt_model3d_load_node_animation(path, index),
                                        "SceneAsset.LoadNodeAnimation failed");
}

/// @brief Load a packed model asset and return a retained NodeAnimation3D clip by index.
void *rt_model3d_load_node_animation_asset(rt_string path, int64_t index) {
    return model3d_load_clip_from_model(path, index, 1, 1);
}

/// @brief Load a node/object animation clip from a packed asset as a Result.
/// @param path Asset path or URI.
/// @param index Zero-based node animation index.
/// @return Owned Viper.Result carrying NodeAnimation3D or an error string.
void *rt_model3d_load_node_animation_asset_result(rt_string path, int64_t index) {
    return model3d_load_value_to_result(rt_model3d_load_node_animation_asset(path, index),
                                        "SceneAsset.LoadNodeAnimationAsset failed");
}

/// @brief Locate a node in the template subtree by exact name match. NULL if not found.
void *rt_model3d_find_node(void *obj, rt_string name) {
    rt_model3d *model = model3d_checked(obj);
    if (!model || !model->template_root)
        return NULL;
    return model_find_node_safe(model->template_root, name);
}

/// @brief Locate a template node by exact name and return it as an Option.
/// @details This is the absence-aware companion to `rt_model3d_find_node`.
///          The found node is still part of the immutable imported template;
///          mutate instantiated nodes rather than the template node.
/// @param obj SceneAsset/Model3D handle.
/// @param name Exact node name to match.
/// @return `Some(SceneNode3D)` when the template contains the node, otherwise `None`.
void *rt_model3d_find_node_option(void *obj, rt_string name) {
    void *node = rt_model3d_find_node(obj, name);
    return node ? rt_option_some(node) : rt_option_none();
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
