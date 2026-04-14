//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_features.c
// Purpose: Runtime bindings for advanced ViperGUI feature widgets: CommandPalette
//   (fuzzy-searchable command list), Tooltip (hover annotation), Toast
//   (transient notification), Breadcrumb (navigation path), Minimap (scaled
//   document overview), and Drag & Drop. Each widget type wraps the corresponding
//   vg_* C widget with a GC-safe state struct that captures selection/event data
//   for polling by Zia code.
//
// Key invariants:
//   - rt_commandpalette_on_execute callback fires synchronously inside the vg
//     event loop; it strdup's the selected command id for later polling.
//   - Toast messages are transient: they auto-dismiss after the configured
//     duration; no explicit dismiss call is required.
//   - Minimap content is updated via rt_minimap_set_content and rendered at
//     reduced scale; the pixel buffer is owned by the vg_minimap_t widget.
//   - Drag & Drop uses VGFX drag events; drag data is stored as C strings
//     allocated by the platform and freed after the drop handler returns.
//   - All widget constructors accept a parent void* and cast it to vg_widget_t*.
//
// Ownership/Lifetime:
//   - Wrapper state structs (e.g. rt_commandpalette_data_t) are allocated via
//     rt_obj_new_i64 (GC heap); their embedded vg_* pointers are manually freed
//     in the corresponding destroy functions.
//   - selected_command is strdup'd on selection and freed on next selection or
//     at destroy time.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/include/vg.h (ViperGUI C API),
//        src/runtime/rt_platform.h (platform detection helpers)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

/// @brief Return the GUI subsystem's monotonic clock in milliseconds.
///
/// Wrapper around `rt_gui_now_ms`. The unused `app` parameter
/// reserves the option to read a per-app clock in the future
/// (e.g. for paused-game time).
static uint64_t rt_gui_feature_now_ms(rt_gui_app_t *app) {
    (void)app;
    return rt_gui_now_ms();
}

//=============================================================================
// Phase 6: CommandPalette
//=============================================================================

// CommandPalette state tracking
typedef struct {
    rt_gui_app_t *app;
    vg_commandpalette_t *palette;
    char *selected_command;
    int64_t was_selected;
} rt_commandpalette_data_t;

/// @brief Callback fired by the command palette when the user activates a command.
///
/// Captures the selected command's ID into the per-palette data
/// struct so the next `rt_commandpalette_get_selected_id` call
/// returns it. Edge-triggered: each invocation overwrites the
/// previous selection.
static void rt_commandpalette_on_execute(vg_commandpalette_t *palette,
                                         vg_command_t *cmd,
                                         void *user_data) {
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)user_data;
    if (data && cmd && cmd->id) {
        if (data->selected_command)
            free(data->selected_command);
        data->selected_command = strdup(cmd->id);
        data->was_selected = 1;
    }
    (void)palette;
}

/// @brief Create a Ctrl+Shift+P-style command palette overlay.
///
/// The palette renders as a modal overlay above the active app's
/// window and supports fuzzy-search across registered commands.
/// Returns a small wrapper struct (`rt_commandpalette_data_t`)
/// rather than the bare `vg_commandpalette_t*` so callers can
/// poll the most recently activated command via
/// `rt_commandpalette_get_selected_id`.
void *rt_commandpalette_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    if (!app)
        app = s_current_app;
    vg_commandpalette_t *palette = vg_commandpalette_create();
    if (!palette)
        return NULL;

    rt_commandpalette_data_t *data =
        (rt_commandpalette_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_commandpalette_data_t));
    if (!data) {
        vg_commandpalette_destroy(palette);
        return NULL;
    }
    data->app = app;
    data->palette = palette;
    data->selected_command = NULL;
    data->was_selected = 0;
    palette->base.user_data = app;

    vg_commandpalette_set_callbacks(palette, rt_commandpalette_on_execute, NULL, data);
    if (app && app->default_font)
        vg_commandpalette_set_font(palette, app->default_font, app->default_font_size);
    rt_gui_register_command_palette(app, palette);

    return data;
}

/// @brief Release resources and destroy the commandpalette.
void rt_commandpalette_destroy(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    rt_gui_unregister_command_palette(data->app, data->palette);
    if (data->palette) {
        vg_commandpalette_destroy(data->palette);
    }
    if (data->selected_command)
        free(data->selected_command);
}

/// @brief Register a command in the palette's fuzzy-searchable list.
void rt_commandpalette_add_command(void *palette,
                                   rt_string id,
                                   rt_string label,
                                   rt_string category) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    char *clabel = rt_string_to_cstr(label);
    char *ccat = rt_string_to_cstr(category);

    // Prepend category to label if non-empty (e.g. "[File] Open")
    char *display = clabel;
    if (ccat && ccat[0]) {
        size_t len = strlen(ccat) + (clabel ? strlen(clabel) : 0) + 4;
        display = (char *)malloc(len);
        if (display)
            snprintf(display, len, "[%s] %s", ccat, clabel ? clabel : "");
        else
            display = clabel;
    }

    vg_commandpalette_add_command(data->palette, cid, display, NULL, NULL, NULL);

    if (display != clabel)
        free(display);
    free(ccat);
    free(cid);
    free(clabel);
}

/// @brief Register a command in the palette with an associated keyboard shortcut.
///
/// As `rt_commandpalette_add_command` but the shortcut text (e.g.
/// `"Ctrl+S"`) is shown next to the entry. The shortcut is purely
/// informational — wiring it up to actually fire is up to the
/// caller's keyboard handler.
void rt_commandpalette_add_command_with_shortcut(
    void *palette, rt_string id, rt_string label, rt_string category, rt_string shortcut) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    char *clabel = rt_string_to_cstr(label);
    char *cshort = rt_string_to_cstr(shortcut);
    char *ccat = rt_string_to_cstr(category);

    // Prepend category to label if non-empty
    char *display = clabel;
    if (ccat && ccat[0]) {
        size_t len = strlen(ccat) + (clabel ? strlen(clabel) : 0) + 4;
        display = (char *)malloc(len);
        if (display)
            snprintf(display, len, "[%s] %s", ccat, clabel ? clabel : "");
        else
            display = clabel;
    }

    vg_commandpalette_add_command(data->palette, cid, display, cshort, NULL, NULL);

    if (display != clabel)
        free(display);
    free(ccat);
    free(cid);
    free(clabel);
    free(cshort);
}

