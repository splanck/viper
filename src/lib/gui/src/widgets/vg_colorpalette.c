//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_colorpalette.c
// Purpose: Color palette widget — displays a grid of colour swatches that the
//          user can click to select; fires a per-colour callback on selection.
// Key invariants:
//   - colors is a heap-allocated uint32_t array (AARRGGBB); NULL when empty.
//   - selected_index is -1 when no swatch is selected.
//   - Measured size is computed from columns × swatch_size + gaps; a single
//     swatch_size fallback is used when color_count == 0.
//   - colorpalette_hit_test_swatch uses absolute widget coordinates from the
//     event; gaps between swatches intentionally return -1 (no hit).
// Ownership/Lifetime:
//   - palette->colors is owned by the widget; freed on colorpalette_destroy,
//     vg_colorpalette_clear, and each call to vg_colorpalette_set_colors.
// Links: lib/gui/include/vg_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void colorpalette_destroy(vg_widget_t *widget);
static void colorpalette_measure(vg_widget_t *widget,
                                 float available_width,
                                 float available_height);
static void colorpalette_paint(vg_widget_t *widget, void *canvas);
static bool colorpalette_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool colorpalette_can_focus(vg_widget_t *widget);

//=============================================================================
// ColorPalette VTable
//=============================================================================

static vg_widget_vtable_t g_colorpalette_vtable = {.destroy = colorpalette_destroy,
                                                   .measure = colorpalette_measure,
                                                   .arrange = NULL,
                                                   .paint = colorpalette_paint,
                                                   .handle_event = colorpalette_handle_event,
                                                   .can_focus = colorpalette_can_focus,
                                                   .on_focus = NULL};

//=============================================================================
// Standard 16-color palette (classic Windows/DOS colors)
//=============================================================================

static const uint32_t g_standard_16_colors[] = {
    0xFF000000, // Black
    0xFF800000, // Dark Red
    0xFF008000, // Dark Green
    0xFF808000, // Dark Yellow (Olive)
    0xFF000080, // Dark Blue
    0xFF800080, // Dark Magenta
    0xFF008080, // Dark Cyan
    0xFFC0C0C0, // Light Gray
    0xFF808080, // Dark Gray
    0xFFFF0000, // Red
    0xFF00FF00, // Green
    0xFFFFFF00, // Yellow
    0xFF0000FF, // Blue
    0xFFFF00FF, // Magenta
    0xFF00FFFF, // Cyan
    0xFFFFFFFF  // White
};

//=============================================================================
// ColorPalette Implementation
//=============================================================================

/// @brief Create an empty color palette widget.
///
/// @details Default layout is 8 columns, 20×20 pixel swatches with 2-pixel
///          gaps.  Appearance colours are taken from the current theme.
///
/// @param parent Widget to attach to as a child (may be NULL).
/// @return Newly allocated vg_colorpalette_t, or NULL on allocation failure.
vg_colorpalette_t *vg_colorpalette_create(vg_widget_t *parent) {
    vg_colorpalette_t *palette = calloc(1, sizeof(vg_colorpalette_t));
    if (!palette)
        return NULL;

    // Initialize base widget
    vg_widget_init(&palette->base, VG_WIDGET_COLORPALETTE, &g_colorpalette_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize palette-specific fields
    palette->colors = NULL;
    palette->color_count = 0;
    palette->columns = 8;
    palette->selected_index = -1;

    // Default appearance
    palette->swatch_size = 20.0f;
    palette->gap = 2.0f;
    palette->bg_color = theme->colors.bg_secondary;
    palette->border_color = theme->colors.border_primary;
    palette->selected_border = theme->colors.accent_primary;

    // Callbacks
    palette->on_select = NULL;
    palette->on_select_data = NULL;

    // Set minimum size
    palette->base.constraints.min_width = palette->swatch_size;
    palette->base.constraints.min_height = palette->swatch_size;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &palette->base);
    }

    return palette;
}

/// @brief VTable destroy: frees the owned colours array and resets color_count to zero.
static void colorpalette_destroy(vg_widget_t *widget) {
    vg_colorpalette_t *palette = (vg_colorpalette_t *)widget;
    if (palette->colors) {
        free(palette->colors);
        palette->colors = NULL;
    }
    palette->color_count = 0;
}

/// @brief VTable measure: computes grid dimensions from column count × swatch_size + gaps, then
/// applies constraints; falls back to a single swatch size when the palette is empty.
static void colorpalette_measure(vg_widget_t *widget,
                                 float available_width,
                                 float available_height) {
    vg_colorpalette_t *palette = (vg_colorpalette_t *)widget;
    (void)available_width;
    (void)available_height;

    if (palette->color_count == 0 || palette->columns <= 0) {
        widget->measured_width = palette->swatch_size;
        widget->measured_height = palette->swatch_size;
        return;
    }

    // Calculate grid dimensions
    int rows = (palette->color_count + palette->columns - 1) / palette->columns;

    float total_width =
        palette->columns * palette->swatch_size + (palette->columns - 1) * palette->gap;
    float total_height = rows * palette->swatch_size + (rows - 1) * palette->gap;

    widget->measured_width = total_width;
    widget->measured_height = total_height;

    // Apply constraints
    if (widget->constraints.min_width > widget->measured_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->constraints.min_height > widget->measured_height) {
        widget->measured_height = widget->constraints.min_height;
    }
    if (widget->constraints.max_width > 0 &&
        widget->measured_width > widget->constraints.max_width) {
        widget->measured_width = widget->constraints.max_width;
    }
    if (widget->constraints.max_height > 0 &&
        widget->measured_height > widget->constraints.max_height) {
        widget->measured_height = widget->constraints.max_height;
    }
}

