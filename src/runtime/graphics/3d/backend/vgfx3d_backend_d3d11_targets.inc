//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_targets.inc  (#included from
// vgfx3d_backend_d3d11.c; not a standalone translation unit) Purpose: D3D11 render-target
// lifecycle: scene, overlay, post-FX, RTT, and shadow targets
//===----------------------------------------------------------------------===//

/// @brief Allocate a render-target color texture (with optional shader-readable SRV).
///
/// Defaults to single-sample, MipLevels=1. Format chosen by the
/// caller-supplied `format_class`. If `out_srv` is non-NULL, also adds
/// `D3D11_BIND_SHADER_RESOURCE` so post-FX passes can sample the
/// rendered scene.
static HRESULT d3d11_create_color_target(d3d11_context_t *ctx,
                                         int32_t width,
                                         int32_t height,
                                         vgfx3d_d3d11_color_format_t format_class,
                                         ID3D11Texture2D **out_tex,
                                         ID3D11RenderTargetView **out_rtv,
                                         ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_rtv)
        *out_rtv = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !ctx->device || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height) ||
        !out_tex || !out_rtv)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = d3d11_color_format_to_dxgi(format_class);
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | (out_srv ? D3D11_BIND_SHADER_RESOURCE : 0);

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (FAILED(hr))
        return hr;

    memset(&rtv_desc, 0, sizeof(rtv_desc));
    rtv_desc.Format = desc.Format;
    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = ID3D11Device_CreateRenderTargetView(
        ctx->device, (ID3D11Resource *)*out_tex, &rtv_desc, out_rtv);
    if (FAILED(hr)) {
        SAFE_RELEASE(*out_tex);
        return hr;
    }

    if (out_srv) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
        if (FAILED(hr)) {
            SAFE_RELEASE(*out_rtv);
            SAFE_RELEASE(*out_tex);
            return hr;
        }
    }

    return S_OK;
}

/// @brief Allocate a depth-stencil texture (with optional shader-readable SRV).
///
/// Uses TYPELESS storage when `shader_readable` is set so the texture
/// can be aliased as both DSV (D32_FLOAT) and SRV (R32_FLOAT) — this
/// pattern lets shadow maps and SSAO read the depth buffer.
static HRESULT d3d11_create_depth_target(d3d11_context_t *ctx,
                                         int32_t width,
                                         int32_t height,
                                         int shader_readable,
                                         ID3D11Texture2D **out_tex,
                                         ID3D11DepthStencilView **out_dsv,
                                         ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_dsv)
        *out_dsv = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !ctx->device || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height) ||
        !out_tex || !out_dsv)
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = shader_readable ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags =
        D3D11_BIND_DEPTH_STENCIL | (shader_readable && out_srv ? D3D11_BIND_SHADER_RESOURCE : 0);

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (FAILED(hr))
        return hr;

    memset(&dsv_desc, 0, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = ID3D11Device_CreateDepthStencilView(
        ctx->device, (ID3D11Resource *)*out_tex, &dsv_desc, out_dsv);
    if (FAILED(hr)) {
        SAFE_RELEASE(*out_tex);
        return hr;
    }

    if (shader_readable && out_srv) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
        if (FAILED(hr)) {
            SAFE_RELEASE(*out_dsv);
            SAFE_RELEASE(*out_tex);
            return hr;
        }
    }

    return S_OK;
}

/// @brief Allocate a CPU-readable staging texture for `Map(READ)`-based readback.
///
/// Used by `d3d11_readback_*` to copy GPU textures to system memory
/// for screenshot capture, golden-frame tests, etc.
static HRESULT d3d11_create_staging_texture(d3d11_context_t *ctx,
                                            int32_t width,
                                            int32_t height,
                                            DXGI_FORMAT format,
                                            ID3D11Texture2D **out_tex) {
    D3D11_TEXTURE2D_DESC desc;

    if (out_tex)
        *out_tex = NULL;
    if (!ctx || !ctx->device || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height) ||
        !out_tex ||
        (format != DXGI_FORMAT_R8G8B8A8_UNORM && format != DXGI_FORMAT_R16G16B16A16_FLOAT))
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    return ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
}

