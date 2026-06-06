//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/world/rt_terrain3d_draw.inc  (#included from rt_terrain3d.c; not a standalone translation unit)
// Purpose: Terrain3D culling, prewarm, draw, nav-mesh build, and public LOD/stat API
//===----------------------------------------------------------------------===//
/// @brief Return the terrain-specific CPU frustum padding in render/world units.
/// @details The union AABB is already conservative for LOD geometry; this extra half-cell guard
///   covers float rounding and camera-relative conversion differences at the horizon.
static float terrain_chunk_culling_pad(const rt_terrain3d *t) {
    double sx = terrain_scale_axis_or(t, 0, 1.0);
    double sz = terrain_scale_axis_or(t, 2, 1.0);
    double pad = fmax(fabs(sx), fabs(sz)) * 0.5;
    if (!isfinite(pad) || pad < 1e-3)
        pad = 1e-3;
    if (pad > 10000.0)
        pad = 10000.0;
    return (float)pad;
}

/// @brief Prebuild every missing terrain chunk LOD mesh.
/// @details The draw loop only selects between resident chunk meshes. Building LOD meshes before
///   visibility/selection avoids an individual chunk allocating geometry the first frame it crosses
///   an LOD threshold, which can otherwise produce visible stalls or inconsistent terrain edges.
static void terrain_prewarm_chunk_lods(rt_terrain3d *t) {
    int all_ready = 1;
    if (!t || !terrain_has_valid_chunk_grid(t))
        return;
    for (int32_t cz = 0; cz < t->chunks_z; cz++) {
        for (int32_t cx = 0; cx < t->chunks_x; cx++) {
            int32_t idx = cz * t->chunks_x + cx;
            if (!terrain_build_chunk_lod_if_missing(t, cx, cz, idx, 0))
                all_ready = 0;
            if (!terrain_build_chunk_lod_if_missing(t, cx, cz, idx, 1))
                all_ready = 0;
            if (!terrain_build_chunk_lod_if_missing(t, cx, cz, idx, 2))
                all_ready = 0;
            if (!terrain_rebuild_chunk_union_aabb(t, idx))
                all_ready = 0;
        }
    }
    t->lod_prewarm_dirty = all_ready ? 0 : 1;
}

/// @brief Return true when any terrain chunk LOD mesh is missing.
/// @details Used to keep prewarming out of the steady-state draw path while still recovering
///   from partial allocation failures or explicit terrain invalidation.
static int terrain_needs_lod_prewarm(const rt_terrain3d *t) {
    if (!t || !terrain_has_valid_chunk_grid(t))
        return 0;
    if (t->lod_prewarm_dirty)
        return 1;
    for (int32_t i = 0; i < terrain_safe_chunk_count(t); i++) {
        if (!terrain_lod_mesh_or_null(t, i, 0) || !terrain_lod_mesh_or_null(t, i, 1) ||
            !terrain_lod_mesh_or_null(t, i, 2) ||
            !terrain_aabb_is_valid(&t->chunk_aabbs_union[i * 6]))
            return 1;
    }
    return 0;
}

/// @brief Set chunk LOD switch distances. Within `near_dist` chunks render at full resolution;
/// between near and far they use step=2 (¼ triangles); beyond `far_dist` they use step=4
/// (1/16 triangles). Lower distances trade visual quality for triangle count.
void rt_terrain3d_set_lod_distances(void *obj, double near_dist, double far_dist) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(near_dist) || near_dist < 0.0)
        near_dist = 50.0;
    if (!isfinite(far_dist) || far_dist <= near_dist)
        far_dist = near_dist + 100.0;
    if (near_dist < 0.0)
        near_dist = 0.0;
    if (near_dist > TERRAIN3D_ABS_MAX)
        near_dist = TERRAIN3D_ABS_MAX;
    if (far_dist > TERRAIN3D_ABS_MAX)
        far_dist = TERRAIN3D_ABS_MAX;
    if (far_dist <= near_dist) {
        far_dist = near_dist + 1.0;
        if (far_dist > TERRAIN3D_ABS_MAX) {
            far_dist = TERRAIN3D_ABS_MAX;
            near_dist = far_dist - 1.0;
            if (near_dist < 0.0)
                near_dist = 0.0;
        }
    }
    if (t->lod_dist1 == (float)near_dist && t->lod_dist2 == (float)far_dist)
        return;
    t->lod_dist1 = (float)near_dist;
    t->lod_dist2 = (float)far_dist;
    terrain_reset_lod_state(t);
}

