//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_textureasset3d.c
// Purpose: KTX2/precompressed TextureAsset3D metadata and RGBA8 fallback loader.
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_textureasset3d.h"

#include "rt_asset.h"
#include "rt_graphics3d_ids.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_trap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTUREASSET3D_KTX2_HEADER_SIZE 80u
#define TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE 24u
#define TEXTUREASSET3D_MAX_FILE_BYTES (256u * 1024u * 1024u)
#define TEXTUREASSET3D_MAX_RGBA8_FALLBACK_BYTES (256u * 1024u * 1024u)

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

typedef struct {
    const char *name;
    int8_t compressed;
    int32_t block_width;
    int32_t block_height;
    int32_t block_bytes;
} textureasset3d_format_info;

typedef struct {
    uint64_t offset;
    uint64_t length;
    uint64_t uncompressed_length;
    uint32_t width;
    uint32_t height;
} textureasset3d_mip;

typedef struct {
    void *vptr;
    void *pixels;
    void **mip_pixels;
    uint8_t **mip_payloads;
    textureasset3d_mip *mips;
    int64_t width;
    int64_t height;
    int64_t mip_count;
    int64_t resident_mip_start;
    int64_t resident_mip_count;
    int64_t resident_bytes;
    const char *format;
    int8_t compressed;
    int32_t block_width;
    int32_t block_height;
    int32_t block_bytes;
} rt_textureasset3d;

static const uint8_t ktx2_identifier[12] = {
    0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A,
};

static rt_textureasset3d *textureasset3d_checked(void *obj) {
    return (rt_textureasset3d *)rt_g3d_checked_or_null(obj, RT_G3D_TEXTUREASSET3D_CLASS_ID);
}

static void textureasset3d_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void textureasset3d_finalize(void *obj) {
    rt_textureasset3d *asset = (rt_textureasset3d *)obj;
    if (!asset)
        return;
    asset->pixels = NULL;
    if (asset->mip_pixels) {
        for (int64_t i = 0; i < asset->mip_count; i++)
            textureasset3d_release_ref(&asset->mip_pixels[i]);
        free(asset->mip_pixels);
        asset->mip_pixels = NULL;
    }
    if (asset->mip_payloads) {
        for (int64_t i = 0; i < asset->mip_count; i++)
            free(asset->mip_payloads[i]);
        free(asset->mip_payloads);
        asset->mip_payloads = NULL;
    }
    free(asset->mips);
    asset->mips = NULL;
}

static uint32_t textureasset3d_read_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t textureasset3d_read_u64le(const uint8_t *p) {
    uint64_t lo = textureasset3d_read_u32le(p);
    uint64_t hi = textureasset3d_read_u32le(p + 4);
    return lo | (hi << 32);
}

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
    if (vk_format >= VK_FORMAT_ASTC_4X4_UNORM_BLOCK && vk_format <= VK_FORMAT_ASTC_12X12_SRGB_BLOCK) {
        static const int8_t astc_dims[][2] = {
            {4, 4},  {4, 4},  {5, 4},  {5, 4},  {5, 5},   {5, 5},  {6, 5},
            {6, 5},  {6, 6},  {6, 6},  {8, 5},  {8, 5},   {8, 6},  {8, 6},
            {8, 8},  {8, 8},  {10, 5}, {10, 5}, {10, 6},  {10, 6}, {10, 8},
            {10, 8}, {10, 10}, {10, 10}, {12, 10}, {12, 10}, {12, 12}, {12, 12},
        };
        uint32_t index = vk_format - VK_FORMAT_ASTC_4X4_UNORM_BLOCK;
        if (index < (uint32_t)(sizeof(astc_dims) / sizeof(astc_dims[0]))) {
            return (textureasset3d_format_info){"astc",
                                                1,
                                                astc_dims[index][0],
                                                astc_dims[index][1],
                                                16};
        }
    }
    return (textureasset3d_format_info){"unknown", 1, 0, 0, 0};
}

static uint32_t textureasset3d_mip_dimension(uint32_t base, uint32_t level) {
    uint32_t value = base >> level;
    return value > 0 ? value : 1;
}

static int textureasset3d_set_resident_mip_range_internal(
    rt_textureasset3d *asset, int64_t first_mip, int64_t mip_count, const char *api_name) {
    uint64_t total = 0;

    if (!asset)
        return 0;
    if (first_mip < 0 || mip_count < 0) {
        rt_trap(api_name ? api_name : "TextureAsset3D.SetResidentMipRange: negative mip range");
        return 0;
    }
    if (mip_count == 0 || first_mip >= asset->mip_count) {
        asset->resident_mip_start = first_mip;
        asset->resident_mip_count = 0;
        asset->resident_bytes = 0;
        asset->pixels = NULL;
        return 1;
    }
    if (mip_count > asset->mip_count - first_mip)
        mip_count = asset->mip_count - first_mip;

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
    asset->pixels = (asset->mip_pixels && first_mip < asset->mip_count)
                        ? asset->mip_pixels[first_mip]
                        : NULL;
    return 1;
}

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

