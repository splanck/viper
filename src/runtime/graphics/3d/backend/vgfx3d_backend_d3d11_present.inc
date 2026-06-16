//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_present.inc  (#included from
// vgfx3d_backend_d3d11.c; not a standalone translation unit) Purpose: D3D11 frame begin/end,
// resize, readback, post-FX chain, present, shadow, and skybox
//===----------------------------------------------------------------------===//

/// @brief Backend `clear` op — stores the clear color for the next BeginFrame.
///
/// We don't actually clear the targets here; that happens in
/// `BeginFrame`'s `clear_current_targets` call so we know which
/// target to clear (depending on RTT/scene/swapchain selection).
static void d3d11_clear(void *ctx_ptr, vgfx_window_t win, float r, float g, float b) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    (void)win;
    if (!ctx)
        return;
    ctx->clear_r = vgfx3d_d3d11_clamp_float_param(r, 0.0f, 1.0f, 0.0f);
    ctx->clear_g = vgfx3d_d3d11_clamp_float_param(g, 0.0f, 1.0f, 0.0f);
    ctx->clear_b = vgfx3d_d3d11_clamp_float_param(b, 0.0f, 1.0f, 0.0f);
}

/// @brief Backend `begin_frame` — set up matrices, target selection, and clear.
///
/// Increments the frame serial (used by mesh-cache LRU), recomputes
/// view-projection (and its inverse), captures fog/camera state,
/// chooses the active target kind based on RTT/postfx/overlay flags,
/// updates the frame-history (prev VP for motion blur), and calls
/// `select_current_targets` + `clear_current_targets` to set the
/// pipeline up for the upcoming draws.
static void d3d11_begin_frame(void *ctx_ptr, const vgfx3d_camera_params_t *cam) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;

    if (!ctx || !cam)
        return;

    ctx->frame_serial++;
    ctx->presented_color_valid = 0;
    ctx->shadow_pass_slot = -1;

    vgfx3d_d3d11_copy_mat4_finite_or_identity(ctx->view, cam->view);
    vgfx3d_d3d11_copy_mat4_finite_or_identity(ctx->projection, cam->projection);
    mat4f_mul_d3d(ctx->projection, ctx->view, ctx->vp);
    if (vgfx3d_invert_matrix4(ctx->vp, ctx->inv_vp) != 0)
        memcpy(ctx->inv_vp, k_identity4x4, sizeof(ctx->inv_vp));
    vgfx3d_d3d11_copy_float_array_finite_or(ctx->cam_pos, cam->position, 3u, 0.0f);
    if (vgfx3d_d3d11_float_array_is_finite(cam->forward, 3u))
        memcpy(ctx->cam_forward, cam->forward, sizeof(float) * 3u);
    else {
        ctx->cam_forward[0] = 0.0f;
        ctx->cam_forward[1] = 0.0f;
        ctx->cam_forward[2] = -1.0f;
    }
    ctx->cam_is_ortho = cam->is_ortho ? 1 : 0;
    ctx->fog_enabled = cam->fog_enabled;
    ctx->fog_near = vgfx3d_d3d11_finite_or(cam->fog_near, 10.0f);
    if (ctx->fog_near < 0.0f)
        ctx->fog_near = 0.0f;
    ctx->fog_far = vgfx3d_d3d11_finite_or(cam->fog_far, 50.0f);
    if (ctx->fog_far <= ctx->fog_near)
        ctx->fog_far = ctx->fog_near + 1.0f;
    ctx->fog_color[0] = vgfx3d_d3d11_clamp_float_param(cam->fog_color[0], 0.0f, 1.0f, 0.5f);
    ctx->fog_color[1] = vgfx3d_d3d11_clamp_float_param(cam->fog_color[1], 0.0f, 1.0f, 0.5f);
    ctx->fog_color[2] = vgfx3d_d3d11_clamp_float_param(cam->fog_color[2], 0.0f, 1.0f, 0.5f);
    if (vgfx3d_d3d11_should_reset_composited_swapchain_for_frame(ctx->rtt_active,
                                                                 cam->load_existing_color))
        ctx->scene_composited_to_swapchain = 0;
    ctx->active_target_kind = vgfx3d_d3d11_choose_target_kind(
        ctx->rtt_active, ctx->gpu_postfx_enabled, cam->load_existing_color);
    if (!ctx->rtt_active && cam->load_existing_color && ctx->scene_composited_to_swapchain)
        ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;

    if ((!ctx->rtv || !ctx->depth_tex || !ctx->dsv) &&
        vgfx3d_d3d11_is_valid_texture2d_extent(ctx->width, ctx->height)) {
        d3d11_release_swapchain_main_targets(ctx);
        hr = d3d11_recreate_swapchain_main_targets(
            ctx, ctx->width, ctx->height, "Recreate missing swapchain targets");
        if (FAILED(hr))
            d3d11_log_hresult("Recreate missing swapchain targets", hr);
    }

    if (!ctx->rtt_active && ctx->active_target_kind != VGFX3D_D3D11_TARGET_SWAPCHAIN) {
        hr = d3d11_ensure_scene_targets(ctx, ctx->width, ctx->height);
        if (FAILED(hr)) {
            d3d11_log_hresult("CreateSceneTargets", hr);
            ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
        }
    }
    if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
        hr = d3d11_ensure_overlay_target(ctx, ctx->width, ctx->height);
        if (FAILED(hr))
            d3d11_log_hresult("CreateOverlayTarget", hr);
    }
    d3d11_refresh_pass_flags(ctx, cam);

    if (!ctx->current_pass_is_overlay) {
        ctx->texture_upload_bytes = 0;
        ctx->shadow_count = 0;
        ctx->overlay_used_this_frame = 0;
    }

    {
        vgfx3d_d3d11_frame_history_t history;

        memcpy(history.scene_vp, ctx->scene_vp, sizeof(history.scene_vp));
        memcpy(history.scene_prev_vp, ctx->scene_prev_vp, sizeof(history.scene_prev_vp));
        memcpy(history.scene_inv_vp, ctx->scene_inv_vp, sizeof(history.scene_inv_vp));
        memcpy(history.draw_prev_vp, ctx->draw_prev_vp, sizeof(history.draw_prev_vp));
        memcpy(history.scene_cam_pos, ctx->scene_cam_pos, sizeof(history.scene_cam_pos));
        history.scene_history_valid = ctx->scene_history_valid;
        history.overlay_used_this_frame = ctx->overlay_used_this_frame;
        vgfx3d_d3d11_update_frame_history(
            &history,
            ctx->vp,
            ctx->inv_vp,
            ctx->cam_pos,
            ctx->current_pass_is_overlay,
            (int8_t)vgfx3d_d3d11_uses_separate_overlay_target(ctx->active_target_kind,
                                                              d3d11_has_overlay_target(ctx)));
        memcpy(ctx->scene_vp, history.scene_vp, sizeof(ctx->scene_vp));
        memcpy(ctx->scene_prev_vp, history.scene_prev_vp, sizeof(ctx->scene_prev_vp));
        memcpy(ctx->scene_inv_vp, history.scene_inv_vp, sizeof(ctx->scene_inv_vp));
        memcpy(ctx->draw_prev_vp, history.draw_prev_vp, sizeof(ctx->draw_prev_vp));
        memcpy(ctx->scene_cam_pos, history.scene_cam_pos, sizeof(ctx->scene_cam_pos));
        ctx->scene_history_valid = history.scene_history_valid;
        ctx->overlay_used_this_frame = history.overlay_used_this_frame;
    }

    d3d11_select_current_targets(ctx);
    d3d11_clear_current_targets(ctx, ctx->current_load_existing_color, cam->load_existing_depth);
    d3d11_bind_common_state(ctx, NULL);
    d3d11_begin_frame_timing(ctx);
    if ((ctx->frame_serial & 31u) == 0u) {
        d3d11_prune_texture_cache(ctx);
        d3d11_prune_cubemap_cache(ctx);
    }
}

