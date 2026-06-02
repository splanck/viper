//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_textureasset3d.c
// Purpose: TextureAsset3D — loads KTX2 textures (RGBA8/BC3/BC7/ETC2/ASTC),
//   retains native compressed mip payloads for GPU upload, and software-decodes
//   RGBA8/BC3/BC7/ETC2 plus ASTC LDR void-extent blocks to a Pixels fallback
//   for backends without native support.
//
// Key invariants:
//   - Only 2D, single-layer, single-face KTX2 (no array/cube/3D) is accepted.
//   - Software fallbacks cap at TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES; files
//     cap at TEXTUREASSET3D_MAX_FILE_BYTES. All offset/length math is overflow-
//     and bounds-checked against the file size before any read.
//   - BC7 software decode covers modes 0-7. ETC2 fallback covers RGBA8/EAC
//     blocks in individual/differential color modes. ASTC fallback covers LDR
//     2D void-extent blocks; unsupported compressed blocks keep native payloads.
//   - cache_identity is a process-unique nonzero id; native_revision bumps on
//     every resident-range change so the backend cache key invalidates.
//
// Ownership/Lifetime:
//   - TextureAsset3D is GC-managed; the finalizer frees the mip table, the
//     per-mip native payloads, and releases each decoded Pixels mip.
//
// Links: rt_textureasset3d.h, rt_pixels.h, rt_asset.h
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_textureasset3d.h"

#include "rt_asset.h"
#include "rt_graphics3d_ids.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_platform.h"
#include "rt_trap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTUREASSET3D_KTX2_HEADER_SIZE 80u
#define TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE 24u
#define TEXTUREASSET3D_MAX_FILE_BYTES (256u * 1024u * 1024u)
#define TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES (256u * 1024u * 1024u)
#define TEXTUREASSET3D_MAX_MIP_COUNT 32u

#define VK_FORMAT_R8G8B8A8_UNORM 37u
#define VK_FORMAT_R8G8B8A8_SRGB 43u
#define VK_FORMAT_BC3_UNORM_BLOCK 137u
#define VK_FORMAT_BC3_SRGB_BLOCK 138u
#define VK_FORMAT_BC7_UNORM_BLOCK 145u
#define VK_FORMAT_BC7_SRGB_BLOCK 146u
#define VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK 151u
#define VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK 152u
#define VK_FORMAT_ASTC_4X4_UNORM_BLOCK 157u
#define VK_FORMAT_ASTC_12X12_SRGB_BLOCK 184u

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
    const char *format;
    int8_t compressed;
    int32_t block_width;
    int32_t block_height;
    int32_t block_bytes;
    uint64_t cache_identity;
    uint64_t native_revision;
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

static volatile uint64_t g_next_textureasset3d_cache_identity = 1;

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
    if (vk_format == VK_FORMAT_BC3_UNORM_BLOCK || vk_format == VK_FORMAT_BC3_SRGB_BLOCK)
        return (textureasset3d_format_info){"bc3", 1, 4, 4, 16};
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
        uint32_t index = vk_format - VK_FORMAT_ASTC_4X4_UNORM_BLOCK;
        if (index < (uint32_t)(sizeof(astc_dims) / sizeof(astc_dims[0]))) {
            return (textureasset3d_format_info){
                "astc", 1, astc_dims[index][0], astc_dims[index][1], 16};
        }
    }
    return (textureasset3d_format_info){"unknown", 1, 0, 0, 0};
}

/// @brief Compute a mip level's dimension as base >> level, clamped to a minimum of 1.
static uint32_t textureasset3d_mip_dimension(uint32_t base, uint32_t level) {
    uint32_t value = base >> level;
    return value > 0 ? value : 1;
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

    if (!asset)
        return 0;
    old_start = asset->resident_mip_start;
    old_count = asset->resident_mip_count;
    if (first_mip < 0 || mip_count < 0) {
        rt_trap(api_name ? api_name : "TextureAsset3D.SetResidentMipRange: negative mip range");
        return 0;
    }
    safe_mip_count = textureasset3d_safe_mip_count(asset);
    if (mip_count == 0 || first_mip >= safe_mip_count || safe_mip_count <= 0) {
        asset->resident_mip_start =
            (safe_mip_count > 0 && first_mip > safe_mip_count) ? safe_mip_count : first_mip;
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
        const textureasset3d_mip *mip = &asset->mips[first_mip + i];
        if (UINT64_MAX - total < mip->length) {
            total = UINT64_MAX;
            break;
        }
        total += mip->length;
    }

    asset->resident_mip_start = first_mip;
    asset->resident_mip_count = mip_count;
    asset->resident_bytes = total > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)total;
    asset->pixels =
        (asset->mip_pixels && first_mip < safe_mip_count) ? asset->mip_pixels[first_mip] : NULL;
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
        if (asset->mip_payloads[mip] && asset->mips[mip].length > 0 &&
            asset->mips[mip].width > 0 && asset->mips[mip].height > 0)
            return 1;
    }
    return 0;
}

