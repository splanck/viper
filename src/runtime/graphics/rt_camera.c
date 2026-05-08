//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_camera.c
// Purpose: 2D camera transform for Viper game scenes. Maintains a world-space
//   viewport defined by a position, an integer zoom percentage, and an optional
//   rotation angle. Provides coordinate conversion (world↔screen), optional
//   world-bounds clamping, viewport culling, and a dirty flag to let renderers
//   skip unnecessary redraws when the camera hasn't moved.
//
// Key invariants:
//   - All coordinates are integers (pixels). Zoom is an integer percentage:
//     100 = 1× (no zoom), 200 = 2× (zoomed in), 50 = ½× (zoomed out).
//     Zoom is clamped to [10, 1000] (10% – 10×) to prevent division by zero
//     and absurdly small viewports.
//   - The viewport in world-space has dimensions:
//       world_width  = camera.width  × 100 / zoom
//       world_height = camera.height × 100 / zoom
//   - The dirty flag is set to 1 at creation and whenever x, y, zoom, or
//     rotation change. It is cleared only by rt_camera_clear_dirty(). Renderers
//     that cache the camera transform should check is_dirty() each frame.
//   - If camera bounds are set, the camera position is clamped after every
//     mutation that changes x or y (Follow, Move, SetX, SetY). Bounds are
//     applied in world-space (no zoom scaling).
//   - rt_camera_is_visible() uses a simple AABB overlap test in world-space.
//     A NULL camera pointer is treated conservatively as always-visible.
//
// Ownership/Lifetime:
//   - Camera objects are GC-managed via rt_obj_new_i64. They are freed
//     automatically when the GC collects them; there is no explicit finalizer
//     beyond the GC reclaiming the allocation.
//
// Links: src/runtime/graphics/rt_camera.h (public API),
//        docs/viperlib/game.md (Camera section)
//
//===----------------------------------------------------------------------===//

#include "rt_camera.h"

#include "rt_graphics.h"
#include "rt_heap.h"
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/// Maximum number of parallax scrolling layers per camera.
#define RT_CAMERA_MAX_PARALLAX 8
#define RT_CAMERA_MAX_PARALLAX_TILES 65536

/// @brief A single parallax scrolling layer.
typedef struct {
    void *pixels;            ///< Pixels buffer to tile across the viewport
    int64_t scroll_factor_x; ///< X scroll % (100 = camera speed, 50 = half, 0 = static)
    int64_t scroll_factor_y; ///< Y scroll % (100 = camera speed, 50 = half, 0 = static)
    int64_t offset_y;        ///< Vertical pixel offset for layer positioning
    int8_t active;           ///< 1 if this layer slot is in use
} rt_parallax_layer;

/// @brief Camera implementation structure.
typedef struct rt_camera_impl {
    int64_t x;          ///< Camera X position (world coordinates)
    int64_t y;          ///< Camera Y position (world coordinates)
    int64_t width;      ///< Viewport width
    int64_t height;     ///< Viewport height
    int64_t zoom;       ///< Zoom level (100 = 100%)
    int64_t rotation;   ///< Rotation in degrees
    int64_t has_bounds; ///< Whether bounds are set
    int64_t min_x;      ///< Minimum X bound
    int64_t min_y;      ///< Minimum Y bound
    int64_t max_x;      ///< Maximum X bound
    int64_t max_y;      ///< Maximum Y bound
    int64_t dirty;      ///< 1 if position/zoom/rotation changed since last rt_camera_clear_dirty
    int64_t deadzone_w; ///< Deadzone width (0 = disabled). Target within zone doesn't move camera.
    int64_t deadzone_h; ///< Deadzone height (0 = disabled).
    rt_parallax_layer parallax[RT_CAMERA_MAX_PARALLAX]; ///< Fixed parallax layer slots
    int64_t parallax_count;                             ///< Number of active layers
} rt_camera_impl;

static rt_camera_impl *camera_checked_or_null(void *camera_ptr) {
    if (!camera_ptr || rt_obj_class_id(camera_ptr) != RT_CAMERA_CLASS_ID)
        return NULL;
    return (rt_camera_impl *)camera_ptr;
}

/// @brief Release a GC-managed object held in `*slot` and NULL-out the slot.
static void camera_release_ref(void **slot) {
    if (!slot || !*slot)
        return;
    if (rt_heap_is_payload(*slot) && rt_obj_release_check0(*slot))
        rt_obj_free(*slot);
    *slot = NULL;
}

