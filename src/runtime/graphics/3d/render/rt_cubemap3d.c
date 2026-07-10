//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/render/rt_cubemap3d.c
// Purpose: Viper.Graphics3D.CubeMap3D — 6-face cube map texture for
//   skybox rendering and environment reflections.
//
// Key invariants:
//   - All 6 faces must be square and the same dimensions
//   - Faces are retained Pixels objects while the cubemap is alive.
//   - Face order: +X, -X, +Y, -Y, +Z, -Z
//   - Per-cubemap `cache_identity` is unique within a process and skips zero
//     so callers can use 0 as "no identity yet".
//
// Ownership/Lifetime:
//   - CubeMap3D is GC-managed; finalizer releases each face Pixels reference.
//   - Faces are retained on construction and held for the cubemap's lifetime.
//   - Canvas/Material env-map setters retain/release the cubemap on assign.
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, plans/3d/11-cube-maps.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_g3d_ref_slots.h"
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_platform.h"
#include "rt_asset.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
extern void rt_material3d_assign_env_map_checked(void *obj, void *cubemap);
#include "rt_trap.h"

extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
extern int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);

/// @brief Validate that @p pixels is a live `Viper.Graphics.Pixels` handle.
static int cubemap_pixels_valid(void *pixels) {
    return rt_pixels_checked_impl_or_null(pixels) != NULL;
}

/// @brief Validate that @p cm is still a live CubeMap3D handle before dereferencing it.
static int cubemap_handle_valid(const rt_cubemap3d *cm) {
    return cm && rt_g3d_has_class((void *)(uintptr_t)cm, RT_G3D_CUBEMAP3D_CLASS_ID);
}

/// @brief Return a checked Pixels face implementation, or NULL for stale/corrupt slots.
static rt_pixels_impl *cubemap_face_pixels_impl(void *pixels) {
    rt_pixels_impl *pv = rt_pixels_checked_impl_or_null(pixels);
    if (!pv || !pv->data || pv->width <= 0 || pv->height <= 0 || pv->width > INT_MAX ||
        pv->height > INT_MAX)
        return NULL;
    return pv;
}

static uint64_t g_next_cubemap_cache_identity = 1;

#define CUBEMAP3D_MAX_FACE_SIZE 32768

/// @brief Validate the full six-face cubemap invariant before sampling.
static int cubemap_faces_valid(const rt_cubemap3d *cm) {
    int64_t face_size;
    if (!cubemap_handle_valid(cm))
        return 0;
    face_size = cm->face_size;
    if (face_size <= 0 || face_size > CUBEMAP3D_MAX_FACE_SIZE || cm->cache_identity == 0)
        return 0;
    for (int face = 0; face < 6; face++) {
        rt_pixels_impl *pv = cubemap_face_pixels_impl(cm->faces[face]);
        if (!pv || pv->width != face_size || pv->height != face_size)
            return 0;
    }
    return 1;
}

/// @brief True if the cubemap has all six faces present, square, and at a consistent face size.
int rt_cubemap3d_is_complete(void *cubemap) {
    return cubemap_faces_valid((const rt_cubemap3d *)cubemap) ? 1 : 0;
}

/// @brief Drop a GC-managed reference held in a `**slot` and null the slot.
/// @details Idempotent — safe to call on already-null slots. Used by the
///          finalizer to release each of the six cached face Pixels.
static void cubemap_release_ref(void **slot) {
    rt_g3d_ref_slot_release(slot);
}

/// @brief Release a cubemap face only when it still points at a Pixels object.
static void cubemap_release_face_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!cubemap_pixels_valid(*slot)) {
        rt_g3d_ref_slot_clear_unowned(slot);
        return;
    }
    cubemap_release_ref(slot);
}

/// @brief Return a process-unique nonzero cache identity for skybox/env-map invalidation.
static uint64_t cubemap_next_cache_identity(void) {
    uint64_t id =
        rt_atomic_fetch_add_u64(&g_next_cubemap_cache_identity, UINT64_C(1), __ATOMIC_RELAXED);
    if (id == 0) {
        id = rt_atomic_fetch_add_u64(&g_next_cubemap_cache_identity, UINT64_C(1), __ATOMIC_RELAXED);
        if (id == 0)
            id = 1;
    }
    return id;
}

/// @brief Return a finite scalar, falling back to @p fallback for NaN/Inf.
static float cubemap_finite_or(float value, float fallback) {
    return isfinite(value) ? value : fallback;
}

/// @brief Clamp extreme face UVs while preserving small out-of-face edge taps.
static float cubemap_limited_uv(float value) {
    value = cubemap_finite_or(value, 0.5f);
    if (value < -2.0f)
        return -2.0f;
    if (value > 3.0f)
        return 3.0f;
    return value;
}

/// @brief Normalize a direction with max-component scaling to avoid float overflow.
static int cubemap_normalize_direction(float *dx, float *dy, float *dz) {
    double x;
    double y;
    double z;
    double scale;
    double len;

    if (!dx || !dy || !dz || !isfinite(*dx) || !isfinite(*dy) || !isfinite(*dz))
        return 0;
    x = (double)*dx;
    y = (double)*dy;
    z = (double)*dz;
    scale = fabs(x);
    if (fabs(y) > scale)
        scale = fabs(y);
    if (fabs(z) > scale)
        scale = fabs(z);
    if (!isfinite(scale) || scale <= 1e-20)
        return 0;
    x /= scale;
    y /= scale;
    z /= scale;
    len = sqrt(x * x + y * y + z * z);
    if (!isfinite(len) || len <= 1e-20)
        return 0;
    *dx = (float)(x / len);
    *dy = (float)(y / len);
    *dz = (float)(z / len);
    return 1;
}

/// @brief GC finalizer: release every face Pixels reference and null the slots.
/// @details Called when the cubemap's refcount reaches zero. Each face
///          is released independently so cubemaps built with a subset
///          of faces (e.g. a 3-face placeholder during streaming) don't
///          trip a null-deref in the inner release. Prefiltered IBL mip
///          faces (owned creation references) are released the same way.
static void cubemap_finalize(void *obj) {
    rt_cubemap3d *cm = (rt_cubemap3d *)obj;
    if (!cm)
        return;
    for (int i = 0; i < 6; i++)
        cubemap_release_face_slot(&cm->faces[i]);
    for (int m = 0; m < RT_CUBEMAP3D_IBL_MAX_MIPS; m++)
        for (int f = 0; f < 6; f++)
            cubemap_release_face_slot(&cm->ibl_mips[m][f]);
    cm->ibl_mip_count = 0;
    cm->ibl_ready = 0;
}

/// @brief Convert a 3D direction vector into a cubemap face index plus UV.
/// @details Implements the classic "pick the major axis" projection used
///          by every GPU cubemap sampler:
///          1. Compare |dx|, |dy|, |dz| — the largest component picks
///             one of the six cube faces.
///          2. Divide the other two components by the major axis value
///             to get (u, v) in `[-1, +1]`, then remap to `[0, 1]`.
///          The per-face U/V sign conventions here match the
///          `cube_pos_x.png` / `cube_neg_x.png` layout Blender and
///          most tool chains export, so no extra flip is needed at
///          load time.
///          Floor at `1e-8` on the major axis prevents division by
///          zero when a direction component is exactly zero (e.g.
///          sampling straight down (0,-1,0)).
static void cubemap_direction_to_face_uv(
    float dx, float dy, float dz, int *out_face, float *out_u, float *out_v) {
    float ax;
    float ay;
    float az;
    int face;
    float u, v, ma;

    if (!out_face || !out_u || !out_v)
        return;
    if (!cubemap_normalize_direction(&dx, &dy, &dz)) {
        dx = 0.0f;
        dy = 0.0f;
        dz = -1.0f;
    }
    ax = fabsf(dx);
    ay = fabsf(dy);
    az = fabsf(dz);

    if (ax >= ay && ax >= az) {
        ma = ax;
        if (dx > 0.0f) {
            face = 0; /* +X */
            u = -dz;
            v = -dy;
        } else {
            face = 1; /* -X */
            u = dz;
            v = -dy;
        }
    } else if (ay >= ax && ay >= az) {
        ma = ay;
        if (dy > 0.0f) {
            face = 2; /* +Y */
            u = dx;
            v = dz;
        } else {
            face = 3; /* -Y */
            u = dx;
            v = -dz;
        }
    } else {
        ma = az;
        if (dz > 0.0f) {
            face = 4; /* +Z */
            u = dx;
            v = -dy;
        } else {
            face = 5; /* -Z */
            u = -dx;
            v = -dy;
        }
    }

    if (ma < 1e-8f)
        ma = 1e-8f;
    *out_face = face;
    *out_u = (u / ma + 1.0f) * 0.5f;
    *out_v = (v / ma + 1.0f) * 0.5f;
}