/// @brief Allocate/reuse the default-usage texture that snapshots the final swapchain image.
static HRESULT d3d11_ensure_presented_snapshot_texture(d3d11_context_t *ctx,
                                                       int32_t width,
                                                       int32_t height) {
    D3D11_TEXTURE2D_DESC desc;
    HRESULT hr;

    if (!ctx || !ctx->device || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->presented_color_tex && ctx->presented_width == width &&
        ctx->presented_height == height)
        return S_OK;

    SAFE_RELEASE(ctx->presented_color_tex);
    ctx->presented_width = 0;
    ctx->presented_height = 0;
    ctx->presented_color_valid = 0;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)width;
    desc.Height = (UINT)height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, &ctx->presented_color_tex);
    if (SUCCEEDED(hr)) {
        ctx->presented_width = width;
        ctx->presented_height = height;
    }
    return hr;
}

/// @brief Capture the current swapchain backbuffer before Present invalidates it.
static int d3d11_snapshot_backbuffer_for_readback(d3d11_context_t *ctx) {
    ID3D11Texture2D *back_buffer = NULL;
    HRESULT hr;

    if (!ctx || !ctx->swap_chain || !ctx->ctx)
        return 0;
    ctx->presented_color_valid = 0;
    hr = d3d11_ensure_presented_snapshot_texture(ctx, ctx->width, ctx->height);
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D(presentedSnapshot)", hr);
        return 0;
    }
    hr = IDXGISwapChain_GetBuffer(ctx->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) {
        d3d11_log_hresult("IDXGISwapChain::GetBuffer(presentedSnapshot)", hr);
        return 0;
    }
    d3d11_unbind_draw_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    ID3D11DeviceContext_CopyResource(
        ctx->ctx, (ID3D11Resource *)ctx->presented_color_tex, (ID3D11Resource *)back_buffer);
    SAFE_RELEASE(back_buffer);
    d3d11_restore_current_target_bindings(ctx);
    ctx->presented_color_valid = 1;
    return 1;
}

/// @brief Release every offscreen render-target the scene path uses.
///
/// Tears down: scene color RTV/SRV, motion-vector RTV/SRV, depth
/// DSV/SRV, and the overlay color target. Called on resize and
/// during context destruction.
static void d3d11_destroy_scene_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    d3d11_unbind_postfx_resources(ctx);
    d3d11_unbind_shadow_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_SCENE ||
        ctx->current_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
        d3d11_clear_current_target_bindings(ctx);
    }
    SAFE_RELEASE(ctx->postfx_scratch_srv);
    SAFE_RELEASE(ctx->postfx_scratch_rtv);
    SAFE_RELEASE(ctx->postfx_scratch_tex);
    SAFE_RELEASE(ctx->postfx_color_srv);
    SAFE_RELEASE(ctx->postfx_color_rtv);
    SAFE_RELEASE(ctx->postfx_color_tex);
    SAFE_RELEASE(ctx->presented_color_tex);
    SAFE_RELEASE(ctx->overlay_color_srv);
    SAFE_RELEASE(ctx->overlay_color_rtv);
    SAFE_RELEASE(ctx->overlay_color_tex);
    SAFE_RELEASE(ctx->scene_depth_srv);
    SAFE_RELEASE(ctx->scene_dsv);
    SAFE_RELEASE(ctx->scene_depth_tex);
    SAFE_RELEASE(ctx->scene_motion_srv);
    SAFE_RELEASE(ctx->scene_motion_rtv);
    SAFE_RELEASE(ctx->scene_motion_tex);
    SAFE_RELEASE(ctx->scene_color_srv);
    SAFE_RELEASE(ctx->scene_color_rtv);
    SAFE_RELEASE(ctx->scene_color_tex);
    ctx->scene_width = 0;
    ctx->scene_height = 0;
    ctx->overlay_width = 0;
    ctx->overlay_height = 0;
    ctx->postfx_width = 0;
    ctx->postfx_height = 0;
    ctx->postfx_scratch_width = 0;
    ctx->postfx_scratch_height = 0;
    ctx->presented_width = 0;
    ctx->presented_height = 0;
    ctx->presented_color_valid = 0;
    d3d11_reset_temporal_scene_state(ctx);
}

