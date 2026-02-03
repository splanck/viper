//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_statemachine.h
/// @brief State machine for game and application state management.
///
/// Provides a simple state machine abstraction for managing game states
/// (menu, gameplay, pause, etc.) with enter/exit callbacks and transitions.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_STATEMACHINE_H
#define VIPER_RT_STATEMACHINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Maximum number of states a state machine can hold.
#define RT_STATE_MAX 32

/// Opaque handle to a StateMachine instance.
typedef struct rt_statemachine_impl *rt_statemachine;

/// Creates a new StateMachine.
/// @return A new StateMachine instance.
rt_statemachine rt_statemachine_new(void);

/// Destroys a StateMachine and frees its memory.
/// @param sm The state machine to destroy.
void rt_statemachine_destroy(rt_statemachine sm);

/// Adds a state to the state machine.
/// @param sm The state machine.
/// @param state_id Unique identifier for the state (0 to RT_STATE_MAX-1).
/// @return 1 on success, 0 if state_id already exists or out of range.
int8_t rt_statemachine_add_state(rt_statemachine sm, int64_t state_id);

/// Sets the initial state (call before first update).
/// @param sm The state machine.
/// @param state_id The state to start in.
/// @return 1 on success, 0 if state doesn't exist.
int8_t rt_statemachine_set_initial(rt_statemachine sm, int64_t state_id);

/// Gets the current state.
/// @param sm The state machine.
/// @return Current state ID, or -1 if no state is set.
int64_t rt_statemachine_current(rt_statemachine sm);

/// Gets the previous state.
/// @param sm The state machine.
/// @return Previous state ID, or -1 if no previous state.
int64_t rt_statemachine_previous(rt_statemachine sm);

/// Checks if the machine is in a specific state.
/// @param sm The state machine.
/// @param state_id The state to check.
/// @return 1 if in that state, 0 otherwise.
int8_t rt_statemachine_is_state(rt_statemachine sm, int64_t state_id);

/// Transitions to a new state.
/// @param sm The state machine.
/// @param state_id The state to transition to.
/// @return 1 on success, 0 if state doesn't exist.
int8_t rt_statemachine_transition(rt_statemachine sm, int64_t state_id);

/// Checks if a transition just occurred this frame.
/// Call after transition(), returns true once then resets.
/// @param sm The state machine.
/// @return 1 if a transition just occurred, 0 otherwise.
int8_t rt_statemachine_just_entered(rt_statemachine sm);

/// Checks if we just exited the previous state.
/// Call after transition(), returns true once then resets.
/// @param sm The state machine.
/// @return 1 if we just exited the previous state, 0 otherwise.
int8_t rt_statemachine_just_exited(rt_statemachine sm);

/// Clears the transition flags (call at end of frame).
/// @param sm The state machine.
void rt_statemachine_clear_flags(rt_statemachine sm);

/// Gets the number of frames spent in the current state.
/// @param sm The state machine.
/// @return Number of frames since entering current state.
int64_t rt_statemachine_frames_in_state(rt_statemachine sm);

/// Increments the frame counter (call once per frame).
/// @param sm The state machine.
void rt_statemachine_update(rt_statemachine sm);

/// Checks if a state exists.
/// @param sm The state machine.
/// @param state_id The state to check.
/// @return 1 if state exists, 0 otherwise.
int8_t rt_statemachine_has_state(rt_statemachine sm, int64_t state_id);

/// Gets the number of states registered.
/// @param sm The state machine.
/// @return Number of registered states.
int64_t rt_statemachine_state_count(rt_statemachine sm);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_STATEMACHINE_H
