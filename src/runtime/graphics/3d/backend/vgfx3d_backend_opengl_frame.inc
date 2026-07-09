//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_frame.inc  (#included from
// vgfx3d_backend_opengl.c; not a standalone translation unit) Purpose: OpenGL frame begin/end,
// submit, resize, readback, present, post-FX, and shadow passes
//===----------------------------------------------------------------------===//

/// @brief Backend `clear` op: store the clear color for use during `BeginFrame`.
///
/// We don't actually issue `glClear` here; that happens in
/// `BeginFrame` so we know the current target/load-state.
static void gl_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    ctx->clearR = r;
    ctx->clearG = g;
    ctx->clearB = b;
}

/// @brief Radical-inverse Halton sequence sample for TAA sub-pixel jitter (Plan 05).
static float gl_halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    while (index > 0) {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

/// @brief True when the exported postfx chain contains an effect of @p type_value.
static int gl_chain_has_effect(const vgfx3d_postfx_chain_t *chain, int32_t type_value) {
    if (!chain || !chain->enabled || chain->effect_count <= 0 || !chain->effects)
        return 0;
    for (int32_t i = 0; i < chain->effect_count; i++) {
        if (chain->effects[i].type == type_value)
            return 1;
    }
    return 0;
}

/// @brief Backend `begin_frame` — set up matrices, target, frame-history, and clear.
///
/// Per-frame setup steps:
///   1. Make GLX context current (multi-window apps may have switched).
///   2. Bump frame serial; reset shadow-active flag.
///   3. Detect overlay pass (skips scene composition).
///   4. Prune all caches (texture / cubemap / morph / mesh) to bound residency.
///   5. Compute VP + inverse VP from camera, capture fog state.
///   6. Roll the frame-history (current VP → previous VP, etc.).
///   7. If transitioning from scene to overlay, composite scene → backbuffer.
///   8. Choose active target kind, ensure offscreen targets exist.
///   9. Clear color/depth based on `load_existing_*` flags.
///  10. Reset blend / depth / cull / poly-mode state for clean draws.
static void gl_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    vgfx3d_opengl_frame_history_t history;
    static const float kIdentity4x4[16] = {
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        1.0f,
    };
    if (!ctx || !cam)
        return;
    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return;

    ctx->frame_serial++;
    /* Plan 07: cluster tables are camera-dependent; a light revision surviving
     * into a new frame does not imply the same froxel contents. */
    ctx->uploaded_cluster_revision = 0;
    /* Plan 10: the opaque depth snapshot is per-pass state. */
    ctx->opaque_depth_valid = 0;
    ctx->cam_znear = cam->znear;
    ctx->cam_zfar = cam->zfar;
    ctx->shadow_pass_slot = -1;
    ctx->shadow_count = 0;
    memset(ctx->shadow_complete, 0, sizeof(ctx->shadow_complete));
    ctx->current_pass_is_overlay = (!ctx->rtt_active && cam->load_existing_color) ? 1 : 0;
    if (!ctx->current_pass_is_overlay)
        ctx->texture_upload_bytes = 0;
    if (!ctx->current_pass_is_overlay) {
        ctx->scene_composited_to_backbuffer = 0;
        ctx->scene_postfx_pending = 0;
        /* Harvest last frame's depth-probe PBO (a frame boundary has passed, so the
         * map does not stall) and restart probe slot ids for the new frame. */
        if (ctx->depth_probe_pending_count > 0 && ctx->depth_probe_pbo) {
            gl.BindBuffer(GL_PIXEL_PACK_BUFFER, ctx->depth_probe_pbo);
            const float *mapped = (const float *)gl.MapBufferRange(
                GL_PIXEL_PACK_BUFFER,
                0,
                (GLsizeiptr)((size_t)ctx->depth_probe_pending_count * sizeof(float)),
                GL_MAP_READ_BIT);
            if (mapped) {
                memcpy(ctx->depth_probe_results,
                       mapped,
                       (size_t)ctx->depth_probe_pending_count * sizeof(float));
                ctx->depth_probe_result_count = ctx->depth_probe_pending_count;
                gl.UnmapBuffer(GL_PIXEL_PACK_BUFFER);
            }
            gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            ctx->depth_probe_pending_count = 0;
        }
        ctx->depth_probe_request_count = 0;
    }
    texture_cache_prune(ctx);
    cubemap_cache_prune(ctx);
    morph_cache_prune(ctx);
    gl_mesh_cache_prune(ctx);

    memcpy(ctx->view, cam->view, sizeof(ctx->view));
    memcpy(ctx->projection, cam->projection, sizeof(ctx->projection));
    mat4f_mul_gl(cam->projection, cam->view, ctx->vp);
    /* Plan 05 TAA: apply Halton(2,3) sub-pixel jitter to the scene-pass VP when the
     * latched chain contains a TAA resolve. Injected before inv/prev VP derivation so
     * every consumer this frame sees one consistent (jittered) VP; the resolve pass
     * removes the current-minus-previous jitter delta from velocities. Overlay passes
     * keep the previous state so UI stays fixed and the sequence advances once per
     * frame. */
    if (!ctx->rtt_active && !ctx->current_pass_is_overlay) {
        ctx->taa_prev_jitter_clip[0] = ctx->taa_jitter_clip[0];
        ctx->taa_prev_jitter_clip[1] = ctx->taa_jitter_clip[1];
        ctx->taa_jitter_clip[0] = 0.0f;
        ctx->taa_jitter_clip[1] = 0.0f;
        if (ctx->gpu_postfx_enabled && ctx->gpu_postfx_chain_valid && ctx->taa_program &&
            ctx->width > 0 && ctx->height > 0 &&
            gl_chain_has_effect(&ctx->gpu_postfx_chain, (int32_t)VGFX3D_POSTFX_EFFECT_TAA)) {
            uint32_t idx = (ctx->taa_frame_index++ & 7u) + 1u;
            float jx = (gl_halton(idx, 2) - 0.5f) * 2.0f / (float)ctx->width;
            float jy = (gl_halton(idx, 3) - 0.5f) * 2.0f / (float)ctx->height;
            ctx->taa_jitter_clip[0] = jx;
            ctx->taa_jitter_clip[1] = jy;
            /* Row-major clip-space translate: x-row += jx * w-row, y-row += jy * w-row. */
            for (int jc = 0; jc < 4; jc++) {
                ctx->vp[0 * 4 + jc] += jx * ctx->vp[3 * 4 + jc];
                ctx->vp[1 * 4 + jc] += jy * ctx->vp[3 * 4 + jc];
            }
        }
    }
    if (mat4f_inverse_gl(ctx->vp, ctx->inv_vp) != 0)
        memcpy(ctx->inv_vp, kIdentity4x4, sizeof(ctx->inv_vp));
    memcpy(ctx->cam_pos, cam->position, sizeof(float) * 3);
    memcpy(ctx->cam_forward, cam->forward, sizeof(float) * 3);
    ctx->cam_is_ortho = cam->is_ortho ? 1 : 0;
    ctx->fog_enabled = cam->fog_enabled;
    ctx->height_fog[0] = cam->height_fog_base;
    ctx->height_fog[1] = cam->height_fog_falloff;
    ctx->height_fog[2] = cam->height_fog_enabled
                             ? cam->height_fog_density * cam->height_fog_blend
                             : 0.0f;
    ctx->height_fog[3] = 0.0f;
    ctx->fog_near = cam->fog_near;
    ctx->fog_far = cam->fog_far;
    ctx->fog_color[0] = cam->fog_color[0];
    ctx->fog_color[1] = cam->fog_color[1];
    ctx->fog_color[2] = cam->fog_color[2];
    ctx->ibl_enabled = cam->ibl_enabled;
    ctx->ibl_intensity = cam->ibl_intensity;
    memcpy(ctx->ibl_sh, cam->ibl_sh, sizeof(ctx->ibl_sh));
    ctx->shadow_strength = cam->shadow_strength;
    ctx->shadow_slope_bias = cam->shadow_slope_bias;
    ctx->shadow_quality = cam->shadow_quality;
    memcpy(history.scene_vp, ctx->scene_vp, sizeof(history.scene_vp));
    memcpy(history.scene_prev_vp, ctx->scene_prev_vp, sizeof(history.scene_prev_vp));
    memcpy(history.scene_inv_vp, ctx->scene_inv_vp, sizeof(history.scene_inv_vp));
    memcpy(history.draw_prev_vp, ctx->draw_prev_vp, sizeof(history.draw_prev_vp));
    memcpy(history.scene_cam_pos, ctx->scene_cam_pos, sizeof(history.scene_cam_pos));
    history.scene_history_valid = ctx->scene_history_valid;
    vgfx3d_opengl_update_frame_history(
        &history, ctx->vp, ctx->inv_vp, cam->position, ctx->current_pass_is_overlay);
    memcpy(ctx->scene_vp, history.scene_vp, sizeof(ctx->scene_vp));
    memcpy(ctx->scene_prev_vp, history.scene_prev_vp, sizeof(ctx->scene_prev_vp));
    memcpy(ctx->scene_inv_vp, history.scene_inv_vp, sizeof(ctx->scene_inv_vp));
    memcpy(ctx->draw_prev_vp, history.draw_prev_vp, sizeof(ctx->draw_prev_vp));
    memcpy(ctx->scene_cam_pos, history.scene_cam_pos, sizeof(ctx->scene_cam_pos));
    ctx->scene_history_valid = history.scene_history_valid;

    if (ctx->current_pass_is_overlay && ctx->default_framebuffer_writable &&
        ctx->gpu_postfx_enabled && ctx->scene_postfx_pending &&
        !ctx->scene_composited_to_backbuffer) {
        if (draw_scene_texture(ctx, ctx->gpu_postfx_chain_valid ? &ctx->gpu_postfx_chain : NULL))
            ctx->scene_composited_to_backbuffer = 1;
    }

    if (ctx->current_pass_is_overlay && ctx->gpu_postfx_enabled)
        ctx->active_target_kind = VGFX3D_OPENGL_TARGET_SWAPCHAIN;
    else
        ctx->active_target_kind =
            vgfx3d_opengl_choose_target_kind(ctx->rtt_active, ctx->gpu_postfx_enabled);
    if (!ctx->current_pass_is_overlay &&
        ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SWAPCHAIN &&
        !ctx->default_framebuffer_writable) {
        ctx->active_target_kind = VGFX3D_OPENGL_TARGET_SCENE;
    }

    if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SCENE) {
        if (ensure_scene_targets(ctx, ctx->width, ctx->height) != 0)
            ctx->active_target_kind = VGFX3D_OPENGL_TARGET_SWAPCHAIN;
        else
            ctx->scene_postfx_pending = 1;
    } else if (!ctx->current_pass_is_overlay) {
        ctx->scene_postfx_pending = 0;
    }

    bind_main_framebuffer(ctx);

    if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_RTT) {
        GLbitfield clear_mask = 0;
        if (!cam->load_existing_color) {
            gl.ClearColor(ctx->clearR, ctx->clearG, ctx->clearB, 1.0f);
            clear_mask |= GL_COLOR_BUFFER_BIT;
        }
        if (!cam->load_existing_depth) {
            gl.ClearDepth(1.0);
            clear_mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (clear_mask)
            gl.Clear(clear_mask);
    } else if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SCENE) {
        if (!cam->load_existing_color) {
            gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl.ClearColor(ctx->clearR, ctx->clearG, ctx->clearB, 1.0f);
            gl.Clear(GL_COLOR_BUFFER_BIT);
            gl.DrawBuffer(GL_COLOR_ATTACHMENT1);
            gl.ClearColor(0.5f, 0.5f, 0.0f, 0.0f); /* alpha = SSR mask; empty = 0 */
            gl.Clear(GL_COLOR_BUFFER_BIT);
        }
        if (!cam->load_existing_depth) {
            gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl.ClearDepth(1.0);
            gl.Clear(GL_DEPTH_BUFFER_BIT);
        }
        {
            const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            gl.DrawBuffers(2, attachments);
        }
    } else {
        GLbitfield clear_mask = 0;
        if (!cam->load_existing_color) {
            gl.ClearColor(ctx->clearR, ctx->clearG, ctx->clearB, 1.0f);
            clear_mask |= GL_COLOR_BUFFER_BIT;
        }
        if (!cam->load_existing_depth) {
            gl.ClearDepth(1.0);
            clear_mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (clear_mask)
            gl.Clear(clear_mask);
    }
    gl.Disable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthMask(GL_TRUE);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.UseProgram(ctx->program);
    gl.BindVertexArray(ctx->vao);
}

