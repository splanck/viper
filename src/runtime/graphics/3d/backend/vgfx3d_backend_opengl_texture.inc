//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_opengl_texture.inc  (#included from
// vgfx3d_backend_opengl.c; not a standalone translation unit) Purpose: OpenGL texture/cubemap/morph
// cache lifecycle and streamed upload
//===----------------------------------------------------------------------===//
/// @brief Compare two `uint64_t` values for ascending `qsort()` ordering.
static int gl_u64_compare_ascending(const void *lhs, const void *rhs) {
    const uint64_t a = *(const uint64_t *)lhs;
    const uint64_t b = *(const uint64_t *)rhs;
    return (a > b) - (a < b);
}

/// @brief Return reusable context-owned storage for cache age selection.
static uint64_t *gl_cache_age_scratch(gl_context_t *ctx, int32_t count) {
    uint64_t *grown;
    int32_t capacity;
    if (!ctx || count <= 0)
        return NULL;
    if (ctx->cache_age_scratch_capacity >= count)
        return ctx->cache_age_scratch;
    capacity = vgfx3d_opengl_next_capacity(ctx->cache_age_scratch_capacity, count, 64);
    grown = (uint64_t *)realloc(ctx->cache_age_scratch, (size_t)capacity * sizeof(*grown));
    if (!grown)
        return NULL;
    ctx->cache_age_scratch = grown;
    ctx->cache_age_scratch_capacity = capacity;
    return grown;
}

/// @brief Release one texture-cache slot's owned texture objects and zero the entry.
static void texture_cache_release_entry(gl_texture_cache_entry_t *entry) {
    if (!entry)
        return;
    if (entry->tex)
        gl.DeleteTextures(1, &entry->tex);
    if (entry->fallback_tex)
        gl.DeleteTextures(1, &entry->fallback_tex);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Hash an object identity into the direct-mapped texture-cache hint table.
static uint32_t gl_texture_cache_hint_slot(const void *key) {
    uintptr_t value = (uintptr_t)key;
    value ^= value >> 7u;
    value ^= value >> 17u;
    return (uint32_t)value & (GL_TEXTURE_CACHE_HINT_CAPACITY - 1u);
}

/// @brief Remember the current array index for a Pixels or TextureAsset3D identity.
static void gl_texture_cache_remember(gl_context_t *ctx, const void *key, int32_t index) {
    if (!ctx || !key || index < 0 || index >= ctx->texture_cache_count)
        return;
    ctx->texture_cache_hints[gl_texture_cache_hint_slot(key)] = index + 1;
}

/// @brief Return a validated hinted Pixels entry index, or -1 on collision/staleness.
static int32_t gl_texture_cache_hint_pixels(const gl_context_t *ctx, const void *pixels) {
    int32_t index;
    if (!ctx || !pixels)
        return -1;
    index = ctx->texture_cache_hints[gl_texture_cache_hint_slot(pixels)] - 1;
    if (index < 0 || index >= ctx->texture_cache_count ||
        ctx->texture_cache[index].pixels != pixels)
        return -1;
    return index;
}

/// @brief Return a validated hinted native-asset entry index, or -1 on collision/staleness.
static int32_t gl_texture_cache_hint_asset(const gl_context_t *ctx, const void *asset) {
    int32_t index;
    if (!ctx || !asset)
        return -1;
    index = ctx->texture_cache_hints[gl_texture_cache_hint_slot(asset)] - 1;
    if (index < 0 || index >= ctx->texture_cache_count ||
        ctx->texture_cache[index].texture_asset != asset)
        return -1;
    return index;
}

/// @brief Rebuild hints after an in-place LRU compaction moved cache entries.
static void gl_texture_cache_rebuild_hints(gl_context_t *ctx) {
    if (!ctx)
        return;
    memset(ctx->texture_cache_hints, 0, sizeof(ctx->texture_cache_hints));
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        if (ctx->texture_cache[i].pixels)
            gl_texture_cache_remember(ctx, ctx->texture_cache[i].pixels, i);
        if (ctx->texture_cache[i].texture_asset)
            gl_texture_cache_remember(ctx, ctx->texture_cache[i].texture_asset, i);
    }
}

/// @brief Release one cubemap-cache slot's owned texture object and zero the entry.
static void cubemap_cache_release_entry(gl_cubemap_cache_entry_t *entry) {
    if (!entry)
        return;
    if (entry->tex)
        gl.DeleteTextures(1, &entry->tex);
    memset(entry, 0, sizeof(*entry));
}

static void texture_cache_clear(gl_context_t *ctx) {
    if (!ctx)
        return;
    memset(ctx->texture_cache_hints, 0, sizeof(ctx->texture_cache_hints));
    if (!ctx->texture_cache)
        return;
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        texture_cache_release_entry(&ctx->texture_cache[i]);
    }
    ctx->texture_cache_count = 0;
}

/// @brief Delete every cached cubemap and reset the count.
static void cubemap_cache_clear(gl_context_t *ctx) {
    if (!ctx || !ctx->cubemap_cache)
        return;
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        cubemap_cache_release_entry(&ctx->cubemap_cache[i]);
    }
    ctx->cubemap_cache_count = 0;
}

/// @brief Delete every cached morph-target buffer/TBO pair.
///
/// Each entry holds two `(buffer, TBO)` pairs — one for position
/// deltas and one for normal deltas. Both are released.
static void morph_cache_clear(gl_context_t *ctx) {
    if (!ctx || !ctx->morph_cache)
        return;
    for (int32_t i = 0; i < ctx->morph_cache_count; i++) {
        if (ctx->morph_cache[i].morph_tbo)
            gl.DeleteTextures(1, &ctx->morph_cache[i].morph_tbo);
        if (ctx->morph_cache[i].morph_buffer)
            gl.DeleteBuffers(1, &ctx->morph_cache[i].morph_buffer);
        if (ctx->morph_cache[i].morph_normal_tbo)
            gl.DeleteTextures(1, &ctx->morph_cache[i].morph_normal_tbo);
        if (ctx->morph_cache[i].morph_normal_buffer)
            gl.DeleteBuffers(1, &ctx->morph_cache[i].morph_normal_buffer);
    }
    ctx->morph_cache_count = 0;
}

/// @brief Release one morph-cache slot's GL objects and zero the entry.
static void morph_cache_release_entry(gl_morph_cache_entry_t *entry) {
    if (!entry)
        return;
    if (entry->morph_tbo)
        gl.DeleteTextures(1, &entry->morph_tbo);
    if (entry->morph_buffer)
        gl.DeleteBuffers(1, &entry->morph_buffer);
    if (entry->morph_normal_tbo)
        gl.DeleteTextures(1, &entry->morph_normal_tbo);
    if (entry->morph_normal_buffer)
        gl.DeleteBuffers(1, &entry->morph_normal_buffer);
    memset(entry, 0, sizeof(*entry));
}

