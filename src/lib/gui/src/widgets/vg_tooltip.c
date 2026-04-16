//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_tooltip.c
//
//===----------------------------------------------------------------------===//
// vg_tooltip.c - Tooltip widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void tooltip_destroy(vg_widget_t *widget);
static void tooltip_measure(vg_widget_t *widget, float available_width, float available_height);
static void tooltip_paint(vg_widget_t *widget, void *canvas);

static bool tooltip_widget_is_descendant_of(const vg_widget_t *widget, const vg_widget_t *ancestor) {
    for (const vg_widget_t *current = widget; current; current = current->parent) {
        if (current == ancestor)
            return true;
    }
    return false;
}

static char *tooltip_dup_range(const char *text, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

static int tooltip_wrap_text(vg_tooltip_t *tooltip, char ***out_lines, float *out_max_width) {
    if (out_lines)
        *out_lines = NULL;
    if (out_max_width)
        *out_max_width = 0.0f;
    if (!tooltip || !tooltip->text || !tooltip->font)
        return 0;

    float max_text_width = 0.0f;
    if (tooltip->max_width > tooltip->padding * 2) {
        max_text_width = (float)tooltip->max_width - (float)tooltip->padding * 2.0f;
    }

    int cap = 4;
    int count = 0;
    char **lines = out_lines ? (char **)calloc((size_t)cap, sizeof(char *)) : NULL;
    const char *text = tooltip->text;
    size_t text_len = strlen(text);
    size_t start = 0;

    while (start <= text_len) {
        if (text[start] == '\0') {
            if (count == 0 && out_lines && lines) {
                lines[count++] = strdup("");
            }
            break;
        }

        size_t line_end = start;
        size_t best_end = start;
        size_t best_next = start;
        float best_width = 0.0f;
        bool found_break = false;

        while (line_end < text_len && text[line_end] != '\n') {
            size_t candidate_end = line_end + 1;
            char *candidate = tooltip_dup_range(text + start, candidate_end - start);
            if (!candidate)
                break;

            vg_text_metrics_t metrics = {0};
            vg_font_measure_text(tooltip->font, tooltip->font_size, candidate, &metrics);
            free(candidate);

            if (max_text_width > 0.0f && metrics.width > max_text_width) {
                break;
            }

            best_end = candidate_end;
            best_next = candidate_end;
            best_width = metrics.width;
            found_break = true;
            if (text[line_end] == ' ' || text[line_end] == '\t') {
                best_end = line_end;
                best_next = candidate_end;
            }
            line_end = candidate_end;
        }

        if (!found_break) {
            best_end = start + 1;
            best_next = best_end;
            char *candidate = tooltip_dup_range(text + start, best_end - start);
            if (candidate) {
                vg_text_metrics_t metrics = {0};
                vg_font_measure_text(tooltip->font, tooltip->font_size, candidate, &metrics);
                best_width = metrics.width;
                free(candidate);
            }
        }

        while (best_end > start && (text[best_end - 1] == ' ' || text[best_end - 1] == '\t')) {
            best_end--;
        }

        if (out_lines && lines) {
            if (count >= cap) {
                int new_cap = cap * 2;
                char **new_lines = (char **)realloc(lines, (size_t)new_cap * sizeof(char *));
                if (!new_lines)
                    break;
                memset(new_lines + cap, 0, (size_t)(new_cap - cap) * sizeof(char *));
                lines = new_lines;
                cap = new_cap;
            }
            lines[count] = tooltip_dup_range(text + start, best_end - start);
            if (!lines[count])
                break;
            count++;
        } else {
            count++;
        }

        if (best_width > 0.0f && out_max_width && best_width > *out_max_width)
            *out_max_width = best_width;

        size_t prev_start = start;
        start = best_next;
        while (text[start] == ' ' || text[start] == '\t')
            start++;
        // Progress guard: if no advance happened, force one. Without this the
        // loop can spin forever on input where best_next == prev_start (e.g. a
        // whitespace-only suffix that wraps into a zero-length segment).
        if (start <= prev_start) {
            if (text[start] == '\0')
                break;
            start = prev_start + 1;
        }
        if (text[start] == '\n') {
            start++;
            if (out_lines && lines) {
                if (count >= cap) {
                    int new_cap = cap * 2;
                    char **new_lines = (char **)realloc(lines, (size_t)new_cap * sizeof(char *));
                    if (!new_lines)
                        break;
                    memset(new_lines + cap, 0, (size_t)(new_cap - cap) * sizeof(char *));
                    lines = new_lines;
                    cap = new_cap;
                }
                lines[count] = strdup("");
                if (!lines[count])
                    break;
                count++;
            } else {
                count++;
            }
        }
    }

    if (out_lines) {
        *out_lines = lines;
    } else if (lines) {
        for (int i = 0; i < count; i++)
            free(lines[i]);
        free(lines);
    }
    return count > 0 ? count : 1;
}

//=============================================================================
// Tooltip VTable
//=============================================================================

static vg_widget_vtable_t g_tooltip_vtable = {.destroy = tooltip_destroy,
                                              .measure = tooltip_measure,
                                              .arrange = NULL,
                                              .paint = tooltip_paint,
                                              .handle_event = NULL,
                                              .can_focus = NULL,
                                              .on_focus = NULL};

//=============================================================================
// Global Tooltip Manager
//=============================================================================

static vg_tooltip_manager_t g_tooltip_manager = {0};

static uint64_t tooltip_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
#endif
}

