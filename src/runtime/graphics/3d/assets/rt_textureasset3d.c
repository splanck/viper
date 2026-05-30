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
    textureasset3d_mip *mips;
    int64_t width;
    int64_t height;
    int64_t mip_count;
    int64_t resident_mip_start;
    int64_t resident_mip_count;
    int64_t resident_bytes;
    const char *format;
    int8_t compressed;
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
        return (textureasset3d_format_info){"rgba8", 0};
    if (vk_format == VK_FORMAT_BC3_UNORM_BLOCK || vk_format == VK_FORMAT_BC3_SRGB_BLOCK)
        return (textureasset3d_format_info){"bc3", 1};
    if (vk_format == VK_FORMAT_BC7_UNORM_BLOCK || vk_format == VK_FORMAT_BC7_SRGB_BLOCK)
        return (textureasset3d_format_info){"bc7", 1};
    if (vk_format == VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK ||
        vk_format == VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK)
        return (textureasset3d_format_info){"etc2", 1};
    if (vk_format >= VK_FORMAT_ASTC_4X4_UNORM_BLOCK && vk_format <= VK_FORMAT_ASTC_12X12_SRGB_BLOCK)
        return (textureasset3d_format_info){"astc", 1};
    return (textureasset3d_format_info){"unknown", 1};
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

    asset = (rt_textureasset3d *)rt_obj_new_i64(RT_G3D_TEXTUREASSET3D_CLASS_ID,
                                               (int64_t)sizeof(rt_textureasset3d));
    if (!asset) {
        textureasset3d_release_mip_pixels(mip_pixels, mip_count);
        free(mips);
        rt_trap("TextureAsset3D.LoadKTX2: allocation failed");
        return NULL;
    }
    memset(asset, 0, sizeof(*asset));
    rt_obj_set_finalizer(asset, textureasset3d_finalize);
    asset->mip_pixels = mip_pixels;
    asset->mips = mips;
    asset->width = (int64_t)pixel_width;
    asset->height = (int64_t)pixel_height;
    asset->mip_count = mip_count;
    asset->format = format.name;
    asset->compressed = (format.compressed || supercompression_scheme != 0) ? 1 : 0;
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

#endif