/// @brief Drop texture cache entries that exceed the LRU limit.
///
/// "Exceed" = total residency above 256 entries AND last-used frame
/// older than 240 frames ago. In-place compaction (write index < read
/// index when entries are dropped). Keeps frequently-used textures
/// regardless of the count cap.
static void texture_cache_prune(gl_context_t *ctx) {
    int32_t write_index = 0;

    if (!ctx || !ctx->texture_cache)
        return;
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        int keep = 1;
        if (ctx->texture_cache_count - write_index > GL_TEXTURE_CACHE_MAX_RESIDENT &&
            vgfx3d_opengl_should_prune_cache_entry(ctx->frame_serial,
                                                   ctx->texture_cache[i].last_used_frame,
                                                   GL_TEXTURE_CACHE_PRUNE_AGE)) {
            keep = 0;
        }
        if (!keep) {
            texture_cache_release_entry(&ctx->texture_cache[i]);
            continue;
        }
        if (write_index != i)
            ctx->texture_cache[write_index] = ctx->texture_cache[i];
        write_index++;
    }
    ctx->texture_cache_count = write_index;
    if (ctx->texture_cache_count > GL_TEXTURE_CACHE_MAX_RESIDENT) {
        const int32_t remove_goal = ctx->texture_cache_count - GL_TEXTURE_CACHE_MAX_RESIDENT;
        uint64_t *ages = gl_cache_age_scratch(ctx, ctx->texture_cache_count);
        if (ages) {
            uint64_t cutoff;
            int32_t remaining_to_remove = remove_goal;
            write_index = 0;
            for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
                ages[i] = ctx->texture_cache[i].last_used_frame;
            }
            qsort(ages, (size_t)ctx->texture_cache_count, sizeof(*ages), gl_u64_compare_ascending);
            cutoff = ages[remove_goal - 1];
            for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
                const uint64_t age = ctx->texture_cache[i].last_used_frame;
                int evict = 0;
                if (age < cutoff) {
                    evict = 1;
                    remaining_to_remove--;
                } else if (age == cutoff && remaining_to_remove > 0) {
                    evict = 1;
                    remaining_to_remove--;
                }
                if (evict) {
                    texture_cache_release_entry(&ctx->texture_cache[i]);
                    continue;
                }
                if (write_index != i)
                    ctx->texture_cache[write_index] = ctx->texture_cache[i];
                write_index++;
            }
            ctx->texture_cache_count = write_index;
        } else {
            while (ctx->texture_cache_count > GL_TEXTURE_CACHE_MAX_RESIDENT) {
                int32_t oldest = 0;
                for (int32_t i = 1; i < ctx->texture_cache_count; i++) {
                    if (ctx->texture_cache[i].last_used_frame <
                        ctx->texture_cache[oldest].last_used_frame)
                        oldest = i;
                }
                texture_cache_release_entry(&ctx->texture_cache[oldest]);
                if (oldest + 1 < ctx->texture_cache_count) {
                    memmove(&ctx->texture_cache[oldest],
                            &ctx->texture_cache[oldest + 1],
                            (size_t)(ctx->texture_cache_count - oldest - 1) *
                                sizeof(ctx->texture_cache[0]));
                }
                ctx->texture_cache_count--;
            }
        }
    }
    gl_texture_cache_rebuild_hints(ctx);
}

/// @brief LRU prune for the cubemap cache (limit 64, age 240 frames).
static void cubemap_cache_prune(gl_context_t *ctx) {
    int32_t write_index = 0;

    if (!ctx || !ctx->cubemap_cache)
        return;
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        int keep = 1;
        if (ctx->cubemap_cache_count - write_index > GL_CUBEMAP_CACHE_MAX_RESIDENT &&
            vgfx3d_opengl_should_prune_cache_entry(ctx->frame_serial,
                                                   ctx->cubemap_cache[i].last_used_frame,
                                                   GL_CUBEMAP_CACHE_PRUNE_AGE)) {
            keep = 0;
        }
        if (!keep) {
            cubemap_cache_release_entry(&ctx->cubemap_cache[i]);
            continue;
        }
        if (write_index != i)
            ctx->cubemap_cache[write_index] = ctx->cubemap_cache[i];
        write_index++;
    }
    ctx->cubemap_cache_count = write_index;
    if (ctx->cubemap_cache_count > GL_CUBEMAP_CACHE_MAX_RESIDENT) {
        const int32_t remove_goal = ctx->cubemap_cache_count - GL_CUBEMAP_CACHE_MAX_RESIDENT;
        uint64_t *ages = gl_cache_age_scratch(ctx, ctx->cubemap_cache_count);
        if (ages) {
            uint64_t cutoff;
            int32_t remaining_to_remove = remove_goal;
            write_index = 0;
            for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
                ages[i] = ctx->cubemap_cache[i].last_used_frame;
            }
            qsort(ages, (size_t)ctx->cubemap_cache_count, sizeof(*ages), gl_u64_compare_ascending);
            cutoff = ages[remove_goal - 1];
            for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
                const uint64_t age = ctx->cubemap_cache[i].last_used_frame;
                int evict = 0;
                if (age < cutoff) {
                    evict = 1;
                    remaining_to_remove--;
                } else if (age == cutoff && remaining_to_remove > 0) {
                    evict = 1;
                    remaining_to_remove--;
                }
                if (evict) {
                    cubemap_cache_release_entry(&ctx->cubemap_cache[i]);
                    continue;
                }
                if (write_index != i)
                    ctx->cubemap_cache[write_index] = ctx->cubemap_cache[i];
                write_index++;
            }
            ctx->cubemap_cache_count = write_index;
        } else {
            while (ctx->cubemap_cache_count > GL_CUBEMAP_CACHE_MAX_RESIDENT) {
                int32_t oldest = 0;
                for (int32_t i = 1; i < ctx->cubemap_cache_count; i++) {
                    if (ctx->cubemap_cache[i].last_used_frame <
                        ctx->cubemap_cache[oldest].last_used_frame)
                        oldest = i;
                }
                cubemap_cache_release_entry(&ctx->cubemap_cache[oldest]);
                if (oldest + 1 < ctx->cubemap_cache_count) {
                    memmove(&ctx->cubemap_cache[oldest],
                            &ctx->cubemap_cache[oldest + 1],
                            (size_t)(ctx->cubemap_cache_count - oldest - 1) *
                                sizeof(ctx->cubemap_cache[0]));
                }
                ctx->cubemap_cache_count--;
            }
        }
    }
}

