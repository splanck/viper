//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_textureasset3d_ktx2.inc  (#included from
// rt_textureasset3d.c; not a standalone translation unit) Purpose: TextureAsset3D KTX2 parser and
// the public TextureAsset3D API
//===----------------------------------------------------------------------===//
/// @brief Parse a KTX2 byte stream into a fully-populated TextureAsset3D.
/// @details Validates the identifier and dimensionality, reads the level index, builds the
///          mip table with bounds checks, then — for uncompressed level data — produces an
///          RGBA8/BC3/BC7/ETC2/ASTC software Pixels fallback and/or retains native compressed
///          payloads.
///          Seeds the cache identity and makes all mips resident. Traps with @p api_name on
///          malformed input.
/// @return New TextureAsset3D handle, or NULL on validation/allocation failure.
static void *textureasset3d_parse_ktx2(const uint8_t *data, size_t size, const char *api_name) {
    uint32_t vk_format;
    uint32_t pixel_width;
    uint32_t pixel_height;
    uint32_t pixel_depth;
    uint32_t layer_count;
    uint32_t face_count;
    uint32_t level_count;
    uint32_t supercompression_scheme;
    uint64_t level_table_bytes;
    textureasset3d_format_info format;
    textureasset3d_mip *mips = NULL;
    int64_t mip_count;
    void **mip_pixels = NULL;
    uint8_t **mip_payloads = NULL;
    int attempted_cpu_decode = 0;
    const char *decode_failure_detail = "unsupported or corrupt compressed blocks";
    rt_textureasset3d *asset;

    if (!api_name)
        api_name = "TextureAsset3D.LoadKTX2";
    if (!data || size < TEXTUREASSET3D_KTX2_HEADER_SIZE ||
        memcmp(data, ktx2_identifier, sizeof(ktx2_identifier)) != 0) {
        rt_trap("TextureAsset3D.LoadKTX2: not a KTX2 texture");
        return NULL;
    }

    vk_format = textureasset3d_read_u32le(data + 12);
    pixel_width = textureasset3d_read_u32le(data + 20);
    pixel_height = textureasset3d_read_u32le(data + 24);
    pixel_depth = textureasset3d_read_u32le(data + 28);
    layer_count = textureasset3d_read_u32le(data + 32);
    face_count = textureasset3d_read_u32le(data + 36);
    level_count = textureasset3d_read_u32le(data + 40);
    supercompression_scheme = textureasset3d_read_u32le(data + 44);

    if (pixel_width == 0 || pixel_height == 0 || pixel_width > INT32_MAX ||
        pixel_height > INT32_MAX) {
        rt_trap("TextureAsset3D.LoadKTX2: invalid dimensions");
        return NULL;
    }
    if (pixel_depth > 1 || layer_count > 1 || face_count != 1) {
        rt_trap("TextureAsset3D.LoadKTX2: unsupported KTX2 dimensionality");
        return NULL;
    }
    if (supercompression_scheme != 0) {
        rt_trap("TextureAsset3D.LoadKTX2: unsupported KTX2 supercompression");
        return NULL;
    }
    if (level_count == 0) {
        rt_trap("TextureAsset3D.LoadKTX2: implicit mip generation is unsupported");
        return NULL;
    }
    mip_count = (int64_t)level_count;
    if (mip_count > (int64_t)TEXTUREASSET3D_MAX_MIP_COUNT) {
        rt_trap("TextureAsset3D.LoadKTX2: unsupported mip count");
        return NULL;
    }
    if ((uint64_t)level_count >
        (UINT64_MAX - TEXTUREASSET3D_KTX2_HEADER_SIZE) / TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE) {
        rt_trap("TextureAsset3D.LoadKTX2: level index overflow");
        return NULL;
    }
    level_table_bytes = TEXTUREASSET3D_KTX2_HEADER_SIZE +
                        (uint64_t)level_count * TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE;
    if (level_table_bytes > (uint64_t)size) {
        rt_trap("TextureAsset3D.LoadKTX2: truncated level index");
        return NULL;
    }

    format = textureasset3d_format_from_vk(vk_format);
    mips = (textureasset3d_mip *)calloc((size_t)mip_count, sizeof(textureasset3d_mip));
    if (!mips) {
        rt_trap("TextureAsset3D.LoadKTX2: mip table allocation failed");
        return NULL;
    }
    for (int64_t i = 0; i < mip_count; i++) {
        mips[i].width = textureasset3d_mip_dimension(pixel_width, (uint32_t)i);
        mips[i].height = textureasset3d_mip_dimension(pixel_height, (uint32_t)i);
        if (level_count > 0) {
            const uint8_t *entry = data + TEXTUREASSET3D_KTX2_HEADER_SIZE +
                                   (uint64_t)i * TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE;
            mips[i].offset = textureasset3d_read_u64le(entry);
            mips[i].length = textureasset3d_read_u64le(entry + 8u);
            mips[i].uncompressed_length = textureasset3d_read_u64le(entry + 16u);
            if (mips[i].length > 0 && (mips[i].offset > (uint64_t)size ||
                                       mips[i].length > (uint64_t)size - mips[i].offset)) {
                free(mips);
                rt_trap("TextureAsset3D.LoadKTX2: truncated mip payload");
                return NULL;
            }
            {
                uint64_t expected_bytes = 0;
                if (!textureasset3d_expected_mip_bytes(
                        &format, mips[i].width, mips[i].height, &expected_bytes) ||
                    mips[i].length != expected_bytes) {
                    free(mips);
                    rt_trap("TextureAsset3D.LoadKTX2: invalid mip payload length");
                    return NULL;
                }
                /* This loader handles only non-supercompressed levels (length == expected
                 * above), so the file's stored uncompressed_length must be 0 (KTX2's
                 * "same as level length" sentinel) or exactly expected_bytes. Validate it
                 * rather than retaining an unchecked file field that a future consumer
                 * might trust. */
                if (mips[i].uncompressed_length != 0 &&
                    mips[i].uncompressed_length != expected_bytes) {
                    free(mips);
                    rt_trap("TextureAsset3D.LoadKTX2: inconsistent uncompressed level length");
                    return NULL;
                }
            }
        }
    }

    if (strcmp(format.name, "rgba8") == 0)
        mip_pixels = textureasset3d_decode_rgba8_mips(data, size, mips, mip_count);
    /* BC3 software reference decode: also produce an RGBA8 Pixels fallback so BC3 textures render
     * on backends that cannot upload BC3 natively (native blocks are still retained below). */
    if (strcmp(format.name, "bc3") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "corrupt BC3 block payload";
        mip_pixels = textureasset3d_decode_bc3_mips(data, size, mips, mip_count);
    }
    /* BC7 software reference decode -> RGBA8 Pixels fallback; native blocks are still retained. */
    if (strcmp(format.name, "bc7") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "unsupported reserved BC7 mode or corrupt block payload";
        mip_pixels = textureasset3d_decode_bc7_mips(data, size, mips, mip_count);
    }
    if (strcmp(format.name, "etc2") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "unsupported ETC2 color mode or corrupt block payload";
        mip_pixels = textureasset3d_decode_compressed_mips(
            data,
            size,
            mips,
            mip_count,
            format.block_width,
            format.block_height,
            format.block_bytes,
            textureasset3d_decode_etc2_block_adapter,
            "TextureAsset3D.LoadKTX2: ETC2 Pixels fallback allocation failed");
    }
    if (strcmp(format.name, "astc") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "unsupported ASTC block type or corrupt block payload";
        mip_pixels = textureasset3d_decode_compressed_mips(
            data,
            size,
            mips,
            mip_count,
            format.block_width,
            format.block_height,
            format.block_bytes,
            textureasset3d_decode_astc_block_adapter,
            "TextureAsset3D.LoadKTX2: ASTC Pixels fallback allocation failed");
    }
    if (format.compressed && attempted_cpu_decode && !mip_pixels) {
        mip_pixels = textureasset3d_make_decode_failure_checker_mips(mip_count);
        if (!mip_pixels) {
            free(mips);
            rt_trap("TextureAsset3D.LoadKTX2: checker fallback allocation failed");
            return NULL;
        }
        rt_asset_error_add_warningf("%s: %s CPU texture decode failed (%s); using 8x8 "
                                    "magenta/black checker fallback",
                                    api_name,
                                    format.name,
                                    decode_failure_detail);
    }
    if (format.compressed) {
        mip_payloads = textureasset3d_copy_native_mip_payloads(data, size, mips, mip_count);
        if (!mip_payloads) {
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            free(mips);
            rt_trap("TextureAsset3D.LoadKTX2: native mip payload allocation failed");
            return NULL;
        }
    }

    asset = (rt_textureasset3d *)rt_obj_new_i64(RT_G3D_TEXTUREASSET3D_CLASS_ID,
                                                (int64_t)sizeof(rt_textureasset3d));
    if (!asset) {
        textureasset3d_release_mip_pixels(mip_pixels, mip_count);
        textureasset3d_release_mip_payloads(mip_payloads, mip_count);
        free(mips);
        rt_trap("TextureAsset3D.LoadKTX2: allocation failed");
        return NULL;
    }
    memset(asset, 0, sizeof(*asset));
    rt_obj_set_finalizer(asset, textureasset3d_finalize);
    asset->mip_pixels = mip_pixels;
    asset->mip_payloads = mip_payloads;
    asset->mips = mips;
    asset->width = (int64_t)pixel_width;
    asset->height = (int64_t)pixel_height;
    asset->mip_count = mip_count;
    asset->mip_capacity = mip_count;
    asset->format = format.name;
    asset->compressed = format.compressed ? 1 : 0;
    asset->block_width = format.block_width;
    asset->block_height = format.block_height;
    asset->block_bytes = format.block_bytes;
    asset->cache_identity = textureasset3d_next_cache_identity();
    asset->native_revision = 1;
    asset->alpha_metadata_known =
        textureasset3d_compute_alpha_metadata(mip_pixels, mip_count, &asset->has_alpha_texels) ? 1
                                                                                               : 0;
    asset->retained_bytes = textureasset3d_compute_retained_bytes(asset);
    textureasset3d_set_resident_mip_range_internal(asset, 0, asset->mip_count, NULL);
    return asset;
}