/// @brief Allocate scene render targets at the requested size, idempotent on size match.
///
/// Builds three targets: scene color (HDR-capable), motion vectors
/// (LDR), and depth (R32). On any failure the partially-built set is
/// torn back down so the next call can retry from a clean state.
static HRESULT d3d11_ensure_scene_targets(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->scene_color_tex && ctx->scene_color_rtv && ctx->scene_color_srv &&
        ctx->scene_motion_tex && ctx->scene_motion_rtv && ctx->scene_motion_srv &&
        ctx->scene_depth_tex && ctx->scene_dsv && ctx->scene_depth_srv &&
        ctx->scene_width == width && ctx->scene_height == height)
        return S_OK;

    d3d11_destroy_scene_targets(ctx);

    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE),
                                   &ctx->scene_color_tex,
                                   &ctx->scene_color_rtv,
                                   &ctx->scene_color_srv);
    if (FAILED(hr))
        return hr;
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SWAPCHAIN),
                                   &ctx->scene_motion_tex,
                                   &ctx->scene_motion_rtv,
                                   &ctx->scene_motion_srv);
    if (FAILED(hr)) {
        d3d11_destroy_scene_targets(ctx);
        return hr;
    }
    hr = d3d11_create_depth_target(
        ctx, width, height, 1, &ctx->scene_depth_tex, &ctx->scene_dsv, &ctx->scene_depth_srv);
    if (FAILED(hr)) {
        d3d11_destroy_scene_targets(ctx);
        return hr;
    }

    ctx->scene_width = width;
    ctx->scene_height = height;
    return S_OK;
}

/// @brief Allocate the overlay render target (HUD layer above 3D scene).
///
/// Single color target — overlay doesn't need its own depth (it's
/// drawn 2D on top of the composited scene).
static HRESULT d3d11_ensure_overlay_target(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->overlay_color_tex && ctx->overlay_color_rtv && ctx->overlay_color_srv &&
        ctx->overlay_width == width && ctx->overlay_height == height)
        return S_OK;
    if (ctx->overlay_color_tex || ctx->overlay_color_rtv || ctx->overlay_color_srv) {
        d3d11_unbind_postfx_resources(ctx);
        if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_OVERLAY) {
            d3d11_unbind_output_targets(ctx);
            d3d11_clear_current_target_bindings(ctx);
        }
        SAFE_RELEASE(ctx->overlay_color_srv);
        SAFE_RELEASE(ctx->overlay_color_rtv);
        SAFE_RELEASE(ctx->overlay_color_tex);
        ctx->overlay_width = 0;
        ctx->overlay_height = 0;
        ctx->overlay_used_this_frame = 0;
    }
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_OVERLAY),
                                   &ctx->overlay_color_tex,
                                   &ctx->overlay_color_rtv,
                                   &ctx->overlay_color_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(ctx->overlay_color_srv);
        SAFE_RELEASE(ctx->overlay_color_rtv);
        SAFE_RELEASE(ctx->overlay_color_tex);
    } else {
        ctx->overlay_width = width;
        ctx->overlay_height = height;
    }
    return hr;
}