/// @brief Round a long double to the nearest int64, saturating at INT64_MIN/MAX instead of overflowing.
static int64_t camera_ld_to_i64_sat(long double value) {
    if (value >= (long double)INT64_MAX)
        return INT64_MAX;
    if (value <= (long double)INT64_MIN)
        return INT64_MIN;
    return (int64_t)(value >= 0.0L ? value + 0.5L : value - 0.5L);
}

/// @brief Add two int64 values with saturation at INT64_MIN/MAX.
static int64_t camera_add_saturating(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b)
        return INT64_MAX;
    if (b < 0 && a < INT64_MIN - b)
        return INT64_MIN;
    return a + b;
}

/// @brief Subtract two int64 values with saturation at INT64_MIN/MAX (delegates through long double).
static int64_t camera_sub_saturating(int64_t a, int64_t b) {
    return camera_ld_to_i64_sat((long double)a - (long double)b);
}

/// @brief Compute `value * mul / div` via long double with saturation; returns 0 on zero divisor.
static int64_t camera_mul_div_saturating(int64_t value, int64_t mul, int64_t div) {
    if (div == 0)
        return 0;
    return camera_ld_to_i64_sat(((long double)value * (long double)mul) / (long double)div);
}

/// @brief World-space width covered by the viewport at the current zoom (zoom is in percent).
static int64_t camera_world_width(const rt_camera_impl *camera) {
    return camera_mul_div_saturating(camera->width, 100, camera->zoom);
}

/// @brief World-space height covered by the viewport at the current zoom.
static int64_t camera_world_height(const rt_camera_impl *camera) {
    return camera_mul_div_saturating(camera->height, 100, camera->zoom);
}

/// @brief World-space X coordinate at the centre of the viewport.
static double camera_center_x(const rt_camera_impl *camera) {
    return (double)camera->x + (double)camera_world_width(camera) * 0.5;
}

/// @brief World-space Y coordinate at the centre of the viewport.
static double camera_center_y(const rt_camera_impl *camera) {
    return (double)camera->y + (double)camera_world_height(camera) * 0.5;
}

/// @brief World → screen forward transform (translate, rotate, zoom, recenter).
/// @details Applies the camera's view transform: translate the world point by
///          minus the camera centre, rotate by the negated camera rotation
///          (so positive `rotation` rotates the *world* clockwise = camera
///          counter-clockwise), scale by zoom/100, then translate to screen
///          centre. Used by the parallax draw loop to place each tile.
static void camera_apply_transform(const rt_camera_impl *camera,
                                   double world_x,
                                   double world_y,
                                   double *screen_x,
                                   double *screen_y) {
    double dx = world_x - camera_center_x(camera);
    double dy = world_y - camera_center_y(camera);
    double rad = -((double)camera->rotation) * 3.14159265358979323846 / 180.0;
    double cos_r = cos(rad);
    double sin_r = sin(rad);
    double rx = dx * cos_r - dy * sin_r;
    double ry = dx * sin_r + dy * cos_r;
    if (screen_x)
        *screen_x = rx * (double)camera->zoom / 100.0 + (double)camera->width * 0.5;
    if (screen_y)
        *screen_y = ry * (double)camera->zoom / 100.0 + (double)camera->height * 0.5;
}

/// @brief Screen → world inverse transform.
/// @details Inverts `camera_apply_transform`: translate by minus screen
///          centre, scale by 100/zoom, rotate by the unnegated camera
///          rotation, then translate by camera centre. Used to compute
///          world-space tile coverage from screen corners during parallax
///          rendering.
static void camera_apply_inverse_transform(const rt_camera_impl *camera,
                                           double screen_x,
                                           double screen_y,
                                           double *world_x,
                                           double *world_y) {
    double dx = (screen_x - (double)camera->width * 0.5) * 100.0 / (double)camera->zoom;
    double dy = (screen_y - (double)camera->height * 0.5) * 100.0 / (double)camera->zoom;
    double rad = ((double)camera->rotation) * 3.14159265358979323846 / 180.0;
    double cos_r = cos(rad);
    double sin_r = sin(rad);
    double rx = dx * cos_r - dy * sin_r;
    double ry = dx * sin_r + dy * cos_r;
    if (world_x)
        *world_x = rx + camera_center_x(camera);
    if (world_y)
        *world_y = ry + camera_center_y(camera);
}

/// @brief Floor-division on int64 (rounds toward negative infinity, not toward zero).
/// @details C's `/` operator rounds toward zero, which produces the wrong
///          tile coordinate for negative world positions: `(-1)/16 == 0`
///          in C, but the tile containing `-1` is tile `-1`, not tile `0`.
///          The mismatch-of-signs check (`r != 0 && (sign(r) != sign(divisor))`)
///          decrements the quotient by one to recover floor semantics.
///          Used by the parallax tile loop to find the first/last tile
///          indices straddling the visible world bounds.
static int64_t camera_floor_div(int64_t value, int64_t divisor) {
    int64_t q = value / divisor;
    int64_t r = value % divisor;
    if (r != 0 && ((r < 0) != (divisor < 0)))
        q--;
    return q;
}