/// @brief VTable paint: fills the background, draws each swatch in the grid, and renders a
/// double-border highlight around the selected swatch.
static void colorpalette_paint(vg_widget_t *widget, void *canvas) {
    vg_colorpalette_t *palette = (vg_colorpalette_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;

    int32_t bx = (int32_t)widget->x;
    int32_t by = (int32_t)widget->y;
    int32_t bw = (int32_t)widget->width;
    int32_t bh = (int32_t)widget->height;
    if (bw > 0 && bh > 0)
        vgfx_fill_rect(win, bx, by, bw, bh, palette->bg_color);

    if (palette->color_count == 0 || !palette->colors) {
        if (bw > 0 && bh > 0)
            vgfx_rect(win, bx, by, bw, bh, palette->border_color);
        return;
    }

    // Draw each color swatch in the grid
    for (int i = 0; i < palette->color_count; i++) {
        int col = i % palette->columns;
        int row = i / palette->columns;

        float swatch_x = widget->x + col * (palette->swatch_size + palette->gap);
        float swatch_y = widget->y + row * (palette->swatch_size + palette->gap);

        int32_t sx = (int32_t)swatch_x;
        int32_t sy = (int32_t)swatch_y;
        int32_t ss = (int32_t)palette->swatch_size;
        if (ss <= 0)
            continue;

        vgfx_fill_rect(win, sx, sy, ss, ss, palette->colors[i]);

        if (i == palette->selected_index) {
            vgfx_rect(win, sx - 1, sy - 1, ss + 2, ss + 2, palette->selected_border);
            vgfx_rect(win, sx, sy, ss, ss, palette->selected_border);
        } else {
            vgfx_rect(win, sx, sy, ss, ss, palette->border_color);
        }
    }

    if (bw > 0 && bh > 0)
        vgfx_rect(win, bx, by, bw, bh, palette->border_color);
}

/// @brief Return the index of the swatch under absolute coordinates (x, y), or -1 if none.
static int colorpalette_hit_test_swatch(vg_colorpalette_t *palette, float x, float y) {
    if (palette->color_count == 0 || !palette->colors) {
        return -1;
    }

    // Convert to local coordinates
    float local_x = x - palette->base.x;
    float local_y = y - palette->base.y;

    if (local_x < 0 || local_y < 0) {
        return -1;
    }

    // Calculate which cell
    float cell_size = palette->swatch_size + palette->gap;
    int col = (int)(local_x / cell_size);
    int row = (int)(local_y / cell_size);

    // Check if within bounds
    if (col >= palette->columns) {
        return -1;
    }

    // Check if within the actual swatch (not in the gap)
    float cell_x = local_x - col * cell_size;
    float cell_y = local_y - row * cell_size;
    if (cell_x > palette->swatch_size || cell_y > palette->swatch_size) {
        return -1; // In the gap
    }

    int index = row * palette->columns + col;
    if (index >= palette->color_count) {
        return -1;
    }

    return index;
}

/// @brief VTable handle_event: hit-tests mouse-down and click events against the swatch grid,
/// updating selected_index and firing on_select on a confirmed click.
static bool colorpalette_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_colorpalette_t *palette = (vg_colorpalette_t *)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    if (event->type == VG_EVENT_MOUSE_DOWN) {
        int index = colorpalette_hit_test_swatch(palette, event->mouse.x, event->mouse.y);
        if (index >= 0) {
            event->handled = true;
            return true;
        }
    }

    if (event->type == VG_EVENT_CLICK) {
        // Hit test to find which swatch was clicked
        int index = colorpalette_hit_test_swatch(palette, event->mouse.x, event->mouse.y);
        if (index >= 0) {
            palette->selected_index = index;
            widget->needs_paint = true;

            // Call selection callback
            if (palette->on_select) {
                palette->on_select(widget, palette->colors[index], index, palette->on_select_data);
            }
            // Also call widget's generic on_click if set
            if (widget->on_click) {
                widget->on_click(widget, widget->callback_data);
            }
            return true;
        }
    }

    return false;
}

/// @brief VTable can_focus: returns true when the widget is both enabled and visible.
static bool colorpalette_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

//=============================================================================
// ColorPalette API
//=============================================================================

