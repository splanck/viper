//===----------------------------------------------------------------------===//
// Part of the Zanna project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_fbx_loader_anim.inc  (#included from rt_fbx_loader.c; not
// a standalone translation unit) Purpose: FBX animation extraction
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Animation extraction
 *=========================================================================*/

#define FBX_TIME_SECOND 46186158000LL

/// @brief Find the direct child of the top-level Objects node whose first property matches @p id.
/// @details FBX assigns every scene object a unique 64-bit ID stored as the first property on
///          the node.  This linear scan is used for per-object resolution; call sites are bounded
///          by the number of clusters / connections, not the total object count.
/// @return Pointer into the @p objects children array, or NULL if no child carries that ID.
static fbx_node_t *fbx_find_object_by_id(fbx_node_t *objects, int64_t id) {
    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        if (obj->prop_count >= 1 && fbx_prop_i64(obj, 0) == id)
            return obj;
    }
    return NULL;
}

/// @brief Locate a child node named @p child_name and return its first property as an int64 array.
/// @details Expects the property type byte to be `'l'` (FBX long-array).  Writes the element
///          count to *count and returns a pointer into the already-decoded in-memory array.
///          Returns NULL and leaves *count unchanged on a missing child or wrong property type.
static const int64_t *fbx_get_i64_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'l' && p->v.array.data) {
        *count = p->v.array.count;
        return (const int64_t *)p->v.array.data;
    }
    return NULL;
}

/// @brief Locate a child node named @p child_name and return its first property as an int32 array.
/// @details Expects property type `'i'` (FBX int-array).  Used to read per-vertex indices such
///          as cluster Indexes.  Returns NULL and leaves *count unchanged on type mismatch.
static const int32_t *fbx_get_i32_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'i' && p->v.array.data) {
        *count = p->v.array.count;
        return (const int32_t *)p->v.array.data;
    }
    return NULL;
}

/// @brief Locate a child node named @p child_name and return its first property as a double array.
/// @details Expects property type `'d'` (FBX double-array). When the property is type `'f'`
///          (float-array), writes *count but returns NULL — callers should fall back to
///          fbx_get_f32_array in that case. Used for animation curve values and blend weights.
static const double *fbx_get_f64_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'd' && p->v.array.data) {
        *count = p->v.array.count;
        return (const double *)p->v.array.data;
    }
    /* Also handle float arrays by reading as float */
    if (p->type == 'f' && p->v.array.data) {
        *count = p->v.array.count;
        return NULL; /* caller must handle float arrays separately */
    }
    return NULL;
}

/// @brief Convenience: locate `node->child_name` and return its float-array data + count.
/// Returns NULL on missing child or non-float-array property.
static const float *fbx_get_f32_array(fbx_node_t *node, const char *child_name, uint32_t *count) {
    fbx_node_t *child = fbx_find_child(node, child_name);
    if (!child || child->prop_count < 1)
        return NULL;
    fbx_prop_t *p = &child->props[0];
    if (p->type == 'f' && p->v.array.data) {
        *count = p->v.array.count;
        return (const float *)p->v.array.data;
    }
    return NULL;
}

/// @brief Resolve an FBX Model node ID to its engine bone index in @p skeleton.
/// @details Looks up the Model node by @p model_id, decodes its display name (stripping the
///          FBX `"Name\x00\x01Type"` suffix), then asks the engine skeleton for the matching
///          bone index via `rt_skeleton3d_find_bone`.
/// @return Engine bone index in [0, bone_count), or -1 if the model is not found or not a bone.
static int32_t fbx_find_bone_index_for_model(fbx_node_t *objects,
                                             void *skeleton,
                                             const fbx_bone_binding_t *bone_bindings,
                                             int32_t bone_binding_count,
                                             int64_t model_id) {
    fbx_node_t *model_node;
    char decoded_name[64];
    int64_t bone_index;
    if (!objects || !skeleton || model_id == 0)
        return -1;
    if (bone_bindings && bone_binding_count > 0) {
        for (int32_t i = 0; i < bone_binding_count; i++) {
            if (bone_bindings[i].model_id == model_id && bone_bindings[i].bone_index >= 0)
                return bone_bindings[i].bone_index;
        }
    }
    model_node = fbx_find_object_by_id(objects, model_id);
    if (!model_node || strcmp(model_node->name, "Model") != 0 || model_node->prop_count < 2)
        return -1;
    fbx_decode_object_name(fbx_prop_str(model_node, 1), decoded_name, sizeof(decoded_name));
    if (decoded_name[0] == '\0')
        return -1;
    {
        rt_string name = rt_const_cstr(decoded_name);
        bone_index = rt_skeleton3d_find_bone(skeleton, name);
        rt_string_unref(name);
    }
    if (bone_index < 0 || bone_index > INT32_MAX)
        return -1;
    return (int32_t)bone_index;
}

/// @brief Find the Model node that owns the given Cluster deformer in the FBX connection table.
/// @details A Cluster (sub-deformer) is connected child→parent to the Model node representing
///          the bone it drives. This function walks the connection table for an entry whose
///          child_id is @p cluster_id and whose parent is a "Model" node.
/// @return FBX object ID of the owning Model, or 0 if not found.
static int64_t fbx_find_cluster_bone_model(fbx_node_t *objects,
                                           const fbx_conn_table_t *ct,
                                           int64_t cluster_id) {
    if (!objects || !ct)
        return 0;
    for (int32_t i = 0; i < ct->count; i++) {
        fbx_node_t *parent;
        if (ct->entries[i].child_id != cluster_id)
            continue;
        parent = fbx_find_object_by_id(objects, ct->entries[i].parent_id);
        if (parent && strcmp(parent->name, "Model") == 0)
            return ct->entries[i].parent_id;
    }
    return 0;
}

/// @brief Walk the FBX connection table to find the geometry that owns a given skin.
/// @details In FBX, a Deformer (skin) is connected OO (object→object) upward to a
///          Geometry (Mesh) node. This scan finds the first matching parent of type
///          `Geometry` with subtype `"Mesh"` — that's the mesh the skin drives.
/// @return Geometry object ID, or 0 if not found / not a mesh skin.
static int64_t fbx_find_skin_geometry(fbx_node_t *objects,
                                      const fbx_conn_table_t *ct,
                                      int64_t skin_id) {
    if (!objects || !ct)
        return 0;
    for (int32_t i = 0; i < ct->count; i++) {
        fbx_node_t *parent;
        if (ct->entries[i].child_id != skin_id)
            continue;
        parent = fbx_find_object_by_id(objects, ct->entries[i].parent_id);
        if (parent && strcmp(parent->name, "Geometry") == 0 && parent->prop_count >= 3 &&
            strcmp(fbx_prop_str(parent, 2), "Mesh") == 0)
            return ct->entries[i].parent_id;
    }
    return 0;
}

