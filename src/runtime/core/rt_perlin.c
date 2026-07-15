//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/core/rt_perlin.c
// Purpose: Implements Ken Perlin's improved noise algorithm (2002) for the
//          Viper runtime. Supports 2D and 3D noise evaluation and octave/fractal
//          layering (fBm) for procedural terrain, textures, and animations.
//
// Key invariants:
//   - The permutation table is seeded from the caller-supplied integer seed
//     using a Fisher-Yates shuffle; the same seed reproduces the same field.
//   - The doubled permutation table (perm[512]) avoids modular arithmetic in
//     the inner loop; indices are masked with 0xFF before lookup.
//   - Noise values are in the range approximately [-1, 1] for 3D and [-0.7, 0.7]
//     for 2D; callers should normalise if a [0, 1] range is required.
//   - All noise functions are pure (no side effects); concurrent calls on
//     different objects are safe without synchronization.
//
// Ownership/Lifetime:
//   - Perlin instances are heap-allocated via rt_obj_new_i64 and managed by
//     the runtime GC; the finalizer is a no-op (no dynamic sub-allocations).
//   - Caller does not need to free instances explicitly.
//
// Links: src/runtime/core/rt_perlin.h (public API),
//        src/runtime/core/rt_easing.c (complementary interpolation utilities)
//
//===----------------------------------------------------------------------===//

#include "rt_perlin.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RT_PERLIN_MAX_OCTAVES 16

typedef struct rt_perlin_impl {
    void **vptr;
    uint8_t perm[512]; // Doubled permutation table
} rt_perlin_impl;