/// @brief Replace the palette's colour list with a copy of the supplied array.
///
/// @details The previous colour array is freed.  The selection index is reset
///          to -1.  Passing NULL or count ≤ 0 clears the palette.
///
/// @param palette The color palette to update.
/// @param colors  Array of count AARRGGBB colour values; may be NULL to clear.
/// @param count   Number of colours in the array.
void vg_colorpalette_set_colors(vg_colorpalette_t *palette, const uint32_t *colors, int count) {
    if (!palette)
        return;

    // Free existing colors
    if (palette->colors) {
        free(palette->colors);
        palette->colors = NULL;
    }

    palette->color_count = 0;
    palette->selected_index = -1;

    if (colors && count > 0) {
        palette->colors = malloc(count * sizeof(uint32_t));
        if (palette->colors) {
            memcpy(palette->colors, colors, count * sizeof(uint32_t));
            palette->color_count = count;
        }
    }

    palette->base.needs_layout = true;
    palette->base.needs_paint = true;
}

/// @brief Append a single colour to the end of the palette.
///
/// @param palette The color palette to update.
/// @param color   AARRGGBB colour value to append.
void vg_colorpalette_add_color(vg_colorpalette_t *palette, uint32_t color) {
    if (!palette)
        return;

    int new_count = palette->color_count + 1;
    uint32_t *new_colors = realloc(palette->colors, new_count * sizeof(uint32_t));
    if (!new_colors)
        return;

    palette->colors = new_colors;
    palette->colors[palette->color_count] = color;
    palette->color_count = new_count;

    palette->base.needs_layout = true;
    palette->base.needs_paint = true;
}

/// @brief Remove all colours from the palette and reset the selection.
///
/// @param palette The color palette to clear.
void vg_colorpalette_clear(vg_colorpalette_t *palette) {
    if (!palette)
        return;

    if (palette->colors) {
        free(palette->colors);
        palette->colors = NULL;
    }
    palette->color_count = 0;
    palette->selected_index = -1;

    palette->base.needs_layout = true;
    palette->base.needs_paint = true;
}

/// @brief Set the number of columns in the swatch grid.
///
/// @param palette The color palette to configure.
/// @param columns Number of columns; values ≤ 0 are ignored.
void vg_colorpalette_set_columns(vg_colorpalette_t *palette, int columns) {
    if (!palette || columns <= 0)
        return;

    palette->columns = columns;
    palette->base.needs_layout = true;
    palette->base.needs_paint = true;
}

/// @brief Programmatically select a swatch by zero-based index.
///
/// @param palette The color palette to update.
/// @param index   Zero-based index of the swatch to select; out-of-range or
///                negative values clear the selection (set to -1).
void vg_colorpalette_set_selected(vg_colorpalette_t *palette, int index) {
    if (!palette)
        return;

    if (index < -1 || index >= palette->color_count) {
        index = -1;
    }

    palette->selected_index = index;
    palette->base.needs_paint = true;
}

/// @brief Return the index of the currently selected swatch.
///
/// @param palette The color palette to query.
/// @return Zero-based selected index, or -1 if none selected or palette is NULL.
int vg_colorpalette_get_selected(vg_colorpalette_t *palette) {
    if (!palette)
        return -1;
    return palette->selected_index;
}

/// @brief Return the AARRGGBB colour of the currently selected swatch.
///
/// @param palette The color palette to query.
/// @return Selected colour, or 0 if no swatch is selected or palette is NULL.
uint32_t vg_colorpalette_get_selected_color(vg_colorpalette_t *palette) {
    if (!palette || palette->selected_index < 0 ||
        palette->selected_index >= palette->color_count) {
        return 0;
    }
    return palette->colors[palette->selected_index];
}

/// @brief Register a callback invoked when a swatch is clicked.
///
/// @param palette   The color palette to configure.
/// @param callback  Function called with (widget, color, index, user_data) on
///                  each swatch click.  May be NULL to unregister.
/// @param user_data Opaque pointer forwarded unchanged to the callback.
void vg_colorpalette_set_on_select(vg_colorpalette_t *palette,
                                   vg_colorpalette_callback_t callback,
                                   void *user_data) {
    if (!palette)
        return;

    palette->on_select = callback;
    palette->on_select_data = user_data;
}

/// @brief Set the uniform pixel size (width and height) of each swatch.
///
/// @param palette The color palette to configure.
/// @param size    Desired swatch size in logical pixels; values ≤ 0 are ignored.
void vg_colorpalette_set_swatch_size(vg_colorpalette_t *palette, float size) {
    if (!palette || size <= 0)
        return;

    palette->swatch_size = size;
    palette->base.needs_layout = true;
    palette->base.needs_paint = true;
}

/// @brief Populate the palette with the classic 16 Windows/DOS colours in 2 rows of 8.
///
/// @param palette The color palette to populate.
void vg_colorpalette_load_standard_16(vg_colorpalette_t *palette) {
    if (!palette)
        return;

    vg_colorpalette_set_colors(palette, g_standard_16_colors, 16);
    palette->columns = 8; // 2 rows of 8
}
