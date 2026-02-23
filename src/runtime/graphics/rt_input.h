//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_input.h
// Purpose: Keyboard, mouse, and gamepad input handling for the Viper.Input runtime namespace, providing per-frame key state, mouse position/button, and gamepad queries.
//
// Key invariants:
//   - Input state is frame-coherent; pressed/released edge lists reset each rt_input_poll call.
//   - Key code constants are GLFW-compatible integer values.
//   - Mouse coordinates are in logical pixels relative to the window top-left.
//   - Gamepad axes are in [-1.0, 1.0]; triggers are in [0.0, 1.0].
//
// Ownership/Lifetime:
//   - Canvas owns the input subsystem; input callbacks are non-owning function pointers.
//   - No heap allocation in state query functions.
//
// Links: src/runtime/graphics/rt_input.c (implementation), src/runtime/core/rt_string.h
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
// Key Code Constants (GLFW-compatible values)
//=========================================================================

// Unknown key
#define VIPER_KEY_UNKNOWN 0

// Printable ASCII keys (letters and numbers match ASCII)
#define VIPER_KEY_SPACE 32
#define VIPER_KEY_QUOTE 39  // '
#define VIPER_KEY_COMMA 44  // ,
#define VIPER_KEY_MINUS 45  // -
#define VIPER_KEY_PERIOD 46 // .
#define VIPER_KEY_SLASH 47  // /

#define VIPER_KEY_0 48
#define VIPER_KEY_1 49
#define VIPER_KEY_2 50
#define VIPER_KEY_3 51
#define VIPER_KEY_4 52
#define VIPER_KEY_5 53
#define VIPER_KEY_6 54
#define VIPER_KEY_7 55
#define VIPER_KEY_8 56
#define VIPER_KEY_9 57

#define VIPER_KEY_SEMICOLON 59 // ;
#define VIPER_KEY_EQUALS 61    // =

#define VIPER_KEY_A 65
#define VIPER_KEY_B 66
#define VIPER_KEY_C 67
#define VIPER_KEY_D 68
#define VIPER_KEY_E 69
#define VIPER_KEY_F 70
#define VIPER_KEY_G 71
#define VIPER_KEY_H 72
#define VIPER_KEY_I 73
#define VIPER_KEY_J 74
#define VIPER_KEY_K 75
#define VIPER_KEY_L 76
#define VIPER_KEY_M 77
#define VIPER_KEY_N 78
#define VIPER_KEY_O 79
#define VIPER_KEY_P 80
#define VIPER_KEY_Q 81
#define VIPER_KEY_R 82
#define VIPER_KEY_S 83
#define VIPER_KEY_T 84
#define VIPER_KEY_U 85
#define VIPER_KEY_V 86
#define VIPER_KEY_W 87
#define VIPER_KEY_X 88
#define VIPER_KEY_Y 89
#define VIPER_KEY_Z 90

#define VIPER_KEY_LBRACKET 91  // [
#define VIPER_KEY_BACKSLASH 92 // backslash
#define VIPER_KEY_RBRACKET 93  // ]
#define VIPER_KEY_GRAVE 96     // `

// Special keys (GLFW-style values >= 256)
#define VIPER_KEY_ESCAPE 256
#define VIPER_KEY_ENTER 257
#define VIPER_KEY_TAB 258
#define VIPER_KEY_BACKSPACE 259
#define VIPER_KEY_INSERT 260
#define VIPER_KEY_DELETE 261
#define VIPER_KEY_RIGHT 262
#define VIPER_KEY_LEFT 263
#define VIPER_KEY_DOWN 264
#define VIPER_KEY_UP 265
#define VIPER_KEY_PAGEUP 266
#define VIPER_KEY_PAGEDOWN 267
#define VIPER_KEY_HOME 268
#define VIPER_KEY_END 269

