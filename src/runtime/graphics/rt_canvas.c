//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_canvas.c
// Purpose: Canvas lifecycle and window management functions. Handles creation,
//   destruction, event polling, resize, fullscreen, window position/focus,
//   and screenshot/save operations.
//
// Key invariants:
//   - All rt_canvas_* functions guard against NULL canvas_ptr and NULL gfx_win.
//   - rt_canvas_flip() presents the back-buffer and must be called each frame.
//   - rt_canvas_poll() drives the event loop and returns the last event type processed.
//   - The rt_canvas struct is GC-managed with a finalizer that destroys gfx_win.
//
// Ownership/Lifetime:
//   - rt_canvas objects are allocated via rt_obj_new_i64 (GC heap); gfx_win is
//     released in rt_canvas_finalize when the GC collects the canvas.
//
// Links: rt_graphics_internal.h, rt_graphics.h (public API),
//        vgfx.h (ViperGFX C API)
//
//===----------------------------------------------------------------------===//

#include "rt_graphics_internal.h"
#include "rt_time.h"

#ifdef VIPER_ENABLE_GRAPHICS

static void rt_canvas_detach_input(vgfx_window_t gfx_win) {
    if (!gfx_win)
        return;
    rt_keyboard_clear_canvas_if_matches(gfx_win);
    rt_mouse_clear_canvas_if_matches(gfx_win);
}

static void rt_canvas_destroy_window(rt_canvas *canvas) {
    if (!canvas || !canvas->gfx_win)
        return;
    rt_canvas_detach_input(canvas->gfx_win);
    vgfx_destroy_window(canvas->gfx_win);
    canvas->gfx_win = NULL;
}

static void rt_canvas_finalize(void *obj) {
    if (!obj)
        return;

    rt_canvas *canvas = (rt_canvas *)obj;
    canvas->magic = 0;
    if (canvas->title) {
        free(canvas->title);
        canvas->title = NULL;
    }
    rt_canvas_destroy_window(canvas);
}

static void rt_canvas_update_mouse_from_physical(vgfx_window_t gfx_win, int32_t x, int32_t y) {
    float scale = vgfx_window_get_scale(gfx_win);
    if (scale < 0.001f)
        scale = 1.0f;
    rt_mouse_update_pos((int64_t)((double)x / (double)scale),
                        (int64_t)((double)y / (double)scale));
}

/// @brief Report that Canvas support is compiled into this runtime.
int8_t rt_canvas_is_available(void) {
    return 1;
}

/// @brief Create a new Canvas window with the given title and dimensions.
/// @details Allocates a GC-managed rt_canvas struct, initializes the ViperGFX
///   window backend, sets up HiDPI coordinate scaling, and initializes keyboard,
///   mouse, and gamepad input subsystems. The canvas is ready for drawing after
///   this call returns.
/// @param title Window title string (displayed in the title bar). NULL for untitled.
/// @param width Canvas width in logical pixels (scaled by HiDPI factor internally).
/// @param height Canvas height in logical pixels.
/// @return Opaque canvas handle, or NULL if window creation fails (e.g., no
///   display server available). On failure, traps with a diagnostic message.
void *rt_canvas_new(rt_string title, int64_t width, int64_t height) {
    rt_canvas *canvas = (rt_canvas *)rt_obj_new_i64(0, (int64_t)sizeof(rt_canvas));
    if (!canvas)
        return NULL;

    canvas->vptr = NULL;
    canvas->magic = RT_CANVAS_MAGIC;
    canvas->gfx_win = NULL;
    canvas->should_close = 0;
    canvas->title = NULL;
    canvas->last_event.type = VGFX_EVENT_NONE;
    canvas->last_flip_us = 0;
    canvas->delta_time_ms = 0;
    canvas->dt_max_ms = 0;
    canvas->clip_enabled = 0;
    canvas->clip_x = 0;
    canvas->clip_y = 0;
    canvas->clip_w = 0;
    canvas->clip_h = 0;
    rt_obj_set_finalizer(canvas, rt_canvas_finalize);

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    char *ctitle = NULL;
    if (title) {
        size_t title_len = (size_t)rt_str_len(title);
        ctitle = (char *)malloc(title_len + 1);
        if (!ctitle) {
            if (rt_obj_release_check0(canvas))
                rt_obj_free(canvas);
            rt_trap("Canvas.New: failed to allocate title buffer");
            return NULL;
        }
        memcpy(ctitle, rt_string_cstr(title), title_len);
        ctitle[title_len] = '\0';
        params.title = ctitle;
    }

    canvas->gfx_win = vgfx_create_window(&params);
    free(ctitle);
    if (!canvas->gfx_win) {
        if (rt_obj_release_check0(canvas))
            rt_obj_free(canvas);
        rt_trap("Canvas.New: failed to create window (display server unavailable?)");
        return NULL;
    }

    // Enable HiDPI coordinate scaling so Canvas apps draw in logical pixels
    // while the framebuffer is at physical resolution.
    vgfx_set_coord_scale(canvas->gfx_win, vgfx_window_get_scale(canvas->gfx_win));

    // Cache the title for GetTitle
    if (title) {
        const char *cstr = rt_string_cstr(title);
        canvas->title = cstr ? strdup(cstr) : NULL;
    }

    // Initialize keyboard input for this canvas
    rt_keyboard_set_canvas(canvas->gfx_win);

    // Initialize mouse input for this canvas
    rt_mouse_set_canvas(canvas->gfx_win);

    // Initialize gamepad input (no canvas reference needed)
    rt_pad_init();

    return canvas;
}

