//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_d3d11_texture.inc  (#included from vgfx3d_backend_d3d11.c; not a standalone translation unit)
// Purpose: D3D11 texture/cubemap cache lifecycle and streamed upload + SRV creation
//===----------------------------------------------------------------------===//

// Texture and cubemap caches — keyed by stable host identity + generation so
// re-uploaded textures (Pixels.Set) get fresh GPU resources without
// aliasing allocator-reused addresses to stale uploads.

/// @brief Release one 2D-texture cache slot and clear all identity/upload metadata.
static void d3d11_release_texture_cache_entry(d3d_tex_cache_entry_t *entry) {
    if (!entry)
        return;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Release one cubemap cache slot and clear all identity/upload metadata.
static void d3d11_release_cubemap_cache_entry(d3d_cubemap_cache_entry_t *entry) {
    if (!entry)
        return;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Release every entry in the 2D-texture cache (teardown path).
static void d3d11_release_texture_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->tex_cache_count > 0)
        d3d11_unbind_draw_resources(ctx);
    for (int32_t i = 0; i < ctx->tex_cache_count; i++)
        d3d11_release_texture_cache_entry(&ctx->tex_cache[i]);
    ctx->tex_cache_count = 0;
    free(ctx->tex_cache);
    ctx->tex_cache = NULL;
    ctx->tex_cache_capacity = 0;
}

/// @brief Evict aged 2D texture-cache entries while keeping a resident floor.
static void d3d11_prune_texture_cache(d3d11_context_t *ctx) {
    int32_t write_index = 0;
    int32_t total_count;

    if (!ctx || !ctx->tex_cache)
        return;
    d3d11_unbind_draw_resources(ctx);
    total_count = ctx->tex_cache_count;
    for (int32_t i = 0; i < total_count; i++) {
        uint64_t age = ctx->frame_serial > ctx->tex_cache[i].last_used_frame
                           ? (ctx->frame_serial - ctx->tex_cache[i].last_used_frame)
                           : 0u;
        if (vgfx3d_d3d11_should_prune_cache_entry(total_count,
                                                  write_index,
                                                  i,
                                                  age,
                                                  D3D11_TEXTURE_CACHE_MAX_RESIDENT,
                                                  D3D11_TEXTURE_CACHE_PRUNE_AGE)) {
            d3d11_release_texture_cache_entry(&ctx->tex_cache[i]);
            continue;
        }
        if (write_index != i) {
            ctx->tex_cache[write_index] = ctx->tex_cache[i];
            memset(&ctx->tex_cache[i], 0, sizeof(ctx->tex_cache[i]));
        }
        write_index++;
    }
    ctx->tex_cache_count = write_index;
}

/// @brief Release every entry in the cubemap cache (teardown path).
static void d3d11_release_cubemap_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    if (ctx->cubemap_cache_count > 0)
        d3d11_unbind_draw_resources(ctx);
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++)
        d3d11_release_cubemap_cache_entry(&ctx->cubemap_cache[i]);
    ctx->cubemap_cache_count = 0;
    free(ctx->cubemap_cache);
    ctx->cubemap_cache = NULL;
    ctx->cubemap_cache_capacity = 0;
}

/// @brief Evict aged cubemap-cache entries while keeping a minimum resident set.
/// @details Runs a compacting sweep: entries whose `last_used_frame` is more than
///   `D3D11_CUBEMAP_CACHE_PRUNE_AGE` frames behind the current `frame_serial` are
///   released, and the rest are packed to the front of the array. The
///   `count - write_index > MAX_RESIDENT` guard means we never shrink below the
///   resident floor even if everything is aged — a scene that hasn't drawn in 10
///   seconds still keeps its working set warm for the next frame.
static void d3d11_prune_cubemap_cache(d3d11_context_t *ctx) {
    int32_t write_index = 0;
    int32_t total_count;

    if (!ctx || !ctx->cubemap_cache)
        return;
    d3d11_unbind_draw_resources(ctx);
    total_count = ctx->cubemap_cache_count;
    for (int32_t i = 0; i < total_count; i++) {
        uint64_t age = ctx->frame_serial > ctx->cubemap_cache[i].last_used_frame
                           ? (ctx->frame_serial - ctx->cubemap_cache[i].last_used_frame)
                           : 0u;
        if (vgfx3d_d3d11_should_prune_cache_entry(total_count,
                                                  write_index,
                                                  i,
                                                  age,
                                                  D3D11_CUBEMAP_CACHE_MAX_RESIDENT,
                                                  D3D11_CUBEMAP_CACHE_PRUNE_AGE)) {
            d3d11_release_cubemap_cache_entry(&ctx->cubemap_cache[i]);
            continue;
        }
        if (write_index != i) {
            ctx->cubemap_cache[write_index] = ctx->cubemap_cache[i];
            memset(&ctx->cubemap_cache[i], 0, sizeof(ctx->cubemap_cache[i]));
        }
        write_index++;
    }
    ctx->cubemap_cache_count = write_index;
}

