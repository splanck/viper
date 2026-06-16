//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_targets.inc  (#included from vgfx3d_backend_opengl.c; not a standalone translation unit)
// Purpose: OpenGL render-target lifecycle, draw-output config, and post-FX application
//===----------------------------------------------------------------------===//

static int gl_sync_render_target_color(void *ctx_ptr, vgfx3d_rendertarget_t *rt);
static void bind_texture_unit(GLint uniform_loc, int unit, GLenum target, GLuint texture);

/// @brief Release the scene FBO and its color/motion/depth attachments.
static void destroy_scene_targets(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->scene_depth_tex)
        gl.DeleteTextures(1, &ctx->scene_depth_tex);
    if (ctx->scene_motion_tex)
        gl.DeleteTextures(1, &ctx->scene_motion_tex);
    if (ctx->scene_color_tex)
        gl.DeleteTextures(1, &ctx->scene_color_tex);
    if (ctx->scene_fbo)
        gl.DeleteFramebuffers(1, &ctx->scene_fbo);
    ctx->scene_fbo = 0;
    ctx->scene_color_tex = 0;
    ctx->scene_motion_tex = 0;
    ctx->scene_depth_tex = 0;
    ctx->scene_width = 0;
    ctx->scene_height = 0;
}

/// @brief Release the post-FX readback FBO + texture.
///
/// The readback target stages tonemapped post-FX output before
/// `glReadPixels` copies to host memory for screenshot capture.
static void destroy_postfx_readback_target(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->postfx_readback_tex)
        gl.DeleteTextures(1, &ctx->postfx_readback_tex);
    if (ctx->postfx_readback_fbo)
        gl.DeleteFramebuffers(1, &ctx->postfx_readback_fbo);
    ctx->postfx_readback_tex = 0;
    ctx->postfx_readback_fbo = 0;
    ctx->postfx_readback_width = 0;
    ctx->postfx_readback_height = 0;
}

/// @brief Build a scene FBO with color (HDR/LDR), motion-vector, and depth attachments.
///
/// MRT layout (color attachment 0 = scene color, 1 = motion vectors).
/// Color format chosen by the active context capabilities — HDR16F when the
/// driver can render to float color targets, RGBA8 otherwise.
/// Depth uses the 32-bit float format so post-FX can reconstruct
/// world position with high precision.
/// Verifies framebuffer completeness before returning success.
static int ensure_scene_targets(gl_context_t *ctx, int32_t w, int32_t h) {
    GLint color_format;
    GLint depth_format;
    GLenum color_type;
    GLenum depth_type;

    if (!ctx || w <= 0 || h <= 0)
        return -1;
    if (ctx->scene_fbo && ctx->scene_width == w && ctx->scene_height == h)
        return 0;

    destroy_scene_targets(ctx);
    color_format = GL_RGBA;
    color_type = GL_UNSIGNED_BYTE;
    depth_format = ctx->supports_depth_float_target ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24;
    depth_type = ctx->supports_depth_float_target ? GL_FLOAT : GL_UNSIGNED_INT;

    gl.GenFramebuffers(1, &ctx->scene_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);

    gl.GenTextures(1, &ctx->scene_color_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->scene_color_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, color_format, w, h, 0, GL_RGBA, color_type, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->scene_color_tex, 0);
    gl.GenTextures(1, &ctx->scene_motion_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->scene_motion_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, ctx->scene_motion_tex, 0);
    {
        const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        gl.DrawBuffers(2, attachments);
    }
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);

    gl.GenTextures(1, &ctx->scene_depth_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->scene_depth_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, depth_format, w, h, 0, GL_DEPTH_COMPONENT, depth_type, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->scene_depth_tex, 0);

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !gl_upload_ok()) {
        destroy_scene_targets(ctx);
        return -1;
    }

    ctx->scene_width = w;
    ctx->scene_height = h;
    return 0;
}

/// @brief Build a single-attachment LDR FBO for tonemapped post-FX readback.
static int ensure_postfx_readback_target(gl_context_t *ctx, int32_t w, int32_t h) {
    GLint color_format;
    GLenum color_type;

    if (!ctx || w <= 0 || h <= 0)
        return -1;
    if (ctx->postfx_readback_fbo && ctx->postfx_readback_width == w &&
        ctx->postfx_readback_height == h) {
        return 0;
    }

    destroy_postfx_readback_target(ctx);

    color_format = GL_RGBA;
    color_type = GL_UNSIGNED_BYTE;

    gl.GenFramebuffers(1, &ctx->postfx_readback_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->postfx_readback_fbo);
    gl.GenTextures(1, &ctx->postfx_readback_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->postfx_readback_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, color_format, w, h, 0, GL_RGBA, color_type, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->postfx_readback_tex, 0);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !gl_upload_ok()) {
        destroy_postfx_readback_target(ctx);
        return -1;
    }

    ctx->postfx_readback_width = w;
    ctx->postfx_readback_height = h;
    return 0;
}

/// @brief Release the intermediate post-FX scratch FBO + texture.
///
/// The scratch target forms the second side of the post-FX ping-pong pair. Keeping it separate
/// from the scene FBO preserves the original scene color while multi-pass effects run.
static void destroy_postfx_scratch_target(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->postfx_scratch_tex)
        gl.DeleteTextures(1, &ctx->postfx_scratch_tex);
    if (ctx->postfx_scratch_fbo)
        gl.DeleteFramebuffers(1, &ctx->postfx_scratch_fbo);
    ctx->postfx_scratch_tex = 0;
    ctx->postfx_scratch_fbo = 0;
    ctx->postfx_scratch_width = 0;
    ctx->postfx_scratch_height = 0;
}

