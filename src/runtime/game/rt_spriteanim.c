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
#include "rt_trap.h"

#include <limits.h>
#include <math.h>
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

static rt_spriteanim checked_spriteanim(rt_spriteanim anim, const char *api) {
    if (!anim)
        return NULL;
    if (rt_obj_class_id(anim) != RT_SPRITEANIM_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return anim;
}

static int64_t spriteanim_percent_i64(int64_t value, int64_t total) {
    if (value <= 0 || total <= 0)
        return 0;
    long double scaled = ((long double)value * 100.0L) / (long double)total;
    int64_t pct = scaled >= (long double)INT64_MAX ? INT64_MAX : (int64_t)scaled;
    return pct > 100 ? 100 : pct;
}

static int8_t rt_spriteanim_advance_one_frame(rt_spriteanim anim) {
    int8_t crossed_end = 0;
    int8_t crossed_start = 0;

    if (anim->direction > 0) {
        if (anim->current_frame >= anim->end_frame)
            crossed_end = 1;
        else
            anim->current_frame++;
    } else {
        if (anim->current_frame <= anim->start_frame)
            crossed_start = 1;
        else
            anim->current_frame--;
    }

    if (anim->pingpong) {
        if (anim->direction == 1 && crossed_end) {
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
        } else if (anim->direction == -1 && crossed_start) {
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
    } else if (crossed_end) {
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
    struct rt_spriteanim_impl *anim = (struct rt_spriteanim_impl *)rt_obj_new_i64(
        RT_SPRITEANIM_CLASS_ID, (int64_t)sizeof(struct rt_spriteanim_impl));
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
    anim = checked_spriteanim(anim, "SpriteAnimation.Destroy: expected Viper.Game.SpriteAnimation");
    if (anim && rt_obj_release_check0(anim))
        rt_obj_free(anim);
}

void rt_spriteanim_setup(rt_spriteanim anim,
                         int64_t start_frame,
                         int64_t end_frame,
                         int64_t frame_duration) {
    anim = checked_spriteanim(anim, "SpriteAnimation.Setup: expected Viper.Game.SpriteAnimation");
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
    anim = checked_spriteanim(anim, "SpriteAnimation.SetLoop: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return;
    anim->loop = loop ? 1 : 0;
}

/// @brief Enable or disable ping-pong mode (forward then reverse, then forward again).
void rt_spriteanim_set_pingpong(rt_spriteanim anim, int8_t pingpong) {
    anim = checked_spriteanim(anim,
                              "SpriteAnimation.SetPingPong: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return;
    anim->pingpong = pingpong ? 1 : 0;
}

/// @brief Return whether looping is enabled for this animation.
int8_t rt_spriteanim_loop(rt_spriteanim anim) {
    anim = checked_spriteanim(anim, "SpriteAnimation.Loop: expected Viper.Game.SpriteAnimation");
    return anim ? anim->loop : 0;
}

/// @brief Return whether ping-pong mode is enabled for this animation.
int8_t rt_spriteanim_pingpong(rt_spriteanim anim) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.PingPong: expected Viper.Game.SpriteAnimation");
    return anim ? anim->pingpong : 0;
}

/// @brief Start playback from the first frame, resetting all internal counters.
void rt_spriteanim_play(rt_spriteanim anim) {
    anim = checked_spriteanim(anim, "SpriteAnimation.Play: expected Viper.Game.SpriteAnimation");
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
    anim = checked_spriteanim(anim, "SpriteAnimation.Stop: expected Viper.Game.SpriteAnimation");
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
    anim = checked_spriteanim(anim, "SpriteAnimation.Pause: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return;
    if (anim->playing)
        anim->paused = 1;
}

/// @brief Resume a paused animation from the frame where it was paused.
void rt_spriteanim_resume(rt_spriteanim anim) {
    anim = checked_spriteanim(anim, "SpriteAnimation.Resume: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return;
    anim->paused = 0;
}

/// @brief Reset the animation to its first frame without changing play/pause state.
void rt_spriteanim_reset(rt_spriteanim anim) {
    anim = checked_spriteanim(anim, "SpriteAnimation.Reset: expected Viper.Game.SpriteAnimation");
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
    anim = checked_spriteanim(anim, "SpriteAnimation.Update: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;

    anim->frame_changed = 0;

    if (!anim->playing || anim->paused || anim->finished)
        return 0;

    // Apply speed multiplier
    if (!isfinite(anim->speed) || anim->speed < 0.0)
        anim->speed = 0.0;
    if (!isfinite(anim->speed_accum))
        anim->speed_accum = 0.0;
    anim->speed_accum += anim->speed;
    if (!isfinite(anim->speed_accum))
        anim->speed_accum = 0.0;
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
    anim = checked_spriteanim(anim, "SpriteAnimation.Frame: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    return anim->current_frame;
}

/// @brief Jump to a specific frame, clamped to [start_frame, end_frame].
void rt_spriteanim_set_frame(rt_spriteanim anim, int64_t frame) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.SetFrame: expected Viper.Game.SpriteAnimation");
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
    anim = checked_spriteanim(anim,
                              "SpriteAnimation.FrameDuration: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    return anim->frame_duration;
}

/// @brief Set how many update ticks each frame is displayed (minimum 1).
void rt_spriteanim_set_frame_duration(rt_spriteanim anim, int64_t duration) {
    anim = checked_spriteanim(
        anim, "SpriteAnimation.SetFrameDuration: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return;
    if (duration < 1)
        duration = 1;
    anim->frame_duration = duration;
}

/// @brief Return the count of elements in the spriteanim.
int64_t rt_spriteanim_frame_count(rt_spriteanim anim) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.FrameCount: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    int64_t diff = anim->end_frame - anim->start_frame;
    if (diff == INT64_MAX)
        return INT64_MAX;
    return diff + 1;
}

/// @brief Check whether the animation is currently playing (not paused or stopped).
int8_t rt_spriteanim_is_playing(rt_spriteanim anim) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.IsPlaying: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    return anim->playing && !anim->paused;
}

/// @brief Check whether the animation is paused (can be resumed).
int8_t rt_spriteanim_is_paused(rt_spriteanim anim) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.IsPaused: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    return anim->paused;
}

/// @brief Check whether a non-looping animation has reached its last frame.
int8_t rt_spriteanim_is_finished(rt_spriteanim anim) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.IsFinished: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    return anim->finished;
}

/// @brief Return the animation progress as a percentage (0-100).
int64_t rt_spriteanim_progress(rt_spriteanim anim) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.Progress: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    int64_t total = anim->end_frame - anim->start_frame;
    if (total <= 0)
        return 100;
    int64_t current = anim->current_frame - anim->start_frame;
    return spriteanim_percent_i64(current, total);
}

/// @brief Set the playback speed multiplier, clamped to [0.0, 10.0] (1.0 = normal).
void rt_spriteanim_set_speed(rt_spriteanim anim, double speed) {
    anim =
        checked_spriteanim(anim, "SpriteAnimation.SetSpeed: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return;
    if (!isfinite(speed) || speed < 0.0)
        speed = 0.0;
    if (speed > 10.0)
        speed = 10.0;
    anim->speed = speed;
}

/// @brief Return the current playback speed multiplier.
double rt_spriteanim_speed(rt_spriteanim anim) {
    anim = checked_spriteanim(anim, "SpriteAnimation.Speed: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 1.0;
    return anim->speed;
}

/// @brief Check whether the current frame changed during the last update call.
int8_t rt_spriteanim_frame_changed(rt_spriteanim anim) {
    anim = checked_spriteanim(anim,
                              "SpriteAnimation.FrameChanged: expected Viper.Game.SpriteAnimation");
    if (!anim)
        return 0;
    return anim->frame_changed;
}