/// @brief Insert a (bone, weight) influence into a 4-slot fixed-size influence record,
///        keeping the four largest weights.
/// @details GPU skinning palettes are capped at 4 influences per vertex. FBX clusters
///          can contribute an arbitrary number of (bone, weight) pairs per vertex, so
///          the loader must reduce them to the top four. Strategy:
///            1. If `bone_index` is already present, fold the weight in (handles
///               duplicate cluster entries that target the same bone/vertex pair).
///            2. Else, if any slot is empty (weight <= 0), fill it.
///            3. Else replace the weakest slot only when the new weight is strictly
///               greater — this drops the smallest contribution and keeps the four
///               most significant.
///          Weights are not renormalized here; that happens in
///          `fbx_apply_control_skin_to_vertex` after all clusters are accumulated.
/// @param dst Destination influence (mutated in place).
/// @param bone_index Engine bone index to add.
/// @param weight Non-negative weight; values <= 0 are silently dropped.
static void fbx_skin_add_influence(fbx_skin_influence_t *dst, int32_t bone_index, double weight) {
    int32_t weakest = 0;
    if (!dst || bone_index < 0 || !isfinite(weight) || weight <= 0.0)
        return;
    if (weight > FBX_SKIN_WEIGHT_MAX)
        weight = FBX_SKIN_WEIGHT_MAX;
    for (int i = 0; i < 4; i++) {
        if (dst->weights[i] > 0.0 && dst->bone_indices[i] == bone_index) {
            dst->weights[i] += weight;
            if (!isfinite(dst->weights[i]) || dst->weights[i] > FBX_SKIN_WEIGHT_MAX)
                dst->weights[i] = FBX_SKIN_WEIGHT_MAX;
            return;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (dst->weights[i] <= 0.0) {
            dst->bone_indices[i] = bone_index;
            dst->weights[i] = weight;
            return;
        }
        if (dst->weights[i] < dst->weights[weakest])
            weakest = i;
    }
    if (weight > dst->weights[weakest]) {
        dst->bone_indices[weakest] = bone_index;
        dst->weights[weakest] = weight;
    }
}

/// @brief Finalize and write a per-vertex influence record to the mesh's skinning data.
/// @details Copies up to 4 (bone, weight) pairs from `influence`, filters out empty
///          slots (weight <= 0), renormalizes the surviving weights to sum to 1, and
///          commits them via `rt_mesh3d_set_bone_weights`. Updates `mesh->bone_count`
///          as a running high-water mark so the skinning palette is sized correctly
///          for the shader upload. No-op if every influence slot is empty (the vertex
///          has no skin binding and should use the bind pose).
static void fbx_apply_control_skin_to_vertex(rt_mesh3d *mesh,
                                             int32_t vertex_index,
                                             const fbx_skin_influence_t *influence) {
    double weights[4];
    double sum = 0.0;
    int64_t bones[4];
    if (!mesh || !influence || vertex_index < 0 || vertex_index >= (int32_t)mesh->vertex_count)
        return;
    for (int i = 0; i < 4; i++) {
        bones[i] = influence->weights[i] > 0.0 && influence->bone_indices[i] >= 0
                       ? influence->bone_indices[i]
                       : 0;
        weights[i] = isfinite(influence->weights[i]) && influence->weights[i] > 0.0 &&
                             influence->bone_indices[i] >= 0
                         ? influence->weights[i]
                         : 0.0;
        if (weights[i] > FBX_SKIN_WEIGHT_MAX)
            weights[i] = FBX_SKIN_WEIGHT_MAX;
        sum += weights[i];
    }
    if (!isfinite(sum) || sum <= 0.0) {
        memset(mesh->vertices[vertex_index].bone_indices,
               0,
               sizeof(mesh->vertices[vertex_index].bone_indices));
        memset(mesh->vertices[vertex_index].bone_weights,
               0,
               sizeof(mesh->vertices[vertex_index].bone_weights));
        return;
    }
    for (int i = 0; i < 4; i++) {
        weights[i] /= sum;
        if (weights[i] > 0.0 && bones[i] + 1 > mesh->bone_count)
            mesh->bone_count = (int32_t)(bones[i] + 1);
    }
    rt_mesh3d_set_bone_weights(mesh,
                               vertex_index,
                               bones[0],
                               weights[0],
                               bones[1],
                               weights[1],
                               bones[2],
                               weights[2],
                               bones[3],
                               weights[3]);
}

/// @brief Stamp a fully-accumulated control-point influence table onto every vertex
///        of the mesh, respecting the control-point → vertex fan-out recorded in
///        `remap`.
/// @details FBX stores skinning at the "control point" granularity (the canonical
///          unique vertex positions), not the triangulated / duplicated vertices the
///          GPU consumes. When a control point maps to multiple mesh vertices (common
///          for hard edges where positions share but normals diverge), each of those
///          duplicate vertices must receive the same influences. The remap's
///          `control_vertices[ci]` is a flat list of mesh vertex indices spawned by
///          control point `ci` — we expand that fan-out here. If no remap exists we
///          assume 1:1 (no deduplication happened during load).
static void fbx_apply_skin_to_mesh(void *mesh_obj,
                                   const fbx_mesh_remap_t *remap,
                                   const fbx_skin_influence_t *controls,
                                   int32_t control_count) {
    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    if (!mesh || !controls || control_count <= 0)
        return;
    for (int32_t ci = 0; ci < control_count; ci++) {
        if (remap && remap->control_vertices && ci < remap->control_count) {
            const fbx_vertex_index_list_t *list = &remap->control_vertices[ci];
            for (int32_t vi = 0; vi < list->count; vi++)
                fbx_apply_control_skin_to_vertex(mesh, list->vertices[vi], &controls[ci]);
        } else {
            fbx_apply_control_skin_to_vertex(mesh, ci, &controls[ci]);
        }
    }
}

/// @brief Apply all FBX Skin deformers to their target meshes, populating bone weights.
/// @details Iterates every Skin Deformer in the Objects section. For each skin it resolves
///          the target geometry (via `fbx_find_skin_geometry`), finds the engine mesh object
///          (via @p mesh_bindings), allocates a per-control-point influence accumulator, then
///          iterates all connected Cluster sub-deformers to accumulate (bone, weight) pairs.
///          Finalization (renormalization, write to mesh) is done by `fbx_apply_skin_to_mesh`.
///          Float and double weight arrays are both handled; the remap table is used when the
///          mesh was deduplicated to map control-point indices back to the original FBX layout.
static void fbx_apply_skinning(fbx_node_t *objects,
                               const fbx_conn_table_t *ct,
                               void *skeleton,
                               const fbx_bone_binding_t *bone_bindings,
                               int32_t bone_binding_count,
                               const fbx_mesh_binding_t *mesh_bindings,
                               int32_t mesh_binding_count,
                               const fbx_mesh_remap_t *mesh_remaps,
                               int32_t mesh_remap_count) {
    if (!objects || !ct || !skeleton || !mesh_bindings || mesh_binding_count <= 0)
        return;

    for (int32_t oi = 0; oi < objects->child_count; oi++) {
        fbx_node_t *skin = &objects->children[oi];
        int64_t skin_id;
        int64_t geometry_id;
        void *mesh_obj = NULL;
        const fbx_mesh_remap_t *remap;
        fbx_skin_influence_t *controls;
        int32_t control_count;

        if (strcmp(skin->name, "Deformer") != 0 || skin->prop_count < 3 ||
            strcmp(fbx_prop_str(skin, 2), "Skin") != 0)
            continue;

        skin_id = fbx_prop_i64(skin, 0);
        geometry_id = fbx_find_skin_geometry(objects, ct, skin_id);
        if (geometry_id == 0)
            continue;
        for (int32_t bi = 0; bi < mesh_binding_count; bi++) {
            if (mesh_bindings[bi].id == geometry_id) {
                mesh_obj = mesh_bindings[bi].mesh;
                break;
            }
        }
        if (!mesh_obj)
            continue;
        remap = fbx_find_mesh_remap(mesh_remaps, mesh_remap_count, geometry_id);
        if (!remap && ((rt_mesh3d *)mesh_obj)->vertex_count > (uint32_t)INT32_MAX)
            continue;
        control_count =
            remap ? remap->control_count : (int32_t)((rt_mesh3d *)mesh_obj)->vertex_count;
        if (control_count <= 0)
            continue;
        if (!rt_untrusted_count_ok(
                (int64_t)control_count, 3u * sizeof(double), FBX_MAX_DECOMPRESSED_ARRAY_BYTES))
            continue;
        if ((size_t)control_count > SIZE_MAX / sizeof(*controls))
            continue;
        controls = (fbx_skin_influence_t *)calloc((size_t)control_count, sizeof(*controls));
        if (!controls)
            continue;

        for (int32_t ci = 0; ci < ct->count; ci++) {
            fbx_node_t *cluster;
            int32_t bone_index;
            int64_t bone_model_id;
            uint32_t index_count = 0;
            uint32_t weight_count = 0;
            const int32_t *indices;
            const double *weights64;
            const float *weights32 = NULL;

            if (ct->entries[ci].parent_id != skin_id)
                continue;
            cluster = fbx_find_object_by_id(objects, ct->entries[ci].child_id);
            if (!cluster || strcmp(cluster->name, "Deformer") != 0 || cluster->prop_count < 3 ||
                strcmp(fbx_prop_str(cluster, 2), "Cluster") != 0)
                continue;
            bone_model_id = fbx_find_cluster_bone_model(objects, ct, fbx_prop_i64(cluster, 0));
            bone_index = fbx_find_bone_index_for_model(
                objects, skeleton, bone_bindings, bone_binding_count, bone_model_id);
            if (bone_index < 0)
                continue;

            indices = fbx_get_i32_array(cluster, "Indexes", &index_count);
            weights64 = fbx_get_f64_array(cluster, "Weights", &weight_count);
            if (!weights64)
                weights32 = fbx_get_f32_array(cluster, "Weights", &weight_count);
            if (!indices || (!weights64 && !weights32))
                continue;
            if (weight_count < index_count)
                index_count = weight_count;
            for (uint32_t wi = 0; wi < index_count; wi++) {
                int32_t control_index = indices[wi];
                double weight = weights64 ? weights64[wi] : (double)weights32[wi];
                if (control_index >= 0 && control_index < control_count)
                    fbx_skin_add_influence(&controls[control_index], bone_index, weight);
            }
        }

        fbx_apply_skin_to_mesh(mesh_obj, remap, controls, control_count);
        free(controls);
    }
}

typedef struct {
    const int64_t *times;
    const double *values64;
    const float *values32;
    const int32_t *attr_flags;
    const double *attr_data64;
    const float *attr_data32;
    uint32_t count;
    uint32_t attr_flag_count;
    uint32_t attr_data_count;
} fbx_anim_curve_view_t;

#define FBX_ANIM_INTERP_LINEAR 0
#define FBX_ANIM_INTERP_CONSTANT 1
#define FBX_ANIM_INTERP_CUBIC 2
#define FBX_ANIM_INTERP_FLAG_CONSTANT 0x00000002
#define FBX_ANIM_INTERP_FLAG_LINEAR 0x00000004
#define FBX_ANIM_INTERP_FLAG_CUBIC 0x00000008

typedef struct {
    int8_t initialized;
    double base_translation[3];
    double base_rotation[3];
    double base_scale[3];
    fbx_anim_curve_view_t curves[3][3]; /* trs, component */
} fbx_anim_bone_builder_t;

/// @brief Extract the local-space TRS components from an FBX Model node's Properties70 block.
/// @details Scans the `Properties70` child for `"Lcl Translation"`, `"Lcl Rotation"`, and
///          `"Lcl Scaling"` P-nodes and writes their XYZ values to the caller's arrays.
///          NULL output pointers are silently skipped. Arrays are pre-zeroed (translation/rotation)
///          or set to {1,1,1} (scale) before parsing so the caller gets sane defaults when a
///          component is absent.
static void fbx_extract_model_lcl_components(fbx_node_t *model_node,
                                             double *translation,
                                             double *rotation,
                                             double *scale) {
    if (translation) {
        translation[0] = translation[1] = translation[2] = 0.0;
    }
    if (rotation) {
        rotation[0] = rotation[1] = rotation[2] = 0.0;
    }
    if (scale) {
        scale[0] = scale[1] = scale[2] = 1.0;
    }
    if (!model_node)
        return;
    fbx_node_t *p70 = fbx_find_child(model_node, "Properties70");
    if (!p70)
        return;
    for (int32_t pi = 0; pi < p70->child_count; pi++) {
        fbx_node_t *p = &p70->children[pi];
        const char *pn;
        if (strcmp(p->name, "P") != 0 || p->prop_count < 7)
            continue;
        pn = fbx_prop_str(p, 0);
        if (translation && strcmp(pn, "Lcl Translation") == 0) {
            double x = fbx_prop_f64(p, 4);
            double y = fbx_prop_f64(p, 5);
            double z = fbx_prop_f64(p, 6);
            if (fbx_sanitize_position3(&x, &y, &z)) {
                translation[0] = x;
                translation[1] = y;
                translation[2] = z;
            }
        } else if (rotation && strcmp(pn, "Lcl Rotation") == 0) {
            double x = fbx_prop_f64(p, 4);
            double y = fbx_prop_f64(p, 5);
            double z = fbx_prop_f64(p, 6);
            if (isfinite(x) && isfinite(y) && isfinite(z)) {
                rotation[0] = fbx_sanitize_rotation_degrees(x);
                rotation[1] = fbx_sanitize_rotation_degrees(y);
                rotation[2] = fbx_sanitize_rotation_degrees(z);
            }
        } else if (scale && strcmp(pn, "Lcl Scaling") == 0) {
            double x = fbx_prop_f64(p, 4);
            double y = fbx_prop_f64(p, 5);
            double z = fbx_prop_f64(p, 6);
            if (isfinite(x) && isfinite(y) && isfinite(z)) {
                scale[0] = fbx_scale_or_unit(x);
                scale[1] = fbx_scale_or_unit(y);
                scale[2] = fbx_scale_or_unit(z);
            }
        }
    }
}

/// @brief Return non-zero if @p curve contains at least one keyframe with usable value data.
static int fbx_anim_curve_has_data(const fbx_anim_curve_view_t *curve) {
    return curve && curve->times && (curve->values64 || curve->values32) && curve->count > 0 &&
           curve->count <= FBX_ANIM_CURVE_KEYS_MAX;
}

/// @brief Validate an FBX animation curve before the sampler assumes sorted finite data.
static int fbx_anim_curve_view_valid(const fbx_anim_curve_view_t *curve) {
    if (!fbx_anim_curve_has_data(curve))
        return 0;
    for (uint32_t i = 0; i < curve->count; i++) {
        double value = curve->values64 ? curve->values64[i] : (double)curve->values32[i];
        double seconds = (double)curve->times[i] / (double)FBX_TIME_SECOND;
        if (!isfinite(value) || !isfinite(seconds) || fabs(seconds) > FBX_ANIM_TIME_SECONDS_MAX)
            return 0;
        if (i > 0 && curve->times[i] <= curve->times[i - 1])
            return 0;
    }
    return 1;
}

/// @brief Case-insensitive ASCII string equality (NULL operands compare unequal).
static int fbx_ascii_streq_ignore_case(const char *a, const char *b) {
    if (!a || !b)
        return 0;
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb))
            return 0;
    }
    return *a == '\0' && *b == '\0';
}

