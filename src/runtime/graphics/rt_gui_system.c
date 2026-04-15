//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_system.c
// Purpose: System-level GUI services for the Viper runtime: clipboard read/write,
//   keyboard shortcut registration and frame-based polling, window management
//   helpers (title, opacity, always-on-top, position, maximise/minimise), and
//   cursor style control. These are global services not tied to a specific widget.
//
// Key invariants:
//   - Shortcuts are stored per GUI app and the table grows dynamically.
//   - Shortcut trigger state is edge-triggered per frame on the active app: it is
//     set when polled and cleared on the next GUI app poll.
//   - shortcuts_global_enabled can disable all shortcut processing for the app
//     (e.g. when a text input widget has focus).
//   - Clipboard operations delegate directly to vgfx_clipboard_*; text is
//     converted to/from rt_string via rt_string_to_cstr / rt_string_from_bytes.
//   - Cursor style constants map 1:1 to VGFX_CURSOR_* enum values.
//
// Ownership/Lifetime:
//   - Shortcut id/keys/description strings are strdup'd into the active app's
//     shortcut table and freed by rt_shortcuts_clear().
//   - Clipboard text returned by vgfx_clipboard_get_text is malloc'd by the
//     platform; this file frees it after converting to rt_string.
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/graphics/include/vgfx.h (clipboard and window management API),
//        src/runtime/rt_platform.h (platform detection)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

// Clipboard Functions (Phase 1)
//=============================================================================

/// @brief Copy text to the system clipboard.
void rt_clipboard_set_text(rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    char *ctext = rt_string_to_cstr(text);
    if (ctext) {
        vgfx_clipboard_set_text(ctext);
        free(ctext);
    }
}

/// @brief Get the text of the clipboard.
rt_string rt_clipboard_get_text(void) {
    RT_ASSERT_MAIN_THREAD();
    char *text = vgfx_clipboard_get_text();
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

/// @brief Has the text of the clipboard.
int64_t rt_clipboard_has_text(void) {
    RT_ASSERT_MAIN_THREAD();
    return vgfx_clipboard_has_format(VGFX_CLIPBOARD_TEXT) ? 1 : 0;
}

/// @brief Remove all entries from the clipboard.
void rt_clipboard_clear(void) {
    RT_ASSERT_MAIN_THREAD();
    vgfx_clipboard_clear();
}

//=============================================================================
// Keyboard Shortcuts (Phase 1)
//=============================================================================

/// @brief Pick the app whose shortcut table to operate on.
/// Falls back to the active app if no current-app override is set.
static rt_gui_app_t *rt_shortcuts_app(void) {
    return s_current_app ? s_current_app : rt_gui_get_active_app();
}

/// @brief Grow the shortcut table if it's full (doubles capacity, default 16).
static void rt_shortcuts_ensure_capacity(rt_gui_app_t *app) {
    if (!app || app->shortcut_count < app->shortcut_cap)
        return;
    int new_cap = app->shortcut_cap ? app->shortcut_cap * 2 : 16;
    void *p = realloc(app->shortcuts, (size_t)new_cap * sizeof(*app->shortcuts));
    if (!p)
        return;
    app->shortcuts = p;
    app->shortcut_cap = new_cap;
}

/// @brief Parse a `+`-separated shortcut string (e.g. `"Ctrl+Shift+S"`) into modifier flags + key code.
///
/// Recognises `Ctrl`/`Control`, `Shift`, `Alt`, `Cmd`/`Command`
/// (mapped to Ctrl on non-macOS for cross-platform consistency),
/// `F1`-`F12`, named keys (`Enter`, `Escape`, arrows, etc.), and
/// any single character. Case-insensitive.
/// @return 1 on success, 0 on alloc failure.
static int parse_shortcut_keys(const char *keys, int *ctrl, int *shift, int *alt, int *key) {
    *ctrl = 0;
    *shift = 0;
    *alt = 0;
    *key = 0;

    if (!keys)
        return 0;

    char *copy = strdup(keys);
    if (!copy)
        return 0;

    char *saveptr = NULL;
    char *token = rt_strtok_r(copy, "+", &saveptr);
    while (token) {
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
            *end-- = '\0';

        if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0) {
            *ctrl = 1;
        } else if (strcasecmp(token, "Shift") == 0) {
            *shift = 1;
        } else if (strcasecmp(token, "Alt") == 0) {
            *alt = 1;
        } else if (strcasecmp(token, "Cmd") == 0 || strcasecmp(token, "Command") == 0) {
            *ctrl = 1; // Map Cmd to Ctrl for cross-platform
        } else if (strlen(token) == 1) {
            // Single character key
            *key = toupper(token[0]);
        } else if (token[0] == 'F' && strlen(token) <= 3) {
            // Function key (F1-F12)
            int fnum = atoi(token + 1);
            if (fnum >= 1 && fnum <= 12) {
                *key = VG_KEY_F1 + (fnum - 1);
            }
        } else if (strcasecmp(token, "Enter") == 0 || strcasecmp(token, "Return") == 0)
            *key = VG_KEY_ENTER;
        else if (strcasecmp(token, "Escape") == 0 || strcasecmp(token, "Esc") == 0)
            *key = VG_KEY_ESCAPE;
        else if (strcasecmp(token, "Space") == 0)
            *key = VG_KEY_SPACE;
        else if (strcasecmp(token, "Tab") == 0)
            *key = VG_KEY_TAB;
        else if (strcasecmp(token, "Backspace") == 0)
            *key = VG_KEY_BACKSPACE;
        else if (strcasecmp(token, "Delete") == 0 || strcasecmp(token, "Del") == 0)
            *key = VG_KEY_DELETE;
        else if (strcasecmp(token, "Home") == 0)
            *key = VG_KEY_HOME;
        else if (strcasecmp(token, "End") == 0)
            *key = VG_KEY_END;
        else if (strcasecmp(token, "Left") == 0)
            *key = VG_KEY_LEFT;
        else if (strcasecmp(token, "Right") == 0)
            *key = VG_KEY_RIGHT;
        else if (strcasecmp(token, "Up") == 0)
            *key = VG_KEY_UP;
        else if (strcasecmp(token, "Down") == 0)
            *key = VG_KEY_DOWN;
        token = rt_strtok_r(NULL, "+", &saveptr);
    }

    free(copy);
    return (*key != 0);
}

