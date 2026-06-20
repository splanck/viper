//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_gltf_mesh.inc  (#included from rt_gltf.c; not a standalone translation unit)
// Purpose: glTF mesh import (preloaded and JSON paths)
//===----------------------------------------------------------------------===//

/// @brief Append one imported vertex to @p mesh during glTF import, taking position, normal,
///   UV0/UV1, color, and tangent (any pointer may be NULL to use that attribute's default).
/// @return 1 on success, 0 if the mesh vertex storage cannot grow.
static int gltf_mesh_append_import_vertex(rt_mesh3d *mesh,
                                          const float *pos,
                                          const float *nrm,
                                          const float *uv0,
                                          const float *uv1,
                                          const float *color,
                                          const float *tangent) {
    vgfx3d_vertex_t *vertex;
    if (!mesh || mesh->build_failed || !pos || !nrm || !uv0 || !uv1 || !color || !tangent)
        return 0;
    if (mesh->vertex_count >= mesh->vertex_capacity || mesh->vertex_count == UINT32_MAX)
        return 0;
    vertex = &mesh->vertices[mesh->vertex_count++];
    memset(vertex, 0, sizeof(*vertex));
    vertex->pos[0] = pos[0];
    vertex->pos[1] = pos[1];
    vertex->pos[2] = pos[2];
    if (mesh->positions64) {
        mesh->positions64[(size_t)(mesh->vertex_count - 1u) * 3u + 0] = (double)pos[0];
        mesh->positions64[(size_t)(mesh->vertex_count - 1u) * 3u + 1] = (double)pos[1];
        mesh->positions64[(size_t)(mesh->vertex_count - 1u) * 3u + 2] = (double)pos[2];
    }
    vertex->normal[0] = nrm[0];
    vertex->normal[1] = nrm[1];
    vertex->normal[2] = nrm[2];
    vertex->uv[0] = uv0[0];
    vertex->uv[1] = uv0[1];
    vertex->uv1[0] = uv1[0];
    vertex->uv1[1] = uv1[1];
    memcpy(vertex->color, color, sizeof(vertex->color));
    memcpy(vertex->tangent, tangent, sizeof(vertex->tangent));
    return 1;
}

typedef struct {
    gltf_accessor_view_t pos;
    gltf_accessor_view_t norm;
    gltf_accessor_view_t uv0;
    gltf_accessor_view_t uv1;
    gltf_accessor_view_t color;
    gltf_accessor_view_t tangent;
    gltf_accessor_view_t joints;
    gltf_accessor_view_t weights;
    gltf_accessor_view_t joints1;
    gltf_accessor_view_t weights1;
    gltf_accessor_view_t idx;
    int has_normals;
    int has_uv0;
    int has_uv1;
    int has_colors;
    int has_tangents;
    int has_joints;
    int has_weights;
    int has_joints1;
    int has_weights1;
    int has_indices;
    int has_skin0;
    int has_skin1;
    int32_t triangle_estimate;
} gltf_mesh_primitive_views_t;

static void *gltf_mesh_resolve_primitive_material(rt_gltf_asset *asset,
                                                  int64_t material_idx,
                                                  int32_t material_capacity,
                                                  void **default_material_io) {
    void *prim_material = NULL;
    if (!asset)
        return NULL;
    if (material_idx >= 0 && material_idx < asset->material_count)
        prim_material = asset->materials[material_idx];
    if (!prim_material && asset->materials) {
        if (default_material_io && !*default_material_io &&
            asset->material_count < material_capacity) {
            *default_material_io = gltf_material_create_default_pbr();
            if (*default_material_io)
                asset->materials[asset->material_count++] = *default_material_io;
        }
        prim_material = default_material_io ? *default_material_io : NULL;
    }
    return prim_material;
}