/// @brief Ensure the post-processing render target is allocated at the requested dimensions.
/// @details Skips reallocation when an existing target already matches @p width × @p height.
///          On a size change (or first call) releases the old color SRV/RTV/texture, then
///          calls d3d11_create_color_target to allocate a new scene-format texture. Cleans up
///          all three D3D11 objects and returns the failure HRESULT if allocation fails.
/// @return S_OK on success; E_INVALIDARG for a null context or non-positive dimensions;
///         the D3D11 HRESULT from d3d11_create_color_target on allocation failure.
static HRESULT d3d11_ensure_postfx_target(d3d11_context_t *ctx, int32_t width, int32_t height) {
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->postfx_color_tex && ctx->postfx_color_rtv && ctx->postfx_color_srv &&
        ctx->postfx_width == width && ctx->postfx_height == height)
        return S_OK;

    d3d11_unbind_postfx_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    SAFE_RELEASE(ctx->postfx_color_srv);
    SAFE_RELEASE(ctx->postfx_color_rtv);
    SAFE_RELEASE(ctx->postfx_color_tex);
    ctx->postfx_width = 0;
    ctx->postfx_height = 0;
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE),
                                   &ctx->postfx_color_tex,
                                   &ctx->postfx_color_rtv,
                                   &ctx->postfx_color_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(ctx->postfx_color_srv);
        SAFE_RELEASE(ctx->postfx_color_rtv);
        SAFE_RELEASE(ctx->postfx_color_tex);
        return hr;
    }
    ctx->postfx_width = width;
    ctx->postfx_height = height;
    return S_OK;
}

/// @brief Ensure the secondary post-FX scratch target used for non-destructive ping-pong.
static HRESULT d3d11_ensure_postfx_scratch_target(d3d11_context_t *ctx,
                                                  int32_t width,
                                                  int32_t height) {
    HRESULT hr;

    if (!ctx || !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->postfx_scratch_tex && ctx->postfx_scratch_rtv && ctx->postfx_scratch_srv &&
        ctx->postfx_scratch_width == width && ctx->postfx_scratch_height == height)
        return S_OK;

    d3d11_unbind_postfx_resources(ctx);
    d3d11_unbind_output_targets(ctx);
    SAFE_RELEASE(ctx->postfx_scratch_srv);
    SAFE_RELEASE(ctx->postfx_scratch_rtv);
    SAFE_RELEASE(ctx->postfx_scratch_tex);
    ctx->postfx_scratch_width = 0;
    ctx->postfx_scratch_height = 0;
    hr = d3d11_create_color_target(ctx,
                                   width,
                                   height,
                                   vgfx3d_d3d11_choose_color_format(VGFX3D_D3D11_TARGET_SCENE),
                                   &ctx->postfx_scratch_tex,
                                   &ctx->postfx_scratch_rtv,
                                   &ctx->postfx_scratch_srv);
    if (FAILED(hr)) {
        SAFE_RELEASE(ctx->postfx_scratch_srv);
        SAFE_RELEASE(ctx->postfx_scratch_rtv);
        SAFE_RELEASE(ctx->postfx_scratch_tex);
        return hr;
    }
    ctx->postfx_scratch_width = width;
    ctx->postfx_scratch_height = height;
    return S_OK;
}