/// @brief Backend `draw_skybox` op — type-cast wrapper over `gl_draw_skybox_impl`.
static void gl_draw_skybox(void *ctx_ptr, const void *cubemap_ptr) {
    gl_draw_skybox_impl((gl_context_t *)ctx_ptr, (const rt_cubemap3d *)cubemap_ptr);
}

/// @brief Backend `submit_draw` — emit a single mesh draw call.
///
/// Pipeline: backface cull / wireframe / poly-mode setup → mesh
/// VBO/IBO acquisition (cached or stream) → vertex attribute layout
/// → uniform upload (matrices, material, lights, fog, anim) →
/// texture binding → draw config (blend / depth) → `glDrawElements`.
/// Mark the RTT color dirty if rendering to RTT (so the next sync
/// readback knows to copy).
static void gl_submit_draw(void *ctx_ptr,
                           vgfx_window_t win,
                           const vgfx3d_draw_cmd_t *cmd,
                           const vgfx3d_light_params_t *lights,
                           int32_t light_count,
                           const float *ambient,
                           int8_t wireframe,
                           int8_t backface_cull) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLuint mesh_vbo, mesh_ibo;
    uint32_t index_count;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0) {
        if (ctx)
            ctx->stats.dropped_draws++;
        return;
    }
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0) {
        ctx->stats.dropped_draws++;
        return;
    }
    if (!prepare_mesh_buffers(ctx, cmd, &mesh_vbo, &mesh_ibo)) {
        ctx->stats.dropped_draws++;
        return;
    }

    gl.FrontFace(GL_CCW);
    if (backface_cull)
        gl.Enable(GL_CULL_FACE);
    else
        gl.Disable(GL_CULL_FACE);
    gl.PolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    gl_configure_draw_output(ctx, cmd);
    gl_apply_depth_bias(cmd);

    gl.UseProgram(ctx->program);
    upload_main_uniforms(ctx, cmd, lights, light_count, ambient, 0);
    bind_material_textures(ctx, cmd);
    configure_mesh_attributes(ctx, mesh_vbo, mesh_ibo);
    if (gl_debug_enabled()) {
        GLint draw_buffer = 0;
        GLint framebuffer = 0;
        GLint viewport[4] = {0, 0, 0, 0};
        GLint scissor = 0;
        GLint depth_test = 0;
        GLint cull_face = 0;
        GLint color_mask[4] = {0, 0, 0, 0};
        gl.GetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
        gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        gl.GetIntegerv(GL_VIEWPORT, viewport);
        gl.GetIntegerv(GL_SCISSOR_TEST, &scissor);
        gl.GetIntegerv(GL_DEPTH_TEST, &depth_test);
        gl.GetIntegerv(GL_CULL_FACE, &cull_face);
        gl.GetIntegerv(GL_COLOR_WRITEMASK, color_mask);
        fprintf(stderr,
                "[OpenGL] draw target=%d fbo=%d drawbuf=0x%04X viewport=%d,%d %dx%d "
                "scissor=%d depth=%d cull=%d mask=%d%d%d%d verts=%u indices=%u ortho=%d "
                "first=(%.3f,%.3f,%.3f) color=(%.3f,%.3f,%.3f,%.3f) unlit=%d depthoff=%d\n",
                (int)ctx->active_target_kind,
                (int)framebuffer,
                (unsigned)draw_buffer,
                (int)viewport[0],
                (int)viewport[1],
                (int)viewport[2],
                (int)viewport[3],
                (int)scissor,
                (int)depth_test,
                (int)cull_face,
                (int)color_mask[0],
                (int)color_mask[1],
                (int)color_mask[2],
                (int)color_mask[3],
                cmd->vertex_count,
                index_count,
                (int)ctx->cam_is_ortho,
                cmd->vertices[0].pos[0],
                cmd->vertices[0].pos[1],
                cmd->vertices[0].pos[2],
                cmd->vertices[0].color[0],
                cmd->vertices[0].color[1],
                cmd->vertices[0].color[2],
                cmd->vertices[0].color[3],
                (int)cmd->unlit,
                (int)cmd->disable_depth_test);
    }
    gl.DrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL);
    ctx->stats.draw_calls++;
    if (gl_debug_enabled()) {
        GLenum err = gl.GetError();
        if (err != GL_NO_ERROR)
            fprintf(stderr, "[OpenGL] draw error 0x%04X\n", (unsigned)err);
    }
    GL_CHECK();
}

