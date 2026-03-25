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
//   - rt_canvas_poll() drives the event loop; returns 0 when window closes.
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

#ifdef VIPER_ENABLE_GRAPHICS

extern int64_t rt_clock_ticks_us(void);

static void rt_canvas_finalize(void *obj)
{
    if (!obj)
        return;

    rt_canvas *canvas = (rt_canvas *)obj;
    if (canvas->title)
    {
        free(canvas->title);
        canvas->title = NULL;
    }
    if (canvas->gfx_win)
    {
        vgfx_destroy_window(canvas->gfx_win);
        canvas->gfx_win = NULL;
    }
}

void *rt_canvas_new(rt_string title, int64_t width, int64_t height)
{
    rt_canvas *canvas = (rt_canvas *)rt_obj_new_i64(0, (int64_t)sizeof(rt_canvas));
    if (!canvas)
        return NULL;

    canvas->vptr = NULL;
    canvas->gfx_win = NULL;
    canvas->should_close = 0;
    canvas->title = NULL;
    canvas->last_event.type = VGFX_EVENT_NONE;
    canvas->last_flip_us = 0;
    canvas->delta_time_ms = 0;
    canvas->dt_max_ms = 0;
    rt_obj_set_finalizer(canvas, rt_canvas_finalize);

    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)width;
    params.height = (int32_t)height;
    if (title)
        params.title = rt_string_cstr(title);

    canvas->gfx_win = vgfx_create_window(&params);
    if (!canvas->gfx_win)
    {
        if (rt_obj_release_check0(canvas))
            rt_obj_free(canvas);
        rt_trap("Canvas.New: failed to create window (display server unavailable?)");
        return NULL;
    }

    // Enable HiDPI coordinate scaling so Canvas apps draw in logical pixels
    // while the framebuffer is at physical resolution.
    vgfx_set_coord_scale(canvas->gfx_win, vgfx_window_get_scale(canvas->gfx_win));

    // Cache the title for GetTitle
    if (title)
    {
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

void rt_canvas_destroy(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;

    if (rt_obj_release_check0(canvas_ptr))
        rt_obj_free(canvas_ptr);
}

int64_t rt_canvas_width(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    int32_t width = 0;
    vgfx_get_size(canvas->gfx_win, &width, NULL);
    return (int64_t)width;
}

int64_t rt_canvas_height(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    int32_t height = 0;
    vgfx_get_size(canvas->gfx_win, NULL, &height);
    return (int64_t)height;
}

int64_t rt_canvas_should_close(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 1;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    return canvas->should_close;
}

void rt_canvas_flip(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return;

    vgfx_update(canvas->gfx_win);

    /* Compute delta time between consecutive Flip() calls */
    int64_t now_us = rt_clock_ticks_us();
    if (canvas->last_flip_us > 0)
    {
        int64_t delta_us = now_us - canvas->last_flip_us;
        canvas->delta_time_ms = delta_us > 0 ? delta_us / 1000 : 0;
    }
    else
    {
        canvas->delta_time_ms = 0; /* first frame */
    }
    canvas->last_flip_us = now_us;

    /* Signal close to the application; caller checks canvas.should_close */
    if (vgfx_close_requested(canvas->gfx_win))
    {
        vgfx_destroy_window(canvas->gfx_win);
        canvas->gfx_win = NULL;
        canvas->should_close = 1;
    }
}

int64_t rt_canvas_get_delta_time(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    int64_t dt = canvas->delta_time_ms;
    if (canvas->dt_max_ms > 0)
    {
        if (dt < 1)
            dt = 1;
        if (dt > canvas->dt_max_ms)
            dt = canvas->dt_max_ms;
    }
    return dt;
}

void rt_canvas_clear(void *canvas_ptr, int64_t color)
{
    if (!canvas_ptr)
        return;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_cls(canvas->gfx_win, (vgfx_color_t)color);
}

int64_t rt_canvas_poll(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;

    // Reset keyboard, mouse, and gamepad per-frame state at the start of polling
    rt_keyboard_begin_frame();
    rt_mouse_begin_frame();
    rt_pad_begin_frame();

    // Poll gamepads for state updates
    rt_pad_poll();

    while (vgfx_poll_event(canvas->gfx_win, &canvas->last_event))
    {
        if (canvas->last_event.type == VGFX_EVENT_CLOSE)
            canvas->should_close = 1;

        // Forward keyboard events to keyboard module
        if (canvas->last_event.type == VGFX_EVENT_KEY_DOWN)
            rt_keyboard_on_key_down((int64_t)canvas->last_event.data.key.key);
        else if (canvas->last_event.type == VGFX_EVENT_KEY_UP)
            rt_keyboard_on_key_up((int64_t)canvas->last_event.data.key.key);

        // Forward mouse events to mouse module (convert physical -> logical)
        if (canvas->last_event.type == VGFX_EVENT_MOUSE_MOVE)
        {
            float cs = vgfx_window_get_scale(canvas->gfx_win);
            if (cs < 0.001f)
                cs = 1.0f;
            int64_t emx = (int64_t)(canvas->last_event.data.mouse_move.x / cs);
            int64_t emy = (int64_t)(canvas->last_event.data.mouse_move.y / cs);
            rt_mouse_update_pos(emx, emy);
        }
        else if (canvas->last_event.type == VGFX_EVENT_MOUSE_DOWN)
        {
            float cs = vgfx_window_get_scale(canvas->gfx_win);
            if (cs < 0.001f)
                cs = 1.0f;
            int64_t emx = (int64_t)(canvas->last_event.data.mouse_button.x / cs);
            int64_t emy = (int64_t)(canvas->last_event.data.mouse_button.y / cs);
            rt_mouse_update_pos(emx, emy);
            rt_mouse_button_down((int64_t)canvas->last_event.data.mouse_button.button);
        }
        else if (canvas->last_event.type == VGFX_EVENT_MOUSE_UP)
        {
            float cs = vgfx_window_get_scale(canvas->gfx_win);
            if (cs < 0.001f)
                cs = 1.0f;
            int64_t emx = (int64_t)(canvas->last_event.data.mouse_button.x / cs);
            int64_t emy = (int64_t)(canvas->last_event.data.mouse_button.y / cs);
            rt_mouse_update_pos(emx, emy);
            rt_mouse_button_up((int64_t)canvas->last_event.data.mouse_button.button);
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

int64_t rt_canvas_key_held(void *canvas_ptr, int64_t key)
{
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

void rt_canvas_set_clip_rect(void *canvas_ptr, int64_t x, int64_t y, int64_t w, int64_t h)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win)
        vgfx_set_clip(canvas->gfx_win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h);
}

void rt_canvas_clear_clip_rect(void *canvas_ptr)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win)
        vgfx_clear_clip(canvas->gfx_win);
}

void rt_canvas_set_title(void *canvas_ptr, rt_string title)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win && title)
    {
        const char *cstr = rt_string_cstr(title);
        vgfx_set_title(canvas->gfx_win, cstr);
        // Update cached title
        free(canvas->title);
        canvas->title = cstr ? strdup(cstr) : NULL;
    }
}

