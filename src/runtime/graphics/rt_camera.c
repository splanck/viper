//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_camera.c
// Purpose: 2D Camera class implementation.
//
//===----------------------------------------------------------------------===//

#include "rt_camera.h"

#include "rt_internal.h"
#include "rt_object.h"

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
