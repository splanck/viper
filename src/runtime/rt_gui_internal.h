//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui_internal.h
// Purpose: Shared internal header for the split rt_gui modules.
//
//===----------------------------------------------------------------------===//

#ifndef RT_GUI_INTERNAL_H
#define RT_GUI_INTERNAL_H

#include "rt_gui.h"
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
#elif defined(__viperdos__)
static inline int viperdos_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

#define strcasecmp viperdos_strcasecmp
#else
#include <strings.h>
#endif

//=============================================================================
// App state (defined in rt_gui_app.c)
//=============================================================================

typedef struct
{
    vgfx_window_t window;      // Underlying graphics window
    vg_widget_t *root;         // Root widget container
    vg_font_t *default_font;   // Default font for widgets
    float default_font_size;   // Default font size
    int64_t should_close;      // Close flag
    vg_widget_t *last_clicked; // Widget clicked this frame
    int32_t mouse_x;           // Current mouse X
    int32_t mouse_y;           // Current mouse Y
} rt_gui_app_t;

// Global pointer to the current app for widget constructors to access the default font.
extern rt_gui_app_t *s_current_app;

//=============================================================================
// Shared helpers
//=============================================================================

static inline char *rt_string_to_cstr(rt_string str)
{
    if (!str)
        return NULL;
    size_t len = (size_t)rt_len(str);
    char *result = malloc(len + 1);
    if (!result)
        return NULL;
    memcpy(result, rt_string_cstr(str), len);
    result[len] = '\0';
    return result;
}

// Ensure the default font is loaded (lazy init on first use).
// Defined in rt_gui_app.c.
void rt_gui_ensure_default_font(void);

// Track the last clicked widget (set by GUI.App.Poll).
// Defined in rt_gui_widgets_complex.c.
void rt_gui_set_last_clicked(void *widget);

// Internal shortcut helpers called from rt_gui_app.c poll loop.
// Defined in rt_gui_system.c.
void rt_shortcuts_clear_triggered(void);
int rt_shortcuts_check_key(int key, int mods);

#endif // RT_GUI_INTERNAL_H
