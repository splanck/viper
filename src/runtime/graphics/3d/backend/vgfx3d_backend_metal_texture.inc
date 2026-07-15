//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_metal_texture.inc
//   (#included from vgfx3d_backend_metal.m; not a standalone translation unit)
// Purpose: Texture/cubemap/morph upload paths and caches, IBL mips, bone/morph binding, and instance storage
//===----------------------------------------------------------------------===//

/// @brief Map a native compressed mip's format/block size to the matching MTLPixelFormat (0 if
/// unsupported).
static MTLPixelFormat metal_native_texture_pixel_format(const vgfx3d_native_texture_mip_t *mip) {
    if (!mip)
        return MTLPixelFormatInvalid;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7)
        return MTLPixelFormatBC7_RGBAUnorm;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC1)
        return MTLPixelFormatBC1_RGBA;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC3)
        return MTLPixelFormatBC3_RGBA;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC4)
        return MTLPixelFormatBC4_RUnorm;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC5)
        return MTLPixelFormatBC5_RGUnorm;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ETC2)
        return MTLPixelFormatEAC_RGBA8;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ASTC) {
        if (mip->block_width == 4 && mip->block_height == 4)
            return MTLPixelFormatASTC_4x4_LDR;
        if (mip->block_width == 5 && mip->block_height == 4)
            return MTLPixelFormatASTC_5x4_LDR;
        if (mip->block_width == 5 && mip->block_height == 5)
            return MTLPixelFormatASTC_5x5_LDR;
        if (mip->block_width == 6 && mip->block_height == 5)
            return MTLPixelFormatASTC_6x5_LDR;
        if (mip->block_width == 6 && mip->block_height == 6)
            return MTLPixelFormatASTC_6x6_LDR;
        if (mip->block_width == 8 && mip->block_height == 5)
            return MTLPixelFormatASTC_8x5_LDR;
        if (mip->block_width == 8 && mip->block_height == 6)
            return MTLPixelFormatASTC_8x6_LDR;
        if (mip->block_width == 8 && mip->block_height == 8)
            return MTLPixelFormatASTC_8x8_LDR;
        if (mip->block_width == 10 && mip->block_height == 5)
            return MTLPixelFormatASTC_10x5_LDR;
        if (mip->block_width == 10 && mip->block_height == 6)
            return MTLPixelFormatASTC_10x6_LDR;
        if (mip->block_width == 10 && mip->block_height == 8)
            return MTLPixelFormatASTC_10x8_LDR;
        if (mip->block_width == 10 && mip->block_height == 10)
            return MTLPixelFormatASTC_10x10_LDR;
        if (mip->block_width == 12 && mip->block_height == 10)
            return MTLPixelFormatASTC_12x10_LDR;
        if (mip->block_width == 12 && mip->block_height == 12)
            return MTLPixelFormatASTC_12x12_LDR;
    }
    return MTLPixelFormatInvalid;
}

/// @brief Bytes per block-row of a native compressed mip (block columns × block byte size,
/// overflow-safe).
static uint64_t metal_native_texture_row_bytes(const vgfx3d_native_texture_mip_t *mip) {
    uint64_t cols;
    if (!mip || mip->width <= 0 || mip->block_width <= 0 || mip->block_bytes <= 0)
        return 0;
    cols = ((uint64_t)(uint32_t)mip->width + (uint64_t)(uint32_t)mip->block_width - 1u) /
           (uint64_t)(uint32_t)mip->block_width;
    if (cols > UINT64_MAX / (uint64_t)(uint32_t)mip->block_bytes)
        return 0;
    return cols * (uint64_t)(uint32_t)mip->block_bytes;
}

/// @brief Bytes still to upload for a cached texture entry (native or RGBA streaming path).
static uint64_t metal_texture_pending_bytes(VGFXMetalTextureCacheEntry *entry) {
    if (!entry)
        return 0;
    if (entry.textureAsset)
        return vgfx3d_textureasset_pending_native_snapshot_bytes(entry.textureAsset,
                                                                 entry.nativeMipStart,
                                                                 entry.nativeMipCount,
                                                                 entry.nativeNextMip,
                                                                 entry.nativeNextBlockRow,
                                                                 entry.uploadInProgress ? 1 : 0);
    return vgfx3d_pending_rgba_upload_bytes(
        entry.width, entry.height, entry.uploadNextRow, entry.uploadInProgress ? 1 : 0);
}

/// @brief Bytes still to upload for a cached cubemap entry across its remaining faces/rows.
static uint64_t metal_cubemap_pending_bytes(VGFXMetalCubemapCacheEntry *entry) {
    if (!entry)
        return 0;
    return vgfx3d_pending_cubemap_rgba_upload_bytes(
        entry.faceSize, entry.uploadFace, entry.uploadNextRow, entry.uploadInProgress ? 1 : 0);
}

