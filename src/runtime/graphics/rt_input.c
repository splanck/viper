//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_input.c
// Purpose: Keyboard and mouse input state manager for Viper games. Buffers the
//   platform window's raw key/mouse events between frames and exposes a
//   snapshot API (IsDown, WasPressed, WasReleased, WasClicked) that is stable
//   for the entire duration of a frame update. Callers poll state once per
//   frame after rt_input_begin_frame() and before rt_input_end_frame().
//
// Key invariants:
//   - State is double-buffered: rt_input_begin_frame() captures the current
//     event queue into the "current frame" snapshot. WasPressed/WasReleased
//     compare current and previous snapshots (edge detection). IsDown reflects
//     the current snapshot (level detection).
//   - Key codes use the platform's native integer key codes (forwarded from
//     the windowing backend). There is no re-mapping layer here.
//   - Mouse button indices: 1 = left, 2 = right, 3 = middle (matches SDL/X11
//     conventions). WasClicked is a shorthand for WasPressed && WasReleased
//     in the same frame (single-frame tap detection for quick presses).
//   - Mouse position (X, Y) is in canvas-pixel coordinates (top-left origin,
//     +Y downward), already scaled by the HiDPI scale factor so callers always
//     work in logical canvas pixels.
//   - All state is stored in a GC-managed input context object; there is one
//     context per Canvas window.
//
// Ownership/Lifetime:
//   - Input context objects are GC-managed (rt_obj_new_i64). They are created
//     by rt_graphics.c alongside the Canvas and freed by the GC finalizer.
//
// Links: src/runtime/graphics/rt_input.h (public API),
//        src/runtime/graphics/rt_graphics.c (Canvas event pump integration)
//
//===----------------------------------------------------------------------===//

#include "rt_input.h"
#include "rt_box.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Key Code Mapping (GLFW <-> vgfx)
//=============================================================================

// vgfx key codes from vgfx.h
#define VGFX_KEY_UNKNOWN 0
#define VGFX_KEY_SPACE ' '
#define VGFX_KEY_0 '0'
#define VGFX_KEY_A 'A'
#define VGFX_KEY_ESCAPE_VG 256
#define VGFX_KEY_ENTER_VG 257
#define VGFX_KEY_LEFT_VG 258
#define VGFX_KEY_RIGHT_VG 259
#define VGFX_KEY_UP_VG 260
#define VGFX_KEY_DOWN_VG 261

/// @brief Convert vgfx key code to GLFW-style key code.
static int64_t vgfx_to_glfw(int64_t vgfx_key)
{
    // Letters and numbers match directly (ASCII)
    if (vgfx_key >= 'A' && vgfx_key <= 'Z')
        return vgfx_key;
    if (vgfx_key >= '0' && vgfx_key <= '9')
        return vgfx_key;
    if (vgfx_key == VGFX_KEY_SPACE)
        return VIPER_KEY_SPACE;

    // Map special keys from vgfx to GLFW
    switch (vgfx_key)
    {
        case VGFX_KEY_ESCAPE_VG:
            return VIPER_KEY_ESCAPE;
        case VGFX_KEY_ENTER_VG:
            return VIPER_KEY_ENTER;
        case VGFX_KEY_LEFT_VG:
            return VIPER_KEY_LEFT;
        case VGFX_KEY_RIGHT_VG:
            return VIPER_KEY_RIGHT;
        case VGFX_KEY_UP_VG:
            return VIPER_KEY_UP;
        case VGFX_KEY_DOWN_VG:
            return VIPER_KEY_DOWN;
        default:
            return vgfx_key;
    }
}

//=============================================================================
// Keyboard State
//=============================================================================

// Current key state (true = pressed)
static bool g_key_state[VIPER_KEY_MAX];

// Keys pressed this frame
static int64_t g_pressed_keys[64];
static int g_pressed_count;

// Keys released this frame
static int64_t g_released_keys[64];
static int g_released_count;

// Text input buffer
static char g_text_buffer[256];
static int g_text_length;
static bool g_text_input_enabled;

// Caps lock state
static bool g_caps_lock;

// Active canvas for key state queries
static void *g_active_canvas;

// Track if initialized
static bool g_initialized;

