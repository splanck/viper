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

// Global pointer to the app currently bound to the runtime-facing constructors.
rt_gui_app_t *s_current_app = NULL;
static rt_gui_app_t *s_active_app = NULL;

extern int64_t rt_clock_ticks_us(void);

uint64_t rt_gui_now_ms(void) {
    return (uint64_t)(rt_clock_ticks_us() / 1000);
}

static void rt_gui_clear_widget_runtime_state(void) {
    vg_widget_runtime_state_t empty = {0};
    vg_widget_set_runtime_state(&empty);
    *vg_tooltip_manager_get() = (vg_tooltip_manager_t){0};
    vg_theme_set_current(vg_theme_dark());
}

static void rt_gui_save_app_runtime_state(rt_gui_app_t *app) {
    if (!app)
        return;
    vg_widget_get_runtime_state(&app->widget_runtime_state);
    app->tooltip_manager_state = *vg_tooltip_manager_get();
}

static void rt_gui_restore_app_runtime_state(rt_gui_app_t *app) {
    if (!app) {
        rt_gui_clear_widget_runtime_state();
        return;
    }
    vg_widget_set_runtime_state(&app->widget_runtime_state);
    *vg_tooltip_manager_get() = app->tooltip_manager_state;
}

static const vg_theme_t *rt_gui_theme_base(rt_gui_theme_kind_t kind) {
    return (kind == RT_GUI_THEME_LIGHT) ? vg_theme_light() : vg_theme_dark();
}

static void rt_gui_scale_theme(vg_theme_t *theme, float scale) {
    if (!theme)
        return;
    if (scale <= 0.0f)
        scale = 1.0f;
    theme->ui_scale = scale;
    theme->typography.size_small *= scale;
    theme->typography.size_normal *= scale;
    theme->typography.size_large *= scale;
    theme->typography.size_heading *= scale;
    theme->spacing.xs *= scale;
    theme->spacing.sm *= scale;
    theme->spacing.md *= scale;
    theme->spacing.lg *= scale;
    theme->spacing.xl *= scale;
    theme->button.height *= scale;
    theme->button.padding_h *= scale;
    theme->input.height *= scale;
    theme->input.padding_h *= scale;
    theme->scrollbar.width *= scale;
}

void rt_gui_refresh_theme(rt_gui_app_t *app) {
    if (!app)
        return;

    const vg_theme_t *base = rt_gui_theme_base(app->theme_kind);
    float scale = app->window ? vgfx_window_get_scale(app->window) : 1.0f;
    if (scale <= 0.0f)
        scale = 1.0f;

    if (app->theme && app->theme_base == base && app->theme_scale == scale) {
        if (s_active_app == app)
            vg_theme_set_current(app->theme);
        return;
    }

    vg_theme_t *theme = vg_theme_create(base->name, base);
    if (!theme)
        return;
    rt_gui_scale_theme(theme, scale);

    vg_theme_t *old_theme = app->theme;
    app->theme = theme;
    app->theme_base = base;
    app->theme_scale = scale;

    if (s_active_app == app)
        vg_theme_set_current(app->theme);
    if (old_theme)
        vg_theme_destroy(old_theme);
}

void rt_gui_set_theme_kind(rt_gui_app_t *app, rt_gui_theme_kind_t kind) {
    if (!app)
        return;
    app->theme_kind = kind;
    app->theme_base = NULL;
    app->theme_scale = 0.0f;
    rt_gui_refresh_theme(app);
}

rt_gui_app_t *rt_gui_get_active_app(void) {
    return s_active_app;
}

void rt_gui_activate_app(rt_gui_app_t *app) {
    RT_ASSERT_MAIN_THREAD();
    if (app == s_active_app) {
        s_current_app = app;
        rt_gui_refresh_theme(app);
        return;
    }

    if (s_active_app) {
        rt_gui_save_app_runtime_state(s_active_app);
    }

    s_active_app = app;
    s_current_app = app;
    rt_gui_refresh_theme(app);
    rt_gui_restore_app_runtime_state(app);
    rt_gui_macos_menu_sync_app(app);
}

rt_gui_app_t *rt_gui_app_from_widget(vg_widget_t *widget) {
    for (vg_widget_t *w = widget; w; w = w->parent) {
        if (!w->parent && w->user_data) {
            return (rt_gui_app_t *)w->user_data;
        }
    }
    return s_current_app;
}

