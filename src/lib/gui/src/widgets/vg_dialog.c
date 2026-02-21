// vg_dialog.c - Dialog widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Constants
//=============================================================================

#define DIALOG_DEFAULT_MIN_WIDTH 300
#define DIALOG_DEFAULT_MIN_HEIGHT 150
#define DIALOG_DEFAULT_MAX_WIDTH 800
#define DIALOG_DEFAULT_MAX_HEIGHT 600
#define DIALOG_TITLE_BAR_HEIGHT 32
#define DIALOG_BUTTON_BAR_HEIGHT 48
#define DIALOG_CONTENT_PADDING 16
#define DIALOG_BUTTON_PADDING 8
#define DIALOG_BUTTON_HEIGHT 28
#define DIALOG_BUTTON_MIN_WIDTH 80
#define DIALOG_CLOSE_BUTTON_SIZE 24
#define DIALOG_ICON_SIZE 32

//=============================================================================
// Forward Declarations
//=============================================================================

static void dialog_destroy(vg_widget_t *widget);
static void dialog_measure(vg_widget_t *widget, float available_width, float available_height);
static void dialog_arrange(vg_widget_t *widget, float x, float y, float width, float height);
static void dialog_paint(vg_widget_t *widget, void *canvas);
static bool dialog_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// Dialog VTable
//=============================================================================

