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
static uint32_t tooltip_apply_alpha(uint32_t color, uint32_t backdrop);
static void tooltip_fill_round_rect(vgfx_window_t win,
                                    int32_t x,
                                    int32_t y,
                                    int32_t w,
                                    int32_t h,
                                    int32_t radius,
                                    uint32_t color);
static void tooltip_stroke_round_rect(vgfx_window_t win,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h,
                                      int32_t radius,
                                      uint32_t color);

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

static uint32_t tooltip_apply_alpha(uint32_t color, uint32_t backdrop) {
    uint8_t alpha = (uint8_t)(color >> 24);
    uint32_t rgb = color & 0x00FFFFFFu;
    if (alpha == 0 || alpha == 0xFF)
        return rgb;
    return vg_color_blend(backdrop, rgb, (float)alpha / 255.0f);
}

static void tooltip_fill_round_rect(vgfx_window_t win,
                                    int32_t x,
                                    int32_t y,
                                    int32_t w,
                                    int32_t h,
                                    int32_t radius,
                                    uint32_t color) {
    if (w <= 0 || h <= 0)
        return;
    int32_t max_radius = w < h ? w / 2 : h / 2;
    if (radius <= 0 || max_radius <= 0 || w <= radius * 2 || h <= radius * 2) {
        vgfx_fill_rect(win, x, y, w, h, color);
        return;
    }
    if (radius > max_radius)
        radius = max_radius;
    vgfx_fill_rect(win, x + radius, y, w - radius * 2, h, color);
    vgfx_fill_rect(win, x, y + radius, radius, h - radius * 2, color);
    vgfx_fill_rect(win, x + w - radius, y + radius, radius, h - radius * 2, color);
    vgfx_fill_circle(win, x + radius, y + radius, radius, color);
    vgfx_fill_circle(win, x + w - radius - 1, y + radius, radius, color);
    vgfx_fill_circle(win, x + radius, y + h - radius - 1, radius, color);
    vgfx_fill_circle(win, x + w - radius - 1, y + h - radius - 1, radius, color);
}

static void tooltip_stroke_round_rect(vgfx_window_t win,
                                      int32_t x,
                                      int32_t y,
                                      int32_t w,
                                      int32_t h,
                                      int32_t radius,
                                      uint32_t color) {
    if (w <= 1 || h <= 1)
        return;
    int32_t max_radius = w < h ? w / 2 : h / 2;
    if (radius <= 0 || max_radius <= 0 || w <= radius * 2 || h <= radius * 2) {
        vgfx_rect(win, x, y, w, h, color);
        return;
    }
    if (radius > max_radius)
        radius = max_radius;
    vgfx_line(win, x + radius, y, x + w - radius - 1, y, color);
    vgfx_line(win, x + radius, y + h - 1, x + w - radius - 1, y + h - 1, color);
    vgfx_line(win, x, y + radius, x, y + h - radius - 1, color);
    vgfx_line(win, x + w - 1, y + radius, x + w - 1, y + h - radius - 1, color);
    vgfx_circle(win, x + radius, y + radius, radius, color);
    vgfx_circle(win, x + w - radius - 1, y + radius, radius, color);
    vgfx_circle(win, x + radius, y + h - radius - 1, radius, color);
    vgfx_circle(win, x + w - radius - 1, y + h - radius - 1, radius, color);
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
    tooltip->padding = 8;
    tooltip->corner_radius = 6;
    tooltip->bg_color = 0xE0222A36;
    tooltip->text_color = 0x00FFFFFF;
    tooltip->border_color = 0xCC66758A;

    tooltip->font = theme->typography.font_regular;
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
    vg_theme_t *theme = vg_theme_get_current();

    if (!tooltip->is_visible || !tooltip->text)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;
    int32_t x = (int32_t)widget->x;
    int32_t y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->measured_width;
    int32_t h = (int32_t)widget->measured_height;
    int32_t radius = (int32_t)tooltip->corner_radius;
    uint32_t bg = tooltip_apply_alpha(tooltip->bg_color, theme ? theme->colors.bg_primary : 0x00202020u);
    uint32_t border =
        tooltip_apply_alpha(tooltip->border_color, theme ? theme->colors.border_primary : 0x00555555u);

    tooltip_fill_round_rect(win, x + 3, y + 4, w, h, radius, vg_color_darken(bg, 0.68f));
    tooltip_fill_round_rect(win, x, y, w, h, radius, bg);
    if (w > radius * 2)
        vgfx_fill_rect(win, x + radius, y + 1, w - radius * 2, 1, vg_color_lighten(bg, 0.08f));
    tooltip_stroke_round_rect(win, x, y, w, h, radius, border);

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
    int32_t clip_x = x + (int32_t)tooltip->padding;
    int32_t clip_y = y + (int32_t)tooltip->padding;
    int32_t clip_w = w - (int32_t)tooltip->padding * 2;
    int32_t clip_h = h - (int32_t)tooltip->padding * 2;

    if (clip_w > 0 && clip_h > 0)
        vgfx_set_clip(win, clip_x, clip_y, clip_w, clip_h);

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

    if (clip_w > 0 && clip_h > 0)
        vgfx_clear_clip(win);

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
    tooltip->base.needs_paint = true;
}

