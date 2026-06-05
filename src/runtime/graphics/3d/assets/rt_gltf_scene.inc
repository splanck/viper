//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_gltf_scene.inc  (#included from rt_gltf.c; not a standalone translation unit)
// Purpose: glTF scene-graph node build and scene import
//===----------------------------------------------------------------------===//

static void gltf_node_read_transform(void *node_json, rt_scene_node3d *node) {
    void *matrix_arr = jarr(node_json, "matrix");
    if (matrix_arr && jarr_len(matrix_arr) >= 16) {
        double m[16];
        double row_major[16];
        for (int i = 0; i < 16; i++)
            m[i] = jvalue_num(rt_seq_get(matrix_arr, (int64_t)i), i % 5 == 0 ? 1.0 : 0.0);
        gltf_matrix_column_major_to_row_major(m, row_major);
        gltf_matrix_to_trs(row_major, node->position, node->rotation, node->scale_xyz);
    } else {
        void *translation = jarr(node_json, "translation");
        void *rotation = jarr(node_json, "rotation");
        void *scale_arr = jarr(node_json, "scale");
        node->position[0] = gltf_arr_num(translation, 0, 0.0);
        node->position[1] = gltf_arr_num(translation, 1, 0.0);
        node->position[2] = gltf_arr_num(translation, 2, 0.0);
        node->rotation[0] = gltf_arr_num(rotation, 0, 0.0);
        node->rotation[1] = gltf_arr_num(rotation, 1, 0.0);
        node->rotation[2] = gltf_arr_num(rotation, 2, 0.0);
        node->rotation[3] = gltf_arr_num(rotation, 3, 1.0);
        node->scale_xyz[0] = gltf_arr_num(scale_arr, 0, 1.0);
        node->scale_xyz[1] = gltf_arr_num(scale_arr, 1, 1.0);
        node->scale_xyz[2] = gltf_arr_num(scale_arr, 2, 1.0);
    }
    gltf_sanitize_trs(node->position, node->rotation, node->scale_xyz);
    node->world_dirty = 1;
}

static int gltf_node_bind_light(rt_scene_node3d *node,
                                int64_t light_ref,
                                void **imported_lights,
                                int32_t imported_light_count) {
    if (light_ref < 0)
        return 1;
    if (light_ref < imported_light_count && imported_lights && imported_lights[light_ref]) {
        rt_scene_node3d_set_light(node, imported_lights[light_ref]);
        return 1;
    }
    return 0;
}

static void gltf_node_set_primitive_material(rt_scene_node3d *node,
                                             void **primitive_materials,
                                             int32_t primitive_index) {
    if (primitive_materials && primitive_index >= 0 && primitive_materials[primitive_index])
        rt_scene_node3d_set_material(node, primitive_materials[primitive_index]);
}

static int gltf_node_attach_primitive_mesh(rt_gltf_asset *asset,
                                           rt_scene_node3d *node,
                                           int32_t mesh_index,
                                           int64_t skin_ref,
                                           void *weights_arr,
                                           const gltf_skin_t *skins,
                                           int32_t skin_count) {
    void *node_mesh;
    if (!asset || !node || mesh_index < 0 || mesh_index >= asset->mesh_count)
        return 0;
    node_mesh =
        gltf_make_node_mesh_variant(asset, mesh_index, skin_ref, weights_arr, skins, skin_count);
    if (!node_mesh)
        return 0;
    rt_scene_node3d_set_mesh(node, node_mesh);
    if (node_mesh != asset->meshes[mesh_index])
        gltf_release_local(node_mesh);
    return 1;
}

static int gltf_node_attach_extra_primitive(rt_gltf_asset *asset,
                                            rt_scene_node3d *node,
                                            int32_t mesh_index,
                                            int64_t skin_ref,
                                            void *weights_arr,
                                            void **primitive_materials,
                                            const gltf_skin_t *skins,
                                            int32_t skin_count) {
    rt_scene_node3d *prim_node = (rt_scene_node3d *)rt_scene_node3d_new();
    if (!prim_node)
        return 0;
    if (!gltf_node_attach_primitive_mesh(
            asset, prim_node, mesh_index, skin_ref, weights_arr, skins, skin_count)) {
        gltf_release_local(prim_node);
        return 0;
    }
    gltf_node_set_primitive_material(prim_node, primitive_materials, mesh_index);
    if (!rt_scene_node3d_try_add_child(node, prim_node)) {
        gltf_release_local(prim_node);
        return 0;
    }
    gltf_release_local(prim_node);
    return 1;
}