static vg_widget_vtable_t g_dialog_vtable = {.destroy = dialog_destroy,
                                             .measure = dialog_measure,
                                             .arrange = dialog_arrange,
                                             .paint = dialog_paint,
                                             .handle_event = dialog_handle_event,
                                             .can_focus = NULL,
                                             .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

typedef struct
{
    const char *label;
    vg_dialog_result_t result;
    bool is_default;
    bool is_cancel;
} preset_button_t;

static const preset_button_t g_ok_buttons[] = {{"OK", VG_DIALOG_RESULT_OK, true, false}};

static const preset_button_t g_ok_cancel_buttons[] = {
    {"OK", VG_DIALOG_RESULT_OK, true, false}, {"Cancel", VG_DIALOG_RESULT_CANCEL, false, true}};

static const preset_button_t g_yes_no_buttons[] = {{"Yes", VG_DIALOG_RESULT_YES, true, false},
                                                   {"No", VG_DIALOG_RESULT_NO, false, true}};

static const preset_button_t g_yes_no_cancel_buttons[] = {
    {"Yes", VG_DIALOG_RESULT_YES, true, false},
    {"No", VG_DIALOG_RESULT_NO, false, false},
    {"Cancel", VG_DIALOG_RESULT_CANCEL, false, true}};

static const preset_button_t g_retry_cancel_buttons[] = {
    {"Retry", VG_DIALOG_RESULT_RETRY, true, false},
    {"Cancel", VG_DIALOG_RESULT_CANCEL, false, true}};

static void get_preset_buttons(vg_dialog_buttons_t preset,
                               const preset_button_t **buttons,
                               size_t *count)
{
    switch (preset)
    {
        case VG_DIALOG_BUTTONS_OK:
            *buttons = g_ok_buttons;
            *count = 1;
            break;
        case VG_DIALOG_BUTTONS_OK_CANCEL:
            *buttons = g_ok_cancel_buttons;
            *count = 2;
            break;
        case VG_DIALOG_BUTTONS_YES_NO:
            *buttons = g_yes_no_buttons;
            *count = 2;
            break;
        case VG_DIALOG_BUTTONS_YES_NO_CANCEL:
            *buttons = g_yes_no_cancel_buttons;
            *count = 3;
            break;
        case VG_DIALOG_BUTTONS_RETRY_CANCEL:
            *buttons = g_retry_cancel_buttons;
            *count = 2;
            break;
        default:
            *buttons = NULL;
            *count = 0;
            break;
    }
}

static float get_button_width(vg_dialog_t *dlg, const char *label)
{
    float width = DIALOG_BUTTON_MIN_WIDTH;
    if (dlg->font && label)
    {
        vg_text_metrics_t metrics;
        vg_font_measure_text(dlg->font, dlg->font_size, label, &metrics);
        width = metrics.width + DIALOG_BUTTON_PADDING * 4;
        if (width < DIALOG_BUTTON_MIN_WIDTH)
        {
            width = DIALOG_BUTTON_MIN_WIDTH;
        }
    }
    return width;
}

static const char *get_icon_glyph(vg_dialog_icon_t icon)
{
    switch (icon)
    {
        case VG_DIALOG_ICON_INFO:
            return "ℹ";
        case VG_DIALOG_ICON_WARNING:
            return "⚠";
        case VG_DIALOG_ICON_ERROR:
            return "✗";
        case VG_DIALOG_ICON_QUESTION:
            return "?";
        default:
            return NULL;
    }
}

//=============================================================================
// Dialog Implementation
//=============================================================================

vg_dialog_t *vg_dialog_create(const char *title)
{
    vg_dialog_t *dlg = calloc(1, sizeof(vg_dialog_t));
    if (!dlg)
        return NULL;

    // Initialize base widget
    vg_widget_init(&dlg->base, VG_WIDGET_DIALOG, &g_dialog_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Title bar
    dlg->title = title ? strdup(title) : NULL;
    dlg->show_close_button = true;
    dlg->draggable = true;

    // Content
    dlg->content = NULL;
    dlg->icon = VG_DIALOG_ICON_NONE;
    dlg->custom_icon.type = VG_ICON_NONE;
    dlg->message = NULL;

    // Buttons
    dlg->button_preset = VG_DIALOG_BUTTONS_OK;
    dlg->custom_buttons = NULL;
    dlg->custom_button_count = 0;

    // Sizing
    dlg->min_width = DIALOG_DEFAULT_MIN_WIDTH;
    dlg->min_height = DIALOG_DEFAULT_MIN_HEIGHT;
    dlg->max_width = DIALOG_DEFAULT_MAX_WIDTH;
    dlg->max_height = DIALOG_DEFAULT_MAX_HEIGHT;
    dlg->resizable = false;

    // Modal
    dlg->modal = true;
    dlg->modal_parent = NULL;

    // Font
    dlg->font = NULL;
    dlg->font_size = theme->typography.size_normal;
    dlg->title_font_size = theme->typography.size_normal;

    // Colors
    dlg->bg_color = theme->colors.bg_primary;
    dlg->title_bg_color = theme->colors.bg_tertiary;
    dlg->title_text_color = theme->colors.fg_primary;
    dlg->text_color = theme->colors.fg_primary;
    dlg->button_bg_color = theme->colors.bg_secondary;
    dlg->button_hover_color = theme->colors.bg_hover;
    dlg->overlay_color = 0x80000000; // Semi-transparent black

    // State
    dlg->result = VG_DIALOG_RESULT_NONE;
    dlg->is_open = false;
    dlg->is_dragging = false;
    dlg->drag_offset_x = 0;
    dlg->drag_offset_y = 0;
    dlg->hovered_button = -1;

    // Callbacks
    dlg->user_data = NULL;
    dlg->on_result = NULL;
    dlg->on_close = NULL;

    // Set size constraints
    dlg->base.constraints.min_width = (float)dlg->min_width;
    dlg->base.constraints.min_height = (float)dlg->min_height;

    return dlg;
}

static void dialog_destroy(vg_widget_t *widget)
{
    vg_dialog_t *dlg = (vg_dialog_t *)widget;

    free(dlg->title);
    free(dlg->message);
    vg_icon_destroy(&dlg->custom_icon);

    if (dlg->custom_buttons)
    {
        for (size_t i = 0; i < dlg->custom_button_count; i++)
        {
            free(dlg->custom_buttons[i].label);
        }
        free(dlg->custom_buttons);
    }
}

static void dialog_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_dialog_t *dlg = (vg_dialog_t *)widget;
    (void)available_width;
    (void)available_height;

    // Calculate content size
    float content_width = 0;
    float content_height = 0;

    if (dlg->content)
    {
        // Measure content widget
        vg_widget_measure(dlg->content,
                          dlg->max_width - DIALOG_CONTENT_PADDING * 2,
                          dlg->max_height - DIALOG_TITLE_BAR_HEIGHT - DIALOG_BUTTON_BAR_HEIGHT -
                              DIALOG_CONTENT_PADDING * 2);
        content_width = dlg->content->measured_width;
        content_height = dlg->content->measured_height;
    }
    else if (dlg->message && dlg->font)
    {
        // Measure message text
        vg_text_metrics_t metrics;
        vg_font_measure_text(dlg->font, dlg->font_size, dlg->message, &metrics);
        content_width = metrics.width;
        content_height = metrics.height;

        // Add space for icon if present
        if (dlg->icon != VG_DIALOG_ICON_NONE || dlg->custom_icon.type != VG_ICON_NONE)
        {
            content_width += DIALOG_ICON_SIZE + DIALOG_CONTENT_PADDING;
        }
    }

    // Calculate button bar width
    float buttons_width = 0;
    const preset_button_t *preset_buttons;
    size_t button_count;
    get_preset_buttons(dlg->button_preset, &preset_buttons, &button_count);

    if (dlg->button_preset == VG_DIALOG_BUTTONS_CUSTOM && dlg->custom_buttons)
    {
        for (size_t i = 0; i < dlg->custom_button_count; i++)
        {
            buttons_width +=
                get_button_width(dlg, dlg->custom_buttons[i].label) + DIALOG_BUTTON_PADDING;
        }
    }
    else if (preset_buttons)
    {
        for (size_t i = 0; i < button_count; i++)
        {
            buttons_width += get_button_width(dlg, preset_buttons[i].label) + DIALOG_BUTTON_PADDING;
        }
    }

    // Calculate total size
    float total_width = content_width + DIALOG_CONTENT_PADDING * 2;
    if (buttons_width + DIALOG_CONTENT_PADDING * 2 > total_width)
    {
        total_width = buttons_width + DIALOG_CONTENT_PADDING * 2;
    }

    float total_height = DIALOG_TITLE_BAR_HEIGHT + content_height + DIALOG_CONTENT_PADDING * 2 +
                         DIALOG_BUTTON_BAR_HEIGHT;

    // Apply constraints
    if (total_width < dlg->min_width)
        total_width = (float)dlg->min_width;
    if (total_width > dlg->max_width)
        total_width = (float)dlg->max_width;
    if (total_height < dlg->min_height)
        total_height = (float)dlg->min_height;
    if (total_height > dlg->max_height)
        total_height = (float)dlg->max_height;

    widget->measured_width = total_width;
    widget->measured_height = total_height;
}

static void dialog_arrange(vg_widget_t *widget, float x, float y, float width, float height)
{
    vg_dialog_t *dlg = (vg_dialog_t *)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    // Arrange content widget if present
    if (dlg->content)
    {
        float content_x = x + DIALOG_CONTENT_PADDING;
        float content_y = y + DIALOG_TITLE_BAR_HEIGHT + DIALOG_CONTENT_PADDING;
        float content_w = width - DIALOG_CONTENT_PADDING * 2;
        float content_h = height - DIALOG_TITLE_BAR_HEIGHT - DIALOG_BUTTON_BAR_HEIGHT -
                          DIALOG_CONTENT_PADDING * 2;

        // Account for icon space
        if (dlg->icon != VG_DIALOG_ICON_NONE || dlg->custom_icon.type != VG_ICON_NONE)
        {
            content_x += DIALOG_ICON_SIZE + DIALOG_CONTENT_PADDING;
            content_w -= DIALOG_ICON_SIZE + DIALOG_CONTENT_PADDING;
        }

        vg_widget_arrange(dlg->content, content_x, content_y, content_w, content_h);
    }
}

static void dialog_paint(vg_widget_t *widget, void *canvas)
{
    vg_dialog_t *dlg = (vg_dialog_t *)widget;

    if (!dlg->is_open)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;
    int32_t x = (int32_t)widget->x;
    int32_t y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width;
    int32_t h = (int32_t)widget->height;

    // Dialog background
    vgfx_fill_rect(win, x, y, w, h, dlg->bg_color);

    // Title bar background
    vgfx_fill_rect(win, x, y, w, DIALOG_TITLE_BAR_HEIGHT, dlg->title_bg_color);

    // Separator between title bar and content
    vgfx_fill_rect(win, x, y + DIALOG_TITLE_BAR_HEIGHT - 1, w, 1, 0x00505050);

    // Outer border
    vgfx_rect(win, x, y, w, h, 0x00505050);

    // Title text
    if (dlg->title && dlg->font)
    {
        float title_x = (float)x + DIALOG_CONTENT_PADDING;
        float title_y = (float)y + DIALOG_TITLE_BAR_HEIGHT / 2.0f + dlg->title_font_size / 3.0f;
        vg_font_draw_text(canvas, dlg->font, dlg->title_font_size, title_x, title_y,
                          dlg->title, dlg->title_text_color);
    }

    // Close button — draw X glyph
    if (dlg->show_close_button && dlg->font)
    {
        float close_cx = (float)x + (float)w - (float)DIALOG_CLOSE_BUTTON_SIZE / 2.0f - 4.0f;
        float close_cy = (float)y + DIALOG_TITLE_BAR_HEIGHT / 2.0f + dlg->font_size / 3.0f;
        vg_font_draw_text(canvas, dlg->font, dlg->font_size,
                          close_cx - dlg->font_size / 4.0f, close_cy,
                          "X", dlg->title_text_color);
    }

    // Content area
    float content_x = (float)x + DIALOG_CONTENT_PADDING;
    float content_y = (float)y + DIALOG_TITLE_BAR_HEIGHT + DIALOG_CONTENT_PADDING;

    // Icon glyph
    if (dlg->icon != VG_DIALOG_ICON_NONE)
    {
        const char *glyph = get_icon_glyph(dlg->icon);
        if (glyph && dlg->font)
        {
            vg_font_draw_text(canvas, dlg->font, (float)DIALOG_ICON_SIZE,
                              content_x, content_y + (float)DIALOG_ICON_SIZE * 0.8f,
                              glyph, dlg->text_color);
        }
        content_x += DIALOG_ICON_SIZE + DIALOG_CONTENT_PADDING;
    }

    // Message text
    if (dlg->message && dlg->font)
    {
        vg_font_draw_text(canvas, dlg->font, dlg->font_size,
                          content_x, content_y + dlg->font_size,
                          dlg->message, dlg->text_color);
    }

    // Content widget
    if (dlg->content)
    {
        vg_widget_paint(dlg->content, canvas);
    }

    // Button bar background + separator
    int32_t btn_bar_y = y + h - DIALOG_BUTTON_BAR_HEIGHT;
    vgfx_fill_rect(win, x, btn_bar_y, w, DIALOG_BUTTON_BAR_HEIGHT, dlg->title_bg_color);
    vgfx_fill_rect(win, x, btn_bar_y, w, 1, 0x00505050);

    float button_y = (float)btn_bar_y + (DIALOG_BUTTON_BAR_HEIGHT - DIALOG_BUTTON_HEIGHT) / 2.0f;
    float button_x = (float)x + (float)w - DIALOG_CONTENT_PADDING;

    const preset_button_t *buttons;
    size_t button_count;
    get_preset_buttons(dlg->button_preset, &buttons, &button_count);

    if (dlg->button_preset == VG_DIALOG_BUTTONS_CUSTOM && dlg->custom_buttons)
    {
        for (int i = (int)dlg->custom_button_count - 1; i >= 0; i--)
        {
            vg_dialog_button_def_t *btn = &dlg->custom_buttons[i];
            float btn_w = get_button_width(dlg, btn->label);
            button_x -= btn_w;

            uint32_t btn_bg =
                (dlg->hovered_button == i) ? dlg->button_hover_color : dlg->button_bg_color;
            vgfx_fill_rect(win, (int32_t)button_x, (int32_t)button_y,
                           (int32_t)btn_w, DIALOG_BUTTON_HEIGHT, btn_bg);
            vgfx_rect(win, (int32_t)button_x, (int32_t)button_y,
                      (int32_t)btn_w, DIALOG_BUTTON_HEIGHT, 0x00505050);

            if (btn->label && dlg->font)
            {
                vg_text_metrics_t metrics;
                vg_font_measure_text(dlg->font, dlg->font_size, btn->label, &metrics);
                float text_x = button_x + (btn_w - metrics.width) / 2.0f;
                float text_y = button_y + DIALOG_BUTTON_HEIGHT / 2.0f + dlg->font_size / 3.0f;
                vg_font_draw_text(canvas, dlg->font, dlg->font_size, text_x, text_y,
                                  btn->label, dlg->text_color);
            }

            button_x -= DIALOG_BUTTON_PADDING;
        }
    }
    else if (buttons)
    {
        for (int i = (int)button_count - 1; i >= 0; i--)
        {
            const preset_button_t *btn = &buttons[i];
            float btn_w = get_button_width(dlg, btn->label);
            button_x -= btn_w;

            uint32_t btn_bg =
                (dlg->hovered_button == i) ? dlg->button_hover_color : dlg->button_bg_color;
            vgfx_fill_rect(win, (int32_t)button_x, (int32_t)button_y,
                           (int32_t)btn_w, DIALOG_BUTTON_HEIGHT, btn_bg);
            vgfx_rect(win, (int32_t)button_x, (int32_t)button_y,
                      (int32_t)btn_w, DIALOG_BUTTON_HEIGHT, 0x00505050);

            if (btn->label && dlg->font)
            {
                vg_text_metrics_t metrics;
                vg_font_measure_text(dlg->font, dlg->font_size, btn->label, &metrics);
                float text_x = button_x + (btn_w - metrics.width) / 2.0f;
                float text_y = button_y + DIALOG_BUTTON_HEIGHT / 2.0f + dlg->font_size / 3.0f;
                vg_font_draw_text(canvas, dlg->font, dlg->font_size, text_x, text_y,
                                  btn->label, dlg->text_color);
            }

            button_x -= DIALOG_BUTTON_PADDING;
        }
    }
}

static int find_button_at(vg_dialog_t *dlg, float px, float py)
{
    float x = dlg->base.x;
    float y = dlg->base.y;
    float w = dlg->base.width;
    float h = dlg->base.height;

    // Check if in button bar area
    float button_bar_y = y + h - DIALOG_BUTTON_BAR_HEIGHT;
    if (py < button_bar_y || py > y + h)
        return -1;

    // Calculate button positions right-to-left
    float button_y = button_bar_y + (DIALOG_BUTTON_BAR_HEIGHT - DIALOG_BUTTON_HEIGHT) / 2;
    float button_x = x + w - DIALOG_CONTENT_PADDING;

    const preset_button_t *buttons;
    size_t button_count;
    get_preset_buttons(dlg->button_preset, &buttons, &button_count);

    if (dlg->button_preset == VG_DIALOG_BUTTONS_CUSTOM && dlg->custom_buttons)
    {
        for (int i = (int)dlg->custom_button_count - 1; i >= 0; i--)
        {
            float btn_w = get_button_width(dlg, dlg->custom_buttons[i].label);
            button_x -= btn_w;

            if (px >= button_x && px < button_x + btn_w && py >= button_y &&
                py < button_y + DIALOG_BUTTON_HEIGHT)
            {
                return i;
            }

            button_x -= DIALOG_BUTTON_PADDING;
        }
    }
    else if (buttons)
    {
        for (int i = (int)button_count - 1; i >= 0; i--)
        {
            float btn_w = get_button_width(dlg, buttons[i].label);
            button_x -= btn_w;

            if (px >= button_x && px < button_x + btn_w && py >= button_y &&
                py < button_y + DIALOG_BUTTON_HEIGHT)
            {
                return i;
            }

            button_x -= DIALOG_BUTTON_PADDING;
        }
    }

    return -1;
}

static bool is_in_title_bar(vg_dialog_t *dlg, float px, float py)
{
    float x = dlg->base.x;
    float y = dlg->base.y;
    float w = dlg->base.width;

    return px >= x && px < x + w && py >= y && py < y + DIALOG_TITLE_BAR_HEIGHT;
}

static bool is_on_close_button(vg_dialog_t *dlg, float px, float py)
{
    if (!dlg->show_close_button)
        return false;

    float x = dlg->base.x + dlg->base.width - DIALOG_CLOSE_BUTTON_SIZE - 4;
    float y = dlg->base.y + (DIALOG_TITLE_BAR_HEIGHT - DIALOG_CLOSE_BUTTON_SIZE) / 2;

    return px >= x && px < x + DIALOG_CLOSE_BUTTON_SIZE && py >= y &&
           py < y + DIALOG_CLOSE_BUTTON_SIZE;
}

static void trigger_button_click(vg_dialog_t *dlg, int button_index)
{
    vg_dialog_result_t result = VG_DIALOG_RESULT_NONE;

    if (dlg->button_preset == VG_DIALOG_BUTTONS_CUSTOM && dlg->custom_buttons)
    {
        if (button_index >= 0 && button_index < (int)dlg->custom_button_count)
        {
            result = dlg->custom_buttons[button_index].result;
        }
    }
    else
    {
        const preset_button_t *buttons;
        size_t button_count;
        get_preset_buttons(dlg->button_preset, &buttons, &button_count);
        if (buttons && button_index >= 0 && button_index < (int)button_count)
        {
            result = buttons[button_index].result;
        }
    }

    if (result != VG_DIALOG_RESULT_NONE)
    {
        vg_dialog_close(dlg, result);
    }
}

static bool dialog_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_dialog_t *dlg = (vg_dialog_t *)widget;

    if (!dlg->is_open)
        return false;

    switch (event->type)
    {
        case VG_EVENT_MOUSE_MOVE:
        {
            float px = event->mouse.x;
            float py = event->mouse.y;

            // Handle dragging
            if (dlg->is_dragging)
            {
                widget->x = px - dlg->drag_offset_x;
                widget->y = py - dlg->drag_offset_y;
                widget->needs_paint = true;
                return true;
            }

            // Update hovered button
            int new_hovered = find_button_at(dlg, px, py);
            if (new_hovered != dlg->hovered_button)
            {
                dlg->hovered_button = new_hovered;
                widget->needs_paint = true;
            }

            return dlg->modal; // Consume if modal
        }

        case VG_EVENT_MOUSE_DOWN:
        {
            float px = event->mouse.x;
            float py = event->mouse.y;

            // Check close button
            if (is_on_close_button(dlg, px, py))
            {
                vg_dialog_close(dlg, VG_DIALOG_RESULT_CANCEL);
                return true;
            }

            // Check button click
            int button = find_button_at(dlg, px, py);
            if (button >= 0)
            {
                trigger_button_click(dlg, button);
                return true;
            }

            // Start dragging
            if (dlg->draggable && is_in_title_bar(dlg, px, py))
            {
                dlg->is_dragging = true;
                dlg->drag_offset_x = (int)(px - widget->x);
                dlg->drag_offset_y = (int)(py - widget->y);
                return true;
            }

            return dlg->modal;
        }

        case VG_EVENT_MOUSE_UP:
            dlg->is_dragging = false;
            return dlg->modal;

        case VG_EVENT_KEY_DOWN:
            // Handle Enter (default button) and Escape (cancel button)
            if (event->key.key == VG_KEY_ENTER)
            {
                // Find default button
                if (dlg->button_preset == VG_DIALOG_BUTTONS_CUSTOM && dlg->custom_buttons)
                {
                    for (size_t i = 0; i < dlg->custom_button_count; i++)
                    {
                        if (dlg->custom_buttons[i].is_default)
                        {
                            trigger_button_click(dlg, (int)i);
                            return true;
                        }
                    }
                }
                else
                {
                    const preset_button_t *buttons;
                    size_t button_count;
                    get_preset_buttons(dlg->button_preset, &buttons, &button_count);
                    if (buttons)
                    {
                        for (size_t i = 0; i < button_count; i++)
                        {
                            if (buttons[i].is_default)
                            {
                                trigger_button_click(dlg, (int)i);
                                return true;
                            }
                        }
                    }
                }
            }
            else if (event->key.key == VG_KEY_ESCAPE)
            {
                // Find cancel button
                if (dlg->button_preset == VG_DIALOG_BUTTONS_CUSTOM && dlg->custom_buttons)
                {
                    for (size_t i = 0; i < dlg->custom_button_count; i++)
                    {
                        if (dlg->custom_buttons[i].is_cancel)
                        {
                            trigger_button_click(dlg, (int)i);
                            return true;
                        }
                    }
                }
                else
                {
                    const preset_button_t *buttons;
                    size_t button_count;
                    get_preset_buttons(dlg->button_preset, &buttons, &button_count);
                    if (buttons)
                    {
                        for (size_t i = 0; i < button_count; i++)
                        {
                            if (buttons[i].is_cancel)
                            {
                                trigger_button_click(dlg, (int)i);
                                return true;
                            }
                        }
                    }
                }
                // If no cancel button, just close
                vg_dialog_close(dlg, VG_DIALOG_RESULT_CANCEL);
                return true;
            }
            return dlg->modal;

        default:
            return dlg->modal;
    }
}

