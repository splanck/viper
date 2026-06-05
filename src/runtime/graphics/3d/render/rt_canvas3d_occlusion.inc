//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/render/rt_canvas3d_occlusion.inc  (#included from rt_canvas3d.c; not a standalone translation unit)
// Purpose: Canvas3D deferred world-bounds, shadow-cascade splits, frustum tests, and CPU occlusion grid/history
//===----------------------------------------------------------------------===//

/// @brief Hash one float into a stable CPU-occlusion identity fingerprint.
/// @details The occlusion history table must not depend on queue position because streaming,
///   LOD, and debug draws can legitimately change submission order between frames. Quantizing
///   the model matrix keeps static objects stable while forcing moving objects to re-prove
///   visibility instead of inheriting stale covered streaks from an old screen position.
static uint64_t canvas3d_occlusion_mix_quantized_float(uint64_t key, float value) {
    double scaled;
    int64_t quantized;
    if (!isfinite(value))
        value = 0.0f;
    scaled = floor((double)value * 1024.0 + ((value >= 0.0f) ? 0.5 : -0.5));
    if (scaled > (double)INT64_MAX)
        quantized = INT64_MAX;
    else if (scaled < (double)INT64_MIN)
        quantized = INT64_MIN;
    else
        quantized = (int64_t)scaled;
    return canvas3d_hash_u64(key ^ (uint64_t)quantized);
}

/// @brief Return the stable base identity used by CPU occlusion-history hashing.
/// @details Uses the material/geometry sort identity when available and falls back to the queued
///   vertex pointer for synthetic draws. This intentionally ignores `occlusion_key`, which may
///   already include duplicate-occurrence disambiguation after the draw has been queued.
static uintptr_t canvas3d_occlusion_base_identity(const deferred_draw_t *dd) {
    uintptr_t base_key;
    if (!dd)
        return 0;
    base_key = dd->stable_sort_id ? dd->stable_sort_id : (uintptr_t)dd->cmd.vertices;
    return base_key;
}

/// @brief Build a queue-order-independent draw fingerprint for CPU occlusion history.
/// @details Combines persistent mesh/material identity, pass/kind, instance count, and a quantized
///   model matrix. It separates ordinary repeated meshes at different transforms without using the
///   global enqueue index, so unrelated draw insertion does not churn occlusion history.
static uintptr_t canvas3d_occlusion_draw_fingerprint(const deferred_draw_t *dd) {
    uint64_t key;
    uintptr_t base_key;
    base_key = canvas3d_occlusion_base_identity(dd);
    if (!base_key)
        return 0;
    key = canvas3d_hash_u64((uint64_t)base_key ^ ((uint64_t)dd->kind << 56u));
    key = canvas3d_hash_u64(key ^ ((uint64_t)dd->pass_kind << 48u));
    key = canvas3d_hash_u64(key ^ ((uint64_t)(uint32_t)dd->instance_count << 32u));
    for (int i = 0; i < 12; i++)
        key = canvas3d_occlusion_mix_quantized_float(key, dd->cmd.model_matrix[i]);
    key = canvas3d_hash_u64(key);
    return key ? (uintptr_t)key : base_key;
}

/// @brief Count prior queued draws with the same occlusion fingerprint.
/// @details Identical duplicate submissions need distinct history entries or they repeatedly reset
///   a shared covered streak within the same frame. Counting only prior matching fingerprints gives
///   duplicates stable per-peer ordinals while avoiding the old global enqueue-index dependency.
static int32_t canvas3d_occlusion_prior_duplicate_count(const rt_canvas3d *c,
                                                        uintptr_t fingerprint,
                                                        int32_t prior_count) {
    int32_t count = 0;
    const deferred_draw_t *queued;
    if (!c || !fingerprint || !c->draw_cmds || prior_count <= 0)
        return 0;
    if (prior_count > c->draw_count)
        prior_count = c->draw_count;
    queued = (const deferred_draw_t *)c->draw_cmds;
    for (int32_t i = 0; i < prior_count; ++i) {
        if (queued[i].occlusion_fingerprint == fingerprint)
            count++;
    }
    return count;
}

/// @brief Build the final CPU occlusion-history key for a deferred draw.
/// @details The base fingerprint is independent of unrelated queue order. A stable duplicate
///   ordinal is then folded in only when identical earlier fingerprints already exist this frame,
///   preserving one history slot per duplicated draw and preventing false flicker/collapse.
static uintptr_t canvas3d_occlusion_draw_key(const rt_canvas3d *c,
                                             deferred_draw_t *dd,
                                             int32_t prior_count) {
    uintptr_t fingerprint;
    int32_t duplicate_index;
    uint64_t key;
    if (!dd)
        return 0;
    fingerprint = canvas3d_occlusion_draw_fingerprint(dd);
    dd->occlusion_fingerprint = fingerprint;
    if (!fingerprint)
        return 0;
    duplicate_index = canvas3d_occlusion_prior_duplicate_count(c, fingerprint, prior_count);
    if (duplicate_index <= 0)
        return fingerprint;
    key = canvas3d_hash_u64((uint64_t)fingerprint ^
                            ((uint64_t)(uint32_t)duplicate_index + 0x9E3779B97F4A7C15ull));
    return key ? (uintptr_t)key : fingerprint;
}

/// @brief Append a fully prepared deferred draw and stamp shared queue metadata.
/// @details Manual mesh draws and helper-built draws both need stable enqueue order and cached
/// world
///   bounds before end-of-frame sorting/culling. Centralizing that bookkeeping prevents one queue
///   path from diverging and reintroducing unstable transparent ordering.
static int canvas3d_append_prepared_deferred_draw(rt_canvas3d *c, deferred_draw_t *dd) {
    if (!c || !dd)
        return 0;
    if (!ensure_deferred_capacity(&c->draw_cmds, &c->draw_capacity, c->draw_count + 1))
        return 0;
    dd->enqueue_index = c->draw_count;
    dd->occlusion_key = canvas3d_occlusion_draw_key(c, dd, c->draw_count);
    canvas3d_cache_deferred_world_bounds(dd);
    ((deferred_draw_t *)c->draw_cmds)[c->draw_count++] = *dd;
    return 1;
}