// Function keys
#define VIPER_KEY_F1 290
#define VIPER_KEY_F2 291
#define VIPER_KEY_F3 292
#define VIPER_KEY_F4 293
#define VIPER_KEY_F5 294
#define VIPER_KEY_F6 295
#define VIPER_KEY_F7 296
#define VIPER_KEY_F8 297
#define VIPER_KEY_F9 298
#define VIPER_KEY_F10 299
#define VIPER_KEY_F11 300
#define VIPER_KEY_F12 301

// Numpad keys
#define VIPER_KEY_NUM0 320
#define VIPER_KEY_NUM1 321
#define VIPER_KEY_NUM2 322
#define VIPER_KEY_NUM3 323
#define VIPER_KEY_NUM4 324
#define VIPER_KEY_NUM5 325
#define VIPER_KEY_NUM6 326
#define VIPER_KEY_NUM7 327
#define VIPER_KEY_NUM8 328
#define VIPER_KEY_NUM9 329
#define VIPER_KEY_NUMDOT 330
#define VIPER_KEY_NUMDIV 331
#define VIPER_KEY_NUMMUL 332
#define VIPER_KEY_NUMSUB 333
#define VIPER_KEY_NUMADD 334
#define VIPER_KEY_NUMENTER 335

// Modifier keys
#define VIPER_KEY_LSHIFT 340
#define VIPER_KEY_LCTRL 341
#define VIPER_KEY_LALT 342
#define VIPER_KEY_RSHIFT 344
#define VIPER_KEY_RCTRL 345
#define VIPER_KEY_RALT 346

// Alias for generic modifier keys
#define VIPER_KEY_SHIFT VIPER_KEY_LSHIFT
#define VIPER_KEY_CTRL VIPER_KEY_LCTRL
#define VIPER_KEY_ALT VIPER_KEY_LALT

