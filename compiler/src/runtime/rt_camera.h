//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_camera.h
// Purpose: 2D Camera class for viewport and scrolling.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
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
    void rt_camera_set_bounds(
        void *camera, int64_t min_x, int64_t min_y, int64_t max_x, int64_t max_y);

    /// @brief Clear camera bounds (allow unlimited movement).
    void rt_camera_clear_bounds(void *camera);

#ifdef __cplusplus
}
#endif
