//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/render/rt_canvas3d_instanced.inc  (#included from rt_canvas3d.c;
// not a standalone translation unit) Purpose: Canvas3D instanced-batch submission (fallback and
// hardware paths)
//===----------------------------------------------------------------------===//

/// @brief Queue an instanced draw — render `instance_count` copies of `mesh` with per-instance
/// transforms.
///
/// One draw call instead of `instance_count` separate calls.
/// Used for foliage, debris, particle clouds, and any scene with
/// many copies of the same mesh. The matrix array is owned by the
/// caller for the duration of the frame; the canvas keeps a
/// reference until the deferred queue flushes.
/// @brief Owned per-batch references for an instanced draw, so the many failure
///        exits release the tracked mesh/material temp-objects through one helper.
typedef struct {
    rt_canvas3d *c;
    rt_mesh3d *mesh;
    rt_material3d *mat;
    void *mesh_obj;
    void *material_obj;
    void *new_refs[CANVAS3D_DRAW_RESOURCE_ROLLBACK_CAP];
    int32_t new_ref_count;
} canvas3d_instanced_batch_t;

/// @brief Release the batch's tracked mesh/material temp-object references (material then mesh).
static void canvas3d_instanced_release_refs(const canvas3d_instanced_batch_t *b) {
    if (!b)
        return;
    canvas3d_release_new_draw_resources(b->c, b->new_refs, b->new_ref_count);
}

/// @brief Populate the shared base draw command from a mesh's geometry/skin/morph state.
static void canvas3d_build_instanced_base_cmd(rt_mesh3d *mesh,
                                              rt_material3d *mat,
                                              rt_canvas3d *canvas,
                                              vgfx3d_draw_cmd_t *base_cmd) {
    memset(base_cmd, 0, sizeof(*base_cmd));
    base_cmd->vertices = mesh->vertices;
    base_cmd->vertex_count = rt_mesh3d_safe_vertex_count(mesh);
    base_cmd->indices = mesh->indices;
    base_cmd->index_count = rt_mesh3d_validated_index_count(mesh);
    canvas3d_mark_cmd_validated_indices(base_cmd, mesh);
    base_cmd->model_matrix[0] = base_cmd->model_matrix[5] = base_cmd->model_matrix[10] =
        base_cmd->model_matrix[15] = 1.0f;
    canvas3d_fill_material_cmd(mat, base_cmd);
    canvas3d_apply_ibl_env(canvas, base_cmd);
    canvas3d_bind_skinning_cmd(canvas, base_cmd, mesh, NULL);
    canvas3d_bind_morph_cmd(canvas, base_cmd, mesh, NULL);
}

/// @brief Choose local bounds for an instanced draw, preferring caller-provided conservative data.
/// @details Extras such as vegetation can expand local bounds for shader-like deformation without
///   mutating the shared mesh. Invalid explicit bounds are ignored and the mesh/snapshot AABB is
///   used instead.
static void canvas3d_select_instanced_local_bounds(const rt_mesh3d *mesh,
                                                   const vgfx3d_vertex_t *vertices,
                                                   const float *explicit_min,
                                                   const float *explicit_max,
                                                   float out_min[3],
                                                   float out_max[3]) {
    if (explicit_min && explicit_max && canvas3d_bounds_are_valid(explicit_min, explicit_max)) {
        memcpy(out_min, explicit_min, sizeof(float) * 3u);
        memcpy(out_max, explicit_max, sizeof(float) * 3u);
        return;
    }
    canvas3d_copy_or_compute_local_bounds(mesh, vertices, out_min, out_max);
}