/// @brief Grow the texture cache table to hold `needed` entries.
///
/// Geometric growth via the shared `vgfx3d_d3d11_next_capacity` helper.
static int d3d11_ensure_tex_cache_capacity(d3d11_context_t *ctx, int32_t needed) {
    int32_t new_capacity;
    d3d_tex_cache_entry_t *new_entries;

    if (!ctx || needed <= 0)
        return 0;
    if (ctx->tex_cache_capacity >= needed)
        return 1;
    new_capacity = vgfx3d_d3d11_next_capacity(ctx->tex_cache_capacity, needed, 64);
    if (new_capacity <= ctx->tex_cache_capacity ||
        (size_t)new_capacity > SIZE_MAX / sizeof(*new_entries))
        return 0;
    new_entries = (d3d_tex_cache_entry_t *)realloc(ctx->tex_cache,
                                                   (size_t)new_capacity * sizeof(*new_entries));
    if (!new_entries)
        return 0;
    memset(new_entries + ctx->tex_cache_capacity,
           0,
           (size_t)(new_capacity - ctx->tex_cache_capacity) * sizeof(*new_entries));
    ctx->tex_cache = new_entries;
    ctx->tex_cache_capacity = new_capacity;
    return 1;
}

/// @brief Grow the cubemap cache table to hold `needed` entries.
static int d3d11_ensure_cubemap_cache_capacity(d3d11_context_t *ctx, int32_t needed) {
    int32_t new_capacity;
    d3d_cubemap_cache_entry_t *new_entries;

    if (!ctx || needed <= 0)
        return 0;
    if (ctx->cubemap_cache_capacity >= needed)
        return 1;
    new_capacity = vgfx3d_d3d11_next_capacity(ctx->cubemap_cache_capacity, needed, 16);
    if (new_capacity <= ctx->cubemap_cache_capacity ||
        (size_t)new_capacity > SIZE_MAX / sizeof(*new_entries))
        return 0;
    new_entries = (d3d_cubemap_cache_entry_t *)realloc(ctx->cubemap_cache,
                                                       (size_t)new_capacity * sizeof(*new_entries));
    if (!new_entries)
        return 0;
    memset(new_entries + ctx->cubemap_cache_capacity,
           0,
           (size_t)(new_capacity - ctx->cubemap_cache_capacity) * sizeof(*new_entries));
    ctx->cubemap_cache = new_entries;
    ctx->cubemap_cache_capacity = new_capacity;
    return 1;
}

/// @brief Maximum LOD index (float) for a cubemap's full mip chain.
/// @details Returned as a float because D3D11's `SampleLevel` / `SampleBias` expect
///   a float LOD. A 1×1 face has only one mip and thus max-LOD 0. Used to clamp sampler
///   LOD ranges and compute prefiltered environment-map roughness lookups.
static float d3d11_cubemap_max_lod(const rt_cubemap3d *cubemap) {
    int32_t face_size;
    int32_t mip_count;

    if (!cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size) || face_size <= 1)
        return 0.0f;
    mip_count = vgfx3d_d3d11_compute_mip_count(face_size, face_size);
    return mip_count > 1 ? (float)(mip_count - 1) : 0.0f;
}

/// @brief Release the GPU resources in one morph-target cache slot.
///
/// Each slot holds two `(buffer, srv)` pairs — one for position deltas
/// and one for normal deltas. Both are released together.
static void d3d11_release_morph_cache_entry(d3d11_morph_cache_entry_t *entry) {
    if (!entry)
        return;
    SAFE_RELEASE(entry->normal_srv);
    SAFE_RELEASE(entry->normal_buffer);
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->buffer);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Release every morph-cache slot — called during context teardown.
static void d3d11_release_morph_cache(d3d11_context_t *ctx) {
    if (!ctx)
        return;
    for (int32_t i = 0; i < D3D11_MORPH_CACHE_CAPACITY; i++)
        d3d11_release_morph_cache_entry(&ctx->morph_cache[i]);
}

/// @brief Release a temporary (non-cached) SRV after the draw consumes it.
///
/// Used when the texture cache is full; we still create the resource
/// for the current draw but don't try to keep it around. Marking
/// `temporary` lets the per-draw cleanup distinguish it from cached
/// resources (which must NOT be released here).
static void d3d11_release_temp_srv(d3d_temp_srv_t *entry) {
    if (!entry || !entry->temporary)
        return;
    SAFE_RELEASE(entry->srv);
    SAFE_RELEASE(entry->tex);
    entry->temporary = 0;
}

/// @brief Add @p bytes to the context's running per-frame texture-upload total (saturating).
static void d3d11_record_texture_upload_bytes(d3d11_context_t *ctx, uint64_t bytes) {
    if (!ctx || bytes == 0)
        return;
    ctx->texture_upload_bytes = vgfx3d_d3d11_saturating_add_u64(ctx->texture_upload_bytes, bytes);
}

/// @brief Bytes still to upload for a cached texture entry (native or RGBA streaming path).
static uint64_t d3d11_texture_pending_bytes(const d3d_tex_cache_entry_t *entry) {
    if (!entry)
        return 0;
    if (entry->texture_asset)
        return vgfx3d_textureasset_pending_native_snapshot_bytes(
            entry->texture_asset,
            entry->native_mip_start,
            entry->native_mip_count,
            entry->native_next_mip,
            entry->native_next_block_row,
            entry->upload_in_progress);
    return vgfx3d_pending_rgba_upload_bytes(
        entry->width, entry->height, entry->upload_next_row, entry->upload_in_progress);
}

/// @brief Bytes still to upload for a cached cubemap entry across its remaining faces/rows.
static uint64_t d3d11_cubemap_pending_bytes(const d3d_cubemap_cache_entry_t *entry) {
    if (!entry)
        return 0;
    return vgfx3d_pending_cubemap_rgba_upload_bytes(
        entry->face_size, entry->upload_face, entry->upload_next_row, entry->upload_in_progress);
}