/// @brief Set the LOD distance hysteresis band used by terrain chunk selection.
/// @details A positive band prevents chunks from switching LOD every frame while the camera is
///   close to `SetLODDistances` thresholds. The value is clamped to a finite non-negative range;
///   final selection also clamps it below half the near/far threshold gap, so setting a large value
///   remains safe and simply means "use the widest stable band possible."
void rt_terrain3d_set_lod_hysteresis(void *obj, double distance) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(distance) || distance < 0.0)
        distance = 0.0;
    if (distance > TERRAIN3D_ABS_MAX)
        distance = TERRAIN3D_ABS_MAX;
    if (t->lod_hysteresis == (float)distance)
        return;
    t->lod_hysteresis = (float)distance;
    terrain_reset_lod_state(t);
}

/// @brief Set the depth (world units) of the downward-extruded skirt geometry generated along
/// LOD>0 chunk edges. Skirts hide T-junction cracks where adjacent chunks render at different
/// LODs. Set to 0 to disable. Invalidates cached chunks (skirts are baked into the mesh).
void rt_terrain3d_set_skirt_depth(void *obj, double depth) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    if (!isfinite(depth) || depth < 0.0)
        depth = 0.0;
    if (depth > 1000.0)
        depth = 1000.0;
    if (t->skirt_depth == (float)depth)
        return;
    t->skirt_depth = (float)depth;
    invalidate_all_chunks(t);
}

/// @brief Enable or disable Terrain3D CPU occlusion-grid participation.
/// @details Terrain remains frustum-culled in both modes. When disabled (the default), chunks are
///   submitted with conservative bounds and cannot become coarse AABB occluders, preventing
///   horizon/floor cracks caused by over-aggressive CPU occlusion.
void rt_terrain3d_set_cpu_occlusion(void *obj, int8_t enabled) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!t)
        return;
    t->cpu_occlusion_enabled = enabled ? 1 : 0;
}

/// @brief Return whether Terrain3D CPU occlusion-grid participation is enabled.
int8_t rt_terrain3d_get_cpu_occlusion(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    return t && t->cpu_occlusion_enabled ? 1 : 0;
}

/// @brief Clamp an internal terrain diagnostic counter to the public non-negative int64 range.
static int64_t terrain_stat_or_zero(int32_t value) {
    return value > 0 ? (int64_t)value : 0;
}

/// @brief Number of chunks in the most recent terrain draw attempt.
int64_t rt_terrain3d_get_last_chunk_count(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    return t ? terrain_stat_or_zero(t->last_chunk_count) : 0;
}

/// @brief Number of chunks submitted to Canvas3D by the most recent terrain draw.
int64_t rt_terrain3d_get_last_drawn_chunk_count(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    return t ? terrain_stat_or_zero(t->last_drawn_chunk_count) : 0;
}

/// @brief Number of chunks rejected by Terrain3D's conservative frustum pre-pass.
int64_t rt_terrain3d_get_last_frustum_culled_chunk_count(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    return t ? terrain_stat_or_zero(t->last_frustum_culled_chunk_count) : 0;
}

/// @brief Number of chunks whose selected LOD was absent and required fallback or skipping.
int64_t rt_terrain3d_get_last_missing_lod_count(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    return t ? terrain_stat_or_zero(t->last_missing_lod_count) : 0;
}

/// @brief Number of chunks clamped to keep neighboring LOD levels crack-safe.
int64_t rt_terrain3d_get_last_lod_clamped_chunk_count(void *obj) {
    rt_terrain3d *t = (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID);
    return t ? terrain_stat_or_zero(t->last_lod_clamped_chunk_count) : 0;
}

/// @brief Number of chunks drawn at the requested/actual LOD index.
static int64_t terrain_last_lod_count(rt_terrain3d *t, int32_t lod) {
    if (!t || lod < 0 || lod >= TERRAIN_LOD_LEVELS)
        return 0;
    return terrain_stat_or_zero(t->last_lod_counts[lod]);
}