/// @brief LRU prune for the morph-target cache (limit 64, age 240 frames).
static void morph_cache_prune(gl_context_t *ctx) {
    int32_t write_index = 0;

    if (!ctx || !ctx->morph_cache)
        return;
    for (int32_t i = 0; i < ctx->morph_cache_count; i++) {
        int keep = 1;
        if (ctx->morph_cache_count - write_index > GL_MORPH_CACHE_MAX_RESIDENT &&
            vgfx3d_opengl_should_prune_cache_entry(
                ctx->frame_serial, ctx->morph_cache[i].last_used_frame, GL_MORPH_CACHE_PRUNE_AGE)) {
            keep = 0;
        }
        if (!keep) {
            morph_cache_release_entry(&ctx->morph_cache[i]);
            continue;
        }
        if (write_index != i)
            ctx->morph_cache[write_index] = ctx->morph_cache[i];
        write_index++;
    }
    ctx->morph_cache_count = write_index;
    if (ctx->morph_cache_count > GL_MORPH_CACHE_MAX_RESIDENT) {
        const int32_t remove_goal = ctx->morph_cache_count - GL_MORPH_CACHE_MAX_RESIDENT;
        uint64_t *ages = gl_cache_age_scratch(ctx, ctx->morph_cache_count);
        if (ages) {
            uint64_t cutoff;
            int32_t remaining_to_remove = remove_goal;
            write_index = 0;
            for (int32_t i = 0; i < ctx->morph_cache_count; i++) {
                ages[i] = ctx->morph_cache[i].last_used_frame;
            }
            qsort(ages, (size_t)ctx->morph_cache_count, sizeof(*ages), gl_u64_compare_ascending);
            cutoff = ages[remove_goal - 1];
            for (int32_t i = 0; i < ctx->morph_cache_count; i++) {
                const uint64_t age = ctx->morph_cache[i].last_used_frame;
                int evict = 0;
                if (age < cutoff) {
                    evict = 1;
                    remaining_to_remove--;
                } else if (age == cutoff && remaining_to_remove > 0) {
                    evict = 1;
                    remaining_to_remove--;
                }
                if (evict) {
                    morph_cache_release_entry(&ctx->morph_cache[i]);
                    continue;
                }
                if (write_index != i)
                    ctx->morph_cache[write_index] = ctx->morph_cache[i];
                write_index++;
            }
            ctx->morph_cache_count = write_index;
        } else {
            while (ctx->morph_cache_count > GL_MORPH_CACHE_MAX_RESIDENT) {
                int32_t oldest = 0;
                for (int32_t i = 1; i < ctx->morph_cache_count; i++) {
                    if (ctx->morph_cache[i].last_used_frame <
                        ctx->morph_cache[oldest].last_used_frame)
                        oldest = i;
                }
                morph_cache_release_entry(&ctx->morph_cache[oldest]);
                if (oldest + 1 < ctx->morph_cache_count) {
                    memmove(&ctx->morph_cache[oldest],
                            &ctx->morph_cache[oldest + 1],
                            (size_t)(ctx->morph_cache_count - oldest - 1) *
                                sizeof(ctx->morph_cache[0]));
                }
                ctx->morph_cache_count--;
            }
        }
    }
}

/// @brief Tear down all three caches and free their backing arrays.
///
/// Called during context destruction. Order matters: clear-then-free
/// each cache so we delete the GL objects before freeing the host array.
static void texture_cache_destroy(gl_context_t *ctx) {
    if (!ctx)
        return;
    texture_cache_clear(ctx);
    cubemap_cache_clear(ctx);
    morph_cache_clear(ctx);
    free(ctx->texture_cache);
    ctx->texture_cache = NULL;
    ctx->texture_cache_capacity = 0;
    free(ctx->cubemap_cache);
    ctx->cubemap_cache = NULL;
    ctx->cubemap_cache_capacity = 0;
    free(ctx->morph_cache);
    ctx->morph_cache = NULL;
    ctx->morph_cache_capacity = 0;
    free(ctx->cache_age_scratch);
    ctx->cache_age_scratch = NULL;
    ctx->cache_age_scratch_capacity = 0;
}

/// @brief Add @p bytes to the context's running per-frame texture-upload total (saturating).
static void gl_record_texture_upload_bytes(gl_context_t *ctx, uint64_t bytes) {
    if (!ctx || bytes == 0)
        return;
    if (ctx->texture_upload_bytes > UINT64_MAX - bytes) {
        ctx->texture_upload_bytes = UINT64_MAX;
        return;
    }
    ctx->texture_upload_bytes += bytes;
}

/// @brief Report native texture-related capabilities this GL context can use.
/// @details Extension discovery is captured during context creation, but native block-compressed
///          upload is not advertised until OpenGL has an upload+sample conformance gate. Some Mesa
///          paths accept BC/ASTC/ETC2 uploads without producing valid sampled texels, and
///          BackendSupports must mean the full draw path works. Anisotropic filtering is a sampler
///          state and can be advertised directly from the extension/limit query.
static int64_t gl_get_native_texture_caps(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    int64_t caps = 0;
    if (!ctx)
        return 0;
    if (ctx->anisotropy_supported && ctx->max_texture_anisotropy > 1.0f)
        caps |= RT_CANVAS3D_BACKEND_CAP_ANISOTROPY;
    return caps;
}

/// @brief Map a native compressed mip's format/block size to the matching GL internal format enum.
/// @details BC7→BPTC, ETC2→ETC2_EAC, ASTC→the specific ASTC block-size enum. Returns 0 if
/// unsupported.
static GLenum gl_native_texture_internal_format(const vgfx3d_native_texture_mip_t *mip) {
    if (!mip)
        return 0;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7)
        return GL_COMPRESSED_RGBA_BPTC_UNORM;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC1)
        return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC3)
        return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC4)
        return GL_COMPRESSED_RED_RGTC1;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC5)
        return GL_COMPRESSED_RG_RGTC2;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ETC2)
        return GL_COMPRESSED_RGBA8_ETC2_EAC;
    if (mip->format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ASTC) {
        if (mip->block_width == 4 && mip->block_height == 4)
            return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        if (mip->block_width == 5 && mip->block_height == 4)
            return GL_COMPRESSED_RGBA_ASTC_5x4_KHR;
        if (mip->block_width == 5 && mip->block_height == 5)
            return GL_COMPRESSED_RGBA_ASTC_5x5_KHR;
        if (mip->block_width == 6 && mip->block_height == 5)
            return GL_COMPRESSED_RGBA_ASTC_6x5_KHR;
        if (mip->block_width == 6 && mip->block_height == 6)
            return GL_COMPRESSED_RGBA_ASTC_6x6_KHR;
        if (mip->block_width == 8 && mip->block_height == 5)
            return GL_COMPRESSED_RGBA_ASTC_8x5_KHR;
        if (mip->block_width == 8 && mip->block_height == 6)
            return GL_COMPRESSED_RGBA_ASTC_8x6_KHR;
        if (mip->block_width == 8 && mip->block_height == 8)
            return GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        if (mip->block_width == 10 && mip->block_height == 5)
            return GL_COMPRESSED_RGBA_ASTC_10x5_KHR;
        if (mip->block_width == 10 && mip->block_height == 6)
            return GL_COMPRESSED_RGBA_ASTC_10x6_KHR;
        if (mip->block_width == 10 && mip->block_height == 8)
            return GL_COMPRESSED_RGBA_ASTC_10x8_KHR;
        if (mip->block_width == 10 && mip->block_height == 10)
            return GL_COMPRESSED_RGBA_ASTC_10x10_KHR;
        if (mip->block_width == 12 && mip->block_height == 10)
            return GL_COMPRESSED_RGBA_ASTC_12x10_KHR;
        if (mip->block_width == 12 && mip->block_height == 12)
            return GL_COMPRESSED_RGBA_ASTC_12x12_KHR;
    }
    return 0;
}