/// @brief Remove a command from the palette by its ID.
void rt_commandpalette_remove_command(void *palette, rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *cid = rt_string_to_cstr(id);
    vg_commandpalette_remove_command(data->palette, cid);
    if (cid)
        free(cid);
}

/// @brief Remove all entries from the commandpalette.
void rt_commandpalette_clear(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    vg_commandpalette_clear(data->palette);
}

/// @brief Show the commandpalette.
void rt_commandpalette_show(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    data->was_selected = 0; // Reset selection state when showing
    vg_commandpalette_show(data->palette);
}

/// @brief Hide the commandpalette.
void rt_commandpalette_hide(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    vg_commandpalette_hide(data->palette);
}

/// @brief Check whether the command palette is currently visible.
int64_t rt_commandpalette_is_visible(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return 0;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    return data->palette->base.visible ? 1 : 0;
}

/// @brief Set the placeholder of the commandpalette.
void rt_commandpalette_set_placeholder(void *palette, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    char *ctext = rt_string_to_cstr(text);
    if (data->palette)
        vg_commandpalette_set_placeholder(data->palette, ctext);
    if (ctext)
        free(ctext);
}

/// @brief Get the selected command of the commandpalette.
rt_string rt_commandpalette_get_selected_command(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return rt_str_empty();
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    if (data->selected_command) {
        return rt_string_from_bytes(data->selected_command, strlen(data->selected_command));
    }
    return rt_str_empty();
}

/// @brief Check if a command was selected since the last call (edge-triggered, resets).
int64_t rt_commandpalette_was_command_selected(void *palette) {
    RT_ASSERT_MAIN_THREAD();
    if (!palette)
        return 0;
    rt_commandpalette_data_t *data = (rt_commandpalette_data_t *)palette;
    int64_t result = data->was_selected;
    data->was_selected = 0; // Reset after checking
    return result;
}

//=============================================================================
// Phase 7: Tooltip Implementation
//=============================================================================

/// @brief Show the tooltip.
void rt_tooltip_show(rt_string text, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    if (!app)
        return;
    char *ctext = rt_string_to_cstr(text);

    // Create tooltip if needed
    if (!app->manual_tooltip) {
        app->manual_tooltip = vg_tooltip_create();
    }

    if (app->manual_tooltip && ctext) {
        if (app->default_font) {
            app->manual_tooltip->font = app->default_font;
            app->manual_tooltip->font_size = app->default_font_size;
        }
        vg_tooltip_set_timing(app->manual_tooltip, app->manual_tooltip_delay_ms, 100, 0);
        vg_tooltip_set_text(app->manual_tooltip, ctext);
        vg_tooltip_show_at(app->manual_tooltip, (int)x, (int)y);
    }

    if (ctext)
        free(ctext);
}

/// @brief Show a rich tooltip with a title and body at a specific screen position.
void rt_tooltip_show_rich(rt_string title, rt_string body, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    if (!app)
        return;
    char *ctitle = rt_string_to_cstr(title);
    char *cbody = rt_string_to_cstr(body);

    // Create tooltip if needed
    if (!app->manual_tooltip) {
        app->manual_tooltip = vg_tooltip_create();
    }

    if (app->manual_tooltip) {
        // Combines title and body as plain text separated by newline.
        // Rich formatting (bold, colors) would require vg_tooltip_t enhancements.
        const char *t = ctitle ? ctitle : "";
        const char *b = cbody ? cbody : "";
        size_t needed = strlen(t) + strlen(b) + 2;
        char *combined = (char *)malloc(needed);
        if (combined) {
            snprintf(combined, needed, "%s\n%s", t, b);
            if (app->default_font) {
                app->manual_tooltip->font = app->default_font;
                app->manual_tooltip->font_size = app->default_font_size;
            }
            vg_tooltip_set_timing(app->manual_tooltip, app->manual_tooltip_delay_ms, 100, 0);
            vg_tooltip_set_text(app->manual_tooltip, combined);
            free(combined);
        }
        vg_tooltip_show_at(app->manual_tooltip, (int)x, (int)y);
    }

    if (ctitle)
        free(ctitle);
    if (cbody)
        free(cbody);
}

/// @brief Hide the tooltip.
void rt_tooltip_hide(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    if (app && app->manual_tooltip) {
        vg_tooltip_hide(app->manual_tooltip);
    }
}

/// @brief Set the delay of the tooltip.
void rt_tooltip_set_delay(int64_t delay_ms) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    if (!app)
        return;
    if (delay_ms < 0)
        delay_ms = 0;
    app->manual_tooltip_delay_ms = (uint32_t)delay_ms;
    if (app->manual_tooltip) {
        vg_tooltip_set_timing(app->manual_tooltip, app->manual_tooltip_delay_ms, 100, 0);
    }
    if (vg_tooltip_manager_get()->active_tooltip) {
        vg_tooltip_set_timing(
            vg_tooltip_manager_get()->active_tooltip, app->manual_tooltip_delay_ms, 100, 0);
    }
}

/// @brief Set the tooltip of the widget.
void rt_widget_set_tooltip(void *widget, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    char *ctext = rt_string_to_cstr(text);
    vg_widget_set_tooltip_text((vg_widget_t *)widget, ctext);
    if (ctext)
        free(ctext);
}

/// @brief Attach a rich tooltip (title + body) to a widget for hover display.
void rt_widget_set_tooltip_rich(void *widget, rt_string title, rt_string body) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    // Combines title and body as plain text. Rich formatting would require
    // vg_tooltip_t enhancements.
    char *ctitle = rt_string_to_cstr(title);
    char *cbody = rt_string_to_cstr(body);

    const char *t = ctitle ? ctitle : "";
    const char *b = cbody ? cbody : "";
    size_t needed = strlen(t) + strlen(b) + 2;
    char *combined = (char *)malloc(needed);
    if (combined) {
        snprintf(combined, needed, "%s\n%s", t, b);
        vg_widget_set_tooltip_text((vg_widget_t *)widget, combined);
        free(combined);
    }

    if (ctitle)
        free(ctitle);
    if (cbody)
        free(cbody);
}

