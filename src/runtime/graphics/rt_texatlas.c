//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_texatlas.c
// Purpose: 2D texture atlas implementation. Maps string names to rectangular
//          sub-regions of a backing Pixels buffer. Supports manual region
//          definition and grid-based auto-slicing for sprite sheets.
//
// Key invariants:
//   - Backing Pixels is retained on creation and released on finalize.
//   - Region lookup uses a fixed-size open-addressing table for O(1) average
//     lookup while keeping storage bounded.
//   - Region names longer than 31 bytes are rejected instead of truncated.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64 with finalizer.
//
// Links: rt_texatlas.h (public API), rt_spritebatch.c (batch atlas drawing)
//
//===----------------------------------------------------------------------===//

#include "rt_texatlas.h"

#ifdef VIPER_ENABLE_GRAPHICS

#include "rt_heap.h"
#include "rt_object.h"
#include "rt_pixels.h"
#include "rt_spritebatch.h"
#include "rt_string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_trap.h"

//=============================================================================
// Internal Types
//=============================================================================

#define TEXATLAS_MAX_REGIONS 512
#define TEXATLAS_NAME_LEN 32
#define TEXATLAS_HASH_SIZE 1024

typedef struct {
    char name[TEXATLAS_NAME_LEN];
    int64_t x, y, w, h;
} texatlas_region;

typedef struct {
    void *pixels; // Retained backing Pixels
    texatlas_region regions[TEXATLAS_MAX_REGIONS];
    int32_t region_count;
    int32_t region_slots[TEXATLAS_HASH_SIZE]; // index + 1, 0 = empty
} texatlas_impl;

//=============================================================================
// Helpers
//=============================================================================

static texatlas_impl *get_impl(void *atlas) {
    return (texatlas_impl *)atlas;
}

static uint32_t hash_name(const char *name) {
    uint32_t h = 2166136261u;
    while (*name) {
        h ^= (uint8_t)*name++;
        h *= 16777619u;
    }
    return h;
}

static int find_region(texatlas_impl *impl, const char *name) {
    if (!impl || !name || !*name)
        return -1;

    uint32_t h = hash_name(name);
    for (int probe = 0; probe < TEXATLAS_HASH_SIZE; ++probe) {
        int32_t entry = impl->region_slots[(h + (uint32_t)probe) & (TEXATLAS_HASH_SIZE - 1)];
        if (entry == 0)
            return -1;
        int idx = entry - 1;
        if (idx >= 0 && idx < impl->region_count && strcmp(impl->regions[idx].name, name) == 0)
            return idx;
    }
    return -1;
}

static void bind_region_slot(texatlas_impl *impl, int idx) {
    const char *name = impl->regions[idx].name;
    uint32_t h = hash_name(name);
    for (int probe = 0; probe < TEXATLAS_HASH_SIZE; ++probe) {
        int32_t *slot = &impl->region_slots[(h + (uint32_t)probe) & (TEXATLAS_HASH_SIZE - 1)];
        if (*slot == 0 || *slot == idx + 1) {
            *slot = idx + 1;
            return;
        }
    }

    rt_trap("TextureAtlas: lookup table exhausted");
}

//=============================================================================
// Lifecycle
//=============================================================================

static void texatlas_finalize(void *obj) {
    texatlas_impl *impl = get_impl(obj);
    if (impl->pixels) {
        rt_heap_release(impl->pixels);
        impl->pixels = NULL;
    }
}

/// @brief Construct an empty texture atlas backed by `pixels`. The Pixels object is retained;
/// regions are added later via `rt_texatlas_add` or by using `rt_texatlas_load_grid`. Traps if
/// `pixels` is NULL. Returns NULL on allocation failure.
void *rt_texatlas_new(void *pixels) {
    if (!pixels) {
        rt_trap("TextureAtlas.New: null pixels");
        return NULL;
    }

    texatlas_impl *impl = (texatlas_impl *)rt_obj_new_i64(0, (int64_t)sizeof(texatlas_impl));
    if (!impl)
        return NULL;

    memset(impl, 0, sizeof(texatlas_impl));
    impl->pixels = pixels;
    if (pixels)
        rt_heap_retain(pixels);
    rt_obj_set_finalizer(impl, texatlas_finalize);
    return impl;
}