//=============================================================================
// Dialog API
//=============================================================================

void vg_dialog_set_title(vg_dialog_t *dialog, const char *title)
{
    if (!dialog)
        return;
    free(dialog->title);
    dialog->title = title ? strdup(title) : NULL;
    dialog->base.needs_paint = true;
}

void vg_dialog_set_content(vg_dialog_t *dialog, vg_widget_t *content)
{
    if (!dialog)
        return;
    dialog->content = content;
    dialog->base.needs_layout = true;
}

void vg_dialog_set_message(vg_dialog_t *dialog, const char *message)
{
    if (!dialog)
        return;
    free(dialog->message);
    dialog->message = message ? strdup(message) : NULL;
    dialog->base.needs_layout = true;
}

void vg_dialog_set_icon(vg_dialog_t *dialog, vg_dialog_icon_t icon)
{
    if (!dialog)
        return;
    dialog->icon = icon;
    dialog->base.needs_layout = true;
}

void vg_dialog_set_custom_icon(vg_dialog_t *dialog, vg_icon_t icon)
{
    if (!dialog)
        return;
    vg_icon_destroy(&dialog->custom_icon);
    dialog->custom_icon = icon;
    dialog->base.needs_layout = true;
}

void vg_dialog_set_buttons(vg_dialog_t *dialog, vg_dialog_buttons_t buttons)
{
    if (!dialog)
        return;
    dialog->button_preset = buttons;
    dialog->base.needs_layout = true;
}