/// @brief Decode a caller-owned KTX2 byte stream into a TextureAsset3D.
void *rt_textureasset3d_load_ktx2_memory(const uint8_t *data, uint64_t size) {
    if (!data || size == 0 || size > (uint64_t)SIZE_MAX ||
        size > (uint64_t)TEXTUREASSET3D_MAX_FILE_BYTES) {
        rt_trap("TextureAsset3D.LoadKTX2Memory: invalid payload");
        return NULL;
    }
    return textureasset3d_parse_ktx2(data, (size_t)size, "TextureAsset3D.LoadKTX2Memory");
}

/// @brief Load a KTX2 texture from a filesystem path. Traps on bad path or read failure.
void *rt_textureasset3d_load_ktx2(rt_string path) {
    const char *cpath = path ? rt_string_cstr(path) : NULL;
    size_t size = 0;
    uint8_t *data;
    void *asset;

    if (!cpath || !*cpath) {
        rt_trap("TextureAsset3D.LoadKTX2: invalid path");
        return NULL;
    }
    data = textureasset3d_read_file_bytes(cpath, &size);
    if (!data) {
        rt_trap("TextureAsset3D.LoadKTX2: cannot read file");
        return NULL;
    }
    asset = textureasset3d_parse_ktx2(data, size, "TextureAsset3D.LoadKTX2");
    free(data);
    return asset;
}

