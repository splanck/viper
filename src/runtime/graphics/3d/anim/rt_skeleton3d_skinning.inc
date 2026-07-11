//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/anim/rt_skeleton3d_skinning.inc  (#included from rt_skeleton3d.c;
// not a standalone translation unit) Purpose: Mesh3D skinning extensions and Canvas3D
// DrawMeshSkinned
//===----------------------------------------------------------------------===//
/*==========================================================================
 * Mesh3D extensions
 *=========================================================================*/

/// @brief Recompute the highest positively weighted bone referenced by a mesh.
static int32_t mesh3d_recompute_weight_bone_count(const rt_mesh3d *m) {
    int32_t max_bone = -1;
    uint32_t vertex_count = rt_mesh3d_safe_vertex_count(m);
    if (!m || !m->vertices)
        return 0;
    for (uint32_t vi = 0; vi < vertex_count; vi++) {
        const vgfx3d_vertex_t *v = &m->vertices[vi];
        for (int i = 0; i < 4; i++) {
            if (v->bone_weights[i] > 0.0f && v->bone_indices[i] > max_bone)
                max_bone = v->bone_indices[i];
        }
    }
    return max_bone >= 0 ? max_bone + 1 : 0;
}

/// @brief Bind a skeleton to a mesh so its per-vertex `bone_indices/weights` can drive skinning.
void rt_mesh3d_set_skeleton(void *mesh, void *skeleton) {
    rt_mesh3d *m = (rt_mesh3d *)rt_g3d_checked_or_null(mesh, RT_G3D_MESH3D_CLASS_ID);
    rt_skeleton3d *s =
        (rt_skeleton3d *)rt_g3d_checked_or_null(skeleton, RT_G3D_SKELETON3D_CLASS_ID);
    if (!m)
        return;
    if (m->skeleton_ref && !rt_g3d_has_class(m->skeleton_ref, RT_G3D_SKELETON3D_CLASS_ID))
        mesh3d_release_skeleton_slot(&m->skeleton_ref);
    if (skeleton && !s)
        return;
    mesh3d_assign_skeleton_ref(&m->skeleton_ref, s);
    if (s) {
        skeleton3d_freeze(s);
        /* Partitioned sub-meshes (bone_map set) keep their local palette size;
         * their vertices index the map, not the skeleton. */
        if (!m->bone_map)
            m->bone_count = s->bone_count > VGFX3D_MAX_BONES ? VGFX3D_MAX_BONES : s->bone_count;
    } else {
        m->bone_count = 0;
    }
    rt_mesh3d_touch_geometry(m);
}

/// @brief Attach the per-vertex bone influence data (4 indices + 4 weights per vertex) to a mesh.
///
/// Required for any skinned draw call. The arrays are referenced
/// (not copied), so the caller must keep them alive for the
/// lifetime of the mesh.
void rt_mesh3d_set_bone_weights(void *obj,
                                int64_t vertex_index,
                                int64_t b0,
                                double w0,
                                int64_t b1,
                                double w1,
                                int64_t b2,
                                double w2,
                                int64_t b3,
                                double w3) {
    rt_mesh3d *m = (rt_mesh3d *)rt_g3d_checked_or_null(obj, RT_G3D_MESH3D_CLASS_ID);
    if (!m)
        return;
    rt_mesh3d_repair_geometry_counts(m);
    if (vertex_index < 0 || vertex_index >= m->vertex_count)
        return;

    // Bone indices are stored as uint8_t. A raw int64 -> uint8 cast silently
    // wraps values outside [0, 255] to bogus valid-looking indices, then the
    // skinning palette applies the wrong transform. Preserve valid authored
    // indices even when their weight is zero so importers can round-trip JOINTS
    // attributes, but only positive finite weights contribute to deformation.
    int64_t idx[4] = {b0, b1, b2, b3};
    double wt[4] = {w0, w1, w2, w3};
    vgfx3d_vertex_t *v = &m->vertices[vertex_index];
    double sum = 0.0;
    int64_t max_bone_index = -1;
    int64_t old_max_bone_index = -1;
    for (int i = 0; i < 4; i++) {
        if (v->bone_weights[i] > 0.0f && v->bone_indices[i] > old_max_bone_index)
            old_max_bone_index = v->bone_indices[i];
    }
    for (int i = 0; i < 4; i++) {
        if (idx[i] < 0 || idx[i] >= VGFX3D_MAX_BONES) {
            v->bone_indices[i] = 0;
            v->bone_weights[i] = 0.0f;
        } else {
            v->bone_indices[i] = (uint8_t)idx[i];
            if (isfinite(wt[i]) && wt[i] > 0.0) {
                v->bone_weights[i] = (float)wt[i];
                sum += wt[i];
                if (idx[i] > max_bone_index)
                    max_bone_index = idx[i];
            } else {
                v->bone_weights[i] = 0.0f;
            }
        }
    }
    if (sum > 1e-12) {
        for (int i = 0; i < 4; i++)
            v->bone_weights[i] = (float)((double)v->bone_weights[i] / sum);
    } else {
        for (int i = 0; i < 4; i++)
            v->bone_weights[i] = 0.0f;
    }
    if (max_bone_index >= 0 && max_bone_index + 1 > m->bone_count)
        m->bone_count = (int32_t)(max_bone_index + 1);
    else if (!m->skeleton_ref && old_max_bone_index >= 0 &&
             old_max_bone_index + 1 >= m->bone_count &&
             (max_bone_index < 0 || max_bone_index + 1 < m->bone_count))
        m->bone_count = mesh3d_recompute_weight_bone_count(m);
    rt_mesh3d_touch_geometry(m);
}