void vg_dialog_set_custom_buttons(vg_dialog_t *dialog,
                                  vg_dialog_button_def_t *buttons,
                                  size_t count)
{
    if (!dialog)
        return;

    // Free existing custom buttons
    if (dialog->custom_buttons)
    {
        for (size_t i = 0; i < dialog->custom_button_count; i++)
        {
            free(dialog->custom_buttons[i].label);
        }
        free(dialog->custom_buttons);
    }

    // Copy new buttons
    dialog->custom_buttons = calloc(count, sizeof(vg_dialog_button_def_t));
    if (dialog->custom_buttons)
    {
        for (size_t i = 0; i < count; i++)
        {
            dialog->custom_buttons[i].label = buttons[i].label ? strdup(buttons[i].label) : NULL;
            dialog->custom_buttons[i].result = buttons[i].result;
            dialog->custom_buttons[i].is_default = buttons[i].is_default;
            dialog->custom_buttons[i].is_cancel = buttons[i].is_cancel;
        }
        dialog->custom_button_count = count;
        dialog->button_preset = VG_DIALOG_BUTTONS_CUSTOM;
    }

    dialog->base.needs_layout = true;
}

void vg_dialog_set_resizable(vg_dialog_t *dialog, bool resizable)
{
    if (!dialog)
        return;
    dialog->resizable = resizable;
}