/// @brief Backend `end_frame` op — mark RTT color as dirty for next sync.
///
/// Doesn't immediately read pixels back to the host RT buffer — that
/// happens lazily in `gl_sync_render_target_color` when the user
/// queries pixels. Keeps the GL pipeline async for RTT-heavy workloads.
/// @brief Issue this frame's queued scene-depth probe reads into the probe PBO.
/// @details Reads the scene FBO's depth attachment at each probe pixel; results are
///   harvested at the next frame's begin so the readback never syncs the pipeline.
static void gl_issue_depth_probes(gl_context_t *ctx) {
    if (!ctx || ctx->depth_probe_request_count <= 0)
        return;
    if (!ctx->scene_fbo || !ctx->scene_depth_tex || ctx->scene_width <= 0 ||
        ctx->scene_height <= 0) {
        ctx->depth_probe_request_count = 0;
        return;
    }
    if (!ctx->depth_probe_pbo) {
        gl.GenBuffers(1, &ctx->depth_probe_pbo);
        if (!ctx->depth_probe_pbo) {
            ctx->depth_probe_request_count = 0;
            return;
        }
        gl.BindBuffer(GL_PIXEL_PACK_BUFFER, ctx->depth_probe_pbo);
        gl.BufferData(GL_PIXEL_PACK_BUFFER,
                      (GLsizeiptr)(VGFX3D_DEPTH_PROBE_MAX * sizeof(float)),
                      NULL,
                      GL_STREAM_READ);
    } else {
        gl.BindBuffer(GL_PIXEL_PACK_BUFFER, ctx->depth_probe_pbo);
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, ctx->scene_fbo);
    for (int32_t i = 0; i < ctx->depth_probe_request_count; i++) {
        float ndc_x = ctx->depth_probe_requests[i][0];
        float ndc_y = ctx->depth_probe_requests[i][1];
        /* GL window origin is bottom-left, matching NDC y-up: no flip. */
        GLint px = (GLint)((ndc_x * 0.5f + 0.5f) * (float)ctx->scene_width);
        GLint py = (GLint)((ndc_y * 0.5f + 0.5f) * (float)ctx->scene_height);
        if (px < 0)
            px = 0;
        if (py < 0)
            py = 0;
        if (px >= ctx->scene_width)
            px = ctx->scene_width - 1;
        if (py >= ctx->scene_height)
            py = ctx->scene_height - 1;
        gl.ReadPixels(px,
                      py,
                      1,
                      1,
                      GL_DEPTH_COMPONENT,
                      GL_FLOAT,
                      (void *)((size_t)i * sizeof(float)));
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl.BindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    ctx->depth_probe_pending_count = ctx->depth_probe_request_count;
    ctx->depth_probe_request_count = 0;
}

static void gl_end_frame(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;

    gl_issue_depth_probes(ctx);
    if (ctx->rtt_active && ctx->rtt_target && ctx->rtt_color_tex) {
        ctx->rtt_color_dirty = 1;
        ctx->rtt_target->color_dirty = 1;
        ctx->rtt_target->hdr_color_valid = 0;
    }
}

/// @brief Backend hook: toggle GLX swap-interval vsync (no-op without EXT_swap_control).
static void gl_set_vsync(void *ctx_ptr, int8_t enabled) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx || !glx.SwapIntervalEXT)
        return;
    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return;
    glx.SwapIntervalEXT(ctx->display, ctx->window, enabled ? 1 : 0);
}