/// @brief Read an entire file into a freshly malloc'd buffer (caller frees).
/// @details Rejects empty files and files larger than TEXTUREASSET3D_MAX_FILE_BYTES.
/// @return Byte buffer with @p out_size set, or NULL on open/read/size failure.
static uint8_t *textureasset3d_read_file_bytes(const char *path, size_t *out_size) {
    FILE *file;
    long file_size;
    uint8_t *bytes;

    if (out_size)
        *out_size = 0;
    if (!path || !*path)
        return NULL;
    file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    file_size = ftell(file);
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    if (file_size <= 0 || (unsigned long)file_size > TEXTUREASSET3D_MAX_FILE_BYTES) {
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

/* --- BC3 (DXT5) software reference decode. Produces an RGBA8 Pixels fallback so BC3-compressed
 *     textures render on backends that cannot upload BC3 natively (R-TEX-001). --- */
/// @brief Expand a 5-bit color component to 8 bits by bit replication (BC1/BC3 R and B endpoints).
static uint8_t textureasset3d_expand5(uint32_t v) {
    return (uint8_t)((v << 3) | (v >> 2));
}

/// @brief Expand a 6-bit color component to 8 bits by bit replication (BC1/BC3 green endpoints).
static uint8_t textureasset3d_expand6(uint32_t v) {
    return (uint8_t)((v << 2) | (v >> 4));
}

/// @brief Decode one 16-byte BC3 (DXT5) block into 16 row-major RGBA texels (@p out_rgba is 64
///   bytes). Non-static so the unit test can verify the decode against known blocks.
void rt_textureasset3d_decode_bc3_block(const uint8_t *b, uint8_t *out_rgba) {
    uint8_t a[8];
    uint8_t a0, a1;
    uint64_t abits = 0;
    uint32_t c0, c1, cbits;
    uint8_t cr[4], cg[4], cb[4];
    if (!b || !out_rgba)
        return;
    /* Alpha block (bytes 0..7): two endpoints then 16 x 3-bit indices. */
    a0 = b[0];
    a1 = b[1];
    a[0] = a0;
    a[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; i++)
            a[i + 1] = (uint8_t)(((7 - i) * (int)a0 + i * (int)a1) / 7);
    } else {
        for (int i = 1; i <= 4; i++)
            a[i + 1] = (uint8_t)(((5 - i) * (int)a0 + i * (int)a1) / 5);
        a[6] = 0;
        a[7] = 255;
    }
    for (int i = 0; i < 6; i++)
        abits |= (uint64_t)b[2 + i] << (8 * i);
    /* Color block (bytes 8..15): BC1 endpoints, always 4-colour in BC3. */
    c0 = (uint32_t)b[8] | ((uint32_t)b[9] << 8);
    c1 = (uint32_t)b[10] | ((uint32_t)b[11] << 8);
    cr[0] = textureasset3d_expand5((c0 >> 11) & 0x1F);
    cg[0] = textureasset3d_expand6((c0 >> 5) & 0x3F);
    cb[0] = textureasset3d_expand5(c0 & 0x1F);
    cr[1] = textureasset3d_expand5((c1 >> 11) & 0x1F);
    cg[1] = textureasset3d_expand6((c1 >> 5) & 0x3F);
    cb[1] = textureasset3d_expand5(c1 & 0x1F);
    cr[2] = (uint8_t)((2 * (int)cr[0] + (int)cr[1]) / 3);
    cg[2] = (uint8_t)((2 * (int)cg[0] + (int)cg[1]) / 3);
    cb[2] = (uint8_t)((2 * (int)cb[0] + (int)cb[1]) / 3);
    cr[3] = (uint8_t)(((int)cr[0] + 2 * (int)cr[1]) / 3);
    cg[3] = (uint8_t)(((int)cg[0] + 2 * (int)cg[1]) / 3);
    cb[3] = (uint8_t)(((int)cb[0] + 2 * (int)cb[1]) / 3);
    cbits = (uint32_t)b[12] | ((uint32_t)b[13] << 8) | ((uint32_t)b[14] << 16) |
            ((uint32_t)b[15] << 24);
    for (int t = 0; t < 16; t++) {
        uint32_t ci = (cbits >> (2 * t)) & 0x3u;
        uint32_t ai = (uint32_t)((abits >> (3 * t)) & 0x7u);
        out_rgba[t * 4 + 0] = cr[ci];
        out_rgba[t * 4 + 1] = cg[ci];
        out_rgba[t * 4 + 2] = cb[ci];
        out_rgba[t * 4 + 3] = a[ai];
    }
}

/// @brief Software-decode a whole BC3 mip into a new RGBA8 Pixels object.
/// @details Iterates 4x4 blocks, decodes each via rt_textureasset3d_decode_bc3_block, and
///          writes only the texels inside the image (edge blocks may overhang). Bounds- and
///          budget-checked. Returns NULL on overflow, oversize, or allocation failure.
static void *textureasset3d_decode_bc3_fallback(const uint8_t *data,
                                                size_t size,
                                                uint32_t width,
                                                uint32_t height,
                                                uint64_t offset,
                                                uint64_t length) {
    void *pixels;
    uint32_t blocks_x = (width + 3u) / 4u;
    uint32_t blocks_y = (height + 3u) / 4u;
    uint64_t needed = (uint64_t)blocks_x * (uint64_t)blocks_y * 16u;
    uint64_t rgba_bytes = 0;
    if (width == 0 || height == 0)
        return NULL;
    if (!textureasset3d_rgba8_byte_count(width, height, &rgba_bytes))
        return NULL;
    if (rgba_bytes > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES)
        return NULL;
    if (length < needed || offset > (uint64_t)size || needed > (uint64_t)size - offset)
        return NULL;
    pixels = rt_pixels_new((int64_t)width, (int64_t)height);
    if (!pixels) {
        rt_trap("TextureAsset3D.LoadKTX2: BC3 Pixels fallback allocation failed");
        return NULL;
    }
    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint8_t texels[64];
            const uint8_t *block = data + offset + ((uint64_t)by * blocks_x + bx) * 16u;
            rt_textureasset3d_decode_bc3_block(block, texels);
            for (int ty = 0; ty < 4; ty++) {
                for (int tx = 0; tx < 4; tx++) {
                    uint32_t px = bx * 4u + (uint32_t)tx;
                    uint32_t py = by * 4u + (uint32_t)ty;
                    const uint8_t *c = &texels[(ty * 4 + tx) * 4];
                    uint32_t rgba;
                    if (px >= width || py >= height)
                        continue;
                    rgba = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) | ((uint32_t)c[2] << 8) |
                           (uint32_t)c[3];
                    rt_pixels_set_rgba(pixels, (int64_t)px, (int64_t)py, (int64_t)rgba);
                }
            }
        }
    }
    return pixels;
}

/// @brief Decode all mips of a BC3 texture into a Pixels array (NULL if any mip fails).
static void **textureasset3d_decode_bc3_mips(const uint8_t *data,
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
        mip_pixels[i] = textureasset3d_decode_bc3_fallback(
            data, size, mips[i].width, mips[i].height, mips[i].offset, mips[i].length);
        if (!mip_pixels[i]) {
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            return NULL;
        }
    }
    return mip_pixels;
}