static int textureasset3d_rgba8_byte_count(
    uint32_t width, uint32_t height, uint64_t *out_byte_count) {
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

static void *textureasset3d_decode_rgba8_fallback(
    const uint8_t *data, size_t size, uint32_t width, uint32_t height, uint64_t offset, uint64_t length) {
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
            uint32_t rgba =
                ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
                (uint32_t)p[3];
            rt_pixels_set_rgba(pixels, (int64_t)x, (int64_t)y, (int64_t)rgba);
        }
    }
    return pixels;
}

static void textureasset3d_release_mip_pixels(void **mip_pixels, int64_t mip_count) {
    if (!mip_pixels)
        return;
    for (int64_t i = 0; i < mip_count; i++)
        textureasset3d_release_ref(&mip_pixels[i]);
    free(mip_pixels);
}

static void textureasset3d_release_mip_payloads(uint8_t **mip_payloads, int64_t mip_count) {
    if (!mip_payloads)
        return;
    for (int64_t i = 0; i < mip_count; i++)
        free(mip_payloads[i]);
    free(mip_payloads);
}

static uint8_t **textureasset3d_copy_native_mip_payloads(
    const uint8_t *data, size_t size, const textureasset3d_mip *mips, int64_t mip_count) {
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

static void **textureasset3d_decode_rgba8_mips(
    const uint8_t *data, size_t size, const textureasset3d_mip *mips, int64_t mip_count) {
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
static uint8_t textureasset3d_expand5(uint32_t v) {
    return (uint8_t)((v << 3) | (v >> 2));
}
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

static void **textureasset3d_decode_bc3_mips(
    const uint8_t *data, size_t size, const textureasset3d_mip *mips, int64_t mip_count) {
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
 * BC7 software reference decode (single-subset modes 4/5/6).
 *
 * BC7 packs a 4x4 RGBA block into 16 bytes across eight "modes" that trade
 * subset count, endpoint precision, and index precision. Modes 4/5/6 use a
 * single subset (every texel is subset 0), so they need no partition or anchor
 * tables — only subset 0's implicit rule that texel 0's index stores one fewer
 * bit (high bit == 0). We decode those three (they dominate high-quality RGBA
 * encodes; mode 6 especially) and decline a fallback for the partitioned modes
 * 0-3/7, which keep their native blocks for GPU upload.
 * ------------------------------------------------------------------------- */

/* Interpolation weights (numerator over 64) for 2-, 3-, and 4-bit indices. */
static const uint8_t BC7_W2[4] = {0, 21, 43, 64};
static const uint8_t BC7_W3[8] = {0, 9, 18, 27, 37, 46, 55, 64};
static const uint8_t BC7_W4[16] = {0, 4,  9,  13, 17, 21, 26, 30,
                                   34, 38, 43, 47, 51, 55, 60, 64};

typedef struct {
    const uint8_t *p;
    int pos; /* next bit to read, LSB-first within each byte */
} bc7_reader;

static uint32_t bc7_get(bc7_reader *r, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        v |= (uint32_t)((r->p[r->pos >> 3] >> (r->pos & 7)) & 1u) << i;
        r->pos++;
    }
    return v;
}

/* Expand a `bits`-bit endpoint component to 8 bits by bit replication. */
static uint8_t bc7_unq(uint32_t v, int bits) {
    return (uint8_t)((v << (8 - bits)) | (v >> (2 * bits - 8)));
}

static uint8_t bc7_interp(uint8_t e0, uint8_t e1, uint8_t w) {
    return (uint8_t)(((uint32_t)e0 * (64u - w) + (uint32_t)e1 * w + 32u) >> 6);
}

/* Modes 4/5 may swap the alpha channel with one color channel after decode. */
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

/* The BC7 mode is the index of the first set bit in byte 0 (LSB-first); 8 = invalid. */
static int bc7_block_mode(const uint8_t *b) {
    int mode = 0;
    while (mode < 8 && !((b[0] >> mode) & 1u))
        mode++;
    return mode;
}

/// @brief Decode one 16-byte BC7 block into 16 row-major RGBA texels (@p out_rgba is 64 bytes).
///   Handles single-subset modes 4/5/6; returns 0 for partitioned modes (0-3/7) and leaves
///   @p out_rgba untouched. Non-static so the unit test can verify against constructed blocks.
int rt_textureasset3d_decode_bc7_block(const uint8_t *b, uint8_t *out_rgba) {
    bc7_reader r;
    int mode;
    if (!b || !out_rgba)
        return 0;
    r.p = b;
    r.pos = 0;
    mode = 0;
    while (mode < 8 && bc7_get(&r, 1) == 0)
        mode++;
    if (mode != 4 && mode != 5 && mode != 6)
        return 0; /* partitioned / invalid: no software fallback */

    if (mode == 6) {
        /* 1 subset, RGBA 7 bits + 1 p-bit per endpoint (8-bit effective), 4-bit indices. */
        uint32_t c0[4], c1[4], p0, p1;
        uint8_t e0[4], e1[4];
        for (int c = 0; c < 4; c++) {
            c0[c] = bc7_get(&r, 7);
            c1[c] = bc7_get(&r, 7);
        }
        p0 = bc7_get(&r, 1);
        p1 = bc7_get(&r, 1);
        for (int c = 0; c < 4; c++) {
            e0[c] = (uint8_t)((c0[c] << 1) | p0);
            e1[c] = (uint8_t)((c1[c] << 1) | p1);
        }
        for (int t = 0; t < 16; t++) {
            uint8_t w = BC7_W4[bc7_get(&r, t == 0 ? 3 : 4)];
            for (int c = 0; c < 4; c++)
                out_rgba[t * 4 + c] = bc7_interp(e0[c], e1[c], w);
        }
        return 1;
    }

    if (mode == 5) {
        /* 1 subset, RGB 7 bits + A 8 bits, 2-bit rotation, separate 2-bit color/alpha indices. */
        uint32_t rot = bc7_get(&r, 2);
        uint32_t cr0[3], cr1[3], a0, a1;
        uint8_t e0[4], e1[4], cidx[16], aidx[16];
        for (int c = 0; c < 3; c++) {
            cr0[c] = bc7_get(&r, 7);
            cr1[c] = bc7_get(&r, 7);
        }
        a0 = bc7_get(&r, 8);
        a1 = bc7_get(&r, 8);
        for (int c = 0; c < 3; c++) {
            e0[c] = bc7_unq(cr0[c], 7);
            e1[c] = bc7_unq(cr1[c], 7);
        }
        e0[3] = (uint8_t)a0;
        e1[3] = (uint8_t)a1;
        for (int t = 0; t < 16; t++)
            cidx[t] = (uint8_t)bc7_get(&r, t == 0 ? 1 : 2);
        for (int t = 0; t < 16; t++)
            aidx[t] = (uint8_t)bc7_get(&r, t == 0 ? 1 : 2);
        for (int t = 0; t < 16; t++) {
            uint8_t cw = BC7_W2[cidx[t]];
            out_rgba[t * 4 + 0] = bc7_interp(e0[0], e1[0], cw);
            out_rgba[t * 4 + 1] = bc7_interp(e0[1], e1[1], cw);
            out_rgba[t * 4 + 2] = bc7_interp(e0[2], e1[2], cw);
            out_rgba[t * 4 + 3] = bc7_interp(e0[3], e1[3], BC7_W2[aidx[t]]);
            bc7_apply_rotation(rot, &out_rgba[t * 4]);
        }
        return 1;
    }

    /* mode == 4: 1 subset, RGB 5 bits + A 6 bits, 2-bit rotation, 1-bit index selector,
     * a 2-bit and a 3-bit index set (idxMode picks which drives color vs alpha). */
    {
        uint32_t rot = bc7_get(&r, 2);
        uint32_t idx_mode = bc7_get(&r, 1);
        uint32_t cr0[3], cr1[3], a0, a1;
        uint8_t e0[4], e1[4], i2[16], i3[16];
        for (int c = 0; c < 3; c++) {
            cr0[c] = bc7_get(&r, 5);
            cr1[c] = bc7_get(&r, 5);
        }
        a0 = bc7_get(&r, 6);
        a1 = bc7_get(&r, 6);
        for (int c = 0; c < 3; c++) {
            e0[c] = bc7_unq(cr0[c], 5);
            e1[c] = bc7_unq(cr1[c], 5);
        }
        e0[3] = bc7_unq(a0, 6);
        e1[3] = bc7_unq(a1, 6);
        for (int t = 0; t < 16; t++)
            i2[t] = (uint8_t)bc7_get(&r, t == 0 ? 1 : 2);
        for (int t = 0; t < 16; t++)
            i3[t] = (uint8_t)bc7_get(&r, t == 0 ? 2 : 3);
        for (int t = 0; t < 16; t++) {
            uint8_t cw = idx_mode ? BC7_W3[i3[t]] : BC7_W2[i2[t]];
            uint8_t aw = idx_mode ? BC7_W2[i2[t]] : BC7_W3[i3[t]];
            out_rgba[t * 4 + 0] = bc7_interp(e0[0], e1[0], cw);
            out_rgba[t * 4 + 1] = bc7_interp(e0[1], e1[1], cw);
            out_rgba[t * 4 + 2] = bc7_interp(e0[2], e1[2], cw);
            out_rgba[t * 4 + 3] = bc7_interp(e0[3], e1[3], aw);
            bc7_apply_rotation(rot, &out_rgba[t * 4]);
        }
        return 1;
    }
}

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
    /* Only produce an RGBA fallback when every block is a software-supported mode; otherwise the
     * texture keeps its native blocks (partitioned modes have no software path here). */
    for (uint32_t by = 0; by < blocks_y; by++)
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint8_t *block = data + offset + ((uint64_t)by * blocks_x + bx) * 16u;
            int m = bc7_block_mode(block);
            if (m != 4 && m != 5 && m != 6)
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

static void **textureasset3d_decode_bc7_mips(
    const uint8_t *data, size_t size, const textureasset3d_mip *mips, int64_t mip_count) {
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
            /* A partitioned-mode mip (or any failure) means no whole-texture RGBA fallback. */
            textureasset3d_release_mip_pixels(mip_pixels, mip_count);
            return NULL;
        }
    }
    return mip_pixels;
}

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

    if (pixel_width == 0 || pixel_height == 0) {
        rt_trap("TextureAsset3D.LoadKTX2: invalid dimensions");
        return NULL;
    }
    if (pixel_depth > 1 || layer_count > 1 || face_count != 1) {
        rt_trap("TextureAsset3D.LoadKTX2: unsupported KTX2 dimensionality");
        return NULL;
    }
    mip_count = level_count > 0 ? (int64_t)level_count : 1;
    if (level_count > 0) {
        if ((uint64_t)level_count > (UINT64_MAX - TEXTUREASSET3D_KTX2_HEADER_SIZE) /
                                        TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE) {
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
            const uint8_t *entry =
                data + TEXTUREASSET3D_KTX2_HEADER_SIZE + (uint64_t)i * TEXTUREASSET3D_KTX2_LEVEL_ENTRY_SIZE;
            mips[i].offset = textureasset3d_read_u64le(entry);
            mips[i].length = textureasset3d_read_u64le(entry + 8u);
            mips[i].uncompressed_length = textureasset3d_read_u64le(entry + 16u);
            if (mips[i].length > 0 &&
                (mips[i].offset > (uint64_t)size || mips[i].length > (uint64_t)size - mips[i].offset)) {
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
    /* BC7 software reference decode (single-subset modes 4/5/6) -> RGBA8 Pixels fallback; native
     * blocks are still retained below. Partitioned modes leave mip_pixels NULL (native-only). */
    if (strcmp(format.name, "bc7") == 0 && supercompression_scheme == 0 && level_count > 0)
        mip_pixels = textureasset3d_decode_bc7_mips(data, size, mips, mip_count);
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
    asset->format = format.name;
    asset->compressed = (format.compressed || supercompression_scheme != 0) ? 1 : 0;
    asset->block_width = format.block_width;
    asset->block_height = format.block_height;
    asset->block_bytes = format.block_bytes;
    textureasset3d_set_resident_mip_range_internal(asset, 0, asset->mip_count, NULL);
    (void)api_name;
    return asset;
}

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

int64_t rt_textureasset3d_get_width(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->width : 0;
}

int64_t rt_textureasset3d_get_height(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->height : 0;
}

int64_t rt_textureasset3d_get_mip_count(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->mip_count : 0;
}

rt_string rt_textureasset3d_get_format(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return rt_const_cstr((asset && asset->format) ? asset->format : "");
}

int8_t rt_textureasset3d_get_compressed(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return (asset && asset->compressed) ? 1 : 0;
}

int64_t rt_textureasset3d_get_resident_mip_start(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->resident_mip_start : 0;
}

int64_t rt_textureasset3d_get_resident_mip_count(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->resident_mip_count : 0;
}

int64_t rt_textureasset3d_get_resident_bytes(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->resident_bytes : 0;
}

void rt_textureasset3d_set_resident_mip_range(void *obj, int64_t first_mip, int64_t mip_count) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    textureasset3d_set_resident_mip_range_internal(
        asset, first_mip, mip_count, "TextureAsset3D.SetResidentMipRange: negative mip range");
}

void *rt_textureasset3d_get_pixels(void *obj) {
    rt_textureasset3d *asset = textureasset3d_checked(obj);
    return asset ? asset->pixels : NULL;
}

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
    if (!asset || !asset->compressed || !asset->mip_payloads || mip < 0 || mip >= asset->mip_count ||
        !asset->mip_payloads[mip] || asset->mips[mip].length == 0 || asset->block_width <= 0 ||
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
