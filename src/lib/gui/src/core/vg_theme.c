// vg_theme.c - Theme system implementation
#include "../../include/vg_theme.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Current Theme
//=============================================================================

static vg_theme_t* g_current_theme = NULL;

//=============================================================================
// Built-in Dark Theme
//=============================================================================

static vg_theme_t g_dark_theme = {
    .name = "Dark",
    .colors = {
        // Background colors
        .bg_primary     = 0xFF1E1E1E,  // Main background
        .bg_secondary   = 0xFF252526,  // Secondary panels
        .bg_tertiary    = 0xFF2D2D30,  // Tertiary elements
        .bg_hover       = 0xFF3E3E42,  // Hover state
        .bg_active      = 0xFF094771,  // Active/pressed
        .bg_selected    = 0xFF264F78,  // Selected items
        .bg_disabled    = 0xFF3D3D3D,  // Disabled elements

        // Foreground colors
        .fg_primary     = 0xFFD4D4D4,  // Primary text
        .fg_secondary   = 0xFF808080,  // Secondary text
        .fg_tertiary    = 0xFF6E6E6E,  // Tertiary text
        .fg_disabled    = 0xFF5D5D5D,  // Disabled text
        .fg_placeholder = 0xFF6E6E6E,  // Placeholder text
        .fg_link        = 0xFF3794FF,  // Links

        // Accent colors
        .accent_primary = 0xFF0E639C,  // Primary accent (blue)
        .accent_secondary = 0xFF007ACC, // Secondary accent
        .accent_danger  = 0xFFF14C4C,  // Danger/error (red)
        .accent_warning = 0xFFCCA700,  // Warning (yellow)
        .accent_success = 0xFF89D185,  // Success (green)
        .accent_info    = 0xFF3794FF,  // Info (blue)

        // Border colors
        .border_primary = 0xFF3C3C3C,  // Primary border
        .border_secondary = 0xFF2D2D30, // Secondary border
        .border_focus   = 0xFF007ACC,  // Focus ring

        // Syntax highlighting
        .syntax_keyword = 0xFF569CD6,  // Blue
        .syntax_type    = 0xFF4EC9B0,  // Teal
        .syntax_function= 0xFFDCDCAA,  // Yellow
        .syntax_variable= 0xFF9CDCFE,  // Light blue
        .syntax_string  = 0xFFCE9178,  // Orange
        .syntax_number  = 0xFFB5CEA8,  // Light green
        .syntax_comment = 0xFF6A9955,  // Green
        .syntax_operator= 0xFFD4D4D4,  // White
        .syntax_error   = 0xFFF14C4C,  // Red
    },
    .typography = {
        .font_regular = NULL,
        .font_bold = NULL,
        .font_mono = NULL,
        .size_small = 11.0f,
        .size_normal = 13.0f,
        .size_large = 16.0f,
        .size_heading = 20.0f,
        .line_height = 1.4f,
    },
    .spacing = {
        .xs = 2.0f,
        .sm = 4.0f,
        .md = 8.0f,
        .lg = 16.0f,
        .xl = 24.0f,
    },
    .button = {
        .height = 26.0f,
        .padding_h = 14.0f,
        .border_radius = 2.0f,
        .border_width = 1.0f,
    },
    .input = {
        .height = 24.0f,
        .padding_h = 8.0f,
        .border_radius = 2.0f,
        .border_width = 1.0f,
    },
    .scrollbar = {
        .width = 14.0f,
        .min_thumb_size = 40.0f,
        .border_radius = 7.0f,
    },
};

//=============================================================================
// Built-in Light Theme
//=============================================================================