/* ------------------------------------------------------------------------- *
 * BC7 software reference decode.
 *
 * BC7 packs a 4x4 RGBA block into 16 bytes across eight modes. Modes 0-3/7 split
 * texels across two or three endpoint subsets and need the fixed partition and
 * anchor-index tables from the BC7/BPTC format. Modes 4-6 are single-subset but
 * share the same endpoint/index expansion rules, so the decoder below uses one
 * table-driven path for all non-reserved modes.
 * ------------------------------------------------------------------------- */

/* Interpolation weights (numerator over 64) for 2-, 3-, and 4-bit indices. */
static const uint8_t BC7_W2[4] = {0, 21, 43, 64};
static const uint8_t BC7_W3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
static const uint8_t BC7_W4[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

typedef struct {
    uint8_t subset_count;
    uint8_t partition_bits;
    uint8_t rotation_bits;
    uint8_t index_selection_bits;
    uint8_t color_bits;
    uint8_t alpha_bits;
    uint8_t endpoint_pbits;
    uint8_t shared_pbits;
    uint8_t primary_index_bits;
    uint8_t primary_index_total_bits;
    uint8_t secondary_index_bits;
} bc7_mode_info;

static const bc7_mode_info BC7_MODES[8] = {
    {3, 4, 0, 0, 4, 0, 1, 0, 3, 45, 0},
    {2, 6, 0, 0, 6, 0, 0, 1, 3, 46, 0},
    {3, 6, 0, 0, 5, 0, 0, 0, 2, 29, 0},
    {2, 6, 0, 0, 7, 0, 1, 0, 2, 30, 0},
    {1, 0, 2, 1, 5, 6, 0, 0, 2, 31, 3},
    {1, 0, 2, 0, 7, 8, 0, 0, 2, 31, 2},
    {1, 0, 0, 0, 7, 7, 1, 0, 4, 63, 0},
    {2, 6, 0, 0, 5, 5, 1, 0, 2, 30, 0},
};

static const uint8_t BC7_PARTITION2[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1},
    {0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0},
    {0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0},
    {0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0},
    {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1},
    {0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0},
    {0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1},
    {0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1},
    {0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0},
    {0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0},
    {0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1},
    {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0},
    {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1},
};

static const uint8_t BC7_PARTITION3[64][16] = {
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2},
    {0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2},
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2},
    {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2},
    {0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0},
    {0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2},
    {0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0},
    {0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1},
    {0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2},
    {0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2},
    {0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0},
    {0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0},
    {0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1},
    {0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2},
    {0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1},
    {0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2},
    {0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2},
    {0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0},
    {0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0},
    {0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0},
    {0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0},
    {0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1},
    {0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1},
    {0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1},
    {0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1},
    {0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1},
    {0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2},
    {0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2},
    {0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2},
    {0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2},
    {0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2},
    {0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1},
    {0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2},
    {0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
    {0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0},
};

static const uint8_t BC7_ANCHOR2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,  8, 2,  2, 8,
    8,  15, 2,  8,  2,  2,  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15, 15, 2, 8,  2, 2,
    2,  15, 15, 6,  6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2, 15,
};

static const uint8_t BC7_ANCHOR3A[64] = {
    3,  3,  15, 15, 8, 3,  15, 15, 8,  8,  6, 6,  6, 5,  3, 3,  3,  3,  8,  15, 3,  3,
    6,  10, 5,  8,  8, 6,  8,  5,  15, 15, 8, 15, 3, 5,  6, 10, 8,  15, 15, 3,  15, 5,
    15, 15, 15, 15, 3, 15, 5,  5,  5,  8,  5, 10, 5, 10, 8, 13, 15, 12, 3,  3,
};

static const uint8_t BC7_ANCHOR3B[64] = {
    15, 8, 8, 3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,  15, 8,  15, 3,  15, 8,
    15, 8, 3, 15, 6,  10, 15, 15, 10, 8,  15, 3,  15, 10, 10, 8,  9,  10, 6,  15, 8,  15,
    3,  6, 6, 8,  15, 3,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8,
};

/// @brief Read `n` bits at a fixed bit offset, LSB-first within each byte.
static uint32_t bc7_get_bits_at(const uint8_t *b, int offset, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint32_t)((b[(offset + i) >> 3] >> ((offset + i) & 7)) & 1u) << i;
    return v;
}

/// @brief Expand a `bits`-bit endpoint component to 8 bits by bit replication.
static uint8_t bc7_unq(uint32_t v, int bits) {
    if (bits >= 8)
        return (uint8_t)v;
    return (uint8_t)((v << (8 - bits)) | (v >> (2 * bits - 8)));
}

/// @brief Interpolate two endpoints by weight `w` (out of 64) with rounding: lerp(e0, e1, w/64).
static uint8_t bc7_interp(uint8_t e0, uint8_t e1, uint8_t w) {
    return (uint8_t)(((uint32_t)e0 * (64u - w) + (uint32_t)e1 * w + 32u) >> 6);
}

/// @brief Modes 4/5 may swap the alpha channel with one color channel after decode.
static void bc7_apply_rotation(uint32_t rot, uint8_t *px) {
    uint8_t t;
    if (rot == 1) {
        t = px[0];
        px[0] = px[3];
        px[3] = t;
    } else if (rot == 2) {
        t = px[1];
        px[1] = px[3];
        px[3] = t;
    } else if (rot == 3) {
        t = px[2];
        px[2] = px[3];
        px[3] = t;
    }
}

/// @brief The BC7 mode is the index of the first set bit in byte 0 (LSB-first); 8 = invalid.
static int bc7_block_mode(const uint8_t *b) {
    int mode = 0;
    while (mode < 8 && !((b[0] >> mode) & 1u))
        mode++;
    return mode;
}

/// @brief Endpoint color-value count for a block: two endpoints per subset.
static int bc7_num_colors(const bc7_mode_info *info) {
    return (int)info->subset_count * 2;
}

/// @brief Bit offset of the first color-endpoint field, past the mode/partition/rotation/
///   index-selection header bits.
static int bc7_endpoint_base(int mode, const bc7_mode_info *info) {
    return mode + 1 + (int)info->partition_bits + (int)info->rotation_bits +
           (int)info->index_selection_bits;
}