/// @brief Compute the tile span from `first` to `last` (inclusive) and validate it is within
///        `RT_CAMERA_MAX_PARALLAX_TILES`. Writes the span count to `out_span` on success.
/// @return 1 if the span fits the parallax budget, 0 otherwise.
static int8_t camera_tile_span_within_limit(int64_t first, int64_t last, int64_t *out_span) {
    if (!out_span || last < first)
        return 0;
    uint64_t diff = (uint64_t)last - (uint64_t)first;
    if (diff >= (uint64_t)RT_CAMERA_MAX_PARALLAX_TILES)
        return 0;
    *out_span = (int64_t)(diff + 1u);
    return 1;
}

/// @brief Return 1 if `span_x * span_y` is within the `RT_CAMERA_MAX_PARALLAX_TILES` budget.
/// @details Divides rather than multiplies to avoid overflow on large span values.
static int8_t camera_tile_product_within_limit(int64_t span_x, int64_t span_y) {
    if (span_x <= 0 || span_y <= 0)
        return 0;
    return span_x <= RT_CAMERA_MAX_PARALLAX_TILES / span_y;
}

/// @brief Compute the number of tiles visible across a viewport dimension plus a one-tile margin.
/// @details Returns one more than the integer quotient so the visible strip fully covers the
///          viewport even when the camera offset is not tile-aligned. Clamped to
///          `RT_CAMERA_MAX_PARALLAX_TILES + 1` to trigger the "exceeds budget" early-out.
static int64_t camera_view_tile_span(int64_t viewport, int64_t tile_size) {
    if (viewport <= 0 || tile_size <= 0)
        return 0;
    int64_t base = viewport / tile_size;
    if (base > RT_CAMERA_MAX_PARALLAX_TILES - 2)
        return RT_CAMERA_MAX_PARALLAX_TILES + 1;
    return base + 2;
}