/// @brief Number of chunks drawn at LOD0 during the most recent terrain draw.
int64_t rt_terrain3d_get_last_lod0_chunk_count(void *obj) {
    return terrain_last_lod_count(
        (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID), 0);
}

/// @brief Number of chunks drawn at LOD1 during the most recent terrain draw.
int64_t rt_terrain3d_get_last_lod1_chunk_count(void *obj) {
    return terrain_last_lod_count(
        (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID), 1);
}

/// @brief Number of chunks drawn at LOD2 during the most recent terrain draw.
int64_t rt_terrain3d_get_last_lod2_chunk_count(void *obj) {
    return terrain_last_lod_count(
        (rt_terrain3d *)rt_g3d_checked_or_null(obj, RT_G3D_TERRAIN3D_CLASS_ID), 2);
}

/// @brief Number of nav-mesh grid samples along an axis of @p max_index cells decimated by @p step.
/// @details Always includes the final cell (adds an extra sample when max_index isn't a multiple of
///          step) and yields at least 2, so the nav source spans the full terrain footprint.
static int32_t terrain_nav_sample_count(int32_t max_index, int32_t step) {
    int32_t count;
    if (max_index <= 0)
        return 1;
    if (step <= 0)
        step = 1;
    count = max_index / step + 1;
    if (max_index % step != 0)
        count++;
    if (count < 2)
        count = 2;
    return count;
}

/// @brief Map a decimated nav sample @p index to its grid coordinate, snapping the ends to
/// 0/max_index.
static int32_t terrain_nav_sample_coord(int32_t index,
                                        int32_t count,
                                        int32_t max_index,
                                        int32_t step) {
    if (index <= 0)
        return 0;
    if (index >= count - 1)
        return max_index;
    int32_t value = index * step;
    return value > max_index ? max_index : value;
}

