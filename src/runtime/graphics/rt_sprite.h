//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_sprite.h
// Purpose: 2D sprite with transform (position, rotation, scale), animation frame tracking, and
// canvas-based rendering for game development.
//
// Key invariants:
//   - Position is in logical pixels; rotation is in degrees.
//   - Scale is a multiplier applied to the source frame dimensions.
//   - rt_sprite_draw renders the current frame to the canvas.
//   - Frame index wraps when set beyond the frame count.
//
// Ownership/Lifetime:
//   - Sprite objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_sprite.c (implementation)
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

    /// @brief Get sprite horizontal flip.
    int64_t rt_sprite_get_flip_x(void *sprite);

    /// @brief Set sprite horizontal flip.
    void rt_sprite_set_flip_x(void *sprite, int64_t flip);

    /// @brief Get sprite vertical flip.
    int64_t rt_sprite_get_flip_y(void *sprite);

    /// @brief Set sprite vertical flip.
    void rt_sprite_set_flip_y(void *sprite, int64_t flip);

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

    //=========================================================================
    // Sprite Animator (named animation clip state machine)
    //=========================================================================

    /// Maximum number of named animation clips per animator.
#define RT_ANIM_MAX_CLIPS 32

    /// @brief A single named animation clip.
    typedef struct rt_anim_clip
    {
        char name[64];          ///< Clip name (NUL-terminated)
        int64_t start_frame;    ///< First frame index in the sprite's frames[]
        int64_t frame_count;    ///< Number of frames in this clip
        int64_t frame_delay_ms; ///< Milliseconds between frames
        int loop;               ///< 1 = loop, 0 = play once and stop
    } rt_anim_clip_t;

    /// @brief Named animation state machine for sprites.
    /// @details Tracks which clip is playing, the position within that clip,
    ///          and elapsed time. Call rt_sprite_animator_update() each frame
    ///          to advance the animation and synchronize the sprite's frame.
    typedef struct rt_sprite_animator
    {
        rt_anim_clip_t clips[RT_ANIM_MAX_CLIPS]; ///< Registered clips
        int clip_count;                          ///< Number of registered clips
        int current_clip;                        ///< Index of playing clip (-1 = idle)
        int64_t clip_frame;                      ///< Frame index within current clip
        int64_t last_update_ms;                  ///< Timestamp of last frame advance
        int playing;                             ///< 1 if animation is running
    } rt_sprite_animator_t;

    /// @brief Allocate a new animator (caller owns; free with rt_sprite_animator_destroy).
    rt_sprite_animator_t *rt_sprite_animator_new(void);

    /// @brief Free an animator allocated with rt_sprite_animator_new.
    void rt_sprite_animator_destroy(rt_sprite_animator_t *animator);

    /// @brief Register a named animation clip.
    /// @param animator   Animator to add clip to.
    /// @param name       Clip name (max 63 chars).
    /// @param start_frame First frame index in the sprite's frames[].
    /// @param frame_count Number of frames in the clip.
    /// @param frame_delay_ms Milliseconds between frames.
    /// @param loop       1 = loop, 0 = play once then stop.
    /// @return 1 on success, 0 if clip table is full or name is NULL.
    int rt_sprite_animator_add_clip(rt_sprite_animator_t *animator,
                                    const char *name,
                                    int64_t start_frame,
                                    int64_t frame_count,
                                    int64_t frame_delay_ms,
                                    int loop);

    /// @brief Start playing a named clip.
    /// @param animator Animator.
    /// @param name     Clip name (must match a registered clip).
    /// @return 1 if clip found and started, 0 if not found.
    int rt_sprite_animator_play(rt_sprite_animator_t *animator, const char *name);

    /// @brief Stop the current animation (leaves sprite on its current frame).
    void rt_sprite_animator_stop(rt_sprite_animator_t *animator);

    /// @brief Advance the animation and update the sprite's current frame.
    /// @details Must be called once per frame. Automatically advances clip_frame
    ///          based on elapsed time and sets the sprite's frame accordingly.
    /// @param animator Animator to advance.
    /// @param sprite   Sprite whose frame will be updated.
    void rt_sprite_animator_update(rt_sprite_animator_t *animator, void *sprite);

    /// @brief Return 1 if the animator is currently playing a clip, 0 otherwise.
    int rt_sprite_animator_is_playing(rt_sprite_animator_t *animator);

    /// @brief Return the name of the currently playing clip, or NULL if idle.
    const char *rt_sprite_animator_get_current(rt_sprite_animator_t *animator);

#ifdef __cplusplus
}
#endif
