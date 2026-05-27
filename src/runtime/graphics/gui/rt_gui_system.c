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
    if (app->shortcut_cap > INT_MAX / 2)
        return;
    int new_cap = app->shortcut_cap ? app->shortcut_cap * 2 : 16;
    if ((size_t)new_cap > SIZE_MAX / sizeof(*app->shortcuts))
        return;
    void *p = realloc(app->shortcuts, (size_t)new_cap * sizeof(*app->shortcuts));
    if (!p)
        return;
    app->shortcuts = p;
    app->shortcut_cap = new_cap;
}

/// @brief Parse a `+`-separated shortcut string (e.g. `"Ctrl+Shift+S"`) into modifier flags + key code.
///
/// Recognises `Ctrl`/`Control`, `Shift`, `Alt`, `Cmd`/`Command`
/// (`Cmd` is distinct on macOS and maps to Ctrl/Super-compatible shortcuts elsewhere),
/// `F1`-`F12`, named keys (`Enter`, `Escape`, arrows, etc.), and
/// any single character. Case-insensitive.
/// @return 1 on success, 0 on invalid syntax or alloc failure.
static int parse_shortcut_keys(
    const char *keys, int *ctrl, int *shift, int *alt, int *super, int *key) {
    *ctrl = 0;
    *shift = 0;
    *alt = 0;
    *super = 0;
    *key = 0;

    if (!keys)
        return 0;

    char *copy = strdup(keys);
    if (!copy)
        return 0;

    char *saveptr = NULL;
    char *token = rt_strtok_r(copy, "+", &saveptr);
    while (token) {
        int recognized = 0;
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token);
        while (end > token && end[-1] == ' ')
            *--end = '\0';

        if (*token == '\0') {
            free(copy);
            return 0;
        }

        if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0) {
            *ctrl = 1;
            recognized = 1;
        } else if (strcasecmp(token, "Shift") == 0) {
            *shift = 1;
            recognized = 1;
        } else if (strcasecmp(token, "Alt") == 0) {
            *alt = 1;
            recognized = 1;
        } else if (strcasecmp(token, "Cmd") == 0 || strcasecmp(token, "Command") == 0) {
            *super = 1;
            recognized = 1;
        } else if (strlen(token) == 1) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            // Single character key
            *key = toupper((unsigned char)token[0]);
            recognized = 1;
        } else if ((token[0] == 'F' || token[0] == 'f') && strlen(token) <= 3) {
            // Function key (F1-F12)
            char *parse_end = NULL;
            long fnum = strtol(token + 1, &parse_end, 10);
            if (parse_end && *parse_end == '\0' && fnum >= 1 && fnum <= 12) {
                if (*key != 0) {
                    free(copy);
                    return 0;
                }
                *key = VG_KEY_F1 + ((int)fnum - 1);
                recognized = 1;
            }
        } else if (strcasecmp(token, "Enter") == 0 || strcasecmp(token, "Return") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_ENTER;
            recognized = 1;
        } else if (strcasecmp(token, "Escape") == 0 || strcasecmp(token, "Esc") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_ESCAPE;
            recognized = 1;
        } else if (strcasecmp(token, "Space") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_SPACE;
            recognized = 1;
        } else if (strcasecmp(token, "Tab") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_TAB;
            recognized = 1;
        } else if (strcasecmp(token, "Backspace") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_BACKSPACE;
            recognized = 1;
        } else if (strcasecmp(token, "Delete") == 0 || strcasecmp(token, "Del") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_DELETE;
            recognized = 1;
        } else if (strcasecmp(token, "Home") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_HOME;
            recognized = 1;
        } else if (strcasecmp(token, "End") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_END;
            recognized = 1;
        } else if (strcasecmp(token, "PageUp") == 0 || strcasecmp(token, "PgUp") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_PAGE_UP;
            recognized = 1;
        } else if (strcasecmp(token, "PageDown") == 0 || strcasecmp(token, "PgDn") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_PAGE_DOWN;
            recognized = 1;
        } else if (strcasecmp(token, "Left") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_LEFT;
            recognized = 1;
        } else if (strcasecmp(token, "Right") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_RIGHT;
            recognized = 1;
        } else if (strcasecmp(token, "Up") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_UP;
            recognized = 1;
        } else if (strcasecmp(token, "Down") == 0) {
            if (*key != 0) {
                free(copy);
                return 0;
            }
            *key = VG_KEY_DOWN;
            recognized = 1;
        }
        if (!recognized) {
            free(copy);
            return 0;
        }
        token = rt_strtok_r(NULL, "+", &saveptr);
    }

    free(copy);
    return (*key != 0);
}