/// @brief Total bytes still pending across all in-progress texture/cubemap uploads (saturating).
static uint64_t metal_get_texture_upload_pending_bytes(void *ctx_ptr) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    uint64_t total = 0;
    if (!ctx || !ctx.textureCache)
        return 0;
    for (NSValue *key in ctx.textureCache) {
        VGFXMetalTextureCacheEntry *entry = ctx.textureCache[key];
        uint64_t bytes = metal_texture_pending_bytes(entry);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    for (NSValue *key in ctx.cubemapCache) {
        VGFXMetalCubemapCacheEntry *entry = ctx.cubemapCache[key];
        uint64_t bytes = metal_cubemap_pending_bytes(entry);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    return total;
}

/// @brief Set the per-frame byte budget that paces streaming texture uploads on this context.
static void metal_set_texture_upload_budget(void *ctx_ptr, uint64_t bytes) {
    VGFXMetalContext *ctx = (__bridge VGFXMetalContext *)ctx_ptr;
    if (ctx)
        ctx.textureUploadBudgetBytes = bytes;
}

/// @brief Upload more of an in-progress native compressed texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more mips remain.
static int metal_continue_native_texture_upload(VGFXMetalContext *ctx,
                                                VGFXMetalTextureCacheEntry *entry) {
    if (!ctx || !entry || !entry.textureAsset || !entry.uploadInProgress || !entry.texture)
        return 0;
    while (entry.nativeNextMip < entry.nativeMipCount) {
        vgfx3d_native_texture_mip_t mip;
        uint64_t row_bytes;
        uint64_t block_rows;
        int32_t rows;
        uint64_t offset;
        uint64_t upload_bytes;
        uint64_t y;
        uint64_t h;
        MTLRegion region;

        if (ctx.textureUploadBudgetBytes != UINT64_MAX &&
            ctx.textureUploadBytes >= ctx.textureUploadBudgetBytes)
            return 0;
        if (!vgfx3d_textureasset_get_native_snapshot_mip(entry.textureAsset,
                                                         entry.nativeMipStart,
                                                         entry.nativeMipCount,
                                                         entry.nativeNextMip,
                                                         &mip))
            return 0;
        row_bytes = metal_native_texture_row_bytes(&mip);
        if (row_bytes == 0 || row_bytes > (uint64_t)NSUIntegerMax ||
            mip.bytes > (uint64_t)NSUIntegerMax)
            return 0;
        block_rows = ((uint64_t)(uint32_t)mip.height + (uint64_t)(uint32_t)mip.block_height - 1u) /
                     (uint64_t)(uint32_t)mip.block_height;
        if (block_rows == 0 || block_rows > (uint64_t)INT32_MAX)
            return 0;
        if (entry.nativeNextBlockRow < 0 ||
            (uint64_t)(uint32_t)entry.nativeNextBlockRow >= block_rows)
            entry.nativeNextBlockRow = 0;
        rows = vgfx3d_upload_block_rows_for_budget(mip.width,
                                                   mip.height,
                                                   mip.block_width,
                                                   mip.block_height,
                                                   mip.block_bytes,
                                                   entry.nativeNextBlockRow,
                                                   ctx.textureUploadBudgetBytes,
                                                   ctx.textureUploadBytes);
        if (rows <= 0)
            return 0;
        offset = (uint64_t)(uint32_t)entry.nativeNextBlockRow * row_bytes;
        upload_bytes = (uint64_t)(uint32_t)rows * row_bytes;
        y = (uint64_t)(uint32_t)entry.nativeNextBlockRow * (uint64_t)(uint32_t)mip.block_height;
        h = (uint64_t)(uint32_t)rows * (uint64_t)(uint32_t)mip.block_height;
        if (offset > mip.bytes || upload_bytes > mip.bytes - offset || y >= (uint64_t)mip.height)
            return 0;
        if (h > (uint64_t)mip.height - y)
            h = (uint64_t)mip.height - y;
        if (upload_bytes > (uint64_t)NSUIntegerMax || y > (uint64_t)NSUIntegerMax ||
            h > (uint64_t)NSUIntegerMax)
            return 0;
        region = MTLRegionMake2D(0, (NSUInteger)y, (NSUInteger)mip.width, (NSUInteger)h);
        [entry.texture replaceRegion:region
                         mipmapLevel:(NSUInteger)entry.nativeNextMip
                           withBytes:(const uint8_t *)mip.data + offset
                         bytesPerRow:(NSUInteger)row_bytes];
        metal_record_texture_upload_bytes(ctx, upload_bytes);
        entry.nativeNextBlockRow += rows;
        if ((uint64_t)(uint32_t)entry.nativeNextBlockRow >= block_rows) {
            entry.nativeNextMip++;
            entry.nativeNextBlockRow = 0;
        }
        entry.lastUsedFrame = ctx.frameSerial;
    }
    entry.generation = entry.pendingGeneration;
    entry.pendingGeneration = 0;
    entry.uploadInProgress = 0;
    return 1;
}

/// @brief Begin uploading a native compressed TextureAsset3D: create the texture and seed the
/// cursor.
static int metal_start_native_texture_upload(VGFXMetalContext *ctx,
                                             VGFXMetalTextureCacheEntry *entry,
                                             void *asset,
                                             uint64_t cache_key,
                                             int64_t mip_start,
                                             int64_t mip_count) {
    vgfx3d_native_texture_mip_t first_mip;
    MTLPixelFormat pixel_format;

    if (!ctx || !entry || !asset ||
        !vgfx3d_textureasset_native_supported(
            asset, metal_get_native_texture_caps((__bridge void *)ctx)) ||
        !vgfx3d_textureasset_get_native_snapshot_mip(asset, mip_start, mip_count, 0, &first_mip))
        return 0;
    pixel_format = metal_native_texture_pixel_format(&first_mip);
    if (pixel_format == MTLPixelFormatInvalid)
        return 0;
    if (mip_count <= 0)
        return 0;

    MTLTextureDescriptor *texDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixel_format
                                                           width:(NSUInteger)first_mip.width
                                                          height:(NSUInteger)first_mip.height
                                                       mipmapped:(mip_count > 1)];
    texDesc.mipmapLevelCount = (NSUInteger)mip_count;
    texDesc.usage = MTLTextureUsageShaderRead;
    texDesc.storageMode = MTLStorageModeShared;
    entry.texture = [ctx.device newTextureWithDescriptor:texDesc];
    if (!entry.texture)
        return 0;

    entry.textureAsset = asset;
    entry.nativeFormat = first_mip.format_id;
    entry.nativeNextBlockRow = 0;
    entry.nativeNextMip = 0;
    entry.nativeMipStart = mip_start;
    entry.nativeMipCount = mip_count;
    entry.pendingGeneration = cache_key;
    entry.generation = 0;
    entry.width = first_mip.width;
    entry.height = first_mip.height;
    entry.uploadNextRow = 0;
    entry.uploadInProgress = 1;
    entry.lastUsedFrame = ctx.frameSerial;
    metal_continue_native_texture_upload(ctx, entry);
    return 1;
}

