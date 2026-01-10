// vg_theme.h - Theming system for consistent widget appearance
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "vg_font.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Color Scheme
//=============================================================================

typedef struct vg_color_scheme {
    // Background colors
    uint32_t bg_primary;
    uint32_t bg_secondary;
    uint32_t bg_tertiary;
    uint32_t bg_hover;
    uint32_t bg_active;
    uint32_t bg_selected;
    uint32_t bg_disabled;

    // Foreground (text) colors
    uint32_t fg_primary;
    uint32_t fg_secondary;
    uint32_t fg_tertiary;
    uint32_t fg_disabled;
    uint32_t fg_placeholder;
    uint32_t fg_link;

    // Accent colors
    uint32_t accent_primary;
    uint32_t accent_secondary;
    uint32_t accent_danger;
    uint32_t accent_warning;
    uint32_t accent_success;
    uint32_t accent_info;

    // Border colors
    uint32_t border_primary;
    uint32_t border_secondary;
    uint32_t border_focus;

    // Syntax highlighting (for code editor)
    uint32_t syntax_keyword;
    uint32_t syntax_type;
    uint32_t syntax_function;
    uint32_t syntax_variable;
    uint32_t syntax_string;
    uint32_t syntax_number;
    uint32_t syntax_comment;
    uint32_t syntax_operator;
    uint32_t syntax_error;
} vg_color_scheme_t;

//=============================================================================
// Typography
//=============================================================================

typedef struct vg_typography {
    vg_font_t* font_regular;
    vg_font_t* font_bold;
    vg_font_t* font_mono;

    float size_small;        // e.g., 11px
    float size_normal;       // e.g., 13px
    float size_large;        // e.g., 16px
    float size_heading;      // e.g., 20px

    float line_height;       // e.g., 1.4
} vg_typography_t;

//=============================================================================
// Spacing
//=============================================================================

typedef struct vg_spacing {
    float xs;                // Extra small (2px)
    float sm;                // Small (4px)
    float md;                // Medium (8px)
    float lg;                // Large (16px)
    float xl;                // Extra large (24px)
} vg_spacing_t;

//=============================================================================
// Button Style
//=============================================================================

typedef struct vg_button_theme {
    float height;
    float padding_h;
    float border_radius;
    float border_width;
} vg_button_theme_t;

//=============================================================================
// Input Style
//=============================================================================

typedef struct vg_input_theme {
    float height;
    float padding_h;
    float border_radius;
    float border_width;
} vg_input_theme_t;

//=============================================================================
// Scrollbar Style
//=============================================================================

typedef struct vg_scrollbar_theme {
    float width;
    float min_thumb_size;
    float border_radius;
} vg_scrollbar_theme_t;

//=============================================================================
// Complete Theme
//=============================================================================

typedef struct vg_theme {
    const char* name;
    vg_color_scheme_t colors;
    vg_typography_t typography;
    vg_spacing_t spacing;
    vg_button_theme_t button;
    vg_input_theme_t input;
    vg_scrollbar_theme_t scrollbar;
} vg_theme_t;

//=============================================================================
// Theme API
//=============================================================================

/// Get current theme
vg_theme_t* vg_theme_get_current(void);

/// Set current theme
void vg_theme_set_current(vg_theme_t* theme);

/// Get built-in dark theme
vg_theme_t* vg_theme_dark(void);

/// Get built-in light theme
vg_theme_t* vg_theme_light(void);

/// Create custom theme (copy from base)
vg_theme_t* vg_theme_create(const char* name, vg_theme_t* base);

/// Destroy custom theme
void vg_theme_destroy(vg_theme_t* theme);

//=============================================================================
// Color Helpers
//=============================================================================

/// Create color from RGB components (0-255)
static inline uint32_t vg_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/// Create color from RGBA components (0-255)
static inline uint32_t vg_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/// Extract red component
static inline uint8_t vg_color_r(uint32_t color) {
    return (color >> 16) & 0xFF;
}

/// Extract green component
static inline uint8_t vg_color_g(uint32_t color) {
    return (color >> 8) & 0xFF;
}

/// Extract blue component
static inline uint8_t vg_color_b(uint32_t color) {
    return color & 0xFF;
}

/// Extract alpha component
static inline uint8_t vg_color_a(uint32_t color) {
    return (color >> 24) & 0xFF;
}

/// Blend two colors with factor t (0.0 = c1, 1.0 = c2)
uint32_t vg_color_blend(uint32_t c1, uint32_t c2, float t);

/// Lighten a color
uint32_t vg_color_lighten(uint32_t color, float amount);

/// Darken a color
uint32_t vg_color_darken(uint32_t color, float amount);

#ifdef __cplusplus
}
#endif