//=============================================================================
// Initialization
//=============================================================================

void rt_keyboard_init(void)
{
    if (g_initialized)
        return;

    memset(g_key_state, 0, sizeof(g_key_state));
    g_pressed_count = 0;
    g_released_count = 0;
    g_text_length = 0;
    g_text_input_enabled = false;
    g_caps_lock = false;
    g_active_canvas = NULL;
    g_initialized = true;
}

void rt_keyboard_begin_frame(void)
{
    // Clear per-frame event lists
    g_pressed_count = 0;
    g_released_count = 0;
    g_text_length = 0;
}

void rt_keyboard_on_key_down(int64_t key)
{
    // Convert vgfx key to GLFW-style
    int64_t glfw_key = vgfx_to_glfw(key);

    if (glfw_key <= 0 || glfw_key >= VIPER_KEY_MAX)
        return;

    // Only record press if key wasn't already down
    if (!g_key_state[glfw_key])
    {
        g_key_state[glfw_key] = true;

        if (g_pressed_count < 64)
            g_pressed_keys[g_pressed_count++] = glfw_key;
    }

    // Track caps lock toggle
    // Note: caps lock state would need OS query for accurate tracking
}

void rt_keyboard_on_key_up(int64_t key)
{
    // Convert vgfx key to GLFW-style
    int64_t glfw_key = vgfx_to_glfw(key);

    if (glfw_key <= 0 || glfw_key >= VIPER_KEY_MAX)
        return;

    if (g_key_state[glfw_key])
    {
        g_key_state[glfw_key] = false;

        if (g_released_count < 64)
            g_released_keys[g_released_count++] = glfw_key;
    }
}

void rt_keyboard_text_input(int32_t ch)
{
    if (!g_text_input_enabled)
        return;

    // Simple UTF-8 encoding for ASCII characters
    // Full UTF-8 would require more complex handling
    if (ch >= 32 && ch < 127 && g_text_length < 255)
    {
        g_text_buffer[g_text_length++] = (char)ch;
    }
}

void rt_keyboard_set_canvas(void *canvas)
{
    g_active_canvas = canvas;
    if (canvas)
        rt_keyboard_init();
}

//=============================================================================
// Polling Methods
//=============================================================================

int8_t rt_keyboard_is_down(int64_t key)
{
    if (key <= 0 || key >= VIPER_KEY_MAX)
        return 0;

    return g_key_state[key] ? 1 : 0;
}

int8_t rt_keyboard_is_up(int64_t key)
{
    if (key <= 0 || key >= VIPER_KEY_MAX)
        return 1;

    return g_key_state[key] ? 0 : 1;
}

int8_t rt_keyboard_any_down(void)
{
    for (int i = 0; i < VIPER_KEY_MAX; i++)
    {
        if (g_key_state[i])
            return 1;
    }
    return 0;
}

int64_t rt_keyboard_get_down(void)
{
    for (int i = 0; i < VIPER_KEY_MAX; i++)
    {
        if (g_key_state[i])
            return (int64_t)i;
    }
    return 0;
}

//=============================================================================
// Event Methods
//=============================================================================

int8_t rt_keyboard_was_pressed(int64_t key)
{
    for (int i = 0; i < g_pressed_count; i++)
    {
        if (g_pressed_keys[i] == key)
            return 1;
    }
    return 0;
}

int8_t rt_keyboard_was_released(int64_t key)
{
    for (int i = 0; i < g_released_count; i++)
    {
        if (g_released_keys[i] == key)
            return 1;
    }
    return 0;
}

void *rt_keyboard_get_pressed(void)
{
    void *seq = rt_seq_new();
    for (int i = 0; i < g_pressed_count; i++)
    {
        rt_seq_push(seq, rt_box_i64(g_pressed_keys[i]));
    }
    return seq;
}

void *rt_keyboard_get_released(void)
{
    void *seq = rt_seq_new();
    for (int i = 0; i < g_released_count; i++)
    {
        rt_seq_push(seq, rt_box_i64(g_released_keys[i]));
    }
    return seq;
}

//=============================================================================
// Text Input
//=============================================================================

rt_string rt_keyboard_get_text(void)
{
    if (g_text_length == 0)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(g_text_buffer, g_text_length);
}