/// @brief Bytes per block-row of a native compressed mip (block columns times block bytes).
static uint64_t gl_native_texture_row_bytes(const vgfx3d_native_texture_mip_t *mip) {
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
static uint64_t gl_texture_pending_bytes(const gl_texture_cache_entry_t *entry) {
    if (!entry)
        return 0;
    if (entry->texture_asset)
        return vgfx3d_textureasset_pending_native_snapshot_bytes(entry->texture_asset,
                                                                 entry->native_mip_start,
                                                                 entry->native_mip_count,
                                                                 entry->native_next_mip,
                                                                 entry->native_next_block_row,
                                                                 entry->upload_in_progress);
    return vgfx3d_pending_rgba_upload_bytes(
        entry->width, entry->height, entry->upload_next_row, entry->upload_in_progress);
}

/// @brief Bytes still to upload for a cached cubemap entry across its remaining faces/rows.
static uint64_t gl_cubemap_pending_bytes(const gl_cubemap_cache_entry_t *entry) {
    if (!entry)
        return 0;
    return vgfx3d_pending_cubemap_rgba_upload_bytes(
        entry->face_size, entry->upload_face, entry->upload_next_row, entry->upload_in_progress);
}

/// @brief Total bytes still pending across all in-progress texture uploads on the context
/// (saturating).
static uint64_t gl_get_texture_upload_pending_bytes(void *ctx_ptr) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    uint64_t total = 0;

    if (!ctx)
        return 0;
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        uint64_t bytes = gl_texture_pending_bytes(&ctx->texture_cache[i]);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        uint64_t bytes = gl_cubemap_pending_bytes(&ctx->cubemap_cache[i]);
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    return total;
}

/// @brief Set the per-frame byte budget that paces streaming texture uploads on this context.
static void gl_set_texture_upload_budget(void *ctx_ptr, uint64_t bytes) {
    gl_context_t *ctx = (gl_context_t *)ctx_ptr;
    if (ctx)
        ctx->texture_upload_budget_bytes = bytes;
}

/// @brief Upload more of an in-progress native compressed texture, bounded by the upload budget.
/// @details Uploads one or more block-row bands through glCompressedTexSubImage2D. Whole-mip image
///          storage is defined with a NULL glCompressedTexImage2D payload when a mip begins.
/// @return 1 if the upload finished this call, 0 if more remains (or on a precondition failure).
static int gl_continue_native_texture_upload(gl_context_t *ctx, gl_texture_cache_entry_t *entry) {
    if (!ctx || !entry || !entry->texture_asset || !entry->upload_in_progress || !entry->tex ||
        !gl.CompressedTexImage2D || !gl.CompressedTexSubImage2D)
        return 0;
    if (entry->native_mip_count <= 0)
        return 0;
    if (entry->native_next_mip < 0)
        entry->native_next_mip = 0;
    if (entry->native_next_mip > entry->native_mip_count)
        entry->native_next_mip = entry->native_mip_count;
    gl.BindTexture(GL_TEXTURE_2D, entry->tex);
    while (entry->native_next_mip < entry->native_mip_count) {
        vgfx3d_native_texture_mip_t mip;
        GLenum internal_format;
        uint64_t row_bytes;
        uint64_t block_rows;
        int32_t rows;
        int32_t y;
        int32_t h;
        uint64_t offset;
        uint64_t upload_bytes;
        if (ctx->texture_upload_budget_bytes != UINT64_MAX &&
            ctx->texture_upload_bytes >= ctx->texture_upload_budget_bytes)
            return 0;
        if (!vgfx3d_textureasset_get_native_snapshot_mip(entry->texture_asset,
                                                         entry->native_mip_start,
                                                         entry->native_mip_count,
                                                         entry->native_next_mip,
                                                         &mip))
            return 0;
        internal_format = gl_native_texture_internal_format(&mip);
        row_bytes = gl_native_texture_row_bytes(&mip);
        block_rows = ((uint64_t)(uint32_t)mip.height + (uint64_t)(uint32_t)mip.block_height - 1u) /
                     (uint64_t)(uint32_t)mip.block_height;
        if (!internal_format || row_bytes == 0 || row_bytes > (uint64_t)INT32_MAX ||
            block_rows > (uint64_t)INT32_MAX || mip.bytes > (uint64_t)INT32_MAX)
            return 0;
        if (entry->native_next_block_row < 0 ||
            (uint64_t)(uint32_t)entry->native_next_block_row >= block_rows)
            entry->native_next_block_row = 0;
        if (entry->native_next_block_row == 0) {
            gl.CompressedTexImage2D(GL_TEXTURE_2D,
                                    (GLint)entry->native_next_mip,
                                    internal_format,
                                    (GLsizei)mip.width,
                                    (GLsizei)mip.height,
                                    0,
                                    (GLsizei)mip.bytes,
                                    NULL);
            if (!gl_upload_ok())
                return 0;
        }
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
        y = entry->native_next_block_row * mip.block_height;
        h = rows * mip.block_height;
        if (y < 0 || y >= mip.height)
            return 0;
        if (h > mip.height - y)
            h = mip.height - y;
        offset = (uint64_t)(uint32_t)entry->native_next_block_row * row_bytes;
        upload_bytes = (uint64_t)(uint32_t)rows * row_bytes;
        if (offset > mip.bytes || upload_bytes > mip.bytes - offset ||
            upload_bytes > (uint64_t)INT32_MAX)
            return 0;
        gl.CompressedTexSubImage2D(GL_TEXTURE_2D,
                                   (GLint)entry->native_next_mip,
                                   0,
                                   (GLint)y,
                                   (GLsizei)mip.width,
                                   (GLsizei)h,
                                   internal_format,
                                   (GLsizei)upload_bytes,
                                   (const uint8_t *)mip.data + offset);
        if (!gl_upload_ok())
            return 0;
        gl_record_texture_upload_bytes(ctx, upload_bytes);
        entry->native_next_block_row += rows;
        if ((uint64_t)(uint32_t)entry->native_next_block_row >= block_rows) {
            entry->native_next_mip++;
            entry->native_next_block_row = 0;
        }
        entry->last_used_frame = ctx->frame_serial;
    }
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D,
                     GL_TEXTURE_MIN_FILTER,
                     entry->native_mip_count > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)(entry->native_mip_count - 1));
    if (!gl_upload_ok())
        return 0;
    if (entry->fallback_tex) {
        gl.DeleteTextures(1, &entry->fallback_tex);
        entry->fallback_tex = 0;
    }
    entry->generation = entry->pending_generation;
    entry->pending_generation = 0;
    entry->upload_in_progress = 0;
    return 1;
}

