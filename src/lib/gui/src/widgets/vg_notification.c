// vg_notification.c - Notification widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void notification_manager_destroy(vg_widget_t* widget);
static void notification_manager_measure(vg_widget_t* widget, float available_width, float available_height);
static void notification_manager_paint(vg_widget_t* widget, void* canvas);
static bool notification_manager_handle_event(vg_widget_t* widget, vg_event_t* event);

//=============================================================================
// Notification Manager VTable
//=============================================================================

static vg_widget_vtable_t g_notification_manager_vtable = {
    .destroy = notification_manager_destroy,
    .measure = notification_manager_measure,
    .arrange = NULL,
    .paint = notification_manager_paint,
    .handle_event = notification_manager_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Notification Helpers
//=============================================================================

static void free_notification(vg_notification_t* notif) {
    if (!notif) return;
    free(notif->title);
    free(notif->message);
    free(notif->action_label);
    free(notif);
}

static uint32_t type_to_color(vg_notification_manager_t* mgr, vg_notification_type_t type) {
    switch (type) {
        case VG_NOTIFICATION_INFO:    return mgr->info_color;
        case VG_NOTIFICATION_SUCCESS: return mgr->success_color;
        case VG_NOTIFICATION_WARNING: return mgr->warning_color;
        case VG_NOTIFICATION_ERROR:   return mgr->error_color;
        default:                      return mgr->info_color;
    }
}

//=============================================================================
// Notification Manager Implementation
//=============================================================================

vg_notification_manager_t* vg_notification_manager_create(void) {
    vg_notification_manager_t* mgr = calloc(1, sizeof(vg_notification_manager_t));
    if (!mgr) return NULL;

    vg_widget_init(&mgr->base, VG_WIDGET_CUSTOM, &g_notification_manager_vtable);

    vg_theme_t* theme = vg_theme_get_current();

    // Defaults
    mgr->position = VG_NOTIFICATION_TOP_RIGHT;
    mgr->max_visible = 5;
    mgr->notification_width = 350;
    mgr->spacing = 8;
    mgr->margin = 16;
    mgr->padding = 12;

    mgr->font_size = theme->typography.size_normal;
    mgr->title_font_size = theme->typography.size_normal + 2;

    mgr->info_color = 0xFF2196F3;      // Blue
    mgr->success_color = 0xFF4CAF50;   // Green
    mgr->warning_color = 0xFFFFC107;   // Amber
    mgr->error_color = 0xFFF44336;     // Red
    mgr->bg_color = 0xFF2D2D2D;
    mgr->text_color = 0xFFFFFFFF;

    mgr->fade_duration_ms = 200;
    mgr->slide_duration_ms = 300;

    mgr->next_id = 1;

    return mgr;
}

static void notification_manager_destroy(vg_widget_t* widget) {
    vg_notification_manager_t* mgr = (vg_notification_manager_t*)widget;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        free_notification(mgr->notifications[i]);
    }
    free(mgr->notifications);
}

void vg_notification_manager_destroy(vg_notification_manager_t* mgr) {
    if (!mgr) return;
    vg_widget_destroy(&mgr->base);
}

static void notification_manager_measure(vg_widget_t* widget, float available_width, float available_height) {
    (void)available_width;
    (void)available_height;

    // Notification manager fills the whole window
    widget->measured_width = available_width;
    widget->measured_height = available_height;
}