/// @brief Ken Perlin's 6t⁵−15t⁴+10t³ fade curve for smooth interpolation.
/// @details Smoothstep replacement chosen so the first AND second derivatives are zero
///          at t=0 and t=1 — produces visually continuous gradients across cell
///          boundaries in the noise field.
static double fade(double t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

/// @brief Linear interpolation between @p a and @p b parameterised by @p t.
static double lerp(double t, double a, double b) {
    return a + t * (b - a);
}

/// @brief 3D gradient hash: dot product of (x, y, z) with one of 12 fixed gradient vectors.
/// @details Selects from the 12 edge-midpoint vectors of a cube (the original Perlin '02
///          gradient set) by indexing into the low 4 bits of @p hash. The bit-twiddled
///          form avoids a lookup table — `(h & 1) ? -u : u` flips signs based on the
///          hash bits, and the `u`/`v` selection picks two of the three input
///          coordinates per gradient.
static double grad3(int hash, double x, double y, double z) {
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

/// @brief 2D gradient hash: dot product of (x, y) with one of four diagonal gradients.
/// @details The four cases cover the diagonals (+x+y, -x+y, +x-y, -x-y), which is the
///          standard 2D Perlin gradient set.
static double grad2(int hash, double x, double y) {
    int h = hash & 3;
    switch (h) {
        case 0:
            return x + y;
        case 1:
            return -x + y;
        case 2:
            return x - y;
        case 3:
            return -x - y;
    }
    return 0;
}

/// @brief Convert a coordinate to a safe Perlin cell index and fractional offset.
static int perlin_cell(double value, int *index_out, double *fraction_out) {
    if (!isfinite(value))
        return 0;
    double cell = floor(value);
    if (cell < (double)INT_MIN || cell > (double)INT_MAX)
        return 0;
    *index_out = ((int)cell) & 255;
    *fraction_out = value - cell;
    return 1;
}

static int64_t perlin_clamp_octaves(int64_t octaves) {
    if (octaves <= 0)
        return 0;
    return octaves > RT_PERLIN_MAX_OCTAVES ? RT_PERLIN_MAX_OCTAVES : octaves;
}

static double perlin_finish_octaves(double total, double max_value) {
    if (!isfinite(total) || !isfinite(max_value) || max_value == 0.0)
        return 0.0;
    return total / max_value;
}

/// @brief GC finalizer for Perlin generators; no dynamic allocations to release.
static void rt_perlin_finalize(void *obj) {
    // No dynamic allocations beyond the object itself
    (void)obj;
}

void *rt_perlin_new(int64_t seed) {
    rt_perlin_impl *p = (rt_perlin_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_perlin_impl));
    if (!p)
        return NULL;
    p->vptr = NULL;

    // Initialize permutation table with 0..255
    uint8_t base[256];
    for (int i = 0; i < 256; ++i)
        base[i] = (uint8_t)i;

    // Fisher-Yates shuffle with seed
    uint64_t s = (uint64_t)seed;
    for (int i = 255; i > 0; --i) {
        // Simple LCG for deterministic shuffle
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        int j = (int)((s >> 16) % (uint64_t)(i + 1));
        uint8_t tmp = base[i];
        base[i] = base[j];
        base[j] = tmp;
    }

    // Double the table for wrapping
    for (int i = 0; i < 256; ++i) {
        p->perm[i] = base[i];
        p->perm[i + 256] = base[i];
    }

    rt_obj_set_finalizer(p, rt_perlin_finalize);
    return p;
}

/// @brief Compute 2D Perlin noise at the given coordinates.
/// @details Uses gradient interpolation with smoothstep fade curves for seamless
///          spatial transitions. Output is approximately in [-1, 1]. The noise
///          is deterministic — same (obj, x, y) always produces the same value.
/// @param obj PerlinNoise object containing the permutation table.
/// @param x X coordinate in noise space.
/// @param y Y coordinate in noise space.
/// @return Noise value (approximately [-1, 1]).
double rt_perlin_noise2d(void *obj, double x, double y) {
    if (!obj)
        return 0.0;
    rt_perlin_impl *p = (rt_perlin_impl *)obj;

    int xi = 0;
    int yi = 0;
    double xf = 0.0;
    double yf = 0.0;
    if (!perlin_cell(x, &xi, &xf) || !perlin_cell(y, &yi, &yf))
        return 0.0;

    double u = fade(xf);
    double v = fade(yf);

    int aa = p->perm[p->perm[xi] + yi];
    int ab = p->perm[p->perm[xi] + yi + 1];
    int ba = p->perm[p->perm[xi + 1] + yi];
    int bb = p->perm[p->perm[xi + 1] + yi + 1];

    double x1 = lerp(u, grad2(aa, xf, yf), grad2(ba, xf - 1, yf));
    double x2 = lerp(u, grad2(ab, xf, yf - 1), grad2(bb, xf - 1, yf - 1));
    return lerp(v, x1, x2);
}

/// @brief Compute 3D Perlin noise at the given coordinates.
/// @details Extends the 2D algorithm to three dimensions with trilinear
///          gradient interpolation. Useful for volumetric effects (clouds, fog).
/// @param obj PerlinNoise object containing the permutation table.
/// @param x X coordinate in noise space.
/// @param y Y coordinate in noise space.
/// @param z Z coordinate in noise space.
/// @return Noise value (approximately [-1, 1]).
double rt_perlin_noise3d(void *obj, double x, double y, double z) {
    if (!obj)
        return 0.0;
    rt_perlin_impl *p = (rt_perlin_impl *)obj;

    int xi = 0;
    int yi = 0;
    int zi = 0;
    double xf = 0.0;
    double yf = 0.0;
    double zf = 0.0;
    if (!perlin_cell(x, &xi, &xf) || !perlin_cell(y, &yi, &yf) || !perlin_cell(z, &zi, &zf))
        return 0.0;

    double u = fade(xf);
    double v = fade(yf);
    double w = fade(zf);

    int a = p->perm[xi] + yi;
    int aa = p->perm[a] + zi;
    int ab = p->perm[a + 1] + zi;
    int b = p->perm[xi + 1] + yi;
    int ba = p->perm[b] + zi;
    int bb = p->perm[b + 1] + zi;

    return lerp(
        w,
        lerp(v,
             lerp(u, grad3(p->perm[aa], xf, yf, zf), grad3(p->perm[ba], xf - 1, yf, zf)),
             lerp(u, grad3(p->perm[ab], xf, yf - 1, zf), grad3(p->perm[bb], xf - 1, yf - 1, zf))),
        lerp(v,
             lerp(u,
                  grad3(p->perm[aa + 1], xf, yf, zf - 1),
                  grad3(p->perm[ba + 1], xf - 1, yf, zf - 1)),
             lerp(u,
                  grad3(p->perm[ab + 1], xf, yf - 1, zf - 1),
                  grad3(p->perm[bb + 1], xf - 1, yf - 1, zf - 1))));
}

/// @brief Compute fractal Brownian motion (fBm) using multiple Perlin octaves.
/// @details Sums noise layers at exponentially increasing frequency (lacunarity=2)
///          and exponentially decreasing amplitude (persistence). More octaves add
///          fine detail but cost proportionally more computation. Typical values:
///          octaves=4-8, persistence=0.5.
/// @param obj PerlinNoise object.
/// @param x X coordinate.
/// @param y Y coordinate.
/// @param octaves Number of noise layers to sum (non-positive returns 0; positive values clamp to 16).
/// @param persistence Amplitude multiplier per octave (0.5 = halve each layer).
/// @return Summed noise value (range depends on octaves and persistence).
double rt_perlin_octave2d(void *obj, double x, double y, int64_t octaves, double persistence) {
    octaves = perlin_clamp_octaves(octaves);
    if (!obj || octaves <= 0 || !isfinite(persistence))
        return 0.0;

    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double max_value = 0.0;

    for (int64_t i = 0; i < octaves; ++i) {
        total += rt_perlin_noise2d(obj, x * frequency, y * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return perlin_finish_octaves(total, max_value);
}

double rt_perlin_octave3d(
    void *obj, double x, double y, double z, int64_t octaves, double persistence) {
    octaves = perlin_clamp_octaves(octaves);
    if (!obj || octaves <= 0 || !isfinite(persistence))
        return 0.0;

    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double max_value = 0.0;

    for (int64_t i = 0; i < octaves; ++i) {
        total += rt_perlin_noise3d(obj, x * frequency, y * frequency, z * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return perlin_finish_octaves(total, max_value);
}
