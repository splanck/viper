//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_gltf_anim.inc  (#included from rt_gltf.c; not a standalone translation unit)
// Purpose: glTF skin parsing plus skeletal and node animation extraction
//===----------------------------------------------------------------------===//

/// @brief Reverse-lookup: given a glTF node index, return the local joint slot within
///        this skin (or -1 if the node isn't part of it). Linear scan is fine because
///        typical skins have fewer than ~64 joints.
static int gltf_skin_find_joint(const gltf_skin_t *skin, int32_t node_idx) {
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    if (!skin)
        return -1;
    for (int32_t i = 0; i < joint_count; i++)
        if (skin->joint_nodes[i] == node_idx)
            return i;
    return -1;
}

/// @brief Depth-first registration of a glTF joint into the engine's skeleton.
/// @details Converts one joint (plus all of its joint-space descendants) into engine
///          bones, preserving the parent/child hierarchy. Each node's local TRS is
///          folded into a bind matrix via `gltf_node_local_matrix` and registered with
///          `rt_skeleton3d_add_bone`.
///
///          `skin->joint_to_bone[joint_local]` doubles as a cycle-break tri-state:
///            -1 = not visited, -2 = currently on the recursion stack, >=0 = bone idx.
///          A self-referential or cyclic joint graph (malformed glTF) will hit the -2
///          guard and bail instead of overflowing the stack. Unresolved joints stay at
///          -1 so callers can retry them as roots on a second pass.
///
///          Children are discovered through the JSON "children" array of the node, not
///          through a prebuilt parent table — this lets one joint be skipped (e.g. when
///          its own parent-joint lookup failed) and still be picked up later as a root.
/// @param skin Skin context; its `joint_to_bone` array is mutated in place.
/// @param nodes_arr JSON "nodes" array from the glTF root (used for traversal).
/// @param joint_local Zero-based index into `skin->joint_nodes`.
/// @param parent_bone Engine bone index of the parent, or -1 to make a root bone.
/// @return The newly added bone index, or -1 on failure / cycle.
static int64_t gltf_add_skin_joint_recursive(gltf_skin_t *skin,
                                             void *nodes_arr,
                                             int32_t joint_local,
                                             int64_t parent_bone) {
    int32_t node_idx;
    void *node_json;
    const char *name;
    char generated_name[64];
    double local[16];
    void *bind_mat;
    int64_t bone_idx;
    void *children;
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    if (!skin || !skin->skeleton || !nodes_arr || joint_local < 0 ||
        joint_local >= joint_count)
        return -1;
    if (skin->joint_to_bone[joint_local] >= 0)
        return skin->joint_to_bone[joint_local];
    if (skin->joint_to_bone[joint_local] == -2)
        return -1;
    skin->joint_to_bone[joint_local] = -2;
    node_idx = skin->joint_nodes[joint_local];
    if (!gltf_node_local_matrix(nodes_arr, node_idx, local)) {
        skin->joint_to_bone[joint_local] = -1;
        return -1;
    }
    node_json = rt_seq_get(nodes_arr, node_idx);
    name = jstr(node_json, "name");
    if (!name || name[0] == '\0') {
        snprintf(generated_name, sizeof(generated_name), "joint_%d", (int)joint_local);
        name = generated_name;
    }
    bind_mat = rt_mat4_new(local[0],
                           local[1],
                           local[2],
                           local[3],
                           local[4],
                           local[5],
                           local[6],
                           local[7],
                           local[8],
                           local[9],
                           local[10],
                           local[11],
                           local[12],
                           local[13],
                           local[14],
                           local[15]);
    bone_idx = rt_skeleton3d_add_bone(skin->skeleton, rt_const_cstr(name), parent_bone, bind_mat);
    gltf_release_ref(&bind_mat);
    if (bone_idx < 0) {
        skin->joint_to_bone[joint_local] = -1;
        return -1;
    }
    skin->joint_to_bone[joint_local] = (int32_t)bone_idx;

    children = jarr(node_json, "children");
    for (int64_t ci = 0; ci < jarr_len(children); ci++) {
        int32_t child_node = gltf_i32_from_i64_or(jvalue_int(rt_seq_get(children, ci), -1), -1);
        int32_t child_joint = gltf_skin_find_joint(skin, child_node);
        if (child_joint >= 0)
            gltf_add_skin_joint_recursive(skin, nodes_arr, child_joint, bone_idx);
    }
    return bone_idx;
}

/// @brief Release the per-skin scratch buffers held in an array produced by
///        `gltf_parse_skins`. Does NOT release the skeleton object itself — that is
///        owned by `asset->skeletons` and lives beyond the parse step.
static void gltf_free_skins(gltf_skin_t *skins, int32_t skin_count) {
    if (!skins)
        return;
    for (int32_t i = 0; i < skin_count; i++) {
        free(skins[i].joint_nodes);
        free(skins[i].joint_to_bone);
    }
    free(skins);
}

/// @brief Release Skeleton3D handles stored in skin scratch entries and clear the slots.
static void gltf_release_skin_skeletons(gltf_skin_t *skins, int32_t skin_count) {
    if (!skins)
        return;
    for (int32_t i = 0; i < skin_count; i++)
        gltf_release_ref(&skins[i].skeleton);
}

static int gltf_inverse_bind_accessor_valid(const gltf_accessor_view_t *view,
                                            int32_t joint_count);

static void gltf_parse_skins_clear_asset(rt_gltf_asset *asset) {
    if (!asset)
        return;
    asset->skeleton_count = 0;
    free(asset->skeletons);
    asset->skeletons = NULL;
    asset->skeleton_capacity = 0;
}

static void gltf_parse_skins_fail(rt_gltf_asset *asset,
                                  gltf_skin_t *skins,
                                  int32_t skin_count,
                                  int32_t *node_parents,
                                  int *hard_error) {
    if (hard_error)
        *hard_error = 1;
    gltf_release_skin_skeletons(skins, skin_count);
    gltf_parse_skins_clear_asset(asset);
    free(node_parents);
    gltf_free_skins(skins, skin_count);
}

static int32_t *gltf_build_node_parent_table(void *nodes_arr, int32_t node_count) {
    int32_t *node_parents = (int32_t *)malloc((size_t)node_count * sizeof(*node_parents));
    if (!node_parents)
        return NULL;
    for (int32_t i = 0; i < node_count; i++)
        node_parents[i] = -1;
    for (int32_t ni = 0; ni < node_count; ni++) {
        void *node_json = rt_seq_get(nodes_arr, ni);
        void *children = jarr(node_json, "children");
        for (int64_t ci = 0; ci < jarr_len(children); ci++) {
            int32_t child = gltf_i32_from_i64_or(jvalue_int(rt_seq_get(children, ci), -1), -1);
            if (child >= 0 && child < node_count)
                node_parents[child] = ni;
        }
    }
    return node_parents;
}

static int gltf_parse_skins_alloc_state(rt_gltf_asset *asset,
                                        void *nodes_arr,
                                        int32_t skin_count,
                                        int32_t node_count,
                                        int32_t **out_node_parents,
                                        gltf_skin_t **out_skins) {
    int32_t *node_parents = gltf_build_node_parent_table(nodes_arr, node_count);
    gltf_skin_t *skins = NULL;
    *out_node_parents = NULL;
    *out_skins = NULL;
    if (!node_parents)
        return 0;
    skins = (gltf_skin_t *)calloc((size_t)skin_count, sizeof(*skins));
    asset->skeletons = (void **)calloc((size_t)skin_count, sizeof(void *));
    asset->skeleton_count = 0;
    asset->skeleton_capacity = asset->skeletons ? skin_count : 0;
    if (!skins || !asset->skeletons) {
        free(node_parents);
        gltf_free_skins(skins, skin_count);
        free(asset->skeletons);
        asset->skeletons = NULL;
        asset->skeleton_count = 0;
        asset->skeleton_capacity = 0;
        return 0;
    }
    *out_node_parents = node_parents;
    *out_skins = skins;
    return 1;
}

static int gltf_skin_init_entry(gltf_skin_t *skin, int32_t joint_count) {
    skin->joint_nodes = (int32_t *)calloc((size_t)joint_count, sizeof(int32_t));
    skin->joint_to_bone = (int32_t *)malloc((size_t)joint_count * sizeof(int32_t));
    skin->joint_count = joint_count;
    skin->skeleton = rt_skeleton3d_new();
    if (!skin->joint_nodes || !skin->joint_to_bone || !skin->skeleton) {
        if (skin->skeleton)
            gltf_release_ref(&skin->skeleton);
        free(skin->joint_nodes);
        free(skin->joint_to_bone);
        skin->joint_nodes = NULL;
        skin->joint_to_bone = NULL;
        skin->joint_count = 0;
        return 0;
    }
    return 1;
}

static int gltf_skin_read_joint_nodes(gltf_skin_t *skin,
                                      void *joints,
                                      int32_t joint_count,
                                      int32_t node_count) {
    for (int32_t ji = 0; ji < joint_count; ji++) {
        int32_t joint_node = gltf_i32_from_i64_or(jvalue_int(rt_seq_get(joints, ji), -1), -1);
        if (joint_node < 0 || joint_node >= node_count)
            return 0;
        for (int32_t prev = 0; prev < ji; prev++) {
            if (skin->joint_nodes[prev] == joint_node)
                return 0;
        }
        skin->joint_nodes[ji] = joint_node;
        skin->joint_to_bone[ji] = -1;
    }
    return 1;
}