/// @brief Load a KTX2 texture from a packed asset path (rt_asset registry). Traps if not found.
void *rt_textureasset3d_load_ktx2_asset(rt_string path) {
    size_t size = 0;
    uint8_t *data;
    void *asset;

    if (!path) {
        rt_trap("TextureAsset3D.LoadKTX2Asset: invalid path");
        return NULL;
    }
    data = rt_asset_load_raw(path, &size);
    if (!data) {
        rt_trap("TextureAsset3D.LoadKTX2Asset: asset not found");
        return NULL;
    }
    asset = textureasset3d_parse_ktx2(data, size, "TextureAsset3D.LoadKTX2Asset");
    free(data);
    return asset;
}

/// @brief Return true when the runtime has a CPU fallback path for a KTX2 texture format.
int8_t rt_textureasset3d_cpu_supports_format(const char *format_name) {
    if (!format_name)
        return 0;
    return strcmp(format_name, "rgba8") == 0 || strcmp(format_name, "bc3") == 0 ||
           strcmp(format_name, "bc7") == 0 || strcmp(format_name, "etc2") == 0 ||
           strcmp(format_name, "astc") == 0;
}

/// @brief Return true when the runtime KTX2 parser can produce CPU texture payloads.
int8_t rt_textureasset3d_cpu_supports_ktx2(void) {
    return 1;
}

