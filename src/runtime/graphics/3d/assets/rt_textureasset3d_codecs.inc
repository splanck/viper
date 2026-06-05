//===----------------------------------------------------------------------===//
// Part of the Viper project, under the GNU GPL v3. See LICENSE.
// File: src/runtime/graphics/3d/assets/rt_textureasset3d_codecs.inc  (#included from rt_textureasset3d.c; not a standalone translation unit)
// Purpose: TextureAsset3D BC3/BC7/ETC2/ASTC software decoders and compressed fallback
//===----------------------------------------------------------------------===//
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