/// @brief Destroy a Canvas, releasing the window and associated resources.
/// @details Decrements the GC refcount. If the count reaches zero, the
///   finalizer frees the title string and destroys the ViperGFX window.
/// @param canvas_ptr Opaque canvas handle from rt_canvas_new(). NULL-safe.
void rt_canvas_destroy(void *canvas_ptr) {
    if (!canvas_ptr)
        return;

    if (rt_obj_release_check0(canvas_ptr))
        rt_obj_free(canvas_ptr);
}

/// @brief Get the canvas width in logical pixels.
/// @details Returns the width as set during creation or after resize. On HiDPI
///   displays, the physical framebuffer may be larger; use GetScale() for the ratio.
/// @param canvas_ptr Canvas handle. Returns 0 if NULL or window not created.
/// @return Width in logical pixels, or 0 on error.
int64_t rt_canvas_width(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    rt_canvas_resync_window_state(canvas);

    int32_t width = 0;
    vgfx_get_size(canvas->gfx_win, &width, NULL);
    return (int64_t)width;
}

/// @brief Get the canvas height in logical pixels.
/// @param canvas_ptr Canvas handle. Returns 0 if NULL or window not created.
/// @return Height in logical pixels, or 0 on error.
int64_t rt_canvas_height(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    rt_canvas_resync_window_state(canvas);

    int32_t height = 0;
    vgfx_get_size(canvas->gfx_win, NULL, &height);
    return (int64_t)height;
}

/// @brief Check whether the user has requested to close the canvas window.
/// @details Returns 1 after the OS close button is pressed or the window is
///   destroyed. Once set, this flag is permanent — the canvas cannot be reopened.
///   The game loop should check this each frame and exit when true.
/// @param canvas_ptr Canvas handle. Returns 1 (should close) if NULL.
/// @return 1 if the window should close, 0 if still open.
int64_t rt_canvas_should_close(void *canvas_ptr) {
    if (!canvas_ptr)
        return 1;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    return canvas->should_close;
}

/// @brief Present the back buffer to the screen and compute delta time.
/// @details Swaps the framebuffer (via vgfx_update), measures the time elapsed
///   since the previous Flip() call (stored as delta_time_ms), and checks if the
///   OS has requested window closure. This function must be called once per frame
///   after all drawing is complete.
///
///   If SetFps() was called, the ViperGFX backend rate-limits Flip() to the
///   target frame rate — no additional sleep is needed.
///
///   Delta time is computed from monotonic microsecond timestamps, converted
///   to milliseconds. The first frame always reports dt=0.
/// @param canvas_ptr Canvas handle. NULL-safe (no-op).
void rt_canvas_flip(void *canvas_ptr) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    rt_canvas_resync_window_state(canvas);
    vgfx_update(canvas->gfx_win);

    /* Compute delta time between consecutive Flip() calls */
    int64_t now_us = rt_clock_ticks_us();
    if (canvas->last_flip_us > 0) {
        int64_t delta_us = now_us - canvas->last_flip_us;
        canvas->delta_time_ms = delta_us > 0 ? (delta_us + 999) / 1000 : 0;
    } else {
        canvas->delta_time_ms = 0; /* first frame */
    }
    canvas->last_flip_us = now_us;

    /* Signal close to the application; caller checks canvas.should_close */
    if (vgfx_close_requested(canvas->gfx_win)) {
        rt_canvas_destroy_window(canvas);
        canvas->should_close = 1;
    }
}