/// @brief Bit offset of endpoint `endpoint`'s red component (all reds are packed first).
static int bc7_red_offset(int mode, const bc7_mode_info *info, int endpoint) {
    return bc7_endpoint_base(mode, info) + (int)info->color_bits * endpoint;
}

/// @brief Bit offset of endpoint `endpoint`'s green component (greens follow all reds).
static int bc7_green_offset(int mode, const bc7_mode_info *info, int endpoint) {
    return bc7_red_offset(mode, info, bc7_num_colors(info)) + (int)info->color_bits * endpoint;
}

/// @brief Bit offset of endpoint `endpoint`'s blue component (blues follow all greens).
static int bc7_blue_offset(int mode, const bc7_mode_info *info, int endpoint) {
    return bc7_green_offset(mode, info, bc7_num_colors(info)) + (int)info->color_bits * endpoint;
}

/// @brief Bit offset of endpoint `endpoint`'s alpha component (alphas follow all blues).
static int bc7_alpha_offset(int mode, const bc7_mode_info *info, int endpoint) {
    return bc7_blue_offset(mode, info, bc7_num_colors(info)) + (int)info->alpha_bits * endpoint;
}

/// @brief Bit offset of per-endpoint p-bit `endpoint` (p-bits follow the alpha endpoints).
static int bc7_endpoint_pbit_offset(int mode, const bc7_mode_info *info, int endpoint) {
    return bc7_alpha_offset(mode, info, bc7_num_colors(info)) +
           (int)info->endpoint_pbits * endpoint;
}

/// @brief Bit offset of shared (per-subset) p-bit `subset`, used by modes with shared p-bits.
static int bc7_shared_pbit_offset(int mode, const bc7_mode_info *info, int subset) {
    return bc7_endpoint_pbit_offset(mode, info, bc7_num_colors(info)) +
           (int)info->shared_pbits * subset;
}

/// @brief Bit offset where per-texel index data begins, past all endpoint and p-bit fields.
static int bc7_index_base(int mode, const bc7_mode_info *info) {
    return bc7_shared_pbit_offset(mode, info, 2);
}

/// @brief Subset (partition region) that `texel` belongs to, read from the 2-/3-subset
///   partition tables; 0 for single-subset modes.
static int bc7_subset_index(const bc7_mode_info *info, int partition, int texel) {
    if (info->subset_count == 2)
        return BC7_PARTITION2[partition][texel];
    if (info->subset_count == 3)
        return BC7_PARTITION3[partition][texel];
    return 0;
}

/// @brief Texel position of subset `subset`'s anchor index (the index whose high bit is
///   implicitly 0); 0 for subset 0.
static int bc7_anchor_index(const bc7_mode_info *info, int partition, int subset) {
    if (subset == 1)
        return info->subset_count == 2 ? BC7_ANCHOR2[partition] : BC7_ANCHOR3A[partition];
    if (subset == 2)
        return BC7_ANCHOR3B[partition];
    return 0;
}

/// @brief Read texel `anchor`'s color index: selects the primary or secondary index set per the
///   block's index-selection bit / color rotation, drops the implicit MSB for anchor texels,
///   advances *bit_offset past the bits read, and reports the index bit-width in *out_bits.
static uint32_t bc7_index_value(const uint8_t *b,
                                int mode,
                                const bc7_mode_info *info,
                                int anchor,
                                int color_channel,
                                int *bit_offset,
                                int *out_bits) {
    int selection_offset = mode + 1 + (int)info->partition_bits + (int)info->rotation_bits;
    int selection = info->index_selection_bits ? (int)bc7_get_bits_at(b, selection_offset, 1) : 0;
    int secondary =
        color_channel ? selection == 1 : (info->secondary_index_bits != 0 && selection == 0);
    int bits = secondary ? (int)info->secondary_index_bits : (int)info->primary_index_bits;
    int read_bits = bits - (anchor ? 1 : 0);
    int base = bc7_index_base(mode, info) + (secondary ? (int)info->primary_index_total_bits : 0) +
               *bit_offset;
    uint32_t value = read_bits > 0 ? bc7_get_bits_at(b, base, read_bits) : 0;
    *bit_offset += read_bits;
    if (out_bits)
        *out_bits = bits;
    return value;
}

/// @brief Interpolation weight (out of 64) for a 2-, 3-, or 4-bit BC7 index.
static uint8_t bc7_index_weight(uint32_t index, int bits) {
    if (bits == 2)
        return BC7_W2[index & 3u];
    if (bits == 3)
        return BC7_W3[index & 7u];
    return BC7_W4[index & 15u];
}