/// @brief Render one parallax layer at the camera's current zoom + rotation.
/// @details Three stages:
///          1. Build a *layer-specific* camera by scaling the parent
///             camera's position by the layer's per-axis `scroll_factor_*`
///             (a layer at factor 50 scrolls half as fast = appears
///             further away). The layer-camera retains the parent's
///             zoom/rotation so the layer transforms identically.
///          2. Pre-bake the tile pixels: scale to the camera's zoom (if
///             not 100), then rotate to the camera's negated rotation
///             (cancels the camera's view rotation when the tile is
///             drawn back through `camera_apply_transform` below). The
///             scaled/rotated buffers are reference-counted and released
///             at the end.
///          3. Compute the world-space tile coverage by inverse-
///             transforming the four screen corners, then iterate every
///             tile in that AABB and `vgfx_blit_alpha` the prepared
///             pixels at the screen position from the forward transform.
///             Wrapping is implicit — tiles at any integer `(tx, ty)`
///             repeat the layer's source texture.
///          The final blit is centre-anchored, so the tile's centre of
///          rotation matches the precomputed rotated buffer's centre.
static int64_t camera_draw_parallax_transformed(const rt_camera_impl *camera,
                                                const rt_parallax_layer *layer,
                                                void *canvas) {
    rt_camera_impl layer_camera = *camera;
    layer_camera.x = camera_mul_div_saturating(camera->x, layer->scroll_factor_x, 100);
    layer_camera.y = camera_add_saturating(
        camera_mul_div_saturating(camera->y, layer->scroll_factor_y, 100), layer->offset_y);
    layer_camera.has_bounds = 0;

    int64_t pw = rt_pixels_width(layer->pixels);
    int64_t ph = rt_pixels_height(layer->pixels);
    if (pw <= 0 || ph <= 0)
        return 0;

    void *tile_pixels = layer->pixels;
    void *scaled = NULL;
    void *rotated = NULL;

    if (camera->zoom != 100) {
        int64_t scaled_w = camera_mul_div_saturating(pw, camera->zoom, 100);
        int64_t scaled_h = camera_mul_div_saturating(ph, camera->zoom, 100);
        if (scaled_w < 1)
            scaled_w = 1;
        if (scaled_h < 1)
            scaled_h = 1;
        scaled = rt_pixels_scale(layer->pixels, scaled_w, scaled_h);
        if (!scaled)
            return 0;
        tile_pixels = scaled;
    }

    if (camera->rotation != 0) {
        rotated = rt_pixels_rotate(tile_pixels, -(double)camera->rotation);
        if (!rotated) {
            camera_release_ref(&scaled);
            return 0;
        }
        tile_pixels = rotated;
    }

    int64_t draw_w = rt_pixels_width(tile_pixels);
    int64_t draw_h = rt_pixels_height(tile_pixels);
    double world_x = 0.0;
    double world_y = 0.0;
    double min_world_x = 0.0;
    double max_world_x = 0.0;
    double min_world_y = 0.0;
    double max_world_y = 0.0;

    camera_apply_inverse_transform(&layer_camera, 0.0, 0.0, &min_world_x, &min_world_y);
    max_world_x = min_world_x;
    max_world_y = min_world_y;

    camera_apply_inverse_transform(&layer_camera, (double)camera->width, 0.0, &world_x, &world_y);
    if (world_x < min_world_x)
        min_world_x = world_x;
    if (world_x > max_world_x)
        max_world_x = world_x;
    if (world_y < min_world_y)
        min_world_y = world_y;
    if (world_y > max_world_y)
        max_world_y = world_y;

    camera_apply_inverse_transform(&layer_camera, 0.0, (double)camera->height, &world_x, &world_y);
    if (world_x < min_world_x)
        min_world_x = world_x;
    if (world_x > max_world_x)
        max_world_x = world_x;
    if (world_y < min_world_y)
        min_world_y = world_y;
    if (world_y > max_world_y)
        max_world_y = world_y;

    camera_apply_inverse_transform(
        &layer_camera, (double)camera->width, (double)camera->height, &world_x, &world_y);
    if (world_x < min_world_x)
        min_world_x = world_x;
    if (world_x > max_world_x)
        max_world_x = world_x;
    if (world_y < min_world_y)
        min_world_y = world_y;
    if (world_y > max_world_y)
        max_world_y = world_y;

    int64_t first_tile_x =
        camera_floor_div(camera_sub_saturating(camera_ld_to_i64_sat(floorl(min_world_x)), pw), pw);
    int64_t last_tile_x =
        camera_floor_div(camera_add_saturating(camera_ld_to_i64_sat(floorl(max_world_x)), pw), pw);
    int64_t first_tile_y =
        camera_floor_div(camera_sub_saturating(camera_ld_to_i64_sat(floorl(min_world_y)), ph), ph);
    int64_t last_tile_y =
        camera_floor_div(camera_add_saturating(camera_ld_to_i64_sat(floorl(max_world_y)), ph), ph);

    int64_t span_x = 0;
    int64_t span_y = 0;
    if (!camera_tile_span_within_limit(first_tile_x, last_tile_x, &span_x) ||
        !camera_tile_span_within_limit(first_tile_y, last_tile_y, &span_y) ||
        !camera_tile_product_within_limit(span_x, span_y)) {
        camera_release_ref(&rotated);
        camera_release_ref(&scaled);
        return 0;
    }

    for (int64_t ty = first_tile_y; ty <= last_tile_y; ty++) {
        for (int64_t tx = first_tile_x; tx <= last_tile_x; tx++) {
            double screen_x = 0.0;
            double screen_y = 0.0;
            camera_apply_transform(&layer_camera,
                                   (double)camera_mul_div_saturating(tx, pw, 1) + (double)pw * 0.5,
                                   (double)camera_mul_div_saturating(ty, ph, 1) + (double)ph * 0.5,
                                   &screen_x,
                                   &screen_y);
            rt_canvas_blit_alpha(
                canvas,
                camera_ld_to_i64_sat((long double)screen_x - (long double)draw_w * 0.5L),
                camera_ld_to_i64_sat((long double)screen_y - (long double)draw_h * 0.5L),
                tile_pixels);
        }
    }

    camera_release_ref(&rotated);
    camera_release_ref(&scaled);
    return 1;
}

/// @brief Release all resources held by a parallax layer and mark it inactive.
static void camera_release_parallax_layer(rt_parallax_layer *layer) {
    if (!layer || !layer->active)
        return;
    camera_release_ref(&layer->pixels);
    layer->scroll_factor_x = 0;
    layer->scroll_factor_y = 0;
    layer->offset_y = 0;
    layer->active = 0;
}

/// @brief GC finalizer: release all parallax layers before the camera allocation is freed.
static void camera_finalize(void *obj) {
    rt_camera_impl *camera = camera_checked_or_null(obj);
    if (!camera)
        return;
    for (int i = 0; i < RT_CAMERA_MAX_PARALLAX; i++)
        camera_release_parallax_layer(&camera->parallax[i]);
    camera->parallax_count = 0;
}