/// @brief Total bytes still pending across all in-progress texture/cubemap uploads (saturating).
static uint64_t d3d11_get_texture_upload_pending_bytes(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    uint64_t total = 0;

    if (!ctx)
        return 0;
    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        uint64_t bytes = d3d11_texture_pending_bytes(&ctx->tex_cache[i]);
        total = vgfx3d_d3d11_saturating_add_u64(total, bytes);
        if (total == UINT64_MAX)
            return UINT64_MAX;
    }
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        uint64_t bytes = d3d11_cubemap_pending_bytes(&ctx->cubemap_cache[i]);
        total = vgfx3d_d3d11_saturating_add_u64(total, bytes);
        if (total == UINT64_MAX)
            return UINT64_MAX;
    }
    return total;
}

/// @brief Set the per-frame byte budget that paces streaming texture uploads on this context.
static void d3d11_set_texture_upload_budget(void *ctx_ptr, uint64_t bytes) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    if (ctx)
        ctx->texture_upload_budget_bytes = bytes;
}

/// @brief Whether the device can use @p format as a sampleable 2D texture (CheckFormatSupport
/// query).
static int d3d11_format_supports_texture_sampling(d3d11_context_t *ctx, DXGI_FORMAT format) {
    UINT support = 0;
    if (!ctx || !ctx->device)
        return 0;
    if (FAILED(ID3D11Device_CheckFormatSupport(ctx->device, format, &support)))
        return 0;
    return ((support & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0 &&
            (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0);
}

/// @brief Report which native compressed texture formats this device can upload (BC7 only on
/// D3D11), plus sampler anisotropy support.
static int64_t d3d11_get_native_texture_caps(void *ctx_ptr) {
    d3d11_context_t *ctx = (d3d11_context_t *)ctx_ptr;
    int64_t caps = 0;
    if (ctx && ctx->device)
        caps |= RT_CANVAS3D_BACKEND_CAP_ANISOTROPY;
    if (d3d11_format_supports_texture_sampling(ctx, DXGI_FORMAT_BC7_UNORM))
        caps |= RT_CANVAS3D_BACKEND_CAP_BC7;
    return caps;
}

/// @brief Map a native texture format id to its DXGI format (only BC7 is supported; else UNKNOWN).
static DXGI_FORMAT d3d11_native_texture_format(int32_t format_id) {
    if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7)
        return DXGI_FORMAT_BC7_UNORM;
    return DXGI_FORMAT_UNKNOWN;
}

/// @brief Bytes per block-row of a native compressed mip (block columns × block byte size).
/// @details D3D11 texture updates need the source row pitch; this computes it overflow-safely.
static uint64_t d3d11_native_texture_row_bytes(const vgfx3d_native_texture_mip_t *mip) {
    return vgfx3d_d3d11_native_mip_row_bytes(mip);
}

/// @brief Validate a draw-time native compressed mip snapshot before creating D3D resources.
static int d3d11_validate_native_texture_snapshot(void *asset,
                                                  int64_t mip_start,
                                                  int64_t mip_count,
                                                  vgfx3d_native_texture_mip_t *out_first_mip) {
    vgfx3d_native_texture_mip_t first_mip;
    vgfx3d_native_texture_mip_t previous_mip;

    if (out_first_mip)
        memset(out_first_mip, 0, sizeof(*out_first_mip));
    if (!asset || mip_start < 0 || mip_count <= 0)
        return 0;
    if (!vgfx3d_textureasset_get_native_snapshot_mip(asset, mip_start, mip_count, 0, &first_mip))
        return 0;
    if (d3d11_native_texture_format(first_mip.format_id) == DXGI_FORMAT_UNKNOWN ||
        !vgfx3d_d3d11_is_valid_native_mip_count(first_mip.width, first_mip.height, mip_count) ||
        !vgfx3d_d3d11_validate_native_mip_desc(&first_mip,
                                               NULL,
                                               first_mip.format_id,
                                               first_mip.block_width,
                                               first_mip.block_height,
                                               first_mip.block_bytes) ||
        first_mip.bytes > UINT_MAX) {
        return 0;
    }

    previous_mip = first_mip;
    for (int64_t i = 1; i < mip_count; i++) {
        vgfx3d_native_texture_mip_t mip;
        if (!vgfx3d_textureasset_get_native_snapshot_mip(
                asset, mip_start, mip_count, i, &mip)) {
            return 0;
        }
        if (!vgfx3d_d3d11_validate_native_mip_desc(&mip,
                                                   &previous_mip,
                                                   first_mip.format_id,
                                                   first_mip.block_width,
                                                   first_mip.block_height,
                                                   first_mip.block_bytes) ||
            mip.bytes > UINT_MAX) {
            return 0;
        }
        previous_mip = mip;
    }
    if (out_first_mip)
        *out_first_mip = first_mip;
    return 1;
}

/// @brief Create an RGBA8 2D texture and its shader-resource view at the given size.
/// @return S_OK with @p out_tex / @p out_srv set, or a failure HRESULT.
static HRESULT d3d11_allocate_texture_srv(d3d11_context_t *ctx,
                                          int32_t w,
                                          int32_t h,
                                          ID3D11Texture2D **out_tex,
                                          ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    int32_t mip_count;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !out_tex || !out_srv)
        return E_INVALIDARG;
    if (!vgfx3d_d3d11_is_valid_texture2d_extent(w, h) || w > (int32_t)(UINT_MAX / 4u)) {
        return E_INVALIDARG;
    }

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)w;
    desc.Height = (UINT)h;
    mip_count = vgfx3d_d3d11_compute_mip_count(w, h);
    desc.MipLevels = (UINT)mip_count;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = desc.MipLevels;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(texture)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