static void rt_gui_grow_dialog_stack(rt_gui_app_t *app) {
    if (!app || app->dialog_count < app->dialog_cap)
        return;
    int new_cap = app->dialog_cap ? app->dialog_cap * 2 : 4;
    void *p = realloc(app->dialog_stack, (size_t)new_cap * sizeof(*app->dialog_stack));
    if (!p)
        return;
    app->dialog_stack = p;
    app->dialog_cap = new_cap;
}

void rt_gui_sync_modal_root(rt_gui_app_t *app) {
    if (!app) {
        vg_widget_set_modal_root(NULL);
        return;
    }

    vg_dialog_t *top = NULL;
    int write = 0;
    for (int i = 0; i < app->dialog_count; i++) {
        vg_dialog_t *dlg = app->dialog_stack[i];
        if (!dlg || !dlg->is_open)
            continue;
        app->dialog_stack[write++] = dlg;
        top = dlg;
    }
    app->dialog_count = write;
    vg_widget_set_modal_root(top ? &top->base : NULL);
}

void rt_gui_push_dialog(rt_gui_app_t *app, vg_dialog_t *dlg) {
    if (!app || !dlg)
        return;
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] == dlg)
            return;
    }
    rt_gui_grow_dialog_stack(app);
    if (app->dialog_count < app->dialog_cap) {
        dlg->base.user_data = app;
        app->dialog_stack[app->dialog_count++] = dlg;
        rt_gui_sync_modal_root(app);
    }
}

void rt_gui_remove_dialog(rt_gui_app_t *app, vg_dialog_t *dlg) {
    if (!app || !dlg)
        return;
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] != dlg)
            continue;
        memmove(&app->dialog_stack[i],
                &app->dialog_stack[i + 1],
                (size_t)(app->dialog_count - i - 1) * sizeof(*app->dialog_stack));
        app->dialog_count--;
        rt_gui_sync_modal_root(app);
        break;
    }
}

vg_dialog_t *rt_gui_top_dialog(rt_gui_app_t *app) {
    if (!app)
        return NULL;
    rt_gui_sync_modal_root(app);
    return app->dialog_count > 0 ? app->dialog_stack[app->dialog_count - 1] : NULL;
}

static void rt_gui_grow_command_palette_array(rt_gui_app_t *app) {
    if (!app || app->command_palette_count < app->command_palette_cap)
        return;
    int new_cap = app->command_palette_cap ? app->command_palette_cap * 2 : 4;
    void *p = realloc(app->command_palettes, (size_t)new_cap * sizeof(*app->command_palettes));
    if (!p)
        return;
    app->command_palettes = p;
    app->command_palette_cap = new_cap;
}

void rt_gui_register_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette) {
    if (!app || !palette)
        return;
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] == palette)
            return;
    }
    rt_gui_grow_command_palette_array(app);
    if (app->command_palette_count < app->command_palette_cap) {
        app->command_palettes[app->command_palette_count++] = palette;
    }
}

void rt_gui_unregister_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette) {
    if (!app || !palette)
        return;
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] != palette)
            continue;
        memmove(&app->command_palettes[i],
                &app->command_palettes[i + 1],
                (size_t)(app->command_palette_count - i - 1) *
                    sizeof(*app->command_palettes));
        app->command_palette_count--;
        break;
    }
}

static vg_commandpalette_t *rt_gui_top_visible_command_palette(rt_gui_app_t *app) {
    if (!app)
        return NULL;
    for (int i = app->command_palette_count - 1; i >= 0; i--) {
        vg_commandpalette_t *palette = app->command_palettes[i];
        if (palette && palette->is_visible && palette->base.visible)
            return palette;
    }
    return NULL;
}

/// @brief Set the active dialog value.
/// @param dlg
void rt_gui_set_active_dialog(void *dlg) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = s_current_app ? s_current_app : s_active_app;
    if (!app)
        return;
    if (dlg) {
        rt_gui_push_dialog(app, (vg_dialog_t *)dlg);
    } else {
        vg_dialog_t *top = rt_gui_top_dialog(app);
        if (top)
            rt_gui_remove_dialog(app, top);
    }
}

// Resize callback: called from the platform's windowDidResize: (macOS) to
// keep the window repainted during the Cocoa live-resize modal loop.
static void rt_gui_app_resize_render(void *userdata, int32_t w, int32_t h) {
    (void)w;
    (void)h;
    rt_gui_app_render(userdata);
}