static int gltf_mesh_take_preloaded_primitive(rt_gltf_asset *asset,
                                              rt_gltf_preload_bundle *preload_bundle,
                                              int32_t mesh_json_index,
                                              int32_t primitive_index,
                                              void *primitive_material,
                                              void **primitive_materials,
                                              int32_t *mesh_idx_io) {
    uint32_t decoded_mesh_flags = 0u;
    void *decoded_mesh =
        gltf_preload_take_decoded_mesh(preload_bundle, mesh_json_index, primitive_index, &decoded_mesh_flags);
    if (!decoded_mesh)
        return 0;
    if ((decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_NORMALS) == 0)
        rt_mesh3d_recalc_normals(decoded_mesh);
    if ((decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_TANGENTS) == 0 &&
        (decoded_mesh_flags & GLTF_PRELOAD_MESH_POD_HAS_UV0) != 0) {
        rt_material3d *tangent_material = (rt_material3d *)primitive_material;
        if (tangent_material && tangent_material->normal_map)
            rt_mesh3d_calc_tangents(decoded_mesh);
    }
    asset->meshes[*mesh_idx_io] = decoded_mesh;
    (*mesh_idx_io)++;
    asset->mesh_count = *mesh_idx_io;
    if (primitive_materials)
        primitive_materials[*mesh_idx_io - 1] = primitive_material;
    return 1;
}

static int gltf_mesh_validate_skin_views(gltf_mesh_primitive_views_t *views,
                                         int32_t vertex_count) {
    if (!views)
        return 0;
    if ((views->has_joints != views->has_weights) || (views->has_joints1 != views->has_weights1))
        return 0;
    if (views->has_joints &&
        (!gltf_accessor_has_payload(&views->joints) || !gltf_accessor_has_payload(&views->weights) ||
         !gltf_accessor_is_joints(&views->joints) || !gltf_accessor_is_weights(&views->weights) ||
         views->joints.count < vertex_count || views->weights.count < vertex_count))
        return 0;
    if (views->has_joints1 &&
        (!gltf_accessor_has_payload(&views->joints1) ||
         !gltf_accessor_has_payload(&views->weights1) ||
         !gltf_accessor_is_joints(&views->joints1) ||
         !gltf_accessor_is_weights(&views->weights1) || views->joints1.count < vertex_count ||
         views->weights1.count < vertex_count))
        return 0;
    return 1;
}

