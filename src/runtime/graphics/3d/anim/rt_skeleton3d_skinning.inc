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
    if (bone_count > VGFX3D_MAX_BONES)
        bone_count = VGFX3D_MAX_BONES;
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
    if (c->backend && vgfx3d_backend_prefers_gpu_skinning(c->backend->name, bone_count)) {
        rt_mesh3d tmp = *mesh;
        tmp.bone_palette = bone_palette;
        tmp.prev_bone_palette = prev_bone_palette;
        tmp.bone_count = bone_count;
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

    vgfx3d_skin_vertices(mesh->vertices, skinned, mesh->vertex_count, bone_palette, bone_count);

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