/// @brief Fallback instanced submission: enqueue each instance as an individual mesh draw.
/// @details Used when the backend lacks hardware instancing or the material needs blending.
static void canvas3d_queue_instanced_fallback(const canvas3d_instanced_batch_t *b,
                                              const vgfx3d_draw_cmd_t *base_cmd,
                                              const float *instance_matrices,
                                              int32_t instance_count,
                                              const float *prev_instance_matrices,
                                              int8_t has_prev_instance_matrices,
                                              int rebase_positions64_instances,
                                              int generate_rebased_tangents,
                                              const float *explicit_local_bounds_min,
                                              const float *explicit_local_bounds_max,
                                              int8_t conservative_bounds,
                                              int8_t disable_occlusion) {
    rt_canvas3d *c = b->c;
    rt_mesh3d *mesh = b->mesh;
    rt_material3d *mat = b->mat;
    float local_bounds_min[3];
    float local_bounds_max[3];
    canvas3d_select_instanced_local_bounds(mesh,
                                           base_cmd ? base_cmd->vertices : NULL,
                                           explicit_local_bounds_min,
                                           explicit_local_bounds_max,
                                           local_bounds_min,
                                           local_bounds_max);
    if (!ensure_deferred_capacity(
            &c->draw_cmds, &c->draw_capacity, c->draw_count + instance_count)) {
        canvas3d_instanced_release_refs(b);
        rt_trap("Canvas3D.DrawMeshInstanced: deferred draw queue allocation failed");
        return;
    }
    for (int32_t i = 0; i < instance_count; i++) {
        float preflight[16];
        double shift[3];
        int preflight_ok;
        if (rebase_positions64_instances) {
            preflight_ok = canvas3d_compute_vertex_rebase_shift_f32(
                c, &instance_matrices[(size_t)i * 16u], shift, preflight);
        } else {
            preflight_ok = canvas3d_copy_mat4_f32_for_frame(
                               c, &instance_matrices[(size_t)i * 16u], preflight) &&
                           (!has_prev_instance_matrices || !prev_instance_matrices ||
                            canvas3d_copy_mat4_f32_for_frame(
                                c, &prev_instance_matrices[(size_t)i * 16u], preflight));
        }
        if (!preflight_ok) {
            canvas3d_instanced_release_refs(b);
            rt_trap("Canvas3D.DrawMeshInstanced: rebased instance matrix is out of float range");
            return;
        }
    }
    for (int32_t i = 0; i < instance_count; i++) {
        vgfx3d_draw_cmd_t per_instance = *base_cmd;
        uintptr_t instance_motion_key = canvas3d_instance_motion_key(
            b->mesh_obj, b->material_obj, instance_matrices, instance_count, i);
        if (rebase_positions64_instances) {
            double vertex_rebase_origin[3] = {0.0, 0.0, 0.0};
            vgfx3d_vertex_t *rebased_vertices = NULL;
            uint32_t *rebased_indices = NULL;
            if (!canvas3d_compute_vertex_rebase_shift_f32(c,
                                                          &instance_matrices[(size_t)i * 16u],
                                                          vertex_rebase_origin,
                                                          per_instance.model_matrix) ||
                !canvas3d_snapshot_mesh_geometry_rebased(
                    c, mesh, vertex_rebase_origin, &rebased_vertices, &rebased_indices)) {
                rt_trap("Canvas3D.DrawMeshInstanced: rebased instance geometry allocation failed");
                return;
            }
            if (generate_rebased_tangents &&
                !canvas3d_generate_snapshot_tangents(mesh, rebased_vertices, rebased_indices))
                canvas3d_disable_cmd_normal_map(&per_instance);
            per_instance.vertices = rebased_vertices;
            per_instance.indices = rebased_indices;
            per_instance.geometry_key = NULL;
            per_instance.geometry_revision = 0;
            memcpy(per_instance.prev_model_matrix,
                   per_instance.model_matrix,
                   sizeof(per_instance.prev_model_matrix));
            per_instance.has_prev_model_matrix = 0;
        } else if (!canvas3d_copy_mat4_f32_for_frame(
                       c, &instance_matrices[(size_t)i * 16u], per_instance.model_matrix)) {
            rt_trap("Canvas3D.DrawMeshInstanced: rebased instance matrix is out of float range");
            return;
        }
        if (!rebase_positions64_instances && has_prev_instance_matrices && prev_instance_matrices) {
            if (!canvas3d_copy_mat4_f32_for_frame(
                    c, &prev_instance_matrices[(size_t)i * 16u], per_instance.prev_model_matrix)) {
                rt_trap("Canvas3D.DrawMeshInstanced: rebased previous instance matrix is out of "
                        "float range");
                return;
            }
            per_instance.has_prev_model_matrix = 1;
        } else if (!rebase_positions64_instances) {
            canvas3d_resolve_previous_model(c,
                                            instance_motion_key,
                                            per_instance.model_matrix,
                                            per_instance.prev_model_matrix,
                                            &per_instance.has_prev_model_matrix);
        }
        per_instance.prev_instance_matrices = NULL;
        per_instance.has_prev_instance_matrices = 0;
        int8_t instance_blends = canvas3d_cmd_requires_blend(&per_instance);
        if (!canvas3d_enqueue_draw(
                c,
                &per_instance,
                DEFERRED_DRAW_MESH,
                DEFERRED_PASS_MAIN,
                NULL,
                0,
                1,
                c->wireframe,
                canvas3d_material_backface_cull(c, mat),
                canvas3d_sanitize_sort_key(canvas3d_compute_sort_key(c,
                                                                     per_instance.model_matrix,
                                                                     local_bounds_min,
                                                                     local_bounds_max,
                                                                     1,
                                                                     instance_blends),
                                           instance_blends),
                local_bounds_min,
                local_bounds_max)) {
            /* Earlier instances may already be queued and still borrow the retained refs. */
            rt_trap("Canvas3D.DrawMeshInstanced: deferred draw queue allocation failed");
            return;
        }
        {
            deferred_draw_t *queued = &((deferred_draw_t *)c->draw_cmds)[c->draw_count - 1];
            queued->stable_sort_id = instance_motion_key
                                         ? instance_motion_key
                                         : (queued->stable_sort_id ^ (uintptr_t)(uint32_t)i);
            queued->occlusion_fingerprint = 0;
            queued->occlusion_key = 0;
            canvas3d_finalize_deferred_visibility_flags(
                queued, conservative_bounds, disable_occlusion);
        }
    }
}

