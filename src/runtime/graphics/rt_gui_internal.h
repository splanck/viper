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
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
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
    int parsed_super;
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
    vgfx_window_t window;    ///< Underlying graphics window handle.
    vg_widget_t *root;       ///< Root widget container for the UI hierarchy.
    vg_font_t *default_font; ///< Default font for widgets (lazily loaded).
    float default_font_size; ///< Default font size in points.
    int default_font_owned;  ///< Non-zero when default_font is owned by the app.
    vg_font_t **retired_fonts;
    int retired_font_count;
    int retired_font_cap;
    int64_t should_close;      ///< Non-zero when the application should exit.
    int64_t close_requested;   ///< Non-zero when a close request arrived this frame.
    int32_t prevent_close;     ///< Non-zero when close requests should not set should_close.
    vg_widget_t *last_clicked; ///< Widget clicked during the current frame.
    vg_statusbar_item_t *last_statusbar_clicked;
    vg_toolbar_item_t *last_toolbar_clicked;
    int32_t mouse_x; ///< Current mouse X coordinate in window space.
    int32_t mouse_y; ///< Current mouse Y coordinate in window space.
    uint64_t last_event_time_ms;
    uint64_t last_render_time_ms;
    int32_t last_layout_width;
    int32_t last_layout_height;
    vg_theme_t *theme;
    const vg_theme_t *theme_base;
    float theme_scale;
    rt_gui_theme_kind_t theme_kind;
    char *title; ///< Window title (owned, heap-allocated).
    vg_dialog_t **dialog_stack;
    int dialog_count;
    int dialog_cap;
    vg_widget_t *drag_candidate;
    int32_t drag_start_x;
    int32_t drag_start_y;
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

/// @brief The currently active GUI app (the one receiving input/rendering).
rt_gui_app_t *rt_gui_get_active_app(void);
/// @brief Make @p app the active GUI app.
void rt_gui_activate_app(rt_gui_app_t *app);
/// @brief Resolve the owning GUI app for a widget (NULL if unparented).
rt_gui_app_t *rt_gui_app_from_widget(vg_widget_t *widget);
/// @brief Monotonic millisecond clock used for GUI timing/animation.
uint64_t rt_gui_now_ms(void);
/// @brief Re-apply the current theme to @p app's widget tree (after a change).
void rt_gui_refresh_theme(rt_gui_app_t *app);
/// @brief Switch @p app's theme kind (e.g. light/dark) and refresh.
void rt_gui_set_theme_kind(rt_gui_app_t *app, rt_gui_theme_kind_t kind);
/// @brief Resync the modal overlay root after modals open/close.
void rt_gui_sync_modal_root(rt_gui_app_t *app);
/// @brief Assign the app's default font to @p widget if it has none.
void rt_gui_apply_default_font(vg_widget_t *widget);
/// @brief Re-apply @p app's current default font/size to all app-owned GUI surfaces.
void rt_gui_reapply_default_font(rt_gui_app_t *app);
/// @brief Register a command palette so @p app routes its shortcut to it.
void rt_gui_register_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette);
/// @brief Unregister a previously registered command palette.
void rt_gui_unregister_command_palette(rt_gui_app_t *app, vg_commandpalette_t *palette);
/// @brief Queue a dropped file @p path for @p app's drag-and-drop handlers.
void rt_gui_file_drop_add(rt_gui_app_t *app, const char *path);
/// @brief Whether @p handle is a currently-live GUI app handle.
int rt_gui_is_app_handle_known(const void *handle);
/// @brief Whether @p handle refers to a destroyed (stale) GUI app handle.
int rt_gui_is_destroyed_app_handle(const void *handle);
/// @brief Retire @p font from @p app (drop its reference once unused).
/// @return non-zero if the font was retired.
int rt_gui_retire_font(rt_gui_app_t *app, vg_font_t *font);
/// @brief Retire @p font globally only if no widget still uses it.
/// @return non-zero if the font was retired.
int rt_gui_retire_font_if_in_use(vg_font_t *font);

/// @brief True if @p handle is a known (live) GUI App handle.
static inline int rt_gui_is_app_handle(const void *handle) {
    return rt_gui_is_app_handle_known(handle);
}

/// @brief True if @p handle is a live widget handle.
static inline int rt_gui_is_widget_handle(const void *handle) {
    return handle && vg_widget_is_live((const vg_widget_t *)handle);
}

