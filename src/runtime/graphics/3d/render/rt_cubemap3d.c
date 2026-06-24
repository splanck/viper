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
#include "rt_pixels.h"
#include "rt_pixels_internal.h"
#include "rt_platform.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
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

static int64_t g_next_cubemap_cache_identity = 1;

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
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Release a cubemap face only when it still points at a Pixels object.
static void cubemap_release_face_slot(void **slot) {
    if (!slot || !*slot)
        return;
    if (!cubemap_pixels_valid(*slot)) {
        *slot = NULL;
        return;
    }
    cubemap_release_ref(slot);
}

/// @brief Return a process-unique nonzero cache identity for skybox/env-map invalidation.
static uint64_t cubemap_next_cache_identity(void) {
    uint64_t id =
        (uint64_t)__atomic_fetch_add(&g_next_cubemap_cache_identity, (int64_t)1, __ATOMIC_RELAXED);
    if (id == 0) {
        id = (uint64_t)__atomic_fetch_add(
            &g_next_cubemap_cache_identity, (int64_t)1, __ATOMIC_RELAXED);
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
///          trip a null-deref in the inner release.
static void cubemap_finalize(void *obj) {
    rt_cubemap3d *cm = (rt_cubemap3d *)obj;
    if (!cm)
        return;
    for (int i = 0; i < 6; i++)
        cubemap_release_face_slot(&cm->faces[i]);
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
// Canvas3D skybox
//=============================================================================

/// @brief Defensively validate a canvas's skybox slot: clear it (and invalidate the skybox
///   cache) when the cubemap handle is invalid or the cubemap is incomplete. Returns 1 if
///   it cleared the slot, else 0.
static int canvas3d_repair_skybox_slot(rt_canvas3d *c) {
    if (!c || !c->skybox)
        return 0;
    if (!cubemap_handle_valid(c->skybox)) {
        c->skybox = NULL;
        rt_canvas3d_invalidate_skybox_cache(c);
        return 1;
    }
    if (!rt_cubemap3d_is_complete(c->skybox)) {
        if (rt_obj_release_check0(c->skybox))
            rt_obj_free(c->skybox);
        c->skybox = NULL;
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
    if (cubemap_handle_valid(c->skybox) && rt_obj_release_check0(c->skybox))
        rt_obj_free(c->skybox);
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
    if (cubemap_handle_valid(c->skybox) && rt_obj_release_check0(c->skybox))
        rt_obj_free(c->skybox);
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
