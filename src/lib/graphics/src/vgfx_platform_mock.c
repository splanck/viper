//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// ViperGFX Mock Platform Backend (Testing Only)
//
// Provides an in-memory window simulation with NO OS dependencies.  Used for
// unit testing and deterministic behavior verification.  This backend never
// creates real windows or processes OS events - instead, tests manually inject
// events and control time progression.
//
// Key Features:
//   - Deterministic Time: Controllable clock via vgfx_mock_set_time_ms()
//   - Event Injection: Synthetic events via vgfx_mock_inject_*() functions
//   - No External Dependencies: Pure in-memory simulation
//   - No Display: vgfx_platform_present() is a no-op (framebuffer stays in memory)
//
// Use Cases:
//   - Unit Testing: Validate drawing, events, FPS limiting without real windows
//   - CI/CD: Run tests headless on servers without X11/Cocoa/Win32
//   - Determinism: Precise control over time and events for reproducible tests
//
//===----------------------------------------------------------------------===//

/// @file
/// @brief Mock platform backend for unit testing ViperGFX.
/// @details Provides in-memory window simulation with manual event injection
///          and time control.  No real OS windows are created.

#include "vgfx_internal.h"
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Mock Platform State
//===----------------------------------------------------------------------===//

/// @brief Global mock time in milliseconds.
/// @details Controlled by vgfx_mock_set_time_ms() and vgfx_mock_advance_time_ms().
///          Advanced automatically by vgfx_platform_sleep_ms() (simulates sleep).
///          Used by vgfx_platform_now_ms() to return consistent timestamps.
///
/// @invariant g_mock_time_ms >= 0 (never negative)
static int64_t g_mock_time_ms = 0;

/// @brief Mock platform data structure (minimal, no OS resources).
/// @details Stored in vgfx_window->platform_data.  Contains no real handles
///          or resources - just an initialization flag for consistency with
///          other backends.
typedef struct {
    int initialized;  ///< 1 if successfully initialized, 0 otherwise
} vgfx_mock_platform;

//===----------------------------------------------------------------------===//
// Platform API Implementation (Stubs for Mock)
//===----------------------------------------------------------------------===//

/// @brief Initialize platform-specific window resources (mock version).
/// @details Allocates a minimal platform data structure but creates NO real
///          window.  Always succeeds unless allocation fails.  The framebuffer
///          remains in memory only (no display surface created).
///
/// @param win    Pointer to the ViperGFX window structure
/// @param params Window creation parameters (unused in mock backend)
/// @return 1 on success, 0 on allocation failure
///
/// @pre  win != NULL
/// @pre  params != NULL
/// @post On success: platform_data allocated, initialized flag set
/// @post On failure: platform_data NULL, error set
///
/// @note This function NEVER creates a real OS window.  It's a testing stub.
int vgfx_platform_init_window(struct vgfx_window* win,
                               const vgfx_window_params_t* params) {
    if (!win || !params) return 0;

    /* Allocate mock platform data (minimal structure) */
    vgfx_mock_platform* platform = (vgfx_mock_platform*)calloc(1, sizeof(vgfx_mock_platform));
    if (!platform) {
        vgfx_internal_set_error(VGFX_ERR_ALLOC, "Failed to allocate mock platform data");
        return 0;
    }

    platform->initialized = 1;
    win->platform_data = platform;

    /* Success - no actual window created */
    return 1;
}

/// @brief Destroy platform-specific window resources (mock version).
/// @details Frees the platform data structure.  No real window to close.
///          Safe to call even if init failed.
///
/// @param win Pointer to the ViperGFX window structure
///
/// @pre  win != NULL
/// @post platform_data freed and set to NULL
void vgfx_platform_destroy_window(struct vgfx_window* win) {
    if (!win || !win->platform_data) return;

    vgfx_mock_platform* platform = (vgfx_mock_platform*)win->platform_data;
    free(platform);
    win->platform_data = NULL;
}

/// @brief Process OS events (mock version - no-op).
/// @details The mock backend does NOT generate events automatically.  Tests
///          must manually inject events using vgfx_mock_inject_*() functions.
///          This function always succeeds and does nothing.
///
/// @param win Pointer to the ViperGFX window structure (unused)
/// @return 1 (always succeeds)
///
/// @note To simulate events, use vgfx_mock_inject_key_event(),
///       vgfx_mock_inject_mouse_move(), etc.
int vgfx_platform_process_events(struct vgfx_window* win) {
    (void)win;
    /* Mock backend doesn't generate events automatically */
    /* Events are injected via vgfx_mock_inject_* functions */
    return 1;
}

/// @brief Present framebuffer to window (mock version - no-op).
/// @details The mock backend has no display surface.  The framebuffer remains
///          in memory and can be inspected directly via win->pixels.  This
///          function always succeeds and does nothing.
///
/// @param win Pointer to the ViperGFX window structure (unused)
/// @return 1 (always succeeds)
///
/// @note To verify rendering, tests can directly read win->pixels.
int vgfx_platform_present(struct vgfx_window* win) {
    (void)win;
    /* Mock backend has no display to update */
    /* Framebuffer remains in memory for inspection by tests */
    return 1;
}

