//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_animstate.h
// Purpose: Animation state machine combining StateMachine state tracking with
//          SpriteAnimation frame playback. Each state maps to an animation clip
//          (frame range + duration + loop flag). Transitions automatically
//          reconfigure the animation to play the new state's clip.
//
// Key invariants:
//   - Maximum 32 animation states per machine.
//   - State IDs must be non-negative integers.
//   - Update() must be called once per frame to advance both state and animation.
//   - JustEntered/JustExited flags latch until ClearFlags() is called.
//   - Frame-based timing (not millisecond-based) matching existing game helpers.
//
// Ownership/Lifetime:
//   - GC-managed via rt_obj_new_i64; no finalizer needed (no retained refs).
//
// Links: rt_animstate.c (implementation),
//        rt_statemachine.h (state tracking pattern),
//        rt_spriteanim.h (animation playback pattern)
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // AnimStateMachine Creation
    //=========================================================================

    /// @brief Create a new animation state machine.
    /// @return New AnimStateMachine object.
    void *rt_animstate_new(void);

    //=========================================================================
    // State/Clip Definition
    //=========================================================================

    /// @brief Register a state with its animation clip parameters.
    /// @param asm_ AnimStateMachine object.
    /// @param state_id Non-negative integer state identifier.
    /// @param start_frame First frame index of the clip.
    /// @param end_frame Last frame index of the clip (inclusive).
    /// @param frame_duration Frames to display each animation frame.
    /// @param loop 1 to loop the clip, 0 for one-shot.
    void rt_animstate_add_state(void *asm_,
                                int64_t state_id,
                                int64_t start_frame,
                                int64_t end_frame,
                                int64_t frame_duration,
                                int8_t loop);

    /// @brief Set the initial state (must have been added).
    /// @return 1 on success, 0 if state not found.
    int8_t rt_animstate_set_initial(void *asm_, int64_t state_id);

    //=========================================================================
    // Transitions & Update
    //=========================================================================

    /// @brief Transition to a new state, reconfiguring the animation clip.
    /// @return 1 on success, 0 if state not found or already in that state.
    int8_t rt_animstate_transition(void *asm_, int64_t state_id);

    /// @brief Advance one frame. Call once per game loop iteration.
    void rt_animstate_update(void *asm_);

    /// @brief Clear JustEntered / JustExited edge flags.
    void rt_animstate_clear_flags(void *asm_);

    //=========================================================================
    // Properties
    //=========================================================================

    /// @brief Current state ID.
    int64_t rt_animstate_current_state(void *asm_);

    /// @brief Previous state ID (-1 if no transition has occurred).
    int64_t rt_animstate_previous_state(void *asm_);

    /// @brief 1 if a transition occurred since last ClearFlags().
    int8_t rt_animstate_just_entered(void *asm_);

    /// @brief 1 if the previous state was exited since last ClearFlags().
    int8_t rt_animstate_just_exited(void *asm_);

    /// @brief Frames spent in the current state.
    int64_t rt_animstate_frames_in_state(void *asm_);

    /// @brief Current animation frame index.
    int64_t rt_animstate_current_frame(void *asm_);

    /// @brief 1 if the current clip is a one-shot that has finished.
    int8_t rt_animstate_is_anim_finished(void *asm_);

    /// @brief Animation progress 0-100 within the current clip.
    int64_t rt_animstate_progress(void *asm_);

#ifdef __cplusplus
}
#endif
