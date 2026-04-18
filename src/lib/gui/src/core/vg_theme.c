//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/core/vg_theme.c
//
//===----------------------------------------------------------------------===//
// vg_theme.c - Theme system implementation
#include "../../include/vg_theme.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Current Theme
//=============================================================================

static vg_theme_t *g_current_theme = NULL;

//=============================================================================
// Built-in Dark Theme
//=============================================================================

static vg_theme_t g_dark_theme = {
    .name = "Dark",
    .colors =
        {
            // Background colors (0x00RRGGBB format for vgfx compatibility)
            .bg_primary = 0x131922,
            .bg_secondary = 0x1A2230,
            .bg_tertiary = 0x243041,
            .bg_hover = 0x2D3B50,
            .bg_active = 0x1E527E,
            .bg_selected = 0x214F7A,
            .bg_disabled = 0x293240,

            // Foreground colors
            .fg_primary = 0xE7EEF7,
            .fg_secondary = 0xA9B5C6,
            .fg_tertiary = 0x7D8A9C,
            .fg_disabled = 0x627080,
            .fg_placeholder = 0x718097,
            .fg_link = 0x7CC4FF,

            // Accent colors
            .accent_primary = 0x49A6FF,
            .accent_secondary = 0x2F7DC6,
            .accent_danger = 0xFF6B6B,
            .accent_warning = 0xE8B04C,
            .accent_success = 0x71C784,
            .accent_info = 0x69B9FF,

            // Border colors
            .border_primary = 0x334156,
            .border_secondary = 0x273247,
            .border_focus = 0x49A6FF,

            // Syntax highlighting
            .syntax_keyword = 0x569CD6,  // Blue
            .syntax_type = 0x4EC9B0,     // Teal
            .syntax_function = 0xDCDCAA, // Yellow
            .syntax_variable = 0x9CDCFE, // Light blue
            .syntax_string = 0xCE9178,   // Orange
            .syntax_number = 0xB5CEA8,   // Light green
            .syntax_comment = 0x6A9955,  // Green
            .syntax_operator = 0xE0E0E0, // White
            .syntax_error = 0xF14C4C,    // Red
        },
    .typography =
        {
            .font_regular = NULL,
            .font_bold = NULL,
            .font_mono = NULL,
            .size_small = 11.0f,
            .size_normal = 13.5f,
            .size_large = 17.0f,
            .size_heading = 21.0f,
            .line_height = 1.4f,
        },
    .spacing =
        {
            .xs = 2.0f,
            .sm = 4.0f,
            .md = 8.0f,
            .lg = 16.0f,
            .xl = 24.0f,
        },
    .button =
        {
            .height = 28.0f,
            .padding_h = 16.0f,
            .border_radius = 4.0f,
            .border_width = 1.0f,
        },
    .input =
        {
            .height = 28.0f,
            .padding_h = 10.0f,
            .border_radius = 4.0f,
            .border_width = 1.0f,
        },
    .scrollbar =
        {
            .width = 12.0f,
            .min_thumb_size = 32.0f,
            .border_radius = 6.0f,
        },
};

//=============================================================================
// Built-in Light Theme
//=============================================================================