/// @brief Texture width in pixels (0 if the handle is invalid).
int64_t rt_textureasset3d_get_width(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return (asset && asset->width > 0) ? asset->width : 0;
}

/// @brief Texture height in pixels (0 if the handle is invalid).
int64_t rt_textureasset3d_get_height(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return (asset && asset->height > 0) ? asset->height : 0;
}

/// @brief Number of mip levels (0 if the handle is invalid).
int64_t rt_textureasset3d_get_mip_count(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return textureasset3d_safe_mip_count(asset);
}

/// @brief Runtime format name ("rgba8"/"bc3"/"bc7"/"astc"/"etc2"/"unknown"; "" if invalid).
rt_string rt_textureasset3d_get_format(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return rt_const_cstr((asset && asset->format) ? asset->format : "");
}

/// @brief Whether the texture stores native block-compressed mip payloads.
int8_t rt_textureasset3d_get_compressed(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return (asset && asset->compressed) ? 1 : 0;
}

/// @brief First mip index currently marked resident.
int64_t rt_textureasset3d_get_resident_mip_start(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    int64_t safe_mip_count = textureasset3d_safe_mip_count(asset);
    if (!asset || safe_mip_count <= 0 || asset->resident_mip_start < 0)
        return 0;
    if (asset->resident_mip_start > safe_mip_count)
        return safe_mip_count;
    return asset->resident_mip_start;
}

/// @brief Number of mip levels currently marked resident.
int64_t rt_textureasset3d_get_resident_mip_count(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return textureasset3d_resident_range_valid(asset) ? asset->resident_mip_count : 0;
}

/// @brief Total byte size of the currently-resident mips (saturated to INT64_MAX).
int64_t rt_textureasset3d_get_resident_bytes(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    if (!textureasset3d_resident_range_valid(asset) || asset->resident_bytes < 0)
        return 0;
    return asset->resident_bytes;
}

/// @brief Total byte size of all decoded/native mip payloads retained by the asset.
/// @details This is intentionally different from `ResidentBytes`: the resident
///          range describes the active upload window, while retained bytes reflects
///          process memory held so the upload window can move without reloading.
int64_t rt_textureasset3d_get_retained_bytes(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    if (!asset)
        return 0;
    if (asset->retained_bytes < 0)
        asset->retained_bytes = textureasset3d_compute_retained_bytes(asset);
    return asset->retained_bytes > 0 ? asset->retained_bytes : 0;
}

/// @brief Set the resident mip window (traps on a negative range); see the internal helper.
void rt_textureasset3d_set_resident_mip_range(void *obj, int64_t first_mip, int64_t mip_count) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    textureasset3d_set_resident_mip_range_internal(
        asset, first_mip, mip_count, "TextureAsset3D.SetResidentMipRange: negative mip range");
}

/// @brief Borrow the decoded Pixels object for the first resident mip (NULL if none/invalid).
void *rt_textureasset3d_get_pixels(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    if (!textureasset3d_resident_range_valid(asset) || !asset->mip_pixels)
        return NULL;
    return asset->mip_pixels[asset->resident_mip_start];
}

/// @brief Return whether this asset has exact load-time alpha metadata.
/// @details Exact metadata is available when decoded Pixels fallbacks were produced for all mips.
///          Native-only payloads without decoded fallbacks intentionally report unknown so Canvas3D
///          does not assume alpha solely from an alpha-capable compressed format.
int8_t rt_textureasset3d_alpha_metadata_known(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return (asset && asset->alpha_metadata_known) ? 1 : 0;
}

