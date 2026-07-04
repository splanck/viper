//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_textureasset3d_core.inc  (#included from
// rt_textureasset3d.c; not a standalone translation unit) Purpose: TextureAsset3D format info,
// container lifecycle, mip residency, alpha, and RGBA8 fallback
//===----------------------------------------------------------------------===//
/// @brief Decoded properties of a Vulkan texture format: runtime name, whether it
///        is block-compressed, and its block dimensions/byte size.
typedef struct {
    const char *name;
    int8_t compressed;
    int32_t block_width;
    int32_t block_height;
    int32_t block_bytes;
} textureasset3d_format_info;

/// @brief One mip level's location in the KTX2 byte stream (offset/length), its
///        uncompressed size, and pixel dimensions.
typedef struct {
    uint64_t offset;
    uint64_t length;
    uint64_t uncompressed_length;
    uint32_t width;
    uint32_t height;
} textureasset3d_mip;

/// @brief TextureAsset3D state: the mip table plus parallel arrays of decoded
///        Pixels mips and retained native payloads, format/block metadata, the
///        currently-resident mip window, and the cache identity/revision pair.
typedef struct {
    void *vptr;
    void *pixels;
    void **mip_pixels;
    uint8_t **mip_payloads;
    textureasset3d_mip *mips;
    int64_t width;
    int64_t height;
    int64_t mip_count;
    int64_t mip_capacity;
    int64_t resident_mip_start;
    int64_t resident_mip_count;
    int64_t resident_bytes;
    int64_t retained_bytes;
    const char *format;
    int8_t compressed;
    int32_t block_width;
    int32_t block_height;
    int32_t block_bytes;
    uint64_t cache_identity;
    uint64_t native_revision;
    int8_t alpha_metadata_known;
    int8_t has_alpha_texels;
} rt_textureasset3d;

static const uint8_t ktx2_identifier[12] = {
    0xAB,
    0x4B,
    0x54,
    0x58,
    0x20,
    0x32,
    0x30,
    0xBB,
    0x0D,
    0x0A,
    0x1A,
    0x0A,
};

static uint64_t g_next_textureasset3d_cache_identity = 1;

/// @brief Atomically allocate the next process-unique, nonzero cache identity.
/// @details 0 is reserved as "no key", so the loop retries past a wrapped-to-zero value.
static uint64_t textureasset3d_next_cache_identity(void) {
    uint64_t id;
    do {
        id = __atomic_fetch_add(
            &g_next_textureasset3d_cache_identity, UINT64_C(1), __ATOMIC_RELAXED);
    } while (id == 0);
    return id;
}

/// @brief Bump the native-payload revision so the backend cache key changes.
/// @details Wraps past 0 (a reserved value) to keep every revision nonzero.
static void textureasset3d_bump_native_revision(rt_textureasset3d *asset) {
    if (!asset)
        return;
    if (asset->native_revision == UINT64_MAX)
        asset->native_revision = 1;
    else
        asset->native_revision++;
    if (asset->native_revision == 0)
        asset->native_revision = 1;
}

/// @brief Validate @p obj as a TextureAsset3D handle and return its typed pointer (NULL on
/// mismatch).
static rt_textureasset3d *textureasset3d_checked(void *obj) {
    return (rt_textureasset3d *)rt_g3d_checked_or_null(obj, RT_G3D_TEXTUREASSET3D_CLASS_ID);
}

/// @brief Number of mip slots allocated for the asset's parallel mip arrays.
static int64_t textureasset3d_allocated_mip_count(const rt_textureasset3d *asset) {
    if (!asset || asset->mip_capacity <= 0)
        return 0;
    if (asset->mip_capacity > (int64_t)TEXTUREASSET3D_MAX_MIP_COUNT)
        return (int64_t)TEXTUREASSET3D_MAX_MIP_COUNT;
    return asset->mip_capacity;
}

/// @brief Mip count safe for public access, clamped to the allocated mip table.
static int64_t textureasset3d_safe_mip_count(const rt_textureasset3d *asset) {
    int64_t allocated = textureasset3d_allocated_mip_count(asset);
    if (!asset || !asset->mips || asset->mip_count <= 0 || allocated <= 0)
        return 0;
    return asset->mip_count < allocated ? asset->mip_count : allocated;
}