void vg_dialog_set_size_constraints(
    vg_dialog_t *dialog, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h)
{
    if (!dialog)
        return;
    dialog->min_width = min_w;
    dialog->min_height = min_h;
    dialog->max_width = max_w;
    dialog->max_height = max_h;
    dialog->base.constraints.min_width = (float)min_w;
    dialog->base.constraints.min_height = (float)min_h;
    dialog->base.needs_layout = true;
}

void vg_dialog_set_modal(vg_dialog_t *dialog, bool modal, vg_widget_t *parent)
{
    if (!dialog)
        return;
    dialog->modal = modal;
    dialog->modal_parent = parent;
}

void vg_dialog_show(vg_dialog_t *dialog)
{
    if (!dialog)
        return;
    dialog->is_open = true;
    dialog->result = VG_DIALOG_RESULT_NONE;
    dialog->base.needs_layout = true;
    dialog->base.needs_paint = true;
}

void vg_dialog_show_centered(vg_dialog_t *dialog, vg_widget_t *relative_to)
{
    if (!dialog)
        return;

    vg_dialog_show(dialog);

    // Measure first to get dialog size
    vg_widget_measure(&dialog->base, 0, 0);

    // Center on relative widget
    float center_x, center_y;
    if (relative_to)
    {
        center_x = relative_to->x + relative_to->width / 2;
        center_y = relative_to->y + relative_to->height / 2;
    }
    else
    {
        // Center on screen (assume 800x600 default)
        center_x = 400;
        center_y = 300;
    }

    dialog->base.x = center_x - dialog->base.measured_width / 2;
    dialog->base.y = center_y - dialog->base.measured_height / 2;

    vg_widget_arrange(&dialog->base,
                      dialog->base.x,
                      dialog->base.y,
                      dialog->base.measured_width,
                      dialog->base.measured_height);
}

