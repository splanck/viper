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
//   - Shortcuts are stored in a fixed-size static table (MAX_SHORTCUTS = 256);
//     registering beyond that limit is silently ignored.
//   - Shortcut trigger state (g_triggered_shortcut_id) is edge-triggered per
//     frame: it is set when polled and cleared on the next frame update.
//   - g_shortcuts_global_enabled can disable all shortcut processing at once
//     (e.g. when a text input widget has focus).
//   - Clipboard operations delegate directly to vgfx_clipboard_*; text is
//     converted to/from rt_string via rt_string_to_cstr / rt_string_from_bytes.
//   - Cursor style constants map 1:1 to VGFX_CURSOR_* enum values.
//
// Ownership/Lifetime:
//   - Shortcut id/keys/description strings are strdup'd into the static table
//     and freed by rt_shortcuts_clear().
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

/// @brief Copy a Viper string into the system clipboard as plain text.
/// @details Converts the runtime string to a heap-allocated C string, passes it
///          to the platform clipboard API, then frees the temporary C string.
///          Must be called from the main thread because clipboard access is
///          tied to the window system's event loop.
void rt_clipboard_set_text(rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    char *ctext = rt_string_to_cstr(text);
    if (ctext) {
        vgfx_clipboard_set_text(ctext);
        free(ctext);
    }
}

/// @brief Retrieve the current clipboard contents as a Viper runtime string.
/// @details Calls the platform clipboard API to get a malloc'd C string, wraps
///          it in an rt_string, then frees the platform buffer. Returns an empty
///          string if the clipboard is empty or contains non-text data. The
///          caller owns the returned string reference.
rt_string rt_clipboard_get_text(void) {
    RT_ASSERT_MAIN_THREAD();
    char *text = vgfx_clipboard_get_text();
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

/// @brief Check whether the system clipboard currently contains text data.
/// @details Queries the platform clipboard for the VGFX_CLIPBOARD_TEXT format
///          without actually reading the content. Useful for enabling/disabling
///          a "Paste" button in the UI based on clipboard availability.
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

// Internal shortcut storage
typedef struct {
    char *id;
    char *keys;
    char *description;
    int enabled;
    int triggered; // Set to 1 when shortcut is triggered this frame
    // Pre-parsed modifier/key values (cached at registration time)
    int parsed_ctrl;
    int parsed_shift;
    int parsed_alt;
    int parsed_key;
} rt_shortcut_t;

#define MAX_SHORTCUTS 256
static rt_shortcut_t g_shortcuts[MAX_SHORTCUTS];
static int g_shortcut_count = 0;
static int g_shortcuts_global_enabled = 1;
static char *g_triggered_shortcut_id = NULL;

// Parse modifier keys from string like "Ctrl+Shift+S"
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

/// @brief Register a keyboard shortcut with the global shortcut table.
/// @details Parses a human-readable key combo string like "Ctrl+Shift+S" into
///          modifier flags and a key code, then stores the parsed result in a
///          static table slot for frame-based polling. The id string is used
///          later to query which shortcut was triggered. Silently ignores
///          registrations beyond MAX_SHORTCUTS (256). Cmd is mapped to Ctrl
///          for cross-platform compatibility.
void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description) {
    RT_ASSERT_MAIN_THREAD();
    if (g_shortcut_count >= MAX_SHORTCUTS)
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
    for (int i = 0; i < g_shortcut_count; i++) {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0) {
            free(g_shortcuts[i].keys);
            free(g_shortcuts[i].description);
            g_shortcuts[i].keys = ckeys;
            g_shortcuts[i].description = cdesc;
            // Re-parse cached modifier/key values for the new keys string
            parse_shortcut_keys(ckeys,
                                &g_shortcuts[i].parsed_ctrl,
                                &g_shortcuts[i].parsed_shift,
                                &g_shortcuts[i].parsed_alt,
                                &g_shortcuts[i].parsed_key);
            free(cid);
            return;
        }
    }

    // Add new shortcut
    rt_shortcut_t *sc = &g_shortcuts[g_shortcut_count];
    sc->id = cid;
    sc->keys = ckeys;
    sc->description = cdesc;
    sc->enabled = 1;
    sc->triggered = 0;
    // Pre-parse modifier/key values so rt_shortcuts_check_key doesn't
    // need to re-parse the string on every keypress.
    parse_shortcut_keys(
        ckeys, &sc->parsed_ctrl, &sc->parsed_shift, &sc->parsed_alt, &sc->parsed_key);
    g_shortcut_count++;
}

