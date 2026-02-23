//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_internal.h
// Purpose: Shared internal header for the split rt_gui implementation modules, declaring the global application pointer, default font state, and common helper functions.
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
#ifndef RT_GUI_INTERNAL_H
#define RT_GUI_INTERNAL_H

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
typedef struct
{
    vgfx_window_t window;      ///< Underlying graphics window handle.
    vg_widget_t *root;         ///< Root widget container for the UI hierarchy.
    vg_font_t *default_font;   ///< Default font for widgets (lazily loaded).
    float default_font_size;   ///< Default font size in points.
    int64_t should_close;      ///< Non-zero when the application should exit.
    vg_widget_t *last_clicked; ///< Widget clicked during the current frame.
    int32_t mouse_x;           ///< Current mouse X coordinate in window space.
    int32_t mouse_y;           ///< Current mouse Y coordinate in window space.
} rt_gui_app_t;

/// @brief Global pointer to the current app for widget constructors to access the default font.
extern rt_gui_app_t *s_current_app;

//=============================================================================
// Shared helpers
//=============================================================================

/// @brief Convert a runtime string to a heap-allocated NUL-terminated C string.
/// @details Allocates a new buffer via malloc, copies the string contents, and
///          appends a NUL terminator. The caller is responsible for freeing the
///          returned buffer.
/// @param str Runtime string to convert (may be NULL).
/// @return Heap-allocated C string, or NULL if str is NULL or allocation fails.
static inline char *rt_string_to_cstr(rt_string str)
{
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

/// @brief Set the active modal dialog.
/// @details When non-NULL, all events are routed to this dialog and the
///          dialog is rendered as a centered modal overlay above all other
///          widgets. Set to NULL to dismiss. Defined in rt_gui_app.c.
/// @param dlg Dialog handle (vg_dialog_t*), or NULL to clear.
void rt_gui_set_active_dialog(void *dlg);

/// @brief Clear all triggered shortcut flags for the current frame.
/// @details Called at the start of each poll cycle to reset shortcut state.
///          Defined in rt_gui_system.c.
void rt_shortcuts_clear_triggered(void);

/// @brief Check whether a key/modifier combination matches any registered shortcut.
/// @details Called during the poll loop to dispatch keyboard shortcuts.
///          Defined in rt_gui_system.c.
/// @param key  The key code that was pressed.
/// @param mods Active modifier flags (shift, ctrl, alt, etc.).
/// @return Non-zero if a matching shortcut was triggered; 0 otherwise.
int rt_shortcuts_check_key(int key, int mods);

#endif // RT_GUI_INTERNAL_H