/// @brief Begin uploading a native compressed TextureAsset3D: create the GL texture and seed the
/// cursor.
/// @details Allocates the texture object and mip metadata, then kicks the first budgeted upload
/// chunk.
static int gl_start_native_texture_upload(gl_context_t *ctx,
                                          gl_texture_cache_entry_t *entry,
                                          void *asset,
                                          uint64_t cache_key,
                                          int64_t mip_start,
                                          int64_t mip_count) {
    vgfx3d_native_texture_mip_t first_mip;

    if (!ctx || !entry || !asset ||
        !vgfx3d_textureasset_native_supported(asset, gl_get_native_texture_caps(ctx)) ||
        !vgfx3d_textureasset_get_native_snapshot_mip(asset, mip_start, mip_count, 0, &first_mip) ||
        !gl_native_texture_internal_format(&first_mip))
        return 0;
    if (mip_count <= 0)
        return 0;
    if (ctx->max_texture_size > 0 &&
        (first_mip.width > ctx->max_texture_size || first_mip.height > ctx->max_texture_size))
        return 0;
    if (entry->tex && entry->generation != 0 && entry->generation != cache_key) {
        if (entry->fallback_tex)
            gl.DeleteTextures(1, &entry->fallback_tex);
        entry->fallback_tex = entry->tex;
        entry->tex = 0;
    }
    if (!entry->tex) {
        gl.GenTextures(1, &entry->tex);
        if (!entry->tex)
            return 0;
    }
    entry->pixels = NULL;
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
    gl_continue_native_texture_upload(ctx, entry);
    return 1;
}

/// @brief Upload more rows of an in-progress RGBA texture, bounded by the upload budget.
/// @return 1 if the upload finished this call, 0 if more rows remain.
static int gl_continue_texture_upload(gl_context_t *ctx,
                                      gl_texture_cache_entry_t *entry,
                                      const void *pixels_ptr) {
    int32_t rows;
    int32_t slice_w = 0;
    int32_t slice_rows = 0;
    uint8_t *rgba = NULL;
    uint64_t bytes;
    GLint unpack_alignment;

    if (!ctx || !entry || !pixels_ptr || !entry->upload_in_progress || !entry->tex)
        return 0;
    if (entry->width <= 0 || entry->height <= 0)
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
            pixels_ptr, entry->upload_next_row, rows, 1, &slice_w, &slice_rows, &rgba) != 0 ||
        !rgba || slice_w != entry->width || slice_rows <= 0) {
        free(rgba);
        return 0;
    }

    gl.BindTexture(GL_TEXTURE_2D, entry->tex);
    unpack_alignment = gl_set_pixel_store_alignment(GL_UNPACK_ALIGNMENT, 1);
    gl.TexSubImage2D(GL_TEXTURE_2D,
                     0,
                     0,
                     entry->upload_next_row,
                     slice_w,
                     slice_rows,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     rgba);
    if (!gl_upload_ok()) {
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        free(rgba);
        return 0;
    }
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    bytes = (uint64_t)(uint32_t)slice_w * (uint64_t)(uint32_t)slice_rows * 4u;
    gl_record_texture_upload_bytes(ctx, bytes);
    free(rgba);

    entry->upload_next_row += slice_rows;
    entry->last_used_frame = ctx->frame_serial;
    if (entry->upload_next_row >= entry->height) {
        gl.GenerateMipmap(GL_TEXTURE_2D);
        if (!gl_upload_ok())
            return 0;
        if (entry->fallback_tex) {
            gl.DeleteTextures(1, &entry->fallback_tex);
            entry->fallback_tex = 0;
        }
        entry->generation = entry->pending_generation;
        entry->pending_generation = 0;
        entry->upload_in_progress = 0;
        return 1;
    }
    return 0;
}

/// @brief Begin uploading an RGBA Pixels texture: allocate the GL texture and seed the row cursor.
static int gl_start_texture_upload(gl_context_t *ctx,
                                   gl_texture_cache_entry_t *entry,
                                   const void *pixels_ptr,
                                   uint64_t cache_key) {
    int32_t w = 0;
    int32_t h = 0;

    if (!ctx || !entry || !pixels_ptr || !vgfx3d_get_pixels_extent(pixels_ptr, &w, &h))
        return 0;
    if (ctx->max_texture_size > 0 && (w > ctx->max_texture_size || h > ctx->max_texture_size))
        return 0;
    if (entry->tex && entry->generation != 0 && entry->generation != cache_key) {
        if (entry->fallback_tex)
            gl.DeleteTextures(1, &entry->fallback_tex);
        entry->fallback_tex = entry->tex;
        entry->tex = 0;
    }
    if (!entry->tex) {
        gl.GenTextures(1, &entry->tex);
        if (!entry->tex)
            return 0;
    }

    entry->pixels = pixels_ptr;
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

    gl.BindTexture(GL_TEXTURE_2D, entry->tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!gl_upload_ok())
        return 0;
    gl_continue_texture_upload(ctx, entry, pixels_ptr);
    return 1;
}

/// @brief Texture-cache lookup with auto-create.
///
/// Three-way:
///   1. Hit (pixels + cache-key match) → bump LRU timestamp, return.
///   2. Pixels match but generation stale → re-upload into the existing
///      texture object (saves a `glDeleteTextures`/`glGenTextures`
///      round-trip).
///   3. Miss → create a new texture, append to cache.
/// Returns 0 on failure (caller falls back to no texture).
static GLuint gl_resolve_cached_pixels_texture(gl_context_t *ctx,
                                               int32_t index,
                                               const void *pixels_ptr,
                                               uint64_t cache_key) {
    gl_texture_cache_entry_t *entry;
    if (!ctx || index < 0 || index >= ctx->texture_cache_count)
        return 0;
    entry = &ctx->texture_cache[index];
    if (entry->pixels != pixels_ptr)
        return 0;
    gl_texture_cache_remember(ctx, pixels_ptr, index);
    if (entry->generation == cache_key) {
        entry->last_used_frame = ctx->frame_serial;
        return entry->tex;
    }
    if (entry->pending_generation == cache_key && entry->upload_in_progress) {
        if (gl_continue_texture_upload(ctx, entry, pixels_ptr))
            return entry->tex;
        ctx->stats.texture_fallback_binds++;
        return entry->fallback_tex ? entry->fallback_tex : gl_fallback_white_texture(ctx);
    }
    if (!gl_start_texture_upload(ctx, entry, pixels_ptr, cache_key))
        return 0;
    if (entry->upload_in_progress) {
        ctx->stats.texture_fallback_binds++;
        return entry->fallback_tex ? entry->fallback_tex : gl_fallback_white_texture(ctx);
    }
    return entry->tex;
}