/// @brief Upload more rows of an in-progress RGBA texture (swizzled to BGRA), bounded by the
/// budget.
/// @return 1 if the upload finished this call, 0 if more rows remain.
static int metal_continue_texture_upload(VGFXMetalContext *ctx,
                                         VGFXMetalTextureCacheEntry *entry,
                                         const void *pixels_ptr) {
    int32_t rows;
    int32_t slice_w = 0;
    int32_t slice_rows = 0;
    uint8_t *rgba = NULL;
    uint64_t bytes;

    if (!ctx || !entry || !pixels_ptr || !entry.uploadInProgress || !entry.texture)
        return 0;
    rows = vgfx3d_upload_rows_for_budget(entry.width,
                                         entry.height,
                                         entry.uploadNextRow,
                                         ctx.textureUploadBudgetBytes,
                                         ctx.textureUploadBytes);
    if (rows <= 0)
        return 0;
    if (vgfx3d_unpack_pixels_rgba_rows(
            pixels_ptr, entry.uploadNextRow, rows, 0, &slice_w, &slice_rows, &rgba) != 0 ||
        !rgba || slice_w != entry.width || slice_rows <= 0) {
        free(rgba);
        return 0;
    }
    if (!metal_upload_rgba_to_bgra_texture_rows(
            entry.texture, rgba, slice_w, entry.uploadNextRow, slice_rows)) {
        free(rgba);
        return 0;
    }
    bytes = (uint64_t)(uint32_t)slice_w * (uint64_t)(uint32_t)slice_rows * 4u;
    metal_record_texture_upload_bytes(ctx, bytes);
    free(rgba);

    entry.uploadNextRow += slice_rows;
    entry.lastUsedFrame = ctx.frameSerial;
    if (entry.uploadNextRow >= entry.height) {
        entry.generation = entry.pendingGeneration;
        entry.pendingGeneration = 0;
        entry.uploadInProgress = 0;
        metal_generate_mipmaps(ctx, entry.texture);
        return 1;
    }
    return 0;
}

/// @brief Begin uploading an RGBA Pixels texture: allocate the BGRA texture and seed the row
/// cursor.
static int metal_start_texture_upload(VGFXMetalContext *ctx,
                                      VGFXMetalTextureCacheEntry *entry,
                                      const void *pixels_ptr,
                                      uint64_t cache_key) {
    int32_t tw = 0;
    int32_t th = 0;
    int32_t mip_count;

    if (!ctx || !entry || !pixels_ptr || !vgfx3d_get_pixels_extent(pixels_ptr, &tw, &th))
        return 0;

    mip_count = vgfx3d_metal_compute_mip_count(tw, th);
    if (!entry.texture || (int32_t)entry.texture.width != tw ||
        (int32_t)entry.texture.height != th) {
        MTLTextureDescriptor *texDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:(NSUInteger)tw
                                                              height:(NSUInteger)th
                                                           mipmapped:(mip_count > 1)];
        texDesc.usage = MTLTextureUsageShaderRead;
        texDesc.storageMode = MTLStorageModeShared;
        entry.texture = [ctx.device newTextureWithDescriptor:texDesc];
    }
    if (!entry.texture)
        return 0;

    entry.pendingGeneration = cache_key;
    entry.generation = 0;
    entry.textureAsset = NULL;
    entry.nativeFormat = RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
    entry.nativeNextBlockRow = 0;
    entry.nativeNextMip = 0;
    entry.nativeMipCount = 0;
    entry.width = tw;
    entry.height = th;
    entry.uploadNextRow = 0;
    entry.uploadInProgress = 1;
    entry.lastUsedFrame = ctx.frameSerial;
    metal_continue_texture_upload(ctx, entry, pixels_ptr);
    return 1;
}