/// @brief Backend `end_frame` — mark RTT pixels dirty for lazy host readback.
///
/// CPU readback is deferred until a caller explicitly asks for pixels.
/// This keeps render-to-texture frames on the GPU fast instead of forcing
/// a staging copy at the end of every RTT frame.
static void d3d11_end_frame(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;

    if (!ctx)
        return;
    d3d11_end_frame_timing(ctx);
    if (!vgfx3d_d3d11_should_mark_rtt_dirty(ctx->rtt_active,
                                            ctx->rtt_target != NULL,
                                            ctx->rtt_color_tex != NULL,
                                            ctx->rtt_rtv != NULL,
                                            ctx->rtt_depth_tex != NULL,
                                            ctx->rtt_dsv != NULL,
                                            ctx->rtt_staging != NULL))
        return;
    ctx->rtt_target->sync_color = d3d11_sync_render_target_color;
    ctx->rtt_target->sync_color_userdata = ctx;
    ctx->rtt_target->color_dirty = 1;
    ctx->rtt_target->hdr_color_valid = 0;
}

/// @brief Read the total bytes uploaded to D3D11 textures so far this frame (diagnostics counter).
static uint64_t d3d11_get_texture_upload_bytes(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    return ctx ? ctx->texture_upload_bytes : 0;
}

/// @brief Release swapchain-owned color/depth targets and clear stale CPU binding mirrors.
static void d3d11_release_swapchain_main_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    d3d11_unbind_output_targets(ctx);
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_SWAPCHAIN)
        d3d11_clear_current_target_bindings(ctx);
    SAFE_RELEASE(ctx->dsv);
    SAFE_RELEASE(ctx->depth_tex);
    SAFE_RELEASE(ctx->rtv);
}

/// @brief Rebuild the swapchain-derived main render targets (back-buffer RTV, scene/depth
///   textures) at @p width × @p height after a resize or device reset; @p log_context
///   tags any failure log. Returns the first failing HRESULT, or S_OK on success.
static HRESULT d3d11_recreate_swapchain_main_targets(d3d11_context_t *ctx,
                                                     int32_t width,
                                                     int32_t height,
                                                     const char *log_context) {
    ID3D11Texture2D *back_buffer = NULL;
    HRESULT hr;

    if (!ctx || !ctx->device || !ctx->swap_chain ||
        !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;

    hr = IDXGISwapChain_GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult(log_context ? log_context : "IDXGISwapChain::GetBuffer", hr);
        return hr;
    }
    hr = ID3D11Device_CreateRenderTargetView(
        ctx->device, (ID3D11Resource *)back_buffer, NULL, &ctx->rtv);
    SAFE_RELEASE(back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult(log_context ? log_context : "CreateRenderTargetView(backbuffer)", hr);
        return hr;
    }
    hr = d3d11_create_depth_target(ctx, width, height, 0, &ctx->depth_tex, &ctx->dsv, NULL);
    if (FAILED(hr)) {
        d3d11_log_hresult(log_context ? log_context : "CreateTexture2D/DepthStencilView(main)", hr);
        SAFE_RELEASE(ctx->rtv);
        return hr;
    }
    return S_OK;
}

/// @brief Backend `resize` — handle window-size changes.
///
/// Unbinds targets, releases the backbuffer RTV + main depth, calls
/// `IDXGISwapChain::ResizeBuffers`, gets the new backbuffer, recreates
/// RTV + depth target. Scene-targets are also destroyed; they get
/// recreated in the next `BeginFrame` at the new size. No-op if the
/// dimensions match.
static void d3d11_resize(void *ctx_ptr, int32_t w, int32_t h) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int32_t old_w;
    int32_t old_h;
    HRESULT hr;

    if (!ctx || !ctx->swap_chain || !vgfx3d_d3d11_is_valid_texture2d_extent(w, h))
        return;
    if (ctx->width == w && ctx->height == h)
        return;
    old_w = ctx->width;
    old_h = ctx->height;

    d3d11_unbind_draw_resources(ctx);
    d3d11_destroy_scene_targets(ctx);
    d3d11_release_swapchain_main_targets(ctx);
    d3d11_clear_current_target_bindings(ctx);

    hr = IDXGISwapChain_ResizeBuffers(ctx->swap_chain, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::ResizeBuffers", hr);
        d3d11_log_device_removed_reason(ctx, "IDXGISwapChain::ResizeBuffers", hr);
        if (SUCCEEDED(d3d11_recreate_swapchain_main_targets(
                ctx, old_w, old_h, "Recreate swapchain targets after failed resize"))) {
            d3d11_bind_swapchain_target(ctx);
        }
        return;
    }

    ctx->width = w;
    ctx->height = h;
    d3d11_reset_temporal_scene_state(ctx);
    hr =
        d3d11_recreate_swapchain_main_targets(ctx, w, h, "Recreate swapchain targets after resize");
    if (FAILED(hr))
        return;
    d3d11_bind_swapchain_target(ctx);
}