/// @brief Release a GC reference held in @p *slot if this is its last drop, then NULL it.
static void textureasset3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a decoded mip fallback only if it is still a live Pixels object.
static void textureasset3d_release_pixels_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (!rt_pixels_checked_impl_or_null(*slot)) {
        *slot = NULL;
        return;
    }
    textureasset3d_release_ref(slot);
}

/// @brief GC finalizer: release each decoded Pixels mip and free the mip table and native payloads.
static void textureasset3d_finalize(void *obj) {
    rt_textureasset3d *asset = (rt_textureasset3d *)obj;
    int64_t allocated;
    if (!asset)
        return;
    allocated = textureasset3d_allocated_mip_count(asset);
    asset->pixels = NULL;
    if (asset->mip_pixels) {
        for (int64_t i = 0; i < allocated; i++)
            textureasset3d_release_pixels_ref(&asset->mip_pixels[i]);
        free(asset->mip_pixels);
        asset->mip_pixels = NULL;
    }
    if (asset->mip_payloads) {
        for (int64_t i = 0; i < allocated; i++)
            free(asset->mip_payloads[i]);
        free(asset->mip_payloads);
        asset->mip_payloads = NULL;
    }
    free(asset->mips);
    asset->mips = NULL;
    asset->mip_count = 0;
    asset->mip_capacity = 0;
    asset->retained_bytes = 0;
}

/// @brief Read a little-endian uint32 from @p p (KTX2 is little-endian on all hosts).
static uint32_t textureasset3d_read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/// @brief Read a little-endian uint64 from @p p.
static uint64_t textureasset3d_read_u64le(const uint8_t *p) {
    uint64_t lo = textureasset3d_read_u32le(p);
    uint64_t hi = textureasset3d_read_u32le(p + 4);
    return lo | (hi << 32);
}

/// @brief Map a Vulkan format enum to runtime format metadata (name, block size, compression).
/// @details Recognizes RGBA8, BC3, BC7, ETC2, and the ASTC block sizes via a dimension table;
///          unknown formats return a "unknown" entry flagged compressed with a zero block size.
static textureasset3d_format_info textureasset3d_format_from_vk(uint32_t vk_format) {
    if (vk_format == VK_FORMAT_R8G8B8A8_UNORM || vk_format == VK_FORMAT_R8G8B8A8_SRGB)
        return (textureasset3d_format_info){"rgba8", 0, 1, 1, 4};
    if (vk_format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && vk_format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK)
        return (textureasset3d_format_info){"bc1", 1, 4, 4, 8};
    if (vk_format == VK_FORMAT_BC3_UNORM_BLOCK || vk_format == VK_FORMAT_BC3_SRGB_BLOCK)
        return (textureasset3d_format_info){"bc3", 1, 4, 4, 16};
    if (vk_format == VK_FORMAT_BC4_UNORM_BLOCK)
        return (textureasset3d_format_info){"bc4", 1, 4, 4, 8};
    if (vk_format == VK_FORMAT_BC5_UNORM_BLOCK)
        return (textureasset3d_format_info){"bc5", 1, 4, 4, 16};
    if (vk_format == VK_FORMAT_BC7_UNORM_BLOCK || vk_format == VK_FORMAT_BC7_SRGB_BLOCK)
        return (textureasset3d_format_info){"bc7", 1, 4, 4, 16};
    if (vk_format == VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK ||
        vk_format == VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK)
        return (textureasset3d_format_info){"etc2", 1, 4, 4, 16};
    if (vk_format >= VK_FORMAT_ASTC_4X4_UNORM_BLOCK &&
        vk_format <= VK_FORMAT_ASTC_12X12_SRGB_BLOCK) {
        static const int8_t astc_dims[][2] = {
            {4, 4},  {4, 4},   {5, 4},   {5, 4},   {5, 5},   {5, 5},   {6, 5},
            {6, 5},  {6, 6},   {6, 6},   {8, 5},   {8, 5},   {8, 6},   {8, 6},
            {8, 8},  {8, 8},   {10, 5},  {10, 5},  {10, 6},  {10, 6},  {10, 8},
            {10, 8}, {10, 10}, {10, 10}, {12, 10}, {12, 10}, {12, 12}, {12, 12},
        };
        /* Keep the dimension table locked to the handled ASTC VkFormat span so a
         * future format addition can't silently index past the array. */
        _Static_assert(sizeof(astc_dims) / sizeof(astc_dims[0]) ==
                           (VK_FORMAT_ASTC_12X12_SRGB_BLOCK - VK_FORMAT_ASTC_4X4_UNORM_BLOCK + 1),
                       "ASTC dimension table must cover every handled ASTC VkFormat");
        uint32_t index = vk_format - VK_FORMAT_ASTC_4X4_UNORM_BLOCK;
        if (index < (uint32_t)(sizeof(astc_dims) / sizeof(astc_dims[0]))) {
            return (textureasset3d_format_info){
                "astc", 1, astc_dims[index][0], astc_dims[index][1], 16};
        }
    }
    return (textureasset3d_format_info){"unknown", 1, 0, 0, 0};
}

