//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_screenfx.h
// Purpose: Screen effects manager for camera shake, color flash, and screen fades, with up to RT_SCREENFX_MAX_EFFECTS (8) concurrent effects outputting shake offsets and overlay colors per frame.
//
// Key invariants:
//   - Time values use fixed-point milliseconds; 1000 = 1 pixel for shake offsets.
//   - Colors are packed as 0xRRGGBBAA.
//   - rt_screenfx_update must be called once per frame with the elapsed delta time.
//   - Maximum concurrent effects is RT_SCREENFX_MAX_EFFECTS (8).
//
// Ownership/Lifetime:
//   - Caller owns the rt_screenfx handle; destroy with rt_screenfx_destroy.
//   - No reference counting; explicit destruction is required.
//
// Links: src/runtime/collections/rt_screenfx.c (implementation), src/runtime/graphics/rt_camera.h, src/runtime/collections/rt_particle.h
//
//===----------------------------------------------------------------------===//
#ifndef VIPER_RT_SCREENFX_H
#define VIPER_RT_SCREENFX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum number of concurrent effects.
#define RT_SCREENFX_MAX_EFFECTS 8

    /// Effect types.
    typedef enum rt_screenfx_type
    {
        RT_SCREENFX_NONE = 0,
        RT_SCREENFX_SHAKE = 1,
        RT_SCREENFX_FLASH = 2,
        RT_SCREENFX_FADE_IN = 3,
        RT_SCREENFX_FADE_OUT = 4,
    } rt_screenfx_type;

    /// Opaque handle to a ScreenFX manager.
    typedef struct rt_screenfx_impl *rt_screenfx;

    /// @brief Allocates and initializes a new ScreenFX effects manager.
    /// @return A new ScreenFX handle with no active effects. The caller must
    ///   free it with rt_screenfx_destroy().
    rt_screenfx rt_screenfx_new(void);

    /// @brief Destroys a ScreenFX manager and releases its memory.
    /// @param fx The effects manager to destroy. Passing NULL is a no-op.
    void rt_screenfx_destroy(rt_screenfx fx);

    /// @brief Advances all active screen effects by the given time delta.
    ///
    /// Decays shake intensity, advances flash/fade timers, and removes expired
    /// effects. Must be called once per frame.
    /// @param fx The effects manager to update.
    /// @param dt Elapsed time since the last update, in fixed-point seconds
    ///   (1000 = 1 second).
    void rt_screenfx_update(rt_screenfx fx, int64_t dt);

    /// @brief Starts a camera shake effect with configurable intensity and
    ///   decay.
    ///
    /// Produces random per-frame offsets that the renderer should add to the
    /// camera position. The offsets decay over the duration.
    /// @param fx The effects manager.
    /// @param intensity Maximum shake displacement in fixed-point pixels
    ///   (1000 = 1 pixel).
    /// @param duration Duration of the shake in milliseconds.
    /// @param decay Decay rate controlling how quickly intensity falls off,
    ///   in the range [0, 1000] where 0 = no decay (constant intensity) and
    ///   1000 = instant decay.
    void rt_screenfx_shake(rt_screenfx fx, int64_t intensity, int64_t duration, int64_t decay);

    /// @brief Starts a full-screen color flash effect.
    ///
    /// The overlay appears instantly at the given color and fades out over the
    /// duration. Useful for damage indicators, pickups, or impact feedback.
    ///
    /// @note Color format: packed @b 0xRRGGBBAA (alpha in the least-significant
    ///   byte). This differs from the @c 0x00RRGGBB format used by Canvas
    ///   drawing methods. Example: a full-opacity white flash is @c 0xFFFFFFFF;
    ///   a half-opacity red flash is @c 0xFF000080.
    /// @param fx The effects manager.
    /// @param color Flash color in packed 0xRRGGBBAA format.
    /// @param duration Duration of the flash in milliseconds.
    void rt_screenfx_flash(rt_screenfx fx, int64_t color, int64_t duration);

    /// @brief Starts a fade-in effect (from an opaque color overlay to fully
    ///   transparent).
    ///
    /// Commonly used for scene transitions where the screen starts covered
    /// and gradually reveals the scene.
    ///
    /// @note Color format: packed @b 0xRRGGBBAA — alpha in the least-significant
    ///   byte. Example: fade in from opaque black: @c 0x000000FF.
    /// @param fx The effects manager.
    /// @param color Starting overlay color in packed 0xRRGGBBAA format.
    /// @param duration Duration of the fade in milliseconds.
    void rt_screenfx_fade_in(rt_screenfx fx, int64_t color, int64_t duration);

    /// @brief Starts a fade-out effect (from fully transparent to an opaque
    ///   color overlay).
    ///
    /// Commonly used for scene transitions where the screen gradually becomes
    /// covered before switching scenes.
    ///
    /// @note Color format: packed @b 0xRRGGBBAA — alpha in the least-significant
    ///   byte. Example: fade to opaque black: @c 0x000000FF.
    /// @param fx The effects manager.
    /// @param color Target overlay color in packed 0xRRGGBBAA format.
    /// @param duration Duration of the fade in milliseconds.
    void rt_screenfx_fade_out(rt_screenfx fx, int64_t color, int64_t duration);

    /// @brief Immediately cancels all active effects.
    ///
    /// Resets shake offsets to zero and overlay alpha to transparent.
    /// @param fx The effects manager.
    void rt_screenfx_cancel_all(rt_screenfx fx);

    /// @brief Cancels all active effects of a specific type.
    /// @param fx The effects manager.
    /// @param type The effect type to cancel, as an rt_screenfx_type value
    ///   (e.g., RT_SCREENFX_SHAKE, RT_SCREENFX_FLASH).
    void rt_screenfx_cancel_type(rt_screenfx fx, int64_t type);

    /// @brief Queries whether any screen effect is currently running.
    /// @param fx The effects manager to query.
    /// @return 1 if at least one effect is active, 0 if all effects have
    ///   expired or been cancelled.
    int8_t rt_screenfx_is_active(rt_screenfx fx);

    /// @brief Queries whether a specific effect type is currently running.
    /// @param fx The effects manager to query.
    /// @param type The effect type to check, as an rt_screenfx_type value.
    /// @return 1 if at least one effect of this type is active, 0 otherwise.
    int8_t rt_screenfx_is_type_active(rt_screenfx fx, int64_t type);

    /// @brief Retrieves the current horizontal camera shake offset.
    ///
    /// The renderer should add this offset to the camera X position each frame
    /// to produce the shake effect.
    /// @param fx The effects manager to query.
    /// @return X displacement in fixed-point pixels (1000 = 1 pixel). Zero
    ///   when no shake is active.
    int64_t rt_screenfx_get_shake_x(rt_screenfx fx);

    /// @brief Retrieves the current vertical camera shake offset.
    ///
    /// The renderer should add this offset to the camera Y position each frame
    /// to produce the shake effect.
    /// @param fx The effects manager to query.
    /// @return Y displacement in fixed-point pixels (1000 = 1 pixel). Zero
    ///   when no shake is active.
    int64_t rt_screenfx_get_shake_y(rt_screenfx fx);

    /// @brief Retrieves the current overlay color for flash and fade effects.
    ///
    /// The renderer should draw a filled rectangle with this color over the
    /// entire screen at the alpha returned by rt_screenfx_get_overlay_alpha().
    /// @param fx The effects manager to query.
    /// @return The overlay color in packed 0xRRGGBBAA format. Returns 0 when
    ///   no flash or fade effect is active.
    int64_t rt_screenfx_get_overlay_color(rt_screenfx fx);

    /// @brief Retrieves the current overlay alpha intensity for flash and fade
    ///   effects.
    /// @param fx The effects manager to query.
    /// @return Alpha value in [0, 255], where 0 is fully transparent and 255
    ///   is fully opaque. Returns 0 when no flash or fade effect is active.
    int64_t rt_screenfx_get_overlay_alpha(rt_screenfx fx);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SCREENFX_H