// Maximum key code we track
#define VIPER_KEY_MAX 512

    //=========================================================================
    // Keyboard State Management
    //=========================================================================

    /// @brief Initialize the keyboard input system.
    /// @details Called internally when Canvas is created.
    void rt_keyboard_init(void);

    /// @brief Reset keyboard state for new frame.
    /// @details Called by Canvas.Poll() to clear pressed/released lists.
    void rt_keyboard_begin_frame(void);

    /// @brief Register a key press event.
    /// @param key Key code.
    void rt_keyboard_on_key_down(int64_t key);

    /// @brief Register a key release event.
    /// @param key Key code.
    void rt_keyboard_on_key_up(int64_t key);

    /// @brief Add text input character.
    /// @param ch Unicode codepoint.
    void rt_keyboard_text_input(int32_t ch);

    /// @brief Set the active Canvas for keyboard input.
    /// @param canvas Canvas handle (opaque pointer to vgfx window).
    void rt_keyboard_set_canvas(void *canvas);

    //=========================================================================
    // Polling Methods (Current State)
    //=========================================================================

    /// @brief Check if a key is currently pressed.
    /// @param key Key code to check.
    /// @return 1 if pressed, 0 otherwise.
    int8_t rt_keyboard_is_down(int64_t key);

    /// @brief Check if a key is currently released.
    /// @param key Key code to check.
    /// @return 1 if released, 0 otherwise.
    int8_t rt_keyboard_is_up(int64_t key);

    /// @brief Check if any key is currently pressed.
    /// @return 1 if any key is pressed, 0 otherwise.
    int8_t rt_keyboard_any_down(void);

    /// @brief Get the first pressed key code.
    /// @return Key code of first pressed key, or 0 if none.
    int64_t rt_keyboard_get_down(void);

    //=========================================================================
    // Event Methods (Since Last Poll)
    //=========================================================================

    /// @brief Check if a key was pressed this frame.
    /// @param key Key code to check.
    /// @return 1 if pressed this frame, 0 otherwise.
    int8_t rt_keyboard_was_pressed(int64_t key);

    /// @brief Check if a key was released this frame.
    /// @param key Key code to check.
    /// @return 1 if released this frame, 0 otherwise.
    int8_t rt_keyboard_was_released(int64_t key);

    /// @brief Get all keys pressed this frame.
    /// @return Seq of key codes (integers).
    void *rt_keyboard_get_pressed(void);

    /// @brief Get all keys released this frame.
    /// @return Seq of key codes (integers).
    void *rt_keyboard_get_released(void);

    //=========================================================================
    // Text Input
    //=========================================================================

    /// @brief Get text typed since last poll.
    /// @return String containing typed characters.
    rt_string rt_keyboard_get_text(void);

    /// @brief Enable text input mode.
    /// @details Enables text input events on platforms that support IME.
    void rt_keyboard_enable_text_input(void);

    /// @brief Disable text input mode.
    void rt_keyboard_disable_text_input(void);

    //=========================================================================
    // Modifier State
    //=========================================================================

    /// @brief Check if Shift key is held.
    /// @return 1 if Shift is held, 0 otherwise.
    int8_t rt_keyboard_shift(void);

    /// @brief Check if Ctrl key is held.
    /// @return 1 if Ctrl is held, 0 otherwise.
    int8_t rt_keyboard_ctrl(void);

    /// @brief Check if Alt key is held.
    /// @return 1 if Alt is held, 0 otherwise.
    int8_t rt_keyboard_alt(void);

    /// @brief Check if Caps Lock is on.
    /// @return 1 if Caps Lock is on, 0 otherwise.
    int8_t rt_keyboard_caps_lock(void);

    //=========================================================================
    // Key Name Helper
    //=========================================================================

    /// @brief Get human-readable name for a key code.
    /// @param key Key code.
    /// @return String name (e.g., "A", "Enter", "F1").
    rt_string rt_keyboard_key_name(int64_t key);

    //=========================================================================
    // Key Code Constants (Runtime getters)
    //=========================================================================

    int64_t rt_keyboard_key_unknown(void);
    int64_t rt_keyboard_key_a(void);
    int64_t rt_keyboard_key_b(void);
    int64_t rt_keyboard_key_c(void);
    int64_t rt_keyboard_key_d(void);
    int64_t rt_keyboard_key_e(void);
    int64_t rt_keyboard_key_f(void);
    int64_t rt_keyboard_key_g(void);
    int64_t rt_keyboard_key_h(void);
    int64_t rt_keyboard_key_i(void);
    int64_t rt_keyboard_key_j(void);
    int64_t rt_keyboard_key_k(void);
    int64_t rt_keyboard_key_l(void);
    int64_t rt_keyboard_key_m(void);
    int64_t rt_keyboard_key_n(void);
    int64_t rt_keyboard_key_o(void);
    int64_t rt_keyboard_key_p(void);
    int64_t rt_keyboard_key_q(void);
    int64_t rt_keyboard_key_r(void);
    int64_t rt_keyboard_key_s(void);
    int64_t rt_keyboard_key_t(void);
    int64_t rt_keyboard_key_u(void);
    int64_t rt_keyboard_key_v(void);
    int64_t rt_keyboard_key_w(void);
    int64_t rt_keyboard_key_x(void);
    int64_t rt_keyboard_key_y(void);
    int64_t rt_keyboard_key_z(void);
    int64_t rt_keyboard_key_0(void);
    int64_t rt_keyboard_key_1(void);
    int64_t rt_keyboard_key_2(void);
    int64_t rt_keyboard_key_3(void);
    int64_t rt_keyboard_key_4(void);
    int64_t rt_keyboard_key_5(void);
    int64_t rt_keyboard_key_6(void);
    int64_t rt_keyboard_key_7(void);
    int64_t rt_keyboard_key_8(void);
    int64_t rt_keyboard_key_9(void);
    int64_t rt_keyboard_key_f1(void);
    int64_t rt_keyboard_key_f2(void);
    int64_t rt_keyboard_key_f3(void);
    int64_t rt_keyboard_key_f4(void);
    int64_t rt_keyboard_key_f5(void);
    int64_t rt_keyboard_key_f6(void);
    int64_t rt_keyboard_key_f7(void);
    int64_t rt_keyboard_key_f8(void);
    int64_t rt_keyboard_key_f9(void);
    int64_t rt_keyboard_key_f10(void);
    int64_t rt_keyboard_key_f11(void);
    int64_t rt_keyboard_key_f12(void);
    int64_t rt_keyboard_key_up(void);
    int64_t rt_keyboard_key_down(void);
    int64_t rt_keyboard_key_left(void);
    int64_t rt_keyboard_key_right(void);
    int64_t rt_keyboard_key_home(void);
    int64_t rt_keyboard_key_end(void);
    int64_t rt_keyboard_key_pageup(void);
    int64_t rt_keyboard_key_pagedown(void);
    int64_t rt_keyboard_key_insert(void);
    int64_t rt_keyboard_key_delete(void);
    int64_t rt_keyboard_key_backspace(void);
    int64_t rt_keyboard_key_tab(void);
    int64_t rt_keyboard_key_enter(void);
    int64_t rt_keyboard_key_space(void);
    int64_t rt_keyboard_key_escape(void);
    int64_t rt_keyboard_key_shift(void);
    int64_t rt_keyboard_key_ctrl(void);
    int64_t rt_keyboard_key_alt(void);
    int64_t rt_keyboard_key_lshift(void);
    int64_t rt_keyboard_key_rshift(void);
    int64_t rt_keyboard_key_lctrl(void);
    int64_t rt_keyboard_key_rctrl(void);
    int64_t rt_keyboard_key_lalt(void);
    int64_t rt_keyboard_key_ralt(void);
    int64_t rt_keyboard_key_minus(void);
    int64_t rt_keyboard_key_equals(void);
    int64_t rt_keyboard_key_lbracket(void);
    int64_t rt_keyboard_key_rbracket(void);
    int64_t rt_keyboard_key_backslash(void);
    int64_t rt_keyboard_key_semicolon(void);
    int64_t rt_keyboard_key_quote(void);
    int64_t rt_keyboard_key_grave(void);
    int64_t rt_keyboard_key_comma(void);
    int64_t rt_keyboard_key_period(void);
    int64_t rt_keyboard_key_slash(void);
    int64_t rt_keyboard_key_num0(void);
    int64_t rt_keyboard_key_num1(void);
    int64_t rt_keyboard_key_num2(void);
    int64_t rt_keyboard_key_num3(void);
    int64_t rt_keyboard_key_num4(void);
    int64_t rt_keyboard_key_num5(void);
    int64_t rt_keyboard_key_num6(void);
    int64_t rt_keyboard_key_num7(void);
    int64_t rt_keyboard_key_num8(void);
    int64_t rt_keyboard_key_num9(void);
    int64_t rt_keyboard_key_numadd(void);
    int64_t rt_keyboard_key_numsub(void);
    int64_t rt_keyboard_key_nummul(void);
    int64_t rt_keyboard_key_numdiv(void);
    int64_t rt_keyboard_key_numenter(void);
    int64_t rt_keyboard_key_numdot(void);

    //=========================================================================
    // Mouse Input
    //=========================================================================

    //=========================================================================
    // Mouse Button Constants
    //=========================================================================