/// @brief Clear the tooltip of the widget.
void rt_widget_clear_tooltip(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    vg_widget_set_tooltip_text((vg_widget_t *)widget, NULL);
}

//=============================================================================
// Phase 7: Toast/Notifications Implementation
//=============================================================================

// Wrapper to track toast state
typedef struct rt_toast_data {
    rt_gui_app_t *app;
    uint32_t id;
    int64_t was_action_clicked;
    int64_t was_dismissed;
    char *action_label; ///< Optional action button label (owned, may be NULL)
} rt_toast_data_t;

/// @brief Toast action-button callback — flips an edge-trigger when the user clicks "Undo" / "Retry" etc.
static void rt_toast_on_action(uint32_t id, void *user_data) {
    rt_toast_data_t *data = (rt_toast_data_t *)user_data;
    if (!data || data->id != id)
        return;
    data->was_action_clicked = 1;
}

/// @brief Return (and lazily create) the per-app notification manager.
///
/// Notifications stack on the active app's overlay; each app gets
/// its own manager so background apps don't show toasts on the
/// foreground window.
static vg_notification_manager_t *rt_get_notification_manager(rt_gui_app_t *app) {
    if (!app)
        return NULL;
    if (!app->notification_manager) {
        app->notification_manager = vg_notification_manager_create();
        if (app->notification_manager && app->default_font) {
            vg_notification_manager_set_font(
                app->notification_manager, app->default_font, app->default_font_size);
        }
    }
    return app->notification_manager;
}

/// @brief Map a public `RT_TOAST_*` enum to the internal `VG_NOTIFICATION_*` enum.
/// Defaults to INFO for any unknown value.
static vg_notification_type_t rt_toast_type_to_vg(int64_t type) {
    switch (type) {
        case RT_TOAST_INFO:
            return VG_NOTIFICATION_INFO;
        case RT_TOAST_SUCCESS:
            return VG_NOTIFICATION_SUCCESS;
        case RT_TOAST_WARNING:
            return VG_NOTIFICATION_WARNING;
        case RT_TOAST_ERROR:
            return VG_NOTIFICATION_ERROR;
        default:
            return VG_NOTIFICATION_INFO;
    }
}

/// @brief Map a public `RT_TOAST_POSITION_*` enum to the internal `VG_NOTIFICATION_*` corner.
/// Defaults to TOP_RIGHT for unknown positions.
static vg_notification_position_t rt_toast_position_to_vg(int64_t position) {
    switch (position) {
        case RT_TOAST_POSITION_TOP_RIGHT:
            return VG_NOTIFICATION_TOP_RIGHT;
        case RT_TOAST_POSITION_TOP_LEFT:
            return VG_NOTIFICATION_TOP_LEFT;
        case RT_TOAST_POSITION_BOTTOM_RIGHT:
            return VG_NOTIFICATION_BOTTOM_RIGHT;
        case RT_TOAST_POSITION_BOTTOM_LEFT:
            return VG_NOTIFICATION_BOTTOM_LEFT;
        case RT_TOAST_POSITION_TOP_CENTER:
            return VG_NOTIFICATION_TOP_CENTER;
        case RT_TOAST_POSITION_BOTTOM_CENTER:
            return VG_NOTIFICATION_BOTTOM_CENTER;
        default:
            return VG_NOTIFICATION_TOP_RIGHT;
    }
}

/// @brief Show an informational toast notification (auto-dismisses after 3 seconds).
void rt_toast_info(rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    vg_notification_manager_t *mgr = rt_get_notification_manager(app);
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    uint32_t id = vg_notification_show(mgr, VG_NOTIFICATION_INFO, "Info", cmsg, 3000);
    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == id) {
            mgr->notifications[i]->created_at = rt_gui_feature_now_ms(app);
            break;
        }
    }
    if (cmsg)
        free(cmsg);
}

/// @brief Show a success toast notification (auto-dismisses after 3 seconds).
void rt_toast_success(rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    vg_notification_manager_t *mgr = rt_get_notification_manager(app);
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    uint32_t id = vg_notification_show(mgr, VG_NOTIFICATION_SUCCESS, "Success", cmsg, 3000);
    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == id) {
            mgr->notifications[i]->created_at = rt_gui_feature_now_ms(app);
            break;
        }
    }
    if (cmsg)
        free(cmsg);
}

/// @brief Show a warning toast notification (auto-dismisses after 5 seconds).
void rt_toast_warning(rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    vg_notification_manager_t *mgr = rt_get_notification_manager(app);
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    uint32_t id = vg_notification_show(mgr, VG_NOTIFICATION_WARNING, "Warning", cmsg, 5000);
    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == id) {
            mgr->notifications[i]->created_at = rt_gui_feature_now_ms(app);
            break;
        }
    }
    if (cmsg)
        free(cmsg);
}

/// @brief Show an error toast notification (does not auto-dismiss; user must close).
void rt_toast_error(rt_string message) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    vg_notification_manager_t *mgr = rt_get_notification_manager(app);
    if (!mgr)
        return;

    char *cmsg = rt_string_to_cstr(message);
    uint32_t id = vg_notification_show(mgr, VG_NOTIFICATION_ERROR, "Error", cmsg, 0);
    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == id) {
            mgr->notifications[i]->created_at = rt_gui_feature_now_ms(app);
            break;
        }
    }
    if (cmsg)
        free(cmsg);
}