static void rt_gui_retain_font(rt_gui_app_t *app, vg_font_t *font) {
    if (!app || !font)
        return;
    if (app->retired_font_count >= app->retired_font_cap) {
        int new_cap = app->retired_font_cap ? app->retired_font_cap * 2 : 4;
        void *p = realloc(app->retired_fonts, (size_t)new_cap * sizeof(*app->retired_fonts));
        if (!p)
            return;
        app->retired_fonts = p;
        app->retired_font_cap = new_cap;
    }
    app->retired_fonts[app->retired_font_count++] = font;
}

static void rt_gui_apply_font_to_widget(vg_widget_t *widget, vg_font_t *font, float size) {
    if (!widget || !font)
        return;
    switch (widget->type) {
        case VG_WIDGET_LABEL:
            vg_label_set_font((vg_label_t *)widget, font, size);
            break;
        case VG_WIDGET_BUTTON:
            vg_button_set_font((vg_button_t *)widget, font, size);
            break;
        case VG_WIDGET_TEXTINPUT:
            vg_textinput_set_font((vg_textinput_t *)widget, font, size);
            break;
        case VG_WIDGET_CHECKBOX: {
            vg_checkbox_t *checkbox = (vg_checkbox_t *)widget;
            checkbox->font = font;
            checkbox->font_size = size;
            break;
        }
        case VG_WIDGET_LISTBOX:
            vg_listbox_set_font((vg_listbox_t *)widget, font, size);
            break;
        case VG_WIDGET_DROPDOWN:
            vg_dropdown_set_font((vg_dropdown_t *)widget, font, size);
            break;
        case VG_WIDGET_SPINNER:
            vg_spinner_set_font((vg_spinner_t *)widget, font, size);
            break;
        case VG_WIDGET_COLORPICKER:
            vg_colorpicker_set_font((vg_colorpicker_t *)widget, font, size);
            break;
        case VG_WIDGET_TREEVIEW:
            vg_treeview_set_font((vg_treeview_t *)widget, font, size);
            break;
        case VG_WIDGET_TABBAR:
            vg_tabbar_set_font((vg_tabbar_t *)widget, font, size);
            break;
        case VG_WIDGET_MENUBAR:
            vg_menubar_set_font((vg_menubar_t *)widget, font, size);
            break;
        case VG_WIDGET_TOOLBAR:
            vg_toolbar_set_font((vg_toolbar_t *)widget, font, size);
            break;
        case VG_WIDGET_STATUSBAR:
            vg_statusbar_set_font((vg_statusbar_t *)widget, font, size);
            break;
        case VG_WIDGET_DIALOG:
            vg_dialog_set_font((vg_dialog_t *)widget, font, size);
            break;
        case VG_WIDGET_CODEEDITOR:
            vg_codeeditor_set_font((vg_codeeditor_t *)widget, font, size);
            break;
        case VG_WIDGET_RADIO: {
            vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
            radio->font = font;
            radio->font_size = size;
            break;
        }
        default:
            break;
    }
    widget->needs_layout = true;
    widget->needs_paint = true;
    for (vg_widget_t *child = widget->first_child; child; child = child->next_sibling) {
        rt_gui_apply_font_to_widget(child, font, size);
    }
}

void rt_gui_apply_default_font(vg_widget_t *widget) {
    if (!widget)
        return;
    rt_gui_app_t *app = rt_gui_app_from_widget(widget);
    if (!app)
        app = s_current_app;
    if (!app)
        return;

    rt_gui_activate_app(app);
    if (!app->default_font)
        rt_gui_ensure_default_font();
    if (!app->default_font)
        return;
    rt_gui_apply_font_to_widget(widget, app->default_font, app->default_font_size);
}