/// @brief Create a native compressed (BC7) 2D texture and its shader-resource view for a mip chain.
/// @return S_OK with the texture/SRV set, or a failure HRESULT.
static HRESULT d3d11_allocate_native_texture_srv(d3d11_context_t *ctx,
                                                 const vgfx3d_native_texture_mip_t *first_mip,
                                                 int64_t mip_count,
                                                 ID3D11Texture2D **out_tex,
                                                 ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    DXGI_FORMAT format;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !out_tex || !out_srv || !first_mip || mip_count <= 0 || mip_count > UINT_MAX)
        return E_INVALIDARG;
    format = d3d11_native_texture_format(first_mip->format_id);
    if (format == DXGI_FORMAT_UNKNOWN ||
        !vgfx3d_d3d11_is_valid_native_mip_count(first_mip->width,
                                                first_mip->height,
                                                mip_count) ||
        !vgfx3d_d3d11_validate_native_mip_desc(first_mip,
                                               NULL,
                                               first_mip->format_id,
                                               first_mip->block_width,
                                               first_mip->block_height,
                                               first_mip->block_bytes))
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)first_mip->width;
    desc.Height = (UINT)first_mip->height;
    desc.MipLevels = (UINT)mip_count;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = desc.MipLevels;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(native texture)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

/// @brief Upload more of an in-progress native compressed texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more mips remain.
static int d3d11_continue_native_texture_upload(d3d11_context_t *ctx,
                                                d3d_tex_cache_entry_t *entry) {
    if (!ctx || !entry || !entry->texture_asset || !entry->upload_in_progress || !entry->tex ||
        !entry->srv)
        return 0;
    if (entry->native_mip_count <= 0)
        return 0;
    if (entry->native_next_mip < 0)
        entry->native_next_mip = 0;
    if (entry->native_next_mip > entry->native_mip_count)
        entry->native_next_mip = entry->native_mip_count;
    while (entry->native_next_mip < entry->native_mip_count) {
        vgfx3d_native_texture_mip_t mip;
        uint64_t row_bytes;
        uint64_t block_rows;
        int32_t rows;
        uint64_t offset;
        uint64_t upload_bytes;
        D3D11_BOX box;
        vgfx3d_native_texture_mip_t previous_mip;
        const vgfx3d_native_texture_mip_t *previous_mip_ptr = NULL;
        int32_t expected_block_width;
        int32_t expected_block_height;
        int32_t expected_block_bytes;

        if (ctx->texture_upload_budget_bytes != UINT64_MAX &&
            ctx->texture_upload_bytes >= ctx->texture_upload_budget_bytes)
            return 0;
        if (!vgfx3d_textureasset_get_native_snapshot_mip(entry->texture_asset,
                                                         entry->native_mip_start,
                                                         entry->native_mip_count,
                                                         entry->native_next_mip,
                                                         &mip))
            return 0;
        if (entry->native_next_mip > 0) {
            if (!vgfx3d_textureasset_get_native_snapshot_mip(entry->texture_asset,
                                                             entry->native_mip_start,
                                                             entry->native_mip_count,
                                                             entry->native_next_mip - 1,
                                                             &previous_mip))
                return 0;
            previous_mip_ptr = &previous_mip;
        }
        expected_block_width = previous_mip_ptr ? previous_mip.block_width : mip.block_width;
        expected_block_height = previous_mip_ptr ? previous_mip.block_height : mip.block_height;
        expected_block_bytes = previous_mip_ptr ? previous_mip.block_bytes : mip.block_bytes;
        row_bytes = d3d11_native_texture_row_bytes(&mip);
        block_rows = ((uint64_t)(uint32_t)mip.height + (uint64_t)(uint32_t)mip.block_height - 1u) /
                     (uint64_t)(uint32_t)mip.block_height;
        if (mip.format_id != entry->native_format ||
            d3d11_native_texture_format(mip.format_id) == DXGI_FORMAT_UNKNOWN ||
            !vgfx3d_d3d11_validate_native_mip_desc(&mip,
                                                   previous_mip_ptr,
                                                   entry->native_format,
                                                   expected_block_width,
                                                   expected_block_height,
                                                   expected_block_bytes) ||
            row_bytes == 0 || row_bytes > UINT_MAX || block_rows > UINT_MAX ||
            mip.bytes > UINT_MAX)
            return 0;
        if (entry->native_next_block_row < 0 ||
            (uint64_t)(uint32_t)entry->native_next_block_row >= block_rows)
            entry->native_next_block_row = 0;
        rows = vgfx3d_upload_block_rows_for_budget(mip.width,
                                                   mip.height,
                                                   mip.block_width,
                                                   mip.block_height,
                                                   mip.block_bytes,
                                                   entry->native_next_block_row,
                                                   ctx->texture_upload_budget_bytes,
                                                   ctx->texture_upload_bytes);
        if (rows <= 0)
            return 0;
        offset = (uint64_t)(uint32_t)entry->native_next_block_row * row_bytes;
        upload_bytes = (uint64_t)(uint32_t)rows * row_bytes;
        if (offset > mip.bytes || upload_bytes > mip.bytes - offset || upload_bytes > UINT_MAX)
            return 0;
        memset(&box, 0, sizeof(box));
        box.left = 0;
        box.right = (UINT)mip.width;
        box.top = (UINT)(entry->native_next_block_row * mip.block_height);
        box.bottom = box.top + (UINT)(rows * mip.block_height);
        if (box.top >= (UINT)mip.height)
            return 0;
        if (box.bottom > (UINT)mip.height)
            box.bottom = (UINT)mip.height;
        box.front = 0;
        box.back = 1;
        d3d11_unbind_draw_resources(ctx);
        ID3D11DeviceContext_UpdateSubresource(ctx->ctx,
                                              (ID3D11Resource *)entry->tex,
                                              (UINT)entry->native_next_mip,
                                              &box,
                                              (const uint8_t *)mip.data + offset,
                                              (UINT)row_bytes,
                                              (UINT)upload_bytes);
        d3d11_record_texture_upload_bytes(ctx, upload_bytes);
        entry->native_next_block_row += rows;
        if ((uint64_t)(uint32_t)entry->native_next_block_row >= block_rows) {
            entry->native_next_mip++;
            entry->native_next_block_row = 0;
        }
        entry->last_used_frame = ctx->frame_serial;
    }
    entry->generation = entry->pending_generation;
    entry->pending_generation = 0;
    entry->upload_in_progress = 0;
    return 1;
}

