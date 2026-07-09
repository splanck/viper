//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/render/rt_canvas3d_render_pass.inc  (#included from rt_canvas3d.c;
// not a standalone translation unit) Purpose: Canvas3D shadow/main render passes and frame end
//===----------------------------------------------------------------------===//
static void canvas3d_refresh_texture_upload_bytes(rt_canvas3d *c) {
    uint64_t bytes = 0;

    if (c && c->backend && c->backend->get_texture_upload_bytes)
        bytes = c->backend->get_texture_upload_bytes(c->backend_ctx);
    if (c)
        c->last_texture_upload_bytes = bytes > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)bytes;
}

/// @brief Recompute the canvas's latest backend GPU frame-time counter.
static void canvas3d_refresh_frame_gpu_time(rt_canvas3d *c) {
    uint64_t us = 0;

    if (c && c->backend && c->backend->get_frame_gpu_time_us)
        us = c->backend->get_frame_gpu_time_us(c->backend_ctx);
    if (c)
        c->last_frame_gpu_time_us = us > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)us;
}

/// @brief Render one non-cascaded shadow light into the next available shadow slot.
/// @details Used both by the simple shadow path and by the cascaded path after the primary
///   directional light has consumed its cascade slots. The helper owns slot validation, light VP
///   construction, backend begin/draw/end, and writing the packed `shadow_lights` entry consumed by
///   main-pass light parameter patching.
/// @return 1 when a shadow slot was rendered and `c->shadow_count` advanced, 0 otherwise.
static int canvas3d_render_shadow_light_slot(
    rt_canvas3d *c,
    deferred_draw_t *cmds,
    const float shadow_world_min[3],
    const float shadow_world_max[3],
    const vgfx3d_light_params_t *selected_light,
    vgfx3d_light_params_t shadow_lights[VGFX3D_MAX_SHADOW_LIGHTS]) {
    vgfx3d_rendertarget_t *shadow_rt;
    vgfx3d_light_params_t light;
    int32_t shadow_resolution;
    float light_vp[16];
    int32_t slot;

    if (!c || !cmds || !shadow_world_min || !shadow_world_max || !selected_light ||
        !shadow_lights || c->shadow_count < 0 || c->shadow_count >= VGFX3D_MAX_SHADOW_LIGHTS)
        return 0;
    slot = c->shadow_count;
    light = *selected_light;
    light.shadow_cascade_count = 1;
    light.shadow_projection_type = light.type == 3 ? VGFX3D_SHADOW_PROJECTION_PERSPECTIVE
                                                   : VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC;
    if (!canvas3d_ensure_shadow_target_slot(c, slot, c->shadow_resolution))
        return 0;
    shadow_rt = c->shadow_rts[slot];
    if (!shadow_rt || !shadow_rt->depth_buf)
        return 0;
    shadow_resolution = canvas3d_shadow_slot_resolution(c, slot);
    if (shadow_resolution <= 0)
        return 0;
    if (!canvas3d_build_shadow_light_vp(
            shadow_world_min, shadow_world_max, &light, shadow_resolution, light_vp))
        return 0;

    memcpy(c->shadow_light_vps[slot], light_vp, sizeof(light_vp));
    shadow_lights[slot] = light;
    shadow_lights[slot].shadow_index = slot;
    c->backend->shadow_begin(
        c->backend_ctx, slot, shadow_rt->depth_buf, shadow_rt->width, shadow_rt->height, light_vp);
    for (int32_t i = 0; i < c->draw_count; i++) {
        if (!canvas3d_deferred_casts_shadow(&cmds[i]))
            continue;
        canvas3d_shadow_deferred(c, &cmds[i]);
    }
    c->backend->shadow_end(c->backend_ctx, slot, c->shadow_bias);
    c->shadow_count++;
    return 1;
}