/// @brief Create a configurable toast and return a handle for action / dismissal polling.
///
/// Unlike the `rt_toast_info/success/warning/error` shortcuts which
/// fire-and-forget, this version returns a wrapper struct that
/// callers can poll via `rt_toast_was_action_clicked` to detect
/// user interaction. `duration_ms == 0` means no auto-dismiss.
void *rt_toast_new(rt_string message, int64_t type, int64_t duration_ms) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_get_active_app();
    vg_notification_manager_t *mgr = rt_get_notification_manager(app);
    if (!mgr)
        return NULL;

    char *cmsg = rt_string_to_cstr(message);

    rt_toast_data_t *data = (rt_toast_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_toast_data_t));
    if (!data) {
        free(cmsg);
        return NULL;
    }
    data->app = app;

    data->id =
        vg_notification_show(mgr, rt_toast_type_to_vg(type), NULL, cmsg, (uint32_t)duration_ms);
    data->was_action_clicked = 0;
    data->was_dismissed = 0;
    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == data->id) {
            mgr->notifications[i]->created_at = rt_gui_feature_now_ms(app);
            break;
        }
    }

    if (cmsg)
        free(cmsg);
    return data;
}

/// @brief Set the action of the toast.
void rt_toast_set_action(void *toast, rt_string label) {
    RT_ASSERT_MAIN_THREAD();
    if (!toast)
        return;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    free(data->action_label);
    data->action_label = rt_string_to_cstr(label);
    vg_notification_manager_t *mgr = rt_get_notification_manager(data->app);
    if (!mgr)
        return;
    for (size_t i = 0; i < mgr->notification_count; i++) {
        vg_notification_t *notif = mgr->notifications[i];
        if (!notif || notif->id != data->id)
            continue;
        free(notif->action_label);
        notif->action_label = data->action_label ? strdup(data->action_label) : NULL;
        notif->action_callback = rt_toast_on_action;
        notif->action_user_data = data;
        mgr->base.needs_paint = true;
        break;
    }
}

/// @brief Check if the toast's action button was clicked (edge-triggered).
int64_t rt_toast_was_action_clicked(void *toast) {
    RT_ASSERT_MAIN_THREAD();
    if (!toast)
        return 0;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    int64_t result = data->was_action_clicked;
    data->was_action_clicked = 0;
    return result;
}

/// @brief Check if the toast was dismissed (expired or manually closed).
int64_t rt_toast_was_dismissed(void *toast) {
    RT_ASSERT_MAIN_THREAD();
    if (!toast)
        return 0;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;

    // Return cached result if already known dismissed
    if (data->was_dismissed)
        return 1;

    // Check with the notification manager for auto-timeout dismissal
    vg_notification_manager_t *mgr = rt_get_notification_manager(data->app);
    if (mgr) {
        bool found = false;
        for (size_t i = 0; i < mgr->notification_count; i++) {
            if (mgr->notifications[i] && mgr->notifications[i]->id == data->id) {
                found = true;
                if (mgr->notifications[i]->dismissed) {
                    data->was_dismissed = 1;
                    return 1;
                }
                break;
            }
        }
        // If notification is no longer tracked by the manager, it was dismissed
        if (!found) {
            data->was_dismissed = 1;
            return 1;
        }
    }
    return 0;
}

/// @brief Dismiss the toast.
void rt_toast_dismiss(void *toast) {
    RT_ASSERT_MAIN_THREAD();
    if (!toast)
        return;
    rt_toast_data_t *data = (rt_toast_data_t *)toast;
    vg_notification_manager_t *mgr = rt_get_notification_manager(data->app);
    if (mgr) {
        vg_notification_dismiss(mgr, data->id);
        data->was_dismissed = 1;
    }
}

/// @brief Set the position of the toast.
void rt_toast_set_position(int64_t position) {
    RT_ASSERT_MAIN_THREAD();
    vg_notification_manager_t *mgr = rt_get_notification_manager(rt_gui_get_active_app());
    if (mgr) {
        vg_notification_manager_set_position(mgr, rt_toast_position_to_vg(position));
    }
}

/// @brief Set the max visible of the toast.
void rt_toast_set_max_visible(int64_t count) {
    RT_ASSERT_MAIN_THREAD();
    if (count < 1)
        count = 1;
    if (count > 100)
        count = 100;
    vg_notification_manager_t *mgr = rt_get_notification_manager(rt_gui_get_active_app());
    if (mgr) {
        mgr->max_visible = (uint32_t)count;
    }
}

/// @brief Dismiss the all of the toast.
void rt_toast_dismiss_all(void) {
    RT_ASSERT_MAIN_THREAD();
    vg_notification_manager_t *mgr = rt_get_notification_manager(rt_gui_get_active_app());
    if (mgr) {
        vg_notification_dismiss_all(mgr);
    }
}

//=============================================================================
// Phase 8: Breadcrumb Implementation
//=============================================================================

// Wrapper to track breadcrumb state
typedef struct rt_breadcrumb_data {
    vg_breadcrumb_t *breadcrumb;
    int64_t clicked_index;
    char *clicked_data;
    int64_t was_clicked;
} rt_breadcrumb_data_t;

/// @brief Breadcrumb segment click callback — captures index + per-item data for polling.
///
/// Each breadcrumb item may carry an opaque user-data string
/// (e.g. a path component). On click we deep-copy that into the
/// wrapper so the language layer can read it via
/// `rt_breadcrumb_get_clicked_data` without lifetime concerns.
static void rt_breadcrumb_on_click(vg_breadcrumb_t *bc, int index, void *user_data) {
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)user_data;
    if (!data)
        return;

    data->clicked_index = index;
    data->was_clicked = 1;

    // Store the clicked item's data
    if (data->clicked_data) {
        free(data->clicked_data);
        data->clicked_data = NULL;
    }

    if (index >= 0 && (size_t)index < bc->item_count) {
        if (bc->items[index].user_data) {
            data->clicked_data = strdup((const char *)bc->items[index].user_data);
        }
    }
}

