//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_popuplist.c
// Purpose: Caret-anchored, filterable, keyboard-navigable popup list (exposed as
//          Zanna.GUI.PopupList). Renders in the overlay pass at an absolute anchor;
//          the host drives keyboard and visibility and supplies pre-ranked items.
// Key invariants:
//   - filtered[] holds the indices of items matching the (case-insensitive) filter;
//     selected is an index into filtered[] in [0, filtered_count).
//   - Each item string is heap-owned; freed on clear/destroy.
//   - The widget contributes no normal-flow size (measured 0); it paints only in the
//     overlay pass at (anchor_x, anchor_y) when visible.
// Links: lib/gui/include/vg_ide_widgets_ui.h, lib/gui/include/vg_font.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_font.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward declarations
//=============================================================================

static void popuplist_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void popuplist_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void popuplist_paint_overlay(vg_widget_t *widget, void *canvas);
static void popuplist_get_visual_bounds(
    vg_widget_t *widget, float *x, float *y, float *width, float *height);
static void popuplist_destroy(vg_widget_t *widget);

static vg_widget_vtable_t g_popuplist_vtable = {
    .destroy = popuplist_destroy,
    .measure = popuplist_measure,
    .arrange = popuplist_arrange,
    .paint = NULL, // the popup only draws in the overlay pass
    .paint_overlay = popuplist_paint_overlay,
    .handle_event = NULL,
    .can_focus = NULL,
    .on_focus = NULL,
    .get_visual_bounds = popuplist_get_visual_bounds,
};

//=============================================================================
// Internal helpers
//=============================================================================

static char *popuplist_dup(const char *text) {
    if (!text)
        return NULL;
    size_t n = strlen(text);
    char *copy = (char *)malloc(n + 1);
    if (copy)
        memcpy(copy, text, n + 1);
    return copy;
}

/// @brief Case-insensitive substring test; an empty/NULL needle always matches.
static bool popuplist_contains_ci(const char *hay, const char *needle) {
    if (!needle || !needle[0])
        return true;
    if (!hay)
        return false;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nlen)
            return true;
    }
    return false;
}

/// @brief Rebuild filtered[] from items + filter and clamp the selection.
static void popuplist_recompute(vg_popuplist_t *list) {
    list->filtered_count = 0;
    if (list->filtered) {
        for (int i = 0; i < list->item_count; i++) {
            if (popuplist_contains_ci(list->items[i], list->filter))
                list->filtered[list->filtered_count++] = i;
        }
    }
    if (list->filtered_count == 0)
        list->selected = 0;
    else if (list->selected >= list->filtered_count)
        list->selected = list->filtered_count - 1;
    else if (list->selected < 0)
        list->selected = 0;
}

/// @brief Grow items[] and filtered[] to hold at least @p min_cap entries.
static bool popuplist_ensure_capacity(vg_popuplist_t *list, int min_cap) {
    if (min_cap <= list->item_capacity)
        return true;
    int new_cap = list->item_capacity > 0 ? list->item_capacity * 2 : 16;
    if (new_cap < min_cap)
        new_cap = min_cap;
    char **ni = (char **)realloc(list->items, (size_t)new_cap * sizeof(char *));
    if (!ni)
        return false;
    list->items = ni;
    int *nf = (int *)realloc(list->filtered, (size_t)new_cap * sizeof(int));
    if (!nf)
        return false;
    list->filtered = nf;
    list->item_capacity = new_cap;
    return true;
}

//=============================================================================
// VTable implementations
//=============================================================================

static void popuplist_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    (void)avail_w;
    (void)avail_h;
    // Floats at an absolute anchor; contribute no normal-flow size.
    widget->measured_width = 0.0f;
    widget->measured_height = 0.0f;
    vg_widget_apply_constraints(widget);
}

