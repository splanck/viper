//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_sprite.h
// Purpose: Sprite class for 2D game development with transform and animation.
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Sprite Creation
    //=========================================================================

    /// @brief Create a new sprite from a Pixels buffer.
    /// @param pixels Source Pixels object.
    /// @return New Sprite object.
    void *rt_sprite_new(void *pixels);

    /// @brief Create a new sprite by loading from a BMP file.
    /// @param path File path (runtime string).
    /// @return New Sprite object, or NULL on failure.
    void *rt_sprite_from_file(void *path);

    //=========================================================================
    // Sprite Properties
    //=========================================================================

    /// @brief Get sprite X position.
    int64_t rt_sprite_get_x(void *sprite);

    /// @brief Set sprite X position.
    void rt_sprite_set_x(void *sprite, int64_t x);

    /// @brief Get sprite Y position.
    int64_t rt_sprite_get_y(void *sprite);

    /// @brief Set sprite Y position.
    void rt_sprite_set_y(void *sprite, int64_t y);

    /// @brief Get sprite width (of current frame).
    int64_t rt_sprite_get_width(void *sprite);

    /// @brief Get sprite height (of current frame).
    int64_t rt_sprite_get_height(void *sprite);

    /// @brief Get sprite horizontal scale (100 = 100%).
    int64_t rt_sprite_get_scale_x(void *sprite);

    /// @brief Set sprite horizontal scale (100 = 100%).
    void rt_sprite_set_scale_x(void *sprite, int64_t scale);

    /// @brief Get sprite vertical scale (100 = 100%).
    int64_t rt_sprite_get_scale_y(void *sprite);

    /// @brief Set sprite vertical scale (100 = 100%).
    void rt_sprite_set_scale_y(void *sprite, int64_t scale);

    /// @brief Get sprite rotation in degrees.
    int64_t rt_sprite_get_rotation(void *sprite);

    /// @brief Set sprite rotation in degrees.
    void rt_sprite_set_rotation(void *sprite, int64_t degrees);

    /// @brief Get sprite visibility.
    int64_t rt_sprite_get_visible(void *sprite);

    /// @brief Set sprite visibility.
    void rt_sprite_set_visible(void *sprite, int64_t visible);

    /// @brief Get current animation frame index.
    int64_t rt_sprite_get_frame(void *sprite);

    /// @brief Set current animation frame index.
    void rt_sprite_set_frame(void *sprite, int64_t frame);

    /// @brief Get total number of frames.
    int64_t rt_sprite_get_frame_count(void *sprite);

    //=========================================================================
    // Sprite Methods
    //=========================================================================

    /// @brief Draw the sprite to a canvas.
    /// @param sprite Sprite object.
    /// @param canvas Canvas to draw on.
    void rt_sprite_draw(void *sprite, void *canvas);

    /// @brief Set the sprite's origin point for rotation/scaling.
    /// @param sprite Sprite object.
    /// @param x Origin X (relative to top-left).
    /// @param y Origin Y (relative to top-left).
    void rt_sprite_set_origin(void *sprite, int64_t x, int64_t y);

    /// @brief Add an animation frame from a Pixels buffer.
    /// @param sprite Sprite object.
    /// @param pixels Pixels object for the frame.
    void rt_sprite_add_frame(void *sprite, void *pixels);

    /// @brief Set the animation frame delay in milliseconds.
    /// @param sprite Sprite object.
    /// @param ms Delay between frames in milliseconds.
    void rt_sprite_set_frame_delay(void *sprite, int64_t ms);

    /// @brief Update animation (advances frame if delay has passed).
    /// @param sprite Sprite object.
    void rt_sprite_update(void *sprite);

    /// @brief Check if this sprite overlaps another sprite.
    /// @param sprite Sprite object.
    /// @param other Other sprite to check.
    /// @return true if overlapping, false otherwise.
    bool rt_sprite_overlaps(void *sprite, void *other);

    /// @brief Check if a point is inside the sprite's bounding box.
    /// @param sprite Sprite object.
    /// @param x Point X coordinate.
    /// @param y Point Y coordinate.
    /// @return true if point is inside, false otherwise.
    bool rt_sprite_contains(void *sprite, int64_t x, int64_t y);

    /// @brief Move sprite by delta amounts.
    /// @param sprite Sprite object.
    /// @param dx Delta X.
    /// @param dy Delta Y.
    void rt_sprite_move(void *sprite, int64_t dx, int64_t dy);

#ifdef __cplusplus
}
#endif
