//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_gui_internal.h
// Purpose: Shared internal header for the split rt_gui implementation modules, declaring the global
// application pointer, default font state, and common helper functions.
//
// Key invariants:
//   - s_current_app must be set before widget constructors run.
//   - Default font is lazily initialized on first use.
//   - This header is implementation-only; it is not part of the public runtime API.
//   - App state persists for the duration of the GUI event loop.
//
// Ownership/Lifetime:
//   - Internal module header; not included by user code or public headers.
//   - All state declared here is owned by the GUI subsystem.
//
// Links: src/runtime/graphics/rt_gui.h, src/runtime/core/rt_string.h, src/runtime/oop/rt_object.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_gui.h"
#include "rt_object.h"
#include "rt_string.h"

#include "../lib/graphics/include/vgfx.h"
#include "../lib/gui/include/vg_event.h"
#include "../lib/gui/include/vg_font.h"
#include "../lib/gui/include/vg_ide_widgets.h"
#include "../lib/gui/include/vg_layout.h"
#include "../lib/gui/include/vg_theme.h"
#include "../lib/gui/include/vg_widget.h"
#include "../lib/gui/include/vg_widgets.h"

// Native file dialogs on macOS
#ifdef __APPLE__
#include "../lib/gui/src/dialogs/vg_filedialog_native.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// For strcasecmp: Windows uses _stricmp, POSIX uses strcasecmp
#ifdef _WIN32
#define strcasecmp _stricmp
#else
// Unix and ViperDOS: strings.h provides strcasecmp.
#include <strings.h>
#endif

//=============================================================================
// App state (defined in rt_gui_app.c)
//=============================================================================

/// @brief Internal application state for the GUI runtime.
/// @details Holds the graphics window, root widget, default font, mouse state,
///          and close flag. Defined in rt_gui_app.c and shared across split GUI modules.
typedef struct {
    char *id;
    char *keys;
    char *description;
    int enabled;
    int triggered;
    int parsed_ctrl;
    int parsed_shift;
    int parsed_alt;
    int parsed_key;
} rt_gui_shortcut_t;

typedef struct {
    char **files;
    int64_t file_count;
    int64_t was_dropped;
} rt_gui_file_drop_data_t;

typedef enum {
    RT_GUI_THEME_DARK = 0,
    RT_GUI_THEME_LIGHT = 1,
} rt_gui_theme_kind_t;

typedef struct {
    uint64_t magic;
    vgfx_window_t window;      ///< Underlying graphics window handle.
    vg_widget_t *root;         ///< Root widget container for the UI hierarchy.
    vg_font_t *default_font;   ///< Default font for widgets (lazily loaded).
    float default_font_size;   ///< Default font size in points.
    int default_font_owned;    ///< Non-zero when default_font is owned by the app.
    vg_font_t **retired_fonts;
    int retired_font_count;
    int retired_font_cap;
    int64_t should_close;      ///< Non-zero when the application should exit.
    vg_widget_t *last_clicked; ///< Widget clicked during the current frame.
    vg_statusbar_item_t *last_statusbar_clicked;
    vg_toolbar_item_t *last_toolbar_clicked;
    int32_t mouse_x;           ///< Current mouse X coordinate in window space.
    int32_t mouse_y;           ///< Current mouse Y coordinate in window space.
    uint64_t last_event_time_ms;
    vg_theme_t *theme;
    const vg_theme_t *theme_base;
    float theme_scale;
    rt_gui_theme_kind_t theme_kind;
    char *title;               ///< Window title (owned, heap-allocated).
    vg_dialog_t **dialog_stack;
    int dialog_count;
    int dialog_cap;
    vg_widget_t *drag_source;
    vg_widget_t *drag_over_widget;
    rt_gui_shortcut_t *shortcuts;
    int shortcut_count;
    int shortcut_cap;
    int shortcuts_global_enabled;
    char *triggered_shortcut_id;
    rt_gui_file_drop_data_t file_drop;
    vg_commandpalette_t **command_palettes;
    int command_palette_count;
    int command_palette_cap;
    vg_notification_manager_t *notification_manager;
    vg_tooltip_manager_t tooltip_manager_state;
    vg_tooltip_t *manual_tooltip;
    uint32_t manual_tooltip_delay_ms;
    vg_widget_runtime_state_t widget_runtime_state;
} rt_gui_app_t;

#define RT_GUI_APP_MAGIC UINT64_C(0x5254475541505031)

/// @brief Global pointer to the current app for widget constructors to access the default font.
extern rt_gui_app_t *s_current_app;

