//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/backend/vgfx3d_backend_utils.c
// Purpose: Cross-backend utility helpers shared between the Metal/OpenGL/D3D11
//   3D backends — pixel/cubemap unpack to RGBA8, generation tracking,
//   row-flip, normal-matrix derivation from a model matrix, and 4×4 inverse.
//
// Key invariants:
//   - Pixels payloads are 0xRRGGBBAA in `uint32_t`, row-major, top-left origin.
//   - Normal matrix is the inverse-transpose of the model matrix's upper 3×3,
//     stored in the upper-left 3×3 of the 4×4 output (M[15] = 1, rest 0).
//   - Cubemap generation mixes a stable cubemap identity plus all six face
//     generations, enabling cheap "did anything change?" checks for backend
//     caches even when the allocator reuses object addresses.
//
// Links: vgfx3d_backend_utils.h, vgfx3d_backend_*.c (per-API implementations)
//
//===----------------------------------------------------------------------===//

#include "vgfx3d_backend_utils.h"

#include "rt_canvas3d.h"
#include "rt_textureasset3d.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Determinant magnitude below which a 3x3/4x4 matrix is treated as singular.
 * Shared by the normal-matrix derivation and the 4x4 inverse so both agree on
 * the bound; the value matches the inverse's long-standing threshold and stays
 * permissive enough not to drop the rotation of legitimately small-scaled
 * (sub-0.01) objects, whose normal matrix is renormalized after derivation. */
static const float kVgfx3dSingularDetEps = 1e-12f;

#define VGFX3D_BACKEND_MAX_CUBEMAP_FACE_SIZE 32768

typedef struct {
    int64_t w;
    int64_t h;
    uint32_t *data;
    uint64_t generation;
    uint64_t cache_identity;
} vgfx3d_pixels_view_t;

typedef struct {
    void *vptr;
    void *faces[6];
    int64_t face_size;
    uint64_t cache_identity;
} vgfx3d_cubemap_view_t;

/// @brief Read the monotonic generation counter on a Pixels object.
/// Returns 0 for null. Backends compare against last-seen generation to detect
/// when a GPU texture upload is required.
uint64_t vgfx3d_get_pixels_generation(const void *pixels_ptr) {
    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    if (!pv)
        return 0;
    return pv->generation;
}

/// @brief Combine a Pixels object's identity + content generation into one cache key.
/// @details Backends cache GPU-side texture uploads keyed by this value.
///   The key must change whenever *either* the Pixels object is a
///   different identity (recreated with the same address by the
///   allocator — common after GC free+new) *or* the existing object's
///   content mutates (generation bump from a Set / Fill / Paste).
///   Seed is the FNV-1a 64-bit offset basis; the mixing step uses the
///   golden-ratio increment from Boost's hash_combine so unrelated
///   (identity, generation) pairs distribute uniformly across the output
///   space. Null pointer returns 0 as a distinguishable "no Pixels"
///   sentinel — 0 cannot otherwise appear because the mixing always
///   XORs in the seed.
uint64_t vgfx3d_get_pixels_cache_key(const void *pixels_ptr) {
    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    uint64_t signature = 1469598103934665603ull;

    if (!pv)
        return 0;

    signature ^= pv->cache_identity + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);
    signature ^= pv->generation + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);
    return signature;
}

/// @brief Whether a texture asset's native compressed format can be uploaded under @p native_caps.
/// @details Maps the asset's native format id (BC7/ASTC/ETC2) to the matching backend capability
/// bit;
///          returns 0 for uncompressed assets or formats the backend cannot upload natively.
int vgfx3d_textureasset_native_supported(void *asset, int64_t native_caps) {
    int32_t format_id;

    if (!asset || rt_textureasset3d_get_native_cache_key(asset) == 0)
        return 0;
    format_id = rt_textureasset3d_get_native_format_id(asset);
    if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_BC7)
        return (native_caps & RT_CANVAS3D_BACKEND_CAP_BC7) != 0;
    if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ASTC)
        return (native_caps & RT_CANVAS3D_BACKEND_CAP_ASTC) != 0;
    if (format_id == RT_TEXTUREASSET3D_NATIVE_FORMAT_ETC2)
        return (native_caps & RT_CANVAS3D_BACKEND_CAP_ETC2) != 0;
    return 0;
}

/// @brief Fill @p out_mip with the native compressed payload for the resident mip at @p
/// relative_mip.
/// @details @p relative_mip is offset from the asset's first resident mip. Validates that the
/// payload,
///          dimensions, block geometry, and format are all usable before reporting success.
/// @return 1 with @p out_mip populated, or 0 (out_mip zeroed) if out of range or incomplete.
int vgfx3d_textureasset_get_native_resident_mip(void *asset,
                                                int64_t relative_mip,
                                                vgfx3d_native_texture_mip_t *out_mip) {
    int64_t first;
    int64_t count;

    if (out_mip)
        memset(out_mip, 0, sizeof(*out_mip));
    if (!asset || !out_mip || relative_mip < 0)
        return 0;
    first = rt_textureasset3d_get_resident_mip_start(asset);
    count = rt_textureasset3d_get_resident_mip_count(asset);
    return vgfx3d_textureasset_get_native_snapshot_mip(
        asset, first, count, relative_mip, out_mip);
}