/// @brief Begin uploading a native compressed TextureAsset3D: create the texture and seed the
/// cursor.
static int d3d11_start_native_texture_upload(d3d11_context_t *ctx,
                                             d3d_tex_cache_entry_t *entry,
                                             void *asset,
                                             uint64_t cache_key,
                                             int64_t mip_start,
                                             int64_t mip_count) {
    vgfx3d_native_texture_mip_t first_mip;

    if (!ctx || !entry || !asset ||
        !vgfx3d_textureasset_native_supported(asset, d3d11_get_native_texture_caps(ctx)) ||
        !d3d11_validate_native_texture_snapshot(asset, mip_start, mip_count, &first_mip))
        return 0;
    d3d11_unbind_draw_resources(ctx);
    d3d11_release_texture_cache_entry(entry);
    if (FAILED(d3d11_allocate_native_texture_srv(
            ctx, &first_mip, mip_count, &entry->tex, &entry->srv))) {
        d3d11_release_texture_cache_entry(entry);
        return 0;
    }

    entry->pixels_ptr = NULL;
    entry->texture_asset = asset;
    entry->pending_generation = cache_key;
    entry->generation = 0;
    entry->width = first_mip.width;
    entry->height = first_mip.height;
    entry->upload_next_row = 0;
    entry->native_format = first_mip.format_id;
    entry->native_next_block_row = 0;
    entry->native_next_mip = 0;
    entry->native_mip_start = mip_start;
    entry->native_mip_count = mip_count;
    entry->upload_in_progress = 1;
    entry->last_used_frame = ctx->frame_serial;
    d3d11_continue_native_texture_upload(ctx, entry);
    if (ctx->texture_upload_budget_bytes == UINT64_MAX && entry->upload_in_progress) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_texture_cache_entry(entry);
        return 0;
    }
    return 1;
}

/// @brief Upload more rows of an in-progress RGBA texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more rows remain.
static int d3d11_continue_texture_upload(d3d11_context_t *ctx,
                                         d3d_tex_cache_entry_t *entry,
                                         const void *pixels) {
    int32_t rows;
    int32_t slice_w = 0;
    int32_t slice_rows = 0;
    uint8_t *rgba = NULL;
    D3D11_BOX box;
    uint64_t bytes;
    uint32_t row_pitch;

    if (!ctx || !entry || !pixels || !entry->upload_in_progress || !entry->tex || !entry->srv)
        return 0;
    if (!vgfx3d_d3d11_is_valid_texture2d_extent(entry->width, entry->height))
        return 0;
    if (entry->upload_next_row < 0 || entry->upload_next_row >= entry->height)
        entry->upload_next_row = 0;
    rows = vgfx3d_upload_rows_for_budget(entry->width,
                                         entry->height,
                                         entry->upload_next_row,
                                         ctx->texture_upload_budget_bytes,
                                         ctx->texture_upload_bytes);
    if (rows <= 0)
        return 0;
    if (vgfx3d_unpack_pixels_rgba_rows(
            pixels, entry->upload_next_row, rows, 0, &slice_w, &slice_rows, &rgba) != 0 ||
        !rgba || slice_w != entry->width || slice_rows <= 0) {
        free(rgba);
        return 0;
    }

    memset(&box, 0, sizeof(box));
    box.left = 0;
    box.right = (UINT)slice_w;
    box.top = (UINT)entry->upload_next_row;
    box.bottom = (UINT)(entry->upload_next_row + slice_rows);
    box.front = 0;
    box.back = 1;
    d3d11_unbind_draw_resources(ctx);
    if (!vgfx3d_d3d11_compute_rgba8_upload_pitch(slice_w, &row_pitch)) {
        free(rgba);
        return 0;
    }
    ID3D11DeviceContext_UpdateSubresource(
        ctx->ctx, (ID3D11Resource *)entry->tex, 0, &box, rgba, (UINT)row_pitch, 0);
    bytes = (uint64_t)(uint32_t)slice_w * (uint64_t)(uint32_t)slice_rows * 4u;
    d3d11_record_texture_upload_bytes(ctx, bytes);
    free(rgba);

    entry->upload_next_row += slice_rows;
    entry->last_used_frame = ctx->frame_serial;
    if (entry->upload_next_row >= entry->height) {
        ID3D11DeviceContext_GenerateMips(ctx->ctx, entry->srv);
        entry->generation = entry->pending_generation;
        entry->pending_generation = 0;
        entry->upload_in_progress = 0;
        return 1;
    }
    return 0;
}