void *rt_gui_app_new(rt_string title, int64_t width, int64_t height) {
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
    if (ctitle) {
        params.title = ctitle;
    }
    params.resizable = 1;

    app->window = vgfx_create_window(&params);
    app->title = ctitle ? strdup(ctitle) : NULL;
    free(ctitle);

    if (!app->window) {
        // app is GC-allocated (rt_obj_new_i64) so it will be reclaimed by the
        // collector. Zero the struct so the GC finalizer (if any) sees clean state.
        free(app->title);
        app->title = NULL;
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

    app->theme_kind = RT_GUI_THEME_DARK;
    app->root->user_data = app;
    app->shortcuts_global_enabled = 1;
    app->manual_tooltip_delay_ms = 500;

    rt_gui_activate_app(app);
    return app;
}

// Ensure the default font is loaded (lazy init on first use).
/// @brief Default the font of the ensure.
void rt_gui_ensure_default_font(void) {
    RT_ASSERT_MAIN_THREAD();
    if (!s_current_app || s_current_app->default_font)
        return;

    // Try the embedded JetBrains Mono Regular first (always available).
    s_current_app->default_font =
        vg_font_load(vg_embedded_font_data, (size_t)vg_embedded_font_size);
    if (s_current_app->default_font) {
        s_current_app->default_font_owned = 1;
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
    for (int i = 0; font_paths[i]; i++) {
        s_current_app->default_font = vg_font_load_file(font_paths[i]);
        if (s_current_app->default_font) {
            s_current_app->default_font_owned = 1;
            float _scale =
                s_current_app->window ? vgfx_window_get_scale(s_current_app->window) : 1.0f;
            s_current_app->default_font_size = 14.0f * _scale;
            break;
        }
    }
}

/// @brief Release resources and destroy the app.
void rt_gui_app_destroy(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    rt_gui_activate_app(app);
    rt_gui_features_cleanup(app);

    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i]) {
            vg_widget_destroy(&app->dialog_stack[i]->base);
        }
    }
    free(app->dialog_stack);
    app->dialog_stack = NULL;
    app->dialog_count = 0;
    app->dialog_cap = 0;

    free(app->command_palettes);
    app->command_palettes = NULL;
    app->command_palette_count = 0;
    app->command_palette_cap = 0;

    for (int i = 0; i < app->shortcut_count; i++) {
        free(app->shortcuts[i].id);
        free(app->shortcuts[i].keys);
        free(app->shortcuts[i].description);
    }
    free(app->shortcuts);
    app->shortcuts = NULL;
    app->shortcut_count = 0;
    app->shortcut_cap = 0;
    free(app->triggered_shortcut_id);
    app->triggered_shortcut_id = NULL;

    if (s_current_app == app || s_active_app == app) {
        rt_gui_macos_menu_app_destroy(app);
        s_current_app = NULL;
        s_active_app = NULL;
        rt_gui_clear_widget_runtime_state();
    }

    free(app->title);
    app->title = NULL;
    if (app->theme) {
        if (vg_theme_get_current() == app->theme)
            vg_theme_set_current(vg_theme_dark());
        vg_theme_destroy(app->theme);
        app->theme = NULL;
    }
    if (app->default_font && app->default_font_owned) {
        vg_font_destroy(app->default_font);
        app->default_font = NULL;
    }
    for (int i = 0; i < app->retired_font_count; i++) {
        if (app->retired_fonts[i])
            vg_font_destroy(app->retired_fonts[i]);
    }
    free(app->retired_fonts);
    app->retired_fonts = NULL;
    app->retired_font_count = 0;
    app->retired_font_cap = 0;
    if (app->root) {
        vg_widget_destroy(app->root);
    }
    if (app->window) {
        vgfx_destroy_window(app->window);
    }
}