void vg_dialog_hide(vg_dialog_t *dialog)
{
    if (!dialog)
        return;
    dialog->is_open = false;
}

void vg_dialog_close(vg_dialog_t *dialog, vg_dialog_result_t result)
{
    if (!dialog)
        return;

    dialog->result = result;
    dialog->is_open = false;

    if (dialog->on_result)
    {
        dialog->on_result(dialog, result, dialog->user_data);
    }

    if (dialog->on_close)
    {
        dialog->on_close(dialog, dialog->user_data);
    }
}

vg_dialog_result_t vg_dialog_get_result(vg_dialog_t *dialog)
{
    if (!dialog)
        return VG_DIALOG_RESULT_NONE;
    return dialog->result;
}

bool vg_dialog_is_open(vg_dialog_t *dialog)
{
    if (!dialog)
        return false;
    return dialog->is_open;
}

void vg_dialog_set_on_result(vg_dialog_t *dialog,
                             void (*callback)(vg_dialog_t *, vg_dialog_result_t, void *),
                             void *user_data)
{
    if (!dialog)
        return;
    dialog->on_result = callback;
    dialog->user_data = user_data;
}

void vg_dialog_set_on_close(vg_dialog_t *dialog,
                            void (*callback)(vg_dialog_t *, void *),
                            void *user_data)
{
    if (!dialog)
        return;
    dialog->on_close = callback;
    if (!dialog->on_result)
    {
        dialog->user_data = user_data;
    }
}

