//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_cubemap3d.c
// Purpose: Viper.Graphics3D.CubeMap3D — 6-face cube map texture for
//   skybox rendering and environment reflections.
//
// Key invariants:
//   - All 6 faces must be square and the same dimensions
//   - Faces are retained Pixels objects while the cubemap is alive.
//   - Face order: +X, -X, +Y, -Y, +Z, -Z
//
// Links: rt_canvas3d.h, rt_canvas3d_internal.h, plans/3d/11-cube-maps.md
//
//===----------------------------------------------------------------------===//

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_canvas3d.h"
#include "rt_canvas3d_internal.h"
#include "rt_platform.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

extern void *rt_obj_new_i64(int64_t class_id, int64_t byte_size);
extern void rt_obj_set_finalizer(void *obj, void (*fn)(void *));
extern void rt_obj_retain_maybe(void *obj);
extern int rt_obj_release_check0(void *obj);
extern void rt_obj_free(void *obj);
#include "rt_trap.h"

extern int64_t rt_pixels_width(void *pixels);
extern int64_t rt_pixels_height(void *pixels);
extern int64_t rt_pixels_get(void *pixels, int64_t x, int64_t y);

static volatile int64_t g_next_cubemap_cache_identity = 1;

static void cubemap_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

static void cubemap_finalize(void *obj) {
    rt_cubemap3d *cm = (rt_cubemap3d *)obj;
    if (!cm)
        return;
    for (int i = 0; i < 6; i++)
        cubemap_release_ref(&cm->faces[i]);
}