/// @brief Render an omnidirectional point-light shadow as six perspective faces.
/// @details Claims VGFX3D_SHADOW_CUBE_FACES consecutive slots (atlas tiles on GPU
///   backends). The light entry advertises the FIRST slot with the CUBE projection
///   type; shaders select the face by the dominant axis of light->fragment.
/// @return 1 when all six faces rendered and the light was granted, 0 otherwise.
static int canvas3d_render_point_shadow_cube(
    rt_canvas3d *c,
    deferred_draw_t *cmds,
    const float shadow_world_min[3],
    const float shadow_world_max[3],
    const vgfx3d_light_params_t *selected_light,
    vgfx3d_light_params_t shadow_lights[VGFX3D_MAX_SHADOW_LIGHTS],
    int32_t slot_cap) {
    vgfx3d_light_params_t light;
    int32_t base;

    if (!c || !cmds || !shadow_world_min || !shadow_world_max || !selected_light ||
        !shadow_lights || c->shadow_count < 0)
        return 0;
    base = c->shadow_count;
    if (base + VGFX3D_SHADOW_CUBE_FACES > slot_cap)
        return 0;
    light = *selected_light;
    light.shadow_cascade_count = 1;
    light.shadow_projection_type = VGFX3D_SHADOW_PROJECTION_CUBE;

    for (int32_t face = 0; face < VGFX3D_SHADOW_CUBE_FACES; face++) {
        vgfx3d_rendertarget_t *shadow_rt;
        float light_vp[16];
        int32_t slot = base + face;

        if (!canvas3d_ensure_shadow_target_slot(c, slot, c->shadow_resolution))
            return 0;
        shadow_rt = c->shadow_rts[slot];
        if (!shadow_rt || !shadow_rt->depth_buf)
            return 0;
        if (!canvas3d_build_point_shadow_face_vp(
                shadow_world_min, shadow_world_max, &light, face, light_vp))
            return 0;
        memcpy(c->shadow_light_vps[slot], light_vp, sizeof(light_vp));
        c->backend->shadow_begin(c->backend_ctx,
                                 slot,
                                 shadow_rt->depth_buf,
                                 shadow_rt->width,
                                 shadow_rt->height,
                                 light_vp);
        for (int32_t i = 0; i < c->draw_count; i++) {
            if (!canvas3d_deferred_casts_shadow(&cmds[i]))
                continue;
            canvas3d_shadow_deferred(c, &cmds[i]);
        }
        c->backend->shadow_end(c->backend_ctx, slot, c->shadow_bias);
        c->shadow_count++;
    }
    shadow_lights[base] = light;
    shadow_lights[base].shadow_index = base;
    return 1;
}