/// @brief Build a single-attachment LDR FBO used as post-FX intermediate storage.
/// @return Zero on success, -1 when allocation or framebuffer completeness fails.
static int ensure_postfx_scratch_target(gl_context_t *ctx, int32_t w, int32_t h) {
    GLint color_format;
    GLenum color_type;

    if (!ctx || w <= 0 || h <= 0)
        return -1;
    if (ctx->postfx_scratch_fbo && ctx->postfx_scratch_width == w &&
        ctx->postfx_scratch_height == h) {
        return 0;
    }

    destroy_postfx_scratch_target(ctx);

    color_format = vgfx3d_opengl_choose_color_format(VGFX3D_OPENGL_TARGET_SCENE) ==
                               VGFX3D_OPENGL_COLOR_FORMAT_HDR16F &&
                           ctx->supports_hdr_color_target
                       ? GL_RGBA16F
                       : GL_RGBA8;
    color_type = color_format == GL_RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE;

    gl.GenFramebuffers(1, &ctx->postfx_scratch_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->postfx_scratch_fbo);
    gl.GenTextures(1, &ctx->postfx_scratch_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->postfx_scratch_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, color_format, w, h, 0, GL_RGBA, color_type, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->postfx_scratch_tex, 0);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !gl_upload_ok()) {
        destroy_postfx_scratch_target(ctx);
        return -1;
    }

    ctx->postfx_scratch_width = w;
    ctx->postfx_scratch_height = h;
    return 0;
}

/// @brief Release RTT FBO + color texture + depth renderbuffer.
///
/// If the user-visible RT target has dirty pixels, syncs them back to
/// the RT's host buffer first via `gl_sync_render_target_color`.
/// Then signals end-of-render to anything waiting (`clear_sync`).
static void destroy_rtt_targets(gl_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->rtt_target) {
        int sync_ok = 1;
        if (ctx->rtt_color_dirty)
            sync_ok = gl_sync_render_target_color(ctx, ctx->rtt_target);
        if (sync_ok || !ctx->rtt_target->color_dirty)
            vgfx3d_rendertarget_clear_sync(ctx->rtt_target);
        else
            vgfx3d_rendertarget_detach_sync_preserve_dirty(ctx->rtt_target);
    }
    if (ctx->rtt_depth_rbo)
        gl.DeleteRenderbuffers(1, &ctx->rtt_depth_rbo);
    if (ctx->rtt_color_tex)
        gl.DeleteTextures(1, &ctx->rtt_color_tex);
    if (ctx->rtt_fbo)
        gl.DeleteFramebuffers(1, &ctx->rtt_fbo);
    ctx->rtt_fbo = 0;
    ctx->rtt_color_tex = 0;
    ctx->rtt_depth_rbo = 0;
    ctx->rtt_width = 0;
    ctx->rtt_height = 0;
    ctx->rtt_color_format = (int32_t)VGFX3D_RENDERTARGET_COLOR_FORMAT_UNORM8;
    ctx->rtt_active = 0;
    ctx->rtt_color_dirty = 0;
    ctx->rtt_target = NULL;
}

/// @brief Build / re-bind RTT targets sized to `rt`.
///
/// Idempotent on identical size + receiver. Switching to a different
/// `rt` of the same size syncs the previous RT first. Uses a renderbuffer
/// (not texture) for depth — RTT users don't need to read depth back
/// from the shader, so renderbuffer is faster.
static int ensure_rtt_targets(gl_context_t *ctx, vgfx3d_rendertarget_t *rt) {
    GLenum color_internal_format;
    GLenum depth_internal_format;
    GLenum color_type;

    if (!ctx || !rt || rt->width <= 0 || rt->height <= 0)
        return -1;
    if (ctx->rtt_fbo && ctx->rtt_width == rt->width && ctx->rtt_height == rt->height &&
        ctx->rtt_color_format == rt->color_format) {
        if (ctx->rtt_target && ctx->rtt_target != rt) {
            destroy_rtt_targets(ctx);
        } else {
            ctx->rtt_active = 1;
            ctx->rtt_target = rt;
            ctx->rtt_color_dirty = 0;
            rt->color_dirty = 0;
            rt->sync_color = gl_sync_render_target_color;
            rt->sync_color_userdata = ctx;
            return 0;
        }
    }

    destroy_rtt_targets(ctx);

    gl.GenFramebuffers(1, &ctx->rtt_fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);

    gl.GenTextures(1, &ctx->rtt_color_tex);
    gl.BindTexture(GL_TEXTURE_2D, ctx->rtt_color_tex);
    color_internal_format =
        (vgfx3d_rendertarget_is_hdr(rt) && ctx->supports_hdr_color_target) ? GL_RGBA16F : GL_RGBA8;
    color_type = color_internal_format == GL_RGBA16F ? GL_FLOAT : GL_UNSIGNED_BYTE;
    depth_internal_format =
        ctx->supports_depth_float_target ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24;
    gl.TexImage2D(GL_TEXTURE_2D,
                  0,
                  color_internal_format,
                  rt->width,
                  rt->height,
                  0,
                  GL_RGBA,
                  color_type,
                  NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->rtt_color_tex, 0);
    gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);

    gl.GenRenderbuffers(1, &ctx->rtt_depth_rbo);
    gl.BindRenderbuffer(GL_RENDERBUFFER, ctx->rtt_depth_rbo);
    gl.RenderbufferStorage(GL_RENDERBUFFER, depth_internal_format, rt->width, rt->height);
    gl.FramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, ctx->rtt_depth_rbo);

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !gl_upload_ok()) {
        destroy_rtt_targets(ctx);
        return -1;
    }

    ctx->rtt_width = rt->width;
    ctx->rtt_height = rt->height;
    ctx->rtt_color_format = rt->color_format;
    ctx->rtt_active = 1;
    ctx->rtt_target = rt;
    ctx->rtt_color_dirty = 0;
    rt->color_dirty = 0;
    rt->sync_color = gl_sync_render_target_color;
    rt->sync_color_userdata = ctx;
    return 0;
}