void rt_keyboard_enable_text_input(void)
{
    g_text_input_enabled = true;
}

void rt_keyboard_disable_text_input(void)
{
    g_text_input_enabled = false;
}

//=============================================================================
// Modifier State
//=============================================================================

int8_t rt_keyboard_shift(void)
{
    return (g_key_state[VIPER_KEY_LSHIFT] || g_key_state[VIPER_KEY_RSHIFT]) ? 1 : 0;
}

int8_t rt_keyboard_ctrl(void)
{
    return (g_key_state[VIPER_KEY_LCTRL] || g_key_state[VIPER_KEY_RCTRL]) ? 1 : 0;
}

int8_t rt_keyboard_alt(void)
{
    return (g_key_state[VIPER_KEY_LALT] || g_key_state[VIPER_KEY_RALT]) ? 1 : 0;
}

int8_t rt_keyboard_caps_lock(void)
{
    return g_caps_lock ? 1 : 0;
}

//=============================================================================
// Key Name Helper
//=============================================================================

rt_string rt_keyboard_key_name(int64_t key)
{
    const char *name = NULL;

    // Letters
    if (key >= VIPER_KEY_A && key <= VIPER_KEY_Z)
    {
        static char letter[2] = {0, 0};
        letter[0] = (char)key;
        return rt_string_from_bytes(letter, 1);
    }

    // Numbers
    if (key >= VIPER_KEY_0 && key <= VIPER_KEY_9)
    {
        static char digit[2] = {0, 0};
        digit[0] = (char)key;
        return rt_string_from_bytes(digit, 1);
    }

    // Special keys
    switch (key)
    {
        case VIPER_KEY_UNKNOWN:
            name = "Unknown";
            break;
        case VIPER_KEY_SPACE:
            name = "Space";
            break;
        case VIPER_KEY_ESCAPE:
            name = "Escape";
            break;
        case VIPER_KEY_ENTER:
            name = "Enter";
            break;
        case VIPER_KEY_TAB:
            name = "Tab";
            break;
        case VIPER_KEY_BACKSPACE:
            name = "Backspace";
            break;
        case VIPER_KEY_INSERT:
            name = "Insert";
            break;
        case VIPER_KEY_DELETE:
            name = "Delete";
            break;
        case VIPER_KEY_RIGHT:
            name = "Right";
            break;
        case VIPER_KEY_LEFT:
            name = "Left";
            break;
        case VIPER_KEY_DOWN:
            name = "Down";
            break;
        case VIPER_KEY_UP:
            name = "Up";
            break;
        case VIPER_KEY_PAGEUP:
            name = "PageUp";
            break;
        case VIPER_KEY_PAGEDOWN:
            name = "PageDown";
            break;
        case VIPER_KEY_HOME:
            name = "Home";
            break;
        case VIPER_KEY_END:
            name = "End";
            break;
        case VIPER_KEY_F1:
            name = "F1";
            break;
        case VIPER_KEY_F2:
            name = "F2";
            break;
        case VIPER_KEY_F3:
            name = "F3";
            break;
        case VIPER_KEY_F4:
            name = "F4";
            break;
        case VIPER_KEY_F5:
            name = "F5";
            break;
        case VIPER_KEY_F6:
            name = "F6";
            break;
        case VIPER_KEY_F7:
            name = "F7";
            break;
        case VIPER_KEY_F8:
            name = "F8";
            break;
        case VIPER_KEY_F9:
            name = "F9";
            break;
        case VIPER_KEY_F10:
            name = "F10";
            break;
        case VIPER_KEY_F11:
            name = "F11";
            break;
        case VIPER_KEY_F12:
            name = "F12";
            break;
        case VIPER_KEY_LSHIFT:
            name = "Left Shift";
            break;
        case VIPER_KEY_RSHIFT:
            name = "Right Shift";
            break;
        case VIPER_KEY_LCTRL:
            name = "Left Ctrl";
            break;
        case VIPER_KEY_RCTRL:
            name = "Right Ctrl";
            break;
        case VIPER_KEY_LALT:
            name = "Left Alt";
            break;
        case VIPER_KEY_RALT:
            name = "Right Alt";
            break;
        case VIPER_KEY_MINUS:
            name = "Minus";
            break;
        case VIPER_KEY_EQUALS:
            name = "Equals";
            break;
        case VIPER_KEY_LBRACKET:
            name = "Left Bracket";
            break;
        case VIPER_KEY_RBRACKET:
            name = "Right Bracket";
            break;
        case VIPER_KEY_BACKSLASH:
            name = "Backslash";
            break;
        case VIPER_KEY_SEMICOLON:
            name = "Semicolon";
            break;
        case VIPER_KEY_QUOTE:
            name = "Quote";
            break;
        case VIPER_KEY_GRAVE:
            name = "Grave";
            break;
        case VIPER_KEY_COMMA:
            name = "Comma";
            break;
        case VIPER_KEY_PERIOD:
            name = "Period";
            break;
        case VIPER_KEY_SLASH:
            name = "Slash";
            break;
        case VIPER_KEY_NUM0:
            name = "Numpad 0";
            break;
        case VIPER_KEY_NUM1:
            name = "Numpad 1";
            break;
        case VIPER_KEY_NUM2:
            name = "Numpad 2";
            break;
        case VIPER_KEY_NUM3:
            name = "Numpad 3";
            break;
        case VIPER_KEY_NUM4:
            name = "Numpad 4";
            break;
        case VIPER_KEY_NUM5:
            name = "Numpad 5";
            break;
        case VIPER_KEY_NUM6:
            name = "Numpad 6";
            break;
        case VIPER_KEY_NUM7:
            name = "Numpad 7";
            break;
        case VIPER_KEY_NUM8:
            name = "Numpad 8";
            break;
        case VIPER_KEY_NUM9:
            name = "Numpad 9";
            break;
        case VIPER_KEY_NUMADD:
            name = "Numpad Add";
            break;
        case VIPER_KEY_NUMSUB:
            name = "Numpad Subtract";
            break;
        case VIPER_KEY_NUMMUL:
            name = "Numpad Multiply";
            break;
        case VIPER_KEY_NUMDIV:
            name = "Numpad Divide";
            break;
        case VIPER_KEY_NUMENTER:
            name = "Numpad Enter";
            break;
        case VIPER_KEY_NUMDOT:
            name = "Numpad Decimal";
            break;
        default:
            name = "Unknown";
            break;
    }

    return rt_string_from_bytes(name, strlen(name));
}

