//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_app.c
// Purpose: GUI application lifecycle management for Viper's GUI runtime layer.
//   Creates and owns the ViperGFX window, the root vg_widget container, and the
//   default font. Provides the main loop entry points: rt_gui_app_poll (event
//   dispatch), rt_gui_app_render (layout + paint + present), and
//   rt_gui_app_destroy. Also manages the active modal dialog and a resize
//   callback so the window repaints during macOS live-resize.
//
// Key invariants:
//   - s_current_app is a global pointer valid between app_new and app_destroy;
//     widget constructors use it to inherit the default font.
//   - The root widget must NOT have a fixed size set; layout is driven by the
//     physical window dimensions on every render call.
//   - g_active_dialog is at most one modal dialog; nested dialogs are rejected.
//   - The default font is loaded lazily via rt_gui_ensure_default_font() and
//     uses the embedded font if no file path is configured.
//   - HiDPI scale is applied immediately after window creation; all widget
//     sizes and font sizes are in physical pixels.
//   - Dark theme is applied by default at app creation.
//
// Ownership/Lifetime:
//   - rt_gui_app_t is allocated via rt_obj_new_i64 (GC heap) and zeroed;
//     rt_gui_app_destroy must be called explicitly to release the window and
//     widget tree before GC reclaims the struct.
//   - The root widget and all its children are owned by the vg widget tree;
//     vg_widget_destroy(root) frees the entire subtree.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/runtime/graphics/rt_gui_widgets.c (basic widget implementations),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/lib/graphics/include/vgfx.h (window/event layer)
//
//===----------------------------------------------------------------------===//

#include "fonts/embedded_font.h"
#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

// Global pointer to the current app for widget constructors to access the default font.
rt_gui_app_t *s_current_app = NULL;

// Active modal dialog (NULL = none). Set by rt_gui_set_active_dialog().
// Rendered on top of everything else during rt_gui_app_render().
static vg_dialog_t *g_active_dialog = NULL;

void rt_gui_set_active_dialog(void *dlg)
{
    RT_ASSERT_MAIN_THREAD();
    // Reject nested dialogs — overwriting g_active_dialog would orphan the first.
    if (dlg && g_active_dialog)
        return;
    g_active_dialog = (vg_dialog_t *)dlg;
}

// Resize callback: called from the platform's windowDidResize: (macOS) to
// keep the window repainted during the Cocoa live-resize modal loop.
static void rt_gui_app_resize_render(void *userdata, int32_t w, int32_t h)
{
    (void)w;
    (void)h;
    rt_gui_app_render(userdata);
}

void *rt_gui_app_new(rt_string title, int64_t width, int64_t height)
{
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = (rt_gui_app_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_gui_app_t));
    if (!app)
        return NULL;
    memset(app, 0, sizeof(rt_gui_app_t));

    // Create window
    vgfx_window_params_t params = vgfx_window_params_default();
    params.width = (int32_t)(width < 1 ? 1 : width > INT32_MAX ? INT32_MAX : width);
    params.height = (int32_t)(height < 1 ? 1 : height > INT32_MAX ? INT32_MAX : height);
    char *ctitle = rt_string_to_cstr(title);
    if (ctitle)
    {
        params.title = ctitle;
    }
    params.resizable = 1;

    app->window = vgfx_create_window(&params);
    free(ctitle);

    if (!app->window)
    {
        // app is GC-allocated (rt_obj_new_i64) so it will be reclaimed by the
        // collector. Zero the struct so the GC finalizer (if any) sees clean state.
        memset(app, 0, sizeof(rt_gui_app_t));
        return NULL;
    }

    // Register resize callback so the window repaints during macOS live-resize.
    // Without this, the Cocoa modal resize loop blocks our main thread and
    // the framebuffer stays black until the drag ends.
    vgfx_set_resize_callback(app->window, rt_gui_app_resize_render, app);

    // Create root container. The root is sized dynamically every frame by
    // vg_widget_layout(root, win_w, win_h) in rt_gui_app_render, which reads
    // the current physical window dimensions from vgfx_get_size. Do NOT pin it
    // with vg_widget_set_fixed_size — that creates hard min=max constraints that
    // prevent the layout engine from resizing the root on window resize.
    app->root = vg_widget_create(VG_WIDGET_CONTAINER);

    // Set dark theme by default, then propagate the HiDPI scale factor so that
    // widget creation functions can derive correctly-scaled pixel measurements.
    // Guard: only scale once (cumulative scaling produces wrong metrics).
    vg_theme_set_current(vg_theme_dark());
    static int s_theme_scaled = 0;
    if (!s_theme_scaled)
    {
        float _s = app->window ? vgfx_window_get_scale(app->window) : 1.0f;
        if (_s <= 0.0f)
            _s = 1.0f;
        vg_theme_t *_t = vg_theme_get_current();
        _t->ui_scale = _s;
        // Scale typography so theme-derived font sizes render at the correct
        // visual size on HiDPI displays (e.g. 13pt × 2 = 26pt physical on Retina).
        _t->typography.size_small *= _s;
        _t->typography.size_normal *= _s;
        _t->typography.size_large *= _s;
        _t->typography.size_heading *= _s;
        // Scale spacing presets and per-widget-class metrics.
        _t->spacing.xs *= _s;
        _t->spacing.sm *= _s;
        _t->spacing.md *= _s;
        _t->spacing.lg *= _s;
        _t->spacing.xl *= _s;
        _t->button.height *= _s;
        _t->button.padding_h *= _s;
        _t->input.height *= _s;
        _t->input.padding_h *= _s;
        _t->scrollbar.width *= _s;
        s_theme_scaled = 1;
    }

    s_current_app = app;
    return app;
}