/// @brief Begin uploading an RGBA Pixels texture: allocate the texture/SRV and seed the row cursor.
static int d3d11_start_texture_upload(d3d11_context_t *ctx,
                                      d3d_tex_cache_entry_t *entry,
                                      const void *pixels,
                                      uint64_t cache_key) {
    int32_t w = 0;
    int32_t h = 0;

    if (!ctx || !entry || !pixels || !vgfx3d_get_pixels_extent(pixels, &w, &h))
        return 0;
    d3d11_unbind_draw_resources(ctx);
    d3d11_release_texture_cache_entry(entry);
    if (FAILED(d3d11_allocate_texture_srv(ctx, w, h, &entry->tex, &entry->srv))) {
        d3d11_release_texture_cache_entry(entry);
        return 0;
    }

    entry->pixels_ptr = pixels;
    entry->texture_asset = NULL;
    entry->pending_generation = cache_key;
    entry->generation = 0;
    entry->width = w;
    entry->height = h;
    entry->upload_next_row = 0;
    entry->native_format = RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
    entry->native_next_block_row = 0;
    entry->native_next_mip = 0;
    entry->native_mip_count = 0;
    entry->upload_in_progress = 1;
    entry->last_used_frame = ctx->frame_serial;
    d3d11_continue_texture_upload(ctx, entry, pixels);
    if (ctx->texture_upload_budget_bytes == UINT64_MAX && entry->upload_in_progress) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_texture_cache_entry(entry);
        return 0;
    }
    return 1;
}

/// @brief Cache lookup for textures, with auto-create and budgeted row uploads.
///
/// Three-way decision:
///   1. Hit (pixels_ptr + cache-key match) → return cached SRV.
///   2. Pixels match but stale generation → restart upload in that slot.
///   3. Miss → create new, append to cache, and return only when upload completes.
static ID3D11ShaderResourceView *d3d11_get_or_create_srv(d3d11_context_t *ctx,
                                                         const void *pixels,
                                                         d3d_temp_srv_t *out_temp) {
    uint64_t cache_key;

    if (out_temp)
        memset(out_temp, 0, sizeof(*out_temp));
    if (!ctx || !pixels)
        return NULL;
    cache_key = vgfx3d_get_pixels_cache_key(pixels);

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels && ctx->tex_cache[i].generation == cache_key &&
            ctx->tex_cache[i].tex && ctx->tex_cache[i].srv) {
            ctx->tex_cache[i].last_used_frame = ctx->frame_serial;
            return ctx->tex_cache[i].srv;
        }
        if (ctx->tex_cache[i].pixels_ptr == pixels &&
            ctx->tex_cache[i].pending_generation == cache_key &&
            ctx->tex_cache[i].upload_in_progress) {
            return d3d11_continue_texture_upload(ctx, &ctx->tex_cache[i], pixels)
                       ? ctx->tex_cache[i].srv
                       : NULL;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].pixels_ptr == pixels) {
            if (!d3d11_start_texture_upload(ctx, &ctx->tex_cache[i], pixels, cache_key))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (!ctx->tex_cache[i].pixels_ptr && !ctx->tex_cache[i].texture_asset) {
            if (!d3d11_start_texture_upload(ctx, &ctx->tex_cache[i], pixels, cache_key))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }
    if (ctx->tex_cache_count < INT_MAX &&
        d3d11_ensure_tex_cache_capacity(ctx, ctx->tex_cache_count + 1)) {
        d3d_tex_cache_entry_t *entry = &ctx->tex_cache[ctx->tex_cache_count++];
        memset(entry, 0, sizeof(*entry));
        if (!d3d11_start_texture_upload(ctx, entry, pixels, cache_key)) {
            ctx->tex_cache_count--;
            return NULL;
        }
        return entry->upload_in_progress ? NULL : entry->srv;
    }
    (void)out_temp;
    return NULL;
}

/// @brief Get the SRV for a native TextureAsset3D, creating/streaming it on a cache miss.
/// @details Keyed by the asset's native cache key so residency changes invalidate the entry.
/// @return The shader-resource view to bind, or NULL if it cannot be created.
static ID3D11ShaderResourceView *d3d11_get_or_create_native_srv(d3d11_context_t *ctx,
                                                                void *asset,
                                                                uint64_t cache_key,
                                                                int64_t mip_start,
                                                                int64_t mip_count) {
    if (!ctx || !asset ||
        !vgfx3d_textureasset_native_supported(asset, d3d11_get_native_texture_caps(ctx)))
        return NULL;
    if (cache_key == 0)
        cache_key = rt_textureasset3d_get_native_cache_key(asset);
    if (mip_start < 0)
        mip_start = rt_textureasset3d_get_resident_mip_start(asset);
    if (mip_count <= 0)
        mip_count = rt_textureasset3d_get_resident_mip_count(asset);
    if (cache_key == 0)
        return NULL;

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].texture_asset == asset && ctx->tex_cache[i].generation == cache_key &&
            ctx->tex_cache[i].native_mip_start == mip_start &&
            ctx->tex_cache[i].native_mip_count == mip_count &&
            ctx->tex_cache[i].tex && ctx->tex_cache[i].srv) {
            ctx->tex_cache[i].last_used_frame = ctx->frame_serial;
            return ctx->tex_cache[i].srv;
        }
        if (ctx->tex_cache[i].texture_asset == asset &&
            ctx->tex_cache[i].pending_generation == cache_key &&
            ctx->tex_cache[i].native_mip_start == mip_start &&
            ctx->tex_cache[i].native_mip_count == mip_count &&
            ctx->tex_cache[i].upload_in_progress) {
            return d3d11_continue_native_texture_upload(ctx, &ctx->tex_cache[i])
                       ? ctx->tex_cache[i].srv
                       : NULL;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (ctx->tex_cache[i].texture_asset == asset) {
            if (!d3d11_start_native_texture_upload(
                    ctx, &ctx->tex_cache[i], asset, cache_key, mip_start, mip_count))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }

    for (int32_t i = 0; i < ctx->tex_cache_count; i++) {
        if (!ctx->tex_cache[i].pixels_ptr && !ctx->tex_cache[i].texture_asset) {
            if (!d3d11_start_native_texture_upload(
                    ctx, &ctx->tex_cache[i], asset, cache_key, mip_start, mip_count))
                return NULL;
            return ctx->tex_cache[i].upload_in_progress ? NULL : ctx->tex_cache[i].srv;
        }
    }
    if (ctx->tex_cache_count < INT_MAX &&
        d3d11_ensure_tex_cache_capacity(ctx, ctx->tex_cache_count + 1)) {
        d3d_tex_cache_entry_t *entry = &ctx->tex_cache[ctx->tex_cache_count++];
        memset(entry, 0, sizeof(*entry));
        if (!d3d11_start_native_texture_upload(ctx, entry, asset, cache_key, mip_start, mip_count)) {
            ctx->tex_cache_count--;
            return NULL;
        }
        return entry->upload_in_progress ? NULL : entry->srv;
    }
    return NULL;
}