/// @brief Remove a keyboard shortcut by its string ID.
/// @details Searches the static shortcut table for a matching ID, frees its
///          strdup'd strings (id, keys, description), then shifts all later
///          entries down to fill the gap so the table stays compact.
void rt_shortcuts_unregister(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < g_shortcut_count; i++) {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0) {
            free(g_shortcuts[i].id);
            free(g_shortcuts[i].keys);
            free(g_shortcuts[i].description);

            // Shift remaining shortcuts down
            for (int j = i; j < g_shortcut_count - 1; j++) {
                g_shortcuts[j] = g_shortcuts[j + 1];
            }
            g_shortcut_count--;
            break;
        }
    }

    free(cid);
}

/// @brief Unregister all keyboard shortcuts and free their string storage.
/// @details Iterates the entire shortcut table freeing every strdup'd string,
///          resets the count to 0, and clears the triggered-shortcut pointer.
///          Called during GUI teardown or when the shortcut context changes.
void rt_shortcuts_clear(void) {
    RT_ASSERT_MAIN_THREAD();
    for (int i = 0; i < g_shortcut_count; i++) {
        free(g_shortcuts[i].id);
        free(g_shortcuts[i].keys);
        free(g_shortcuts[i].description);
    }
    g_shortcut_count = 0;
    g_triggered_shortcut_id = NULL;
}

/// @brief Check whether a specific shortcut was triggered during this frame.
/// @details Looks up the shortcut by its string ID and returns 1 if its
///          triggered flag was set by rt_shortcuts_check_key during the
///          current frame's input processing. Returns 0 if not found, not
///          triggered, or shortcuts are globally disabled.
int64_t rt_shortcuts_was_triggered(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_shortcuts_global_enabled)
        return 0;

    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < g_shortcut_count; i++) {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0) {
            free(cid);
            return g_shortcuts[i].triggered ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

/// @brief Reset all shortcut triggered flags at the start of a new frame.
/// @details Called once per frame before input processing so that each
///          shortcut's triggered flag is edge-detected: set only during the
///          frame when the key combo is first pressed, not held down.
void rt_shortcuts_clear_triggered(void) {
    RT_ASSERT_MAIN_THREAD();
    for (int i = 0; i < g_shortcut_count; i++) {
        g_shortcuts[i].triggered = 0;
    }
    g_triggered_shortcut_id = NULL;
}

/// @brief Test a key-down event against all registered shortcuts.
/// @details Called by the input system when a key is pressed. Compares the
///          key code and modifier flags (Ctrl/Shift/Alt) against each shortcut's
///          pre-parsed values. On macOS, Cmd is treated as Ctrl. When a match
///          is found, the shortcut's triggered flag is set and its ID is stored
///          in g_triggered_shortcut_id for retrieval by was_triggered/get_triggered.
/// @return 1 if a shortcut was matched, 0 otherwise.
int8_t rt_shortcuts_check_key(int key, int mods) {
    RT_ASSERT_MAIN_THREAD();
    if (!g_shortcuts_global_enabled)
        return 0;

    // On macOS, Cmd is used instead of Ctrl for shortcuts.
    // Treat VGFX_MOD_CMD as Ctrl for cross-platform compatibility.
    int has_ctrl = (mods & VGFX_MOD_CTRL) || (mods & VGFX_MOD_CMD);
    int has_shift = (mods & VGFX_MOD_SHIFT) ? 1 : 0;
    int has_alt = (mods & VGFX_MOD_ALT) ? 1 : 0;

    // Only check if at least one modifier is held (plain keys aren't shortcuts)
    if (!has_ctrl && !has_alt)
        return 0;

    int upper_key = (key >= 'a' && key <= 'z') ? key - ('a' - 'A') : key;

    for (int i = 0; i < g_shortcut_count; i++) {
        if (!g_shortcuts[i].enabled || !g_shortcuts[i].parsed_key)
            continue;

        if (g_shortcuts[i].parsed_ctrl == has_ctrl && g_shortcuts[i].parsed_shift == has_shift &&
            g_shortcuts[i].parsed_alt == has_alt && g_shortcuts[i].parsed_key == upper_key) {
            g_shortcuts[i].triggered = 1;
            g_triggered_shortcut_id = g_shortcuts[i].id;
            return 1;
        }
    }
    return 0;
}

/// @brief Return the ID of the most recently triggered shortcut, or empty string.
/// @details The ID is set by rt_shortcuts_check_key when a matching key combo is
///          detected and cleared on the next frame by clear_triggered.
rt_string rt_shortcuts_get_triggered(void) {
    RT_ASSERT_MAIN_THREAD();
    if (g_triggered_shortcut_id) {
        return rt_string_from_bytes(g_triggered_shortcut_id, strlen(g_triggered_shortcut_id));
    }
    return rt_str_empty();
}

/// @brief Enable or disable an individual shortcut by its string ID.
/// @details When disabled, the shortcut's key combo is still registered but
///          rt_shortcuts_check_key skips it during matching. This is useful for
///          context-dependent shortcuts (e.g., disabling "Delete" when nothing is selected).
void rt_shortcuts_set_enabled(rt_string id, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < g_shortcut_count; i++) {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0) {
            g_shortcuts[i].enabled = enabled != 0;
            break;
        }
    }

    free(cid);
}

