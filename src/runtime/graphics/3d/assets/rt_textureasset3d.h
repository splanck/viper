//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
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
// Links: rt_textureasset3d.c, rt_pixels.h, docs/zannalib/graphics/rendering3d.md
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

/** @name TextureAsset3D CPU fallback quality
 * @{ */
#define RT_TEXTUREASSET3D_CPU_SUPPORT_NONE 0
#define RT_TEXTUREASSET3D_CPU_SUPPORT_PARTIAL 1
#define RT_TEXTUREASSET3D_CPU_SUPPORT_FULL 2
/** @} */

/** @name Backend capability bits associated with native TextureAsset3D formats
 * @details These values are the authoritative texture subset of Canvas3D's backend bitmask.
 *          Canvas3D aliases its corresponding public constants to these definitions so the
 *          format table and backend query layer cannot drift.
 * @{ */
#define RT_TEXTUREASSET3D_BACKEND_CAP_NONE 0LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_BC7 0x10000LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_ASTC 0x20000LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_ETC2 0x40000LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_BC1 0x4000000LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_BC3 0x8000000LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_BC4 0x10000000LL
#define RT_TEXTUREASSET3D_BACKEND_CAP_BC5 0x20000000LL

/** @} */

/// @brief Read-only description of one authoritative KTX2 Vulkan-format capability row.
/// @details Inclusive Vulkan ranges contain only formats with identical normalized name, block
///          layout, decoder behavior, and native upload family. The @c cpu_support field is one of
///          RT_TEXTUREASSET3D_CPU_SUPPORT_*; @c native_format_id and @c backend_capability_bit are
///          zero when no backend consumes the original blocks directly.
typedef struct rt_textureasset3d_format_capability {
    uint32_t vk_format_first;       ///< First Vulkan format value in this inclusive row.
    uint32_t vk_format_last;        ///< Last Vulkan format value in this inclusive row.
    const char *name;               ///< Static normalized runtime format name.
    int8_t compressed;              ///< Nonzero for block-compressed level bytes.
    int8_t cpu_support;             ///< NONE, PARTIAL, or FULL CPU fallback quality.
    int32_t block_width;            ///< Texel width of one stored block.
    int32_t block_height;           ///< Texel height of one stored block.
    int32_t block_bytes;            ///< Stored bytes per block.
    int32_t native_format_id;       ///< RT_TEXTUREASSET3D_NATIVE_FORMAT_* or NONE.
    int64_t backend_capability_bit; ///< Matching RT_TEXTUREASSET3D_BACKEND_CAP_* bit or zero.
} rt_textureasset3d_format_capability;

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
/// @brief Strict filesystem loader that rejects a CPU decode requiring checker substitution.
/// @details Structural validation and supported successful inputs match @ref
///          rt_textureasset3d_load_ktx2. Unlike that compatibility loader, a recognized compressed
///          format whose CPU fallback cannot be decoded records a recoverable unsupported-data
///          error and returns NULL instead of publishing a degraded checker asset.
/// @param path Non-empty UTF-8 filesystem path.
/// @return New GC-managed TextureAsset3D, or NULL with the last asset error populated.
void *rt_textureasset3d_load_ktx2_strict(rt_string path);
/// @brief Load a KTX2 texture through the runtime asset manager.
void *rt_textureasset3d_load_ktx2_asset(rt_string path);
/// @brief Strict asset-registry loader; see @ref rt_textureasset3d_load_ktx2_strict.
/// @param path Non-NULL packed-asset path.
/// @return New GC-managed non-degraded TextureAsset3D, or NULL with an asset error.
void *rt_textureasset3d_load_ktx2_asset_strict(rt_string path);
/// @brief Internal importer bridge: decode a KTX2 byte stream into a TextureAsset3D.
void *rt_textureasset3d_load_ktx2_memory(const uint8_t *data, uint64_t size);
/// @brief Internal strict importer bridge for a caller-owned KTX2 byte stream.
/// @details The byte span is borrowed for the duration of the call and never retained. Checker
///          substitution is forbidden exactly as in @ref rt_textureasset3d_load_ktx2_strict.
/// @param data Non-NULL complete KTX2 bytes.
/// @param size Byte length, bounded by the runtime texture-file limit.
/// @return New GC-managed non-degraded asset, or NULL after recording a recoverable asset error.
void *rt_textureasset3d_load_ktx2_memory_strict(const uint8_t *data, uint64_t size);
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
/// @brief True when permissive loading substituted a visible CPU checker fallback.
/// @param obj TextureAsset3D handle; invalid handles report false.
/// @return 1 for a degraded checker asset, otherwise 0.
int8_t rt_textureasset3d_get_degraded(void *obj);
/// @brief Stable machine-readable degradation reason, or the empty string.
/// @details The current checker-substitution reason is `cpu_decode_failed`. The returned runtime
///          string borrows immutable storage and requires no caller release.
/// @param obj TextureAsset3D handle; invalid handles report an empty string.
/// @return Runtime string containing the stable reason token.
rt_string rt_textureasset3d_get_degraded_reason(void *obj);
/// @brief First resident/requested mip level.
int64_t rt_textureasset3d_get_resident_mip_start(void *obj);
/// @brief Number of resident/requested mip levels.
int64_t rt_textureasset3d_get_resident_mip_count(void *obj);
/// @brief Declared byte size of resident/requested mip levels.
int64_t rt_textureasset3d_get_resident_bytes(void *obj);
/// @brief Byte size of canonical source backing plus decoded resident mip allocations.
/// @details Native upload views borrow the canonical backing and are not double-counted. Moving
///          the resident range releases decoded Pixels outside the window, so this value falls
///          after eviction while retaining enough immutable source bytes to reconstruct them.
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
/// @brief Return the row-derived Canvas3D backend capability bit for an asset's native format.
/// @details This is the single bridge used by backend upload-support queries; it returns zero for
///          uncompressed, CPU-only, invalid, or unknown formats.
/// @param obj TextureAsset3D handle.
/// @return One RT_TEXTUREASSET3D_BACKEND_CAP_* bit, or zero.
int64_t rt_textureasset3d_get_native_capability_bit(void *obj);
/// @brief Return the number of immutable rows in the authoritative format-capability table.
/// @return Positive row count, stable for the running runtime build.
int64_t rt_textureasset3d_get_format_capability_count(void);
/// @brief Copy one authoritative format-capability row for diagnostics and contract tests.
/// @param index Zero-based row index.
/// @param out_capability Caller-owned output; zeroed before validation.
/// @return 1 when @p index is valid and the row was copied, otherwise 0.
int8_t rt_textureasset3d_get_format_capability(int64_t index,
                                               rt_textureasset3d_format_capability *out_capability);