/// @brief Copy a mapped texture's rows to an RGBA8 destination, format-aware.
///
/// Direct memcpy for R8G8B8A8_UNORM. For R16G16B16A16_FLOAT, decodes
/// each half to float then quantizes to 8-bit. Other formats are
/// silently skipped.
static int d3d11_copy_mapped_texture_to_rgba8(uint8_t *dst_rgba,
                                              int32_t dst_stride,
                                              int32_t copy_w,
                                              int32_t copy_h,
                                              const D3D11_MAPPED_SUBRESOURCE *mapped,
                                              DXGI_FORMAT format) {
    if (!dst_rgba || !mapped || !mapped->pData || copy_w <= 0 || copy_h <= 0)
        return 0;

    if (format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        size_t src_row_bytes;
        size_t dst_row_bytes;

        if (mapped->RowPitch > (UINT)INT_MAX ||
            !vgfx3d_d3d11_validate_mapped_texture_copy(
                copy_w, dst_stride, mapped->RowPitch, 8, &src_row_bytes, &dst_row_bytes))
            return 0;
        (void)src_row_bytes;
        (void)dst_row_bytes;
        vgfx3d_copy_linear_rgba16f_to_rgba8(dst_rgba,
                                            dst_stride,
                                            copy_w,
                                            copy_h,
                                            (const uint16_t *)mapped->pData,
                                            (int32_t)mapped->RowPitch);
        return 1;
    }

    if (format == DXGI_FORMAT_R8G8B8A8_UNORM) {
        size_t src_row_bytes;
        size_t dst_row_bytes;

        if (!vgfx3d_d3d11_validate_mapped_texture_copy(
                copy_w, dst_stride, mapped->RowPitch, 4, &src_row_bytes, &dst_row_bytes))
            return 0;
        (void)src_row_bytes;
        for (int32_t y = 0; y < copy_h; y++) {
            uint8_t *dst_row = dst_rgba + (size_t)y * (size_t)dst_stride;
            const uint8_t *src_row = (const uint8_t *)mapped->pData + (size_t)y * mapped->RowPitch;
            memcpy(dst_row, src_row, dst_row_bytes);
        }
        return 1;
    }
    return 0;
}

/// @brief Read back a Texture2D into a host RGBA8 buffer.
///
/// Allocates a transient staging texture matching the source's format
/// + size, CopyResource into it, Map(READ), copy rows. Caller's
/// destination doesn't have to match the source size — clipping is
/// applied. Returns 0 on any failure.
static int d3d11_readback_texture_rgba(d3d11_context_t *ctx,
                                       ID3D11Texture2D *source_tex,
                                       uint8_t *dst_rgba,
                                       int32_t w,
                                       int32_t h,
                                       int32_t stride) {
    ID3D11Texture2D *staging = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int32_t copy_w;
    int32_t copy_h;
    size_t clear_bytes;
    int restore_target = 0;
    int mapped_ok = 0;
    int ok = 0;

    if (!ctx || !ctx->ctx || !source_tex || !dst_rgba ||
        !vgfx3d_d3d11_validate_rgba8_destination(w, h, stride, &clear_bytes))
        return 0;

    ID3D11Texture2D_GetDesc(source_tex, &desc);
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) {
        return 0;
    }
    hr = d3d11_create_staging_texture(
        ctx, (int32_t)desc.Width, (int32_t)desc.Height, desc.Format, &staging);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D(sceneStaging)", hr);
        return 0;
    }

    memset(dst_rgba, 0, clear_bytes);
    restore_target = (ctx->current_rtv_count > 0 || ctx->current_dsv) ? 1 : 0;
    d3d11_unbind_draw_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    ID3D11DeviceContext_CopyResource(
        ctx->ctx, (ID3D11Resource *)staging, (ID3D11Resource *)source_tex);
    hr =
        ID3D11DeviceContext_Map(ctx->ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(sceneStaging)", hr);
        goto cleanup;
    }
    if (!mapped.pData)
        goto cleanup;
    mapped_ok = 1;

    copy_w = (int32_t)desc.Width < w ? (int32_t)desc.Width : w;
    copy_h = (int32_t)desc.Height < h ? (int32_t)desc.Height : h;
    ok = d3d11_copy_mapped_texture_to_rgba8(dst_rgba, stride, copy_w, copy_h, &mapped, desc.Format);

cleanup:
    if (mapped_ok)
        ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)staging, 0);
    SAFE_RELEASE(staging);
    if (restore_target)
        d3d11_restore_current_target_bindings(ctx);
    return ok;
}

/// @brief Point the D3D11 output-merger at one postfx render target and set its viewport.
/// @details Each postfx pass (bloom extract, blur, tonemap) draws a full-screen triangle
///   into a different RTV. This helper batches the OMSetRenderTargets + RSSetViewports
///   calls so each pass is one line, not three, and so the viewport tracks the RTV size
///   automatically (missing that is a classic source of postfx-chain letterboxing bugs).
static void d3d11_bind_postfx_target(d3d11_context_t *ctx,
                                     ID3D11RenderTargetView *rtv,
                                     int32_t width,
                                     int32_t height) {
    D3D11_VIEWPORT viewport;

    if (!ctx || !rtv || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return;
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    d3d11_unbind_postfx_resources(ctx);
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
}

/// @brief Execute one post-FX pass: upload the effect's constant data and issue a
///        fullscreen-triangle draw from @p source_color_srv into @p dest_rtv.
/// @details The pass uses `vs_postfx` (a no-input-layout shader that generates a
///   clip-space fullscreen triangle from `SV_VertexID`) together with `ps_postfx`
///   which samples four PS SRV slots:
///     - slot 0: @p source_color_srv — the colour result of the previous pass
///     - slot 1: `ctx->scene_depth_srv` — linear depth for depth-aware effects
///     - slot 2: `ctx->scene_motion_srv` — screen-space motion vectors (TAA / blur)
///     - slot 3: unused (NULL)
///   The postfx constant buffer is updated from @p snapshot before the draw so
///   per-effect uniforms (exposure, bloom threshold, vignette, etc.) are current.
///   Depth testing and stencil are disabled; the opaque blend state is used because
///   the post-FX shaders output the final composited colour directly. SRVs are
///   unbound after the draw to avoid hazards on the next write to the scene targets.
/// @param ctx               Active D3D11 backend context.
/// @param source_color_srv  The colour texture from the previous pass or the scene.
/// @param dest_rtv          The render target to write this pass's output into.
/// @param width             Viewport width in pixels (must match RTV dimensions).
/// @param height            Viewport height in pixels.
/// @param snapshot          Per-effect uniform data written into the postfx cbuffer.
/// @return 1 on success, 0 if any parameter is invalid or the cbuffer update failed.
static int d3d11_draw_postfx_pass(d3d11_context_t *ctx,
                                  ID3D11ShaderResourceView *source_color_srv,
                                  ID3D11RenderTargetView *dest_rtv,
                                  int32_t width,
                                  int32_t height,
                                  const vgfx3d_postfx_snapshot_t *snapshot) {
    d3d_postfx_cb_t postfx_data;
    ID3D11ShaderResourceView *srvs[4];
    ID3D11ShaderResourceView *null_srvs[4] = {NULL, NULL, NULL, NULL};
    float blend_factor[4] = {0, 0, 0, 0};
    HRESULT hr;

    if (!ctx || !source_color_srv || !dest_rtv ||
        !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return 0;

    d3d11_prepare_postfx_data(ctx, snapshot, &postfx_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_postfx, &postfx_data, sizeof(postfx_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPostFX)", hr);
        return 0;
    }

    d3d11_bind_postfx_target(ctx, dest_rtv, width, height);
    srvs[0] = source_color_srv;
    srvs[1] = ctx->scene_depth_srv;
    srvs[2] = ctx->scene_motion_srv;
    srvs[3] = NULL;
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_postfx, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_postfx, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_postfx);
    ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_postfx);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, srvs);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, NULL, 0);
    ID3D11DeviceContext_OMSetBlendState(
        ctx->ctx, ctx->blend_state_opaque, blend_factor, 0xFFFFFFFF);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, null_srvs);
    return 1;
}

