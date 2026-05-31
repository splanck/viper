//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_textureasset3d.h
// Purpose: TextureAsset3D runtime surface for KTX2/precompressed texture assets.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Decode one 16-byte BC3 (DXT5) block into 16 row-major RGBA texels (@p out_rgba is 64
///   bytes). The software reference decode used to render BC3 textures on non-BC backends.
void rt_textureasset3d_decode_bc3_block(const uint8_t *block16, uint8_t *out_rgba);

/// @brief Decode one 16-byte BC7 block into 16 row-major RGBA texels (@p out_rgba is 64 bytes).
///   Handles single-subset modes 4/5/6; returns 0 (leaving @p out_rgba untouched) for the
///   partitioned modes 0-3/7. @return 1 if decoded, 0 if the mode is not software-supported.
int rt_textureasset3d_decode_bc7_block(const uint8_t *block16, uint8_t *out_rgba);

/// @brief Load a KTX2 texture from the filesystem.
void *rt_textureasset3d_load_ktx2(rt_string path);
/// @brief Load a KTX2 texture through the runtime asset manager.
void *rt_textureasset3d_load_ktx2_asset(rt_string path);
/// @brief Texture width in pixels.
int64_t rt_textureasset3d_get_width(void *obj);
/// @brief Texture height in pixels.
int64_t rt_textureasset3d_get_height(void *obj);
/// @brief Number of mip levels declared by the texture.
int64_t rt_textureasset3d_get_mip_count(void *obj);
/// @brief Normalized format name: rgba8, bc3, bc7, astc, etc2, or unknown.
rt_string rt_textureasset3d_get_format(void *obj);
/// @brief True when the stored texture data is precompressed or supercompressed.
int8_t rt_textureasset3d_get_compressed(void *obj);
/// @brief First resident/requested mip level.
int64_t rt_textureasset3d_get_resident_mip_start(void *obj);
/// @brief Number of resident/requested mip levels.
int64_t rt_textureasset3d_get_resident_mip_count(void *obj);
/// @brief Declared byte size of resident/requested mip levels.
int64_t rt_textureasset3d_get_resident_bytes(void *obj);
/// @brief Request a resident mip-level range. Negative inputs trap; count clamps to available mips.
void rt_textureasset3d_set_resident_mip_range(void *obj, int64_t first_mip, int64_t mip_count);

/// @brief Internal bridge: borrow the active resident RGBA8 Pixels fallback, if one was decoded.
void *rt_textureasset3d_get_pixels(void *obj);
/// @brief Internal bridge: borrow one retained native-compressed mip payload, if available.
int rt_textureasset3d_get_native_mip_info(void *obj,
                                          int64_t mip,
                                          const uint8_t **out_data,
                                          uint64_t *out_bytes,
                                          int32_t *out_width,
                                          int32_t *out_height,
                                          int32_t *out_block_width,
                                          int32_t *out_block_height,
                                          int32_t *out_block_bytes);

#ifdef __cplusplus
}
#endif
