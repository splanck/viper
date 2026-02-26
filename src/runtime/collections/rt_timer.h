//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_timer.h
// Purpose: Frame-based countdown timer using frame counts rather than wall-clock time, with
// expiration as a one-shot edge flag and progress as an integer percentage.
//
// Key invariants:
//   - Duration is measured in frames; must be >= 1.
//   - rt_timer_update must be called once per frame.
//   - Expiration is a one-shot edge: returns 1 only on the expiring frame.
//   - Progress is reported as an integer percentage in [0, 100].
//
// Ownership/Lifetime:
//   - Caller owns the rt_timer handle; destroy with rt_timer_destroy.
//   - No reference counting; explicit destruction is required.
//
// Links: src/runtime/collections/rt_timer.c (implementation)
//
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

    /// @brief Allocates and initializes a new Timer in the stopped state.
    /// @return A new Timer handle. The caller must free it with
    ///   rt_timer_destroy().
    rt_timer rt_timer_new(void);

    /// @brief Destroys a Timer and releases its memory.
    /// @param timer The timer to destroy. Passing NULL is a no-op.
    void rt_timer_destroy(rt_timer timer);

    /// @brief Starts a one-shot countdown timer.
    ///
    /// The timer counts down from the given duration and expires once.
    /// Calling this on a running timer restarts it with the new duration.
    /// @param timer The timer to start.
    /// @param frames Number of frames until expiration. Must be >= 1.
    void rt_timer_start(rt_timer timer, int64_t frames);

    /// @brief Starts a repeating countdown timer that auto-restarts on
    ///   expiration.
    ///
    /// Each time the timer's countdown reaches zero, it fires (update returns
    /// 1) and immediately restarts for another cycle.
    /// @param timer The timer to start.
    /// @param frames Number of frames per cycle. Must be >= 1.
    void rt_timer_start_repeating(rt_timer timer, int64_t frames);

    /// @brief Stops the timer and resets its elapsed time.
    /// @param timer The timer to stop. Subsequent calls to rt_timer_update()
    ///   will return 0 until the timer is started again.
    void rt_timer_stop(rt_timer timer);

    /// @brief Resets the timer's elapsed count to zero without changing its
    ///   running/stopped state or duration.
    ///
    /// If the timer is running, it effectively restarts the current countdown
    /// from the beginning.
    /// @param timer The timer to reset.
    void rt_timer_reset(rt_timer timer);

    /// @brief Advances the timer by one frame and checks for expiration.
    ///
    /// Must be called exactly once per game frame while the timer is running.
    /// For repeating timers, automatically restarts the countdown on each
    /// expiration.
    /// @param timer The timer to update.
    /// @return 1 if the timer expired on this frame, 0 otherwise. For
    ///   repeating timers, returns 1 on each cycle completion.
    int8_t rt_timer_update(rt_timer timer);

    /// @brief Queries whether the timer is currently counting down.
    /// @param timer The timer to query.
    /// @return 1 if the timer is running (started and not yet stopped),
    ///   0 if stopped.
    int8_t rt_timer_is_running(rt_timer timer);

    /// @brief Queries whether the timer has reached its expiration point.
    /// @param timer The timer to query.
    /// @return 1 if the timer has expired (elapsed >= duration), 0 otherwise.
    ///   For repeating timers, this may only be briefly true before the timer
    ///   auto-restarts.
    int8_t rt_timer_is_expired(rt_timer timer);

    /// @brief Retrieves the number of frames elapsed since the timer was
    ///   started or last reset.
    /// @param timer The timer to query.
    /// @return Elapsed frames, in [0, duration]. Returns 0 if not started.
    int64_t rt_timer_elapsed(rt_timer timer);

    /// @brief Retrieves the number of frames remaining before expiration.
    /// @param timer The timer to query.
    /// @return Remaining frames. Returns 0 if the timer has already expired
    ///   or has not been started.
    int64_t rt_timer_remaining(rt_timer timer);

    /// @brief Retrieves the timer's progress as an integer percentage.
    /// @param timer The timer to query.
    /// @return A value from 0 (just started) to 100 (fully elapsed / expired).
    int64_t rt_timer_progress(rt_timer timer);

    /// @brief Retrieves the total duration the timer was configured with.
    /// @param timer The timer to query.
    /// @return Total countdown duration in frames.
    int64_t rt_timer_duration(rt_timer timer);

    /// @brief Queries whether the timer is set to repeat automatically.
    /// @param timer The timer to query.
    /// @return 1 if the timer was started with rt_timer_start_repeating(),
    ///   0 if it is a one-shot timer.
    int8_t rt_timer_is_repeating(rt_timer timer);

    /// @brief Changes the timer's duration without restarting or stopping it.
    ///
    /// The new duration takes effect on the current or next countdown cycle.
    /// If the elapsed time already exceeds the new duration, the timer will
    /// expire on the next update.
    /// @param timer The timer to modify.
    /// @param frames New duration in frames. Must be >= 1.
    void rt_timer_set_duration(rt_timer timer, int64_t frames);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_TIMER_H