/// @brief Release the shadow-map FBO + depth texture.
static void destroy_shadow_targets(gl_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (ctx->shadow_depth_tex[slot])
            gl.DeleteTextures(1, &ctx->shadow_depth_tex[slot]);
        if (ctx->shadow_fbo[slot])
            gl.DeleteFramebuffers(1, &ctx->shadow_fbo[slot]);
        ctx->shadow_fbo[slot] = 0;
        ctx->shadow_depth_tex[slot] = 0;
        ctx->shadow_width[slot] = 0;
        ctx->shadow_height[slot] = 0;
        ctx->shadow_complete[slot] = 0;
    }
    ctx->shadow_pass_slot = -1;
    ctx->shadow_count = 0;
}

/// @brief Recount the contiguous fully-prepared shadow slots advertised to GLSL.
///
/// The shader receives a single `shadowCount` upper bound for the sampler array. Keeping that
/// count to the completed prefix starting at slot 0 makes `0 <= shadowIndex < shadowCount`
/// equivalent to "a complete GL shadow texture exists", matching the D3D11 backend contract.
static void gl_recompute_shadow_count(gl_context_t *ctx) {
    int32_t count = 0;
    if (!ctx)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (!ctx->shadow_complete[slot] || !ctx->shadow_depth_tex[slot] || !ctx->shadow_fbo[slot] ||
            ctx->shadow_width[slot] <= 0 || ctx->shadow_height[slot] <= 0)
            break;
        count++;
    }
    ctx->shadow_count = count;
}

/// @brief Validate a material light's requested shadow texture slot against real GL resources.
///
/// This remains a direct resource guard even though `uShadowCount` is now contiguous, so a failed
/// resize or allocation cannot leave a light sampling an incomplete shadow FBO.
static int32_t gl_sanitize_shadow_slot(gl_context_t *ctx, int32_t shadow_index) {
    int32_t slot;
    if (!ctx)
        return -1;
    slot = vgfx3d_opengl_sanitize_shadow_index(shadow_index, ctx->shadow_count);
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return -1;
    if (!ctx->shadow_complete[slot] || !ctx->shadow_depth_tex[slot] || !ctx->shadow_fbo[slot] ||
        ctx->shadow_width[slot] <= 0 || ctx->shadow_height[slot] <= 0) {
        return -1;
    }
    return slot;
}

/// @brief Build a depth-only FBO for shadow-map rendering.
///
/// `glDrawBuffer(GL_NONE)` + `glReadBuffer(GL_NONE)` because there are
/// no color attachments (depth-only). Linear filtering on the depth
/// texture so the main pass can use hardware PCF when sampling.
static int ensure_shadow_targets(gl_context_t *ctx, int32_t slot, int32_t w, int32_t h) {
    GLint depth_format;
    GLenum depth_type;
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS || w <= 0 || h <= 0)
        return -1;
    if (ctx->shadow_fbo[slot] && ctx->shadow_width[slot] == w && ctx->shadow_height[slot] == h)
        return 0;

    ctx->shadow_complete[slot] = 0;
    gl_recompute_shadow_count(ctx);
    if (ctx->shadow_depth_tex[slot])
        gl.DeleteTextures(1, &ctx->shadow_depth_tex[slot]);
    if (ctx->shadow_fbo[slot])
        gl.DeleteFramebuffers(1, &ctx->shadow_fbo[slot]);
    ctx->shadow_fbo[slot] = 0;
    ctx->shadow_depth_tex[slot] = 0;
    ctx->shadow_width[slot] = 0;
    ctx->shadow_height[slot] = 0;
    depth_format = ctx->supports_depth_float_target ? GL_DEPTH_COMPONENT32F : GL_DEPTH_COMPONENT24;
    depth_type = ctx->supports_depth_float_target ? GL_FLOAT : GL_UNSIGNED_INT;

    gl.GenFramebuffers(1, &ctx->shadow_fbo[slot]);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo[slot]);

    gl.GenTextures(1, &ctx->shadow_depth_tex[slot]);
    gl.BindTexture(GL_TEXTURE_2D, ctx->shadow_depth_tex[slot]);
    gl.TexImage2D(GL_TEXTURE_2D, 0, depth_format, w, h, 0, GL_DEPTH_COMPONENT, depth_type, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.FramebufferTexture2D(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->shadow_depth_tex[slot], 0);
    gl.DrawBuffer(GL_NONE);
    gl.ReadBuffer(GL_NONE);

    if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || !gl_upload_ok()) {
        if (ctx->shadow_depth_tex[slot])
            gl.DeleteTextures(1, &ctx->shadow_depth_tex[slot]);
        if (ctx->shadow_fbo[slot])
            gl.DeleteFramebuffers(1, &ctx->shadow_fbo[slot]);
        ctx->shadow_fbo[slot] = 0;
        ctx->shadow_depth_tex[slot] = 0;
        ctx->shadow_width[slot] = 0;
        ctx->shadow_height[slot] = 0;
        ctx->shadow_complete[slot] = 0;
        gl_recompute_shadow_count(ctx);
        return -1;
    }

    ctx->shadow_width[slot] = w;
    ctx->shadow_height[slot] = h;
    return 0;
}