/// @brief Backend hook: register one NDC scene-depth probe for this frame.
static int32_t gl_queue_depth_probe(void *ctx_ptr, float ndc_x, float ndc_y) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    int32_t slot;
    if (!ctx || ctx->depth_probe_request_count >= VGFX3D_DEPTH_PROBE_MAX || !isfinite(ndc_x) ||
        !isfinite(ndc_y))
        return -1;
    slot = ctx->depth_probe_request_count++;
    ctx->depth_probe_requests[slot][0] = ndc_x;
    ctx->depth_probe_requests[slot][1] = ndc_y;
    return slot;
}

/// @brief Backend hook: read a probe result captured on the previous frame.
static float gl_read_depth_probe(void *ctx_ptr, int32_t slot) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx || slot < 0 || slot >= ctx->depth_probe_result_count)
        return -1.0f;
    return ctx->depth_probe_results[slot];
}

/// @brief Read the total bytes uploaded to GL textures so far this frame (diagnostics counter).
static uint64_t gl_get_texture_upload_bytes(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    return ctx ? ctx->texture_upload_bytes : 0;
}

/// @brief Backend `resize` op — handle window-size changes.
///
/// Updates dimensions and tears down the offscreen scene + postfx
/// readback targets so they get rebuilt at the new size on the next
/// `BeginFrame`. RTT targets aren't touched (they're sized by the
/// user-provided RT object, not the window).
static void gl_resize(void *ctx_ptr, int32_t w, int32_t h) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx || w <= 0 || h <= 0)
        return;
    ctx->width = w;
    ctx->height = h;
    ctx->scene_composited_to_backbuffer = 0;
    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return;
    if (!ctx->rtt_active) {
        destroy_scene_targets(ctx);
        destroy_postfx_readback_target(ctx);
        destroy_postfx_scratch_target(ctx);
        destroy_bloom_targets(ctx);
        destroy_taa_targets(ctx);
        destroy_ssr_target(ctx);
    }
}

/// @brief Ensure the context has a reusable host-side RGBA readback buffer.
/// @details GPU readback paths are called by screenshots, overlays, and the X11 fallback
///          presenter. Keeping one scratch buffer on the context avoids allocating and freeing
///          a large temporary every frame while still preserving the exact byte-count overflow
///          checks in each caller.
/// @param ctx OpenGL backend context that owns the scratch allocation.
/// @param bytes Minimum number of bytes required by the caller.
/// @return Pointer to at least @p bytes of writable storage, or NULL on allocation failure.
static uint8_t *gl_ensure_readback_scratch(gl_context_t *ctx, size_t bytes) {
    uint8_t *next;

    if (!ctx || bytes == 0)
        return NULL;
    if (ctx->readback_scratch_rgba && ctx->readback_scratch_bytes >= bytes)
        return ctx->readback_scratch_rgba;
    next = (uint8_t *)realloc(ctx->readback_scratch_rgba, bytes);
    if (!next)
        return NULL;
    ctx->readback_scratch_rgba = next;
    ctx->readback_scratch_bytes = bytes;
    return next;
}