/// @brief Composite the 2D GUI overlay texture on top of the 3D scene using alpha blending.
/// @details After all post-FX passes have resolved the scene colour, the 2D overlay
///   (rendered earlier into `ctx->overlay_color_srv`) is blended over @p dest_rtv
///   using `ps_overlay_composite` and a premultiplied-alpha blend state. The overlay SRV is bound
///   at PS slot 3 (matching the shader's expected binding), and the same
///   no-input-layout fullscreen-triangle technique used by `d3d11_draw_postfx_pass`
///   is re-used here for consistency. The function is a no-op (returns 1) when no
///   overlay was produced this frame (`ctx->overlay_used_this_frame == 0`) or when
///   the overlay resources or composite shader are absent, so callers can always
///   call it unconditionally without checking state themselves.
/// @param ctx        Active D3D11 backend context.
/// @param dest_rtv   Render target to composite the overlay into (final output or
///                   intermediate offscreen buffer when `force_offscreen_final` is set).
/// @param width      Viewport width in pixels.
/// @param height     Viewport height in pixels.
/// @return 1 on successful composite or no-op; 0 when the destination is invalid.
static int d3d11_draw_overlay_composite(d3d11_context_t *ctx,
                                        ID3D11RenderTargetView *dest_rtv,
                                        int32_t width,
                                        int32_t height) {
    ID3D11ShaderResourceView *overlay_srv;
    ID3D11BlendState *blend_state;
    ID3D11ShaderResourceView *null_srvs[4] = {NULL, NULL, NULL, NULL};
    float blend_factor[4] = {0, 0, 0, 0};

    if (!ctx || !dest_rtv || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return 0;
    if (!ctx->overlay_used_this_frame || !ctx->overlay_color_srv || !ctx->ps_overlay_composite) {
        return 1;
    }

    overlay_srv = ctx->overlay_color_srv;
    blend_state = ctx->blend_state_premultiplied_alpha ? ctx->blend_state_premultiplied_alpha
                                                       : ctx->blend_state_alpha;
    d3d11_bind_postfx_target(ctx, dest_rtv, width, height);
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_postfx, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_overlay_composite, NULL, 0);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 3, 1, &overlay_srv);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, NULL, 0);
    ID3D11DeviceContext_OMSetBlendState(ctx->ctx, blend_state, blend_factor, 0xFFFFFFFF);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 4, null_srvs);
    return 1;
}