//=============================================================================
// Key Code Constant Getters
//=============================================================================

int64_t rt_keyboard_key_unknown(void)
{
    return VIPER_KEY_UNKNOWN;
}

int64_t rt_keyboard_key_a(void)
{
    return VIPER_KEY_A;
}

int64_t rt_keyboard_key_b(void)
{
    return VIPER_KEY_B;
}

int64_t rt_keyboard_key_c(void)
{
    return VIPER_KEY_C;
}

int64_t rt_keyboard_key_d(void)
{
    return VIPER_KEY_D;
}

int64_t rt_keyboard_key_e(void)
{
    return VIPER_KEY_E;
}

int64_t rt_keyboard_key_f(void)
{
    return VIPER_KEY_F;
}

int64_t rt_keyboard_key_g(void)
{
    return VIPER_KEY_G;
}

int64_t rt_keyboard_key_h(void)
{
    return VIPER_KEY_H;
}

int64_t rt_keyboard_key_i(void)
{
    return VIPER_KEY_I;
}

int64_t rt_keyboard_key_j(void)
{
    return VIPER_KEY_J;
}

int64_t rt_keyboard_key_k(void)
{
    return VIPER_KEY_K;
}

int64_t rt_keyboard_key_l(void)
{
    return VIPER_KEY_L;
}

int64_t rt_keyboard_key_m(void)
{
    return VIPER_KEY_M;
}

int64_t rt_keyboard_key_n(void)
{
    return VIPER_KEY_N;
}

int64_t rt_keyboard_key_o(void)
{
    return VIPER_KEY_O;
}

int64_t rt_keyboard_key_p(void)
{
    return VIPER_KEY_P;
}

int64_t rt_keyboard_key_q(void)
{
    return VIPER_KEY_Q;
}

int64_t rt_keyboard_key_r(void)
{
    return VIPER_KEY_R;
}

