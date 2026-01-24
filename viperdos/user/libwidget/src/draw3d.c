//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/draw3d.c
// Purpose: Amiga-style 3D drawing effects.
//
//===----------------------------------------------------------------------===//

#include <widget.h>

//===----------------------------------------------------------------------===//
// 3D Drawing Functions
//===----------------------------------------------------------------------===//

void draw_3d_raised(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow) {
    // Fill face
    gui_fill_rect(win, x, y, w, h, face);

    // Top edge (light)
    gui_draw_hline(win, x, x + w - 1, y, light);

    // Left edge (light)
    gui_draw_vline(win, x, y, y + h - 1, light);

    // Bottom edge (shadow)
    gui_draw_hline(win, x, x + w - 1, y + h - 1, shadow);

    // Right edge (shadow)
    gui_draw_vline(win, x + w - 1, y, y + h - 1, shadow);
}

void draw_3d_sunken(gui_window_t *win, int x, int y, int w, int h,
                    uint32_t face, uint32_t light, uint32_t shadow) {
    // Fill face
    gui_fill_rect(win, x, y, w, h, face);

    // Top edge (shadow)
    gui_draw_hline(win, x, x + w - 1, y, shadow);

    // Left edge (shadow)
    gui_draw_vline(win, x, y, y + h - 1, shadow);

    // Bottom edge (light)
    gui_draw_hline(win, x, x + w - 1, y + h - 1, light);

    // Right edge (light)
    gui_draw_vline(win, x + w - 1, y, y + h - 1, light);
}

void draw_3d_button(gui_window_t *win, int x, int y, int w, int h, bool pressed) {
    if (pressed) {
        // Pressed state - sunken
        draw_3d_sunken(win, x, y, w, h, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

        // Inner shadow for more depth
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_GRAY_DARK);
    } else {
        // Normal state - raised
        draw_3d_raised(win, x, y, w, h, WB_GRAY_LIGHT, WB_WHITE, WB_GRAY_DARK);

        // Extra highlight for Amiga look
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_WHITE);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_WHITE);
    }
}

void draw_3d_frame(gui_window_t *win, int x, int y, int w, int h, bool sunken) {
    if (sunken) {
        // Outer shadow
        gui_draw_hline(win, x, x + w - 1, y, WB_GRAY_DARK);
        gui_draw_vline(win, x, y, y + h - 1, WB_GRAY_DARK);

        // Outer light
        gui_draw_hline(win, x + 1, x + w - 1, y + h - 1, WB_WHITE);
        gui_draw_vline(win, x + w - 1, y + 1, y + h - 1, WB_WHITE);

        // Inner light
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_WHITE);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_WHITE);

        // Inner shadow
        gui_draw_hline(win, x + 2, x + w - 2, y + h - 2, WB_GRAY_DARK);
        gui_draw_vline(win, x + w - 2, y + 2, y + h - 2, WB_GRAY_DARK);
    } else {
        // Outer light
        gui_draw_hline(win, x, x + w - 1, y, WB_WHITE);
        gui_draw_vline(win, x, y, y + h - 1, WB_WHITE);

        // Outer shadow
        gui_draw_hline(win, x + 1, x + w - 1, y + h - 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + w - 1, y + 1, y + h - 1, WB_GRAY_DARK);

        // Inner shadow
        gui_draw_hline(win, x + 1, x + w - 2, y + 1, WB_GRAY_DARK);
        gui_draw_vline(win, x + 1, y + 1, y + h - 2, WB_GRAY_DARK);

        // Inner light
        gui_draw_hline(win, x + 2, x + w - 2, y + h - 2, WB_WHITE);
        gui_draw_vline(win, x + w - 2, y + 2, y + h - 2, WB_WHITE);
    }
}

void draw_3d_groove(gui_window_t *win, int x, int y, int w, int h) {
    // Horizontal groove
    if (w > h) {
        int cy = y + h / 2;
        gui_draw_hline(win, x, x + w - 1, cy, WB_GRAY_DARK);
        gui_draw_hline(win, x, x + w - 1, cy + 1, WB_WHITE);
    }
    // Vertical groove
    else {
        int cx = x + w / 2;
        gui_draw_vline(win, cx, y, y + h - 1, WB_GRAY_DARK);
        gui_draw_vline(win, cx + 1, y, y + h - 1, WB_WHITE);
    }
}