/// @brief Render the directional, spot, and cascaded shadow-map pass for the queued
///   3D draws.
///   Extracted from rt_canvas3d_end; populates c->shadow_rts and stamps shadow params
///   onto the draw commands. Self-contained scratch (shadow_lights/cascades).
static void canvas3d_render_shadow_pass(rt_canvas3d *c, deferred_draw_t *cmds) {
    vgfx3d_light_params_t shadow_lights[VGFX3D_MAX_SHADOW_LIGHTS];
    vgfx3d_light_params_t selected_lights[VGFX3D_MAX_SHADOW_LIGHTS];

    memset(shadow_lights, 0, sizeof(shadow_lights));
    memset(selected_lights, 0, sizeof(selected_lights));

    int32_t shadow_request_count = 0;
    int32_t shadow_lights_granted = 0;
    c->last_shadow_slots_used = 0;
    c->last_shadow_requests_dropped = 0;
    if (c->shadows_enabled && c->backend->shadow_begin && c->backend->shadow_draw &&
        c->backend->shadow_end && canvas3d_ensure_shadow_targets(c, c->shadow_resolution)) {
        /* Backends without atlas-slot sampling cap the frame at the classic
         * per-texture slots so no light gets an unresolvable shadow index. */
        int32_t slot_cap =
            c->backend->shadow_atlas_slots ? VGFX3D_MAX_SHADOW_LIGHTS : VGFX3D_CSM_SLOTS;
        int32_t shadow_budget = c->shadow_budget;
        if (shadow_budget < 1 || shadow_budget > slot_cap)
            shadow_budget = slot_cap;
        int32_t selected_shadow_count = canvas3d_select_shadow_lights_from_draws(
            c, cmds, c->draw_count, selected_lights, shadow_budget, &shadow_request_count);
        float shadow_world_min[3];
        float shadow_world_max[3];
        int has_shadow_bounds = canvas3d_build_shadow_world_bounds(
            c, cmds, c->draw_count, shadow_world_min, shadow_world_max);

        if (has_shadow_bounds && selected_shadow_count > 0 && selected_lights[0].type == 0 &&
            c->shadow_cascade_count > 1) {
            vgfx3d_light_params_t primary_light = selected_lights[0];
            int32_t cascade_count = c->shadow_cascade_count;
            float cascade_splits[VGFX3D_CSM_SLOTS];
            float cascade_near = 0.0f;
            float cascade_far = 0.0f;

            if (cascade_count > VGFX3D_CSM_SLOTS)
                cascade_count = VGFX3D_CSM_SLOTS;
            if (!canvas3d_compute_shadow_cascade_splits(c,
                                                        shadow_world_min,
                                                        shadow_world_max,
                                                        cascade_count,
                                                        cascade_splits,
                                                        &cascade_near,
                                                        &cascade_far))
                cascade_count = 0;
            primary_light.shadow_index = 0;
            primary_light.shadow_cascade_count = cascade_count;
            primary_light.shadow_projection_type = VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC;
            memcpy(primary_light.shadow_cascade_splits,
                   cascade_splits,
                   sizeof(primary_light.shadow_cascade_splits));
            for (int32_t cascade = 0; cascade < cascade_count; cascade++) {
                vgfx3d_rendertarget_t *shadow_rt;
                int32_t shadow_resolution;
                float cascade_min[3];
                float cascade_max[3];
                float cascade_depth_near =
                    cascade == 0 ? cascade_near : cascade_splits[cascade - 1];
                float cascade_depth_far = cascade_splits[cascade];
                float light_vp[16];

                if (!canvas3d_ensure_shadow_target_slot(c, c->shadow_count, c->shadow_resolution))
                    continue;
                shadow_rt = c->shadow_rts[c->shadow_count];
                if (!shadow_rt || !shadow_rt->depth_buf)
                    continue;
                shadow_resolution = canvas3d_shadow_slot_resolution(c, c->shadow_count);
                if (shadow_resolution <= 0)
                    continue;
                if (!canvas3d_build_shadow_cascade_world_bounds(c,
                                                                cmds,
                                                                c->draw_count,
                                                                cascade_depth_near,
                                                                cascade_depth_far,
                                                                shadow_world_min,
                                                                shadow_world_max,
                                                                cascade_min,
                                                                cascade_max))
                    continue;
                if (!canvas3d_build_shadow_light_vp(
                        cascade_min, cascade_max, &primary_light, shadow_resolution, light_vp))
                    continue;

                memcpy(c->shadow_light_vps[c->shadow_count], light_vp, sizeof(light_vp));
                shadow_lights[c->shadow_count] = primary_light;
                shadow_lights[c->shadow_count].shadow_index = c->shadow_count;
                shadow_lights[c->shadow_count].shadow_cascade_count = cascade_count;
                shadow_lights[c->shadow_count].shadow_projection_type =
                    VGFX3D_SHADOW_PROJECTION_ORTHOGRAPHIC;
                memcpy(shadow_lights[c->shadow_count].shadow_cascade_splits,
                       cascade_splits,
                       sizeof(shadow_lights[c->shadow_count].shadow_cascade_splits));
                c->backend->shadow_begin(c->backend_ctx,
                                         c->shadow_count,
                                         shadow_rt->depth_buf,
                                         shadow_rt->width,
                                         shadow_rt->height,
                                         light_vp);
                for (int32_t i = 0; i < c->draw_count; i++) {
                    if (!canvas3d_deferred_casts_shadow(&cmds[i]))
                        continue;
                    if (!canvas3d_deferred_intersects_view_depth(
                            c, &cmds[i], cascade_depth_near, cascade_depth_far))
                        continue;
                    if (!canvas3d_deferred_intersects_world_bounds(
                            c, &cmds[i], cascade_min, cascade_max))
                        continue;
                    canvas3d_shadow_deferred(c, &cmds[i]);
                }
                c->backend->shadow_end(c->backend_ctx, c->shadow_count, c->shadow_bias);
                c->shadow_count++;
            }
            if (c->shadow_count > 0) {
                shadow_lights[0] = primary_light;
                shadow_lights[0].shadow_index = 0;
                shadow_lights_granted++;
            }
            for (int32_t slot = 1; has_shadow_bounds && slot < selected_shadow_count &&
                                   c->shadow_count < slot_cap;
                 slot++) {
                shadow_lights_granted +=
                    selected_lights[slot].type == 1
                        ? canvas3d_render_point_shadow_cube(c,
                                                            cmds,
                                                            shadow_world_min,
                                                            shadow_world_max,
                                                            &selected_lights[slot],
                                                            shadow_lights,
                                                            slot_cap)
                        : canvas3d_render_shadow_light_slot(c,
                                                            cmds,
                                                            shadow_world_min,
                                                            shadow_world_max,
                                                            &selected_lights[slot],
                                                            shadow_lights);
            }
        } else {
            for (int32_t slot = 0; has_shadow_bounds && slot < selected_shadow_count &&
                                   c->shadow_count < slot_cap;
                 slot++)
                shadow_lights_granted +=
                    selected_lights[slot].type == 1
                        ? canvas3d_render_point_shadow_cube(c,
                                                            cmds,
                                                            shadow_world_min,
                                                            shadow_world_max,
                                                            &selected_lights[slot],
                                                            shadow_lights,
                                                            slot_cap)
                        : canvas3d_render_shadow_light_slot(c,
                                                            cmds,
                                                            shadow_world_min,
                                                            shadow_world_max,
                                                            &selected_lights[slot],
                                                            shadow_lights);
        }
        c->last_shadow_slots_used = c->shadow_count;
        c->last_shadow_requests_dropped = shadow_request_count > shadow_lights_granted
                                              ? shadow_request_count - shadow_lights_granted
                                              : 0;
    }

    for (int32_t i = 0; i < c->draw_count; i++)
        canvas3d_apply_shadow_light_params(
            cmds[i].lights, cmds[i].light_count, shadow_lights, c->shadow_count);

    /* Shadow patching mutated every queued light snapshot, so the pre-patch
     * revision stamps are stale. Remap each run of identical old stamps to a
     * fresh stamp (preserving distinctness so mid-frame light changes never
     * alias) and invalidate the snapshot cache for the next frame. */
    if (c->shadow_count > 0 && c->draw_count > 0) {
        uint32_t remap_from = 0;
        uint32_t remap_to = 0;
        c->last_light_snapshot_valid = 0;
        for (int32_t i = 0; i < c->draw_count; i++) {
            if (cmds[i].light_count <= 0 || cmds[i].cmd.lights_revision == 0)
                continue;
            if (remap_to != 0 && cmds[i].cmd.lights_revision == remap_from) {
                cmds[i].cmd.lights_revision = remap_to;
                continue;
            }
            remap_from = cmds[i].cmd.lights_revision;
            c->lights_revision++;
            if (c->lights_revision == 0)
                c->lights_revision = 1;
            remap_to = c->lights_revision;
            cmds[i].cmd.lights_revision = remap_to;
        }
    }
}