/// @brief Should the close of the app.
int64_t rt_gui_app_should_close(void *app_ptr) {
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
static int rt_gui_widget_accepts_drop_type(vg_widget_t *widget, const char *type);
static int rt_gui_send_event_to_widget(vg_widget_t *widget, vg_event_t *event);

static int rt_gui_widget_accepts_drop_type(vg_widget_t *widget, const char *type) {
    if (!widget || !widget->is_drop_target)
        return 0;
    if (!widget->accepted_drop_types || !widget->accepted_drop_types[0] || !type || !type[0])
        return 1;

    const char *p = widget->accepted_drop_types;
    while (*p) {
        while (*p == ' ' || *p == ',')
            p++;
        const char *start = p;
        while (*p && *p != ',')
            p++;
        const char *end = p;
        while (end > start && end[-1] == ' ')
            end--;
        size_t len = (size_t)(end - start);
        if (len == strlen(type) && strncasecmp(start, type, len) == 0)
            return 1;
    }
    return 0;
}

static int rt_gui_send_event_to_widget(vg_widget_t *widget, vg_event_t *event) {
    if (!widget || !event)
        return 0;
    event->target = widget;
    if (event->type == VG_EVENT_MOUSE_MOVE || event->type == VG_EVENT_MOUSE_DOWN ||
        event->type == VG_EVENT_MOUSE_UP || event->type == VG_EVENT_CLICK ||
        event->type == VG_EVENT_DOUBLE_CLICK || event->type == VG_EVENT_MOUSE_WHEEL) {
        float sx = 0.0f, sy = 0.0f;
        vg_widget_get_screen_bounds(widget, &sx, &sy, NULL, NULL);
        event->mouse.x = event->mouse.screen_x - sx;
        event->mouse.y = event->mouse.screen_y - sy;
    }
    return vg_event_send(widget, event) ? 1 : 0;
}

static vg_widget_t *rt_gui_hit_root(rt_gui_app_t *app) {
    vg_dialog_t *top_dialog = rt_gui_top_dialog(app);
    return top_dialog ? &top_dialog->base : app->root;
}

static void rt_gui_layout_command_palette(rt_gui_app_t *app,
                                          vg_commandpalette_t *palette,
                                          int32_t win_w,
                                          int32_t win_h) {
    if (!app || !palette)
        return;
    vg_widget_measure(&palette->base, (float)win_w, (float)win_h);
    float pw = palette->base.measured_width;
    float ph = palette->base.measured_height;
    vg_widget_arrange(&palette->base, (win_w - pw) / 2.0f, win_h * 0.15f, pw, ph);
}

static int rt_gui_palette_contains_point(vg_commandpalette_t *palette, float x, float y) {
    if (!palette)
        return 0;
    return x >= palette->base.x && x < palette->base.x + palette->base.width &&
           y >= palette->base.y && y < palette->base.y + palette->base.height;
}

/// @brief Poll the app.
void rt_gui_app_poll(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (!app->window)
        return;
    rt_gui_activate_app(app);
    rt_gui_sync_modal_root(app);

    // Clear last clicked
    app->last_clicked = NULL;
    app->last_statusbar_clicked = NULL;
    app->last_toolbar_clicked = NULL;
    rt_gui_set_last_clicked(NULL);

    if (app->drag_over_widget) {
        app->drag_over_widget->_is_drag_over = false;
        app->drag_over_widget = NULL;
    }

    // Clear shortcut triggered flags from previous frame
    rt_shortcuts_clear_triggered(app);

    // Get mouse position
    vgfx_mouse_pos(app->window, &app->mouse_x, &app->mouse_y);

    // Poll events
    vgfx_event_t event;
    while (vgfx_poll_event(app->window, &event)) {
        app->last_event_time_ms = (uint64_t)event.time_ms;
        if (event.type == VGFX_EVENT_CLOSE) {
            app->should_close = 1;
            continue;
        }

        // File drop events — collect per-app state.
        if (event.type == VGFX_EVENT_FILE_DROP) {
            rt_gui_file_drop_add(app, event.data.file_drop.path);
            continue;
        }

        // Convert platform event to GUI event and dispatch to widget tree
        if (app->root) {
            vg_event_t gui_event = vg_event_from_platform(&event);
            vg_commandpalette_t *top_palette = rt_gui_top_visible_command_palette(app);
            vg_widget_t *event_root = rt_gui_hit_root(app);
            int32_t win_w = 0, win_h = 0;
            if (top_palette) {
                vgfx_get_size(app->window, &win_w, &win_h);
                rt_gui_layout_command_palette(app, top_palette, win_w, win_h);
            }

            // Track mouse position from events
            if (event.type == VGFX_EVENT_MOUSE_MOVE) {
                app->mouse_x = event.data.mouse_move.x;
                app->mouse_y = event.data.mouse_move.y;

                // Drag-and-drop: update drag-over state during drag
                if (app->drag_source) {
                    vg_widget_t *hit = vg_widget_hit_test(
                        event_root, (float)app->mouse_x, (float)app->mouse_y);
                    // Clear previous drag-over
                    if (app->drag_over_widget && app->drag_over_widget != hit)
                        app->drag_over_widget->_is_drag_over = false;
                    // Set new drag-over if target accepts drops
                    if (hit && hit != app->drag_source &&
                        rt_gui_widget_accepts_drop_type(hit, app->drag_source->drag_type)) {
                        hit->_is_drag_over = true;
                        app->drag_over_widget = hit;
                    } else {
                        app->drag_over_widget = NULL;
                    }
                }

                if (top_palette) {
                    vg_tooltip_manager_on_leave(vg_tooltip_manager_get());
                } else {
                    vg_widget_t *hovered = vg_widget_hit_test(
                        event_root, (float)app->mouse_x, (float)app->mouse_y);
                    if (hovered) {
                        vg_tooltip_manager_on_hover(
                            vg_tooltip_manager_get(), hovered, app->mouse_x, app->mouse_y);
                    } else {
                        vg_tooltip_manager_on_leave(vg_tooltip_manager_get());
                    }
                }
            }

            // Drag-and-drop: start drag on mouse-down over draggable widget
            if (event.type == VGFX_EVENT_MOUSE_DOWN && !app->drag_source) {
                vg_widget_t *hit =
                    vg_widget_hit_test(event_root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit && hit->draggable) {
                    app->drag_source = hit;
                    hit->_is_being_dragged = true;
                }
            }

            // Track clicked widget for Button.WasClicked() + complete drag-and-drop
            if (event.type == VGFX_EVENT_MOUSE_UP) {
                vg_widget_t *hit =
                    vg_widget_hit_test(event_root, (float)app->mouse_x, (float)app->mouse_y);
                if (hit) {
                    app->last_clicked = hit;
                    // Also set global for rt_widget_was_clicked
                    rt_gui_set_last_clicked(hit);
                }

                // Drag-and-drop: complete drop on mouse-up
                if (app->drag_source) {
                    app->drag_source->_is_being_dragged = false;
                    if (hit && hit != app->drag_source &&
                        rt_gui_widget_accepts_drop_type(hit, app->drag_source->drag_type)) {
                        // Transfer drag data to drop target
                        free(hit->_drop_received_type);
                        free(hit->_drop_received_data);
                        hit->_drop_received_type =
                            app->drag_source->drag_type ? strdup(app->drag_source->drag_type) : NULL;
                        hit->_drop_received_data =
                            app->drag_source->drag_data ? strdup(app->drag_source->drag_data)
                                                        : NULL;
                        hit->_was_dropped = true;
                        hit->_is_drag_over = false;
                    }
                    app->drag_source = NULL;
                    app->drag_over_widget = NULL;
                }
            }

            // Check keyboard shortcuts before dispatching KEY_DOWN.
            // If a shortcut matches, set its triggered flag and suppress
            // the KEY_CHAR synthesis (so Ctrl+N doesn't insert 'N').
            if (event.type == VGFX_EVENT_KEY_DOWN) {
                rt_shortcuts_check_key(app, event.data.key.key, event.data.key.modifiers);
            }

            if (top_palette && top_palette->is_visible) {
                int is_mouse_event = gui_event.type == VG_EVENT_MOUSE_MOVE ||
                                     gui_event.type == VG_EVENT_MOUSE_DOWN ||
                                     gui_event.type == VG_EVENT_MOUSE_UP ||
                                     gui_event.type == VG_EVENT_CLICK ||
                                     gui_event.type == VG_EVENT_DOUBLE_CLICK ||
                                     gui_event.type == VG_EVENT_MOUSE_WHEEL;
                int inside_palette =
                    rt_gui_palette_contains_point(
                        top_palette, gui_event.mouse.screen_x, gui_event.mouse.screen_y);

                if (is_mouse_event && inside_palette) {
                    rt_gui_send_event_to_widget(&top_palette->base, &gui_event);
                    continue;
                }
                if (is_mouse_event && !inside_palette) {
                    if (gui_event.type == VG_EVENT_MOUSE_DOWN || gui_event.type == VG_EVENT_CLICK) {
                        vg_commandpalette_hide(top_palette);
                    }
                    continue;
                }
                if (gui_event.type == VG_EVENT_KEY_DOWN || gui_event.type == VG_EVENT_KEY_UP ||
                    gui_event.type == VG_EVENT_KEY_CHAR) {
                    rt_gui_send_event_to_widget(&top_palette->base, &gui_event);
                    continue;
                }
            }

            if (app->notification_manager &&
                rt_gui_send_event_to_widget(&app->notification_manager->base, &gui_event)) {
                continue;
            }

            // Dispatch all events to widget tree (handles focus, keyboard, etc.)
            vg_event_dispatch(app->root, &gui_event);
            rt_gui_sync_modal_root(app);
        }
    }
}

/// @brief Render the app.
void rt_gui_app_render(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    if (!app->window)
        return;
    rt_gui_activate_app(app);

    // Ensure a default font is available for widget rendering.
    rt_gui_ensure_default_font();
    rt_gui_refresh_theme(app);

    if (app->notification_manager && app->default_font) {
        vg_notification_manager_set_font(
            app->notification_manager, app->default_font, app->default_font_size);
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] && app->default_font) {
            vg_commandpalette_set_font(
                app->command_palettes[i], app->default_font, app->default_font_size);
        }
    }
    if (app->manual_tooltip && app->default_font) {
        app->manual_tooltip->font = app->default_font;
        app->manual_tooltip->font_size = app->default_font_size;
    }

    // Cache window dimensions once — reused for layout and dialog centering.
    int32_t win_w = 0, win_h = 0;
    vgfx_get_size(app->window, &win_w, &win_h);

    // Perform layout using the GUI library's proper layout system.
    // This handles VBox/HBox flex, padding, spacing, and widget constraints.
    if (app->root) {
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
    if (app->root) {
        render_widget_tree(app->window, app->root, 0.0f, 0.0f);
    }

    // Paint overlays (popups, dropdowns) on top of all other widgets.
    // The widget with input capture is typically the one with an open popup.
    vg_widget_t *capture = vg_widget_get_input_capture();
    if (capture && capture->vtable && capture->vtable->paint_overlay) {
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

    for (int i = 0; i < app->command_palette_count; i++) {
        vg_commandpalette_t *palette = app->command_palettes[i];
        if (!palette || !palette->is_visible)
            continue;
        rt_gui_layout_command_palette(app, palette, win_w, win_h);
        if (palette->base.vtable && palette->base.vtable->paint) {
            palette->base.vtable->paint(&palette->base, (void *)app->window);
        }
    }

    for (int i = 0; i < app->dialog_count; i++) {
        vg_dialog_t *dlg = app->dialog_stack[i];
        if (!dlg || !dlg->is_open)
            continue;
        if (app->default_font) {
            vg_dialog_set_font(dlg, app->default_font, app->default_font_size);
        }
        if (dlg->base.measured_width < 1.0f || dlg->base.needs_layout) {
            vg_widget_measure(&dlg->base, (float)win_w, (float)win_h);
        }
        float ref_x = 0.0f;
        float ref_y = 0.0f;
        float ref_w = (float)win_w;
        float ref_h = (float)win_h;
        if (dlg->modal_parent) {
            vg_widget_get_screen_bounds(dlg->modal_parent, &ref_x, &ref_y, &ref_w, &ref_h);
        }
        float dw = dlg->base.measured_width;
        float dh = dlg->base.measured_height;
        vg_widget_arrange(
            &dlg->base, ref_x + (ref_w - dw) / 2.0f, ref_y + (ref_h - dh) / 2.0f, dw, dh);
        if (dlg->base.vtable && dlg->base.vtable->paint) {
            dlg->base.vtable->paint(&dlg->base, (void *)app->window);
        }
    }

    uint64_t now_ms = rt_gui_now_ms();

    if (app->notification_manager) {
        app->notification_manager->base.x = 0.0f;
        app->notification_manager->base.y = 0.0f;
        app->notification_manager->base.width = (float)win_w;
        app->notification_manager->base.height = (float)win_h;
        vg_notification_manager_update(app->notification_manager, now_ms);
        if (app->notification_manager->base.vtable &&
            app->notification_manager->base.vtable->paint) {
            app->notification_manager->base.vtable->paint(
                &app->notification_manager->base, (void *)app->window);
        }
    }

    vg_tooltip_manager_t *tooltip_mgr = vg_tooltip_manager_get();
    vg_tooltip_manager_update(tooltip_mgr, now_ms);
    if (tooltip_mgr->active_tooltip && tooltip_mgr->active_tooltip->is_visible) {
        if (app->default_font) {
            tooltip_mgr->active_tooltip->font = app->default_font;
            tooltip_mgr->active_tooltip->font_size = app->default_font_size;
        }
        vg_widget_measure(&tooltip_mgr->active_tooltip->base, (float)win_w, (float)win_h);
        if (tooltip_mgr->active_tooltip->base.vtable &&
            tooltip_mgr->active_tooltip->base.vtable->paint) {
            tooltip_mgr->active_tooltip->base.vtable->paint(
                &tooltip_mgr->active_tooltip->base, (void *)app->window);
        }
    }

    if (app->manual_tooltip && app->manual_tooltip->is_visible) {
        vg_widget_measure(&app->manual_tooltip->base, (float)win_w, (float)win_h);
        if (app->manual_tooltip->base.vtable && app->manual_tooltip->base.vtable->paint) {
            app->manual_tooltip->base.vtable->paint(&app->manual_tooltip->base,
                                                    (void *)app->window);
        }
    }

    // Present
    vgfx_update(app->window);
}

void *rt_gui_app_get_root(void *app_ptr) {
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return NULL;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    return app->root;
}

/// @brief Set the font of the app.
void rt_gui_app_set_font(void *app_ptr, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (!app_ptr)
        return;
    rt_gui_app_t *app = (rt_gui_app_t *)app_ptr;
    rt_gui_activate_app(app);
    vg_font_t *old_font = app->default_font;
    int old_owned = app->default_font_owned;
    app->default_font = (vg_font_t *)font;
    app->default_font_size = (float)size;
    app->default_font_owned = 0;

    if (app->root && app->default_font) {
        rt_gui_apply_font_to_widget(app->root, app->default_font, app->default_font_size);
    }

    if (app->notification_manager && app->default_font) {
        vg_notification_manager_set_font(
            app->notification_manager, app->default_font, app->default_font_size);
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i] && app->default_font) {
            vg_commandpalette_set_font(
                app->command_palettes[i], app->default_font, app->default_font_size);
        }
    }
    if (app->manual_tooltip && app->default_font) {
        app->manual_tooltip->font = app->default_font;
        app->manual_tooltip->font_size = app->default_font_size;
    }
    for (int i = 0; i < app->dialog_count; i++) {
        if (app->dialog_stack[i] && app->default_font) {
            vg_dialog_set_font(app->dialog_stack[i], app->default_font, app->default_font_size);
        }
    }
    if (old_owned && old_font && old_font != app->default_font)
        rt_gui_retain_font(app, old_font);
}