static void popuplist_arrange(vg_widget_t *widget, float x, float y, float w, float h) {
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

/// @brief Report the absolute rectangle occupied by the visible suggestion list.
/// @details Uses the same row limit and line-height fallback as overlay painting.
///          The list deliberately has zero normal-flow size, so this callback is
///          the authoritative retained-damage extent for its popup pixels.
/// @param widget Popup-list widget being queried.
/// @param x Receives popup anchor X, or zero when nothing can be painted.
/// @param y Receives popup anchor Y, or zero when nothing can be painted.
/// @param width Receives popup width, or zero when nothing can be painted.
/// @param height Receives visible rows times line height, or zero when empty.
static void popuplist_get_visual_bounds(
    vg_widget_t *widget, float *x, float *y, float *width, float *height) {
    if (x)
        *x = 0.0f;
    if (y)
        *y = 0.0f;
    if (width)
        *width = 0.0f;
    if (height)
        *height = 0.0f;
    vg_popuplist_t *list = (vg_popuplist_t *)widget;
    if (!list || !widget->visible || list->filtered_count <= 0 || !list->font)
        return;
    int rows = list->filtered_count;
    if (list->max_rows > 0 && rows > list->max_rows)
        rows = list->max_rows;
    float line_height = list->line_height > 0.0f ? list->line_height : 18.0f;
    if (x)
        *x = list->anchor_x;
    if (y)
        *y = list->anchor_y;
    if (width)
        *width = list->width;
    if (height)
        *height = (float)rows * line_height;
}

static void popuplist_paint_overlay(vg_widget_t *widget, void *canvas) {
    vg_popuplist_t *list = (vg_popuplist_t *)widget;
    if (!widget->visible || list->filtered_count == 0 || !list->font)
        return;
    vgfx_window_t win = (vgfx_window_t)canvas;

    float lh = list->line_height > 0.0f ? list->line_height : 18.0f;
    int rows = list->filtered_count;
    if (list->max_rows > 0 && rows > list->max_rows)
        rows = list->max_rows;

    int32_t x = (int32_t)list->anchor_x;
    int32_t y = (int32_t)list->anchor_y;
    int32_t w = (int32_t)list->width;
    int32_t h = (int32_t)((float)rows * lh);

    vgfx_fill_rect(win, x, y, w, h, list->bg_color);
    vgfx_rect(win, x, y, w, h, list->border_color);

    // Scroll so the selected row stays visible.
    int first = 0;
    if (list->selected >= rows)
        first = list->selected - rows + 1;

    vg_font_metrics_t fm = {0};
    vg_font_get_metrics(list->font, list->font_size, &fm);
    float baseline = (lh - (float)(fm.ascent - fm.descent)) * 0.5f + (float)fm.ascent;

    for (int r = 0; r < rows; r++) {
        int fi = first + r;
        if (fi >= list->filtered_count)
            break;
        int32_t ry = y + (int32_t)((float)r * lh);
        bool sel = (fi == list->selected);
        if (sel)
            vgfx_fill_rect(win, x, ry, w, (int32_t)lh, list->sel_bg_color);
        const char *text = list->items[list->filtered[fi]];
        if (text)
            vg_font_draw_text(canvas,
                              list->font,
                              list->font_size,
                              (float)x + 6.0f,
                              (float)ry + baseline,
                              text,
                              sel ? list->sel_fg_color : list->fg_color);
    }
}

static void popuplist_destroy(vg_widget_t *widget) {
    vg_popuplist_t *list = (vg_popuplist_t *)widget;
    for (int i = 0; i < list->item_count; i++)
        free(list->items[i]);
    free(list->items);
    free(list->filtered);
    free(list->filter);
}

//=============================================================================
// Public API
//=============================================================================

vg_popuplist_t *vg_popuplist_create(vg_widget_t *root) {
    vg_popuplist_t *list = (vg_popuplist_t *)calloc(1, sizeof(vg_popuplist_t));
    if (!list)
        return NULL;

    vg_widget_init(&list->base, VG_WIDGET_POPUPLIST, &g_popuplist_vtable);
    list->base.visible = false;
    list->width = 240.0f;
    list->max_rows = 10;

    vg_theme_t *theme = vg_theme_get_current();
    list->font = theme->typography.font_regular;
    list->font_size = theme->typography.size_normal;
    if (list->font && list->font_size > 0.0f) {
        vg_font_metrics_t m = {0};
        vg_font_get_metrics(list->font, list->font_size, &m);
        list->line_height = m.line_height > 0 ? (float)m.line_height : list->font_size * 1.4f;
    } else {
        list->line_height = 18.0f;
    }
    list->bg_color = theme->colors.bg_secondary;
    list->fg_color = theme->colors.fg_primary;
    list->sel_bg_color = theme->colors.accent_primary;
    list->sel_fg_color = theme->colors.bg_primary;
    list->border_color = theme->colors.border_primary;

    if (root)
        vg_widget_add_child(root, &list->base);

    return list;
}

void vg_popuplist_destroy(vg_popuplist_t *list) {
    if (!list)
        return;
    vg_widget_destroy(&list->base);
}

void vg_popuplist_add_item(vg_popuplist_t *list, const char *text) {
    if (!list || !text)
        return;
    if (!popuplist_ensure_capacity(list, list->item_count + 1))
        return;
    char *copy = popuplist_dup(text);
    if (!copy)
        return;
    list->items[list->item_count++] = copy;
    popuplist_recompute(list);
    list->base.needs_paint = true;
}

void vg_popuplist_clear(vg_popuplist_t *list) {
    if (!list)
        return;
    for (int i = 0; i < list->item_count; i++)
        free(list->items[i]);
    list->item_count = 0;
    list->filtered_count = 0;
    list->selected = 0;
    list->base.needs_paint = true;
}

void vg_popuplist_set_filter(vg_popuplist_t *list, const char *filter) {
    if (!list)
        return;
    free(list->filter);
    list->filter = popuplist_dup(filter);
    list->selected = 0;
    popuplist_recompute(list);
    list->base.needs_paint = true;
}

int vg_popuplist_visible_count(const vg_popuplist_t *list) {
    return list ? list->filtered_count : 0;
}

void vg_popuplist_navigate_up(vg_popuplist_t *list) {
    if (!list || list->filtered_count == 0)
        return;
    if (list->selected > 0)
        list->selected--;
    list->base.needs_paint = true;
}

void vg_popuplist_navigate_down(vg_popuplist_t *list) {
    if (!list || list->filtered_count == 0)
        return;
    if (list->selected < list->filtered_count - 1)
        list->selected++;
    list->base.needs_paint = true;
}

void vg_popuplist_set_selected_index(vg_popuplist_t *list, int index) {
    if (!list || list->filtered_count == 0)
        return;
    if (index < 0)
        index = 0;
    if (index >= list->filtered_count)
        index = list->filtered_count - 1;
    list->selected = index;
    list->base.needs_paint = true;
}

int vg_popuplist_selected_index(const vg_popuplist_t *list) {
    if (!list || list->filtered_count == 0)
        return -1;
    return list->selected;
}

const char *vg_popuplist_selected_text(const vg_popuplist_t *list) {
    if (!list || list->filtered_count == 0 || list->selected < 0 ||
        list->selected >= list->filtered_count)
        return NULL;
    return list->items[list->filtered[list->selected]];
}

void vg_popuplist_accept_selected(vg_popuplist_t *list) {
    if (list && list->filtered_count > 0)
        list->accepted = true;
}

bool vg_popuplist_was_accepted(vg_popuplist_t *list) {
    if (!list)
        return false;
    bool accepted = list->accepted;
    list->accepted = false;
    return accepted;
}

void vg_popuplist_anchor_at(vg_popuplist_t *list, float x, float y) {
    if (!list)
        return;
    list->anchor_x = x;
    list->anchor_y = y;
    list->base.needs_paint = true;
}

void vg_popuplist_set_width(vg_popuplist_t *list, float width) {
    if (list && width > 0.0f) {
        list->width = width;
        list->base.needs_paint = true;
    }
}

void vg_popuplist_set_max_rows(vg_popuplist_t *list, int max_rows) {
    if (list && max_rows > 0) {
        list->max_rows = max_rows;
        list->base.needs_paint = true;
    }
}

void vg_popuplist_set_font(vg_popuplist_t *list, vg_font_t *font, float size) {
    if (!list)
        return;
    list->font = font;
    list->font_size = size > 0.0f ? size : list->font_size;
    if (font && list->font_size > 0.0f) {
        vg_font_metrics_t m = {0};
        vg_font_get_metrics(font, list->font_size, &m);
        if (m.line_height > 0)
            list->line_height = (float)m.line_height;
    }
    list->base.needs_paint = true;
}

void vg_popuplist_set_visible(vg_popuplist_t *list, bool visible) {
    if (!list)
        return;
    list->base.visible = visible;
    list->base.needs_paint = true;
}

bool vg_popuplist_is_visible(const vg_popuplist_t *list) {
    return list ? list->base.visible : false;
}