/// @brief Allocate a canvas-tracked temp buffer and copy @p instance_count 4×4 instance
///   matrices into it (overflow-checked), used when deferring an instanced draw. Traps with
///   @p trap_message on allocation failure.
/// @return The snapshot buffer, or NULL for invalid arguments.
static float *canvas3d_alloc_instance_matrix_snapshot(rt_canvas3d *c,
                                                      const float *matrices,
                                                      int32_t instance_count,
                                                      const char *trap_message) {
    size_t matrix_float_count;
    float *snapshot;
    if (!c || !matrices || instance_count <= 0)
        return NULL;
    if ((size_t)instance_count > SIZE_MAX / (16u * sizeof(float))) {
        rt_trap(trap_message ? trap_message
                             : "Canvas3D.DrawMeshInstanced: matrix allocation overflow");
        return NULL;
    }
    matrix_float_count = (size_t)instance_count * 16u;
    snapshot = (float *)malloc(matrix_float_count * sizeof(*snapshot));
    if (!snapshot) {
        rt_trap(trap_message ? trap_message
                             : "Canvas3D.DrawMeshInstanced: matrix allocation failed");
        return NULL;
    }
    for (int32_t i = 0; i < instance_count; ++i) {
        if (!canvas3d_copy_mat4_f32_for_frame(
                c, &matrices[(size_t)i * 16u], &snapshot[(size_t)i * 16u])) {
            free(snapshot);
            rt_trap(trap_message
                        ? trap_message
                        : "Canvas3D.DrawMeshInstanced: rebased matrix is out of float range");
            return NULL;
        }
    }
    return snapshot;
}