static vg_theme_t g_light_theme = {
    .name = "Light",
    .colors =
        {
            // Background colors (0x00RRGGBB format for vgfx compatibility)
            .bg_primary = 0xFAFBFC,
            .bg_secondary = 0xF1F4F8,
            .bg_tertiary = 0xE6ECF2,
            .bg_hover = 0xDFE8F2,
            .bg_active = 0xC6E0FF,
            .bg_selected = 0xD7E9FF,
            .bg_disabled = 0xF0F2F5,

            // Foreground colors
            .fg_primary = 0x17212E,
            .fg_secondary = 0x556273,
            .fg_tertiary = 0x788598,
            .fg_disabled = 0x99A3AF,
            .fg_placeholder = 0x8894A5,
            .fg_link = 0x1877F2,

            // Accent colors
            .accent_primary = 0x1877F2,
            .accent_secondary = 0x0D5FCC,
            .accent_danger = 0xD64545,
            .accent_warning = 0xC88926,
            .accent_success = 0x2C8F5A,
            .accent_info = 0x1877F2,

            // Border colors
            .border_primary = 0xCAD4E0,
            .border_secondary = 0xE0E6EE,
            .border_focus = 0x1877F2,

            // Syntax highlighting
            .syntax_keyword = 0x0000FF,  // Blue
            .syntax_type = 0x267F99,     // Teal
            .syntax_function = 0x795E26, // Brown
            .syntax_variable = 0x001080, // Dark blue
            .syntax_string = 0xA31515,   // Red
            .syntax_number = 0x098658,   // Green
            .syntax_comment = 0x008000,  // Green
            .syntax_operator = 0x000000, // Black
            .syntax_error = 0xE81123,    // Red
        },
    .typography =
        {
            .font_regular = NULL,
            .font_bold = NULL,
            .font_mono = NULL,
            .size_small = 11.0f,
            .size_normal = 13.5f,
            .size_large = 17.0f,
            .size_heading = 21.0f,
            .line_height = 1.4f,
        },
    .spacing =
        {
            .xs = 2.0f,
            .sm = 4.0f,
            .md = 8.0f,
            .lg = 16.0f,
            .xl = 24.0f,
        },
    .button =
        {
            .height = 28.0f,
            .padding_h = 16.0f,
            .border_radius = 4.0f,
            .border_width = 1.0f,
        },
    .input =
        {
            .height = 28.0f,
            .padding_h = 10.0f,
            .border_radius = 4.0f,
            .border_width = 1.0f,
        },
    .scrollbar =
        {
            .width = 12.0f,
            .min_thumb_size = 32.0f,
            .border_radius = 6.0f,
        },
};

//=============================================================================
// Theme API
//=============================================================================

vg_theme_t *vg_theme_get_current(void) {
    if (!g_current_theme) {
        g_current_theme = &g_dark_theme;
    }
    return g_current_theme;
}

/// @brief Theme set current.
void vg_theme_set_current(vg_theme_t *theme) {
    g_current_theme = theme ? theme : &g_dark_theme;
}

vg_theme_t *vg_theme_dark(void) {
    return &g_dark_theme;
}

vg_theme_t *vg_theme_light(void) {
    return &g_light_theme;
}

vg_theme_t *vg_theme_create(const char *name, const vg_theme_t *base) {
    vg_theme_t *theme = malloc(sizeof(vg_theme_t));
    if (!theme)
        return NULL;

    if (base) {
        *theme = *base;
    } else {
        *theme = g_dark_theme;
    }

    if (name) {
        theme->name = strdup(name);
    } else {
        theme->name = strdup("Custom");
    }

    return theme;
}

/// @brief Theme destroy.
void vg_theme_destroy(vg_theme_t *theme) {
    if (!theme)
        return;

    // Don't destroy built-in themes
    if (theme == &g_dark_theme || theme == &g_light_theme)
        return;

    if (theme->name) {
        free((void *)theme->name);
    }

    free(theme);
}

//=============================================================================
// Color Helpers
//=============================================================================

uint32_t vg_color_blend(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f)
        return c1;
    if (t >= 1.0f)
        return c2;

    uint8_t r1 = vg_color_r(c1), g1 = vg_color_g(c1), b1 = vg_color_b(c1), a1 = vg_color_a(c1);
    uint8_t r2 = vg_color_r(c2), g2 = vg_color_g(c2), b2 = vg_color_b(c2), a2 = vg_color_a(c2);

    float inv_t = 1.0f - t;
    uint8_t r = (uint8_t)(r1 * inv_t + r2 * t);
    uint8_t g = (uint8_t)(g1 * inv_t + g2 * t);
    uint8_t b = (uint8_t)(b1 * inv_t + b2 * t);
    uint8_t a = (uint8_t)(a1 * inv_t + a2 * t);

    return vg_rgba(r, g, b, a);
}

uint32_t vg_color_lighten(uint32_t color, float amount) {
    return vg_color_blend(color, 0xFFFFFFFF, amount);
}

uint32_t vg_color_darken(uint32_t color, float amount) {
    return vg_color_blend(color, 0xFF000000, amount);
}