/// @brief Convert an already-normalized finite direction into a cubemap face plus UV.
/// @details Same major-axis projection as @ref cubemap_direction_to_face_uv, but skips the
///          defensive normalization step. This is for CPU hot paths that have already sanitized
///          each direction and would otherwise pay a second square root per sample.
static void cubemap_unit_direction_to_face_uv(
    float dx, float dy, float dz, int *out_face, float *out_u, float *out_v) {
    float ax;
    float ay;
    float az;
    int face;
    float u;
    float v;
    float ma;

    if (!out_face || !out_u || !out_v)
        return;
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz) ||
        (fabsf(dx) < 1e-10f && fabsf(dy) < 1e-10f && fabsf(dz) < 1e-10f)) {
        dx = 0.0f;
        dy = 0.0f;
        dz = -1.0f;
    }
    ax = fabsf(dx);
    ay = fabsf(dy);
    az = fabsf(dz);
    if (ax >= ay && ax >= az) {
        ma = ax;
        if (dx > 0.0f) {
            face = 0;
            u = -dz;
            v = -dy;
        } else {
            face = 1;
            u = dz;
            v = -dy;
        }
    } else if (ay >= ax && ay >= az) {
        ma = ay;
        if (dy > 0.0f) {
            face = 2;
            u = dx;
            v = dz;
        } else {
            face = 3;
            u = dx;
            v = -dz;
        }
    } else {
        ma = az;
        if (dz > 0.0f) {
            face = 4;
            u = dx;
            v = -dy;
        } else {
            face = 5;
            u = -dx;
            v = -dy;
        }
    }
    if (ma < 1e-8f)
        ma = 1e-8f;
    *out_face = face;
    *out_u = (u / ma + 1.0f) * 0.5f;
    *out_v = (v / ma + 1.0f) * 0.5f;
}

/// @brief Inverse of `direction_to_face_uv` — convert (face, u, v) back to a unit direction.
/// @details Used when *baking* from a cubemap into an equirectangular
///          texture (or prefiltering for IBL). For each texel on the
///          destination equirect, we walk the cubemap face grid in
///          reverse: remap `(u, v)` from `[0, 1]` to `[-1, +1]`, pick
///          the fixed major axis for the face, then normalize. The
///          `1e-8` floor on the length avoids a NaN if the inputs
///          collapsed to zero.
static void cubemap_face_uv_to_direction(
    int face, float u, float v, float *out_dx, float *out_dy, float *out_dz) {
    float uu;
    float vv;
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    float len;

    if (!out_dx || !out_dy || !out_dz)
        return;
    if (face < 0 || face > 5)
        face = 5;
    u = cubemap_limited_uv(u);
    v = cubemap_limited_uv(v);
    uu = u * 2.0f - 1.0f;
    vv = v * 2.0f - 1.0f;

    switch (face) {
        case 0: /* +X */
            dx = 1.0f;
            dy = -vv;
            dz = -uu;
            break;
        case 1: /* -X */
            dx = -1.0f;
            dy = -vv;
            dz = uu;
            break;
        case 2: /* +Y */
            dx = uu;
            dy = 1.0f;
            dz = vv;
            break;
        case 3: /* -Y */
            dx = uu;
            dy = -1.0f;
            dz = -vv;
            break;
        case 4: /* +Z */
            dx = uu;
            dy = -vv;
            dz = 1.0f;
            break;
        default: /* -Z */
            dx = -uu;
            dy = -vv;
            dz = -1.0f;
            break;
    }

    len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (isfinite(len) && len > 1e-8f) {
        dx /= len;
        dy /= len;
        dz /= len;
    } else {
        dx = 0.0f;
        dy = 0.0f;
        dz = -1.0f;
    }
    *out_dx = dx;
    *out_dy = dy;
    *out_dz = dz;
}

/// @brief Nearest-neighbor RGBA sample from a cubemap along direction `(dx, dy, dz)`.
/// @details Three-step classical cubemap fetch:
///          1. Project the direction into face + UV.
///          2. Clamp UV to `[0, 1]` (guards against directions that
///             drift off-face by floating-point error at cube edges).
///          3. Floor-scale UV into pixel coords and fetch through
///             `rt_pixels_get`.
///          Returns 0 (transparent black) for null cubemaps or empty
///          faces. Used by skybox backgrounds and environment-map
///          reflection in Material.reflectivity > 0 draws.
static uint32_t cubemap_sample_nearest_rgba(const rt_cubemap3d *cm, float dx, float dy, float dz) {
    int face = 0;
    float u = 0.5f;
    float v = 0.5f;
    rt_pixels_impl *pv;
    int64_t fw;
    int64_t fh;
    int xi;
    int yi;

    if (!cubemap_faces_valid(cm))
        return 0;
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz) ||
        (fabsf(dx) < 1e-10f && fabsf(dy) < 1e-10f && fabsf(dz) < 1e-10f))
        return 0;

    cubemap_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    if (face < 0 || face > 5)
        return 0;
    pv = cubemap_face_pixels_impl(cm->faces[face]);
    if (!pv)
        return 0;
    fw = pv->width;
    fh = pv->height;

    if (u < 0.0f)
        u = 0.0f;
    else if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;

    xi = (int)floorf(u * (float)fw);
    yi = (int)floorf(v * (float)fh);
    if (xi >= (int)fw)
        xi = (int)fw - 1;
    if (yi >= (int)fh)
        yi = (int)fh - 1;
    if (xi < 0)
        xi = 0;
    if (yi < 0)
        yi = 0;
    return pv->data[(int64_t)yi * pv->width + xi];
}

/// @brief Create a cube map from six square face textures.
/// @details The six Pixels objects represent the +X, -X, +Y, -Y, +Z, -Z faces
///          of a cube. All must be square and the same dimensions. The cube map
///          retains the face textures so skybox and reflection sampling remain
///          valid even if the caller drops its references. Used
///          for skyboxes and environment-map reflections.
/// @param px Positive-X face (right).
/// @param nx Negative-X face (left).
/// @param py Positive-Y face (top).
/// @param ny Negative-Y face (bottom).
/// @param pz Positive-Z face (front).
/// @param nz Negative-Z face (back).
/// @return Opaque cube map handle, or NULL on validation failure.
//=============================================================================
// Radiance .hdr panorama loading (from-scratch RGBE decoder)
//=============================================================================

/// @brief Exact power of two via IEEE-754 bit construction (no ldexpf: the
///   native-link runtime symbol set excludes it).
static float cubemap_hdr_exp2i(int n) {
    uint32_t bits;
    float f;
    if (n < -149)
        return 0.0f;
    if (n < -126) {
        bits = 1u << (uint32_t)(n + 149); /* denormal */
    } else {
        if (n > 127)
            n = 127;
        bits = (uint32_t)(n + 127) << 23;
    }
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/// @brief One RGBE quadruple to linear float RGB (Radiance shared-exponent form).
static void cubemap_hdr_rgbe_to_float(const uint8_t rgbe[4], float *out_rgb) {
    if (rgbe[3] == 0) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
        return;
    }
    {
        float scale = cubemap_hdr_exp2i((int)rgbe[3] - 136); /* 2^(e-128) / 256 */
        out_rgb[0] = (float)rgbe[0] * scale;
        out_rgb[1] = (float)rgbe[1] * scale;
        out_rgb[2] = (float)rgbe[2] * scale;
    }
}