/// @brief Backend `readback_rgba` op — copy current screen to a host RGBA buffer.
///
/// Two paths:
///   - Postfx readback: when scene is rendered offscreen with postfx
///     pending, runs the composite shader into a staging FBO and reads
///     from it. Gives the user the post-FX'd image even though it
///     hasn't been presented yet.
///   - Direct readback: pulls from the context's active default framebuffer buffer.
/// Always flips Y because GL has bottom-left origin; ours is top-left.
static int gl_readback_rgba(
    void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    int use_postfx_readback;
    int32_t source_w, source_h;
    int32_t copy_w, copy_h;
    GLuint readback_framebuffer = 0;
    GLenum readback_buffer = ctx ? ctx->default_draw_buffer : GL_BACK;
    uint8_t *tmp;
    size_t tmp_bytes;
    size_t dst_bytes;
    gl_framebuffer_state_t saved_state;

    if (!ctx || !dst_rgba || !vgfx3d_opengl_validate_rgba8_destination(w, h, stride, &dst_bytes))
        return 0;

    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return 0;
    use_postfx_readback =
        ctx->default_framebuffer_writable && !ctx->scene_composited_to_backbuffer &&
        vgfx3d_opengl_choose_readback_kind((ctx->gpu_postfx_enabled && ctx->scene_postfx_pending)
                                               ? 1
                                               : 0) == VGFX3D_OPENGL_READBACK_POSTFX_COMPOSITE;
    if (use_postfx_readback) {
        if (!ctx->scene_fbo || ctx->scene_width <= 0 || ctx->scene_height <= 0)
            return 0;
        if (!ctx->gpu_postfx_chain_valid || !gl_apply_postfx_chain(ctx,
                                                                   ctx->scene_color_tex,
                                                                   ctx->scene_width,
                                                                   ctx->scene_height,
                                                                   &ctx->gpu_postfx_chain,
                                                                   0,
                                                                   ctx->default_draw_buffer,
                                                                   1,
                                                                   &readback_framebuffer,
                                                                   &readback_buffer)) {
            readback_framebuffer = ctx->scene_fbo;
            readback_buffer = GL_COLOR_ATTACHMENT0;
        }
        source_w = ctx->scene_width;
        source_h = ctx->scene_height;
    } else if (!ctx->default_framebuffer_writable && ctx->scene_fbo) {
        readback_framebuffer = ctx->scene_fbo;
        readback_buffer = GL_COLOR_ATTACHMENT0;
        source_w = ctx->scene_width;
        source_h = ctx->scene_height;
        use_postfx_readback = 1;
    } else {
        source_w = ctx->width;
        source_h = ctx->height;
    }

    copy_w = source_w < w ? source_w : w;
    copy_h = source_h < h ? source_h : h;
    if (copy_w <= 0 || copy_h <= 0)
        return 0;
    if ((size_t)copy_w > SIZE_MAX / (size_t)copy_h ||
        (size_t)copy_w * (size_t)copy_h > SIZE_MAX / 4u) {
        return 0;
    }
    tmp_bytes = (size_t)copy_w * (size_t)copy_h * 4u;
    tmp = gl_ensure_readback_scratch(ctx, tmp_bytes);
    if (!tmp)
        return 0;

    memset(dst_rgba, 0, dst_bytes);
    gl_capture_framebuffer_state(&saved_state);
    (void)gl_drain_errors("pre-readback");
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.BindFramebuffer(GL_FRAMEBUFFER, use_postfx_readback ? readback_framebuffer : 0);
    gl.ReadBuffer(use_postfx_readback ? readback_buffer : ctx->default_draw_buffer);
    gl.ReadPixels(0, 0, copy_w, copy_h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    if (!gl_drain_errors("readback")) {
        gl_restore_framebuffer_state(&saved_state);
        gl_restore_main_draw_state(ctx);
        return 0;
    }
    vgfx3d_flip_rgba_rows(tmp, copy_w, copy_h);
    for (int32_t y = 0; y < copy_h; y++)
        memcpy(dst_rgba + (size_t)y * (size_t)stride,
               tmp + (size_t)y * (size_t)copy_w * 4u,
               (size_t)copy_w * 4u);

    gl_restore_framebuffer_state(&saved_state);
    gl_restore_main_draw_state(ctx);
    return 1;
}

/// @brief Present an offscreen OpenGL final target through the ViperGFX window buffer.
/// @details This fallback is used only when the GLX default framebuffer probe shows that the
///          window drawable does not accept RGB writes. Rendering and post-FX remain on OpenGL;
///          the resolved final FBO is copied to the existing RGBA ViperGFX framebuffer and
///          presented through the platform XImage path.
static int gl_present_offscreen_vgfx(gl_context_t *ctx) {
    vgfx_framebuffer_t fb;
    GLuint framebuffer;
    int32_t source_w;
    int32_t source_h;
    int32_t copy_w;
    int32_t copy_h;
    uint8_t *tmp;
    size_t tmp_bytes;
    gl_framebuffer_state_t saved_state;

    if (!ctx || !ctx->vgfx_win)
        return 0;
    if (!ctx->default_framebuffer_writable && ctx->scene_fbo) {
        framebuffer = ctx->scene_fbo;
        source_w = ctx->scene_width;
        source_h = ctx->scene_height;
    } else if (ctx->scene_fbo) {
        framebuffer = ctx->scene_fbo;
        source_w = ctx->scene_width;
        source_h = ctx->scene_height;
    } else {
        return 0;
    }
    if (!vgfx_get_framebuffer(ctx->vgfx_win, &fb) || !fb.pixels || fb.width <= 0 ||
        fb.height <= 0 || fb.stride < fb.width * 4 || source_w <= 0 || source_h <= 0) {
        return 0;
    }
    copy_w = source_w < fb.width ? source_w : fb.width;
    copy_h = source_h < fb.height ? source_h : fb.height;
    if (copy_w <= 0 || copy_h <= 0 || (size_t)copy_w > SIZE_MAX / (size_t)copy_h ||
        (size_t)copy_w * (size_t)copy_h > SIZE_MAX / 4u) {
        return 0;
    }
    tmp_bytes = (size_t)copy_w * (size_t)copy_h * 4u;
    tmp = gl_ensure_readback_scratch(ctx, tmp_bytes);
    if (!tmp)
        return 0;

    gl_capture_framebuffer_state(&saved_state);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadPixels(0, 0, copy_w, copy_h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    vgfx3d_flip_rgba_rows(tmp, copy_w, copy_h);
    for (int32_t y = 0; y < copy_h; y++)
        memcpy((uint8_t *)fb.pixels + (size_t)y * (size_t)fb.stride,
               tmp + (size_t)y * (size_t)copy_w * 4u,
               (size_t)copy_w * 4u);
    gl_restore_framebuffer_state(&saved_state);
    gl_restore_main_draw_state(ctx);

    vgfx_set_gpu_present(ctx->vgfx_win, 0);
    (void)vgfx_update(ctx->vgfx_win);
    vgfx_set_gpu_present(ctx->vgfx_win, 1);
    return 1;
}

/// @brief Internal present — composite scene to backbuffer (if needed) then SwapBuffers.
///
/// Skips the composite when scene was never rendered offscreen (no
/// postfx). After present, clears the per-frame state flags so the
/// next frame starts clean.
static void gl_present_impl(gl_context_t *ctx, const vgfx3d_postfx_chain_t *chain) {
    int use_postfx;

    if (!ctx || ctx->rtt_active)
        return;
    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return;
    use_postfx = (chain != NULL && chain->enabled && chain->effect_count > 0 &&
                  ctx->gpu_postfx_enabled && ctx->scene_postfx_pending)
                     ? 1
                     : 0;
    if (ctx->default_framebuffer_writable && ctx->gpu_postfx_enabled && ctx->scene_postfx_pending &&
        !ctx->scene_composited_to_backbuffer) {
        if (draw_scene_texture(ctx, use_postfx ? chain : NULL))
            ctx->scene_composited_to_backbuffer = 1;
    }
    if (gl_debug_enabled()) {
        GLint draw_buffer = 0;
        GLint framebuffer = 0;
        gl.GetIntegerv(GL_DRAW_BUFFER, &draw_buffer);
        gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        fprintf(stderr,
                "[OpenGL] present fbo=%d drawbuf=0x%04X postfx_enabled=%d pending=%d "
                "composited=%d use_postfx=%d\n",
                (int)framebuffer,
                (unsigned)draw_buffer,
                (int)ctx->gpu_postfx_enabled,
                (int)ctx->scene_postfx_pending,
                (int)ctx->scene_composited_to_backbuffer,
                use_postfx);
    }
    if (!ctx->default_framebuffer_writable) {
        (void)gl_present_offscreen_vgfx(ctx);
        ctx->stats.offscreen_presents++;
        ctx->stats.present_path = 2;
        ctx->scene_postfx_pending = 0;
        ctx->scene_composited_to_backbuffer = 0;
        return;
    }
    glx.SwapBuffers(ctx->display, ctx->window);
    ctx->stats.direct_presents++;
    ctx->stats.present_path = 1;
    ctx->scene_postfx_pending = 0;
    ctx->scene_composited_to_backbuffer = 0;
}

/// @brief Backend `present` (no postfx) — direct SwapBuffers via `gl_present_impl`.
static void gl_present(void *ctx_ptr) {
    gl_present_impl((gl_context_t *)ctx_ptr, NULL);
}

/// @brief Backend `present_postfx` hook — composite the scene through the supplied chain and swap.
/// @details Canvas3D calls this legacy single-step hook when the active frame does not need
///          final-overlay replay after post-processing. The implementation delegates to the
///          shared presenter so a disabled or empty chain still resolves the offscreen scene to
///          the swapchain before presentation.
static void gl_present_postfx(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    gl_present_impl((gl_context_t *)ctx_ptr, postfx);
}

/// @brief Backend split post-FX hook — composite pending scene color without swapping.
/// @details This is used when Canvas3D must replay final overlays after post-FX or capture a
///          finalized screenshot without presenting the drawable. It writes the pending scene
///          target into the backbuffer and marks it composited; a later `present` call performs
///          the actual buffer swap.
static void gl_apply_postfx(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    int use_postfx;
    if (!ctx || ctx->rtt_active)
        return;
    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return;
    use_postfx = (postfx && postfx->enabled && postfx->effect_count > 0 &&
                  ctx->gpu_postfx_enabled && ctx->scene_postfx_pending)
                     ? 1
                     : 0;
    if (ctx->gpu_postfx_enabled && ctx->scene_postfx_pending &&
        !ctx->scene_composited_to_backbuffer) {
        int drawn = draw_scene_texture(ctx, use_postfx ? postfx : NULL);
        if (drawn)
            ctx->scene_composited_to_backbuffer = 1;
    }
}

/// @brief Backend `set_gpu_postfx_enabled` hook — route future frames through scene targets.
/// @details When enabled, `begin_frame` chooses the offscreen scene FBO so post-processing can
///          sample the rendered color/depth/motion buffers. Disabling post-FX clears latched
///          chain state and any pending scene-composite flags so the next frame returns to direct
///          swapchain rendering.
static void gl_set_gpu_postfx_enabled(void *ctx_ptr, int8_t enabled) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    int8_t next_enabled = enabled ? 1 : 0;
    if (!ctx)
        return;
    if (ctx->gpu_postfx_enabled != next_enabled) {
        ctx->scene_postfx_pending = 0;
        ctx->scene_composited_to_backbuffer = 0;
    }
    ctx->gpu_postfx_enabled = next_enabled;
    if (!ctx->gpu_postfx_enabled) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
    }
}

