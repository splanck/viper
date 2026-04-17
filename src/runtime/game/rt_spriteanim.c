//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_spriteanim.c
// Purpose: Frame-index animation controller for Viper sprite sheets. Advances
//   an integer frame index through a configurable sequence of frames at a
//   specified frames-per-second rate relative to the game's frame rate.
//   Supports looping, ping-pong (forward then reverse), one-shot (stops at
//   last frame), and manual frame control. The controller does not draw
//   anything — it only computes which frame to display each update, leaving
//   rendering to the sprite/spritebatch layer.
//
// Key invariants:
//   - Frames are identified by non-negative integers (indices into a sprite
//     sheet row). The range [start_frame, end_frame] is inclusive. There is no
//     compile-time cap on frame count — any integer range is valid.
//   - Animation speed is expressed as `fps` (frames of animation per second).
//     The controller accumulates fractional frame advances each Update() call
//     based on `fps / game_fps`. Callers pass `game_fps` (e.g. 60) to Update.
//   - Loop mode: wraps from end_frame back to start_frame automatically.
//   - PingPong mode: plays start→end, then end→start, alternating direction.
//   - OneShot mode: stops at end_frame; is_complete() returns 1 thereafter.
//   - Calling Reset() returns to start_frame and clears the complete flag.
//   - The current frame is always in [start_frame, end_frame].
//
// Ownership/Lifetime:
//   - SpriteAnim objects are GC-managed (rt_obj_new_i64). They hold no external
//     resources and require no finalizer beyond the GC reclaiming the struct.
//
// Links: src/runtime/game/rt_spriteanim.h (public API),
//        src/runtime/graphics/rt_spritebatch.h (rendering),
//        docs/viperlib/game.md (SpriteAnim section)
//
//===----------------------------------------------------------------------===//

#include "rt_spriteanim.h"
#include "rt_object.h"

#include <stdlib.h>

/// Internal structure for SpriteAnimation.
struct rt_spriteanim_impl {
    int64_t start_frame;    ///< First frame index.
    int64_t end_frame;      ///< Last frame index (inclusive).
    int64_t current_frame;  ///< Current frame index.
    int64_t frame_duration; ///< Frames to display each animation frame.
    int64_t frame_counter;  ///< Counter for frame timing.

    double speed;       ///< Playback speed multiplier.
    double speed_accum; ///< Accumulator for fractional speed.

    int8_t playing;       ///< 1 if animation is playing.
    int8_t paused;        ///< 1 if animation is paused.
    int8_t loop;          ///< 1 if animation loops.
    int8_t pingpong;      ///< 1 if animation ping-pongs.
    int8_t finished;      ///< 1 if one-shot animation completed.
    int8_t direction;     ///< 1 = forward, -1 = backward (for pingpong).
    int8_t frame_changed; ///< 1 if frame changed this update.
};

static int8_t rt_spriteanim_advance_one_frame(rt_spriteanim anim) {
    anim->current_frame += anim->direction;

    if (anim->pingpong) {
        if (anim->direction == 1 && anim->current_frame > anim->end_frame) {
            anim->direction = -1;
            anim->current_frame = anim->end_frame - 1;
            if (anim->current_frame < anim->start_frame) {
                if (!anim->loop) {
                    anim->current_frame = anim->start_frame;
                    anim->finished = 1;
                    anim->playing = 0;
                    return 1;
                }
                anim->current_frame = anim->start_frame;
            }
        } else if (anim->direction == -1 && anim->current_frame < anim->start_frame) {
            if (anim->loop) {
                anim->direction = 1;
                anim->current_frame = anim->start_frame + 1;
                if (anim->current_frame > anim->end_frame)
                    anim->current_frame = anim->start_frame;
            } else {
                anim->current_frame = anim->start_frame;
                anim->finished = 1;
                anim->playing = 0;
                return 1;
            }
        }
    } else if (anim->current_frame > anim->end_frame) {
        if (anim->loop) {
            anim->current_frame = anim->start_frame;
        } else {
            anim->current_frame = anim->end_frame;
            anim->finished = 1;
            anim->playing = 0;
            return 1;
        }
    }

    return 0;
}