/// @brief Create a breadcrumb navigation widget (e.g. `Home > Docs > Project`).
///
/// Returns a wrapper struct (`rt_breadcrumb_data_t`) so callers can
/// poll segment clicks via `rt_breadcrumb_was_item_clicked`.
void *rt_breadcrumb_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_gui_app_from_handle(parent);
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_breadcrumb_t *bc = vg_breadcrumb_create();
    if (!bc)
        return NULL;

    rt_breadcrumb_data_t *data =
        (rt_breadcrumb_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_breadcrumb_data_t));
    if (!data) {
        vg_breadcrumb_destroy(bc);
        return NULL;
    }
    data->breadcrumb = bc;
    data->clicked_index = -1;
    data->clicked_data = NULL;
    data->was_clicked = 0;

    vg_breadcrumb_set_on_click(bc, rt_breadcrumb_on_click, data);
    if (parent_widget) {
        vg_widget_add_child(parent_widget, &bc->base);
    }
    if (app && app->default_font) {
        vg_breadcrumb_set_font(bc, app->default_font, app->default_font_size);
    }

    return data;
}

/// @brief Release resources and destroy the breadcrumb.
void rt_breadcrumb_destroy(void *crumb) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    if (data->breadcrumb) {
        vg_breadcrumb_destroy(data->breadcrumb);
    }
    if (data->clicked_data)
        free(data->clicked_data);
}

/// @brief Set the path of the breadcrumb.
void rt_breadcrumb_set_path(void *crumb, rt_string path, rt_string separator) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *cpath = rt_string_to_cstr(path);
    char *csep = rt_string_to_cstr(separator);

    // Clear existing items
    vg_breadcrumb_clear(data->breadcrumb);

    // Parse path and add items
    if (cpath && csep && csep[0]) {
        char *saveptr = NULL;
        char *token = rt_strtok_r(cpath, csep, &saveptr);
        while (token) {
            char *label = strdup(token);
            if (label)
                vg_breadcrumb_push(data->breadcrumb, token, label);
            token = rt_strtok_r(NULL, csep, &saveptr);
        }
    }

    if (cpath)
        free(cpath);
    if (csep)
        free(csep);
}

/// @brief Set the items of the breadcrumb.
void rt_breadcrumb_set_items(void *crumb, rt_string items) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *citems = rt_string_to_cstr(items);

    // Clear existing items
    vg_breadcrumb_clear(data->breadcrumb);

    // Parse comma-separated items
    if (citems) {
        char *saveptr = NULL;
        char *token = rt_strtok_r(citems, ",", &saveptr);
        while (token) {
            // Trim whitespace
            while (*token == ' ')
                token++;
            char *end = token + strlen(token) - 1;
            while (end > token && *end == ' ')
                *end-- = '\0';

            char *label = strdup(token);
            if (label)
                vg_breadcrumb_push(data->breadcrumb, token, label);
            token = rt_strtok_r(NULL, ",", &saveptr);
        }
        free(citems);
    }
}

/// @brief Add a path segment to a breadcrumb navigation widget.
void rt_breadcrumb_add_item(void *crumb, rt_string text, rt_string item_data) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;

    char *ctext = rt_string_to_cstr(text);
    char *cdata = rt_string_to_cstr(item_data);

    if (ctext) {
        vg_breadcrumb_push(data->breadcrumb, ctext, cdata ? strdup(cdata) : NULL);
        free(ctext);
    }
    if (cdata)
        free(cdata);
}

/// @brief Remove all entries from the breadcrumb.
void rt_breadcrumb_clear(void *crumb) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    vg_breadcrumb_clear(data->breadcrumb);
}

/// @brief Check if a breadcrumb segment was clicked this frame (edge-triggered).
int64_t rt_breadcrumb_was_item_clicked(void *crumb) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return 0;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    int64_t result = data->was_clicked;
    data->was_clicked = 0; // Reset after checking
    return result;
}

/// @brief Get the clicked index of the breadcrumb.
int64_t rt_breadcrumb_get_clicked_index(void *crumb) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return -1;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    return data->clicked_index;
}

/// @brief Get the clicked data of the breadcrumb.
rt_string rt_breadcrumb_get_clicked_data(void *crumb) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return rt_str_empty();
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    if (data->clicked_data) {
        return rt_string_from_bytes(data->clicked_data, strlen(data->clicked_data));
    }
    return rt_str_empty();
}

/// @brief Set the separator of the breadcrumb.
void rt_breadcrumb_set_separator(void *crumb, rt_string sep) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    char *csep = rt_string_to_cstr(sep);
    if (csep) {
        vg_breadcrumb_set_separator(data->breadcrumb, csep);
        free(csep);
    }
}

/// @brief Set the max items of the breadcrumb.
void rt_breadcrumb_set_max_items(void *crumb, int64_t max) {
    RT_ASSERT_MAIN_THREAD();
    if (!crumb)
        return;
    rt_breadcrumb_data_t *data = (rt_breadcrumb_data_t *)crumb;
    vg_breadcrumb_set_max_items(data->breadcrumb, (int)max);
}

//=============================================================================
// Phase 8: Minimap Implementation
//=============================================================================

// Wrapper to track minimap state
typedef struct rt_minimap_data {
    vg_minimap_t *minimap;
    int64_t width;
} rt_minimap_data_t;

/// @brief Create a minimap widget — a small scaled-down preview of a larger document.
///
/// Used by code editors and large-canvas views. The minimap
/// observes its target widget (set via `rt_minimap_set_target`)
/// and reflects the visible viewport as an overlay rectangle.
void *rt_minimap_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_gui_widget_parent_from_handle(parent);
    vg_minimap_t *minimap = vg_minimap_create(NULL);
    if (!minimap)
        return NULL;

    rt_minimap_data_t *data =
        (rt_minimap_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_minimap_data_t));
    if (!data) {
        vg_minimap_destroy(minimap);
        return NULL;
    }
    data->minimap = minimap;
    data->width = 80; // Default width
    if (parent_widget) {
        vg_widget_add_child(parent_widget, &minimap->base);
    }
    return data;
}

/// @brief Release resources and destroy the minimap.
void rt_minimap_destroy(void *minimap) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    if (data->minimap) {
        vg_minimap_destroy(data->minimap);
    }
}

/// @brief Bind the editor of the minimap.
void rt_minimap_bind_editor(void *minimap, void *editor) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap || !editor)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_editor(data->minimap, (vg_codeeditor_t *)editor);
}