/// @brief Sleep for the specified duration (mock version - advances time).
/// @details Advances the global mock time by `ms` milliseconds.  Does NOT
///          actually sleep (no blocking).  Used by vgfx_update() for FPS
///          limiting in tests.
///
/// @param ms Duration to "sleep" in milliseconds
///
/// @post g_mock_time_ms increased by ms (if ms > 0)
///
/// @note This allows deterministic testing of FPS limiting without waiting.
void vgfx_platform_sleep_ms(int32_t ms) {
    if (ms > 0) {
        g_mock_time_ms += ms;
    }
}

/// @brief Get current time in milliseconds (mock version - returns mock time).
/// @details Returns the global mock time, which is controlled by test code.
///          The epoch is arbitrary (typically starts at 0 when tests begin).
///
/// @return Current mock time in milliseconds
///
/// @note Time progression is entirely manual in the mock backend.  Call
///       vgfx_mock_advance_time_ms() or vgfx_platform_sleep_ms() to advance time.
int64_t vgfx_platform_now_ms(void) {
    return g_mock_time_ms;
}

//===----------------------------------------------------------------------===//
// Mock Control Functions (Test API)
//===----------------------------------------------------------------------===//
// These functions are NOT part of the platform abstraction layer.  They are
// test utilities for controlling the mock backend's time simulation.
//===----------------------------------------------------------------------===//

/// @brief Set the mock time to an absolute value.
/// @details Directly sets g_mock_time_ms to the specified value.  Useful for
///          resetting time between tests or simulating specific timestamps.
///
/// @param ms New mock time in milliseconds
///
/// @post g_mock_time_ms == ms
/// @post vgfx_platform_now_ms() returns ms
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_set_time_ms(int64_t ms) {
    g_mock_time_ms = ms;
}

/// @brief Get the current mock time.
/// @details Returns the current value of g_mock_time_ms.  Equivalent to
///          vgfx_platform_now_ms() but more explicit for test code.
///
/// @return Current mock time in milliseconds
///
/// @note Only available in mock backend (not in real platform backends).
int64_t vgfx_mock_get_time_ms(void) {
    return g_mock_time_ms;
}

/// @brief Advance mock time by a relative delta.
/// @details Increments g_mock_time_ms by the specified delta.  Useful for
///          simulating time progression in tests without setting absolute
///          timestamps.
///
/// @param delta_ms Amount to advance time in milliseconds (can be negative)
///
/// @post g_mock_time_ms increased by delta_ms
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_advance_time_ms(int64_t delta_ms) {
    g_mock_time_ms += delta_ms;
}

//===----------------------------------------------------------------------===//
// Event Injection Functions (Test API)
//===----------------------------------------------------------------------===//
// These functions allow tests to synthetically generate events as if they
// came from the OS.  Events are enqueued using the same mechanism as real
// platform backends, so the core library processes them identically.
//===----------------------------------------------------------------------===//

/// @brief Inject a synthetic keyboard event.
/// @details Simulates a key press or release.  Updates win->key_state and
///          enqueues a KEY_DOWN or KEY_UP event.  The event timestamp is
///          set to the current mock time.
///
/// @param window Window handle
/// @param key    Key code (must be < 512 and != VGFX_KEY_UNKNOWN)
/// @param down   1 for key press, 0 for key release
///
/// @pre  window != NULL
/// @pre  key < 512 && key != VGFX_KEY_UNKNOWN
/// @post win->key_state[key] updated to down ? 1 : 0
/// @post Corresponding KEY_DOWN or KEY_UP event enqueued
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_inject_key_event(vgfx_window_t window, vgfx_key_t key, int down) {
    struct vgfx_window* win = (struct vgfx_window*)window;
    if (!win || key == VGFX_KEY_UNKNOWN || key >= 512) return;

    /* Update key state (mirrors real backend behavior) */
    win->key_state[key] = down ? 1 : 0;

    /* Enqueue event with current mock time */
    vgfx_event_t event = {
        .type = down ? VGFX_EVENT_KEY_DOWN : VGFX_EVENT_KEY_UP,
        .time_ms = g_mock_time_ms,
        .data.key = {
            .key = key,
            .is_repeat = 0  /* Mock backend never generates repeats */
        }
    };

    vgfx_internal_enqueue_event(win, &event);
}

/// @brief Inject a synthetic mouse move event.
/// @details Simulates mouse cursor movement.  Updates win->mouse_x and
///          win->mouse_y, then enqueues a MOUSE_MOVE event.  Coordinates
///          can be out of bounds (test code may want to simulate that).
///
/// @param window Window handle
/// @param x      New mouse X coordinate
/// @param y      New mouse Y coordinate
///
/// @pre  window != NULL
/// @post win->mouse_x == x
/// @post win->mouse_y == y
/// @post MOUSE_MOVE event enqueued
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_inject_mouse_move(vgfx_window_t window, int32_t x, int32_t y) {
    struct vgfx_window* win = (struct vgfx_window*)window;
    if (!win) return;

    /* Update mouse position (even if out of bounds - test may want that) */
    win->mouse_x = x;
    win->mouse_y = y;

    /* Enqueue event with current mock time */
    vgfx_event_t event = {
        .type = VGFX_EVENT_MOUSE_MOVE,
        .time_ms = g_mock_time_ms,
        .data.mouse_move = { .x = x, .y = y }
    };

    vgfx_internal_enqueue_event(win, &event);
}