/// @brief Create a new spriteanim object.
rt_spriteanim rt_spriteanim_new(void) {
    struct rt_spriteanim_impl *anim =
        (struct rt_spriteanim_impl *)rt_obj_new_i64(0, (int64_t)sizeof(struct rt_spriteanim_impl));
    if (!anim)
        return NULL;

    anim->start_frame = 0;
    anim->end_frame = 0;
    anim->current_frame = 0;
    anim->frame_duration = 6; // Default: 10fps at 60fps game
    anim->frame_counter = 0;

    anim->speed = 1.0;
    anim->speed_accum = 0.0;

    anim->playing = 0;
    anim->paused = 0;
    anim->loop = 1; // Default to looping
    anim->pingpong = 0;
    anim->finished = 0;
    anim->direction = 1;
    anim->frame_changed = 0;

    return anim;
}

/// @brief Release resources and destroy the spriteanim.
void rt_spriteanim_destroy(rt_spriteanim anim) {
    (void)anim;
}

void rt_spriteanim_setup(rt_spriteanim anim,
                         int64_t start_frame,
                         int64_t end_frame,
                         int64_t frame_duration) {
    if (!anim)
        return;

    if (start_frame < 0)
        start_frame = 0;
    if (end_frame < start_frame)
        end_frame = start_frame;
    if (frame_duration < 1)
        frame_duration = 1;

    anim->start_frame = start_frame;
    anim->end_frame = end_frame;
    anim->frame_duration = frame_duration;
    anim->current_frame = start_frame;
    anim->frame_counter = 0;
    anim->direction = 1;
    anim->finished = 0;
}

/// @brief Enable or disable looping; when enabled, the animation restarts after the last frame.
void rt_spriteanim_set_loop(rt_spriteanim anim, int8_t loop) {
    if (!anim)
        return;
    anim->loop = loop ? 1 : 0;
}

/// @brief Enable or disable ping-pong mode (forward then reverse, then forward again).
void rt_spriteanim_set_pingpong(rt_spriteanim anim, int8_t pingpong) {
    if (!anim)
        return;
    anim->pingpong = pingpong ? 1 : 0;
}

/// @brief Return whether looping is enabled for this animation.
int8_t rt_spriteanim_loop(rt_spriteanim anim) {
    return anim ? anim->loop : 0;
}

/// @brief Return whether ping-pong mode is enabled for this animation.
int8_t rt_spriteanim_pingpong(rt_spriteanim anim) {
    return anim ? anim->pingpong : 0;
}

/// @brief Start playback from the first frame, resetting all internal counters.
void rt_spriteanim_play(rt_spriteanim anim) {
    if (!anim)
        return;
    anim->current_frame = anim->start_frame;
    anim->frame_counter = 0;
    anim->playing = 1;
    anim->paused = 0;
    anim->finished = 0;
    anim->direction = 1;
    anim->speed_accum = 0.0;
}

/// @brief Stop playback entirely (not paused — position is not preserved for resume).
void rt_spriteanim_stop(rt_spriteanim anim) {
    if (!anim)
        return;
    anim->current_frame = anim->start_frame;
    anim->frame_counter = 0;
    anim->playing = 0;
    anim->paused = 0;
    anim->finished = 0;
    anim->direction = 1;
    anim->speed_accum = 0.0;
    anim->frame_changed = 0;
}

/// @brief Pause a playing animation so it can be resumed from the current frame.
void rt_spriteanim_pause(rt_spriteanim anim) {
    if (!anim)
        return;
    if (anim->playing)
        anim->paused = 1;
}