/// @brief Copy the GPU-side RTT color texture down to the CPU-side `rt_pixels` payload.
/// @details When a Canvas3D rendertarget is read back via `RenderTarget3D.Pixels()`, the
///   D3D11 backend copies the bound `rtt_color_tex` through a staging texture (CPU-readable),
///   maps it, and writes the pixels into `target->color_pixels` using `rt_pixels` layout.
///   Returns 0 when preconditions fail so the caller can fall back to the last cached copy.
static int d3d11_sync_render_target_color(void *ctx_ptr, vgfx3d_rendertarget_t *target) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    int restore_target = 0;
    int mapped_ok = 0;
    int ok = 0;

    if (!ctx || !ctx->ctx || !target || !ctx->rtt_color_tex || !ctx->rtt_staging ||
        ctx->rtt_target != target || target->width <= 0 || target->height <= 0) {
        return 0;
    }
    if (!vgfx3d_rendertarget_ensure_color(target))
        return 0;
    if (vgfx3d_rendertarget_is_hdr(target) && !vgfx3d_rendertarget_ensure_hdr_color(target))
        return 0;

    restore_target = ctx->current_target_kind == VGFX3D_D3D11_TARGET_RTT ? 1 : 0;
    if (restore_target)
        d3d11_unbind_output_targets(ctx);

    ID3D11DeviceContext_CopyResource(
        ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, (ID3D11Resource *)ctx->rtt_color_tex);
    hr = ID3D11DeviceContext_Map(
        ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        d3d11_log_hresult("Map(rttStaging)", hr);
        goto cleanup;
    }
    if (!mapped.pData)
        goto cleanup;
    mapped_ok = 1;

    if (vgfx3d_rendertarget_is_hdr(target)) {
        size_t hdr_row_bytes;
        size_t rgba_row_bytes;
        if (mapped.RowPitch > (UINT)INT32_MAX)
            goto cleanup;
        if (!vgfx3d_d3d11_compute_row_bytes(target->width, 8, &hdr_row_bytes) ||
            !vgfx3d_d3d11_compute_row_bytes(target->width, 4, &rgba_row_bytes) ||
            mapped.RowPitch < hdr_row_bytes || (size_t)target->stride < rgba_row_bytes)
            goto cleanup;
        vgfx3d_copy_linear_rgba16f_to_rgba8(target->color_buf,
                                            target->stride,
                                            target->width,
                                            target->height,
                                            (const uint16_t *)mapped.pData,
                                            (int32_t)mapped.RowPitch);
        vgfx3d_copy_linear_rgba16f_to_rgba32f(target->hdr_color_buf,
                                              target->width * 4,
                                              target->width,
                                              target->height,
                                              (const uint16_t *)mapped.pData,
                                              (int32_t)mapped.RowPitch);
        target->hdr_color_valid = 1;
        ok = 1;
    } else {
        size_t row_bytes;
        if (!vgfx3d_d3d11_compute_row_bytes(target->width, 4, &row_bytes) ||
            (size_t)target->stride < row_bytes || mapped.RowPitch < row_bytes)
            goto cleanup;
        for (int32_t y = 0; y < target->height; y++) {
            memcpy(&target->color_buf[(size_t)y * (size_t)target->stride],
                   (const uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch,
                   (size_t)row_bytes);
        }
        target->hdr_color_valid = 0;
        ok = 1;
    }

cleanup:
    if (mapped_ok)
        ID3D11DeviceContext_Unmap(ctx->ctx, (ID3D11Resource *)ctx->rtt_staging, 0);
    if (restore_target)
        d3d11_restore_current_target_bindings(ctx);
    if (ok)
        target->color_dirty = 0;
    return ok;
}

/// @brief Release every offscreen render-to-texture (RTT) resource.
///
/// `rtt_target` is the user-visible RT object — we set it to NULL so
/// the next bind cycle doesn't think the targets are still live.
static void d3d11_destroy_rtt_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->rtt_target) {
        int sync_ok = 1;
        if (ctx->rtt_target->color_dirty)
            sync_ok = d3d11_sync_render_target_color(ctx, ctx->rtt_target);
        if (sync_ok || !ctx->rtt_target->color_dirty)
            vgfx3d_rendertarget_clear_sync(ctx->rtt_target);
        else
            vgfx3d_rendertarget_detach_sync_preserve_dirty(ctx->rtt_target);
    }
    if (ctx->current_target_kind == VGFX3D_D3D11_TARGET_RTT) {
        d3d11_unbind_output_targets(ctx);
        d3d11_clear_current_target_bindings(ctx);
    }
    SAFE_RELEASE(ctx->rtt_staging);
    SAFE_RELEASE(ctx->rtt_dsv);
    SAFE_RELEASE(ctx->rtt_depth_tex);
    SAFE_RELEASE(ctx->rtt_rtv);
    SAFE_RELEASE(ctx->rtt_color_tex);
    ctx->rtt_width = 0;
    ctx->rtt_height = 0;
    ctx->rtt_color_format = (int32_t)VGFX3D_RENDERTARGET_COLOR_FORMAT_UNORM8;
    ctx->rtt_active = 0;
    ctx->rtt_target = NULL;
    if (ctx->active_target_kind == VGFX3D_D3D11_TARGET_RTT)
        ctx->active_target_kind = VGFX3D_D3D11_TARGET_SWAPCHAIN;
    ctx->current_pass_is_overlay = 0;
    ctx->current_load_existing_color = 0;
    ctx->overlay_used_this_frame = 0;
}

