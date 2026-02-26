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
//     and freed by rt_shortcuts_destroy_all().
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

// Clipboard Functions (Phase 1)
//=============================================================================

void rt_clipboard_set_text(rt_string text)
{
    char *ctext = rt_string_to_cstr(text);
    if (ctext)
    {
        vgfx_clipboard_set_text(ctext);
        free(ctext);
    }
}

rt_string rt_clipboard_get_text(void)
{
    char *text = vgfx_clipboard_get_text();
    if (!text)
        return rt_str_empty();
    rt_string result = rt_string_from_bytes(text, strlen(text));
    free(text);
    return result;
}

int64_t rt_clipboard_has_text(void)
{
    return vgfx_clipboard_has_format(VGFX_CLIPBOARD_TEXT) ? 1 : 0;
}

void rt_clipboard_clear(void)
{
    vgfx_clipboard_clear();
}

//=============================================================================
// Keyboard Shortcuts (Phase 1)
//=============================================================================

// Internal shortcut storage
typedef struct
{
    char *id;
    char *keys;
    char *description;
    int enabled;
    int triggered; // Set to 1 when shortcut is triggered this frame
} rt_shortcut_t;

#define MAX_SHORTCUTS 256
static rt_shortcut_t g_shortcuts[MAX_SHORTCUTS];
static int g_shortcut_count = 0;
static int g_shortcuts_global_enabled = 1;
static char *g_triggered_shortcut_id = NULL;

// Parse modifier keys from string like "Ctrl+Shift+S"
static int parse_shortcut_keys(const char *keys, int *ctrl, int *shift, int *alt, int *key)
{
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
    while (token)
    {
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
            *end-- = '\0';

        if (strcasecmp(token, "Ctrl") == 0 || strcasecmp(token, "Control") == 0)
        {
            *ctrl = 1;
        }
        else if (strcasecmp(token, "Shift") == 0)
        {
            *shift = 1;
        }
        else if (strcasecmp(token, "Alt") == 0)
        {
            *alt = 1;
        }
        else if (strcasecmp(token, "Cmd") == 0 || strcasecmp(token, "Command") == 0)
        {
            *ctrl = 1; // Map Cmd to Ctrl for cross-platform
        }
        else if (strlen(token) == 1)
        {
            // Single character key
            *key = toupper(token[0]);
        }
        else if (token[0] == 'F' && strlen(token) <= 3)
        {
            // Function key (F1-F12)
            int fnum = atoi(token + 1);
            if (fnum >= 1 && fnum <= 12)
            {
                *key = 289 + fnum; // VGFX_KEY_F1 = 290 is approximated
            }
        }
        token = rt_strtok_r(NULL, "+", &saveptr);
    }

    free(copy);
    return (*key != 0);
}

void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description)
{
    if (g_shortcut_count >= MAX_SHORTCUTS)
        return;

    char *cid = rt_string_to_cstr(id);
    char *ckeys = rt_string_to_cstr(keys);
    char *cdesc = rt_string_to_cstr(description);

    if (!cid)
        return;

    // Check if already registered and update
    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(g_shortcuts[i].keys);
            free(g_shortcuts[i].description);
            g_shortcuts[i].keys = ckeys;
            g_shortcuts[i].description = cdesc;
            free(cid);
            return;
        }
    }

    // Add new shortcut
    g_shortcuts[g_shortcut_count].id = cid;
    g_shortcuts[g_shortcut_count].keys = ckeys;
    g_shortcuts[g_shortcut_count].description = cdesc;
    g_shortcuts[g_shortcut_count].enabled = 1;
    g_shortcuts[g_shortcut_count].triggered = 0;
    g_shortcut_count++;
}

void rt_shortcuts_unregister(rt_string id)
{
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(g_shortcuts[i].id);
            free(g_shortcuts[i].keys);
            free(g_shortcuts[i].description);

            // Shift remaining shortcuts down
            for (int j = i; j < g_shortcut_count - 1; j++)
            {
                g_shortcuts[j] = g_shortcuts[j + 1];
            }
            g_shortcut_count--;
            break;
        }
    }

    free(cid);
}

void rt_shortcuts_clear(void)
{
    for (int i = 0; i < g_shortcut_count; i++)
    {
        free(g_shortcuts[i].id);
        free(g_shortcuts[i].keys);
        free(g_shortcuts[i].description);
    }
    g_shortcut_count = 0;
    g_triggered_shortcut_id = NULL;
}

int64_t rt_shortcuts_was_triggered(rt_string id)
{
    if (!g_shortcuts_global_enabled)
        return 0;

    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(cid);
            return g_shortcuts[i].triggered ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

// Clear all shortcut triggered flags (call at start of each frame)
void rt_shortcuts_clear_triggered(void)
{
    for (int i = 0; i < g_shortcut_count; i++)
    {
        g_shortcuts[i].triggered = 0;
    }
    g_triggered_shortcut_id = NULL;
}

// Check if a key event matches any registered shortcut.
// Returns 1 if a shortcut was triggered, 0 otherwise.
int rt_shortcuts_check_key(int key, int mods)
{
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

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (!g_shortcuts[i].enabled || !g_shortcuts[i].keys)
            continue;

        int sc_ctrl = 0, sc_shift = 0, sc_alt = 0, sc_key = 0;
        if (!parse_shortcut_keys(g_shortcuts[i].keys, &sc_ctrl, &sc_shift, &sc_alt, &sc_key))
            continue;

        if (sc_ctrl == has_ctrl && sc_shift == has_shift && sc_alt == has_alt &&
            sc_key == upper_key)
        {
            g_shortcuts[i].triggered = 1;
            g_triggered_shortcut_id = g_shortcuts[i].id;
            return 1;
        }
    }
    return 0;
}