static int textureasset3d_expected_mip_bytes(const textureasset3d_format_info *format,
                                             uint32_t width,
                                             uint32_t height,
                                             uint64_t *out_bytes) {
    uint64_t blocks_x;
    uint64_t blocks_y;
    if (!format || !out_bytes || width == 0 || height == 0 || format->block_width <= 0 ||
        format->block_height <= 0 || format->block_bytes <= 0)
        return 0;
    blocks_x =
        ((uint64_t)width + (uint64_t)format->block_width - 1u) / (uint64_t)format->block_width;
    blocks_y =
        ((uint64_t)height + (uint64_t)format->block_height - 1u) / (uint64_t)format->block_height;
    if (blocks_x != 0 && blocks_y > UINT64_MAX / blocks_x)
        return 0;
    if (blocks_x * blocks_y > UINT64_MAX / (uint64_t)format->block_bytes)
        return 0;
    *out_bytes = blocks_x * blocks_y * (uint64_t)format->block_bytes;
    return 1;
}

/// @brief Compute a mip level's dimension as base >> level, clamped to a minimum of 1.
static uint32_t textureasset3d_mip_dimension(uint32_t base, uint32_t level) {
    uint32_t value = base >> level;
    return value > 0 ? value : 1;
}

static void textureasset3d_add_saturating_u64(uint64_t *total, uint64_t value) {
    if (!total)
        return;
    if (UINT64_MAX - *total < value)
        *total = UINT64_MAX;
    else
        *total += value;
}

static uint64_t textureasset3d_pixels_allocation_bytes(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl_or_null(pixels);
    uint64_t pixel_count;
    uint64_t data_bytes;
    uint64_t total;
    if (!p || p->width <= 0 || p->height <= 0)
        return 0;
    if ((uint64_t)p->width > UINT64_MAX / (uint64_t)p->height)
        return UINT64_MAX;
    pixel_count = (uint64_t)p->width * (uint64_t)p->height;
    if (pixel_count > UINT64_MAX / sizeof(uint32_t))
        return UINT64_MAX;
    data_bytes = pixel_count * (uint64_t)sizeof(uint32_t);
    total = data_bytes;
    return total;
}

/// @brief Scan one decoded mip for any non-opaque texel using Pixels' generation cache.
static int textureasset3d_decoded_mip_has_alpha(void *pixels) {
    rt_pixels_impl *p = rt_pixels_checked_impl_or_null(pixels);
    return rt_pixels_has_alpha_texels_cached(p) ? 1 : 0;
}

/// @brief Compute exact alpha metadata from decoded mip fallbacks.
/// @details Returns 1 only when every loaded mip has a decoded Pixels fallback, making the
///          resulting alpha classification exact. Native-only compressed assets without a decoded
///          fallback remain "unknown" instead of being classified by format name.
static int textureasset3d_compute_alpha_metadata(void **mip_pixels,
                                                 int64_t mip_count,
                                                 int8_t *out_has_alpha) {
    int has_alpha = 0;
    if (out_has_alpha)
        *out_has_alpha = 0;
    if (!mip_pixels || mip_count <= 0)
        return 0;
    for (int64_t i = 0; i < mip_count; i++) {
        if (!mip_pixels[i])
            return 0;
        if (textureasset3d_decoded_mip_has_alpha(mip_pixels[i]))
            has_alpha = 1;
    }
    if (out_has_alpha)
        *out_has_alpha = has_alpha ? 1 : 0;
    return 1;
}

