//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

#include "rt_statemachine.h"

#include <stdlib.h>
#include <string.h>

/// Internal state machine implementation.
struct rt_statemachine_impl
{
    int64_t current_state;         ///< Current state ID (-1 if none).
    int64_t previous_state;        ///< Previous state ID (-1 if none).
    int64_t frames_in_state;       ///< Frames since entering current state.
    int8_t just_entered;           ///< Flag: just entered new state.
    int8_t just_exited;            ///< Flag: just exited previous state.
    int8_t states[RT_STATE_MAX];   ///< Registered states (1 = exists).
    int64_t state_count;           ///< Number of registered states.
};

rt_statemachine rt_statemachine_new(void)
{
    rt_statemachine sm = malloc(sizeof(struct rt_statemachine_impl));
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
    if (sm)
    {
        free(sm);
    }
}

int8_t rt_statemachine_add_state(rt_statemachine sm, int64_t state_id)
{
    if (!sm)
        return 0;
    if (state_id < 0 || state_id >= RT_STATE_MAX)
        return 0;
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