/*==========================================================================
 * Canvas3D extension — DrawMeshSkinned
 *=========================================================================*/

/// @brief Queue a skinned mesh draw with per-frame motion-vector continuity.
/// @details Chooses between GPU and CPU skinning at the start:
///   `vgfx3d_backend_prefers_gpu_skinning` asks the backend whether it wants
///   to consume the palette directly (for D3D11/OpenGL with enough uniform
///   space); if yes, we hand the palettes through to the normal draw path
///   via a shallow-copy `rt_mesh3d` so the backend's vertex shader does the
///   matrix blend. Otherwise we `vgfx3d_skin_vertices` on the CPU into a
///   scratch buffer, tracked as a temp allocation so it survives until the
///   frame is submitted and flushed. `motion_key` threads through for
///   temporal effects (TAA, motion blur) so the engine can correlate this
///   draw with the matching one in the previous frame. `prev_bone_palette`
///   supplies the previous-frame palette for per-vertex motion vectors —
///   without it, skinned motion blur falls back to object-space smearing.
void rt_canvas3d_draw_mesh_matrix_skinned_keyed_bounds(void *canvas,
                                                       void *mesh_obj,
                                                       const double *model_matrix,
                                                       void *material,
                                                       const void *motion_key,
                                                       const float *bone_palette,
                                                       const float *prev_bone_palette,
                                                       int32_t bone_count,
                                                       const float *local_bounds_min,
                                                       const float *local_bounds_max,
                                                       int8_t conservative_bounds,
                                                       int8_t disable_occlusion) {
    rt_mesh3d *mesh;
    rt_canvas3d *c;

    if (!canvas || !mesh_obj || !model_matrix || !material || !bone_palette || bone_count <= 0)
        return;
    mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
#if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (!mesh && mesh_obj && !rt_heap_is_payload(mesh_obj))
        mesh = (rt_mesh3d *)mesh_obj;
#endif
    if (!mesh)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    if (mesh->vertex_count == 0)
        return;

    c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    if (rt_heap_is_payload(mesh_obj) && !rt_canvas3d_add_temp_object(canvas, mesh_obj))
        return;
    if (mesh->bone_map && mesh->bone_count > 0) {
        /* Partitioned sub-mesh: gather the skeleton-wide palette (possibly larger
         * than VGFX3D_MAX_BONES) down to the slots this sub-mesh references. The
         * gathered copies live in frame-owned temp buffers so deferred draws
         * never observe a later animation tick. */
        int32_t local_count =
            mesh->bone_count > VGFX3D_MAX_BONES ? VGFX3D_MAX_BONES : mesh->bone_count;
        size_t lanes = (size_t)local_count * 16u;
        float *gathered = (float *)malloc(lanes * sizeof(float));
        float *gathered_prev = prev_bone_palette ? (float *)malloc(lanes * sizeof(float)) : NULL;
        if (!gathered || !rt_canvas3d_add_temp_buffer(canvas, gathered)) {
            free(gathered);
            free(gathered_prev);
            return;
        }
        if (gathered_prev && !rt_canvas3d_add_temp_buffer(canvas, gathered_prev)) {
            free(gathered_prev);
            gathered_prev = NULL;
        }
        for (int32_t i = 0; i < local_count; i++) {
            int32_t src = mesh->bone_map[i];
            if (src < 0 || src >= bone_count)
                src = 0;
            memcpy(
                gathered + (size_t)i * 16u, bone_palette + (size_t)src * 16u, 16u * sizeof(float));
            if (gathered_prev)
                memcpy(gathered_prev + (size_t)i * 16u,
                       prev_bone_palette + (size_t)src * 16u,
                       16u * sizeof(float));
        }
        bone_palette = gathered;
        prev_bone_palette = gathered_prev;
        bone_count = local_count;
    }
    if (bone_count > VGFX3D_MAX_BONES)
        bone_count = VGFX3D_MAX_BONES;
    if (c->backend && !c->force_cpu_skinning && !mesh->extra_influences &&
        vgfx3d_backend_prefers_gpu_skinning(c->backend->name, bone_count)) {
        rt_mesh3d tmp = *mesh;
        tmp.bone_palette = bone_palette;
        tmp.prev_bone_palette = prev_bone_palette;
        tmp.bone_count = bone_count;
        c->gpu_skinned_draw_count++;
        c->skinning_upload_bytes +=
            (int64_t)bone_count * 16 * (int64_t)sizeof(float) * (prev_bone_palette ? 2 : 1);
        rt_canvas3d_draw_mesh_matrix_keyed_bounds(canvas,
                                                  &tmp,
                                                  model_matrix,
                                                  material,
                                                  motion_key,
                                                  prev_bone_palette,
                                                  NULL,
                                                  local_bounds_min,
                                                  local_bounds_max,
                                                  conservative_bounds,
                                                  disable_occlusion,
                                                  0.0f);
        return;
    }

    if ((size_t)mesh->vertex_count > SIZE_MAX / sizeof(vgfx3d_vertex_t))
        return;
    vgfx3d_vertex_t *skinned =
        (vgfx3d_vertex_t *)malloc((size_t)mesh->vertex_count * sizeof(vgfx3d_vertex_t));
    if (!skinned)
        return;

    vgfx3d_skin_vertices_extra(mesh->vertices,
                               skinned,
                               mesh->vertex_count,
                               bone_palette,
                               bone_count,
                               mesh->extra_influences,
                               &c->skinning_scratch);

    rt_mesh3d tmp = *mesh;
    tmp.vertices = skinned;
    tmp.positions64 = NULL;
    tmp.bone_palette = NULL;
    tmp.prev_bone_palette = NULL;
    tmp.bone_count = 0;
    if (!rt_canvas3d_add_temp_buffer(canvas, skinned)) {
        free(skinned);
        return;
    }
    rt_canvas3d_draw_mesh_matrix_keyed_bounds(canvas,
                                              &tmp,
                                              model_matrix,
                                              material,
                                              motion_key,
                                              NULL,
                                              NULL,
                                              local_bounds_min,
                                              local_bounds_max,
                                              conservative_bounds,
                                              disable_occlusion,
                                              0.0f);
}