/// @brief Get the time elapsed between the last two Flip() calls, in milliseconds.
/// @details If SetDTMax() was called with a positive value, the returned delta
///   time is clamped to the range [1, max]. This prevents physics explosions
///   after lag spikes or window drags. If dt_max is 0, the raw value is returned.
///   Returns 0 for the first frame (before a second Flip() has occurred).
/// @param canvas_ptr Canvas handle. Returns 0 if NULL.
/// @return Delta time in milliseconds, possibly clamped.
int64_t rt_canvas_get_delta_time(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    int64_t dt = canvas->delta_time_ms;
    if (canvas->dt_max_ms > 0) {
        if (dt == 0)
            return 0;
        if (dt < 1)
            dt = 1;
        if (dt > canvas->dt_max_ms)
            dt = canvas->dt_max_ms;
    }
    return dt;
}

/// @brief Fill the entire canvas with a solid color, erasing all previous drawing.
/// @details Typically called at the start of each frame before drawing game objects.
///   Color format is 0x00RRGGBB (24-bit RGB, no alpha).
/// @param canvas_ptr Canvas handle. NULL-safe (no-op).
/// @param color Fill color in 0x00RRGGBB format.
void rt_canvas_clear(void *canvas_ptr, int64_t color) {
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win) {
        rt_canvas_resync_window_state(canvas);
        vgfx_cls(canvas->gfx_win, (vgfx_color_t)color);
    }
}

/// @brief Poll for input events and update the keyboard, mouse, and gamepad state.
/// @details Processes all pending OS events (key presses, mouse movement, gamepad
///   input, window events). Must be called once per frame before reading input.
///   Internally calls rt_keyboard_begin_frame(), rt_mouse_begin_frame(), and
///   rt_pad_begin_frame() to reset per-frame input state, then dispatches all
///   queued events from the vgfx event queue.
///
///   Returns the type of the last event processed (0 if none). The return value
///   is rarely used directly — most games check Action.Pressed()/Held() instead.
/// @param canvas_ptr Canvas handle. Returns 0 if NULL.
/// @return Last event type processed, or 0 if no events.
int64_t rt_canvas_poll(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    rt_canvas_resync_window_state(canvas);

    // Reset keyboard, mouse, and gamepad per-frame state at the start of polling
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();
    canvas->last_event.type = VGFX_EVENT_NONE;

    // Poll gamepads for state updates
    rt_pad_poll();

    // Pump the native event queue before draining translated events.
    if (!vgfx_pump_events(canvas->gfx_win))
        canvas->should_close = 1;

    while (vgfx_poll_event(canvas->gfx_win, &canvas->last_event)) {
        if (canvas->last_event.type == VGFX_EVENT_CLOSE)
            canvas->should_close = 1;

        // Forward keyboard events to keyboard module
        if (canvas->last_event.type == VGFX_EVENT_KEY_DOWN)
            rt_keyboard_on_key_down((int64_t)canvas->last_event.data.key.key);
        else if (canvas->last_event.type == VGFX_EVENT_KEY_UP)
            rt_keyboard_on_key_up((int64_t)canvas->last_event.data.key.key);
        else if (canvas->last_event.type == VGFX_EVENT_TEXT_INPUT)
            rt_keyboard_text_input((int32_t)canvas->last_event.data.text.codepoint);

        // Forward mouse events to mouse module (convert physical -> logical)
        if (canvas->last_event.type == VGFX_EVENT_MOUSE_MOVE) {
            rt_canvas_update_mouse_from_physical(
                canvas->gfx_win,
                canvas->last_event.data.mouse_move.x,
                canvas->last_event.data.mouse_move.y);
        } else if (canvas->last_event.type == VGFX_EVENT_MOUSE_DOWN) {
            rt_canvas_update_mouse_from_physical(
                canvas->gfx_win,
                canvas->last_event.data.mouse_button.x,
                canvas->last_event.data.mouse_button.y);
            rt_mouse_button_down((int64_t)canvas->last_event.data.mouse_button.button);
        } else if (canvas->last_event.type == VGFX_EVENT_MOUSE_UP) {
            rt_canvas_update_mouse_from_physical(
                canvas->gfx_win,
                canvas->last_event.data.mouse_button.x,
                canvas->last_event.data.mouse_button.y);
            rt_mouse_button_up((int64_t)canvas->last_event.data.mouse_button.button);
        } else if (canvas->last_event.type == VGFX_EVENT_SCROLL) {
            rt_canvas_update_mouse_from_physical(
                canvas->gfx_win, canvas->last_event.data.scroll.x, canvas->last_event.data.scroll.y);
            rt_mouse_update_wheel((double)canvas->last_event.data.scroll.delta_x,
                                  (double)canvas->last_event.data.scroll.delta_y);
        }
    }

    // Finish with the live cursor position so queued historical move events
    // cannot leave the frame using stale coordinates.
    int32_t mx = 0, my = 0;
    vgfx_mouse_pos(canvas->gfx_win, &mx, &my);
    rt_mouse_update_pos((int64_t)mx, (int64_t)my);

    // Update action mapping state AFTER events are processed so that
    // Action.Pressed/Held/Released reflect this frame's input.
    rt_action_update();

    /* Returns the type of the LAST event processed this frame (not a boolean).
     * Close detection is via Canvas.ShouldClose, not via this return value. */
    return (int64_t)canvas->last_event.type;
}