/// @brief Map an AnimationCurve connection component label to XYZ index.
/// @details Binary and ASCII FBX commonly use `d|X`, `d|Y`, `d|Z`, but some exporters
///          write the bare aliases `X`, `Y`, `Z`; others preserve the meaning but vary
///          ASCII case. Accept these forms so valid curves do not disappear solely
///          because of exporter spelling.
static int32_t fbx_anim_component_from_connection_prop(const char *prop) {
    if (!prop)
        return -1;
    if (fbx_ascii_streq_ignore_case(prop, "d|X") || fbx_ascii_streq_ignore_case(prop, "X"))
        return 0;
    if (fbx_ascii_streq_ignore_case(prop, "d|Y") || fbx_ascii_streq_ignore_case(prop, "Y"))
        return 1;
    if (fbx_ascii_streq_ignore_case(prop, "d|Z") || fbx_ascii_streq_ignore_case(prop, "Z"))
        return 2;
    return -1;
}

/// @brief Return one animation curve key value as double.
/// @param curve Animation curve view.
/// @param key_index Key index to read.
/// @param fallback Returned when the key is out of range or non-finite.
/// @return The finite key value or @p fallback.
static double fbx_anim_curve_key_value(const fbx_anim_curve_view_t *curve,
                                       uint32_t key_index,
                                       double fallback) {
    double value;
    if (!fbx_anim_curve_has_data(curve) || key_index >= curve->count)
        return fallback;
    value = curve->values64 ? curve->values64[key_index] : (double)curve->values32[key_index];
    return isfinite(value) ? value : fallback;
}