/// @brief Clamp camera position to bounds.
static void camera_clamp_bounds(rt_camera_impl *camera) {
    if (!camera->has_bounds)
        return;

    int64_t max_x = camera_sub_saturating(camera->max_x, camera_world_width(camera));
    int64_t max_y = camera_sub_saturating(camera->max_y, camera_world_height(camera));
    if (max_x < camera->min_x)
        max_x = camera->min_x;
    if (max_y < camera->min_y)
        max_y = camera->min_y;

    if (camera->x < camera->min_x)
        camera->x = camera->min_x;
    if (camera->y < camera->min_y)
        camera->y = camera->min_y;
    if (camera->x > max_x)
        camera->x = max_x;
    if (camera->y > max_y)
        camera->y = max_y;
}

//=============================================================================
// Camera Creation
//=============================================================================

/// @brief Construct a new Camera2D bound to a viewport size in pixels.
/// @details Width and height below 1 are clamped to 1 to avoid divide-by-zero
///          in `camera_world_width` / `_height`. Initial position is the
///          world origin, zoom is 100 (1×), rotation is 0, no bounds. The
///          camera starts marked dirty so the first render computes its
///          view transform fresh. Returns NULL on allocation failure.
void *rt_camera_new(int64_t width, int64_t height) {
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;

    rt_camera_impl *camera =
        (rt_camera_impl *)rt_obj_new_i64(RT_CAMERA_CLASS_ID, (int64_t)sizeof(rt_camera_impl));
    if (!camera)
        return NULL;

    camera->x = 0;
    camera->y = 0;
    camera->width = width;
    camera->height = height;
    camera->zoom = 100;
    camera->rotation = 0;
    camera->has_bounds = 0;
    camera->min_x = 0;
    camera->min_y = 0;
    camera->max_x = 0;
    camera->max_y = 0;
    camera->dirty = 1; /* newly created camera is always dirty */
    camera->parallax_count = 0;
    memset(camera->parallax, 0, sizeof(camera->parallax));
    rt_obj_set_finalizer(camera, camera_finalize);

    return camera;
}

//=============================================================================
// Camera Properties
//=============================================================================

/// @brief Read the camera's world-space X coordinate (the point centered on screen). Traps on null.
int64_t rt_camera_get_x(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.X: null camera");
        return 0;
    }
    return camera->x;
}

/// @brief Set the camera's world-space X (clamped to active bounds, if any). Marks the
/// view-transform dirty so the next render recomputes derived state.
void rt_camera_set_x(void *camera_ptr, int64_t x) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.X: null camera");
        return;
    }
    camera->x = x;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

/// @brief Read the camera's world-space Y coordinate. Traps on null.
int64_t rt_camera_get_y(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Y: null camera");
        return 0;
    }
    return camera->y;
}

/// @brief Set the camera's world-space Y (clamped to active bounds, if any).
void rt_camera_set_y(void *camera_ptr, int64_t y) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Y: null camera");
        return;
    }
    camera->y = y;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

/// @brief Read the zoom level (100 = 1.0×, 200 = 2.0× zoom-in, 50 = 0.5× zoom-out).
int64_t rt_camera_get_zoom(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Zoom: null camera");
        return 100;
    }
    return camera->zoom;
}

/// @brief Set zoom level (clamped to [10, 1000] = 0.1×–10×). Marks dirty and re-clamps to bounds.
void rt_camera_set_zoom(void *camera_ptr, int64_t zoom) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Zoom: null camera");
        return;
    }
    if (zoom < 10)
        zoom = 10;
    if (zoom > 1000)
        zoom = 1000;
    camera->zoom = zoom;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

/// @brief Read the camera's rotation in degrees (positive = counter-clockwise).
int64_t rt_camera_get_rotation(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Rotation: null camera");
        return 0;
    }
    return camera->rotation;
}

/// @brief Set rotation in degrees. No clamping (full ±360+ range allowed; renders modulo 360).
void rt_camera_set_rotation(void *camera_ptr, int64_t degrees) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Rotation: null camera");
        return;
    }
    camera->rotation = degrees;
    camera->dirty = 1;
}

/// @brief Read the camera viewport width in pixels (set on construction).
int64_t rt_camera_get_width(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Width: null camera");
        return 0;
    }
    return camera->width;
}

/// @brief Read the camera viewport height in pixels (set on construction).
int64_t rt_camera_get_height(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Height: null camera");
        return 0;
    }
    return camera->height;
}

//=============================================================================
// Camera Methods
//=============================================================================

