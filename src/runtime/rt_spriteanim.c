//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_spriteanim.c
/// @brief Implementation of frame-based sprite animation controller.
///
//===----------------------------------------------------------------------===//

#include "rt_spriteanim.h"

#include <stdlib.h>

/// Internal structure for SpriteAnimation.
struct rt_spriteanim_impl
{
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

rt_spriteanim rt_spriteanim_new(void)
{
    struct rt_spriteanim_impl *anim = malloc(sizeof(struct rt_spriteanim_impl));
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

void rt_spriteanim_destroy(rt_spriteanim anim)
{
    if (anim)
        free(anim);
}

void rt_spriteanim_setup(rt_spriteanim anim,
                         int64_t start_frame,
                         int64_t end_frame,
                         int64_t frame_duration)
{
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

void rt_spriteanim_set_loop(rt_spriteanim anim, int8_t loop)
{
    if (!anim)
        return;
    anim->loop = loop ? 1 : 0;
}

void rt_spriteanim_set_pingpong(rt_spriteanim anim, int8_t pingpong)
{
    if (!anim)
        return;
    anim->pingpong = pingpong ? 1 : 0;
}

int8_t rt_spriteanim_loop(rt_spriteanim anim)
{
    return anim ? anim->loop : 0;
}

int8_t rt_spriteanim_pingpong(rt_spriteanim anim)
{
    return anim ? anim->pingpong : 0;
}

void rt_spriteanim_play(rt_spriteanim anim)
{
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

void rt_spriteanim_stop(rt_spriteanim anim)
{
    if (!anim)
        return;
    anim->playing = 0;
    anim->paused = 0;
}

void rt_spriteanim_pause(rt_spriteanim anim)
{
    if (!anim)
        return;
    if (anim->playing)
        anim->paused = 1;
}

void rt_spriteanim_resume(rt_spriteanim anim)
{
    if (!anim)
        return;
    anim->paused = 0;
}

void rt_spriteanim_reset(rt_spriteanim anim)
{
    if (!anim)
        return;
    anim->current_frame = anim->start_frame;
    anim->frame_counter = 0;
    anim->direction = 1;
    anim->finished = 0;
    anim->speed_accum = 0.0;
}

int8_t rt_spriteanim_update(rt_spriteanim anim)
{
    if (!anim)
        return 0;

    anim->frame_changed = 0;

    if (!anim->playing || anim->paused || anim->finished)
        return 0;

    // Apply speed multiplier
    anim->speed_accum += anim->speed;
    while (anim->speed_accum >= 1.0)
    {
        anim->speed_accum -= 1.0;
        anim->frame_counter++;
    }

    // Check if it's time to advance the frame
    if (anim->frame_counter >= anim->frame_duration)
    {
        anim->frame_counter = 0;
        anim->frame_changed = 1;

        // Advance frame
        anim->current_frame += anim->direction;

        // Check bounds
        if (anim->pingpong)
        {
            if (anim->direction == 1 && anim->current_frame > anim->end_frame)
            {
                anim->direction = -1;
                anim->current_frame = anim->end_frame - 1;
                if (anim->current_frame < anim->start_frame)
                    anim->current_frame = anim->start_frame;
            }
            else if (anim->direction == -1 && anim->current_frame < anim->start_frame)
            {
                if (anim->loop)
                {
                    anim->direction = 1;
                    anim->current_frame = anim->start_frame + 1;
                    if (anim->current_frame > anim->end_frame)
                        anim->current_frame = anim->start_frame;
                }
                else
                {
                    anim->current_frame = anim->start_frame;
                    anim->finished = 1;
                    anim->playing = 0;
                    return 1;
                }
            }
        }
        else
        {
            // Normal forward animation
            if (anim->current_frame > anim->end_frame)
            {
                if (anim->loop)
                {
                    anim->current_frame = anim->start_frame;
                }
                else
                {
                    anim->current_frame = anim->end_frame;
                    anim->finished = 1;
                    anim->playing = 0;
                    return 1;
                }
            }
        }
    }

    return 0;
}

int64_t rt_spriteanim_frame(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->current_frame;
}

void rt_spriteanim_set_frame(rt_spriteanim anim, int64_t frame)
{
    if (!anim)
        return;
    if (frame < anim->start_frame)
        frame = anim->start_frame;
    if (frame > anim->end_frame)
        frame = anim->end_frame;
    anim->current_frame = frame;
    anim->frame_counter = 0;
}

int64_t rt_spriteanim_frame_duration(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->frame_duration;
}

void rt_spriteanim_set_frame_duration(rt_spriteanim anim, int64_t duration)
{
    if (!anim)
        return;
    if (duration < 1)
        duration = 1;
    anim->frame_duration = duration;
}

int64_t rt_spriteanim_frame_count(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->end_frame - anim->start_frame + 1;
}

int8_t rt_spriteanim_is_playing(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->playing && !anim->paused;
}

int8_t rt_spriteanim_is_paused(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->paused;
}

int8_t rt_spriteanim_is_finished(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->finished;
}

int64_t rt_spriteanim_progress(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    int64_t total = anim->end_frame - anim->start_frame;
    if (total <= 0)
        return 100;
    int64_t current = anim->current_frame - anim->start_frame;
    return (current * 100) / total;
}

void rt_spriteanim_set_speed(rt_spriteanim anim, double speed)
{
    if (!anim)
        return;
    if (speed < 0.0)
        speed = 0.0;
    if (speed > 10.0)
        speed = 10.0;
    anim->speed = speed;
}

double rt_spriteanim_speed(rt_spriteanim anim)
{
    if (!anim)
        return 1.0;
    return anim->speed;
}

int8_t rt_spriteanim_frame_changed(rt_spriteanim anim)
{
    if (!anim)
        return 0;
    return anim->frame_changed;
}