/// @brief Append a draw to the deferred-draw queue (transparency / sort path).
///
/// The queue is dispatched at end-of-frame in sorted order. Allocation failure
/// traps instead of submitting immediately, because bypassing the queue breaks
/// transparent ordering and can render a visibly incorrect frame.
static int canvas3d_enqueue_draw(rt_canvas3d *c,
                                 const vgfx3d_draw_cmd_t *cmd,
                                 deferred_draw_kind_t kind,
                                 deferred_pass_t pass_kind,
                                 const float *instance_matrices,
                                 int32_t instance_count,
                                 int include_lights,
                                 int8_t wireframe,
                                 int8_t backface_cull,
                                 float sort_key,
                                 const float *local_bounds_min,
                                 const float *local_bounds_max) {
    deferred_draw_t dd;

    if (!c || !cmd)
        return 0;
    canvas3d_fill_deferred_draw(c,
                                &dd,
                                cmd,
                                kind,
                                pass_kind,
                                instance_matrices,
                                instance_count,
                                include_lights,
                                wireframe,
                                backface_cull,
                                sort_key,
                                local_bounds_min,
                                local_bounds_max);
    if (!canvas3d_append_prepared_deferred_draw(c, &dd)) {
        rt_trap("Canvas3D: deferred draw queue allocation failed");
        return 0;
    }
    return 1;
}

/// @brief Append a draw to the final-overlay queue for replay after post-FX.
static int canvas3d_enqueue_final_overlay_draw(rt_canvas3d *c,
                                               const vgfx3d_draw_cmd_t *cmd,
                                               const float *local_bounds_min,
                                               const float *local_bounds_max) {
    deferred_draw_t *dd;

    if (!c || !cmd)
        return 0;
    if (!ensure_deferred_capacity(
            &c->final_overlay_cmds, &c->final_overlay_capacity, c->final_overlay_count + 1))
        return 0;
    dd = &((deferred_draw_t *)c->final_overlay_cmds)[c->final_overlay_count++];
    canvas3d_fill_deferred_draw(c,
                                dd,
                                cmd,
                                DEFERRED_DRAW_MESH,
                                DEFERRED_PASS_MAIN,
                                NULL,
                                0,
                                0,
                                0,
                                0,
                                0.0f,
                                local_bounds_min,
                                local_bounds_max);
    dd->enqueue_index = c->final_overlay_count - 1;
    return 1;
}

/// @brief Dispatch a single deferred draw to the backend.
///
/// Routes mesh kind directly to `submit_draw`. Routes instanced kind
/// to `submit_draw_instanced` if the backend supports it, otherwise
/// falls back to `canvas3d_submit_instanced_as_meshes`.
static void canvas3d_submit_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    if (!c || !dd)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        if (c->backend->submit_draw_instanced && dd->instance_count > 0) {
            c->backend->submit_draw_instanced(c->backend_ctx,
                                              c->gfx_win,
                                              &dd->cmd,
                                              dd->instance_matrices,
                                              dd->instance_count,
                                              dd->lights,
                                              dd->light_count,
                                              dd->ambient,
                                              dd->wireframe,
                                              dd->backface_cull);
            return;
        }
        canvas3d_submit_instanced_as_meshes(c, dd, 0);
        return;
    }
    canvas3d_submit_mesh(
        c, &dd->cmd, dd->lights, dd->light_count, dd->ambient, dd->wireframe, dd->backface_cull);
}

/// @brief Submit screen-space overlay geometry without writing depth.
static void canvas3d_submit_screen_overlay_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    deferred_draw_t overlay;

    if (!c || !dd)
        return;
    overlay = *dd;
    overlay.cmd.alpha_mode = RT_MATERIAL3D_ALPHA_MODE_BLEND;
    canvas3d_submit_deferred(c, &overlay);
}

/// @brief Dispatch a deferred draw to the shadow-pass path.
///
/// Same dispatch shape as `submit_deferred` but routes through the
/// backend's depth-only shadow draw entry. Instanced shadow draws
/// always decompose to per-mesh draws (most backends don't have an
/// instanced shadow path).
static void canvas3d_shadow_deferred(rt_canvas3d *c, const deferred_draw_t *dd) {
    vgfx3d_draw_cmd_t shadow_cmd;

    if (!c || !dd || !c->backend || !c->backend->shadow_draw)
        return;
    if (dd->kind == DEFERRED_DRAW_INSTANCED) {
        canvas3d_submit_instanced_as_meshes(c, dd, 1);
        return;
    }
    shadow_cmd = dd->cmd;
    canvas3d_prepare_shadow_draw_cmd(c, &shadow_cmd);
    c->backend->shadow_draw(c->backend_ctx, &shadow_cmd);
}

/// @brief In-place AABB union: `[io_min, io_max] ⊇ [mn, mx]`.
static void canvas3d_expand_bounds(float *io_min, float *io_max, const float *mn, const float *mx) {
    if (!io_min || !io_max || !mn || !mx)
        return;
    for (int i = 0; i < 3; i++) {
        if (mn[i] < io_min[i])
            io_min[i] = mn[i];
        if (mx[i] > io_max[i])
            io_max[i] = mx[i];
    }
}

/// @brief Compute a deferred draw's complete world-space AABB without reading cached fields.
/// @details Normal draws transform their one local mesh bound by `model_matrix`. Instanced draws
///   scan every instance matrix and union all transformed bounds, avoiding sampled sort/cull inputs
///   that can flicker when an unsampled instance becomes visible. Invalid transforms are skipped;
///   the function returns 0 only when no finite contribution remains.
static int canvas3d_compute_deferred_world_bounds_uncached(const deferred_draw_t *dd,
                                                           float *out_min,
                                                           float *out_max) {
    int8_t has_bounds = 0;

    if (!dd || !out_min || !out_max || !dd->has_local_bounds)
        return 0;
    if (dd->kind == DEFERRED_DRAW_INSTANCED && dd->instance_matrices && dd->instance_count > 0) {
        for (int32_t i = 0; i < dd->instance_count; i++) {
            double world_matrix[16];
            float instance_min[3];
            float instance_max[3];
            for (int j = 0; j < 16; j++)
                world_matrix[j] = (double)dd->instance_matrices[(size_t)i * 16u + (size_t)j];
            if (!vgfx3d_transform_aabb_checked(dd->local_bounds_min,
                                               dd->local_bounds_max,
                                               world_matrix,
                                               instance_min,
                                               instance_max))
                continue;
            if (!has_bounds) {
                memcpy(out_min, instance_min, sizeof(float) * 3);
                memcpy(out_max, instance_max, sizeof(float) * 3);
                has_bounds = 1;
            } else {
                canvas3d_expand_bounds(out_min, out_max, instance_min, instance_max);
            }
        }
        return has_bounds ? 1 : 0;
    }

    {
        double world_matrix[16];
        for (int j = 0; j < 16; j++)
            world_matrix[j] = (double)dd->cmd.model_matrix[j];
        return vgfx3d_transform_aabb_checked(
            dd->local_bounds_min, dd->local_bounds_max, world_matrix, out_min, out_max);
    }
}