/// @brief Resolve the offscreen scene color to a final target without applying user effects.
static int d3d11_resolve_scene_to_target(d3d11_context_t *ctx,
                                         ID3D11RenderTargetView *final_rtv,
                                         int32_t final_width,
                                         int32_t final_height,
                                         int force_offscreen_final,
                                         ID3D11Texture2D **out_result_tex) {
    ID3D11RenderTargetView *dest_rtv;
    int32_t width;
    int32_t height;

    if (out_result_tex)
        *out_result_tex = NULL;
    if (!ctx || !ctx->scene_color_srv || !ctx->scene_color_tex || !ctx->scene_depth_srv ||
        !ctx->scene_motion_srv)
        return 0;
    if (force_offscreen_final) {
        if (FAILED(d3d11_ensure_postfx_target(ctx, ctx->scene_width, ctx->scene_height)))
            return 0;
        dest_rtv = ctx->postfx_color_rtv;
        width = ctx->scene_width;
        height = ctx->scene_height;
    } else {
        dest_rtv = final_rtv;
        width = final_width;
        height = final_height;
    }
    if (!dest_rtv || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return 0;
    if (!d3d11_draw_postfx_pass(ctx, ctx->scene_color_srv, dest_rtv, width, height, NULL))
        return 0;
    if (!d3d11_draw_overlay_composite(ctx, dest_rtv, width, height))
        return 0;
    if (out_result_tex)
        *out_result_tex = force_offscreen_final ? ctx->postfx_color_tex : NULL;
    return 1;
}

/// @brief Apply a full chain of post-FX effects to the rendered scene using ping-pong buffers.
/// @details Iterates `chain->effects[0..effect_count-1]`, drawing each via
///   `d3d11_draw_postfx_pass`. Passes alternate between `scene_color_srv/rtv` and
///   `postfx_color_srv/rtv` and `postfx_scratch_srv/rtv` as intermediate destinations so
///   no pass reads and writes the same texture simultaneously and screenshot readback does
///   not overwrite the live scene color. For the final
///   pass, unless @p force_offscreen_final is set, the output goes directly into
///   @p final_rtv (usually the swapchain back buffer) at @p final_width × @p
///   final_height so no extra blit is needed.
///
///   When @p force_offscreen_final is non-zero (used by the screenshot readback path)
///   the final pass also writes to an offscreen scene texture and the result texture
///   pointer is returned via @p out_result_tex so the caller can stage-copy it.
///
///   After all post-FX passes, `d3d11_draw_overlay_composite` is called once to
///   alpha-blend the 2D GUI overlay over the result regardless of effect count.
///
///   Returns 0 early if any of the required scene resources (scene_color_srv,
///   scene_depth_srv, scene_motion_srv) are absent, or if any individual pass fails.
/// @param ctx                 Active D3D11 backend context.
/// @param chain               Effect chain descriptor (enabled flag + effects array).
/// @param final_rtv           Swapchain (or caller-supplied) render target for the last pass.
/// @param final_width         Width of @p final_rtv in pixels.
/// @param final_height        Height of @p final_rtv in pixels.
/// @param force_offscreen_final If non-zero, the last pass writes to an offscreen texture
///                             instead of @p final_rtv; the texture is returned via
///                             @p out_result_tex.
/// @param out_result_tex      Receives the offscreen result texture when
///                            @p force_offscreen_final is set; set to NULL otherwise.
/// @return 1 on success, 0 if resources are missing or any pass fails.
static int d3d11_apply_postfx_chain(d3d11_context_t *ctx,
                                    const vgfx3d_postfx_chain_t *chain,
                                    ID3D11RenderTargetView *final_rtv,
                                    int32_t final_width,
                                    int32_t final_height,
                                    int force_offscreen_final,
                                    ID3D11Texture2D **out_result_tex) {
    ID3D11ShaderResourceView *source_srv;
    ID3D11RenderTargetView *dest_rtv;
    ID3D11Texture2D *result_tex = NULL;
    int need_postfx_target;
    int need_scratch_target;
    int32_t width;
    int32_t height;

    if (out_result_tex)
        *out_result_tex = NULL;
    if (!ctx || !vgfx3d_d3d11_postfx_chain_is_usable(chain) || !ctx->scene_color_srv ||
        !ctx->scene_color_rtv || !ctx->scene_color_tex || !ctx->scene_depth_srv ||
        !ctx->scene_motion_srv) {
        return 0;
    }

    need_postfx_target = force_offscreen_final || chain->effect_count > 1;
    need_scratch_target = (force_offscreen_final && chain->effect_count > 1) ||
                          (!force_offscreen_final && chain->effect_count > 2);
    if (need_postfx_target &&
        FAILED(d3d11_ensure_postfx_target(ctx, ctx->scene_width, ctx->scene_height))) {
        return 0;
    }
    if (need_scratch_target &&
        FAILED(d3d11_ensure_postfx_scratch_target(ctx, ctx->scene_width, ctx->scene_height))) {
        return 0;
    }

    source_srv = ctx->scene_color_srv;
    result_tex = ctx->scene_color_tex;
    for (int32_t i = 0; i < chain->effect_count; i++) {
        const int is_last = (i == chain->effect_count - 1) ? 1 : 0;
        if (is_last && !force_offscreen_final) {
            dest_rtv = final_rtv;
            width = final_width;
            height = final_height;
            result_tex = NULL;
        } else if (source_srv == ctx->postfx_color_srv) {
            dest_rtv = ctx->postfx_scratch_rtv;
            width = ctx->scene_width;
            height = ctx->scene_height;
            result_tex = ctx->postfx_scratch_tex;
        } else {
            dest_rtv = ctx->postfx_color_rtv;
            width = ctx->scene_width;
            height = ctx->scene_height;
            result_tex = ctx->postfx_color_tex;
        }
        if (!dest_rtv || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
            return 0;
        if (!d3d11_draw_postfx_pass(
                ctx, source_srv, dest_rtv, width, height, &chain->effects[i].snapshot))
            return 0;
        if (!is_last || force_offscreen_final) {
            source_srv = result_tex == ctx->postfx_scratch_tex ? ctx->postfx_scratch_srv
                                                               : ctx->postfx_color_srv;
        }
    }

    if (force_offscreen_final) {
        dest_rtv =
            result_tex == ctx->postfx_scratch_tex ? ctx->postfx_scratch_rtv : ctx->postfx_color_rtv;
        width = ctx->scene_width;
        height = ctx->scene_height;
        if (!d3d11_draw_overlay_composite(ctx, dest_rtv, width, height))
            return 0;
        if (out_result_tex)
            *out_result_tex = result_tex;
        return result_tex != NULL ? 1 : 0;
    }

    if (!d3d11_draw_overlay_composite(ctx, final_rtv, final_width, final_height))
        return 0;
    return 1;
}

/// @brief Acquire the swapchain backbuffer for readback fallback paths.
static int d3d11_acquire_backbuffer_for_readback(d3d11_context_t *ctx,
                                                 ID3D11Texture2D **out_back_buffer) {
    HRESULT hr;

    if (out_back_buffer)
        *out_back_buffer = NULL;
    if (!ctx || !ctx->swap_chain || !out_back_buffer)
        return 0;
    hr = IDXGISwapChain_GetBuffer(
        ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)out_back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::GetBuffer(readback)", hr);
        return 0;
    }
    return 1;
}

/// @brief Backend `readback_rgba` op - pick the best source texture to read.
static int d3d11_readback_rgba(
    void *ctx_ptr, uint8_t *dst_rgba, int32_t w, int32_t h, int32_t stride) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    ID3D11Texture2D *back_buffer = NULL;
    ID3D11Texture2D *source_tex = NULL;
    vgfx3d_d3d11_readback_kind_t readback_kind;

    if (!ctx || !dst_rgba || !vgfx3d_d3d11_validate_rgba8_destination(w, h, stride, NULL))
        return 0;
    readback_kind = vgfx3d_d3d11_choose_readback_kind(
        ctx->presented_color_valid,
        ctx->presented_color_tex != NULL,
        ctx->scene_composited_to_swapchain,
        ctx->gpu_postfx_enabled,
        ctx->gpu_postfx_chain_valid,
        ctx->gpu_postfx_chain.enabled,
        ctx->gpu_postfx_chain.effect_count,
        vgfx3d_d3d11_postfx_chain_is_usable(&ctx->gpu_postfx_chain),
        d3d11_has_scene_targets(ctx),
        ctx->current_target_kind);
    if (readback_kind == VGFX3D_D3D11_READBACK_PRESENTED_SNAPSHOT) {
        source_tex = ctx->presented_color_tex;
    } else if (readback_kind == VGFX3D_D3D11_READBACK_POSTFX_COMPOSITE) {
        if (!d3d11_apply_postfx_chain(ctx, &ctx->gpu_postfx_chain, NULL, 0, 0, 1, &source_tex) &&
            d3d11_has_scene_targets(ctx))
            source_tex = ctx->scene_color_tex;
    } else if (readback_kind == VGFX3D_D3D11_READBACK_SCENE_COLOR && ctx->scene_width > 0 &&
               ctx->scene_height > 0) {
        if (ctx->overlay_used_this_frame && d3d11_has_overlay_target(ctx)) {
            if (!d3d11_resolve_scene_to_target(ctx, NULL, 0, 0, 1, &source_tex))
                source_tex = ctx->scene_color_tex;
        } else {
            source_tex = ctx->scene_color_tex;
        }
    }

    if (!source_tex) {
        if (!d3d11_acquire_backbuffer_for_readback(ctx, &back_buffer))
            return 0;
        source_tex = back_buffer;
    }

    {
        int ok = d3d11_readback_texture_rgba(ctx, source_tex, dst_rgba, w, h, stride);
        SAFE_RELEASE(back_buffer);
        return ok;
    }
}