/// @brief Bind whichever framebuffer the active target kind says is current.
///
/// Three-way dispatch: RTT → user RTT FBO; SCENE → offscreen MRT FBO
/// with both color attachments enabled; else → swapchain (FBO 0).
/// Sets the matching viewport dimensions.
static void bind_main_framebuffer(gl_context_t *ctx) {
    if (ctx->rtt_active) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl.Viewport(0, 0, ctx->rtt_width, ctx->rtt_height);
    } else if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SWAPCHAIN) {
        if (!ctx->default_framebuffer_writable && ctx->scene_fbo) {
            gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
            gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
            gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
        } else {
            gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
            gl.DrawBuffer(ctx->default_draw_buffer);
            gl.ReadBuffer(ctx->default_draw_buffer);
        }
        gl.Viewport(0, 0, ctx->width, ctx->height);
    } else {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
        {
            const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            gl.DrawBuffers(2, attachments);
        }
        gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
        gl.Viewport(0, 0, ctx->scene_width, ctx->scene_height);
    }
}

/// @brief Per-draw blend / depth / motion-attachment setup.
///
/// Picks blend mode (alpha or opaque) + depth-write toggle + motion
/// attachment count from the draw command. The motion attachment is
/// only enabled in the SCENE target with the right blend mode — we
/// don't emit motion vectors for transparent passes since they'd
/// just overwrite opaque results.
static void gl_configure_draw_output(gl_context_t *ctx, const vgfx3d_draw_cmd_t *cmd) {
    vgfx3d_opengl_blend_mode_t blend_mode;
    vgfx3d_opengl_motion_attachment_mode_t motion_mode;

    if (!ctx)
        return;
    blend_mode = vgfx3d_opengl_choose_blend_mode(cmd);
    motion_mode = vgfx3d_opengl_choose_motion_attachment_mode(ctx->active_target_kind, cmd);

    if (blend_mode == VGFX3D_OPENGL_BLEND_ALPHA || blend_mode == VGFX3D_OPENGL_BLEND_ADDITIVE)
        gl.Enable(GL_BLEND);
    else
        gl.Disable(GL_BLEND);
    if (blend_mode == VGFX3D_OPENGL_BLEND_ADDITIVE)
        gl.BlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
        gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (cmd && cmd->disable_depth_test) {
        gl.Disable(GL_DEPTH_TEST);
        gl.DepthMask(GL_FALSE);
    } else {
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GL_LEQUAL);
        gl.DepthMask(blend_mode == VGFX3D_OPENGL_BLEND_OPAQUE ? GL_TRUE : GL_FALSE);
    }

    if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_SCENE) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
        if (motion_mode == VGFX3D_OPENGL_MOTION_ATTACHMENTS_COLOR_AND_MOTION) {
            const GLenum attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
            gl.DrawBuffers(2, attachments);
        } else {
            gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        }
        gl.Viewport(0, 0, ctx->scene_width, ctx->scene_height);
        return;
    }
    if (ctx->active_target_kind == VGFX3D_OPENGL_TARGET_RTT) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
        gl.Viewport(0, 0, ctx->rtt_width, ctx->rtt_height);
        return;
    }
    if (!ctx->default_framebuffer_writable && ctx->scene_fbo) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->scene_fbo);
        gl.DrawBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl.DrawBuffer(ctx->default_draw_buffer);
    }
    gl.Viewport(0, 0, ctx->width, ctx->height);
}

/// @brief Apply per-material polygon offset for coplanar geometry.
/// @details OpenGL keeps polygon offset as sticky global state, so every draw explicitly enables
///   and configures it or disables it. The command contract uses negative constant values to pull
///   triangles toward the camera and positive values to push them away, matching GL's depth offset
///   direction with the renderer's `GL_LESS` depth test.
static void gl_apply_depth_bias(const vgfx3d_draw_cmd_t *cmd) {
    if (cmd && (fabsf(cmd->depth_bias) > 1e-8f || fabsf(cmd->slope_scaled_depth_bias) > 1e-8f)) {
        gl.Enable(GL_POLYGON_OFFSET_FILL);
        gl.PolygonOffset(cmd->slope_scaled_depth_bias,
                         vgfx3d_depth_bias_constant_units(cmd->depth_bias));
    } else {
        gl.Disable(GL_POLYGON_OFFSET_FILL);
    }
}