/// @brief Store a deferred draw's world-space AABB on the draw itself for later passes.
/// @details Main-pass culling, occlusion, shadow bounds, and cascade assignment all need the same
///   world bound. Caching it once keeps those passes deterministic and removes repeated full scans
///   over large instanced batches.
static void canvas3d_cache_deferred_world_bounds(deferred_draw_t *dd) {
    if (!dd)
        return;
    dd->has_world_bounds = canvas3d_compute_deferred_world_bounds_uncached(
                               dd, dd->world_bounds_min, dd->world_bounds_max)
                               ? 1
                               : 0;
}

/// @brief Row-major 4×4 matrix multiply: `out = a * b`.
static void canvas3d_mul_mat4(const float *a, const float *b, float *out) {
    if (!a || !b || !out)
        return;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
                             a[r * 4 + 2] * b[2 * 4 + c] + a[r * 4 + 3] * b[3 * 4 + c];
        }
    }
}

/// @brief Compute world-space AABB for a deferred draw, unioning into `[io_min, io_max]`.
///
/// Iterates per-instance for instanced draws (so the shadow VP fits
/// every instance) or just transforms the single mesh AABB for normal
/// draws. `io_has_bounds` flips to true on first contribution.
static void canvas3d_accumulate_deferred_world_bounds(const deferred_draw_t *dd,
                                                      float *io_min,
                                                      float *io_max,
                                                      int8_t *io_has_bounds) {
    if (!dd || !io_min || !io_max || !io_has_bounds || !dd->has_local_bounds)
        return;

    if (dd->has_world_bounds) {
        if (!*io_has_bounds) {
            memcpy(io_min, dd->world_bounds_min, sizeof(float) * 3);
            memcpy(io_max, dd->world_bounds_max, sizeof(float) * 3);
            *io_has_bounds = 1;
        } else {
            canvas3d_expand_bounds(io_min, io_max, dd->world_bounds_min, dd->world_bounds_max);
        }
        return;
    }

    {
        float world_min[3];
        float world_max[3];
        if (!canvas3d_compute_deferred_world_bounds_uncached(dd, world_min, world_max))
            return;
        if (!*io_has_bounds) {
            memcpy(io_min, world_min, sizeof(float) * 3);
            memcpy(io_max, world_max, sizeof(float) * 3);
            *io_has_bounds = 1;
        } else {
            canvas3d_expand_bounds(io_min, io_max, world_min, world_max);
        }
    }
}

/// @brief Compute the camera-forward depth span of a world-space AABB.
static int canvas3d_world_bounds_view_depth_range(const rt_canvas3d *c,
                                                  const float *world_min,
                                                  const float *world_max,
                                                  float *out_near_depth,
                                                  float *out_far_depth) {
    float near_depth = FLT_MAX;
    float far_depth = -FLT_MAX;

    if (out_near_depth)
        *out_near_depth = 0.0f;
    if (out_far_depth)
        *out_far_depth = 0.0f;
    if (!c || !world_min || !world_max)
        return 0;
    for (int xi = 0; xi < 2; xi++) {
        for (int yi = 0; yi < 2; yi++) {
            for (int zi = 0; zi < 2; zi++) {
                float point[3];
                float depth;

                point[0] = xi ? world_max[0] : world_min[0];
                point[1] = yi ? world_max[1] : world_min[1];
                point[2] = zi ? world_max[2] : world_min[2];
                depth = canvas3d_depth_along_view(c, point);
                if (depth < near_depth)
                    near_depth = depth;
                if (depth > far_depth)
                    far_depth = depth;
            }
        }
    }
    if (!isfinite(near_depth) || !isfinite(far_depth))
        return 0;
    if (out_near_depth)
        *out_near_depth = near_depth;
    if (out_far_depth)
        *out_far_depth = far_depth;
    return 1;
}

/// @brief Build practical view-depth split distances for the active shadow-cascade count.
/// @details Prefer the active camera clip range over per-frame caster bounds so cascades do not
///   move every time an object streams, animates, or gets culled. If the cached camera range is
///   unavailable, fall back to the draw bounds to preserve shadows in unusual internal call paths.
///   Uses a uniform/logarithmic blend so near cascades get enough precision to avoid shimmer while
///   far cascades still cover the complete clip range.
static int canvas3d_compute_shadow_cascade_splits(const rt_canvas3d *c,
                                                  const float *world_min,
                                                  const float *world_max,
                                                  int32_t cascade_count,
                                                  float *out_splits,
                                                  float *out_near_depth,
                                                  float *out_far_depth) {
    float near_depth;
    float far_depth;
    float span;

    if (!out_splits || cascade_count <= 0)
        return 0;
    for (int32_t i = 0; i < VGFX3D_MAX_SHADOW_LIGHTS; i++)
        out_splits[i] = 0.0f;
    near_depth = c ? c->cached_cam_near : 0.0f;
    far_depth = c ? c->cached_cam_far : 0.0f;
    if (!isfinite(near_depth) || !isfinite(far_depth) || far_depth <= near_depth + 1e-3f) {
        if (!canvas3d_world_bounds_view_depth_range(
                c, world_min, world_max, &near_depth, &far_depth))
            return 0;
    }
    if (far_depth < near_depth) {
        float tmp = near_depth;
        near_depth = far_depth;
        far_depth = tmp;
    }
    if (far_depth - near_depth < 1e-3f)
        far_depth = near_depth + 1.0f;
    span = far_depth - near_depth;
    for (int32_t i = 0; i < cascade_count && i < VGFX3D_MAX_SHADOW_LIGHTS; i++) {
        const float p = (float)(i + 1) / (float)cascade_count;
        const float uniform_split = near_depth + span * p;
        float practical_split = uniform_split;
        if (near_depth > 1e-4f && far_depth > near_depth) {
            const float log_split = near_depth * powf(far_depth / near_depth, p);
            if (isfinite(log_split))
                practical_split = 0.5f * uniform_split + 0.5f * log_split;
        }
        if (i > 0 && practical_split <= out_splits[i - 1] + 1e-3f)
            practical_split = out_splits[i - 1] + 1e-3f;
        if (i == cascade_count - 1 || practical_split > far_depth)
            practical_split = far_depth;
        out_splits[i] = practical_split;
    }
    if (out_near_depth)
        *out_near_depth = near_depth;
    if (out_far_depth)
        *out_far_depth = far_depth;
    return 1;
}

