//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_screenfx.h
/// @brief Screen effects manager for camera shake, color flash, and fades.
///
/// Provides common screen-wide visual effects used in games:
/// - Camera shake with configurable intensity and decay
/// - Color flash effects (damage flash, pickup flash, etc.)
/// - Screen fades (fade in, fade out, crossfade)
///
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

    /// Creates a new ScreenFX manager.
    /// @return A new ScreenFX instance.
    rt_screenfx rt_screenfx_new(void);

    /// Destroys a ScreenFX manager.
    /// @param fx The manager to destroy.
    void rt_screenfx_destroy(rt_screenfx fx);

    /// Updates all active effects.
    /// @param fx The manager.
    /// @param dt Delta time in seconds (fixed-point: 1000 = 1 second).
    void rt_screenfx_update(rt_screenfx fx, int64_t dt);

    /// Starts a camera shake effect.
    /// @param fx The manager.
    /// @param intensity Shake intensity (pixels, fixed-point: 1000 = 1 pixel).
    /// @param duration Duration in milliseconds.
    /// @param decay Decay rate (0-1000, where 1000 = instant decay).
    void rt_screenfx_shake(rt_screenfx fx, int64_t intensity, int64_t duration, int64_t decay);

    /// Starts a color flash effect.
    /// @param fx The manager.
    /// @param color Flash color (RGBA as 0xRRGGBBAA).
    /// @param duration Duration in milliseconds.
    void rt_screenfx_flash(rt_screenfx fx, int64_t color, int64_t duration);

    /// Starts a fade-in effect (from color to clear).
    /// @param fx The manager.
    /// @param color Fade color (RGBA as 0xRRGGBBAA).
    /// @param duration Duration in milliseconds.
    void rt_screenfx_fade_in(rt_screenfx fx, int64_t color, int64_t duration);

    /// Starts a fade-out effect (from clear to color).
    /// @param fx The manager.
    /// @param color Fade color (RGBA as 0xRRGGBBAA).
    /// @param duration Duration in milliseconds.
    void rt_screenfx_fade_out(rt_screenfx fx, int64_t color, int64_t duration);

    /// Cancels all active effects.
    /// @param fx The manager.
    void rt_screenfx_cancel_all(rt_screenfx fx);

    /// Cancels effects of a specific type.
    /// @param fx The manager.
    /// @param type Effect type to cancel.
    void rt_screenfx_cancel_type(rt_screenfx fx, int64_t type);

    /// Checks if any effect is active.
    /// @param fx The manager.
    /// @return 1 if any effect is active, 0 otherwise.
    int8_t rt_screenfx_is_active(rt_screenfx fx);

    /// Checks if a specific effect type is active.
    /// @param fx The manager.
    /// @param type Effect type to check.
    /// @return 1 if effect is active, 0 otherwise.
    int8_t rt_screenfx_is_type_active(rt_screenfx fx, int64_t type);

    /// Gets the current camera shake offset X.
    /// @param fx The manager.
    /// @return X offset (fixed-point: 1000 = 1 pixel).
    int64_t rt_screenfx_get_shake_x(rt_screenfx fx);

    /// Gets the current camera shake offset Y.
    /// @param fx The manager.
    /// @return Y offset (fixed-point: 1000 = 1 pixel).
    int64_t rt_screenfx_get_shake_y(rt_screenfx fx);

    /// Gets the current overlay color (for flash/fade effects).
    /// @param fx The manager.
    /// @return RGBA color (0xRRGGBBAA).
    int64_t rt_screenfx_get_overlay_color(rt_screenfx fx);

    /// Gets the current overlay alpha (0-255).
    /// @param fx The manager.
    /// @return Alpha value 0-255.
    int64_t rt_screenfx_get_overlay_alpha(rt_screenfx fx);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SCREENFX_H