/// @brief Push every post-FX uniform from the snapshot into the program.
///
/// Each toggle has a paired enable flag + parameter set: bloom,
/// tonemap, FXAA, color grading, vignette, SSAO, DoF, motion blur.
/// NULL snapshot zeros every toggle (post-FX disabled). The camera
/// + inverse VP + previous VP go through unconditionally so motion-
/// blur and SSAO can do their world-space reconstructions.
static void gl_apply_postfx_uniforms(gl_context_t *ctx, const vgfx3d_postfx_snapshot_t *snapshot) {
    int32_t bloom_passes;
    int32_t ssao_samples;
    int32_t motion_samples;
    if (!ctx)
        return;
    gl.Uniform2f(ctx->postfx_uInvResolution,
                 1.0f / (float)ctx->scene_width,
                 1.0f / (float)ctx->scene_height);
    gl.Uniform3f(ctx->postfx_uCameraPos,
                 ctx->scene_cam_pos[0],
                 ctx->scene_cam_pos[1],
                 ctx->scene_cam_pos[2]);
    gl.UniformMatrix4fv(ctx->postfx_uInvViewProjection, 1, GL_TRUE, ctx->scene_inv_vp);
    gl.UniformMatrix4fv(ctx->postfx_uPrevViewProjection, 1, GL_TRUE, ctx->scene_prev_vp);

    if (!snapshot) {
        gl.Uniform1i(ctx->postfx_uBloomEnabled, 0);
        gl.Uniform1f(ctx->postfx_uBloomThreshold, 1.0f);
        gl.Uniform1f(ctx->postfx_uBloomIntensity, 0.0f);
        gl.Uniform1i(ctx->postfx_uBloomPasses, 0);
        gl.Uniform1i(ctx->postfx_uTonemapMode, 0);
        gl.Uniform1f(ctx->postfx_uTonemapExposure, 1.0f);
        gl.Uniform1i(ctx->postfx_uFxaaEnabled, 0);
        gl.Uniform1i(ctx->postfx_uColorGradeEnabled, 0);
        gl.Uniform1f(ctx->postfx_uCgBrightness, 0.0f);
        gl.Uniform1f(ctx->postfx_uCgContrast, 1.0f);
        gl.Uniform1f(ctx->postfx_uCgSaturation, 1.0f);
        gl.Uniform1i(ctx->postfx_uVignetteEnabled, 0);
        gl.Uniform1f(ctx->postfx_uVignetteRadius, 1.0f);
        gl.Uniform1f(ctx->postfx_uVignetteSoftness, 0.0f);
        gl.Uniform1i(ctx->postfx_uSsaoEnabled, 0);
        gl.Uniform1f(ctx->postfx_uSsaoRadius, 0.0f);
        gl.Uniform1f(ctx->postfx_uSsaoIntensity, 0.0f);
        gl.Uniform1i(ctx->postfx_uSsaoSamples, 0);
        gl.Uniform1i(ctx->postfx_uDofEnabled, 0);
        gl.Uniform1f(ctx->postfx_uDofFocusDistance, 0.0f);
        gl.Uniform1f(ctx->postfx_uDofAperture, 0.0f);
        gl.Uniform1f(ctx->postfx_uDofMaxBlur, 0.0f);
        gl.Uniform1i(ctx->postfx_uMotionBlurEnabled, 0);
        gl.Uniform1f(ctx->postfx_uMotionBlurIntensity, 0.0f);
        gl.Uniform1i(ctx->postfx_uMotionBlurSamples, 0);
        return;
    }

    bloom_passes = snapshot->bloom_passes < 0 ? 0 : snapshot->bloom_passes;
    if (bloom_passes > 32)
        bloom_passes = 32;
    ssao_samples = snapshot->ssao_samples < 1 ? 1 : snapshot->ssao_samples;
    if (ssao_samples > 8)
        ssao_samples = 8;
    motion_samples = snapshot->motion_blur_samples < 2 ? 2 : snapshot->motion_blur_samples;
    if (motion_samples > 8)
        motion_samples = 8;
    gl.Uniform1i(ctx->postfx_uBloomEnabled, snapshot->bloom_enabled ? 1 : 0);
    gl.Uniform1f(ctx->postfx_uBloomThreshold,
                 isfinite(snapshot->bloom_threshold) ? snapshot->bloom_threshold : 1.0f);
    gl.Uniform1f(ctx->postfx_uBloomIntensity,
                 isfinite(snapshot->bloom_intensity) ? snapshot->bloom_intensity : 0.0f);
    gl.Uniform1i(ctx->postfx_uBloomPasses, bloom_passes);
    gl.Uniform1i(ctx->postfx_uTonemapMode, snapshot->tonemap_mode);
    gl.Uniform1f(ctx->postfx_uTonemapExposure,
                 isfinite(snapshot->tonemap_exposure) ? snapshot->tonemap_exposure : 1.0f);
    gl.Uniform1i(ctx->postfx_uFxaaEnabled, snapshot->fxaa_enabled ? 1 : 0);
    gl.Uniform1i(ctx->postfx_uColorGradeEnabled, snapshot->color_grade_enabled ? 1 : 0);
    gl.Uniform1f(ctx->postfx_uCgBrightness,
                 isfinite(snapshot->cg_brightness) ? snapshot->cg_brightness : 0.0f);
    gl.Uniform1f(ctx->postfx_uCgContrast,
                 isfinite(snapshot->cg_contrast) ? snapshot->cg_contrast : 1.0f);
    gl.Uniform1f(ctx->postfx_uCgSaturation,
                 isfinite(snapshot->cg_saturation) ? snapshot->cg_saturation : 1.0f);
    gl.Uniform1i(ctx->postfx_uVignetteEnabled, snapshot->vignette_enabled ? 1 : 0);
    gl.Uniform1f(ctx->postfx_uVignetteRadius,
                 isfinite(snapshot->vignette_radius) ? snapshot->vignette_radius : 1.0f);
    gl.Uniform1f(ctx->postfx_uVignetteSoftness,
                 isfinite(snapshot->vignette_softness) ? snapshot->vignette_softness : 0.0f);
    gl.Uniform1i(ctx->postfx_uSsaoEnabled, snapshot->ssao_enabled ? 1 : 0);
    gl.Uniform1f(ctx->postfx_uSsaoRadius,
                 isfinite(snapshot->ssao_radius) ? snapshot->ssao_radius : 0.0f);
    gl.Uniform1f(ctx->postfx_uSsaoIntensity,
                 isfinite(snapshot->ssao_intensity) ? snapshot->ssao_intensity : 0.0f);
    gl.Uniform1i(ctx->postfx_uSsaoSamples, ssao_samples);
    gl.Uniform1i(ctx->postfx_uDofEnabled, snapshot->dof_enabled ? 1 : 0);
    gl.Uniform1f(ctx->postfx_uDofFocusDistance,
                 isfinite(snapshot->dof_focus_distance) ? snapshot->dof_focus_distance : 0.0f);
    gl.Uniform1f(ctx->postfx_uDofAperture,
                 isfinite(snapshot->dof_aperture) ? snapshot->dof_aperture : 0.0f);
    gl.Uniform1f(ctx->postfx_uDofMaxBlur,
                 isfinite(snapshot->dof_max_blur) ? snapshot->dof_max_blur : 0.0f);
    gl.Uniform1i(ctx->postfx_uMotionBlurEnabled, snapshot->motion_blur_enabled ? 1 : 0);
    gl.Uniform1f(ctx->postfx_uMotionBlurIntensity,
                 isfinite(snapshot->motion_blur_intensity) ? snapshot->motion_blur_intensity
                                                           : 0.0f);
    gl.Uniform1i(ctx->postfx_uMotionBlurSamples, motion_samples);
}