/// @brief Build an atlas from `pixels` by auto-slicing it into a uniform grid of `frame_w ×
/// frame_h` cells (left-to-right, top-to-bottom, named "0", "1", ... up to 512 regions). Traps
/// if `pixels` is NULL or either dimension is non-positive. Useful for sprite sheets.
void *rt_texatlas_load_grid(void *pixels, int64_t frame_w, int64_t frame_h) {
    if (!pixels || frame_w <= 0 || frame_h <= 0) {
        rt_trap("TextureAtlas.LoadGrid: invalid arguments");
        return NULL;
    }

    void *atlas = rt_texatlas_new(pixels);
    if (!atlas)
        return NULL;

    texatlas_impl *impl = get_impl(atlas);
    int64_t img_w = rt_pixels_width(pixels);
    int64_t img_h = rt_pixels_height(pixels);
    int64_t cols = img_w / frame_w;
    int64_t rows = img_h / frame_h;

    int idx = 0;
    for (int64_t row = 0; row < rows && idx < TEXATLAS_MAX_REGIONS; row++) {
        for (int64_t col = 0; col < cols && idx < TEXATLAS_MAX_REGIONS; col++) {
            texatlas_region *r = &impl->regions[idx];
            snprintf(r->name, TEXATLAS_NAME_LEN, "%d", idx);
            r->x = col * frame_w;
            r->y = row * frame_h;
            r->w = frame_w;
            r->h = frame_h;
            bind_region_slot(impl, idx);
            idx++;
        }
    }
    impl->region_count = idx;
    return atlas;
}

//=============================================================================
// Region Management
//=============================================================================

/// @brief Add an element to the texatlas.
void rt_texatlas_add(void *atlas, void *name, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!atlas || !name)
        return;

    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return;
    if (strlen(cname) >= TEXATLAS_NAME_LEN) {
        rt_trap("TextureAtlas.Add: name too long (max 31 bytes)");
        return;
    }
    if (w <= 0 || h <= 0) {
        rt_trap("TextureAtlas.Add: width/height must be positive");
        return;
    }

    // Overwrite if name already exists
    int existing = find_region(impl, cname);
    if (existing < 0 && impl->region_count >= TEXATLAS_MAX_REGIONS) {
        rt_trap("TextureAtlas.Add: region limit exceeded (512)");
        return;
    }
    texatlas_region *r;
    if (existing >= 0) {
        r = &impl->regions[existing];
    } else {
        r = &impl->regions[impl->region_count];
        impl->region_count++;
    }

    strncpy(r->name, cname, TEXATLAS_NAME_LEN - 1);
    r->name[TEXATLAS_NAME_LEN - 1] = '\0';
    r->x = x;
    r->y = y;
    r->w = w;
    r->h = h;
    bind_region_slot(impl, existing >= 0 ? existing : impl->region_count - 1);
}

/// @brief Check whether a key/element exists in the texatlas.
int8_t rt_texatlas_has(void *atlas, void *name) {
    if (!atlas || !name)
        return 0;
    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;
    return find_region(impl, cname) >= 0 ? 1 : 0;
}

/// @brief Get the x of the texatlas.
int64_t rt_texatlas_get_x(void *atlas, void *name) {
    if (!atlas || !name)
        return 0;
    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;
    int idx = find_region(impl, cname);
    return idx >= 0 ? impl->regions[idx].x : 0;
}

/// @brief Get the y of the texatlas.
int64_t rt_texatlas_get_y(void *atlas, void *name) {
    if (!atlas || !name)
        return 0;
    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;
    int idx = find_region(impl, cname);
    return idx >= 0 ? impl->regions[idx].y : 0;
}

/// @brief Get the w of the texatlas.
int64_t rt_texatlas_get_w(void *atlas, void *name) {
    if (!atlas || !name)
        return 0;
    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;
    int idx = find_region(impl, cname);
    return idx >= 0 ? impl->regions[idx].w : 0;
}

/// @brief Get the h of the texatlas.
int64_t rt_texatlas_get_h(void *atlas, void *name) {
    if (!atlas || !name)
        return 0;
    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return 0;
    int idx = find_region(impl, cname);
    return idx >= 0 ? impl->regions[idx].h : 0;
}

/// @brief Return the backing Pixels object (still owned by the atlas — do not release).
void *rt_texatlas_get_pixels(void *atlas) {
    if (!atlas)
        return NULL;
    return get_impl(atlas)->pixels;
}

/// @brief Return the count of elements in the texatlas.
int64_t rt_texatlas_region_count(void *atlas) {
    if (!atlas)
        return 0;
    return get_impl(atlas)->region_count;
}

//=============================================================================
// SpriteBatch Atlas Drawing Extensions
//=============================================================================

// These are thin wrappers that resolve the named region and delegate to
// the existing rt_spritebatch_draw_region function.

/// @brief Look up region `name` in `atlas` and queue a sprite-batch draw at (x, y) at native
/// size. Silent no-op if the named region doesn't exist (no trap — typical sprite-sheet usage).
void rt_spritebatch_draw_atlas(void *batch, void *atlas, void *name, int64_t x, int64_t y) {
    if (!batch || !atlas || !name)
        return;

    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return;
    int idx = find_region(impl, cname);
    if (idx < 0)
        return; // Silent no-op for missing region

    texatlas_region *r = &impl->regions[idx];
    rt_spritebatch_draw_region(batch, impl->pixels, x, y, r->x, r->y, r->w, r->h);
}