/// @brief Report NONE/PARTIAL/FULL CPU fallback quality for a normalized format name.
/// @details Duplicate table rows such as ASTC footprints must agree; the helper returns their
///          common quality. Unknown and NULL names return RT_TEXTUREASSET3D_CPU_SUPPORT_NONE.
/// @param format_name NUL-terminated normalized format name.
/// @return One RT_TEXTUREASSET3D_CPU_SUPPORT_* value.
int32_t rt_textureasset3d_cpu_format_support_level(const char *format_name);
/// @brief Legacy boolean CPU-support query derived from the authoritative quality table.
/// @return 1 for PARTIAL or FULL coverage, otherwise 0.
int8_t rt_textureasset3d_cpu_supports_format(const char *format_name);
/// @brief Return true when the KTX2 parser and CPU fallback layer are compiled in.
int8_t rt_textureasset3d_cpu_supports_ktx2(void);

/// @brief Reset allocation telemetry for the next KTX2 supercompression parse.
/// @details CTest-only hook; it does not replace allocators or affect production limits.
void rt_textureasset3d_test_reset_supercompression_allocation_telemetry(void);
/// @brief Return final canonical backing bytes allocated by the latest supercompressed parse.
/// @return Saturating byte count, or zero when no supercompressed parse followed the last reset.
uint64_t rt_textureasset3d_test_get_supercompression_final_bytes(void);
/// @brief Return peak live final-destination bytes during the latest supercompressed parse.
/// @details A direct-to-backing implementation has a peak equal to final canonical bytes rather
///          than final bytes plus a redundant whole-level intermediate.
/// @return Saturating peak byte count.
uint64_t rt_textureasset3d_test_get_supercompression_peak_bytes(void);
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