/// @brief Snap the camera so (x, y) is at the viewport center. Re-clamps to bounds, marks dirty.
/// Use `_smooth_follow` for non-jarring tracking.
void rt_camera_follow(void *camera_ptr, int64_t x, int64_t y) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Follow: null camera");
        return;
    }

    // Center the camera on the given position
    camera->x = camera_sub_saturating(x, camera_world_width(camera) / 2);
    camera->y = camera_sub_saturating(y, camera_world_height(camera) / 2);
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

/// @brief Lerp the camera toward (target_x, target_y). `lerp_pct` is 0..1000 (0 = no move,
/// 1000 = instant snap). When a deadzone is set, no movement happens while the target stays
/// inside it — useful for platformer-style "loose" tracking.
void rt_camera_smooth_follow(void *camera_ptr,
                             int64_t target_x,
                             int64_t target_y,
                             int64_t lerp_pct) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.SmoothFollow: null camera");
        return;
    }

    // Desired camera position (center target in viewport)
    int64_t desired_x = camera_sub_saturating(target_x, camera_world_width(camera) / 2);
    int64_t desired_y = camera_sub_saturating(target_y, camera_world_height(camera) / 2);

    // Deadzone: skip if target is within deadzone of current position
    if (camera->deadzone_w > 0 || camera->deadzone_h > 0) {
        int64_t dx = camera_sub_saturating(desired_x, camera->x);
        int64_t dy = camera_sub_saturating(desired_y, camera->y);
        int64_t hw = camera->deadzone_w / 2;
        int64_t hh = camera->deadzone_h / 2;
        if (dx > -hw && dx < hw && dy > -hh && dy < hh)
            return;
    }

    // Lerp toward desired position. lerp_pct: 0-1000 (1000 = instant)
    if (lerp_pct >= 1000) {
        camera->x = desired_x;
        camera->y = desired_y;
    } else if (lerp_pct > 0) {
        camera->x = camera_add_saturating(
            camera->x,
            camera_mul_div_saturating(camera_sub_saturating(desired_x, camera->x), lerp_pct, 1000));
        camera->y = camera_add_saturating(
            camera->y,
            camera_mul_div_saturating(camera_sub_saturating(desired_y, camera->y), lerp_pct, 1000));
    }

    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

/// @brief Set the rectangular deadzone (centered on current position) in which `_smooth_follow`
/// is a no-op. Negative values are clamped to 0 (deadzone disabled).
void rt_camera_set_deadzone(void *camera_ptr, int64_t w, int64_t h) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return;
    camera->deadzone_w = w > 0 ? w : 0;
    camera->deadzone_h = h > 0 ? h : 0;
}

/// @brief Project a world-space point into screen-space pixels, applying zoom, rotation, and
/// translation. Outputs are written through `screen_x` / `screen_y` (rounded). Useful for HUD
/// markers anchored to world entities.
void rt_camera_world_to_screen(
    void *camera_ptr, int64_t world_x, int64_t world_y, int64_t *screen_x, int64_t *screen_y) {
    if (!camera_ptr || !screen_x || !screen_y)
        return;

    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return;
    double sx = 0.0, sy = 0.0;
    camera_apply_transform(camera, (double)world_x, (double)world_y, &sx, &sy);
    *screen_x = camera_ld_to_i64_sat((long double)sx);
    *screen_y = camera_ld_to_i64_sat((long double)sy);
}

/// @brief One-axis convenience: project just the X component of a world point. Y is implicitly
/// the camera's vertical center so rotation contributes correctly.
int64_t rt_camera_to_screen_x(void *camera_ptr, int64_t world_x) {
    if (!camera_ptr)
        return world_x;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return world_x;
    double sx = 0.0;
    camera_apply_transform(camera, (double)world_x, camera_center_y(camera), &sx, NULL);
    return camera_ld_to_i64_sat((long double)sx);
}

/// @brief One-axis convenience: project just the Y component of a world point.
int64_t rt_camera_to_screen_y(void *camera_ptr, int64_t world_y) {
    if (!camera_ptr)
        return world_y;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return world_y;
    double sy = 0.0;
    camera_apply_transform(camera, camera_center_x(camera), (double)world_y, NULL, &sy);
    return camera_ld_to_i64_sat((long double)sy);
}

/// @brief Inverse of `_world_to_screen`: turn a screen pixel into world coordinates. Useful
/// for hit-testing mouse clicks against world entities.
void rt_camera_screen_to_world(
    void *camera_ptr, int64_t screen_x, int64_t screen_y, int64_t *world_x, int64_t *world_y) {
    if (!camera_ptr || !world_x || !world_y)
        return;

    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return;
    double wx = 0.0, wy = 0.0;
    camera_apply_inverse_transform(camera, (double)screen_x, (double)screen_y, &wx, &wy);
    *world_x = camera_ld_to_i64_sat((long double)wx);
    *world_y = camera_ld_to_i64_sat((long double)wy);
}

