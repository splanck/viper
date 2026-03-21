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
#include "rt_internal.h"
#include "rt_object.h"
#include "rt_pixels.h"

#include <string.h>

/// Maximum number of parallax scrolling layers per camera.
#define RT_CAMERA_MAX_PARALLAX 8

/// @brief A single parallax scrolling layer.
typedef struct
{
    void *pixels;            ///< Pixels buffer to tile across the viewport
    int64_t scroll_factor_x; ///< X scroll % (100 = camera speed, 50 = half, 0 = static)
    int64_t scroll_factor_y; ///< Y scroll % (100 = camera speed, 50 = half, 0 = static)
    int64_t offset_y;        ///< Vertical pixel offset for layer positioning
    int8_t active;           ///< 1 if this layer slot is in use
} rt_parallax_layer;

/// @brief Camera implementation structure.
typedef struct rt_camera_impl
{
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
    rt_parallax_layer parallax[RT_CAMERA_MAX_PARALLAX]; ///< Fixed parallax layer slots
    int64_t parallax_count;                             ///< Number of active layers
} rt_camera_impl;

/// @brief Clamp camera position to bounds.
static void camera_clamp_bounds(rt_camera_impl *camera)
{
    if (!camera->has_bounds)
        return;

    if (camera->x < camera->min_x)
        camera->x = camera->min_x;
    if (camera->y < camera->min_y)
        camera->y = camera->min_y;
    if (camera->x > camera->max_x)
        camera->x = camera->max_x;
    if (camera->y > camera->max_y)
        camera->y = camera->max_y;
}

//=============================================================================
// Camera Creation
//=============================================================================

void *rt_camera_new(int64_t width, int64_t height)
{
    if (width <= 0)
        width = 1;
    if (height <= 0)
        height = 1;

    rt_camera_impl *camera = (rt_camera_impl *)rt_obj_new_i64(0, (int64_t)sizeof(rt_camera_impl));
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

    return camera;
}

//=============================================================================
// Camera Properties
//=============================================================================

int64_t rt_camera_get_x(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.X: null camera");
        return 0;
    }
    return ((rt_camera_impl *)camera_ptr)->x;
}

void rt_camera_set_x(void *camera_ptr, int64_t x)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.X: null camera");
        return;
    }
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    camera->x = x;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

int64_t rt_camera_get_y(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Y: null camera");
        return 0;
    }
    return ((rt_camera_impl *)camera_ptr)->y;
}

void rt_camera_set_y(void *camera_ptr, int64_t y)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Y: null camera");
        return;
    }
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    camera->y = y;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

int64_t rt_camera_get_zoom(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Zoom: null camera");
        return 100;
    }
    return ((rt_camera_impl *)camera_ptr)->zoom;
}

void rt_camera_set_zoom(void *camera_ptr, int64_t zoom)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Zoom: null camera");
        return;
    }
    if (zoom < 10)
        zoom = 10;
    if (zoom > 1000)
        zoom = 1000;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    camera->zoom = zoom;
    camera->dirty = 1;
}

int64_t rt_camera_get_rotation(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Rotation: null camera");
        return 0;
    }
    return ((rt_camera_impl *)camera_ptr)->rotation;
}

void rt_camera_set_rotation(void *camera_ptr, int64_t degrees)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Rotation: null camera");
        return;
    }
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    camera->rotation = degrees;
    camera->dirty = 1;
}

int64_t rt_camera_get_width(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Width: null camera");
        return 0;
    }
    return ((rt_camera_impl *)camera_ptr)->width;
}

int64_t rt_camera_get_height(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Height: null camera");
        return 0;
    }
    return ((rt_camera_impl *)camera_ptr)->height;
}

//=============================================================================
// Camera Methods
//=============================================================================

void rt_camera_follow(void *camera_ptr, int64_t x, int64_t y)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Follow: null camera");
        return;
    }
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;

    // Center the camera on the given position
    camera->x = x - camera->width / 2;
    camera->y = y - camera->height / 2;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

void rt_camera_world_to_screen(
    void *camera_ptr, int64_t world_x, int64_t world_y, int64_t *screen_x, int64_t *screen_y)
{
    if (!camera_ptr || !screen_x || !screen_y)
        return;

    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;

    // Apply camera offset and zoom
    *screen_x = (world_x - camera->x) * camera->zoom / 100;
    *screen_y = (world_y - camera->y) * camera->zoom / 100;
}

int64_t rt_camera_to_screen_x(void *camera_ptr, int64_t world_x)
{
    if (!camera_ptr)
        return world_x;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    return (world_x - camera->x) * camera->zoom / 100;
}

int64_t rt_camera_to_screen_y(void *camera_ptr, int64_t world_y)
{
    if (!camera_ptr)
        return world_y;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    return (world_y - camera->y) * camera->zoom / 100;
}

void rt_camera_screen_to_world(
    void *camera_ptr, int64_t screen_x, int64_t screen_y, int64_t *world_x, int64_t *world_y)
{
    if (!camera_ptr || !world_x || !world_y)
        return;

    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;

    // Reverse the camera transform
    *world_x = screen_x * 100 / camera->zoom + camera->x;
    *world_y = screen_y * 100 / camera->zoom + camera->y;
}

int64_t rt_camera_to_world_x(void *camera_ptr, int64_t screen_x)
{
    if (!camera_ptr)
        return screen_x;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    return screen_x * 100 / camera->zoom + camera->x;
}