/// @brief Parse the Radiance header; returns the offset of the pixel data and
///   the image dimensions, or 0 on malformed input. Only the standard "-Y H +X W"
///   row order is accepted (top-down rows, left-to-right columns).
static size_t cubemap_hdr_parse_header(const uint8_t *data, size_t size, int *out_w, int *out_h) {
    size_t pos = 0;
    int saw_format = 0;
    if (size < 11 || data[0] != '#' || data[1] != '?')
        return 0;
    while (pos < size) {
        size_t line_start = pos;
        size_t line_len;
        while (pos < size && data[pos] != '\n')
            pos++;
        if (pos >= size)
            return 0;
        line_len = pos - line_start;
        pos++; /* consume newline */
        if (line_len == 0) {
            /* Blank line ends the header; the resolution line follows. */
            break;
        }
        if (line_len >= 7 && memcmp(data + line_start, "FORMAT=", 7) == 0) {
            if (line_len >= 22 &&
                memcmp(data + line_start + 7, "32-bit_rle_rgbe", 15) == 0)
                saw_format = 1;
            else
                return 0;
        }
    }
    if (!saw_format || pos >= size)
        return 0;
    {
        /* Resolution line, e.g. "-Y 512 +X 1024". */
        char line[96];
        size_t line_start = pos;
        size_t line_len;
        int h = 0;
        int w = 0;
        while (pos < size && data[pos] != '\n')
            pos++;
        if (pos >= size)
            return 0;
        line_len = pos - line_start;
        pos++;
        if (line_len == 0 || line_len >= sizeof(line))
            return 0;
        memcpy(line, data + line_start, line_len);
        line[line_len] = 0;
        if (sscanf(line, "-Y %d +X %d", &h, &w) != 2)
            return 0;
        if (w <= 0 || h <= 0 || w > 16384 || h > 16384)
            return 0;
        *out_w = w;
        *out_h = h;
    }
    return pos;
}

/// @brief Decode the RGBE scanlines (new RLE, old RLE, and flat forms) into a
///   linear float RGB image. @return malloc-owned w*h*3 floats, or NULL.
static float *cubemap_hdr_decode(const uint8_t *data, size_t size, int *out_w, int *out_h) {
    int w = 0;
    int h = 0;
    size_t pos = cubemap_hdr_parse_header(data, size, &w, &h);
    float *rgb;
    uint8_t *row;
    if (pos == 0)
        return NULL;
    if ((size_t)w > SIZE_MAX / (size_t)h / (3u * sizeof(float)))
        return NULL;
    rgb = (float *)malloc((size_t)w * (size_t)h * 3u * sizeof(float));
    row = (uint8_t *)malloc((size_t)w * 4u);
    if (!rgb || !row) {
        free(rgb);
        free(row);
        return NULL;
    }
    for (int y = 0; y < h; y++) {
        if (pos + 4 > size)
            goto fail;
        if (data[pos] == 2 && data[pos + 1] == 2 &&
            (((int)data[pos + 2] << 8) | data[pos + 3]) == w && w >= 8 && w <= 32767) {
            /* New-style RLE: four independent component planes. */
            pos += 4;
            for (int c = 0; c < 4; c++) {
                int x = 0;
                while (x < w) {
                    uint8_t count;
                    if (pos >= size)
                        goto fail;
                    count = data[pos++];
                    if (count > 128) {
                        uint8_t value;
                        int run = count - 128;
                        if (pos >= size || x + run > w)
                            goto fail;
                        value = data[pos++];
                        for (int i = 0; i < run; i++)
                            row[(size_t)(x + i) * 4u + (size_t)c] = value;
                        x += run;
                    } else {
                        if (count == 0 || x + count > w || pos + count > size)
                            goto fail;
                        for (int i = 0; i < count; i++)
                            row[(size_t)(x + i) * 4u + (size_t)c] = data[pos + (size_t)i];
                        pos += count;
                        x += count;
                    }
                }
            }
        } else {
            /* Flat / old-style RLE rows. */
            int x = 0;
            int shift = 0;
            while (x < w) {
                uint8_t px[4];
                if (pos + 4 > size)
                    goto fail;
                memcpy(px, data + pos, 4);
                pos += 4;
                if (px[0] == 1 && px[1] == 1 && px[2] == 1) {
                    int run = (int)px[3] << shift;
                    if (x == 0 || x + run > w)
                        goto fail;
                    for (int i = 0; i < run; i++)
                        memcpy(&row[(size_t)(x + i) * 4u], &row[(size_t)(x - 1) * 4u], 4u);
                    x += run;
                    shift += 8;
                    if (shift > 24)
                        goto fail;
                } else {
                    memcpy(&row[(size_t)x * 4u], px, 4u);
                    x++;
                    shift = 0;
                }
            }
        }
        for (int x = 0; x < w; x++)
            cubemap_hdr_rgbe_to_float(&row[(size_t)x * 4u],
                                      &rgb[((size_t)y * (size_t)w + (size_t)x) * 3u]);
    }
    free(row);
    *out_w = w;
    *out_h = h;
    return rgb;
fail:
    free(rgb);
    free(row);
    return NULL;
}

/// @brief Bilinear sample of the equirectangular panorama along @p dir
///   (u wraps around the seam, v clamps at the poles).
static void cubemap_hdr_sample_panorama(
    const float *rgb, int w, int h, const float dir[3], float *out_rgb) {
    float u = 0.5f + atan2f(dir[0], dir[2]) * (float)(1.0 / (2.0 * 3.14159265358979323846));
    float dy = dir[1] < -1.0f ? -1.0f : (dir[1] > 1.0f ? 1.0f : dir[1]);
    float v = acosf(dy) * (float)(1.0 / 3.14159265358979323846);
    float fx = u * (float)w - 0.5f;
    float fy = v * (float)h - 0.5f;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    float tx = fx - (float)x0;
    float ty = fy - (float)y0;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    int xs[2];
    int ys[2];
    xs[0] = ((x0 % w) + w) % w;
    xs[1] = ((x1 % w) + w) % w;
    ys[0] = y0 < 0 ? 0 : (y0 > h - 1 ? h - 1 : y0);
    ys[1] = y1 < 0 ? 0 : (y1 > h - 1 ? h - 1 : y1);
    for (int c = 0; c < 3; c++) {
        float c00 = rgb[((size_t)ys[0] * (size_t)w + (size_t)xs[0]) * 3u + (size_t)c];
        float c10 = rgb[((size_t)ys[0] * (size_t)w + (size_t)xs[1]) * 3u + (size_t)c];
        float c01 = rgb[((size_t)ys[1] * (size_t)w + (size_t)xs[0]) * 3u + (size_t)c];
        float c11 = rgb[((size_t)ys[1] * (size_t)w + (size_t)xs[1]) * 3u + (size_t)c];
        float top = c00 + (c10 - c00) * tx;
        float bot = c01 + (c11 - c01) * tx;
        out_rgb[c] = top + (bot - top) * ty;
    }
}