/// @brief Allocate RTT color + depth + staging textures sized to `rt`.
///
/// Sets `rtt_active` so subsequent bind calls route to the RTT instead
/// of the swapchain. The staging texture is for CPU readback.
static HRESULT d3d11_ensure_rtt_targets(d3d11_context_t *ctx, vgfx3d_rendertarget_t *rt) {
    HRESULT hr;
    vgfx3d_d3d11_color_format_t color_format;
    DXGI_FORMAT staging_format;

    if (!ctx || !rt || !vgfx3d_d3d11_is_valid_texture2d_extent(rt->width, rt->height))
        return E_INVALIDARG;
    if (ctx->rtt_color_tex && ctx->rtt_rtv && ctx->rtt_depth_tex && ctx->rtt_dsv &&
        ctx->rtt_staging && ctx->rtt_width == rt->width && ctx->rtt_height == rt->height &&
        ctx->rtt_color_format == rt->color_format) {
        if (ctx->rtt_target && ctx->rtt_target != rt) {
            d3d11_destroy_rtt_targets(ctx);
        } else {
            ctx->rtt_active = 1;
            ctx->rtt_target = rt;
            ctx->current_pass_is_overlay = 0;
            ctx->current_load_existing_color = 0;
            ctx->overlay_used_this_frame = 0;
            rt->color_dirty = 0;
            rt->sync_color = d3d11_sync_render_target_color;
            rt->sync_color_userdata = ctx;
            return S_OK;
        }
    }

    d3d11_destroy_rtt_targets(ctx);
    color_format = vgfx3d_rendertarget_is_hdr(rt) ? VGFX3D_D3D11_COLOR_FORMAT_HDR16F
                                                  : VGFX3D_D3D11_COLOR_FORMAT_UNORM8;
    staging_format = vgfx3d_rendertarget_is_hdr(rt) ? DXGI_FORMAT_R16G16B16A16_FLOAT
                                                    : DXGI_FORMAT_R8G8B8A8_UNORM;

    hr = d3d11_create_color_target(
        ctx, rt->width, rt->height, color_format, &ctx->rtt_color_tex, &ctx->rtt_rtv, NULL);
    if (FAILED(hr))
        return hr;
    hr = d3d11_create_depth_target(
        ctx, rt->width, rt->height, 0, &ctx->rtt_depth_tex, &ctx->rtt_dsv, NULL);
    if (FAILED(hr)) {
        d3d11_destroy_rtt_targets(ctx);
        return hr;
    }
    hr =
        d3d11_create_staging_texture(ctx, rt->width, rt->height, staging_format, &ctx->rtt_staging);
    if (FAILED(hr)) {
        d3d11_destroy_rtt_targets(ctx);
        return hr;
    }

    ctx->rtt_width = rt->width;
    ctx->rtt_height = rt->height;
    ctx->rtt_color_format = rt->color_format;
    ctx->rtt_active = 1;
    ctx->rtt_target = rt;
    ctx->current_pass_is_overlay = 0;
    ctx->current_load_existing_color = 0;
    ctx->overlay_used_this_frame = 0;
    rt->color_dirty = 0;
    rt->sync_color = d3d11_sync_render_target_color;
    rt->sync_color_userdata = ctx;
    return S_OK;
}