/// @brief Build stable render-space bounds covered by one camera-depth shadow cascade.
/// @details Cascades must not resize when individual casters stream in/out or cross split
///   boundaries, because changing the fitted orthographic projection causes visible shadow shimmer.
///   This function therefore derives a conservative sphere from the camera depth interval and
///   forward vector, then returns its AABB in the same render space as queued draw bounds.
static int canvas3d_build_shadow_cascade_world_bounds(const rt_canvas3d *c,
                                                      const deferred_draw_t *cmds,
                                                      int32_t count,
                                                      float cascade_near_depth,
                                                      float cascade_far_depth,
                                                      const float *fallback_min,
                                                      const float *fallback_max,
                                                      float *out_min,
                                                      float *out_max) {
    float near_depth;
    float far_depth;
    float depth_mid;
    float radius;

    if (!c || !cmds || count <= 0 || !fallback_min || !fallback_max || !out_min || !out_max)
        return 0;
    (void)cmds;
    (void)count;
    near_depth = cascade_near_depth;
    far_depth = cascade_far_depth;
    if (!isfinite(near_depth) || !isfinite(far_depth) || far_depth <= near_depth) {
        memcpy(out_min, fallback_min, sizeof(float) * 3);
        memcpy(out_max, fallback_max, sizeof(float) * 3);
        return 1;
    }
    depth_mid = 0.5f * (near_depth + far_depth);
    radius = fmaxf(1.0f, fabsf(far_depth) * 0.75f + fabsf(far_depth - near_depth) * 0.25f);
    if (!isfinite(depth_mid) || !isfinite(radius)) {
        memcpy(out_min, fallback_min, sizeof(float) * 3);
        memcpy(out_max, fallback_max, sizeof(float) * 3);
        return 1;
    }
    for (int axis = 0; axis < 3; axis++) {
        float center = c->cached_cam_pos[axis] + c->cached_cam_forward[axis] * depth_mid;
        out_min[axis] = center - radius;
        out_max[axis] = center + radius;
    }
    return 1;
}

/// @brief True if a queued draw's world bounds overlap the supplied camera-depth interval.
/// @details Used during cascaded shadow rendering to avoid submitting every caster to every split.
///   Commands without usable bounds conservatively pass so procedural or corrupted bounds do not
///   drop valid shadows.
static int canvas3d_deferred_intersects_view_depth(const rt_canvas3d *c,
                                                   const deferred_draw_t *dd,
                                                   float near_depth,
                                                   float far_depth) {
    float draw_min[3];
    float draw_max[3];
    float draw_near;
    float draw_far;
    int8_t has_bounds = 0;

    if (!c || !dd)
        return 1;
    canvas3d_accumulate_deferred_world_bounds(dd, draw_min, draw_max, &has_bounds);
    if (!has_bounds)
        return 1;
    if (!canvas3d_world_bounds_view_depth_range(c, draw_min, draw_max, &draw_near, &draw_far))
        return 1;
    if (draw_far < draw_near) {
        float tmp = draw_near;
        draw_near = draw_far;
        draw_far = tmp;
    }
    return draw_far >= near_depth && draw_near <= far_depth;
}

/// @brief True if a deferred draw's cached world bounds overlap a supplied world-space AABB.
/// @details Cascaded shadow maps first select casters by camera-depth interval, then by the fitted
///   cascade volume. This second test prevents far-away casters that share a depth slice but sit
///   outside the cascade fit from wasting shadow-map draw calls and perturbing depth precision.
static int canvas3d_deferred_intersects_world_bounds(const deferred_draw_t *dd,
                                                     const float *world_min,
                                                     const float *world_max) {
    float draw_min[3];
    float draw_max[3];
    int8_t has_bounds = 0;

    if (!dd || !world_min || !world_max)
        return 1;
    canvas3d_accumulate_deferred_world_bounds(dd, draw_min, draw_max, &has_bounds);
    if (!has_bounds)
        return 1;
    for (int axis = 0; axis < 3; axis++) {
        if (draw_max[axis] < world_min[axis] || draw_min[axis] > world_max[axis])
            return 0;
    }
    return 1;
}

/// @brief Decide whether a queued deferred draw survives frustum culling.
/// @details Accumulates the union of the draw's world-space AABBs (one per
///   instance for instanced batches, otherwise the single transformed bound)
///   and tests it against the view frustum. Draws without usable bounds
///   (e.g. procedural geometry with no declared local AABB) conservatively
///   pass so nothing visible is clipped away.
/// @return 1 if the draw may be visible (keep it), 0 if definitely outside.
static int canvas3d_deferred_intersects_frustum(const deferred_draw_t *dd,
                                                const vgfx3d_frustum_t *frustum) {
    float world_min[3] = {0.0f, 0.0f, 0.0f};
    float world_max[3] = {0.0f, 0.0f, 0.0f};
    int8_t has_bounds = 0;

    if (!dd || !frustum)
        return 1;
    canvas3d_accumulate_deferred_world_bounds(dd, world_min, world_max, &has_bounds);
    if (!has_bounds)
        return 1;
    return vgfx3d_frustum_test_aabb_padded(frustum, world_min, world_max, dd->culling_pad) != 0;
}