/// @brief Borrow one native compressed mip from an explicit resident-window snapshot.
/// @details Draw commands record the resident mip window they observed at queue time. Backends use
///          this helper during deferred submission so native compressed uploads cannot switch to a
///          different mip window if streaming code mutates the TextureAsset3D later in the frame.
int vgfx3d_textureasset_get_native_snapshot_mip(void *asset,
                                                int64_t first_mip,
                                                int64_t mip_count,
                                                int64_t relative_mip,
                                                vgfx3d_native_texture_mip_t *out_mip) {
    if (out_mip)
        memset(out_mip, 0, sizeof(*out_mip));
    if (!asset || !out_mip || first_mip < 0 || mip_count <= 0 || relative_mip < 0 ||
        relative_mip >= mip_count || relative_mip > INT64_MAX - first_mip)
        return 0;
    if (!rt_textureasset3d_get_native_mip_info(asset,
                                               first_mip + relative_mip,
                                               &out_mip->data,
                                               &out_mip->bytes,
                                               &out_mip->width,
                                               &out_mip->height,
                                               &out_mip->block_width,
                                               &out_mip->block_height,
                                               &out_mip->block_bytes))
        return 0;
    out_mip->format_id = rt_textureasset3d_get_native_format_id(asset);
    return out_mip->data && out_mip->bytes > 0 && out_mip->width > 0 && out_mip->height > 0 &&
           out_mip->block_width > 0 && out_mip->block_height > 0 && out_mip->block_bytes > 0 &&
           out_mip->format_id != RT_TEXTUREASSET3D_NATIVE_FORMAT_NONE;
}

/// @brief Native bytes still to upload from the current mip/block-row cursor onward.
/// @details Counts the current resident mip from @p next_block_row and later mips from the
///          beginning, saturating at UINT64_MAX. Returns 0 when no upload is in progress, so
///          callers can budget streaming work per frame.
uint64_t vgfx3d_textureasset_pending_native_bytes(void *asset,
                                                  int64_t next_relative_mip,
                                                  int32_t next_block_row,
                                                  int upload_in_progress) {
    int64_t first;
    int64_t count;

    first = asset ? rt_textureasset3d_get_resident_mip_start(asset) : 0;
    count = asset ? rt_textureasset3d_get_resident_mip_count(asset) : 0;
    return vgfx3d_textureasset_pending_native_snapshot_bytes(
        asset, first, count, next_relative_mip, next_block_row, upload_in_progress);
}

/// @brief Compute pending native upload bytes inside an explicit resident-window snapshot.
uint64_t vgfx3d_textureasset_pending_native_snapshot_bytes(void *asset,
                                                           int64_t first_mip,
                                                           int64_t mip_count,
                                                           int64_t next_relative_mip,
                                                           int32_t next_block_row,
                                                           int upload_in_progress) {
    uint64_t total = 0;

    if (!upload_in_progress || !asset || first_mip < 0 || mip_count <= 0 ||
        next_relative_mip < 0)
        return 0;
    if (next_relative_mip >= mip_count)
        return 0;
    for (int64_t i = next_relative_mip; i < mip_count; i++) {
        vgfx3d_native_texture_mip_t mip;
        if (!vgfx3d_textureasset_get_native_snapshot_mip(asset, first_mip, mip_count, i, &mip))
            return total;
        uint64_t bytes =
            (i == next_relative_mip)
                ? vgfx3d_pending_block_upload_bytes(mip.width,
                                                    mip.height,
                                                    mip.block_width,
                                                    mip.block_height,
                                                    mip.block_bytes,
                                                    next_block_row,
                                                    upload_in_progress)
                : mip.bytes;
        if (total > UINT64_MAX - bytes)
            return UINT64_MAX;
        total += bytes;
    }
    return total;
}

/// @brief Decode a Pixels object into a freshly malloc'd RGBA8 byte array.
/// Caller owns and frees the returned buffer. Returns 0 on success, -1 on
/// invalid dimensions or allocation failure. Out-params are unmodified on error.
int vgfx3d_unpack_pixels_rgba(const void *pixels_ptr,
                              int32_t *out_w,
                              int32_t *out_h,
                              uint8_t **out_rgba) {
    if (!pixels_ptr || !out_w || !out_h || !out_rgba)
        return -1;

    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    if (!pv->data || pv->w <= 0 || pv->h <= 0 || pv->w > INT32_MAX || pv->h > INT32_MAX)
        return -1;

    int32_t w = (int32_t)pv->w;
    int32_t h = (int32_t)pv->h;
    size_t pixel_count = (size_t)w * (size_t)h;
    if ((size_t)w != 0 && pixel_count / (size_t)w != (size_t)h)
        return -1;
    if (pixel_count > SIZE_MAX / 4u)
        return -1;
    uint8_t *rgba = (uint8_t *)malloc(pixel_count * 4);
    if (!rgba)
        return -1;

    for (size_t i = 0; i < pixel_count; i++) {
        uint32_t px = pv->data[i]; /* 0xRRGGBBAA */
        rgba[i * 4 + 0] = (uint8_t)((px >> 24) & 0xFF);
        rgba[i * 4 + 1] = (uint8_t)((px >> 16) & 0xFF);
        rgba[i * 4 + 2] = (uint8_t)((px >> 8) & 0xFF);
        rgba[i * 4 + 3] = (uint8_t)(px & 0xFF);
    }

    *out_w = w;
    *out_h = h;
    *out_rgba = rgba;
    return 0;
}

/// @brief Read a Pixels object's width/height without unpacking its data.
/// @return 1 with @p out_w / @p out_h set, or 0 (both zeroed) for a NULL/empty/oversized surface.
int vgfx3d_get_pixels_extent(const void *pixels_ptr, int32_t *out_w, int32_t *out_h) {
    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;

    if (out_w)
        *out_w = 0;
    if (out_h)
        *out_h = 0;
    if (!pv || !out_w || !out_h || !pv->data || pv->w <= 0 || pv->h <= 0 || pv->w > INT32_MAX ||
        pv->h > INT32_MAX)
        return 0;

    *out_w = (int32_t)pv->w;
    *out_h = (int32_t)pv->h;
    return 1;
}