/// @brief Check whether an individual shortcut is currently enabled.
/// @details Returns 0 if the shortcut ID is not found or is disabled.
int64_t rt_shortcuts_is_enabled(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < g_shortcut_count; i++) {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0) {
            free(cid);
            return g_shortcuts[i].enabled ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

/// @brief Enable or disable all shortcut processing globally.
/// @details When disabled (e.g., while a text input has focus), check_key
///          always returns 0 regardless of what keys are pressed.
void rt_shortcuts_set_global_enabled(int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    g_shortcuts_global_enabled = enabled != 0;
}

/// @brief Check whether global shortcut processing is enabled.
int64_t rt_shortcuts_get_global_enabled(void) {
    RT_ASSERT_MAIN_THREAD();
    return g_shortcuts_global_enabled ? 1 : 0;
}

//=============================================================================
// Window Management (Phase 1)
//=============================================================================

/// @brief Set the window title bar text.
/// @details Converts the runtime string to a C string, updates the platform
///          window title via vgfx, and keeps a strdup'd copy in gui_app->title
///          because there is no vgfx_get_title API to read it back later.
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
        free(cstr);
    }
}

/// @brief Return the current window title as a runtime string.
/// @details Reads from the cached gui_app->title copy, not the platform window,
///          because vgfx has no get-title API. Returns empty if app is NULL.
rt_string rt_app_get_title(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return rt_str_empty();
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->title)
        return rt_string_from_bytes(gui_app->title, strlen(gui_app->title));
    return rt_str_empty();
}

/// @brief Resize the application window to the given pixel dimensions.
/// @details Delegates to vgfx_set_window_size. The root widget layout is NOT
///          explicitly resized here — it is recalculated in the next render
///          frame by rt_gui_app_render using the new physical window size.
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

/// @brief Return the current window width in pixels.
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

/// @brief Return the current window height in pixels.
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

/// @brief Move the application window to screen coordinates (x, y).
void rt_app_set_position(void *app, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_position(gui_app->window, (int32_t)x, (int32_t)y);
}

/// @brief Return the window's current X position on screen.
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

/// @brief Return the window's current Y position on screen.
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

/// @brief Check whether the window is currently minimized (iconified).
int64_t rt_app_is_minimized(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_minimized(gui_app->window) : 0;
}

/// @brief Check whether the window is currently maximized.
int64_t rt_app_is_maximized(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_maximized(gui_app->window) : 0;
}

/// @brief Enter or exit fullscreen mode for the application window.
void rt_app_set_fullscreen(void *app, int64_t fullscreen) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_fullscreen(gui_app->window, (int32_t)fullscreen);
}

/// @brief Check whether the window is currently in fullscreen mode.
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

/// @brief Check whether the application window currently has input focus.
int64_t rt_app_is_focused(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_focused(gui_app->window) : 0;
}

/// @brief Enable or disable close prevention (the X button asks but doesn't close).
/// @details When enabled, clicking the window close button sets should_close
///          but does not actually destroy the window, letting the app handle
///          confirmation dialogs or save prompts.
void rt_app_set_prevent_close(void *app, int64_t prevent) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_prevent_close(gui_app->window, (int32_t)prevent);
}

/// @brief Check whether the user attempted to close the window (pending close request).
/// @details Only meaningful when prevent_close is enabled. The app should check
///          this flag each frame and show a confirmation dialog before closing.
int64_t rt_app_was_close_requested(void *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->should_close;
}

/// @brief Return the primary monitor's width in pixels.
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

/// @brief Return the primary monitor's height in pixels.
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

/// @brief Resize the application window (alternative entry point to set_size).
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

/// @brief Return the default UI font size in logical points (HiDPI-adjusted).
/// @details Divides the internally stored physical-pixel font size by the
///          window's HiDPI scale factor to produce a logical-point value.
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