/// @brief Look up region `name` in `atlas` and queue a sprite-batch draw at (x, y) scaled
/// uniformly by `scale` (integer multiplier). Silent no-op for missing regions.
void rt_spritebatch_draw_atlas_scaled(
    void *batch, void *atlas, void *name, int64_t x, int64_t y, int64_t scale) {
    if (!batch || !atlas || !name)
        return;

    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    if (!cname)
        return;
    int idx = find_region(impl, cname);
    if (idx < 0)
        return;

    texatlas_region *r = &impl->regions[idx];
    rt_spritebatch_draw_region_ex(
        batch, impl->pixels, x, y, r->x, r->y, r->w, r->h, scale, scale, 0, 0);
}

/// @brief Full sprite-batch atlas draw: scale + rotation (degrees) + depth-sort layer. Silent
/// no-op for missing regions or null inputs.
void rt_spritebatch_draw_atlas_ex(void *batch,
                                  void *atlas,
                                  void *name,
                                  int64_t x,
                                  int64_t y,
                                  int64_t scale,
                                  int64_t rotation,
                                  int64_t depth) {
    if (!batch || !atlas || !name)
        return;

    texatlas_impl *impl = get_impl(atlas);
    const char *cname = rt_string_cstr(name);
    int idx = find_region(impl, cname);
    if (idx < 0)
        return;

    texatlas_region *r = &impl->regions[idx];
    rt_spritebatch_draw_region_ex(
        batch, impl->pixels, x, y, r->x, r->y, r->w, r->h, scale, scale, rotation, depth);
}

#else /* !VIPER_ENABLE_GRAPHICS */

// Stubs when graphics is disabled — every public entry-point returns a benign zero/no-op so
// callers compile and link, but no pixels are produced. See the `VIPER_ENABLE_GRAPHICS` block
// above for behavioral docs.

/// @brief Stub — graphics disabled at build time. Returns 0.
void *rt_texatlas_new(void *pixels) {
    (void)pixels;
    return 0;
}

/// @brief Stub — graphics disabled at build time. Returns 0.
void *rt_texatlas_load_grid(void *p, int64_t w, int64_t h) {
    (void)p;
    (void)w;
    (void)h;
    return 0;
}

/// @brief Add an element to the texatlas.
void rt_texatlas_add(void *a, void *n, int64_t x, int64_t y, int64_t w, int64_t h) {
    (void)a;
    (void)n;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

/// @brief Check whether a key/element exists in the texatlas.
int8_t rt_texatlas_has(void *a, void *n) {
    (void)a;
    (void)n;
    return 0;
}

/// @brief Get the x of the texatlas.
int64_t rt_texatlas_get_x(void *a, void *n) {
    (void)a;
    (void)n;
    return 0;
}

/// @brief Get the y of the texatlas.
int64_t rt_texatlas_get_y(void *a, void *n) {
    (void)a;
    (void)n;
    return 0;
}

/// @brief Get the w of the texatlas.
int64_t rt_texatlas_get_w(void *a, void *n) {
    (void)a;
    (void)n;
    return 0;
}

/// @brief Get the h of the texatlas.
int64_t rt_texatlas_get_h(void *a, void *n) {
    (void)a;
    (void)n;
    return 0;
}

/// @brief Stub — graphics disabled at build time. Returns 0.
void *rt_texatlas_get_pixels(void *a) {
    (void)a;
    return 0;
}

/// @brief Return the count of elements in the texatlas.
int64_t rt_texatlas_region_count(void *a) {
    (void)a;
    return 0;
}

/// @brief Draw the atlas of the spritebatch.
void rt_spritebatch_draw_atlas(void *b, void *a, void *n, int64_t x, int64_t y) {
    (void)b;
    (void)a;
    (void)n;
    (void)x;
    (void)y;
}

/// @brief Draw the atlas scaled of the spritebatch.
void rt_spritebatch_draw_atlas_scaled(void *b, void *a, void *n, int64_t x, int64_t y, int64_t s) {
    (void)b;
    (void)a;
    (void)n;
    (void)x;
    (void)y;
    (void)s;
}

/// @brief Draw the atlas ex of the spritebatch.
void rt_spritebatch_draw_atlas_ex(
    void *b, void *a, void *n, int64_t x, int64_t y, int64_t s, int64_t r, int64_t d) {
    (void)b;
    (void)a;
    (void)n;
    (void)x;
    (void)y;
    (void)s;
    (void)r;
    (void)d;
}

#endif /* VIPER_ENABLE_GRAPHICS */