rt_gui_app_t *rt_gui_get_active_app(void);
void rt_gui_activate_app(rt_gui_app_t *app);
rt_gui_app_t *rt_gui_app_from_widget(vg_widget_t *widget);
uint64_t rt_gui_now_ms(void);
void rt_gui_refresh_theme(rt_gui_app_t *app);
void rt_gui_set_theme_kind(rt_gui_app_t *app, rt_gui_theme_kind_t kind);
void rt_gui_sync_modal_root(rt_gui_app_t *app);
void rt_gui_apply_default_font(vg_widget_t *widget);
void rt_gui_register_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette);
void rt_gui_unregister_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette);
void rt_gui_file_drop_add(rt_gui_app_t *app, const char *path);

static inline int rt_gui_is_app_handle(const void *handle) {
    if (!handle)
        return 0;
    const rt_gui_app_t *app = (const rt_gui_app_t *)handle;
    return app->magic == RT_GUI_APP_MAGIC;
}

static inline rt_gui_app_t *rt_gui_app_from_handle(void *handle) {
    if (!handle)
        return rt_gui_get_active_app();
    if (rt_gui_is_app_handle(handle))
        return (rt_gui_app_t *)handle;
    return rt_gui_app_from_widget((vg_widget_t *)handle);
}

static inline vg_widget_t *rt_gui_widget_parent_from_handle(void *handle) {
    if (!handle)
        return NULL;
    if (rt_gui_is_app_handle(handle)) {
        rt_gui_app_t *app = (rt_gui_app_t *)handle;
        return app->root;
    }
    return (vg_widget_t *)handle;
}

//=============================================================================
// Shared helpers
//=============================================================================

/// @brief Convert a runtime string to a heap-allocated NUL-terminated C string.
/// @details Allocates a new buffer via malloc, copies the string contents, and
///          appends a NUL terminator. The caller is responsible for freeing the
///          returned buffer.
/// @param str Runtime string to convert (may be NULL).
/// @return Heap-allocated C string, or NULL if str is NULL or allocation fails.
static inline char *rt_string_to_cstr(rt_string str) {
    if (!str)
        return NULL;
    size_t len = (size_t)rt_str_len(str);
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    memcpy(result, rt_string_cstr(str), len);
    result[len] = '\0';
    return result;
}

/// @brief Ensure the default font is loaded (lazy init on first use).
/// @details Loads the default font from the embedded font data if it has not
///          been loaded yet. Defined in rt_gui_app.c.
void rt_gui_ensure_default_font(void);

/// @brief Track the last clicked widget (set by GUI.App.Poll).
/// @details Records the widget that was clicked during the current event poll
///          cycle so that click handlers can query it. Defined in
///          rt_gui_widgets_complex.c.
/// @param widget Pointer to the clicked widget (may be NULL to clear).
void rt_gui_set_last_clicked(void *widget);

void rt_gui_push_dialog(rt_gui_app_t *app, vg_dialog_t *dlg);
void rt_gui_remove_dialog(rt_gui_app_t *app, vg_dialog_t *dlg);
vg_dialog_t *rt_gui_top_dialog(rt_gui_app_t *app);

/// @brief Free per-app feature resources owned by rt_gui_features.c.
/// @details Called from rt_gui_app_destroy. Defined in rt_gui_features.c.
void rt_gui_features_cleanup(rt_gui_app_t *app);

/// @brief Re-apply HiDPI scale to the current theme. Called after theme switch.
void rt_theme_apply_hidpi_scale(void);

//=============================================================================
// macOS native app menubar bridge
//=============================================================================

#ifdef __APPLE__
bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar);
void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar);
void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar);
void rt_gui_macos_menu_sync_app(rt_gui_app_t *app);
void rt_gui_macos_menu_app_destroy(rt_gui_app_t *app);
#else
static inline bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar) {
    (void)menubar;
    return false;
}
static inline void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar) {
    (void)menubar;
}
static inline void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar) {
    (void)menubar;
}
static inline void rt_gui_macos_menu_sync_app(rt_gui_app_t *app) {
    (void)app;
}
static inline void rt_gui_macos_menu_app_destroy(rt_gui_app_t *app) {
    (void)app;
}
#endif

/// @brief Clear all triggered shortcut flags for the current frame.
/// @details Called at the start of each poll cycle to reset shortcut state.
///          Defined in rt_gui_system.c.
void rt_shortcuts_clear_triggered(rt_gui_app_t *app);

/// @brief Check whether a key/modifier combination matches any registered shortcut.
/// @details Called during the poll loop to dispatch keyboard shortcuts.
///          Defined in rt_gui_system.c.
/// @param key  The key code that was pressed.
/// @param mods Active modifier flags (shift, ctrl, alt, etc.).
/// @return Non-zero if a matching shortcut was triggered; 0 otherwise.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods);