// Ensure the default font is loaded (lazy init on first use).
void rt_gui_ensure_default_font(void)
{
    RT_ASSERT_MAIN_THREAD();
    if (!s_current_app || s_current_app->default_font)
        return;

    // Try the embedded JetBrains Mono Regular first (always available).
    s_current_app->default_font =
        vg_font_load(vg_embedded_font_data, (size_t)vg_embedded_font_size);
    if (s_current_app->default_font)
    {
        // Scale the raster size by the HiDPI factor so glyphs are rendered at
        // native resolution (e.g. 28 px on a 2× Retina display for 14 pt text).
        float _scale = s_current_app->window ? vgfx_window_get_scale(s_current_app->window) : 1.0f;
        s_current_app->default_font_size = 14.0f * _scale;
        return;
    }

    // Fall back to system fonts if the embedded data somehow fails.
    const char *font_paths[] = {"/System/Library/Fonts/Menlo.ttc",
                                "/System/Library/Fonts/SFNSMono.ttf",
                                "/System/Library/Fonts/Monaco.dfont",
                                "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                                "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
                                "C:\\Windows\\Fonts\\consola.ttf",
                                "C:\\Windows\\Fonts\\cour.ttf",
                                NULL};
    for (int i = 0; font_paths[i]; i++)
    {
        s_current_app->default_font = vg_font_load_file(font_paths[i]);
        if (s_current_app->default_font)
        {
            float _scale =
                s_current_app->window ? vgfx_window_get_scale(s_current_app->window) : 1.0f;
            s_current_app->default_font_size = 14.0f * _scale;
            break;
        }
    }
}

void rt_gui_app_destroy(void *app_ptr)
{
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;

    // Clear modal dialog pointer to prevent dangling reference after widget
    // tree destruction (the dialog lives inside the widget tree).
    rt_gui_set_active_dialog(NULL);

    // Free global resources owned by the features module (tooltip, notification
    // manager, file drop data).
    rt_gui_features_cleanup();

    // Clear global pointer before freeing to prevent use-after-free
    if (s_current_app == app)
        s_current_app = NULL;

    free(app->title);
    app->title = NULL;
    if (app->default_font)
    {
        vg_font_destroy(app->default_font);
        app->default_font = NULL;
    }
    if (app->root)
    {
        vg_widget_destroy(app->root);
    }
    if (app->window)
    {
        vgfx_destroy_window(app->window);
    }
}

int64_t rt_gui_app_should_close(void *app_ptr)
{
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return 1;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    return app->should_close;
}

// Forward declarations
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               float parent_abs_x,
                               float parent_abs_y);