/// @brief Unbind the editor of the minimap.
void rt_minimap_unbind_editor(void *minimap) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_editor(data->minimap, NULL);
}

/// @brief Set the width of the minimap.
void rt_minimap_set_width(void *minimap, int64_t width) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    data->width = width;
    data->minimap->base.width = (float)width;
}

/// @brief Get the width of the minimap.
int64_t rt_minimap_get_width(void *minimap) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return 0;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    return data->width;
}

/// @brief Set the scale of the minimap.
void rt_minimap_set_scale(void *minimap, double scale) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_scale(data->minimap, (float)scale);
}

/// @brief Set the show slider of the minimap.
void rt_minimap_set_show_slider(void *minimap, int64_t show) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_set_show_viewport(data->minimap, show != 0);
}

/// @brief Add a highlighted marker region to the minimap.
void rt_minimap_add_marker(void *minimap, int64_t line, int64_t color, int64_t type) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_t *mm = data->minimap;

    if (mm->marker_count >= mm->marker_cap) {
        int new_cap = mm->marker_cap ? mm->marker_cap * 2 : 8;
        void *p = realloc(mm->markers, (size_t)new_cap * sizeof(*mm->markers));
        if (!p)
            return;
        mm->markers = p;
        mm->marker_cap = new_cap;
    }
    struct vg_minimap_marker *m = &mm->markers[mm->marker_count++];
    m->line = (int)line;
    m->color = (uint32_t)color;
    m->type = (int)type;
    mm->base.needs_paint = true;
}

/// @brief Clear all markers from the minimap.
void rt_minimap_remove_markers(void *minimap, int64_t line) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_t *mm = data->minimap;
    int w = 0;
    for (int i = 0; i < mm->marker_count; i++) {
        if (mm->markers[i].line != (int)line)
            mm->markers[w++] = mm->markers[i];
    }
    if (w != mm->marker_count) {
        mm->marker_count = w;
        mm->base.needs_paint = true;
    }
}

/// @brief Clear the markers of the minimap.
void rt_minimap_clear_markers(void *minimap) {
    RT_ASSERT_MAIN_THREAD();
    if (!minimap)
        return;
    rt_minimap_data_t *data = (rt_minimap_data_t *)minimap;
    vg_minimap_t *mm = data->minimap;
    free(mm->markers);
    mm->markers = NULL;
    mm->marker_count = 0;
    mm->marker_cap = 0;
    mm->base.needs_paint = true;
}

//=============================================================================
// Phase 8: Drag and Drop Implementation
//=============================================================================

// Drag and drop state per widget (would need to be stored in widget user_data)
typedef struct rt_drag_drop_data {
    int64_t is_draggable;
    char *drag_type;
    char *drag_data;
    int64_t is_drop_target;
    char *accepted_types;
    int64_t is_being_dragged;
    int64_t is_drag_over;
    int64_t was_dropped;
    char *drop_type;
    char *drop_data;
} rt_drag_drop_data_t;

/// @brief Set the draggable of the widget.
void rt_widget_set_draggable(void *widget, int64_t draggable) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    ((vg_widget_t *)widget)->draggable = draggable != 0;
}

/// @brief Set the drag data of the widget.
void rt_widget_set_drag_data(void *widget, rt_string type, rt_string data) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    vg_widget_t *w = (vg_widget_t *)widget;
    free(w->drag_type);
    free(w->drag_data);
    w->drag_type = rt_string_to_cstr(type);
    w->drag_data = rt_string_to_cstr(data);
}

/// @brief Check whether a widget is currently being dragged.
int64_t rt_widget_is_being_dragged(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return ((vg_widget_t *)widget)->_is_being_dragged ? 1 : 0;
}

/// @brief Get a value from the widget.
void rt_widget_set_drop_target(void *widget, int64_t target) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    ((vg_widget_t *)widget)->is_drop_target = target != 0;
}

/// @brief Set the accepted drop types of the widget.
void rt_widget_set_accepted_drop_types(void *widget, rt_string types) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return;
    vg_widget_t *w = (vg_widget_t *)widget;
    free(w->accepted_drop_types);
    w->accepted_drop_types = rt_string_to_cstr(types);
}

/// @brief Check whether a dragged item is hovering over this drop target.
int64_t rt_widget_is_drag_over(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    return ((vg_widget_t *)widget)->_is_drag_over ? 1 : 0;
}

/// @brief Check whether a drop was completed on this widget this frame.
int64_t rt_widget_was_dropped(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return 0;
    vg_widget_t *w = (vg_widget_t *)widget;
    int64_t result = w->_was_dropped ? 1 : 0;
    w->_was_dropped = false; // Clear after read (edge-triggered)
    return result;
}

/// @brief Get the drop type of the widget.
rt_string rt_widget_get_drop_type(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return rt_str_empty();
    vg_widget_t *w = (vg_widget_t *)widget;
    if (w->_drop_received_type)
        return rt_string_from_bytes(w->_drop_received_type, strlen(w->_drop_received_type));
    return rt_str_empty();
}

/// @brief Get the drop data of the widget.
rt_string rt_widget_get_drop_data(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    if (!widget)
        return rt_str_empty();
    vg_widget_t *w = (vg_widget_t *)widget;
    if (w->_drop_received_data)
        return rt_string_from_bytes(w->_drop_received_data, strlen(w->_drop_received_data));
    return rt_str_empty();
}

/// @brief Check whether files were dropped onto the app window this frame.
int64_t rt_app_was_file_dropped(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app)
        return 0;
    int64_t result = gui_app->file_drop.was_dropped;
    gui_app->file_drop.was_dropped = 0;
    return result;
}

/// @brief Get the number of files dropped onto the app window.
int64_t rt_app_get_dropped_file_count(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app ? gui_app->file_drop.file_count : 0;
}

/// @brief Get the dropped file of the app.
rt_string rt_app_get_dropped_file(void *app, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app && index >= 0 && index < gui_app->file_drop.file_count &&
        gui_app->file_drop.files) {
        char *file = gui_app->file_drop.files[index];
        if (file) {
            return rt_string_from_bytes(file, strlen(file));
        }
    }
    return rt_str_empty();
}

