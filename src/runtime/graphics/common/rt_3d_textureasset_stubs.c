//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
///
/// @file rt_3d_textureasset_stubs.c
/// @brief Graphics-disabled TextureAsset3D loader, residency, and native-payload stubs.
///
/// @details This source keeps the complete TextureAsset3D ABI linkable when graphics support is
/// disabled. Stateful loader operations use the shared unavailable-operation trap, while passive
/// queries return deterministic empty fallbacks and never publish fabricated texture state.
///
// File: src/runtime/graphics/common/rt_3d_textureasset_stubs.c
// Purpose: Isolate TextureAsset3D's graphics-disabled ABI from the general 3D asset stub unit.
//
// Key invariants:
//   - Compiled only as part of the graphics-disabled runtime source set.
//   - Loaders trap consistently; queries return empty, non-owning values.
//   - Native mip output parameters are always initialized on fallback.
//
// Ownership/Lifetime:
//   - These stubs allocate no texture storage and retain no caller handles.
//
// Links: src/runtime/graphics/common/rt_graphics_stubs_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_stubs_internal.h"

/// @brief Trapping stub for filesystem `TextureAsset3D.LoadKTX2`.
/// @param path Filesystem path ignored because no graphics texture can be created.
/// @return NULL only if the shared graphics-unavailable trap recovers.
void *rt_textureasset3d_load_ktx2(rt_string path) {
    (void)path;
    rt_graphics_unavailable_("TextureAsset3D.LoadKTX2: graphics support not compiled in");
    return NULL;
}

/// @brief Trapping stub for packed-asset `TextureAsset3D.LoadKTX2Asset`.
/// @param path Packed-asset path ignored because no graphics texture can be created.
/// @return NULL only if the shared graphics-unavailable trap recovers.
void *rt_textureasset3d_load_ktx2_asset(rt_string path) {
    (void)path;
    rt_graphics_unavailable_("TextureAsset3D.LoadKTX2Asset: graphics support not compiled in");
    return NULL;
}

/// @brief Trapping stub for strict filesystem KTX2 loading.
/// @details The strict variant retains the same unavailable-operation behavior as the ordinary
/// loader; it never degrades a missing graphics backend into a fabricated empty texture.
/// @param path Filesystem path ignored by the disabled build.
/// @return NULL only if the shared graphics-unavailable trap recovers.
void *rt_textureasset3d_load_ktx2_strict(rt_string path) {
    (void)path;
    rt_graphics_unavailable_("TextureAsset3D.LoadKTX2Strict: graphics support not compiled in");
    return NULL;
}

/// @brief Trapping stub for strict packed-asset KTX2 loading.
/// @param path Packed-asset path ignored by the disabled build.
/// @return NULL only if the shared graphics-unavailable trap recovers.
void *rt_textureasset3d_load_ktx2_asset_strict(rt_string path) {
    (void)path;
    rt_graphics_unavailable_(
        "TextureAsset3D.LoadKTX2AssetStrict: graphics support not compiled in");
    return NULL;
}

/// @brief Silent fallback for `TextureAsset3D.Width`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero pixels.
int64_t rt_textureasset3d_get_width(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.Height`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero pixels.
int64_t rt_textureasset3d_get_height(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.MipCount`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero mip levels.
int64_t rt_textureasset3d_get_mip_count(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.Format`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Shared empty runtime string.
rt_string rt_textureasset3d_get_format(void *obj) {
    (void)obj;
    return rt_const_cstr("");
}

/// @brief Silent fallback for `TextureAsset3D.Compressed`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero (false).
int8_t rt_textureasset3d_get_compressed(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.Degraded`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero because no load result exists to degrade.
int8_t rt_textureasset3d_get_degraded(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.DegradedReason`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Shared empty runtime string.
rt_string rt_textureasset3d_get_degraded_reason(void *obj) {
    (void)obj;
    return rt_const_cstr("");
}

/// @brief Silent fallback for `TextureAsset3D.ResidentMipStart`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero.
int64_t rt_textureasset3d_get_resident_mip_start(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.ResidentMipCount`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero.
int64_t rt_textureasset3d_get_resident_mip_count(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.ResidentBytes`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero bytes.
int64_t rt_textureasset3d_get_resident_bytes(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for `TextureAsset3D.RetainedBytes`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero bytes because no backing allocation exists.
int64_t rt_textureasset3d_get_retained_bytes(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent no-op fallback for `TextureAsset3D.SetResidentMipRange`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @param first_mip Requested first resident mip, ignored.
/// @param mip_count Requested resident mip count, ignored.
void rt_textureasset3d_set_resident_mip_range(void *obj, int64_t first_mip, int64_t mip_count) {
    (void)obj;
    (void)first_mip;
    (void)mip_count;
}

/// @brief Silent fallback for `TextureAsset3D.Pixels`.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return NULL because no decoded Pixels object exists.
void *rt_textureasset3d_get_pixels(void *obj) {
    (void)obj;
    return NULL;
}

/// @brief Silent fallback for the backend-native texture cache identity.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return Zero, the reserved absent-cache-key value.
uint64_t rt_textureasset3d_get_native_cache_key(void *obj) {
    (void)obj;
    return 0;
}

/// @brief Silent fallback for the backend-native texture format identifier.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @return `RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE`.
int32_t rt_textureasset3d_get_native_format_id(void *obj) {
    (void)obj;
    return RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
}

/// @brief Silent fallback for one backend-native texture mip description.
/// @details Every optional output is initialized to the documented empty value, so a caller that
/// probes native payload support cannot observe stale stack data in a graphics-disabled build.
/// @param obj Texture handle ignored because disabled builds cannot create one.
/// @param mip Requested mip index, ignored.
/// @param out_data Optional destination for a NULL data pointer.
/// @param out_bytes Optional destination for a zero byte count.
/// @param out_width Optional destination for a zero width.
/// @param out_height Optional destination for a zero height.
/// @param out_block_width Optional destination for a zero compression-block width.
/// @param out_block_height Optional destination for a zero compression-block height.
/// @param out_block_bytes Optional destination for a zero compression-block byte count.
/// @return Zero because no native mip exists.
/// @note This is a silent fallback and never allocates native texture storage.
int rt_textureasset3d_get_native_mip_info(void *obj,
                                          int64_t mip,
                                          const uint8_t **out_data,
                                          uint64_t *out_bytes,
                                          int32_t *out_width,
                                          int32_t *out_height,
                                          int32_t *out_block_width,
                                          int32_t *out_block_height,
                                          int32_t *out_block_bytes) {
    (void)obj;
    (void)mip;
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
    return 0;
}