/// @brief Render the main 3D pass: frustum/occlusion culling then opaque+blended draws.
///   Extracted from rt_canvas3d_end; reads c->cached_vp and the queued draw commands.
static void canvas3d_render_main_pass(rt_canvas3d *c, deferred_draw_t *cmds) {
    vgfx3d_frustum_t visibility_frustum;
    canvas3d_occlusion_grid_t occlusion_grid;
    int use_occlusion_grid = c->occlusion_culling ? 1 : 0;
    int sort_opaque = use_occlusion_grid || c->opaque_depth_sorting;
    int state_sort_opaque = sort_opaque && !canvas3d_backend_uses_software_raster(c);

    if (use_occlusion_grid) {
        canvas3d_occlusion_history_prune(c);
        canvas3d_occlusion_grid_clear(&occlusion_grid);
    }

    if (c->frustum_culling || c->occlusion_culling) {
        vgfx3d_frustum_extract(&visibility_frustum, c->cached_vp);
        for (int32_t i = 0; i < c->draw_count; i++) {
            if (cmds[i].pass_kind == DEFERRED_PASS_MAIN) {
                cmds[i].visible =
                    (int8_t)canvas3d_deferred_intersects_frustum(c, &cmds[i], &visibility_frustum);
                if (!cmds[i].visible) {
                    c->last_occluded_draw_count++;
                    c->last_frustum_culled_draw_count++;
                }
            }
        }
    } else {
        for (int32_t i = 0; i < c->draw_count; i++) {
            if (cmds[i].pass_kind == DEFERRED_PASS_MAIN)
                cmds[i].visible = 1;
        }
    }

    {
        int32_t opaque_count = 0;
        int32_t trans_count = 0;
        int32_t staged_total;
        for (int32_t i = 0; i < c->draw_count; i++) {
            if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || !cmds[i].visible)
                continue;
            if (cmds[i].requires_blend)
                trans_count++;
            else
                opaque_count++;
        }
        staged_total = opaque_count + trans_count;
        if (staged_total > 0 &&
            ensure_deferred_capacity(&c->trans_cmds, &c->trans_capacity, staged_total)) {
            deferred_draw_t *opaque = (deferred_draw_t *)c->trans_cmds;
            deferred_draw_t *trans = opaque + opaque_count;
            int32_t oi = 0;
            int32_t ti = 0;
            for (int32_t i = 0; i < c->draw_count; i++) {
                if (cmds[i].pass_kind != DEFERRED_PASS_MAIN || !cmds[i].visible)
                    continue;
                if (cmds[i].requires_blend)
                    trans[ti++] = cmds[i];
                else
                    opaque[oi++] = cmds[i];
            }
            if (sort_opaque && opaque_count > 1 &&
                !(state_sort_opaque
                      ? canvas3d_sort_opaque_draws_state_first(c, opaque, opaque_count)
                      : canvas3d_sort_opaque_draws_depth_first(c, opaque, opaque_count))) {
                rt_trap("Canvas3D.End: deferred opaque sort scratch allocation failed");
                return;
            }
            for (int32_t i = 0; i < opaque_count; i++) {
                if (use_occlusion_grid &&
                    canvas3d_occlusion_test_and_write(c, &occlusion_grid, &opaque[i])) {
                    c->last_occluded_draw_count++;
                    c->last_cpu_occluded_draw_count++;
                    continue;
                }
                canvas3d_submit_deferred(c, &opaque[i]);
            }
            /* Plan 10: snapshot opaque depth for soft particles/SSR before any
             * blend draw samples it. Skipped when nothing transparent follows. */
            if (trans_count > 0 && c->backend->resolve_opaque_targets)
                c->backend->resolve_opaque_targets(c->backend_ctx);
            if (trans_count > 1 && !canvas3d_sort_transparent_draws(c, trans, trans_count)) {
                rt_trap("Canvas3D.End: deferred transparent sort scratch allocation failed");
                return;
            }
            for (int32_t i = 0; i < trans_count; i++)
                canvas3d_submit_deferred(c, &trans[i]);
        } else if (staged_total > 0) {
            rt_trap("Canvas3D.End: deferred sort buffer allocation failed");
            return;
        }
    }
}