#define VIPER_MOUSE_BUTTON_LEFT 0
#define VIPER_MOUSE_BUTTON_RIGHT 1
#define VIPER_MOUSE_BUTTON_MIDDLE 2
#define VIPER_MOUSE_BUTTON_X1 3
#define VIPER_MOUSE_BUTTON_X2 4
#define VIPER_MOUSE_BUTTON_MAX 5

    //=========================================================================
    // Mouse State Management
    //=========================================================================

    /// @brief Initialize the mouse input system.
    /// @details Called internally when Canvas is created.
    void rt_mouse_init(void);

    /// @brief Reset mouse state for new frame.
    /// @details Called by Canvas.Poll() to clear deltas and event lists.
    void rt_mouse_begin_frame(void);

    /// @brief Update mouse position.
    /// @param x Current X position.
    /// @param y Current Y position.
    void rt_mouse_update_pos(int64_t x, int64_t y);

    /// @brief Register a mouse button press event.
    /// @param button Button index.
    void rt_mouse_button_down(int64_t button);

    /// @brief Register a mouse button release event.
    /// @param button Button index.
    void rt_mouse_button_up(int64_t button);

    /// @brief Update scroll wheel deltas.
    /// @param dx Horizontal scroll delta.
    /// @param dy Vertical scroll delta.
    void rt_mouse_update_wheel(int64_t dx, int64_t dy);

    /// @brief Set the active Canvas for mouse input.
    /// @param canvas Canvas handle (opaque pointer to vgfx window).
    void rt_mouse_set_canvas(void *canvas);

    //=========================================================================
    // Position Methods
    //=========================================================================

    /// @brief Get current mouse X position.
    /// @return X coordinate in canvas pixels.
    int64_t rt_mouse_x(void);

    /// @brief Get current mouse Y position.
    /// @return Y coordinate in canvas pixels.
    int64_t rt_mouse_y(void);

    /// @brief Get X movement since last poll.
    /// @return Delta X in pixels.
    int64_t rt_mouse_delta_x(void);

    /// @brief Get Y movement since last poll.
    /// @return Delta Y in pixels.
    int64_t rt_mouse_delta_y(void);

    //=========================================================================
    // Button State (Polling)
    //=========================================================================

    /// @brief Check if a mouse button is currently pressed.
    /// @param button Button index to check.
    /// @return 1 if pressed, 0 otherwise.
    int8_t rt_mouse_is_down(int64_t button);

    /// @brief Check if a mouse button is currently released.
    /// @param button Button index to check.
    /// @return 1 if released, 0 otherwise.
    int8_t rt_mouse_is_up(int64_t button);

    /// @brief Check if left mouse button is pressed.
    /// @return 1 if pressed, 0 otherwise.
    int8_t rt_mouse_left(void);

    /// @brief Check if right mouse button is pressed.
    /// @return 1 if pressed, 0 otherwise.
    int8_t rt_mouse_right(void);

    /// @brief Check if middle mouse button is pressed.
    /// @return 1 if pressed, 0 otherwise.
    int8_t rt_mouse_middle(void);

    //=========================================================================
    // Button Events (Since Last Poll)
    //=========================================================================

    /// @brief Check if a button was pressed this frame.
    /// @param button Button index to check.
    /// @return 1 if pressed this frame, 0 otherwise.
    int8_t rt_mouse_was_pressed(int64_t button);

    /// @brief Check if a button was released this frame.
    /// @param button Button index to check.
    /// @return 1 if released this frame, 0 otherwise.
    int8_t rt_mouse_was_released(int64_t button);

    /// @brief Check if a button was clicked this frame.
    /// @param button Button index to check.
    /// @return 1 if clicked (pressed and released quickly), 0 otherwise.
    int8_t rt_mouse_was_clicked(int64_t button);

    /// @brief Check if a button was double-clicked this frame.
    /// @param button Button index to check.
    /// @return 1 if double-clicked, 0 otherwise.
    int8_t rt_mouse_was_double_clicked(int64_t button);

    //=========================================================================
    // Scroll Wheel
    //=========================================================================

    /// @brief Get horizontal scroll delta.
    /// @return Horizontal scroll amount since last poll.
    int64_t rt_mouse_wheel_x(void);

    /// @brief Get vertical scroll delta.
    /// @return Vertical scroll amount (positive = up) since last poll.
    int64_t rt_mouse_wheel_y(void);

    //=========================================================================
    // Cursor Control
    //=========================================================================

    /// @brief Show the system cursor.
    void rt_mouse_show(void);

    /// @brief Hide the system cursor.
    void rt_mouse_hide(void);

    /// @brief Check if cursor is hidden.
    /// @return 1 if hidden, 0 otherwise.
    int8_t rt_mouse_is_hidden(void);

    /// @brief Capture the mouse to the window.
    /// @details For FPS-style games, confines cursor to window.
    void rt_mouse_capture(void);

    /// @brief Release mouse capture.
    void rt_mouse_release(void);

    /// @brief Check if mouse is captured.
    /// @return 1 if captured, 0 otherwise.
    int8_t rt_mouse_is_captured(void);

    /// @brief Warp cursor to a specific position.
    /// @param x Target X coordinate.
    /// @param y Target Y coordinate.
    void rt_mouse_set_pos(int64_t x, int64_t y);

    //=========================================================================
    // Button Constant Getters
    //=========================================================================

    int64_t rt_mouse_button_left(void);
    int64_t rt_mouse_button_right(void);
    int64_t rt_mouse_button_middle(void);
    int64_t rt_mouse_button_x1(void);
    int64_t rt_mouse_button_x2(void);

    //=========================================================================
    // Gamepad/Controller Input
    //=========================================================================

    //=========================================================================
    // Gamepad Button Constants (Standard Gamepad Layout)
    //=========================================================================