/// MTL-03: Retrieve or create a cached MTLTexture from a Pixels object pointer.
/// Cache uses the stable Pixels cache key so allocator-reused addresses do not
/// alias a fresh image to an older GPU upload. Conversion is RGBA→BGRA.
static id<MTLTexture> metal_get_cached_texture(VGFXMetalContext *ctx, const void *pixels_ptr) {
    uint64_t cache_key;
    NSValue *key;
    VGFXMetalTextureCacheEntry *cached;

    if (!ctx || !pixels_ptr)
        return nil;
    key = [NSValue valueWithPointer:pixels_ptr];
    cached = ctx.textureCache[key];
    cache_key = vgfx3d_get_pixels_cache_key(pixels_ptr);
    if (cached && cached.texture && cached.generation == cache_key) {
        cached.lastUsedFrame = ctx.frameSerial;
        return cached.texture;
    }
    if (cached && cached.pendingGeneration == cache_key && cached.uploadInProgress) {
        return metal_continue_texture_upload(ctx, cached, pixels_ptr) ? cached.texture : nil;
    }
    if (!cached)
        cached = [[VGFXMetalTextureCacheEntry alloc] init];
    ctx.textureCache[key] = cached;
    if (!metal_start_texture_upload(ctx, cached, pixels_ptr, cache_key))
        return nil;
    return cached.uploadInProgress ? nil : cached.texture;
}

/// @brief Get the Metal texture for a native TextureAsset3D, creating/streaming it on a cache miss.
/// @details Keyed by the asset's native cache key; returns nil while an upload is still in
/// progress.
static id<MTLTexture> metal_get_cached_native_texture(
    VGFXMetalContext *ctx, void *asset, uint64_t cache_key, int64_t mip_start, int64_t mip_count) {
    NSValue *key;
    VGFXMetalTextureCacheEntry *cached;

    if (!ctx || !asset ||
        !vgfx3d_textureasset_native_supported(asset,
                                              metal_get_native_texture_caps((__bridge void *)ctx)))
        return nil;
    if (cache_key == 0)
        cache_key = rt_textureasset3d_get_native_cache_key(asset);
    if (mip_start < 0)
        mip_start = rt_textureasset3d_get_resident_mip_start(asset);
    if (mip_count <= 0)
        mip_count = rt_textureasset3d_get_resident_mip_count(asset);
    if (cache_key == 0)
        return nil;
    key = [NSValue valueWithPointer:asset];
    cached = ctx.textureCache[key];
    if (cached && cached.texture && cached.textureAsset == asset &&
        cached.generation == cache_key && cached.nativeMipStart == mip_start &&
        cached.nativeMipCount == mip_count) {
        cached.lastUsedFrame = ctx.frameSerial;
        return cached.texture;
    }
    if (cached && cached.textureAsset == asset && cached.pendingGeneration == cache_key &&
        cached.nativeMipStart == mip_start && cached.nativeMipCount == mip_count &&
        cached.uploadInProgress) {
        return metal_continue_native_texture_upload(ctx, cached) ? cached.texture : nil;
    }
    if (!cached)
        cached = [[VGFXMetalTextureCacheEntry alloc] init];
    ctx.textureCache[key] = cached;
    if (!metal_start_native_texture_upload(ctx, cached, asset, cache_key, mip_start, mip_count))
        return nil;
    return cached.uploadInProgress ? nil : cached.texture;
}

/// @brief Resolve a material's texture to an MTLTexture, preferring native blocks then RGBA Pixels.
/// @return The texture to bind, or nil if neither source is uploadable yet.
static id<MTLTexture> metal_get_material_texture(VGFXMetalContext *ctx,
                                                 void *asset,
                                                 const void *pixels_ptr,
                                                 uint64_t asset_cache_key,
                                                 int64_t mip_start,
                                                 int64_t mip_count) {
    /* Native-supported assets stay on the native upload path. Falling back to
     * RGBA while native upload is budget-paused leaves abandoned pending bytes. */
    if (asset && vgfx3d_textureasset_native_supported(
                     asset, metal_get_native_texture_caps((__bridge void *)ctx)))
        return metal_get_cached_native_texture(ctx, asset, asset_cache_key, mip_start, mip_count);
    return pixels_ptr ? metal_get_cached_texture(ctx, pixels_ptr) : nil;
}