/// @brief Release shadow-map depth target resources.
static void d3d11_destroy_shadow_targets(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    d3d11_unbind_shadow_resources(ctx);
    if (ctx->shadow_pass_slot >= 0) {
        d3d11_unbind_output_targets(ctx);
        ctx->shadow_pass_slot = -1;
    }
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        SAFE_RELEASE(ctx->shadow_srv[slot]);
        SAFE_RELEASE(ctx->shadow_dsv[slot]);
        SAFE_RELEASE(ctx->shadow_depth_tex[slot]);
        ctx->shadow_width[slot] = 0;
        ctx->shadow_height[slot] = 0;
    }
    ctx->shadow_pass_slot = -1;
    ctx->shadow_count = 0;
}

/// @brief Release one shadow-map slot and detach it from any active bindings.
/// @details Clears all shadow SRVs first because D3D11 forbids releasing/rebinding
///   a depth texture while a shader-resource view of the same texture is still
///   visible to the pixel shader. If the slot is currently being rendered, the
///   output-merger is also detached and the active shadow-pass marker is cleared.
static void d3d11_release_shadow_slot(d3d11_context_t *ctx, int32_t slot) {
    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS)
        return;
    d3d11_unbind_shadow_resources(ctx);
    if (ctx->shadow_pass_slot == slot) {
        d3d11_unbind_output_targets(ctx);
        ctx->shadow_pass_slot = -1;
    }
    SAFE_RELEASE(ctx->shadow_srv[slot]);
    SAFE_RELEASE(ctx->shadow_dsv[slot]);
    SAFE_RELEASE(ctx->shadow_depth_tex[slot]);
    ctx->shadow_width[slot] = 0;
    ctx->shadow_height[slot] = 0;
}

/// @brief Recompute the highest contiguous shadow slot advertised to shaders.
/// @details The shader receives `shadowCount` and validates each light's
///   `shadowIndex` against it. Recomputing after allocation failures prevents a
///   stale count from making an empty slot sample a NULL SRV.
static void d3d11_recompute_shadow_count(d3d11_context_t *ctx) {
    int complete[VGFX3D_MAX_SHADOW_LIGHTS] = {0};

    if (!ctx)
        return;
    for (int32_t slot = 0; slot < VGFX3D_MAX_SHADOW_LIGHTS; slot++) {
        if (ctx->shadow_srv[slot] && ctx->shadow_dsv[slot] && ctx->shadow_depth_tex[slot])
            complete[slot] = 1;
    }
    ctx->shadow_count = vgfx3d_d3d11_compute_shadow_count(VGFX3D_MAX_SHADOW_LIGHTS, complete);
}

/// @brief Allocate the shadow-map depth target at the requested size.
///
/// Always shader-readable so the main pass can sample shadows.
static HRESULT d3d11_ensure_shadow_targets(d3d11_context_t *ctx,
                                           int32_t slot,
                                           int32_t width,
                                           int32_t height) {
    HRESULT hr;

    if (!ctx || slot < 0 || slot >= VGFX3D_MAX_SHADOW_LIGHTS ||
        !vgfx3d_d3d11_is_valid_texture2d_extent(width, height))
        return E_INVALIDARG;
    if (ctx->shadow_depth_tex[slot] && ctx->shadow_dsv[slot] && ctx->shadow_srv[slot] &&
        ctx->shadow_width[slot] == width && ctx->shadow_height[slot] == height)
        return S_OK;

    d3d11_release_shadow_slot(ctx, slot);
    hr = d3d11_create_depth_target(ctx,
                                   width,
                                   height,
                                   1,
                                   &ctx->shadow_depth_tex[slot],
                                   &ctx->shadow_dsv[slot],
                                   &ctx->shadow_srv[slot]);
    if (FAILED(hr))
        return hr;
    ctx->shadow_width[slot] = width;
    ctx->shadow_height[slot] = height;
    /* Fold the freshly-allocated slot into the advertised shadow_count so the main pass's
     * `shadowIndex < shadowCount` guard accepts it. The GL backend and the D3D11
     * failure/release paths recompute, but this success path previously did not, leaving a
     * valid shadow map silently unsampled until some other path refreshed the count. */
    d3d11_recompute_shadow_count(ctx);
    return S_OK;
}
