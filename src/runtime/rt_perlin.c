//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_perlin.c
// Purpose: Implement Perlin noise using the improved algorithm (2002).
//          Supports 2D and 3D noise with octave/fractal layering.
//
//===----------------------------------------------------------------------===//

#include "rt_perlin.h"

#include "rt_internal.h"
#include "rt_object.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct rt_perlin_impl
{
    void **vptr;
    uint8_t perm[512]; // Doubled permutation table
} rt_perlin_impl;

static double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

static double lerp(double t, double a, double b)
{
    return a + t * (b - a);
}

static double grad3(int hash, double x, double y, double z)
{
    int h = hash & 15;
    double u = h < 8 ? x : y;
    double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static double grad2(int hash, double x, double y)
{
    int h = hash & 3;
    switch (h)
    {
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

static void rt_perlin_finalize(void *obj)
{
    // No dynamic allocations beyond the object itself
    (void)obj;
}

void *rt_perlin_new(int64_t seed)
{
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
    for (int i = 255; i > 0; --i)
    {
        // Simple LCG for deterministic shuffle
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        int j = (int)((s >> 16) % (uint64_t)(i + 1));
        uint8_t tmp = base[i];
        base[i] = base[j];
        base[j] = tmp;
    }

    // Double the table for wrapping
    for (int i = 0; i < 256; ++i)
    {
        p->perm[i] = base[i];
        p->perm[i + 256] = base[i];
    }

    rt_obj_set_finalizer(p, rt_perlin_finalize);
    return p;
}

double rt_perlin_noise2d(void *obj, double x, double y)
{
    if (!obj)
        return 0.0;
    rt_perlin_impl *p = (rt_perlin_impl *)obj;

    int xi = (int)floor(x) & 255;
    int yi = (int)floor(y) & 255;
    double xf = x - floor(x);
    double yf = y - floor(y);

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

double rt_perlin_noise3d(void *obj, double x, double y, double z)
{
    if (!obj)
        return 0.0;
    rt_perlin_impl *p = (rt_perlin_impl *)obj;

    int xi = (int)floor(x) & 255;
    int yi = (int)floor(y) & 255;
    int zi = (int)floor(z) & 255;
    double xf = x - floor(x);
    double yf = y - floor(y);
    double zf = z - floor(z);

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

double rt_perlin_octave2d(void *obj, double x, double y, int64_t octaves, double persistence)
{
    if (!obj || octaves <= 0)
        return 0.0;

    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double max_value = 0.0;

    for (int64_t i = 0; i < octaves; ++i)
    {
        total += rt_perlin_noise2d(obj, x * frequency, y * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total / max_value;
}

double rt_perlin_octave3d(
    void *obj, double x, double y, double z, int64_t octaves, double persistence)
{
    if (!obj || octaves <= 0)
        return 0.0;

    double total = 0.0;
    double frequency = 1.0;
    double amplitude = 1.0;
    double max_value = 0.0;

    for (int64_t i = 0; i < octaves; ++i)
    {
        total += rt_perlin_noise3d(obj, x * frequency, y * frequency, z * frequency) * amplitude;
        max_value += amplitude;
        amplitude *= persistence;
        frequency *= 2.0;
    }

    return total / max_value;
}