/// @brief Decode a horizontal band of a Pixels object into a fresh RGBA8 buffer (caller frees).
/// @details Unpacks @p row_count rows from @p start_row (clamped to the image), optionally flipping
///          vertically (@p flip_y) for backends with a bottom-left origin. Enables streaming a
///          large texture upload row-band by row-band.
/// @return 0 on success with out-params set, -1 on invalid args or allocation failure.
int vgfx3d_unpack_pixels_rgba_rows(const void *pixels_ptr,
                                   int32_t start_row,
                                   int32_t row_count,
                                   int flip_y,
                                   int32_t *out_w,
                                   int32_t *out_rows,
                                   uint8_t **out_rgba) {
    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    int32_t w;
    int32_t h;
    size_t row_bytes;
    size_t total_bytes;
    uint8_t *rgba;

    if (out_w)
        *out_w = 0;
    if (out_rows)
        *out_rows = 0;
    if (out_rgba)
        *out_rgba = NULL;
    if (!pv || !out_w || !out_rows || !out_rgba || !pv->data || pv->w <= 0 || pv->h <= 0 ||
        pv->w > INT32_MAX || pv->h > INT32_MAX || start_row < 0 || row_count <= 0)
        return -1;

    w = (int32_t)pv->w;
    h = (int32_t)pv->h;
    if (start_row >= h)
        return -1;
    if (row_count > h - start_row)
        row_count = h - start_row;
    if ((size_t)w > SIZE_MAX / 4u)
        return -1;
    row_bytes = (size_t)w * 4u;
    if ((size_t)row_count > SIZE_MAX / row_bytes)
        return -1;
    total_bytes = (size_t)row_count * row_bytes;
    rgba = (uint8_t *)malloc(total_bytes);
    if (!rgba)
        return -1;

    for (int32_t y = 0; y < row_count; y++) {
        int32_t src_y = flip_y ? (h - 1 - (start_row + y)) : (start_row + y);
        const uint32_t *src = pv->data + ((size_t)src_y * (size_t)w);
        uint8_t *dst = rgba + ((size_t)y * row_bytes);
        for (int32_t x = 0; x < w; x++) {
            uint32_t px = src[x]; /* 0xRRGGBBAA */
            dst[(size_t)x * 4u + 0u] = (uint8_t)((px >> 24) & 0xFF);
            dst[(size_t)x * 4u + 1u] = (uint8_t)((px >> 16) & 0xFF);
            dst[(size_t)x * 4u + 2u] = (uint8_t)((px >> 8) & 0xFF);
            dst[(size_t)x * 4u + 3u] = (uint8_t)(px & 0xFF);
        }
    }

    *out_w = w;
    *out_rows = row_count;
    *out_rgba = rgba;
    return 0;
}

/// @brief Compute the RGBA8 byte count uploaded for one Pixels texture.
int vgfx3d_estimate_pixels_rgba_upload_bytes(const void *pixels_ptr, uint64_t *out_bytes) {
    const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)pixels_ptr;
    uint64_t w;
    uint64_t h;
    uint64_t pixel_count;

    if (out_bytes)
        *out_bytes = 0;
    if (!pv || !out_bytes || !pv->data || pv->w <= 0 || pv->h <= 0 || pv->w > INT32_MAX ||
        pv->h > INT32_MAX)
        return 0;

    w = (uint64_t)pv->w;
    h = (uint64_t)pv->h;
    if (w > UINT64_MAX / h)
        return 0;
    pixel_count = w * h;
    if (pixel_count > UINT64_MAX / 4u)
        return 0;

    *out_bytes = pixel_count * 4u;
    return 1;
}

/// @brief How many texture rows from @p next_row fit in the remaining per-frame upload byte budget.
/// @details UINT64_MAX budget means "all remaining rows"; otherwise divides the leftover budget by
///          the row size, always allowing at least one row so progress is guaranteed.
int32_t vgfx3d_upload_rows_for_budget(
    int32_t width, int32_t height, int32_t next_row, uint64_t budget, uint64_t used) {
    uint64_t row_bytes;
    uint64_t remaining_budget;
    uint64_t budget_rows;
    int32_t remaining_rows;

    if (width <= 0 || height <= 0 || next_row < 0 || next_row >= height)
        return 0;
    remaining_rows = height - next_row;
    if (budget == UINT64_MAX)
        return remaining_rows;
    if (budget == 0)
        return 0;

    row_bytes = (uint64_t)(uint32_t)width * 4u;
    if (row_bytes == 0 || used >= budget)
        return 0;
    remaining_budget = budget - used;
    budget_rows = remaining_budget / row_bytes;
    if (budget_rows == 0)
        budget_rows = 1;
    if (budget_rows > (uint64_t)remaining_rows)
        budget_rows = (uint64_t)remaining_rows;
    return (int32_t)budget_rows;
}

/// @brief Bytes still to upload for an RGBA texture from @p next_row to the last row.
/// @details Returns 0 when no upload is in progress; saturates at UINT64_MAX. Lets the scheduler
///          weigh this texture's remaining work against the frame budget.
uint64_t vgfx3d_pending_rgba_upload_bytes(int32_t width,
                                          int32_t height,
                                          int32_t next_row,
                                          int upload_in_progress) {
    uint64_t remaining_rows;
    uint64_t row_bytes;

    if (!upload_in_progress || width <= 0 || height <= 0 || next_row < 0 || next_row >= height)
        return 0;
    remaining_rows = (uint64_t)(uint32_t)(height - next_row);
    row_bytes = (uint64_t)(uint32_t)width * 4u;
    if (row_bytes != 0 && remaining_rows > UINT64_MAX / row_bytes)
        return UINT64_MAX;
    return remaining_rows * row_bytes;
}