/// @brief Backend `set_gpu_postfx_snapshot` hook — latch a stable copy of the frame chain.
/// @details The Canvas3D-side chain is mutable after frame setup, so the backend stores its own
///          copy for present-time composition and readback. Snapshot refresh can also occur while
///          replaying final overlays after `apply_postfx`; it must preserve an already-composited
///          backbuffer so overlay replay is not overwritten by a second scene composite. Copy
///          failure resets the cached chain instead of leaving partial effect state visible to
///          later frames.
static void gl_set_gpu_postfx_snapshot(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    if (!postfx) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        ctx->taa_history_valid = 0;
        return;
    }
    if (!vgfx3d_postfx_chain_copy(&ctx->gpu_postfx_chain, postfx)) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        ctx->taa_history_valid = 0;
        return;
    }
    ctx->gpu_postfx_chain_valid = 1;
    /* Plan 05: dropping TAA from the chain invalidates temporal history so a later
     * re-enable starts from a fresh accumulation instead of stale frames. */
    if (!gl_chain_has_effect(postfx, (int32_t)VGFX3D_POSTFX_EFFECT_TAA))
        ctx->taa_history_valid = 0;
}

/// @brief Backend `set_render_target` op — bind / unbind RTT.
///
/// NULL `rt` tears down RTT and reverts to the swapchain. Non-NULL
/// allocates / re-binds RTT targets sized for `rt`. Clears any
/// in-flight scene postfx state since RTT bypasses that path.
static void gl_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return;
    ctx->scene_postfx_pending = 0;
    ctx->scene_composited_to_backbuffer = 0;
    if (!rt) {
        destroy_rtt_targets(ctx);
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    if (ensure_rtt_targets(ctx, rt) != 0) {
        destroy_rtt_targets(ctx);
        ctx->active_target_kind = VGFX3D_OPENGL_TARGET_SWAPCHAIN;
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

/// @brief Backend `resolve_opaque_targets` op — snapshot the scene depth at the
///   opaque->transparent seam. The live depth attachment cannot be sampled by
///   the very pass writing it, so the depth planes are blitted into a dedicated
///   snapshot texture that blend draws (soft particles) read via
///   `uOpaqueDepthTex`. The currently bound framebuffer is restored afterwards.
static void gl_resolve_opaque_targets(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLint prev_fbo = 0;
    int32_t w;
    int32_t h;

    if (!ctx || !gl.BlitFramebuffer)
        return;
    ctx->opaque_depth_valid = 0;
    w = ctx->rtt_active ? ctx->rtt_width : ctx->width;
    h = ctx->rtt_active ? ctx->rtt_height : ctx->height;
    if (w <= 0 || h <= 0)
        return;
    gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    if (!ctx->opaque_depth_tex)
        gl.GenTextures(1, &ctx->opaque_depth_tex);
    if (!ctx->opaque_depth_fbo)
        gl.GenFramebuffers(1, &ctx->opaque_depth_fbo);
    if (!ctx->opaque_depth_tex || !ctx->opaque_depth_fbo)
        return;
    if (ctx->opaque_depth_w != w || ctx->opaque_depth_h != h) {
        gl.BindTexture(GL_TEXTURE_2D, ctx->opaque_depth_tex);
        gl.TexImage2D(
            GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.BindTexture(GL_TEXTURE_2D, 0);
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->opaque_depth_fbo);
        gl.FramebufferTexture2D(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->opaque_depth_tex, 0);
        gl.DrawBuffer(GL_NONE);
        gl.ReadBuffer(GL_NONE);
        ctx->opaque_depth_w = w;
        ctx->opaque_depth_h = h;
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev_fbo);
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx->opaque_depth_fbo);
    gl.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    gl.BindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    ctx->opaque_depth_valid = gl_upload_ok() ? 1 : 0;
}

/// @brief Backend `shadow_begin` — switch the pipeline to shadow-pass mode.
///
/// Allocates the shadow FBO if needed, captures the light-space VP,
/// binds the depth-only FBO + viewport, clears depth, switches to
/// the shadow vertex shader (no PS).
static void gl_shadow_begin(
    void *ctx_ptr, int32_t slot, float *depth_buf, int32_t w, int32_t h, const float *light_vp) {
    (void)depth_buf;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx)
        return;
    ctx->shadow_pass_slot = -1;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    ctx->shadow_complete[slot] = 0;
    gl_recompute_shadow_count(ctx);
    if (!light_vp || w <= 0 || h <= 0)
        return;
    if (ensure_shadow_targets(ctx, slot, w, h) != 0) {
        ctx->shadow_complete[slot] = 0;
        gl_recompute_shadow_count(ctx);
        return;
    }
    ctx->shadow_pass_slot = slot;
    memcpy(ctx->shadow_vp[slot], light_vp, sizeof(ctx->shadow_vp[slot]));
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo[slot]);
    gl.Viewport(0, 0, w, h);
    gl.ClearDepth(1.0);
    gl.Clear(GL_DEPTH_BUFFER_BIT);
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthMask(GL_TRUE);
    gl.ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    gl.Enable(GL_CULL_FACE);
    gl.CullFace(GL_BACK);
    gl.FrontFace(GL_CCW);
    gl.Disable(GL_POLYGON_OFFSET_FILL);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.UseProgram(ctx->shadow_program);
    gl.BindVertexArray(ctx->vao);
}