/// @brief Register (or update) a named keyboard shortcut on the active GUI app.
/// @details If a shortcut with the same `id` is already registered its key
///          binding and description are updated in-place and the pre-parsed
///          modifier/key fields are recomputed.  If it is new, the shortcut
///          table is grown as needed (doubling policy) before appending.
///          All three strings are duplicated into the table and freed by
///          `rt_shortcuts_clear` or `rt_shortcuts_unregister`.
/// @param id       Unique string identifier used to poll and toggle the shortcut.
/// @param keys     Human-readable key binding, e.g. "Ctrl+Shift+S".
/// @param description Optional description shown in a help/shortcut dialog.
void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return;
    if (rt_string_contains_nul(id) || rt_string_contains_nul(keys))
        return;

    char *cid = rt_string_to_cstr(id);
    char *ckeys = rt_string_to_cstr(keys);
    char *cdesc = rt_string_to_gui_cstr(description);

    if (!cid || !ckeys) {
        free(cid);
        free(ckeys);
        free(cdesc);
        return;
    }

    int parsed_ctrl = 0;
    int parsed_shift = 0;
    int parsed_alt = 0;
    int parsed_super = 0;
    int parsed_key = 0;
    if (!parse_shortcut_keys(
            ckeys, &parsed_ctrl, &parsed_shift, &parsed_alt, &parsed_super, &parsed_key)) {
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
            app->shortcuts[i].parsed_ctrl = parsed_ctrl;
            app->shortcuts[i].parsed_shift = parsed_shift;
            app->shortcuts[i].parsed_alt = parsed_alt;
            app->shortcuts[i].parsed_super = parsed_super;
            app->shortcuts[i].parsed_key = parsed_key;
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
    sc->parsed_ctrl = parsed_ctrl;
    sc->parsed_shift = parsed_shift;
    sc->parsed_alt = parsed_alt;
    sc->parsed_super = parsed_super;
    sc->parsed_key = parsed_key;
    app->shortcut_count++;
}

/// @brief Remove a registered shortcut by id, freeing its stored strings.
/// @details Scans the table linearly; on match frees id/keys/description,
///          then shifts the tail of the array down to keep it contiguous.
/// @param id Identifier passed to `rt_shortcuts_register`.
void rt_shortcuts_unregister(rt_string id) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app)
        return;
    if (rt_string_contains_nul(id))
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
    if (!app || !app->triggered_shortcut_id || !id)
        return 0;
    if (rt_string_contains_nul(id))
        return 0;

    int64_t id_len64 = rt_str_len(id);
    if (id_len64 < 0)
        return 0;
    size_t id_len = (size_t)id_len64;
    size_t triggered_len = strlen(app->triggered_shortcut_id);
    if (id_len != triggered_len)
        return 0;

    const char *id_bytes = id_len > 0 ? rt_string_cstr(id) : "";
    return id_bytes && memcmp(id_bytes, app->triggered_shortcut_id, id_len) == 0 ? 1 : 0;
}