static GLuint gl_get_cached_texture(gl_context_t *ctx, const void *pixels_ptr) {
    uint64_t cache_key;
    int32_t hinted;
    if (!ctx || !pixels_ptr)
        return 0;
    cache_key = vgfx3d_get_pixels_cache_key(pixels_ptr);

    hinted = gl_texture_cache_hint_pixels(ctx, pixels_ptr);
    if (hinted >= 0)
        return gl_resolve_cached_pixels_texture(ctx, hinted, pixels_ptr, cache_key);
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        if (ctx->texture_cache[i].pixels == pixels_ptr)
            return gl_resolve_cached_pixels_texture(ctx, i, pixels_ptr, cache_key);
    }

    if (ctx->texture_cache_count >= ctx->texture_cache_capacity) {
        int32_t new_cap = vgfx3d_opengl_next_capacity(
            ctx->texture_cache_capacity, ctx->texture_cache_count + 1, 16);
        gl_texture_cache_entry_t *nv = (gl_texture_cache_entry_t *)realloc(
            ctx->texture_cache, (size_t)new_cap * sizeof(gl_texture_cache_entry_t));
        if (!nv)
            return 0;
        ctx->texture_cache = nv;
        memset(ctx->texture_cache + ctx->texture_cache_capacity,
               0,
               (size_t)(new_cap - ctx->texture_cache_capacity) * sizeof(*ctx->texture_cache));
        ctx->texture_cache_capacity = new_cap;
    }

    int32_t index = ctx->texture_cache_count++;
    gl_texture_cache_entry_t *entry = &ctx->texture_cache[index];
    memset(entry, 0, sizeof(*entry));
    if (!gl_start_texture_upload(ctx, entry, pixels_ptr, cache_key)) {
        if (entry->tex)
            gl.DeleteTextures(1, &entry->tex);
        memset(entry, 0, sizeof(*entry));
        ctx->texture_cache_count--;
        return 0;
    }
    gl_texture_cache_remember(ctx, pixels_ptr, index);
    if (entry->upload_in_progress) {
        ctx->stats.texture_fallback_binds++;
        return gl_fallback_white_texture(ctx);
    }
    return entry->tex;
}

/// @brief Resolve a native TextureAsset3D from a known cache slot.
static GLuint gl_resolve_cached_native_texture(gl_context_t *ctx,
                                               int32_t index,
                                               void *asset,
                                               uint64_t cache_key,
                                               int64_t mip_start,
                                               int64_t mip_count) {
    gl_texture_cache_entry_t *entry;
    if (!ctx || index < 0 || index >= ctx->texture_cache_count)
        return 0;
    entry = &ctx->texture_cache[index];
    if (entry->texture_asset != asset)
        return 0;
    gl_texture_cache_remember(ctx, asset, index);
    if (entry->generation == cache_key && entry->native_mip_start == mip_start &&
        entry->native_mip_count == mip_count) {
        entry->last_used_frame = ctx->frame_serial;
        return entry->tex;
    }
    if (entry->pending_generation == cache_key && entry->native_mip_start == mip_start &&
        entry->native_mip_count == mip_count && entry->upload_in_progress) {
        if (gl_continue_native_texture_upload(ctx, entry))
            return entry->tex;
        ctx->stats.texture_fallback_binds++;
        return entry->fallback_tex ? entry->fallback_tex : gl_fallback_white_texture(ctx);
    }
    if (!gl_start_native_texture_upload(ctx, entry, asset, cache_key, mip_start, mip_count))
        return 0;
    if (entry->upload_in_progress) {
        ctx->stats.texture_fallback_binds++;
        return entry->fallback_tex ? entry->fallback_tex : gl_fallback_white_texture(ctx);
    }
    return entry->tex;
}

/// @brief Get the GL texture for a native TextureAsset3D, creating/streaming it on a cache miss.
/// @details Keyed by the asset's native cache key so residency changes invalidate the cache entry.
/// @return The GL texture name, or 0 if it cannot be created.
static GLuint gl_get_cached_native_texture(
    gl_context_t *ctx, void *asset, uint64_t cache_key, int64_t mip_start, int64_t mip_count) {
    int32_t hinted;
    if (!ctx || !asset ||
        !vgfx3d_textureasset_native_supported(asset, gl_get_native_texture_caps(ctx)))
        return 0;
    if (cache_key == 0)
        cache_key = rt_textureasset3d_get_native_cache_key(asset);
    if (mip_start < 0)
        mip_start = rt_textureasset3d_get_resident_mip_start(asset);
    if (mip_count <= 0)
        mip_count = rt_textureasset3d_get_resident_mip_count(asset);
    if (cache_key == 0)
        return 0;

    hinted = gl_texture_cache_hint_asset(ctx, asset);
    if (hinted >= 0)
        return gl_resolve_cached_native_texture(
            ctx, hinted, asset, cache_key, mip_start, mip_count);
    for (int32_t i = 0; i < ctx->texture_cache_count; i++) {
        if (ctx->texture_cache[i].texture_asset == asset)
            return gl_resolve_cached_native_texture(ctx, i, asset, cache_key, mip_start, mip_count);
    }

    if (ctx->texture_cache_count >= ctx->texture_cache_capacity) {
        int32_t new_cap = vgfx3d_opengl_next_capacity(
            ctx->texture_cache_capacity, ctx->texture_cache_count + 1, 16);
        gl_texture_cache_entry_t *nv = (gl_texture_cache_entry_t *)realloc(
            ctx->texture_cache, (size_t)new_cap * sizeof(gl_texture_cache_entry_t));
        if (!nv)
            return 0;
        ctx->texture_cache = nv;
        memset(ctx->texture_cache + ctx->texture_cache_capacity,
               0,
               (size_t)(new_cap - ctx->texture_cache_capacity) * sizeof(*ctx->texture_cache));
        ctx->texture_cache_capacity = new_cap;
    }

    int32_t index = ctx->texture_cache_count++;
    gl_texture_cache_entry_t *entry = &ctx->texture_cache[index];
    memset(entry, 0, sizeof(*entry));
    if (!gl_start_native_texture_upload(ctx, entry, asset, cache_key, mip_start, mip_count)) {
        if (entry->tex)
            gl.DeleteTextures(1, &entry->tex);
        memset(entry, 0, sizeof(*entry));
        ctx->texture_cache_count--;
        return 0;
    }
    gl_texture_cache_remember(ctx, asset, index);
    if (entry->upload_in_progress) {
        ctx->stats.texture_fallback_binds++;
        return gl_fallback_white_texture(ctx);
    }
    return entry->tex;
}