static void notification_manager_paint(vg_widget_t* widget, void* canvas) {
    vg_notification_manager_t* mgr = (vg_notification_manager_t*)widget;

    if (mgr->notification_count == 0) return;

    // Calculate starting position based on position mode
    float x, y;
    bool from_top = true;
    bool from_right = true;

    switch (mgr->position) {
        case VG_NOTIFICATION_TOP_LEFT:
            x = widget->x + mgr->margin;
            y = widget->y + mgr->margin;
            from_right = false;
            break;
        case VG_NOTIFICATION_TOP_RIGHT:
            x = widget->x + widget->width - mgr->margin - mgr->notification_width;
            y = widget->y + mgr->margin;
            break;
        case VG_NOTIFICATION_BOTTOM_LEFT:
            x = widget->x + mgr->margin;
            y = widget->y + widget->height - mgr->margin;
            from_top = false;
            from_right = false;
            break;
        case VG_NOTIFICATION_BOTTOM_RIGHT:
            x = widget->x + widget->width - mgr->margin - mgr->notification_width;
            y = widget->y + widget->height - mgr->margin;
            from_top = false;
            break;
        case VG_NOTIFICATION_TOP_CENTER:
            x = widget->x + (widget->width - mgr->notification_width) / 2;
            y = widget->y + mgr->margin;
            break;
        case VG_NOTIFICATION_BOTTOM_CENTER:
            x = widget->x + (widget->width - mgr->notification_width) / 2;
            y = widget->y + widget->height - mgr->margin;
            from_top = false;
            break;
    }
    (void)from_right;

    // Draw visible notifications
    size_t visible_count = mgr->notification_count;
    if (visible_count > mgr->max_visible) visible_count = mgr->max_visible;

    for (size_t i = 0; i < visible_count; i++) {
        vg_notification_t* notif = mgr->notifications[i];
        if (!notif || notif->dismissed) continue;

        // Calculate notification height
        float notif_height = mgr->padding * 2;
        if (notif->title) notif_height += mgr->title_font_size + 4;
        if (notif->message) notif_height += mgr->font_size + 4;
        if (notif->action_label) notif_height += mgr->font_size + 8;

        float notif_y = from_top ? y : y - notif_height;

        // Apply opacity for fade animation
        uint8_t alpha = (uint8_t)(notif->opacity * 255);
        (void)alpha;

        // Draw notification background
        uint32_t type_color = type_to_color(mgr, notif->type);
        (void)type_color;
        (void)mgr->bg_color;

        // Draw color accent bar (left side)
        // Background drawing placeholder

        // Draw content
        float content_x = x + mgr->padding + 4;  // Account for accent bar
        float content_y = notif_y + mgr->padding;

        if (notif->title && mgr->font) {
            vg_font_draw_text(canvas, mgr->font, mgr->title_font_size,
                content_x, content_y, notif->title, mgr->text_color);
            content_y += mgr->title_font_size + 4;
        }

        if (notif->message && mgr->font) {
            vg_font_draw_text(canvas, mgr->font, mgr->font_size,
                content_x, content_y, notif->message, mgr->text_color);
            content_y += mgr->font_size + 4;
        }

        // Draw action button if present
        if (notif->action_label && mgr->font) {
            // Action button drawing placeholder
            (void)content_y;
        }

        // Move to next position
        if (from_top) {
            y += notif_height + mgr->spacing;
        } else {
            y -= notif_height + mgr->spacing;
        }
    }
}

static bool notification_manager_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_notification_manager_t* mgr = (vg_notification_manager_t*)widget;

    if (event->type == VG_EVENT_CLICK) {
        // Check if click is on a notification or its action button
        // For now, just dismiss any clicked notification
        for (size_t i = 0; i < mgr->notification_count; i++) {
            vg_notification_t* notif = mgr->notifications[i];
            if (!notif || notif->dismissed) continue;

            // Simple hit test - check if within notification area
            // Full implementation would track notification bounds
            (void)event->mouse.x;
            (void)event->mouse.y;
        }
    }

    return false;
}

