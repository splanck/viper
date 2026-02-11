//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_tween.h
// Purpose: Frame-based tweening and interpolation with various easing
//   functions for smooth animations. Supports double and int64 endpoints,
//   play/pause/stop/resume lifecycle, and static lerp/easing helpers.
// Key invariants: Duration >= 1 frame. Easing type must be a valid
//   rt_ease_type (0..RT_EASE_COUNT-1). Progress is [0, 100]. Tween value
//   equals 'from' at start and 'to' upon completion.
// Ownership/Lifetime: Caller owns the rt_tween handle and must free it with
//   rt_tween_destroy(). Static functions are pure and allocation-free.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_TWEEN_H
#define VIPER_RT_TWEEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Easing function types.
    typedef enum rt_ease_type
    {
        RT_EASE_LINEAR = 0,    ///< Linear interpolation (no easing)
        RT_EASE_IN_QUAD,       ///< Quadratic ease-in
        RT_EASE_OUT_QUAD,      ///< Quadratic ease-out
        RT_EASE_IN_OUT_QUAD,   ///< Quadratic ease-in-out
        RT_EASE_IN_CUBIC,      ///< Cubic ease-in
        RT_EASE_OUT_CUBIC,     ///< Cubic ease-out
        RT_EASE_IN_OUT_CUBIC,  ///< Cubic ease-in-out
        RT_EASE_IN_SINE,       ///< Sinusoidal ease-in
        RT_EASE_OUT_SINE,      ///< Sinusoidal ease-out
        RT_EASE_IN_OUT_SINE,   ///< Sinusoidal ease-in-out
        RT_EASE_IN_EXPO,       ///< Exponential ease-in
        RT_EASE_OUT_EXPO,      ///< Exponential ease-out
        RT_EASE_IN_OUT_EXPO,   ///< Exponential ease-in-out
        RT_EASE_IN_BACK,       ///< Back ease-in (overshoots)
        RT_EASE_OUT_BACK,      ///< Back ease-out (overshoots)
        RT_EASE_IN_OUT_BACK,   ///< Back ease-in-out
        RT_EASE_IN_BOUNCE,     ///< Bounce ease-in
        RT_EASE_OUT_BOUNCE,    ///< Bounce ease-out
        RT_EASE_IN_OUT_BOUNCE, ///< Bounce ease-in-out
        RT_EASE_COUNT          ///< Number of easing types
    } rt_ease_type;

    /// Opaque handle to a Tween instance.
    typedef struct rt_tween_impl *rt_tween;

    /// @brief Allocates and initializes a new Tween in the idle state.
    /// @return A new Tween handle. The caller must free it with
    ///   rt_tween_destroy().
    rt_tween rt_tween_new(void);

    /// @brief Destroys a Tween and releases its memory.
    /// @param tween The tween to destroy. Passing NULL is a no-op.
    void rt_tween_destroy(rt_tween tween);

    /// @brief Begins a tween animation interpolating between two double values.
    ///
    /// Resets the tween state and starts interpolating from @p from toward
    /// @p to over the given duration using the specified easing curve.
    /// @param tween The tween to start.
    /// @param from Starting value of the interpolation.
    /// @param to Ending value of the interpolation.
    /// @param duration Total duration in game frames. Must be >= 1.
    /// @param ease_type Easing function to apply, as an rt_ease_type value
    ///   (0 = linear, up to RT_EASE_COUNT-1).
    void rt_tween_start(
        rt_tween tween, double from, double to, int64_t duration, int64_t ease_type);

    /// @brief Begins a tween animation interpolating between two integer values.
    ///
    /// Behaves identically to rt_tween_start() but accepts int64 endpoints.
    /// Retrieve the result with rt_tween_value_i64().
    /// @param tween The tween to start.
    /// @param from Starting integer value.
    /// @param to Ending integer value.
    /// @param duration Total duration in game frames. Must be >= 1.
    /// @param ease_type Easing function to apply, as an rt_ease_type value.
    void rt_tween_start_i64(
        rt_tween tween, int64_t from, int64_t to, int64_t duration, int64_t ease_type);

    /// @brief Advances the tween by one game frame.
    ///
    /// Must be called once per frame while the tween is running.
    /// @param tween The tween to update.
    /// @return 1 if the tween just completed on this frame (elapsed reached
    ///   duration), 0 otherwise.
    int8_t rt_tween_update(rt_tween tween);

    /// @brief Retrieves the current interpolated value as a double.
    /// @param tween The tween to query.
    /// @return The eased value between 'from' and 'to' at the current elapsed
    ///   time. Returns 'from' before the first update, and 'to' after
    ///   completion.
    double rt_tween_value(rt_tween tween);

    /// @brief Retrieves the current interpolated value as a rounded integer.
    /// @param tween The tween to query.
    /// @return The eased value between 'from' and 'to', rounded to the nearest
    ///   int64.
    int64_t rt_tween_value_i64(rt_tween tween);

    /// @brief Queries whether the tween is currently running (not paused, not
    ///   completed).
    /// @param tween The tween to query.
    /// @return 1 if the tween is actively interpolating, 0 if idle, paused, or
    ///   completed.
    int8_t rt_tween_is_running(rt_tween tween);

    /// @brief Queries whether the tween has reached its end value.
    /// @param tween The tween to query.
    /// @return 1 if the tween has completed (elapsed >= duration), 0 otherwise.
    int8_t rt_tween_is_complete(rt_tween tween);

    /// @brief Retrieves the tween progress as an integer percentage.
    /// @param tween The tween to query.
    /// @return A value from 0 (just started) to 100 (completed).
    int64_t rt_tween_progress(rt_tween tween);

    /// @brief Retrieves the number of frames elapsed since the tween started.
    /// @param tween The tween to query.
    /// @return Elapsed frames, in [0, duration].
    int64_t rt_tween_elapsed(rt_tween tween);

    /// @brief Retrieves the total duration of the tween.
    /// @param tween The tween to query.
    /// @return Total duration in game frames.
    int64_t rt_tween_duration(rt_tween tween);

    /// @brief Stops the tween and marks it as complete at the current value.
    /// @param tween The tween to stop.
    void rt_tween_stop(rt_tween tween);

    /// @brief Resets the tween to the beginning without changing its from/to
    ///   or duration settings.
    /// @param tween The tween to reset. The tween remains in the running state
    ///   if it was running.
    void rt_tween_reset(rt_tween tween);

    /// @brief Pauses the tween at its current position.
    ///
    /// The tween retains its elapsed time and can be resumed with
    /// rt_tween_resume().
    /// @param tween The tween to pause.
    void rt_tween_pause(rt_tween tween);

    /// @brief Resumes a paused tween from its current position.
    /// @param tween The tween to resume. Has no effect if not paused.
    void rt_tween_resume(rt_tween tween);

    /// @brief Queries whether the tween is currently paused.
    /// @param tween The tween to query.
    /// @return 1 if paused (can be resumed), 0 otherwise.
    int8_t rt_tween_is_paused(rt_tween tween);

    //=============================================================================
    // Static interpolation functions (no Tween instance needed)
    //=============================================================================

    /// @brief Performs integer linear interpolation between two values.
    /// @param from Starting value.
    /// @param to Ending value.
    /// @param t Interpolation progress from 0.0 (returns @p from) to 1.0
    ///   (returns @p to). Values outside [0, 1] will extrapolate.
    /// @return The interpolated value, rounded to the nearest integer.
    int64_t rt_tween_lerp_i64(int64_t from, int64_t to, double t);

    /// @brief Applies an easing function to a normalized progress value.
    /// @param t Linear progress from 0.0 to 1.0.
    /// @param ease_type Easing function to apply, as an rt_ease_type value
    ///   (0 = linear, up to RT_EASE_COUNT-1).
    /// @return The eased progress value. For most easing types the result is
    ///   in [0, 1], but back and elastic types may overshoot.
    double rt_tween_ease(double t, int64_t ease_type);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_TWEEN_H
