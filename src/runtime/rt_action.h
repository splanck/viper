//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_action.h
// Purpose: Action mapping system that abstracts raw input devices into named
//          actions with keyboard, mouse, and gamepad bindings for both button
//          and axis input types.
// Key invariants: Action names are unique; button actions and axis actions are
//                 disjoint sets; axis values are clamped to -1.0..1.0; all
//                 state queries reflect the current frame after rt_action_update.
// Ownership/Lifetime: The action system is globally initialized/shutdown with
//                     rt_action_init/rt_action_shutdown; action names are
//                     rt_string values following runtime refcount rules.
// Links: docs/viperlib.md
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // Axis Constants (for gamepad analog bindings)
    //=========================================================================

#define VIPER_AXIS_LEFT_X 0
#define VIPER_AXIS_LEFT_Y 1
#define VIPER_AXIS_RIGHT_X 2
#define VIPER_AXIS_RIGHT_Y 3
#define VIPER_AXIS_LEFT_TRIGGER 4
#define VIPER_AXIS_RIGHT_TRIGGER 5
#define VIPER_AXIS_MAX 6

    //=========================================================================
    // Action System Lifecycle
    //=========================================================================

    /// @brief Initialize the action mapping system.
    /// @details Called automatically when Canvas is created.
    void rt_action_init(void);

    /// @brief Shutdown the action mapping system and free all resources.
    void rt_action_shutdown(void);

    /// @brief Update action states for new frame.
    /// @details Called by Canvas.Poll() after input devices are updated.
    void rt_action_update(void);

    /// @brief Clear all defined actions and bindings.
    void rt_action_clear(void);

    //=========================================================================
    // Action Definition
    //=========================================================================

    /// @brief Define a new button action.
    /// @param name Action name (e.g., "jump", "fire", "pause").
    /// @return 1 on success, 0 if action already exists or name is invalid.
    int8_t rt_action_define(rt_string name);

    /// @brief Define a new axis action.
    /// @param name Action name (e.g., "move_x", "look_y").
    /// @return 1 on success, 0 if action already exists or name is invalid.
    int8_t rt_action_define_axis(rt_string name);

    /// @brief Check if an action is defined.
    /// @param name Action name.
    /// @return 1 if defined, 0 otherwise.
    int8_t rt_action_exists(rt_string name);

    /// @brief Check if an action is an axis action.
    /// @param name Action name.
    /// @return 1 if axis action, 0 if button action or not defined.
    int8_t rt_action_is_axis(rt_string name);

    /// @brief Remove an action and all its bindings.
    /// @param name Action name.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_action_remove(rt_string name);

    //=========================================================================
    // Keyboard Bindings
    //=========================================================================

    /// @brief Bind a keyboard key to a button action.
    /// @param action Action name.
    /// @param key Key code (VIPER_KEY_*).
    /// @return 1 on success, 0 if action not found or is an axis action.
    int8_t rt_action_bind_key(rt_string action, int64_t key);

    /// @brief Bind a keyboard key to an axis action with a specific value.
    /// @param action Action name.
    /// @param key Key code.
    /// @param value Axis value when key is pressed (-1.0 to 1.0).
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_key_axis(rt_string action, int64_t key, double value);

    /// @brief Unbind a keyboard key from an action.
    /// @param action Action name.
    /// @param key Key code.
    /// @return 1 if unbound, 0 if binding not found.
    int8_t rt_action_unbind_key(rt_string action, int64_t key);

    //=========================================================================
    // Mouse Bindings
    //=========================================================================

    /// @brief Bind a mouse button to a button action.
    /// @param action Action name.
    /// @param button Mouse button (VIPER_MOUSE_BUTTON_*).
    /// @return 1 on success, 0 if action not found or is an axis action.
    int8_t rt_action_bind_mouse(rt_string action, int64_t button);

    /// @brief Unbind a mouse button from an action.
    /// @param action Action name.
    /// @param button Mouse button.
    /// @return 1 if unbound, 0 if binding not found.
    int8_t rt_action_unbind_mouse(rt_string action, int64_t button);

    /// @brief Bind mouse X delta to an axis action.
    /// @param action Action name.
    /// @param sensitivity Multiplier for mouse delta.
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_mouse_x(rt_string action, double sensitivity);

    /// @brief Bind mouse Y delta to an axis action.
    /// @param action Action name.
    /// @param sensitivity Multiplier for mouse delta.
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_mouse_y(rt_string action, double sensitivity);

    /// @brief Bind mouse scroll X to an axis action.
    /// @param action Action name.
    /// @param sensitivity Multiplier for scroll delta.
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_scroll_x(rt_string action, double sensitivity);

    /// @brief Bind mouse scroll Y to an axis action.
    /// @param action Action name.
    /// @param sensitivity Multiplier for scroll delta.
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_scroll_y(rt_string action, double sensitivity);

    //=========================================================================
    // Gamepad Bindings
    //=========================================================================

    /// @brief Bind a gamepad button to a button action.
    /// @param action Action name.
    /// @param pad_index Controller index (0-3), or -1 for any controller.
    /// @param button Gamepad button (VIPER_PAD_*).
    /// @return 1 on success, 0 if action not found or is an axis action.
    int8_t rt_action_bind_pad_button(rt_string action, int64_t pad_index, int64_t button);

    /// @brief Unbind a gamepad button from an action.
    /// @param action Action name.
    /// @param pad_index Controller index, or -1 for any controller.
    /// @param button Gamepad button.
    /// @return 1 if unbound, 0 if binding not found.
    int8_t rt_action_unbind_pad_button(rt_string action, int64_t pad_index, int64_t button);

    /// @brief Bind a gamepad axis to an axis action.
    /// @param action Action name.
    /// @param pad_index Controller index (0-3), or -1 for any controller.
    /// @param axis Axis constant (VIPER_AXIS_*).
    /// @param scale Multiplier for axis value (use -1.0 to invert).
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_pad_axis(rt_string action, int64_t pad_index, int64_t axis, double scale);

    /// @brief Unbind a gamepad axis from an action.
    /// @param action Action name.
    /// @param pad_index Controller index, or -1 for any controller.
    /// @param axis Axis constant.
    /// @return 1 if unbound, 0 if binding not found.
    int8_t rt_action_unbind_pad_axis(rt_string action, int64_t pad_index, int64_t axis);

    /// @brief Bind a gamepad button to an axis action with a specific value.
    /// @param action Action name.
    /// @param pad_index Controller index (0-3), or -1 for any controller.
    /// @param button Gamepad button.
    /// @param value Axis value when button is pressed.
    /// @return 1 on success, 0 if action not found or is not an axis action.
    int8_t rt_action_bind_pad_button_axis(rt_string action,
                                          int64_t pad_index,
                                          int64_t button,
                                          double value);

    //=========================================================================
    // Button Action State Queries
    //=========================================================================

    /// @brief Check if a button action was pressed this frame.
    /// @param action Action name.
    /// @return 1 if any bound input was pressed this frame, 0 otherwise.
    int8_t rt_action_pressed(rt_string action);

    /// @brief Check if a button action was released this frame.
    /// @param action Action name.
    /// @return 1 if any bound input was released this frame, 0 otherwise.
    int8_t rt_action_released(rt_string action);

    /// @brief Check if a button action is currently held.
    /// @param action Action name.
    /// @return 1 if any bound input is currently down, 0 otherwise.
    int8_t rt_action_held(rt_string action);

    /// @brief Get the "strength" of a button action.
    /// @param action Action name.
    /// @return 1.0 if held (or trigger value for gamepad triggers), 0.0 otherwise.
    double rt_action_strength(rt_string action);

    //=========================================================================
    // Axis Action Queries
    //=========================================================================

    /// @brief Get the current value of an axis action.
    /// @param action Action name.
    /// @return Combined axis value from all bindings, clamped to -1.0 to 1.0.
    double rt_action_axis(rt_string action);

    /// @brief Get the raw value of an axis action (not clamped).
    /// @param action Action name.
    /// @return Combined axis value from all bindings, not clamped.
    double rt_action_axis_raw(rt_string action);

    //=========================================================================
    // Binding Introspection
    //=========================================================================

    /// @brief Get all defined action names.
    /// @return Seq of action name strings.
    void *rt_action_list(void);

    /// @brief Get all bindings for an action as a human-readable string.
    /// @param action Action name.
    /// @return Description like "Space, Gamepad A" or empty if not found.
    rt_string rt_action_bindings_str(rt_string action);

    /// @brief Get the number of bindings for an action.
    /// @param action Action name.
    /// @return Number of bindings, 0 if action not found.
    int64_t rt_action_binding_count(rt_string action);

    //=========================================================================
    // Conflict Detection
    //=========================================================================

    /// @brief Check if a key is bound to any action.
    /// @param key Key code.
    /// @return Action name if bound, empty string if not.
    rt_string rt_action_key_bound_to(int64_t key);

    /// @brief Check if a mouse button is bound to any action.
    /// @param button Mouse button.
    /// @return Action name if bound, empty string if not.
    rt_string rt_action_mouse_bound_to(int64_t button);

    /// @brief Check if a gamepad button is bound to any action.
    /// @param pad_index Controller index.
    /// @param button Gamepad button.
    /// @return Action name if bound, empty string if not.
    rt_string rt_action_pad_button_bound_to(int64_t pad_index, int64_t button);

    //=========================================================================
    // Persistence (Save/Load)
    //=========================================================================

    /// @brief Serialize all actions and bindings to a JSON string.
    /// @return JSON string representing current action configuration.
    rt_string rt_action_save(void);

    /// @brief Load actions and bindings from a JSON string.
    /// @param json JSON string previously produced by rt_action_save.
    /// @return 1 on success, 0 on parse error.
    int8_t rt_action_load(rt_string json);

    //=========================================================================
    // Key Chord/Combo Bindings
    //=========================================================================

    /// @brief Bind a key chord (multi-key combo) to a button action.
    /// @details A chord triggers when ALL specified keys are held simultaneously.
    ///          The last key in the chord must be newly pressed this frame for
    ///          the action to register as "pressed". Example: Ctrl+Shift+S.
    /// @param action Action name.
    /// @param keys Seq of key codes (i64 values).
    /// @return 1 on success, 0 if action not found, is axis, or keys invalid.
    int8_t rt_action_bind_chord(rt_string action, void *keys);

    /// @brief Unbind a key chord from an action.
    /// @details Removes the chord that exactly matches the given key set.
    /// @param action Action name.
    /// @param keys Seq of key codes to match.
    /// @return 1 if unbound, 0 if not found.
    int8_t rt_action_unbind_chord(rt_string action, void *keys);

    /// @brief Get the number of chord bindings for an action.
    /// @param action Action name.
    /// @return Number of chord bindings.
    int64_t rt_action_chord_count(rt_string action);

    //=========================================================================
    // Axis Constant Getters (for runtime.def)
    //=========================================================================

    int64_t rt_action_axis_left_x(void);
    int64_t rt_action_axis_left_y(void);
    int64_t rt_action_axis_right_x(void);
    int64_t rt_action_axis_right_y(void);
    int64_t rt_action_axis_left_trigger(void);
    int64_t rt_action_axis_right_trigger(void);

#ifdef __cplusplus
}
#endif