vg_tooltip_manager_t *vg_tooltip_manager_get(void) {
    return &g_tooltip_manager;
}

//=============================================================================
// Tooltip Implementation
//=============================================================================

vg_tooltip_t *vg_tooltip_create(void) {
    vg_tooltip_t *tooltip = calloc(1, sizeof(vg_tooltip_t));
    if (!tooltip)
        return NULL;

    vg_widget_init(&tooltip->base, VG_WIDGET_CUSTOM, &g_tooltip_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    tooltip->show_delay_ms = 500;
    tooltip->hide_delay_ms = 100;
    tooltip->duration_ms = 0; // Stay until leave

    tooltip->position_mode = VG_TOOLTIP_FOLLOW_CURSOR;
    tooltip->offset_x = 10;
    tooltip->offset_y = 20;

    tooltip->max_width = 300;
    tooltip->padding = 6;
    tooltip->corner_radius = 4;
    tooltip->bg_color = 0xF0333333; // Dark semi-transparent
    tooltip->text_color = 0xFFFFFFFF;
    tooltip->border_color = 0xFF555555;

    tooltip->font_size = theme->typography.size_small;
    tooltip->is_visible = false;

    return tooltip;
}

static void tooltip_destroy(vg_widget_t *widget) {
    vg_tooltip_t *tooltip = (vg_tooltip_t *)widget;
    free(tooltip->text);
}

/// @brief Tooltip destroy.
void vg_tooltip_destroy(vg_tooltip_t *tooltip) {
    if (!tooltip)
        return;
    vg_widget_destroy(&tooltip->base);
}

static void tooltip_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_tooltip_t *tooltip = (vg_tooltip_t *)widget;
    (void)available_width;
    (void)available_height;

    if (!tooltip->text || !tooltip->font) {
        widget->measured_width = 0;
        widget->measured_height = 0;
        return;
    }

    float max_line_width = 0.0f;
    int line_count = tooltip_wrap_text(tooltip, NULL, &max_line_width);
    vg_font_metrics_t font_metrics = {0};
    vg_font_get_metrics(tooltip->font, tooltip->font_size, &font_metrics);
    float line_height =
        font_metrics.line_height > 0 ? (float)font_metrics.line_height : tooltip->font_size;

    widget->measured_width = max_line_width + (float)tooltip->padding * 2.0f;
    if (tooltip->max_width > 0 && widget->measured_width > (float)tooltip->max_width) {
        widget->measured_width = (float)tooltip->max_width;
    }
    widget->measured_height = line_height * (float)line_count + (float)tooltip->padding * 2.0f;
}