static int gltf_node_attach_mesh_primitives(rt_gltf_asset *asset,
                                            rt_scene_node3d *node,
                                            int64_t mesh_ref,
                                            int64_t skin_ref,
                                            void *weights_arr,
                                            int32_t mesh_json_count,
                                            int *mesh_prim_start,
                                            int *mesh_prim_count,
                                            void **primitive_materials,
                                            const gltf_skin_t *skins,
                                            int32_t skin_count) {
    int32_t prim_start;
    int32_t prim_count;
    if (mesh_ref < 0)
        return 1;
    /* Reject an out-of-range mesh reference unconditionally — including when the
     * whole document declares zero primitives, in which case the prim tables are
     * NULL. This bounds check is what makes GLTF.Load reject a node pointing at a
     * non-existent mesh; gating it behind the prim-table NULL check (below) let
     * such nodes load silently. */
    if (mesh_ref >= mesh_json_count)
        return 0;
    if (!mesh_prim_count || !mesh_prim_start)
        return 1;
    prim_start = mesh_prim_start[mesh_ref];
    prim_count = mesh_prim_count[mesh_ref];
    if (prim_count <= 0)
        return 1;
    if (prim_start < 0 || prim_start > asset->mesh_count ||
        prim_count > asset->mesh_count - prim_start)
        return 0;
    if (!gltf_node_attach_primitive_mesh(asset,
                                         node,
                                         prim_start,
                                         skin_ref,
                                         weights_arr,
                                         skins,
                                         skin_count))
        return 0;
    gltf_node_set_primitive_material(node, primitive_materials, prim_start);
    for (int32_t pi = 1; pi < prim_count; pi++) {
        if (!gltf_node_attach_extra_primitive(asset,
                                             node,
                                             prim_start + pi,
                                             skin_ref,
                                             weights_arr,
                                             primitive_materials,
                                             skins,
                                             skin_count))
            return 0;
    }
    return 1;
}

static int gltf_build_template_node(rt_gltf_asset *asset,
                                    void *nodes_arr,
                                    int32_t node_index,
                                    int32_t mesh_json_count,
                                    int *mesh_prim_start,
                                    int *mesh_prim_count,
                                    void **primitive_materials,
                                    gltf_skin_t *skins,
                                    int32_t skin_count,
                                    void **imported_lights,
                                    int32_t imported_light_count,
                                    rt_scene_node3d **out_node) {
    void *node_json = rt_seq_get(nodes_arr, (int64_t)node_index);
    rt_scene_node3d *node = (rt_scene_node3d *)rt_scene_node3d_new();
    const char *name = jstr(node_json, "name");
    char fallback_name[64];
    void *weights_arr = jarr(node_json, "weights");
    void *node_extensions = jget(node_json, "extensions");
    void *node_punctual_light =
        node_extensions ? jget(node_extensions, "KHR_lights_punctual") : NULL;
    int64_t mesh_ref = jint(node_json, "mesh", -1);
    int64_t skin_ref = jint(node_json, "skin", -1);
    int64_t light_ref = jint(node_punctual_light, "light", -1);

    *out_node = node;
    if (!node)
        return 0;
    node->import_index = node_index;
    if (!name || name[0] == '\0')
        name =
            gltf_effective_node_name(nodes_arr, node_index, fallback_name, sizeof(fallback_name));
    gltf_set_node_name(node, name);
    if (mesh_ref < -1 || skin_ref < -1 || light_ref < -1)
        return 0;
    if (!gltf_node_bind_light(node, light_ref, imported_lights, imported_light_count))
        return 0;
    gltf_node_read_transform(node_json, node);
    if (skin_ref >= 0 && skin_ref >= skin_count)
        return 0;
    return gltf_node_attach_mesh_primitives(asset,
                                           node,
                                           mesh_ref,
                                           skin_ref,
                                           weights_arr,
                                           mesh_json_count,
                                           mesh_prim_start,
                                           mesh_prim_count,
                                           primitive_materials,
                                           skins,
                                           skin_count);
}

static int gltf_link_template_node_children(void *nodes_arr,
                                            int32_t node_json_count,
                                            rt_scene_node3d **nodes) {
    for (int32_t ni = 0; ni < node_json_count && nodes; ni++) {
        void *node_json = rt_seq_get(nodes_arr, (int64_t)ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int64_t child_idx = jvalue_int(rt_seq_get(children, ci), -1);
            if (child_idx >= 0 && child_idx < node_json_count && nodes[ni] && nodes[child_idx]) {
                if (!rt_scene_node3d_try_add_child(nodes[ni], nodes[child_idx]))
                    return 0;
            }
        }
    }
    return 1;
}

