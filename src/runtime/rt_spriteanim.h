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
/// @details Provides animation management for sprites, tracking current frame,
/// timing, looping, and animation state for games and applications. Supports
/// play/pause/stop/resume lifecycle, looping and ping-pong playback modes,
/// configurable frame duration and speed multiplier, and frame-change
/// detection for triggering events on specific animation frames.
///
/// Key invariants: Frame indices are bounded by the range set in
///   rt_spriteanim_setup(). The maximum frame count per animation is
///   RT_SPRITEANIM_MAX_FRAMES (64). Frame duration must be >= 1. Speed
///   multiplier affects how quickly the internal timer advances.
/// Ownership/Lifetime: The caller owns the rt_spriteanim handle and must
///   free it with rt_spriteanim_destroy().
/// Links: rt_spriteanim.c (implementation), rt_tween.h (for eased animation
///   timing)
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_SPRITEANIM_H
#define VIPER_RT_SPRITEANIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum frames per animation.
#define RT_SPRITEANIM_MAX_FRAMES 64

    /// Opaque handle to a SpriteAnimation instance.
    typedef struct rt_spriteanim_impl *rt_spriteanim;

    /// @brief Allocates and initializes a new SpriteAnimation in the stopped
    ///   state.
    /// @return A new SpriteAnimation handle. The caller must free it with
    ///   rt_spriteanim_destroy().
    rt_spriteanim rt_spriteanim_new(void);

    /// @brief Destroys a SpriteAnimation and releases its memory.
    /// @param anim The animation to destroy. Passing NULL is a no-op.
    void rt_spriteanim_destroy(rt_spriteanim anim);

    /// @brief Configures the animation's frame range and timing.
    /// @param anim The animation to configure.
    /// @param start_frame Index of the first frame in the sprite sheet. Must
    ///   be >= 0.
    /// @param end_frame Index of the last frame (inclusive). Must be >=
    ///   start_frame. The total frame count (end - start + 1) must not exceed
    ///   RT_SPRITEANIM_MAX_FRAMES.
    /// @param frame_duration Number of game frames to display each animation
    ///   frame before advancing. Must be >= 1.
    void rt_spriteanim_setup(rt_spriteanim anim,
                             int64_t start_frame,
                             int64_t end_frame,
                             int64_t frame_duration);

    /// @brief Enables or disables looping playback.
    /// @param anim The animation to modify.
    /// @param loop 1 to loop the animation indefinitely when it reaches the
    ///   last frame, 0 for one-shot playback that stops at the end.
    void rt_spriteanim_set_loop(rt_spriteanim anim, int8_t loop);

    /// @brief Enables or disables ping-pong (palindrome) playback.
    ///
    /// When enabled, the animation plays forward to the last frame, then
    /// reverses back to the first frame, and repeats. Requires looping to be
    /// enabled for continuous ping-pong.
    /// @param anim The animation to modify.
    /// @param pingpong 1 to enable ping-pong mode, 0 for normal forward
    ///   playback.
    void rt_spriteanim_set_pingpong(rt_spriteanim anim, int8_t pingpong);

    /// @brief Queries whether looping is enabled for this animation.
    /// @param anim The animation to query.
    /// @return 1 if the animation loops, 0 for one-shot playback.
    int8_t rt_spriteanim_loop(rt_spriteanim anim);

    /// @brief Queries whether ping-pong mode is enabled for this animation.
    /// @param anim The animation to query.
    /// @return 1 if ping-pong is active, 0 for normal forward playback.
    int8_t rt_spriteanim_pingpong(rt_spriteanim anim);

    /// @brief Starts or restarts the animation from the first frame.
    ///
    /// Resets the internal timer and frame index to the beginning and enters
    /// the playing state.
    /// @param anim The animation to play.
    void rt_spriteanim_play(rt_spriteanim anim);

    /// @brief Stops the animation and resets it to the first frame.
    ///
    /// Unlike pause, stop resets the playback position entirely.
    /// @param anim The animation to stop.
    void rt_spriteanim_stop(rt_spriteanim anim);

    /// @brief Pauses the animation at its current frame without resetting.
    ///
    /// Call rt_spriteanim_resume() to continue from where it was paused.
    /// @param anim The animation to pause.
    void rt_spriteanim_pause(rt_spriteanim anim);

    /// @brief Resumes a previously paused animation from its current position.
    /// @param anim The animation to resume. Has no effect if not paused.
    void rt_spriteanim_resume(rt_spriteanim anim);

    /// @brief Resets the animation to the first frame without changing the
    ///   play/pause/stop state.
    /// @param anim The animation to reset.
    void rt_spriteanim_reset(rt_spriteanim anim);

    /// @brief Advances the animation by one game frame.
    ///
    /// Should be called once per game frame while the animation is playing.
    /// Handles frame advancement, looping, and ping-pong reversal.
    /// @param anim The animation to update.
    /// @return 1 if the animation just completed on this frame (relevant for
    ///   one-shot animations; always 0 for looping animations), 0 otherwise.
    int8_t rt_spriteanim_update(rt_spriteanim anim);

    /// @brief Retrieves the current sprite-sheet frame index.
    /// @param anim The animation to query.
    /// @return The frame index within [start_frame, end_frame] as configured
    ///   by rt_spriteanim_setup().
    int64_t rt_spriteanim_frame(rt_spriteanim anim);

    /// @brief Jumps to a specific frame index without affecting play state.
    /// @param anim The animation to modify.
    /// @param frame The desired frame index. Clamped to [start_frame,
    ///   end_frame].
    void rt_spriteanim_set_frame(rt_spriteanim anim, int64_t frame);

    /// @brief Retrieves the number of game frames each animation frame is
    ///   displayed.
    /// @param anim The animation to query.
    /// @return The frame duration in game frames.
    int64_t rt_spriteanim_frame_duration(rt_spriteanim anim);

    /// @brief Changes the number of game frames each animation frame is
    ///   displayed.
    /// @param anim The animation to modify.
    /// @param duration New frame duration in game frames. Must be >= 1.
    void rt_spriteanim_set_frame_duration(rt_spriteanim anim, int64_t duration);

    /// @brief Retrieves the total number of frames in the animation sequence.
    /// @param anim The animation to query.
    /// @return The frame count (end_frame - start_frame + 1).
    int64_t rt_spriteanim_frame_count(rt_spriteanim anim);

    /// @brief Queries whether the animation is currently in the playing state.
    /// @param anim The animation to query.
    /// @return 1 if actively playing (not stopped or paused), 0 otherwise.
    int8_t rt_spriteanim_is_playing(rt_spriteanim anim);

    /// @brief Queries whether the animation is currently paused.
    /// @param anim The animation to query.
    /// @return 1 if paused (can be resumed), 0 otherwise.
    int8_t rt_spriteanim_is_paused(rt_spriteanim anim);

    /// @brief Queries whether a one-shot animation has reached its final frame.
    /// @param anim The animation to query.
    /// @return 1 if the animation has finished and will not advance further
    ///   (only meaningful for non-looping animations), 0 otherwise.
    int8_t rt_spriteanim_is_finished(rt_spriteanim anim);

    /// @brief Retrieves the animation progress as an integer percentage.
    /// @param anim The animation to query.
    /// @return A value from 0 (just started) to 100 (reached last frame).
    int64_t rt_spriteanim_progress(rt_spriteanim anim);

    /// @brief Sets the playback speed multiplier.
    /// @param anim The animation to modify.
    /// @param speed Speed multiplier applied to frame advancement. 1.0 is
    ///   normal speed, 2.0 is double speed, 0.5 is half speed. Must be > 0.
    void rt_spriteanim_set_speed(rt_spriteanim anim, double speed);

    /// @brief Retrieves the current playback speed multiplier.
    /// @param anim The animation to query.
    /// @return The speed multiplier (1.0 = normal).
    double rt_spriteanim_speed(rt_spriteanim anim);

    /// @brief Queries whether the displayed frame changed on the most recent
    ///   update.
    ///
    /// Useful for triggering game events (e.g., sound effects, hitbox changes)
    /// synchronized with specific animation frames.
    /// @param anim The animation to query.
    /// @return 1 if the frame index changed during the last
    ///   rt_spriteanim_update() call, 0 if it stayed the same.
    int8_t rt_spriteanim_frame_changed(rt_spriteanim anim);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_SPRITEANIM_H
