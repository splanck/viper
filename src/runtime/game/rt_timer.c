//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_timer.c
// Purpose: Frame-counted countdown timer for Viper games. A Timer fires after a
//   specified number of game frames and optionally repeats automatically.
//   Frame-based timing is deterministic (independent of wall-clock drift) and
//   integrates naturally with game loops that call Update() exactly once per
//   rendered frame. Typical uses: cooldowns, enemy respawns, animation delays,
//   and periodic events.
//
// Key invariants:
//   - Duration and elapsed are both integer frame counts. Duration must be > 0;
//     zero or negative durations are silently rejected by Start/StartRepeating.
//   - rt_timer_update() must be called once per frame while the timer is
//     running. It returns 1 on the frame the timer expires, 0 otherwise. For a
//     repeating timer, it fires every `duration` frames and resets elapsed to 0
//     on expiry (never stops automatically).
//   - rt_timer_is_expired() returns 1 only if the timer ran to completion and
//     is no longer running. It returns 0 for a timer that was stopped early.
//   - rt_timer_progress() returns [0, 100] as an integer percentage of elapsed
//     frames. At 0 frames elapsed it is 0; at or beyond duration it is 100.
//   - rt_timer_remaining() returns the number of frames left until expiry, or 0
//     if already expired or not started.
//
// Ownership/Lifetime:
//   - Timer objects are GC-managed (rt_obj_new_i64). rt_timer_destroy() calls
//     rt_obj_free() explicitly; the GC also reclaims them automatically.
//
// Links: src/runtime/game/rt_timer.h (public API),
//        docs/viperlib/game.md (Timer section — note: frame-based, not ms-based)
//
//===----------------------------------------------------------------------===//

#include "rt_timer.h"
#include "rt_object.h"
#include <stdlib.h>

/// Internal structure for Timer.
struct rt_timer_impl {
    int64_t duration; // Total frames (or ms in ms_mode) for the timer
    int64_t elapsed;  // Frames (or ms) elapsed since start
    int8_t running;   // 1 if timer is running
    int8_t repeating; // 1 if timer auto-restarts
    int8_t ms_mode;   // 1 if using millisecond-based timing
};

/// @brief Create a new timer (starts stopped with zero duration).
rt_timer rt_timer_new(void) {
    struct rt_timer_impl *timer =
        (struct rt_timer_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_timer_impl));
    if (!timer) {
        return NULL;
    }

    timer->duration = 0;
    timer->elapsed = 0;
    timer->running = 0;
    timer->repeating = 0;
    timer->ms_mode = 0;

    return timer;
}

/// @brief Destroy a timer and release its GC allocation.
void rt_timer_destroy(rt_timer timer) {
    if (timer && rt_obj_release_check0(timer))
        rt_obj_free(timer);
}

/// @brief Start a one-shot timer that expires after the given number of frames.
void rt_timer_start(rt_timer timer, int64_t frames) {
    if (!timer || frames <= 0)
        return;

    timer->duration = frames;
    timer->elapsed = 0;
    timer->running = 1;
    timer->repeating = 0;
}

/// @brief Start a repeating timer that auto-restarts when it expires.
void rt_timer_start_repeating(rt_timer timer, int64_t frames) {
    if (!timer || frames <= 0)
        return;

    timer->duration = frames;
    timer->elapsed = 0;
    timer->running = 1;
    timer->repeating = 1;
}

/// @brief Stop the timer (elapsed value is preserved for queries).
void rt_timer_stop(rt_timer timer) {
    if (!timer)
        return;
    timer->running = 0;
}

/// @brief Reset the elapsed counter to zero without changing running/repeating state.
void rt_timer_reset(rt_timer timer) {
    if (!timer)
        return;
    timer->elapsed = 0;
}

/// @brief Advance the timer by one tick. Returns 1 if the timer expired this tick.
int8_t rt_timer_update(rt_timer timer) {
    if (!timer || !timer->running) {
        return 0;
    }

    timer->elapsed++;

    if (timer->elapsed >= timer->duration) {
        if (timer->repeating) {
            // Wrap around for repeating timers
            timer->elapsed = 0;
        } else {
            timer->running = 0;
        }
        return 1; // Timer expired this frame
    }

    return 0;
}

/// @brief Check whether the timer is currently counting.
int8_t rt_timer_is_running(rt_timer timer) {
    return timer ? timer->running : 0;
}

/// @brief Check whether the timer has expired (stopped and elapsed >= duration).
int8_t rt_timer_is_expired(rt_timer timer) {
    if (!timer)
        return 0;
    return (!timer->running && timer->elapsed >= timer->duration) ? 1 : 0;
}

/// @brief Get the number of ticks elapsed since the timer was started.
int64_t rt_timer_elapsed(rt_timer timer) {
    return timer ? timer->elapsed : 0;
}

/// @brief Get the number of ticks remaining before the timer expires.
int64_t rt_timer_remaining(rt_timer timer) {
    if (!timer || timer->duration == 0)
        return 0;

    int64_t remaining = timer->duration - timer->elapsed;
    return (remaining > 0) ? remaining : 0;
}

/// @brief Get the timer progress as a percentage (0–100).
int64_t rt_timer_progress(rt_timer timer) {
    if (!timer || timer->duration == 0)
        return 0;

    int64_t progress = (timer->elapsed * 100) / timer->duration;
    return (progress > 100) ? 100 : progress;
}

/// @brief Get the total duration the timer was started with.
int64_t rt_timer_duration(rt_timer timer) {
    return timer ? timer->duration : 0;
}

/// @brief Check whether the timer is in repeating (auto-restart) mode.
int8_t rt_timer_is_repeating(rt_timer timer) {
    return timer ? timer->repeating : 0;
}

/// @brief Set the duration value.
/// @param timer
/// @param frames
void rt_timer_set_duration(rt_timer timer, int64_t frames) {
    if (!timer || frames <= 0)
        return;
    timer->duration = frames;
}

// =========================================================================
// Millisecond-based timer mode
// =========================================================================

void rt_timer_start_ms(rt_timer timer, int64_t duration_ms) {
    if (!timer || duration_ms <= 0)
        return;
    timer->duration = duration_ms;
    timer->elapsed = 0;
    timer->running = 1;
    timer->repeating = 0;
    timer->ms_mode = 1;
}

void rt_timer_start_repeating_ms(rt_timer timer, int64_t interval_ms) {
    if (!timer || interval_ms <= 0)
        return;
    timer->duration = interval_ms;
    timer->elapsed = 0;
    timer->running = 1;
    timer->repeating = 1;
    timer->ms_mode = 1;
}

int8_t rt_timer_update_ms(rt_timer timer, int64_t dt) {
    if (!timer || !timer->running || dt <= 0)
        return 0;

    timer->elapsed += dt;

    if (timer->elapsed >= timer->duration) {
        if (timer->repeating) {
            // Wrap around, preserving overshoot for accuracy
            timer->elapsed -= timer->duration;
            if (timer->elapsed < 0)
                timer->elapsed = 0;
        } else {
            timer->elapsed = timer->duration;
            timer->running = 0;
        }
        return 1; // Timer expired this update
    }

    return 0;
}

int64_t rt_timer_elapsed_ms(rt_timer timer) {
    return timer ? timer->elapsed : 0;
}

int64_t rt_timer_remaining_ms(rt_timer timer) {
    if (!timer || timer->duration == 0)
        return 0;
    int64_t remaining = timer->duration - timer->elapsed;
    return (remaining > 0) ? remaining : 0;
}
