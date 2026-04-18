//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_notification.c
//
//===----------------------------------------------------------------------===//
// vg_notification.c - Notification widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void notification_manager_destroy(vg_widget_t *widget);
static void notification_manager_measure(vg_widget_t *widget,
                                         float available_width,
                                         float available_height);
static void notification_manager_paint(vg_widget_t *widget, void *canvas);
static bool notification_manager_handle_event(vg_widget_t *widget, vg_event_t *event);
static uint32_t notification_fade_color(uint32_t color, uint32_t backdrop, float opacity);
static void notification_fill_round_rect(vgfx_window_t win,
                                         int32_t x,
                                         int32_t y,
                                         int32_t w,
                                         int32_t h,
                                         int32_t radius,
                                         uint32_t color);
static void notification_stroke_round_rect(vgfx_window_t win,
                                           int32_t x,
                                           int32_t y,
                                           int32_t w,
                                           int32_t h,
                                           int32_t radius,
                                           uint32_t color);
static char *notification_dup_range(const char *text, size_t len);
static int notification_wrap_text(vg_notification_manager_t *mgr,
                                  const char *text,
                                  float font_size,
                                  float max_width,
                                  char ***out_lines,
                                  float *out_max_width);
static void notification_free_lines(char **lines, int line_count);
static float notification_line_height(vg_notification_manager_t *mgr, float font_size);
static float notification_text_block_height(vg_notification_manager_t *mgr,
                                            const char *text,
                                            float font_size,
                                            float max_width);
static float notification_measure_height(vg_notification_manager_t *mgr,
                                         vg_notification_t *notif,
                                         float *out_action_h);
static void notification_request_dismiss(vg_notification_t *notif, uint64_t now_ms);

//=============================================================================
// Notification Manager VTable
//=============================================================================

static vg_widget_vtable_t g_notification_manager_vtable = {.destroy = notification_manager_destroy,
                                                           .measure = notification_manager_measure,
                                                           .arrange = NULL,
                                                           .paint = notification_manager_paint,
                                                           .handle_event =
                                                               notification_manager_handle_event,
                                                           .can_focus = NULL,
                                                           .on_focus = NULL};

//=============================================================================
// Notification Helpers
//=============================================================================

static void free_notification(vg_notification_t *notif) {
    if (!notif)
        return;
    free(notif->title);
    free(notif->message);
    free(notif->action_label);
    free(notif);
}

static uint32_t type_to_color(vg_notification_manager_t *mgr, vg_notification_type_t type) {
    switch (type) {
        case VG_NOTIFICATION_INFO:
            return mgr->info_color;
        case VG_NOTIFICATION_SUCCESS:
            return mgr->success_color;
        case VG_NOTIFICATION_WARNING:
            return mgr->warning_color;
        case VG_NOTIFICATION_ERROR:
            return mgr->error_color;
        default:
            return mgr->info_color;
    }
}

static uint32_t notification_fade_color(uint32_t color, uint32_t backdrop, float opacity) {
    uint32_t rgb = color & 0x00FFFFFFu;
    if (opacity <= 0.0f)
        return backdrop & 0x00FFFFFFu;
    if (opacity >= 1.0f)
        return rgb;
    return vg_color_blend(backdrop & 0x00FFFFFFu, rgb, opacity);
}