/// @brief Resume a paused animation from the frame where it was paused.
void rt_spriteanim_resume(rt_spriteanim anim) {
    if (!anim)
        return;
    anim->paused = 0;
}

/// @brief Reset the animation to its first frame without changing play/pause state.
void rt_spriteanim_reset(rt_spriteanim anim) {
    if (!anim)
        return;
    anim->current_frame = anim->start_frame;
    anim->frame_counter = 0;
    anim->direction = 1;
    anim->finished = 0;
    anim->speed_accum = 0.0;
}

/// @brief Update the spriteanim state (called per frame/tick).
int8_t rt_spriteanim_update(rt_spriteanim anim) {
    if (!anim)
        return 0;

    anim->frame_changed = 0;

    if (!anim->playing || anim->paused || anim->finished)
        return 0;

    // Apply speed multiplier
    anim->speed_accum += anim->speed;
    while (anim->speed_accum >= 1.0) {
        anim->speed_accum -= 1.0;
        anim->frame_counter++;
    }

    while (anim->frame_counter >= anim->frame_duration && !anim->finished) {
        anim->frame_counter -= anim->frame_duration;
        anim->frame_changed = 1;
        if (rt_spriteanim_advance_one_frame(anim))
            return 1;
    }

    return 0;
}

/// @brief Return the current frame index within the sprite sheet.
int64_t rt_spriteanim_frame(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->current_frame;
}

/// @brief Jump to a specific frame, clamped to [start_frame, end_frame].
void rt_spriteanim_set_frame(rt_spriteanim anim, int64_t frame) {
    if (!anim)
        return;
    if (frame < anim->start_frame)
        frame = anim->start_frame;
    if (frame > anim->end_frame)
        frame = anim->end_frame;
    anim->current_frame = frame;
    anim->frame_counter = 0;
}

/// @brief Return how many update ticks each frame is displayed before advancing.
int64_t rt_spriteanim_frame_duration(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->frame_duration;
}

/// @brief Set how many update ticks each frame is displayed (minimum 1).
void rt_spriteanim_set_frame_duration(rt_spriteanim anim, int64_t duration) {
    if (!anim)
        return;
    if (duration < 1)
        duration = 1;
    anim->frame_duration = duration;
}

/// @brief Return the count of elements in the spriteanim.
int64_t rt_spriteanim_frame_count(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->end_frame - anim->start_frame + 1;
}

/// @brief Check whether the animation is currently playing (not paused or stopped).
int8_t rt_spriteanim_is_playing(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->playing && !anim->paused;
}

/// @brief Check whether the animation is paused (can be resumed).
int8_t rt_spriteanim_is_paused(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->paused;
}

/// @brief Check whether a non-looping animation has reached its last frame.
int8_t rt_spriteanim_is_finished(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->finished;
}

/// @brief Return the animation progress as a percentage (0-100).
int64_t rt_spriteanim_progress(rt_spriteanim anim) {
    if (!anim)
        return 0;
    int64_t total = anim->end_frame - anim->start_frame;
    if (total <= 0)
        return 100;
    int64_t current = anim->current_frame - anim->start_frame;
    return (current * 100) / total;
}

/// @brief Set the playback speed multiplier, clamped to [0.0, 10.0] (1.0 = normal).
void rt_spriteanim_set_speed(rt_spriteanim anim, double speed) {
    if (!anim)
        return;
    if (speed < 0.0)
        speed = 0.0;
    if (speed > 10.0)
        speed = 10.0;
    anim->speed = speed;
}

/// @brief Return the current playback speed multiplier.
double rt_spriteanim_speed(rt_spriteanim anim) {
    if (!anim)
        return 1.0;
    return anim->speed;
}

/// @brief Check whether the current frame changed during the last update call.
int8_t rt_spriteanim_frame_changed(rt_spriteanim anim) {
    if (!anim)
        return 0;
    return anim->frame_changed;
}