void rt_gui_app_poll(void *app_ptr)
{
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (!app->window)
        return;

    // Clear last clicked
    app->last_clicked = NULL;
    rt_gui_set_last_clicked(NULL);

    // Clear per-frame drag-and-drop state on all widgets
    // (drag_over and was_dropped are edge-triggered per frame)
    static vg_widget_t *s_drag_source = NULL;
    static vg_widget_t *s_drag_over_widget = NULL;

    if (s_drag_over_widget)
    {
        s_drag_over_widget->_is_drag_over = false;
        s_drag_over_widget = NULL;
    }

    // Clear shortcut triggered flags from previous frame
    rt_shortcuts_clear_triggered();

    // Get mouse position
    vgfx_mouse_pos(app->window, &app->mouse_x, &app->mouse_y);

    // Poll events
    vgfx_event_t event;
    while (vgfx_poll_event(app->window, &event))
    {
        if (event.type == VGFX_EVENT_CLOSE)
        {
            app->should_close = 1;
            continue;
        }

        // File drop events — collect into g_file_drop via helper
        if (event.type == VGFX_EVENT_FILE_DROP)
        {
            rt_gui_file_drop_add(event.data.file_drop.path);
            continue;
        }

        // Convert platform event to GUI event and dispatch to widget tree
        if (app->root)
        {
            vg_event_t gui_event = vg_event_from_platform(&event);

            // Track mouse position from events
            if (event.type == VGFX_EVENT_MOUSE_MOVE)
            {
                app->mouse_x = event.data.mouse_move.x;
                app->mouse_y = event.data.mouse_move.y;

                // Drag-and-drop: update drag-over state during drag
                if (s_drag_source)
                {
                    vg_widget_t *hit = vg_widget_hit_test(
                        app->root, (float)app->mouse_x, (float)app->mouse_y);
                    // Clear previous drag-over
                    if (s_drag_over_widget && s_drag_over_widget != hit)
                        s_drag_over_widget->_is_drag_over = false;
                    // Set new drag-over if target accepts drops
                    if (hit && hit->is_drop_target && hit != s_drag_source)
                    {
                        hit->_is_drag_over = true;
                        s_drag_over_widget = hit;
                    }
                    else
                    {
                        s_drag_over_widget = NULL;
                    }
                }
            }

            // Drag-and-drop: start drag on mouse-down over draggable widget
            if (event.type == VGFX_EVENT_MOUSE_DOWN && !s_drag_source)
            {
                vg_widget_t *hit =
                    vg_widget_hit_test(app->root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit && hit->draggable)
                {
                    s_drag_source = hit;
                    hit->_is_being_dragged = true;
                }
            }

            // Track clicked widget for Button.WasClicked() + complete drag-and-drop
            if (event.type == VGFX_EVENT_MOUSE_UP)
            {
                vg_widget_t *hit =
                    vg_widget_hit_test(app->root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit)
                {
                    app->last_clicked = hit;
                    // Also set global for rt_widget_was_clicked
                    rt_gui_set_last_clicked(hit);
                }

                // Drag-and-drop: complete drop on mouse-up
                if (s_drag_source)
                {
                    s_drag_source->_is_being_dragged = false;
                    if (hit && hit->is_drop_target && hit != s_drag_source)
                    {
                        // Transfer drag data to drop target
                        free(hit->_drop_received_type);
                        free(hit->_drop_received_data);
                        hit->_drop_received_type =
                            s_drag_source->drag_type ? strdup(s_drag_source->drag_type) : NULL;
                        hit->_drop_received_data =
                            s_drag_source->drag_data ? strdup(s_drag_source->drag_data) : NULL;
                        hit->_was_dropped = true;
                        hit->_is_drag_over = false;
                    }
                    s_drag_source = NULL;
                    s_drag_over_widget = NULL;
                }
            }

            // Check keyboard shortcuts before dispatching KEY_DOWN.
            // If a shortcut matches, set its triggered flag and suppress
            // the KEY_CHAR synthesis (so Ctrl+N doesn't insert 'N').
            int shortcut_matched = 0;
            if (event.type == VGFX_EVENT_KEY_DOWN)
            {
                shortcut_matched =
                    rt_shortcuts_check_key(event.data.key.key, event.data.key.modifiers);
            }

            // If a modal dialog is open, route all events to it and skip the
            // normal widget tree dispatch (dialog is modal).
            if (g_active_dialog && g_active_dialog->is_open)
            {
                if (g_active_dialog->base.vtable && g_active_dialog->base.vtable->handle_event)
                {
                    g_active_dialog->base.vtable->handle_event(&g_active_dialog->base, &gui_event);
                }
                continue;
            }

            // Dispatch all events to widget tree (handles focus, keyboard, etc.)
            vg_event_dispatch(app->root, &gui_event);

            // Synthesize KEY_CHAR event from KEY_DOWN for printable characters.
            // Skip if a shortcut matched (so Ctrl+S doesn't insert 'S').
            // Also skip if modifier keys are held (Ctrl/Cmd+key is not text input).
            if (event.type == VGFX_EVENT_KEY_DOWN && !shortcut_matched)
            {
                int key = event.data.key.key;
                int mods = event.data.key.modifiers;
                int has_ctrl_cmd = (mods & VGFX_MOD_CTRL) || (mods & VGFX_MOD_CMD);
                int has_alt = mods & VGFX_MOD_ALT;

                // Only synthesize KEY_CHAR for plain keys or shift+key (not ctrl/cmd/alt)
                if (!has_ctrl_cmd && !has_alt)
                {
                    uint32_t codepoint = 0;

                    if (key >= ' ' && key <= '~')
                    {
                        int has_shift = mods & VGFX_MOD_SHIFT;
                        if (key >= 'A' && key <= 'Z')
                        {
                            // Letters: shift produces uppercase
                            codepoint = has_shift ? key : key + ('a' - 'A');
                        }
                        else if (has_shift)
                        {
                            // US QWERTY shift mapping — non-US layouts may produce wrong
                        // shifted characters. TODO: use platform-level translation.
                            switch (key)
                            {
                                case '1':
                                    codepoint = '!';
                                    break;
                                case '2':
                                    codepoint = '@';
                                    break;
                                case '3':
                                    codepoint = '#';
                                    break;
                                case '4':
                                    codepoint = '$';
                                    break;
                                case '5':
                                    codepoint = '%';
                                    break;
                                case '6':
                                    codepoint = '^';
                                    break;
                                case '7':
                                    codepoint = '&';
                                    break;
                                case '8':
                                    codepoint = '*';
                                    break;
                                case '9':
                                    codepoint = '(';
                                    break;
                                case '0':
                                    codepoint = ')';
                                    break;
                                case '-':
                                    codepoint = '_';
                                    break;
                                case '=':
                                    codepoint = '+';
                                    break;
                                case '[':
                                    codepoint = '{';
                                    break;
                                case ']':
                                    codepoint = '}';
                                    break;
                                case '\\':
                                    codepoint = '|';
                                    break;
                                case ';':
                                    codepoint = ':';
                                    break;
                                case '\'':
                                    codepoint = '"';
                                    break;
                                case ',':
                                    codepoint = '<';
                                    break;
                                case '.':
                                    codepoint = '>';
                                    break;
                                case '/':
                                    codepoint = '?';
                                    break;
                                case '`':
                                    codepoint = '~';
                                    break;
                                default:
                                    codepoint = key;
                                    break;
                            }
                        }
                        else
                        {
                            codepoint = key;
                        }
                    }

                    if (codepoint != 0)
                    {
                        vg_event_t char_event =
                            vg_event_key(VG_EVENT_KEY_CHAR, (vg_key_t)key, codepoint, 0);
                        vg_event_dispatch(app->root, &char_event);
                    }
                }
            }
        }
    }
}