/// @brief Backend `shadow_draw` — emit one mesh into the shadow depth buffer.
///
/// Slim version of `submit_draw`: model + light-VP + skinning + morph
/// only. No materials, no lights, no fog.
static void gl_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLuint mesh_vbo, mesh_ibo;
    uint32_t index_count;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || ctx->shadow_pass_slot < 0 ||
        ctx->shadow_pass_slot >= VGFX3D_MAX_SHADOW_LIGHTS || cmd->vertex_count == 0 ||
        cmd->index_count == 0)
        return;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0)
        return;
    if (cmd->double_sided)
        gl.Disable(GL_CULL_FACE);
    else
        gl.Enable(GL_CULL_FACE);
    gl_apply_depth_bias(cmd);
    if (!prepare_mesh_buffers(ctx, cmd, &mesh_vbo, &mesh_ibo))
        return;
    configure_mesh_attributes(ctx, mesh_vbo, mesh_ibo);
    gl.UniformMatrix4fv(ctx->shadow_uModelMatrix, 1, GL_TRUE, cmd->model_matrix);
    gl.UniformMatrix4fv(
        ctx->shadow_uViewProjection, 1, GL_TRUE, ctx->shadow_vp[ctx->shadow_pass_slot]);
    bind_shadow_anim(ctx, cmd);
    {
        GLuint diffuse_tex = gl_get_material_texture(
            ctx,
            cmd->texture_asset,
            cmd->texture,
            cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
            cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
            cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR]);
        gl.Uniform1i(ctx->shadow_uHasTexture, diffuse_tex ? 1 : 0);
        gl.Uniform1i(ctx->shadow_uAlphaMode, cmd->alpha_mode);
        gl.Uniform1f(ctx->shadow_uAlphaCutoff, cmd->alpha_cutoff);
        gl.Uniform1f(ctx->shadow_uAlpha, cmd->alpha);
        gl.Uniform4f(ctx->shadow_uDiffuseColor,
                     cmd->diffuse_color[0],
                     cmd->diffuse_color[1],
                     cmd->diffuse_color[2],
                     cmd->diffuse_color[3]);
        gl.Uniform4i(ctx->shadow_uTextureUvSets0,
                     cmd->texture_slot_uv_set[0],
                     cmd->texture_slot_uv_set[1],
                     cmd->texture_slot_uv_set[2],
                     cmd->texture_slot_uv_set[3]);
        gl.Uniform4i(ctx->shadow_uTextureUvSets1,
                     cmd->texture_slot_uv_set[4],
                     cmd->texture_slot_uv_set[5],
                     0,
                     0);
        {
            float uv_transform0[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][4];
            float uv_transform1[RT_MATERIAL3D_TEXTURE_SLOT_COUNT][4];
            for (int32_t tex_slot = 0; tex_slot < RT_MATERIAL3D_TEXTURE_SLOT_COUNT; tex_slot++) {
                uv_transform0[tex_slot][0] = cmd->texture_slot_uv_transform[tex_slot][0];
                uv_transform0[tex_slot][1] = cmd->texture_slot_uv_transform[tex_slot][1];
                uv_transform0[tex_slot][2] = cmd->texture_slot_uv_transform[tex_slot][2];
                uv_transform0[tex_slot][3] = cmd->texture_slot_uv_transform[tex_slot][3];
                uv_transform1[tex_slot][0] = cmd->texture_slot_uv_transform[tex_slot][4];
                uv_transform1[tex_slot][1] = cmd->texture_slot_uv_transform[tex_slot][5];
                uv_transform1[tex_slot][2] = 0.0f;
                uv_transform1[tex_slot][3] = 0.0f;
            }
            gl.Uniform4fv(ctx->shadow_uTextureUvTransform0,
                          RT_MATERIAL3D_TEXTURE_SLOT_COUNT,
                          &uv_transform0[0][0]);
            gl.Uniform4fv(ctx->shadow_uTextureUvTransform1,
                          RT_MATERIAL3D_TEXTURE_SLOT_COUNT,
                          &uv_transform1[0][0]);
        }
        bind_texture_unit_with_sampler(ctx,
                                       ctx->shadow_uDiffuseTex,
                                       0,
                                       GL_TEXTURE_2D,
                                       diffuse_tex,
                                       cmd,
                                       RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR);
    }
    gl.DrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL);
}