/// @brief Recompute the total bytes retained by all decoded/native mip payloads.
/// @details Resident mip range controls what backends should upload, but this
///          asset keeps decoded fallback `Pixels` objects and native block payloads
///          alive so the resident range can move without re-reading source bytes.
///          This helper sums retained payload memory for cache-budget accounting
///          and saturates to INT64_MAX for ABI stability.
static int64_t textureasset3d_compute_retained_bytes(rt_textureasset3d *asset) {
    uint64_t total = 0;
    int64_t safe_mip_count = textureasset3d_safe_mip_count(asset);
    if (!asset || safe_mip_count <= 0)
        return 0;
    for (int64_t i = 0; i < safe_mip_count; i++) {
        const textureasset3d_mip *mip = &asset->mips[i];
        if (asset->mip_payloads && asset->mip_payloads[i] && mip->length > 0) {
            textureasset3d_add_saturating_u64(&total, mip->length);
        }
        if (asset->mip_pixels && asset->mip_pixels[i]) {
            textureasset3d_add_saturating_u64(
                &total, textureasset3d_pixels_allocation_bytes(asset->mip_pixels[i]));
        }
    }
    return total > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)total;
}

/// @brief Set the resident mip window and recompute resident byte total / active Pixels.
/// @details Clamps the range to the available mips, sums resident payload lengths (saturating
///          to INT64_MAX), and points `pixels` at the first resident decoded mip. Bumps the
///          native revision only when the start/count actually changed. Traps via @p api_name
///          on a negative range. Returns 1 on success, 0 for a NULL asset.
static int textureasset3d_set_resident_mip_range_internal(rt_textureasset3d *asset,
                                                          int64_t first_mip,
                                                          int64_t mip_count,
                                                          const char *api_name) {
    uint64_t total = 0;
    int64_t old_start;
    int64_t old_count;
    int64_t safe_mip_count;
    int64_t clamped_mip_count;

    if (!asset)
        return 0;
    old_start = asset->resident_mip_start;
    old_count = asset->resident_mip_count;
    if (first_mip < 0 || mip_count < 0) {
        rt_trap(api_name ? api_name : "TextureAsset3D.SetResidentMipRange: negative mip range");
        return 0;
    }
    safe_mip_count = textureasset3d_safe_mip_count(asset);
    clamped_mip_count = safe_mip_count > 0 ? safe_mip_count : 0;
    if (mip_count == 0 || first_mip >= safe_mip_count || safe_mip_count <= 0) {
        asset->resident_mip_start = first_mip > clamped_mip_count ? clamped_mip_count : first_mip;
        asset->resident_mip_count = 0;
        asset->resident_bytes = 0;
        asset->pixels = NULL;
        if (old_start != asset->resident_mip_start || old_count != asset->resident_mip_count)
            textureasset3d_bump_native_revision(asset);
        return 1;
    }
    if (mip_count > safe_mip_count - first_mip)
        mip_count = safe_mip_count - first_mip;

    for (int64_t i = 0; i < mip_count; i++) {
        int64_t absolute_mip = first_mip + i;
        const textureasset3d_mip *mip = &asset->mips[absolute_mip];
        if (asset->mip_payloads && asset->mip_payloads[absolute_mip] && mip->length > 0) {
            textureasset3d_add_saturating_u64(&total, mip->length);
        } else if (asset->mip_pixels && asset->mip_pixels[absolute_mip]) {
            textureasset3d_add_saturating_u64(
                &total, textureasset3d_pixels_allocation_bytes(asset->mip_pixels[absolute_mip]));
        }
    }

    asset->resident_mip_start = first_mip;
    asset->resident_mip_count = mip_count;
    asset->resident_bytes = total > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)total;
    asset->pixels = asset->mip_pixels ? asset->mip_pixels[first_mip] : NULL;
    if (old_start != asset->resident_mip_start || old_count != asset->resident_mip_count)
        textureasset3d_bump_native_revision(asset);
    return 1;
}

/// @brief True when the currently resident mip window is inside the loaded mip table.
static int textureasset3d_resident_range_valid(const rt_textureasset3d *asset) {
    int64_t safe_mip_count = textureasset3d_safe_mip_count(asset);
    if (!asset || safe_mip_count <= 0 || asset->resident_mip_start < 0 ||
        asset->resident_mip_count <= 0)
        return 0;
    if (asset->resident_mip_start >= safe_mip_count)
        return 0;
    return asset->resident_mip_count <= safe_mip_count - asset->resident_mip_start;
}