int64_t rt_keyboard_key_s(void)
{
    return VIPER_KEY_S;
}

int64_t rt_keyboard_key_t(void)
{
    return VIPER_KEY_T;
}

int64_t rt_keyboard_key_u(void)
{
    return VIPER_KEY_U;
}

int64_t rt_keyboard_key_v(void)
{
    return VIPER_KEY_V;
}

int64_t rt_keyboard_key_w(void)
{
    return VIPER_KEY_W;
}

int64_t rt_keyboard_key_x(void)
{
    return VIPER_KEY_X;
}

int64_t rt_keyboard_key_y(void)
{
    return VIPER_KEY_Y;
}

int64_t rt_keyboard_key_z(void)
{
    return VIPER_KEY_Z;
}

int64_t rt_keyboard_key_0(void)
{
    return VIPER_KEY_0;
}

int64_t rt_keyboard_key_1(void)
{
    return VIPER_KEY_1;
}

int64_t rt_keyboard_key_2(void)
{
    return VIPER_KEY_2;
}

int64_t rt_keyboard_key_3(void)
{
    return VIPER_KEY_3;
}

int64_t rt_keyboard_key_4(void)
{
    return VIPER_KEY_4;
}

int64_t rt_keyboard_key_5(void)
{
    return VIPER_KEY_5;
}

int64_t rt_keyboard_key_6(void)
{
    return VIPER_KEY_6;
}

int64_t rt_keyboard_key_7(void)
{
    return VIPER_KEY_7;
}

int64_t rt_keyboard_key_8(void)
{
    return VIPER_KEY_8;
}

int64_t rt_keyboard_key_9(void)
{
    return VIPER_KEY_9;
}

int64_t rt_keyboard_key_f1(void)
{
    return VIPER_KEY_F1;
}

int64_t rt_keyboard_key_f2(void)
{
    return VIPER_KEY_F2;
}

int64_t rt_keyboard_key_f3(void)
{
    return VIPER_KEY_F3;
}

int64_t rt_keyboard_key_f4(void)
{
    return VIPER_KEY_F4;
}

int64_t rt_keyboard_key_f5(void)
{
    return VIPER_KEY_F5;
}

int64_t rt_keyboard_key_f6(void)
{
    return VIPER_KEY_F6;
}

int64_t rt_keyboard_key_f7(void)
{
    return VIPER_KEY_F7;
}

int64_t rt_keyboard_key_f8(void)
{
    return VIPER_KEY_F8;
}

int64_t rt_keyboard_key_f9(void)
{
    return VIPER_KEY_F9;
}

int64_t rt_keyboard_key_f10(void)
{
    return VIPER_KEY_F10;
}

int64_t rt_keyboard_key_f11(void)
{
    return VIPER_KEY_F11;
}

int64_t rt_keyboard_key_f12(void)
{
    return VIPER_KEY_F12;
}

int64_t rt_keyboard_key_up(void)
{
    return VIPER_KEY_UP;
}

int64_t rt_keyboard_key_down(void)
{
    return VIPER_KEY_DOWN;
}

int64_t rt_keyboard_key_left(void)
{
    return VIPER_KEY_LEFT;
}

int64_t rt_keyboard_key_right(void)
{
    return VIPER_KEY_RIGHT;
}

int64_t rt_keyboard_key_home(void)
{
    return VIPER_KEY_HOME;
}

int64_t rt_keyboard_key_end(void)
{
    return VIPER_KEY_END;
}

int64_t rt_keyboard_key_pageup(void)
{
    return VIPER_KEY_PAGEUP;
}

int64_t rt_keyboard_key_pagedown(void)
{
    return VIPER_KEY_PAGEDOWN;
}

int64_t rt_keyboard_key_insert(void)
{
    return VIPER_KEY_INSERT;
}

int64_t rt_keyboard_key_delete(void)
{
    return VIPER_KEY_DELETE;
}

int64_t rt_keyboard_key_backspace(void)
{
    return VIPER_KEY_BACKSPACE;
}

int64_t rt_keyboard_key_tab(void)
{
    return VIPER_KEY_TAB;
}

int64_t rt_keyboard_key_enter(void)
{
    return VIPER_KEY_ENTER;
}