/// @brief Draw a skinned mesh using an explicit model matrix and keyed bone palette.
void rt_canvas3d_draw_mesh_matrix_skinned_keyed(void *canvas,
                                                void *mesh_obj,
                                                const double *model_matrix,
                                                void *material,
                                                const void *motion_key,
                                                const float *bone_palette,
                                                const float *prev_bone_palette,
                                                int32_t bone_count) {
    rt_canvas3d_draw_mesh_matrix_skinned_keyed_bounds(canvas,
                                                      mesh_obj,
                                                      model_matrix,
                                                      material,
                                                      motion_key,
                                                      bone_palette,
                                                      prev_bone_palette,
                                                      bone_count,
                                                      NULL,
                                                      NULL,
                                                      0,
                                                      0);
}

/// @brief Draw a skinned mesh — applies an animator's bone palette before rasterising.
/// @details Accepts either a raw `AnimPlayer3D` or an `AnimController3D`. For a
///   controller, the state-machine's computed final/previous bone palettes and
///   skeleton drive the skinning, so idle/walk states and crossfades render directly.
void rt_canvas3d_draw_mesh_skinned(
    void *canvas, void *mesh, void *transform, void *material, void *anim_player) {
    rt_mesh3d *m;
    rt_anim_player3d *p;
    rt_skeleton3d *skel;
    const float *prev_palette;
    mat4_impl *transform_mat;
    if (!canvas || !mesh || !transform || !material || !anim_player)
        return;
    if (!rt_canvas3d_checked_or_stack(canvas))
        return;
    transform_mat = skeleton3d_mat4_checked(transform);
    if (!transform_mat)
        return;
    m = (rt_mesh3d *)rt_g3d_checked_or_null(mesh, RT_G3D_MESH3D_CLASS_ID);
    if (!m)
        return;
    rt_mesh3d_repair_geometry_counts(m);
    if (m->vertex_count == 0)
        return;

    if (rt_g3d_has_class(anim_player, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
        int32_t ctrl_bone_count = 0;
        const float *ctrl_palette =
            rt_anim_controller3d_get_final_palette_data(anim_player, &ctrl_bone_count);
        const float *ctrl_prev = rt_anim_controller3d_get_previous_palette_data(anim_player, NULL);
        void *ctrl_skel = rt_anim_controller3d_get_skeleton(anim_player);
        if (!ctrl_palette || ctrl_bone_count <= 0 || !ctrl_skel)
            return;
        if (!rt_canvas3d_add_temp_object(canvas, anim_player))
            return;
        rt_canvas3d_draw_mesh_matrix_skinned_keyed(
            canvas,
            mesh,
            transform_mat->m,
            material,
            transform,
            ctrl_palette,
            ctrl_prev,
            skeleton3d_safe_bone_count((rt_skeleton3d *)ctrl_skel));
        return;
    }

    p = (rt_anim_player3d *)rt_g3d_checked_or_null(anim_player, RT_G3D_ANIMPLAYER3D_CLASS_ID);
    if (!p)
        return;
    skel = animation3d_skeleton_slot((void **)&p->skeleton);
    if (!skel || !p->bone_palette)
        return;
    if (!rt_canvas3d_add_temp_object(canvas, anim_player))
        return;
    prev_palette = anim_player_prepare_prev_palette(p, rt_canvas3d_get_frame_serial(canvas));
    rt_canvas3d_draw_mesh_matrix_skinned_keyed(canvas,
                                               mesh,
                                               transform_mat->m,
                                               material,
                                               transform,
                                               p->bone_palette,
                                               prev_palette,
                                               skeleton3d_safe_bone_count(skel));
}

/*==========================================================================
 * Canvas3D extension — per-instance skinned crowds (R18)
 *=========================================================================*/

/// @brief Queue @p instance_count copies of a skinned mesh, each posed by its own palette.
/// @details GPU-skinning backends pack chunks of `VGFX3D_MAX_BONES / bone_count` instances
///   into the existing per-draw palette slot (the vertex shader indexes
///   `palette[instance_id * stride + bone]`), so a 32-bone crowd rides one hardware draw per
///   32 characters instead of one per character. Palettes are snapshotted into frame-owned
///   buffers before queueing so later animation ticks cannot repose queued draws. Draws that
///   cannot take the hardware path (software backend, blending materials, extra influences,
///   partitioned bone maps) fall back to one skinned draw per instance, which preserves the
///   exact single-draw pose semantics on every backend.
/// @param bone_palettes Packed palettes: instance_count * bone_count * 16 floats.
/// @param prev_bone_palettes Optional previous-frame palettes with the same layout, or NULL.
void rt_canvas3d_draw_mesh_instanced_skinned(void *canvas,
                                             void *mesh_obj,
                                             void *material,
                                             const float *instance_matrices,
                                             int32_t instance_count,
                                             const float *bone_palettes,
                                             const float *prev_bone_palettes,
                                             int32_t bone_count) {
    rt_canvas3d *c;
    rt_mesh3d *mesh;
    int gpu_ok;

    if (!canvas || !mesh_obj || !material || !instance_matrices || !bone_palettes ||
        instance_count <= 0 || bone_count <= 0)
        return;
    c = rt_canvas3d_checked_or_stack(canvas);
    if (!c || !c->in_frame || !c->backend)
        return;
    if (bone_count > VGFX3D_MAX_BONES) {
        rt_trap("Canvas3D.DrawMeshInstancedSkinned: bone count exceeds the palette limit");
        return;
    }
    mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
#if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (!mesh && mesh_obj && !rt_heap_is_payload(mesh_obj))
        mesh = (rt_mesh3d *)mesh_obj;
#endif
    if (!mesh)
        return;

    gpu_ok = !c->force_cpu_skinning && !mesh->extra_influences && !mesh->bone_map &&
             vgfx3d_backend_prefers_gpu_skinning(c->backend->name, bone_count);
    if (gpu_ok) {
        c->gpu_skinned_draw_count++;
        int32_t per_draw = VGFX3D_MAX_BONES / bone_count;
        size_t palette_lanes = (size_t)instance_count * (size_t)bone_count * 16u;
        float *palette_snapshot = NULL;
        float *prev_snapshot = NULL;
        int32_t start;

        if (palette_lanes / 16u / (size_t)bone_count != (size_t)instance_count ||
            palette_lanes > SIZE_MAX / sizeof(float))
            return;
        palette_snapshot = (float *)malloc(palette_lanes * sizeof(float));
        if (!palette_snapshot || !rt_canvas3d_add_temp_buffer(canvas, palette_snapshot)) {
            free(palette_snapshot);
            return;
        }
        memcpy(palette_snapshot, bone_palettes, palette_lanes * sizeof(float));
        c->skinning_upload_bytes +=
            (int64_t)(palette_lanes * sizeof(float)) * (prev_bone_palettes ? 2 : 1);
        if (prev_bone_palettes) {
            prev_snapshot = (float *)malloc(palette_lanes * sizeof(float));
            if (prev_snapshot && !rt_canvas3d_add_temp_buffer(canvas, prev_snapshot)) {
                free(prev_snapshot);
                prev_snapshot = NULL;
            }
            if (prev_snapshot)
                memcpy(prev_snapshot, prev_bone_palettes, palette_lanes * sizeof(float));
        }
        for (start = 0; start < instance_count; start += per_draw) {
            int32_t len = instance_count - start;
            if (len > per_draw)
                len = per_draw;
            if (!rt_canvas3d_queue_instanced_batch_skinned(
                    canvas,
                    mesh_obj,
                    material,
                    instance_matrices + (size_t)start * 16u,
                    len,
                    palette_snapshot + (size_t)start * (size_t)bone_count * 16u,
                    prev_snapshot ? prev_snapshot + (size_t)start * (size_t)bone_count * 16u : NULL,
                    len * bone_count,
                    bone_count)) {
                /* Hardware refusal is batch-invariant (same mesh/material), so nothing
                 * was queued yet; drop to the per-instance path for the whole batch. */
                gpu_ok = 0;
                break;
            }
        }
        if (gpu_ok)
            return;
    }

    for (int32_t i = 0; i < instance_count; i++) {
        double model_d[16];
        const float *m = instance_matrices + (size_t)i * 16u;
        const float *palette_i = bone_palettes + (size_t)i * (size_t)bone_count * 16u;
        const float *prev_i =
            prev_bone_palettes ? prev_bone_palettes + (size_t)i * (size_t)bone_count * 16u : NULL;
        for (int j = 0; j < 16; j++)
            model_d[j] = (double)m[j];
        rt_canvas3d_draw_mesh_matrix_skinned_keyed_bounds(canvas,
                                                          mesh_obj,
                                                          model_d,
                                                          material,
                                                          (const void *)palette_i,
                                                          palette_i,
                                                          prev_i,
                                                          bone_count,
                                                          NULL,
                                                          NULL,
                                                          0,
                                                          0);
    }
}

/// @brief Zia entry: draw an InstanceBatch3D with one animator palette per instance.
/// @details @p players is a Seq holding one AnimPlayer3D or AnimController3D per batch
///   instance; every animator must drive a skeleton with the same bone count. Palettes are
///   packed into one contiguous block and submitted through
///   rt_canvas3d_draw_mesh_instanced_skinned.
void rt_canvas3d_draw_instanced_skinned(void *canvas, void *batch, void *players) {
    rt_canvas3d *c;
    void *mesh;
    void *material;
    const float *transforms;
    int32_t instance_count = 0;
    int64_t player_count;
    int32_t bone_count = 0;
    float *packed = NULL;
    float *packed_prev = NULL;
    int has_all_prev = 1;
    size_t lanes;

    if (!canvas || !batch || !players)
        return;
    c = rt_canvas3d_checked_or_stack(canvas);
    if (!c || !c->in_frame)
        return;
    mesh = rt_instbatch3d_borrow_mesh(batch);
    material = rt_instbatch3d_borrow_material(batch);
    transforms = rt_instbatch3d_borrow_transforms(batch, &instance_count);
    if (!mesh || !material || !transforms || instance_count <= 0)
        return;
    player_count = rt_seq_len(players);
    if (player_count != (int64_t)instance_count) {
        rt_trap("Canvas3D.DrawInstancedSkinned: player count must match the batch instance "
                "count");
        return;
    }

    /* First pass: agree on one bone count across every animator. */
    for (int32_t i = 0; i < instance_count; i++) {
        void *item = rt_seq_get(players, i);
        int32_t item_bones = 0;
        if (rt_g3d_has_class(item, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
            if (!rt_anim_controller3d_get_final_palette_data(item, &item_bones))
                item_bones = 0;
        } else {
            rt_anim_player3d *p =
                (rt_anim_player3d *)rt_g3d_checked_or_null(item, RT_G3D_ANIMPLAYER3D_CLASS_ID);
            rt_skeleton3d *skel = p ? animation3d_skeleton_slot((void **)&p->skeleton) : NULL;
            if (p && p->bone_palette && skel)
                item_bones = skeleton3d_safe_bone_count(skel);
        }
        if (item_bones <= 0) {
            rt_trap("Canvas3D.DrawInstancedSkinned: every instance needs an AnimPlayer3D or "
                    "AnimController3D with an active palette");
            return;
        }
        if (bone_count == 0)
            bone_count = item_bones;
        else if (bone_count != item_bones) {
            rt_trap("Canvas3D.DrawInstancedSkinned: instance palettes must share one skeleton "
                    "bone count");
            return;
        }
    }
    if (bone_count <= 0 || bone_count > VGFX3D_MAX_BONES) {
        rt_trap("Canvas3D.DrawInstancedSkinned: bone count exceeds the palette limit");
        return;
    }

    lanes = (size_t)instance_count * (size_t)bone_count * 16u;
    if (lanes > SIZE_MAX / sizeof(float) / 2u)
        return;
    packed = (float *)malloc(lanes * sizeof(float));
    packed_prev = (float *)malloc(lanes * sizeof(float));
    if (!packed || !packed_prev) {
        free(packed);
        free(packed_prev);
        rt_trap("Canvas3D.DrawInstancedSkinned: palette allocation failed");
        return;
    }

    for (int32_t i = 0; i < instance_count; i++) {
        void *item = rt_seq_get(players, i);
        const float *palette = NULL;
        const float *prev = NULL;
        if (rt_g3d_has_class(item, RT_G3D_ANIMCONTROLLER3D_CLASS_ID)) {
            int32_t item_bones = 0;
            palette = rt_anim_controller3d_get_final_palette_data(item, &item_bones);
            prev = rt_anim_controller3d_get_previous_palette_data(item, NULL);
        } else {
            rt_anim_player3d *p =
                (rt_anim_player3d *)rt_g3d_checked_or_null(item, RT_G3D_ANIMPLAYER3D_CLASS_ID);
            palette = p ? p->bone_palette : NULL;
            prev = p ? anim_player_prepare_prev_palette(p, rt_canvas3d_get_frame_serial(canvas))
                     : NULL;
        }
        if (!palette) {
            free(packed);
            free(packed_prev);
            return;
        }
        memcpy(packed + (size_t)i * (size_t)bone_count * 16u,
               palette,
               (size_t)bone_count * 16u * sizeof(float));
        if (prev)
            memcpy(packed_prev + (size_t)i * (size_t)bone_count * 16u,
                   prev,
                   (size_t)bone_count * 16u * sizeof(float));
        else
            has_all_prev = 0;
    }

    rt_canvas3d_draw_mesh_instanced_skinned(canvas,
                                            mesh,
                                            material,
                                            transforms,
                                            instance_count,
                                            packed,
                                            has_all_prev ? packed_prev : NULL,
                                            bone_count);
    free(packed);
    free(packed_prev);
}