static void tooltip_paint(vg_widget_t *widget, void *canvas) {
    vg_tooltip_t *tooltip = (vg_tooltip_t *)widget;

    if (!tooltip->is_visible || !tooltip->text)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->measured_width,
                   (int32_t)widget->measured_height,
                   tooltip->bg_color);

    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->measured_width,
                   1,
                   tooltip->border_color);
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)(widget->y + widget->measured_height - 1.0f),
                   (int32_t)widget->measured_width,
                   1,
                   tooltip->border_color);
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   1,
                   (int32_t)widget->measured_height,
                   tooltip->border_color);
    vgfx_fill_rect(win,
                   (int32_t)(widget->x + widget->measured_width - 1.0f),
                   (int32_t)widget->y,
                   1,
                   (int32_t)widget->measured_height,
                   tooltip->border_color);

    if (!tooltip->font || !tooltip->text[0])
        return;

    char **lines = NULL;
    float ignored_width = 0.0f;
    int line_count = tooltip_wrap_text(tooltip, &lines, &ignored_width);
    vg_font_metrics_t font_metrics = {0};
    vg_font_get_metrics(tooltip->font, tooltip->font_size, &font_metrics);
    float line_height =
        font_metrics.line_height > 0 ? (float)font_metrics.line_height : tooltip->font_size;
    float text_x = widget->x + (float)tooltip->padding;
    float text_y = widget->y + (float)tooltip->padding + (float)font_metrics.ascent;

    for (int i = 0; i < line_count; i++) {
        const char *line = (lines && lines[i]) ? lines[i] : "";
        vg_font_draw_text(canvas,
                          tooltip->font,
                          tooltip->font_size,
                          text_x,
                          text_y + (float)i * line_height,
                          line,
                          tooltip->text_color);
    }

    if (lines) {
        for (int i = 0; i < line_count; i++)
            free(lines[i]);
        free(lines);
    }
}

/// @brief Tooltip set text.
void vg_tooltip_set_text(vg_tooltip_t *tooltip, const char *text) {
    if (!tooltip)
        return;

    free(tooltip->text);
    tooltip->text = text ? strdup(text) : NULL;
    tooltip->base.needs_layout = true;
}

/// @brief Tooltip show at.
void vg_tooltip_show_at(vg_tooltip_t *tooltip, int x, int y) {
    if (!tooltip)
        return;

    tooltip->base.x = (float)x + tooltip->offset_x;
    tooltip->base.y = (float)y + tooltip->offset_y;
    tooltip->is_visible = true;
    tooltip->base.visible = true;
    tooltip->base.needs_paint = true;
}

/// @brief Tooltip hide.
void vg_tooltip_hide(vg_tooltip_t *tooltip) {
    if (!tooltip)
        return;

    tooltip->is_visible = false;
    tooltip->base.visible = false;
}

/// @brief Tooltip set anchor.
void vg_tooltip_set_anchor(vg_tooltip_t *tooltip, vg_widget_t *anchor) {
    if (!tooltip)
        return;

    tooltip->anchor_widget = anchor;
    tooltip->position_mode = VG_TOOLTIP_ANCHOR_WIDGET;
}

/// @brief Tooltip set timing.
void vg_tooltip_set_timing(vg_tooltip_t *tooltip,
                           uint32_t show_delay_ms,
                           uint32_t hide_delay_ms,
                           uint32_t duration_ms) {
    if (!tooltip)
        return;

    tooltip->show_delay_ms = show_delay_ms;
    tooltip->hide_delay_ms = hide_delay_ms;
    tooltip->duration_ms = duration_ms;
}

//=============================================================================
// Tooltip Manager Implementation
//=============================================================================

void vg_tooltip_manager_update(vg_tooltip_manager_t *mgr, uint64_t now_ms) {
    if (!mgr)
        return;

    // Check if pending show should trigger
    if (mgr->pending_show && mgr->hovered_widget) {
        if (mgr->active_tooltip &&
            now_ms >= mgr->hover_start_time + mgr->active_tooltip->show_delay_ms) {
            vg_tooltip_show_at(mgr->active_tooltip, mgr->cursor_x, mgr->cursor_y);
            mgr->pending_show = false;
        }
    }

    // Check auto-hide
    if (mgr->active_tooltip && mgr->active_tooltip->is_visible &&
        mgr->active_tooltip->duration_ms > 0) {
        // Auto-hide logic would go here
        (void)now_ms;
    }
}

