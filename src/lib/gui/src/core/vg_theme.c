//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/core/vg_theme.c
// Purpose: Theme system implementation — built-in dark/light themes,
//          current-theme state, custom theme lifecycle, and colour utilities.
// Key invariants:
//   - Built-in themes are statically allocated; never pass them to vg_theme_destroy.
//   - g_current_theme defaults to the dark theme on first access.
// Ownership/Lifetime:
//   - Custom themes created by vg_theme_create own their name string.
// Links: lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_font.h
//
//===----------------------------------------------------------------------===//
#include "../../include/vg_theme.h"
#include <math.h>
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
    // Refined Depth visual tokens (dark): subtle gradients + real soft shadows,
    // tuned so depth reads clearly without lowering text/background contrast.
    .radius = {.none = 0.0f, .sm = 3.0f, .md = 6.0f, .lg = 10.0f, .xl = 16.0f, .pill = 9999.0f},
    .elevation =
        {
            .shadow_rgb = 0x000000,
            .level0 = {.blur = 0.0f, .dx = 0, .dy = 0, .alpha = 0},
            .level1 = {.blur = 4.0f, .dx = 0, .dy = 1, .alpha = 40},
            .level2 = {.blur = 10.0f, .dx = 0, .dy = 4, .alpha = 64},
            .level3 = {.blur = 20.0f, .dx = 0, .dy = 8, .alpha = 90},
        },
    .gradient = {.enabled = true, .strength = 0.06f},
    .focus = {.glow_color = 0x49A6FF, .glow_width = 2.0f, .glow_alpha = 120},
    .motion = {.enabled = true, .hover_ms = 90.0f, .press_ms = 60.0f, .focus_ms = 120.0f},
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
    // Refined Depth visual tokens (light): softer, cooler shadows on bright bg.
    .radius = {.none = 0.0f, .sm = 3.0f, .md = 6.0f, .lg = 10.0f, .xl = 16.0f, .pill = 9999.0f},
    .elevation =
        {
            .shadow_rgb = 0x0A1A2F,
            .level0 = {.blur = 0.0f, .dx = 0, .dy = 0, .alpha = 0},
            .level1 = {.blur = 6.0f, .dx = 0, .dy = 1, .alpha = 28},
            .level2 = {.blur = 14.0f, .dx = 0, .dy = 4, .alpha = 40},
            .level3 = {.blur = 26.0f, .dx = 0, .dy = 10, .alpha = 56},
        },
    .gradient = {.enabled = true, .strength = 0.04f},
    .focus = {.glow_color = 0x1877F2, .glow_width = 2.0f, .glow_alpha = 110},
    .motion = {.enabled = true, .hover_ms = 90.0f, .press_ms = 60.0f, .focus_ms = 120.0f},
};

//=============================================================================
// Theme API
//=============================================================================

/// @brief Returns the currently active theme, defaulting to the built-in dark theme on first call.
vg_theme_t *vg_theme_get_current(void) {
    if (!g_current_theme) {
        g_current_theme = &g_dark_theme;
    }
    return g_current_theme;
}

/// @brief Sets @p theme as the active theme; falls back to the dark theme if @p theme is NULL.
void vg_theme_set_current(vg_theme_t *theme) {
    g_current_theme = theme ? theme : &g_dark_theme;
}

/// @brief Returns a pointer to the statically-allocated built-in dark theme.
vg_theme_t *vg_theme_dark(void) {
    return &g_dark_theme;
}

/// @brief Returns a pointer to the statically-allocated built-in light theme.
vg_theme_t *vg_theme_light(void) {
    return &g_light_theme;
}

/// @brief Heap-allocates a new theme copied from @p base (or the dark theme if NULL), with @p name
/// strdup'd as its name.
vg_theme_t *vg_theme_create(const char *name, const vg_theme_t *base) {
    vg_theme_t *theme = malloc(sizeof(vg_theme_t));
    if (!theme)
        return NULL;

    if (base) {
        *theme = *base;
    } else {
        *theme = g_dark_theme;
    }

    theme->name = strdup(name ? name : "Custom");
    if (!theme->name) {
        free(theme);
        return NULL;
    }

    return theme;
}

/// @brief Frees a custom theme's name string and the theme struct; no-ops on NULL or built-in
/// themes.
void vg_theme_destroy(vg_theme_t *theme) {
    if (!theme)
        return;

    // Don't destroy built-in themes
    if (theme == &g_dark_theme || theme == &g_light_theme)
        return;

    if (g_current_theme == theme)
        g_current_theme = &g_dark_theme;

    if (theme->name) {
        free((void *)theme->name);
    }

    free(theme);
}

//=============================================================================
// Color Helpers
//=============================================================================

/// @brief Linearly interpolates between colors @p c1 and @p c2 by factor @p t in [0,1], blending
/// all four channels.
uint32_t vg_color_blend(uint32_t c1, uint32_t c2, float t) {
    if (!isfinite(t) || t <= 0.0f)
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

/// @brief Blends @p color toward white by @p amount (0 = no change, 1 = fully white).
uint32_t vg_color_lighten(uint32_t color, float amount) {
    return vg_color_blend(color, 0xFFFFFFFF, amount);
}

/// @brief Blends @p color toward black by @p amount (0 = no change, 1 = fully black).
uint32_t vg_color_darken(uint32_t color, float amount) {
    return vg_color_blend(color, 0xFF000000, amount);
}
