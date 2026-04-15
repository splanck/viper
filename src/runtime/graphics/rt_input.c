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
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_time.h"

#if RT_PLATFORM_WINDOWS
#include <windows.h>
#elif RT_PLATFORM_MACOS
#include <ApplicationServices/ApplicationServices.h>
#elif RT_PLATFORM_LINUX && defined(VIPER_ENABLE_GRAPHICS)
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#endif

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
static int64_t vgfx_to_glfw(int64_t vgfx_key) {
    // Letters and numbers match directly (ASCII)
    if (vgfx_key >= 'A' && vgfx_key <= 'Z')
        return vgfx_key;
    if (vgfx_key >= '0' && vgfx_key <= '9')
        return vgfx_key;
    if (vgfx_key == VGFX_KEY_SPACE)
        return VIPER_KEY_SPACE;

    // Map special keys from vgfx to GLFW
    switch (vgfx_key) {
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

// Test hooks let runtime unit tests verify the platform bridge deterministically
// without requiring a real focused window or cursor warp.
typedef int32_t (*rt_caps_lock_query_hook_fn)(void *canvas);
typedef void (*rt_mouse_warp_hook_fn)(void *canvas, int64_t x, int64_t y);

static rt_caps_lock_query_hook_fn g_caps_lock_query_hook = NULL;
static rt_mouse_warp_hook_fn g_mouse_warp_hook = NULL;
static void *g_mouse_canvas;

static int32_t rt_input_query_caps_lock_platform(void);
static void rt_input_warp_mouse_platform(int64_t x, int64_t y);

/// @brief Install a caps-lock query test hook.
///
/// Replaces the platform-native caps-lock query (GetKeyState on Windows,
/// CGEventSourceFlagsState on macOS, XkbGetIndicatorState on Linux) with
/// a caller-supplied callback. Used by unit tests to drive caps-lock
/// state deterministically without a live window or OS keyboard.
///
/// Pass NULL to remove the hook and restore native behavior.
///
/// @param hook Test callback invoked with the active canvas pointer; it
///             should return non-zero when caps-lock is asserted. NULL
///             clears the hook.
void rt_input_set_caps_lock_query_hook(rt_caps_lock_query_hook_fn hook) {
    g_caps_lock_query_hook = hook;
}

/// @brief Install a mouse-warp test hook.
///
/// Replaces the platform-native cursor warp (`vgfx_warp_cursor`, which
/// calls `CGWarpMouseCursorPosition` / `SetCursorPos` / `XWarpPointer`)
/// with a caller-supplied callback. Lets unit tests verify that
/// `Mouse.SetPos` issued the expected warp without actually moving the
/// real cursor.
///
/// @param hook Test callback invoked with `(canvas, x, y)`. NULL clears
///             the hook.
void rt_input_set_mouse_warp_hook(rt_mouse_warp_hook_fn hook) {
    g_mouse_warp_hook = hook;
}

/// @brief Clear every test hook installed via the setters above.
///
/// Convenience used at the start (and especially the end) of a test case
/// so the next test sees a clean platform-default state. Idempotent:
/// safe to call when no hooks are currently installed.
void rt_input_reset_test_hooks(void) {
    g_caps_lock_query_hook = NULL;
    g_mouse_warp_hook = NULL;
}

/// @brief Resolve the current caps-lock state via the OS or the
///        installed test hook.
///
/// If a `caps_lock_query_hook` has been installed via
/// `rt_input_set_caps_lock_query_hook`, defers to it (passing the active
/// canvas). Otherwise queries the platform:
///   - Windows: `GetKeyState(VK_CAPITAL) & 0x0001`
///   - macOS:   `CGEventSourceFlagsState` and the alpha-shift bit
///   - Linux:   `XkbGetIndicatorState` on the X11 display, opening one
///              transiently if no canvas display is available
///
/// On other platforms or when X11 is not available, falls back to the
/// last toggle observed via key events (`g_caps_lock`).
///
/// @return `1` if caps-lock is currently asserted, `0` otherwise.
static int32_t rt_input_query_caps_lock_platform(void) {
    if (g_caps_lock_query_hook)
        return g_caps_lock_query_hook(g_active_canvas) ? 1 : 0;

#if RT_PLATFORM_WINDOWS
    return (GetKeyState(VK_CAPITAL) & 0x0001) ? 1 : 0;
#elif RT_PLATFORM_MACOS
    CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
    return (flags & kCGEventFlagMaskAlphaShift) ? 1 : 0;
#elif RT_PLATFORM_LINUX && defined(VIPER_ENABLE_GRAPHICS)
    extern void *vgfx_get_native_display(void *window);

    Display *display = NULL;
    int opened_display = 0;
    if (g_active_canvas)
        display = (Display *)vgfx_get_native_display(g_active_canvas);
    if (!display) {
        display = XOpenDisplay(NULL);
        opened_display = (display != NULL);
    }
    if (!display)
        return g_caps_lock ? 1 : 0;

    unsigned int indicator_state = 0;
    int status = XkbGetIndicatorState(display, XkbUseCoreKbd, &indicator_state);
    if (opened_display)
        XCloseDisplay(display);
    if (status != Success)
        return g_caps_lock ? 1 : 0;
    return (indicator_state & 0x01U) ? 1 : 0;
#else
    return g_caps_lock ? 1 : 0;
#endif
}

#if defined(VIPER_ENABLE_GRAPHICS)
extern void vgfx_warp_cursor(void *window, int32_t x, int32_t y);
#endif

/// @brief Move the OS cursor to the given canvas-pixel position.
///
/// Routes through the installed `mouse_warp_hook` (test override) when
/// present; otherwise calls the platform `vgfx_warp_cursor` bridge,
/// which dispatches to `CGWarpMouseCursorPosition` (macOS),
/// `SetCursorPos` (Windows), or `XWarpPointer` (Linux). Coordinates are
/// in the active mouse canvas's pixel space (top-left origin).
///
/// Silent no-op when no canvas is bound or when graphics support is
/// disabled at compile time.
///
/// @param x Target x in canvas pixels.
/// @param y Target y in canvas pixels.
static void rt_input_warp_mouse_platform(int64_t x, int64_t y) {
    if (!g_mouse_canvas)
        return;

    if (g_mouse_warp_hook) {
        g_mouse_warp_hook(g_mouse_canvas, x, y);
        return;
    }

#if defined(VIPER_ENABLE_GRAPHICS)
    vgfx_warp_cursor(g_mouse_canvas, (int32_t)x, (int32_t)y);
#else
    (void)x;
    (void)y;
#endif
}

//=============================================================================
// Initialization
//=============================================================================

/// @brief Initialize the keyboard subsystem (zeroes all key state, clears event buffers).
void rt_keyboard_init(void) {
    RT_ASSERT_MAIN_THREAD();
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

/// @brief Clear per-frame pressed/released lists and text buffer. Call once at frame start.
void rt_keyboard_begin_frame(void) {
    RT_ASSERT_MAIN_THREAD();
    // Clear per-frame event lists
    g_pressed_count = 0;
    g_released_count = 0;
    g_text_length = 0;
}

/// @brief Record a key-down event from the platform layer (converts vgfx→GLFW key codes).
void rt_keyboard_on_key_down(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    // Convert vgfx key to GLFW-style
    int64_t glfw_key = vgfx_to_glfw(key);

    if (glfw_key <= 0 || glfw_key >= VIPER_KEY_MAX)
        return;

    // Only record press if key wasn't already down
    if (!g_key_state[glfw_key]) {
        g_key_state[glfw_key] = true;

        if (g_pressed_count < 64)
            g_pressed_keys[g_pressed_count++] = glfw_key;
    }

    // Caps Lock state is queried from the platform on demand.
}

/// @brief Record a key-up event from the platform layer.
void rt_keyboard_on_key_up(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    // Convert vgfx key to GLFW-style
    int64_t glfw_key = vgfx_to_glfw(key);

    if (glfw_key <= 0 || glfw_key >= VIPER_KEY_MAX)
        return;

    if (g_key_state[glfw_key]) {
        g_key_state[glfw_key] = false;

        if (g_released_count < 64)
            g_released_keys[g_released_count++] = glfw_key;
    }
}

/// @brief Append a text-input character to the per-frame text buffer (ASCII only currently).
void rt_keyboard_text_input(int32_t ch) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_text_input_enabled)
        return;

    // Simple UTF-8 encoding for ASCII characters
    // Full UTF-8 would require more complex handling
    if (ch >= 32 && ch < 127 && g_text_length < 255) {
        g_text_buffer[g_text_length++] = (char)ch;
    }
}

/// @brief Bind the keyboard to a canvas window (auto-initializes on first bind).
void rt_keyboard_set_canvas(void *canvas) {
    RT_ASSERT_MAIN_THREAD();
    if (canvas)
        rt_keyboard_init();
    g_active_canvas = canvas;
    if (canvas)
        g_caps_lock = rt_input_query_caps_lock_platform() != 0;
}

/// @brief Conditionally release the keyboard's canvas binding.
///
/// Called from the canvas destruction path (`rt_canvas_destroy_window` /
/// `rt_canvas3d_detach_input`) before the underlying vgfx window is torn
/// down. If the keyboard is currently bound to *this* canvas, the
/// binding is dropped so subsequent input queries won't dereference the
/// freed window pointer. If the keyboard is bound to a different canvas
/// (multi-canvas application), the binding is left untouched.
///
/// Safe to call with a NULL canvas (no-op).
///
/// @param canvas Canvas being destroyed. Compared against the active
///               keyboard canvas; only matching bindings are cleared.
void rt_keyboard_clear_canvas_if_matches(void *canvas) {
    RT_ASSERT_MAIN_THREAD();
    if (canvas && g_active_canvas == canvas)
        g_active_canvas = NULL;
}

//=============================================================================
// Polling Methods
//=============================================================================

/// @brief Check whether a key is currently held down (continuous — true every frame while held).
int8_t rt_keyboard_is_down(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    if (key <= 0 || key >= VIPER_KEY_MAX)
        return 0;

    return g_key_state[key] ? 1 : 0;
}

/// @brief Check whether a key is currently not held down.
int8_t rt_keyboard_is_up(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    if (key <= 0 || key >= VIPER_KEY_MAX)
        return 1;

    return g_key_state[key] ? 0 : 1;
}

/// @brief Check whether any key at all is currently held down.
int8_t rt_keyboard_any_down(void) {
    RT_ASSERT_MAIN_THREAD();
    for (int i = 0; i < VIPER_KEY_MAX; i++) {
        if (g_key_state[i])
            return 1;
    }
    return 0;
}

/// @brief Get the key code of the first key currently held down (0 if none).
int64_t rt_keyboard_get_down(void) {
    RT_ASSERT_MAIN_THREAD();
    for (int i = 0; i < VIPER_KEY_MAX; i++) {
        if (g_key_state[i])
            return (int64_t)i;
    }
    return 0;
}

//=============================================================================
// Event Methods
//=============================================================================

/// @brief Check whether a key was pressed this frame (edge-triggered — true once on key-down).
int8_t rt_keyboard_was_pressed(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    for (int i = 0; i < g_pressed_count; i++) {
        if (g_pressed_keys[i] == key)
            return 1;
    }
    return 0;
}

/// @brief Check whether a key was released this frame (edge-triggered — true once on key-up).
int8_t rt_keyboard_was_released(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    for (int i = 0; i < g_released_count; i++) {
        if (g_released_keys[i] == key)
            return 1;
    }
    return 0;
}

/// @brief Get a list of all keys pressed this frame as a sequence of key codes.
void *rt_keyboard_get_pressed(void) {
    RT_ASSERT_MAIN_THREAD();
    void *seq = rt_seq_new();
    for (int i = 0; i < g_pressed_count; i++) {
        rt_seq_push(seq, rt_box_i64(g_pressed_keys[i]));
    }
    return seq;
}

/// @brief Get a list of all keys released this frame as a sequence of key codes.
void *rt_keyboard_get_released(void) {
    RT_ASSERT_MAIN_THREAD();
    void *seq = rt_seq_new();
    for (int i = 0; i < g_released_count; i++) {
        rt_seq_push(seq, rt_box_i64(g_released_keys[i]));
    }
    return seq;
}

//=============================================================================
// Text Input
//=============================================================================

/// @brief Get the text typed this frame (characters accumulated from text-input events).
rt_string rt_keyboard_get_text(void) {
    RT_ASSERT_MAIN_THREAD();
    if (g_text_length == 0)
        return rt_string_from_bytes("", 0);

    return rt_string_from_bytes(g_text_buffer, g_text_length);
}

/// @brief Enable text input mode (characters are collected in the text buffer each frame).
void rt_keyboard_enable_text_input(void) {
    RT_ASSERT_MAIN_THREAD();
    g_text_input_enabled = true;
}

/// @brief Disable text input mode.
void rt_keyboard_disable_text_input(void) {
    RT_ASSERT_MAIN_THREAD();
    g_text_input_enabled = false;
}

//=============================================================================
// Modifier State
//=============================================================================

/// @brief Check whether either Shift key is currently held.
int8_t rt_keyboard_shift(void) {
    RT_ASSERT_MAIN_THREAD();
    return (g_key_state[VIPER_KEY_LSHIFT] || g_key_state[VIPER_KEY_RSHIFT]) ? 1 : 0;
}

/// @brief Check whether either Ctrl key is currently held.
int8_t rt_keyboard_ctrl(void) {
    RT_ASSERT_MAIN_THREAD();
    return (g_key_state[VIPER_KEY_LCTRL] || g_key_state[VIPER_KEY_RCTRL]) ? 1 : 0;
}

/// @brief Check whether either Alt key is currently held.
int8_t rt_keyboard_alt(void) {
    RT_ASSERT_MAIN_THREAD();
    return (g_key_state[VIPER_KEY_LALT] || g_key_state[VIPER_KEY_RALT]) ? 1 : 0;
}

/// @brief Check whether Caps Lock is active.
int8_t rt_keyboard_caps_lock(void) {
    RT_ASSERT_MAIN_THREAD();
    g_caps_lock = rt_input_query_caps_lock_platform() != 0;
    return g_caps_lock ? 1 : 0;
}

//=============================================================================
// Key Name Helper
//=============================================================================

/// @brief Get the human-readable name of a key code (e.g., "A", "Space", "F1").
rt_string rt_keyboard_key_name(int64_t key) {
    RT_ASSERT_MAIN_THREAD();
    const char *name = NULL;

    // Letters
    if (key >= VIPER_KEY_A && key <= VIPER_KEY_Z) {
        static char letter[2] = {0, 0};
        letter[0] = (char)key;
        return rt_string_from_bytes(letter, 1);
    }

    // Numbers
    if (key >= VIPER_KEY_0 && key <= VIPER_KEY_9) {
        static char digit[2] = {0, 0};
        digit[0] = (char)key;
        return rt_string_from_bytes(digit, 1);
    }

    // Special keys
    switch (key) {
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
//
// Each accessor below returns one of the platform-independent VIPER_KEY_*
// integer constants defined in rt_input.h. These wrappers exist so Zia
// and BASIC programs can refer to keys by name (e.g. `Keyboard.IsDown(
// Keyboard.Key.A)`) rather than hard-coding magic integers, which would
// drift if the key-code numbering ever changed.
//
// All getters are pure (no side effects), thread-safe (read-only access
// to compile-time constants), and constant-folded by the compiler in
// almost every call site, so the wrapper overhead is zero in practice.
// Each function takes no arguments and returns the canonical i64 code.
//=============================================================================

/// @brief Key-code constant for the unknown / unmapped key sentinel.
int64_t rt_keyboard_key_unknown(void) {
    return VIPER_KEY_UNKNOWN;
}

/// @brief Key-code constant for the A key.
int64_t rt_keyboard_key_a(void) {
    return VIPER_KEY_A;
}

/// @brief Key-code constant for the B key.
int64_t rt_keyboard_key_b(void) {
    return VIPER_KEY_B;
}

/// @brief Key-code constant for the C key.
int64_t rt_keyboard_key_c(void) {
    return VIPER_KEY_C;
}

/// @brief Key-code constant for the D key.
int64_t rt_keyboard_key_d(void) {
    return VIPER_KEY_D;
}

/// @brief Key-code constant for the E key.
int64_t rt_keyboard_key_e(void) {
    return VIPER_KEY_E;
}

/// @brief Key-code constant for the F key.
int64_t rt_keyboard_key_f(void) {
    return VIPER_KEY_F;
}

/// @brief Key-code constant for the G key.
int64_t rt_keyboard_key_g(void) {
    return VIPER_KEY_G;
}

/// @brief Key-code constant for the H key.
int64_t rt_keyboard_key_h(void) {
    return VIPER_KEY_H;
}

/// @brief Key-code constant for the I key.
int64_t rt_keyboard_key_i(void) {
    return VIPER_KEY_I;
}

/// @brief Key-code constant for the J key.
int64_t rt_keyboard_key_j(void) {
    return VIPER_KEY_J;
}

/// @brief Key-code constant for the K key.
int64_t rt_keyboard_key_k(void) {
    return VIPER_KEY_K;
}

/// @brief Key-code constant for the L key.
int64_t rt_keyboard_key_l(void) {
    return VIPER_KEY_L;
}

/// @brief Key-code constant for the M key.
int64_t rt_keyboard_key_m(void) {
    return VIPER_KEY_M;
}

/// @brief Key-code constant for the N key.
int64_t rt_keyboard_key_n(void) {
    return VIPER_KEY_N;
}

/// @brief Key-code constant for the O key.
int64_t rt_keyboard_key_o(void) {
    return VIPER_KEY_O;
}

/// @brief Key-code constant for the P key.
int64_t rt_keyboard_key_p(void) {
    return VIPER_KEY_P;
}

/// @brief Key-code constant for the Q key.
int64_t rt_keyboard_key_q(void) {
    return VIPER_KEY_Q;
}

/// @brief Key-code constant for the R key.
int64_t rt_keyboard_key_r(void) {
    return VIPER_KEY_R;
}

/// @brief Key-code constant for the S key.
int64_t rt_keyboard_key_s(void) {
    return VIPER_KEY_S;
}

/// @brief Key-code constant for the T key.
int64_t rt_keyboard_key_t(void) {
    return VIPER_KEY_T;
}

/// @brief Key-code constant for the U key.
int64_t rt_keyboard_key_u(void) {
    return VIPER_KEY_U;
}

/// @brief Key-code constant for the V key.
int64_t rt_keyboard_key_v(void) {
    return VIPER_KEY_V;
}

/// @brief Key-code constant for the W key.
int64_t rt_keyboard_key_w(void) {
    return VIPER_KEY_W;
}

/// @brief Key-code constant for the X key.
int64_t rt_keyboard_key_x(void) {
    return VIPER_KEY_X;
}

/// @brief Key-code constant for the Y key.
int64_t rt_keyboard_key_y(void) {
    return VIPER_KEY_Y;
}

/// @brief Key-code constant for the Z key.
int64_t rt_keyboard_key_z(void) {
    return VIPER_KEY_Z;
}

/// @brief Key-code constant for the 0 (zero) row-digit key.
int64_t rt_keyboard_key_0(void) {
    return VIPER_KEY_0;
}

/// @brief Key-code constant for the 1 row-digit key.
int64_t rt_keyboard_key_1(void) {
    return VIPER_KEY_1;
}

/// @brief Key-code constant for the 2 row-digit key.
int64_t rt_keyboard_key_2(void) {
    return VIPER_KEY_2;
}

/// @brief Key-code constant for the 3 row-digit key.
int64_t rt_keyboard_key_3(void) {
    return VIPER_KEY_3;
}

/// @brief Key-code constant for the 4 row-digit key.
int64_t rt_keyboard_key_4(void) {
    return VIPER_KEY_4;
}

/// @brief Key-code constant for the 5 row-digit key.
int64_t rt_keyboard_key_5(void) {
    return VIPER_KEY_5;
}

/// @brief Key-code constant for the 6 row-digit key.
int64_t rt_keyboard_key_6(void) {
    return VIPER_KEY_6;
}

/// @brief Key-code constant for the 7 row-digit key.
int64_t rt_keyboard_key_7(void) {
    return VIPER_KEY_7;
}

/// @brief Key-code constant for the 8 row-digit key.
int64_t rt_keyboard_key_8(void) {
    return VIPER_KEY_8;
}

/// @brief Key-code constant for the 9 row-digit key.
int64_t rt_keyboard_key_9(void) {
    return VIPER_KEY_9;
}

/// @brief Key-code constant for the F1 function key.
int64_t rt_keyboard_key_f1(void) {
    return VIPER_KEY_F1;
}

/// @brief Key-code constant for the F2 function key.
int64_t rt_keyboard_key_f2(void) {
    return VIPER_KEY_F2;
}

/// @brief Key-code constant for the F3 function key.
int64_t rt_keyboard_key_f3(void) {
    return VIPER_KEY_F3;
}

/// @brief Key-code constant for the F4 function key.
int64_t rt_keyboard_key_f4(void) {
    return VIPER_KEY_F4;
}

/// @brief Key-code constant for the F5 function key.
int64_t rt_keyboard_key_f5(void) {
    return VIPER_KEY_F5;
}

/// @brief Key-code constant for the F6 function key.
int64_t rt_keyboard_key_f6(void) {
    return VIPER_KEY_F6;
}

/// @brief Key-code constant for the F7 function key.
int64_t rt_keyboard_key_f7(void) {
    return VIPER_KEY_F7;
}

/// @brief Key-code constant for the F8 function key.
int64_t rt_keyboard_key_f8(void) {
    return VIPER_KEY_F8;
}

/// @brief Key-code constant for the F9 function key.
int64_t rt_keyboard_key_f9(void) {
    return VIPER_KEY_F9;
}

/// @brief Key-code constant for the F10 function key.
int64_t rt_keyboard_key_f10(void) {
    return VIPER_KEY_F10;
}

/// @brief Key-code constant for the F11 function key.
int64_t rt_keyboard_key_f11(void) {
    return VIPER_KEY_F11;
}

/// @brief Key-code constant for the F12 function key.
int64_t rt_keyboard_key_f12(void) {
    return VIPER_KEY_F12;
}

/// @brief Key-code constant for the Up arrow key.
int64_t rt_keyboard_key_up(void) {
    return VIPER_KEY_UP;
}

/// @brief Key-code constant for the Down arrow key.
int64_t rt_keyboard_key_down(void) {
    return VIPER_KEY_DOWN;
}

/// @brief Key-code constant for the Left arrow key.
int64_t rt_keyboard_key_left(void) {
    return VIPER_KEY_LEFT;
}

/// @brief Key-code constant for the Right arrow key.
int64_t rt_keyboard_key_right(void) {
    return VIPER_KEY_RIGHT;
}

/// @brief Key-code constant for the Home navigation key.
int64_t rt_keyboard_key_home(void) {
    return VIPER_KEY_HOME;
}

/// @brief Key-code constant for the End navigation key.
int64_t rt_keyboard_key_end(void) {
    return VIPER_KEY_END;
}

/// @brief Key-code constant for the Page Up navigation key.
int64_t rt_keyboard_key_pageup(void) {
    return VIPER_KEY_PAGEUP;
}

/// @brief Key-code constant for the Page Down navigation key.
int64_t rt_keyboard_key_pagedown(void) {
    return VIPER_KEY_PAGEDOWN;
}

/// @brief Key-code constant for the Insert editing key.
int64_t rt_keyboard_key_insert(void) {
    return VIPER_KEY_INSERT;
}

/// @brief Key-code constant for the Delete editing key (forward delete).
int64_t rt_keyboard_key_delete(void) {
    return VIPER_KEY_DELETE;
}

/// @brief Key-code constant for the Backspace editing key (backward delete).
int64_t rt_keyboard_key_backspace(void) {
    return VIPER_KEY_BACKSPACE;
}

/// @brief Key-code constant for the Tab key.
int64_t rt_keyboard_key_tab(void) {
    return VIPER_KEY_TAB;
}

/// @brief Key-code constant for the main Enter / Return key (numpad enter
///        is `Key.NumEnter`).
int64_t rt_keyboard_key_enter(void) {
    return VIPER_KEY_ENTER;
}

/// @brief Key-code constant for the Space bar.
int64_t rt_keyboard_key_space(void) {
    return VIPER_KEY_SPACE;
}

/// @brief Key-code constant for the Escape key.
int64_t rt_keyboard_key_escape(void) {
    return VIPER_KEY_ESCAPE;
}

/// @brief Key-code constant for the unspecified Shift modifier (matches
///        either left or right shift on platforms that don't distinguish).
int64_t rt_keyboard_key_shift(void) {
    return VIPER_KEY_SHIFT;
}

/// @brief Key-code constant for the unspecified Ctrl modifier.
int64_t rt_keyboard_key_ctrl(void) {
    return VIPER_KEY_CTRL;
}

/// @brief Key-code constant for the unspecified Alt modifier (Option on macOS).
int64_t rt_keyboard_key_alt(void) {
    return VIPER_KEY_ALT;
}

/// @brief Key-code constant for the Left Shift modifier specifically.
int64_t rt_keyboard_key_lshift(void) {
    return VIPER_KEY_LSHIFT;
}

/// @brief Key-code constant for the Right Shift modifier specifically.
int64_t rt_keyboard_key_rshift(void) {
    return VIPER_KEY_RSHIFT;
}

/// @brief Key-code constant for the Left Ctrl modifier specifically.
int64_t rt_keyboard_key_lctrl(void) {
    return VIPER_KEY_LCTRL;
}

/// @brief Key-code constant for the Right Ctrl modifier specifically.
int64_t rt_keyboard_key_rctrl(void) {
    return VIPER_KEY_RCTRL;
}

/// @brief Key-code constant for the Left Alt modifier specifically.
int64_t rt_keyboard_key_lalt(void) {
    return VIPER_KEY_LALT;
}

/// @brief Key-code constant for the Right Alt modifier specifically (AltGr
///        on European layouts).
int64_t rt_keyboard_key_ralt(void) {
    return VIPER_KEY_RALT;
}

/// @brief Key-code constant for the Minus / Hyphen punctuation key.
int64_t rt_keyboard_key_minus(void) {
    return VIPER_KEY_MINUS;
}

/// @brief Key-code constant for the Equals / Plus punctuation key.
int64_t rt_keyboard_key_equals(void) {
    return VIPER_KEY_EQUALS;
}

/// @brief Key-code constant for the Left Bracket `[` punctuation key.
int64_t rt_keyboard_key_lbracket(void) {
    return VIPER_KEY_LBRACKET;
}

/// @brief Key-code constant for the Right Bracket `]` punctuation key.
int64_t rt_keyboard_key_rbracket(void) {
    return VIPER_KEY_RBRACKET;
}

/// @brief Key-code constant for the Backslash `\\` punctuation key.
int64_t rt_keyboard_key_backslash(void) {
    return VIPER_KEY_BACKSLASH;
}

/// @brief Key-code constant for the Semicolon `;` punctuation key.
int64_t rt_keyboard_key_semicolon(void) {
    return VIPER_KEY_SEMICOLON;
}

/// @brief Key-code constant for the Quote / Apostrophe `'` punctuation key.
int64_t rt_keyboard_key_quote(void) {
    return VIPER_KEY_QUOTE;
}

/// @brief Key-code constant for the Grave / Backtick `` ` `` punctuation key
///        (typically below Esc on US keyboards).
int64_t rt_keyboard_key_grave(void) {
    return VIPER_KEY_GRAVE;
}

/// @brief Key-code constant for the Comma `,` punctuation key.
int64_t rt_keyboard_key_comma(void) {
    return VIPER_KEY_COMMA;
}

/// @brief Key-code constant for the Period `.` punctuation key.
int64_t rt_keyboard_key_period(void) {
    return VIPER_KEY_PERIOD;
}

/// @brief Key-code constant for the Slash `/` punctuation key.
int64_t rt_keyboard_key_slash(void) {
    return VIPER_KEY_SLASH;
}

/// @brief Key-code constant for the numpad 0 key.
int64_t rt_keyboard_key_num0(void) {
    return VIPER_KEY_NUM0;
}

/// @brief Key-code constant for the numpad 1 key.
int64_t rt_keyboard_key_num1(void) {
    return VIPER_KEY_NUM1;
}

/// @brief Key-code constant for the numpad 2 key.
int64_t rt_keyboard_key_num2(void) {
    return VIPER_KEY_NUM2;
}

/// @brief Key-code constant for the numpad 3 key.
int64_t rt_keyboard_key_num3(void) {
    return VIPER_KEY_NUM3;
}

/// @brief Key-code constant for the numpad 4 key.
int64_t rt_keyboard_key_num4(void) {
    return VIPER_KEY_NUM4;
}

/// @brief Key-code constant for the numpad 5 key.
int64_t rt_keyboard_key_num5(void) {
    return VIPER_KEY_NUM5;
}

/// @brief Key-code constant for the numpad 6 key.
int64_t rt_keyboard_key_num6(void) {
    return VIPER_KEY_NUM6;
}

/// @brief Key-code constant for the numpad 7 key.
int64_t rt_keyboard_key_num7(void) {
    return VIPER_KEY_NUM7;
}

/// @brief Key-code constant for the numpad 8 key.
int64_t rt_keyboard_key_num8(void) {
    return VIPER_KEY_NUM8;
}

/// @brief Key-code constant for the numpad 9 key.
int64_t rt_keyboard_key_num9(void) {
    return VIPER_KEY_NUM9;
}

/// @brief Key-code constant for the numpad Add `+` key.
int64_t rt_keyboard_key_numadd(void) {
    return VIPER_KEY_NUMADD;
}

/// @brief Key-code constant for the numpad Subtract `-` key.
int64_t rt_keyboard_key_numsub(void) {
    return VIPER_KEY_NUMSUB;
}

/// @brief Key-code constant for the numpad Multiply `*` key.
int64_t rt_keyboard_key_nummul(void) {
    return VIPER_KEY_NUMMUL;
}

/// @brief Key-code constant for the numpad Divide `/` key.
int64_t rt_keyboard_key_numdiv(void) {
    return VIPER_KEY_NUMDIV;
}

/// @brief Key-code constant for the numpad Enter key (distinct from the
///        main Enter key).
int64_t rt_keyboard_key_numenter(void) {
    return VIPER_KEY_NUMENTER;
}

/// @brief Key-code constant for the numpad Decimal `.` key.
int64_t rt_keyboard_key_numdot(void) {
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
static double g_mouse_wheel_x = 0.0;
static double g_mouse_wheel_y = 0.0;

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

/// @brief Read the runtime monotonic clock in milliseconds.
///
/// Internal helper used by the mouse-button bookkeeping below to time
/// click vs. hold intervals (`CLICK_MAX_DURATION_MS`,
/// `DOUBLE_CLICK_MAX_INTERVAL_MS`). Routes through the shared
/// `rt_clock_ticks_us` source so it stays consistent with other timing
/// surfaces (`Time.NowMs`, `Stopwatch`).
///
/// @return Current monotonic time in milliseconds since process start.
static int64_t get_time_ms(void) {
    return rt_clock_ticks_us() / 1000;
}

/// @brief Initialize the mouse subsystem to a clean state.
///
/// Zeros the pointer position, deltas, wheel accumulators, and every
/// per-button state array. Called automatically the first time a Canvas
/// binds the mouse via `rt_mouse_set_canvas` and is idempotent — repeat
/// calls are safe and short-circuit on the `g_mouse_initialized` flag.
void rt_mouse_init(void) {
    RT_ASSERT_MAIN_THREAD();
    if (g_mouse_initialized)
        return;

    g_mouse_x = 0;
    g_mouse_y = 0;
    g_mouse_prev_x = 0;
    g_mouse_prev_y = 0;
    g_mouse_delta_x = 0;
    g_mouse_delta_y = 0;
    g_mouse_wheel_x = 0.0;
    g_mouse_wheel_y = 0.0;
    g_mouse_hidden = false;
    g_mouse_captured = false;
    g_mouse_canvas = NULL;

    for (int i = 0; i < VIPER_MOUSE_BUTTON_MAX; i++) {
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

/// @brief Snapshot mouse state for the new frame.
///
/// Computes per-frame position deltas (`delta_x`, `delta_y`) by
/// subtracting the previous frame's pointer position, advances the
/// `prev_x/y` reference, and resets the per-frame event arrays
/// (`pressed`, `released`, `clicked`, `double_clicked`) and wheel
/// accumulators back to zero. Called once per game frame between event
/// pumping and game-loop user code.
void rt_mouse_begin_frame(void) {
    RT_ASSERT_MAIN_THREAD();
    // Calculate delta from previous position
    g_mouse_delta_x = g_mouse_x - g_mouse_prev_x;
    g_mouse_delta_y = g_mouse_y - g_mouse_prev_y;
    g_mouse_prev_x = g_mouse_x;
    g_mouse_prev_y = g_mouse_y;

    // Reset per-frame event arrays
    for (int i = 0; i < VIPER_MOUSE_BUTTON_MAX; i++) {
        g_mouse_button_pressed[i] = false;
        g_mouse_button_released[i] = false;
        g_mouse_clicked[i] = false;
        g_mouse_double_clicked[i] = false;
    }

    // Reset wheel deltas
    g_mouse_wheel_x = 0.0;
    g_mouse_wheel_y = 0.0;
}

/// @brief Forward an OS mouse-move event into the runtime state.
///
/// Called by `rt_canvas_poll` for every `VGFX_EVENT_MOUSE_MOVE` event,
/// after applying coordinate-scale conversion. Coordinates are in
/// canvas-pixel space (top-left origin, +Y down), already scaled by
/// the HiDPI factor so callers see logical pixels.
///
/// @param x New pointer x in canvas pixels.
/// @param y New pointer y in canvas pixels.
void rt_mouse_update_pos(int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    g_mouse_x = x;
    g_mouse_y = y;
}

/// @brief Override the mouse delta values for the current frame.
///
/// Used by tests and synthetic input paths (e.g. replay) that don't go
/// through `update_pos` but still want `Mouse.DeltaX`/`DeltaY` to read
/// expected values. Bypasses the normal `prev_x/y`-based delta computation;
/// the next `begin_frame` call will compute deltas normally again.
///
/// @param dx Forced delta x in canvas pixels.
/// @param dy Forced delta y in canvas pixels.
void rt_mouse_force_delta(int64_t dx, int64_t dy) {
    g_mouse_delta_x = dx;
    g_mouse_delta_y = dy;
}

/// @brief Forward an OS mouse-button-press event into the runtime state.
///
/// Called by `rt_canvas_poll` for every `VGFX_EVENT_MOUSE_DOWN`. Only
/// the *transition* from up to down sets `pressed[button]` to true and
/// records the press timestamp; repeat-down events for an already-held
/// button are filtered out (BIOS auto-repeat protection).
///
/// Out-of-range button indices are silently ignored.
///
/// @param button Mouse button index (typically `1`=left, `2`=right,
///               `3`=middle, `4`/`5`=X1/X2). Range `0..VIPER_MOUSE_BUTTON_MAX-1`.
void rt_mouse_button_down(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return;

    if (!g_mouse_button_state[button]) {
        g_mouse_button_state[button] = true;
        g_mouse_button_pressed[button] = true;
        g_mouse_press_time[button] = get_time_ms();
    }
}

/// @brief Forward an OS mouse-button-release event into the runtime state.
///
/// Called by `rt_canvas_poll` for every `VGFX_EVENT_MOUSE_UP`. Records
/// the release in the per-frame arrays, then evaluates the press
/// duration to detect a click (release within `CLICK_MAX_DURATION_MS`
/// of the matching press) and a double-click (click within
/// `DOUBLE_CLICK_MAX_INTERVAL_MS` of the previous click on the same
/// button).
///
/// Out-of-range button indices are silently ignored.
///
/// @param button Mouse button index that was released.
void rt_mouse_button_up(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return;

    if (g_mouse_button_state[button]) {
        g_mouse_button_state[button] = false;
        g_mouse_button_released[button] = true;

        // Check for click (quick press and release)
        int64_t now = get_time_ms();
        int64_t press_duration = now - g_mouse_press_time[button];
        if (press_duration <= CLICK_MAX_DURATION_MS) {
            g_mouse_clicked[button] = true;

            // Check for double-click
            int64_t since_last_click = now - g_mouse_last_click_time[button];
            if (since_last_click <= DOUBLE_CLICK_MAX_INTERVAL_MS) {
                g_mouse_double_clicked[button] = true;
            }
            g_mouse_last_click_time[button] = now;
        }
    }
}

/// @brief Forward an OS scroll-wheel event into the runtime state.
///
/// Accumulates wheel deltas across all events received within a single
/// frame; `Mouse.WheelX`/`WheelY` queries return the sum, then
/// `begin_frame` resets the accumulator. Both axes are exposed so
/// horizontal scroll wheels and trackpad two-finger scroll work.
///
/// @param dx Horizontal wheel delta (positive = right). Units are
///           platform-defined "ticks" — typically integer click counts
///           on traditional wheels, fractional on touchpad scroll.
/// @param dy Vertical wheel delta (positive = up).
void rt_mouse_update_wheel(double dx, double dy) {
    RT_ASSERT_MAIN_THREAD();
    g_mouse_wheel_x += dx;
    g_mouse_wheel_y += dy;
}

/// @brief Bind the mouse to a specific Canvas window.
///
/// All subsequent `update_pos` / `button_down` / etc. calls are
/// associated with this canvas. Called automatically when a Canvas is
/// created or `begin_frame`'d. Auto-initializes the mouse subsystem on
/// the first non-NULL bind.
///
/// Pass NULL to release the binding when the canvas is destroyed (or
/// use the more conservative `rt_mouse_clear_canvas_if_matches` to
/// only clear when the binding matches).
///
/// @param canvas Canvas handle, or NULL.
void rt_mouse_set_canvas(void *canvas) {
    RT_ASSERT_MAIN_THREAD();
    if (canvas)
        rt_mouse_init();
    g_mouse_canvas = canvas;
}

/// @brief Conditionally release the mouse's canvas binding.
///
/// Mirror of `rt_keyboard_clear_canvas_if_matches` for the mouse side.
/// Called from the canvas destruction path so the global mouse state
/// won't hold a dangling pointer to a freed window. Only clears when
/// the binding matches; harmless when the mouse is bound to a different
/// canvas in a multi-canvas application.
///
/// Safe with NULL canvas (no-op).
///
/// @param canvas Canvas being destroyed.
void rt_mouse_clear_canvas_if_matches(void *canvas) {
    RT_ASSERT_MAIN_THREAD();
    if (canvas && g_mouse_canvas == canvas)
        g_mouse_canvas = NULL;
}

//=============================================================================
// Position Methods
//=============================================================================

/// @brief Get the current pointer x position in canvas pixels.
/// @return Pointer x coordinate (top-left origin, +X right).
int64_t rt_mouse_x(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_x;
}

/// @brief Get the current pointer y position in canvas pixels.
/// @return Pointer y coordinate (top-left origin, +Y down).
int64_t rt_mouse_y(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_y;
}

/// @brief Get the per-frame pointer delta along x.
///
/// Computed by `rt_mouse_begin_frame` as `current_x - previous_frame_x`.
/// Stable for the entire duration of the current frame; updated only at
/// the next `begin_frame`.
///
/// @return Movement delta in canvas pixels since the last frame (+X right).
int64_t rt_mouse_delta_x(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_delta_x;
}

/// @brief Get the per-frame pointer delta along y.
/// @return Movement delta in canvas pixels since the last frame (+Y down).
int64_t rt_mouse_delta_y(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_delta_y;
}

//=============================================================================
// Button State (Polling)
//=============================================================================

/// @brief Test whether a mouse button is currently held down (level
///        detection — true every frame while held).
///
/// Out-of-range button indices are silently treated as "not down" and
/// return `0`.
///
/// @param button Mouse button index (1=left, 2=right, 3=middle, etc.).
/// @return `1` if the button is currently held, `0` otherwise.
int8_t rt_mouse_is_down(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_button_state[button] ? 1 : 0;
}

/// @brief Test whether a mouse button is currently released (inverse of
///        `IsDown`).
///
/// Out-of-range button indices are treated as "released" and return `1`,
/// since asking about a non-existent button it can never be down.
///
/// @param button Mouse button index.
/// @return `1` if the button is currently up, `0` if down.
int8_t rt_mouse_is_up(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 1;
    return g_mouse_button_state[button] ? 0 : 1;
}

/// @brief Convenience query for `IsDown(left)`.
/// @return `1` if the left mouse button is held, `0` otherwise.
int8_t rt_mouse_left(void) {
    RT_ASSERT_MAIN_THREAD();
    return rt_mouse_is_down(VIPER_MOUSE_BUTTON_LEFT);
}

/// @brief Convenience query for `IsDown(right)`.
/// @return `1` if the right mouse button is held, `0` otherwise.
int8_t rt_mouse_right(void) {
    RT_ASSERT_MAIN_THREAD();
    return rt_mouse_is_down(VIPER_MOUSE_BUTTON_RIGHT);
}

/// @brief Convenience query for `IsDown(middle)`.
/// @return `1` if the middle mouse button is held, `0` otherwise.
int8_t rt_mouse_middle(void) {
    RT_ASSERT_MAIN_THREAD();
    return rt_mouse_is_down(VIPER_MOUSE_BUTTON_MIDDLE);
}

//=============================================================================
// Button Events (Since Last Poll)
//=============================================================================

/// @brief Edge-detect: was this button newly pressed during the current
///        frame?
///
/// True for exactly one frame when a button transitions from up to
/// down. Use for one-shot input (jump triggers, fire-once weapons,
/// menu confirm). Compare with `IsDown` for level-triggered
/// (continuous) input like movement.
///
/// @param button Mouse button index.
/// @return `1` if the button was pressed this frame, `0` otherwise.
int8_t rt_mouse_was_pressed(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_button_pressed[button] ? 1 : 0;
}

/// @brief Edge-detect: was this button newly released during the
///        current frame?
///
/// True for exactly one frame on the down-to-up transition. Use for
/// charge-attack release, drag-end detection.
///
/// @param button Mouse button index.
/// @return `1` if the button was released this frame, `0` otherwise.
int8_t rt_mouse_was_released(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_button_released[button] ? 1 : 0;
}

/// @brief Did this button complete a click (quick press + release)
///        during the current frame?
///
/// True when both the press and the release of a button happened
/// within `CLICK_MAX_DURATION_MS` of each other. Long holds don't
/// register as clicks. Recorded in `rt_mouse_button_up` at release time.
///
/// @param button Mouse button index.
/// @return `1` if a click was registered this frame, `0` otherwise.
int8_t rt_mouse_was_clicked(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_clicked[button] ? 1 : 0;
}

/// @brief Did this button complete a double-click during the current
///        frame?
///
/// True when a click occurred within `DOUBLE_CLICK_MAX_INTERVAL_MS` of
/// the previous click on the same button. Both the click and the
/// double-click flags are set on the second click; consumers can
/// branch on whichever they care about.
///
/// @param button Mouse button index.
/// @return `1` if a double-click was registered this frame, `0` otherwise.
int8_t rt_mouse_was_double_clicked(int64_t button) {
    RT_ASSERT_MAIN_THREAD();
    if (button < 0 || button >= VIPER_MOUSE_BUTTON_MAX)
        return 0;
    return g_mouse_double_clicked[button] ? 1 : 0;
}

//=============================================================================
// Scroll Wheel
//=============================================================================

/// @brief Get the integer-truncated horizontal scroll delta for this
///        frame. Use the `xf` variant if you need fractional deltas
///        from trackpad scrolling.
///
/// @return Sum of `update_wheel(dx, _)` events received this frame,
///         truncated to int64.
int64_t rt_mouse_wheel_x(void) {
    RT_ASSERT_MAIN_THREAD();
    return (int64_t)g_mouse_wheel_x;
}

/// @brief Get the integer-truncated vertical scroll delta for this frame.
/// @return Sum of `update_wheel(_, dy)` events received this frame,
///         truncated to int64.
int64_t rt_mouse_wheel_y(void) {
    RT_ASSERT_MAIN_THREAD();
    return (int64_t)g_mouse_wheel_y;
}

/// @brief Get the fractional horizontal scroll delta for this frame.
///
/// Preferred over `wheel_x` for smooth-scroll input devices (trackpads,
/// high-resolution mice). Reset to zero by `begin_frame`.
///
/// @return Sum of horizontal wheel deltas received this frame, as a
///         double.
double rt_mouse_wheel_xf(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_wheel_x;
}

/// @brief Get the fractional vertical scroll delta for this frame.
/// @return Sum of vertical wheel deltas received this frame, as a double.
double rt_mouse_wheel_yf(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_wheel_y;
}

//=============================================================================
// Cursor Control
//=============================================================================

extern void vgfx_show_cursor(void);
extern void vgfx_hide_cursor(void);

/// @brief Show the OS cursor (idempotent).
///
/// Calls `vgfx_show_cursor` exactly once on the down-to-up `hidden`
/// transition; further calls when the cursor is already visible are a
/// no-op. Use to restore the cursor after a `Mouse.Hide` call (or
/// implicitly via `Mouse.Release`).
void rt_mouse_show(void) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_mouse_hidden)
        return;
    g_mouse_hidden = false;
    vgfx_show_cursor();
}

/// @brief Hide the OS cursor (idempotent).
///
/// Calls `vgfx_hide_cursor` exactly once on the up-to-down `hidden`
/// transition. Useful for FPS-style mouse-look where the cursor would
/// otherwise be visible drifting around.
void rt_mouse_hide(void) {
    RT_ASSERT_MAIN_THREAD();
    if (g_mouse_hidden)
        return;
    g_mouse_hidden = true;
    vgfx_hide_cursor();
}

/// @brief Query whether the cursor is currently hidden.
/// @return `1` when hidden via `rt_mouse_hide` or `rt_mouse_capture`,
///         `0` otherwise.
int8_t rt_mouse_is_hidden(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_hidden ? 1 : 0;
}

/// @brief Capture the mouse: hide the cursor and mark it as captured.
///
/// "Captured" is a state flag the game can query (`IsCaptured`). The
/// runtime currently only ties it to the cursor visibility — extending
/// to true OS-level pointer warping (relative-only motion) is left to
/// the platform backend.
void rt_mouse_capture(void) {
    RT_ASSERT_MAIN_THREAD();
    g_mouse_captured = true;
    rt_mouse_hide(); /* Hide cursor during capture */
}

/// @brief Release a captured mouse: show the cursor and clear the flag.
///
/// Inverse of `rt_mouse_capture`. Safe to call when the mouse is not
/// captured.
void rt_mouse_release(void) {
    RT_ASSERT_MAIN_THREAD();
    g_mouse_captured = false;
    rt_mouse_show(); /* Restore cursor on release */
}

/// @brief Query whether the mouse is currently in captured mode.
/// @return `1` when captured, `0` otherwise.
int8_t rt_mouse_is_captured(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_mouse_captured ? 1 : 0;
}

/// @brief Move the OS cursor to the given canvas pixel position and
///        sync the runtime's tracked coordinates.
///
/// Implements `Mouse.SetPos`: updates the internal `g_mouse_x`/`y`
/// state immediately so subsequent `Mouse.X`/`Y` queries see the new
/// position even before the next OS event arrives, then warps the OS
/// cursor via the platform bridge (`vgfx_warp_cursor`) or the test hook.
///
/// @param x Target x in canvas pixels.
/// @param y Target y in canvas pixels.
void rt_mouse_set_pos(int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    g_mouse_x = x;
    g_mouse_y = y;
    rt_input_warp_mouse_platform(x, y);
}

//=============================================================================
// Button Constant Getters
//
// Match the keyboard constant getters in style — return the canonical
// VIPER_MOUSE_BUTTON_* int64 code so Zia/BASIC programs can write
// `Mouse.IsDown(Mouse.Button.Left)` instead of magic integers.
//=============================================================================

/// @brief Button-code constant for the left mouse button.
int64_t rt_mouse_button_left(void) {
    return VIPER_MOUSE_BUTTON_LEFT;
}

/// @brief Button-code constant for the right mouse button.
int64_t rt_mouse_button_right(void) {
    return VIPER_MOUSE_BUTTON_RIGHT;
}

/// @brief Button-code constant for the middle (wheel-click) mouse button.
int64_t rt_mouse_button_middle(void) {
    return VIPER_MOUSE_BUTTON_MIDDLE;
}

/// @brief Button-code constant for the X1 / "back" extended mouse button
///        (commonly the lower thumb button on 5-button mice).
int64_t rt_mouse_button_x1(void) {
    return VIPER_MOUSE_BUTTON_X1;
}

/// @brief Button-code constant for the X2 / "forward" extended mouse button.
int64_t rt_mouse_button_x2(void) {
    return VIPER_MOUSE_BUTTON_X2;
}