static void gltf_skin_register_joints(gltf_skin_t *skin,
                                      void *nodes_arr,
                                      const int32_t *node_parents,
                                      int32_t node_count) {
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    for (int32_t ji = 0; ji < joint_count; ji++) {
        int32_t parent_node =
            skin->joint_nodes[ji] >= 0 && skin->joint_nodes[ji] < node_count
                ? node_parents[skin->joint_nodes[ji]]
                : -1;
        if (gltf_skin_find_joint(skin, parent_node) < 0)
            gltf_add_skin_joint_recursive(skin, nodes_arr, ji, -1);
    }
    for (int32_t ji = 0; ji < joint_count; ji++) {
        if (skin->joint_to_bone[ji] < 0)
            gltf_add_skin_joint_recursive(skin, nodes_arr, ji, -1);
    }
}

static void gltf_skin_apply_inverse_bind(void *root,
                                         void *skin_json,
                                         gltf_buffer_t *buffers,
                                         int buf_count,
                                         gltf_skin_t *skin) {
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    int64_t ibm_acc = jint(skin_json, "inverseBindMatrices", -1);
    gltf_accessor_view_t ibm_view;
    if (!gltf_get_accessor_view(root, ibm_acc, buffers, buf_count, &ibm_view) ||
        !gltf_inverse_bind_accessor_valid(&ibm_view, joint_count))
        return;
    {
        rt_skeleton3d *skel = (rt_skeleton3d *)skin->skeleton;
        int32_t bone_count = skeleton3d_safe_bone_count(skel);
        for (int32_t ji = 0; ji < joint_count; ji++) {
            int32_t bone = skin->joint_to_bone[ji];
            float col_major[16];
            if (bone < 0 || bone >= bone_count)
                continue;
            gltf_accessor_read_f32(&ibm_view, ji, col_major, 16);
            for (int row = 0; row < 4; row++)
                for (int col = 0; col < 4; col++)
                    skel->bones[bone].inverse_bind[row * 4 + col] = col_major[col * 4 + row];
        }
    }
}

static int gltf_parse_skin_entry(rt_gltf_asset *asset,
                                 void *root,
                                 void *nodes_arr,
                                 void *skin_json,
                                 gltf_buffer_t *buffers,
                                 int buf_count,
                                 const int32_t *node_parents,
                                 int32_t node_count,
                                 gltf_skin_t *skin) {
    void *joints = jarr(skin_json, "joints");
    int64_t joint_count64 = jarr_len(joints);
    int32_t joint_count;
    if (joint_count64 <= 0)
        return 0;
    if (joint_count64 > INT32_MAX || joint_count64 > VGFX3D_MAX_BONES)
        return -1;
    joint_count = (int32_t)joint_count64;
    if (!gltf_skin_init_entry(skin, joint_count))
        return 0;
    if (!gltf_skin_read_joint_nodes(skin, joints, joint_count, node_count))
        return -1;
    gltf_skin_register_joints(skin, nodes_arr, node_parents, node_count);
    rt_skeleton3d_compute_inverse_bind(skin->skeleton);
    gltf_skin_apply_inverse_bind(root, skin_json, buffers, buf_count, skin);
    asset->skeletons[asset->skeleton_count++] = skin->skeleton;
    return 1;
}

/// @brief Parse every skin in a glTF document into engine `Skeleton3D` objects.
/// @details Walks the top-level "skins" array and, for each skin, builds a Skeleton3D
///          whose bones mirror the skin's joint hierarchy. The pipeline per skin is:
///            1. Build a `node_parents[]` lookup table (single O(N) pass over nodes)
///               so we can identify root joints (parent-of-joint not in same skin).
///            2. For each skin, allocate `joint_nodes` / `joint_to_bone` scratch.
///            3. First pass: register only root joints (parent outside the skin) so
///               they become root bones.
///            4. Second pass: any joint still unresolved (disconnected subtree, joint
///               order shenanigans) is added as its own root.
///            5. `rt_skeleton3d_compute_inverse_bind` computes the fallback inverse
///               bind matrices from local TRS, then if the skin provides
///               "inverseBindMatrices" the values are read and transposed from
///               glTF column-major into the engine's row-major layout.
///
///          On allocation failure — or a malformed skin (out-of-range or duplicate joint
///          index), which also sets *hard_error — the partial state is rolled back
///          (node_parents freed, skins freed, asset->skeletons cleared) so the asset is never
///          left in a half-built state.
///
///          Output `out_skins` is optional; when NULL the caller indicates they only
///          want the skeletons populated on `asset` and the helper table is freed.
/// @param asset Target asset — `skeletons` is allocated and populated.
/// @param root glTF JSON root object.
/// @param buffers Parsed buffer table (indexed by bufferView.buffer).
/// @param buf_count Number of entries in `buffers`.
/// @param out_skins Optional; receives a per-skin scratch array (caller frees via
///                  `gltf_free_skins`). Needed later to map vertex bone indices.
/// @param out_skin_count Optional; receives the count that was written.
static void gltf_parse_skins(rt_gltf_asset *asset,
                             void *root,
                             gltf_buffer_t *buffers,
                             int buf_count,
                             gltf_skin_t **out_skins,
                             int32_t *out_skin_count,
                             int *hard_error) {
    void *skins_arr = jarr(root, "skins");
    void *nodes_arr = jarr(root, "nodes");
    int64_t skin_count64 = jarr_len(skins_arr);
    int64_t node_count64 = jarr_len(nodes_arr);
    int32_t skin_count = 0;
    int32_t node_count = 0;
    int32_t *node_parents = NULL;
    gltf_skin_t *skins = NULL;
    if (hard_error)
        *hard_error = 0;
    if (out_skins)
        *out_skins = NULL;
    if (out_skin_count)
        *out_skin_count = 0;
    if (!asset || !skins_arr || skin_count64 <= 0 || skin_count64 > INT32_MAX || !nodes_arr ||
        node_count64 <= 0 || node_count64 > INT32_MAX)
        return;
    skin_count = (int32_t)skin_count64;
    node_count = (int32_t)node_count64;

    if (!gltf_parse_skins_alloc_state(
            asset, nodes_arr, skin_count, node_count, &node_parents, &skins))
        return;

    for (int32_t si = 0; si < skin_count; si++) {
        void *skin_json = rt_seq_get(skins_arr, si);
        int skin_result = gltf_parse_skin_entry(asset,
                                                root,
                                                nodes_arr,
                                                skin_json,
                                                buffers,
                                                buf_count,
                                                node_parents,
                                                node_count,
                                                &skins[si]);
        if (skin_result < 0) {
            gltf_parse_skins_fail(asset, skins, skin_count, node_parents, hard_error);
            return;
        }
        if (skin_result == 0) {
            gltf_parse_skins_fail(asset, skins, skin_count, node_parents, hard_error);
            return;
        }
    }
    free(node_parents);
    if (out_skins)
        *out_skins = skins;
    else
        gltf_free_skins(skins, skin_count);
    if (out_skin_count)
        *out_skin_count = skin_count;
}

/// @brief Retarget a mesh's raw glTF joint indices onto the skin's engine bone indices.
/// @details After vertex data is loaded, each vertex carries its original glTF joint
///          indices (`bone_indices[0..3]`) and four weights. This function:
///            - Translates each joint through `skin->joint_to_bone[]` to the matching
///              engine bone index, dropping influences where the mapping is invalid or
///              the weight is zero.
///            - Renormalizes the surviving weights so they sum to 1 (glTF assets are
///              meant to ship normalized but some exporters lie; one influence being
///              dropped by the mapping would otherwise skew the result).
///            - Writes the fixed-up (bone, weight) pairs back through
///              `rt_mesh3d_set_bone_weights`.
///          Tracks `max_bone` so the mesh's advertised bone count matches what the
///          vertex data actually references — this is what the skinning shader uses to
///          size the palette upload.
/// @param mesh_obj Target mesh (mutated in place).
/// @param skin Skin whose `joint_to_bone` table drives the remap.
static void gltf_apply_skin_to_mesh(void *mesh_obj, const gltf_skin_t *skin) {
    rt_mesh3d *mesh = (rt_mesh3d *)mesh_obj;
    int32_t max_bone = -1;
    int32_t joint_count = gltf_skin_safe_joint_count(skin);
    if (!mesh || !mesh->vertices || joint_count <= 0)
        return;
    for (uint32_t vi = 0; vi < mesh->vertex_count; vi++) {
        vgfx3d_vertex_t *v = &mesh->vertices[vi];
        int64_t bones[4] = {0, 0, 0, 0};
        double weights[4] = {0.0, 0.0, 0.0, 0.0};
        double sum = 0.0;
        for (int i = 0; i < 4; i++) {
            uint8_t joint = v->bone_indices[i];
            if (joint < joint_count && skin->joint_to_bone[joint] >= 0 &&
                isfinite(v->bone_weights[i]) && v->bone_weights[i] > 0.0f) {
                bones[i] = skin->joint_to_bone[joint];
                weights[i] = v->bone_weights[i];
                sum += weights[i];
            }
        }
        if (sum <= 0.0) {
            memset(v->bone_indices, 0, sizeof(v->bone_indices));
            memset(v->bone_weights, 0, sizeof(v->bone_weights));
            continue;
        }
        for (int i = 0; i < 4; i++) {
            weights[i] /= sum;
            if (weights[i] > 0.0 && bones[i] + 1 > max_bone)
                max_bone = (int32_t)(bones[i] + 1);
        }
        rt_mesh3d_set_bone_weights(mesh,
                                   vi,
                                   bones[0],
                                   weights[0],
                                   bones[1],
                                   weights[1],
                                   bones[2],
                                   weights[2],
                                   bones[3],
                                   weights[3]);
    }
    mesh->bone_count = max_bone + 1;
    rt_mesh3d_set_skeleton(mesh, skin->skeleton);
}