/// @brief Load a Radiance .hdr equirectangular panorama as a CubeMap3D.
/// @details The panorama decodes to linear float RGB, projects onto the six
///   cube faces through the engine's own face basis, and range-compresses with
///   `x' = e*x / (1 + e*x)` (Reinhard, e = @p exposure) into the 8-bit face
///   storage the IBL pipeline consumes. Exposure defaults to 1 when
///   non-positive. Asset resolution follows the asset manager (embedded ->
///   mounted packs -> filesystem).
/// @brief Read an entire file from the plain filesystem (LoadHdrPanorama accepts
///   direct paths like SceneAsset.Load; the asset manager is the fallback).
static uint8_t *cubemap_hdr_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *data;
    *out_size = 0;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = (uint8_t *)malloc((size_t)len ? (size_t)len : 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(data, 1, (size_t)len, f) != (size_t)len) {
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_size = (size_t)len;
    return data;
}

void *rt_cubemap3d_load_hdr_panorama(rt_string path, double exposure) {
    size_t size = 0;
    const char *cpath = rt_string_cstr(path);
    uint8_t *data = cpath ? cubemap_hdr_read_file(cpath, &size) : NULL;
    if (!data)
        data = rt_asset_load_raw(path, &size);
    float *rgb = NULL;
    int w = 0;
    int h = 0;
    int face_size;
    void *faces[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
    void *cubemap = NULL;
    float e = (float)exposure;
    if (!isfinite(e) || e <= 0.0f)
        e = 1.0f;
    if (!data) {
        rt_trap("CubeMap3D.LoadHdrPanorama: asset not found");
        return NULL;
    }
    rgb = cubemap_hdr_decode(data, size, &w, &h);
    free(data);
    if (!rgb) {
        rt_trap("CubeMap3D.LoadHdrPanorama: not a valid Radiance .hdr image");
        return NULL;
    }
    face_size = 16;
    while (face_size * 2 <= w / 4 && face_size < 512)
        face_size *= 2;
    for (int f = 0; f < 6; f++) {
        faces[f] = rt_pixels_new((int64_t)face_size, (int64_t)face_size);
        if (!faces[f])
            goto done;
    }
    for (int f = 0; f < 6; f++) {
        for (int y = 0; y < face_size; y++) {
            for (int x = 0; x < face_size; x++) {
                float dir[3];
                float sample[3];
                uint32_t texel = 0xFF000000u;
                cubemap_face_uv_to_direction(f,
                                             ((float)x + 0.5f) / (float)face_size,
                                             ((float)y + 0.5f) / (float)face_size,
                                             &dir[0],
                                             &dir[1],
                                             &dir[2]);
                cubemap_hdr_sample_panorama(rgb, w, h, dir, sample);
                for (int c = 0; c < 3; c++) {
                    float scaled = sample[c] * e;
                    float mapped;
                    if (!(scaled > 0.0f))
                        scaled = 0.0f;
                    mapped = scaled / (1.0f + scaled);
                    {
                        int b = (int)(mapped * 255.0f + 0.5f);
                        if (b > 255)
                            b = 255;
                        texel |= (uint32_t)b << (c * 8);
                    }
                }
                rt_pixels_set(faces[f], (int64_t)x, (int64_t)y, (int64_t)texel);
            }
        }
    }
    cubemap = rt_cubemap3d_new(faces[0], faces[1], faces[2], faces[3], faces[4], faces[5]);
done:
    for (int f = 0; f < 6; f++) {
        if (faces[f] && rt_obj_release_check0(faces[f]))
            rt_obj_free(faces[f]);
    }
    free(rgb);
    return cubemap;
}

void *rt_cubemap3d_new(void *px, void *nx, void *py, void *ny, void *pz, void *nz) {
    void *faces[6] = {px, nx, py, ny, pz, nz};

    /* Validate: all faces must be Pixels handles */
    for (int i = 0; i < 6; i++) {
        if (!cubemap_pixels_valid(faces[i])) {
            rt_trap("CubeMap3D.New: all 6 faces must be valid Pixels objects");
            return NULL;
        }
    }

    /* Validate: all faces must be square and same dimensions */
    int64_t size = rt_pixels_width(faces[0]);
    if (size <= 0 || rt_pixels_height(faces[0]) != size) {
        rt_trap("CubeMap3D.New: faces must be square");
        return NULL;
    }
    if (size > CUBEMAP3D_MAX_FACE_SIZE) {
        rt_trap("CubeMap3D.New: face dimensions exceed supported maximum");
        return NULL;
    }

    for (int i = 1; i < 6; i++) {
        if (rt_pixels_width(faces[i]) != size || rt_pixels_height(faces[i]) != size) {
            rt_trap("CubeMap3D.New: all faces must have same dimensions");
            return NULL;
        }
    }

    rt_cubemap3d *cm =
        (rt_cubemap3d *)rt_obj_new_i64(RT_G3D_CUBEMAP3D_CLASS_ID, (int64_t)sizeof(rt_cubemap3d));
    if (!cm) {
        rt_trap("CubeMap3D.New: memory allocation failed");
        return NULL;
    }
    cm->vptr = NULL;
    for (int i = 0; i < 6; i++) {
        rt_obj_retain_maybe(faces[i]);
        cm->faces[i] = faces[i];
    }
    cm->face_size = size;
    cm->cache_identity = cubemap_next_cache_identity();
    for (int i = 0; i < 27; i++)
        cm->ibl_sh[i] = 0.0f;
    for (int m = 0; m < RT_CUBEMAP3D_IBL_MAX_MIPS; m++)
        for (int f = 0; f < 6; f++)
            cm->ibl_mips[m][f] = NULL;
    cm->ibl_mip_count = 0;
    cm->ibl_base_size = 0;
    cm->ibl_ready = 0;
    cm->ibl_identity = 0;
    rt_obj_set_finalizer(cm, cubemap_finalize);
    return cm;
}

//=============================================================================
// Software cube map sampling
//=============================================================================

/// @brief Bilinearly sample a cubemap along a world-space direction vector.
/// @details Picks the face whose major axis dominates the direction, converts
///   to per-face UV, then performs a cross-face bilinear tap: each of the
///   four sample positions is re-projected back to a direction vector so that
///   taps that fall outside the current face's UV range wrap naturally onto
///   adjacent faces. That avoids the "seam" artifacts a simple per-face clamp
///   would introduce at cube edges. Zero-length directions and null cubemaps
///   produce black output so callers can skip explicit guards.
/// @param out_r,out_g,out_b Output color channels in normalized [0.0, 1.0].
void rt_cubemap_sample(const rt_cubemap3d *cm,
                       float dx,
                       float dy,
                       float dz,
                       float *out_r,
                       float *out_g,
                       float *out_b) {
    if (!out_r || !out_g || !out_b)
        return;
    if (!cubemap_faces_valid(cm)) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    /* Guard against invalid or zero-length directions (undefined ray). */
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz) ||
        (fabsf(dx) < 1e-10f && fabsf(dy) < 1e-10f && fabsf(dz) < 1e-10f)) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    int face = 0;
    float u = 0.5f;
    float v = 0.5f;

    /* Sample face texture using public API */
    cubemap_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    if (face < 0 || face > 5) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    rt_pixels_impl *pv = cubemap_face_pixels_impl(cm->faces[face]);
    if (!pv) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    int64_t fw = pv->width;
    int64_t fh = pv->height;

    /* Bilinear interpolation for smooth cubemap sampling */
    float fx = u * (float)fw - 0.5f;
    float fy = v * (float)fh - 0.5f;
    int x0 = (int)floorf(fx);
    int y0 = (int)floorf(fy);
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    float sx = fx - (float)x0;
    float sy = fy - (float)y0;

    /* Bilinear taps follow the cubemap topology instead of clamping to one face. */
    float tap_u0 = ((float)x0 + 0.5f) / (float)fw;
    float tap_u1 = ((float)x1 + 0.5f) / (float)fw;
    float tap_v0 = ((float)y0 + 0.5f) / (float)fh;
    float tap_v1 = ((float)y1 + 0.5f) / (float)fh;
    float dir00[3];
    float dir10[3];
    float dir01[3];
    float dir11[3];
    uint32_t p00;
    uint32_t p10;
    uint32_t p01;
    uint32_t p11;

    cubemap_face_uv_to_direction(face, tap_u0, tap_v0, &dir00[0], &dir00[1], &dir00[2]);
    cubemap_face_uv_to_direction(face, tap_u1, tap_v0, &dir10[0], &dir10[1], &dir10[2]);
    cubemap_face_uv_to_direction(face, tap_u0, tap_v1, &dir01[0], &dir01[1], &dir01[2]);
    cubemap_face_uv_to_direction(face, tap_u1, tap_v1, &dir11[0], &dir11[1], &dir11[2]);

    p00 = cubemap_sample_nearest_rgba(cm, dir00[0], dir00[1], dir00[2]);
    p10 = cubemap_sample_nearest_rgba(cm, dir10[0], dir10[1], dir10[2]);
    p01 = cubemap_sample_nearest_rgba(cm, dir01[0], dir01[1], dir01[2]);
    p11 = cubemap_sample_nearest_rgba(cm, dir11[0], dir11[1], dir11[2]);