/// @brief Safe-cast @p handle to a widget, rejecting App/destroyed handles.
/// @return The widget, or NULL if @p handle is not a live widget handle.
static inline vg_widget_t *rt_gui_widget_handle_checked(void *handle) {
    if (!handle || rt_gui_is_app_handle(handle) || rt_gui_is_destroyed_app_handle(handle))
        return NULL;
    return rt_gui_is_widget_handle(handle) ? (vg_widget_t *)handle : NULL;
}

/// @brief Safe-cast @p handle to a widget of a specific @p type.
/// @return The widget if it is live and matches @p type, else NULL.
static inline vg_widget_t *rt_gui_widget_handle_checked_type(void *handle,
                                                            vg_widget_type_t type) {
    vg_widget_t *widget = rt_gui_widget_handle_checked(handle);
    return widget && widget->type == type ? widget : NULL;
}

/// @brief Safe-cast @p handle to an App, or NULL if it is not an App handle.
static inline rt_gui_app_t *rt_gui_app_handle_checked(void *handle) {
    return rt_gui_is_app_handle(handle) ? (rt_gui_app_t *)handle : NULL;
}

/// @brief Resolve any handle (NULL/App/widget) to its owning App.
/// @details NULL yields the active app; an App handle returns itself; a live
///          widget returns its owning app; a destroyed App handle yields NULL.
static inline rt_gui_app_t *rt_gui_app_from_handle(void *handle) {
    if (!handle)
        return rt_gui_get_active_app();
    if (rt_gui_is_app_handle(handle))
        return (rt_gui_app_t *)handle;
    if (rt_gui_is_destroyed_app_handle(handle))
        return NULL;
    return rt_gui_is_widget_handle(handle) ? rt_gui_app_from_widget((vg_widget_t *)handle) : NULL;
}

/// @brief Resolve a handle to the widget that should act as a parent.
/// @details An App handle resolves to its root widget; a live widget resolves
///          to itself; NULL or a destroyed App handle yields NULL.
static inline vg_widget_t *rt_gui_widget_parent_from_handle(void *handle) {
    if (!handle)
        return NULL;
    if (rt_gui_is_app_handle(handle)) {
        rt_gui_app_t *app = (rt_gui_app_t *)handle;
        return app->root;
    }
    if (rt_gui_is_destroyed_app_handle(handle))
        return NULL;
    return rt_gui_is_widget_handle(handle) ? (vg_widget_t *)handle : NULL;
}

/// @brief True if widgets of @p type can safely own arbitrary runtime children.
static inline int rt_gui_widget_type_accepts_runtime_children(vg_widget_type_t type) {
    switch (type) {
        case VG_WIDGET_CONTAINER:
        case VG_WIDGET_SCROLLVIEW:
        case VG_WIDGET_SPLITPANE:
        case VG_WIDGET_DIALOG:
        case VG_WIDGET_LISTVIEW:
        case VG_WIDGET_CUSTOM:
            return 1;
        case VG_WIDGET_LABEL:
        case VG_WIDGET_BUTTON:
        case VG_WIDGET_TEXTINPUT:
        case VG_WIDGET_CHECKBOX:
        case VG_WIDGET_RADIO:
        case VG_WIDGET_SLIDER:
        case VG_WIDGET_PROGRESS:
        case VG_WIDGET_LISTBOX:
        case VG_WIDGET_DROPDOWN:
        case VG_WIDGET_TREEVIEW:
        case VG_WIDGET_TABBAR:
        case VG_WIDGET_MENUBAR:
        case VG_WIDGET_MENU:
        case VG_WIDGET_MENUITEM:
        case VG_WIDGET_TOOLBAR:
        case VG_WIDGET_STATUSBAR:
        case VG_WIDGET_CODEEDITOR:
        case VG_WIDGET_IMAGE:
        case VG_WIDGET_SPINNER:
        case VG_WIDGET_COLORSWATCH:
        case VG_WIDGET_COLORPALETTE:
        case VG_WIDGET_COLORPICKER:
        default:
            return 0;
    }
}

/// @brief Resolve a parent handle only when it can own arbitrary runtime children.
/// @details NULL is valid for detached widget creation and returns NULL. Non-NULL
///          invalid handles and leaf widgets both return NULL; callers that need
///          to distinguish explicit NULL should check their original argument.
static inline vg_widget_t *rt_gui_widget_parent_container_from_handle(void *handle) {
    vg_widget_t *parent = rt_gui_widget_parent_from_handle(handle);
    if (!parent)
        return NULL;
    return rt_gui_widget_type_accepts_runtime_children(parent->type) ? parent : NULL;
}