/// @brief End drawing for the current scene or 2D pass and flush deferred draws.
/// @details Processes the deferred queue built during Begin/DrawMesh calls
///          in this strict pass order:
///          1. Skybox (if attached) into the scene color buffer.
///          2. Shadow pass: build a tight ortho light-VP, depth-render
///             every opaque main draw into the shadow map.
///          3. Opaque main draws — front-to-back on software when enabled
///             (maximizes early-Z rejection), state-grouped on GPU backends.
///          4. Translucent main draws — sorted back-to-front so alpha
///             blends compose correctly.
///          5. Pre/post-3D HUD overlay draws via `begin_overlay_frame`.
///          Must be called after all DrawMesh calls and before finalization or Flip.
///          Side effects: increments `frame_serial`, clears `draw_count`,
///          drains temp-buffer + temp-object queues, resets `in_frame`.
///          Post-FX, final overlay replay, screenshots, and presentation happen
///          in `FinalizeFrame`, `ScreenshotFinal`, and `Flip`, not here.
void rt_canvas3d_end(void *obj) {
    deferred_draw_t *cmds;
    int32_t queued_draw_count;
    int32_t main_count = 0;
    int32_t overlay_count = 0;

    rt_canvas3d *c = rt_canvas3d_checked_or_stack(obj);
    if (!c)
        return;
    if (!c->in_frame)
        return;
    if (!c->backend) {
        c->in_frame = 0;
        c->frame_is_2d = 0;
        c->frame_is_view_model = 0;
        c->last_draw_count = 0;
        c->last_texture_upload_bytes = 0;
        c->last_frame_gpu_time_us = 0;
        c->draw_count = 0;
        canvas3d_clear_temp_buffers(c);
        canvas3d_clear_temp_objects(c);
        return;
    }

    /* Any frame ending into an offscreen target changes its contents (even a
     * clear-only frame) — advance its revision so material mirrors refresh. */
    if (c->render_target)
        c->render_target->content_revision++;

    cmds = (deferred_draw_t *)c->draw_cmds;
    queued_draw_count = c->draw_count;
    canvas3d_assign_occlusion_keys(c, cmds, queued_draw_count);

    if (c->skybox) {
        if (!rt_g3d_has_class(c->skybox, RT_G3D_CUBEMAP3D_CLASS_ID)) {
            c->skybox = NULL;
            rt_canvas3d_invalidate_skybox_cache(c);
        } else if (!rt_cubemap3d_is_complete(c->skybox)) {
            if (rt_obj_release_check0(c->skybox))
                rt_obj_free(c->skybox);
            c->skybox = NULL;
            rt_canvas3d_invalidate_skybox_cache(c);
        }
    }

    if (!c->frame_is_2d && !c->frame_is_view_model && c->skybox) {
        uint8_t *out_pixels = NULL;
        int32_t out_w = 0;
        int32_t out_h = 0;
        int32_t out_stride = 0;

        if (c->backend->draw_skybox) {
            c->backend->draw_skybox(c->backend_ctx, c->skybox);
        } else {
            if (c->render_target) {
                if (vgfx3d_rendertarget_ensure_color(c->render_target)) {
                    out_pixels = c->render_target->color_buf;
                    out_w = c->render_target->width;
                    out_h = c->render_target->height;
                    out_stride = c->render_target->stride;
                }
            } else {
                vgfx_framebuffer_t fb;
                if (c->gfx_win && vgfx_get_framebuffer(c->gfx_win, &fb)) {
                    out_pixels = fb.pixels;
                    out_w = fb.width;
                    out_h = fb.height;
                    out_stride = fb.stride;
                }
            }
        }

        if (!c->backend->draw_skybox && out_pixels && !canvas3d_backend_owns_gpu_rtt(c) &&
            canvas3d_ensure_skybox_cpu_cache(c, out_w, out_h)) {
            canvas3d_blit_skybox_cpu_cache(c, out_pixels, out_w, out_h, out_stride);
            if (c->render_target)
                c->render_target->color_dirty = 1;
            if (c->render_target)
                c->render_target->hdr_color_valid = 0;
        }
    }

    for (int32_t i = 0; i < c->draw_count; i++) {
        if (cmds[i].pass_kind == DEFERRED_PASS_MAIN)
            main_count++;
        else if (cmds[i].pass_kind == DEFERRED_PASS_SCREEN_OVERLAY)
            overlay_count++;
    }
    c->last_draw_count = main_count;

    if (main_count == 0 && overlay_count == 0) {
        c->backend->end_frame(c->backend_ctx);
        canvas3d_refresh_texture_upload_bytes(c);
        canvas3d_refresh_frame_gpu_time(c);
        c->in_frame = 0;
        c->frame_is_2d = 0;
        c->frame_is_view_model = 0;
        c->draw_count = 0;
        canvas3d_clear_temp_buffers(c);
        canvas3d_clear_temp_objects(c);
        return;
    }

    if (!c->frame_is_view_model) {
        c->shadow_count = 0;
        memset(c->shadow_light_vps, 0, sizeof(c->shadow_light_vps));
    }
    if (!c->frame_is_2d && !c->frame_is_view_model && main_count > 0) {
        canvas3d_render_shadow_pass(c, cmds);
    }

    if (main_count > 0) {
        canvas3d_render_main_pass(c, cmds);
    }

    c->backend->end_frame(c->backend_ctx);
    canvas3d_refresh_texture_upload_bytes(c);
    canvas3d_refresh_frame_gpu_time(c);
    c->in_frame = 0;

    if (!c->frame_is_2d && overlay_count > 0) {
        if (canvas3d_begin_overlay_frame(c, 1)) {
            for (int32_t i = 0; i < queued_draw_count; i++) {
                if (cmds[i].pass_kind != DEFERRED_PASS_SCREEN_OVERLAY)
                    continue;
                canvas3d_submit_screen_overlay_deferred(c, &cmds[i]);
            }
            c->backend->end_frame(c->backend_ctx);
            canvas3d_refresh_texture_upload_bytes(c);
            canvas3d_refresh_frame_gpu_time(c);
        }
    }

    c->in_frame = 0;
    c->frame_is_2d = 0;
    c->frame_is_view_model = 0;
    c->draw_count = 0;
    canvas3d_clear_temp_buffers(c);
    canvas3d_clear_temp_objects(c);
}