/// @brief Add an element to the file.
void rt_gui_file_drop_add(rt_gui_app_t *app, const char *path) {
    if (!app || !path)
        return;
    // Free old data on first file of a new batch
    if (!app->file_drop.was_dropped) {
        for (int64_t i = 0; i < app->file_drop.file_count; i++)
            free(app->file_drop.files[i]);
        free(app->file_drop.files);
        app->file_drop.files = NULL;
        app->file_drop.file_count = 0;
    }

    // Grow array
    char **new_files = (char **)realloc(app->file_drop.files,
                                        (size_t)(app->file_drop.file_count + 1) * sizeof(char *));
    if (!new_files)
        return;
    app->file_drop.files = new_files;
    app->file_drop.files[app->file_drop.file_count++] = strdup(path);
    app->file_drop.was_dropped = 1;
}

/// @brief Cleanup the features.
void rt_gui_features_cleanup(rt_gui_app_t *app) {
    if (!app)
        return;
    if (app->manual_tooltip) {
        vg_tooltip_destroy(app->manual_tooltip);
        app->manual_tooltip = NULL;
    }
    if (app->notification_manager) {
        vg_notification_manager_destroy(app->notification_manager);
        app->notification_manager = NULL;
    }
    for (int i = 0; i < app->command_palette_count; i++) {
        if (app->command_palettes[i]) {
            vg_commandpalette_destroy(app->command_palettes[i]);
            app->command_palettes[i] = NULL;
        }
    }
    for (int64_t i = 0; i < app->file_drop.file_count; i++)
        free(app->file_drop.files[i]);
    free(app->file_drop.files);
    app->file_drop = (rt_gui_file_drop_data_t){0};
}

#else /* !VIPER_ENABLE_GRAPHICS */

// ===========================================================================
// Headless stubs — same prototypes as the real implementations above so
// non-graphical builds (server / CLI / ViperDOS) link without pulling in
// the GUI. Each stub no-ops or returns a sentinel; doc comments are
// inherited from the real functions above by virtue of identical names.
// ===========================================================================

void *rt_commandpalette_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Release resources and destroy the commandpalette.
void rt_commandpalette_destroy(void *palette) {
    (void)palette;
}

/// @brief Register a command in the palette's fuzzy-searchable list.
void rt_commandpalette_add_command(void *palette,
                                   rt_string id,
                                   rt_string label,
                                   rt_string category) {
    (void)palette;
    (void)id;
    (void)label;
    (void)category;
}

void rt_commandpalette_add_command_with_shortcut(
    void *palette, rt_string id, rt_string label, rt_string category, rt_string shortcut) {
    (void)palette;
    (void)id;
    (void)label;
    (void)category;
    (void)shortcut;
}

/// @brief Remove a command from the palette by its ID.
void rt_commandpalette_remove_command(void *palette, rt_string id) {
    (void)palette;
    (void)id;
}

/// @brief Remove all entries from the commandpalette.
void rt_commandpalette_clear(void *palette) {
    (void)palette;
}

/// @brief Show the commandpalette.
void rt_commandpalette_show(void *palette) {
    (void)palette;
}

/// @brief Hide the commandpalette.
void rt_commandpalette_hide(void *palette) {
    (void)palette;
}

/// @brief Check whether the command palette is currently visible.
int64_t rt_commandpalette_is_visible(void *palette) {
    (void)palette;
    return 0;
}

/// @brief Set the placeholder of the commandpalette.
void rt_commandpalette_set_placeholder(void *palette, rt_string text) {
    (void)palette;
    (void)text;
}

/// @brief Get the selected command of the commandpalette.
rt_string rt_commandpalette_get_selected_command(void *palette) {
    (void)palette;
    return rt_str_empty();
}

/// @brief Check if a command was selected since the last call (edge-triggered, resets).
int64_t rt_commandpalette_was_command_selected(void *palette) {
    (void)palette;
    return 0;
}

/// @brief Show the tooltip.
void rt_tooltip_show(rt_string text, int64_t x, int64_t y) {
    (void)text;
    (void)x;
    (void)y;
}

/// @brief Show a rich tooltip with a title and body at a specific screen position.
void rt_tooltip_show_rich(rt_string title, rt_string body, int64_t x, int64_t y) {
    (void)title;
    (void)body;
    (void)x;
    (void)y;
}

/// @brief Hide the tooltip.
void rt_tooltip_hide(void) {}

/// @brief Set the delay of the tooltip.
void rt_tooltip_set_delay(int64_t delay_ms) {
    (void)delay_ms;
}

/// @brief Set the tooltip of the widget.
void rt_widget_set_tooltip(void *widget, rt_string text) {
    (void)widget;
    (void)text;
}

/// @brief Attach a rich tooltip (title + body) to a widget for hover display.
void rt_widget_set_tooltip_rich(void *widget, rt_string title, rt_string body) {
    (void)widget;
    (void)title;
    (void)body;
}

/// @brief Clear the tooltip of the widget.
void rt_widget_clear_tooltip(void *widget) {
    (void)widget;
}

/// @brief Show an informational toast notification (auto-dismisses after 3 seconds).
void rt_toast_info(rt_string message) {
    (void)message;
}

/// @brief Show a success toast notification (auto-dismisses after 3 seconds).
void rt_toast_success(rt_string message) {
    (void)message;
}

/// @brief Show a warning toast notification (auto-dismisses after 5 seconds).
void rt_toast_warning(rt_string message) {
    (void)message;
}

/// @brief Show an error toast notification (does not auto-dismiss; user must close).
void rt_toast_error(rt_string message) {
    (void)message;
}

void *rt_toast_new(rt_string message, int64_t type, int64_t duration_ms) {
    (void)message;
    (void)type;
    (void)duration_ms;
    return NULL;
}

/// @brief Set the action of the toast.
void rt_toast_set_action(void *toast, rt_string label) {
    (void)toast;
    (void)label;
}

/// @brief Check if the toast's action button was clicked (edge-triggered).
int64_t rt_toast_was_action_clicked(void *toast) {
    (void)toast;
    return 0;
}