/// @brief Check if a key is currently held down (raw vgfx key query).
/// @details This is a low-level function; most games use Action.Held() instead.
///   Queries the ViperGFX backend directly for the current key state.
/// @param canvas_ptr Canvas handle. Returns 0 if NULL.
/// @param key ViperGFX key code (from vgfx.h constants).
/// @return 1 if the key is currently pressed, 0 if released or invalid.
int64_t rt_canvas_key_held(void *canvas_ptr, int64_t key) {
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    return (int64_t)vgfx_key_down(canvas->gfx_win, (vgfx_key_t)key);
}

//=============================================================================
// Canvas Window Management
//=============================================================================

/// @brief Set the clipping rectangle for all subsequent drawing operations.
/// @details Only pixels within the clip rect will be drawn. Useful for HUD panels,
///   minimap viewports, or any region-restricted rendering. Call ClearClipRect()
///   to restore full-canvas drawing.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param x Left edge of clip region (logical pixels).
/// @param y Top edge of clip region.
/// @param w Width of clip region.
/// @param h Height of clip region.
void rt_canvas_set_clip_rect(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win) {
        canvas->clip_enabled = 1;
        canvas->clip_x = x;
        canvas->clip_y = y;
        canvas->clip_w = w;
        canvas->clip_h = h;
        rt_canvas_resync_window_state(canvas);
    }
}

/// @brief Remove the clipping rectangle, restoring full-canvas drawing.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_clear_clip_rect(void *canvas_ptr) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win) {
        canvas->clip_enabled = 0;
        canvas->clip_x = 0;
        canvas->clip_y = 0;
        canvas->clip_w = 0;
        canvas->clip_h = 0;
        rt_canvas_resync_window_state(canvas);
    }
}

/// @brief Change the window title bar text.
/// @details Updates both the OS window title and the internal cached copy
///   (used by GetTitle). The old cached title is freed.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param title New title string. NULL is ignored.
void rt_canvas_set_title(void *canvas_ptr, rt_string title) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win && title) {
        const char *cstr = rt_string_cstr(title);
        vgfx_set_title(canvas->gfx_win, cstr);
        // Update cached title
        free(canvas->title);
        canvas->title = cstr ? strdup(cstr) : NULL;
    }
}

/// @brief Get the current window title.
/// @param canvas_ptr Canvas handle. Returns empty string if NULL.
/// @return The cached title as an rt_string.
rt_string rt_canvas_get_title(void *canvas_ptr) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->title)
        return rt_string_from_bytes(canvas->title, strlen(canvas->title));
    return rt_string_from_bytes("", 0);
}

