//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_inputmgr.h
/// @brief High-level input manager with debouncing and action mapping.
///
/// Provides a unified interface for handling keyboard, mouse, and gamepad
/// input with built-in debouncing support for menu navigation and similar
/// use cases where rapid repeated inputs are not desired.
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_INPUTMGR_H
#define VIPER_RT_INPUTMGR_H

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle to an InputManager instance.
typedef struct rt_inputmgr_impl *rt_inputmgr;

/// Creates a new InputManager.
/// @return A new InputManager instance.
rt_inputmgr rt_inputmgr_new(void);

/// Destroys an InputManager and frees its memory.
/// @param mgr The input manager to destroy.
void rt_inputmgr_destroy(rt_inputmgr mgr);

/// Updates the input manager state. Call once per frame after Canvas.Poll().
/// @param mgr The input manager.
void rt_inputmgr_update(rt_inputmgr mgr);

//=============================================================================
// Keyboard - Just Pressed/Released (Edge Detection)
//=============================================================================

/// Check if a key was just pressed this frame.
/// @param mgr The input manager.
/// @param key Key code.
/// @return 1 if pressed this frame, 0 otherwise.
int8_t rt_inputmgr_key_pressed(rt_inputmgr mgr, int64_t key);

/// Check if a key was just released this frame.
/// @param mgr The input manager.
/// @param key Key code.
/// @return 1 if released this frame, 0 otherwise.
int8_t rt_inputmgr_key_released(rt_inputmgr mgr, int64_t key);

/// Check if a key is currently held.
/// @param mgr The input manager.
/// @param key Key code.
/// @return 1 if held, 0 otherwise.
int8_t rt_inputmgr_key_held(rt_inputmgr mgr, int64_t key);

//=============================================================================
// Keyboard - Debounced (for menus)
//=============================================================================

/// Check if a key was pressed with debouncing.
/// Returns true once, then requires the key to be released before returning
/// true again. Useful for menu navigation.
/// @param mgr The input manager.
/// @param key Key code.
/// @return 1 if pressed (debounced), 0 otherwise.
int8_t rt_inputmgr_key_pressed_debounced(rt_inputmgr mgr, int64_t key);

/// Set the debounce delay in frames.
/// @param mgr The input manager.
/// @param frames Number of frames to wait before allowing repeat.
void rt_inputmgr_set_debounce_delay(rt_inputmgr mgr, int64_t frames);

/// Get the current debounce delay.
/// @param mgr The input manager.
/// @return Debounce delay in frames.
int64_t rt_inputmgr_get_debounce_delay(rt_inputmgr mgr);

//=============================================================================
// Mouse - Just Pressed/Released (Edge Detection)
//=============================================================================

/// Check if a mouse button was just pressed this frame.
/// @param mgr The input manager.
/// @param button Button index (0=left, 1=right, 2=middle).
/// @return 1 if pressed this frame, 0 otherwise.
int8_t rt_inputmgr_mouse_pressed(rt_inputmgr mgr, int64_t button);

/// Check if a mouse button was just released this frame.
/// @param mgr The input manager.
/// @param button Button index.
/// @return 1 if released this frame, 0 otherwise.
int8_t rt_inputmgr_mouse_released(rt_inputmgr mgr, int64_t button);

/// Check if a mouse button is currently held.
/// @param mgr The input manager.
/// @param button Button index.
/// @return 1 if held, 0 otherwise.
int8_t rt_inputmgr_mouse_held(rt_inputmgr mgr, int64_t button);

//=============================================================================
// Mouse - Position and Movement
//=============================================================================

/// Get current mouse X position.
/// @param mgr The input manager.
/// @return X coordinate.
int64_t rt_inputmgr_mouse_x(rt_inputmgr mgr);

/// Get current mouse Y position.
/// @param mgr The input manager.
/// @return Y coordinate.
int64_t rt_inputmgr_mouse_y(rt_inputmgr mgr);

/// Get mouse X movement since last frame.
/// @param mgr The input manager.
/// @return Delta X.
int64_t rt_inputmgr_mouse_delta_x(rt_inputmgr mgr);

/// Get mouse Y movement since last frame.
/// @param mgr The input manager.
/// @return Delta Y.
int64_t rt_inputmgr_mouse_delta_y(rt_inputmgr mgr);