static void notification_fill_round_rect(vgfx_window_t win,
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

static void notification_stroke_round_rect(vgfx_window_t win,
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

static char *notification_dup_range(const char *text, size_t len) {
    char *copy = (char *)malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

static int notification_wrap_text(vg_notification_manager_t *mgr,
                                  const char *text,
                                  float font_size,
                                  float max_width,
                                  char ***out_lines,
                                  float *out_max_width) {
    if (out_lines)
        *out_lines = NULL;
    if (out_max_width)
        *out_max_width = 0.0f;
    if (!mgr || !mgr->font || !text || !text[0])
        return 0;

    int cap = 4;
    int count = 0;
    char **lines = out_lines ? (char **)calloc((size_t)cap, sizeof(char *)) : NULL;
    size_t text_len = strlen(text);
    size_t start = 0;

    while (start <= text_len) {
        if (text[start] == '\0') {
            if (count == 0 && lines)
                lines[count++] = strdup("");
            break;
        }

        size_t line_end = start;
        size_t best_end = start;
        size_t best_next = start;
        float best_width = 0.0f;
        bool found_break = false;

        while (line_end < text_len && text[line_end] != '\n') {
            size_t candidate_end = line_end + 1;
            char *candidate = notification_dup_range(text + start, candidate_end - start);
            if (!candidate)
                break;
            vg_text_metrics_t metrics = {0};
            vg_font_measure_text(mgr->font, font_size, candidate, &metrics);
            free(candidate);
            if (max_width > 0.0f && metrics.width > max_width)
                break;

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
            char *candidate = notification_dup_range(text + start, best_end - start);
            if (candidate) {
                vg_text_metrics_t metrics = {0};
                vg_font_measure_text(mgr->font, font_size, candidate, &metrics);
                best_width = metrics.width;
                free(candidate);
            }
        }

        while (best_end > start && (text[best_end - 1] == ' ' || text[best_end - 1] == '\t'))
            best_end--;

        if (lines) {
            if (count >= cap) {
                int new_cap = cap * 2;
                char **new_lines = (char **)realloc(lines, (size_t)new_cap * sizeof(char *));
                if (!new_lines)
                    break;
                memset(new_lines + cap, 0, (size_t)(new_cap - cap) * sizeof(char *));
                lines = new_lines;
                cap = new_cap;
            }
            lines[count] = notification_dup_range(text + start, best_end - start);
            if (!lines[count])
                break;
            count++;
        } else {
            count++;
        }

        if (out_max_width && best_width > *out_max_width)
            *out_max_width = best_width;

        size_t prev_start = start;
        start = best_next;
        while (text[start] == ' ' || text[start] == '\t')
            start++;
        if (start <= prev_start) {
            if (text[start] == '\0')
                break;
            start = prev_start + 1;
        }
        if (text[start] == '\n')
            start++;
    }

    if (out_lines) {
        *out_lines = lines;
    } else if (lines) {
        notification_free_lines(lines, count);
    }
    return count > 0 ? count : 1;
}

static void notification_free_lines(char **lines, int line_count) {
    if (!lines)
        return;
    for (int i = 0; i < line_count; i++)
        free(lines[i]);
    free(lines);
}

static float notification_line_height(vg_notification_manager_t *mgr, float font_size) {
    vg_font_metrics_t metrics = {0};
    if (!mgr || !mgr->font)
        return font_size;
    vg_font_get_metrics(mgr->font, font_size, &metrics);
    return metrics.line_height > 0 ? (float)metrics.line_height : font_size;
}

static float notification_text_block_height(vg_notification_manager_t *mgr,
                                            const char *text,
                                            float font_size,
                                            float max_width) {
    int line_count = notification_wrap_text(mgr, text, font_size, max_width, NULL, NULL);
    return notification_line_height(mgr, font_size) * (float)line_count;
}

static float notification_measure_height(vg_notification_manager_t *mgr,
                                         vg_notification_t *notif,
                                         float *out_action_h) {
    if (!mgr || !notif)
        return 0.0f;

    float content_width = (float)mgr->notification_width - (float)(mgr->padding * 2 + 16);
    float notif_height = (float)mgr->padding * 2.0f;
    float action_h = notification_line_height(mgr, mgr->font_size) + 8.0f;

    if (notif->title && notif->title[0]) {
        notif_height += notification_text_block_height(mgr, notif->title, mgr->title_font_size, content_width);
    }
    if (notif->message && notif->message[0]) {
        if (notif->title && notif->title[0])
            notif_height += 6.0f;
        notif_height += notification_text_block_height(mgr, notif->message, mgr->font_size, content_width);
    }
    if (notif->action_label && notif->action_label[0]) {
        if ((notif->title && notif->title[0]) || (notif->message && notif->message[0]))
            notif_height += 10.0f;
        notif_height += action_h;
    }

    if (out_action_h)
        *out_action_h = action_h;
    return notif_height;
}

static void notification_request_dismiss(vg_notification_t *notif, uint64_t now_ms) {
    if (!notif)
        return;
    notif->dismissed = true;
    if (notif->dismiss_started_at == 0 && now_ms > 0)
        notif->dismiss_started_at = now_ms;
}

static bool notification_bounds_for_index(vg_notification_manager_t *mgr,
                                          size_t target_index,
                                          float *out_x,
                                          float *out_y,
                                          float *out_w,
                                          float *out_h,
                                          float *action_x,
                                          float *action_y,
                                          float *action_w,
                                          float *action_h) {
    if (!mgr || target_index >= mgr->notification_count)
        return false;

    float x, y;
    bool from_top = true;
    bool from_right = true;

    switch (mgr->position) {
        case VG_NOTIFICATION_TOP_LEFT:
            x = mgr->base.x + mgr->margin;
            y = mgr->base.y + mgr->margin;
            from_right = false;
            break;
        case VG_NOTIFICATION_TOP_RIGHT:
            x = mgr->base.x + mgr->base.width - mgr->margin - mgr->notification_width;
            y = mgr->base.y + mgr->margin;
            break;
        case VG_NOTIFICATION_BOTTOM_LEFT:
            x = mgr->base.x + mgr->margin;
            y = mgr->base.y + mgr->base.height - mgr->margin;
            from_top = false;
            from_right = false;
            break;
        case VG_NOTIFICATION_BOTTOM_RIGHT:
            x = mgr->base.x + mgr->base.width - mgr->margin - mgr->notification_width;
            y = mgr->base.y + mgr->base.height - mgr->margin;
            from_top = false;
            break;
        case VG_NOTIFICATION_TOP_CENTER:
            x = mgr->base.x + (mgr->base.width - mgr->notification_width) / 2;
            y = mgr->base.y + mgr->margin;
            break;
        case VG_NOTIFICATION_BOTTOM_CENTER:
            x = mgr->base.x + (mgr->base.width - mgr->notification_width) / 2;
            y = mgr->base.y + mgr->base.height - mgr->margin;
            from_top = false;
            break;
    }

    size_t visible_count = 0;
    for (size_t i = 0; i < mgr->notification_count; i++) {
        vg_notification_t *notif = mgr->notifications[i];
        if (!notif)
            continue;
        if (visible_count >= mgr->max_visible)
            break;

        float action_h_px = 0.0f;
        float notif_height = notification_measure_height(mgr, notif, &action_h_px);
        float slide_t = notif->slide_progress;
        if (slide_t < 0.0f)
            slide_t = 0.0f;
        if (slide_t > 1.0f)
            slide_t = 1.0f;

        float notif_x = x + (1.0f - slide_t) * 24.0f * (from_right ? 1.0f : -1.0f);
        float notif_y = (from_top ? y : y - notif_height) + (1.0f - slide_t) * 8.0f * (from_top ? -1.0f : 1.0f);
        if (i == target_index) {
            if (out_x)
                *out_x = notif_x;
            if (out_y)
                *out_y = notif_y;
            if (out_w)
                *out_w = (float)mgr->notification_width;
            if (out_h)
                *out_h = notif_height;
            if (action_x)
                *action_x = notif_x + (float)mgr->padding + 8.0f;
            if (action_y)
                *action_y = notif_y + notif_height - (float)mgr->padding - action_h_px;
            if (action_w)
                *action_w = (float)mgr->notification_width - (float)(mgr->padding * 2 + 16);
            if (action_h)
                *action_h = action_h_px;
            return true;
        }

        if (from_top)
            y += notif_height + mgr->spacing;
        else
            y -= notif_height + mgr->spacing;
        visible_count++;
    }

    return false;
}

//=============================================================================
// Notification Manager Implementation
//=============================================================================

vg_notification_manager_t *vg_notification_manager_create(void) {
    vg_notification_manager_t *mgr = calloc(1, sizeof(vg_notification_manager_t));
    if (!mgr)
        return NULL;

    vg_widget_init(&mgr->base, VG_WIDGET_CUSTOM, &g_notification_manager_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    mgr->position = VG_NOTIFICATION_TOP_RIGHT;
    mgr->max_visible = 5;
    mgr->notification_width = 350;
    mgr->spacing = 8;
    mgr->margin = 16;
    mgr->padding = 12;

    mgr->font = theme->typography.font_regular;
    mgr->font_size = theme->typography.size_normal;
    mgr->title_font_size = theme->typography.size_normal + 2;

    mgr->info_color = 0xFF2196F3;    // Blue
    mgr->success_color = 0xFF4CAF50; // Green
    mgr->warning_color = 0xFFFFC107; // Amber
    mgr->error_color = 0xFFF44336;   // Red
    mgr->bg_color = 0xF0212934;
    mgr->text_color = theme->colors.fg_primary;

    mgr->fade_duration_ms = 200;
    mgr->slide_duration_ms = 300;

    mgr->next_id = 1;

    return mgr;
}

static void notification_manager_destroy(vg_widget_t *widget) {
    vg_notification_manager_t *mgr = (vg_notification_manager_t *)widget;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        free_notification(mgr->notifications[i]);
    }
    free(mgr->notifications);
}

/// @brief Notification manager destroy.
void vg_notification_manager_destroy(vg_notification_manager_t *mgr) {
    if (!mgr)
        return;
    vg_widget_destroy(&mgr->base);
}

static void notification_manager_measure(vg_widget_t *widget,
                                         float available_width,
                                         float available_height) {
    (void)available_width;
    (void)available_height;

    // Notification manager fills the whole window
    widget->measured_width = available_width;
    widget->measured_height = available_height;
}

static void notification_manager_paint(vg_widget_t *widget, void *canvas) {
    vg_notification_manager_t *mgr = (vg_notification_manager_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    if (mgr->notification_count == 0)
        return;

    size_t visible_count = 0;
    for (size_t i = 0; i < mgr->notification_count; i++) {
        vg_notification_t *notif = mgr->notifications[i];
        if (!notif)
            continue;
        if (visible_count >= mgr->max_visible)
            break;

        float notif_x = 0.0f, notif_y = 0.0f, notif_w = 0.0f, notif_h = 0.0f;
        float action_x = 0.0f, action_y = 0.0f, action_w = 0.0f, action_h = 0.0f;
        if (!notification_bounds_for_index(
                mgr, i, &notif_x, &notif_y, &notif_w, &notif_h, &action_x, &action_y, &action_w, &action_h)) {
            continue;
        }

        float opacity = notif->opacity;
        if (opacity <= 0.0f) {
            visible_count++;
            continue;
        }
        if (opacity > 1.0f)
            opacity = 1.0f;

        uint32_t backdrop = theme->colors.bg_primary;
        uint32_t type_color = type_to_color(mgr, notif->type);
        uint32_t card_bg = notification_fade_color(mgr->bg_color, backdrop, opacity);
        uint32_t card_border = notification_fade_color(
            vg_color_blend(type_color, theme->colors.border_primary, 0.55f), backdrop, opacity);
        uint32_t accent_color = notification_fade_color(type_color, backdrop, opacity);
        uint32_t title_color =
            notification_fade_color(vg_color_lighten(mgr->text_color, 0.08f), backdrop, opacity);
        uint32_t body_color = notification_fade_color(mgr->text_color, backdrop, opacity);
        uint32_t shadow_color = notification_fade_color(0x000000u, backdrop, opacity * 0.24f);
        uint32_t action_bg = notification_fade_color(
            vg_color_blend(type_color, mgr->bg_color, 0.78f), backdrop, opacity);
        uint32_t action_border = notification_fade_color(
            vg_color_blend(type_color, theme->colors.border_primary, 0.35f), backdrop, opacity);
        uint32_t action_text =
            notification_fade_color(vg_color_lighten(type_color, 0.14f), backdrop, opacity);

        int32_t x = (int32_t)notif_x;
        int32_t y = (int32_t)notif_y;
        int32_t w = (int32_t)notif_w;
        int32_t h = (int32_t)notif_h;
        int32_t radius = 10;
        int32_t accent_w = 5;

        notification_fill_round_rect(win, x + 2, y + 4, w, h, radius, shadow_color);
        notification_fill_round_rect(win, x, y, w, h, radius, card_bg);
        notification_stroke_round_rect(win, x, y, w, h, radius, card_border);
        notification_fill_round_rect(win, x, y, accent_w, h, radius / 2, accent_color);
        if (w > 12) {
            vgfx_fill_rect(
                win, x + 10, y + 1, w - 20, 1, notification_fade_color(vg_color_lighten(card_bg, 0.07f), backdrop, opacity));
        }

        float content_x = notif_x + (float)mgr->padding + 10.0f;
        float content_y = notif_y + (float)mgr->padding;
        float content_w = notif_w - (float)(mgr->padding * 2) - 18.0f;
        if (content_w < 8.0f)
            content_w = 8.0f;

        int32_t clip_x = x + 6;
        int32_t clip_y = y + 4;
        int32_t clip_w = w - 12;
        int32_t clip_h = h - 8;
        if (clip_w > 0 && clip_h > 0)
            vgfx_set_clip(win, clip_x, clip_y, clip_w, clip_h);

        if (mgr->font) {
            if (notif->title && notif->title[0]) {
                char **lines = NULL;
                int line_count = notification_wrap_text(
                    mgr, notif->title, mgr->title_font_size, content_w, &lines, NULL);
                float line_h = notification_line_height(mgr, mgr->title_font_size);
                vg_font_metrics_t metrics = {0};
                vg_font_get_metrics(mgr->font, mgr->title_font_size, &metrics);
                for (int line = 0; line < line_count; line++) {
                    const char *text = (lines && lines[line]) ? lines[line] : "";
                    vg_font_draw_text(canvas,
                                      mgr->font,
                                      mgr->title_font_size,
                                      content_x,
                                      content_y + (float)metrics.ascent,
                                      text,
                                      title_color);
                    content_y += line_h;
                }
                notification_free_lines(lines, line_count);
            }

            if (notif->message && notif->message[0]) {
                if (notif->title && notif->title[0])
                    content_y += 6.0f;
                char **lines = NULL;
                int line_count =
                    notification_wrap_text(mgr, notif->message, mgr->font_size, content_w, &lines, NULL);
                float line_h = notification_line_height(mgr, mgr->font_size);
                vg_font_metrics_t metrics = {0};
                vg_font_get_metrics(mgr->font, mgr->font_size, &metrics);
                for (int line = 0; line < line_count; line++) {
                    const char *text = (lines && lines[line]) ? lines[line] : "";
                    vg_font_draw_text(canvas,
                                      mgr->font,
                                      mgr->font_size,
                                      content_x,
                                      content_y + (float)metrics.ascent,
                                      text,
                                      body_color);
                    content_y += line_h;
                }
                notification_free_lines(lines, line_count);
            }

            if (notif->action_label && notif->action_label[0]) {
                int32_t ar = (int32_t)(action_h * 0.5f);
                notification_fill_round_rect(
                    win, (int32_t)action_x, (int32_t)action_y, (int32_t)action_w, (int32_t)action_h, ar, action_bg);
                notification_stroke_round_rect(
                    win, (int32_t)action_x, (int32_t)action_y, (int32_t)action_w, (int32_t)action_h, ar, action_border);
                vg_font_metrics_t metrics = {0};
                vg_text_metrics_t text_metrics = {0};
                vg_font_get_metrics(mgr->font, mgr->font_size, &metrics);
                vg_font_measure_text(mgr->font, mgr->font_size, notif->action_label, &text_metrics);
                float label_x = action_x + 12.0f;
                if (text_metrics.width + 24.0f < action_w) {
                    label_x = action_x + (action_w - text_metrics.width) * 0.5f;
                }
                float label_y =
                    action_y + (action_h - (float)(metrics.ascent - metrics.descent)) * 0.5f +
                    (float)metrics.ascent;
                vg_font_draw_text(canvas,
                                  mgr->font,
                                  mgr->font_size,
                                  label_x,
                                  label_y,
                                  notif->action_label,
                                  action_text);
            }
        }

        if (clip_w > 0 && clip_h > 0)
            vgfx_clear_clip(win);
        visible_count++;
    }
}

static bool notification_manager_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_notification_manager_t *mgr = (vg_notification_manager_t *)widget;

    if (event->type == VG_EVENT_CLICK || event->type == VG_EVENT_MOUSE_DOWN) {
        for (size_t i = 0; i < mgr->notification_count; i++) {
            vg_notification_t *notif = mgr->notifications[i];
            if (!notif || notif->dismissed)
                continue;
            float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
            float ax = 0.0f, ay = 0.0f, aw = 0.0f, ah = 0.0f;
            if (!notification_bounds_for_index(mgr, i, &x, &y, &w, &h, &ax, &ay, &aw, &ah)) {
                continue;
            }
            float px = event->mouse.screen_x;
            float py = event->mouse.screen_y;
            if (px < x || px >= x + w || py < y || py >= y + h)
                continue;
            if (notif->action_label && px >= ax && px < ax + aw && py >= ay && py < ay + ah &&
                notif->action_callback) {
                notif->action_callback(notif->id, notif->action_user_data);
            }
            notification_request_dismiss(notif, 0);
            mgr->base.needs_paint = true;
            return true;
        }
    }

    return false;
}

/// @brief Notification manager update.
void vg_notification_manager_update(vg_notification_manager_t *mgr, uint64_t now_ms) {
    if (!mgr)
        return;

    size_t old_count = mgr->notification_count;
    bool needs_repaint = false;
    for (size_t i = 0; i < mgr->notification_count; i++) {
        vg_notification_t *notif = mgr->notifications[i];
        if (!notif)
            continue;

        if (notif->dismissed && notif->created_at == 0 && notif->dismiss_started_at == 0) {
            notif->opacity = 0.0f;
            notif->slide_progress = 0.0f;
            continue;
        }

        if (notif->created_at == 0) {
            notif->created_at = now_ms ? now_ms : 1;
            needs_repaint = true;
        }

        if (notif->duration_ms > 0 && !notif->dismissed) {
            uint64_t elapsed = now_ms - notif->created_at;
            if (elapsed >= notif->duration_ms) {
                notification_request_dismiss(notif, notif->created_at + notif->duration_ms);
                needs_repaint = true;
            }
        }

        if (notif->dismissed) {
            if (notif->dismiss_started_at == 0)
                notif->dismiss_started_at = now_ms;
            uint64_t dismiss_at = notif->dismiss_started_at;
            uint64_t elapsed = now_ms >= dismiss_at ? (now_ms - dismiss_at) : 0;
            if (mgr->fade_duration_ms > 0) {
                float t = (float)elapsed / (float)mgr->fade_duration_ms;
                if (t > 1.0f)
                    t = 1.0f;
                notif->opacity = 1.0f - t;
            } else {
                notif->opacity = 0.0f;
            }
            if (mgr->slide_duration_ms > 0) {
                float t = (float)elapsed / (float)mgr->slide_duration_ms;
                if (t > 1.0f)
                    t = 1.0f;
                notif->slide_progress = 1.0f - t;
            } else {
                notif->slide_progress = 0.0f;
            }
            if (notif->opacity < 0.0f)
                notif->opacity = 0.0f;
            if (notif->slide_progress < 0.0f)
                notif->slide_progress = 0.0f;
            if (notif->opacity > 0.0f || notif->slide_progress > 0.0f)
                needs_repaint = true;
        } else {
            uint64_t elapsed = now_ms >= notif->created_at ? (now_ms - notif->created_at) : 0;
            if (mgr->fade_duration_ms > 0) {
                notif->opacity = (float)elapsed / (float)mgr->fade_duration_ms;
                if (notif->opacity > 1.0f)
                    notif->opacity = 1.0f;
            } else {
                notif->opacity = 1.0f;
            }
            if (mgr->slide_duration_ms > 0) {
                notif->slide_progress = (float)elapsed / (float)mgr->slide_duration_ms;
                if (notif->slide_progress > 1.0f)
                    notif->slide_progress = 1.0f;
            } else {
                notif->slide_progress = 1.0f;
            }
            if (notif->opacity < 1.0f || notif->slide_progress < 1.0f)
                needs_repaint = true;
        }
    }

    size_t write_idx = 0;
    for (size_t i = 0; i < mgr->notification_count; i++) {
        vg_notification_t *notif = mgr->notifications[i];
        bool keep = notif != NULL;
        if (notif && notif->dismissed && notif->opacity <= 0.0f && notif->slide_progress <= 0.0f) {
            keep = false;
        }
        if (keep) {
            mgr->notifications[write_idx++] = mgr->notifications[i];
        } else {
            free_notification(mgr->notifications[i]);
        }
    }
    mgr->notification_count = write_idx;
    if (needs_repaint || old_count != write_idx)
        mgr->base.needs_paint = true;
}

uint32_t vg_notification_show(vg_notification_manager_t *mgr,
                              vg_notification_type_t type,
                              const char *title,
                              const char *message,
                              uint32_t duration_ms) {
    return vg_notification_show_with_action(
        mgr, type, title, message, duration_ms, NULL, NULL, NULL);
}

uint32_t vg_notification_show_with_action(vg_notification_manager_t *mgr,
                                          vg_notification_type_t type,
                                          const char *title,
                                          const char *message,
                                          uint32_t duration_ms,
                                          const char *action_label,
                                          void (*action_callback)(uint32_t, void *),
                                          void *user_data) {
    if (!mgr)
        return 0;

    vg_notification_t *notif = calloc(1, sizeof(vg_notification_t));
    if (!notif)
        return 0;

    notif->id = mgr->next_id++;
    notif->type = type;
    notif->title = title ? strdup(title) : NULL;
    notif->message = message ? strdup(message) : NULL;
    notif->duration_ms = duration_ms;
    notif->created_at = 0; // Sentinel — populated on first manager_update tick.
    notif->action_label = action_label ? strdup(action_label) : NULL;
    notif->action_callback = action_callback;
    notif->action_user_data = user_data;
    notif->opacity = 0.0f;
    notif->slide_progress = 0.0f;
    notif->dismiss_started_at = 0;
    notif->dismissed = false;

    // Add to array
    if (mgr->notification_count >= mgr->notification_capacity) {
        size_t new_cap = mgr->notification_capacity * 2;
        if (new_cap < 8)
            new_cap = 8;
        vg_notification_t **new_notifs =
            realloc(mgr->notifications, new_cap * sizeof(vg_notification_t *));
        if (!new_notifs) {
            free_notification(notif);
            return 0;
        }
        mgr->notifications = new_notifs;
        mgr->notification_capacity = new_cap;
    }

    mgr->notifications[mgr->notification_count++] = notif;
    mgr->base.needs_paint = true;

    return notif->id;
}

/// @brief Notification dismiss.
void vg_notification_dismiss(vg_notification_manager_t *mgr, uint32_t id) {
    if (!mgr)
        return;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == id) {
            notification_request_dismiss(mgr->notifications[i], 0);
            mgr->base.needs_paint = true;
            return;
        }
    }
}

/// @brief Notification dismiss all.
void vg_notification_dismiss_all(vg_notification_manager_t *mgr) {
    if (!mgr)
        return;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i]) {
            notification_request_dismiss(mgr->notifications[i], 0);
        }
    }
    mgr->base.needs_paint = true;
}

/// @brief Notification manager set position.
void vg_notification_manager_set_position(vg_notification_manager_t *mgr,
                                          vg_notification_position_t position) {
    if (!mgr)
        return;
    mgr->position = position;
    mgr->base.needs_paint = true;
}

/// @brief Notification manager set font.
void vg_notification_manager_set_font(vg_notification_manager_t *mgr, vg_font_t *font, float size) {
    if (!mgr)
        return;
    mgr->font = font;
    mgr->font_size = size;
    mgr->title_font_size = size + 2;
    mgr->base.needs_paint = true;
}