int64_t rt_keyboard_key_space(void)
{
    return VIPER_KEY_SPACE;
}

int64_t rt_keyboard_key_escape(void)
{
    return VIPER_KEY_ESCAPE;
}

int64_t rt_keyboard_key_shift(void)
{
    return VIPER_KEY_SHIFT;
}

int64_t rt_keyboard_key_ctrl(void)
{
    return VIPER_KEY_CTRL;
}

int64_t rt_keyboard_key_alt(void)
{
    return VIPER_KEY_ALT;
}

int64_t rt_keyboard_key_lshift(void)
{
    return VIPER_KEY_LSHIFT;
}

int64_t rt_keyboard_key_rshift(void)
{
    return VIPER_KEY_RSHIFT;
}

int64_t rt_keyboard_key_lctrl(void)
{
    return VIPER_KEY_LCTRL;
}

int64_t rt_keyboard_key_rctrl(void)
{
    return VIPER_KEY_RCTRL;
}

int64_t rt_keyboard_key_lalt(void)
{
    return VIPER_KEY_LALT;
}

int64_t rt_keyboard_key_ralt(void)
{
    return VIPER_KEY_RALT;
}

int64_t rt_keyboard_key_minus(void)
{
    return VIPER_KEY_MINUS;
}

int64_t rt_keyboard_key_equals(void)
{
    return VIPER_KEY_EQUALS;
}

int64_t rt_keyboard_key_lbracket(void)
{
    return VIPER_KEY_LBRACKET;
}

int64_t rt_keyboard_key_rbracket(void)
{
    return VIPER_KEY_RBRACKET;
}

int64_t rt_keyboard_key_backslash(void)
{
    return VIPER_KEY_BACKSLASH;
}

int64_t rt_keyboard_key_semicolon(void)
{
    return VIPER_KEY_SEMICOLON;
}

int64_t rt_keyboard_key_quote(void)
{
    return VIPER_KEY_QUOTE;
}

int64_t rt_keyboard_key_grave(void)
{
    return VIPER_KEY_GRAVE;
}

int64_t rt_keyboard_key_comma(void)
{
    return VIPER_KEY_COMMA;
}

int64_t rt_keyboard_key_period(void)
{
    return VIPER_KEY_PERIOD;
}

int64_t rt_keyboard_key_slash(void)
{
    return VIPER_KEY_SLASH;
}

int64_t rt_keyboard_key_num0(void)
{
    return VIPER_KEY_NUM0;
}

int64_t rt_keyboard_key_num1(void)
{
    return VIPER_KEY_NUM1;
}

int64_t rt_keyboard_key_num2(void)
{
    return VIPER_KEY_NUM2;
}

int64_t rt_keyboard_key_num3(void)
{
    return VIPER_KEY_NUM3;
}

int64_t rt_keyboard_key_num4(void)
{
    return VIPER_KEY_NUM4;
}

int64_t rt_keyboard_key_num5(void)
{
    return VIPER_KEY_NUM5;
}

int64_t rt_keyboard_key_num6(void)
{
    return VIPER_KEY_NUM6;
}

int64_t rt_keyboard_key_num7(void)
{
    return VIPER_KEY_NUM7;
}

int64_t rt_keyboard_key_num8(void)
{
    return VIPER_KEY_NUM8;
}

int64_t rt_keyboard_key_num9(void)
{
    return VIPER_KEY_NUM9;
}

int64_t rt_keyboard_key_numadd(void)
{
    return VIPER_KEY_NUMADD;
}

int64_t rt_keyboard_key_numsub(void)
{
    return VIPER_KEY_NUMSUB;
}

int64_t rt_keyboard_key_nummul(void)
{
    return VIPER_KEY_NUMMUL;
}

int64_t rt_keyboard_key_numdiv(void)
{
    return VIPER_KEY_NUMDIV;
}

int64_t rt_keyboard_key_numenter(void)
{
    return VIPER_KEY_NUMENTER;
}

int64_t rt_keyboard_key_numdot(void)
{
    return VIPER_KEY_NUMDOT;
}

//=============================================================================
// Mouse Input Implementation
//=============================================================================