void rt_gui_app_render(void *app_ptr)
{
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (!app->window)
        return;

    // Ensure a default font is available for widget rendering.
    rt_gui_ensure_default_font();

    // Cache window dimensions once — reused for layout and dialog centering.
    int32_t win_w = 0, win_h = 0;
    vgfx_get_size(app->window, &win_w, &win_h);

    // Perform layout using the GUI library's proper layout system.
    // This handles VBox/HBox flex, padding, spacing, and widget constraints.
    if (app->root)
    {
        vg_widget_layout(app->root, (float)win_w, (float)win_h);
    }

    // Clear with theme background
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_cls(app->window, theme ? theme->colors.bg_secondary : 0xFF1E1E1E);

    // Render widget tree — absolute offsets are accumulated during traversal
    // so widget->x/y stay relative. This is critical: hit testing in poll()
    // uses vg_widget_get_screen_bounds() which walks the parent chain from
    // relative coords. If we converted to absolute here, hit testing would
    // double-count parent offsets and fail.
    if (app->root)
    {
        render_widget_tree(app->window, app->root, 0.0f, 0.0f);
    }

    // Paint overlays (popups, dropdowns) on top of all other widgets.
    // The widget with input capture is typically the one with an open popup.
    vg_widget_t *capture = vg_widget_get_input_capture();
    if (capture && capture->vtable && capture->vtable->paint_overlay)
    {
        // Need to compute absolute position for the overlay
        float abs_x = 0, abs_y = 0;
        vg_widget_get_screen_bounds(capture, &abs_x, &abs_y, NULL, NULL);

        // Temporarily set absolute coords for overlay paint
        float rel_x = capture->x;
        float rel_y = capture->y;
        capture->x = abs_x;
        capture->y = abs_y;

        capture->vtable->paint_overlay(capture, (void *)app->window);

        // Restore relative coords
        capture->x = rel_x;
        capture->y = rel_y;
    }

    // Paint active modal dialog on top of everything else.
    if (g_active_dialog)
    {
        if (g_active_dialog->is_open)
        {
            // Measure on first render so we know the dialog size.
            // Always read measured_width/height (set by measure), not width/height
            // (set by arrange). Reading width before the first arrange would return 0.
            if (g_active_dialog->base.measured_width < 1.0f)
            {
                vg_widget_measure(&g_active_dialog->base, (float)win_w, (float)win_h);
            }
            float dw = g_active_dialog->base.measured_width;
            float dh = g_active_dialog->base.measured_height;
            vg_widget_arrange(
                &g_active_dialog->base, (win_w - dw) / 2.0f, (win_h - dh) / 2.0f, dw, dh);

            // Paint the dialog
            if (g_active_dialog->base.vtable && g_active_dialog->base.vtable->paint)
            {
                g_active_dialog->base.vtable->paint(&g_active_dialog->base, (void *)app->window);
            }
        }
        else
        {
            // Dialog was closed (button clicked) — free and clear
            vg_widget_destroy(&g_active_dialog->base);
            g_active_dialog = NULL;
        }
    }

    // Present
    vgfx_update(app->window);
}

