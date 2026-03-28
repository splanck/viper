//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_animstate.c
// Purpose: Animation state machine that combines state tracking with frame-based
//          animation playback. Each registered state maps to a clip (frame range,
//          duration, loop flag). On transition the internal animation is
//          reconfigured and reset automatically.
//
// Key invariants:
//   - Clip table is a fixed-size array (max 32 states). States are looked up by
//     linear scan; the table is small enough that this is faster than hashing.
//   - Animation frame counter and state frame counter are both advanced by
//     Update(). The state counter increments unconditionally; the animation
//     counter follows the SpriteAnimation frame-duration convention.
//   - Edge flags (just_entered, just_exited) latch until ClearFlags().
//
// Ownership/Lifetime:
//   - GC-managed (rt_obj_new_i64). No retained references — no finalizer.
//
// Links: rt_animstate.h (public API)
//
//===----------------------------------------------------------------------===//

#include "rt_animstate.h"
#include "rt_object.h"

#include <string.h>

extern void rt_trap(const char *msg);

//=============================================================================
// Internal Types
//=============================================================================

#define ANIMSTATE_MAX_CLIPS 32

typedef struct {
    int64_t state_id;
    int64_t start_frame;
    int64_t end_frame;
    int64_t frame_duration; // frames per animation frame
    int8_t loop;
    int8_t valid;
} anim_clip_t;

typedef struct {
    anim_clip_t clips[ANIMSTATE_MAX_CLIPS];
    int32_t clip_count;

    // State tracking (mirrors StateMachine)
    int64_t current_state;
    int64_t previous_state;
    int8_t just_entered;
    int8_t just_exited;
    int64_t frames_in_state;

    // Animation playback (mirrors SpriteAnimation)
    int64_t current_frame;
    int64_t frame_counter; // counts up to frame_duration
    int8_t anim_finished;
    int8_t playing;

    // Current clip cache
    int32_t active_clip_idx; // -1 if none
} animstate_impl;

//=============================================================================
// Helpers
//=============================================================================

static animstate_impl *get(void *asm_) {
    return (animstate_impl *)asm_;
}

static int find_clip(animstate_impl *a, int64_t state_id) {
    for (int i = 0; i < a->clip_count; i++) {
        if (a->clips[i].valid && a->clips[i].state_id == state_id)
            return i;
    }
    return -1;
}

static void apply_clip(animstate_impl *a, int clip_idx) {
    a->active_clip_idx = clip_idx;
    if (clip_idx < 0) {
        a->playing = 0;
        return;
    }
    anim_clip_t *c = &a->clips[clip_idx];
    a->current_frame = c->start_frame;
    a->frame_counter = 0;
    a->anim_finished = 0;
    a->playing = 1;
}

//=============================================================================
// Creation
//=============================================================================

void *rt_animstate_new(void) {
    animstate_impl *a = (animstate_impl *)rt_obj_new_i64(0, (int64_t)sizeof(animstate_impl));
    if (!a)
        return NULL;
    memset(a, 0, sizeof(animstate_impl));
    a->current_state = -1;
    a->previous_state = -1;
    a->active_clip_idx = -1;
    return a;
}

//=============================================================================
// State/Clip Definition
//=============================================================================

void rt_animstate_add_state(void *asm_,
                            int64_t state_id,
                            int64_t start_frame,
                            int64_t end_frame,
                            int64_t frame_duration,
                            int8_t loop) {
    if (!asm_)
        return;
    animstate_impl *a = get(asm_);

    // Overwrite existing
    int idx = find_clip(a, state_id);
    if (idx < 0) {
        if (a->clip_count >= ANIMSTATE_MAX_CLIPS) {
            rt_trap("AnimStateMachine.AddState: limit exceeded (32)");
            return;
        }
        idx = a->clip_count++;
    }

    anim_clip_t *c = &a->clips[idx];
    c->state_id = state_id;
    c->start_frame = start_frame;
    c->end_frame = end_frame;
    c->frame_duration = frame_duration > 0 ? frame_duration : 1;
    c->loop = loop;
    c->valid = 1;
}

/// @brief Perform animstate set initial operation.
/// @param asm_
/// @param state_id
/// @return Result value.
int8_t rt_animstate_set_initial(void *asm_, int64_t state_id) {
    if (!asm_)
        return 0;
    animstate_impl *a = get(asm_);
    int idx = find_clip(a, state_id);
    if (idx < 0)
        return 0;

    a->current_state = state_id;
    a->previous_state = -1;
    a->just_entered = 1;
    a->just_exited = 0;
    a->frames_in_state = 0;
    apply_clip(a, idx);
    return 1;
}