/// @brief Internal present path — composite scene + post-FX + overlay onto the swapchain.
static void d3d11_present_internal(d3d11_context_t *ctx, const vgfx3d_postfx_chain_t *chain) {
    int use_postfx;

    if (!ctx || ctx->rtt_active)
        return;
    if (ctx->scene_composited_to_swapchain) {
        d3d11_present_swapchain(ctx);
        ctx->scene_composited_to_swapchain = 0;
        return;
    }
    use_postfx = (vgfx3d_d3d11_postfx_chain_is_usable(chain) && ctx->gpu_postfx_enabled &&
                  d3d11_has_scene_targets(ctx))
                     ? 1
                     : 0;
    if (!use_postfx) {
        if (ctx->gpu_postfx_enabled && d3d11_has_scene_targets(ctx) &&
            d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
            d3d11_present_swapchain(ctx);
            ctx->scene_composited_to_swapchain = 0;
            return;
        }
        d3d11_present_swapchain(ctx);
        ctx->scene_composited_to_swapchain = 0;
        return;
    }

    if (!d3d11_apply_postfx_chain(ctx, chain, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
        if (d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
            d3d11_present_swapchain(ctx);
            ctx->scene_composited_to_swapchain = 0;
            return;
        }
        d3d11_present_swapchain(ctx);
        ctx->scene_composited_to_swapchain = 0;
        return;
    }
    d3d11_present_swapchain(ctx);
    ctx->scene_composited_to_swapchain = 0;
}

/// @brief Backend `apply_postfx` — composite scene/post-FX onto the swapchain without present.
static void d3d11_apply_postfx(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int use_postfx;

    if (!ctx || ctx->rtt_active)
        return;
    ctx->presented_color_valid = 0;
    if (ctx->scene_composited_to_swapchain)
        return;
    if (!vgfx3d_d3d11_should_composite_to_swapchain(ctx->rtt_active,
                                                    ctx->gpu_postfx_enabled,
                                                    d3d11_has_scene_targets(ctx),
                                                    ctx->scene_composited_to_swapchain))
        return;
    use_postfx = (vgfx3d_d3d11_postfx_chain_is_usable(postfx) && ctx->gpu_postfx_enabled &&
                  d3d11_has_scene_targets(ctx))
                     ? 1
                     : 0;
    if (!use_postfx) {
        if (ctx->gpu_postfx_enabled && d3d11_has_scene_targets(ctx) &&
            d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL))
            ctx->scene_composited_to_swapchain = 1;
        return;
    }
    if (d3d11_apply_postfx_chain(ctx, postfx, ctx->rtv, ctx->width, ctx->height, 0, NULL)) {
        ctx->scene_composited_to_swapchain = 1;
        return;
    }
    if (d3d11_resolve_scene_to_target(ctx, ctx->rtv, ctx->width, ctx->height, 0, NULL))
        ctx->scene_composited_to_swapchain = 1;
}

/// @brief Backend `present` (no post-FX) — direct swapchain present.
static void d3d11_present(void *ctx_ptr) {
    d3d11_present_internal((d3d11_context_t *)ctx_ptr, NULL);
}

/// @brief Backend `present_postfx` — present with an ordered post-FX chain applied.
static void d3d11_present_postfx(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    d3d11_present_internal((d3d11_context_t *)ctx_ptr, postfx);
}

/// @brief Backend `set_gpu_postfx_enabled` — toggle the postfx route.
///
/// When enabled, frames render to the offscreen scene target so postfx
/// can read it. When disabled, frames render straight to the swapchain.
static void d3d11_set_gpu_postfx_enabled(void *ctx_ptr, int8_t enabled) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int8_t next_enabled = enabled ? 1 : 0;
    if (!ctx)
        return;
    if (vgfx3d_d3d11_should_reset_composited_swapchain_for_postfx_update(ctx->gpu_postfx_enabled,
                                                                         next_enabled)) {
        ctx->scene_composited_to_swapchain = 0;
        ctx->presented_color_valid = 0;
    }
    ctx->gpu_postfx_enabled = next_enabled;
    if (!ctx->gpu_postfx_enabled) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        d3d11_unbind_postfx_resources(ctx);
        d3d11_reset_temporal_scene_state(ctx);
    }
}

/// @brief Invalidate stale presented snapshots when a new post-FX latch starts a frame.
static void d3d11_invalidate_presented_snapshot_for_postfx_latch(d3d11_context_t *ctx) {
    if (!ctx || ctx->scene_composited_to_swapchain)
        return;
    ctx->presented_color_valid = 0;
}

/// @brief Backend `set_gpu_postfx_snapshot` - latch the frame's postfx chain for replay.
/// @details Called once per frame from the canvas. A null `postfx` clears the cached
///   chain (equivalent to "no postfx this frame"); otherwise the chain is deep-copied
///   into the context so later present-time passes see a stable snapshot even if the
///   caller mutates their chain afterward. A copy failure aborts-and-resets rather than
///   rendering with a partial chain.
static void d3d11_set_gpu_postfx_snapshot(void *ctx_ptr, const vgfx3d_postfx_chain_t *postfx) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx)
        return;
    d3d11_invalidate_presented_snapshot_for_postfx_latch(ctx);
    if (!postfx) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        return;
    }
    if (!vgfx3d_postfx_chain_copy(&ctx->gpu_postfx_chain, postfx)) {
        vgfx3d_postfx_chain_reset(&ctx->gpu_postfx_chain);
        ctx->gpu_postfx_chain_valid = 0;
        return;
    }
    ctx->gpu_postfx_chain_valid = 1;
}

