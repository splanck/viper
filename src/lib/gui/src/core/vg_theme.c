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
            .bg_primary = 0x1E1E1E,   // Main background - dark gray
            .bg_secondary = 0x2D2D30, // Secondary panels - noticeably lighter
            .bg_tertiary = 0x383838,  // Tertiary elements
            .bg_hover = 0x454545,     // Hover state - clearly visible
            .bg_active = 0x0A5A9C,    // Active/pressed - bright blue
            .bg_selected = 0x264F78,  // Selected items - blue highlight
            .bg_disabled = 0x3D3D3D,  // Disabled elements

            // Foreground colors
            .fg_primary = 0xE0E0E0,     // Primary text - bright white-gray
            .fg_secondary = 0xA0A0A0,   // Secondary text
            .fg_tertiary = 0x808080,    // Tertiary text
            .fg_disabled = 0x606060,    // Disabled text
            .fg_placeholder = 0x707070, // Placeholder text
            .fg_link = 0x3794FF,        // Links

            // Accent colors
            .accent_primary = 0x0E639C,   // Primary accent (blue)
            .accent_secondary = 0x007ACC, // Secondary accent
            .accent_danger = 0xF14C4C,    // Danger/error (red)
            .accent_warning = 0xCCA700,   // Warning (yellow)
            .accent_success = 0x89D185,   // Success (green)
            .accent_info = 0x3794FF,      // Info (blue)

            // Border colors
            .border_primary = 0x505050,   // Primary border - more visible
            .border_secondary = 0x404040, // Secondary border
            .border_focus = 0x007ACC,     // Focus ring

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
            .size_normal = 13.0f,
            .size_large = 16.0f,
            .size_heading = 20.0f,
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
            .height = 26.0f,
            .padding_h = 14.0f,
            .border_radius = 2.0f,
            .border_width = 1.0f,
        },
    .input =
        {
            .height = 24.0f,
            .padding_h = 8.0f,
            .border_radius = 2.0f,
            .border_width = 1.0f,
        },
    .scrollbar =
        {
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
    .colors =
        {
            // Background colors (0x00RRGGBB format for vgfx compatibility)
            .bg_primary = 0xFFFFFF,   // Main background - white
            .bg_secondary = 0xF0F0F0, // Secondary panels
            .bg_tertiary = 0xE0E0E0,  // Tertiary elements
            .bg_hover = 0xD8D8D8,     // Hover state
            .bg_active = 0x0060C0,    // Active/pressed
            .bg_selected = 0xCCE8FF,  // Selected items
            .bg_disabled = 0xEBEBEB,  // Disabled elements

            // Foreground colors
            .fg_primary = 0x1E1E1E,     // Primary text - black
            .fg_secondary = 0x6E6E6E,   // Secondary text
            .fg_tertiary = 0x8E8E8E,    // Tertiary text
            .fg_disabled = 0xA0A0A0,    // Disabled text
            .fg_placeholder = 0x8E8E8E, // Placeholder text
            .fg_link = 0x006AB1,        // Links

            // Accent colors
            .accent_primary = 0x0078D4,   // Primary accent (blue)
            .accent_secondary = 0x005A9E, // Secondary accent
            .accent_danger = 0xE81123,    // Danger/error (red)
            .accent_warning = 0xCA5010,   // Warning (orange)
            .accent_success = 0x107C10,   // Success (green)
            .accent_info = 0x0078D4,      // Info (blue)

            // Border colors
            .border_primary = 0xD4D4D4,   // Primary border
            .border_secondary = 0xE8E8E8, // Secondary border
            .border_focus = 0x0078D4,     // Focus ring

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
            .size_normal = 13.0f,
            .size_large = 16.0f,
            .size_heading = 20.0f,
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
            .height = 26.0f,
            .padding_h = 14.0f,
            .border_radius = 2.0f,
            .border_width = 1.0f,
        },
    .input =
        {
            .height = 24.0f,
            .padding_h = 8.0f,
            .border_radius = 2.0f,
            .border_width = 1.0f,
        },
    .scrollbar =
        {
            .width = 14.0f,
            .min_thumb_size = 40.0f,
            .border_radius = 7.0f,
        },
};

//=============================================================================
// Theme API
//=============================================================================

vg_theme_t *vg_theme_get_current(void)
{
    if (!g_current_theme)
    {
        g_current_theme = &g_dark_theme;
    }
    return g_current_theme;
}

void vg_theme_set_current(vg_theme_t *theme)
{
    g_current_theme = theme ? theme : &g_dark_theme;
}

vg_theme_t *vg_theme_dark(void)
{
    return &g_dark_theme;
}

vg_theme_t *vg_theme_light(void)
{
    return &g_light_theme;
}

vg_theme_t *vg_theme_create(const char *name, vg_theme_t *base)
{
    vg_theme_t *theme = malloc(sizeof(vg_theme_t));
    if (!theme)
        return NULL;

    if (base)
    {
        *theme = *base;
    }
    else
    {
        *theme = g_dark_theme;
    }

    if (name)
    {
        theme->name = strdup(name);
    }
    else
    {
        theme->name = strdup("Custom");
    }

    return theme;
}

void vg_theme_destroy(vg_theme_t *theme)
{
    if (!theme)
        return;

    // Don't destroy built-in themes
    if (theme == &g_dark_theme || theme == &g_light_theme)
        return;

    if (theme->name)
    {
        free((void *)theme->name);
    }

    free(theme);
}

//=============================================================================
// Color Helpers
//=============================================================================

uint32_t vg_color_blend(uint32_t c1, uint32_t c2, float t)
{
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

uint32_t vg_color_lighten(uint32_t color, float amount)
{
    return vg_color_blend(color, 0xFFFFFFFF, amount);
}

uint32_t vg_color_darken(uint32_t color, float amount)
{
    return vg_color_blend(color, 0xFF000000, amount);
}