void vg_notification_manager_update(vg_notification_manager_t* mgr, uint64_t now_ms) {
    if (!mgr) return;

    bool needs_cleanup = false;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        vg_notification_t* notif = mgr->notifications[i];
        if (!notif) continue;

        // Check for auto-dismiss
        if (notif->duration_ms > 0 && !notif->dismissed) {
            uint64_t elapsed = now_ms - notif->created_at;
            if (elapsed >= notif->duration_ms) {
                // Start fade out
                float fade_elapsed = (float)(elapsed - notif->duration_ms);
                notif->opacity = 1.0f - (fade_elapsed / mgr->fade_duration_ms);
                if (notif->opacity <= 0) {
                    notif->opacity = 0;
                    notif->dismissed = true;
                    needs_cleanup = true;
                }
                mgr->base.needs_paint = true;
            }
        }

        // Fade in newly added notifications
        if (!notif->dismissed && notif->opacity < 1.0f) {
            float elapsed = (float)(now_ms - notif->created_at);
            if (elapsed < mgr->fade_duration_ms) {
                notif->opacity = elapsed / mgr->fade_duration_ms;
                mgr->base.needs_paint = true;
            } else {
                notif->opacity = 1.0f;
            }
        }
    }

    // Remove dismissed notifications
    if (needs_cleanup) {
        size_t write_idx = 0;
        for (size_t i = 0; i < mgr->notification_count; i++) {
            if (mgr->notifications[i] && !mgr->notifications[i]->dismissed) {
                mgr->notifications[write_idx++] = mgr->notifications[i];
            } else {
                free_notification(mgr->notifications[i]);
            }
        }
        mgr->notification_count = write_idx;
    }
}

uint32_t vg_notification_show(vg_notification_manager_t* mgr,
    vg_notification_type_t type, const char* title, const char* message,
    uint32_t duration_ms) {

    return vg_notification_show_with_action(mgr, type, title, message,
        duration_ms, NULL, NULL, NULL);
}

uint32_t vg_notification_show_with_action(vg_notification_manager_t* mgr,
    vg_notification_type_t type, const char* title, const char* message,
    uint32_t duration_ms, const char* action_label,
    void (*action_callback)(uint32_t, void*), void* user_data) {

    if (!mgr) return 0;

    vg_notification_t* notif = calloc(1, sizeof(vg_notification_t));
    if (!notif) return 0;

    notif->id = mgr->next_id++;
    notif->type = type;
    notif->title = title ? strdup(title) : NULL;
    notif->message = message ? strdup(message) : NULL;
    notif->duration_ms = duration_ms;
    notif->created_at = 0;  // Should be set by caller
    notif->action_label = action_label ? strdup(action_label) : NULL;
    notif->action_callback = action_callback;
    notif->action_user_data = user_data;
    notif->opacity = 0;  // Will fade in

    // Add to array
    if (mgr->notification_count >= mgr->notification_capacity) {
        size_t new_cap = mgr->notification_capacity * 2;
        if (new_cap < 8) new_cap = 8;
        vg_notification_t** new_notifs = realloc(mgr->notifications,
            new_cap * sizeof(vg_notification_t*));
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

void vg_notification_dismiss(vg_notification_manager_t* mgr, uint32_t id) {
    if (!mgr) return;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i] && mgr->notifications[i]->id == id) {
            mgr->notifications[i]->dismissed = true;
            mgr->base.needs_paint = true;
            return;
        }
    }
}

void vg_notification_dismiss_all(vg_notification_manager_t* mgr) {
    if (!mgr) return;

    for (size_t i = 0; i < mgr->notification_count; i++) {
        if (mgr->notifications[i]) {
            mgr->notifications[i]->dismissed = true;
        }
    }
    mgr->base.needs_paint = true;
}

void vg_notification_manager_set_position(vg_notification_manager_t* mgr,
    vg_notification_position_t position) {

    if (!mgr) return;
    mgr->position = position;
    mgr->base.needs_paint = true;
}

void vg_notification_manager_set_font(vg_notification_manager_t* mgr,
    vg_font_t* font, float size) {

    if (!mgr) return;
    mgr->font = font;
    mgr->font_size = size;
    mgr->title_font_size = size + 2;
    mgr->base.needs_paint = true;
}