/// Get vertical scroll wheel delta.
/// @param mgr The input manager.
/// @return Scroll delta (positive = up).
int64_t rt_inputmgr_scroll_y(rt_inputmgr mgr);

/// Get horizontal scroll wheel delta.
/// @param mgr The input manager.
/// @return Scroll delta (positive = right).
int64_t rt_inputmgr_scroll_x(rt_inputmgr mgr);

//=============================================================================
// Gamepad - Just Pressed/Released (Edge Detection)
//=============================================================================

/// Check if a gamepad button was just pressed this frame.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3), or -1 for any gamepad.
/// @param button Button constant.
/// @return 1 if pressed this frame, 0 otherwise.
int8_t rt_inputmgr_pad_pressed(rt_inputmgr mgr, int64_t pad, int64_t button);

/// Check if a gamepad button was just released this frame.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3), or -1 for any gamepad.
/// @param button Button constant.
/// @return 1 if released this frame, 0 otherwise.
int8_t rt_inputmgr_pad_released(rt_inputmgr mgr, int64_t pad, int64_t button);

/// Check if a gamepad button is currently held.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3), or -1 for any gamepad.
/// @param button Button constant.
/// @return 1 if held, 0 otherwise.
int8_t rt_inputmgr_pad_held(rt_inputmgr mgr, int64_t pad, int64_t button);

//=============================================================================
// Gamepad - Analog Inputs
//=============================================================================

/// Get left stick X axis.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3).
/// @return Value from -1.0 to 1.0.
double rt_inputmgr_pad_left_x(rt_inputmgr mgr, int64_t pad);

/// Get left stick Y axis.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3).
/// @return Value from -1.0 to 1.0.
double rt_inputmgr_pad_left_y(rt_inputmgr mgr, int64_t pad);

/// Get right stick X axis.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3).
/// @return Value from -1.0 to 1.0.
double rt_inputmgr_pad_right_x(rt_inputmgr mgr, int64_t pad);

/// Get right stick Y axis.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3).
/// @return Value from -1.0 to 1.0.
double rt_inputmgr_pad_right_y(rt_inputmgr mgr, int64_t pad);

/// Get left trigger value.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3).
/// @return Value from 0.0 to 1.0.
double rt_inputmgr_pad_left_trigger(rt_inputmgr mgr, int64_t pad);

/// Get right trigger value.
/// @param mgr The input manager.
/// @param pad Gamepad index (0-3).
/// @return Value from 0.0 to 1.0.
double rt_inputmgr_pad_right_trigger(rt_inputmgr mgr, int64_t pad);

//=============================================================================
// Unified Direction Input (combines keyboard, D-pad, and sticks)
//=============================================================================

/// Check if any "up" input is active (arrow key, W, D-pad up, or left stick up).
/// @param mgr The input manager.
/// @return 1 if up is pressed, 0 otherwise.
int8_t rt_inputmgr_up(rt_inputmgr mgr);

/// Check if any "down" input is active.
/// @param mgr The input manager.
/// @return 1 if down is pressed, 0 otherwise.
int8_t rt_inputmgr_down(rt_inputmgr mgr);

/// Check if any "left" input is active.
/// @param mgr The input manager.
/// @return 1 if left is pressed, 0 otherwise.
int8_t rt_inputmgr_left(rt_inputmgr mgr);

/// Check if any "right" input is active.
/// @param mgr The input manager.
/// @return 1 if right is pressed, 0 otherwise.
int8_t rt_inputmgr_right(rt_inputmgr mgr);

/// Check if any "confirm" input is active (Enter, Space, A button).
/// @param mgr The input manager.
/// @return 1 if confirm is pressed, 0 otherwise.
int8_t rt_inputmgr_confirm(rt_inputmgr mgr);

/// Check if any "cancel" input is active (Escape, B button).
/// @param mgr The input manager.
/// @return 1 if cancel is pressed, 0 otherwise.
int8_t rt_inputmgr_cancel(rt_inputmgr mgr);

/// Get horizontal axis value (-1.0 to 1.0) from any input source.
/// @param mgr The input manager.
/// @return Horizontal axis value.
double rt_inputmgr_axis_x(rt_inputmgr mgr);

/// Get vertical axis value (-1.0 to 1.0) from any input source.
/// @param mgr The input manager.
/// @return Vertical axis value.
double rt_inputmgr_axis_y(rt_inputmgr mgr);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_INPUTMGR_H