/// @brief Check if the toast was dismissed (expired or manually closed).
int64_t rt_toast_was_dismissed(void *toast) {
    (void)toast;
    return 0;
}

/// @brief Dismiss the toast.
void rt_toast_dismiss(void *toast) {
    (void)toast;
}

/// @brief Set the position of the toast.
void rt_toast_set_position(int64_t position) {
    (void)position;
}

/// @brief Set the max visible of the toast.
void rt_toast_set_max_visible(int64_t count) {
    (void)count;
}

/// @brief Dismiss the all of the toast.
void rt_toast_dismiss_all(void) {}

void *rt_breadcrumb_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Release resources and destroy the breadcrumb.
void rt_breadcrumb_destroy(void *crumb) {
    (void)crumb;
}

/// @brief Set the path of the breadcrumb.
void rt_breadcrumb_set_path(void *crumb, rt_string path, rt_string separator) {
    (void)crumb;
    (void)path;
    (void)separator;
}

/// @brief Set the items of the breadcrumb.
void rt_breadcrumb_set_items(void *crumb, rt_string items) {
    (void)crumb;
    (void)items;
}

/// @brief Add a path segment to a breadcrumb navigation widget.
void rt_breadcrumb_add_item(void *crumb, rt_string text, rt_string item_data) {
    (void)crumb;
    (void)text;
    (void)item_data;
}

/// @brief Remove all entries from the breadcrumb.
void rt_breadcrumb_clear(void *crumb) {
    (void)crumb;
}

/// @brief Check if a breadcrumb segment was clicked this frame (edge-triggered).
int64_t rt_breadcrumb_was_item_clicked(void *crumb) {
    (void)crumb;
    return 0;
}

/// @brief Get the clicked index of the breadcrumb.
int64_t rt_breadcrumb_get_clicked_index(void *crumb) {
    (void)crumb;
    return -1;
}

/// @brief Get the clicked data of the breadcrumb.
rt_string rt_breadcrumb_get_clicked_data(void *crumb) {
    (void)crumb;
    return rt_str_empty();
}

/// @brief Set the separator of the breadcrumb.
void rt_breadcrumb_set_separator(void *crumb, rt_string sep) {
    (void)crumb;
    (void)sep;
}

/// @brief Set the max items of the breadcrumb.
void rt_breadcrumb_set_max_items(void *crumb, int64_t max) {
    (void)crumb;
    (void)max;
}

void *rt_minimap_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Release resources and destroy the minimap.
void rt_minimap_destroy(void *minimap) {
    (void)minimap;
}

/// @brief Bind the editor of the minimap.
void rt_minimap_bind_editor(void *minimap, void *editor) {
    (void)minimap;
    (void)editor;
}

/// @brief Unbind the editor of the minimap.
void rt_minimap_unbind_editor(void *minimap) {
    (void)minimap;
}

/// @brief Set the width of the minimap.
void rt_minimap_set_width(void *minimap, int64_t width) {
    (void)minimap;
    (void)width;
}

/// @brief Get the width of the minimap.
int64_t rt_minimap_get_width(void *minimap) {
    (void)minimap;
    return 0;
}

/// @brief Set the scale of the minimap.
void rt_minimap_set_scale(void *minimap, double scale) {
    (void)minimap;
    (void)scale;
}

/// @brief Set the show slider of the minimap.
void rt_minimap_set_show_slider(void *minimap, int64_t show) {
    (void)minimap;
    (void)show;
}

/// @brief Add a highlighted marker region to the minimap.
void rt_minimap_add_marker(void *minimap, int64_t line, int64_t color, int64_t type) {
    (void)minimap;
    (void)line;
    (void)color;
    (void)type;
}

/// @brief Clear all markers from the minimap.
void rt_minimap_remove_markers(void *minimap, int64_t line) {
    (void)minimap;
    (void)line;
}

/// @brief Clear the markers of the minimap.
void rt_minimap_clear_markers(void *minimap) {
    (void)minimap;
}

/// @brief Set the draggable of the widget.
void rt_widget_set_draggable(void *widget, int64_t draggable) {
    (void)widget;
    (void)draggable;
}

/// @brief Set the drag data of the widget.
void rt_widget_set_drag_data(void *widget, rt_string type, rt_string data) {
    (void)widget;
    (void)type;
    (void)data;
}

/// @brief Check whether a widget is currently being dragged.
int64_t rt_widget_is_being_dragged(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get a value from the widget.
void rt_widget_set_drop_target(void *widget, int64_t target) {
    (void)widget;
    (void)target;
}

/// @brief Set the accepted drop types of the widget.
void rt_widget_set_accepted_drop_types(void *widget, rt_string types) {
    (void)widget;
    (void)types;
}

/// @brief Check whether a dragged item is hovering over this drop target.
int64_t rt_widget_is_drag_over(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Check whether a drop was completed on this widget this frame.
int64_t rt_widget_was_dropped(void *widget) {
    (void)widget;
    return 0;
}

/// @brief Get the drop type of the widget.
rt_string rt_widget_get_drop_type(void *widget) {
    (void)widget;
    return rt_str_empty();
}

/// @brief Get the drop data of the widget.
rt_string rt_widget_get_drop_data(void *widget) {
    (void)widget;
    return rt_str_empty();
}

/// @brief Check whether files were dropped onto the app window this frame.
int64_t rt_app_was_file_dropped(void *app) {
    (void)app;
    return 0;
}

/// @brief Get the number of files dropped onto the app window.
int64_t rt_app_get_dropped_file_count(void *app) {
    (void)app;
    return 0;
}

/// @brief Get the dropped file of the app.
rt_string rt_app_get_dropped_file(void *app, int64_t index) {
    (void)app;
    (void)index;
    return rt_str_empty();
}

/// @brief Add an element to the file.
void rt_gui_file_drop_add(rt_gui_app_t *app, const char *path) {
    (void)app;
    (void)path;
}

/// @brief Cleanup the features.
void rt_gui_features_cleanup(rt_gui_app_t *app) {
    (void)app;
}

#endif /* VIPER_ENABLE_GRAPHICS */