/// @brief Reset the software occlusion grid (clear all cells to "not occluded").
static void canvas3d_occlusion_grid_clear(canvas3d_occlusion_grid_t *grid) {
    if (!grid)
        return;
    for (int32_t i = 0; i < CANVAS3D_OCCLUSION_GRID_CELLS; ++i) {
        grid->depth[i] = FLT_MAX;
        grid->covered[i] = 0;
    }
}

/// @brief Clear the CPU occlusion-history hash index while keeping allocated storage.
static void canvas3d_occlusion_history_hash_clear(rt_canvas3d *c) {
    if (!c || !c->occlusion_history_hash || c->occlusion_history_hash_capacity <= 0)
        return;
    for (int32_t i = 0; i < c->occlusion_history_hash_capacity; i++)
        c->occlusion_history_hash[i] = -1;
}

/// @brief Drop all CPU occlusion history while keeping allocated tables reusable.
/// @details Visibility-history streaks are tied to the prior camera/projection/render-target
///          relationship. Clearing counts but retaining capacity makes invalidation cheap during
///          floating-origin rebases, camera cuts, output resizes, and occlusion-setting toggles.
void canvas3d_clear_occlusion_history(rt_canvas3d *c) {
    if (!c)
        return;
    c->occlusion_history_count = 0;
    canvas3d_occlusion_history_hash_clear(c);
}

/// @brief Rebuild the CPU occlusion-history open-addressing index from compact entries.
static void canvas3d_occlusion_history_rebuild_hash(rt_canvas3d *c) {
    if (!c || !c->occlusion_history_hash || c->occlusion_history_hash_capacity <= 0)
        return;
    canvas3d_occlusion_history_hash_clear(c);
    int32_t mask = c->occlusion_history_hash_capacity - 1;
    for (int32_t i = 0; i < c->occlusion_history_count; i++) {
        uintptr_t key = c->occlusion_history[i].key;
        int32_t slot = (int32_t)(canvas3d_hash_u64(key) & (uint32_t)mask);
        for (int32_t probe = 0; probe < c->occlusion_history_hash_capacity; probe++) {
            if (c->occlusion_history_hash[slot] < 0) {
                c->occlusion_history_hash[slot] = i;
                break;
            }
            slot = (slot + 1) & mask;
        }
    }
}

/// @brief Ensure occlusion-history storage can hold @p needed entries and a sparse hash index.
static int canvas3d_ensure_occlusion_history_capacity(rt_canvas3d *c, int32_t needed) {
    int32_t new_cap;
    int32_t hash_cap;

    if (!c || needed < 0)
        return 0;
    if (c->occlusion_history_capacity < needed) {
        new_cap = c->occlusion_history_capacity > 0 ? c->occlusion_history_capacity : 64;
        while (new_cap < needed) {
            if (new_cap > INT32_MAX / 2)
                new_cap = needed;
            else
                new_cap *= 2;
        }
        if ((size_t)new_cap > SIZE_MAX / sizeof(*c->occlusion_history))
            return 0;
        rt_canvas3d_occlusion_history_entry *grown = (rt_canvas3d_occlusion_history_entry *)realloc(
            c->occlusion_history, (size_t)new_cap * sizeof(*grown));
        if (!grown)
            return 0;
        c->occlusion_history = grown;
        c->occlusion_history_capacity = new_cap;
    }
    hash_cap = canvas3d_next_power_of_two_i32(needed > 0 ? needed * 2 : 64);
    if (hash_cap < 64)
        hash_cap = 64;
    if (c->occlusion_history_hash_capacity < hash_cap) {
        if ((size_t)hash_cap > SIZE_MAX / sizeof(*c->occlusion_history_hash))
            return 0;
        int32_t *grown =
            (int32_t *)realloc(c->occlusion_history_hash, (size_t)hash_cap * sizeof(*grown));
        if (!grown)
            return 0;
        c->occlusion_history_hash = grown;
        c->occlusion_history_hash_capacity = hash_cap;
        canvas3d_occlusion_history_hash_clear(c);
        canvas3d_occlusion_history_rebuild_hash(c);
    }
    return 1;
}

/// @brief Find or optionally create an occlusion-history entry for @p key.
/// @details Returns NULL when the key is absent and @p create is false, or when allocation fails.
static rt_canvas3d_occlusion_history_entry *canvas3d_occlusion_history_entry(rt_canvas3d *c,
                                                                             uintptr_t key,
                                                                             int create) {
    int32_t mask;
    int32_t slot;

    if (!c || key == 0)
        return NULL;
    if (!canvas3d_ensure_occlusion_history_capacity(c,
                                                    c->occlusion_history_count + (create ? 1 : 0)))
        return NULL;
    mask = c->occlusion_history_hash_capacity - 1;
    slot = (int32_t)(canvas3d_hash_u64(key) & (uint32_t)mask);
    for (int32_t probe = 0; probe < c->occlusion_history_hash_capacity; probe++) {
        int32_t index = c->occlusion_history_hash[slot];
        if (index < 0) {
            if (!create)
                return NULL;
            index = c->occlusion_history_count++;
            c->occlusion_history[index].key = key;
            c->occlusion_history[index].covered_streak = 0;
            c->occlusion_history[index].last_frame_seen = 0;
            c->occlusion_history_hash[slot] = index;
            return &c->occlusion_history[index];
        }
        if (index >= 0 && index < c->occlusion_history_count &&
            c->occlusion_history[index].key == key)
            return &c->occlusion_history[index];
        slot = (slot + 1) & mask;
    }
    return NULL;
}

/// @brief Number of consecutive covered frames required before CPU occlusion can hide a draw.
/// @details Even solid opaque meshes can flicker when their projected AABB is near a depth edge or
///          the camera jitters. Exact, depth-writing draws use a two-frame gate; conservative or
///          coverage-ambiguous draws require an additional confirmation frame when they are not
///          already excluded from testing.
static int32_t canvas3d_occlusion_required_covered_streak(const deferred_draw_t *dd) {
    const vgfx3d_draw_cmd_t *cmd;

    if (!dd || dd->conservative_bounds || dd->occlusion_test_disabled ||
        dd->occlusion_write_disabled || dd->requires_blend)
        return 3;
    cmd = &dd->cmd;
    return (cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_OPAQUE && !cmd->has_alpha_texture &&
            !canvas3d_cmd_has_runtime_deformation(cmd))
               ? 2
               : 3;
}