void *rt_gui_app_get_root(void *app_ptr)
{
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return NULL;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    return app->root;
}

void rt_gui_app_set_font(void *app_ptr, void *font, double size)
{
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (app->default_font)
        vg_font_destroy(app->default_font);
    app->default_font = (vg_font_t *)font;
    app->default_font_size = (float)size;
}

// Render widget tree. Accumulates absolute offsets from parent positions
// so paint functions see absolute screen coordinates in widget->x/y.
// Coordinates are restored to relative after painting so that hit testing
// (which walks the parent chain) works correctly during event dispatch.
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               float parent_abs_x,
                               float parent_abs_y)
{
    if (!widget || !widget->visible)
        return;

    // Compute absolute position from relative + parent offset
    float abs_x = widget->x + parent_abs_x;
    float abs_y = widget->y + parent_abs_y;

    // Save relative coords, set absolute for paint
    float rel_x = widget->x;
    float rel_y = widget->y;
    widget->x = abs_x;
    widget->y = abs_y;

    // Delegate to vtable paint if available. All concrete widget types
    // (Label, Button, MenuBar, Toolbar, StatusBar, etc.) have vtable paint.
    // Paint functions use widget->x/y directly (now absolute).
    if (widget->vtable && widget->vtable->paint)
    {
        widget->vtable->paint(widget, (void *)window);
    }

    // Restore relative coords immediately after painting
    widget->x = rel_x;
    widget->y = rel_y;

    // Render children — pass our absolute position as their parent offset
    vg_widget_t *child = widget->first_child;
    while (child)
    {
        render_widget_tree(window, child, abs_x, abs_y);
        child = child->next_sibling;
    }
}

#else /* !VIPER_ENABLE_GRAPHICS */

rt_gui_app_t *s_current_app = NULL;

void rt_gui_set_active_dialog(void *dlg)
{
    (void)dlg;
}

void *rt_gui_app_new(rt_string title, int64_t width, int64_t height)
{
    (void)title;
    (void)width;
    (void)height;
    return NULL;
}

void rt_gui_ensure_default_font(void) {}

void rt_gui_app_destroy(void *app_ptr)
{
    (void)app_ptr;
}

int64_t rt_gui_app_should_close(void *app_ptr)
{
    (void)app_ptr;
    return 1;
}

void rt_gui_app_poll(void *app_ptr)
{
    (void)app_ptr;
}

void rt_gui_app_render(void *app_ptr)
{
    (void)app_ptr;
}

void *rt_gui_app_get_root(void *app_ptr)
{
    (void)app_ptr;
    return NULL;
}

void rt_gui_app_set_font(void *app_ptr, void *font, double size)
{
    (void)app_ptr;
    (void)font;
    (void)size;
}

#endif /* VIPER_ENABLE_GRAPHICS */