/// @brief Bytes still to upload across all remaining cubemap faces and rows.
/// @details Counts the rows left in the current face plus every row of the faces after it (faces
/// are
///          uploaded in order 0..5). Returns 0 when idle; saturates at UINT64_MAX.
uint64_t vgfx3d_pending_cubemap_rgba_upload_bytes(int32_t face_size,
                                                  int32_t upload_face,
                                                  int32_t upload_next_row,
                                                  int upload_in_progress) {
    uint64_t remaining_rows;
    uint64_t row_bytes;

    if (!upload_in_progress || face_size <= 0 || upload_face < 0 || upload_face >= 6 ||
        upload_next_row < 0 || upload_next_row >= face_size)
        return 0;
    remaining_rows = (uint64_t)(uint32_t)(face_size - upload_next_row);
    remaining_rows += (uint64_t)(uint32_t)(5 - upload_face) * (uint64_t)(uint32_t)face_size;
    row_bytes = (uint64_t)(uint32_t)face_size * 4u;
    if (row_bytes != 0 && remaining_rows > UINT64_MAX / row_bytes)
        return UINT64_MAX;
    return remaining_rows * row_bytes;
}

/// @brief Compute a block-compressed texture's block-row count and per-block-row byte size.
/// @details Rounds width/height up to whole blocks (BCn/ASTC/ETC2 tile the image in fixed blocks),
///          overflow-checking the row size. Shared by the block-upload budget/pending helpers.
/// @return 1 with the out-params set, 0 on invalid dimensions or overflow.
static int vgfx3d_block_upload_shape(int32_t width,
                                     int32_t height,
                                     int32_t block_width,
                                     int32_t block_height,
                                     int32_t block_bytes,
                                     uint64_t *out_block_rows,
                                     uint64_t *out_row_bytes) {
    uint64_t block_cols;
    uint64_t block_rows;

    if (out_block_rows)
        *out_block_rows = 0;
    if (out_row_bytes)
        *out_row_bytes = 0;
    if (width <= 0 || height <= 0 || block_width <= 0 || block_height <= 0 || block_bytes <= 0)
        return 0;

    block_cols = ((uint64_t)(uint32_t)width + (uint64_t)(uint32_t)block_width - 1u) /
                 (uint64_t)(uint32_t)block_width;
    block_rows = ((uint64_t)(uint32_t)height + (uint64_t)(uint32_t)block_height - 1u) /
                 (uint64_t)(uint32_t)block_height;
    if (block_cols == 0 || block_rows == 0 ||
        block_cols > UINT64_MAX / (uint64_t)(uint32_t)block_bytes)
        return 0;
    if (out_block_rows)
        *out_block_rows = block_rows;
    if (out_row_bytes)
        *out_row_bytes = block_cols * (uint64_t)(uint32_t)block_bytes;
    return 1;
}

/// @brief How many block-rows from @p next_block_row fit in the remaining per-frame upload budget.
/// @details Block-compressed analogue of vgfx3d_upload_rows_for_budget; UINT64_MAX budget means all
///          remaining block-rows, and at least one block-row is always returned so uploads
///          progress.
int32_t vgfx3d_upload_block_rows_for_budget(int32_t width,
                                            int32_t height,
                                            int32_t block_width,
                                            int32_t block_height,
                                            int32_t block_bytes,
                                            int32_t next_block_row,
                                            uint64_t budget,
                                            uint64_t used) {
    uint64_t block_rows;
    uint64_t row_bytes;
    uint64_t remaining_rows;
    uint64_t remaining_budget;
    uint64_t budget_rows;

    if (!vgfx3d_block_upload_shape(
            width, height, block_width, block_height, block_bytes, &block_rows, &row_bytes))
        return 0;
    if (next_block_row < 0 || (uint64_t)(uint32_t)next_block_row >= block_rows || budget == 0 ||
        used >= budget)
        return 0;
    remaining_rows = block_rows - (uint64_t)(uint32_t)next_block_row;
    if (budget == UINT64_MAX)
        return remaining_rows > (uint64_t)INT32_MAX ? INT32_MAX : (int32_t)remaining_rows;

    remaining_budget = budget - used;
    budget_rows = remaining_budget / row_bytes;
    if (budget_rows == 0)
        budget_rows = 1;
    if (budget_rows > remaining_rows)
        budget_rows = remaining_rows;
    return (int32_t)budget_rows;
}

/// @brief Bytes still to upload for a block-compressed texture from @p next_block_row onward.
/// @details Returns 0 when idle; saturates at UINT64_MAX. The block analogue of
///          vgfx3d_pending_rgba_upload_bytes.
uint64_t vgfx3d_pending_block_upload_bytes(int32_t width,
                                           int32_t height,
                                           int32_t block_width,
                                           int32_t block_height,
                                           int32_t block_bytes,
                                           int32_t next_block_row,
                                           int upload_in_progress) {
    uint64_t block_rows;
    uint64_t row_bytes;
    uint64_t remaining_rows;

    if (!upload_in_progress ||
        !vgfx3d_block_upload_shape(
            width, height, block_width, block_height, block_bytes, &block_rows, &row_bytes))
        return 0;
    if (next_block_row < 0 || (uint64_t)(uint32_t)next_block_row >= block_rows)
        return 0;
    remaining_rows = block_rows - (uint64_t)(uint32_t)next_block_row;
    if (row_bytes != 0 && remaining_rows > UINT64_MAX / row_bytes)
        return UINT64_MAX;
    return remaining_rows * row_bytes;
}