/// @brief Register the shortcuts.
void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return;

    char *cid = rt_string_to_cstr(id);
    char *ckeys = rt_string_to_cstr(keys);
    char *cdesc = rt_string_to_cstr(description);

    if (!cid || !ckeys) {
        free(cid);
        free(ckeys);
        free(cdesc);
        return;
    }

    // Check if already registered and update
    for (int i = 0; i < app->shortcut_count; i++) {
        if (app->shortcuts[i].id && strcmp(app->shortcuts[i].id, cid) == 0) {
            free(app->shortcuts[i].keys);
            free(app->shortcuts[i].description);
            app->shortcuts[i].keys = ckeys;
            app->shortcuts[i].description = cdesc;
            // Re-parse cached modifier/key values for the new keys string
            parse_shortcut_keys(ckeys,
                                &app->shortcuts[i].parsed_ctrl,
                                &app->shortcuts[i].parsed_shift,
                                &app->shortcuts[i].parsed_alt,
                                &app->shortcuts[i].parsed_key);
            free(cid);
            return;
        }
    }

    // Add new shortcut
    rt_shortcuts_ensure_capacity(app);
    if (app->shortcut_count >= app->shortcut_cap) {
        free(cid);
        free(ckeys);
        free(cdesc);
        return;
    }
    rt_gui_shortcut_t *sc = &app->shortcuts[app->shortcut_count];
    sc->id = cid;
    sc->keys = ckeys;
    sc->description = cdesc;
    sc->enabled = 1;
    sc->triggered = 0;
    // Pre-parse modifier/key values so rt_shortcuts_check_key doesn't
    // need to re-parse the string on every keypress.
    parse_shortcut_keys(
        ckeys, &sc->parsed_ctrl, &sc->parsed_shift, &sc->parsed_alt, &sc->parsed_key);
    app->shortcut_count++;
}