static int64_t gltf_scene_order_index(int64_t order, int64_t active_scene, int active_valid) {
    if (!active_valid)
        return order;
    return order == 0 ? active_scene : (order <= active_scene ? order - 1 : order);
}

static int gltf_scene_roots_are_valid(void *scene_nodes,
                                      int32_t node_json_count,
                                      const int *node_parent,
                                      int *scene_seen) {
    for (int64_t i = 0; i < jarr_len(scene_nodes); i++) {
        int64_t node_idx = jvalue_int(rt_seq_get(scene_nodes, i), -1);
        if (node_idx < 0 || node_idx >= node_json_count || scene_seen[node_idx] ||
            (node_parent && node_parent[node_idx] >= 0))
            return 0;
        scene_seen[node_idx] = 1;
    }
    return 1;
}

static int gltf_clone_scene_root_into(rt_scene_node3d *scene_root,
                                      void *nodes_arr,
                                      int32_t node_json_count,
                                      rt_scene_node3d **nodes,
                                      int32_t node_idx,
                                      uint8_t *scene_active) {
    rt_scene_node3d *clone;
    if (node_idx < 0 || node_idx >= node_json_count || !nodes[node_idx])
        return 1;
    clone = gltf_clone_scene_node(nodes[node_idx]);
    if (!clone)
        return 0;
    if (!rt_scene_node3d_try_add_child(scene_root, clone)) {
        gltf_release_local(clone);
        return 0;
    }
    gltf_release_local(clone);
    return gltf_mark_active_node_subtree(nodes_arr, node_json_count, node_idx, scene_active);
}

static int gltf_import_scene_from_json(rt_gltf_asset *asset,
                                       void *root,
                                       void *nodes_arr,
                                       rt_scene_node3d **nodes,
                                       int32_t node_json_count,
                                       const int *node_parent,
                                       void *scene_json,
                                       int64_t scene_index,
                                       int64_t order,
                                       int *built_any_io) {
    void *scene_nodes = jarr(scene_json, "nodes");
    int *scene_seen = (int *)calloc((size_t)node_json_count, sizeof(int));
    uint8_t *scene_active = (uint8_t *)calloc((size_t)node_json_count, sizeof(*scene_active));
    rt_scene_node3d *scene_root = NULL;
    int ok = scene_seen != NULL && scene_active != NULL;
    char fallback_scene_name[64];
    const char *scene_name = jstr(scene_json, "name");
    if (ok)
        ok = gltf_scene_roots_are_valid(scene_nodes, node_json_count, node_parent, scene_seen);
    if (ok) {
        scene_root = (rt_scene_node3d *)rt_scene_node3d_new();
        ok = scene_root != NULL;
    }
    for (int64_t i = 0; ok && i < jarr_len(scene_nodes); i++) {
        int64_t node_idx = jvalue_int(rt_seq_get(scene_nodes, i), -1);
        ok = gltf_clone_scene_root_into(
            scene_root, nodes_arr, node_json_count, nodes, (int32_t)node_idx, scene_active);
    }
    if (ok && scene_root) {
        snprintf(fallback_scene_name,
                 sizeof(fallback_scene_name),
                 order == 0 ? "default" : "scene_%d",
                 (int)scene_index);
        ok = gltf_append_scene(asset,
                               scene_root,
                               (scene_name && scene_name[0] != '\0') ? scene_name
                                                                      : fallback_scene_name);
        if (ok) {
            gltf_scene_info_t *scene = &asset->scenes[asset->scene_count - 1];
            ok =
                gltf_import_scene_cameras(scene, root, nodes_arr, nodes, node_json_count, scene_active);
            scene_root = NULL;
            *built_any_io = 1;
        }
    }
    if (scene_root)
        gltf_release_local(scene_root);
    free(scene_active);
    free(scene_seen);
    return ok;
}