/// @brief Classify an FBX curve segment's interpolation mode from `KeyAttrFlags`.
/// @details FBX SDK interpolation bits are commonly 0x2 constant, 0x4 linear, and 0x8 cubic.
///          Some exporters omit flags or write sparse metadata; those segments fall back to
///          linear interpolation because that is the safest representation for the runtime.
/// @param curve Animation curve view.
/// @param segment_index Segment starting key index.
/// @return One of FBX_ANIM_INTERP_*.
static int fbx_anim_curve_segment_mode(const fbx_anim_curve_view_t *curve, uint32_t segment_index) {
    int32_t flags;
    if (!curve || !curve->attr_flags || segment_index >= curve->attr_flag_count)
        return FBX_ANIM_INTERP_LINEAR;
    flags = curve->attr_flags[segment_index];
    if (flags & FBX_ANIM_INTERP_FLAG_CONSTANT)
        return FBX_ANIM_INTERP_CONSTANT;
    if (flags & FBX_ANIM_INTERP_FLAG_CUBIC)
        return FBX_ANIM_INTERP_CUBIC;
    if (flags & FBX_ANIM_INTERP_FLAG_LINEAR)
        return FBX_ANIM_INTERP_LINEAR;
    return FBX_ANIM_INTERP_LINEAR;
}

/// @brief Read tangent slope metadata from a curve's `KeyAttrDataFloat` array.
/// @details Exporters typically store four floats per key; this helper treats slot 0 as the
///          outgoing slope and slot 1 as the incoming slope. Two-float-per-key data is accepted
///          as a compact variant. Missing/non-finite data returns @p fallback_slope.
/// @param curve Animation curve view.
/// @param key_index Key whose tangent is requested.
/// @param incoming Non-zero for incoming tangent, zero for outgoing tangent.
/// @param fallback_slope Value used when no usable tangent metadata exists.
/// @return Tangent slope in value-per-second units.
static double fbx_anim_curve_tangent_slope(const fbx_anim_curve_view_t *curve,
                                           uint32_t key_index,
                                           int incoming,
                                           double fallback_slope) {
    uint32_t stride = 0;
    uint32_t slot;
    double value;
    if (!curve || key_index >= curve->count || curve->attr_data_count == 0 ||
        (!curve->attr_data64 && !curve->attr_data32))
        return fallback_slope;
    if (curve->attr_data_count >= curve->count * 4u)
        stride = 4u;
    else if (curve->attr_data_count >= curve->count * 2u)
        stride = 2u;
    if (stride == 0)
        return fallback_slope;
    slot = key_index * stride + (incoming ? 1u : 0u);
    if (slot >= curve->attr_data_count)
        return fallback_slope;
    value = curve->attr_data64 ? curve->attr_data64[slot] : (double)curve->attr_data32[slot];
    if (!isfinite(value))
        return fallback_slope;
    return fbx_clamp_abs_or(value, fallback_slope, FBX_NUMERIC_ABS_MAX);
}