/// @brief Upload one full face image (BGRA-swizzled) to a specific cubemap mip level.
static int metal_upload_rgba_to_bgra_cubemap_level(
    id<MTLTexture> tex, const uint8_t *rgba, int32_t w, int32_t h, int32_t face, int32_t level) {
    uint8_t *bgra;
    size_t pixel_count;

    if (!tex || !rgba || w <= 0 || h <= 0 || face < 0 || face >= 6 || level < 0)
        return 0;
    pixel_count = (size_t)w * (size_t)h;
    if (pixel_count > SIZE_MAX / 4u)
        return 0;
    bgra = (uint8_t *)malloc(pixel_count * 4u);
    if (!bgra)
        return 0;
    for (size_t i = 0; i < pixel_count; i++) {
        bgra[i * 4 + 0] = rgba[i * 4 + 2];
        bgra[i * 4 + 1] = rgba[i * 4 + 1];
        bgra[i * 4 + 2] = rgba[i * 4 + 0];
        bgra[i * 4 + 3] = rgba[i * 4 + 3];
    }
    [tex replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)w, (NSUInteger)h)
           mipmapLevel:(NSUInteger)level
                 slice:(NSUInteger)face
             withBytes:bgra
           bytesPerRow:(NSUInteger)(w * 4)
         bytesPerImage:(NSUInteger)(pixel_count * 4u)];
    free(bgra);
    return 1;
}

/// @brief Overlay the cubemap's GGX-prefiltered IBL chain onto the texture's tail mips.
/// @details Mirrors the OpenGL backend: box mips above level_base keep the skybox
///   sharp; levels level_base.. are replaced with the prefiltered chain so
///   roughness-mapped environment lookups integrate correctly. Idempotent per
///   ibl_identity; re-applied after base-face re-uploads.
static void metal_apply_ibl_mips(VGFXMetalContext *ctx,
                                 VGFXMetalCubemapCacheEntry *entry,
                                 const rt_cubemap3d *cubemap) {
    int32_t level_base = 0;
    int32_t size;

    if (!ctx || !entry || !entry.texture || entry.uploadInProgress || !cubemap ||
        !cubemap->ibl_ready || cubemap->ibl_mip_count <= 0)
        return;
    if (entry.appliedIblIdentity == cubemap->ibl_identity)
        return;
    size = entry.faceSize;
    while (size > cubemap->ibl_base_size && size > 1) {
        size >>= 1;
        level_base++;
    }
    if (size != cubemap->ibl_base_size)
        return;
    if ((NSUInteger)(level_base + cubemap->ibl_mip_count) > entry.texture.mipmapLevelCount)
        return;
    for (int32_t m = 0; m < cubemap->ibl_mip_count; m++) {
        for (int f = 0; f < 6; f++) {
            int32_t w = 0;
            int32_t h = 0;
            uint8_t *rgba = NULL;

            if (vgfx3d_unpack_pixels_rgba(cubemap->ibl_mips[m][f], &w, &h, &rgba) != 0 || !rgba) {
                free(rgba);
                return;
            }
            if (!metal_upload_rgba_to_bgra_cubemap_level(
                    entry.texture, rgba, w, h, f, level_base + m)) {
                free(rgba);
                return;
            }
            metal_record_texture_upload_bytes(ctx, (uint64_t)(uint32_t)w * (uint32_t)h * 4u);
            free(rgba);
        }
    }
    entry.appliedIblIdentity = cubemap->ibl_identity;
}

/// @brief Upload more of an in-progress cubemap (face by face, row band by row band) within budget.
/// @return 1 if all six faces finished this call, 0 if more remains.
static int metal_continue_cubemap_upload(VGFXMetalContext *ctx,
                                         VGFXMetalCubemapCacheEntry *entry,
                                         const rt_cubemap3d *cubemap) {
    if (!ctx || !entry || !cubemap || !entry.uploadInProgress || !entry.texture ||
        entry.faceSize <= 0)
        return 0;

    while (entry.uploadFace < 6) {
        int32_t rows = vgfx3d_upload_rows_for_budget(entry.faceSize,
                                                     entry.faceSize,
                                                     entry.uploadNextRow,
                                                     ctx.textureUploadBudgetBytes,
                                                     ctx.textureUploadBytes);
        int32_t slice_size = 0;
        int32_t slice_rows = 0;
        uint8_t *rgba = NULL;
        uint64_t bytes;

        if (rows <= 0)
            return 0;
        if (vgfx3d_unpack_cubemap_rgba_rows(cubemap,
                                            entry.uploadFace,
                                            entry.uploadNextRow,
                                            rows,
                                            0,
                                            &slice_size,
                                            &slice_rows,
                                            &rgba) != 0 ||
            !rgba || slice_size != entry.faceSize || slice_rows <= 0) {
            free(rgba);
            return 0;
        }
        if (!metal_upload_rgba_to_bgra_cubemap_rows(entry.texture,
                                                    rgba,
                                                    slice_size,
                                                    entry.uploadFace,
                                                    entry.uploadNextRow,
                                                    slice_rows)) {
            free(rgba);
            return 0;
        }
        bytes = (uint64_t)(uint32_t)slice_size * (uint64_t)(uint32_t)slice_rows * 4u;
        metal_record_texture_upload_bytes(ctx, bytes);
        free(rgba);

        entry.uploadNextRow += slice_rows;
        entry.lastUsedFrame = ctx.frameSerial;
        if (entry.uploadNextRow < entry.faceSize)
            return 0;
        entry.uploadFace++;
        entry.uploadNextRow = 0;
    }

    entry.generation = entry.pendingGeneration;
    entry.pendingGeneration = 0;
    entry.uploadInProgress = 0;
    metal_generate_mipmaps(ctx, entry.texture);
    metal_apply_ibl_mips(ctx, entry, cubemap);
    return 1;
}