/// @brief Unregister the shortcuts.
void rt_shortcuts_unregister(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return;
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < app->shortcut_count; i++) {
        if (app->shortcuts[i].id && strcmp(app->shortcuts[i].id, cid) == 0) {
            free(app->shortcuts[i].id);
            free(app->shortcuts[i].keys);
            free(app->shortcuts[i].description);

            // Shift remaining shortcuts down
            memmove(&app->shortcuts[i],
                    &app->shortcuts[i + 1],
                    (size_t)(app->shortcut_count - i - 1) * sizeof(*app->shortcuts));
            app->shortcut_count--;
            break;
        }
    }

    free(cid);
}

/// @brief Remove all entries from the shortcuts.
void rt_shortcuts_clear(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return;
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
}

/// @brief Check if any registered keyboard shortcut was triggered this frame.
int64_t rt_shortcuts_was_triggered(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app || !app->shortcuts_global_enabled)
        return 0;

    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < app->shortcut_count; i++) {
        if (app->shortcuts[i].id && strcmp(app->shortcuts[i].id, cid) == 0) {
            free(cid);
            return app->shortcuts[i].triggered ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

// Clear all shortcut triggered flags (call at start of each frame)
/// @brief Clear the triggered of the shortcuts.
void rt_shortcuts_clear_triggered(rt_gui_app_t *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    for (int i = 0; i < app->shortcut_count; i++) {
        app->shortcuts[i].triggered = 0;
    }
    free(app->triggered_shortcut_id);
    app->triggered_shortcut_id = NULL;
}

// Check if a key event matches any registered shortcut.
// Returns 1 if a shortcut was triggered, 0 otherwise.
/// @brief Check the key of the shortcuts.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods) {
    RT_ASSERT_MAIN_THREAD();
    if (!app || !app->shortcuts_global_enabled)
        return 0;

    // On macOS, Cmd is used instead of Ctrl for shortcuts.
    // Treat VGFX_MOD_CMD as Ctrl for cross-platform compatibility.
    int has_ctrl = (mods & VGFX_MOD_CTRL) || (mods & VGFX_MOD_CMD);
    int has_shift = (mods & VGFX_MOD_SHIFT) ? 1 : 0;
    int has_alt = (mods & VGFX_MOD_ALT) ? 1 : 0;

    // Only check if at least one modifier is held (plain keys aren't shortcuts)
    if (!has_ctrl && !has_alt && !(key >= VG_KEY_F1 && key <= VG_KEY_F12))
        return 0;

    int upper_key = (key >= 'a' && key <= 'z') ? key - ('a' - 'A') : key;

    for (int i = 0; i < app->shortcut_count; i++) {
        if (!app->shortcuts[i].enabled || !app->shortcuts[i].parsed_key)
            continue;

        if (app->shortcuts[i].parsed_ctrl == has_ctrl &&
            app->shortcuts[i].parsed_shift == has_shift &&
            app->shortcuts[i].parsed_alt == has_alt && app->shortcuts[i].parsed_key == upper_key) {
            app->shortcuts[i].triggered = 1;
            free(app->triggered_shortcut_id);
            app->triggered_shortcut_id = app->shortcuts[i].id ? strdup(app->shortcuts[i].id) : NULL;
            return 1;
        }
    }
    return 0;
}

/// @brief Get the triggered of the shortcuts.
rt_string rt_shortcuts_get_triggered(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (app && app->triggered_shortcut_id) {
        return rt_string_from_bytes(app->triggered_shortcut_id, strlen(app->triggered_shortcut_id));
    }
    return rt_str_empty();
}

/// @brief Enable or disable global keyboard shortcut processing.
void rt_shortcuts_set_enabled(rt_string id, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return;
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < app->shortcut_count; i++) {
        if (app->shortcuts[i].id && strcmp(app->shortcuts[i].id, cid) == 0) {
            app->shortcuts[i].enabled = enabled != 0;
            break;
        }
    }

    free(cid);
}