rt_string rt_canvas_get_title(void *canvas_ptr)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->title)
        return rt_string_from_bytes(canvas->title, strlen(canvas->title));
    return rt_string_from_bytes("", 0);
}

void rt_canvas_resize(void *canvas_ptr, int64_t width, int64_t height)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win)
        vgfx_set_window_size(canvas->gfx_win, (int32_t)width, (int32_t)height);
}

void rt_canvas_close(void *canvas_ptr)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win)
    {
        vgfx_destroy_window(canvas->gfx_win);
        canvas->gfx_win = NULL;
        canvas->should_close = 1;
    }
}

void *rt_canvas_screenshot(void *canvas_ptr)
{
    if (!canvas_ptr)
        return NULL;

    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return NULL;

    int32_t w, h;
    if (vgfx_get_size(canvas->gfx_win, &w, &h) == 0)
        return NULL;

    return rt_canvas_copy_rect(canvas_ptr, 0, 0, w, h);
}

void rt_canvas_fullscreen(void *canvas_ptr)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win)
        vgfx_set_fullscreen(canvas->gfx_win, 1);
}

void rt_canvas_windowed(void *canvas_ptr)
{
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas && canvas->gfx_win)
        vgfx_set_fullscreen(canvas->gfx_win, 0);
}

void rt_canvas_set_fps(void *canvas_ptr, int64_t fps)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_set_fps(canvas->gfx_win, (int32_t)fps);
}

int64_t rt_canvas_get_fps(void *canvas_ptr)
{
    if (!canvas_ptr)
        return -1;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return -1;
    return (int64_t)vgfx_get_fps(canvas->gfx_win);
}

void rt_canvas_set_dt_max(void *canvas_ptr, int64_t max_ms)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    canvas->dt_max_ms = max_ms > 0 ? max_ms : 0;
}

int64_t rt_canvas_begin_frame(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;
    rt_canvas_poll(canvas_ptr);
    return rt_canvas_should_close(canvas_ptr) ? 0 : 1;
}

double rt_canvas_get_scale(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 1.0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 1.0;
    return (double)vgfx_window_get_scale(canvas->gfx_win);
}

void rt_canvas_get_position(void *canvas_ptr, int64_t *out_x, int64_t *out_y)
{
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

void rt_canvas_set_position(void *canvas_ptr, int64_t x, int64_t y)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_set_position(canvas->gfx_win, (int32_t)x, (int32_t)y);
}

int8_t rt_canvas_is_maximized(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;
    return (int8_t)vgfx_is_maximized(canvas->gfx_win);
}

void rt_canvas_maximize(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_maximize(canvas->gfx_win);
}

int8_t rt_canvas_is_minimized(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;
    return (int8_t)vgfx_is_minimized(canvas->gfx_win);
}

void rt_canvas_minimize(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_minimize(canvas->gfx_win);
}

void rt_canvas_restore(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_restore(canvas->gfx_win);
}

int8_t rt_canvas_is_focused(void *canvas_ptr)
{
    if (!canvas_ptr)
        return 0;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (!canvas->gfx_win)
        return 0;
    return (int8_t)vgfx_is_focused(canvas->gfx_win);
}

void rt_canvas_focus(void *canvas_ptr)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_focus(canvas->gfx_win);
}

void rt_canvas_prevent_close(void *canvas_ptr, int64_t prevent)
{
    if (!canvas_ptr)
        return;
    rt_canvas *canvas = (rt_canvas *)canvas_ptr;
    if (canvas->gfx_win)
        vgfx_set_prevent_close(canvas->gfx_win, (int32_t)(prevent != 0));
}

void rt_canvas_get_monitor_size(void *canvas_ptr, int64_t *out_w, int64_t *out_h)
{
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

#endif /* VIPER_ENABLE_GRAPHICS */