/// @brief Resolve a material's texture to a GL texture, preferring native blocks then RGBA Pixels.
/// @return The GL texture name to bind, or 0 if neither source is uploadable.
static GLuint gl_get_material_texture(gl_context_t *ctx,
                                      void *asset,
                                      const void *pixels,
                                      uint64_t asset_cache_key,
                                      int64_t mip_start,
                                      int64_t mip_count) {
    /* Native-supported assets stay on the native upload path. Falling back to
     * RGBA while native upload is budget-paused leaves abandoned pending bytes. */
    if (asset && vgfx3d_textureasset_native_supported(asset, gl_get_native_texture_caps(ctx)))
        return gl_get_cached_native_texture(ctx, asset, asset_cache_key, mip_start, mip_count);
    return pixels ? gl_get_cached_texture(ctx, pixels) : 0;
}

/// @brief Float max-LOD index for a cubemap's mip pyramid (parity with D3D11 version).
/// @details Counts `log2(face_size)` levels down to 1×1. Returned as a float because
///   GL's `TEXTURE_MAX_LOD` / `textureLod` uniforms take floats, and the returned value
///   feeds prefiltered-environment sampling where fractional LODs select a blend between
///   two adjacent mips for glossy reflection blur.
static float gl_cubemap_max_lod(const rt_cubemap3d *cubemap) {
    int32_t size;
    float lod = 0.0f;

    if (!cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &size) || size <= 1)
        return 0.0f;
    while (size > 1) {
        size >>= 1;
        lod += 1.0f;
    }
    return lod;
}

/// @brief Overlay the cubemap's GGX-prefiltered IBL chain onto the texture's tail mips.
/// @details The prefiltered chain starts at `ibl_base_size` (<= face_size), which by
///   construction sits exactly on the mip pyramid: level_base = log2(face_size /
///   ibl_base_size). Box-filtered mips above level_base keep skybox sampling sharp;
///   levels level_base.. are replaced with GGX-prefiltered data so roughness-mapped
///   environment lookups integrate correctly. Idempotent per ibl_identity.
static void gl_apply_ibl_mips(gl_context_t *ctx,
                              gl_cubemap_cache_entry_t *entry,
                              const rt_cubemap3d *cubemap) {
    int32_t level_base = 0;
    int32_t size;

    if (!ctx || !entry || !entry->tex || entry->upload_in_progress || !cubemap ||
        !cubemap->ibl_ready || cubemap->ibl_mip_count <= 0)
        return;
    if (entry->applied_ibl_identity == cubemap->ibl_identity)
        return;
    size = entry->face_size;
    while (size > cubemap->ibl_base_size && size > 1) {
        size >>= 1;
        level_base++;
    }
    if (size != cubemap->ibl_base_size)
        return; /* unexpected geometry: leave box mips in place */

    GLint unpack_alignment;
    gl.BindTexture(GL_TEXTURE_CUBE_MAP, entry->tex);
    unpack_alignment = gl_set_pixel_store_alignment(GL_UNPACK_ALIGNMENT, 1);
    for (int32_t m = 0; m < cubemap->ibl_mip_count; m++) {
        for (int f = 0; f < 6; f++) {
            int32_t w = 0;
            int32_t h = 0;
            uint8_t *rgba = NULL;

            if (vgfx3d_unpack_pixels_rgba(cubemap->ibl_mips[m][f], &w, &h, &rgba) != 0 || !rgba) {
                free(rgba);
                gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
                return;
            }
            gl.TexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)f,
                             level_base + m,
                             0,
                             0,
                             w,
                             h,
                             GL_RGBA,
                             GL_UNSIGNED_BYTE,
                             rgba);
            gl_record_texture_upload_bytes(ctx, (uint64_t)(uint32_t)w * (uint32_t)h * 4u);
            free(rgba);
            if (!gl_upload_ok()) {
                gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
                return;
            }
        }
    }
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
    entry->applied_ibl_identity = cubemap->ibl_identity;
}

/// @brief Upload more of an in-progress cubemap (face by face, row band by row band) within budget.
/// @return 1 if all six faces finished this call, 0 if more remains.
static int gl_continue_cubemap_upload(gl_context_t *ctx,
                                      gl_cubemap_cache_entry_t *entry,
                                      const rt_cubemap3d *cubemap) {
    if (!ctx || !entry || !cubemap || !entry->upload_in_progress || !entry->tex ||
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

    while (entry->upload_face < 6) {
        int32_t rows = vgfx3d_upload_rows_for_budget(entry->face_size,
                                                     entry->face_size,
                                                     entry->upload_next_row,
                                                     ctx->texture_upload_budget_bytes,
                                                     ctx->texture_upload_bytes);
        int32_t slice_size = 0;
        int32_t slice_rows = 0;
        uint8_t *rgba = NULL;
        uint64_t bytes;

        if (rows <= 0)
            return 0;
        if (vgfx3d_unpack_cubemap_rgba_rows(cubemap,
                                            entry->upload_face,
                                            entry->upload_next_row,
                                            rows,
                                            1,
                                            &slice_size,
                                            &slice_rows,
                                            &rgba) != 0 ||
            !rgba || slice_size != entry->face_size || slice_rows <= 0) {
            free(rgba);
            return 0;
        }

        GLint unpack_alignment;
        gl.BindTexture(GL_TEXTURE_CUBE_MAP, entry->tex);
        unpack_alignment = gl_set_pixel_store_alignment(GL_UNPACK_ALIGNMENT, 1);
        gl.TexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)entry->upload_face,
                         0,
                         0,
                         entry->upload_next_row,
                         slice_size,
                         slice_rows,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         rgba);
        if (!gl_upload_ok()) {
            gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
            free(rgba);
            return 0;
        }
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, unpack_alignment);
        bytes = (uint64_t)(uint32_t)slice_size * (uint64_t)(uint32_t)slice_rows * 4u;
        gl_record_texture_upload_bytes(ctx, bytes);
        free(rgba);

        entry->upload_next_row += slice_rows;
        entry->last_used_frame = ctx->frame_serial;
        if (entry->upload_next_row < entry->face_size)
            return 0;
        entry->upload_face++;
        entry->upload_next_row = 0;
    }

    gl.GenerateMipmap(GL_TEXTURE_CUBE_MAP);
    if (!gl_upload_ok())
        return 0;
    entry->generation = entry->pending_generation;
    entry->pending_generation = 0;
    entry->upload_in_progress = 0;
    gl_apply_ibl_mips(ctx, entry, cubemap);
    return 1;
}