/// @brief Hermite-sample one cubic FBX animation segment.
/// @details The runtime animation format is keyframe-only, but importing cubic source curves still
///          benefits from correct intermediate samples. Tangents are interpreted as value/second;
///          when missing, linear slope fallback makes this reduce to ordinary linear interpolation.
/// @param curve Animation curve view.
/// @param lo Segment start key index.
/// @param hi Segment end key index.
/// @param fbx_time Time inside the segment.
/// @param fallback Returned on malformed data.
/// @return Interpolated curve value.
static double fbx_anim_curve_cubic_value(const fbx_anim_curve_view_t *curve,
                                         uint32_t lo,
                                         uint32_t hi,
                                         int64_t fbx_time,
                                         double fallback) {
    int64_t t0 = curve->times[lo];
    int64_t t1 = curve->times[hi];
    double span_seconds;
    double u;
    double v0;
    double v1;
    double slope;
    double m0;
    double m1;
    double u2;
    double u3;
    double h00;
    double h10;
    double h01;
    double h11;
    if (t1 <= t0)
        return fallback;
    span_seconds = (double)(t1 - t0) / (double)FBX_TIME_SECOND;
    if (!isfinite(span_seconds) || span_seconds <= 0.0)
        return fallback;
    u = (double)(fbx_time - t0) / (double)(t1 - t0);
    if (!isfinite(u) || u < 0.0)
        u = 0.0;
    if (u > 1.0)
        u = 1.0;
    v0 = fbx_anim_curve_key_value(curve, lo, fallback);
    v1 = fbx_anim_curve_key_value(curve, hi, fallback);
    if (!isfinite(v0) || !isfinite(v1))
        return fallback;
    slope = (v1 - v0) / span_seconds;
    m0 = fbx_anim_curve_tangent_slope(curve, lo, 0, slope);
    m1 = fbx_anim_curve_tangent_slope(curve, hi, 1, slope);
    u2 = u * u;
    u3 = u2 * u;
    h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    h10 = u3 - 2.0 * u2 + u;
    h01 = -2.0 * u3 + 3.0 * u2;
    h11 = u3 - u2;
    return h00 * v0 + h10 * span_seconds * m0 + h01 * v1 + h11 * span_seconds * m1;
}

/// @brief Sample the animation curve at the given FBX tick time using FBX interpolation flags.
/// @details Times before the first keyframe return the first value; times after the last return
///          the last value (no extrapolation).  Constant segments hold the left key, cubic
///          segments use Hermite interpolation with `KeyAttrDataFloat` tangents when available,
///          and all other segments are linear. FBX time is in units of 1/46,186,158,000 second.
/// @param fallback Value returned when the curve has no data.
static double fbx_anim_curve_value(const fbx_anim_curve_view_t *curve,
                                   int64_t fbx_time,
                                   double fallback) {
    uint32_t lo;
    uint32_t hi;
    int64_t t0;
    int64_t t1;
    double v0;
    double v1;
    double a;
    if (!fbx_anim_curve_has_data(curve))
        return fallback;
    v0 = fbx_anim_curve_key_value(curve, 0, fallback);
    v1 = fbx_anim_curve_key_value(curve, curve->count - 1, fallback);
    if (!isfinite(v0) || !isfinite(v1))
        return fallback;
    if (fbx_time <= curve->times[0])
        return v0;
    if (fbx_time >= curve->times[curve->count - 1])
        return v1;
    lo = 0;
    hi = curve->count - 1;
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (curve->times[mid] <= fbx_time)
            lo = mid;
        else
            hi = mid;
    }
    t0 = curve->times[lo];
    t1 = curve->times[hi];
    if (t1 <= t0)
        return fallback;
    v0 = fbx_anim_curve_key_value(curve, lo, fallback);
    v1 = fbx_anim_curve_key_value(curve, hi, fallback);
    if (!isfinite(v0) || !isfinite(v1))
        return fallback;
    {
        int mode = fbx_anim_curve_segment_mode(curve, lo);
        if (mode == FBX_ANIM_INTERP_CONSTANT)
            return v0;
        if (mode == FBX_ANIM_INTERP_CUBIC)
            return fbx_anim_curve_cubic_value(curve, lo, hi, fbx_time, fallback);
    }
    a = (double)(fbx_time - t0) / (double)(t1 - t0);
    if (!isfinite(a) || a < 0.0)
        a = 0.0;
    if (a > 1.0)
        a = 1.0;
    return v0 + (v1 - v0) * a;
}

/// @brief qsort comparator for int64 keys, ascending (returns -1/0/1).
static int fbx_i64_compare(const void *a, const void *b) {
    int64_t av = *(const int64_t *)a;
    int64_t bv = *(const int64_t *)b;
    return (av > bv) - (av < bv);
}