/// @brief Resolve a material's texture to an SRV, preferring native blocks then RGBA Pixels.
/// @return The shader-resource view to bind, or NULL if neither source is uploadable.
static ID3D11ShaderResourceView *d3d11_get_or_create_material_srv(d3d11_context_t *ctx,
                                                                  void *asset,
                                                                  const void *pixels,
                                                                  uint64_t asset_cache_key,
                                                                  int64_t mip_start,
                                                                  int64_t mip_count,
                                                                  d3d_temp_srv_t *out_temp) {
    /* Native-supported assets stay on the native upload path. Falling back to
     * RGBA while native upload is budget-paused leaves abandoned pending bytes. */
    if (asset && vgfx3d_textureasset_native_supported(asset, d3d11_get_native_texture_caps(ctx)))
        return d3d11_get_or_create_native_srv(ctx, asset, asset_cache_key, mip_start, mip_count);
    return d3d11_get_or_create_srv(ctx, pixels, out_temp);
}

/// @brief Create a cubemap texture (6 faces) and its cube shader-resource view at the given size.
/// @return S_OK with the texture/SRV set, or a failure HRESULT.
static HRESULT d3d11_allocate_cubemap_srv(d3d11_context_t *ctx,
                                          int32_t face_size,
                                          ID3D11Texture2D **out_tex,
                                          ID3D11ShaderResourceView **out_srv) {
    D3D11_TEXTURE2D_DESC desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    int32_t mip_count;
    HRESULT hr;

    if (out_tex)
        *out_tex = NULL;
    if (out_srv)
        *out_srv = NULL;
    if (!ctx || !out_tex || !out_srv)
        return E_INVALIDARG;
    if (!vgfx3d_d3d11_is_valid_cubemap_extent(face_size) || face_size > (int32_t)(UINT_MAX / 4u))
        return E_INVALIDARG;

    memset(&desc, 0, sizeof(desc));
    desc.Width = (UINT)face_size;
    desc.Height = (UINT)face_size;
    mip_count = vgfx3d_d3d11_compute_mip_count(face_size, face_size);
    desc.MipLevels = (UINT)mip_count;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    hr = ID3D11Device_CreateTexture2D(ctx->device, &desc, NULL, out_tex);
    if (SUCCEEDED(hr)) {
        memset(&srv_desc, 0, sizeof(srv_desc));
        srv_desc.Format = desc.Format;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv_desc.TextureCube.MipLevels = desc.MipLevels;
        hr = ID3D11Device_CreateShaderResourceView(
            ctx->device, (ID3D11Resource *)*out_tex, &srv_desc, out_srv);
    }
    if (FAILED(hr)) {
        d3d11_log_hresult("CreateTexture2D/ShaderResourceView(cubemap)", hr);
        SAFE_RELEASE(*out_srv);
        SAFE_RELEASE(*out_tex);
    }
    return hr;
}