/// @brief Decode all six cubemap faces into separate RGBA8 byte arrays.
/// All faces must be square and the same size. Caller owns and frees each
/// face buffer. On error any partially-allocated faces are freed automatically.
int vgfx3d_unpack_cubemap_faces_rgba(const void *cubemap_ptr,
                                     int32_t *out_face_size,
                                     uint8_t *out_faces[6]) {
    int32_t face_size = 0;
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;

    if (out_face_size)
        *out_face_size = 0;
    if (out_faces) {
        for (int face = 0; face < 6; face++)
            out_faces[face] = NULL;
    }
    if (!cubemap || !out_face_size || !out_faces ||
        !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return -1;

    for (int face = 0; face < 6; face++) {
        int32_t w = 0;
        int32_t h = 0;
        if (vgfx3d_unpack_pixels_rgba(cubemap->faces[face], &w, &h, &out_faces[face]) != 0 ||
            w != face_size || h != face_size) {
            for (int cleanup = 0; cleanup < 6; cleanup++) {
                free(out_faces[cleanup]);
                out_faces[cleanup] = NULL;
            }
            return -1;
        }
    }

    *out_face_size = face_size;
    return 0;
}

/// @brief Read a cubemap's face size, verifying all six faces are square and identically sized.
/// @return 1 with @p out_face_size set, or 0 (zeroed) if any face is missing or mis-sized.
int vgfx3d_get_cubemap_face_size(const void *cubemap_ptr, int32_t *out_face_size) {
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;
    int32_t face_size;

    if (out_face_size)
        *out_face_size = 0;
    if (!cubemap || !out_face_size || cubemap->cache_identity == 0 || cubemap->face_size <= 0 ||
        cubemap->face_size > VGFX3D_BACKEND_MAX_CUBEMAP_FACE_SIZE)
        return 0;

    face_size = (int32_t)cubemap->face_size;
    for (int face = 0; face < 6; face++) {
        int32_t w = 0;
        int32_t h = 0;
        if (!vgfx3d_get_pixels_extent(cubemap->faces[face], &w, &h) || w != face_size ||
            h != face_size)
            return 0;
    }

    *out_face_size = face_size;
    return 1;
}

/// @brief Decode a horizontal band of one cubemap face into a fresh RGBA8 buffer (caller frees).
/// @details Per-face, row-band analogue of vgfx3d_unpack_pixels_rgba_rows for streaming cubemap
///          uploads; validates @p face_index in [0, 6) and that the face matches the cube size.
/// @return 0 on success with out-params set, -1 on invalid args or allocation failure.
int vgfx3d_unpack_cubemap_rgba_rows(const void *cubemap_ptr,
                                    int32_t face_index,
                                    int32_t start_row,
                                    int32_t row_count,
                                    int flip_y,
                                    int32_t *out_face_size,
                                    int32_t *out_rows,
                                    uint8_t **out_rgba) {
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;
    int32_t face_size = 0;
    int32_t w = 0;
    int32_t rows = 0;
    uint8_t *rgba = NULL;

    if (out_face_size)
        *out_face_size = 0;
    if (out_rows)
        *out_rows = 0;
    if (out_rgba)
        *out_rgba = NULL;
    if (!cubemap || !out_face_size || !out_rows || !out_rgba || face_index < 0 || face_index >= 6 ||
        !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return -1;

    if (vgfx3d_unpack_pixels_rgba_rows(
            cubemap->faces[face_index], start_row, row_count, flip_y, &w, &rows, &rgba) != 0 ||
        !rgba || w != face_size || rows <= 0) {
        free(rgba);
        return -1;
    }

    *out_face_size = face_size;
    *out_rows = rows;
    *out_rgba = rgba;
    return 0;
}

/// @brief Compute the RGBA8 byte count uploaded for one six-face cubemap.
int vgfx3d_estimate_cubemap_rgba_upload_bytes(const void *cubemap_ptr, uint64_t *out_bytes) {
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;
    int32_t face_size = 0;
    uint64_t face_bytes = 0;
    uint64_t total = 0;

    if (out_bytes)
        *out_bytes = 0;
    if (!cubemap || !out_bytes || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;

    for (int face = 0; face < 6; face++) {
        const vgfx3d_pixels_view_t *pv = (const vgfx3d_pixels_view_t *)cubemap->faces[face];
        if (!pv || pv->w != face_size || pv->h != face_size ||
            !vgfx3d_estimate_pixels_rgba_upload_bytes(pv, &face_bytes))
            return 0;
        if (total > UINT64_MAX - face_bytes)
            return 0;
        total += face_bytes;
    }

    *out_bytes = total;
    return 1;
}

/// @brief Hash cubemap identity + all six face cache keys into one signature.
/// Uses an FNV-prime mixing scheme so face mutations, face replacement, and
/// cubemap object replacement all invalidate backend caches. Returns 0 when no
/// complete face set is bound.
uint64_t vgfx3d_get_cubemap_generation(const void *cubemap_ptr) {
    const vgfx3d_cubemap_view_t *cubemap = (const vgfx3d_cubemap_view_t *)cubemap_ptr;
    uint64_t signature = 1469598103934665603ull;
    int32_t face_size = 0;

    if (!cubemap || !vgfx3d_get_cubemap_face_size(cubemap, &face_size))
        return 0;

    signature ^=
        cubemap->cache_identity + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);

    for (int face = 0; face < 6; face++) {
        uint64_t face_key = vgfx3d_get_pixels_cache_key(cubemap->faces[face]);
        if (face_key == 0)
            return 0;
        signature ^= face_key + 0x9e3779b97f4a7c15ull + (signature << 6) + (signature >> 2);
    }

    return signature;
}

/// @brief Flip an RGBA8 image vertically in place (top<->bottom row swap).
/// Used to convert between Pixels' top-left origin and APIs that expect
/// bottom-left (e.g., OpenGL textures).
void vgfx3d_flip_rgba_rows(uint8_t *rgba, int32_t w, int32_t h) {
    if (!rgba || w <= 0 || h <= 1)
        return;

    if ((size_t)w > SIZE_MAX / 4u)
        return;
    size_t row_bytes = (size_t)w * 4;
    if (row_bytes != 0 && (size_t)h > SIZE_MAX / row_bytes)
        return;
    uint8_t *tmp = (uint8_t *)malloc(row_bytes);
    if (!tmp)
        return;

    for (int32_t y = 0; y < h / 2; y++) {
        uint8_t *top = rgba + (size_t)y * row_bytes;
        uint8_t *bot = rgba + (size_t)(h - 1 - y) * row_bytes;
        memcpy(tmp, top, row_bytes);
        memcpy(top, bot, row_bytes);
        memcpy(bot, tmp, row_bytes);
    }

    free(tmp);
}

/// @brief Convert IEEE-754 binary16 to binary32.
float vgfx3d_half_to_float(uint16_t bits) {
    uint32_t sign = (uint32_t)(bits & 0x8000u) << 16;
    uint32_t exp = (bits >> 10) & 0x1Fu;
    uint32_t mant = bits & 0x03FFu;
    uint32_t fbits;
    float out;

    if (exp == 0) {
        if (mant == 0) {
            fbits = sign;
        } else {
            exp = 127u - 15u + 1u;
            while ((mant & 0x0400u) == 0) {
                mant <<= 1u;
                exp--;
            }
            mant &= 0x03FFu;
            fbits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1Fu) {
        fbits = sign | 0x7F800000u | (mant << 13);
    } else {
        fbits = sign | ((exp + (127u - 15u)) << 23) | (mant << 13);
    }

    memcpy(&out, &fbits, sizeof(out));
    return out;
}

/// @brief Clamp a float to [0,1] and quantize to UNORM8.
uint8_t vgfx3d_float_to_unorm8(float value) {
    if (!(value > 0.0f))
        return 0;
    if (value >= 1.0f)
        return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

/// @brief Apply a simple Reinhard tonemap before UNORM8 quantization.
uint8_t vgfx3d_hdr_to_unorm8(float value) {
    if (!(value > 0.0f))
        return 0;
    return vgfx3d_float_to_unorm8(value / (1.0f + value));
}

/// @brief Validate a row-copy request: positive extents, non-negative strides, no 32-bit
///   overflow in the per-row byte/unit math, and strides large enough for one row.
static int vgfx3d_copy_dims_are_valid(int32_t copy_w,
                                      int32_t copy_h,
                                      int32_t dst_stride_units,
                                      int32_t src_stride_bytes,
                                      int32_t dst_units_per_pixel,
                                      int32_t src_bytes_per_pixel) {
    int64_t dst_required;
    int64_t src_required;
    if (copy_w <= 0 || copy_h <= 0 || dst_stride_units < 0 || src_stride_bytes < 0)
        return 0;
    dst_required = (int64_t)copy_w * (int64_t)dst_units_per_pixel;
    src_required = (int64_t)copy_w * (int64_t)src_bytes_per_pixel;
    if (dst_required > INT32_MAX || src_required > INT32_MAX)
        return 0;
    return (int64_t)dst_stride_units >= dst_required && (int64_t)src_stride_bytes >= src_required;
}

/// @brief Convert linear RGBA16F rows to displayable RGBA8.
void vgfx3d_copy_linear_rgba16f_to_rgba8(uint8_t *dst_rgba,
                                         int32_t dst_stride,
                                         int32_t copy_w,
                                         int32_t copy_h,
                                         const uint16_t *src_rgba16f,
                                         int32_t src_stride_bytes) {
    if (!dst_rgba || !src_rgba16f ||
        !vgfx3d_copy_dims_are_valid(
            copy_w, copy_h, dst_stride, src_stride_bytes, 4, (int32_t)(sizeof(uint16_t) * 4u))) {
        return;
    }

    for (int32_t y = 0; y < copy_h; y++) {
        uint8_t *dst_row = dst_rgba + (size_t)y * (size_t)dst_stride;
        const uint16_t *src_row =
            (const uint16_t *)((const uint8_t *)src_rgba16f + (size_t)y * (size_t)src_stride_bytes);
        for (int32_t x = 0; x < copy_w; x++) {
            dst_row[(size_t)x * 4u + 0u] = vgfx3d_hdr_to_unorm8(vgfx3d_half_to_float(src_row[0]));
            dst_row[(size_t)x * 4u + 1u] = vgfx3d_hdr_to_unorm8(vgfx3d_half_to_float(src_row[1]));
            dst_row[(size_t)x * 4u + 2u] = vgfx3d_hdr_to_unorm8(vgfx3d_half_to_float(src_row[2]));
            dst_row[(size_t)x * 4u + 3u] = vgfx3d_float_to_unorm8(vgfx3d_half_to_float(src_row[3]));
            src_row += 4;
        }
    }
}

/// @brief Convert linear RGBA16F rows to linear RGBA32F.
void vgfx3d_copy_linear_rgba16f_to_rgba32f(float *dst_rgba32f,
                                           int32_t dst_stride_floats,
                                           int32_t copy_w,
                                           int32_t copy_h,
                                           const uint16_t *src_rgba16f,
                                           int32_t src_stride_bytes) {
    if (!dst_rgba32f || !src_rgba16f ||
        !vgfx3d_copy_dims_are_valid(copy_w,
                                    copy_h,
                                    dst_stride_floats,
                                    src_stride_bytes,
                                    4,
                                    (int32_t)(sizeof(uint16_t) * 4u))) {
        return;
    }

    for (int32_t y = 0; y < copy_h; y++) {
        float *dst_row = dst_rgba32f + (size_t)y * (size_t)dst_stride_floats;
        const uint16_t *src_row =
            (const uint16_t *)((const uint8_t *)src_rgba16f + (size_t)y * (size_t)src_stride_bytes);
        for (int32_t x = 0; x < copy_w; x++) {
            dst_row[(size_t)x * 4u + 0u] = vgfx3d_half_to_float(src_row[0]);
            dst_row[(size_t)x * 4u + 1u] = vgfx3d_half_to_float(src_row[1]);
            dst_row[(size_t)x * 4u + 2u] = vgfx3d_half_to_float(src_row[2]);
            dst_row[(size_t)x * 4u + 3u] = vgfx3d_half_to_float(src_row[3]);
            src_row += 4;
        }
    }
}

/// @brief Convert linear RGBA32F rows to displayable RGBA8.
void vgfx3d_copy_linear_rgba32f_to_rgba8(uint8_t *dst_rgba,
                                         int32_t dst_stride,
                                         int32_t copy_w,
                                         int32_t copy_h,
                                         const float *src_rgba32f,
                                         int32_t src_stride_bytes) {
    if (!dst_rgba || !src_rgba32f ||
        !vgfx3d_copy_dims_are_valid(
            copy_w, copy_h, dst_stride, src_stride_bytes, 4, (int32_t)(sizeof(float) * 4u))) {
        return;
    }

    for (int32_t y = 0; y < copy_h; y++) {
        uint8_t *dst_row = dst_rgba + (size_t)y * (size_t)dst_stride;
        const float *src_row =
            (const float *)((const uint8_t *)src_rgba32f + (size_t)y * (size_t)src_stride_bytes);
        for (int32_t x = 0; x < copy_w; x++) {
            dst_row[(size_t)x * 4u + 0u] = vgfx3d_hdr_to_unorm8(src_row[0]);
            dst_row[(size_t)x * 4u + 1u] = vgfx3d_hdr_to_unorm8(src_row[1]);
            dst_row[(size_t)x * 4u + 2u] = vgfx3d_hdr_to_unorm8(src_row[2]);
            dst_row[(size_t)x * 4u + 3u] = vgfx3d_float_to_unorm8(src_row[3]);
            src_row += 4;
        }
    }
}

/// @brief Write a 4×4 identity matrix into @p out_matrix — the normal-matrix fallback
///   when the model matrix is singular and cannot be inverse-transposed.
static void vgfx3d_store_identity_normal_matrix4(float *out_matrix) {
    memset(out_matrix, 0, sizeof(float) * 16);
    out_matrix[0] = 1.0f;
    out_matrix[5] = 1.0f;
    out_matrix[10] = 1.0f;
    out_matrix[15] = 1.0f;
}

/// @brief Compute the normal matrix (inverse-transpose of the upper 3×3 of
/// @p model_matrix) and place it in the upper-left 3×3 of @p out_matrix.
/// Falls back to identity when the matrix is singular or non-finite, avoiding
/// NaN/Inf propagation in shaders and CPU skinning.
void vgfx3d_compute_normal_matrix4(const float *model_matrix, float *out_matrix) {
    if (!model_matrix || !out_matrix)
        return;

    const float a = model_matrix[0], b = model_matrix[1], c = model_matrix[2];
    const float d = model_matrix[4], e = model_matrix[5], f = model_matrix[6];
    const float g = model_matrix[8], h = model_matrix[9], i = model_matrix[10];

    if (!isfinite(a) || !isfinite(b) || !isfinite(c) || !isfinite(d) || !isfinite(e) ||
        !isfinite(f) || !isfinite(g) || !isfinite(h) || !isfinite(i)) {
        vgfx3d_store_identity_normal_matrix4(out_matrix);
        return;
    }

    const float c00 = e * i - f * h;
    const float c01 = -(d * i - f * g);
    const float c02 = d * h - e * g;
    const float c10 = -(b * i - c * h);
    const float c11 = a * i - c * g;
    const float c12 = -(a * h - b * g);
    const float c20 = b * f - c * e;
    const float c21 = -(a * f - c * d);
    const float c22 = a * e - b * d;

    float det = a * c00 + b * c01 + c * c02;
    float inv_det = 0.0f;
    if (isfinite(det) && fabsf(det) > kVgfx3dSingularDetEps)
        inv_det = 1.0f / det;

    memset(out_matrix, 0, sizeof(float) * 16);
    out_matrix[15] = 1.0f;

    if (!isfinite(inv_det) || inv_det == 0.0f) {
        vgfx3d_store_identity_normal_matrix4(out_matrix);
        return;
    }

    /* Normal matrix = (M^-1)^T = cofactor_matrix / det. The cofactor `cij` is the
     * cofactor of element [i][j], so it is placed DIRECTLY at out[i][j] — no
     * transpose. (The plain inverse M^-1 = adjugate/det = cofactor^T/det uses the
     * transpose; the inverse-transpose un-does it. Placing cij at out[j][i] is the
     * classic adjugate/cofactor mix-up and yields M^-1, which counter-rotates
     * normals under any rotation/shear while looking correct for diagonal scales.) */
    out_matrix[0] = c00 * inv_det;
    out_matrix[1] = c01 * inv_det;
    out_matrix[2] = c02 * inv_det;
    out_matrix[4] = c10 * inv_det;
    out_matrix[5] = c11 * inv_det;
    out_matrix[6] = c12 * inv_det;
    out_matrix[8] = c20 * inv_det;
    out_matrix[9] = c21 * inv_det;
    out_matrix[10] = c22 * inv_det;
}

/// @brief Invert a 4×4 row-major matrix using cofactor expansion.
/// @return 0 on success, -1 if @p matrix is null or singular (|det| < 1e-12).
/// Out-buffer is unmodified on failure.
int vgfx3d_invert_matrix4(const float *matrix, float *out_matrix) {
    float inv[16];
    float det;

    if (!matrix || !out_matrix)
        return -1;

    inv[0] = matrix[5] * matrix[10] * matrix[15] - matrix[5] * matrix[11] * matrix[14] -
             matrix[9] * matrix[6] * matrix[15] + matrix[9] * matrix[7] * matrix[14] +
             matrix[13] * matrix[6] * matrix[11] - matrix[13] * matrix[7] * matrix[10];
    inv[4] = -matrix[4] * matrix[10] * matrix[15] + matrix[4] * matrix[11] * matrix[14] +
             matrix[8] * matrix[6] * matrix[15] - matrix[8] * matrix[7] * matrix[14] -
             matrix[12] * matrix[6] * matrix[11] + matrix[12] * matrix[7] * matrix[10];
    inv[8] = matrix[4] * matrix[9] * matrix[15] - matrix[4] * matrix[11] * matrix[13] -
             matrix[8] * matrix[5] * matrix[15] + matrix[8] * matrix[7] * matrix[13] +
             matrix[12] * matrix[5] * matrix[11] - matrix[12] * matrix[7] * matrix[9];
    inv[12] = -matrix[4] * matrix[9] * matrix[14] + matrix[4] * matrix[10] * matrix[13] +
              matrix[8] * matrix[5] * matrix[14] - matrix[8] * matrix[6] * matrix[13] -
              matrix[12] * matrix[5] * matrix[10] + matrix[12] * matrix[6] * matrix[9];
    inv[1] = -matrix[1] * matrix[10] * matrix[15] + matrix[1] * matrix[11] * matrix[14] +
             matrix[9] * matrix[2] * matrix[15] - matrix[9] * matrix[3] * matrix[14] -
             matrix[13] * matrix[2] * matrix[11] + matrix[13] * matrix[3] * matrix[10];
    inv[5] = matrix[0] * matrix[10] * matrix[15] - matrix[0] * matrix[11] * matrix[14] -
             matrix[8] * matrix[2] * matrix[15] + matrix[8] * matrix[3] * matrix[14] +
             matrix[12] * matrix[2] * matrix[11] - matrix[12] * matrix[3] * matrix[10];
    inv[9] = -matrix[0] * matrix[9] * matrix[15] + matrix[0] * matrix[11] * matrix[13] +
             matrix[8] * matrix[1] * matrix[15] - matrix[8] * matrix[3] * matrix[13] -
             matrix[12] * matrix[1] * matrix[11] + matrix[12] * matrix[3] * matrix[9];
    inv[13] = matrix[0] * matrix[9] * matrix[14] - matrix[0] * matrix[10] * matrix[13] -
              matrix[8] * matrix[1] * matrix[14] + matrix[8] * matrix[2] * matrix[13] +
              matrix[12] * matrix[1] * matrix[10] - matrix[12] * matrix[2] * matrix[9];
    inv[2] = matrix[1] * matrix[6] * matrix[15] - matrix[1] * matrix[7] * matrix[14] -
             matrix[5] * matrix[2] * matrix[15] + matrix[5] * matrix[3] * matrix[14] +
             matrix[13] * matrix[2] * matrix[7] - matrix[13] * matrix[3] * matrix[6];
    inv[6] = -matrix[0] * matrix[6] * matrix[15] + matrix[0] * matrix[7] * matrix[14] +
             matrix[4] * matrix[2] * matrix[15] - matrix[4] * matrix[3] * matrix[14] -
             matrix[12] * matrix[2] * matrix[7] + matrix[12] * matrix[3] * matrix[6];
    inv[10] = matrix[0] * matrix[5] * matrix[15] - matrix[0] * matrix[7] * matrix[13] -
              matrix[4] * matrix[1] * matrix[15] + matrix[4] * matrix[3] * matrix[13] +
              matrix[12] * matrix[1] * matrix[7] - matrix[12] * matrix[3] * matrix[5];
    inv[14] = -matrix[0] * matrix[5] * matrix[14] + matrix[0] * matrix[6] * matrix[13] +
              matrix[4] * matrix[1] * matrix[14] - matrix[4] * matrix[2] * matrix[13] -
              matrix[12] * matrix[1] * matrix[6] + matrix[12] * matrix[2] * matrix[5];
    inv[3] = -matrix[1] * matrix[6] * matrix[11] + matrix[1] * matrix[7] * matrix[10] +
             matrix[5] * matrix[2] * matrix[11] - matrix[5] * matrix[3] * matrix[10] -
             matrix[9] * matrix[2] * matrix[7] + matrix[9] * matrix[3] * matrix[6];
    inv[7] = matrix[0] * matrix[6] * matrix[11] - matrix[0] * matrix[7] * matrix[10] -
             matrix[4] * matrix[2] * matrix[11] + matrix[4] * matrix[3] * matrix[10] +
             matrix[8] * matrix[2] * matrix[7] - matrix[8] * matrix[3] * matrix[6];
    inv[11] = -matrix[0] * matrix[5] * matrix[11] + matrix[0] * matrix[7] * matrix[9] +
              matrix[4] * matrix[1] * matrix[11] - matrix[4] * matrix[3] * matrix[9] -
              matrix[8] * matrix[1] * matrix[7] + matrix[8] * matrix[3] * matrix[5];
    inv[15] = matrix[0] * matrix[5] * matrix[10] - matrix[0] * matrix[6] * matrix[9] -
              matrix[4] * matrix[1] * matrix[10] + matrix[4] * matrix[2] * matrix[9] +
              matrix[8] * matrix[1] * matrix[6] - matrix[8] * matrix[2] * matrix[5];

    det = matrix[0] * inv[0] + matrix[1] * inv[4] + matrix[2] * inv[8] + matrix[3] * inv[12];
    if (!isfinite(det) || fabsf(det) < kVgfx3dSingularDetEps)
        return -1;

    det = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out_matrix[i] = inv[i] * det;
    return 0;
}