/// @brief Hardware instanced submission: snapshot instance (and motion) matrices into
///        frame-owned buffers and enqueue a single instanced draw command.
static void canvas3d_queue_instanced_hardware(const canvas3d_instanced_batch_t *b,
                                              vgfx3d_draw_cmd_t *base_cmd,
                                              const float *instance_matrices,
                                              int32_t instance_count,
                                              const float *prev_instance_matrices,
                                              int8_t has_prev_instance_matrices,
                                              const float *explicit_local_bounds_min,
                                              const float *explicit_local_bounds_max,
                                              int8_t conservative_bounds,
                                              int8_t disable_occlusion) {
    rt_canvas3d *c = b->c;
    rt_mesh3d *mesh = b->mesh;
    float local_bounds_min[3];
    float local_bounds_max[3];

    size_t matrix_float_count = (size_t)instance_count * 16u;
    float *queued_instance_matrices = canvas3d_alloc_instance_matrix_snapshot(
        c,
        instance_matrices,
        instance_count,
        "Canvas3D.DrawMeshInstanced: instance matrix allocation failed");
    if (!queued_instance_matrices) {
        canvas3d_instanced_release_refs(b);
        return;
    }
    if (!canvas3d_track_temp_buffer(c, queued_instance_matrices)) {
        free(queued_instance_matrices);
        canvas3d_instanced_release_refs(b);
        return;
    }

    float *queued_prev_instance_matrices = NULL;
    int needs_motion_vectors = canvas3d_frame_needs_motion_vectors(c);
    if (needs_motion_vectors) {
        if (has_prev_instance_matrices && prev_instance_matrices) {
            queued_prev_instance_matrices = canvas3d_alloc_instance_matrix_snapshot(
                c,
                prev_instance_matrices,
                instance_count,
                "Canvas3D.DrawMeshInstanced: previous instance matrix allocation failed");
        } else {
            queued_prev_instance_matrices =
                (float *)malloc(matrix_float_count * sizeof(*queued_prev_instance_matrices));
        }
        if (!queued_prev_instance_matrices) {
            canvas3d_release_tracked_temp_buffer(c, queued_instance_matrices);
            canvas3d_instanced_release_refs(b);
            if (!(has_prev_instance_matrices && prev_instance_matrices))
                rt_trap("Canvas3D.DrawMeshInstanced: previous instance matrix allocation failed");
            return;
        }
        if (!canvas3d_track_temp_buffer(c, queued_prev_instance_matrices)) {
            canvas3d_release_tracked_temp_buffer(c, queued_instance_matrices);
            free(queued_prev_instance_matrices);
            canvas3d_instanced_release_refs(b);
            return;
        }
    }
    for (int32_t i = 0; i < instance_count; ++i) {
        int8_t has_prev = 0;
        float ignored_prev[16];
        float *dst_prev =
            needs_motion_vectors ? &queued_prev_instance_matrices[(size_t)i * 16u] : ignored_prev;
        const float *current = &queued_instance_matrices[(size_t)i * 16u];
        if (needs_motion_vectors && has_prev_instance_matrices && prev_instance_matrices) {
            /* Keep the internal motion history current for the next frame even when the caller
             * supplies an explicit previous-instance snapshot for this draw. */
            canvas3d_resolve_previous_model(
                c,
                canvas3d_instance_motion_key(
                    b->mesh_obj, b->material_obj, instance_matrices, instance_count, i),
                current,
                ignored_prev,
                &has_prev);
            continue;
        }
        canvas3d_resolve_previous_model(
            c,
            canvas3d_instance_motion_key(
                b->mesh_obj, b->material_obj, instance_matrices, instance_count, i),
            current,
            dst_prev,
            &has_prev);
        if (needs_motion_vectors && !has_prev)
            memcpy(dst_prev, current, 16u * sizeof(float));
    }

    base_cmd->prev_instance_matrices = queued_prev_instance_matrices;
    base_cmd->has_prev_instance_matrices = queued_prev_instance_matrices ? 1 : 0;
    canvas3d_select_instanced_local_bounds(mesh,
                                           base_cmd->vertices,
                                           explicit_local_bounds_min,
                                           explicit_local_bounds_max,
                                           local_bounds_min,
                                           local_bounds_max);
    if (!canvas3d_enqueue_draw(
            c,
            base_cmd,
            DEFERRED_DRAW_INSTANCED,
            DEFERRED_PASS_MAIN,
            queued_instance_matrices,
            instance_count,
            1,
            c->wireframe,
            canvas3d_material_backface_cull(c, b->mat),
            canvas3d_compute_instanced_batch_sort_key(
                c, queued_instance_matrices, instance_count, local_bounds_min, local_bounds_max),
            local_bounds_min,
            local_bounds_max)) {
        rt_trap("Canvas3D.DrawMeshInstanced: deferred draw queue allocation failed");
        canvas3d_release_tracked_temp_buffer(c, queued_prev_instance_matrices);
        canvas3d_release_tracked_temp_buffer(c, queued_instance_matrices);
        canvas3d_instanced_release_refs(b);
        return;
    }
    /* A hardware-instanced draw has one aggregate AABB for many world positions. Let the
     * backend render it normally, but keep the CPU occlusion grid from culling/writing the
     * whole batch from that coarse rectangle. */
    canvas3d_finalize_deferred_visibility_flags(
        &((deferred_draw_t *)c->draw_cmds)[c->draw_count - 1],
        conservative_bounds,
        disable_occlusion || instance_count > 1);
}

