//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_statemachine.c
// Purpose: Finite state machine implementation for Viper game and application
//   state management. States are integers registered before use and the machine
//   tracks current/previous state, enter/exit edge flags, and a per-state frame
//   counter. Designed for NPC AI (idle/patrol/attack), menus, and any other
//   logic that follows a discrete set of named modes.
//
// Key invariants:
//   - State IDs are non-negative integers in [0, RT_STATE_MAX-1]. The states
//     array is a flat bitset of 256 bytes (1 byte per ID), so registration and
//     lookup are O(1) with no allocations.
//   - A state must be registered with rt_statemachine_add_state() before it
//     can be used as a transition target or initial state. Registering the same
//     ID twice is a no-op (returns 0).
//   - just_entered and just_exited are edge flags: they are set to 1 on the
//     frame a transition occurs and remain 1 until rt_statemachine_clear_flags()
//     is called. Callers are responsible for clearing them each frame.
//   - rt_statemachine_update() increments frames_in_state by 1. It must be
//     called exactly once per frame while the machine is in a valid state.
//   - Transitioning to the current state is a no-op (returns 1, no flag set).
//
// Ownership/Lifetime:
//   - StateMachine objects are GC-managed via rt_obj_new_i64. The GC reclaims
//     them automatically; rt_statemachine_destroy() is a no-op provided for
//     API symmetry and forward-compatibility.
//
// Links: src/runtime/collections/rt_statemachine.h (public API, with full
//        per-function documentation), docs/viperlib/game.md (StateMachine)
//
//===----------------------------------------------------------------------===//

#include "rt_statemachine.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>
#include <string.h>

/// Internal state machine implementation.
struct rt_statemachine_impl
{
    int64_t current_state;       ///< Current state ID (-1 if none).
    int64_t previous_state;      ///< Previous state ID (-1 if none).
    int64_t frames_in_state;     ///< Frames since entering current state.
    int8_t just_entered;         ///< Flag: just entered new state.
    int8_t just_exited;          ///< Flag: just exited previous state.
    int8_t states[RT_STATE_MAX]; ///< Registered states (1 = exists).
    int64_t state_count;         ///< Number of registered states.
};

rt_statemachine rt_statemachine_new(void)
{
    rt_statemachine sm = rt_obj_new_i64(0, sizeof(struct rt_statemachine_impl));
    if (!sm)
        return NULL;

    sm->current_state = -1;
    sm->previous_state = -1;
    sm->frames_in_state = 0;
    sm->just_entered = 0;
    sm->just_exited = 0;
    sm->state_count = 0;
    memset(sm->states, 0, sizeof(sm->states));

    return sm;
}

void rt_statemachine_destroy(rt_statemachine sm)
{
    // Object is GC-managed via rt_obj_new_i64; no manual free needed.
    (void)sm;
}

int8_t rt_statemachine_add_state(rt_statemachine sm, int64_t state_id)
{
    if (!sm)
        return 0;
    if (state_id < 0 || state_id >= RT_STATE_MAX)
    {
        rt_trap("StateMachine.AddState: state_id out of range [0, RT_STATE_MAX-1]");
        return 0;
    }
    if (sm->states[state_id])
        return 0; // Already exists

    sm->states[state_id] = 1;
    sm->state_count++;
    return 1;
}

int8_t rt_statemachine_set_initial(rt_statemachine sm, int64_t state_id)
{
    if (!sm)
        return 0;
    if (state_id < 0 || state_id >= RT_STATE_MAX)
        return 0;
    if (!sm->states[state_id])
        return 0;

    sm->current_state = state_id;
    sm->previous_state = -1;
    sm->frames_in_state = 0;
    sm->just_entered = 1;
    sm->just_exited = 0;
    return 1;
}

int64_t rt_statemachine_current(rt_statemachine sm)
{
    if (!sm)
        return -1;
    return sm->current_state;
}

int64_t rt_statemachine_previous(rt_statemachine sm)
{
    if (!sm)
        return -1;
    return sm->previous_state;
}

int8_t rt_statemachine_is_state(rt_statemachine sm, int64_t state_id)
{
    if (!sm)
        return 0;
    return sm->current_state == state_id ? 1 : 0;
}

int8_t rt_statemachine_transition(rt_statemachine sm, int64_t state_id)
{
    if (!sm)
        return 0;
    if (state_id < 0 || state_id >= RT_STATE_MAX)
        return 0;
    if (!sm->states[state_id])
        return 0;
    if (sm->current_state == state_id)
        return 1; // Already in this state, no-op

    sm->previous_state = sm->current_state;
    sm->current_state = state_id;
    sm->frames_in_state = 0;
    sm->just_entered = 1;
    sm->just_exited = (sm->previous_state >= 0) ? 1 : 0;
    return 1;
}

int8_t rt_statemachine_just_entered(rt_statemachine sm)
{
    if (!sm)
        return 0;
    return sm->just_entered;
}

int8_t rt_statemachine_just_exited(rt_statemachine sm)
{
    if (!sm)
        return 0;
    return sm->just_exited;
}

void rt_statemachine_clear_flags(rt_statemachine sm)
{
    if (!sm)
        return;
    sm->just_entered = 0;
    sm->just_exited = 0;
}

int64_t rt_statemachine_frames_in_state(rt_statemachine sm)
{
    if (!sm)
        return 0;
    return sm->frames_in_state;
}

void rt_statemachine_update(rt_statemachine sm)
{
    if (!sm)
        return;
    if (sm->current_state >= 0)
    {
        sm->frames_in_state++;
    }
}

int8_t rt_statemachine_has_state(rt_statemachine sm, int64_t state_id)
{
    if (!sm)
        return 0;
    if (state_id < 0 || state_id >= RT_STATE_MAX)
        return 0;
    return sm->states[state_id];
}

int64_t rt_statemachine_state_count(rt_statemachine sm)
{
    if (!sm)
        return 0;
    return sm->state_count;
}