/// @brief Return whether exact alpha metadata found any non-opaque texel.
/// @details Returns 0 for both "known opaque" and "unknown"; callers that need to distinguish those
///          states should first call `rt_textureasset3d_alpha_metadata_known`.
int8_t rt_textureasset3d_has_alpha_texels(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return (asset && asset->alpha_metadata_known && asset->has_alpha_texels) ? 1 : 0;
}

/// @brief Compute the backend texture-cache key for this asset's resident native blocks.
/// @details Folds the asset's cache identity, native revision, and resident mip range into a
///          single hash (FNV offset basis seed, golden-ratio mix per term). Returns 0 when there
///          is nothing to cache (no native payloads or no resident mips); never returns 0
///          otherwise.
uint64_t rt_textureasset3d_get_native_cache_key(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    uint64_t signature = 1469598103934665603ull;

    if (!textureasset3d_resident_range_has_native_payload(asset))
        return 0;
    signature ^=
        asset->cache_identity + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);
    signature ^=
        asset->native_revision + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);
    signature ^= (uint64_t)asset->resident_mip_start + 0x9e3779b97f4a7c15ull + (signature << 6) +
                 (signature >> 2);
    signature ^= (uint64_t)asset->resident_mip_count + 0x9e3779b97f4a7c15ull + (signature << 6) +
                 (signature >> 2);
    return signature ? signature : 1;
}

/// @brief Map the asset's compressed format to a native format id for the backend (NONE if
///        uncompressed/unknown). Used to pick the GPU upload path for retained native blocks.
int32_t rt_textureasset3d_get_native_format_id(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);

    if (!asset || !asset->compressed || !asset->format)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
    if (strcmp(asset->format, "bc3") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_BC3;
    if (strcmp(asset->format, "bc7") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7;
    if (strcmp(asset->format, "astc") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_ASTC;
    if (strcmp(asset->format, "etc2") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_ETC2;
    return RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
}

/// @brief Borrow a retained native compressed mip's bytes and geometry for backend upload.
/// @details Reports the payload pointer, byte size, pixel dimensions, and block geometry for the
///          given absolute @p mip. Only valid for compressed assets with retained native payloads.
/// @return 1 with the out-params set, 0 if the mip is out of range or has no native payload.
int rt_textureasset3d_get_native_mip_info(void *obj,
                                          int64_t mip,
                                          const uint8_t **out_data,
                                          uint64_t *out_bytes,
                                          int32_t *out_width,
                                          int32_t *out_height,
                                          int32_t *out_block_width,
                                          int32_t *out_block_height,
                                          int32_t *out_block_bytes) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);

    if (out_data)
        *out_data = NULL;
    if (out_bytes)
        *out_bytes = 0;
    if (out_width)
        *out_width = 0;
    if (out_height)
        *out_height = 0;
    if (out_block_width)
        *out_block_width = 0;
    if (out_block_height)
        *out_block_height = 0;
    if (out_block_bytes)
        *out_block_bytes = 0;
    int64_t safe_mip_count = textureasset3d_safe_mip_count(asset);
    if (!asset || !asset->compressed || !asset->mip_payloads || !asset->mips || mip < 0 ||
        mip >= safe_mip_count || !asset->mip_payloads[mip] || asset->mips[mip].length == 0 ||
        asset->mips[mip].width == 0 || asset->mips[mip].height == 0 ||
        asset->mips[mip].width > (uint32_t)INT32_MAX ||
        asset->mips[mip].height > (uint32_t)INT32_MAX || asset->block_width <= 0 ||
        asset->block_height <= 0 || asset->block_bytes <= 0)
        return 0;

    if (out_data)
        *out_data = asset->mip_payloads[mip];
    if (out_bytes)
        *out_bytes = asset->mips[mip].length;
    if (out_width)
        *out_width = (int32_t)asset->mips[mip].width;
    if (out_height)
        *out_height = (int32_t)asset->mips[mip].height;
    if (out_block_width)
        *out_block_width = asset->block_width;
    if (out_block_height)
        *out_block_height = asset->block_height;
    if (out_block_bytes)
        *out_block_bytes = asset->block_bytes;
    return 1;
}
