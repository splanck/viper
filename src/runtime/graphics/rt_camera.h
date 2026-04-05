//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_camera.h
// Purpose: 2D camera for viewport and scrolling, providing world-to-screen and screen-to-world
// coordinate transforms, zoom, and shake offset application.
//
// Key invariants:
//   - Camera tracks a viewport rectangle in world space.
//   - World-to-screen transforms apply offset and zoom.
//   - Screen-to-world is the inverse transform for mouse picking.
//   - Shake offsets (from rt_screenfx) are applied separately to avoid accumulating into the base
//   position.
//
// Ownership/Lifetime:
//   - Camera objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_camera.c (implementation), src/runtime/collections/rt_screenfx.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//=========================================================================
// Camera Creation
//=========================================================================

/// @brief Create a new camera with specified viewport size.
/// @param width Viewport width.
/// @param height Viewport height.
/// @return New Camera object.
void *rt_camera_new(int64_t width, int64_t height);

//=========================================================================
// Camera Properties
//=========================================================================

/// @brief Get camera X position (world coordinates).
int64_t rt_camera_get_x(void *camera);

/// @brief Set camera X position (world coordinates).
void rt_camera_set_x(void *camera, int64_t x);

/// @brief Get camera Y position (world coordinates).
int64_t rt_camera_get_y(void *camera);

/// @brief Set camera Y position (world coordinates).
void rt_camera_set_y(void *camera, int64_t y);

/// @brief Get camera zoom level (100 = 100%).
int64_t rt_camera_get_zoom(void *camera);

/// @brief Set camera zoom level (100 = 100%).
void rt_camera_set_zoom(void *camera, int64_t zoom);

/// @brief Get camera rotation in degrees.
int64_t rt_camera_get_rotation(void *camera);

/// @brief Set camera rotation in degrees.
void rt_camera_set_rotation(void *camera, int64_t degrees);

/// @brief Get viewport width.
int64_t rt_camera_get_width(void *camera);

/// @brief Get viewport height.
int64_t rt_camera_get_height(void *camera);

//=========================================================================
// Camera Methods
//=========================================================================

/// @brief Center the camera on a world position.
/// @param camera Camera object.
/// @param x World X coordinate to center on.
/// @param y World Y coordinate to center on.
void rt_camera_follow(void *camera, int64_t x, int64_t y);

/// @brief Convert world coordinates to screen coordinates.
/// @param camera Camera object.
/// @param world_x World X coordinate.
/// @param world_y World Y coordinate.
/// @param screen_x Output: Screen X coordinate.
/// @param screen_y Output: Screen Y coordinate.
void rt_camera_world_to_screen(
    void *camera, int64_t world_x, int64_t world_y, int64_t *screen_x, int64_t *screen_y);

/// @brief Get screen X from world X.
int64_t rt_camera_to_screen_x(void *camera, int64_t world_x);

/// @brief Get screen Y from world Y.
int64_t rt_camera_to_screen_y(void *camera, int64_t world_y);

/// @brief Convert screen coordinates to world coordinates.
/// @param camera Camera object.
/// @param screen_x Screen X coordinate.
/// @param screen_y Screen Y coordinate.
/// @param world_x Output: World X coordinate.
/// @param world_y Output: World Y coordinate.
void rt_camera_screen_to_world(
    void *camera, int64_t screen_x, int64_t screen_y, int64_t *world_x, int64_t *world_y);

/// @brief Get world X from screen X.
int64_t rt_camera_to_world_x(void *camera, int64_t screen_x);

/// @brief Get world Y from screen Y.
int64_t rt_camera_to_world_y(void *camera, int64_t screen_y);

/// @brief Move the camera by delta amounts.
/// @param camera Camera object.
/// @param dx Delta X.
/// @param dy Delta Y.
void rt_camera_move(void *camera, int64_t dx, int64_t dy);

/// @brief Set camera bounds (limits where camera can go).
/// @param camera Camera object.
/// @param min_x Minimum X position.
/// @param min_y Minimum Y position.
/// @param max_x Maximum X position.
/// @param max_y Maximum Y position.
void rt_camera_set_bounds(void *camera, int64_t min_x, int64_t min_y, int64_t max_x, int64_t max_y);

/// @brief Clear camera bounds (allow unlimited movement).
void rt_camera_clear_bounds(void *camera);

/// @brief Smoothly follow a target position with linear interpolation.
/// Respects deadzone if set. Applies bounds clamping after movement.
/// @param camera Camera object.
/// @param target_x Target world X (center of follow).
/// @param target_y Target world Y (center of follow).
/// @param lerp_pct Interpolation speed 0-1000 (1000 = instant, 100 = slow smooth).
void rt_camera_smooth_follow(void *camera, int64_t target_x, int64_t target_y, int64_t lerp_pct);

/// @brief Set camera deadzone size. Target within the deadzone won't move the camera.
/// @param camera Camera object.
/// @param w Deadzone width in pixels (0 = disabled).
/// @param h Deadzone height in pixels (0 = disabled).
void rt_camera_set_deadzone(void *camera, int64_t w, int64_t h);

//=========================================================================
// Visibility Culling
//=========================================================================

/// @brief Test whether a world-space AABB overlaps the camera's viewport.
///
/// Converts the viewport to world space using the camera's current
/// position and zoom, then performs an AABB overlap test. Use this to
/// skip drawing off-screen entities each frame.
/// @param camera Camera object. Passing NULL conservatively returns 1
///   (visible).
/// @param x   Left edge of the AABB in world coordinates.
/// @param y   Top edge of the AABB in world coordinates.
/// @param w   Width of the AABB in world coordinates.
/// @param h   Height of the AABB in world coordinates.
/// @return 1 if any part of the AABB is visible through the viewport,
///   0 if entirely outside.
int64_t rt_camera_is_visible(void *camera, int64_t x, int64_t y, int64_t w, int64_t h);

//=========================================================================
// Dirty Flag — Skip re-rendering when camera is stationary
//=========================================================================

/// @brief Check whether the camera has moved, zoomed, or rotated since the
///        last call to rt_camera_clear_dirty.
/// @return 1 if dirty, 0 if clean.
int64_t rt_camera_is_dirty(void *camera);

/// @brief Clear the dirty flag after consuming the change.
void rt_camera_clear_dirty(void *camera);

//=========================================================================
// Parallax Layers
//=========================================================================

/// @brief Add a parallax scrolling layer.
/// @param camera Camera object.
/// @param pixels Pixels buffer to tile across the viewport.
/// @param scroll_x_pct X scroll factor (100 = camera speed, 50 = half, 0 = static).
/// @param scroll_y_pct Y scroll factor (100 = camera speed, 50 = half, 0 = static).
/// @return Layer index [0,7] on success, -1 if full or invalid args.
int64_t rt_camera_add_parallax(void *camera,
                               void *pixels,
                               int64_t scroll_x_pct,
                               int64_t scroll_y_pct);

/// @brief Remove a parallax layer by index.
void rt_camera_remove_parallax(void *camera, int64_t index);

/// @brief Remove all parallax layers.
void rt_camera_clear_parallax(void *camera);

/// @brief Get count of active parallax layers.
int64_t rt_camera_parallax_count(void *camera);

/// @brief Render all parallax layers to a canvas.
/// Layers are drawn in slot order (0-7). Each layer's Pixels buffer is tiled
/// horizontally and vertically to fill the viewport, scrolled by the camera
/// position multiplied by the layer's scroll factor.
/// @return Number of layers drawn.
int64_t rt_camera_draw_parallax(void *camera, void *canvas);

#ifdef __cplusplus
}
#endif