/// @brief Composite the offscreen scene texture onto a target FBO via post-FX.
///
/// Binds the postfx program + fullscreen-triangle VAO, plus scene
/// color/depth/motion as samplers, applies post-FX uniforms, draws
/// 3 vertices (the standard "fullscreen triangle covers the viewport"
/// trick — cheaper than a 6-vertex quad).
static int gl_draw_texture_to_target(gl_context_t *ctx,
                                     GLuint source_color_tex,
                                     GLuint framebuffer,
                                     GLenum draw_buffer,
                                     int32_t width,
                                     int32_t height,
                                     const vgfx3d_postfx_snapshot_t *snapshot) {
    gl_framebuffer_state_t saved;
    int ok;

    if (!ctx || !ctx->postfx_program || !source_color_tex || width <= 0 || height <= 0)
        return 0;

    gl_capture_framebuffer_state(&saved);
    gl.BindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl.DrawBuffer(draw_buffer);
    gl.ReadBuffer(draw_buffer);
    gl.Viewport(0, 0, width, height);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_BLEND);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.DepthMask(GL_FALSE);
    gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    gl.UseProgram(ctx->postfx_program);
    gl.BindVertexArray(ctx->fullscreen_vao);
    gl.Uniform1i(ctx->postfx_uSceneTex, 0);
    gl.Uniform1i(ctx->postfx_uSceneDepthTex, 1);
    gl.Uniform1i(ctx->postfx_uSceneMotionTex, 2);
    bind_texture_unit(ctx->postfx_uSceneTex, 0, GL_TEXTURE_2D, source_color_tex);
    bind_texture_unit(ctx->postfx_uSceneDepthTex, 1, GL_TEXTURE_2D, ctx->scene_depth_tex);
    bind_texture_unit(ctx->postfx_uSceneMotionTex, 2, GL_TEXTURE_2D, ctx->scene_motion_tex);
    gl_apply_postfx_uniforms(ctx, snapshot);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);
    ok = gl_drain_errors("postfx draw");
    gl_restore_framebuffer_state(&saved);
    gl_restore_main_draw_state(ctx);
    return ok;
}