/// @brief One-axis convenience: unproject just the X component of a screen pixel to world.
int64_t rt_camera_to_world_x(void *camera_ptr, int64_t screen_x) {
    if (!camera_ptr)
        return screen_x;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return screen_x;
    double wx = 0.0;
    camera_apply_inverse_transform(
        camera, (double)screen_x, (double)camera->height * 0.5, &wx, NULL);
    return camera_ld_to_i64_sat((long double)wx);
}

/// @brief One-axis convenience: unproject just the Y component of a screen pixel to world.
int64_t rt_camera_to_world_y(void *camera_ptr, int64_t screen_y) {
    if (!camera_ptr)
        return screen_y;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return screen_y;
    double wy = 0.0;
    camera_apply_inverse_transform(
        camera, (double)camera->width * 0.5, (double)screen_y, NULL, &wy);
    return camera_ld_to_i64_sat((long double)wy);
}

/// @brief Translate the camera by (dx, dy) world units. Re-clamps to bounds; marks dirty.
void rt_camera_move(void *camera_ptr, int64_t dx, int64_t dy) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.Move: null camera");
        return;
    }
    camera->x = camera_add_saturating(camera->x, dx);
    camera->y = camera_add_saturating(camera->y, dy);
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

/// @brief Constrain camera position to stay within the rectangle [(min_x, min_y), (max_x,
/// max_y)]. The current position is immediately clamped. Use to prevent the camera from showing
/// "outside the level".
void rt_camera_set_bounds(
    void *camera_ptr, int64_t min_x, int64_t min_y, int64_t max_x, int64_t max_y) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.SetBounds: null camera");
        return;
    }
    camera->has_bounds = 1;
    camera->min_x = min_x;
    camera->min_y = min_y;
    camera->max_x = max_x;
    camera->max_y = max_y;
    int64_t old_x = camera->x;
    int64_t old_y = camera->y;
    camera_clamp_bounds(camera);
    if (camera->x != old_x || camera->y != old_y)
        camera->dirty = 1;
}

/// @brief Disable bounds clamping. Camera can be moved freely afterwards.
void rt_camera_clear_bounds(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera) {
        rt_trap("Camera.ClearBounds: null camera");
        return;
    }
    camera->has_bounds = 0;
}

//=============================================================================
// Visibility Culling
//=============================================================================

/// @brief Test whether a world-space rectangle has any overlap with the camera viewport.
/// Useful as a cheap broad-phase cull before drawing each entity. Conservative: rotation is
/// handled by projecting the four corners and checking the screen-space AABB.
int64_t rt_camera_is_visible(void *camera_ptr, int64_t x, int64_t y, int64_t w, int64_t h) {
    if (!camera_ptr)
        return 1; // Null camera — conservatively treat as visible
    if (w <= 0 || h <= 0)
        return 0;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return 0;

    double sx[4];
    double sy[4];
    camera_apply_transform(camera, (double)x, (double)y, &sx[0], &sy[0]);
    camera_apply_transform(
        camera, (double)((long double)x + (long double)w), (double)y, &sx[1], &sy[1]);
    camera_apply_transform(
        camera, (double)x, (double)((long double)y + (long double)h), &sx[2], &sy[2]);
    camera_apply_transform(camera,
                           (double)((long double)x + (long double)w),
                           (double)((long double)y + (long double)h),
                           &sx[3],
                           &sy[3]);

    double min_x = sx[0], max_x = sx[0];
    double min_y = sy[0], max_y = sy[0];
    for (int i = 1; i < 4; i++) {
        if (sx[i] < min_x)
            min_x = sx[i];
        if (sx[i] > max_x)
            max_x = sx[i];
        if (sy[i] < min_y)
            min_y = sy[i];
        if (sy[i] > max_y)
            max_y = sy[i];
    }

    if (max_x <= 0.0 || min_x >= (double)camera->width || max_y <= 0.0 ||
        min_y >= (double)camera->height)
        return 0;
    return 1;
}

//=============================================================================
// Dirty Flag — Enables callers to skip re-rendering when camera is stationary
//=============================================================================

/// @brief Returns 1 if the camera's transform has changed since the last `_clear_dirty`. Lets
/// callers skip costly re-renders when the view is stationary.
int64_t rt_camera_is_dirty(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return 0;
    return camera->dirty;
}

/// @brief Reset the dirty flag — call after rendering to acknowledge the latest transform.
void rt_camera_clear_dirty(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return;
    camera->dirty = 0;
}

