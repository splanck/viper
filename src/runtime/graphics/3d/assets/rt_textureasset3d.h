//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/assets/rt_textureasset3d.h
// Purpose: TextureAsset3D runtime surface for KTX2/precompressed texture assets.
// Key invariants:
//   - Public loaders preserve KTX2 dimensions, mip metadata, and retained native payloads.
//   - CPU fallback capability queries describe runtime decoder coverage, not GPU upload support.
// Ownership/Lifetime:
//   - TextureAsset3D handles are GC-managed runtime objects.
//   - Borrowed native mip pointers remain owned by the TextureAsset3D object.
// Links: rt_textureasset3d.c, rt_pixels.h, docs/viperlib/graphics/rendering3d.md
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"

#include <stdint.h>

#define RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE 0
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_BC3 1
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7 2
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_ASTC 3
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_ETC2 4
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_BC1 5
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_BC4 6
#define RT_TEXTUREASSET3D_NATIVE_FORMAT_BC5 7

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Decode one 16-byte BC3 (DXT5) block into 16 row-major RGBA texels (@p out_rgba is 64
///   bytes). The software reference decode used to render BC3 textures on non-BC backends.
void rt_textureasset3d_decode_bc3_block(const uint8_t *block16, uint8_t *out_rgba);

/// @brief Decode one 16-byte BC7 block into 16 row-major RGBA texels (@p out_rgba is 64 bytes).
///   Handles modes 0-7, including the partitioned modes. @return 1 if decoded, 0 for reserved
///   encodings (leaving @p out_rgba untouched).
int rt_textureasset3d_decode_bc7_block(const uint8_t *block16, uint8_t *out_rgba);

/// @brief Decode one 8-byte BC1 (DXT1) block into 16 row-major RGBA texels (@p out_rgba is 64
///   bytes). Handles the opaque 4-colour and 3-colour + punch-through-alpha modes.
void rt_textureasset3d_decode_bc1_block(const uint8_t *block8, uint8_t *out_rgba);

/// @brief Decode one 8-byte BC4 (single-channel) block into 16 row-major RGBA texels; the
///   channel replicates into R/G/B with opaque alpha for the software rendering path.
void rt_textureasset3d_decode_bc4_block(const uint8_t *block8, uint8_t *out_rgba);

/// @brief Decode one 16-byte BC5 (two-channel) block into 16 row-major RGBA texels with the
///   decoded channels in R and G (B = 255, alpha opaque).
void rt_textureasset3d_decode_bc5_block(const uint8_t *block16, uint8_t *out_rgba);

/// @brief Decode one ETC2 RGBA8/EAC 16-byte block into 16 row-major RGBA texels.
///   @return 1 if decoded, 0 if the ETC2 color mode is not software-supported.
int rt_textureasset3d_decode_etc2_rgba8_block(const uint8_t *block16, uint8_t *out_rgba);

/// @brief Decode one ASTC LDR 2D void-extent block into a row-major RGBA texel block.
///   @p block_width and @p block_height select the ASTC footprint; @p out_rgba must fit
///   block_width*block_height*4 bytes. @return 1 if decoded, 0 for non-void or HDR blocks.
int rt_textureasset3d_decode_astc_ldr_block(const uint8_t *block16,
                                            int32_t block_width,
                                            int32_t block_height,
                                            uint8_t *out_rgba);

/// @brief Load a KTX2 texture from the filesystem.
void *rt_textureasset3d_load_ktx2(rt_string path);
/// @brief Load a KTX2 texture through the runtime asset manager.
void *rt_textureasset3d_load_ktx2_asset(rt_string path);
/// @brief Internal importer bridge: decode a KTX2 byte stream into a TextureAsset3D.
void *rt_textureasset3d_load_ktx2_memory(const uint8_t *data, uint64_t size);
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
/// @brief Internal bridge: byte size of every decoded/native mip payload retained in memory.
/// @details Resident mip range controls the active upload window; retained bytes
///          reports process memory that remains allocated for fast residency changes.
int64_t rt_textureasset3d_get_retained_bytes(void *obj);
/// @brief Request a resident mip-level range. Negative inputs trap; count clamps to available mips.
void rt_textureasset3d_set_resident_mip_range(void *obj, int64_t first_mip, int64_t mip_count);

/// @brief Internal bridge: borrow the active resident RGBA8 Pixels fallback, if one was decoded.
void *rt_textureasset3d_get_pixels(void *obj);
/// @brief Internal bridge: true when load-time alpha metadata is exact for this asset.
int8_t rt_textureasset3d_alpha_metadata_known(void *obj);
/// @brief Internal bridge: true when exact metadata found any non-opaque texel.
int8_t rt_textureasset3d_has_alpha_texels(void *obj);
/// @brief Internal bridge: stable key that changes when native mip residency changes.
uint64_t rt_textureasset3d_get_native_cache_key(void *obj);
/// @brief Internal bridge: process-unique identity, stable across residency/revision changes.
uint64_t rt_textureasset3d_get_cache_identity(void *obj);
/// @brief Internal bridge: normalized native compressed format id.
int32_t rt_textureasset3d_get_native_format_id(void *obj);
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