/// @brief Begin uploading a cubemap: create the cube texture and seed the face/row cursor.
static int metal_start_cubemap_upload(VGFXMetalContext *ctx,
                                      VGFXMetalCubemapCacheEntry *entry,
                                      const rt_cubemap3d *cubemap,
                                      uint64_t generation) {
    int32_t face_size = 0;
    int32_t mip_count;

    if (!ctx || !entry || !cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;

    mip_count = vgfx3d_metal_compute_mip_count(face_size, face_size);
    if (!entry.texture || (int32_t)entry.texture.width != face_size) {
        MTLTextureDescriptor *cubeDesc =
            [MTLTextureDescriptor textureCubeDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                  size:(NSUInteger)face_size
                                                             mipmapped:(mip_count > 1)];
        cubeDesc.usage = MTLTextureUsageShaderRead;
        cubeDesc.storageMode = MTLStorageModeShared;
        entry.texture = [ctx.device newTextureWithDescriptor:cubeDesc];
    }
    if (!entry.texture)
        return 0;

    entry.pendingGeneration = generation;
    entry.generation = 0;
    entry.faceSize = face_size;
    entry.uploadFace = 0;
    entry.uploadNextRow = 0;
    entry.uploadInProgress = 1;
    entry.appliedIblIdentity = 0; /* base faces changed: re-overlay after upload */
    entry.lastUsedFrame = ctx.frameSerial;
    metal_continue_cubemap_upload(ctx, entry, cubemap);
    return 1;
}

/// @brief Get the Metal cubemap texture for a Cubemap3D, creating/streaming it on a cache miss.
/// @details Returns nil while the cubemap upload is still in progress.
static id<MTLTexture> metal_get_cached_cubemap(VGFXMetalContext *ctx, const rt_cubemap3d *cubemap) {
    uint64_t generation;
    NSValue *key;
    VGFXMetalCubemapCacheEntry *cached;

    if (!ctx || !cubemap)
        return nil;

    key = [NSValue valueWithPointer:cubemap];
    cached = ctx.cubemapCache[key];
    generation = vgfx3d_get_cubemap_generation(cubemap);
    if (cached && cached.texture && cached.generation == generation) {
        cached.lastUsedFrame = ctx.frameSerial;
        metal_apply_ibl_mips(ctx, cached, cubemap);
        return cached.texture;
    }
    if (cached && cached.pendingGeneration == generation && cached.uploadInProgress) {
        cached.lastUsedFrame = ctx.frameSerial;
        return metal_continue_cubemap_upload(ctx, cached, cubemap) ? cached.texture : nil;
    }
    if (!cached)
        cached = [[VGFXMetalCubemapCacheEntry alloc] init];
    ctx.cubemapCache[key] = cached;
    if (!metal_start_cubemap_upload(ctx, cached, cubemap, generation))
        return nil;
    return cached.uploadInProgress ? nil : cached.texture;
}

typedef struct {
    int32_t shape_count;
    int has_prev_weights;
    int has_normal_deltas;
} metal_morph_bind_status_t;

/// @brief Compute the byte size of a morph-target GPU payload for a vertex count and channel set.
/// @return 1 with the size written, 0 on overflow or invalid inputs.
static int metal_compute_morph_payload_bytes(uint32_t vertex_count,
                                             int32_t morph_count,
                                             size_t *out_bytes) {
    size_t elements;
    size_t bytes;

    if (out_bytes)
        *out_bytes = 0;
    if (!out_bytes || vertex_count == 0 || morph_count <= 0)
        return 0;
    if ((size_t)morph_count > SIZE_MAX / (size_t)vertex_count)
        return 0;
    elements = (size_t)morph_count * (size_t)vertex_count;
    if (elements > SIZE_MAX / 3u)
        return 0;
    elements *= 3u;
    if (elements > SIZE_MAX / sizeof(float))
        return 0;
    bytes = elements * sizeof(float);
    *out_bytes = bytes;
    return 1;
}

/// @brief Get or build the cached GPU morph-delta buffer for a mesh's morph targets.
/// @details Keyed by mesh + morph revision so it rebuilds when the deltas change. NULL on failure.
static VGFXMetalMorphCacheEntry *metal_get_cached_morph_entry(VGFXMetalContext *ctx,
                                                              const vgfx3d_draw_cmd_t *cmd,
                                                              int32_t morph_count) {
    NSValue *key;
    VGFXMetalMorphCacheEntry *entry;
    size_t bytes;

    if (!ctx || !ctx.morphCache || !cmd || morph_count <= 0 || !cmd->morph_key ||
        cmd->morph_revision == 0 || !cmd->morph_deltas || !cmd->morph_weights ||
        cmd->morph_shape_count <= 0 || cmd->vertex_count == 0) {
        return nil;
    }

    key = [NSValue valueWithPointer:cmd->morph_key];
    entry = ctx.morphCache[key];
    if (entry && entry.deltaBuffer &&
        vgfx3d_metal_should_reuse_morph_cache(entry.key,
                                              entry.revision,
                                              entry.shapeCount,
                                              entry.vertexCount,
                                              entry.hasNormalDeltas,
                                              cmd)) {
        entry.lastUsedFrame = ctx.frameSerial;
        return entry;
    }

    if (!metal_compute_morph_payload_bytes(cmd->vertex_count, morph_count, &bytes))
        return nil;
    entry = entry ? entry : [[VGFXMetalMorphCacheEntry alloc] init];
    entry.deltaBuffer = metal_new_shared_buffer(ctx, cmd->morph_deltas, bytes);
    entry.normalBuffer = cmd->morph_normal_deltas
                             ? metal_new_shared_buffer(ctx, cmd->morph_normal_deltas, bytes)
                             : nil;
    if (!entry.deltaBuffer)
        return nil;
    entry.key = cmd->morph_key;
    entry.revision = cmd->morph_revision;
    entry.shapeCount = morph_count;
    entry.vertexCount = cmd->vertex_count;
    entry.hasNormalDeltas = entry.normalBuffer ? 1 : 0;
    entry.lastUsedFrame = ctx.frameSerial;
    ctx.morphCache[key] = entry;
    return entry;
}

/// @brief Bind a draw's skeletal bone matrices (the skinning palette) into the vertex stage.
/// @details Palettes are appended into the transient ring instead of allocating fresh
///          MTLBuffers — with many skinned characters the per-draw device allocations were a
///          measurable frame-time cost. The palette stays identity-padded to the full
///          VGFX3D_METAL_MAX_BONES because the shader clamps bone indices to the palette
///          size, not the draw's bone count (the MSL PerScene/PerObject ABI is contractual).
/// @return 1 on success, 0 if the palette is missing or the upload failed.
static int metal_bind_bone_palettes(VGFXMetalContext *ctx,
                                    const vgfx3d_draw_cmd_t *cmd,
                                    int has_skinning,
                                    int has_prev_skinning,
                                    int *out_prev_ok) {
    float packed_palette[VGFX3D_METAL_MAX_BONES * 16];
    size_t palette_bytes = sizeof(packed_palette);
    id<MTLBuffer> bone_buf;
    NSUInteger bone_offset = 0;

    if (out_prev_ok)
        *out_prev_ok = 0;

    if (!ctx || !cmd || !has_skinning)
        return 0;

    vgfx3d_metal_pack_bone_palette(packed_palette, cmd->bone_palette, cmd->bone_count);
    bone_buf = metal_upload_transient_buffer(
        ctx, VGFX3D_METAL_TRANSIENT_VERTEX, packed_palette, palette_bytes, &bone_offset);
    if (!bone_buf)
        return 0;
    [ctx.encoder setVertexBuffer:bone_buf offset:bone_offset atIndex:3];
    if (has_prev_skinning) {
        id<MTLBuffer> prev_bone_buf;
        NSUInteger prev_offset = 0;
        vgfx3d_metal_pack_bone_palette(packed_palette, cmd->prev_bone_palette, cmd->bone_count);
        prev_bone_buf = metal_upload_transient_buffer(
            ctx, VGFX3D_METAL_TRANSIENT_VERTEX, packed_palette, palette_bytes, &prev_offset);
        if (prev_bone_buf) {
            [ctx.encoder setVertexBuffer:prev_bone_buf offset:prev_offset atIndex:7];
            if (out_prev_ok)
                *out_prev_ok = 1;
        }
    }
    return 1;
}

/// @brief Bind a mesh's influences 5-8 side stream (vertex buffer index 10).
/// @details Import-time data cached by geometry identity + revision (like the
///   morph cache); cache misses upload a shared buffer and keep it alive via
///   frameBuffers until the cache entry takes over. Returns 1 when bound (the
///   caller sets PerObject flags2.y), 0 when the draw has no extras or the
///   upload failed (CPU fallback already handled 8-weight meshes before the
///   backend advertised gpu_skinning_extras, so failure = fewer influences,
///   never corruption).
static int metal_bind_extra_influences(VGFXMetalContext *ctx, const vgfx3d_draw_cmd_t *cmd) {
    id<MTLBuffer> buf = nil;
    const void *key;
    NSNumber *dict_key;
    VGFXMetalExtraInfluenceCacheEntry *entry;
    size_t bytes;
    if (!ctx || !cmd || !cmd->extra_influences || cmd->bone_count <= 0 || cmd->vertex_count == 0)
        return 0;
    if ((size_t)cmd->vertex_count > SIZE_MAX / sizeof(vgfx3d_extra_influences_t))
        return 0;
    bytes = (size_t)cmd->vertex_count * sizeof(vgfx3d_extra_influences_t);
    key = cmd->geometry_key ? cmd->geometry_key : (const void *)cmd->extra_influences;
    dict_key = [NSNumber numberWithUnsignedLongLong:(unsigned long long)(uintptr_t)key];
    entry = ctx.extraInfluenceCache ? ctx.extraInfluenceCache[dict_key] : nil;
    if (entry && entry.key == key && entry.revision == cmd->geometry_revision &&
        entry.vertexCount == cmd->vertex_count) {
        buf = entry.buffer;
        entry.lastUsedFrame = ctx.frameSerial;
    } else {
        buf = metal_new_shared_buffer(ctx, cmd->extra_influences, bytes);
        if (!buf)
            return 0;
        if (ctx.extraInfluenceCache) {
            VGFXMetalExtraInfluenceCacheEntry *fresh =
                [[VGFXMetalExtraInfluenceCacheEntry alloc] init];
            fresh.buffer = buf;
            fresh.key = key;
            fresh.revision = cmd->geometry_revision;
            fresh.vertexCount = cmd->vertex_count;
            fresh.lastUsedFrame = ctx.frameSerial;
            ctx.extraInfluenceCache[dict_key] = fresh;
        } else if (ctx.frameBuffers) {
            [ctx.frameBuffers addObject:buf];
        }
    }
    if (!buf)
        return 0;
    [ctx.encoder setVertexBuffer:buf offset:0 atIndex:10];
    return 1;
}

/// @brief Bind a mesh's morph delta buffer and per-shape weights for a draw.
/// @return A status enum indicating whether morphing is bound, skipped, or failed.
static metal_morph_bind_status_t metal_bind_morph_payload(VGFXMetalContext *ctx,
                                                          const vgfx3d_draw_cmd_t *cmd) {
    metal_morph_bind_status_t status = {0, 0, 0};
    VGFXMetalMorphCacheEntry *cached_entry;
    id<MTLBuffer> delta_buf = nil;
    id<MTLBuffer> normal_buf = nil;
    int32_t morph_count;
    size_t delta_bytes;
    size_t weight_bytes;

    if (!ctx || !cmd || !cmd->morph_deltas || !cmd->morph_weights || cmd->morph_shape_count <= 0)
        return status;
    morph_count = vgfx3d_metal_clamp_morph_shape_count(cmd->vertex_count, cmd->morph_shape_count);
    if (morph_count <= 0)
        return status;

    cached_entry = metal_get_cached_morph_entry(ctx, cmd, morph_count);
    if (cached_entry) {
        delta_buf = cached_entry.deltaBuffer;
        normal_buf = cached_entry.normalBuffer;
    } else {
        if (!metal_compute_morph_payload_bytes(cmd->vertex_count, morph_count, &delta_bytes))
            return status;
        delta_buf = metal_new_shared_buffer(ctx, cmd->morph_deltas, delta_bytes);
        normal_buf = cmd->morph_normal_deltas
                         ? metal_new_shared_buffer(ctx, cmd->morph_normal_deltas, delta_bytes)
                         : nil;
        if (ctx.frameBuffers && delta_buf)
            [ctx.frameBuffers addObject:delta_buf];
        if (ctx.frameBuffers && normal_buf)
            [ctx.frameBuffers addObject:normal_buf];
    }
    if (!delta_buf)
        return status;

    weight_bytes = (size_t)morph_count * sizeof(float);
    [ctx.encoder setVertexBuffer:delta_buf offset:0 atIndex:4];
    if (normal_buf)
        [ctx.encoder setVertexBuffer:normal_buf offset:0 atIndex:9];
    [ctx.encoder setVertexBytes:cmd->morph_weights length:weight_bytes atIndex:5];
    if (cmd->prev_morph_weights)
        [ctx.encoder setVertexBytes:cmd->prev_morph_weights length:weight_bytes atIndex:8];
    status.shape_count = morph_count;
    status.has_prev_weights = cmd->prev_morph_weights ? 1 : 0;
    status.has_normal_deltas = normal_buf ? 1 : 0;
    return status;
}

/// @brief Ensure the CPU-side instance staging scratch can hold @p instance_count instances.
/// @details GPU storage is a per-draw slice of the transient ring (see
///          metal_upload_transient_buffer); only the packing scratch persists here.
static void metal_ensure_instance_storage(VGFXMetalContext *ctx, int32_t instance_count) {
    int32_t needed_capacity;
    int32_t next_capacity;
    NSUInteger byte_count;

    if (!ctx || instance_count <= 0)
        return;

    needed_capacity = instance_count;
    next_capacity = vgfx3d_metal_next_capacity((int32_t)ctx.instanceCapacity, needed_capacity, 64);
    if ((NSUInteger)next_capacity > ctx.instanceCapacity || !ctx.instanceScratch) {
        byte_count = (NSUInteger)next_capacity * sizeof(vgfx3d_metal_instance_data_t);
        ctx.instanceScratch = [NSMutableData dataWithLength:byte_count];
        ctx.instanceCapacity = (NSUInteger)next_capacity;
    }
}