/// @brief Update occlusion history and decide whether a covered draw may be culled this frame.
/// @details A draw must be reported covered on at least two consecutive frames before the coarse
///          CPU grid culls it. This one-frame grace period prevents visible flicker from noisy
///          projected AABBs, moving occluders, or camera jitter.
static int canvas3d_occlusion_history_allows_cull(rt_canvas3d *c, const deferred_draw_t *dd) {
    uintptr_t key = dd ? dd->occlusion_key : 0;
    rt_canvas3d_occlusion_history_entry *entry = canvas3d_occlusion_history_entry(c, key, 1);
    if (!entry)
        return 0;
    if (entry->last_frame_seen == c->frame_serial - 1)
        entry->covered_streak++;
    else
        entry->covered_streak = 1;
    entry->last_frame_seen = c->frame_serial;
    return entry->covered_streak >= canvas3d_occlusion_required_covered_streak(dd);
}

/// @brief Reset the occlusion covered streak for a draw that remained visible.
static void canvas3d_occlusion_history_note_visible(rt_canvas3d *c, const deferred_draw_t *dd) {
    rt_canvas3d_occlusion_history_entry *entry;
    if (!c || !dd || !dd->occlusion_key)
        return;
    entry = canvas3d_occlusion_history_entry(c, dd->occlusion_key, 0);
    if (!entry)
        return;
    entry->covered_streak = 0;
    entry->last_frame_seen = c->frame_serial;
}

/// @brief Drop stale CPU occlusion-history entries and rebuild their hash table.
/// @details The hysteresis table is keyed by stable draw identity, but games can still create
///          many short-lived meshes, materials, and instances over a long session. Pruning entries
///          unseen for two seconds at 60 Hz keeps memory bounded without losing the recent
///          covered-streak state that prevents one-frame culling flicker.
static void canvas3d_occlusion_history_prune(rt_canvas3d *c) {
    int32_t write_index = 0;
    int64_t cutoff;

    if (!c || c->occlusion_history_count <= 0)
        return;
    if (c->frame_serial <= CANVAS3D_OCCLUSION_HISTORY_RETENTION_FRAMES)
        return;
    cutoff = c->frame_serial - CANVAS3D_OCCLUSION_HISTORY_RETENTION_FRAMES;
    for (int32_t read_index = 0; read_index < c->occlusion_history_count; read_index++) {
        rt_canvas3d_occlusion_history_entry entry = c->occlusion_history[read_index];
        if (entry.last_frame_seen >= cutoff) {
            if (write_index != read_index)
                c->occlusion_history[write_index] = entry;
            write_index++;
        }
    }
    if (write_index == c->occlusion_history_count)
        return;
    c->occlusion_history_count = write_index;
    canvas3d_occlusion_history_rebuild_hash(c);
}

/// @brief Return non-zero when scalar camera parameters differ enough to invalidate occlusion.
static int canvas3d_occlusion_param_changed(float a, float b) {
    float scale = fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
    return fabsf(a - b) > scale * 1e-4f;
}

/// @brief Return non-zero when the current camera/output state is a new occlusion space.
/// @details Small camera movement should keep history so occlusion can work. Large teleports,
///          sharp camera cuts, projection-range changes, output-size changes, and render-target
///          switches invalidate old covered streaks because the old screen coverage no longer
///          predicts the new one.
static int canvas3d_occlusion_state_changed_for_begin(const rt_canvas3d *c,
                                                      int32_t output_w,
                                                      int32_t output_h) {
    double dx;
    double dy;
    double dz;
    double dist2;
    float far_plane;
    float cut_distance;
    float dot;

    if (!c || !c->occlusion_state_valid)
        return 1;
    if (c->occlusion_last_render_target != (void *)c->render_target ||
        c->occlusion_last_output_width != output_w || c->occlusion_last_output_height != output_h ||
        c->occlusion_last_is_ortho != c->cached_cam_is_ortho)
        return 1;
    if (canvas3d_occlusion_param_changed(c->occlusion_last_near, c->cached_cam_near) ||
        canvas3d_occlusion_param_changed(c->occlusion_last_far, c->cached_cam_far))
        return 1;

    dx = c->cached_world_cam_pos[0] - c->occlusion_last_world_cam_pos[0];
    dy = c->cached_world_cam_pos[1] - c->occlusion_last_world_cam_pos[1];
    dz = c->cached_world_cam_pos[2] - c->occlusion_last_world_cam_pos[2];
    dist2 = dx * dx + dy * dy + dz * dz;
    far_plane =
        isfinite(c->cached_cam_far) && c->cached_cam_far > 1.0f ? c->cached_cam_far : 1000.0f;
    cut_distance = fmaxf(8.0f, fminf(far_plane * 0.20f, 100000.0f));
    if (isfinite(dist2) && dist2 > cut_distance * cut_distance)
        return 1;

    dot = c->cached_cam_forward[0] * c->occlusion_last_cam_forward[0] +
          c->cached_cam_forward[1] * c->occlusion_last_cam_forward[1] +
          c->cached_cam_forward[2] * c->occlusion_last_cam_forward[2];
    if (!isfinite(dot) || dot < 0.75f)
        return 1;
    return 0;
}

/// @brief Update the occlusion-space signature captured at Canvas3D.Begin.
/// @details Clears covered-streak history before storing the new signature when a discontinuity is
///          detected. This keeps the first frame after a camera cut or output switch conservative.
static void canvas3d_update_occlusion_state_for_begin(rt_canvas3d *c,
                                                      int32_t output_w,
                                                      int32_t output_h) {
    if (!c)
        return;
    if (canvas3d_occlusion_state_changed_for_begin(c, output_w, output_h))
        canvas3d_clear_occlusion_history(c);
    c->occlusion_state_valid = 1;
    c->occlusion_last_render_target = (void *)c->render_target;
    c->occlusion_last_output_width = output_w;
    c->occlusion_last_output_height = output_h;
    memcpy(c->occlusion_last_world_cam_pos,
           c->cached_world_cam_pos,
           sizeof(c->occlusion_last_world_cam_pos));
    memcpy(c->occlusion_last_cam_forward,
           c->cached_cam_forward,
           sizeof(c->occlusion_last_cam_forward));
    c->occlusion_last_near = c->cached_cam_near;
    c->occlusion_last_far = c->cached_cam_far;
    c->occlusion_last_is_ortho = c->cached_cam_is_ortho;
}