#define VIPER_PAD_A 0      // Xbox A / PlayStation Cross
#define VIPER_PAD_B 1      // Xbox B / PlayStation Circle
#define VIPER_PAD_X 2      // Xbox X / PlayStation Square
#define VIPER_PAD_Y 3      // Xbox Y / PlayStation Triangle
#define VIPER_PAD_LB 4     // Left bumper/shoulder
#define VIPER_PAD_RB 5     // Right bumper/shoulder
#define VIPER_PAD_BACK 6   // Back/Select/Share
#define VIPER_PAD_START 7  // Start/Options
#define VIPER_PAD_LSTICK 8 // Left stick click
#define VIPER_PAD_RSTICK 9 // Right stick click
#define VIPER_PAD_UP 10    // D-pad up
#define VIPER_PAD_DOWN 11  // D-pad down
#define VIPER_PAD_LEFT 12  // D-pad left
#define VIPER_PAD_RIGHT 13 // D-pad right
#define VIPER_PAD_GUIDE 14 // Xbox button / PlayStation button
#define VIPER_PAD_BUTTON_MAX 15

// Maximum number of supported controllers
#define VIPER_PAD_MAX 4

    //=========================================================================
    // Gamepad State Management
    //=========================================================================

    /// @brief Initialize the gamepad input system.
    /// @details Called internally when Canvas is created.
    void rt_pad_init(void);

    /// @brief Reset gamepad state for new frame.
    /// @details Called by Canvas.Poll() to clear pressed/released lists.
    void rt_pad_begin_frame(void);

    /// @brief Poll connected gamepads and update state.
    /// @details Should be called each frame to detect hot-plug events.
    void rt_pad_poll(void);

    //=========================================================================
    // Controller Enumeration
    //=========================================================================

    /// @brief Get number of connected controllers.
    /// @return Number of connected controllers (0-4).
    int64_t rt_pad_count(void);

    /// @brief Check if a controller is connected.
    /// @param index Controller index (0-3).
    /// @return 1 if connected, 0 otherwise.
    int8_t rt_pad_is_connected(int64_t index);

    /// @brief Get controller name/description.
    /// @param index Controller index (0-3).
    /// @return Controller name string, or empty string if not connected.
    rt_string rt_pad_name(int64_t index);

    //=========================================================================
    // Button State (Polling)
    //=========================================================================

    /// @brief Check if a button is currently pressed.
    /// @param index Controller index (0-3).
    /// @param button Button constant (VIPER_PAD_*).
    /// @return 1 if pressed, 0 otherwise.
    int8_t rt_pad_is_down(int64_t index, int64_t button);

    /// @brief Check if a button is currently released.
    /// @param index Controller index (0-3).
    /// @param button Button constant (VIPER_PAD_*).
    /// @return 1 if released, 0 otherwise.
    int8_t rt_pad_is_up(int64_t index, int64_t button);

    //=========================================================================
    // Button Events (Since Last Poll)
    //=========================================================================

    /// @brief Check if a button was pressed this frame.
    /// @param index Controller index (0-3).
    /// @param button Button constant.
    /// @return 1 if pressed this frame, 0 otherwise.
    int8_t rt_pad_was_pressed(int64_t index, int64_t button);

    /// @brief Check if a button was released this frame.
    /// @param index Controller index (0-3).
    /// @param button Button constant.
    /// @return 1 if released this frame, 0 otherwise.
    int8_t rt_pad_was_released(int64_t index, int64_t button);

    //=========================================================================
    // Analog Inputs
    //=========================================================================

    /// @brief Get left stick X axis value.
    /// @param index Controller index (0-3).
    /// @return Value from -1.0 to 1.0 (left to right).
    double rt_pad_left_x(int64_t index);

    /// @brief Get left stick Y axis value.
    /// @param index Controller index (0-3).
    /// @return Value from -1.0 to 1.0 (up to down).
    double rt_pad_left_y(int64_t index);

    /// @brief Get right stick X axis value.
    /// @param index Controller index (0-3).
    /// @return Value from -1.0 to 1.0 (left to right).
    double rt_pad_right_x(int64_t index);

    /// @brief Get right stick Y axis value.
    /// @param index Controller index (0-3).
    /// @return Value from -1.0 to 1.0 (up to down).
    double rt_pad_right_y(int64_t index);

    /// @brief Get left trigger value.
    /// @param index Controller index (0-3).
    /// @return Value from 0.0 to 1.0 (released to fully pressed).
    double rt_pad_left_trigger(int64_t index);

    /// @brief Get right trigger value.
    /// @param index Controller index (0-3).
    /// @return Value from 0.0 to 1.0 (released to fully pressed).
    double rt_pad_right_trigger(int64_t index);

    //=========================================================================
    // Deadzone Handling
    //=========================================================================

    /// @brief Set stick deadzone radius.
    /// @param radius Deadzone radius (0.0 to 1.0, default 0.1).
    void rt_pad_set_deadzone(double radius);

    /// @brief Get current deadzone radius.
    /// @return Current deadzone radius.
    double rt_pad_get_deadzone(void);

    //=========================================================================
    // Vibration/Rumble
    //=========================================================================

    /// @brief Set controller vibration.
    /// @param index Controller index (0-3).
    /// @param left_motor Left motor intensity (0.0 to 1.0).
    /// @param right_motor Right motor intensity (0.0 to 1.0).
    void rt_pad_vibrate(int64_t index, double left_motor, double right_motor);

    /// @brief Stop controller vibration.
    /// @param index Controller index (0-3).
    void rt_pad_stop_vibration(int64_t index);

    //=========================================================================
    // Button Constant Getters (for runtime.def)
    //=========================================================================

    int64_t rt_pad_button_a(void);
    int64_t rt_pad_button_b(void);
    int64_t rt_pad_button_x(void);
    int64_t rt_pad_button_y(void);
    int64_t rt_pad_button_lb(void);
    int64_t rt_pad_button_rb(void);
    int64_t rt_pad_button_back(void);
    int64_t rt_pad_button_start(void);
    int64_t rt_pad_button_lstick(void);
    int64_t rt_pad_button_rstick(void);
    int64_t rt_pad_button_up(void);
    int64_t rt_pad_button_down(void);
    int64_t rt_pad_button_left(void);
    int64_t rt_pad_button_right(void);
    int64_t rt_pad_button_guide(void);

#ifdef __cplusplus
}
#endif
