/*
 * ViperGFX - Mock Platform Backend API (Tests Only)
 * Declarations for event injection and time control functions
 */

#pragma once

#include "vgfx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Time Control Functions
 * ============================================================================
 */

/** Set mock time to specific value (milliseconds) */
void vgfx_mock_set_time_ms(int64_t ms);

/** Get current mock time (milliseconds) */
int64_t vgfx_mock_get_time_ms(void);

/** Advance mock time by delta (milliseconds) */
void vgfx_mock_advance_time_ms(int64_t delta_ms);

/*
 * ============================================================================
 * Event Injection Functions
 * ============================================================================
 */

/** Inject synthetic key press or release */
void vgfx_mock_inject_key_event(vgfx_window_t window, vgfx_key_t key, int down);

/** Inject synthetic mouse movement */
void vgfx_mock_inject_mouse_move(vgfx_window_t window, int32_t x, int32_t y);

/** Inject synthetic mouse button press or release */
void vgfx_mock_inject_mouse_button(vgfx_window_t window, vgfx_mouse_button_t btn, int down);

/** Inject synthetic window resize */
void vgfx_mock_inject_resize(vgfx_window_t window, int32_t width, int32_t height);

/** Inject synthetic window close request */
void vgfx_mock_inject_close(vgfx_window_t window);

/** Inject synthetic focus gained/lost event */
void vgfx_mock_inject_focus(vgfx_window_t window, int gained);

#ifdef __cplusplus
}
#endif