/// @brief Upload more of an in-progress cubemap (face by face, row band by row band) within budget.
/// @return 1 if all six faces finished this call, 0 if more remains.
static int d3d11_continue_cubemap_upload(d3d11_context_t *ctx,
                                         d3d_cubemap_cache_entry_t *entry,
                                         const rt_cubemap3d *cubemap) {
    int32_t mip_count;

    if (!ctx || !entry || !cubemap || !entry->upload_in_progress || !entry->tex || !entry->srv ||
        entry->face_size <= 0)
        return 0;
    if (entry->upload_face < 0 || entry->upload_face >= 6) {
        entry->upload_face = 0;
        entry->upload_next_row = 0;
    }
    if (entry->upload_next_row < 0)
        entry->upload_next_row = 0;
    if (entry->upload_next_row >= entry->face_size) {
        entry->upload_face++;
        entry->upload_next_row = 0;
    }

    mip_count = vgfx3d_d3d11_compute_mip_count(entry->face_size, entry->face_size);
    while (entry->upload_face < 6) {
        int32_t rows = vgfx3d_upload_rows_for_budget(entry->face_size,
                                                     entry->face_size,
                                                     entry->upload_next_row,
                                                     ctx->texture_upload_budget_bytes,
                                                     ctx->texture_upload_bytes);
        int32_t slice_size = 0;
        int32_t slice_rows = 0;
        uint8_t *rgba = NULL;
        D3D11_BOX box;
        uint64_t bytes;
        uint32_t row_pitch;

        if (rows <= 0)
            return 0;
        if (vgfx3d_unpack_cubemap_rgba_rows(cubemap,
                                            entry->upload_face,
                                            entry->upload_next_row,
                                            rows,
                                            0,
                                            &slice_size,
                                            &slice_rows,
                                            &rgba) != 0 ||
            !rgba || slice_size != entry->face_size || slice_rows <= 0) {
            free(rgba);
            return 0;
        }

        memset(&box, 0, sizeof(box));
        box.left = 0;
        box.right = (UINT)slice_size;
        box.top = (UINT)entry->upload_next_row;
        box.bottom = (UINT)(entry->upload_next_row + slice_rows);
        box.front = 0;
        box.back = 1;
        d3d11_unbind_draw_resources(ctx);
        if (!vgfx3d_d3d11_compute_rgba8_upload_pitch(slice_size, &row_pitch)) {
            free(rgba);
            return 0;
        }
        ID3D11DeviceContext_UpdateSubresource(ctx->ctx,
                                              (ID3D11Resource *)entry->tex,
                                              D3D11CalcSubresource(0,
                                                                   (UINT)entry->upload_face,
                                                                   (UINT)mip_count),
                                              &box,
                                              rgba,
                                              (UINT)row_pitch,
                                              0);
        bytes = (uint64_t)(uint32_t)slice_size * (uint64_t)(uint32_t)slice_rows * 4u;
        d3d11_record_texture_upload_bytes(ctx, bytes);
        free(rgba);

        entry->upload_next_row += slice_rows;
        entry->last_used_frame = ctx->frame_serial;
        if (entry->upload_next_row < entry->face_size)
            return 0;
        entry->upload_face++;
        entry->upload_next_row = 0;
    }

    ID3D11DeviceContext_GenerateMips(ctx->ctx, entry->srv);
    entry->generation = entry->pending_generation;
    entry->pending_generation = 0;
    entry->upload_in_progress = 0;
    return 1;
}

/// @brief Begin uploading a cubemap: create the cube texture/SRV and seed the face/row cursor.
static int d3d11_start_cubemap_upload(d3d11_context_t *ctx,
                                      d3d_cubemap_cache_entry_t *entry,
                                      const rt_cubemap3d *cubemap,
                                      uint64_t generation) {
    int32_t face_size = 0;

    if (!ctx || !entry || !cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;
    d3d11_unbind_draw_resources(ctx);
    d3d11_release_cubemap_cache_entry(entry);
    if (FAILED(d3d11_allocate_cubemap_srv(ctx, face_size, &entry->tex, &entry->srv))) {
        d3d11_release_cubemap_cache_entry(entry);
        return 0;
    }

    entry->cubemap_ptr = cubemap;
    entry->pending_generation = generation;
    entry->generation = 0;
    entry->face_size = face_size;
    entry->upload_face = 0;
    entry->upload_next_row = 0;
    entry->upload_in_progress = 1;
    entry->last_used_frame = ctx->frame_serial;
    d3d11_continue_cubemap_upload(ctx, entry, cubemap);
    if (ctx->texture_upload_budget_bytes == UINT64_MAX && entry->upload_in_progress) {
        d3d11_unbind_draw_resources(ctx);
        d3d11_release_cubemap_cache_entry(entry);
        return 0;
    }
    return 1;
}

/// @brief Cubemap-cache lookup analogous to `d3d11_get_or_create_srv`.
///
/// Same hit/stale/miss flow as the 2D texture cache; returns NULL while a
/// cubemap is still uploading under the current per-frame budget.
static ID3D11ShaderResourceView *d3d11_get_or_create_cubemap_srv(d3d11_context_t *ctx,
                                                                 const rt_cubemap3d *cubemap,
                                                                 d3d_temp_srv_t *out_temp) {
    uint64_t generation;

    if (out_temp)
        memset(out_temp, 0, sizeof(*out_temp));
    if (!ctx || !cubemap)
        return NULL;
    generation = vgfx3d_get_cubemap_generation(cubemap);

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap &&
            ctx->cubemap_cache[i].generation == generation && ctx->cubemap_cache[i].tex &&
            ctx->cubemap_cache[i].srv) {
            ctx->cubemap_cache[i].last_used_frame = ctx->frame_serial;
            return ctx->cubemap_cache[i].srv;
        }
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap &&
            ctx->cubemap_cache[i].pending_generation == generation &&
            ctx->cubemap_cache[i].upload_in_progress) {
            ctx->cubemap_cache[i].last_used_frame = ctx->frame_serial;
            return d3d11_continue_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap)
                       ? ctx->cubemap_cache[i].srv
                       : NULL;
        }
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap_ptr == cubemap) {
            if (!d3d11_start_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap, generation))
                return NULL;
            return ctx->cubemap_cache[i].upload_in_progress ? NULL : ctx->cubemap_cache[i].srv;
        }
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (!ctx->cubemap_cache[i].cubemap_ptr) {
            if (!d3d11_start_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap, generation))
                return NULL;
            return ctx->cubemap_cache[i].upload_in_progress ? NULL : ctx->cubemap_cache[i].srv;
        }
    }
    if (ctx->cubemap_cache_count < INT_MAX &&
        d3d11_ensure_cubemap_cache_capacity(ctx, ctx->cubemap_cache_count + 1)) {
        d3d_cubemap_cache_entry_t *entry = &ctx->cubemap_cache[ctx->cubemap_cache_count++];
        memset(entry, 0, sizeof(*entry));
        if (!d3d11_start_cubemap_upload(ctx, entry, cubemap, generation)) {
            ctx->cubemap_cache_count--;
            return NULL;
        }
        return entry->upload_in_progress ? NULL : entry->srv;
    }

    (void)out_temp;
    return NULL;
}