/* Extract channels and bilinear blend */
#define BL(ch, shift)                                                                              \
    do {                                                                                           \
        float c00 = (float)((p00 >> (shift)) & 0xFF);                                              \
        float c10 = (float)((p10 >> (shift)) & 0xFF);                                              \
        float c01 = (float)((p01 >> (shift)) & 0xFF);                                              \
        float c11 = (float)((p11 >> (shift)) & 0xFF);                                              \
        *(ch) =                                                                                    \
            ((c00 * (1 - sx) + c10 * sx) * (1 - sy) + (c01 * (1 - sx) + c11 * sx) * sy) / 255.0f;  \
    } while (0)

    BL(out_r, 24);
    BL(out_g, 16);
    BL(out_b, 8);
#undef BL
}

/// @brief Bilinearly sample a cubemap along a pre-normalized direction vector.
/// @details Internal Canvas3D hot-path variant of @ref rt_cubemap_sample. It preserves the same
///          cross-face bilinear tap behavior but skips the defensive input normalization because
///          callers have already sanitized the direction.
void rt_cubemap_sample_unit(const rt_cubemap3d *cm,
                            float dx,
                            float dy,
                            float dz,
                            float *out_r,
                            float *out_g,
                            float *out_b) {
    int face = 0;
    float u = 0.5f;
    float v = 0.5f;
    rt_pixels_impl *pv;
    int64_t fw;
    int64_t fh;
    float fx;
    float fy;
    int x0;
    int y0;
    int x1;
    int y1;
    float sx;
    float sy;
    float tap_u0;
    float tap_u1;
    float tap_v0;
    float tap_v1;
    float dir00[3];
    float dir10[3];
    float dir01[3];
    float dir11[3];
    uint32_t p00;
    uint32_t p10;
    uint32_t p01;
    uint32_t p11;

    if (!out_r || !out_g || !out_b)
        return;
    if (!cubemap_faces_valid(cm)) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    cubemap_unit_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    if (face < 0 || face > 5) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    pv = cubemap_face_pixels_impl(cm->faces[face]);
    if (!pv) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    fw = pv->width;
    fh = pv->height;
    fx = u * (float)fw - 0.5f;
    fy = v * (float)fh - 0.5f;
    x0 = (int)floorf(fx);
    y0 = (int)floorf(fy);
    x1 = x0 + 1;
    y1 = y0 + 1;
    sx = fx - (float)x0;
    sy = fy - (float)y0;
    tap_u0 = ((float)x0 + 0.5f) / (float)fw;
    tap_u1 = ((float)x1 + 0.5f) / (float)fw;
    tap_v0 = ((float)y0 + 0.5f) / (float)fh;
    tap_v1 = ((float)y1 + 0.5f) / (float)fh;
    cubemap_face_uv_to_direction(face, tap_u0, tap_v0, &dir00[0], &dir00[1], &dir00[2]);
    cubemap_face_uv_to_direction(face, tap_u1, tap_v0, &dir10[0], &dir10[1], &dir10[2]);
    cubemap_face_uv_to_direction(face, tap_u0, tap_v1, &dir01[0], &dir01[1], &dir01[2]);
    cubemap_face_uv_to_direction(face, tap_u1, tap_v1, &dir11[0], &dir11[1], &dir11[2]);
    p00 = cubemap_sample_nearest_rgba(cm, dir00[0], dir00[1], dir00[2]);
    p10 = cubemap_sample_nearest_rgba(cm, dir10[0], dir10[1], dir10[2]);
    p01 = cubemap_sample_nearest_rgba(cm, dir01[0], dir01[1], dir01[2]);
    p11 = cubemap_sample_nearest_rgba(cm, dir11[0], dir11[1], dir11[2]);

#define BL(ch, shift)                                                                              \
    do {                                                                                           \
        float c00 = (float)((p00 >> (shift)) & 0xFF);                                              \
        float c10 = (float)((p10 >> (shift)) & 0xFF);                                              \
        float c01 = (float)((p01 >> (shift)) & 0xFF);                                              \
        float c11 = (float)((p11 >> (shift)) & 0xFF);                                              \
        *(ch) =                                                                                    \
            ((c00 * (1 - sx) + c10 * sx) * (1 - sy) + (c01 * (1 - sx) + c11 * sx) * sy) / 255.0f;  \
    } while (0)

    BL(out_r, 24);
    BL(out_g, 16);
    BL(out_b, 8);
#undef BL
}

/// @brief Roughness-aware cubemap sample that fakes a prefiltered environment map.
/// @details Blurs the reflection by taking weighted taps around the base
///   direction in the local tangent plane — center tap plus four axis-aligned
///   taps, with four diagonal taps added for medium/high roughness. The `spread`
///   angle grows with `roughness`, widening the taps so rougher surfaces
///   receive a softer, more integrated reflection. Low roughness uses the
///   cheaper five-tap path because the diagonal taps converge closely to the
///   sharp sample. For `roughness <= 0.001`
///   we short-circuit to the sharp `rt_cubemap_sample` path. This is a CPU
///   approximation of the split-sum / prefiltered environment map trick used
///   by physically-based renderers and avoids the need to precompute per-mip
///   convolutions.
/// @param roughness Surface roughness in [0, 1]; 0 = mirror, 1 = fully diffuse.
void rt_cubemap_sample_roughness(const rt_cubemap3d *cm,
                                 float dx,
                                 float dy,
                                 float dz,
                                 float roughness,
                                 float *out_r,
                                 float *out_g,
                                 float *out_b) {
    static const float k_offsets[9][2] = {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {-1.0f, 0.0f},
        {0.0f, 1.0f},
        {0.0f, -1.0f},
        {0.70710678f, 0.70710678f},
        {-0.70710678f, 0.70710678f},
        {0.70710678f, -0.70710678f},
        {-0.70710678f, -0.70710678f},
    };
    static const float k_weights[9] = {4.0f, 1.5f, 1.5f, 1.5f, 1.5f, 1.0f, 1.0f, 1.0f, 1.0f};
    float len;
    float tx;
    float ty;
    float tz;
    float bx;
    float by;
    float bz;
    float accum_r = 0.0f;
    float accum_g = 0.0f;
    float accum_b = 0.0f;
    float total_w = 0.0f;
    float spread;
    int tap_count;

    if (!out_r || !out_g || !out_b) {
        return;
    }
    if (!cubemap_faces_valid(cm)) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    if (!isfinite(roughness) || roughness < 0.0f)
        roughness = 0.0f;
    if (roughness > 1.0f)
        roughness = 1.0f;
    if (!cubemap_normalize_direction(&dx, &dy, &dz)) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    if (roughness <= 0.001f) {
        rt_cubemap_sample(cm, dx, dy, dz, out_r, out_g, out_b);
        return;
    }

    if (fabsf(dz) < 0.999f) {
        tx = -dy;
        ty = dx;
        tz = 0.0f;
    } else {
        tx = 0.0f;
        ty = -dz;
        tz = dy;
    }
    len = sqrtf(tx * tx + ty * ty + tz * tz);
    if (!isfinite(len) || len <= 1e-8f) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    tx /= len;
    ty /= len;
    tz /= len;
    bx = dy * tz - dz * ty;
    by = dz * tx - dx * tz;
    bz = dx * ty - dy * tx;

    spread = roughness * roughness * 1.25f;
    tap_count = roughness < 0.35f ? 5 : 9;
    for (int i = 0; i < tap_count; i++) {
        float sample_dx = dx + tx * k_offsets[i][0] * spread + bx * k_offsets[i][1] * spread;
        float sample_dy = dy + ty * k_offsets[i][0] * spread + by * k_offsets[i][1] * spread;
        float sample_dz = dz + tz * k_offsets[i][0] * spread + bz * k_offsets[i][1] * spread;
        float sample_len =
            sqrtf(sample_dx * sample_dx + sample_dy * sample_dy + sample_dz * sample_dz);
        float sr;
        float sg;
        float sb;

        if (!isfinite(sample_len) || sample_len <= 1e-8f)
            continue;
        sample_dx /= sample_len;
        sample_dy /= sample_len;
        sample_dz /= sample_len;
        rt_cubemap_sample(cm, sample_dx, sample_dy, sample_dz, &sr, &sg, &sb);
        accum_r += sr * k_weights[i];
        accum_g += sg * k_weights[i];
        accum_b += sb * k_weights[i];
        total_w += k_weights[i];
    }

    if (total_w <= 1e-8f) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    *out_r = accum_r / total_w;
    *out_g = accum_g / total_w;
    *out_b = accum_b / total_w;
}