/// @brief Deep-clone a mesh for per-node variant use.
/// @details `rt_mesh3d_clone` already clones attached morph targets, so this wrapper
///          deliberately avoids a second clone/reattach pass.
static void *gltf_clone_mesh_variant(void *source_mesh) {
    if (!source_mesh)
        return NULL;
    return rt_mesh3d_clone(source_mesh);
}

/// @brief Return the mesh to attach to a glTF scene node, creating a per-node variant when needed.
/// @details Returns the shared asset mesh directly when neither skinning nor morph weights require
///          per-instance state. When a skin or a weights array is present, clones the mesh via
///          `gltf_clone_mesh_variant`, applies the skin's inverse-bind matrices, and writes the
///          initial morph weights. Returns NULL on clone failure so callers can fail
///          the import rather than silently binding mutable state to a shared mesh.
static void *gltf_make_node_mesh_variant(rt_gltf_asset *asset,
                                         int32_t mesh_index,
                                         int64_t skin_ref,
                                         void *weights_arr,
                                         const gltf_skin_t *skins,
                                         int32_t skin_count) {
    int has_skin = skin_ref >= 0 && skin_ref < skin_count;
    int needs_variant = has_skin || weights_arr != NULL;
    void *source_mesh;
    void *variant;
    if (!asset || mesh_index < 0 || mesh_index >= asset->mesh_count)
        return NULL;
    if (!needs_variant)
        return asset->meshes[mesh_index];
    source_mesh = asset->meshes[mesh_index];
    variant = gltf_clone_mesh_variant(source_mesh);
    if (!variant)
        return NULL;
    if (has_skin)
        gltf_apply_skin_to_mesh(variant, &skins[skin_ref]);
    if (weights_arr)
        gltf_apply_node_morph_weights(variant, weights_arr);
    return variant;
}

typedef struct {
    int8_t valid;
    int32_t path; /* 0=translation, 1=rotation, 2=scale */
    int32_t node_idx;
    int32_t bone_idx;
    const char *interpolation;
    gltf_accessor_view_t input;
    gltf_accessor_view_t output;
} gltf_anim_curve_t;

/// @brief Return non-zero if a skeletal animation curve already targets this bone/path.
/// @details glTF leaves duplicate animation channels for the same target undefined. The
///          runtime stores a single TRS curve per bone property, so importing later duplicates
///          would otherwise mix their key times while sampling only one curve.
static int gltf_anim_curve_already_targets(const gltf_anim_curve_t *curves,
                                           int32_t count,
                                           int32_t bone_idx,
                                           int32_t path) {
    if (!curves || count <= 0 || bone_idx < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (curves[i].valid && curves[i].bone_idx == bone_idx && curves[i].path == path)
            return 1;
    }
    return 0;
}

/// @brief Return non-zero if a node animation channel already owns this node/path pair.
static int gltf_node_anim_channel_seen(const int32_t *nodes,
                                       const int32_t *paths,
                                       int32_t count,
                                       int32_t node_idx,
                                       int32_t path) {
    if (!nodes || !paths || count <= 0 || node_idx < 0)
        return 0;
    for (int32_t i = 0; i < count; i++) {
        if (nodes[i] == node_idx && paths[i] == path)
            return 1;
    }
    return 0;
}

/// @brief Look up the engine bone index for a glTF node in a specific skin.
/// @return Engine bone index, or -1 if the node is not a joint in this skin.
static int32_t gltf_skin_bone_for_node(const gltf_skin_t *skin, int32_t node_idx) {
    int32_t joint;
    if (!skin || !skin->joint_to_bone)
        return -1;
    joint = gltf_skin_find_joint(skin, node_idx);
    if (joint >= 0 && skin->joint_to_bone[joint] >= 0)
        return skin->joint_to_bone[joint];
    return -1;
}

/// @brief Read the time value at @p index from a glTF animation input accessor as a double.
/// @details glTF animation times are stored as float32 scalars in the input accessor;
///          this function reads one float and promotes it to double for precision.
static double gltf_curve_time(const gltf_accessor_view_t *view, int32_t index) {
    float t = 0.0f;
    gltf_accessor_read_f32(view, index, &t, 1);
    return (double)t;
}

/// @brief Insert `value` into a sorted-unique time list, growing the backing buffer as
///        needed. Ignores values within 1e-6 of an existing entry (de-dup tolerance
///        mirrors typical DCC exporter precision). The buffer doubles on growth
///        starting at 16 entries so amortized cost is O(N log N) total.
/// @return 1 on success, 0 on allocation failure (caller must free `*times` later).
static int gltf_anim_insert_time(double **times, int32_t *count, int32_t *capacity, double value) {
    int32_t pos = 0;
    if (!times || !count || !capacity || *count < 0 || *capacity < 0 || *count > *capacity ||
        (*count > 0 && !*times) || !isfinite(value) || value < 0.0)
        return 0;
    while (pos < *count && (*times)[pos] < value - 1e-6)
        pos++;
    if (pos < *count && fabs((*times)[pos] - value) <= 1e-6)
        return 1;
    if (*count >= *capacity) {
        int32_t new_capacity;
        double *grown;
        if (*capacity > INT32_MAX / 2)
            new_capacity = *count + 1;
        else
            new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        if (new_capacity <= *capacity || (size_t)new_capacity > SIZE_MAX / sizeof(*grown))
            return 0;
        grown = (double *)realloc(*times, (size_t)new_capacity * sizeof(*grown));
        if (!grown)
            return 0;
        *times = grown;
        *capacity = new_capacity;
    }
    memmove(&(*times)[pos + 1], &(*times)[pos], (size_t)(*count - pos) * sizeof(**times));
    (*times)[pos] = value;
    (*count)++;
    return 1;
}

/// @brief Return a pre-next-key hold time for importing STEP curves into linear Animation3D tracks.
/// @details Animation3D has linear/slerp playback only. Adding a hold key just before the next
///          authored key preserves glTF STEP semantics closely enough for skeletal clips while
///          avoiding duplicate-time replacement in Animation3D.
static double gltf_anim_step_hold_time(double current, double next) {
    double gap = next - current;
    double eps;
    if (!isfinite(current) || !isfinite(next) || gap <= 4e-6)
        return -1.0;
    eps = gap * 0.25;
    if (eps > 1e-4)
        eps = 1e-4;
    if (eps <= 1e-6)
        return -1.0;
    return next - eps;
}

/// @brief Insert all sample times for a skeletal curve, expanding STEP curves with hold keys.
static int gltf_anim_insert_curve_times(double **times,
                                        int32_t *count,
                                        int32_t *capacity,
                                        const gltf_anim_curve_t *curve) {
    int step;
    if (!curve || !curve->valid)
        return 1;
    step = curve->interpolation && strcmp(curve->interpolation, "STEP") == 0;
    for (int32_t ti = 0; ti < curve->input.count; ti++) {
        double t = gltf_curve_time(&curve->input, ti);
        if (!gltf_anim_insert_time(times, count, capacity, t) ||
            *count > RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL)
            return 0;
        if (step && ti + 1 < curve->input.count) {
            double hold = gltf_anim_step_hold_time(t, gltf_curve_time(&curve->input, ti + 1));
            if (hold >= 0.0 &&
                (!gltf_anim_insert_time(times, count, capacity, hold) ||
                 *count > RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL))
                return 0;
        }
    }
    return 1;
}

/// @brief Convert a keyframe index into the real output-accessor offset for the curve.
/// @details glTF CUBICSPLINE samplers store three floats-per-component per keyframe in
///          the order [in-tangent, value, out-tangent]. So for N keyframes the output
///          accessor holds 3N entries and the actual "value" element we want is
///          `key_index * 3 + 1`. STEP and LINEAR samplers store one entry per key and
///          map identity.
static int32_t gltf_curve_output_index(const gltf_anim_curve_t *curve, int32_t key_index) {
    if (key_index < 0)
        return -1;
    if (curve && curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0) {
        if (key_index > (INT32_MAX - 1) / 3)
            return -1;
        return key_index * 3 + 1;
    }
    return key_index;
}

/// @brief Read @p components floats from the curve's output accessor at an already-computed index.
/// @details Thin wrapper around `gltf_accessor_read_f32` that accepts a pre-multiplied index
///          (CUBICSPLINE callers pass `key * 3 + 1` for the value sample; linear callers pass
///          `key`).
static void gltf_curve_read_output_value(const gltf_anim_curve_t *curve,
                                         int32_t output_index,
                                         float *out,
                                         int32_t components) {
    if (!curve || !out || components <= 0 || components > 4 || output_index < 0 ||
        output_index >= curve->output.count)
        return;
    gltf_accessor_read_f32(&curve->output, output_index, out, components);
}

/// @brief Read `components` floats for one logical keyframe from the curve's output
///        accessor, handling the CUBICSPLINE stride quirk transparently.
static void gltf_curve_read_value(const gltf_anim_curve_t *curve,
                                  int32_t key_index,
                                  float *out,
                                  int32_t components) {
    gltf_curve_read_output_value(curve, gltf_curve_output_index(curve, key_index), out, components);
}

/// @brief Read a single scalar float from an accessor by flat component index.
/// @details Converts a flat index (element_index * comp_count + component) into the correct
///          (element, component) pair, reads a full element into a temporary buffer, then
///          returns the requested component. Used to read per-vertex weight scalars from
///          multi-component accessor views.
/// @return The float value at @p flat_index, or 0.0 on out-of-range or null view.
static float gltf_accessor_read_flat_f32(const gltf_accessor_view_t *view, int32_t flat_index) {
    float tmp[16];
    int32_t comp_count;
    int32_t element;
    int32_t component;
    if (!view || flat_index < 0 || view->comp_count <= 0)
        return 0.0f;
    comp_count = view->comp_count;
    if (comp_count > (int32_t)(sizeof(tmp) / sizeof(tmp[0])))
        return 0.0f;
    element = flat_index / comp_count;
    component = flat_index % comp_count;
    if (component < 0 || element < 0 || element >= view->count)
        return 0.0f;
    gltf_accessor_read_f32(view, element, tmp, comp_count);
    return tmp[component];
}