/// @brief Decode one 16-byte BC7 block into 16 row-major RGBA texels (@p out_rgba is 64 bytes).
///   Handles modes 0-7, including partitioned modes. Non-static so the unit test can verify
///   against constructed blocks.
int rt_textureasset3d_decode_bc7_block(const uint8_t *b, uint8_t *out_rgba) {
    int mode;
    if (!b || !out_rgba)
        return 0;
    mode = bc7_block_mode(b);
    if (mode >= 8)
        return 0;

    {
        const bc7_mode_info *info = &BC7_MODES[mode];
        uint8_t endpoint[3][2][4];
        int color_bits =
            (int)info->color_bits + (int)info->endpoint_pbits + (int)info->shared_pbits;
        int alpha_bits =
            (int)info->alpha_bits + (int)info->endpoint_pbits + (int)info->shared_pbits;
        int partition =
            info->partition_bits ? (int)bc7_get_bits_at(b, mode + 1, info->partition_bits) : 0;
        int rotation_offset = mode + 1 + (int)info->partition_bits;
        uint32_t rotation =
            info->rotation_bits ? bc7_get_bits_at(b, rotation_offset, info->rotation_bits) : 0;
        int color_index_offset = 0;
        int alpha_index_offset = 0;

        for (int s = 0; s < (int)info->subset_count; s++) {
            for (int e = 0; e < 2; e++) {
                int endpoint_index = s * 2 + e;
                endpoint[s][e][0] = (uint8_t)bc7_get_bits_at(
                    b, bc7_red_offset(mode, info, endpoint_index), info->color_bits);
                endpoint[s][e][1] = (uint8_t)bc7_get_bits_at(
                    b, bc7_green_offset(mode, info, endpoint_index), info->color_bits);
                endpoint[s][e][2] = (uint8_t)bc7_get_bits_at(
                    b, bc7_blue_offset(mode, info, endpoint_index), info->color_bits);
                endpoint[s][e][3] =
                    info->alpha_bits
                        ? (uint8_t)bc7_get_bits_at(
                              b, bc7_alpha_offset(mode, info, endpoint_index), info->alpha_bits)
                        : 255;
            }
        }

        if (info->shared_pbits) {
            for (int s = 0; s < (int)info->subset_count; s++) {
                uint8_t p = (uint8_t)bc7_get_bits_at(b, bc7_shared_pbit_offset(mode, info, s), 1);
                for (int e = 0; e < 2; e++)
                    for (int c = 0; c < 3; c++)
                        endpoint[s][e][c] = (uint8_t)((endpoint[s][e][c] << 1) | p);
            }
        }
        if (info->endpoint_pbits) {
            for (int s = 0; s < (int)info->subset_count; s++) {
                for (int e = 0; e < 2; e++) {
                    int endpoint_index = s * 2 + e;
                    uint8_t p = (uint8_t)bc7_get_bits_at(
                        b, bc7_endpoint_pbit_offset(mode, info, endpoint_index), 1);
                    for (int c = 0; c < 3; c++)
                        endpoint[s][e][c] = (uint8_t)((endpoint[s][e][c] << 1) | p);
                    if (info->alpha_bits)
                        endpoint[s][e][3] = (uint8_t)((endpoint[s][e][3] << 1) | p);
                }
            }
        }
        for (int s = 0; s < (int)info->subset_count; s++) {
            for (int e = 0; e < 2; e++) {
                for (int c = 0; c < 3; c++)
                    endpoint[s][e][c] = bc7_unq(endpoint[s][e][c], color_bits);
                if (info->alpha_bits)
                    endpoint[s][e][3] = bc7_unq(endpoint[s][e][3], alpha_bits);
            }
        }

        for (int t = 0; t < 16; t++) {
            int subset = bc7_subset_index(info, partition, t);
            int anchor = bc7_anchor_index(info, partition, subset) == t;
            int c_bits = 0;
            int a_bits = 0;
            uint32_t cidx = bc7_index_value(b, mode, info, anchor, 1, &color_index_offset, &c_bits);
            uint32_t aidx = bc7_index_value(b, mode, info, anchor, 0, &alpha_index_offset, &a_bits);
            uint8_t cw = bc7_index_weight(cidx, c_bits);
            uint8_t aw = bc7_index_weight(aidx, a_bits);

            for (int c = 0; c < 3; c++)
                out_rgba[t * 4 + c] =
                    bc7_interp(endpoint[subset][0][c], endpoint[subset][1][c], cw);
            out_rgba[t * 4 + 3] = bc7_interp(endpoint[subset][0][3], endpoint[subset][1][3], aw);
            bc7_apply_rotation(rotation, &out_rgba[t * 4]);
        }
        return 1;
    }
}

/// @brief Software-decode a whole BC7 mip into a new RGBA8 Pixels object.
/// @details First scans every block and bails (returns NULL) if any uses the reserved mode 8, so
///          the texture stays native-only rather than partially decoded. Otherwise decodes each
///          block, clipping edge overhang. Bounds/budget-checked.
static void *textureasset3d_decode_bc7_fallback(const uint8_t *data,
                                                size_t size,
                                                uint32_t width,
                                                uint32_t height,
                                                uint64_t offset,
                                                uint64_t length) {
    void *pixels;
    uint32_t blocks_x = (width + 3u) / 4u;
    uint32_t blocks_y = (height + 3u) / 4u;
    uint64_t needed = (uint64_t)blocks_x * (uint64_t)blocks_y * 16u;
    uint64_t rgba_bytes = 0;
    if (width == 0 || height == 0)
        return NULL;
    if (!textureasset3d_rgba8_byte_count(width, height, &rgba_bytes))
        return NULL;
    if (rgba_bytes > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES)
        return NULL;
    if (length < needed || offset > (uint64_t)size || needed > (uint64_t)size - offset)
        return NULL;
    /* Only produce an RGBA fallback when every block is a software-supported BC7 mode. */
    for (uint32_t by = 0; by < blocks_y; by++)
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint8_t *block = data + offset + ((uint64_t)by * blocks_x + bx) * 16u;
            int m = bc7_block_mode(block);
            if (m >= 8)
                return NULL;
        }
    pixels = rt_pixels_new((int64_t)width, (int64_t)height);
    if (!pixels) {
        rt_trap("TextureAsset3D.LoadKTX2: BC7 Pixels fallback allocation failed");
        return NULL;
    }
    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint8_t texels[64];
            const uint8_t *block = data + offset + ((uint64_t)by * blocks_x + bx) * 16u;
            if (!rt_textureasset3d_decode_bc7_block(block, texels))
                continue;
            for (int ty = 0; ty < 4; ty++) {
                for (int tx = 0; tx < 4; tx++) {
                    uint32_t px = bx * 4u + (uint32_t)tx;
                    uint32_t py = by * 4u + (uint32_t)ty;
                    const uint8_t *c = &texels[(ty * 4 + tx) * 4];
                    uint32_t rgba;
                    if (px >= width || py >= height)
                        continue;
                    rgba = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) | ((uint32_t)c[2] << 8) |
                           (uint32_t)c[3];
                    rt_pixels_set_rgba(pixels, (int64_t)px, (int64_t)py, (int64_t)rgba);
                }
            }
        }
    }
    return pixels;
}

/// @brief Decode all mips of a BC7 texture into a Pixels array.
/// @details Returns NULL if any mip contains a reserved/unsupported block, so a texture is either
///          fully software-decoded or left native-only — never a mix.
static void **textureasset3d_decode_bc7_mips(const uint8_t *data,
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
        mip_pixels[i] = textureasset3d_decode_bc7_fallback(
            data, size, mips[i].width, mips[i].height, mips[i].offset, mips[i].length);
        if (!mip_pixels[i]) {
            /* A reserved block (or any failure) means no whole-texture RGBA fallback. */
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            return NULL;
        }
    }
    return mip_pixels;
}