//=============================================================================
// Image-based lighting: SH-9 irradiance + GGX-prefiltered specular chain
//=============================================================================

/// @brief Nearest RGBA fetch from an arbitrary six-face Pixels array (no
///   whole-cubemap validation — used by the prefiltered IBL chain whose faces
///   are internally constructed and guaranteed square).
static uint32_t cubemap_faces_nearest_rgba(void *const faces[6], float dx, float dy, float dz) {
    int face = 0;
    float u = 0.5f;
    float v = 0.5f;
    rt_pixels_impl *pv;
    int xi;
    int yi;

    cubemap_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    if (face < 0 || face > 5)
        return 0;
    pv = cubemap_face_pixels_impl(faces[face]);
    if (!pv)
        return 0;
    if (u < 0.0f)
        u = 0.0f;
    else if (u > 1.0f)
        u = 1.0f;
    if (v < 0.0f)
        v = 0.0f;
    else if (v > 1.0f)
        v = 1.0f;
    xi = (int)floorf(u * (float)pv->width);
    yi = (int)floorf(v * (float)pv->height);
    if (xi >= (int)pv->width)
        xi = (int)pv->width - 1;
    if (yi >= (int)pv->height)
        yi = (int)pv->height - 1;
    if (xi < 0)
        xi = 0;
    if (yi < 0)
        yi = 0;
    return pv->data[(int64_t)yi * pv->width + xi];
}

/// @brief Cross-face bilinear sample from an arbitrary six-face Pixels array.
/// @details Same topology-aware tap scheme as rt_cubemap_sample: the four
///   bilinear taps are re-projected to directions so edge taps wrap onto
///   adjacent faces without seams.
static void cubemap_faces_bilinear(
    void *const faces[6], float dx, float dy, float dz, float *out_r, float *out_g, float *out_b) {
    int face = 0;
    float u = 0.5f;
    float v = 0.5f;
    rt_pixels_impl *pv;

    *out_r = *out_g = *out_b = 0.0f;
    if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz) ||
        (fabsf(dx) < 1e-10f && fabsf(dy) < 1e-10f && fabsf(dz) < 1e-10f))
        return;
    cubemap_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    if (face < 0 || face > 5)
        return;
    pv = cubemap_face_pixels_impl(faces[face]);
    if (!pv)
        return;

    {
        int64_t fw = pv->width;
        int64_t fh = pv->height;
        float fx = u * (float)fw - 0.5f;
        float fy = v * (float)fh - 0.5f;
        int x0 = (int)floorf(fx);
        int y0 = (int)floorf(fy);
        int x1 = x0 + 1;
        int y1 = y0 + 1;
        float sx = fx - (float)x0;
        float sy = fy - (float)y0;
        float tap_u0 = ((float)x0 + 0.5f) / (float)fw;
        float tap_u1 = ((float)x1 + 0.5f) / (float)fw;
        float tap_v0 = ((float)y0 + 0.5f) / (float)fh;
        float tap_v1 = ((float)y1 + 0.5f) / (float)fh;
        float dir[4][3];
        uint32_t p[4];

        cubemap_face_uv_to_direction(face, tap_u0, tap_v0, &dir[0][0], &dir[0][1], &dir[0][2]);
        cubemap_face_uv_to_direction(face, tap_u1, tap_v0, &dir[1][0], &dir[1][1], &dir[1][2]);
        cubemap_face_uv_to_direction(face, tap_u0, tap_v1, &dir[2][0], &dir[2][1], &dir[2][2]);
        cubemap_face_uv_to_direction(face, tap_u1, tap_v1, &dir[3][0], &dir[3][1], &dir[3][2]);
        for (int i = 0; i < 4; i++)
            p[i] = cubemap_faces_nearest_rgba(faces, dir[i][0], dir[i][1], dir[i][2]);

#define VIPER_CUBEMAP_BL(ch, shift)                                                                \
    do {                                                                                           \
        float c00 = (float)((p[0] >> (shift)) & 0xFF);                                             \
        float c10 = (float)((p[1] >> (shift)) & 0xFF);                                             \
        float c01 = (float)((p[2] >> (shift)) & 0xFF);                                             \
        float c11 = (float)((p[3] >> (shift)) & 0xFF);                                             \
        *(ch) =                                                                                    \
            ((c00 * (1 - sx) + c10 * sx) * (1 - sy) + (c01 * (1 - sx) + c11 * sx) * sy) / 255.0f;  \
    } while (0)
        VIPER_CUBEMAP_BL(out_r, 24);
        VIPER_CUBEMAP_BL(out_g, 16);
        VIPER_CUBEMAP_BL(out_b, 8);
#undef VIPER_CUBEMAP_BL
    }
}

/// @brief Evaluate the nine real SH basis functions at unit direction (x, y, z).
/// @details Order: Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22 — the same
///   order the projection and shader evaluation use.
static void cubemap_sh9_basis(float x, float y, float z, float out_basis[9]) {
    out_basis[0] = 0.282095f;
    out_basis[1] = 0.488603f * y;
    out_basis[2] = 0.488603f * z;
    out_basis[3] = 0.488603f * x;
    out_basis[4] = 1.092548f * x * y;
    out_basis[5] = 1.092548f * y * z;
    out_basis[6] = 0.315392f * (3.0f * z * z - 1.0f);
    out_basis[7] = 1.092548f * x * z;
    out_basis[8] = 0.546274f * (x * x - y * y);
}