/// @brief Return non-zero when a float accessor region contains only finite values.
static int gltf_accessor_f32_values_are_finite(const gltf_accessor_view_t *view,
                                               int32_t element_count,
                                               int32_t components) {
    float tmp[16];
    if (!view || element_count < 0 || components <= 0 ||
        components > (int32_t)(sizeof(tmp) / sizeof(tmp[0])) || element_count > view->count)
        return 0;
    for (int32_t i = 0; i < element_count; i++) {
        gltf_accessor_read_f32(view, i, tmp, components);
        for (int32_t c = 0; c < components; c++) {
            if (!isfinite(tmp[c]))
                return 0;
        }
    }
    return 1;
}

enum {
    GLTF_ANIM_INTERP_INVALID = -1,
    GLTF_ANIM_INTERP_LINEAR = 0,
    GLTF_ANIM_INTERP_STEP = 1,
    GLTF_ANIM_INTERP_CUBICSPLINE = 2
};

/// @brief Validate glTF animation sampler input times (SCALAR FLOAT, finite, sorted).
static int gltf_animation_input_accessor_valid(const gltf_accessor_view_t *input,
                                               int32_t interpolation_mode) {
    double previous = -1.0;
    int step = interpolation_mode == GLTF_ANIM_INTERP_STEP;
    if (!input || input->comp_type != 5126 || input->comp_count != 1 || input->count <= 0)
        return 0;
    for (int32_t i = 0; i < input->count; i++) {
        double t = gltf_curve_time(input, i);
        if (!isfinite(t) || t < 0.0)
            return 0;
        if (i > 0 && (t < previous || (!step && t <= previous + 1e-6)))
            return 0;
        previous = t;
    }
    return 1;
}

#define GLTF_NODE_ANIM_KEY_COUNT_MAX 1000000
#define GLTF_NODE_ANIM_VALUE_WIDTH_MAX 4096
#define GLTF_NODE_ANIM_VALUE_COUNT_MAX 4000000
#define GLTF_SKIN_ANIM_KEY_COUNT_MAX RT_ANIMATION3D_MAX_KEYFRAMES_PER_CHANNEL

/// @brief Decode a glTF sampler interpolation string, applying only the spec's absent-field default.
static int32_t gltf_animation_interpolation_mode(const char *interpolation) {
    if (!interpolation || interpolation[0] == '\0' || strcmp(interpolation, "LINEAR") == 0)
        return GLTF_ANIM_INTERP_LINEAR;
    if (strcmp(interpolation, "STEP") == 0)
        return GLTF_ANIM_INTERP_STEP;
    if (strcmp(interpolation, "CUBICSPLINE") == 0)
        return GLTF_ANIM_INTERP_CUBICSPLINE;
    return GLTF_ANIM_INTERP_INVALID;
}

/// @brief Validate translation/rotation/scale animation sampler outputs.
static int gltf_animation_trs_output_accessor_valid(const gltf_accessor_view_t *input,
                                                    const gltf_accessor_view_t *output,
                                                    int32_t components,
                                                    int cubic) {
    int64_t required_count;
    if (!input || !output || components <= 0 || output->comp_type != 5126 ||
        output->comp_count != components)
        return 0;
    required_count = (int64_t)input->count * (int64_t)(cubic ? 3 : 1);
    if (required_count <= 0 || required_count > INT32_MAX || output->count != required_count)
        return 0;
    return gltf_accessor_f32_values_are_finite(output, (int32_t)required_count, components);
}

/// @brief Validate inverseBindMatrices accessors before overriding computed bind data.
static int gltf_inverse_bind_accessor_valid(const gltf_accessor_view_t *view,
                                            int32_t joint_count) {
    if (!view || joint_count <= 0 || view->comp_type != 5126 || view->comp_count != 16 ||
        view->count != joint_count)
        return 0;
    return gltf_accessor_f32_values_are_finite(view, joint_count, 16);
}

/// @brief Normalize @p out in-place when @p components == 4 (quaternion interpolation result).
/// @details glTF CUBICSPLINE and linear interpolation can drift the magnitude of rotation
///          quaternions away from unit length; renormalization prevents visual artifacts.
///          Non-quaternion paths (translation/scale with components ≠ 4) are left unchanged.
static void gltf_normalize_sample_if_quat(float *out, int32_t components) {
    if (!out)
        return;
    if (components == 4) {
        float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        if (isfinite(len) && len > 1e-6f) {
            for (int c = 0; c < 4; c++)
                out[c] /= len;
        } else {
            out[0] = 0.0f;
            out[1] = 0.0f;
            out[2] = 0.0f;
            out[3] = 1.0f;
        }
    }
}

/// @brief Replace non-finite sampled components with the caller's pre-sample fallback.
static void gltf_sanitize_sample_or(float *out, const float *fallback, int32_t components) {
    if (!out || components <= 0 || components > 4)
        return;
    for (int32_t c = 0; c < components; c++) {
        if (!isfinite(out[c]))
            out[c] = fallback && isfinite(fallback[c]) ? fallback[c]
                                                       : (components == 4 && c == 3 ? 1.0f : 0.0f);
    }
    gltf_normalize_sample_if_quat(out, components);
}

/// @brief Spherically interpolate two quaternions by @p alpha ∈ [0, 1].
/// @details Both inputs are normalized before interpolation. When the dot product is
///          negative, `q1` is negated to ensure slerp travels the shortest arc (< 180°).
///          When the quaternions are nearly parallel (dot > 0.9995), plain nlerp is used
///          to avoid division-by-zero in the sin/cos path. Result is always renormalized.
static void gltf_slerp_quat(const float *a, const float *b, double alpha, float *out) {
    float q0[4];
    float q1[4];
    double dot;
    if (!a || !b || !out)
        return;
    if (!isfinite(alpha) || alpha < 0.0)
        alpha = 0.0;
    else if (alpha > 1.0)
        alpha = 1.0;
    memcpy(q0, a, sizeof(q0));
    memcpy(q1, b, sizeof(q1));
    gltf_normalize_sample_if_quat(q0, 4);
    gltf_normalize_sample_if_quat(q1, 4);
    dot = (double)q0[0] * q1[0] + (double)q0[1] * q1[1] + (double)q0[2] * q1[2] +
          (double)q0[3] * q1[3];
    if (dot < 0.0) {
        dot = -dot;
        for (int c = 0; c < 4; c++)
            q1[c] = -q1[c];
    }
    if (!isfinite(dot)) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    if (dot > 1.0)
        dot = 1.0;
    if (dot < -1.0)
        dot = -1.0;
    if (dot > 0.9995) {
        for (int c = 0; c < 4; c++)
            out[c] = (float)((double)q0[c] + ((double)q1[c] - (double)q0[c]) * alpha);
        gltf_normalize_sample_if_quat(out, 4);
        return;
    }
    {
        double theta0 = acos(dot);
        double theta = theta0 * alpha;
        double sin_theta = sin(theta);
        double sin_theta0 = sin(theta0);
        double s0 = cos(theta) - dot * sin_theta / sin_theta0;
        double s1 = sin_theta / sin_theta0;
        for (int c = 0; c < 4; c++)
            out[c] = (float)(s0 * q0[c] + s1 * q1[c]);
        gltf_normalize_sample_if_quat(out, 4);
    }
}