/// @brief Expand a 4-bit value to 8 bits by bit replication (v<<4 | v).
static uint8_t textureasset3d_expand4(uint32_t v) {
    return (uint8_t)((v << 4) | v);
}

/// @brief Clamp a signed integer to the unsigned 8-bit range [0, 255].
static uint8_t textureasset3d_clamp_u8(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

/// @brief Sign-extend a 3-bit two's-complement value to a host int.
static int textureasset3d_sign3(uint32_t v) {
    return (v & 4u) ? (int)v - 8 : (int)v;
}

/// @brief Requantize a 16-bit unorm to 8-bit with round-to-nearest.
static uint8_t textureasset3d_unorm16_to_u8(uint32_t v) {
    return (uint8_t)((v * 255u + 32767u) / 65535u);
}

static const int8_t ETC2_ALPHA_MODIFIERS[16][8] = {
    {-3, -6, -9, -15, 2, 5, 8, 14},
    {-3, -7, -10, -13, 2, 6, 9, 12},
    {-2, -5, -8, -13, 1, 4, 7, 12},
    {-2, -4, -6, -13, 1, 3, 5, 12},
    {-3, -6, -8, -12, 2, 5, 7, 11},
    {-3, -7, -9, -11, 2, 6, 8, 10},
    {-4, -7, -8, -11, 3, 6, 7, 10},
    {-3, -5, -8, -11, 2, 4, 7, 10},
    {-2, -6, -8, -10, 1, 5, 7, 9},
    {-2, -5, -8, -10, 1, 4, 7, 9},
    {-2, -4, -8, -10, 1, 3, 7, 9},
    {-2, -5, -7, -10, 1, 4, 6, 9},
    {-3, -4, -7, -10, 2, 3, 6, 9},
    {-1, -2, -3, -10, 0, 1, 2, 9},
    {-4, -6, -8, -9, 3, 5, 7, 8},
    {-3, -5, -7, -9, 2, 4, 6, 8},
};

static const int16_t ETC2_COLOR_MODIFIERS[8][4] = {
    {-8, -2, 2, 8},
    {-17, -5, 5, 17},
    {-29, -9, 9, 29},
    {-42, -13, 13, 42},
    {-60, -18, 18, 60},
    {-80, -24, 24, 80},
    {-106, -33, 33, 106},
    {-183, -47, 47, 183},
};

/// @brief Read a 48-bit big-endian unsigned integer from the first 6 bytes of `p`.
static uint64_t textureasset3d_read_u48be(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 6; i++)
        v = (v << 8) | (uint64_t)p[i];
    return v;
}

/// @brief Two-bit pixel index for texel (x, y) of an ETC2 color block, gathered from the MSB and
///   LSB index bit-planes packed in bytes 4-7.
static int etc2_color_index(const uint8_t *c, int x, int y) {
    int bit = x * 4 + y;
    uint16_t msb = ((uint16_t)c[4] << 8) | (uint16_t)c[5];
    uint16_t lsb = ((uint16_t)c[6] << 8) | (uint16_t)c[7];
    return (int)(((msb >> bit) & 1u) << 1) | (int)((lsb >> bit) & 1u);
}

/// @brief Decode one ETC2 RGBA8/EAC 16-byte block into 16 row-major RGBA texels.
/// @details Supports the ETC2 individual and differential color modes plus EAC alpha. T/H/planar
///          color modes return 0 so the texture can remain native-only rather than partially
///          decode.
int rt_textureasset3d_decode_etc2_rgba8_block(const uint8_t *b, uint8_t *out_rgba) {
    uint8_t alpha[16];
    uint64_t alpha_bits;
    const uint8_t *c;
    uint8_t base_rgb[2][3];
    int table[2];
    int flip;
    int diff;

    if (!b || !out_rgba)
        return 0;

    alpha_bits = textureasset3d_read_u48be(b + 2);
    for (int t = 0; t < 16; t++) {
        int idx = (int)((alpha_bits >> (45 - t * 3)) & 7u);
        int table_index = b[1] & 0x0F;
        int multiplier = (b[1] >> 4) & 0x0F;
        alpha[t] = textureasset3d_clamp_u8((int)b[0] +
                                           ETC2_ALPHA_MODIFIERS[table_index][idx] * multiplier);
    }

    c = b + 8;
    diff = (c[3] & 0x02u) != 0;
    flip = (c[3] & 0x01u) != 0;
    table[0] = (c[3] >> 5) & 0x07;
    table[1] = (c[3] >> 2) & 0x07;

    if (diff) {
        int r0 = (c[0] >> 3) & 0x1F;
        int g0 = (c[1] >> 3) & 0x1F;
        int b0 = (c[2] >> 3) & 0x1F;
        int r1 = r0 + textureasset3d_sign3(c[0] & 0x07);
        int g1 = g0 + textureasset3d_sign3(c[1] & 0x07);
        int b1 = b0 + textureasset3d_sign3(c[2] & 0x07);
        if (r1 < 0 || r1 > 31 || g1 < 0 || g1 > 31 || b1 < 0 || b1 > 31)
            return 0; /* T/H/planar modes use ETC2's differential overflow selectors. */
        base_rgb[0][0] = textureasset3d_expand5((uint32_t)r0);
        base_rgb[0][1] = textureasset3d_expand5((uint32_t)g0);
        base_rgb[0][2] = textureasset3d_expand5((uint32_t)b0);
        base_rgb[1][0] = textureasset3d_expand5((uint32_t)r1);
        base_rgb[1][1] = textureasset3d_expand5((uint32_t)g1);
        base_rgb[1][2] = textureasset3d_expand5((uint32_t)b1);
    } else {
        base_rgb[0][0] = textureasset3d_expand4((c[0] >> 4) & 0x0F);
        base_rgb[1][0] = textureasset3d_expand4(c[0] & 0x0F);
        base_rgb[0][1] = textureasset3d_expand4((c[1] >> 4) & 0x0F);
        base_rgb[1][1] = textureasset3d_expand4(c[1] & 0x0F);
        base_rgb[0][2] = textureasset3d_expand4((c[2] >> 4) & 0x0F);
        base_rgb[1][2] = textureasset3d_expand4(c[2] & 0x0F);
    }

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            int texel = y * 4 + x;
            int subset = flip ? (y >= 2) : (x >= 2);
            int idx = etc2_color_index(c, x, y);
            int mod = ETC2_COLOR_MODIFIERS[table[subset]][idx];
            out_rgba[texel * 4 + 0] = textureasset3d_clamp_u8((int)base_rgb[subset][0] + mod);
            out_rgba[texel * 4 + 1] = textureasset3d_clamp_u8((int)base_rgb[subset][1] + mod);
            out_rgba[texel * 4 + 2] = textureasset3d_clamp_u8((int)base_rgb[subset][2] + mod);
            out_rgba[texel * 4 + 3] = alpha[texel];
        }
    }
    return 1;
}