/// @brief Backend `set_render_target` — bind / unbind RTT mode.
///
/// NULL `rt` tears down RTT and reverts to swapchain rendering.
/// Non-NULL ensures RTT targets at the requested size and activates them.
static void d3d11_set_render_target(void *ctx_ptr, vgfx3d_rendertarget_t *rt) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    HRESULT hr;

    if (!ctx)
        return;
    ctx->scene_composited_to_swapchain = 0;
    ctx->presented_color_valid = 0;
    if (!rt) {
        d3d11_destroy_rtt_targets(ctx);
        return;
    }

    hr = d3d11_ensure_rtt_targets(ctx, rt);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateRTTTargets", hr);
        d3d11_destroy_rtt_targets(ctx);
    }
}

/// @brief Leave any active shadow pass and restore the current color/depth target set.
static void d3d11_leave_shadow_pass(d3d11_context_t *ctx) {
    if (!ctx || ctx->shadow_pass_slot < 0)
        return;
    d3d11_unbind_output_targets(ctx);
    ctx->shadow_pass_slot = -1;
    d3d11_select_current_targets(ctx);
}

/// @brief Backend `shadow_begin` - switch the pipeline to shadow-pass mode.
///
/// Allocates the shadow depth target if needed, captures the light's
/// view-projection matrix (used by main pass for shadow sampling),
/// unbinds draw/shadow SRVs (so we can write to it), binds the shadow
/// DSV with no color RTV (depth-only pass), clears depth, sets viewport,
/// and binds the shadow vertex shader with no pixel shader.
static void d3d11_shadow_begin(void *ctx_ptr,
                               int32_t slot,
                               float *depth_buf,
                               int32_t width,
                               int32_t height,
                               const float *light_vp) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    D3D11_VIEWPORT viewport;
    HRESULT hr;

    (void)depth_buf;
    if (!ctx)
        return;
    d3d11_leave_shadow_pass(ctx);
    if (!light_vp || width <= 0 || height <= 0 || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    ctx->shadow_pass_slot = -1;
    hr = d3d11_ensure_shadow_targets(ctx, slot, width, height);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateShadowTargets", hr);
        d3d11_release_shadow_slot(ctx, slot);
        d3d11_recompute_shadow_count(ctx);
        return;
    }

    ctx->shadow_pass_slot = slot;
    vgfx3d_d3d11_copy_mat4_finite_or_identity(ctx->shadow_vp[slot], light_vp);
    d3d11_unbind_draw_resources(ctx);
    d3d11_unbind_shadow_resources(ctx);
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 0, NULL, ctx->shadow_dsv[slot]);
    ID3D11DeviceContext_ClearDepthStencilView(
        ctx->ctx, ctx->shadow_dsv[slot], D3D11_CLEAR_DEPTH, 1.0f, 0);
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)width;
    viewport.Height = (FLOAT)height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->depth_state, 0);
    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, ctx->input_layout);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_shadow, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, NULL, NULL, 0);
}

/// @brief Backend `shadow_draw` — emit a single mesh into the shadow depth buffer.
///
/// Slimmer than `submit_draw`: only object/scene cbuffers + bone
/// palettes + morph SRV (if any) — no material, no lights, no
/// pixel shader. Uses the light-space VP captured by `shadow_begin`.
static void d3d11_shadow_draw(void *ctx_ptr, const vgfx3d_draw_cmd_t *cmd) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    d3d_per_object_t object_data;
    d3d_per_scene_t scene_data;
    d3d_per_material_t material_data;
    d3d_temp_srv_t shadow_diffuse = {0};
    ID3D11ShaderResourceView *shadow_diffuse_srv = NULL;
    ID3D11ShaderResourceView *null_shadow_ps_srv[1] = {NULL};
    ID3D11SamplerState *shadow_diffuse_sampler = NULL;
    HRESULT hr;
    UINT stride = sizeof(vgfx3d_vertex_t);
    UINT offset = 0;
    ID3D11Buffer *mesh_vb = NULL;
    ID3D11Buffer *mesh_ib = NULL;
    ID3D11RasterizerState *shadow_rasterizer = NULL;
    int alpha_masked_shadow = cmd && cmd->alpha_mode == RT_MATERIAL3D_ALPHA_MODE_MASK;
    uint32_t index_count;

    if (!ctx || !cmd || ctx->shadow_pass_slot < 0 ||
        ctx->shadow_pass_slot >= VGFX3D_MAX_SHADOW_LIGHTS || !cmd->vertices || !cmd->indices ||
        cmd->vertex_count == 0 || cmd->index_count == 0)
        return;
    index_count = vgfx3d_draw_cmd_validated_index_count(cmd);
    if (index_count == 0)
        return;

    if (d3d11_draw_needs_depth_bias(cmd))
        (void)d3d11_get_depth_biased_rasterizer(
            ctx, cmd, 0, cmd->double_sided ? 0 : 1, &shadow_rasterizer);
    if (!shadow_rasterizer)
        shadow_rasterizer = d3d11_choose_rasterizer(ctx, 0, cmd->double_sided ? 0 : 1);
    if (shadow_rasterizer)
        ID3D11DeviceContext_RSSetState(ctx->ctx, shadow_rasterizer);

    d3d11_prepare_object_data(cmd, &object_data);
    memset(&scene_data, 0, sizeof(scene_data));
    memcpy(scene_data.vp, ctx->shadow_vp[ctx->shadow_pass_slot], sizeof(scene_data.vp));
    d3d11_prepare_anim_resources(ctx, cmd, &object_data);

    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_object, &object_data, sizeof(object_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerObject shadow)", hr);
        return;
    }
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_per_scene, &scene_data, sizeof(scene_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbPerScene shadow)", hr);
        return;
    }
    if (alpha_masked_shadow) {
        shadow_diffuse_srv = d3d11_get_or_create_material_srv(
            ctx,
            cmd->texture_asset,
            cmd->texture,
            cmd->texture_asset_cache_key[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
            cmd->texture_asset_mip_start[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
            cmd->texture_asset_mip_count[RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR],
            &shadow_diffuse);
        d3d11_prepare_material_data(
            cmd, shadow_diffuse_srv != NULL, 0, 0, 0, 0, 0, 0, 0, &material_data);
        hr = d3d11_update_constant_buffer(
            ctx, ctx->cb_per_material, &material_data, sizeof(material_data));
        if (FAILED(hr)) {
            d3d11_log_hresult("Map(cbPerMaterial shadow)", hr);
            d3d11_release_temp_srv(&shadow_diffuse);
            return;
        }
    }

    if (!d3d11_acquire_mesh_buffers(ctx, cmd, &mesh_vb, &mesh_ib)) {
        d3d11_release_temp_srv(&shadow_diffuse);
        return;
    }

    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &mesh_vb, &stride, &offset);
    ID3D11DeviceContext_IASetIndexBuffer(ctx->ctx, mesh_ib, DXGI_FORMAT_R32_UINT, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_per_object);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 1, 1, &ctx->cb_per_scene);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 4, 1, &ctx->cb_bones);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 5, 1, &ctx->cb_prev_bones);
    if (alpha_masked_shadow && ctx->ps_shadow) {
        shadow_diffuse_sampler =
            d3d11_get_material_sampler(ctx, cmd, RT_MATERIAL3D_TEXTURE_SLOT_BASE_COLOR);
        ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_shadow, NULL, 0);
        ID3D11DeviceContext_PSSetConstantBuffers(ctx->ctx, 2, 1, &ctx->cb_per_material);
        ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &shadow_diffuse_srv);
        if (shadow_diffuse_sampler)
            ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &shadow_diffuse_sampler);
    } else {
        ID3D11DeviceContext_PSSetShader(ctx->ctx, NULL, NULL, 0);
    }
    {
        ID3D11ShaderResourceView *vs_srvs[2] = {ctx->current_morph_srv, NULL};
        ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, vs_srvs);
    }
    ID3D11DeviceContext_DrawIndexed(ctx->ctx, index_count, 0, 0);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, null_shadow_ps_srv);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, NULL, NULL, 0);
    d3d11_release_temp_srv(&shadow_diffuse);
    {
        ID3D11ShaderResourceView *null_vs[2] = {NULL, NULL};
        ID3D11DeviceContext_VSSetShaderResources(ctx->ctx, 0, 2, null_vs);
    }
}