/// @brief Check whether global keyboard shortcuts are currently enabled.
int64_t rt_shortcuts_is_enabled(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return 0;
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < app->shortcut_count; i++) {
        if (app->shortcuts[i].id && strcmp(app->shortcuts[i].id, cid) == 0) {
            free(cid);
            return app->shortcuts[i].enabled ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

/// @brief Set the global enabled of the shortcuts.
void rt_shortcuts_set_global_enabled(int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (app)
        app->shortcuts_global_enabled = enabled != 0;
}

/// @brief Get the global enabled of the shortcuts.
int64_t rt_shortcuts_get_global_enabled(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    return app && app->shortcuts_global_enabled ? 1 : 0;
}

//=============================================================================
// Window Management (Phase 1)
//=============================================================================

/// @brief Set the title of the app.
void rt_app_set_title(void *app, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return;
    char *cstr = rt_string_to_cstr(title);
    if (cstr) {
        vgfx_set_title(gui_app->window, cstr);
        // Store a copy for rt_app_get_title (no vgfx_get_title API exists)
        free(gui_app->title);
        gui_app->title = strdup(cstr);
        rt_gui_macos_menu_sync_app(gui_app);
        free(cstr);
    }
}

/// @brief Get the title of the app.
rt_string rt_app_get_title(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return rt_str_empty();
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->title)
        return rt_string_from_bytes(gui_app->title, strlen(gui_app->title));
    return rt_str_empty();
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
void rt_app_set_size(void *app, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window) {
        vgfx_set_window_size(gui_app->window, (int32_t)width, (int32_t)height);
        // Root sizing is handled by vg_widget_layout(root, phys_w, phys_h) in
        // rt_gui_app_render — do not set root fixed size here, as that would
        // prevent the layout engine from resizing the root on window resize.
    }
}

/// @brief Get the width of the app.
int64_t rt_app_get_width(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return w;
}

/// @brief Get the height of the app.
int64_t rt_app_get_height(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return h;
}

/// @brief Move the app window to a specific screen position.
void rt_app_set_position(void *app, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_position(gui_app->window, (int32_t)x, (int32_t)y);
}

/// @brief Get the x of the app.
int64_t rt_app_get_x(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t x = 0, y = 0;
    vgfx_get_position(gui_app->window, &x, &y);
    return x;
}

/// @brief Get the y of the app.
int64_t rt_app_get_y(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t x = 0, y = 0;
    vgfx_get_position(gui_app->window, &x, &y);
    return y;
}

/// @brief Minimize the app.
void rt_app_minimize(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_minimize(gui_app->window);
}

/// @brief Maximize the app.
void rt_app_maximize(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_maximize(gui_app->window);
}

/// @brief Restore the app.
void rt_app_restore(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_restore(gui_app->window);
}

/// @brief Check whether the app window is currently minimized.
int64_t rt_app_is_minimized(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_minimized(gui_app->window) : 0;
}

/// @brief Check whether the app window is currently maximized.
int64_t rt_app_is_maximized(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_maximized(gui_app->window) : 0;
}

/// @brief Set the fullscreen of the app.
void rt_app_set_fullscreen(void *app, int64_t fullscreen) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_fullscreen(gui_app->window, (int32_t)fullscreen);
}

/// @brief Check whether the app window is in fullscreen mode.
int64_t rt_app_is_fullscreen(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_fullscreen(gui_app->window) : 0;
}

/// @brief Focus the app.
void rt_app_focus(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_focus(gui_app->window);
}

/// @brief Check whether the app window currently has OS-level focus.
int64_t rt_app_is_focused(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_focused(gui_app->window) : 0;
}

/// @brief Set the prevent close of the app.
void rt_app_set_prevent_close(void *app, int64_t prevent) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_prevent_close(gui_app->window, (int32_t)prevent);
}

/// @brief Check whether the window close was requested this frame.
int64_t rt_app_was_close_requested(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->should_close;
}

/// @brief Get the monitor width of the app.
int64_t rt_app_get_monitor_width(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(gui_app->window, &w, &h);
    return (int64_t)w;
}