static int gltf_import_fallback_scene(rt_gltf_asset *asset,
                                      void *root,
                                      void *nodes_arr,
                                      rt_scene_node3d **nodes,
                                      int32_t node_json_count,
                                      const int *node_parent) {
    rt_scene_node3d *scene_root = (rt_scene_node3d *)rt_scene_node3d_new();
    uint8_t *scene_active = (uint8_t *)calloc((size_t)node_json_count, sizeof(*scene_active));
    int attached_any = 0;
    int ok = scene_root != NULL && scene_active != NULL;
    for (int32_t i = 0; ok && i < node_json_count; i++) {
        if (node_parent && node_parent[i] < 0 && nodes[i]) {
            ok = gltf_clone_scene_root_into(
                scene_root, nodes_arr, node_json_count, nodes, i, scene_active);
            if (ok)
                attached_any = 1;
        }
    }
    if (ok && attached_any && gltf_append_scene(asset, scene_root, "default")) {
        gltf_scene_info_t *scene = &asset->scenes[asset->scene_count - 1];
        ok = gltf_import_scene_cameras(scene, root, nodes_arr, nodes, node_json_count, scene_active);
        scene_root = NULL;
    } else if (ok) {
        ok = 0;
    }
    if (scene_root)
        gltf_release_local(scene_root);
    free(scene_active);
    return ok;
}

static int gltf_import_node_scenes(rt_gltf_asset *asset,
                                   void *root,
                                   void *nodes_arr,
                                   rt_scene_node3d **nodes,
                                   int32_t node_json_count,
                                   const int *node_parent) {
    void *scenes_arr = jarr(root, "scenes");
    int64_t active_scene = jint(root, "scene", 0);
    int64_t scene_count = jarr_len(scenes_arr);
    int built_any = 0;
    int active_valid =
        scenes_arr && scene_count > 0 && active_scene >= 0 && active_scene < scene_count;
    if (scenes_arr && scene_count > 0) {
        for (int64_t order = 0; order < scene_count; order++) {
            int64_t scene_index = gltf_scene_order_index(order, active_scene, active_valid);
            if (!gltf_import_scene_from_json(asset,
                                             root,
                                             nodes_arr,
                                             nodes,
                                             node_json_count,
                                             node_parent,
                                             rt_seq_get(scenes_arr, scene_index),
                                             scene_index,
                                             order,
                                             &built_any))
                return 0;
        }
    }
    if (!built_any &&
        !gltf_import_fallback_scene(asset, root, nodes_arr, nodes, node_json_count, node_parent))
        return 0;
    return gltf_install_active_scene_compat(asset);
}

/// @brief Build the reusable template node hierarchy and per-scene roots for a
///        glTF asset. Extracted post-asset phase of rt_gltf_load_impl; operates
///        only on already-decoded meshes/materials/skins and never early-returns.
/// @return Non-zero (load_failed) on a fatal allocation/clone failure.
static int gltf_build_node_hierarchy(rt_gltf_asset *asset,
                                     void *root,
                                     int *mesh_prim_start,
                                     int *mesh_prim_count,
                                     void **primitive_materials,
                                     gltf_skin_t *skins,
                                     int32_t skin_count,
                                     void **imported_lights,
                                     int32_t imported_light_count) {
    int load_failed = 0;
    int mesh_json_count = (int)jarr_len(jarr(root, "meshes"));
    {
        void *nodes_arr = jarr(root, "nodes");
        int node_json_count = (int)jarr_len(nodes_arr);
        if (node_json_count > 0) {
            int *node_parent = NULL;
            int graph_valid = gltf_validate_node_graph(nodes_arr, node_json_count, &node_parent);
            rt_scene_node3d **nodes =
                graph_valid ? (rt_scene_node3d **)calloc((size_t)node_json_count, sizeof(*nodes))
                            : NULL;
            if (!graph_valid || !nodes)
                load_failed = 1;
            for (int ni = 0; ni < node_json_count && nodes; ni++) {
                if (!gltf_build_template_node(asset,
                                              nodes_arr,
                                              ni,
                                              mesh_json_count,
                                              mesh_prim_start,
                                              mesh_prim_count,
                                              primitive_materials,
                                              skins,
                                              skin_count,
                                              imported_lights,
                                              imported_light_count,
                                              &nodes[ni])) {
                    load_failed = 1;
                    continue;
                }
            }

            if (!load_failed && !gltf_link_template_node_children(nodes_arr, node_json_count, nodes))
                load_failed = 1;
            if (!load_failed && graph_valid && nodes &&
                !gltf_import_node_scenes(asset, root, nodes_arr, nodes, node_json_count, node_parent))
                load_failed = 1;

            for (int i = 0; i < node_json_count && nodes; i++)
                gltf_release_ref((void **)&nodes[i]);
            free(nodes);
            free(node_parent);
        }
    }
    return load_failed;
}