/// @brief Decode one ASTC LDR 2D void-extent block into a row-major RGBA texel block.
/// @details Non-void ASTC is left native-only; the software fallback covers constant-color ASTC
///          fixtures and the required LDR void-extent baseline without adding an ASTC dependency.
int rt_textureasset3d_decode_astc_ldr_block(const uint8_t *b,
                                            int32_t block_width,
                                            int32_t block_height,
                                            uint8_t *out_rgba) {
    uint64_t low;
    uint8_t rgba[4];

    if (!b || !out_rgba || block_width <= 0 || block_height <= 0 || block_width > 12 ||
        block_height > 12)
        return 0;
    low = textureasset3d_read_u64le(b);
    if ((low & UINT64_C(0xFFF)) != UINT64_C(0xDFC))
        return 0; /* Not an LDR 2D void-extent block. */
    rgba[0] = textureasset3d_unorm16_to_u8((uint32_t)b[8] | ((uint32_t)b[9] << 8));
    rgba[1] = textureasset3d_unorm16_to_u8((uint32_t)b[10] | ((uint32_t)b[11] << 8));
    rgba[2] = textureasset3d_unorm16_to_u8((uint32_t)b[12] | ((uint32_t)b[13] << 8));
    rgba[3] = textureasset3d_unorm16_to_u8((uint32_t)b[14] | ((uint32_t)b[15] << 8));
    for (int32_t y = 0; y < block_height; y++)
        for (int32_t x = 0; x < block_width; x++) {
            uint8_t *dst = &out_rgba[((int)y * block_width + (int)x) * 4];
            dst[0] = rgba[0];
            dst[1] = rgba[1];
            dst[2] = rgba[2];
            dst[3] = rgba[3];
        }
    return 1;
}

typedef int (*textureasset3d_block_decode_fn)(const uint8_t *block,
                                              int32_t block_width,
                                              int32_t block_height,
                                              uint8_t *out_rgba);

/// @brief Block-decode adapter for ETC2 RGBA8: ignores the (fixed 4×4) block dimensions and
///   forwards to the ETC2 block decoder, matching the generic block-decode function pointer.
static int textureasset3d_decode_etc2_block_adapter(const uint8_t *block,
                                                    int32_t block_width,
                                                    int32_t block_height,
                                                    uint8_t *out_rgba) {
    (void)block_width;
    (void)block_height;
    return rt_textureasset3d_decode_etc2_rgba8_block(block, out_rgba);
}

/// @brief Block-decode adapter for ASTC LDR: forwards the block dimensions to the ASTC
///   decoder, matching the generic block-decode function pointer.
static int textureasset3d_decode_astc_block_adapter(const uint8_t *block,
                                                    int32_t block_width,
                                                    int32_t block_height,
                                                    uint8_t *out_rgba) {
    return rt_textureasset3d_decode_astc_ldr_block(block, block_width, block_height, out_rgba);
}

/// @brief Compute the total byte size and block-grid dimensions of a block-compressed image
///   from its pixel and block dimensions, with overflow checks. Outputs are written through
///   @p out_needed / @p out_blocks_x / @p out_blocks_y.
/// @return 1 on success, 0 for zero/negative parameters or on overflow.
static int textureasset3d_compressed_block_bytes(uint32_t width,
                                                 uint32_t height,
                                                 int32_t block_width,
                                                 int32_t block_height,
                                                 int32_t block_bytes,
                                                 uint64_t *out_needed,
                                                 uint32_t *out_blocks_x,
                                                 uint32_t *out_blocks_y) {
    uint32_t blocks_x;
    uint32_t blocks_y;
    uint64_t block_count;

    if (out_needed)
        *out_needed = 0;
    if (out_blocks_x)
        *out_blocks_x = 0;
    if (out_blocks_y)
        *out_blocks_y = 0;
    if (width == 0 || height == 0 || block_width <= 0 || block_height <= 0 || block_bytes <= 0)
        return 0;
    blocks_x = (width + (uint32_t)block_width - 1u) / (uint32_t)block_width;
    blocks_y = (height + (uint32_t)block_height - 1u) / (uint32_t)block_height;
    if ((uint64_t)blocks_x > UINT64_MAX / (uint64_t)blocks_y)
        return 0;
    block_count = (uint64_t)blocks_x * (uint64_t)blocks_y;
    if (block_count > UINT64_MAX / (uint64_t)block_bytes)
        return 0;
    if (out_needed)
        *out_needed = block_count * (uint64_t)block_bytes;
    if (out_blocks_x)
        *out_blocks_x = blocks_x;
    if (out_blocks_y)
        *out_blocks_y = blocks_y;
    return 1;
}