// Mouse state
static int64_t g_mouse_x = 0;
static int64_t g_mouse_y = 0;
static int64_t g_mouse_prev_x = 0;
static int64_t g_mouse_prev_y = 0;
static int64_t g_mouse_delta_x = 0;
static int64_t g_mouse_delta_y = 0;
static int64_t g_mouse_wheel_x = 0;
static int64_t g_mouse_wheel_y = 0;

// Button state arrays
static bool g_mouse_button_state[VIPER_MOUSE_BUTTON_MAX];
static bool g_mouse_button_pressed[VIPER_MOUSE_BUTTON_MAX];
static bool g_mouse_button_released[VIPER_MOUSE_BUTTON_MAX];

// Click detection - track press times for each button
static int64_t g_mouse_press_time[VIPER_MOUSE_BUTTON_MAX];
static int64_t g_mouse_last_click_time[VIPER_MOUSE_BUTTON_MAX];
static bool g_mouse_clicked[VIPER_MOUSE_BUTTON_MAX];
static bool g_mouse_double_clicked[VIPER_MOUSE_BUTTON_MAX];

// Click detection constants (in milliseconds)
#define CLICK_MAX_DURATION_MS 300
#define DOUBLE_CLICK_MAX_INTERVAL_MS 400

// Cursor state
static bool g_mouse_hidden = false;
static bool g_mouse_captured = false;

// Active canvas
static void *g_mouse_canvas = NULL;

// Initialization flag
static bool g_mouse_initialized = false;

// Simple timestamp function
static int64_t get_time_ms(void)
{
    // Use a simple approach - this could be improved with actual time functions
    static int64_t counter = 0;
    return ++counter; // For now, just increment - real impl would use clock
}

void rt_mouse_init(void)
{
    if (g_mouse_initialized)
        return;

    g_mouse_x = 0;
    g_mouse_y = 0;
    g_mouse_prev_x = 0;
    g_mouse_prev_y = 0;
    g_mouse_delta_x = 0;
    g_mouse_delta_y = 0;
    g_mouse_wheel_x = 0;
    g_mouse_wheel_y = 0;
    g_mouse_hidden = false;
    g_mouse_captured = false;
    g_mouse_canvas = NULL;

    for (int i = 0; i < VIPER_MOUSE_BUTTON_MAX; i++)
    {
        g_mouse_button_state[i] = false;
        g_mouse_button_pressed[i] = false;
        g_mouse_button_released[i] = false;
        g_mouse_press_time[i] = 0;
        g_mouse_last_click_time[i] = 0;
        g_mouse_clicked[i] = false;
        g_mouse_double_clicked[i] = false;
    }

    g_mouse_initialized = true;
}

void rt_mouse_begin_frame(void)
{
    // Calculate delta from previous position
    g_mouse_delta_x = g_mouse_x - g_mouse_prev_x;
    g_mouse_delta_y = g_mouse_y - g_mouse_prev_y;
    g_mouse_prev_x = g_mouse_x;
    g_mouse_prev_y = g_mouse_y;

    // Reset per-frame event arrays
    for (int i = 0; i < VIPER_MOUSE_BUTTON_MAX; i++)
    {
        g_mouse_button_pressed[i] = false;
        g_mouse_button_released[i] = false;
        g_mouse_clicked[i] = false;
        g_mouse_double_clicked[i] = false;
    }

    // Reset wheel deltas
    g_mouse_wheel_x = 0;
    g_mouse_wheel_y = 0;
}

void rt_mouse_update_pos(int64_t x, int64_t y)
{
    g_mouse_x = x;
    g_mouse_y = y;
}

void rt_mouse_button_down(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return;

    if (!g_mouse_button_state[button])
    {
        g_mouse_button_state[button] = true;
        g_mouse_button_pressed[button] = true;
        g_mouse_press_time[button] = get_time_ms();
    }
}