/// @brief Project a world point to occlusion-grid cell coordinates and a normalized depth.
/// @return 1 if the point projects in front of the camera and inside the grid, 0 otherwise.
static int canvas3d_project_world_point_to_occlusion_grid(const rt_canvas3d *c,
                                                          const float point[3],
                                                          float *out_x,
                                                          float *out_y) {
    float clip[4];
    float w;
    float ndc_x;
    float ndc_y;
    float clip_slack;

    if (!c || !point || !out_x || !out_y)
        return 0;
    clip[0] = c->cached_vp[0] * point[0] + c->cached_vp[1] * point[1] + c->cached_vp[2] * point[2] +
              c->cached_vp[3];
    clip[1] = c->cached_vp[4] * point[0] + c->cached_vp[5] * point[1] + c->cached_vp[6] * point[2] +
              c->cached_vp[7];
    clip[2] = c->cached_vp[8] * point[0] + c->cached_vp[9] * point[1] +
              c->cached_vp[10] * point[2] + c->cached_vp[11];
    clip[3] = c->cached_vp[12] * point[0] + c->cached_vp[13] * point[1] +
              c->cached_vp[14] * point[2] + c->cached_vp[15];
    w = clip[3];
    if (!isfinite(clip[0]) || !isfinite(clip[1]) || !isfinite(clip[2]) || !isfinite(w) ||
        w <= 1e-6f)
        return 0;
    clip_slack = fmaxf(1e-5f, fabsf(w) * 1e-5f);
    if (clip[2] < -w - clip_slack || clip[2] > w + clip_slack)
        return 0;
    ndc_x = clip[0] / w;
    ndc_y = clip[1] / w;
    if (!isfinite(ndc_x) || !isfinite(ndc_y))
        return 0;
    *out_x = (ndc_x * 0.5f + 0.5f) * (float)(CANVAS3D_OCCLUSION_GRID_W - 1);
    *out_y = (0.5f - ndc_y * 0.5f) * (float)(CANVAS3D_OCCLUSION_GRID_H - 1);
    return 1;
}

/// @brief Compute the occlusion-grid rectangle and conservative depths for a world-space AABB.
/// @details Projects the box corners to grid space. The candidate is tested at its nearest
///          depth, but visible occluders are written at their farthest depth. Writing the far
///          depth keeps large depth-spanning bounds such as floors from acting like near
///          full-screen walls in the coarse grid.
static int canvas3d_deferred_occlusion_rect(const rt_canvas3d *c,
                                            const deferred_draw_t *dd,
                                            int32_t *out_min_x,
                                            int32_t *out_min_y,
                                            int32_t *out_max_x,
                                            int32_t *out_max_y,
                                            float *out_test_depth,
                                            float *out_write_depth) {
    float world_min[3] = {0.0f, 0.0f, 0.0f};
    float world_max[3] = {0.0f, 0.0f, 0.0f};
    int8_t has_bounds = 0;
    float min_x = FLT_MAX;
    float min_y = FLT_MAX;
    float max_x = -FLT_MAX;
    float max_y = -FLT_MAX;
    float near_depth = 0.0f;
    float far_depth = 0.0f;

    if (!c || !dd || !out_min_x || !out_min_y || !out_max_x || !out_max_y || !out_test_depth ||
        !out_write_depth)
        return 0;
    if (dd->conservative_bounds)
        return 0;
    if (!isfinite(dd->sort_key))
        return 0;
    canvas3d_accumulate_deferred_world_bounds(dd, world_min, world_max, &has_bounds);
    if (!has_bounds)
        return 0;
    if (!canvas3d_world_bounds_view_depth_range(c, world_min, world_max, &near_depth, &far_depth))
        return 0;
    if (far_depth < near_depth) {
        float tmp = near_depth;
        near_depth = far_depth;
        far_depth = tmp;
    }
    if (far_depth - near_depth > fmaxf(8.0f, fabsf(near_depth) * 0.5f))
        return 0;

    for (int xi = 0; xi < 2; ++xi) {
        for (int yi = 0; yi < 2; ++yi) {
            for (int zi = 0; zi < 2; ++zi) {
                float point[3];
                float sx;
                float sy;
                point[0] = xi ? world_max[0] : world_min[0];
                point[1] = yi ? world_max[1] : world_min[1];
                point[2] = zi ? world_max[2] : world_min[2];
                if (!canvas3d_project_world_point_to_occlusion_grid(c, point, &sx, &sy))
                    return 0;
                if (sx < min_x)
                    min_x = sx;
                if (sy < min_y)
                    min_y = sy;
                if (sx > max_x)
                    max_x = sx;
                if (sy > max_y)
                    max_y = sy;
            }
        }
    }
    if (!isfinite(min_x) || !isfinite(min_y) || !isfinite(max_x) || !isfinite(max_y))
        return 0;
    if (max_x < 0.0f || max_y < 0.0f || min_x > (float)(CANVAS3D_OCCLUSION_GRID_W - 1) ||
        min_y > (float)(CANVAS3D_OCCLUSION_GRID_H - 1))
        return 0;
    if (min_x < 0.0f)
        min_x = 0.0f;
    if (min_y < 0.0f)
        min_y = 0.0f;
    if (max_x > (float)(CANVAS3D_OCCLUSION_GRID_W - 1))
        max_x = (float)(CANVAS3D_OCCLUSION_GRID_W - 1);
    if (max_y > (float)(CANVAS3D_OCCLUSION_GRID_H - 1))
        max_y = (float)(CANVAS3D_OCCLUSION_GRID_H - 1);

    *out_min_x = canvas3d_floor_to_i32_clamped(min_x, 0, CANVAS3D_OCCLUSION_GRID_W - 1);
    *out_min_y = canvas3d_floor_to_i32_clamped(min_y, 0, CANVAS3D_OCCLUSION_GRID_H - 1);
    *out_max_x = canvas3d_ceil_to_i32_clamped(max_x, 0, CANVAS3D_OCCLUSION_GRID_W - 1);
    *out_max_y = canvas3d_ceil_to_i32_clamped(max_y, 0, CANVAS3D_OCCLUSION_GRID_H - 1);
    {
        int32_t expand = c->occlusion_rect_expand_cells > 0 ? c->occlusion_rect_expand_cells : 0;
        if (expand > 4)
            expand = 4;
        *out_min_x = canvas3d_clamp_i32(*out_min_x - expand, 0, CANVAS3D_OCCLUSION_GRID_W - 1);
        *out_min_y = canvas3d_clamp_i32(*out_min_y - expand, 0, CANVAS3D_OCCLUSION_GRID_H - 1);
        *out_max_x = canvas3d_clamp_i32(*out_max_x + expand, 0, CANVAS3D_OCCLUSION_GRID_W - 1);
        *out_max_y = canvas3d_clamp_i32(*out_max_y + expand, 0, CANVAS3D_OCCLUSION_GRID_H - 1);
    }
    if (*out_max_x < *out_min_x || *out_max_y < *out_min_y)
        return 0;
    if (((*out_max_x - *out_min_x + 1) * (*out_max_y - *out_min_y + 1)) >
        (CANVAS3D_OCCLUSION_GRID_W * CANVAS3D_OCCLUSION_GRID_H) / 3)
        return 0;
    *out_test_depth = near_depth;
    *out_write_depth = far_depth;
    return 1;
}

