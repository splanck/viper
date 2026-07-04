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
///          Seeds the cache identity and makes all mips resident. Malformed or unsupported
///          input reports through rt_asset_error (recoverable: callers receive NULL and can
///          query Assets3D.GetLastLoadError); only allocation failures trap.
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
        rt_asset_error_setf(RT_ASSET_ERROR_BAD_MAGIC, "%s: not a KTX2 texture", api_name);
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
        rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT, "%s: invalid dimensions", api_name);
        return NULL;
    }
    if (pixel_depth > 1 || layer_count > 1 || face_count != 1) {
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNSUPPORTED, "%s: unsupported KTX2 dimensionality", api_name);
        return NULL;
    }
    /* Scheme 0 = none, 2 = Zstandard, 3 = ZLIB (both decompressed per level
     * below). Scheme 1 (BasisLZ/ETC1S) needs the supercompression global data
     * segment and a transcoder, which this loader does not implement yet. */
    if (supercompression_scheme != 0 && supercompression_scheme != 2 &&
        supercompression_scheme != 3) {
        rt_asset_error_setf(RT_ASSET_ERROR_UNSUPPORTED,
                            "%s: unsupported KTX2 supercompression scheme %u",
                            api_name,
                            (unsigned)supercompression_scheme);
        return NULL;
    }
    if (level_count == 0) {
        rt_asset_error_setf(
            RT_ASSET_ERROR_UNSUPPORTED, "%s: implicit mip generation is unsupported", api_name);
        return NULL;
    }
    mip_count = (int64_t)level_count;
    if (mip_count > (int64_t)TEXTUREASSET3D_MAX_MIP_COUNT) {
        rt_asset_error_setf(RT_ASSET_ERROR_UNSUPPORTED, "%s: unsupported mip count", api_name);
        return NULL;
    }
    if ((uint64_t)level_count >
        (UINT64_MAX - TEXTUREASSET3D_KTX2_HEADER_SIZE) / TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE) {
        rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT, "%s: level index overflow", api_name);
        return NULL;
    }
    level_table_bytes = TEXTUREASSET3D_KTX2_HEADER_SIZE +
                        (uint64_t)level_count * TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE;
    if (level_table_bytes > (uint64_t)size) {
        rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT, "%s: truncated level index", api_name);
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
                rt_asset_error_setf(
                    RT_ASSET_ERROR_CORRUPT, "%s: truncated mip payload", api_name);
                return NULL;
            }
            {
                uint64_t expected_bytes = 0;
                if (!textureasset3d_expected_mip_bytes(
                        &format, mips[i].width, mips[i].height, &expected_bytes)) {
                    free(mips);
                    rt_asset_error_setf(
                        RT_ASSET_ERROR_CORRUPT, "%s: invalid mip payload length", api_name);
                    return NULL;
                }
                if (supercompression_scheme == 0) {
                    /* Uncompressed levels: stored length must match exactly, and
                     * the file's uncompressed_length must be 0 (KTX2's "same as
                     * level length" sentinel) or exactly expected_bytes. */
                    if (mips[i].length != expected_bytes ||
                        (mips[i].uncompressed_length != 0 &&
                         mips[i].uncompressed_length != expected_bytes)) {
                        free(mips);
                        rt_asset_error_setf(
                            RT_ASSET_ERROR_CORRUPT, "%s: invalid mip payload length", api_name);
                        return NULL;
                    }
                } else {
                    /* Supercompressed levels: the stored uncompressed size must
                     * match the format's expectation; the compressed length is
                     * validated by the decompressor. */
                    if (mips[i].uncompressed_length != expected_bytes) {
                        free(mips);
                        rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT,
                                            "%s: inconsistent uncompressed level length",
                                            api_name);
                        return NULL;
                    }
                }
            }
        }
    }

    /* Supercompressed levels (Zstandard / ZLIB) are inflated into one packed
     * buffer and the mip table is rewritten to point into it, so every decode
     * and native-retention path below runs unchanged. */
    const uint8_t *level_data = data;
    size_t level_size = size;
    uint8_t *unpacked = NULL;
    if (supercompression_scheme != 0) {
        uint64_t total = 0;
        uint64_t cursor = 0;
        for (int64_t i = 0; i < mip_count; i++) {
            if (mips[i].uncompressed_length > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES ||
                total > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES - mips[i].uncompressed_length) {
                free(mips);
                rt_asset_error_setf(
                    RT_ASSET_ERROR_TOO_LARGE, "%s: supercompressed payload too large", api_name);
                return NULL;
            }
            total += mips[i].uncompressed_length;
        }
        unpacked = (uint8_t *)malloc(total ? (size_t)total : 1u);
        if (!unpacked) {
            free(mips);
            rt_trap("TextureAsset3D.LoadKTX2: supercompression buffer allocation failed");
            return NULL;
        }
        for (int64_t i = 0; i < mip_count; i++) {
            uint8_t *level_out = NULL;
            size_t level_out_len = 0;
            int ok;

            if (supercompression_scheme == 2) {
                ok = rt_zstd_decompress_raw(data + mips[i].offset,
                                            (size_t)mips[i].length,
                                            (size_t)mips[i].uncompressed_length,
                                            &level_out,
                                            &level_out_len);
            } else {
                /* Scheme 3 stores zlib-wrapped DEFLATE: validate and strip the
                 * 2-byte CMF/FLG header and 4-byte adler32 trailer, mirroring
                 * the FBX loader's use of the raw inflate entry point. */
                const uint8_t *z = data + mips[i].offset;
                uint64_t zlen = mips[i].length;
                ok = 0;
                if (zlen > 6 && (z[0] & 0x0F) == 8 &&
                    (((uint32_t)z[0] << 8) | z[1]) % 31u == 0) {
                    ok = rt_compress_inflate_raw(z + 2,
                                                 (size_t)(zlen - 6),
                                                 (size_t)mips[i].uncompressed_length,
                                                 &level_out,
                                                 &level_out_len);
                }
            }
            if (!ok || level_out_len != (size_t)mips[i].uncompressed_length) {
                free(level_out);
                free(unpacked);
                free(mips);
                rt_asset_error_setf(RT_ASSET_ERROR_CORRUPT,
                                    "%s: level %lld failed %s decompression",
                                    api_name,
                                    (long long)i,
                                    supercompression_scheme == 2 ? "Zstandard" : "ZLIB");
                return NULL;
            }
            memcpy(unpacked + cursor, level_out, level_out_len);
            free(level_out);
            mips[i].offset = cursor;
            mips[i].length = mips[i].uncompressed_length;
            cursor += mips[i].uncompressed_length;
        }
        level_data = unpacked;
        level_size = (size_t)total;
    }

    if (strcmp(format.name, "rgba8") == 0)
        mip_pixels = textureasset3d_decode_rgba8_mips(level_data, level_size, mips, mip_count);
    /* BC3 software reference decode: also produce an RGBA8 Pixels fallback so BC3 textures render
     * on backends that cannot upload BC3 natively (native blocks are still retained below). */
    if (strcmp(format.name, "bc3") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "corrupt BC3 block payload";
        mip_pixels = textureasset3d_decode_bc3_mips(level_data, level_size, mips, mip_count);
    }
    /* BC7 software reference decode -> RGBA8 Pixels fallback; native blocks are still retained. */
    if (strcmp(format.name, "bc7") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "unsupported reserved BC7 mode or corrupt block payload";
        mip_pixels = textureasset3d_decode_bc7_mips(level_data, level_size, mips, mip_count);
    }
    if (strcmp(format.name, "bc1") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "corrupt BC1 block payload";
        mip_pixels = textureasset3d_decode_compressed_mips(
            level_data,
            level_size,
            mips,
            mip_count,
            format.block_width,
            format.block_height,
            format.block_bytes,
            textureasset3d_decode_bc1_block_adapter,
            "TextureAsset3D.LoadKTX2: BC1 Pixels fallback allocation failed");
    }
    if (strcmp(format.name, "bc4") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "corrupt BC4 block payload";
        mip_pixels = textureasset3d_decode_compressed_mips(
            level_data,
            level_size,
            mips,
            mip_count,
            format.block_width,
            format.block_height,
            format.block_bytes,
            textureasset3d_decode_bc4_block_adapter,
            "TextureAsset3D.LoadKTX2: BC4 Pixels fallback allocation failed");
    }
    if (strcmp(format.name, "bc5") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "corrupt BC5 block payload";
        mip_pixels = textureasset3d_decode_compressed_mips(
            level_data,
            level_size,
            mips,
            mip_count,
            format.block_width,
            format.block_height,
            format.block_bytes,
            textureasset3d_decode_bc5_block_adapter,
            "TextureAsset3D.LoadKTX2: BC5 Pixels fallback allocation failed");
    }
    if (strcmp(format.name, "etc2") == 0) {
        attempted_cpu_decode = 1;
        decode_failure_detail = "unsupported ETC2 color mode or corrupt block payload";
        mip_pixels = textureasset3d_decode_compressed_mips(
            level_data,
            level_size,
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
            level_data,
            level_size,
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
            free(unpacked);
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
        mip_payloads =
            textureasset3d_copy_native_mip_payloads(level_data, level_size, mips, mip_count);
        if (!mip_payloads) {
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            free(unpacked);
            free(mips);
            rt_trap("TextureAsset3D.LoadKTX2: native mip payload allocation failed");
            return NULL;
        }
    }
    /* Decoded Pixels and native payload copies own their bytes now. */
    free(unpacked);
    unpacked = NULL;

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
/// @details Returns NULL with a recoverable rt_asset_error on malformed or
///          unsupported input (query via Assets3D.GetLastLoadError); traps
///          only on caller misuse (NULL data) and allocation failure.
void *rt_textureasset3d_load_ktx2_memory(const uint8_t *data, uint64_t size) {
    void *asset;

    if (!data) {
        rt_trap("TextureAsset3D.LoadKTX2Memory: invalid payload");
        return NULL;
    }
    rt_asset_error_begin_load();
    if (size == 0 || size > (uint64_t)SIZE_MAX || size > (uint64_t)TEXTUREASSET3D_MAX_FILE_BYTES) {
        rt_asset_error_setf(
            RT_ASSET_ERROR_TOO_LARGE, "TextureAsset3D.LoadKTX2Memory: invalid payload size");
        rt_asset_error_end_load_failure();
        return NULL;
    }
    asset = textureasset3d_parse_ktx2(data, (size_t)size, "TextureAsset3D.LoadKTX2Memory");
    if (asset)
        rt_asset_error_end_load_success();
    else
        rt_asset_error_end_load_failure();
    return asset;
}

/// @brief Load a KTX2 texture from a filesystem path.
/// @details Returns NULL with a recoverable rt_asset_error when the file is
///          missing, unreadable, malformed, or unsupported; traps only on a
///          NULL/empty path (caller misuse) and allocation failure.
void *rt_textureasset3d_load_ktx2(rt_string path) {
    const char *cpath = path ? rt_string_cstr(path) : NULL;
    size_t size = 0;
    uint8_t *data;
    void *asset;

    if (!cpath || !*cpath) {
        rt_trap("TextureAsset3D.LoadKTX2: invalid path");
        return NULL;
    }
    rt_asset_error_begin_load();
    data = textureasset3d_read_file_bytes(cpath, &size);
    if (!data) {
        rt_asset_error_setf(
            RT_ASSET_ERROR_NOT_FOUND, "TextureAsset3D.LoadKTX2: cannot read '%s'", cpath);
        rt_asset_error_end_load_failure();
        return NULL;
    }
    asset = textureasset3d_parse_ktx2(data, size, "TextureAsset3D.LoadKTX2");
    free(data);
    if (asset)
        rt_asset_error_end_load_success();
    else
        rt_asset_error_end_load_failure();
    return asset;
}

/// @brief Load a KTX2 texture from a packed asset path (rt_asset registry).
/// @details Returns NULL with a recoverable rt_asset_error when the asset is
///          missing, malformed, or unsupported; traps only on a NULL path.
void *rt_textureasset3d_load_ktx2_asset(rt_string path) {
    size_t size = 0;
    uint8_t *data;
    void *asset;

    if (!path) {
        rt_trap("TextureAsset3D.LoadKTX2Asset: invalid path");
        return NULL;
    }
    rt_asset_error_begin_load();
    data = rt_asset_load_raw(path, &size);
    if (!data) {
        rt_asset_error_setf(
            RT_ASSET_ERROR_NOT_FOUND, "TextureAsset3D.LoadKTX2Asset: asset not found");
        rt_asset_error_end_load_failure();
        return NULL;
    }
    asset = textureasset3d_parse_ktx2(data, size, "TextureAsset3D.LoadKTX2Asset");
    free(data);
    if (asset)
        rt_asset_error_end_load_success();
    else
        rt_asset_error_end_load_failure();
    return asset;
}

/// @brief Return true when the runtime has a CPU fallback path for a KTX2 texture format.
int8_t rt_textureasset3d_cpu_supports_format(const char *format_name) {
    if (!format_name)
        return 0;
    return strcmp(format_name, "rgba8") == 0 || strcmp(format_name, "bc1") == 0 ||
           strcmp(format_name, "bc3") == 0 || strcmp(format_name, "bc4") == 0 ||
           strcmp(format_name, "bc5") == 0 || strcmp(format_name, "bc7") == 0 ||
           strcmp(format_name, "etc2") == 0 || strcmp(format_name, "astc") == 0;
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
    if (strcmp(asset->format, "bc1") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_BC1;
    if (strcmp(asset->format, "bc3") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_BC3;
    if (strcmp(asset->format, "bc4") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_BC4;
    if (strcmp(asset->format, "bc5") == 0)
        return RT_TEXTUREASSET3D_NATIVE_FORMAT_BC5;
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