void rt_mouse_button_up(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return;

    if (g_mouse_button_state[button])
    {
        g_mouse_button_state[button] = false;
        g_mouse_button_released[button] = true;

        // Check for click (quick press and release)
        int64_t now = get_time_ms();
        int64_t press_duration = now - g_mouse_press_time[button];
        if (press_duration <= CLICK_MAX_DURATION_MS)
        {
            g_mouse_clicked[button] = true;

            // Check for double-click
            int64_t since_last_click = now - g_mouse_last_click_time[button];
            if (since_last_click <= DOUBLE_CLICK_MAX_INTERVAL_MS)
            {
                g_mouse_double_clicked[button] = true;
            }
            g_mouse_last_click_time[button] = now;
        }
    }
}

void rt_mouse_update_wheel(int64_t dx, int64_t dy)
{
    g_mouse_wheel_x += dx;
    g_mouse_wheel_y += dy;
}

void rt_mouse_set_canvas(void *canvas)
{
    g_mouse_canvas = canvas;
    if (canvas)
        rt_mouse_init();
}

//=============================================================================
// Position Methods
//=============================================================================

int64_t rt_mouse_x(void)
{
    return g_mouse_x;
}

int64_t rt_mouse_y(void)
{
    return g_mouse_y;
}

int64_t rt_mouse_delta_x(void)
{
    return g_mouse_delta_x;
}

int64_t rt_mouse_delta_y(void)
{
    return g_mouse_delta_y;
}

//=============================================================================
// Button State (Polling)
//=============================================================================

int8_t rt_mouse_is_down(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_button_state[button] ? 1 : 0;
}

int8_t rt_mouse_is_up(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 1;
    return g_mouse_button_state[button] ? 0 : 1;
}

int8_t rt_mouse_left(void)
{
    return rt_mouse_is_down(VIPER_MOUSE_BUTTON_LEFT);
}

int8_t rt_mouse_right(void)
{
    return rt_mouse_is_down(VIPER_MOUSE_BUTTON_RIGHT);
}

int8_t rt_mouse_middle(void)
{
    return rt_mouse_is_down(VIPER_MOUSE_BUTTON_MIDDLE);
}

//=============================================================================
// Button Events (Since Last Poll)
//=============================================================================

int8_t rt_mouse_was_pressed(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_button_pressed[button] ? 1 : 0;
}

int8_t rt_mouse_was_released(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_button_released[button] ? 1 : 0;
}

int8_t rt_mouse_was_clicked(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_clicked[button] ? 1 : 0;
}

int8_t rt_mouse_was_double_clicked(int64_t button)
{
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_double_clicked[button] ? 1 : 0;
}

//=============================================================================
// Scroll Wheel
//=============================================================================

int64_t rt_mouse_wheel_x(void)
{
    return g_mouse_wheel_x;
}

int64_t rt_mouse_wheel_y(void)
{
    return g_mouse_wheel_y;
}

//=============================================================================
// Cursor Control
//=============================================================================

void rt_mouse_show(void)
{
    g_mouse_hidden = false;
    // Platform-specific cursor show would go here
    // vgfx doesn't currently have cursor hide/show API
}

void rt_mouse_hide(void)
{
    g_mouse_hidden = true;
    // Platform-specific cursor hide would go here
}

int8_t rt_mouse_is_hidden(void)
{
    return g_mouse_hidden ? 1 : 0;
}

void rt_mouse_capture(void)
{
    g_mouse_captured = true;
    // Platform-specific mouse capture would go here
}

void rt_mouse_release(void)
{
    g_mouse_captured = false;
    // Platform-specific mouse release would go here
}

int8_t rt_mouse_is_captured(void)
{
    return g_mouse_captured ? 1 : 0;
}

void rt_mouse_set_pos(int64_t x, int64_t y)
{
    g_mouse_x = x;
    g_mouse_y = y;
    // Platform-specific cursor warp would go here
}

//=============================================================================
// Button Constant Getters
//=============================================================================

int64_t rt_mouse_button_left(void)
{
    return VIPER_MOUSE_BUTTON_LEFT;
}

int64_t rt_mouse_button_right(void)
{
    return VIPER_MOUSE_BUTTON_RIGHT;
}

int64_t rt_mouse_button_middle(void)
{
    return VIPER_MOUSE_BUTTON_MIDDLE;
}

int64_t rt_mouse_button_x1(void)
{
    return VIPER_MOUSE_BUTTON_X1;
}

int64_t rt_mouse_button_x2(void)
{
    return VIPER_MOUSE_BUTTON_X2;
}