static vg_theme_t g_light_theme = {
    .name = "Light",
    .colors = {
        // Background colors
        .bg_primary     = 0xFFFFFFFF,  // Main background
        .bg_secondary   = 0xFFF3F3F3,  // Secondary panels
        .bg_tertiary    = 0xFFE8E8E8,  // Tertiary elements
        .bg_hover       = 0xFFE8E8E8,  // Hover state
        .bg_active      = 0xFF0060C0,  // Active/pressed
        .bg_selected    = 0xFFCCE8FF,  // Selected items
        .bg_disabled    = 0xFFEBEBEB,  // Disabled elements

        // Foreground colors
        .fg_primary     = 0xFF1E1E1E,  // Primary text
        .fg_secondary   = 0xFF6E6E6E,  // Secondary text
        .fg_tertiary    = 0xFF8E8E8E,  // Tertiary text
        .fg_disabled    = 0xFFA0A0A0,  // Disabled text
        .fg_placeholder = 0xFF8E8E8E,  // Placeholder text
        .fg_link        = 0xFF006AB1,  // Links

        // Accent colors
        .accent_primary = 0xFF0078D4,  // Primary accent (blue)
        .accent_secondary = 0xFF005A9E, // Secondary accent
        .accent_danger  = 0xFFE81123,  // Danger/error (red)
        .accent_warning = 0xFFCA5010,  // Warning (orange)
        .accent_success = 0xFF107C10,  // Success (green)
        .accent_info    = 0xFF0078D4,  // Info (blue)

        // Border colors
        .border_primary = 0xFFD4D4D4,  // Primary border
        .border_secondary = 0xFFE8E8E8, // Secondary border
        .border_focus   = 0xFF0078D4,  // Focus ring

        // Syntax highlighting
        .syntax_keyword = 0xFF0000FF,  // Blue
        .syntax_type    = 0xFF267F99,  // Teal
        .syntax_function= 0xFF795E26,  // Brown
        .syntax_variable= 0xFF001080,  // Dark blue
        .syntax_string  = 0xFFA31515,  // Red
        .syntax_number  = 0xFF098658,  // Green
        .syntax_comment = 0xFF008000,  // Green
        .syntax_operator= 0xFF000000,  // Black
        .syntax_error   = 0xFFE81123,  // Red
    },
    .typography = {
        .font_regular = NULL,
        .font_bold = NULL,
        .font_mono = NULL,
        .size_small = 11.0f,
        .size_normal = 13.0f,
        .size_large = 16.0f,
        .size_heading = 20.0f,
        .line_height = 1.4f,
    },
    .spacing = {
        .xs = 2.0f,
        .sm = 4.0f,
        .md = 8.0f,
        .lg = 16.0f,
        .xl = 24.0f,
    },
    .button = {
        .height = 26.0f,
        .padding_h = 14.0f,
        .border_radius = 2.0f,
        .border_width = 1.0f,
    },
    .input = {
        .height = 24.0f,
        .padding_h = 8.0f,
        .border_radius = 2.0f,
        .border_width = 1.0f,
    },
    .scrollbar = {
        .width = 14.0f,
        .min_thumb_size = 40.0f,
        .border_radius = 7.0f,
    },
};

//=============================================================================
// Theme API
//=============================================================================

vg_theme_t* vg_theme_get_current(void) {
    if (!g_current_theme) {
        g_current_theme = &g_dark_theme;
    }
    return g_current_theme;
}

void vg_theme_set_current(vg_theme_t* theme) {
    g_current_theme = theme ? theme : &g_dark_theme;
}

vg_theme_t* vg_theme_dark(void) {
    return &g_dark_theme;
}

vg_theme_t* vg_theme_light(void) {
    return &g_light_theme;
}

vg_theme_t* vg_theme_create(const char* name, vg_theme_t* base) {
    vg_theme_t* theme = malloc(sizeof(vg_theme_t));
    if (!theme) return NULL;

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

void vg_theme_destroy(vg_theme_t* theme) {
    if (!theme) return;

    // Don't destroy built-in themes
    if (theme == &g_dark_theme || theme == &g_light_theme) return;

    if (theme->name) {
        free((void*)theme->name);
    }

    free(theme);
}

//=============================================================================
// Color Helpers
//=============================================================================

uint32_t vg_color_blend(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;

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