/// @brief Backend `shadow_end` — exit shadow-pass mode and rebind the main targets.
///
/// Marks `shadow_active = 1` so the main pass's PerScene cbuffer
/// reports `shadowEnabled` to the shader. Stores the bias the shader
/// uses to bias depth comparisons. Re-selects the active scene/RTT
/// targets so subsequent draws hit the right buffers.
static void d3d11_shadow_end(void *ctx_ptr, int32_t slot, float bias) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (!ctx)
        return;
    if (slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS || ctx->shadow_pass_slot != slot) {
        d3d11_leave_shadow_pass(ctx);
        return;
    }
    d3d11_recompute_shadow_count(ctx);
    ctx->shadow_bias = vgfx3d_d3d11_finite_or(bias, 0.0f);
    ctx->shadow_pass_slot = -1;
    d3d11_select_current_targets(ctx);
}

/// @brief Backend `draw_skybox` — full-screen triangle draw with the cubemap shader.
///
/// Sets up skybox VS/PS, the fullscreen-triangle VB built at context creation,
/// the inverse-projection/inverse-view-rotation constants, and a depth state
/// that draws "behind" everything else (`<=` compare with no depth writes).
/// The cubemap is bound from the cubemap cache or as a temp resource.
static void d3d11_draw_skybox(void *ctx_ptr, const void *cubemap_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    const rt_cubemap3d *cubemap = (const rt_cubemap3d *)cubemap_ptr;
    d3d_skybox_cb_t skybox_data;
    d3d_temp_srv_t cubemap_resource;
    ID3D11ShaderResourceView *srv;
    HRESULT hr;
    UINT stride = sizeof(float) * 3;
    UINT offset = 0;
    D3D11_VIEWPORT viewport;
    float blend_factor[4] = {0, 0, 0, 0};

    if (!ctx || !cubemap || !ctx->vs_skybox || !ctx->ps_skybox || !ctx->skybox_vb ||
        ctx->current_rtv_count == 0 || !ctx->current_rtvs[0] || ctx->current_width <= 0 ||
        ctx->current_height <= 0)
        return;

    memset(&cubemap_resource, 0, sizeof(cubemap_resource));
    srv = d3d11_get_or_create_cubemap_srv(ctx, cubemap, &cubemap_resource);
    if (!srv)
        return;

    d3d11_prepare_skybox_data(ctx, &skybox_data);
    hr = d3d11_update_constant_buffer(ctx, ctx->cb_skybox, &skybox_data, sizeof(skybox_data));
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(cbSkybox)", hr);
        d3d11_release_temp_srv(&cubemap_resource);
        return;
    }

    ID3D11DeviceContext_IASetInputLayout(ctx->ctx, ctx->input_layout_skybox);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetVertexBuffers(ctx->ctx, 0, 1, &ctx->skybox_vb, &stride, &offset);
    memset(&viewport, 0, sizeof(viewport));
    viewport.Width = (FLOAT)ctx->current_width;
    viewport.Height = (FLOAT)ctx->current_height;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_OMSetRenderTargets(ctx->ctx, 1, ctx->current_rtvs, ctx->current_dsv);
    ID3D11DeviceContext_RSSetViewports(ctx->ctx, 1, &viewport);
    ID3D11DeviceContext_VSSetShader(ctx->ctx, ctx->vs_skybox, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx->ctx, ctx->ps_skybox, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx->ctx, 0, 1, &ctx->cb_skybox);
    if (ctx->linear_clamp_sampler)
        ID3D11DeviceContext_PSSetSamplers(ctx->ctx, 0, 1, &ctx->linear_clamp_sampler);
    ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &srv);
    ID3D11DeviceContext_RSSetState(ctx->ctx, ctx->rs_solid_no_cull);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx->ctx, ctx->depth_state_readonly_lequal, 0);
    ID3D11DeviceContext_OMSetBlendState(
        ctx->ctx, ctx->blend_state_opaque, blend_factor, 0xFFFFFFFF);
    ID3D11DeviceContext_Draw(ctx->ctx, 3, 0);
    {
        ID3D11ShaderResourceView *null_srv = NULL;
        ID3D11DeviceContext_PSSetShaderResources(ctx->ctx, 0, 1, &null_srv);
    }
    d3d11_release_temp_srv(&cubemap_resource);
}