// Render widget tree. Accumulates absolute offsets from parent positions
// so paint functions see absolute screen coordinates in widget->x/y.
// Coordinates are restored to relative after painting so that hit testing
// (which walks the parent chain) works correctly during event dispatch.
static void render_widget_tree(vgfx_window_t window,
                               vg_widget_t *widget,
                               float parent_abs_x,
                               float parent_abs_y) {
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
    if (widget->vtable && widget->vtable->paint) {
        widget->vtable->paint(widget, (void *)window);
    }

    // Restore relative coords immediately after painting
    widget->x = rel_x;
    widget->y = rel_y;

    // Render children — pass our absolute position as their parent offset
    vg_widget_t *child = widget->first_child;
    while (child) {
        render_widget_tree(window, child, abs_x, abs_y);
        child = child->next_sibling;
    }
}

#else /* !VIPER_ENABLE_GRAPHICS */

rt_gui_app_t *s_current_app = NULL;

/// @brief Set the active dialog value.
/// @param dlg
void rt_gui_set_active_dialog(void *dlg) {
    (void)dlg;
}

void *rt_gui_app_new(rt_string title, int64_t width, int64_t height) {
    (void)title;
    (void)width;
    (void)height;
    return NULL;
}

/// @brief Default the font of the ensure.
void rt_gui_ensure_default_font(void) {}

/// @brief Release resources and destroy the app.
void rt_gui_app_destroy(void *app_ptr) {
    (void)app_ptr;
}

/// @brief Should the close of the app.
int64_t rt_gui_app_should_close(void *app_ptr) {
    (void)app_ptr;
    return 1;
}

/// @brief Poll the app.
void rt_gui_app_poll(void *app_ptr) {
    (void)app_ptr;
}

/// @brief Render the app.
void rt_gui_app_render(void *app_ptr) {
    (void)app_ptr;
}

void *rt_gui_app_get_root(void *app_ptr) {
    (void)app_ptr;
    return NULL;
}

/// @brief Set the font of the app.
void rt_gui_app_set_font(void *app_ptr, void *font, double size) {
    (void)app_ptr;
    (void)font;
    (void)size;
}

#endif /* VIPER_ENABLE_GRAPHICS */