/// @brief Get the monitor height of the app.
int64_t rt_app_get_monitor_height(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(gui_app->window, &w, &h);
    return (int64_t)h;
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
void rt_app_set_window_size(void *app, int64_t w, int64_t h) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return;
    vgfx_set_window_size(gui_app->window, (int32_t)w, (int32_t)h);
    // Root sizing is handled by vg_widget_layout(root, phys_w, phys_h) in
    // rt_gui_app_render — do not set root->width/height here with the logical
    // dimensions passed from Zia, as that would corrupt the layout geometry.
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
double rt_app_get_font_size(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 14.0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    // Return logical pt size — divide stored physical pixels by HiDPI scale.
    float _s = gui_app->window ? vgfx_window_get_scale(gui_app->window) : 1.0f;
    if (_s <= 0.0f)
        _s = 1.0f;
    return (double)(gui_app->default_font_size / _s);
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
void rt_app_set_font_size(void *app, double size) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (size < 6.0)
        size = 6.0;
    if (size > 72.0)
        size = 72.0;
    // Store physical pixels — multiply logical pt size by HiDPI scale.
    float _s = gui_app->window ? vgfx_window_get_scale(gui_app->window) : 1.0f;
    if (_s <= 0.0f)
        _s = 1.0f;
    gui_app->default_font_size = (float)size * _s;
}

//=============================================================================
// Cursor Styles (Phase 1)
//=============================================================================

/// @brief Set a value in the cursor.
void rt_cursor_set(int64_t type) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (app && app->window)
        vgfx_set_cursor(app->window, (int32_t)type);
}

/// @brief Set a value in the cursor.
void rt_cursor_reset(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_cursor_set(0); /* VGFX_CURSOR_DEFAULT */
}

/// @brief Show or hide the mouse cursor.
void rt_cursor_set_visible(int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (app && app->window)
        vgfx_set_cursor_visible(app->window, (int32_t)visible);
}

/// @note Cursor is global — not per-widget. The widget parameter is reserved for future use.
void rt_widget_set_cursor(void *widget, int64_t type) {
    RT_ASSERT_MAIN_THREAD();
    (void)widget;
    rt_cursor_set(type);
}

/// @note Cursor is global — not per-widget. The widget parameter is reserved for future use.
void rt_widget_reset_cursor(void *widget) {
    RT_ASSERT_MAIN_THREAD();
    (void)widget;
    rt_cursor_reset();
}

#else /* !VIPER_ENABLE_GRAPHICS */

/// @brief Copy text to the system clipboard.
void rt_clipboard_set_text(rt_string text) {
    (void)text;
}

/// @brief Get the text of the clipboard.
rt_string rt_clipboard_get_text(void) {
    return rt_str_empty();
}

/// @brief Has the text of the clipboard.
int64_t rt_clipboard_has_text(void) {
    return 0;
}

/// @brief Remove all entries from the clipboard.
void rt_clipboard_clear(void) {}

/// @brief Register the shortcuts.
void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description) {
    (void)id;
    (void)keys;
    (void)description;
}

/// @brief Unregister the shortcuts.
void rt_shortcuts_unregister(rt_string id) {
    (void)id;
}

/// @brief Remove all entries from the shortcuts.
void rt_shortcuts_clear(void) {}

/// @brief Check if any registered keyboard shortcut was triggered this frame.
int64_t rt_shortcuts_was_triggered(rt_string id) {
    (void)id;
    return 0;
}

/// @brief Clear the triggered of the shortcuts.
void rt_shortcuts_clear_triggered(rt_gui_app_t *app) {
    (void)app;
}

/// @brief Check the key of the shortcuts.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods) {
    (void)app;
    (void)key;
    (void)mods;
    return 0;
}

/// @brief Get the triggered of the shortcuts.
rt_string rt_shortcuts_get_triggered(void) {
    return rt_str_empty();
}

/// @brief Enable or disable global keyboard shortcut processing.
void rt_shortcuts_set_enabled(rt_string id, int64_t enabled) {
    (void)id;
    (void)enabled;
}

/// @brief Check whether global keyboard shortcuts are currently enabled.
int64_t rt_shortcuts_is_enabled(rt_string id) {
    (void)id;
    return 0;
}