/// @brief Whether the grid already occludes the given cell rect at @p depth.
/// @details The margin intentionally over-estimates visibility so the coarse grid cannot flicker
///   triangles out when projected bounds or depth values jitter by a fraction of a cell.
static int canvas3d_occlusion_grid_covers(const rt_canvas3d *c,
                                          const canvas3d_occlusion_grid_t *grid,
                                          int32_t min_x,
                                          int32_t min_y,
                                          int32_t max_x,
                                          int32_t max_y,
                                          float depth) {
    float margin;
    if (!grid || !isfinite(depth))
        return 0;
    margin = c && isfinite(c->occlusion_depth_margin) && c->occlusion_depth_margin >= 0.0f
                 ? c->occlusion_depth_margin
                 : 0.02f;
    margin += fabsf(depth) * 0.002f;
    if (c && isfinite(c->cached_cam_near) && isfinite(c->cached_cam_far) &&
        c->cached_cam_far > c->cached_cam_near)
        margin += (c->cached_cam_far - c->cached_cam_near) * 1e-5f;
    for (int32_t y = min_y; y <= max_y; ++y) {
        for (int32_t x = min_x; x <= max_x; ++x) {
            int32_t index = y * CANVAS3D_OCCLUSION_GRID_W + x;
            if (!grid->covered[index] || grid->depth[index] > depth - margin)
                return 0;
        }
    }
    return 1;
}

/// @brief Write an occluder's depth into the grid cells it covers (keeping the nearest per cell).
static void canvas3d_occlusion_grid_write(canvas3d_occlusion_grid_t *grid,
                                          int32_t min_x,
                                          int32_t min_y,
                                          int32_t max_x,
                                          int32_t max_y,
                                          float depth) {
    if (!grid || !isfinite(depth))
        return;
    for (int32_t y = min_y; y <= max_y; ++y) {
        for (int32_t x = min_x; x <= max_x; ++x) {
            int32_t index = y * CANVAS3D_OCCLUSION_GRID_W + x;
            if (!grid->covered[index] || depth < grid->depth[index])
                grid->depth[index] = depth;
            grid->covered[index] = 1;
        }
    }
}

/// @brief Return whether an AABB depth span is safe to write into the coarse occlusion grid.
/// @details The grid stores one conservative depth per 64x64 cell, so a deep AABB proxy can behave
///   like a wall and incorrectly hide visible triangles behind its nearest face. Testing remains
///   allowed for wider spans, but writing is limited to shallow proxies whose far/near range is
///   small relative to camera depth.
static int canvas3d_occlusion_depth_span_can_write(float test_depth, float write_depth) {
    float span;
    float limit;

    if (!isfinite(test_depth) || !isfinite(write_depth))
        return 0;
    span = write_depth - test_depth;
    if (span < 0.0f)
        span = -span;
    limit = fmaxf(2.0f, fabsf(test_depth) * 0.10f);
    return span <= limit;
}

/// @brief Test an object's bounds against the occlusion grid and record visible objects as
/// occluders.
/// @details Combines the cover test with a write so a single pass both culls hidden objects and
/// lets
///          a visible one occlude later draws.
/// @return Non-zero if the object is fully covered and can be culled, 0 if it remains visible.
static int canvas3d_occlusion_test_and_write(rt_canvas3d *c,
                                             canvas3d_occlusion_grid_t *grid,
                                             const deferred_draw_t *dd) {
    int32_t min_x;
    int32_t min_y;
    int32_t max_x;
    int32_t max_y;
    float test_depth;
    float write_depth;
    int covered = 0;
    if (!c || !grid || !dd)
        return 0;
    if (dd->occlusion_test_disabled && dd->occlusion_write_disabled)
        return 0;
    if (!canvas3d_deferred_occlusion_rect(
            c, dd, &min_x, &min_y, &max_x, &max_y, &test_depth, &write_depth))
        return 0;
    c->last_occlusion_candidate_count++;
    if (!dd->occlusion_test_disabled &&
        canvas3d_occlusion_grid_covers(c, grid, min_x, min_y, max_x, max_y, test_depth)) {
        covered = 1;
        if (canvas3d_occlusion_history_allows_cull(c, dd))
            return 1;
    }
    if (!covered)
        canvas3d_occlusion_history_note_visible(c, dd);
    if (!dd->occlusion_write_disabled &&
        canvas3d_occlusion_depth_span_can_write(test_depth, write_depth))
        canvas3d_occlusion_grid_write(grid, min_x, min_y, max_x, max_y, write_depth);
    return 0;
}