#define RT_GUI_MAX_LAYOUT_VALUE 1000000.0
#define RT_GUI_STRING_DATA_MAGIC UINT64_C(0x5254475544535452)

#ifdef _MSC_VER
#define RT_GUI_FLEX_ARRAY_SIZE 1
#else
#define RT_GUI_FLEX_ARRAY_SIZE
#endif

typedef struct {
    uint64_t magic;
    size_t len;
    char bytes[RT_GUI_FLEX_ARRAY_SIZE];
} rt_gui_string_data_t;

/// @brief 1 if @p value is finite (not NaN/Inf), else 0.
static inline int rt_gui_double_is_finite(double value) {
    return isfinite(value) ? 1 : 0;
}

/// @brief Clamp @p value to the inclusive [min_value, max_value] range.
static inline double rt_gui_clamp_f64(double value, double min_value, double max_value) {
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

/// @brief Clamp a double to [0, max_value] as a float; non-finite -> 0.
static inline float rt_gui_sanitize_nonnegative_float(double value, double max_value) {
    if (!rt_gui_double_is_finite(value))
        return 0.0f;
    return (float)rt_gui_clamp_f64(value, 0.0, max_value);
}

/// @brief Clamp a double to [-max_abs_value, max_abs_value] as a float;
///        non-finite -> 0.
static inline float rt_gui_sanitize_signed_float(double value, double max_abs_value) {
    if (!rt_gui_double_is_finite(value))
        return 0.0f;
    return (float)rt_gui_clamp_f64(value, -max_abs_value, max_abs_value);
}

/// @brief Clamp an int64 to the inclusive [min_value, max_value] int32 range.
static inline int32_t rt_gui_clamp_i64_to_i32(int64_t value, int32_t min_value, int32_t max_value) {
    if (value < (int64_t)min_value)
        return min_value;
    if (value > (int64_t)max_value)
        return max_value;
    return (int32_t)value;
}

/// @brief Validate a font size, returning @p fallback for non-finite input and
///        clamping otherwise to the supported [1, 256] point range.
static inline double rt_gui_sanitize_font_size(double size, double fallback) {
    if (!rt_gui_double_is_finite(size))
        return fallback;
    return rt_gui_clamp_f64(size, 1.0, 256.0);
}

/// @brief Allocate an owned, magic-tagged copy of a runtime string's bytes.
/// @details Used to give widgets a stable NUL-terminated C buffer they own.
///          The leading magic is an internal validation guard for fields whose
///          owns_user_data flag already says they contain this wrapper type.
/// @return New rt_gui_string_data_t (caller frees via free_if_owned), or NULL.
static inline rt_gui_string_data_t *rt_gui_string_data_new(rt_string value) {
    int64_t len64 = rt_str_len(value);
    if (len64 < 0)
        return NULL;
    size_t len = (size_t)len64;
    const size_t header_size = offsetof(rt_gui_string_data_t, bytes);
    if (len > SIZE_MAX - header_size - 1)
        return NULL;
    const char *bytes = len ? rt_string_cstr(value) : "";
    if (len && !bytes)
        return NULL;
    rt_gui_string_data_t *data =
        (rt_gui_string_data_t *)malloc(header_size + len + 1);
    if (!data)
        return NULL;
    data->magic = RT_GUI_STRING_DATA_MAGIC;
    data->len = len;
    if (len)
        memcpy(data->bytes, bytes, len);
    data->bytes[len] = '\0';
    return data;
}

/// @brief True if @p ptr is an rt_gui_string_data_t block (magic matches).
/// @details Only call this for pointers that an out-of-band ownership flag says
///          may be an rt_gui_string_data_t. It is not a safe generic borrowed
///          C-string discriminator.
static inline int rt_gui_string_data_is_owned(const void *ptr) {
    if (!ptr)
        return 0;
    const rt_gui_string_data_t *data = (const rt_gui_string_data_t *)ptr;
    return data->magic == RT_GUI_STRING_DATA_MAGIC;
}

/// @brief free() @p ptr only if it is a GUI-owned string-data block.
/// @details Only use this when an ownership flag says @p ptr may hold runtime
///          string data; plain borrowed strings are not self-describing.
static inline void rt_gui_string_data_free_if_owned(void *ptr) {
    if (rt_gui_string_data_is_owned(ptr))
        free(ptr);
}

/// @brief Convert a GUI string handle back to a runtime string.
/// @details Handles owned string-data blocks using their stored byte length.
///          NULL or an unexpected block yields the empty string.
static inline rt_string rt_gui_string_data_to_rt_string(const void *ptr) {
    if (!ptr)
        return rt_str_empty();
    if (rt_gui_string_data_is_owned(ptr)) {
        const rt_gui_string_data_t *data = (const rt_gui_string_data_t *)ptr;
        return rt_string_from_bytes(data->bytes, data->len);
    }
    return rt_str_empty();
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
    int64_t len64 = rt_str_len(str);
    if (len64 < 0)
        return NULL;
    size_t len = (size_t)len64;
    if (len > SIZE_MAX - 1)
        return NULL;
    const char *bytes = len ? rt_string_cstr(str) : "";
    if (len && !bytes)
        return NULL;
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    if (len)
        memcpy(result, bytes, len);
    result[len] = '\0';
    return result;
}

/// @brief Convert a runtime string to GUI-visible UTF-8 text.
/// @details GUI widgets store and render NUL-terminated text, so embedded NUL
///          bytes are replaced with U+FFFD instead of truncating the suffix.
///          NULL runtime strings become an allocated empty string.
static inline char *rt_string_to_gui_cstr(rt_string str) {
    if (!str) {
        char *empty = malloc(1);
        if (empty)
            empty[0] = '\0';
        return empty;
    }

    int64_t len64 = rt_str_len(str);
    if (len64 < 0)
        return NULL;
    size_t len = (size_t)len64;
    const char *bytes = len ? rt_string_cstr(str) : "";
    if (len && !bytes)
        return NULL;

    size_t nul_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] == '\0')
            nul_count++;
    }
    if (len > SIZE_MAX - 1 || nul_count > (SIZE_MAX - len - 1) / 2)
        return NULL;

    char *result = malloc(len + nul_count * 2 + 1);
    if (!result)
        return NULL;

    char *out = result;
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] == '\0') {
            memcpy(out, "\xEF\xBF\xBD", 3);
            out += 3;
        } else {
            *out++ = bytes[i];
        }
    }
    *out = '\0';
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