/// @brief Queue an instanced-mesh draw (mesh + material + @p instance_count transforms, with
///   optional previous-frame matrices for motion vectors) into the canvas's deferred draw queue.
void rt_canvas3d_queue_instanced_batch_bounds(void *canvas_obj,
                                              void *mesh_obj,
                                              void *material_obj,
                                              const float *instance_matrices,
                                              int32_t instance_count,
                                              const float *prev_instance_matrices,
                                              int8_t has_prev_instance_matrices,
                                              const float *explicit_local_bounds_min,
                                              const float *explicit_local_bounds_max,
                                              int8_t conservative_bounds,
                                              int8_t disable_occlusion) {
    canvas3d_instanced_batch_t batch = {0};
    rt_canvas3d *c;
    rt_mesh3d *mesh;
    rt_material3d *mat;
    vgfx3d_draw_cmd_t base_cmd;
    vgfx3d_vertex_t *queued_vertices;
    uint32_t *queued_indices;
    int needs_generated_tangents;
    int use_fallback_instances;
    int rebase_positions64_instances;
    int suppress_normal_map = 0;
    int32_t requested_instance_count;

    if (!instance_matrices || instance_count <= 0)
        return;
    if (instance_count > CANVAS3D_MAX_INSTANCES) {
        rt_trap("Canvas3D.DrawMeshInstanced: instance count exceeds limit");
        return;
    }
    c = rt_canvas3d_checked_or_stack(canvas_obj);
    mesh = (rt_mesh3d *)rt_g3d_checked_or_null(mesh_obj, RT_G3D_MESH3D_CLASS_ID);
#if defined(RT_G3D_ALLOW_STACK_FIXTURES) && RT_G3D_ALLOW_STACK_FIXTURES
    if (!mesh && mesh_obj && !rt_heap_is_payload(mesh_obj))
        mesh = (rt_mesh3d *)mesh_obj;
#endif
    mat = (rt_material3d *)rt_g3d_checked_or_null(material_obj, RT_G3D_MATERIAL3D_CLASS_ID);
    if (!c || !mesh || !mat)
        return;
    rt_mesh3d_repair_geometry_counts(mesh);
    if (!c->in_frame || !c->backend || !mesh->resident || rt_mesh3d_safe_vertex_count(mesh) == 0 ||
        rt_mesh3d_validated_index_count(mesh) == 0)
        return;
    if (has_prev_instance_matrices && !prev_instance_matrices) {
        rt_trap("Canvas3D.DrawMeshInstanced: previous instance matrices pointer is required");
        return;
    }
    requested_instance_count = instance_count;
    if (!canvas3d_matrices_f32_are_finite(instance_matrices, requested_instance_count) ||
        (has_prev_instance_matrices && prev_instance_matrices &&
         !canvas3d_matrices_f32_are_finite(prev_instance_matrices, requested_instance_count))) {
        rt_trap("Canvas3D.DrawMeshInstanced: instance matrices must contain finite values");
        return;
    }

    rebase_positions64_instances = canvas3d_uses_camera_relative_upload(c) &&
                                   canvas3d_mesh_positions64_needs_vertex_rebase(mesh);
    needs_generated_tangents = canvas3d_prepare_normal_map_tangent_state(mesh, mat);
    rt_mesh3d_refresh_bounds(mesh);
    canvas3d_build_instanced_base_cmd(mesh, mat, c, &base_cmd);

    use_fallback_instances = rebase_positions64_instances ||
                             canvas3d_cmd_requires_blend(&base_cmd) ||
                             !c->backend->submit_draw_instanced;
    if (use_fallback_instances) {
        /* Per-instance queueing is bounded to keep the deferred queue sane;
         * overflow clamps but is now observable via get_InstancedFallbackDroppedCount
         * in addition to the queued fallback count. Opaque batches take the backend
         * instanced hook on every backend, including software, and never hit this path. */
        if (instance_count > CANVAS3D_MAX_FALLBACK_INSTANCES) {
            c->last_instanced_fallback_dropped_count +=
                (int64_t)instance_count - (int64_t)CANVAS3D_MAX_FALLBACK_INSTANCES;
            instance_count = CANVAS3D_MAX_FALLBACK_INSTANCES;
        }
        c->last_instanced_fallback_count += instance_count;
    }

    batch.c = c;
    batch.mesh = mesh;
    batch.mat = mat;
    batch.mesh_obj = mesh_obj;
    batch.material_obj = material_obj;
    if (rt_heap_is_payload(mesh_obj)) {
        if (!canvas3d_track_draw_resource(c,
                                          mesh_obj,
                                          batch.new_refs,
                                          &batch.new_ref_count,
                                          CANVAS3D_DRAW_RESOURCE_ROLLBACK_CAP))
            return;
    }
    if (!canvas3d_track_draw_resource(c,
                                      material_obj,
                                      batch.new_refs,
                                      &batch.new_ref_count,
                                      CANVAS3D_DRAW_RESOURCE_ROLLBACK_CAP)) {
        canvas3d_instanced_release_refs(&batch);
        return;
    }
    if (!canvas3d_track_material_resources(
            c, mat, batch.new_refs, &batch.new_ref_count, CANVAS3D_DRAW_RESOURCE_ROLLBACK_CAP)) {
        canvas3d_instanced_release_refs(&batch);
        return;
    }

    queued_vertices = mesh->vertices;
    queued_indices = mesh->indices;
    if (!rebase_positions64_instances && needs_generated_tangents) {
        if (!canvas3d_snapshot_mesh_geometry_with_tangents_cached(
                c, mesh, mesh_obj, &queued_vertices, &queued_indices)) {
            if (canvas3d_should_snapshot_geometry(mesh, mesh_obj)) {
                if (!canvas3d_snapshot_mesh_geometry_cached(
                        c, mesh, mesh_obj, &queued_vertices, &queued_indices)) {
                    canvas3d_instanced_release_refs(&batch);
                    return;
                }
            } else {
                queued_vertices = mesh->vertices;
                queued_indices = mesh->indices;
            }
            needs_generated_tangents = 0;
            suppress_normal_map = 1;
        }
    } else if (!rebase_positions64_instances && canvas3d_should_snapshot_geometry(mesh, mesh_obj) &&
               !canvas3d_snapshot_mesh_geometry_cached(
                   c, mesh, mesh_obj, &queued_vertices, &queued_indices)) {
        canvas3d_instanced_release_refs(&batch);
        return;
    }
    base_cmd.vertices = queued_vertices;
    base_cmd.indices = queued_indices;
    base_cmd.geometry_key =
        (rt_heap_is_payload(mesh_obj) && !needs_generated_tangents && !rebase_positions64_instances)
            ? mesh_obj
            : NULL;
    base_cmd.geometry_revision = base_cmd.geometry_key ? mesh->geometry_revision : 0;
    if (suppress_normal_map)
        canvas3d_disable_cmd_normal_map(&base_cmd);

    if (use_fallback_instances) {
        canvas3d_queue_instanced_fallback(&batch,
                                          &base_cmd,
                                          instance_matrices,
                                          instance_count,
                                          prev_instance_matrices,
                                          has_prev_instance_matrices,
                                          rebase_positions64_instances,
                                          needs_generated_tangents,
                                          explicit_local_bounds_min,
                                          explicit_local_bounds_max,
                                          conservative_bounds,
                                          disable_occlusion);
        return;
    }
    canvas3d_queue_instanced_hardware(&batch,
                                      &base_cmd,
                                      instance_matrices,
                                      instance_count,
                                      prev_instance_matrices,
                                      has_prev_instance_matrices,
                                      explicit_local_bounds_min,
                                      explicit_local_bounds_max,
                                      conservative_bounds,
                                      disable_occlusion);
}