/// @brief Sample an animation curve at an arbitrary time, respecting its interpolation.
/// @details Handles clamping (before first / after last key), STEP interpolation (hold
///          previous value), LINEAR interpolation, and glTF CUBICSPLINE Hermite sampling
///          using the exported in/out tangents.
///
///          When sampling a rotation curve (`components == 4`) the blended quaternion
///          is renormalized; linear quaternion blends otherwise drift below unit length
///          on long playback and produce subtle scale creep.
///
///          The key-count is clamped to the smaller of input.count and output.count
///          (or output.count/3 for CUBICSPLINE) so truncated/corrupt asset data can't
///          run us past the accessor view.
/// @param curve Source curve (must be marked valid; no-op otherwise).
/// @param time Sample time in seconds.
/// @param out Destination buffer of `components` floats.
/// @param components 3 for translation/scale, 4 for rotation quaternions.
static void gltf_sample_curve(const gltf_anim_curve_t *curve,
                              double time,
                              float *out,
                              int32_t components) {
    int32_t key_count;
    int32_t lo;
    int32_t hi;
    double t0;
    double t1;
    double alpha;
    float fallback[4] = {0.0f, 0.0f, 0.0f, components == 4 ? 1.0f : 0.0f};
    if (!curve || !curve->valid || !out || components <= 0 || curve->input.count <= 0 ||
        components > 4 || !isfinite(time))
        return;
    for (int32_t c = 0; c < components; c++)
        fallback[c] = isfinite(out[c]) ? out[c] : fallback[c];
    key_count = curve->input.count;
    if (curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0) {
        if (key_count > INT32_MAX / 3)
            key_count = curve->output.count / 3;
        else if (curve->output.count < key_count * 3)
            key_count = curve->output.count / 3;
    } else if (curve->output.count < key_count)
        key_count = curve->output.count;
    if (key_count <= 0)
        return;
    if (curve->interpolation && strcmp(curve->interpolation, "STEP") == 0) {
        int32_t upper_lo = 0;
        int32_t upper_hi = key_count;
        int32_t sample_index;
        while (upper_lo < upper_hi) {
            int32_t mid = upper_lo + (upper_hi - upper_lo) / 2;
            if (gltf_curve_time(&curve->input, mid) <= time)
                upper_lo = mid + 1;
            else
                upper_hi = mid;
        }
        sample_index = upper_lo - 1;
        if (sample_index < 0)
            sample_index = 0;
        if (sample_index >= key_count)
            sample_index = key_count - 1;
        gltf_curve_read_value(curve, sample_index, out, components);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
    if (time <= gltf_curve_time(&curve->input, 0)) {
        gltf_curve_read_value(curve, 0, out, components);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
    if (time >= gltf_curve_time(&curve->input, key_count - 1)) {
        gltf_curve_read_value(curve, key_count - 1, out, components);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
    lo = 0;
    hi = key_count - 1;
    while (hi - lo > 1) {
        int32_t mid = lo + (hi - lo) / 2;
        if (gltf_curve_time(&curve->input, mid) <= time)
            lo = mid;
        else
            hi = mid;
    }
    t0 = gltf_curve_time(&curve->input, lo);
    t1 = gltf_curve_time(&curve->input, hi);
    alpha = t1 > t0 ? (time - t0) / (t1 - t0) : 0.0;
    if (!isfinite(alpha) || alpha < 0.0)
        alpha = 0.0;
    if (alpha > 1.0)
        alpha = 1.0;
    {
        float a[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float b[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        gltf_curve_read_value(curve, lo, a, components);
        if (curve->interpolation && strcmp(curve->interpolation, "CUBICSPLINE") == 0) {
            float out_tangent0[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float in_tangent1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            double dt = t1 - t0;
            double t2 = alpha * alpha;
            double t3 = t2 * alpha;
            double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
            double h10 = t3 - 2.0 * t2 + alpha;
            double h01 = -2.0 * t3 + 3.0 * t2;
            double h11 = t3 - t2;
            if (lo > (INT32_MAX - 2) / 3 || hi > INT32_MAX / 3)
                return;
            gltf_curve_read_output_value(curve, lo * 3 + 2, out_tangent0, components);
            gltf_curve_read_output_value(curve, hi * 3, in_tangent1, components);
            gltf_curve_read_value(curve, hi, b, components);
            if (components == 4) {
                double dot;
                gltf_normalize_sample_if_quat(a, 4);
                gltf_normalize_sample_if_quat(b, 4);
                dot = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2] +
                      (double)a[3] * b[3];
                if (isfinite(dot) && dot < 0.0) {
                    for (int c = 0; c < 4; c++) {
                        b[c] = -b[c];
                        in_tangent1[c] = -in_tangent1[c];
                    }
                }
            }
            if (!isfinite(dt))
                dt = 0.0;
            for (int c = 0; c < components; c++) {
                out[c] = (float)(h00 * a[c] + h10 * dt * out_tangent0[c] + h01 * b[c] +
                                 h11 * dt * in_tangent1[c]);
            }
            gltf_sanitize_sample_or(out, fallback, components);
            return;
        }
        gltf_curve_read_value(curve, hi, b, components);
        if (components == 4) {
            gltf_slerp_quat(a, b, alpha, out);
            return;
        }
        for (int c = 0; c < components; c++)
            out[c] = (float)(a[c] + (b[c] - a[c]) * alpha);
        gltf_sanitize_sample_or(out, fallback, components);
        return;
    }
}

static int32_t gltf_skin_anim_path_index(const char *path) {
    if (!path)
        return -1;
    if (strcmp(path, "translation") == 0)
        return 0;
    if (strcmp(path, "rotation") == 0)
        return 1;
    if (strcmp(path, "scale") == 0)
        return 2;
    return -1;
}

static void gltf_skin_anim_resolve_curve(void *root,
                                         void *nodes_arr,
                                         void *samplers,
                                         void *channels,
                                         int32_t channel_index,
                                         gltf_buffer_t *buffers,
                                         int buf_count,
                                         const gltf_skin_t *skin,
                                         gltf_anim_curve_t *curves,
                                         double *duration_io) {
    void *channel = rt_seq_get(channels, channel_index);
    void *target = jget(channel, "target");
    const char *path = jstr(target, "path");
    int64_t sampler_idx = jint(channel, "sampler", -1);
    int64_t node_idx = jint(target, "node", -1);
    void *sampler;
    int32_t interpolation_mode;
    int32_t bone_idx;
    if (sampler_idx < 0 || sampler_idx >= jarr_len(samplers) || node_idx < 0 ||
        node_idx > INT32_MAX || node_idx >= jarr_len(nodes_arr))
        return;
    bone_idx = gltf_skin_bone_for_node(skin, (int32_t)node_idx);
    if (bone_idx < 0)
        return;
    sampler = rt_seq_get(samplers, sampler_idx);
    curves[channel_index].path = gltf_skin_anim_path_index(path);
    if (curves[channel_index].path < 0)
        return;
    if (gltf_anim_curve_already_targets(
            curves, channel_index, bone_idx, curves[channel_index].path))
        return;
    curves[channel_index].interpolation = jstr(sampler, "interpolation");
    interpolation_mode = gltf_animation_interpolation_mode(curves[channel_index].interpolation);
    if (interpolation_mode == GLTF_ANIM_INTERP_INVALID)
        return;
    if (!gltf_get_accessor_view(root,
                                jint(sampler, "input", -1),
                                buffers,
                                buf_count,
                                &curves[channel_index].input) ||
        !gltf_get_accessor_view(root,
                                jint(sampler, "output", -1),
                                buffers,
                                buf_count,
                                &curves[channel_index].output))
        return;
    if (curves[channel_index].input.count > GLTF_SKIN_ANIM_KEY_COUNT_MAX ||
        !gltf_animation_input_accessor_valid(&curves[channel_index].input, interpolation_mode))
        return;
    if (!gltf_animation_trs_output_accessor_valid(
            &curves[channel_index].input,
            &curves[channel_index].output,
            curves[channel_index].path == 1 ? 4 : 3,
            interpolation_mode == GLTF_ANIM_INTERP_CUBICSPLINE))
        return;
    curves[channel_index].valid = 1;
    curves[channel_index].node_idx = (int32_t)node_idx;
    curves[channel_index].bone_idx = bone_idx;
    for (int32_t ti = 0; ti < curves[channel_index].input.count; ti++) {
        double t = gltf_curve_time(&curves[channel_index].input, ti);
        if (t > *duration_io)
            *duration_io = t;
    }
}

static gltf_anim_curve_t *gltf_skin_anim_build_curves(void *root,
                                                     void *nodes_arr,
                                                     void *samplers,
                                                     void *channels,
                                                     int32_t channel_count,
                                                     gltf_buffer_t *buffers,
                                                     int buf_count,
                                                     const gltf_skin_t *skin,
                                                     double *duration_io) {
    gltf_anim_curve_t *curves = (gltf_anim_curve_t *)calloc((size_t)channel_count, sizeof(*curves));
    if (!curves)
        return NULL;
    for (int32_t ci = 0; ci < channel_count; ci++)
        gltf_skin_anim_resolve_curve(root,
                                     nodes_arr,
                                     samplers,
                                     channels,
                                     ci,
                                     buffers,
                                     buf_count,
                                     skin,
                                     curves,
                                     duration_io);
    return curves;
}

static int gltf_skin_anim_collect_bone_curves(const gltf_anim_curve_t *curves,
                                              int32_t channel_count,
                                              int32_t bone,
                                              double **out_times,
                                              int32_t *out_time_count,
                                              int32_t *out_node_idx,
                                              const gltf_anim_curve_t **out_curve_t,
                                              const gltf_anim_curve_t **out_curve_r,
                                              const gltf_anim_curve_t **out_curve_s) {
    double *times = NULL;
    int32_t time_count = 0;
    int32_t time_capacity = 0;
    int32_t node_idx = -1;
    const gltf_anim_curve_t *curve_t = NULL;
    const gltf_anim_curve_t *curve_r = NULL;
    const gltf_anim_curve_t *curve_s = NULL;
    for (int32_t ci = 0; ci < channel_count; ci++) {
        if (!curves[ci].valid || curves[ci].bone_idx != bone)
            continue;
        node_idx = curves[ci].node_idx;
        if (curves[ci].path == 0)
            curve_t = &curves[ci];
        else if (curves[ci].path == 1)
            curve_r = &curves[ci];
        else if (curves[ci].path == 2)
            curve_s = &curves[ci];
        if (!gltf_anim_insert_curve_times(&times, &time_count, &time_capacity, &curves[ci])) {
            free(times);
            return 0;
        }
    }
    if (time_count <= 0 || node_idx < 0) {
        free(times);
        return 0;
    }
    *out_times = times;
    *out_time_count = time_count;
    *out_node_idx = node_idx;
    *out_curve_t = curve_t;
    *out_curve_r = curve_r;
    *out_curve_s = curve_s;
    return 1;
}

static int gltf_skin_anim_emit_bone_samples(void *anim,
                                            void *nodes_arr,
                                            int32_t bone,
                                            int32_t node_idx,
                                            const gltf_anim_curve_t *curve_t,
                                            const gltf_anim_curve_t *curve_r,
                                            const gltf_anim_curve_t *curve_s,
                                            const double *times,
                                            int32_t time_count) {
    double local[16];
    double pos_d[3];
    double rot_d[4];
    double scl_d[3];
    int emitted = 0;
    if (!gltf_node_local_matrix(nodes_arr, node_idx, local))
        return 0;
    gltf_matrix_to_trs(local, pos_d, rot_d, scl_d);
    for (int32_t ti = 0; ti < time_count; ti++) {
        float pos[3] = {(float)pos_d[0], (float)pos_d[1], (float)pos_d[2]};
        float rot[4] = {(float)rot_d[0], (float)rot_d[1], (float)rot_d[2], (float)rot_d[3]};
        float scl[3] = {(float)scl_d[0], (float)scl_d[1], (float)scl_d[2]};
        void *pos_obj;
        void *rot_obj;
        void *scl_obj;
        gltf_sample_curve(curve_t, times[ti], pos, 3);
        gltf_sample_curve(curve_r, times[ti], rot, 4);
        gltf_sample_curve(curve_s, times[ti], scl, 3);
        pos_obj = rt_vec3_new(pos[0], pos[1], pos[2]);
        rot_obj = rt_quat_new(rot[0], rot[1], rot[2], rot[3]);
        scl_obj = rt_vec3_new(scl[0], scl[1], scl[2]);
        if (pos_obj && rot_obj && scl_obj) {
            rt_animation3d_add_keyframe(anim, bone, times[ti], pos_obj, rot_obj, scl_obj);
            emitted = 1;
        }
        gltf_release_ref(&pos_obj);
        gltf_release_ref(&rot_obj);
        gltf_release_ref(&scl_obj);
    }
    return emitted;
}

static int gltf_skin_anim_emit_bone_track(void *anim,
                                          void *nodes_arr,
                                          const gltf_anim_curve_t *curves,
                                          int32_t channel_count,
                                          int32_t bone) {
    double *times = NULL;
    int32_t time_count = 0;
    int32_t node_idx = -1;
    const gltf_anim_curve_t *curve_t = NULL;
    const gltf_anim_curve_t *curve_r = NULL;
    const gltf_anim_curve_t *curve_s = NULL;
    int emitted;
    if (!gltf_skin_anim_collect_bone_curves(curves,
                                            channel_count,
                                            bone,
                                            &times,
                                            &time_count,
                                            &node_idx,
                                            &curve_t,
                                            &curve_r,
                                            &curve_s))
        return 0;
    emitted = gltf_skin_anim_emit_bone_samples(
        anim, nodes_arr, bone, node_idx, curve_t, curve_r, curve_s, times, time_count);
    free(times);
    return emitted;
}

static int gltf_skin_anim_emit_clip_bones(void *anim,
                                          void *nodes_arr,
                                          const gltf_anim_curve_t *curves,
                                          int32_t channel_count,
                                          int32_t bone_limit) {
    int emitted_any = 0;
    for (int32_t bone = 0; bone < bone_limit; bone++) {
        if (gltf_skin_anim_emit_bone_track(anim, nodes_arr, curves, channel_count, bone))
            emitted_any = 1;
    }
    return emitted_any;
}

/// @brief Parse the glTF "animations" array into engine `Animation3D` objects.
/// @details glTF animation data is channel-oriented: each channel pairs a sampler
///          (keyframe data) with a target (node + property path: translation /
///          rotation / scale). The engine, in contrast, is bone-oriented: one
///          Animation3D holds per-bone keyframes where each keyframe records T/R/S
///          atomically.
///
///          This conversion works in two passes per glTF animation:
///            1. Build a `curves[]` table where each entry resolves (node, path,
///               sampler data, interpolation mode). Unknown paths, missing accessors,
///               or non-skinned targets are dropped here. `duration` is accumulated
///               as the max input-time across all valid channels.
///            2. For each engine bone in [0, VGFX3D_MAX_BONES):
///                 - Collect every time stamp from every curve targeting that bone,
///                   deduplicated and sorted by `gltf_anim_insert_time`.
///                 - Seed T/R/S from the node's bind pose so bones without a channel
///                   keep their rest value.
///                 - At each time stamp sample all three curves and emit a merged
///                   keyframe. Missing curves inherit the bind-pose seed.
///                 - Release the Vec3/Quat temporaries immediately (the animation
///                   retains its own references internally).
///          Animations that produce no keyframes (e.g. all targets missed the skin)
///          are released rather than filed — a NULL-keyframe animation would just
///          waste a slot.
///
///          Animations are flagged looping by default; callers can override via
///          `rt_animation3d_set_looping` after load.
/// @param asset Receiver for the populated `animations` array.
/// @param root glTF JSON root.
/// @param buffers Parsed buffer table used by `gltf_get_accessor_view`.
/// @param buf_count Length of `buffers`.
/// @param skins Scratch skins produced by `gltf_parse_skins`, used to resolve
///              (node → bone index) without rebuilding a lookup.
/// @param skin_count Length of `skins`.
static void gltf_parse_animations(rt_gltf_asset *asset,
                                  void *root,
                                  gltf_buffer_t *buffers,
                                  int buf_count,
                                  const gltf_skin_t *skins,
                                  int32_t skin_count) {
    void *anims_arr = jarr(root, "animations");
    void *nodes_arr = jarr(root, "nodes");
    int64_t anim_count64 = jarr_len(anims_arr);
    int32_t anim_count;
    if (!asset || !anims_arr || anim_count64 <= 0 || !skins || skin_count <= 0)
        return;
    if (anim_count64 <= 0 || anim_count64 > INT32_MAX)
        return;
    anim_count = (int32_t)anim_count64;
    if ((int64_t)anim_count * (int64_t)skin_count > INT32_MAX ||
        (size_t)anim_count > SIZE_MAX / (size_t)skin_count ||
        (size_t)anim_count * (size_t)skin_count > SIZE_MAX / sizeof(void *))
        return;
    asset->animations = (void **)calloc((size_t)anim_count * (size_t)skin_count, sizeof(void *));
    asset->animation_capacity = asset->animations ? anim_count * skin_count : 0;
    if (!asset->animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int channel_count_tmp = 0;
        int32_t channel_count;
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        if (!gltf_jarr_len_i32(channels, &channel_count_tmp))
            continue;
        channel_count = (int32_t)channel_count_tmp;
        if (channel_count <= 0 || !samplers)
            continue;
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "animation_%d", (int)ai);
            name = generated_name;
        }
        for (int32_t si = 0; si < skin_count; si++) {
            rt_skeleton3d *skin_skel = (rt_skeleton3d *)skins[si].skeleton;
            int32_t bone_limit;
            gltf_anim_curve_t *curves;
            double duration = 0.0;
            int emitted_any = 0;
            void *anim;
            char skin_name[128];
            const char *clip_name = name;
            if (!skin_skel || !skins[si].joint_to_bone ||
                skeleton3d_safe_bone_count(skin_skel) <= 0)
                continue;
            bone_limit = skeleton3d_safe_bone_count(skin_skel);
            curves = gltf_skin_anim_build_curves(root,
                                                 nodes_arr,
                                                 samplers,
                                                 channels,
                                                 channel_count,
                                                 buffers,
                                                 buf_count,
                                                 &skins[si],
                                                 &duration);
            if (!curves)
                continue;
            if (skin_count > 1) {
                snprintf(skin_name, sizeof(skin_name), "%s_skin_%d", name, (int)si);
                clip_name = skin_name;
            }
            anim = rt_animation3d_new(rt_const_cstr(clip_name), duration > 0.0 ? duration : 1.0);
            if (!anim) {
                free(curves);
                continue;
            }
            emitted_any = gltf_skin_anim_emit_clip_bones(
                anim, nodes_arr, curves, channel_count, bone_limit);
            free(curves);
            if (!emitted_any) {
                gltf_release_ref(&anim);
                continue;
            }
            rt_animation3d_set_looping(anim, 1);
            if (asset->animation_count < 0 ||
                asset->animation_count >= asset->animation_capacity) {
                gltf_release_ref(&anim);
                continue;
            }
            asset->animations[asset->animation_count++] = anim;
        }
    }
}

/// @brief Return non-zero if @p node_idx is a joint in any of the @p skin_count skins.
/// @details Used during scene-graph construction to decide whether a node should be excluded
///          from the scene hierarchy (skin joints are already represented inside the skeleton).
static int gltf_node_is_skin_joint(const gltf_skin_t *skins, int32_t skin_count, int32_t node_idx) {
    if (!skins || skin_count <= 0)
        return 0;
    for (int32_t si = 0; si < skin_count; si++) {
        if (gltf_skin_bone_for_node(&skins[si], node_idx) >= 0)
            return 1;
    }
    return 0;
}

/// @brief Map a glTF animation channel path string to the engine's RT_NODE_ANIM_PATH_* constant.
/// @return RT_NODE_ANIM_PATH_TRANSLATION / ROTATION / SCALE / WEIGHTS, or -1 for unknown paths.
static int32_t gltf_node_anim_path(const char *path) {
    if (!path)
        return -1;
    if (strcmp(path, "translation") == 0)
        return RT_NODE_ANIM_PATH_TRANSLATION;
    if (strcmp(path, "rotation") == 0)
        return RT_NODE_ANIM_PATH_ROTATION;
    if (strcmp(path, "scale") == 0)
        return RT_NODE_ANIM_PATH_SCALE;
    if (strcmp(path, "weights") == 0)
        return RT_NODE_ANIM_PATH_WEIGHTS;
    return -1;
}

/// @brief Return the morph-target count addressed by a node weights animation.
/// @details glTF `weights` animation output width must equal the target mesh's morph-target count.
///          The importer represents multi-primitive meshes as a parent node plus child primitive
///          nodes, then applies animated weights to the subtree, so every morphed primitive with
///          targets must agree on the same count.
static int32_t gltf_node_weights_target_count(void *root, int32_t node_idx) {
    void *nodes_arr = jarr(root, "nodes");
    void *meshes_arr = jarr(root, "meshes");
    void *node_json;
    void *mesh_json;
    void *primitives;
    void *mesh_weights;
    int64_t node_count = jarr_len(nodes_arr);
    int64_t mesh_count = jarr_len(meshes_arr);
    int64_t mesh_ref;
    int64_t primitive_count;
    int32_t target_count = -1;
    if (!root || node_idx < 0 || node_idx >= node_count)
        return 0;
    node_json = rt_seq_get(nodes_arr, node_idx);
    mesh_ref = jint(node_json, "mesh", -1);
    if (mesh_ref < 0 || mesh_ref >= mesh_count)
        return 0;
    mesh_json = rt_seq_get(meshes_arr, mesh_ref);
    primitives = jarr(mesh_json, "primitives");
    primitive_count = jarr_len(primitives);
    for (int64_t pi = 0; pi < primitive_count; pi++) {
        void *targets = jarr(rt_seq_get(primitives, pi), "targets");
        int64_t count = jarr_len(targets);
        if (count <= 0)
            continue;
        if (count > GLTF_NODE_ANIM_VALUE_WIDTH_MAX)
            return 0;
        if (target_count < 0)
            target_count = (int32_t)count;
        else if (target_count != (int32_t)count)
            return 0;
    }
    if (target_count <= 0)
        return 0;
    mesh_weights = jarr(mesh_json, "weights");
    if (mesh_weights) {
        int64_t weight_count = jarr_len(mesh_weights);
        if (weight_count > 0 && weight_count != target_count)
            return 0;
    }
    return target_count;
}

/// @brief Determine how many float components are stored per keyframe for a given animation path.
/// @details For TRANSLATION/SCALE the width is 3 (XYZ); for ROTATION it is 4 (XYZW quaternion).
///          For the WEIGHTS path the width is derived by dividing the total output components by
///          the number of input keyframes (× 3 for CUBICSPLINE). Returns 0 for degenerate data.
static int32_t gltf_node_anim_width_for_path(int32_t path,
                                             const gltf_accessor_view_t *input,
                                             const gltf_accessor_view_t *output,
                                             int32_t interpolation_mode,
                                             int cubic) {
    int64_t total_components;
    int64_t divisor;
    if (!gltf_animation_input_accessor_valid(input, interpolation_mode) || !output ||
        output->count <= 0 || output->comp_type != 5126)
        return 0;
    if (path == RT_NODE_ANIM_PATH_TRANSLATION || path == RT_NODE_ANIM_PATH_SCALE) {
        return gltf_animation_trs_output_accessor_valid(input, output, 3, cubic) ? 3 : 0;
    }
    if (path == RT_NODE_ANIM_PATH_ROTATION) {
        return gltf_animation_trs_output_accessor_valid(input, output, 4, cubic) ? 4 : 0;
    }
    total_components = (int64_t)output->count * (int64_t)output->comp_count;
    divisor = (int64_t)input->count * (cubic ? 3 : 1);
    if (divisor <= 0 || total_components <= 0 || total_components % divisor != 0)
        return 0;
    if (total_components / divisor > INT32_MAX)
        return 0;
    if (total_components / divisor > GLTF_NODE_ANIM_VALUE_WIDTH_MAX)
        return 0;
    if (!gltf_accessor_f32_values_are_finite(output, output->count, output->comp_count))
        return 0;
    return (int32_t)(total_components / divisor);
}

typedef struct {
    int32_t path;
    int32_t node_idx;
    const char *interpolation;
    int32_t interpolation_mode;
    int cubic;
    gltf_accessor_view_t input;
    gltf_accessor_view_t output;
    int32_t width;
    int64_t value_count;
} gltf_node_anim_channel_t;

static int gltf_node_anim_resolve_channel(void *root,
                                          void *nodes_arr,
                                          void *samplers,
                                          void *channel,
                                          gltf_buffer_t *buffers,
                                          int buf_count,
                                          const gltf_skin_t *skins,
                                          int32_t skin_count,
                                          const int32_t *seen_nodes,
                                          const int32_t *seen_paths,
                                          int32_t seen_count,
                                          gltf_node_anim_channel_t *out) {
    void *target = jget(channel, "target");
    const char *path_str = jstr(target, "path");
    int32_t path = gltf_node_anim_path(path_str);
    int64_t sampler_idx = jint(channel, "sampler", -1);
    int64_t node_idx = jint(target, "node", -1);
    void *sampler;
    int32_t interpolation_mode;
    int cubic;
    gltf_accessor_view_t input;
    gltf_accessor_view_t output;
    int32_t width;
    int32_t expected_weight_width = 0;
    int64_t value_count;

    memset(out, 0, sizeof(*out));
    if (path < 0 || sampler_idx < 0 || sampler_idx >= jarr_len(samplers) || node_idx < 0 ||
        node_idx > INT32_MAX || node_idx >= jarr_len(nodes_arr))
        return 0;
    if (path != RT_NODE_ANIM_PATH_WEIGHTS &&
        gltf_node_is_skin_joint(skins, skin_count, (int32_t)node_idx))
        return 0;
    if (path == RT_NODE_ANIM_PATH_WEIGHTS) {
        expected_weight_width = gltf_node_weights_target_count(root, (int32_t)node_idx);
        if (expected_weight_width <= 0)
            return 0;
    }
    if (gltf_node_anim_channel_seen(
            seen_nodes, seen_paths, seen_count, (int32_t)node_idx, path))
        return 0;
    sampler = rt_seq_get(samplers, sampler_idx);
    out->interpolation = jstr(sampler, "interpolation");
    interpolation_mode = gltf_animation_interpolation_mode(out->interpolation);
    if (interpolation_mode == GLTF_ANIM_INTERP_INVALID)
        return 0;
    cubic = interpolation_mode == GLTF_ANIM_INTERP_CUBICSPLINE;
    if (!gltf_get_accessor_view(root, jint(sampler, "input", -1), buffers, buf_count, &input) ||
        !gltf_get_accessor_view(root, jint(sampler, "output", -1), buffers, buf_count, &output))
        return 0;
    if (input.count <= 0 || input.count > GLTF_NODE_ANIM_KEY_COUNT_MAX ||
        !gltf_animation_input_accessor_valid(&input, interpolation_mode))
        return 0;
    width = gltf_node_anim_width_for_path(path, &input, &output, interpolation_mode, cubic);
    if (width <= 0)
        return 0;
    if (path == RT_NODE_ANIM_PATH_WEIGHTS && width != expected_weight_width)
        return 0;
    if (width > GLTF_NODE_ANIM_VALUE_WIDTH_MAX)
        return 0;
    value_count = (int64_t)input.count * (int64_t)width;
    if (value_count <= 0 || value_count > INT32_MAX ||
        value_count > GLTF_NODE_ANIM_VALUE_COUNT_MAX)
        return 0;
    if (cubic && value_count > INT32_MAX / 3)
        return 0;
    if ((size_t)input.count > SIZE_MAX / sizeof(double) ||
        (size_t)value_count > SIZE_MAX / sizeof(float))
        return 0;
    out->path = path;
    out->node_idx = (int32_t)node_idx;
    out->interpolation_mode = interpolation_mode;
    out->cubic = cubic;
    out->input = input;
    out->output = output;
    out->width = width;
    out->value_count = value_count;
    return 1;
}

static void gltf_node_anim_free_samples(double *times,
                                        float *values,
                                        float *in_tangents,
                                        float *out_tangents) {
    free(times);
    free(values);
    free(in_tangents);
    free(out_tangents);
}

static int gltf_node_anim_alloc_samples(const gltf_node_anim_channel_t *channel,
                                        double **out_times,
                                        float **out_values,
                                        float **out_in_tangents,
                                        float **out_out_tangents) {
    double *times = NULL;
    float *values = NULL;
    float *in_tangents = NULL;
    float *out_tangents = NULL;
    *out_times = NULL;
    *out_values = NULL;
    *out_in_tangents = NULL;
    *out_out_tangents = NULL;
    times = (double *)malloc((size_t)channel->input.count * sizeof(double));
    values = (float *)malloc((size_t)channel->value_count * sizeof(float));
    if (channel->cubic) {
        in_tangents = (float *)malloc((size_t)channel->value_count * sizeof(float));
        out_tangents = (float *)malloc((size_t)channel->value_count * sizeof(float));
    }
    if (!times || !values || (channel->cubic && (!in_tangents || !out_tangents))) {
        gltf_node_anim_free_samples(times, values, in_tangents, out_tangents);
        return 0;
    }
    *out_times = times;
    *out_values = values;
    *out_in_tangents = in_tangents;
    *out_out_tangents = out_tangents;
    return 1;
}

static void gltf_node_anim_fill_weight_key(const gltf_node_anim_channel_t *channel,
                                           int32_t key_index,
                                           float *values,
                                           float *in_tangents,
                                           float *out_tangents) {
    int32_t width = channel->width;
    int32_t in_base = channel->cubic ? (key_index * 3) * width : key_index * width;
    int32_t base = channel->cubic ? (key_index * 3 + 1) * width : key_index * width;
    int32_t out_base = channel->cubic ? (key_index * 3 + 2) * width : key_index * width;
    for (int32_t wi = 0; wi < width; wi++)
        values[(size_t)key_index * (size_t)width + (size_t)wi] =
            gltf_accessor_read_flat_f32(&channel->output, base + wi);
    if (channel->cubic) {
        for (int32_t wi = 0; wi < width; wi++) {
            in_tangents[(size_t)key_index * (size_t)width + (size_t)wi] =
                gltf_accessor_read_flat_f32(&channel->output, in_base + wi);
            out_tangents[(size_t)key_index * (size_t)width + (size_t)wi] =
                gltf_accessor_read_flat_f32(&channel->output, out_base + wi);
        }
    }
}

static void gltf_node_anim_fill_trs_key(const gltf_node_anim_channel_t *channel,
                                        int32_t key_index,
                                        int32_t source_key,
                                        float *values,
                                        float *in_tangents,
                                        float *out_tangents) {
    float tmp[4] = {
        0.0f, 0.0f, 0.0f, channel->path == RT_NODE_ANIM_PATH_ROTATION ? 1.0f : 0.0f};
    gltf_accessor_read_f32(&channel->output, source_key, tmp, channel->width);
    if (channel->path == RT_NODE_ANIM_PATH_ROTATION)
        gltf_normalize_sample_if_quat(tmp, 4);
    memcpy(&values[(size_t)key_index * (size_t)channel->width],
           tmp,
           (size_t)channel->width * sizeof(float));
    if (channel->cubic) {
        float in_tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float out_tmp[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        gltf_accessor_read_f32(&channel->output, key_index * 3, in_tmp, channel->width);
        gltf_accessor_read_f32(&channel->output, key_index * 3 + 2, out_tmp, channel->width);
        memcpy(&in_tangents[(size_t)key_index * (size_t)channel->width],
               in_tmp,
               (size_t)channel->width * sizeof(float));
        memcpy(&out_tangents[(size_t)key_index * (size_t)channel->width],
               out_tmp,
               (size_t)channel->width * sizeof(float));
    }
}

static void gltf_node_anim_fill_samples(const gltf_node_anim_channel_t *channel,
                                        double *times,
                                        float *values,
                                        float *in_tangents,
                                        float *out_tangents,
                                        double *duration_io) {
    for (int32_t ki = 0; ki < channel->input.count; ki++) {
        int32_t source_key = channel->cubic ? (ki <= (INT32_MAX - 1) / 3 ? ki * 3 + 1 : -1) : ki;
        /* Initialize the time sample before any early-continue so a skipped
         * cubic key never leaves an uninitialized slot, and coerce a non-finite
         * curve time to 0 so it can't poison the clip duration or interpolation. */
        double tval = gltf_curve_time(&channel->input, ki);
        times[ki] = isfinite(tval) ? tval : 0.0;
        if (source_key < 0)
            continue;
        if (times[ki] > *duration_io)
            *duration_io = times[ki];
        if (channel->path == RT_NODE_ANIM_PATH_WEIGHTS)
            gltf_node_anim_fill_weight_key(channel, ki, values, in_tangents, out_tangents);
        else
            gltf_node_anim_fill_trs_key(
                channel, ki, source_key, values, in_tangents, out_tangents);
    }
}

static int gltf_node_anim_emit_channel(void *node_anim,
                                       void *nodes_arr,
                                       const gltf_node_anim_channel_t *channel,
                                       double *times,
                                       float *values,
                                       float *in_tangents,
                                       float *out_tangents,
                                       int32_t *seen_nodes,
                                       int32_t *seen_paths,
                                       int32_t *seen_count,
                                       int32_t channel_count) {
    char fallback_name[64];
    const char *target_name =
        gltf_effective_node_name(nodes_arr, channel->node_idx, fallback_name, sizeof(fallback_name));
    int64_t channel_index;
    if (!target_name)
        return 0;
    if (channel->cubic) {
        channel_index = rt_node_animation3d_add_cubic_channel(node_anim,
                                                              rt_const_cstr(target_name),
                                                              channel->path,
                                                              channel->input.count,
                                                              channel->width,
                                                              times,
                                                              values,
                                                              in_tangents,
                                                              out_tangents);
    } else {
        channel_index = rt_node_animation3d_add_channel(
            node_anim,
            rt_const_cstr(target_name),
            channel->path,
            channel->interpolation_mode == GLTF_ANIM_INTERP_STEP ? RT_NODE_ANIM_INTERP_STEP
                                                                 : RT_NODE_ANIM_INTERP_LINEAR,
            channel->input.count,
            channel->width,
            times,
            values);
    }
    if (channel_index < 0)
        return 0;
    rt_node_animation3d_set_channel_target_node_index(node_anim, channel_index, channel->node_idx);
    if (*seen_count < channel_count) {
        seen_nodes[*seen_count] = channel->node_idx;
        seen_paths[*seen_count] = channel->path;
        (*seen_count)++;
    }
    return 1;
}

static int gltf_node_anim_process_channel(void *node_anim,
                                          void *root,
                                          void *nodes_arr,
                                          void *samplers,
                                          void *channel_json,
                                          gltf_buffer_t *buffers,
                                          int buf_count,
                                          const gltf_skin_t *skins,
                                          int32_t skin_count,
                                          int32_t *seen_nodes,
                                          int32_t *seen_paths,
                                          int32_t *seen_count,
                                          int32_t channel_count,
                                          double *duration_io) {
    gltf_node_anim_channel_t channel;
    double *times = NULL;
    float *values = NULL;
    float *in_tangents = NULL;
    float *out_tangents = NULL;
    int emitted;
    if (!gltf_node_anim_resolve_channel(root,
                                        nodes_arr,
                                        samplers,
                                        channel_json,
                                        buffers,
                                        buf_count,
                                        skins,
                                        skin_count,
                                        seen_nodes,
                                        seen_paths,
                                        *seen_count,
                                        &channel))
        return 0;
    if (!gltf_node_anim_alloc_samples(&channel, &times, &values, &in_tangents, &out_tangents))
        return 0;
    gltf_node_anim_fill_samples(&channel, times, values, in_tangents, out_tangents, duration_io);
    emitted = gltf_node_anim_emit_channel(node_anim,
                                          nodes_arr,
                                          &channel,
                                          times,
                                          values,
                                          in_tangents,
                                          out_tangents,
                                          seen_nodes,
                                          seen_paths,
                                          seen_count,
                                          channel_count);
    gltf_node_anim_free_samples(times, values, in_tangents, out_tangents);
    return emitted;
}

/// @brief Parse all glTF node animations and attach them to the asset.
/// @details Iterates the `animations` array, builds one `rt_node_animation3d` per animation,
///          then for each channel resolves the sampler, maps the target node to a bone (skin
///          joint) or scene node index, unions the time range across all channels to compute
///          the animation duration, and emits per-track keyframe data. Animations with no
///          valid channels are skipped. Named or auto-named (`"node_animation_N"`) animations
///          are stored in `asset->node_animations` for retrieval by the caller.
static void gltf_parse_node_animations(rt_gltf_asset *asset,
                                       void *root,
                                       gltf_buffer_t *buffers,
                                       int buf_count,
                                       const gltf_skin_t *skins,
                                       int32_t skin_count) {
    void *anims_arr = jarr(root, "animations");
    void *nodes_arr = jarr(root, "nodes");
    int64_t anim_count64 = jarr_len(anims_arr);
    int32_t anim_count;
    if (!asset || !anims_arr || anim_count64 <= 0 || anim_count64 > INT32_MAX)
        return;
    anim_count = (int32_t)anim_count64;
    asset->node_animations = (void **)calloc((size_t)anim_count, sizeof(void *));
    asset->node_animation_capacity = asset->node_animations ? anim_count : 0;
    if (!asset->node_animations)
        return;
    for (int32_t ai = 0; ai < anim_count; ai++) {
        void *anim_json = rt_seq_get(anims_arr, ai);
        void *channels = jarr(anim_json, "channels");
        void *samplers = jarr(anim_json, "samplers");
        int channel_count_tmp = 0;
        int32_t channel_count;
        const char *name = jstr(anim_json, "name");
        char generated_name[64];
        void *node_anim;
        int32_t *seen_nodes;
        int32_t *seen_paths;
        int32_t seen_count = 0;
        double duration = 0.0;
        int emitted_any = 0;
        if (!gltf_jarr_len_i32(channels, &channel_count_tmp))
            continue;
        channel_count = (int32_t)channel_count_tmp;
        if (channel_count <= 0 || !samplers)
            continue;
        if (!name || name[0] == '\0') {
            snprintf(generated_name, sizeof(generated_name), "node_animation_%d", (int)ai);
            name = generated_name;
        }
        node_anim = rt_node_animation3d_new(rt_const_cstr(name), 1.0);
        if (!node_anim)
            continue;
        if ((size_t)channel_count > SIZE_MAX / sizeof(*seen_nodes) ||
            (size_t)channel_count > SIZE_MAX / sizeof(*seen_paths)) {
            gltf_release_ref(&node_anim);
            continue;
        }
        seen_nodes = (int32_t *)calloc((size_t)channel_count, sizeof(*seen_nodes));
        seen_paths = (int32_t *)calloc((size_t)channel_count, sizeof(*seen_paths));
        if (!seen_nodes || !seen_paths) {
            free(seen_nodes);
            free(seen_paths);
            gltf_release_ref(&node_anim);
            continue;
        }
        for (int32_t ci = 0; ci < channel_count; ci++) {
            if (gltf_node_anim_process_channel(node_anim,
                                               root,
                                               nodes_arr,
                                               samplers,
                                               rt_seq_get(channels, ci),
                                               buffers,
                                               buf_count,
                                               skins,
                                               skin_count,
                                               seen_nodes,
                                               seen_paths,
                                               &seen_count,
                                               channel_count,
                                               &duration))
                emitted_any = 1;
        }
        free(seen_nodes);
        free(seen_paths);
        ((rt_node_animation3d *)node_anim)->duration = duration > 0.0 ? duration : 1.0;
        if (!emitted_any) {
            gltf_release_ref(&node_anim);
            continue;
        }
        if (asset->node_animation_count < 0 ||
            asset->node_animation_count >= asset->node_animation_capacity) {
            gltf_release_ref(&node_anim);
            continue;
        }
        asset->node_animations[asset->node_animation_count++] = node_anim;
    }
}