/// @brief Append @p value to an unsorted key-time array; sorting/dedup happens once after collect.
/// @details Non-finite or extreme key times indicate a corrupt animation curve and are reported as
///          failure so callers can reject the stack instead of silently producing an empty clip.
static int fbx_anim_append_time(int64_t **times, int32_t *count, int32_t *capacity, int64_t value) {
    double seconds = (double)value / (double)FBX_TIME_SECOND;
    if (!times || !count || !capacity || *count < 0 || *capacity < 0 || *count > *capacity ||
        (*count > 0 && !*times))
        return 0;
    if (!isfinite(seconds) || fabs(seconds) > FBX_ANIM_TIME_SECONDS_MAX)
        return 0;
    if (*count >= *capacity) {
        int32_t new_capacity;
        if (*capacity > INT32_MAX / 2)
            new_capacity = *count + 1;
        else
            new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        if (new_capacity <= *capacity || (size_t)new_capacity > SIZE_MAX / sizeof(**times))
            return 0;
        int64_t *grown = (int64_t *)realloc(*times, (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        *times = grown;
        *capacity = new_capacity;
    }
    (*times)[*count] = value;
    (*count)++;
    return 1;
}

/// @brief Append all runtime-needed sample times for an FBX animation curve.
/// @details Adds authored key times unconditionally. Constant segments also add one representable
///          pre-step sample before the next key so runtime linear interpolation holds the previous
///          value until the step. Cubic segments add one-third and two-third samples, giving the
///          existing keyframe runtime enough points to approximate Hermite source curves without
///          changing Animation3D's storage model.
/// @param times Dynamic array receiving unsorted FBX tick times.
/// @param count Current array count.
/// @param capacity Current array capacity.
/// @param curve Curve whose key and intermediate sample times should be appended.
/// @return Non-zero on success.
static int fbx_anim_append_curve_sample_times(int64_t **times,
                                              int32_t *count,
                                              int32_t *capacity,
                                              const fbx_anim_curve_view_t *curve) {
    if (!fbx_anim_curve_has_data(curve))
        return 1;
    for (uint32_t k = 0; k < curve->count; k++) {
        if (!fbx_anim_append_time(times, count, capacity, curve->times[k]))
            return 0;
    }
    for (uint32_t k = 0; k + 1u < curve->count; k++) {
        int64_t t0 = curve->times[k];
        int64_t t1 = curve->times[k + 1u];
        int64_t delta = t1 - t0;
        int mode;
        if (delta <= 1)
            continue;
        mode = fbx_anim_curve_segment_mode(curve, k);
        if (mode == FBX_ANIM_INTERP_CONSTANT) {
            int64_t step_delta = FBX_TIME_SECOND / 100000LL; /* 10 microseconds */
            if (step_delta >= delta)
                step_delta = 1;
            if (t1 - step_delta > t0 &&
                !fbx_anim_append_time(times, count, capacity, t1 - step_delta))
                return 0;
        } else if (mode == FBX_ANIM_INTERP_CUBIC && delta >= 3) {
            int64_t t_a = t0 + delta / 3;
            int64_t t_b = t0 + (delta * 2) / 3;
            if (t_a > t0 && t_a < t1 && !fbx_anim_append_time(times, count, capacity, t_a))
                return 0;
            if (t_b > t0 && t_b < t1 && t_b != t_a &&
                !fbx_anim_append_time(times, count, capacity, t_b))
                return 0;
        }
    }
    return 1;
}

/// @brief Sort key times and compact duplicates in place.
static void fbx_anim_sort_unique_times(int64_t *times, int32_t *count) {
    int32_t write = 0;
    if (!times || !count || *count <= 1)
        return;
    qsort(times, (size_t)*count, sizeof(*times), fbx_i64_compare);
    for (int32_t read = 0; read < *count; ++read) {
        if (write == 0 || times[read] != times[write - 1])
            times[write++] = times[read];
    }
    *count = write;
}

/// @brief Walk `AnimationStack` / `AnimationLayer` / `AnimationCurveNode` nodes and
///        build keyframe tracks per bone.
///
/// @details FBX's animation graph is deeply indirected. This function flattens it into
///          the engine's bone-oriented `rt_animation3d` representation by traversing:
///
///            AnimationStack  (one per "clip"/"take")
///                └── AnimationLayer+  (usually one; blending layers are flattened)
///                        └── AnimationCurveNode+  (one per animated channel group)
///                                ├── connects up to a `Model` (bone) by property name
///                                │     ("Lcl Translation" / "Lcl Rotation" / "Lcl Scaling")
///                                └── AnimationCurve (X, Y, Z) via subfield connections
///                                      ("d|X" / "d|Y" / "d|Z") each with parallel
///                                      `KeyTime` (i64 FBX ticks) + `KeyValueFloat` arrays.
///
///          For each AnimationStack we:
///            1. Allocate one `fbx_anim_bone_builder_t` per skeleton bone.
///            2. For each curve found, record the (time, axis-value) samples into the
///               matching bone/TRS slot.
///            3. Unify the sample time lists across T/R/S per bone, seeded by the
///               bind-pose TRS so bones with partial curves snap to rest on the
///               missing axis.
///            4. Emit one merged keyframe per unique time, converting Euler degrees to
///               quaternions for rotation channels. Z-up assets have the translation
///               axes swapped to Y-up via `fbx_correct_zup`.
///            5. Finalize the `rt_animation3d` with the total duration and push it
///               onto the output array.
///
///          FBX time units (1/46186158000 s) are converted to seconds up-front so the
///          engine never has to know about FBX ticks downstream. Missing / unresolved
///          connections are silently skipped rather than failing the whole load.
/// @param root FBX document root.
/// @param ct Connection table (OO/OP connections already resolved).
/// @param skeleton Target skeleton whose bone count determines builder allocation.
/// @param z_up Non-zero for Z-up source assets; triggers axis correction.
/// @param out_anims Receives an array of rt_animation3d* (caller owns).
/// @param out_count Receives the count written.
/// @brief Walk a stack's AnimationLayers → CurveNodes → AnimationCurves, filling @p builders
///        (per-bone T/R/S curve views + base local transform). The deep connection walk
///        resolves each curve node to its target bone and TRS channel.
/// @note Complexity is O(connections) per nesting level (layer → curve-node → model → curve),
///        and each step also calls fbx_find_object_by_id (a linear object-tree walk). This is a
///        deliberate trade-off: FBX scenes typically carry only a few hundred connections, so
///        the one-time load cost is negligible. For very large rigs the optimization path is an
///        id→node hash plus connection indices keyed by parent_id/child_id (O(1) lookups); it is
///        intentionally not built here to keep this untrusted-input parser simple and auditable.
static void fbx_anim_collect_curves(fbx_node_t *objects,
                                    const fbx_conn_table_t *ct,
                                    void *skeleton,
                                    const fbx_bone_binding_t *bone_bindings,
                                    int32_t bone_binding_count,
                                    int64_t bone_count,
                                    int64_t stack_id,
                                    fbx_anim_bone_builder_t *builders) {
    if (!objects || !ct || !builders)
        return;
    for (int32_t li = 0; li < ct->count; li++) {
        int64_t layer_id;
        fbx_node_t *layer_node;
        if (ct->entries[li].parent_id != stack_id)
            continue;
        layer_id = ct->entries[li].child_id;
        layer_node = fbx_find_object_by_id(objects, layer_id);
        if (!layer_node || strcmp(layer_node->name, "AnimationLayer") != 0)
            continue;

        for (int32_t ci = 0; ci < ct->count; ci++) {
            int64_t curve_node_id;
            fbx_node_t *cn_node;
            if (ct->entries[ci].parent_id != layer_id)
                continue;
            curve_node_id = ct->entries[ci].child_id;
            cn_node = fbx_find_object_by_id(objects, curve_node_id);
            if (!cn_node || strcmp(cn_node->name, "AnimationCurveNode") != 0)
                continue;

            /* Find which Model (bone) this curve node connects to and what property */
            /* The CurveNode→Model connection has prop "Lcl Translation"/"Lcl Rotation"/"Lcl
             * Scaling" */
            int32_t trs_type = -1; /* 0=T, 1=R, 2=S */
            int64_t model_id = 0;
            for (int32_t ci2 = 0; ci2 < ct->count; ci2++) {
                if (ct->entries[ci2].child_id == curve_node_id) {
                    int64_t pid = ct->entries[ci2].parent_id;
                    fbx_node_t *pnode = fbx_find_object_by_id(objects, pid);
                    if (pnode && strcmp(pnode->name, "Model") == 0) {
                        model_id = pid;
                        const char *cprop = ct->entries[ci2].prop;
                        if (!cprop)
                            continue;
                        if (strcmp(cprop, "Lcl Translation") == 0)
                            trs_type = 0;
                        else if (strcmp(cprop, "Lcl Rotation") == 0)
                            trs_type = 1;
                        else if (strcmp(cprop, "Lcl Scaling") == 0)
                            trs_type = 2;
                        break;
                    }
                }
            }
            if (trs_type < 0 || model_id == 0)
                continue;

            int64_t bone_idx = fbx_find_bone_index_for_model(
                objects, skeleton, bone_bindings, bone_binding_count, model_id);
            if (bone_idx < 0 || bone_idx >= bone_count)
                continue;
            if (!builders[bone_idx].initialized) {
                fbx_node_t *model_node = fbx_find_object_by_id(objects, model_id);
                fbx_extract_model_lcl_components(model_node,
                                                 builders[bone_idx].base_translation,
                                                 builders[bone_idx].base_rotation,
                                                 builders[bone_idx].base_scale);
                builders[bone_idx].initialized = 1;
            }

            /* Find AnimationCurve children (d|X, d|Y, d|Z) */
            for (int32_t ki = 0; ki < ct->count; ki++) {
                fbx_node_t *curve;
                const char *curve_prop;
                if (ct->entries[ki].parent_id != curve_node_id)
                    continue;
                curve = fbx_find_object_by_id(objects, ct->entries[ki].child_id);
                if (!curve || strcmp(curve->name, "AnimationCurve") != 0)
                    continue;

                curve_prop = ct->entries[ki].prop;
                int comp = fbx_anim_component_from_connection_prop(curve_prop);
                if (comp < 0)
                    continue;

                uint32_t tc = 0;
                const int64_t *times = fbx_get_i64_array(curve, "KeyTime", &tc);
                if (!times || tc == 0)
                    continue;

                uint32_t vc = 0;
                const double *dvals = fbx_get_f64_array(curve, "KeyValueFloat", &vc);
                const float *fvals = NULL;
                if (!dvals)
                    fvals = fbx_get_f32_array(curve, "KeyValueFloat", &vc);
                if (!dvals && !fvals)
                    continue;
                if (tc != vc)
                    continue;

                {
                    fbx_anim_curve_view_t *view = &builders[bone_idx].curves[trs_type][comp];
                    fbx_anim_curve_view_t candidate;
                    uint32_t attr_flag_count = 0;
                    uint32_t attr_data_count = 0;
                    const int32_t *attr_flags =
                        fbx_get_i32_array(curve, "KeyAttrFlags", &attr_flag_count);
                    const double *attr_data64 =
                        fbx_get_f64_array(curve, "KeyAttrDataFloat", &attr_data_count);
                    const float *attr_data32 = NULL;
                    if (fbx_anim_curve_has_data(view))
                        continue;
                    if (!attr_data64)
                        attr_data32 =
                            fbx_get_f32_array(curve, "KeyAttrDataFloat", &attr_data_count);
                    memset(&candidate, 0, sizeof(candidate));
                    candidate.times = times;
                    candidate.values64 = dvals;
                    candidate.values32 = fvals;
                    candidate.attr_flags = attr_flags;
                    candidate.attr_data64 = attr_data64;
                    candidate.attr_data32 = attr_data32;
                    candidate.count = tc;
                    candidate.attr_flag_count = attr_flags ? attr_flag_count : 0;
                    candidate.attr_data_count = (attr_data64 || attr_data32) ? attr_data_count : 0;
                    if (fbx_anim_curve_view_valid(&candidate))
                        *view = candidate;
                }
            }
        }
    }
}

/// @brief True if the given FBX animation stack id owns at least one AnimationLayer child in
///   the connection table.
static int fbx_anim_stack_has_layer(fbx_node_t *objects,
                                    const fbx_conn_table_t *ct,
                                    int64_t stack_id) {
    if (!objects || !ct)
        return 0;
    for (int32_t i = 0; i < ct->count; i++) {
        fbx_node_t *layer_node;
        if (ct->entries[i].parent_id != stack_id)
            continue;
        layer_node = fbx_find_object_by_id(objects, ct->entries[i].child_id);
        if (layer_node && strcmp(layer_node->name, "AnimationLayer") == 0)
            return 1;
    }
    return 0;
}

/// @brief Smallest and largest FBX key ticks across all of @p builders' curves.
static int fbx_anim_compute_time_bounds(const fbx_anim_bone_builder_t *builders,
                                        int64_t bone_count,
                                        int64_t *out_min_time,
                                        int64_t *out_max_time) {
    int found = 0;
    int64_t min_time = 0;
    int64_t max_time = 0;
    if (out_min_time)
        *out_min_time = 0;
    if (out_max_time)
        *out_max_time = 0;
    for (int64_t bone_idx = 0; bone_idx < bone_count; bone_idx++) {
        if (!builders[bone_idx].initialized)
            continue;
        for (int trs = 0; trs < 3; trs++) {
            for (int comp = 0; comp < 3; comp++) {
                const fbx_anim_curve_view_t *curve = &builders[bone_idx].curves[trs][comp];
                if (!fbx_anim_curve_has_data(curve))
                    continue;
                for (uint32_t k = 0; k < curve->count; k++) {
                    double seconds = (double)curve->times[k] / (double)FBX_TIME_SECOND;
                    if (!isfinite(seconds) || fabs(seconds) > FBX_ANIM_TIME_SECONDS_MAX)
                        continue;
                    if (!found || curve->times[k] < min_time)
                        min_time = curve->times[k];
                    if (!found || curve->times[k] > max_time)
                        max_time = curve->times[k];
                    found = 1;
                }
            }
        }
    }
    if (!found)
        return 0;
    if (out_min_time)
        *out_min_time = min_time;
    if (out_max_time)
        *out_max_time = max_time;
    return 1;
}

/// @brief Sample one bone's T/R/S curves at each unique keyframe time and emit keyframes into
///        @p anim (Euler degrees → quaternion). @return 1 if any keyframe was emitted.
static int fbx_anim_build_bone_keyframes(void *anim,
                                         const fbx_anim_bone_builder_t *builders,
                                         int64_t bone_idx,
                                         int z_up,
                                         int64_t clip_start_time) {
    int64_t *times = NULL;
    int32_t time_count = 0;
    int32_t time_capacity = 0;
    int emitted_any = 0;
    int time_failed = 0;
    if (!builders[bone_idx].initialized)
        return 0;
    for (int trs = 0; trs < 3; trs++) {
        for (int comp = 0; comp < 3; comp++) {
            const fbx_anim_curve_view_t *curve = &builders[bone_idx].curves[trs][comp];
            if (!fbx_anim_curve_has_data(curve))
                continue;
            if (!fbx_anim_append_curve_sample_times(&times, &time_count, &time_capacity, curve)) {
                time_failed = 1;
                break;
            }
        }
        if (time_failed)
            break;
    }
    if (time_failed) {
        free(times);
        return 0;
    }
    fbx_anim_sort_unique_times(times, &time_count);
    for (int32_t ti = 0; ti < time_count; ti++) {
        int64_t fbx_time = times[ti];
        double t = ((double)fbx_time - (double)clip_start_time) / (double)FBX_TIME_SECOND;
        double tv[3];
        double rv[3];
        double sv[3];
        if (isfinite(t) && t < 0.0 && t > -1e-9)
            t = 0.0;
        if (!isfinite(t) || t < 0.0 || t > FBX_ANIM_TIME_SECONDS_MAX)
            continue;
        memcpy(tv, builders[bone_idx].base_translation, sizeof(tv));
        memcpy(rv, builders[bone_idx].base_rotation, sizeof(rv));
        memcpy(sv, builders[bone_idx].base_scale, sizeof(sv));
        for (int comp = 0; comp < 3; comp++) {
            tv[comp] =
                fbx_anim_curve_value(&builders[bone_idx].curves[0][comp], fbx_time, tv[comp]);
            rv[comp] =
                fbx_anim_curve_value(&builders[bone_idx].curves[1][comp], fbx_time, rv[comp]);
            sv[comp] =
                fbx_anim_curve_value(&builders[bone_idx].curves[2][comp], fbx_time, sv[comp]);
        }
        if (z_up)
            fbx_correct_zup(&tv[0], &tv[1], &tv[2]);
        if (!fbx_sanitize_position3(&tv[0], &tv[1], &tv[2]))
            continue;
        rv[0] = fbx_sanitize_rotation_degrees(rv[0]);
        rv[1] = fbx_sanitize_rotation_degrees(rv[1]);
        rv[2] = fbx_sanitize_rotation_degrees(rv[2]);
        sv[0] = fbx_scale_or_unit(sv[0]);
        sv[1] = fbx_scale_or_unit(sv[1]);
        sv[2] = fbx_scale_or_unit(sv[2]);

        double rx = rv[0] * 3.14159265358979323846 / 180.0;
        double ry = rv[1] * 3.14159265358979323846 / 180.0;
        double rz = rv[2] * 3.14159265358979323846 / 180.0;
        double cx = cos(rx * 0.5), sx = sin(rx * 0.5);
        double cy = cos(ry * 0.5), sy = sin(ry * 0.5);
        double cz = cos(rz * 0.5), sz = sin(rz * 0.5);
        double qw = cx * cy * cz + sx * sy * sz;
        double qx = sx * cy * cz - cx * sy * sz;
        double qy = cx * sy * cz + sx * cy * sz;
        double qz = cx * cy * sz - sx * sy * cz;
        if (!isfinite(qx) || !isfinite(qy) || !isfinite(qz) || !isfinite(qw))
            continue;
        {
            double qlen = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
            if (!isfinite(qlen) || qlen <= 1e-12)
                continue;
            qx /= qlen;
            qy /= qlen;
            qz /= qlen;
            qw /= qlen;
        }
        void *pos = rt_vec3_new(tv[0], tv[1], tv[2]);
        void *rot = rt_quat_new(qx, qy, qz, qw);
        void *scl = rt_vec3_new(sv[0], sv[1], sv[2]);
        if (pos && rot && scl) {
            rt_animation3d_add_keyframe(anim, bone_idx, t, pos, rot, scl);
            emitted_any = 1;
        }
        fbx_release_ref(&pos);
        fbx_release_ref(&rot);
        fbx_release_ref(&scl);
    }
    free(times);
    return emitted_any;
}

/// @brief Extract animation clips from the FBX node graph: for each animation stack, sample
///   its bone AnimCurves over the union of keyframe times and emit TRS keyframes (Euler
///   degrees converted to quaternions, with optional Z-up correction) into new Animation3Ds.
/// @param out_anims Receives a newly allocated array of animation objects (caller owns).
/// @param out_count Receives the number of animations written.
static void fbx_extract_animations(fbx_node_t *root,
                                   const fbx_conn_table_t *ct,
                                   void *skeleton,
                                   const fbx_bone_binding_t *bone_bindings,
                                   int32_t bone_binding_count,
                                   int z_up,
                                   void ***out_anims,
                                   int32_t *out_count,
                                   int32_t *out_capacity) {
    if (!out_anims || !out_count || !out_capacity)
        return;
    *out_anims = NULL;
    *out_count = 0;
    *out_capacity = 0;

    fbx_node_t *objects = fbx_find_child(root, "Objects");
    if (!objects)
        return;

    int64_t bone_count = skeleton ? rt_skeleton3d_get_bone_count(skeleton) : 0;
    if (bone_count <= 0)
        return;
    if (bone_count > FBX_MAX_SKELETON_BONES ||
        (size_t)bone_count > SIZE_MAX / sizeof(fbx_anim_bone_builder_t))
        return;

    /* Find AnimationStack nodes */
    for (int32_t i = 0; i < objects->child_count; i++) {
        fbx_node_t *obj = &objects->children[i];
        fbx_anim_bone_builder_t *builders;
        int64_t min_time;
        int64_t max_time;
        double duration;
        if (strcmp(obj->name, "AnimationStack") != 0)
            continue;

        int64_t stack_id = fbx_prop_i64(obj, 0);
        char decoded_anim_name[64];
        const char *anim_name = "Untitled";
        if (obj->prop_count >= 2) {
            fbx_decode_object_name(
                fbx_prop_str(obj, 1), decoded_anim_name, sizeof(decoded_anim_name));
            if (decoded_anim_name[0] != '\0')
                anim_name = decoded_anim_name;
        }

        if (!fbx_anim_stack_has_layer(objects, ct, stack_id))
            continue;

        if ((size_t)bone_count > SIZE_MAX / sizeof(*builders))
            continue;
        builders = (fbx_anim_bone_builder_t *)calloc((size_t)bone_count, sizeof(*builders));
        if (!builders)
            continue;

        fbx_anim_collect_curves(objects,
                                ct,
                                skeleton,
                                bone_bindings,
                                bone_binding_count,
                                bone_count,
                                stack_id,
                                builders);
        if (!fbx_anim_compute_time_bounds(builders, bone_count, &min_time, &max_time)) {
            free(builders);
            continue;
        }
        duration = ((double)max_time - (double)min_time) / (double)FBX_TIME_SECOND;
        duration = fbx_clamp_double(duration, 0.0, FBX_ANIM_TIME_SECONDS_MAX, 0.0);
        rt_string runtime_anim_name = rt_const_cstr(anim_name);
        void *anim = rt_animation3d_new(runtime_anim_name, duration > 0.0 ? duration : 1.0);
        rt_string_unref(runtime_anim_name);
        int emitted_any = 0;
        if (!anim) {
            free(builders);
            continue;
        }

        for (int64_t bone_idx = 0; bone_idx < bone_count; bone_idx++) {
            if (fbx_anim_build_bone_keyframes(anim, builders, bone_idx, z_up, min_time))
                emitted_any = 1;
        }
        free(builders);

        if (!emitted_any) {
            fbx_release_ref(&anim);
            continue;
        }
        rt_animation3d_set_looping(anim, 1);

        if (*out_count < 0 || *out_count == INT32_MAX) {
            fbx_release_ref(&anim);
            continue;
        }
        if (!fbx_asset_reserve_ref_array(out_anims, out_capacity, *out_count + 1)) {
            fbx_release_ref(&anim);
            continue;
        }
        (*out_anims)[*out_count] = anim;
        (*out_count)++;
    }
}