//=============================================================================
// Transitions & Update
//=============================================================================

/// @brief Perform animstate transition operation.
/// @param asm_
/// @param state_id
/// @return Result value.
int8_t rt_animstate_transition(void *asm_, int64_t state_id) {
    if (!asm_)
        return 0;
    animstate_impl *a = get(asm_);

    // No-op if already in this state
    if (a->current_state == state_id)
        return 0;

    int idx = find_clip(a, state_id);
    if (idx < 0)
        return 0;

    a->previous_state = a->current_state;
    a->current_state = state_id;
    a->just_entered = 1;
    a->just_exited = 1;
    a->frames_in_state = 0;
    apply_clip(a, idx);
    return 1;
}

/// @brief Perform animstate update operation.
/// @param asm_
void rt_animstate_update(void *asm_) {
    if (!asm_)
        return;
    animstate_impl *a = get(asm_);

    // Advance state frame counter
    a->frames_in_state++;

    // Advance animation
    if (!a->playing || a->anim_finished || a->active_clip_idx < 0)
        return;

    anim_clip_t *c = &a->clips[a->active_clip_idx];
    a->frame_counter++;
    if (a->frame_counter >= c->frame_duration) {
        a->frame_counter = 0;

        if (c->start_frame <= c->end_frame) {
            // Forward clip
            if (a->current_frame < c->end_frame) {
                a->current_frame++;
            } else if (c->loop) {
                a->current_frame = c->start_frame;
            } else {
                a->anim_finished = 1;
            }
        } else {
            // Reverse clip (start > end)
            if (a->current_frame > c->end_frame) {
                a->current_frame--;
            } else if (c->loop) {
                a->current_frame = c->start_frame;
            } else {
                a->anim_finished = 1;
            }
        }
    }
}

/// @brief Perform animstate clear flags operation.
/// @param asm_
void rt_animstate_clear_flags(void *asm_) {
    if (!asm_)
        return;
    animstate_impl *a = get(asm_);
    a->just_entered = 0;
    a->just_exited = 0;
}

//=============================================================================
// Properties
//=============================================================================

/// @brief Perform animstate current state operation.
/// @param asm_
/// @return Result value.
int64_t rt_animstate_current_state(void *asm_) {
    return asm_ ? get(asm_)->current_state : -1;
}

/// @brief Perform animstate previous state operation.
/// @param asm_
/// @return Result value.
int64_t rt_animstate_previous_state(void *asm_) {
    return asm_ ? get(asm_)->previous_state : -1;
}

/// @brief Perform animstate just entered operation.
/// @param asm_
/// @return Result value.
int8_t rt_animstate_just_entered(void *asm_) {
    return asm_ ? get(asm_)->just_entered : 0;
}

/// @brief Perform animstate just exited operation.
/// @param asm_
/// @return Result value.
int8_t rt_animstate_just_exited(void *asm_) {
    return asm_ ? get(asm_)->just_exited : 0;
}

/// @brief Perform animstate frames in state operation.
/// @param asm_
/// @return Result value.
int64_t rt_animstate_frames_in_state(void *asm_) {
    return asm_ ? get(asm_)->frames_in_state : 0;
}

/// @brief Perform animstate current frame operation.
/// @param asm_
/// @return Result value.
int64_t rt_animstate_current_frame(void *asm_) {
    return asm_ ? get(asm_)->current_frame : 0;
}

/// @brief Perform animstate is anim finished operation.
/// @param asm_
/// @return Result value.
int8_t rt_animstate_is_anim_finished(void *asm_) {
    return asm_ ? get(asm_)->anim_finished : 0;
}

/// @brief Perform animstate progress operation.
/// @param asm_
/// @return Result value.
int64_t rt_animstate_progress(void *asm_) {
    if (!asm_)
        return 0;
    animstate_impl *a = get(asm_);
    if (a->active_clip_idx < 0)
        return 0;

    anim_clip_t *c = &a->clips[a->active_clip_idx];
    int64_t total = c->end_frame - c->start_frame;
    if (total == 0)
        return 100;
    int64_t elapsed = a->current_frame - c->start_frame;
    if (total < 0) {
        total = -total;
        elapsed = -elapsed;
    }
    return (elapsed * 100) / total;
}