/// @brief Reset all per-shortcut triggered flags and the cached triggered-id string.
/// @details Called at the start of each frame (from `rt_gui_app_poll`) before
///          processing new key events, so that `rt_shortcuts_was_triggered` reads
///          reflect only events in the current frame.
/// @param app App whose shortcut table is being reset; no-op if NULL.
void rt_shortcuts_clear_triggered(rt_gui_app_t *app) {
    RT_ASSERT_MAIN_THREAD();
    if (!app)
        return;
    if (!app->triggered_shortcut_id)
        return;
    for (int i = 0; i < app->shortcut_count; i++) {
        app->shortcuts[i].triggered = 0;
    }
    free(app->triggered_shortcut_id);
    app->triggered_shortcut_id = NULL;
}

/// @brief Match a key event against registered global shortcuts; trigger on hit.
/// @details Walks the shortcut table and returns 1 if any enabled shortcut's
///          (ctrl, shift, alt, key) tuple matches the incoming event. Several
///          normalisations apply before comparison:
///          - **Cmd/Ctrl intent.** `Cmd` registrations match Super on macOS,
///            while non-Apple platforms keep `Cmd` usable by mapping it to the
///            same Ctrl/Super-compatible event bucket as `Ctrl`.
///          - **Modifier requirement.** Plain keys (no Ctrl, no Alt) are
///            rejected up front *unless* they're function keys F1–F12, which
///            are valid shortcuts on their own. This prevents typing `S` in a
///            text field from accidentally firing the `S` half of `Ctrl+S`.
///          - **Case folding.** Lowercase letters are upper-cased for
///            comparison so the parsed-shortcut table and incoming events
///            match regardless of the user's caps-lock state.
///
///          On match, the shortcut's `triggered` flag is set and its id is
///          copied into `app->triggered_shortcut_id` for `Shortcuts.GetTriggered`
///          to read on the same frame. The previous triggered id (if any) is
///          freed first.
/// @param app App owning the shortcut table.
/// @param key Key code from the event.
/// @param mods Modifier bitmask (`VG_MOD_*`).
/// @return 1 if a shortcut was triggered, 0 otherwise.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods) {
    RT_ASSERT_MAIN_THREAD();
    if (!app || !app->shortcuts_global_enabled)
        return 0;

    int has_ctrl = (mods & VG_MOD_CTRL) ? 1 : 0;
    int has_super = (mods & VG_MOD_SUPER) ? 1 : 0;
    int has_shift = (mods & VG_MOD_SHIFT) ? 1 : 0;
    int has_alt = (mods & VG_MOD_ALT) ? 1 : 0;
#ifdef __APPLE__
    int event_ctrl = has_ctrl;
    int event_super = has_super;
#else
    int event_ctrl = has_ctrl || has_super;
    int event_super = 0;
#endif

    // Only check if at least one modifier is held (plain keys aren't shortcuts)
    if (!event_ctrl && !event_super && !has_alt && !(key >= VG_KEY_F1 && key <= VG_KEY_F12))
        return 0;

    int upper_key = (key >= 'a' && key <= 'z') ? key - ('a' - 'A') : key;

    for (int i = 0; i < app->shortcut_count; i++) {
        if (!app->shortcuts[i].enabled || !app->shortcuts[i].parsed_key)
            continue;

        int expected_ctrl = app->shortcuts[i].parsed_ctrl;
        int expected_super = app->shortcuts[i].parsed_super;
#ifndef __APPLE__
        expected_ctrl = expected_ctrl || expected_super;
        expected_super = 0;
#endif

        if (expected_ctrl == event_ctrl && expected_super == event_super &&
            app->shortcuts[i].parsed_shift == has_shift &&
            app->shortcuts[i].parsed_alt == has_alt && app->shortcuts[i].parsed_key == upper_key) {
            char *new_triggered_id =
                app->shortcuts[i].id ? strdup(app->shortcuts[i].id) : NULL;
            if (app->shortcuts[i].id && !new_triggered_id)
                return 0;
            app->shortcuts[i].triggered = 1;
            free(app->triggered_shortcut_id);
            app->triggered_shortcut_id = new_triggered_id;
            return 1;
        }
    }
    return 0;
}