/// @brief True when the resident range contains at least one complete native payload.
static int textureasset3d_resident_range_has_native_payload(const rt_textureasset3d *asset) {
    if (!asset || !asset->compressed || !asset->mip_payloads || !asset->mips ||
        !textureasset3d_resident_range_valid(asset) || asset->block_width <= 0 ||
        asset->block_height <= 0 || asset->block_bytes <= 0)
        return 0;
    for (int64_t i = 0; i < asset->resident_mip_count; i++) {
        int64_t mip = asset->resident_mip_start + i;
        if (mip < 0 || mip >= textureasset3d_safe_mip_count(asset))
            continue;
        if (asset->mip_payloads[mip] && asset->mips[mip].length > 0 && asset->mips[mip].width > 0 &&
            asset->mips[mip].height > 0)
            return 1;
    }
    return 0;
}

/// @brief Read an entire file into a freshly malloc'd buffer (caller frees).
/// @details Rejects empty files and files larger than TEXTUREASSET3D_MAX_FILE_BYTES.
/// @return Byte buffer with @p out_size set, or NULL on open/read/size failure.
static uint8_t *textureasset3d_read_file_bytes(const char *path, size_t *out_size) {
    FILE *file;
    int64_t file_size;
    uint8_t *bytes;

    if (out_size)
        *out_size = 0;
    if (!path || !*path)
        return NULL;
    file = rt_file_stdio_open_utf8(path, "rb");
    if (!file)
        return NULL;
    if (textureasset3d_fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    file_size = (int64_t)textureasset3d_ftell(file);
    if (textureasset3d_fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    if (file_size <= 0 || (uint64_t)file_size > TEXTUREASSET3D_MAX_FILE_BYTES) {
        fclose(file);
        return NULL;
    }
    bytes = (uint8_t *)malloc((size_t)file_size);
    if (!bytes) {
        fclose(file);
        return NULL;
    }
    if (fread(bytes, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    fclose(file);
    if (out_size)
        *out_size = (size_t)file_size;
    return bytes;
}

/// @brief Compute width*height*4 RGBA8 bytes with overflow checks.
/// @return 1 with @p out_byte_count set, or 0 on zero dimension or multiplication overflow.
static int textureasset3d_rgba8_byte_count(uint32_t width,
                                           uint32_t height,
                                           uint64_t *out_byte_count) {
    uint64_t pixels;
    if (out_byte_count)
        *out_byte_count = 0;
    if (width == 0 || height == 0)
        return 0;
    if ((uint64_t)width > UINT64_MAX / (uint64_t)height)
        return 0;
    pixels = (uint64_t)width * (uint64_t)height;
    if (pixels > UINT64_MAX / 4u)
        return 0;
    if (out_byte_count)
        *out_byte_count = pixels * 4u;
    return 1;
}

/// @brief Copy a raw RGBA8 mip from the file into a new Pixels object.
/// @details Validates the byte budget and that [offset, offset+needed) lies within @p size,
///          then repacks the source bytes into the Pixels RGBA word order.
/// @return New Pixels handle, or NULL on bounds/budget failure.
static void *textureasset3d_decode_rgba8_fallback(const uint8_t *data,
                                                  size_t size,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  uint64_t offset,
                                                  uint64_t length) {
    uint64_t needed = 0;
    void *pixels;

    if (!textureasset3d_rgba8_byte_count(width, height, &needed))
        return NULL;
    if (needed > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES)
        return NULL;
    if (length < needed || offset > (uint64_t)size || needed > (uint64_t)size - offset)
        return NULL;

    pixels = rt_pixels_new((int64_t)width, (int64_t)height);
    if (!pixels) {
        rt_trap("TextureAsset3D.LoadKTX2: Pixels fallback allocation failed");
        return NULL;
    }
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint64_t index = ((uint64_t)y * (uint64_t)width + (uint64_t)x) * 4u;
            const uint8_t *p = data + offset + index;
            uint32_t rgba = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                            ((uint32_t)p[2] << 8) | (uint32_t)p[3];
            rt_pixels_set_rgba(pixels, (int64_t)x, (int64_t)y, (int64_t)rgba);
        }
    }
    return pixels;
}

static void textureasset3d_release_mip_pixels(void **mip_pixels, int64_t mip_count);

/// @brief Allocate an 8x8 magenta/black checker Pixels fallback for failed compressed decodes.
/// @details The conspicuous pattern makes unsupported CPU texture blocks visible on software and
///          RenderTarget3D paths instead of producing an apparently blank material.
static void *textureasset3d_make_decode_failure_checker(void) {
    void *pixels = rt_pixels_new(8, 8);
    if (!pixels) {
        rt_trap("TextureAsset3D.LoadKTX2: checker fallback allocation failed");
        return NULL;
    }
    for (int64_t y = 0; y < 8; y++) {
        for (int64_t x = 0; x < 8; x++) {
            int64_t rgba = (((x + y) & 1) == 0) ? INT64_C(0xFF00FFFF) : INT64_C(0x000000FF);
            rt_pixels_set_rgba(pixels, x, y, rgba);
        }
    }
    return pixels;
}

/// @brief Allocate a checker fallback for each mip slot after whole-texture CPU decode failure.
static void **textureasset3d_make_decode_failure_checker_mips(int64_t mip_count) {
    void **mip_pixels;

    if (mip_count <= 0)
        return NULL;
    mip_pixels = (void **)calloc((size_t)mip_count, sizeof(void *));
    if (!mip_pixels)
        return NULL;
    for (int64_t i = 0; i < mip_count; i++) {
        mip_pixels[i] = textureasset3d_make_decode_failure_checker();
        if (!mip_pixels[i]) {
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            return NULL;
        }
    }
    return mip_pixels;
}

/// @brief Release every decoded Pixels mip in the array, then free the array itself.
static void textureasset3d_release_mip_pixels(void **mip_pixels, int64_t mip_count) {
    if (!mip_pixels)
        return;
    for (int64_t i = 0; i < mip_count; i++)
        textureasset3d_release_pixels_ref(&mip_pixels[i]);
    free(mip_pixels);
}

/// @brief Free every native payload buffer in the array, then free the array itself.
static void textureasset3d_release_mip_payloads(uint8_t **mip_payloads, int64_t mip_count) {
    if (!mip_payloads)
        return;
    for (int64_t i = 0; i < mip_count; i++)
        free(mip_payloads[i]);
    free(mip_payloads);
}

/// @brief Copy each compressed mip's native bytes out of the file into owned per-mip buffers.
/// @details Bounds-checks every mip against @p size; on any failure releases all buffers
///          allocated so far and returns NULL. Zero-length mips leave a NULL slot.
static uint8_t **textureasset3d_copy_native_mip_payloads(const uint8_t *data,
                                                         size_t size,
                                                         const textureasset3d_mip *mips,
                                                         int64_t mip_count) {
    uint8_t **mip_payloads;

    if (!data || !mips || mip_count <= 0)
        return NULL;
    mip_payloads = (uint8_t **)calloc((size_t)mip_count, sizeof(uint8_t *));
    if (!mip_payloads)
        return NULL;
    for (int64_t i = 0; i < mip_count; i++) {
        const textureasset3d_mip *mip = &mips[i];
        if (mip->length == 0)
            continue;
        if (mip->offset > (uint64_t)size || mip->length > (uint64_t)size - mip->offset) {
            textureasset3d_release_mip_payloads(mip_payloads, mip_count);
            return NULL;
        }
        if (mip->length > SIZE_MAX) {
            textureasset3d_release_mip_payloads(mip_payloads, mip_count);
            return NULL;
        }
        mip_payloads[i] = (uint8_t *)malloc((size_t)mip->length);
        if (!mip_payloads[i]) {
            textureasset3d_release_mip_payloads(mip_payloads, mip_count);
            return NULL;
        }
        memcpy(mip_payloads[i], data + mip->offset, (size_t)mip->length);
    }
    return mip_payloads;
}

/// @brief Decode all mips of a raw RGBA8 texture into a Pixels array (NULL if any mip fails).
static void **textureasset3d_decode_rgba8_mips(const uint8_t *data,
                                               size_t size,
                                               const textureasset3d_mip *mips,
                                               int64_t mip_count) {
    void **mip_pixels;

    if (!data || !mips || mip_count <= 0)
        return NULL;
    mip_pixels = (void **)calloc((size_t)mip_count, sizeof(void *));
    if (!mip_pixels)
        return NULL;
    for (int64_t i = 0; i < mip_count; i++) {
        mip_pixels[i] = textureasset3d_decode_rgba8_fallback(
            data, size, mips[i].width, mips[i].height, mips[i].offset, mips[i].length);
        if (!mip_pixels[i]) {
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            return NULL;
        }
    }
    return mip_pixels;
}