/// @brief Push @p dlg onto @p app's modal-dialog stack (it becomes topmost).
void rt_gui_push_dialog(rt_gui_app_t *app, vg_dialog_t *dlg);
/// @brief Remove @p dlg from @p app's dialog stack (no-op if not present).
void rt_gui_remove_dialog(rt_gui_app_t *app, vg_dialog_t *dlg);
/// @brief The topmost dialog on @p app's stack, or NULL if none is open.
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
/// @brief Register @p menubar as the app's native macOS menubar.
/// @return true if it became the active native menubar. Defined in
///         rt_gui_macos_menu.m.
bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar);
/// @brief Unregister a previously registered native macOS menubar.
void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar);
/// @brief Rebuild the native macOS menu from @p menubar's current contents.
void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar);
/// @brief Rebuild the native macOS menu for @p app's current menubar.
void rt_gui_macos_menu_sync_app(rt_gui_app_t *app);
/// @brief Tear down native macOS menu state owned by @p app.
void rt_gui_macos_menu_app_destroy(rt_gui_app_t *app);
#else
/// @brief Non-Apple no-op stub for the native macOS menubar bridge.
/// @return Always false (no native menubar on this platform).
static inline bool rt_gui_macos_menu_register_menubar(vg_menubar_t *menubar) {
    (void)menubar;
    return false;
}

/// @brief Non-Apple no-op stub: unregister a native macOS menubar.
static inline void rt_gui_macos_menu_unregister_menubar(vg_menubar_t *menubar) {
    (void)menubar;
}

/// @brief Non-Apple no-op stub: sync a menubar to the native macOS menu.
static inline void rt_gui_macos_menu_sync_for_menubar(vg_menubar_t *menubar) {
    (void)menubar;
}

/// @brief Non-Apple no-op stub: sync an app's menus to the native macOS menu.
static inline void rt_gui_macos_menu_sync_app(rt_gui_app_t *app) {
    (void)app;
}

/// @brief Non-Apple no-op stub: tear down native macOS menus for an app.
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
/// @param key  The translated VG_KEY_* code that was pressed.
/// @param mods Translated VG_MOD_* flags active for the event.
/// @return Non-zero if a matching shortcut was triggered; 0 otherwise.
int8_t rt_shortcuts_check_key(rt_gui_app_t *app, int key, int mods);