/// @brief CPU-decode one block-compressed mip level into a new RGBA Pixels object by walking
///   its block grid with @p decode and writing each 4×4 (or @p block_width×@p block_height)
///   tile. Traps with @p alloc_error on allocation failure.
/// @return The decoded Pixels object, or NULL on bad input / out-of-range level data.
static void *textureasset3d_decode_compressed_fallback(const uint8_t *data,
                                                       size_t size,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       uint64_t offset,
                                                       uint64_t length,
                                                       int32_t block_width,
                                                       int32_t block_height,
                                                       int32_t block_bytes,
                                                       textureasset3d_block_decode_fn decode,
                                                       const char *alloc_error) {
    void *pixels;
    uint32_t blocks_x;
    uint32_t blocks_y;
    uint64_t needed = 0;
    uint64_t rgba_bytes = 0;
    uint8_t texels[12 * 12 * 4];

    if (!decode)
        return NULL;
    if (!textureasset3d_compressed_block_bytes(
            width, height, block_width, block_height, block_bytes, &needed, &blocks_x, &blocks_y))
        return NULL;
    if (!textureasset3d_rgba8_byte_count(width, height, &rgba_bytes))
        return NULL;
    if (rgba_bytes > TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES)
        return NULL;
    if (length < needed || offset > (uint64_t)size || needed > (uint64_t)size - offset)
        return NULL;

    for (uint32_t by = 0; by < blocks_y; by++)
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint8_t *block =
                data + offset + ((uint64_t)by * blocks_x + bx) * (uint64_t)block_bytes;
            if (!decode(block, block_width, block_height, texels))
                return NULL;
        }

    pixels = rt_pixels_new((int64_t)width, (int64_t)height);
    if (!pixels) {
        rt_trap(alloc_error ? alloc_error
                            : "TextureAsset3D.LoadKTX2: Pixels fallback allocation failed");
        return NULL;
    }
    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint8_t *block =
                data + offset + ((uint64_t)by * blocks_x + bx) * (uint64_t)block_bytes;
            if (!decode(block, block_width, block_height, texels))
                continue;
            for (int32_t ty = 0; ty < block_height; ty++) {
                for (int32_t tx = 0; tx < block_width; tx++) {
                    uint32_t px = bx * (uint32_t)block_width + (uint32_t)tx;
                    uint32_t py = by * (uint32_t)block_height + (uint32_t)ty;
                    const uint8_t *c = &texels[((int)ty * block_width + (int)tx) * 4];
                    uint32_t rgba;
                    if (px >= width || py >= height)
                        continue;
                    rgba = ((uint32_t)c[0] << 24) | ((uint32_t)c[1] << 16) | ((uint32_t)c[2] << 8) |
                           (uint32_t)c[3];
                    rt_pixels_set_rgba(pixels, (int64_t)px, (int64_t)py, (int64_t)rgba);
                }
            }
        }
    }
    return pixels;
}

/// @brief Decode every mip level of a block-compressed texture into a newly allocated array
///   of Pixels objects (one per level) via textureasset3d_decode_compressed_fallback,
///   releasing all on any failure.
/// @return The Pixels array (caller owns), or NULL on bad input or a decode failure.
static void **textureasset3d_decode_compressed_mips(const uint8_t *data,
                                                    size_t size,
                                                    const textureasset3d_mip *mips,
                                                    int64_t mip_count,
                                                    int32_t block_width,
                                                    int32_t block_height,
                                                    int32_t block_bytes,
                                                    textureasset3d_block_decode_fn decode,
                                                    const char *alloc_error) {
    void **mip_pixels;

    if (!data || !mips || mip_count <= 0)
        return NULL;
    mip_pixels = (void **)calloc((size_t)mip_count, sizeof(void *));
    if (!mip_pixels)
        return NULL;
    for (int64_t i = 0; i < mip_count; i++) {
        mip_pixels[i] = textureasset3d_decode_compressed_fallback(data,
                                                                  size,
                                                                  mips[i].width,
                                                                  mips[i].height,
                                                                  mips[i].offset,
                                                                  mips[i].length,
                                                                  block_width,
                                                                  block_height,
                                                                  block_bytes,
                                                                  decode,
                                                                  alloc_error);
        if (!mip_pixels[i]) {
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            return NULL;
        }
    }
    return mip_pixels;
}

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
    mip_count = level_count > 0 ? (int64_t)level_count : 1;
    if (mip_count <= 0 || mip_count > (int64_t)TEXTUREASSET3D_MAX_MIP_COUNT) {
        rt_trap("TextureAsset3D.LoadKTX2: unsupported mip count");
        return NULL;
    }
    if (level_count > 0) {
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
    }

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
        }
    }

    format = textureasset3d_format_from_vk(vk_format);
    if (strcmp(format.name, "rgba8") == 0 && supercompression_scheme == 0 && level_count > 0)
        mip_pixels = textureasset3d_decode_rgba8_mips(data, size, mips, mip_count);
    /* BC3 software reference decode: also produce an RGBA8 Pixels fallback so BC3 textures render
     * on backends that cannot upload BC3 natively (native blocks are still retained below). */
    if (strcmp(format.name, "bc3") == 0 && supercompression_scheme == 0 && level_count > 0)
        mip_pixels = textureasset3d_decode_bc3_mips(data, size, mips, mip_count);
    /* BC7 software reference decode -> RGBA8 Pixels fallback; native blocks are still retained. */
    if (strcmp(format.name, "bc7") == 0 && supercompression_scheme == 0 && level_count > 0)
        mip_pixels = textureasset3d_decode_bc7_mips(data, size, mips, mip_count);
    if (strcmp(format.name, "etc2") == 0 && supercompression_scheme == 0 && level_count > 0)
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
    if (strcmp(format.name, "astc") == 0 && supercompression_scheme == 0 && level_count > 0)
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
    if (format.compressed && supercompression_scheme == 0 && level_count > 0) {
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
    asset->compressed = (format.compressed || supercompression_scheme != 0) ? 1 : 0;
    asset->block_width = format.block_width;
    asset->block_height = format.block_height;
    asset->block_bytes = format.block_bytes;
    asset->cache_identity = textureasset3d_next_cache_identity();
    asset->native_revision = 1;
    textureasset3d_set_resident_mip_range_internal(asset, 0, asset->mip_count, NULL);
    (void)api_name;
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

/// @brief Whether the texture is block-compressed (or supercompressed).
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
    if (safe_mip_count > 0 && asset->resident_mip_start > safe_mip_count)
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

#endif
