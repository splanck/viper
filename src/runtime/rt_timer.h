//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_timer.h
/// @brief Frame-based timer utilities for games.
///
/// Provides countdown timers, repeating timers, and elapsed time tracking
/// based on frame counts rather than wall-clock time.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_TIMER_H
#define VIPER_RT_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /// Opaque handle to a Timer instance.
    typedef struct rt_timer_impl *rt_timer;

    /// Creates a new timer (initially stopped).
    /// @return A new Timer instance.
    rt_timer rt_timer_new(void);

    /// Destroys a timer and frees its memory.
    /// @param timer The timer to destroy.
    void rt_timer_destroy(rt_timer timer);

    /// Starts a countdown timer.
    /// @param timer The timer.
    /// @param frames Number of frames until expiration.
    void rt_timer_start(rt_timer timer, int64_t frames);

    /// Starts a repeating timer that auto-restarts when expired.
    /// @param timer The timer.
    /// @param frames Number of frames per cycle.
    void rt_timer_start_repeating(rt_timer timer, int64_t frames);

    /// Stops the timer.
    /// @param timer The timer.
    void rt_timer_stop(rt_timer timer);

    /// Resets the timer to its initial duration without stopping.
    /// @param timer The timer.
    void rt_timer_reset(rt_timer timer);

    /// Updates the timer (call once per frame).
    /// @param timer The timer.
    /// @return 1 if the timer just expired this frame, 0 otherwise.
    int8_t rt_timer_update(rt_timer timer);

    /// Checks if the timer is currently running.
    /// @param timer The timer.
    /// @return 1 if running, 0 if stopped.
    int8_t rt_timer_is_running(rt_timer timer);

    /// Checks if the timer has expired.
    /// @param timer The timer.
    /// @return 1 if expired (remaining <= 0), 0 otherwise.
    int8_t rt_timer_is_expired(rt_timer timer);

    /// Gets the number of frames elapsed since start.
    /// @param timer The timer.
    /// @return Elapsed frames.
    int64_t rt_timer_elapsed(rt_timer timer);

    /// Gets the number of frames remaining.
    /// @param timer The timer.
    /// @return Remaining frames (0 if expired).
    int64_t rt_timer_remaining(rt_timer timer);

    /// Gets the progress as a percentage (0-100).
    /// @param timer The timer.
    /// @return Progress percentage (100 = complete).
    int64_t rt_timer_progress(rt_timer timer);

    /// Gets the total duration of the timer.
    /// @param timer The timer.
    /// @return Total frames.
    int64_t rt_timer_duration(rt_timer timer);

    /// Checks if this is a repeating timer.
    /// @param timer The timer.
    /// @return 1 if repeating, 0 otherwise.
    int8_t rt_timer_is_repeating(rt_timer timer);

    /// Sets the timer duration without restarting.
    /// @param timer The timer.
    /// @param frames New duration in frames.
    void rt_timer_set_duration(rt_timer timer, int64_t frames);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_TIMER_H
