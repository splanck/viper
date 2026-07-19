//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_accessibility_wayland.c
// Purpose: Display-neutral preference fallback for native Wayland GUI sessions.
// Key invariants:
//   - Queries never connect to X11 or initialize a second display stack.
//   - Missing environment/portal state returns the stable zero fallback.
// Ownership/Lifetime: No window or widget is retained; all arguments are borrowed.
// Links: src/runtime/graphics/gui/rt_gui_accessibility_platform.h,
//        docs/adr/0139-native-wayland-backend-and-linux-runtime-selection.md
//
//===----------------------------------------------------------------------===//

#include "rt_gui_accessibility_platform.h"
#include "rt_gui_atspi_linux.h"
#include "rt_gui_linux_portal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int rt_gui_wayland_contains_ascii(const char *text, const char *needle) {
    if (!text || !needle || !needle[0])
        return 0;
    size_t needle_length = strlen(needle);
    for (; *text; ++text) {
        size_t matched = 0;
        while (matched < needle_length && text[matched] &&
               (unsigned char)tolower((unsigned char)text[matched]) ==
                   (unsigned char)tolower((unsigned char)needle[matched])) {
            ++matched;
        }
        if (matched == needle_length)
            return 1;
    }
    return 0;
}

int32_t rt_gui_accessibility_platform_high_contrast(vgfx_window_t window) {
    (void)window;
    if (rt_gui_wayland_contains_ascii(getenv("GTK_THEME"), "contrast") ||
        rt_gui_wayland_contains_ascii(getenv("QT_STYLE_OVERRIDE"), "contrast"))
        return 1;
    int32_t contrast = 0;
    return rt_gui_linux_portal_read("org.freedesktop.appearance", "contrast", &contrast) &&
                   contrast == 1
               ? 1
               : 0;
}

int32_t rt_gui_accessibility_platform_reduced_motion(vgfx_window_t window) {
    (void)window;
    const char *value = getenv("GTK_ENABLE_ANIMATIONS");
    if (value && (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
                  strcmp(value, "FALSE") == 0))
        return 1;
    int32_t animations = 1;
    return rt_gui_linux_portal_read(
               "org.freedesktop.desktop.interface", "enable-animations", &animations) &&
                   animations == 0
               ? 1
               : 0;
}

int32_t rt_gui_accessibility_platform_prefers_dark(vgfx_window_t window) {
    (void)window;
    if (rt_gui_wayland_contains_ascii(getenv("GTK_THEME"), "dark") ||
        rt_gui_wayland_contains_ascii(getenv("QT_STYLE_OVERRIDE"), "dark"))
        return 1;
    int32_t color_scheme = 0;
    return rt_gui_linux_portal_read(
               "org.freedesktop.appearance", "color-scheme", &color_scheme) &&
                   color_scheme == 1
               ? 1
               : 0;
}

void rt_gui_accessibility_platform_attach(vgfx_window_t window, vg_widget_t *root) {
    rt_gui_atspi_linux_attach(window, root);
}

void rt_gui_accessibility_platform_detach(vgfx_window_t window) {
    rt_gui_atspi_linux_detach(window);
}

void rt_gui_accessibility_platform_notify(vgfx_window_t window, vg_widget_t *widget) {
    rt_gui_atspi_linux_notify(window, widget);
}

void rt_gui_accessibility_platform_sync(vgfx_window_t window, vg_widget_t *root) {
    rt_gui_atspi_linux_sync(window, root);
}

void rt_gui_accessibility_platform_announce(vgfx_window_t window,
                                            vg_widget_t *widget,
                                            const char *text,
                                            vg_live_region_mode_t mode) {
    rt_gui_atspi_linux_announce(window, widget, text, mode);
}
