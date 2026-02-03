//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_tween.h
/// @brief Tweening and interpolation utilities for smooth animations.
///
/// Provides frame-based tweening with various easing functions for smooth
/// animations in games and applications.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_TWEEN_H
#define VIPER_RT_TWEEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Easing function types.
typedef enum rt_ease_type
{
    RT_EASE_LINEAR = 0,      ///< Linear interpolation (no easing)
    RT_EASE_IN_QUAD,         ///< Quadratic ease-in
    RT_EASE_OUT_QUAD,        ///< Quadratic ease-out
    RT_EASE_IN_OUT_QUAD,     ///< Quadratic ease-in-out
    RT_EASE_IN_CUBIC,        ///< Cubic ease-in
    RT_EASE_OUT_CUBIC,       ///< Cubic ease-out
    RT_EASE_IN_OUT_CUBIC,    ///< Cubic ease-in-out
    RT_EASE_IN_SINE,         ///< Sinusoidal ease-in
    RT_EASE_OUT_SINE,        ///< Sinusoidal ease-out
    RT_EASE_IN_OUT_SINE,     ///< Sinusoidal ease-in-out
    RT_EASE_IN_EXPO,         ///< Exponential ease-in
    RT_EASE_OUT_EXPO,        ///< Exponential ease-out
    RT_EASE_IN_OUT_EXPO,     ///< Exponential ease-in-out
    RT_EASE_IN_BACK,         ///< Back ease-in (overshoots)
    RT_EASE_OUT_BACK,        ///< Back ease-out (overshoots)
    RT_EASE_IN_OUT_BACK,     ///< Back ease-in-out
    RT_EASE_IN_BOUNCE,       ///< Bounce ease-in
    RT_EASE_OUT_BOUNCE,      ///< Bounce ease-out
    RT_EASE_IN_OUT_BOUNCE,   ///< Bounce ease-in-out
    RT_EASE_COUNT            ///< Number of easing types
} rt_ease_type;

/// Opaque handle to a Tween instance.
typedef struct rt_tween_impl *rt_tween;

/// Creates a new Tween.
/// @return A new Tween instance.
rt_tween rt_tween_new(void);

/// Destroys a Tween and frees its memory.
/// @param tween The tween to destroy.
void rt_tween_destroy(rt_tween tween);

/// Starts a tween animation.
/// @param tween The tween.
/// @param from Starting value.
/// @param to Ending value.
/// @param duration Duration in frames.
/// @param ease_type Easing function to use.
void rt_tween_start(rt_tween tween, double from, double to, int64_t duration, int64_t ease_type);

/// Starts a tween using integer values.
/// @param tween The tween.
/// @param from Starting value.
/// @param to Ending value.
/// @param duration Duration in frames.
/// @param ease_type Easing function to use.
void rt_tween_start_i64(rt_tween tween, int64_t from, int64_t to, int64_t duration, int64_t ease_type);

/// Updates the tween by one frame.
/// @param tween The tween.
/// @return 1 if the tween just completed this frame, 0 otherwise.
int8_t rt_tween_update(rt_tween tween);

/// Gets the current interpolated value.
/// @param tween The tween.
/// @return Current tweened value.
double rt_tween_value(rt_tween tween);

/// Gets the current interpolated value as an integer.
/// @param tween The tween.
/// @return Current tweened value (rounded).
int64_t rt_tween_value_i64(rt_tween tween);

/// Checks if the tween is currently running.
/// @param tween The tween.
/// @return 1 if running, 0 if stopped or completed.
int8_t rt_tween_is_running(rt_tween tween);

/// Checks if the tween has completed.
/// @param tween The tween.
/// @return 1 if completed, 0 otherwise.
int8_t rt_tween_is_complete(rt_tween tween);

/// Gets the progress as a percentage (0-100).
/// @param tween The tween.
/// @return Progress percentage.
int64_t rt_tween_progress(rt_tween tween);

/// Gets the elapsed frames.
/// @param tween The tween.
/// @return Elapsed frames.
int64_t rt_tween_elapsed(rt_tween tween);

/// Gets the total duration.
/// @param tween The tween.
/// @return Total duration in frames.
int64_t rt_tween_duration(rt_tween tween);

/// Stops the tween.
/// @param tween The tween.
void rt_tween_stop(rt_tween tween);

/// Resets the tween to the beginning.
/// @param tween The tween.
void rt_tween_reset(rt_tween tween);

/// Pauses the tween.
/// @param tween The tween.
void rt_tween_pause(rt_tween tween);

/// Resumes a paused tween.
/// @param tween The tween.
void rt_tween_resume(rt_tween tween);

/// Checks if the tween is paused.
/// @param tween The tween.
/// @return 1 if paused, 0 otherwise.
int8_t rt_tween_is_paused(rt_tween tween);

//=============================================================================
// Static interpolation functions (no Tween instance needed)
//=============================================================================

/// Integer linear interpolation.
/// @param from Starting value.
/// @param to Ending value.
/// @param t Progress (0.0 to 1.0).
/// @return Interpolated value (rounded).
int64_t rt_tween_lerp_i64(int64_t from, int64_t to, double t);

/// Apply an easing function to a progress value.
/// @param t Progress (0.0 to 1.0).
/// @param ease_type Easing function type.
/// @return Eased progress value.
double rt_tween_ease(double t, int64_t ease_type);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_TWEEN_H