/// @brief Project the cubemap's radiance onto SH-9 irradiance coefficients.
/// @details Ramamoorthi-Hanrahan irradiance environment maps: project radiance
///   L_lm over a fixed 32x32 grid per face with per-texel solid-angle weights,
///   normalize the total solid angle to 4*pi (so a constant environment yields
///   an exact DC term), then fold the cosine-lobe convolution factors A_l and
///   the Lambertian 1/pi so the stored coefficients evaluate directly to
///   "reflected color per unit albedo".
static void cubemap_project_sh9(const rt_cubemap3d *cm, float out_sh[27]) {
    /* A_l / pi: l=0 -> 1, l=1 -> 2/3, l=2 -> 1/4 */
    static const float k_al_over_pi[9] = {
        1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
    const int grid = 32;
    double accum[27];
    double total_weight = 0.0;

    for (int i = 0; i < 27; i++)
        accum[i] = 0.0;
    for (int face = 0; face < 6; face++) {
        for (int gy = 0; gy < grid; gy++) {
            for (int gx = 0; gx < grid; gx++) {
                float u = ((float)gx + 0.5f) / (float)grid;
                float v = ((float)gy + 0.5f) / (float)grid;
                /* Face coords in [-1, 1] for the solid-angle weight. */
                float fu = u * 2.0f - 1.0f;
                float fv = v * 2.0f - 1.0f;
                float weight = 1.0f / powf(fu * fu + fv * fv + 1.0f, 1.5f);
                float dx;
                float dy;
                float dz;
                float basis[9];
                float r;
                float g;
                float b;

                cubemap_face_uv_to_direction(face, u, v, &dx, &dy, &dz);
                rt_cubemap_sample(cm, dx, dy, dz, &r, &g, &b);
                cubemap_sh9_basis(dx, dy, dz, basis);
                for (int k = 0; k < 9; k++) {
                    float wb = basis[k] * weight;
                    accum[k * 3 + 0] += (double)(r * wb);
                    accum[k * 3 + 1] += (double)(g * wb);
                    accum[k * 3 + 2] += (double)(b * wb);
                }
                total_weight += (double)weight;
            }
        }
    }
    if (total_weight <= 1e-12) {
        for (int i = 0; i < 27; i++)
            out_sh[i] = 0.0f;
        return;
    }
    /* Normalize integrated solid angle to exactly 4*pi, apply A_l / pi. */
    {
        double norm = (4.0 * 3.14159265358979323846) / total_weight;
        for (int k = 0; k < 9; k++) {
            double scale = norm * (double)k_al_over_pi[k];
            out_sh[k * 3 + 0] = (float)(accum[k * 3 + 0] * scale);
            out_sh[k * 3 + 1] = (float)(accum[k * 3 + 1] * scale);
            out_sh[k * 3 + 2] = (float)(accum[k * 3 + 2] * scale);
        }
    }
}

/// @brief Evaluate stored SH-9 irradiance coefficients along a unit normal.
void rt_sh9_eval_irradiance(const float sh[27], float nx, float ny, float nz, float *out_rgb) {
    float basis[9];

    if (!out_rgb)
        return;
    out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
    if (!sh)
        return;
    if (!cubemap_normalize_direction(&nx, &ny, &nz))
        return;
    cubemap_sh9_basis(nx, ny, nz, basis);
    for (int k = 0; k < 9; k++) {
        out_rgb[0] += sh[k * 3 + 0] * basis[k];
        out_rgb[1] += sh[k * 3 + 1] * basis[k];
        out_rgb[2] += sh[k * 3 + 2] * basis[k];
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(out_rgb[i]) || out_rgb[i] < 0.0f)
            out_rgb[i] = 0.0f;
    }
}

/// @brief Van der Corput radical inverse for the deterministic Hammersley set.
static float cubemap_radical_inverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return (float)bits * 2.3283064365386963e-10f; /* 1 / 2^32 */
}

/// @brief GGX-prefilter one specular mip level into six freshly allocated Pixels
///   faces (owned creation references written into @p out_faces).
/// @details Split-sum first term: for every output texel the reflection
///   direction doubles as both normal and view vector (the standard N = V = R
///   approximation), and radiance is integrated over GGX-importance-sampled
///   half vectors from a deterministic Hammersley sequence. Roughness 0 is a
///   plain bilinear resample (perfect mirror). Returns 1 on success; on
///   failure all allocated faces are released and 0 is returned.
static int cubemap_prefilter_level(const rt_cubemap3d *cm,
                                   float roughness,
                                   int out_size,
                                   void *out_faces[6]) {
    int sample_count = roughness <= 0.35f ? 64 : 128;

    for (int f = 0; f < 6; f++)
        out_faces[f] = NULL;
    for (int f = 0; f < 6; f++) {
        void *pixels = rt_pixels_new((int64_t)out_size, (int64_t)out_size);
        rt_pixels_impl *pv = pixels ? rt_pixels_checked_impl_or_null(pixels) : NULL;

        if (!pv) {
            for (int r = 0; r < 6; r++)
                cubemap_release_face_slot(&out_faces[r]);
            return 0;
        }
        out_faces[f] = pixels;
        for (int y = 0; y < out_size; y++) {
            for (int x = 0; x < out_size; x++) {
                float u = ((float)x + 0.5f) / (float)out_size;
                float v = ((float)y + 0.5f) / (float)out_size;
                float nx;
                float ny;
                float nz;
                float acc_r = 0.0f;
                float acc_g = 0.0f;
                float acc_b = 0.0f;

                cubemap_face_uv_to_direction(f, u, v, &nx, &ny, &nz);
                if (roughness <= 0.001f) {
                    rt_cubemap_sample(cm, nx, ny, nz, &acc_r, &acc_g, &acc_b);
                } else {
                    /* Tangent frame around N. */
                    float tx;
                    float ty;
                    float tz;
                    float bx;
                    float by;
                    float bz;
                    float total = 0.0f;
                    float a = roughness * roughness;

                    if (fabsf(nz) < 0.999f) {
                        tx = -ny;
                        ty = nx;
                        tz = 0.0f;
                    } else {
                        tx = 0.0f;
                        ty = -nz;
                        tz = ny;
                    }
                    {
                        float tlen = sqrtf(tx * tx + ty * ty + tz * tz);
                        if (tlen <= 1e-8f) {
                            tx = 1.0f;
                            ty = 0.0f;
                            tz = 0.0f;
                            tlen = 1.0f;
                        }
                        tx /= tlen;
                        ty /= tlen;
                        tz /= tlen;
                    }
                    bx = ny * tz - nz * ty;
                    by = nz * tx - nx * tz;
                    bz = nx * ty - ny * tx;

                    for (int s = 0; s < sample_count; s++) {
                        float xi1 = ((float)s + 0.5f) / (float)sample_count;
                        float xi2 = cubemap_radical_inverse((uint32_t)s);
                        float phi = 2.0f * 3.14159265358979323846f * xi1;
                        float cos_theta = sqrtf((1.0f - xi2) / (1.0f + (a * a - 1.0f) * xi2));
                        float sin_theta = sqrtf(fmaxf(1.0f - cos_theta * cos_theta, 0.0f));
                        float hx_t = cosf(phi) * sin_theta;
                        float hy_t = sinf(phi) * sin_theta;
                        float hz_t = cos_theta;
                        /* Half vector to world space. */
                        float hx = tx * hx_t + bx * hy_t + nx * hz_t;
                        float hy = ty * hx_t + by * hy_t + ny * hz_t;
                        float hz = tz * hx_t + bz * hy_t + nz * hz_t;
                        /* L = 2 (N.H) H - N  (V = N). */
                        float ndh = nx * hx + ny * hy + nz * hz;
                        float lx = 2.0f * ndh * hx - nx;
                        float ly = 2.0f * ndh * hy - ny;
                        float lz = 2.0f * ndh * hz - nz;
                        float ndl = nx * lx + ny * ly + nz * lz;

                        if (ndl <= 0.0f)
                            continue;
                        {
                            float sr;
                            float sg;
                            float sb;
                            rt_cubemap_sample(cm, lx, ly, lz, &sr, &sg, &sb);
                            acc_r += sr * ndl;
                            acc_g += sg * ndl;
                            acc_b += sb * ndl;
                            total += ndl;
                        }
                    }
                    if (total > 1e-6f) {
                        acc_r /= total;
                        acc_g /= total;
                        acc_b /= total;
                    } else {
                        rt_cubemap_sample(cm, nx, ny, nz, &acc_r, &acc_g, &acc_b);
                    }
                }
                {
                    uint32_t rr = (uint32_t)(fminf(fmaxf(acc_r, 0.0f), 1.0f) * 255.0f + 0.5f);
                    uint32_t gg = (uint32_t)(fminf(fmaxf(acc_g, 0.0f), 1.0f) * 255.0f + 0.5f);
                    uint32_t bb = (uint32_t)(fminf(fmaxf(acc_b, 0.0f), 1.0f) * 255.0f + 0.5f);
                    pv->data[(int64_t)y * pv->width + x] =
                        (rr << 24) | (gg << 16) | (bb << 8) | 0xFFu;
                }
            }
        }
    }
    return 1;
}

