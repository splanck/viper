//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_graphics2d_internal.h
// Purpose: Shared foundation helpers for the 2D graphics classes — saturating
//          integer math, reference-count slot management, allocation-size
//          checks, and pixel-region copy — used by both rt_graphics2d.c and the
//          tilemap classes in rt_graphics2d_tilemap.c.
//
// Key invariants:
//   - Engine-internal; included only by the graphics/2d/ translation units.
//   - Integer helpers saturate / clamp rather than overflow.
//   - rt2d_copy_region_pixels returns a fresh Pixels object owned by the caller.
//
// Ownership/Lifetime:
//   - rt2d_retain_ref / rt2d_release_ref_slot adjust GC refcounts on borrowed handles.
//
// Links: src/runtime/graphics/2d/rt_graphics2d.c (2D class core),
//        src/runtime/graphics/2d/rt_graphics2d_tilemap.c (tilemap classes)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stddef.h>
#include <stdint.h>

// Default initial capacity for growable 2D containers.
#define RT2D_INITIAL_CAP 16

int64_t rt2d_saturating_add_i64(int64_t a, int64_t b);
int32_t rt2d_has_class(void *obj, int64_t class_id);
int32_t rt2d_has_class_min(void *obj, int64_t class_id, size_t min_size);
int32_t rt2d_checked_count(int64_t width, int64_t height, int64_t elem_size, int64_t *out_count);
int64_t rt2d_initial_capacity(int64_t requested);
void rt2d_retain_ref(void *obj);
void rt2d_release_ref_slot(void **slot);
void *rt2d_copy_region_pixels(void *pixels, int64_t sx, int64_t sy, int64_t width, int64_t height);
int64_t rt2d_saturating_mul_i64(int64_t a, int64_t b);
int32_t rt2d_is_bitmap_font_handle(void *font);
int64_t rt2d_draw_rgb(int64_t color);
void rt2d_blit_pixels(void *dst, int64_t dx, int64_t dy, void *src, int64_t sx, int64_t sy, int64_t width,
                 int64_t height, int64_t tint, int64_t alpha, int64_t blend_mode);