/// @brief Inject a synthetic mouse button event.
/// @details Simulates a mouse button press or release.  Updates
///          win->mouse_button_state and enqueues a MOUSE_DOWN or MOUSE_UP
///          event.  The event includes the current mouse position.
///
/// @param window Window handle
/// @param btn    Button code (must be < 8)
/// @param down   1 for button press, 0 for button release
///
/// @pre  window != NULL
/// @pre  btn < 8
/// @post win->mouse_button_state[btn] updated to down ? 1 : 0
/// @post Corresponding MOUSE_DOWN or MOUSE_UP event enqueued
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_inject_mouse_button(vgfx_window_t window,
                                    vgfx_mouse_button_t btn, int down) {
    struct vgfx_window* win = (struct vgfx_window*)window;
    if (!win || btn >= 8) return;

    /* Update button state (mirrors real backend behavior) */
    win->mouse_button_state[btn] = down ? 1 : 0;

    /* Enqueue event with current mock time and mouse position */
    vgfx_event_t event = {
        .type = down ? VGFX_EVENT_MOUSE_DOWN : VGFX_EVENT_MOUSE_UP,
        .time_ms = g_mock_time_ms,
        .data.mouse_button = {
            .x = win->mouse_x,
            .y = win->mouse_y,
            .button = btn
        }
    };

    vgfx_internal_enqueue_event(win, &event);
}

/// @brief Inject a synthetic resize event.
/// @details Simulates window resize.  Updates win->width, win->height, and
///          win->stride, then reallocates the framebuffer to match the new
///          dimensions.  Enqueues a RESIZE event.
///
/// @param window Window handle
/// @param width  New window width in pixels
/// @param height New window height in pixels
///
/// @pre  window != NULL
/// @post win->width == width
/// @post win->height == height
/// @post win->stride == width * 4
/// @post Framebuffer reallocated and cleared to black
/// @post RESIZE event enqueued
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_inject_resize(vgfx_window_t window, int32_t width, int32_t height) {
    struct vgfx_window* win = (struct vgfx_window*)window;
    if (!win) return;

    /* Update window dimensions (mirrors real backend behavior) */
    win->width = width;
    win->height = height;
    win->stride = width * 4;

    /* Reallocate framebuffer to match new size */
    if (win->pixels) {
        free(win->pixels);
    }

    size_t buffer_size = (size_t)width * (size_t)height * 4;
    win->pixels = (uint8_t*)malloc(buffer_size);

    if (win->pixels) {
        /* Clear framebuffer to black (RGB = 0, 0, 0, A = 0) */
        memset(win->pixels, 0, buffer_size);
    }

    /* Enqueue resize event with current mock time */
    vgfx_event_t event = {
        .type = VGFX_EVENT_RESIZE,
        .time_ms = g_mock_time_ms,
        .data.resize = { .width = width, .height = height }
    };

    vgfx_internal_enqueue_event(win, &event);
}

/// @brief Inject a synthetic close event.
/// @details Simulates the user closing the window (clicking the X button).
///          Enqueues a CLOSE event.  Does NOT actually destroy the window -
///          test code must call vgfx_destroy_window() explicitly.
///
/// @param window Window handle
///
/// @pre  window != NULL
/// @post CLOSE event enqueued
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_inject_close(vgfx_window_t window) {
    struct vgfx_window* win = (struct vgfx_window*)window;
    if (!win) return;

    /* Enqueue close event with current mock time */
    vgfx_event_t event = {
        .type = VGFX_EVENT_CLOSE,
        .time_ms = g_mock_time_ms
    };

    vgfx_internal_enqueue_event(win, &event);
}

/// @brief Inject a synthetic focus event.
/// @details Simulates the window gaining or losing focus (becoming active or
///          inactive).  Enqueues a FOCUS_GAINED or FOCUS_LOST event.
///
/// @param window Window handle
/// @param gained 1 for focus gained, 0 for focus lost
///
/// @pre  window != NULL
/// @post Corresponding FOCUS_GAINED or FOCUS_LOST event enqueued
///
/// @note Only available in mock backend (not in real platform backends).
void vgfx_mock_inject_focus(vgfx_window_t window, int gained) {
    struct vgfx_window* win = (struct vgfx_window*)window;
    if (!win) return;

    /* Enqueue focus event with current mock time */
    vgfx_event_t event = {
        .type = gained ? VGFX_EVENT_FOCUS_GAINED : VGFX_EVENT_FOCUS_LOST,
        .time_ms = g_mock_time_ms
    };

    vgfx_internal_enqueue_event(win, &event);
}

//===----------------------------------------------------------------------===//
// End of Mock Backend
//===----------------------------------------------------------------------===//