int64_t rt_camera_to_world_y(void *camera_ptr, int64_t screen_y)
{
    if (!camera_ptr)
        return screen_y;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    return screen_y * 100 / camera->zoom + camera->y;
}

void rt_camera_move(void *camera_ptr, int64_t dx, int64_t dy)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.Move: null camera");
        return;
    }
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    camera->x += dx;
    camera->y += dy;
    camera->dirty = 1;
    camera_clamp_bounds(camera);
}

void rt_camera_set_bounds(
    void *camera_ptr, int64_t min_x, int64_t min_y, int64_t max_x, int64_t max_y)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.SetBounds: null camera");
        return;
    }
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    camera->has_bounds = 1;
    camera->min_x = min_x;
    camera->min_y = min_y;
    camera->max_x = max_x;
    camera->max_y = max_y;
    camera_clamp_bounds(camera);
}

void rt_camera_clear_bounds(void *camera_ptr)
{
    if (!camera_ptr)
    {
        rt_trap("Camera.ClearBounds: null camera");
        return;
    }
    ((rt_camera_impl *)camera_ptr)->has_bounds = 0;
}

//=============================================================================
// Visibility Culling
//=============================================================================

int64_t rt_camera_is_visible(void *camera_ptr, int64_t x, int64_t y, int64_t w, int64_t h)
{
    if (!camera_ptr)
        return 1; // Null camera — conservatively treat as visible
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;

    // Viewport in world space: top-left = (cam_x, cam_y),
    // size = (viewport_w * 100 / zoom, viewport_h * 100 / zoom).
    int64_t vx = camera->x;
    int64_t vy = camera->y;
    int64_t vw = camera->width * 100 / camera->zoom;
    int64_t vh = camera->height * 100 / camera->zoom;

    // AABB overlap test: return 0 if separated on any axis.
    if (x + w <= vx || x >= vx + vw || y + h <= vy || y >= vy + vh)
        return 0;
    return 1;
}

//=============================================================================
// Dirty Flag — Enables callers to skip re-rendering when camera is stationary
//=============================================================================

int64_t rt_camera_is_dirty(void *camera_ptr)
{
    if (!camera_ptr)
        return 0;
    return ((rt_camera_impl *)camera_ptr)->dirty;
}

void rt_camera_clear_dirty(void *camera_ptr)
{
    if (!camera_ptr)
        return;
    ((rt_camera_impl *)camera_ptr)->dirty = 0;
}

//=============================================================================
// Parallax Layer Management
//=============================================================================

int64_t rt_camera_add_parallax(void *camera_ptr,
                               void *pixels,
                               int64_t scroll_x_pct,
                               int64_t scroll_y_pct)
{
    if (!camera_ptr || !pixels)
        return -1;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    if (camera->parallax_count >= RT_CAMERA_MAX_PARALLAX)
        return -1;

    for (int i = 0; i < RT_CAMERA_MAX_PARALLAX; i++)
    {
        if (!camera->parallax[i].active)
        {
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

void rt_camera_remove_parallax(void *camera_ptr, int64_t index)
{
    if (!camera_ptr)
        return;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    if (index < 0 || index >= RT_CAMERA_MAX_PARALLAX)
        return;
    if (camera->parallax[index].active)
    {
        camera->parallax[index].active = 0;
        camera->parallax[index].pixels = NULL;
        camera->parallax_count--;
    }
}

void rt_camera_clear_parallax(void *camera_ptr)
{
    if (!camera_ptr)
        return;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;
    memset(camera->parallax, 0, sizeof(camera->parallax));
    camera->parallax_count = 0;
}

int64_t rt_camera_parallax_count(void *camera_ptr)
{
    if (!camera_ptr)
        return 0;
    return ((rt_camera_impl *)camera_ptr)->parallax_count;
}

int64_t rt_camera_draw_parallax(void *camera_ptr, void *canvas)
{
    if (!camera_ptr || !canvas)
        return 0;
    rt_camera_impl *camera = (rt_camera_impl *)camera_ptr;

    int64_t layers_drawn = 0;

    for (int i = 0; i < RT_CAMERA_MAX_PARALLAX; i++)
    {
        rt_parallax_layer *layer = &camera->parallax[i];
        if (!layer->active || !layer->pixels)
            continue;

        int64_t pw = rt_pixels_width(layer->pixels);
        int64_t ph = rt_pixels_height(layer->pixels);

        if (pw <= 0 || ph <= 0)
            continue;

        /* Compute the parallax scroll offset */
        int64_t scroll_x = camera->x * layer->scroll_factor_x / 100;
        int64_t scroll_y = camera->y * layer->scroll_factor_y / 100 + layer->offset_y;

        /* Compute starting tile position (wrap negative modulo) */
        int64_t start_x = -(scroll_x % pw);
        if (start_x > 0)
            start_x -= pw;
        int64_t start_y = -(scroll_y % ph);
        if (start_y > 0)
            start_y -= ph;

        /* Tile the pixels across the viewport */
        for (int64_t ty = start_y; ty < camera->height; ty += ph)
        {
            for (int64_t tx = start_x; tx < camera->width; tx += pw)
            {
                rt_canvas_blit_alpha(canvas, tx, ty, layer->pixels);
            }
        }

        layers_drawn++;
    }

    return layers_drawn;
}