/// @brief Begin uploading a cubemap: create the GL cubemap texture and seed the face/row cursor.
static int gl_start_cubemap_upload(gl_context_t *ctx,
                                   gl_cubemap_cache_entry_t *entry,
                                   const rt_cubemap3d *cubemap,
                                   uint64_t generation) {
    int32_t face_size = 0;

    if (!ctx || !entry || !cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;
    if (!entry->tex) {
        gl.GenTextures(1, &entry->tex);
        if (!entry->tex)
            return 0;
    }

    entry->cubemap = cubemap;
    entry->pending_generation = generation;
    entry->generation = 0;
    entry->face_size = face_size;
    entry->upload_face = 0;
    entry->upload_next_row = 0;
    entry->upload_in_progress = 1;
    entry->applied_ibl_identity = 0; /* base faces changed: re-overlay after upload */
    entry->last_used_frame = ctx->frame_serial;

    gl.BindTexture(GL_TEXTURE_CUBE_MAP, entry->tex);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl.TexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (int face = 0; face < 6; face++) {
        gl.TexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face,
                      0,
                      GL_RGBA8,
                      face_size,
                      face_size,
                      0,
                      GL_RGBA,
                      GL_UNSIGNED_BYTE,
                      NULL);
    }
    if (!gl_upload_ok())
        return 0;
    gl_continue_cubemap_upload(ctx, entry, cubemap);
    return 1;
}

/// @brief Cubemap-cache lookup with budgeted face-row upload.
static GLuint gl_get_cached_cubemap(gl_context_t *ctx, const rt_cubemap3d *cubemap) {
    uint64_t generation;
    if (!ctx || !cubemap)
        return 0;
    generation = vgfx3d_get_cubemap_generation(cubemap);

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        gl_cubemap_cache_entry_t *entry = &ctx->cubemap_cache[i];
        if (entry->cubemap == cubemap && entry->generation == generation && entry->tex) {
            entry->last_used_frame = ctx->frame_serial;
            gl_apply_ibl_mips(ctx, entry, cubemap);
            return entry->tex;
        }
        if (entry->cubemap == cubemap && entry->pending_generation == generation &&
            entry->upload_in_progress) {
            entry->last_used_frame = ctx->frame_serial;
            if (gl_continue_cubemap_upload(ctx, entry, cubemap) && !entry->upload_in_progress)
                return entry->tex;
            ctx->stats.texture_fallback_binds++;
            return gl_fallback_white_cubemap(ctx);
        }
    }

    for (int32_t i = 0; i < ctx->cubemap_cache_count; i++) {
        if (ctx->cubemap_cache[i].cubemap == cubemap) {
            if (!gl_start_cubemap_upload(ctx, &ctx->cubemap_cache[i], cubemap, generation))
                return 0;
            if (!ctx->cubemap_cache[i].upload_in_progress)
                return ctx->cubemap_cache[i].tex;
            ctx->stats.texture_fallback_binds++;
            return gl_fallback_white_cubemap(ctx);
        }
    }

    if (ctx->cubemap_cache_count >= ctx->cubemap_cache_capacity) {
        int32_t new_cap = vgfx3d_opengl_next_capacity(
            ctx->cubemap_cache_capacity, ctx->cubemap_cache_count + 1, 8);
        gl_cubemap_cache_entry_t *nv = (gl_cubemap_cache_entry_t *)realloc(
            ctx->cubemap_cache, (size_t)new_cap * sizeof(gl_cubemap_cache_entry_t));
        if (!nv)
            return 0;
        ctx->cubemap_cache = nv;
        memset(ctx->cubemap_cache + ctx->cubemap_cache_capacity,
               0,
               (size_t)(new_cap - ctx->cubemap_cache_capacity) * sizeof(*ctx->cubemap_cache));
        ctx->cubemap_cache_capacity = new_cap;
    }

    gl_cubemap_cache_entry_t *entry = &ctx->cubemap_cache[ctx->cubemap_cache_count++];
    memset(entry, 0, sizeof(*entry));
    if (!gl_start_cubemap_upload(ctx, entry, cubemap, generation)) {
        if (entry->tex)
            gl.DeleteTextures(1, &entry->tex);
        memset(entry, 0, sizeof(*entry));
        ctx->cubemap_cache_count--;
        return 0;
    }
    if (!entry->upload_in_progress)
        return entry->tex;
    ctx->stats.texture_fallback_binds++;
    return gl_fallback_white_cubemap(ctx);
}

/// @brief Fairly advance pending uploads even when their materials are not drawn first.
///
/// Each frame starts at the cursor following the last serviced entry. This prevents a stable
/// draw order from letting early textures consume the whole byte budget indefinitely. One chunk
/// per entry is attempted before wrapping; normal material lookup may spend any budget left over.
static void gl_continue_pending_uploads(gl_context_t *ctx) {
    int32_t count;
    if (!ctx)
        return;
    count = ctx->texture_cache_count;
    for (int32_t visited = 0; visited < count; visited++) {
        int32_t index = (ctx->texture_upload_cursor + visited) % count;
        gl_texture_cache_entry_t *entry = &ctx->texture_cache[index];
        if (ctx->texture_upload_budget_bytes != UINT64_MAX &&
            ctx->texture_upload_bytes >= ctx->texture_upload_budget_bytes)
            break;
        if (!entry->upload_in_progress)
            continue;
        if (entry->texture_asset)
            gl_continue_native_texture_upload(ctx, entry);
        else if (entry->pixels)
            gl_continue_texture_upload(ctx, entry, entry->pixels);
        ctx->texture_upload_cursor = (index + 1) % (count > 0 ? count : 1);
    }
    count = ctx->cubemap_cache_count;
    for (int32_t visited = 0; visited < count; visited++) {
        int32_t index = (ctx->cubemap_upload_cursor + visited) % count;
        gl_cubemap_cache_entry_t *entry = &ctx->cubemap_cache[index];
        if (ctx->texture_upload_budget_bytes != UINT64_MAX &&
            ctx->texture_upload_bytes >= ctx->texture_upload_budget_bytes)
            break;
        if (!entry->upload_in_progress || !entry->cubemap)
            continue;
        gl_continue_cubemap_upload(ctx, entry, (const rt_cubemap3d *)entry->cubemap);
        ctx->cubemap_upload_cursor = (index + 1) % (count > 0 ? count : 1);
    }
}
