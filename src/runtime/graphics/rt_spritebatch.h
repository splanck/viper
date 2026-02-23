//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_spritebatch.h
// Purpose: SpriteBatch for efficient batched sprite rendering, accumulating draw calls and submitting them to the GPU in a single batch to reduce render API overhead.
//
// Key invariants:
//   - Begin must be called before any draw calls; End flushes the batch.
//   - Draw calls are sorted by texture to minimize state changes.
//   - Batch size is bounded by RT_SPRITEBATCH_MAX_SPRITES per begin/end pair.
//   - Nested Begin/End pairs are not supported.
//
// Ownership/Lifetime:
//   - SpriteBatch objects are heap-allocated opaque pointers.
//   - Caller is responsible for lifetime management.
//
// Links: src/runtime/graphics/rt_spritebatch.c (implementation)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // SpriteBatch Creation
    //=========================================================================

    /// @brief Create a new sprite batch.
    /// @param capacity Initial capacity for sprites (0 for default).
    /// @return New SpriteBatch object.
    void *rt_spritebatch_new(int64_t capacity);

    //=========================================================================
    // SpriteBatch Operations
    //=========================================================================

    /// @brief Begin a new batch.
    /// @param batch SpriteBatch object.
    /// @note Clears any pending draws from previous batch.
    void rt_spritebatch_begin(void *batch);

    /// @brief End the batch and draw all sprites to the canvas.
    /// @param batch SpriteBatch object.
    /// @param canvas Canvas to draw on.
    /// @note Sprites are drawn in the order they were added.
    void rt_spritebatch_end(void *batch, void *canvas);

    /// @brief Draw a sprite at the given position.
    /// @param batch SpriteBatch object.
    /// @param sprite Sprite object to draw.
    /// @param x X position.
    /// @param y Y position.
    void rt_spritebatch_draw(void *batch, void *sprite, int64_t x, int64_t y);

    /// @brief Draw a sprite with scale.
    /// @param batch SpriteBatch object.
    /// @param sprite Sprite object to draw.
    /// @param x X position.
    /// @param y Y position.
    /// @param scale Scale factor (100 = 100%).
    void rt_spritebatch_draw_scaled(void *batch, void *sprite, int64_t x, int64_t y, int64_t scale);

    /// @brief Draw a sprite with full transform.
    /// @param batch SpriteBatch object.
    /// @param sprite Sprite object to draw.
    /// @param x X position.
    /// @param y Y position.
    /// @param scale_x Horizontal scale (100 = 100%).
    /// @param scale_y Vertical scale (100 = 100%).
    /// @param rotation Rotation in degrees.
    void rt_spritebatch_draw_ex(void *batch,
                                void *sprite,
                                int64_t x,
                                int64_t y,
                                int64_t scale_x,
                                int64_t scale_y,
                                int64_t rotation);

    /// @brief Draw a Pixels buffer directly.
    /// @param batch SpriteBatch object.
    /// @param pixels Pixels object to draw.
    /// @param x X position.
    /// @param y Y position.
    void rt_spritebatch_draw_pixels(void *batch, void *pixels, int64_t x, int64_t y);

    /// @brief Draw a sub-region of a Pixels buffer.
    /// @param batch SpriteBatch object.
    /// @param pixels Pixels object.
    /// @param dx Destination X.
    /// @param dy Destination Y.
    /// @param sx Source X.
    /// @param sy Source Y.
    /// @param sw Source width.
    /// @param sh Source height.
    void rt_spritebatch_draw_region(void *batch,
                                    void *pixels,
                                    int64_t dx,
                                    int64_t dy,
                                    int64_t sx,
                                    int64_t sy,
                                    int64_t sw,
                                    int64_t sh);

    //=========================================================================
    // SpriteBatch Properties
    //=========================================================================

    /// @brief Get the number of sprites currently in the batch.
    int64_t rt_spritebatch_count(void *batch);

    /// @brief Get the current capacity of the batch.
    int64_t rt_spritebatch_capacity(void *batch);

    /// @brief Check if the batch is currently recording.
    int8_t rt_spritebatch_is_active(void *batch);

    //=========================================================================
    // SpriteBatch Settings
    //=========================================================================

    /// @brief Set whether to sort sprites by depth before rendering.
    /// @param batch SpriteBatch object.
    /// @param enabled 1 to enable depth sorting, 0 to disable.
    void rt_spritebatch_set_sort_by_depth(void *batch, int8_t enabled);

    /// @brief Set a global tint color for all sprites in the batch.
    /// @param batch SpriteBatch object.
    /// @param color Tint color (0xAARRGGBB), or 0 for no tint.
    void rt_spritebatch_set_tint(void *batch, int64_t color);

    /// @brief Set a global alpha for all sprites in the batch.
    /// @param batch SpriteBatch object.
    /// @param alpha Alpha value (0-255).
    void rt_spritebatch_set_alpha(void *batch, int64_t alpha);

    /// @brief Clear all settings to defaults.
    void rt_spritebatch_reset_settings(void *batch);

#ifdef __cplusplus
}
#endif