/// @brief Apply post-FX inside the scene FBO when standalone final FBOs are unusable.
/// @details The Linux GLX fallback presents through the ViperGFX CPU window path because the
///          default drawable rejects the fullscreen resolve used by post-FX. On the affected
///          stack, additional single-attachment FBOs can also report complete while discarding
///          color writes. The scene FBO is proven writable by the main render pass, so this
///          fallback ping-pongs between scene color attachment 0 and the motion attachment after
///          motion vectors are no longer needed. The final color is copied back to attachment 0
///          so final-overlay replay, readback, and presentation all consume the normal scene
///          color target.
/// @return Non-zero when every enabled effect pass resolves and the final color is in scene
///         color attachment 0.
static int gl_apply_postfx_chain_in_scene(gl_context_t *ctx,
                                          int32_t width,
                                          int32_t height,
                                          const vgfx3d_postfx_chain_t *chain) {
    GLuint source_tex;

    if (!ctx || !ctx->scene_fbo || !ctx->scene_color_tex || !ctx->scene_motion_tex ||
        width <= 0 || height <= 0 || !chain || !chain->enabled || chain->effect_count <= 0 ||
        !chain->effects) {
        return 0;
    }

    source_tex = ctx->scene_color_tex;
    for (int32_t i = 0; i < chain->effect_count; i++) {
        const int source_is_scene_color = (source_tex == ctx->scene_color_tex) ? 1 : 0;
        const GLenum dst_attachment =
            source_is_scene_color ? GL_COLOR_ATTACHMENT1 : GL_COLOR_ATTACHMENT0;
        const GLuint dst_tex = source_is_scene_color ? ctx->scene_motion_tex : ctx->scene_color_tex;
        if (!gl_draw_texture_to_target(
                ctx, source_tex, ctx->scene_fbo, dst_attachment, width, height, &chain->effects[i].snapshot)) {
            return 0;
        }
        source_tex = dst_tex;
    }

    if (source_tex != ctx->scene_color_tex) {
        if (!gl_draw_texture_to_target(
                ctx, source_tex, ctx->scene_fbo, GL_COLOR_ATTACHMENT0, width, height, NULL)) {
            return 0;
        }
    }
    return 1;
}

/// @brief Apply a full chain of post-FX effects using ping-pong FBO rendering.
/// @details Iterates `chain->effects[0..effect_count-1]`, drawing each pass via
///   `gl_draw_texture_to_target`. Passes alternate between `scene_fbo`/`scene_color_tex`
///   and `postfx_readback_fbo`/`postfx_readback_tex` as source and destination so
///   no pass samples and writes the same texture simultaneously (ping-pong pattern).
///   For the final pass, unless @p force_offscreen_final is non-zero, the output
///   goes directly into @p final_framebuffer / @p final_draw_buffer (usually the
///   default swapchain FBO) at the caller's resolution, avoiding an extra blit.
///
///   When @p force_offscreen_final is set (screenshot readback path), the last
///   pass also writes to an offscreen FBO, and the framebuffer + read-buffer
///   identifiers are returned via @p out_result_framebuffer / @p out_result_read_buffer
///   so the caller can `glReadPixels` from the result without touching the swapchain.
///
///   Returns 0 early if any required inputs are absent, if the postfx readback target
///   can't be allocated, or if any pass encounters a GL error.
/// @param ctx                    Active OpenGL backend context.
/// @param source_tex             GL texture name for the scene colour to process.
/// @param width                  Scene render width in pixels.
/// @param height                 Scene render height in pixels.
/// @param chain                  Effect chain descriptor (enabled flag + effects array).
/// @param final_framebuffer      FBO to write the last pass into (0 = default framebuffer).
/// @param final_draw_buffer      Draw buffer attachment for @p final_framebuffer.
/// @param force_offscreen_final  If non-zero, always write the last pass to an offscreen FBO.
/// @param out_result_framebuffer Receives the FBO holding the last pass's output when
///                               @p force_offscreen_final is set.
/// @param out_result_read_buffer Receives the read-buffer enum for @p out_result_framebuffer.
/// @return 1 on success, 0 if resources are missing or allocation fails.
static int gl_apply_postfx_chain(gl_context_t *ctx,
                                 GLuint source_tex,
                                 int32_t width,
                                 int32_t height,
                                 const vgfx3d_postfx_chain_t *chain,
                                 GLuint final_framebuffer,
                                 GLenum final_draw_buffer,
                                 int force_offscreen_final,
                                 GLuint *out_result_framebuffer,
                                 GLenum *out_result_read_buffer) {
    GLuint last_framebuffer = final_framebuffer;
    GLenum last_read_buffer = final_draw_buffer;

    if (out_result_framebuffer)
        *out_result_framebuffer = 0;
    if (out_result_read_buffer)
        *out_result_read_buffer = ctx ? ctx->default_draw_buffer : GL_BACK;
    if (!ctx || !source_tex || width <= 0 || height <= 0 || !chain || !chain->enabled ||
        chain->effect_count <= 0 || !chain->effects) {
        return 0;
    }
    if (ensure_postfx_readback_target(ctx, width, height) != 0)
        return 0;
    if (ensure_postfx_scratch_target(ctx, width, height) != 0)
        return 0;

    for (int32_t i = 0; i < chain->effect_count; i++) {
        const int is_last = (i == chain->effect_count - 1) ? 1 : 0;
        GLuint dst_framebuffer;
        GLenum dst_draw_buffer;
        GLuint dst_texture;

        if (is_last && !force_offscreen_final) {
            dst_framebuffer = final_framebuffer;
            dst_draw_buffer = final_draw_buffer;
            dst_texture = 0;
        } else if (source_tex == ctx->scene_color_tex) {
            dst_framebuffer = ctx->postfx_readback_fbo;
            dst_draw_buffer = GL_COLOR_ATTACHMENT0;
            dst_texture = ctx->postfx_readback_tex;
        } else if (source_tex == ctx->postfx_readback_tex) {
            dst_framebuffer = ctx->postfx_scratch_fbo;
            dst_draw_buffer = GL_COLOR_ATTACHMENT0;
            dst_texture = ctx->postfx_scratch_tex;
        } else {
            dst_framebuffer = ctx->postfx_readback_fbo;
            dst_draw_buffer = GL_COLOR_ATTACHMENT0;
            dst_texture = ctx->postfx_readback_tex;
        }

        if (!gl_draw_texture_to_target(ctx,
                                       source_tex,
                                       dst_framebuffer,
                                       dst_draw_buffer,
                                       width,
                                       height,
                                       &chain->effects[i].snapshot)) {
            return 0;
        }

        last_framebuffer = dst_framebuffer;
        last_read_buffer = dst_draw_buffer;
        if ((!is_last || force_offscreen_final) && dst_texture)
            source_tex = dst_texture;
    }

    if (force_offscreen_final && last_framebuffer != ctx->postfx_readback_fbo) {
        if (!gl_draw_texture_to_target(ctx,
                                       source_tex,
                                       ctx->postfx_readback_fbo,
                                       GL_COLOR_ATTACHMENT0,
                                       width,
                                       height,
                                       NULL)) {
            return 0;
        }
        last_framebuffer = ctx->postfx_readback_fbo;
        last_read_buffer = GL_COLOR_ATTACHMENT0;
    }

    if (out_result_framebuffer)
        *out_result_framebuffer = last_framebuffer;
    if (out_result_read_buffer)
        *out_result_read_buffer = last_read_buffer;
    return 1;
}