/// @brief Backend `shadow_end` op — exit shadow-pass mode and switch back to main.
///
/// Marks `shadow_active=1` so the main pass's `uShadowEnabled` uniform
/// turns shadow sampling on. Stores the bias the shader uses for
/// depth comparison. Re-binds the main FBO and main program for
/// subsequent draws.
static void gl_shadow_end(void *ctx_ptr, int32_t slot, float bias) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    if (ctx->shadow_pass_slot != slot)
        return;
    ctx->shadow_bias = bias;
    gl.Disable(GL_POLYGON_OFFSET_FILL);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    ctx->shadow_complete[slot] = (ctx->shadow_depth_tex[slot] && ctx->shadow_fbo[slot] &&
                                  ctx->shadow_width[slot] > 0 && ctx->shadow_height[slot] > 0)
                                     ? 1
                                     : 0;
    ctx->shadow_pass_slot = -1;
    gl_recompute_shadow_count(ctx);
    bind_main_framebuffer(ctx);
    gl.UseProgram(ctx->program);
}

/// @brief Backend `submit_draw_instanced` op — emit one `glDrawElementsInstanced`.
///
/// Same flow as `gl_submit_draw` plus per-instance matrix attributes
/// configured via `configure_instance_attributes`. After the draw,
/// resets the instance attributes to identity so subsequent non-
/// instanced draws don't accidentally see leftover instance data.
static void gl_submit_draw_instanced(void *ctx_ptr,
                                     vgfx_window_t win,
                                     const vgfx3d_draw_cmd_t *cmd,
                                     const float *instance_matrices,
                                     int32_t instance_count,
                                     const vgfx3d_light_params_t *lights,
                                     int32_t light_count,
                                     const float *ambient,
                                     int8_t wireframe,
                                     int8_t backface_cull) {
    (void)win;
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    GLuint mesh_vbo, mesh_ibo;
    uint32_t index_count;
    if (!ctx || !cmd || !cmd->vertices || !cmd->indices || cmd->vertex_count == 0 ||
        cmd->index_count == 0 || !instance_matrices || instance_count <= 0) {
        if (ctx)
            ctx->stats.dropped_draws++;
        return;
    }
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0) {
        ctx->stats.dropped_draws++;
        return;
    }
    if (!prepare_mesh_buffers(ctx, cmd, &mesh_vbo, &mesh_ibo)) {
        ctx->stats.dropped_draws++;
        return;
    }

    gl.FrontFace(GL_CCW);
    if (backface_cull)
        gl.Enable(GL_CULL_FACE);
    else
        gl.Disable(GL_CULL_FACE);
    gl.PolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
    gl_configure_draw_output(ctx, cmd);
    gl_apply_depth_bias(cmd);

    gl.UseProgram(ctx->program);
    upload_main_uniforms(ctx, cmd, lights, light_count, ambient, 1);
    bind_material_textures(ctx, cmd);
    configure_mesh_attributes(ctx, mesh_vbo, mesh_ibo);
    if (!configure_instance_attributes(ctx,
                                       instance_matrices,
                                       cmd->has_prev_instance_matrices ? cmd->prev_instance_matrices
                                                                       : NULL,
                                       instance_count)) {
        set_identity_instance_constants();
        ctx->stats.dropped_draws++;
        return;
    }
    gl.DrawElementsInstanced(
        GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, NULL, (GLsizei)instance_count);
    ctx->stats.draw_calls++;
    set_identity_instance_constants();
}