/// @brief Build a Mesh3D approximation of the whole terrain for scene/nav baking.
/// @details This is intentionally separate from the render chunk cache: nav only needs a
///   stable triangle source that follows the terrain's heightmap and scale, while rendering
///   keeps its own LOD/skirt chunk meshes. `step` decimates grid samples but always includes
///   the final row/column so the nav source keeps the streamed tile footprint.
void *rt_terrain3d_build_nav_mesh(void *terrain_obj, int64_t step64) {
    rt_terrain3d *t =
        (rt_terrain3d *)rt_g3d_checked_or_null(terrain_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    if (!terrain_has_valid_grid(t))
        return NULL;
    int32_t step;
    if (step64 < 1)
        step64 = 1;
    if (step64 > 16)
        step64 = 16;
    step = (int32_t)step64;
    if (step < 1)
        step = 1;
    int32_t cols = terrain_nav_sample_count(t->width - 1, step);
    int32_t rows = terrain_nav_sample_count(t->depth - 1, step);
    void *mesh = rt_mesh3d_new();
    if (!mesh)
        return NULL;

    for (int32_t rz = 0; rz < rows; ++rz) {
        int32_t iz = terrain_nav_sample_coord(rz, rows, t->depth - 1, step);
        for (int32_t rx = 0; rx < cols; ++rx) {
            int32_t ix = terrain_nav_sample_coord(rx, cols, t->width - 1, step);
            double wx, wy, wz, nx, ny, nz_n, u, v;
            terrain_vertex(t, ix, iz, &wx, &wy, &wz, &nx, &ny, &nz_n, &u, &v);
            rt_mesh3d_add_vertex(mesh, wx, wy, wz, nx, ny, nz_n, u, v);
        }
    }

    for (int32_t rz = 0; rz < rows - 1; ++rz) {
        for (int32_t rx = 0; rx < cols - 1; ++rx) {
            int64_t base = (int64_t)(rz * cols + rx);
            rt_mesh3d_add_triangle(mesh, base, base + cols, base + 1);
            rt_mesh3d_add_triangle(mesh, base + 1, base + cols, base + cols + 1);
        }
    }

    if (((rt_mesh3d *)mesh)->build_failed) {
        if (rt_obj_release_check0(mesh))
            rt_obj_free(mesh);
        return NULL;
    }
    return mesh;
}

/// @brief Render the terrain at the origin via the Canvas3D.
void rt_canvas3d_draw_terrain(void *canvas_obj, void *terrain_obj) {
    rt_canvas3d_draw_terrain_at(canvas_obj, terrain_obj, 0.0, 0.0, 0.0);
}

/// @brief Render the terrain via the Canvas3D at a world-space translation.
/// @details Lazily builds chunk meshes on first draw, bakes the splat texture if needed, then
///   for each chunk: frustum-cull the translated AABB, pick LOD by translated chunk distance
///   to camera (XZ), and enqueue the chosen mesh with a translated model matrix.
void rt_canvas3d_draw_terrain_at(
    void *canvas_obj, void *terrain_obj, double tx, double ty, double tz) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas_obj);
    rt_terrain3d *t =
        (rt_terrain3d *)rt_g3d_checked_or_null(terrain_obj, RT_G3D_TERRAIN3D_CLASS_ID);
    int32_t chunk_count;
    void *splat_map;
    void *splat_layers[TERRAIN_MAX_SPLAT_LAYERS] = {NULL, NULL, NULL, NULL};
    float splat_layer_scales[TERRAIN_MAX_SPLAT_LAYERS] = {1.0f, 1.0f, 1.0f, 1.0f};
    int complete_splat_layers = 0;
    double camera_relative_origin[3] = {0.0, 0.0, 0.0};
    int use_camera_relative_bounds = 0;
    if (!c || !t)
        return;
    if (!c->in_frame || !c->backend || !t->material ||
        !rt_g3d_has_class(t->material, RT_G3D_MATERIAL3D_CLASS_ID) || !terrain_has_valid_grid(t) ||
        !terrain_has_valid_chunk_grid(t))
        return;
    if (c->frame_is_2d) {
        rt_trap("Canvas3D.DrawTerrain: cannot draw terrain during Begin2D/End");
        return;
    }

    /* Bake splat-blended texture if splat map is set and chunks are invalid */
    chunk_count = terrain_safe_chunk_count(t);
    terrain_reset_draw_stats(t, chunk_count);
    splat_map = terrain_valid_splat_pixels_or_null(t->splat_map);
    if (t->splat_map && !splat_map)
        terrain_clear_invalid_splat_state(t);
    if (splat_map) {
        int any_invalid = 0;
        complete_splat_layers = 1;
        for (int si = 0; si < TERRAIN_MAX_SPLAT_LAYERS; si++) {
            splat_layers[si] = terrain_valid_pixels_or_null(t->layer_textures[si]);
            splat_layer_scales[si] = (float)terrain_layer_scale_or(t, si);
            if (!splat_layers[si])
                complete_splat_layers = 0;
        }
        for (int32_t i = 0; i < chunk_count && !any_invalid; i++)
            if (!t->chunk_meshes[i])
                any_invalid = 1;
        if (t->splat_dirty || any_invalid)
            bake_splat_texture(t);
    }
    if (terrain_needs_lod_prewarm(t))
        terrain_prewarm_chunk_lods(t);

    vgfx3d_frustum_t frustum;
    int use_visibility_culling = (c->frustum_culling || c->occlusion_culling) ? 1 : 0;
    if (use_visibility_culling)
        vgfx3d_frustum_extract(&frustum, c->cached_vp);

    tx = terrain_clamp_abs_or(tx, 0.0);
    ty = terrain_clamp_abs_or(ty, 0.0);
    tz = terrain_clamp_abs_or(tz, 0.0);
    use_camera_relative_bounds =
        rt_canvas3d_get_camera_relative_origin(canvas_obj, camera_relative_origin);

    float lod_dist1;
    float lod_dist2;
    terrain_lod_distances_or(t, &lod_dist1, &lod_dist2);
    float culling_pad = terrain_chunk_culling_pad(t);
    int8_t terrain_disable_occlusion = (int8_t)(t->cpu_occlusion_enabled ? 0 : 1);
    int8_t terrain_conservative_bounds = (int8_t)(terrain_disable_occlusion ? 1 : 0);

    double model[16] = {
        1.0,
        0.0,
        0.0,
        tx,
        0.0,
        1.0,
        0.0,
        ty,
        0.0,
        0.0,
        1.0,
        tz,
        0.0,
        0.0,
        0.0,
        1.0,
    };

    for (int32_t cz = 0; cz < t->chunks_z; cz++) {
        for (int32_t cx = 0; cx < t->chunks_x; cx++) {
            int32_t idx = cz * t->chunks_x + cx;

            if (!terrain_build_chunk_lod_if_missing(t, cx, cz, idx, 0)) {
                t->last_missing_lod_count++;
                continue;
            }
            terrain_rebuild_chunk_union_aabb(t, idx);

            const float *cull_aabb = terrain_chunk_union_aabb(t, idx);
            if (!cull_aabb) {
                t->last_missing_lod_count++;
                continue;
            }
            double origin_x = use_camera_relative_bounds ? camera_relative_origin[0] : 0.0;
            double origin_y = use_camera_relative_bounds ? camera_relative_origin[1] : 0.0;
            double origin_z = use_camera_relative_bounds ? camera_relative_origin[2] : 0.0;
            float world_min[3] = {
                canvas3d_sanitize_f64_to_float((double)cull_aabb[0] + tx - origin_x, 0.0f),
                canvas3d_sanitize_f64_to_float((double)cull_aabb[1] + ty - origin_y, 0.0f),
                canvas3d_sanitize_f64_to_float((double)cull_aabb[2] + tz - origin_z, 0.0f),
            };
            float world_max[3] = {
                canvas3d_sanitize_f64_to_float((double)cull_aabb[3] + tx - origin_x, 0.0f),
                canvas3d_sanitize_f64_to_float((double)cull_aabb[4] + ty - origin_y, 0.0f),
                canvas3d_sanitize_f64_to_float((double)cull_aabb[5] + tz - origin_z, 0.0f),
            };
            if (use_visibility_culling &&
                vgfx3d_frustum_test_aabb_padded(&frustum, world_min, world_max, culling_pad) == 0) {
                t->last_frustum_culled_chunk_count++;
                continue;
            }

            /* Phase B: LOD selection based on distance to camera */
            float chunk_cx = (world_min[0] + world_max[0]) * 0.5f;
            float chunk_cz = (world_min[2] + world_max[2]) * 0.5f;
            double dx = (double)chunk_cx - (double)c->cached_cam_pos[0];
            double dz = (double)chunk_cz - (double)c->cached_cam_pos[2];
            double dist = sqrt(dx * dx + dz * dz);
            if (!isfinite(dist))
                dist = (double)lod_dist2;

            int32_t lod = terrain_select_lod(t, idx, dist, lod_dist1, lod_dist2);
            lod = terrain_clamp_lod_to_neighbors(t, idx, lod);

            const float *selected_aabb = NULL;
            void *draw_mesh =
                terrain_select_draw_mesh_with_fallback(t, idx, lod, &lod, &selected_aabb);
            if (draw_mesh) {
                const float *submit_aabb = terrain_disable_occlusion
                                               ? cull_aabb
                                               : (selected_aabb ? selected_aabb : cull_aabb);
                float local_bounds_min[3] = {submit_aabb[0], submit_aabb[1], submit_aabb[2]};
                float local_bounds_max[3] = {submit_aabb[3], submit_aabb[4], submit_aabb[5]};
                /* Set pending splat data for per-pixel terrain splatting */
                if (splat_map && complete_splat_layers) {
                    c->pending_has_splat = 1;
                    c->pending_splat_map = splat_map;
                    for (int si = 0; si < 4; si++) {
                        c->pending_splat_layers[si] = splat_layers[si];
                        c->pending_splat_layer_scales[si] = splat_layer_scales[si];
                    }
                }
                rt_canvas3d_draw_mesh_matrix_keyed_bounds(
                    canvas_obj,
                    draw_mesh,
                    model,
                    t->material,
                    terrain_chunk_motion_key(t, idx, draw_mesh),
                    NULL,
                    NULL,
                    local_bounds_min,
                    local_bounds_max,
                    terrain_conservative_bounds,
                    terrain_disable_occlusion,
                    culling_pad);
                t->last_drawn_chunk_count++;
                if (lod >= 0 && lod < TERRAIN_LOD_LEVELS)
                    t->last_lod_counts[lod]++;
            }
        }
    }
}