/// @brief Return the id of the last shortcut triggered this frame, or an empty string.
/// @details Reads `app->triggered_shortcut_id` which is set by `rt_shortcuts_check_key`
///          when a key event matches a registered shortcut.  Valid only for the current
///          frame; cleared at the start of the next poll by `rt_shortcuts_clear_triggered`.
/// @return The shortcut id string, or an empty rt_string if no shortcut fired this frame.
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
    if (rt_string_contains_nul(id))
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
    if (rt_string_contains_nul(id))
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

/// @brief Enable or disable all shortcut processing for the active app at once.
/// @details When the global flag is false, `rt_shortcuts_check_key` returns 0
///          without scanning the table, silencing all shortcuts (e.g. while a
///          text input has focus so typing does not fire editor commands).
/// @param enabled Non-zero to enable shortcut processing; 0 to suppress it.
void rt_shortcuts_set_global_enabled(int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (app)
        app->shortcuts_global_enabled = enabled != 0;
}

/// @brief Return whether global shortcut processing is currently active for the app.
/// @return 1 if shortcuts are enabled globally, 0 if suppressed.
int64_t rt_shortcuts_get_global_enabled(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    return app && app->shortcuts_global_enabled ? 1 : 0;
}

//=============================================================================
// Window Management (Phase 1)
//=============================================================================

/// @brief Validate `app` as a live app handle and return it, or NULL if invalid/destroyed.
/// @details Thin wrapper around rt_gui_app_handle_checked that provides a
///          concise name for the window-management helpers in this file.
static rt_gui_app_t *rt_app_checked(void *app) {
    return rt_gui_app_handle_checked(app);
}

/// @brief Return the window's HiDPI scale factor, defaulting to 1.0 for NULL or non-finite results.
/// @details Used by window-size helpers to convert between logical and physical
///          pixels. Guards against zero/NaN scale values from unusual display
///          configurations.
static float rt_app_window_scale(rt_gui_app_t *app) {
    float scale = (app && app->window) ? vgfx_window_get_scale(app->window) : 1.0f;
    return (!isfinite(scale) || scale <= 0.0f) ? 1.0f : scale;
}

/// @brief Resize the platform window after clamping `w`/`h` to the valid vgfx range.
/// @details Scales the maximum allowed size by the current HiDPI factor so
///          the logical-pixel cap is respected even on high-DPI displays. Does
///          NOT set a fixed size on the root widget — root sizing is driven by
///          the layout engine in rt_gui_app_render().
static void rt_app_set_window_size_checked(rt_gui_app_t *app, int64_t w, int64_t h) {
    if (!app || !app->window)
        return;

    float scale = rt_app_window_scale(app);
    double max_w_d = (double)VGFX_MAX_WIDTH / (double)scale;
    double max_h_d = (double)VGFX_MAX_HEIGHT / (double)scale;
    int32_t max_w = max_w_d > (double)INT32_MAX ? INT32_MAX : (int32_t)max_w_d;
    int32_t max_h = max_h_d > (double)INT32_MAX ? INT32_MAX : (int32_t)max_h_d;
    if (max_w < 1)
        max_w = 1;
    if (max_h < 1)
        max_h = 1;

    int32_t clamped_w = rt_gui_clamp_i64_to_i32(w, 1, max_w);
    int32_t clamped_h = rt_gui_clamp_i64_to_i32(h, 1, max_h);
    vgfx_set_window_size(app->window, clamped_w, clamped_h);
    // Root sizing is handled by vg_widget_layout(root, phys_w, phys_h) in
    // rt_gui_app_render; setting a root fixed size here would corrupt layout.
}