/// @brief Resize the canvas window to new dimensions.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param width New width in logical pixels.
/// @param height New height in logical pixels.
void rt_canvas_resize(void *canvas_ptr, int64_t width, int64_t height) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win) {
        vgfx_set_window_size(canvas->gfx_win, (int32_t)width, (int32_t)height);
        rt_canvas_resync_window_state(canvas);
    }
}

/// @brief Programmatically close the canvas window.
/// @details Destroys the ViperGFX window and sets should_close=1. After this
///   call, all drawing operations become no-ops and ShouldClose returns true.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_close(void *canvas_ptr) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win) {
        rt_canvas_destroy_window(canvas);
        canvas->should_close = 1;
    }
}

/// @brief Capture the current canvas contents as a Pixels object.
/// @details Creates a new Pixels object containing a copy of the framebuffer.
///   The returned object is GC-managed and can be saved to BMP/PNG or composited.
/// @param canvas_ptr Canvas handle. Returns NULL if NULL or no window.
/// @return New Pixels object with the canvas contents, or NULL on failure.
void *rt_canvas_screenshot(void *canvas_ptr) {
    rt_canvas *canvas = rt_canvas_checked(canvas_ptr);
    if (!canvas)
        return NULL;
    if (!canvas->gfx_win)
        return NULL;

    int32_t w, h;
    if (vgfx_get_size(canvas->gfx_win, &w, &h) == 0)
        return NULL;

    return rt_canvas_copy_rect(canvas_ptr, 0, 0, w, h);
}

/// @brief Switch the canvas window to fullscreen mode.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_fullscreen(void *canvas_ptr) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win) {
        vgfx_set_fullscreen(canvas->gfx_win, 1);
        rt_canvas_resync_window_state(canvas);
    }
}

/// @brief Switch the canvas window back to windowed mode from fullscreen.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_windowed(void *canvas_ptr) {
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win) {
        vgfx_set_fullscreen(canvas->gfx_win, 0);
        rt_canvas_resync_window_state(canvas);
    }
}

/// @brief Set the target frame rate for the canvas.
/// @details The ViperGFX backend rate-limits Flip() to this target. Pass -1 to
///   disable rate limiting (unlimited FPS). Default is unlimited.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param fps Target frames per second (-1 for unlimited).
void rt_canvas_set_fps(void *canvas_ptr, int64_t fps) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_set_fps(canvas->gfx_win, (int32_t)fps);
}

/// @brief Get the configured target frame rate.
/// @param canvas_ptr Canvas handle. Returns -1 if NULL.
/// @return Target FPS, or -1 if unlimited or error.
int64_t rt_canvas_get_fps(void *canvas_ptr) {
    if (!canvas_ptr)
        return -1;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return -1;
    return (int64_t)vgfx_get_fps(canvas->gfx_win);
}

/// @brief Set the maximum delta time clamp in milliseconds.
/// @details When set to a positive value, get_delta_time() clamps the returned
///   DeltaTime to [1, max_ms]. This prevents physics explosions after lag spikes.
///   Set to 0 to disable clamping. A typical game value is 50ms (20 FPS equivalent).
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param max_ms Maximum delta time in ms. 0 disables clamping. Negative treated as 0.
void rt_canvas_set_dt_max(void *canvas_ptr, int64_t max_ms) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    canvas->dt_max_ms = max_ms > 0 ? max_ms : 0;
}

/// @brief Combined Poll + ShouldClose check for simplified game loops.
/// @details Calls Poll() to process input events, then returns 0 if the window
///   should close, or 1 if the frame should continue. Replaces the common pattern:
///     canvas.Poll();
///     if canvas.ShouldClose { break; }
///   with:
///     while canvas.BeginFrame() != 0 { ... }
/// @param canvas_ptr Canvas handle. Returns 0 (stop) if NULL.
/// @return 1 to continue the frame, 0 to stop (window closing).
int64_t rt_canvas_begin_frame(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;
    rt_canvas_poll(canvas_ptr);
    return rt_canvas_should_close(canvas_ptr) ? 0 : 1;
}