/// @brief Set the default UI font size in logical points (6-72), stored as physical pixels.
/// @details Multiplies the logical point size by the HiDPI scale factor before
///          storing, so all internal font rendering uses physical-pixel sizes.
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

/// @brief Change the mouse cursor to a platform standard style (arrow, hand, beam, etc.).
/// @details The type constant maps directly to VGFX_CURSOR_* enum values.
void rt_cursor_set(int64_t type) {
    RT_ASSERT_MAIN_THREAD();
    if (s_current_app && s_current_app->window)
        vgfx_set_cursor(s_current_app->window, (int32_t)type);
}

/// @brief Reset the mouse cursor to the default arrow style.
void rt_cursor_reset(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_cursor_set(0); /* VGFX_CURSOR_DEFAULT */
}

/// @brief Show or hide the mouse cursor within the application window.
void rt_cursor_set_visible(int64_t visible) {
    RT_ASSERT_MAIN_THREAD();
    if (s_current_app && s_current_app->window)
        vgfx_set_cursor_visible(s_current_app->window, (int32_t)visible);
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

#else /* !VIPER_ENABLE_GRAPHICS — no-op stubs for headless/test builds */

void rt_clipboard_set_text(rt_string text) { (void)text; }
rt_string rt_clipboard_get_text(void) { return rt_str_empty(); }
int64_t rt_clipboard_has_text(void) { return 0; }
void rt_clipboard_clear(void) {}

void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description) {
    (void)id; (void)keys; (void)description;
}
void rt_shortcuts_unregister(rt_string id) { (void)id; }
void rt_shortcuts_clear(void) {}
int64_t rt_shortcuts_was_triggered(rt_string id) { (void)id; return 0; }
void rt_shortcuts_clear_triggered(void) {}
int8_t rt_shortcuts_check_key(int key, int mods) { (void)key; (void)mods; return 0; }
rt_string rt_shortcuts_get_triggered(void) { return rt_str_empty(); }
void rt_shortcuts_set_enabled(rt_string id, int64_t enabled) { (void)id; (void)enabled; }
int64_t rt_shortcuts_is_enabled(rt_string id) { (void)id; return 0; }
void rt_shortcuts_set_global_enabled(int64_t enabled) { (void)enabled; }
int64_t rt_shortcuts_get_global_enabled(void) { return 0; }

void rt_app_set_title(void *app, rt_string title) { (void)app; (void)title; }
rt_string rt_app_get_title(void *app) { (void)app; return rt_str_empty(); }
void rt_app_set_size(void *app, int64_t width, int64_t height) {
    (void)app; (void)width; (void)height;
}
int64_t rt_app_get_width(void *app) { (void)app; return 0; }
int64_t rt_app_get_height(void *app) { (void)app; return 0; }
void rt_app_set_position(void *app, int64_t x, int64_t y) { (void)app; (void)x; (void)y; }
int64_t rt_app_get_x(void *app) { (void)app; return 0; }
int64_t rt_app_get_y(void *app) { (void)app; return 0; }
void rt_app_minimize(void *app) { (void)app; }
void rt_app_maximize(void *app) { (void)app; }
void rt_app_restore(void *app) { (void)app; }
int64_t rt_app_is_minimized(void *app) { (void)app; return 0; }
int64_t rt_app_is_maximized(void *app) { (void)app; return 0; }
void rt_app_set_fullscreen(void *app, int64_t fullscreen) { (void)app; (void)fullscreen; }
int64_t rt_app_is_fullscreen(void *app) { (void)app; return 0; }
void rt_app_focus(void *app) { (void)app; }
int64_t rt_app_is_focused(void *app) { (void)app; return 0; }
void rt_app_set_prevent_close(void *app, int64_t prevent) { (void)app; (void)prevent; }
int64_t rt_app_was_close_requested(void *app) { (void)app; return 0; }
int64_t rt_app_get_monitor_width(void *app) { (void)app; return 0; }
int64_t rt_app_get_monitor_height(void *app) { (void)app; return 0; }
void rt_app_set_window_size(void *app, int64_t w, int64_t h) { (void)app; (void)w; (void)h; }
double rt_app_get_font_size(void *app) { (void)app; return 14.0; }
void rt_app_set_font_size(void *app, double size) { (void)app; (void)size; }

void rt_cursor_set(int64_t type) { (void)type; }
void rt_cursor_reset(void) {}
void rt_cursor_set_visible(int64_t visible) { (void)visible; }
void rt_widget_set_cursor(void *widget, int64_t type) { (void)widget; (void)type; }
void rt_widget_reset_cursor(void *widget) { (void)widget; }

#endif /* VIPER_ENABLE_GRAPHICS */