/// @brief Set the title of the app.
void rt_app_set_title(void *app, rt_string title) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (!gui_app->window)
        return;
    char *cstr = rt_string_to_gui_cstr(title);
    if (!cstr)
        return;
    char *stored = strdup(cstr);
    if (!stored) {
        free(cstr);
        return;
    }
    vgfx_set_title(gui_app->window, cstr);
    // Store a copy for rt_app_get_title (no vgfx_get_title API exists)
    free(gui_app->title);
    gui_app->title = stored;
    rt_gui_macos_menu_sync_app(gui_app);
    free(cstr);
}

/// @brief Get the title of the app.
rt_string rt_app_get_title(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return rt_str_empty();
    if (gui_app->title)
        return rt_string_from_bytes(gui_app->title, strlen(gui_app->title));
    return rt_str_empty();
}

/// @brief Resize the app window to the requested logical-pixel dimensions.
/// @param app    App handle.
/// @param width  New window width in logical pixels.
/// @param height New window height in logical pixels.
void rt_app_set_size(void *app, int64_t width, int64_t height) {
    RT_ASSERT_MAIN_THREAD();
    rt_app_set_window_size_checked(rt_app_checked(app), width, height);
}

/// @brief Get the width of the app.
int64_t rt_app_get_width(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return w;
}

/// @brief Get the height of the app.
int64_t rt_app_get_height(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return h;
}

/// @brief Get the window's HiDPI backing scale factor (>= 1.0).
/// @details Window width/height are reported in physical pixels; dividing by this
///          factor recovers the logical (point) dimensions used for layout
///          breakpoints. Mirrors the scaling already applied by the monitor getters.
double rt_app_get_scale(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 1.0;
    return (double)rt_app_window_scale(gui_app);
}

/// @brief Move the app window to a specific screen position.
void rt_app_set_position(void *app, int64_t x, int64_t y) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_set_position(gui_app->window,
                          rt_gui_clamp_i64_to_i32(x, INT32_MIN, INT32_MAX),
                          rt_gui_clamp_i64_to_i32(y, INT32_MIN, INT32_MAX));
}

/// @brief Return the x screen coordinate of the app window's top-left corner.
/// @return X position in screen pixels, or 0 if the app or window is invalid.
int64_t rt_app_get_x(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    if (!gui_app->window)
        return 0;
    int32_t x = 0, y = 0;
    vgfx_get_position(gui_app->window, &x, &y);
    return x;
}

/// @brief Return the y screen coordinate of the app window's top-left corner.
/// @return Y position in screen pixels, or 0 if the app or window is invalid.
int64_t rt_app_get_y(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    if (!gui_app->window)
        return 0;
    int32_t x = 0, y = 0;
    vgfx_get_position(gui_app->window, &x, &y);
    return y;
}

/// @brief Minimize the app.
void rt_app_minimize(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_minimize(gui_app->window);
}

/// @brief Maximize the app.
void rt_app_maximize(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_maximize(gui_app->window);
}

/// @brief Restore the app.
void rt_app_restore(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_restore(gui_app->window);
}

/// @brief Check whether the app window is currently minimized.
int64_t rt_app_is_minimized(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    return gui_app->window ? vgfx_is_minimized(gui_app->window) : 0;
}

/// @brief Check whether the app window is currently maximized.
int64_t rt_app_is_maximized(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    return gui_app->window ? vgfx_is_maximized(gui_app->window) : 0;
}

/// @brief Enter or exit fullscreen mode for the app window.
/// @param app        App handle.
/// @param fullscreen Non-zero to enter fullscreen, 0 to restore windowed mode.
void rt_app_set_fullscreen(void *app, int64_t fullscreen) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_set_fullscreen(gui_app->window, (int32_t)(fullscreen != 0));
}

/// @brief Check whether the app window is in fullscreen mode.
int64_t rt_app_is_fullscreen(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    return gui_app->window ? vgfx_is_fullscreen(gui_app->window) : 0;
}

/// @brief Focus the app.
void rt_app_focus(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_focus(gui_app->window);
}