/// @brief Set the global enabled of the shortcuts.
void rt_shortcuts_set_global_enabled(int64_t enabled) {
    (void)enabled;
}

/// @brief Get the global enabled of the shortcuts.
int64_t rt_shortcuts_get_global_enabled(void) {
    return 0;
}

/// @brief Set the title of the app.
void rt_app_set_title(void *app, rt_string title) {
    (void)app;
    (void)title;
}

/// @brief Get the title of the app.
rt_string rt_app_get_title(void *app) {
    (void)app;
    return rt_str_empty();
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
void rt_app_set_size(void *app, int64_t width, int64_t height) {
    (void)app;
    (void)width;
    (void)height;
}

/// @brief Get the width of the app.
int64_t rt_app_get_width(void *app) {
    (void)app;
    return 0;
}

/// @brief Get the height of the app.
int64_t rt_app_get_height(void *app) {
    (void)app;
    return 0;
}

/// @brief Move the app window to a specific screen position.
void rt_app_set_position(void *app, int64_t x, int64_t y) {
    (void)app;
    (void)x;
    (void)y;
}

/// @brief Get the x of the app.
int64_t rt_app_get_x(void *app) {
    (void)app;
    return 0;
}

/// @brief Get the y of the app.
int64_t rt_app_get_y(void *app) {
    (void)app;
    return 0;
}

/// @brief Minimize the app.
void rt_app_minimize(void *app) {
    (void)app;
}

/// @brief Maximize the app.
void rt_app_maximize(void *app) {
    (void)app;
}

/// @brief Restore the app.
void rt_app_restore(void *app) {
    (void)app;
}

/// @brief Check whether the app window is currently minimized.
int64_t rt_app_is_minimized(void *app) {
    (void)app;
    return 0;
}

/// @brief Check whether the app window is currently maximized.
int64_t rt_app_is_maximized(void *app) {
    (void)app;
    return 0;
}

/// @brief Set the fullscreen of the app.
void rt_app_set_fullscreen(void *app, int64_t fullscreen) {
    (void)app;
    (void)fullscreen;
}

/// @brief Check whether the app window is in fullscreen mode.
int64_t rt_app_is_fullscreen(void *app) {
    (void)app;
    return 0;
}

/// @brief Focus the app.
void rt_app_focus(void *app) {
    (void)app;
}

/// @brief Check whether the app window currently has OS-level focus.
int64_t rt_app_is_focused(void *app) {
    (void)app;
    return 0;
}

/// @brief Set the prevent close of the app.
void rt_app_set_prevent_close(void *app, int64_t prevent) {
    (void)app;
    (void)prevent;
}

/// @brief Check whether the window close was requested this frame.
int64_t rt_app_was_close_requested(void *app) {
    (void)app;
    return 0;
}

/// @brief Get the monitor width of the app.
int64_t rt_app_get_monitor_width(void *app) {
    (void)app;
    return 0;
}

/// @brief Get the monitor height of the app.
int64_t rt_app_get_monitor_height(void *app) {
    (void)app;
    return 0;
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
void rt_app_set_window_size(void *app, int64_t w, int64_t h) {
    (void)app;
    (void)w;
    (void)h;
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
double rt_app_get_font_size(void *app) {
    (void)app;
    return 14.0;
}

/// @brief Get a dimension of the app window (width, height, or scale factor).
void rt_app_set_font_size(void *app, double size) {
    (void)app;
    (void)size;
}

/// @brief Set a value in the cursor.
void rt_cursor_set(int64_t type) {
    (void)type;
}

/// @brief Set a value in the cursor.
void rt_cursor_reset(void) {}

/// @brief Show or hide the mouse cursor.
void rt_cursor_set_visible(int64_t visible) {
    (void)visible;
}

/// @brief Set the cursor of the widget.
void rt_widget_set_cursor(void *widget, int64_t type) {
    (void)widget;
    (void)type;
}

/// @brief Reset the cursor of the widget.
void rt_widget_reset_cursor(void *widget) {
    (void)widget;
}

#endif /* VIPER_ENABLE_GRAPHICS */
