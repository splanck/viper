//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/vgfx_mock.h
// Purpose: Simple mocks/fakes for the ViperGFX C API used in tests.
// Key invariants: Provide ABI-compatible signatures; avoid platform-specific
//                 behavior; explicit, predictable defaults.
// Ownership/Lifetime: Header-only declarations; concrete fakes defined in tests.
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//

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

/** Set mock display scale for newly-created windows */
void vgfx_mock_set_display_scale(float scale);

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

/** Inject synthetic text input */
void vgfx_mock_inject_text_input(vgfx_window_t window, uint32_t codepoint);

/// @brief Inject the beginning of a deterministic native IME session.
/// @details The replacement range uses Unicode-codepoint offsets in committed text. Pass -1 for
///          both values to request replacement of the focused editor's current selection.
/// @param window Mock window that will receive the pending event.
/// @param replacement_start Committed-text codepoint boundary, or -1 for current selection.
/// @param replacement_length Number of committed codepoints to replace, or -1 with the sentinel.
void vgfx_mock_inject_composition_start(vgfx_window_t window,
                                        int32_t replacement_start,
                                        int32_t replacement_length);

/// @brief Inject a native IME preedit text/selection update.
/// @details The mock copies UTF-8 into the same bounded inline representation as real adapters.
///          Selection values are Unicode-codepoint offsets within @p text.
/// @param window Mock window that will receive the pending event.
/// @param text Borrowed NUL-terminated UTF-8 preedit; NULL becomes empty.
/// @param selection_start Preedit selection or caret start in Unicode codepoints.
/// @param selection_length Selected preedit codepoint count.
void vgfx_mock_inject_composition_update(vgfx_window_t window,
                                         const char *text,
                                         int32_t selection_start,
                                         int32_t selection_length);

/// @brief Inject one atomic native IME commit.
/// @details UTF-8 is copied into the pending event. The focused GUI editor will commit it through
///          one history record rather than receiving independent codepoint events.
/// @param window Mock window that will receive the pending event.
/// @param text Borrowed NUL-terminated UTF-8 commit string; NULL becomes empty.
void vgfx_mock_inject_composition_commit(vgfx_window_t window, const char *text);

/// @brief Inject cancellation of the active native IME session.
/// @param window Mock window that will receive the pending event.
void vgfx_mock_inject_composition_cancel(vgfx_window_t window);

/** Inject synthetic scroll wheel/trackpad delta */
void vgfx_mock_inject_scroll(vgfx_window_t window, float dx, float dy, int32_t x, int32_t y);

/** Inject synthetic relative (raw) mouse motion; drained by
 * vgfx_get_relative_deltas() while relative mouse mode is enabled. */
void vgfx_mock_push_relative_delta(vgfx_window_t window, double dx, double dy);

#ifdef __cplusplus
}
#endif
//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/graphics/tests/vgfx_mock.h
// Purpose: Simple mocks/fakes for the ViperGFX C API used in tests.
// Key invariants: Provide ABI-compatible signatures; avoid platform-specific
//                 behavior; explicit, predictable defaults.
// Ownership/Lifetime: Header-only declarations; concrete fakes defined in tests.
// Links: docs/vgfx-testing.md
//
//===----------------------------------------------------------------------===//