rt_string rt_shortcuts_get_triggered(void)
{
    if (g_triggered_shortcut_id)
    {
        return rt_string_from_bytes(g_triggered_shortcut_id, strlen(g_triggered_shortcut_id));
    }
    return rt_str_empty();
}

void rt_shortcuts_set_enabled(rt_string id, int64_t enabled)
{
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            g_shortcuts[i].enabled = enabled != 0;
            break;
        }
    }

    free(cid);
}

int64_t rt_shortcuts_is_enabled(rt_string id)
{
    char *cid = rt_string_to_cstr(id);
    if (!cid)
        return 0;

    for (int i = 0; i < g_shortcut_count; i++)
    {
        if (g_shortcuts[i].id && strcmp(g_shortcuts[i].id, cid) == 0)
        {
            free(cid);
            return g_shortcuts[i].enabled ? 1 : 0;
        }
    }

    free(cid);
    return 0;
}

void rt_shortcuts_set_global_enabled(int64_t enabled)
{
    g_shortcuts_global_enabled = enabled != 0;
}

int64_t rt_shortcuts_get_global_enabled(void)
{
    return g_shortcuts_global_enabled ? 1 : 0;
}

//=============================================================================
// Window Management (Phase 1)
//=============================================================================

void rt_app_set_title(void *app, rt_string title)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return;
    char *cstr = rt_string_to_cstr(title);
    if (cstr)
    {
        vgfx_set_title(gui_app->window, cstr);
        free(cstr);
    }
}

rt_string rt_app_get_title(void *app)
{
    (void)app;
    return rt_str_empty();
}

void rt_app_set_size(void *app, int64_t width, int64_t height)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->root)
    {
        vg_widget_set_fixed_size(gui_app->root, (float)width, (float)height);
        gui_app->root->width = (float)width;
        gui_app->root->height = (float)height;
    }
}

int64_t rt_app_get_width(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return w;
}

int64_t rt_app_get_height(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_size(gui_app->window, &w, &h);
    return h;
}

void rt_app_set_position(void *app, int64_t x, int64_t y)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_position(gui_app->window, (int32_t)x, (int32_t)y);
}

int64_t rt_app_get_x(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t x = 0, y = 0;
    vgfx_get_position(gui_app->window, &x, &y);
    return x;
}

int64_t rt_app_get_y(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t x = 0, y = 0;
    vgfx_get_position(gui_app->window, &x, &y);
    return y;
}

void rt_app_minimize(void *app)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_minimize(gui_app->window);
}

void rt_app_maximize(void *app)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_maximize(gui_app->window);
}

void rt_app_restore(void *app)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_restore(gui_app->window);
}

int64_t rt_app_is_minimized(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_minimized(gui_app->window) : 0;
}

int64_t rt_app_is_maximized(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_maximized(gui_app->window) : 0;
}

void rt_app_set_fullscreen(void *app, int64_t fullscreen)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_fullscreen(gui_app->window, (int32_t)fullscreen);
}

int64_t rt_app_is_fullscreen(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_fullscreen(gui_app->window) : 0;
}

void rt_app_focus(void *app)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_focus(gui_app->window);
}

int64_t rt_app_is_focused(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->window ? vgfx_is_focused(gui_app->window) : 0;
}

void rt_app_set_prevent_close(void *app, int64_t prevent)
{
    if (!app)
        return;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (gui_app->window)
        vgfx_set_prevent_close(gui_app->window, (int32_t)prevent);
}

int64_t rt_app_was_close_requested(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    return gui_app->should_close;
}

int64_t rt_app_get_monitor_width(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(gui_app->window, &w, &h);
    return (int64_t)w;
}

int64_t rt_app_get_monitor_height(void *app)
{
    if (!app)
        return 0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    if (!gui_app->window)
        return 0;
    int32_t w = 0, h = 0;
    vgfx_get_monitor_size(gui_app->window, &w, &h);
    return (int64_t)h;
}

void rt_app_set_window_size(void *app, int64_t w, int64_t h)
{
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

double rt_app_get_font_size(void *app)
{
    if (!app)
        return 14.0;
    rt_gui_app_t *gui_app = (rt_gui_app_t *)app;
    // Return logical pt size — divide stored physical pixels by HiDPI scale.
    float _s = gui_app->window ? vgfx_window_get_scale(gui_app->window) : 1.0f;
    if (_s <= 0.0f)
        _s = 1.0f;
    return (double)(gui_app->default_font_size / _s);
}

void rt_app_set_font_size(void *app, double size)
{
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

void rt_cursor_set(int64_t type)
{
    if (s_current_app && s_current_app->window)
        vgfx_set_cursor(s_current_app->window, (int32_t)type);
}

void rt_cursor_reset(void)
{
    rt_cursor_set(0); /* VGFX_CURSOR_DEFAULT */
}

void rt_cursor_set_visible(int64_t visible)
{
    if (s_current_app && s_current_app->window)
        vgfx_set_cursor_visible(s_current_app->window, (int32_t)visible);
}

void rt_widget_set_cursor(void *widget, int64_t type)
{
    (void)widget;
    rt_cursor_set(type);
}

void rt_widget_reset_cursor(void *widget)
{
    (void)widget;
    rt_cursor_reset();
}