void vg_dialog_set_font(vg_dialog_t *dialog, vg_font_t *font, float size)
{
    if (!dialog)
        return;
    dialog->font = font;
    dialog->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    dialog->title_font_size = dialog->font_size;
    dialog->base.needs_layout = true;
}

//=============================================================================
// Convenience Constructors
//=============================================================================

vg_dialog_t *vg_dialog_message(const char *title,
                               const char *message,
                               vg_dialog_icon_t icon,
                               vg_dialog_buttons_t buttons)
{
    vg_dialog_t *dlg = vg_dialog_create(title);
    if (!dlg)
        return NULL;

    vg_dialog_set_message(dlg, message);
    vg_dialog_set_icon(dlg, icon);
    vg_dialog_set_buttons(dlg, buttons);

    return dlg;
}

typedef struct
{
    void (*callback)(void *);
    void *user_data;
} confirm_data_t;

static void confirm_result_handler(vg_dialog_t *dialog, vg_dialog_result_t result, void *data)
{
    confirm_data_t *cd = (confirm_data_t *)data;
    if (result == VG_DIALOG_RESULT_YES && cd && cd->callback)
    {
        cd->callback(cd->user_data);
    }
    free(cd);
    (void)dialog;
}

vg_dialog_t *vg_dialog_confirm(const char *title,
                               const char *message,
                               void (*on_confirm)(void *),
                               void *user_data)
{
    vg_dialog_t *dlg = vg_dialog_create(title);
    if (!dlg)
        return NULL;

    vg_dialog_set_message(dlg, message);
    vg_dialog_set_icon(dlg, VG_DIALOG_ICON_QUESTION);
    vg_dialog_set_buttons(dlg, VG_DIALOG_BUTTONS_YES_NO);

    // Set up callback wrapper
    confirm_data_t *cd = malloc(sizeof(confirm_data_t));
    if (cd)
    {
        cd->callback = on_confirm;
        cd->user_data = user_data;
        vg_dialog_set_on_result(dlg, confirm_result_handler, cd);
    }

    return dlg;
}