/// @brief Tooltip manager on hover.
void vg_tooltip_manager_on_hover(vg_tooltip_manager_t *mgr, vg_widget_t *widget, int x, int y) {
    if (!mgr)
        return;

    mgr->cursor_x = x;
    mgr->cursor_y = y;

    if (widget != mgr->hovered_widget) {
        // Widget changed
        if (mgr->active_tooltip) {
            vg_tooltip_hide(mgr->active_tooltip);
        }

        mgr->hovered_widget = widget;
        if (widget && widget->tooltip_text && widget->tooltip_text[0]) {
            if (!mgr->active_tooltip) {
                mgr->active_tooltip = vg_tooltip_create();
            }
            if (mgr->active_tooltip) {
                vg_tooltip_set_text(mgr->active_tooltip, widget->tooltip_text);
                mgr->pending_show = true;
            } else {
                mgr->pending_show = false;
            }
        } else {
            mgr->pending_show = false;
        }
        mgr->hover_start_time = tooltip_now_ms();
        return;
    }

    if (!widget || !widget->tooltip_text || !widget->tooltip_text[0]) {
        if (mgr->active_tooltip) {
            vg_tooltip_hide(mgr->active_tooltip);
        }
        mgr->pending_show = false;
        return;
    }

    if (!mgr->active_tooltip) {
        mgr->active_tooltip = vg_tooltip_create();
    }
    if (!mgr->active_tooltip) {
        mgr->pending_show = false;
        return;
    }

    bool text_changed =
        !mgr->active_tooltip->text || strcmp(mgr->active_tooltip->text, widget->tooltip_text) != 0;
    if (text_changed) {
        vg_tooltip_set_text(mgr->active_tooltip, widget->tooltip_text);
        if (mgr->active_tooltip->is_visible) {
            vg_tooltip_show_at(mgr->active_tooltip, mgr->cursor_x, mgr->cursor_y);
            mgr->pending_show = false;
        } else {
            mgr->hover_start_time = tooltip_now_ms();
            mgr->pending_show = true;
        }
    } else if (mgr->active_tooltip->is_visible &&
               mgr->active_tooltip->position_mode == VG_TOOLTIP_FOLLOW_CURSOR) {
        vg_tooltip_show_at(mgr->active_tooltip, mgr->cursor_x, mgr->cursor_y);
    }
}

/// @brief Tooltip manager on leave.
void vg_tooltip_manager_on_leave(vg_tooltip_manager_t *mgr) {
    if (!mgr)
        return;

    if (mgr->active_tooltip) {
        vg_tooltip_hide(mgr->active_tooltip);
    }

    mgr->hovered_widget = NULL;
    mgr->pending_show = false;
}

/// @brief Tooltip manager widget destroyed.
///
/// Operates on the global manager and any active tooltip's anchor pointer so
/// destroying a hovered or anchored widget does not leave a dangling pointer.
void vg_tooltip_manager_widget_destroyed(vg_widget_t *widget) {
    if (!widget)
        return;

    vg_tooltip_manager_t *mgr = &g_tooltip_manager;
    if (mgr->hovered_widget == widget) {
        if (mgr->active_tooltip) {
            vg_tooltip_hide(mgr->active_tooltip);
        }
        mgr->hovered_widget = NULL;
        mgr->pending_show = false;
    }
    if (mgr->active_tooltip && mgr->active_tooltip->anchor_widget == widget) {
        mgr->active_tooltip->anchor_widget = NULL;
        vg_tooltip_hide(mgr->active_tooltip);
    }
}

void vg_tooltip_manager_widget_hidden(vg_widget_t *widget) {
    if (!widget)
        return;

    vg_tooltip_manager_t *mgr = &g_tooltip_manager;
    if (mgr->hovered_widget && tooltip_widget_is_descendant_of(mgr->hovered_widget, widget)) {
        if (mgr->active_tooltip) {
            vg_tooltip_hide(mgr->active_tooltip);
        }
        mgr->hovered_widget = NULL;
        mgr->pending_show = false;
    }
    if (mgr->active_tooltip && mgr->active_tooltip->anchor_widget &&
        tooltip_widget_is_descendant_of(mgr->active_tooltip->anchor_widget, widget)) {
        mgr->active_tooltip->anchor_widget = NULL;
        vg_tooltip_hide(mgr->active_tooltip);
    }
}

/// @brief Widget set tooltip text.
void vg_widget_set_tooltip_text(vg_widget_t *widget, const char *text) {
    if (!widget)
        return;
    free(widget->tooltip_text);
    widget->tooltip_text = (text && text[0]) ? strdup(text) : NULL;
}