/// @brief Multiply two size_t values with overflow checking; writes the product to @p out
///   and returns 1, or zeroes @p out and returns 0 on overflow or a NULL out pointer.
static int gl_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (out)
        *out = 0;
    if (!out)
        return 0;
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

/// @brief Read back the RTT color buffer into the user's host RGBA buffer.
///
/// Allocates a transient buffer matching the RT's stride×height, reads
/// pixels from `rtt_fbo` color attachment 0, flips the rows (GL's
/// origin is bottom-left vs. ours top-left), and copies into the
/// user-visible buffer. Marks `color_dirty = 0` so the next draw
/// doesn't trigger a redundant readback. No-op if the receiver isn't
/// the active RTT.
static int gl_sync_render_target_color(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    uint8_t *tmp;
    float *tmpf;
    size_t bytes;
    size_t float_count;
    size_t pixel_count;
    size_t float_bytes;
    gl_framebuffer_state_t saved_state;

    if (!ctx || !rt || rt->width <= 0 || rt->height <= 0 || !ctx->rtt_fbo ||
        ctx->rtt_target != rt) {
        return 0;
    }
    if (!vgfx3d_rendertarget_ensure_color(rt)) {
        return 0;
    }
    if (!gl_checked_mul_size((size_t)rt->width, (size_t)rt->height, &pixel_count) ||
        !gl_checked_mul_size((size_t)rt->stride, (size_t)rt->height, &bytes)) {
        return 0;
    }

    if (!glx.MakeCurrent(ctx->display, ctx->window, ctx->glxCtx))
        return 0;
    gl_capture_framebuffer_state(&saved_state);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.BindFramebuffer(GL_FRAMEBUFFER, ctx->rtt_fbo);
    gl.ReadBuffer(GL_COLOR_ATTACHMENT0);
    if (vgfx3d_rendertarget_is_hdr(rt)) {
        if (!gl_checked_mul_size(pixel_count, 4u, &float_count) ||
            !gl_checked_mul_size(float_count, sizeof(float), &float_bytes)) {
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        tmpf = (float *)malloc(float_bytes);
        if (!tmpf) {
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        if (!vgfx3d_rendertarget_ensure_hdr_color(rt)) {
            free(tmpf);
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        gl.ReadPixels(0, 0, rt->width, rt->height, GL_RGBA, GL_FLOAT, tmpf);
        if (!gl_upload_ok()) {
            free(tmpf);
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        for (int32_t y = 0; y < rt->height; y++) {
            const float *src_row = tmpf + (size_t)(rt->height - 1 - y) * (size_t)rt->width * 4u;
            float *dst_row = rt->hdr_color_buf + (size_t)y * (size_t)rt->width * 4u;
            memcpy(dst_row, src_row, (size_t)rt->width * 4u * sizeof(float));
        }
        /* Convert from the already top-left-oriented HDR mirror so both CPU mirrors match. */
        vgfx3d_copy_linear_rgba32f_to_rgba8(rt->color_buf,
                                            rt->stride,
                                            rt->width,
                                            rt->height,
                                            rt->hdr_color_buf,
                                            rt->width * 16);
        rt->hdr_color_valid = 1;
        free(tmpf);
    } else {
        size_t tight_bytes;
        if (!gl_checked_mul_size(pixel_count, 4u, &tight_bytes)) {
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        tmp = (uint8_t *)malloc(tight_bytes);
        if (!tmp) {
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        gl.ReadPixels(0, 0, rt->width, rt->height, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
        if (!gl_upload_ok()) {
            free(tmp);
            gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
            gl_restore_framebuffer_state(&saved_state);
            return 0;
        }
        vgfx3d_flip_rgba_rows(tmp, rt->width, rt->height);
        for (int32_t y = 0; y < rt->height; y++) {
            memcpy(rt->color_buf + (size_t)y * (size_t)rt->stride,
                   tmp + (size_t)y * (size_t)rt->width * 4u,
                   (size_t)rt->width * 4u);
        }
        rt->hdr_color_valid = 0;
        free(tmp);
    }

    ctx->rtt_color_dirty = 0;
    rt->color_dirty = 0;
    gl.PixelStorei(GL_PACK_ALIGNMENT, 4);
    gl_restore_framebuffer_state(&saved_state);
    gl_restore_main_draw_state(ctx);
    return 1;
}