/// @brief Tooltip show at.
void vg_tooltip_show_at(vg_tooltip_t *tooltip, int x, int y) {
    if (!tooltip)
        return;

    bool was_visible = tooltip->is_visible;
    vg_widget_measure(&tooltip->base, 0.0f, 0.0f);
    if (tooltip->position_mode == VG_TOOLTIP_ANCHOR_WIDGET && tooltip->anchor_widget) {
        float ax = 0.0f, ay = 0.0f, aw = 0.0f, ah = 0.0f;
        vg_widget_get_screen_bounds(tooltip->anchor_widget, &ax, &ay, &aw, &ah);
        tooltip->base.x = ax + (float)tooltip->offset_x;
        tooltip->base.y = ay + ah + (float)tooltip->offset_y;

        vg_widget_t *root = tooltip->anchor_widget;
        while (root->parent)
            root = root->parent;
        float vx = 0.0f, vy = 0.0f, vw = 0.0f, vh = 0.0f;
        vg_widget_get_screen_bounds(root, &vx, &vy, &vw, &vh);
        if (vw > 0.0f && tooltip->base.x + tooltip->base.measured_width > vx + vw)
            tooltip->base.x = vx + vw - tooltip->base.measured_width - 2.0f;
        if (vh > 0.0f && tooltip->base.y + tooltip->base.measured_height > vy + vh)
            tooltip->base.y = ay - tooltip->base.measured_height - (float)tooltip->offset_y;
        if (tooltip->base.x < vx)
            tooltip->base.x = vx;
        if (tooltip->base.y < vy)
            tooltip->base.y = vy;
    } else {
        tooltip->base.x = (float)x + (float)tooltip->offset_x;
        tooltip->base.y = (float)y + (float)tooltip->offset_y;
    }
    tooltip->is_visible = true;
    tooltip->base.visible = true;
    if (!was_visible)
        tooltip->show_timer = tooltip_now_ms();
    tooltip->hide_timer = 0;
    tooltip->base.needs_paint = true;
}

/// @brief Tooltip hide.
void vg_tooltip_hide(vg_tooltip_t *tooltip) {
    if (!tooltip)
        return;

    tooltip->is_visible = false;
    tooltip->base.visible = false;
    tooltip->base.needs_paint = true;
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

    if (mgr->active_tooltip && mgr->active_tooltip->is_visible) {
        if (mgr->active_tooltip->hide_timer > 0 && now_ms >= mgr->active_tooltip->hide_timer) {
            vg_tooltip_hide(mgr->active_tooltip);
            mgr->active_tooltip->hide_timer = 0;
        } else if (mgr->active_tooltip->duration_ms > 0 && mgr->active_tooltip->show_timer > 0 &&
                   now_ms >= mgr->active_tooltip->show_timer + mgr->active_tooltip->duration_ms) {
            vg_tooltip_hide(mgr->active_tooltip);
        }
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
                mgr->active_tooltip->position_mode = VG_TOOLTIP_FOLLOW_CURSOR;
                mgr->active_tooltip->anchor_widget = NULL;
                mgr->active_tooltip->hide_timer = 0;
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
        mgr->active_tooltip->hide_timer = 0;
        vg_tooltip_show_at(mgr->active_tooltip, mgr->cursor_x, mgr->cursor_y);
    } else if (!mgr->active_tooltip->is_visible) {
        mgr->active_tooltip->hide_timer = 0;
        mgr->hover_start_time = tooltip_now_ms();
        if (mgr->active_tooltip->show_delay_ms == 0) {
            mgr->pending_show = false;
            vg_tooltip_show_at(mgr->active_tooltip, mgr->cursor_x, mgr->cursor_y);
        } else {
            mgr->pending_show = true;
        }
    }
}

/// @brief Tooltip manager on leave.
void vg_tooltip_manager_on_leave(vg_tooltip_manager_t *mgr) {
    if (!mgr)
        return;

    if (mgr->active_tooltip) {
        if (mgr->active_tooltip->is_visible && mgr->active_tooltip->hide_delay_ms > 0) {
            mgr->active_tooltip->hide_timer =
                tooltip_now_ms() + (uint64_t)mgr->active_tooltip->hide_delay_ms;
        } else {
            vg_tooltip_hide(mgr->active_tooltip);
        }
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