/// @brief Queue an instanced-mesh draw using the mesh's authored local bounds.
void rt_canvas3d_queue_instanced_batch(void *canvas_obj,
                                       void *mesh_obj,
                                       void *material_obj,
                                       const float *instance_matrices,
                                       int32_t instance_count,
                                       const float *prev_instance_matrices,
                                       int8_t has_prev_instance_matrices) {
    rt_canvas3d_queue_instanced_batch_bounds(canvas_obj,
                                             mesh_obj,
                                             material_obj,
                                             instance_matrices,
                                             instance_count,
                                             prev_instance_matrices,
                                             has_prev_instance_matrices,
                                             NULL,
                                             NULL,
                                             0,
                                             0);
}

/// @brief Queue instanced matrices that have already been converted to frame render space.
/// @details Used by InstanceBatch3D's double-matrix camera-relative path. It reuses the normal
///          instanced queueing pipeline but suppresses the queue's translation rebase so the
///          already-rebased matrices are not shifted twice.
void rt_canvas3d_queue_instanced_batch_frame_matrices(void *canvas_obj,
                                                      void *mesh_obj,
                                                      void *material_obj,
                                                      const float *instance_matrices,
                                                      int32_t instance_count,
                                                      const float *prev_instance_matrices,
                                                      int8_t has_prev_instance_matrices) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas_obj);
    int8_t saved_camera_relative;

    if (!c)
        return;
    saved_camera_relative = c->camera_relative_upload;
    c->camera_relative_upload = 0;
    rt_canvas3d_queue_instanced_batch_bounds(canvas_obj,
                                             mesh_obj,
                                             material_obj,
                                             instance_matrices,
                                             instance_count,
                                             prev_instance_matrices,
                                             has_prev_instance_matrices,
                                             NULL,
                                             NULL,
                                             0,
                                             0);
    c->camera_relative_upload = saved_camera_relative;
}

/// @brief Recompute the canvas's pending/active texture-upload byte counters for this frame.
/// @details Aggregates the streaming upload state of referenced textures so the caller can budget
///          GPU upload bandwidth.