/// @brief Check whether the app window currently has OS-level focus.
int64_t rt_app_is_focused(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    return gui_app->window ? vgfx_is_focused(gui_app->window) : 0;
}

/// @brief Control whether the OS close button is allowed to close the window.
/// @details When `prevent` is non-zero the platform close request is intercepted
///          and `rt_app_was_close_requested` returns 1 instead, letting the app
///          show a "Save before quitting?" dialog before deciding to exit.
/// @param app     App handle.
/// @param prevent Non-zero to block automatic close; 0 to allow it.
void rt_app_set_prevent_close(void *app, int64_t prevent) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    if (gui_app->window)
        vgfx_set_prevent_close(gui_app->window, (int32_t)(prevent != 0));
    gui_app->prevent_close = (prevent != 0);
}

/// @brief Check whether the window close was requested this frame.
int64_t rt_app_was_close_requested(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    int64_t requested = gui_app->close_requested ? 1 : 0;
    gui_app->close_requested = 0;
    return requested;
}

/// @brief Get the monitor width of the app.
int64_t rt_app_get_monitor_width(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(gui_app->window, &w, &h);
    float scale = rt_app_window_scale(gui_app);
    return (int64_t)((double)w / (double)scale);
}

/// @brief Get the monitor height of the app.
int64_t rt_app_get_monitor_height(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 0;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(gui_app->window, &w, &h);
    float scale = rt_app_window_scale(gui_app);
    return (int64_t)((double)h / (double)scale);
}

/// @brief Alias for `rt_app_set_size` — resize the app window in logical pixels.
/// @param app App handle.
/// @param w   New window width in logical pixels.
/// @param h   New window height in logical pixels.
void rt_app_set_window_size(void *app, int64_t w, int64_t h) {
    RT_ASSERT_MAIN_THREAD();
    rt_app_set_window_size_checked(rt_app_checked(app), w, h);
}

/// @brief Return the default font size for the app window in logical points.
/// @return Font size in logical points, defaulting to 14.0 if the app is invalid.
double rt_app_get_font_size(void *app) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return 14.0;
    return (double)gui_app->default_font_size;
}

/// @brief Set the default font size for the app window in logical points.
/// @details Clamps `size` to [6, 72] pt before storing. The window/canvas
///          backend owns HiDPI coordinate scaling.
/// @param app  App handle.
/// @param size Desired font size in logical points.
void rt_app_set_font_size(void *app, double size) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *gui_app = rt_app_checked(app);
    if (!gui_app)
        return;
    size = rt_gui_sanitize_font_size(size, rt_app_get_font_size(app));
    if (size < 6.0)
        size = 6.0;
    if (size > 72.0)
        size = 72.0;
    gui_app->default_font_size = (float)size;
    rt_gui_reapply_default_font(gui_app);
}

//=============================================================================
// Cursor Styles (Phase 1)
//=============================================================================

/// @brief Change the mouse cursor shape for the active app window.
/// @param type A `VGFX_CURSOR_*` constant (e.g. VGFX_CURSOR_DEFAULT, VGFX_CURSOR_HAND).
void rt_cursor_set(int64_t type) {
    RT_ASSERT_MAIN_THREAD();
    rt_gui_app_t *app = rt_shortcuts_app();
    if (!app || !app->window)
        return;
    if (type < VGFX_CURSOR_DEFAULT || type > VGFX_CURSOR_WAIT)
        type = VGFX_CURSOR_DEFAULT;
    vgfx_set_cursor(app->window, (int32_t)type);
}

/// @brief Restore the mouse cursor to the default arrow shape.
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

/// @brief Stub: graphics disabled — no-op; shortcut registration is silently ignored.
void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description) {
    (void)id;
    (void)keys;
    (void)description;
}

/// @brief Stub: graphics disabled — no-op; no shortcut table exists to remove from.
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

/// @brief Stub: graphics disabled — no-op; no triggered shortcut state to clear.
void rt_shortcuts_clear_triggered(rt_gui_app_t *app) {
    (void)app;
}

