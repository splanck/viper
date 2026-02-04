//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_spriteanim.h
/// @brief Frame-based sprite animation controller.
///
/// Provides animation management for sprites, tracking current frame,
/// timing, looping, and animation state for games and applications.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_SPRITEANIM_H
#define VIPER_RT_SPRITEANIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum frames per animation.
#define RT_SPRITEANIM_MAX_FRAMES 64

/// Opaque handle to a SpriteAnimation instance.
typedef struct rt_spriteanim_impl *rt_spriteanim;

/// Creates a new SpriteAnimation.
/// @return A new SpriteAnimation instance.
rt_spriteanim rt_spriteanim_new(void);

/// Destroys a SpriteAnimation and frees its memory.
/// @param anim The animation to destroy.
void rt_spriteanim_destroy(rt_spriteanim anim);

/// Sets up the animation frames.
/// @param anim The animation.
/// @param start_frame First frame index.
/// @param end_frame Last frame index (inclusive).
/// @param frame_duration Frames to display each animation frame.
void rt_spriteanim_setup(rt_spriteanim anim, int64_t start_frame, int64_t end_frame, int64_t frame_duration);

/// Sets whether the animation loops.
/// @param anim The animation.
/// @param loop 1 to loop, 0 for one-shot.
void rt_spriteanim_set_loop(rt_spriteanim anim, int8_t loop);

/// Sets whether the animation ping-pongs (plays forward then backward).
/// @param anim The animation.
/// @param pingpong 1 to enable ping-pong, 0 for normal loop.
void rt_spriteanim_set_pingpong(rt_spriteanim anim, int8_t pingpong);

/// Gets whether the animation loops.
/// @param anim The animation.
/// @return 1 if looping, 0 for one-shot.
int8_t rt_spriteanim_loop(rt_spriteanim anim);

/// Gets whether the animation ping-pongs.
/// @param anim The animation.
/// @return 1 if ping-pong enabled, 0 for normal.
int8_t rt_spriteanim_pingpong(rt_spriteanim anim);

/// Starts or restarts the animation from the beginning.
/// @param anim The animation.
void rt_spriteanim_play(rt_spriteanim anim);

/// Stops the animation at the current frame.
/// @param anim The animation.
void rt_spriteanim_stop(rt_spriteanim anim);

/// Pauses the animation (can be resumed).
/// @param anim The animation.
void rt_spriteanim_pause(rt_spriteanim anim);

/// Resumes a paused animation.
/// @param anim The animation.
void rt_spriteanim_resume(rt_spriteanim anim);

/// Resets to the first frame without changing play state.
/// @param anim The animation.
void rt_spriteanim_reset(rt_spriteanim anim);

/// Updates the animation by one frame.
/// @param anim The animation.
/// @return 1 if the animation just completed (for one-shot), 0 otherwise.
int8_t rt_spriteanim_update(rt_spriteanim anim);

/// Gets the current frame index.
/// @param anim The animation.
/// @return Current frame index.
int64_t rt_spriteanim_frame(rt_spriteanim anim);

/// Sets the current frame directly.
/// @param anim The animation.
/// @param frame Frame index (clamped to valid range).
void rt_spriteanim_set_frame(rt_spriteanim anim, int64_t frame);

/// Gets the frame duration.
/// @param anim The animation.
/// @return Frames per animation frame.
int64_t rt_spriteanim_frame_duration(rt_spriteanim anim);

/// Sets the frame duration.
/// @param anim The animation.
/// @param duration Frames per animation frame.
void rt_spriteanim_set_frame_duration(rt_spriteanim anim, int64_t duration);

/// Gets the total number of frames in the animation.
/// @param anim The animation.
/// @return Number of frames.
int64_t rt_spriteanim_frame_count(rt_spriteanim anim);

/// Checks if the animation is currently playing.
/// @param anim The animation.
/// @return 1 if playing, 0 if stopped or paused.
int8_t rt_spriteanim_is_playing(rt_spriteanim anim);

/// Checks if the animation is paused.
/// @param anim The animation.
/// @return 1 if paused, 0 otherwise.
int8_t rt_spriteanim_is_paused(rt_spriteanim anim);

/// Checks if a one-shot animation has finished.
/// @param anim The animation.
/// @return 1 if finished, 0 otherwise.
int8_t rt_spriteanim_is_finished(rt_spriteanim anim);

/// Gets the progress as a percentage (0-100).
/// @param anim The animation.
/// @return Progress percentage.
int64_t rt_spriteanim_progress(rt_spriteanim anim);

/// Sets the playback speed multiplier.
/// @param anim The animation.
/// @param speed Speed multiplier (1.0 = normal, 2.0 = double speed, 0.5 = half speed).
void rt_spriteanim_set_speed(rt_spriteanim anim, double speed);

/// Gets the playback speed multiplier.
/// @param anim The animation.
/// @return Speed multiplier.
double rt_spriteanim_speed(rt_spriteanim anim);

/// Checks if the frame just changed this update.
/// @param anim The animation.
/// @return 1 if frame changed, 0 otherwise.
int8_t rt_spriteanim_frame_changed(rt_spriteanim anim);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SPRITEANIM_H