//=============================================================================
// Parallax Layer Management
//=============================================================================

/// @brief Register a parallax background layer with `pixels` as its texture. `scroll_x_pct` and
/// `scroll_y_pct` are 0..100 (0 = stationary, 100 = scrolls 1:1 with camera). Higher numbers feel
/// "closer" to the viewer. Up to RT_CAMERA_MAX_PARALLAX layers; returns the slot index or -1.
int64_t rt_camera_add_parallax(void *camera_ptr,
                               void *pixels,
                               int64_t scroll_x_pct,
                               int64_t scroll_y_pct) {
    if (!camera_ptr || !pixels)
        return -1;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return -1;
    if (rt_obj_class_id(pixels) != RT_PIXELS_CLASS_ID)
        return -1;
    if (camera->parallax_count >= RT_CAMERA_MAX_PARALLAX)
        return -1;

    for (int i = 0; i < RT_CAMERA_MAX_PARALLAX; i++) {
        if (!camera->parallax[i].active) {
            rt_obj_retain_maybe(pixels);
            camera->parallax[i].pixels = pixels;
            camera->parallax[i].scroll_factor_x = scroll_x_pct;
            camera->parallax[i].scroll_factor_y = scroll_y_pct;
            camera->parallax[i].offset_y = 0;
            camera->parallax[i].active = 1;
            camera->parallax_count++;
            return (int64_t)i;
        }
    }
    return -1;
}

/// @brief Remove a parallax layer by slot index (returned from `_add_parallax`). No-op if
/// the slot is already inactive or the index is out of range.
void rt_camera_remove_parallax(void *camera_ptr, int64_t index) {
    if (!camera_ptr)
        return;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return;
    if (index < 0 || index >= RT_CAMERA_MAX_PARALLAX)
        return;
    if (camera->parallax[index].active) {
        camera_release_parallax_layer(&camera->parallax[index]);
        camera->parallax_count--;
    }
}

/// @brief Remove all parallax layers and reset the layer count.
void rt_camera_clear_parallax(void *camera_ptr) {
    if (!camera_ptr)
        return;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return;
    for (int i = 0; i < RT_CAMERA_MAX_PARALLAX; i++)
        camera_release_parallax_layer(&camera->parallax[i]);
    camera->parallax_count = 0;
}

/// @brief Number of currently-active parallax layers.
int64_t rt_camera_parallax_count(void *camera_ptr) {
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return 0;
    return camera->parallax_count;
}

/// @brief Render every active parallax layer to `canvas` at offsets computed from the camera's
/// current scroll position and each layer's scroll factor. Returns the number of layers drawn.
int64_t rt_camera_draw_parallax(void *camera_ptr, void *canvas) {
    if (!camera_ptr || !canvas)
        return 0;
    rt_camera_impl *camera = camera_checked_or_null(camera_ptr);
    if (!camera)
        return 0;

    int64_t layers_drawn = 0;

    for (int i = 0; i < RT_CAMERA_MAX_PARALLAX; i++) {
        rt_parallax_layer *layer = &camera->parallax[i];
        if (!layer->active || !layer->pixels)
            continue;

        int64_t pw = rt_pixels_width(layer->pixels);
        int64_t ph = rt_pixels_height(layer->pixels);

        if (pw <= 0 || ph <= 0)
            continue;

        if (camera->zoom != 100 || camera->rotation != 0) {
            layers_drawn += camera_draw_parallax_transformed(camera, layer, canvas);
            continue;
        }

        int64_t span_x = camera_view_tile_span(camera->width, pw);
        int64_t span_y = camera_view_tile_span(camera->height, ph);
        if (!camera_tile_product_within_limit(span_x, span_y))
            continue;

        /* Compute the parallax scroll offset */
        int64_t scroll_x = camera_mul_div_saturating(camera->x, layer->scroll_factor_x, 100);
        int64_t scroll_y = camera_add_saturating(
            camera_mul_div_saturating(camera->y, layer->scroll_factor_y, 100), layer->offset_y);

        /* Compute starting tile position (wrap negative modulo) */
        int64_t start_x = -(scroll_x % pw);
        if (start_x > 0)
            start_x -= pw;
        int64_t start_y = -(scroll_y % ph);
        if (start_y > 0)
            start_y -= ph;

        /* Tile the pixels across the viewport */
        for (int64_t ty = start_y; ty < camera->height; ty += ph) {
            for (int64_t tx = start_x; tx < camera->width; tx += pw) {
                rt_canvas_blit_alpha(canvas, tx, ty, layer->pixels);
            }
        }

        layers_drawn++;
    }

    return layers_drawn;
}