static void cubemap_direction_to_face_uv(
    float dx, float dy, float dz, int *out_face, float *out_u, float *out_v) {
    float ax = fabsf(dx), ay = fabsf(dy), az = fabsf(dz);
    int face;
    float u, v, ma;

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

static void cubemap_face_uv_to_direction(
    int face, float u, float v, float *out_dx, float *out_dy, float *out_dz) {
    float uu = u * 2.0f - 1.0f;
    float vv = v * 2.0f - 1.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float dz = 0.0f;
    float len;

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
    if (len > 1e-8f) {
        dx /= len;
        dy /= len;
        dz /= len;
    }
    *out_dx = dx;
    *out_dy = dy;
    *out_dz = dz;
}

static uint32_t cubemap_sample_nearest_rgba(const rt_cubemap3d *cm, float dx, float dy, float dz) {
    int face = 0;
    float u = 0.5f;
    float v = 0.5f;
    void *face_pixels;
    int64_t fw;
    int64_t fh;
    int xi;
    int yi;

    if (!cm)
        return 0;

    cubemap_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    face_pixels = cm->faces[face];
    if (!face_pixels)
        return 0;

    fw = rt_pixels_width(face_pixels);
    fh = rt_pixels_height(face_pixels);
    if (fw <= 0 || fh <= 0)
        return 0;

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
    return (uint32_t)rt_pixels_get(face_pixels, xi, yi);
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

    /* Validate: all faces must be non-null */
    for (int i = 0; i < 6; i++) {
        if (!faces[i]) {
            rt_trap("CubeMap3D.New: all 6 faces must be non-null");
            return NULL;
        }
    }

    /* Validate: all faces must be square and same dimensions */
    int64_t size = rt_pixels_width(faces[0]);
    if (size <= 0 || rt_pixels_height(faces[0]) != size) {
        rt_trap("CubeMap3D.New: faces must be square");
        return NULL;
    }

    for (int i = 1; i < 6; i++) {
        if (rt_pixels_width(faces[i]) != size || rt_pixels_height(faces[i]) != size) {
            rt_trap("CubeMap3D.New: all faces must have same dimensions");
            return NULL;
        }
    }

    rt_cubemap3d *cm = (rt_cubemap3d *)rt_obj_new_i64(0, (int64_t)sizeof(rt_cubemap3d));
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
    cm->cache_identity =
        (uint64_t)__atomic_fetch_add(&g_next_cubemap_cache_identity, (int64_t)1, __ATOMIC_RELAXED);
    if (cm->cache_identity == 0)
        cm->cache_identity =
            (uint64_t)__atomic_fetch_add(&g_next_cubemap_cache_identity, (int64_t)1, __ATOMIC_RELAXED);
    rt_obj_set_finalizer(cm, cubemap_finalize);
    return cm;
}

//=============================================================================
// Software cube map sampling
//=============================================================================

/* Sample a cube map given a 3D direction vector.
 * Returns color as floats [0.0, 1.0]. */
void rt_cubemap_sample(const rt_cubemap3d *cm,
                       float dx,
                       float dy,
                       float dz,
                       float *out_r,
                       float *out_g,
                       float *out_b) {
    if (!cm) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    /* Guard against zero-length direction (undefined ray) */
    if (fabsf(dx) < 1e-10f && fabsf(dy) < 1e-10f && fabsf(dz) < 1e-10f) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    int face = 0;
    float u = 0.5f;
    float v = 0.5f;

    /* Sample face texture using public API */
    cubemap_direction_to_face_uv(dx, dy, dz, &face, &u, &v);
    void *face_pixels = cm->faces[face];
    if (!face_pixels) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    int64_t fw = rt_pixels_width(face_pixels);
    int64_t fh = rt_pixels_height(face_pixels);

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

    if (!out_r || !out_g || !out_b) {
        return;
    }
    if (!cm) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }

    if (roughness <= 0.001f) {
        rt_cubemap_sample(cm, dx, dy, dz, out_r, out_g, out_b);
        return;
    }

    len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len <= 1e-8f) {
        *out_r = *out_g = *out_b = 0.0f;
        return;
    }
    dx /= len;
    dy /= len;
    dz /= len;

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
    if (len <= 1e-8f) {
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
    for (int i = 0; i < 9; i++) {
        float sample_dx = dx + tx * k_offsets[i][0] * spread + bx * k_offsets[i][1] * spread;
        float sample_dy = dy + ty * k_offsets[i][0] * spread + by * k_offsets[i][1] * spread;
        float sample_dz = dz + tz * k_offsets[i][0] * spread + bz * k_offsets[i][1] * spread;
        float sample_len = sqrtf(
            sample_dx * sample_dx + sample_dy * sample_dy + sample_dz * sample_dz);
        float sr;
        float sg;
        float sb;

        if (sample_len <= 1e-8f)
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

/// @brief Set a cube map as the canvas skybox (drawn behind all geometry).
void rt_canvas3d_set_skybox(void *canvas, void *cubemap) {
    if (!canvas)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas;
    if (c->skybox == (rt_cubemap3d *)cubemap)
        return;
    rt_obj_retain_maybe(cubemap);
    if (c->skybox && rt_obj_release_check0(c->skybox))
        rt_obj_free(c->skybox);
    c->skybox = (rt_cubemap3d *)cubemap;
}

/// @brief Remove the skybox from the canvas (reverts to solid clear color).
void rt_canvas3d_clear_skybox(void *canvas) {
    if (!canvas)
        return;
    rt_canvas3d *c = (rt_canvas3d *)canvas;
    if (c->skybox && rt_obj_release_check0(c->skybox))
        rt_obj_free(c->skybox);
    c->skybox = NULL;
}

//=============================================================================
// Material3D env map + reflectivity
//=============================================================================

/// @brief Assign a cube map as the environment reflection map for a material.
void rt_material3d_set_env_map(void *obj, void *cubemap) {
    if (!obj)
        return;
    rt_material3d *mat = (rt_material3d *)obj;
    if (mat->env_map == cubemap)
        return;
    rt_obj_retain_maybe(cubemap);
    if (mat->env_map && rt_obj_release_check0(mat->env_map))
        rt_obj_free(mat->env_map);
    mat->env_map = cubemap;
}

/// @brief Set the environment reflection strength for a material (0.0–1.0).
void rt_material3d_set_reflectivity(void *obj, double r) {
    if (!obj)
        return;
    if (r < 0.0)
        r = 0.0;
    if (r > 1.0)
        r = 1.0;
    ((rt_material3d *)obj)->reflectivity = r;
}

/// @brief Get the current environment reflection strength of a material.
double rt_material3d_get_reflectivity(void *obj) {
    if (!obj)
        return 0.0;
    return ((rt_material3d *)obj)->reflectivity;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