/// @brief Stub: graphics disabled — returns 0; no shortcut can be triggered without graphics.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods) {
    (void)app;
    (void)key;
    (void)mods;
    return 0;
}

/// @brief Stub: graphics disabled — returns empty string; no shortcut was triggered.
rt_string rt_shortcuts_get_triggered(void) {
    return rt_str_empty();
}

/// @brief Stub: graphics disabled — no-op; no shortcut to enable or disable.
void rt_shortcuts_set_enabled(rt_string id, int64_t enabled) {
    (void)id;
    (void)enabled;
}

/// @brief Stub: graphics disabled — returns 0; no shortcuts exist to query.
int64_t rt_shortcuts_is_enabled(rt_string id) {
    (void)id;
    return 0;
}

/// @brief Stub: graphics disabled — no-op; global shortcut flag has no effect.
void rt_shortcuts_set_global_enabled(int64_t enabled) {
    (void)enabled;
}

/// @brief Stub: graphics disabled — returns 0; global shortcuts are always inactive.
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

/// @brief Stub: graphics disabled — no-op; no window exists to resize.
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

/// @brief Stub: graphics disabled — no HiDPI scaling, returns 1.0.
double rt_app_get_scale(void *app) {
    (void)app;
    return 1.0;
}

/// @brief Move the app window to a specific screen position.
void rt_app_set_position(void *app, int64_t x, int64_t y) {
    (void)app;
    (void)x;
    (void)y;
}

/// @brief Stub: graphics disabled — returns 0; no window position exists.
int64_t rt_app_get_x(void *app) {
    (void)app;
    return 0;
}

/// @brief Stub: graphics disabled — returns 0; no window position exists.
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

/// @brief Stub: graphics disabled — no-op; no window to enter or exit fullscreen.
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

/// @brief Stub: graphics disabled — no-op; no window close event can be intercepted.
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

/// @brief Stub: graphics disabled — no-op; no window exists to resize.
void rt_app_set_window_size(void *app, int64_t w, int64_t h) {
    (void)app;
    (void)w;
    (void)h;
}

/// @brief Stub: graphics disabled — returns default 14.0 pt; no real app window exists.
double rt_app_get_font_size(void *app) {
    (void)app;
    return 14.0;
}

/// @brief Stub: graphics disabled — no-op; no app window font size to set.
void rt_app_set_font_size(void *app, double size) {
    (void)app;
    (void)size;
}

/// @brief Stub: graphics disabled — no-op; no window cursor to change.
void rt_cursor_set(int64_t type) {
    (void)type;
}

/// @brief Stub: graphics disabled — no-op; no window cursor to reset.
void rt_cursor_reset(void) {}

/// @brief Show or hide the mouse cursor.
void rt_cursor_set_visible(int64_t visible) {
    (void)visible;
}

/// @brief Stub: graphics disabled — no-op; no widget or cursor exists to change.
void rt_widget_set_cursor(void *widget, int64_t type) {
    (void)widget;
    (void)type;
}

/// @brief Stub: graphics disabled — no-op; no widget or cursor exists to reset.
void rt_widget_reset_cursor(void *widget) {
    (void)widget;
}

#endif /* VIPER_ENABLE_GRAPHICS */

//=============================================================================
// Viper.System.Clipboard compatibility surface
//=============================================================================

/// @brief Copy text to the system clipboard via the canonical GUI clipboard backend.
void rt_system_clipboard_set(rt_string text) {
    rt_clipboard_set_text(text);
}

/// @brief Read UTF-8 text from the system clipboard via the canonical GUI clipboard backend.
rt_string rt_system_clipboard_get(void) {
    return rt_clipboard_get_text();
}

/// @brief Check whether the system clipboard currently exposes text.
int64_t rt_system_clipboard_has_text(void) {
    return rt_clipboard_has_text() ? 1 : 0;
}