/// @brief Lazily compute the cubemap's IBL payload; idempotent.
/// @details Runs synchronously on the calling thread the first time an IBL-enabled
///   PBR draw needs the cubemap. Canvas setters intentionally avoid calling this
///   path so toggling IBL or swapping skyboxes remains cheap. Cost is bounded by
///   the fixed 128-base prefilter chain and the 32x32 SH projection grid,
///   independent of the source face size.
int rt_cubemap3d_ensure_ibl(void *cubemap) {
    rt_cubemap3d *cm = (rt_cubemap3d *)cubemap;
    int base;
    int mips;

    if (!cubemap_faces_valid(cm))
        return 0;
    if (cm->ibl_ready)
        return 1;

    base = cm->face_size < 128 ? (int)cm->face_size : 128;
    if (base < 4)
        base = 4;
    mips = 1;
    while (mips < RT_CUBEMAP3D_IBL_MAX_MIPS && (base >> mips) >= 4)
        mips++;

    cubemap_project_sh9(cm, cm->ibl_sh);
    for (int m = 0; m < mips; m++) {
        float roughness = mips > 1 ? (float)m / (float)(mips - 1) : 0.0f;
        int size = base >> m;

        if (!cubemap_prefilter_level(cm, roughness, size, cm->ibl_mips[m])) {
            for (int mm = 0; mm <= m; mm++)
                for (int f = 0; f < 6; f++)
                    cubemap_release_face_slot(&cm->ibl_mips[mm][f]);
            for (int i = 0; i < 27; i++)
                cm->ibl_sh[i] = 0.0f;
            return 0;
        }
    }
    cm->ibl_mip_count = mips;
    cm->ibl_base_size = base;
    cm->ibl_identity = cubemap_next_cache_identity();
    cm->ibl_ready = 1;
    return 1;
}

/// @brief Sample the prefiltered specular chain with trilinear roughness blending.
void rt_cubemap_sample_ibl(const rt_cubemap3d *cm,
                           float dx,
                           float dy,
                           float dz,
                           float roughness,
                           float *out_r,
                           float *out_g,
                           float *out_b) {
    float lod;
    int k0;
    int k1;
    float t;
    float r0;
    float g0;
    float b0;
    float r1;
    float g1;
    float b1;

    if (!out_r || !out_g || !out_b)
        return;
    if (!cubemap_handle_valid(cm) || !cm->ibl_ready || cm->ibl_mip_count <= 0) {
        rt_cubemap_sample_roughness(cm, dx, dy, dz, roughness, out_r, out_g, out_b);
        return;
    }
    if (!isfinite(roughness) || roughness < 0.0f)
        roughness = 0.0f;
    if (roughness > 1.0f)
        roughness = 1.0f;
    if (!cubemap_normalize_direction(&dx, &dy, &dz)) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    lod = roughness * (float)(cm->ibl_mip_count - 1);
    k0 = (int)floorf(lod);
    if (k0 < 0)
        k0 = 0;
    if (k0 > cm->ibl_mip_count - 1)
        k0 = cm->ibl_mip_count - 1;
    k1 = k0 + 1 < cm->ibl_mip_count ? k0 + 1 : k0;
    t = lod - (float)k0;
    cubemap_faces_bilinear(cm->ibl_mips[k0], dx, dy, dz, &r0, &g0, &b0);
    if (k1 != k0 && t > 0.0001f) {
        cubemap_faces_bilinear(cm->ibl_mips[k1], dx, dy, dz, &r1, &g1, &b1);
        *out_r = r0 * (1.0f - t) + r1 * t;
        *out_g = g0 * (1.0f - t) + g1 * t;
        *out_b = b0 * (1.0f - t) + b1 * t;
    } else {
        *out_r = r0;
        *out_g = g0;
        *out_b = b0;
    }
}

//=============================================================================
// Canvas3D skybox
//=============================================================================

/// @brief Defensively validate a canvas's skybox slot: clear it (and invalidate the skybox
///   cache) when the cubemap handle is invalid or the cubemap is incomplete. Returns 1 if
///   it cleared the slot, else 0.
static int canvas3d_repair_skybox_slot(rt_canvas3d *c) {
    if (!c || !c->skybox)
        return 0;
    if (!cubemap_handle_valid(c->skybox)) {
        rt_g3d_ref_slot_clear_unowned((void **)&c->skybox);
        rt_canvas3d_invalidate_skybox_cache(c);
        return 1;
    }
    if (!rt_cubemap3d_is_complete(c->skybox)) {
        rt_g3d_ref_slot_release((void **)&c->skybox);
        rt_canvas3d_invalidate_skybox_cache(c);
        return 1;
    }
    return 0;
}

/// @brief Set a cube map as the canvas skybox (drawn behind all geometry).
void rt_canvas3d_set_skybox(void *canvas, void *cubemap) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    canvas3d_repair_skybox_slot(c);
    if (cubemap && !rt_cubemap3d_is_complete(cubemap))
        return;
    if (c->skybox == (rt_cubemap3d *)cubemap)
        return;
    rt_obj_retain_maybe(cubemap);
    if (cubemap_handle_valid(c->skybox))
        rt_g3d_ref_slot_release((void **)&c->skybox);
    c->skybox = (rt_cubemap3d *)cubemap;
    rt_canvas3d_invalidate_skybox_cache(c);
}

/// @brief Remove the skybox from the canvas (reverts to solid clear color).
void rt_canvas3d_clear_skybox(void *canvas) {
    rt_canvas3d *c = rt_canvas3d_checked_or_stack(canvas);
    if (!c)
        return;
    if (canvas3d_repair_skybox_slot(c))
        return;
    if (cubemap_handle_valid(c->skybox))
        rt_g3d_ref_slot_release((void **)&c->skybox);
    else
        c->skybox = NULL;
    rt_canvas3d_invalidate_skybox_cache(c);
}

//=============================================================================
// Material3D env map + reflectivity
//=============================================================================

/// @brief Assign a cube map as the environment reflection map for a material.
void rt_material3d_set_env_map(void *obj, void *cubemap) {
    rt_material3d_assign_env_map_checked(obj, cubemap);
}

/// @brief Set the environment reflection strength for a material (0.0–1.0).
void rt_material3d_set_reflectivity(void *obj, double r) {
    rt_material3d *mat = (rt_material3d *)rt_g3d_checked_or_null(obj, RT_G3D_MATERIAL3D_CLASS_ID);
    if (!mat)
        return;
    if (!isfinite(r))
        r = 0.0;
    if (r < 0.0)
        r = 0.0;
    if (r > 1.0)
        r = 1.0;
    mat->reflectivity = r;
}

/// @brief Get the current environment reflection strength of a material.
double rt_material3d_get_reflectivity(void *obj) {
    rt_material3d *mat = (rt_material3d *)rt_g3d_checked_or_null(obj, RT_G3D_MATERIAL3D_CLASS_ID);
    double value;
    if (!mat)
        return 0.0;
    value = mat->reflectivity;
    if (!isfinite(value))
        return 0.0;
    if (value < 0.0)
        return 0.0;
    if (value > 1.0)
        return 1.0;
    return value;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