static int gltf_mesh_read_primitive_views(void *root,
                                          gltf_buffer_t *buffers,
                                          int32_t buf_count,
                                          void *attrs,
                                          void *prim,
                                          int64_t mode,
                                          gltf_mesh_primitive_views_t *views,
                                          int *load_failed_io) {
    int64_t pos_acc = jint(attrs, "POSITION", -1);
    int64_t norm_acc = jint(attrs, "NORMAL", -1);
    int64_t uv0_acc = jint(attrs, "TEXCOORD_0", -1);
    int64_t uv1_acc = jint(attrs, "TEXCOORD_1", -1);
    int64_t color_acc = jint(attrs, "COLOR_0", -1);
    int64_t tangent_acc = jint(attrs, "TANGENT", -1);
    int64_t joints_acc = jint(attrs, "JOINTS_0", -1);
    int64_t weights_acc = jint(attrs, "WEIGHTS_0", -1);
    int64_t joints1_acc = jint(attrs, "JOINTS_1", -1);
    int64_t weights1_acc = jint(attrs, "WEIGHTS_1", -1);
    int64_t idx_acc = jint(prim, "indices", -1);
    int32_t draw_count;
    if (!views)
        return 0;
    memset(views, 0, sizeof(*views));
    if (pos_acc < 0)
        return 0;

    views->has_normals = gltf_get_accessor_view(root, norm_acc, buffers, buf_count, &views->norm);
    views->has_uv0 = gltf_get_accessor_view(root, uv0_acc, buffers, buf_count, &views->uv0);
    views->has_uv1 = gltf_get_accessor_view(root, uv1_acc, buffers, buf_count, &views->uv1);
    views->has_colors = gltf_get_accessor_view(root, color_acc, buffers, buf_count, &views->color);
    views->has_tangents =
        gltf_get_accessor_view(root, tangent_acc, buffers, buf_count, &views->tangent);
    views->has_joints = gltf_get_accessor_view(root, joints_acc, buffers, buf_count, &views->joints);
    views->has_weights =
        gltf_get_accessor_view(root, weights_acc, buffers, buf_count, &views->weights);
    views->has_joints1 =
        gltf_get_accessor_view(root, joints1_acc, buffers, buf_count, &views->joints1);
    views->has_weights1 =
        gltf_get_accessor_view(root, weights1_acc, buffers, buf_count, &views->weights1);
    views->has_indices = gltf_get_accessor_view(root, idx_acc, buffers, buf_count, &views->idx);

    if (!gltf_get_accessor_view(root, pos_acc, buffers, buf_count, &views->pos) ||
        views->pos.count == 0 || !gltf_accessor_has_payload(&views->pos) ||
        !gltf_accessor_is_f32_components(&views->pos, 3, 3)) {
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    if (views->has_normals &&
        (!gltf_accessor_has_payload(&views->norm) ||
         !gltf_accessor_is_f32_components(&views->norm, 3, 3) ||
         views->norm.count < views->pos.count))
        views->has_normals = 0;
    if (views->has_uv0 &&
        (!gltf_accessor_has_payload(&views->uv0) || !gltf_accessor_is_texcoord(&views->uv0) ||
         views->uv0.count < views->pos.count))
        views->has_uv0 = 0;
    if (views->has_uv1 &&
        (!gltf_accessor_has_payload(&views->uv1) || !gltf_accessor_is_texcoord(&views->uv1) ||
         views->uv1.count < views->pos.count))
        views->has_uv1 = 0;
    if (views->has_colors &&
        (!gltf_accessor_has_payload(&views->color) || !gltf_accessor_is_color(&views->color) ||
         views->color.count < views->pos.count))
        views->has_colors = 0;
    if (views->has_tangents &&
        (!gltf_accessor_has_payload(&views->tangent) ||
         !gltf_accessor_is_f32_components(&views->tangent, 4, 4) ||
         views->tangent.count < views->pos.count))
        views->has_tangents = 0;
    if (views->has_indices &&
        (!gltf_accessor_has_payload(&views->idx) || !gltf_accessor_is_indices(&views->idx))) {
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    if (!gltf_mesh_validate_skin_views(views, views->pos.count)) {
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }

    draw_count = views->has_indices ? views->idx.count : views->pos.count;
    views->triangle_estimate = gltf_primitive_triangle_count(mode, draw_count);
    if (views->triangle_estimate <= 0)
        return 0;
    views->has_skin0 = views->has_joints && views->has_weights;
    views->has_skin1 = views->has_joints1 && views->has_weights1;
    return 1;
}

typedef struct {
    float pos[3];
    float nrm[3];
    float uv[2];
    float uv1[2];
    float color[4];
    float tangent[4];
    float weights[4];
    uint32_t joints[4];
    float weights1[4];
    uint32_t joints1[4];
} gltf_mesh_import_vertex_t;

static void gltf_mesh_import_vertex_init(gltf_mesh_import_vertex_t *vertex) {
    static const gltf_mesh_import_vertex_t zero_vertex = {0};
    *vertex = zero_vertex;
    vertex->color[0] = 1.0f;
    vertex->color[1] = 1.0f;
    vertex->color[2] = 1.0f;
    vertex->color[3] = 1.0f;
    vertex->tangent[3] = 1.0f;
}

static int gltf_mesh_import_vertex_is_finite(const gltf_mesh_primitive_views_t *views,
                                             const gltf_mesh_import_vertex_t *vertex) {
    return gltf_f32_array_is_finite(vertex->pos, 3) &&
           gltf_f32_array_is_finite(vertex->nrm, 3) &&
           gltf_f32_array_is_finite(vertex->uv, 2) &&
           gltf_f32_array_is_finite(vertex->uv1, 2) &&
           gltf_f32_array_is_finite(vertex->color, 4) &&
           gltf_f32_array_is_finite(vertex->tangent, 4) &&
           (!views->has_skin0 || gltf_f32_array_is_finite(vertex->weights, 4)) &&
           (!views->has_skin1 || gltf_f32_array_is_finite(vertex->weights1, 4));
}

static void gltf_mesh_merge_extra_joint_influences(gltf_mesh_import_vertex_t *vertex,
                                                   int has_primary_skin) {
    uint32_t merged_joints[4] = {0u, 0u, 0u, 0u};
    float merged_weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (has_primary_skin) {
        for (int j = 0; j < 4; j++)
            gltf_add_top_joint_influence(
                vertex->joints[j], vertex->weights[j], merged_joints, merged_weights);
    }
    for (int j = 0; j < 4; j++)
        gltf_add_top_joint_influence(
            vertex->joints1[j], vertex->weights1[j], merged_joints, merged_weights);
    gltf_normalize_joint_influences(merged_weights);
    memcpy(vertex->joints, merged_joints, sizeof(vertex->joints));
    memcpy(vertex->weights, merged_weights, sizeof(vertex->weights));
}

static void gltf_mesh_clip_joint_influences(gltf_mesh_import_vertex_t *vertex) {
    for (int j = 0; j < 4; j++) {
        if (vertex->joints[j] >= VGFX3D_MAX_BONES) {
            vertex->joints[j] = 0u;
            vertex->weights[j] = 0.0f;
        }
    }
}

static int gltf_mesh_read_import_vertex(const gltf_mesh_primitive_views_t *views,
                                        int32_t vertex_index,
                                        gltf_mesh_import_vertex_t *vertex,
                                        int *invalid_authored_normals_io,
                                        int *invalid_authored_tangents_io) {
    gltf_mesh_import_vertex_init(vertex);
    gltf_accessor_read_f32(&views->pos, vertex_index, vertex->pos, 3);
    if (views->has_normals)
        gltf_accessor_read_f32(&views->norm, vertex_index, vertex->nrm, 3);
    if (views->has_uv0)
        gltf_accessor_read_f32(&views->uv0, vertex_index, vertex->uv, 2);
    if (views->has_uv1)
        gltf_accessor_read_f32(&views->uv1, vertex_index, vertex->uv1, 2);
    else {
        vertex->uv1[0] = vertex->uv[0];
        vertex->uv1[1] = vertex->uv[1];
    }
    if (views->has_colors) {
        gltf_accessor_read_f32(&views->color, vertex_index, vertex->color, 4);
        if (views->color.comp_count < 4)
            vertex->color[3] = 1.0f;
    }
    if (views->has_tangents) {
        gltf_accessor_read_f32(&views->tangent, vertex_index, vertex->tangent, 4);
        if (views->tangent.comp_count < 4)
            vertex->tangent[3] = 1.0f;
    }
    if (views->has_joints)
        gltf_accessor_read_u32(&views->joints, vertex_index, vertex->joints, 4);
    if (views->has_weights)
        gltf_accessor_read_f32(&views->weights, vertex_index, vertex->weights, 4);
    if (views->has_joints1)
        gltf_accessor_read_u32(&views->joints1, vertex_index, vertex->joints1, 4);
    if (views->has_weights1)
        gltf_accessor_read_f32(&views->weights1, vertex_index, vertex->weights1, 4);
    if (!gltf_mesh_import_vertex_is_finite(views, vertex))
        return 0;
    if (views->has_normals && !gltf_normalize_vec3f_in_place(vertex->nrm)) {
        if (invalid_authored_normals_io)
            *invalid_authored_normals_io = 1;
        vertex->nrm[0] = 0.0f;
        vertex->nrm[1] = 0.0f;
        vertex->nrm[2] = 0.0f;
    }
    if (views->has_tangents && !gltf_sanitize_tangent4(vertex->tangent)) {
        if (invalid_authored_tangents_io)
            *invalid_authored_tangents_io = 1;
        vertex->tangent[0] = 1.0f;
        vertex->tangent[1] = 0.0f;
        vertex->tangent[2] = 0.0f;
        vertex->tangent[3] = 1.0f;
    }
    if (views->has_skin1)
        gltf_mesh_merge_extra_joint_influences(vertex, views->has_skin0);
    else if (views->has_skin0) {
        gltf_mesh_clip_joint_influences(vertex);
        gltf_normalize_joint_influences(vertex->weights);
    }
    return 1;
}

static void gltf_mesh_apply_vertex_skin(rt_mesh3d *mesh,
                                        int32_t vertex_index,
                                        const gltf_mesh_import_vertex_t *vertex) {
    rt_mesh3d_set_bone_weights(mesh,
                               vertex_index,
                               (int64_t)vertex->joints[0],
                               vertex->weights[0],
                               (int64_t)vertex->joints[1],
                               vertex->weights[1],
                               (int64_t)vertex->joints[2],
                               vertex->weights[2],
                               (int64_t)vertex->joints[3],
                               vertex->weights[3]);
    for (int j = 0; j < 4; j++) {
        if (vertex->weights[j] > 0.0001f && vertex->joints[j] < VGFX3D_MAX_BONES &&
            (int32_t)(vertex->joints[j] + 1u) > mesh->bone_count) {
            mesh->bone_count = (int32_t)(vertex->joints[j] + 1u);
        }
    }
}

static int gltf_mesh_import_vertices(rt_mesh3d *mesh,
                                     const gltf_mesh_primitive_views_t *views,
                                     int *invalid_authored_normals_io,
                                     int *invalid_authored_tangents_io) {
    for (int32_t vi = 0; vi < views->pos.count; vi++) {
        gltf_mesh_import_vertex_t vertex;
        if (!gltf_mesh_read_import_vertex(
                views, vi, &vertex, invalid_authored_normals_io, invalid_authored_tangents_io))
            return 0;
        if (!gltf_mesh_append_import_vertex(
                mesh, vertex.pos, vertex.nrm, vertex.uv, vertex.uv1, vertex.color, vertex.tangent))
            return 0;
        if (views->has_skin0 || views->has_skin1)
            gltf_mesh_apply_vertex_skin(mesh, vi, &vertex);
    }
    return 1;
}

static int gltf_mesh_finalize_imported_primitive(rt_gltf_asset *asset,
                                                 void *root,
                                                 gltf_buffer_t *buffers,
                                                 int32_t buf_count,
                                                 void *mesh_json,
                                                 void *prim,
                                                 void *mesh,
                                                 int64_t mode,
                                                 const gltf_mesh_primitive_views_t *views,
                                                 int64_t material_idx,
                                                 void *primitive_material,
                                                 int invalid_authored_normals,
                                                 int invalid_authored_tangents,
                                                 int *load_failed_io) {
    if (!gltf_append_primitive_indices(
            mesh, mode, views->has_indices ? &views->idx : NULL, views->pos.count)) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
    if ((!views->has_normals || invalid_authored_normals) &&
        ((rt_mesh3d *)mesh)->vertex_count > 0)
        rt_mesh3d_recalc_normals(mesh);
    if ((!views->has_tangents || invalid_authored_tangents) && views->has_uv0 &&
        material_idx >= 0 && material_idx < asset->material_count) {
        rt_material3d *tangent_material = (rt_material3d *)primitive_material;
        if (tangent_material && tangent_material->normal_map)
            rt_mesh3d_calc_tangents(mesh);
    }
    gltf_import_primitive_morph_targets(
        root, buffers, buf_count, mesh_json, prim, mesh, views->pos.count);
    return 1;
}

typedef struct {
    rt_gltf_asset *asset;
    void *root;
    gltf_buffer_t *buffers;
    int buf_count;
    rt_gltf_preload_bundle *preload_bundle;
    int material_capacity;
    void **primitive_materials;
    void **default_material_io;
    int *load_failed_io;
} gltf_mesh_load_context_t;

/// @brief First of two passes over the glTF mesh array: sum every mesh's primitive count so the
/// output tables (mesh_prim_start/_count, primitive_materials) can be allocated once at the exact
/// total size. This deliberate pre-count trades a cheap O(meshes) walk of jarr_len() for avoiding
/// per-primitive reallocs and fragmentation during the far more expensive decode pass — the count
/// cost is negligible relative to vertex/index decoding, so the two-pass structure is intentional.
static int gltf_mesh_count_total_primitives(void *meshes_arr,
                                            int mesh_json_count,
                                            int *out_total_prims,
                                            int *load_failed_io) {
    int total_prims = 0;
    for (int i = 0; i < mesh_json_count; i++) {
        void *mesh_json = rt_seq_get(meshes_arr, (int64_t)i);
        void *prims = jarr(mesh_json, "primitives");
        int64_t prim_len = jarr_len(prims);
        if (prim_len > INT32_MAX || total_prims > INT32_MAX - prim_len) {
            if (load_failed_io)
                *load_failed_io = 1;
            return 0;
        }
        total_prims += (int)prim_len;
    }
    *out_total_prims = total_prims;
    return 1;
}

static int gltf_mesh_alloc_output_tables(rt_gltf_asset *asset,
                                         int mesh_json_count,
                                         int total_prims,
                                         int **out_mesh_prim_start,
                                         int **out_mesh_prim_count,
                                         void ***out_primitive_materials,
                                         int *load_failed_io) {
    int *mesh_prim_start = (int *)calloc((size_t)mesh_json_count, sizeof(int));
    int *mesh_prim_count = (int *)calloc((size_t)mesh_json_count, sizeof(int));
    void **primitive_materials = (void **)calloc((size_t)total_prims, sizeof(void *));
    asset->meshes = (void **)calloc((size_t)total_prims, sizeof(void *));
    asset->mesh_capacity = asset->meshes ? total_prims : 0;
    if (!mesh_prim_start || !mesh_prim_count || !primitive_materials || !asset->meshes) {
        free(mesh_prim_start);
        free(mesh_prim_count);
        free(primitive_materials);
        free(asset->meshes);
        asset->meshes = NULL;
        asset->mesh_capacity = 0;
        if (load_failed_io)
            *load_failed_io = 1;
        return 0;
    }
    *out_mesh_prim_start = mesh_prim_start;
    *out_mesh_prim_count = mesh_prim_count;
    *out_primitive_materials = primitive_materials;
    return 1;
}

static void gltf_mesh_import_primitive(gltf_mesh_load_context_t *ctx,
                                       void *mesh_json,
                                       void *prim,
                                       int mesh_json_index,
                                       int primitive_index,
                                       int *mesh_idx_io) {
    void *attrs = jget(prim, "attributes");
    int64_t material_idx = jint(prim, "material", -1);
    int64_t mode = jint(prim, "mode", 4);
    void *prim_material = NULL;
    gltf_mesh_primitive_views_t views;
    void *mesh;
    int primitive_failed = 0;
    int invalid_authored_normals = 0;
    int invalid_authored_tangents = 0;
    if (!attrs)
        return;
    prim_material = gltf_mesh_resolve_primitive_material(
        ctx->asset, material_idx, ctx->material_capacity, ctx->default_material_io);
    if (gltf_mesh_take_preloaded_primitive(ctx->asset,
                                           ctx->preload_bundle,
                                           mesh_json_index,
                                           primitive_index,
                                           prim_material,
                                           ctx->primitive_materials,
                                           mesh_idx_io))
        return;
    if (!gltf_mesh_read_primitive_views(ctx->root,
                                        ctx->buffers,
                                        ctx->buf_count,
                                        attrs,
                                        prim,
                                        mode,
                                        &views,
                                        ctx->load_failed_io))
        return;

    mesh = rt_mesh3d_new();
    if (!mesh)
        return;
    rt_mesh3d_begin_geometry_batch((rt_mesh3d *)mesh);
    rt_mesh3d_reserve(mesh, views.pos.count, views.triangle_estimate);
    if (((rt_mesh3d *)mesh)->build_failed) {
        gltf_release_local(mesh);
        if (ctx->load_failed_io)
            *ctx->load_failed_io = 1;
        return;
    }

    primitive_failed = !gltf_mesh_import_vertices((rt_mesh3d *)mesh,
                                                  &views,
                                                  &invalid_authored_normals,
                                                  &invalid_authored_tangents);
    if (primitive_failed) {
        rt_mesh3d_end_geometry_batch((rt_mesh3d *)mesh);
        gltf_release_local(mesh);
        if (ctx->load_failed_io)
            *ctx->load_failed_io = 1;
        return;
    }

    if (!gltf_mesh_finalize_imported_primitive(ctx->asset,
                                               ctx->root,
                                               ctx->buffers,
                                               ctx->buf_count,
                                               mesh_json,
                                               prim,
                                               mesh,
                                               mode,
                                               &views,
                                               material_idx,
                                               prim_material,
                                               invalid_authored_normals,
                                               invalid_authored_tangents,
                                               ctx->load_failed_io)) {
        gltf_release_local(mesh);
        return;
    }

    ctx->asset->meshes[*mesh_idx_io] = mesh;
    (*mesh_idx_io)++;
    ctx->asset->mesh_count = *mesh_idx_io;
    if (ctx->primitive_materials)
        ctx->primitive_materials[*mesh_idx_io - 1] = prim_material;
}

static void gltf_mesh_import_mesh_primitives(gltf_mesh_load_context_t *ctx,
                                             void *meshes_arr,
                                             int mesh_json_index,
                                             int *mesh_prim_start,
                                             int *mesh_prim_count,
                                             int *mesh_idx_io) {
    void *mesh_json = rt_seq_get(meshes_arr, (int64_t)mesh_json_index);
    void *prims = jarr(mesh_json, "primitives");
    int prim_count = (int)jarr_len(prims);
    int mesh_start = *mesh_idx_io;
    if (mesh_prim_start)
        mesh_prim_start[mesh_json_index] = mesh_start;
    if (mesh_prim_count)
        mesh_prim_count[mesh_json_index] = 0;
    for (int pi = 0; pi < prim_count; pi++)
        gltf_mesh_import_primitive(ctx,
                                   mesh_json,
                                   rt_seq_get(prims, (int64_t)pi),
                                   mesh_json_index,
                                   pi,
                                   mesh_idx_io);
    if (mesh_prim_count)
        mesh_prim_count[mesh_json_index] = *mesh_idx_io - mesh_start;
}

/// @brief Decode glTF meshes/primitives into Mesh3D objects on the asset.
///        Extracted post-asset phase of rt_gltf_load_impl. Malformed primitives set
///        load_failed_io so corrupt assets do not render as partial/empty scenes.
/// @note out_mesh_prim_start/out_mesh_prim_count map a glTF mesh index to its
///       owning asset->meshes range; out_primitive_materials parallels
///       asset->meshes. All three are heap arrays owned by the caller's cleanup.
static void gltf_load_meshes(rt_gltf_asset *asset,
                             void *root,
                             gltf_buffer_t *buffers,
                             int buf_count,
                             rt_gltf_preload_bundle *preload_bundle,
                             int **out_mesh_prim_start,
                             int **out_mesh_prim_count,
                             void ***out_primitive_materials,
                             int *load_failed_io) {
    int *mesh_prim_start = NULL;
    int *mesh_prim_count = NULL;
    void **primitive_materials = NULL;
    void *default_material = NULL;
    int64_t gltf_material_count_raw = jarr_len(jarr(root, "materials"));
    int gltf_material_count =
        gltf_material_count_raw > INT32_MAX ? INT32_MAX : (int)gltf_material_count_raw;
    int material_capacity = 1;
    // Extract meshes
    void *meshes_arr = jarr(root, "meshes");
    int64_t mesh_json_count_raw = jarr_len(meshes_arr);
    int mesh_json_count = mesh_json_count_raw > INT32_MAX ? INT32_MAX : (int)mesh_json_count_raw;
    if (gltf_material_count_raw > INT32_MAX - 1 || mesh_json_count_raw > INT32_MAX) {
        if (load_failed_io)
            *load_failed_io = 1;
        goto finish;
    }
    material_capacity = gltf_material_count > 0 ? gltf_material_count + 1 : 1;
    if (mesh_json_count > 0) {
        int total_prims = 0;
        if (!gltf_mesh_count_total_primitives(
                meshes_arr, mesh_json_count, &total_prims, load_failed_io))
            goto finish;

        if (total_prims > 0) {
            gltf_mesh_load_context_t ctx;
            if (!gltf_mesh_alloc_output_tables(asset,
                                               mesh_json_count,
                                               total_prims,
                                               &mesh_prim_start,
                                               &mesh_prim_count,
                                               &primitive_materials,
                                               load_failed_io))
                goto finish;
            ctx.asset = asset;
            ctx.root = root;
            ctx.buffers = buffers;
            ctx.buf_count = buf_count;
            ctx.preload_bundle = preload_bundle;
            ctx.material_capacity = material_capacity;
            ctx.primitive_materials = primitive_materials;
            ctx.default_material_io = &default_material;
            ctx.load_failed_io = load_failed_io;
            int mesh_idx = 0;
            for (int mi = 0; mi < mesh_json_count && asset->meshes; mi++) {
                gltf_mesh_import_mesh_primitives(
                    &ctx, meshes_arr, mi, mesh_prim_start, mesh_prim_count, &mesh_idx);
            }
        }
    }
finish:
    *out_mesh_prim_start = mesh_prim_start;
    *out_mesh_prim_count = mesh_prim_count;
    *out_primitive_materials = primitive_materials;
}