/// @brief Get the HiDPI scale factor for the canvas display.
/// @details Returns 2.0 on Retina/HiDPI displays, 1.0 on standard displays.
///   The canvas draws in logical pixels; the framebuffer is scale_factor × larger.
///   Multiply pixel dimensions by this factor for sharp high-DPI rendering.
/// @param canvas_ptr Canvas handle. Returns 1.0 if NULL.
/// @return Scale factor (typically 1.0 or 2.0).
double rt_canvas_get_scale(void *canvas_ptr) {
    if (!canvas_ptr)
        return 1.0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 1.0;
    return (double)vgfx_window_get_scale(canvas->gfx_win);
}

/// @brief Get the window position on screen in desktop coordinates.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param out_x Pointer to receive X position. NULL-safe (ignored if NULL).
/// @param out_y Pointer to receive Y position. NULL-safe (ignored if NULL).
void rt_canvas_get_position(void *canvas_ptr, int64_t *out_x, int64_t *out_y) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    int32_t x = 0, y = 0;
    vgfx_get_position(canvas->gfx_win, &x, &y);
    if (out_x)
        *out_x = (int64_t)x;
    if (out_y)
        *out_y = (int64_t)y;
}

/// @brief Move the window to a specific position on the desktop.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param x Desktop X coordinate for the window's top-left corner.
/// @param y Desktop Y coordinate.
void rt_canvas_set_position(void *canvas_ptr, int64_t x, int64_t y) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_set_position(canvas->gfx_win, (int32_t)x, (int32_t)y);
}

/// @brief Check if the window is currently maximized.
/// @param canvas_ptr Canvas handle. Returns 0 if NULL.
/// @return 1 if maximized, 0 if not.
int8_t rt_canvas_is_maximized(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;
    return (int8_t)vgfx_is_maximized(canvas->gfx_win);
}

/// @brief Maximize the window to fill the screen (not fullscreen — keeps title bar).
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_maximize(void *canvas_ptr) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_maximize(canvas->gfx_win);
}

/// @brief Check if the window is currently minimized (iconified).
/// @param canvas_ptr Canvas handle. Returns 0 if NULL.
/// @return 1 if minimized, 0 if not.
int8_t rt_canvas_is_minimized(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;
    return (int8_t)vgfx_is_minimized(canvas->gfx_win);
}

/// @brief Minimize the window to the taskbar/dock.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_minimize(void *canvas_ptr) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_minimize(canvas->gfx_win);
}

/// @brief Restore the window from minimized or maximized state to its previous size.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_restore(void *canvas_ptr) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_restore(canvas->gfx_win);
}

/// @brief Check if the window currently has keyboard/mouse focus.
/// @param canvas_ptr Canvas handle. Returns 0 if NULL.
/// @return 1 if focused, 0 if not.
int8_t rt_canvas_is_focused(void *canvas_ptr) {
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;
    return (int8_t)vgfx_is_focused(canvas->gfx_win);
}

/// @brief Request the OS to give keyboard/mouse focus to this window.
/// @param canvas_ptr Canvas handle. NULL-safe.
void rt_canvas_focus(void *canvas_ptr) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_focus(canvas->gfx_win);
}

/// @brief Block or unblock the OS close button.
/// @details When prevent is non-zero, clicking the window's close button does NOT
///   set ShouldClose. The app must call PreventClose(0) before the user can close.
///   Useful for "unsaved changes" prompts.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param prevent Non-zero to block close, 0 to allow.
void rt_canvas_prevent_close(void *canvas_ptr, int64_t prevent) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_set_prevent_close(canvas->gfx_win, (int32_t)(prevent != 0));
}

/// @brief Get the resolution of the monitor containing this window.
/// @param canvas_ptr Canvas handle. NULL-safe.
/// @param out_w Pointer to receive monitor width. NULL-safe.
/// @param out_h Pointer to receive monitor height. NULL-safe.
void rt_canvas_get_monitor_size(void *canvas_ptr, int64_t *out_w, int64_t *out_h) {
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(canvas->gfx_win, &w, &h);
    if (out_w)
        *out_w = (int64_t)w;
    if (out_h)
        *out_h = (int64_t)h;
}

#else
typedef int rt_graphics_disabled_tu_guard;
#endif /* VIPER_ENABLE_GRAPHICS */
