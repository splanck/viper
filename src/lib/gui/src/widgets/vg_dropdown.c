// vg_dropdown.c - Dropdown/ComboBox widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void dropdown_destroy(vg_widget_t *widget);
static void dropdown_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void dropdown_paint(vg_widget_t *widget, void *canvas);
static bool dropdown_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool dropdown_can_focus(vg_widget_t *widget);

//=============================================================================
// Dropdown VTable
//=============================================================================

static vg_widget_vtable_t g_dropdown_vtable = {.destroy = dropdown_destroy,
                                               .measure = dropdown_measure,
                                               .arrange = NULL,
                                               .paint = dropdown_paint,
                                               .handle_event = dropdown_handle_event,
                                               .can_focus = dropdown_can_focus,
                                               .on_focus = NULL};

static void dropdown_destroy(vg_widget_t *widget)
{
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;
    for (int i = 0; i < dd->item_count; i++)
        free(dd->items[i]);
    free(dd->items);
    dd->items = NULL;
    dd->item_count = 0;
    dd->item_capacity = 0;
}

static void dropdown_measure(vg_widget_t *widget, float avail_w, float avail_h)
{
    vg_theme_t *theme = vg_theme_get_current();
    (void)avail_w;
    (void)avail_h;

    widget->measured_width = 140.0f;
    widget->measured_height = theme ? theme->button.height : 28.0f;
}

// Height of one item row in the dropdown panel
static float dropdown_item_height(vg_dropdown_t *dd)
{
    return dd->font_size > 0 ? (dd->font_size * 1.6f) : 24.0f;
}

static void dropdown_paint(vg_widget_t *widget, void *canvas)
{
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    int32_t x = (int32_t)widget->x;
    int32_t y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width;
    int32_t h = (int32_t)widget->height;

    uint32_t bg = (widget->state & VG_STATE_HOVERED) ? theme->colors.bg_hover : dd->bg_color;

    // Header box
    vgfx_fill_rect(win, x, y, w, h, bg);
    vgfx_rect(win,
              x,
              y,
              w,
              h,
              (widget->state & VG_STATE_FOCUSED) ? theme->colors.border_focus : dd->border_color);

    // Selected text or placeholder
    const char *label = (dd->selected_index >= 0 && dd->selected_index < dd->item_count)
                            ? dd->items[dd->selected_index]
                            : dd->placeholder;
    if (label && dd->font)
    {
        float ty = widget->y + widget->height * 0.5f + dd->font_size * 0.35f;
        vg_font_draw_text(canvas, dd->font, dd->font_size, widget->x + 6.0f, ty, label,
                          label == dd->placeholder ? theme->colors.fg_secondary : dd->text_color);
    }

    // Down-arrow (simple triangle via two lines)
    float ax = widget->x + widget->width - dd->arrow_size - 4.0f;
    float ay = widget->y + widget->height / 2.0f;
    float as2 = dd->arrow_size / 2.0f;
    vgfx_line(win, (int32_t)(ax), (int32_t)(ay - as2 / 2),
              (int32_t)(ax + as2), (int32_t)(ay + as2 / 2), dd->text_color);
    vgfx_line(win, (int32_t)(ax + as2), (int32_t)(ay + as2 / 2),
              (int32_t)(ax + dd->arrow_size), (int32_t)(ay - as2 / 2), dd->text_color);

    // Draw open panel below header
    if (dd->open && dd->item_count > 0)
    {
        float ih = dropdown_item_height(dd);
        float panel_h = ih * dd->item_count;
        if (panel_h > dd->dropdown_height)
            panel_h = dd->dropdown_height;

        int32_t px = x;
        int32_t py = y + h;
        int32_t pw = w;
        int32_t ph = (int32_t)panel_h;

        vgfx_fill_rect(win, px, py, pw, ph, dd->dropdown_bg);
        vgfx_rect(win, px, py, pw, ph, dd->border_color);

        int visible_count = (int)(panel_h / ih);
        int start_item = (int)(dd->scroll_y / ih);
        if (start_item < 0)
            start_item = 0;

        for (int i = start_item; i < dd->item_count && i < start_item + visible_count + 1; i++)
        {
            float iy = py + (i - start_item) * ih;
            int32_t iy32 = (int32_t)iy;

            if (i == dd->hovered_index)
                vgfx_fill_rect(win, px + 1, iy32, pw - 2, (int32_t)ih, dd->hover_bg);
            else if (i == dd->selected_index)
                vgfx_fill_rect(win, px + 1, iy32, pw - 2, (int32_t)ih, dd->selected_bg);

            if (dd->items[i] && dd->font)
            {
                float ty2 = iy + ih * 0.7f;
                vg_font_draw_text(canvas, dd->font, dd->font_size, (float)(px + 6), ty2,
                                  dd->items[i], dd->text_color);
            }
        }
    }
}

static bool dropdown_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_dropdown_t *dd = (vg_dropdown_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    (void)theme;

    if (!widget->enabled)
        return false;

    switch (event->type)
    {
        case VG_EVENT_CLICK:
        {
            if (!dd->open)
            {
                dd->open = true;
                dd->hovered_index = dd->selected_index;
                vg_widget_set_input_capture(widget);
            }
            else
            {
                // Check if click is inside the panel
                float ih = dropdown_item_height(dd);
                float panel_top = widget->y + widget->height;
                float rel_y = event->mouse.screen_y - panel_top + dd->scroll_y;
                if (rel_y >= 0)
                {
                    int idx = (int)(rel_y / ih);
                    if (idx >= 0 && idx < dd->item_count)
                    {
                        vg_dropdown_set_selected(dd, idx);
                    }
                }
                dd->open = false;
                dd->hovered_index = -1;
                vg_widget_release_input_capture();
            }
            widget->needs_paint = true;
            event->handled = true;
            return true;
        }

        case VG_EVENT_MOUSE_MOVE:
            if (dd->open)
            {
                float ih = dropdown_item_height(dd);
                float panel_top = widget->y + widget->height;
                float rel_y = event->mouse.screen_y - panel_top + dd->scroll_y;
                dd->hovered_index = (rel_y >= 0) ? (int)(rel_y / ih) : -1;
                if (dd->hovered_index >= dd->item_count)
                    dd->hovered_index = -1;
                widget->needs_paint = true;
                return true;
            }
            return false;

        case VG_EVENT_KEY_DOWN:
            if (!dd->open)
                return false;
            if (event->key.key == VG_KEY_ESCAPE)
            {
                dd->open = false;
                dd->hovered_index = -1;
                vg_widget_release_input_capture();
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            if (event->key.key == VG_KEY_DOWN)
            {
                if (dd->hovered_index < dd->item_count - 1)
                    dd->hovered_index++;
                widget->needs_paint = true;
                return true;
            }
            if (event->key.key == VG_KEY_UP)
            {
                if (dd->hovered_index > 0)
                    dd->hovered_index--;
                widget->needs_paint = true;
                return true;
            }
            if (event->key.key == VG_KEY_ENTER)
            {
                if (dd->hovered_index >= 0 && dd->hovered_index < dd->item_count)
                    vg_dropdown_set_selected(dd, dd->hovered_index);
                dd->open = false;
                dd->hovered_index = -1;
                vg_widget_release_input_capture();
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            return false;

        default:
            return false;
    }
}

static bool dropdown_can_focus(vg_widget_t *widget)
{
    return widget->enabled && widget->visible;
}

vg_dropdown_t *vg_dropdown_create(vg_widget_t *parent)
{
    vg_dropdown_t *dropdown = calloc(1, sizeof(vg_dropdown_t));
    if (!dropdown)
        return NULL;

    vg_widget_init(&dropdown->base, VG_WIDGET_DROPDOWN, &g_dropdown_vtable);
    dropdown->selected_index = -1;
    dropdown->hovered_index = -1;
    dropdown->item_capacity = 8;
    dropdown->items = calloc(dropdown->item_capacity, sizeof(char *));

    // Default appearance
    dropdown->font_size = 14;
    dropdown->dropdown_height = 200;
    dropdown->arrow_size = 12;
    dropdown->bg_color = 0xFF3C3C3C;
    dropdown->text_color = 0xFFCCCCCC;
    dropdown->border_color = 0xFF5A5A5A;
    dropdown->dropdown_bg = 0xFF252526;
    dropdown->hover_bg = 0xFF094771;
    dropdown->selected_bg = 0xFF094771;

    if (parent)
    {
        vg_widget_add_child(parent, &dropdown->base);
    }

    return dropdown;
}

int vg_dropdown_add_item(vg_dropdown_t *dropdown, const char *text)
{
    if (!dropdown || !text)
        return -1;

    // Grow array if needed
    if (dropdown->item_count >= dropdown->item_capacity)
    {
        int new_cap = dropdown->item_capacity * 2;
        char **new_items = realloc(dropdown->items, new_cap * sizeof(char *));
        if (!new_items)
            return -1;
        dropdown->items = new_items;
        dropdown->item_capacity = new_cap;
    }

    dropdown->items[dropdown->item_count] = strdup(text);
    return dropdown->item_count++;
}

void vg_dropdown_remove_item(vg_dropdown_t *dropdown, int index)
{
    if (!dropdown || index < 0 || index >= dropdown->item_count)
        return;

    free(dropdown->items[index]);

    // Shift remaining items
    for (int i = index; i < dropdown->item_count - 1; i++)
    {
        dropdown->items[i] = dropdown->items[i + 1];
    }
    dropdown->item_count--;

    // Adjust selected index
    if (dropdown->selected_index == index)
    {
        dropdown->selected_index = -1;
    }
    else if (dropdown->selected_index > index)
    {
        dropdown->selected_index--;
    }
}

void vg_dropdown_clear(vg_dropdown_t *dropdown)
{
    if (!dropdown)
        return;

    for (int i = 0; i < dropdown->item_count; i++)
    {
        free(dropdown->items[i]);
    }
    dropdown->item_count = 0;
    dropdown->selected_index = -1;
}

void vg_dropdown_set_selected(vg_dropdown_t *dropdown, int index)
{
    if (!dropdown)
        return;

    int old_index = dropdown->selected_index;
    if (index < -1 || index >= dropdown->item_count)
    {
        dropdown->selected_index = -1;
    }
    else
    {
        dropdown->selected_index = index;
    }

    if (old_index != dropdown->selected_index && dropdown->on_change)
    {
        dropdown->on_change(&dropdown->base,
                            dropdown->selected_index,
                            vg_dropdown_get_selected_text(dropdown),
                            dropdown->on_change_data);
    }
}

int vg_dropdown_get_selected(vg_dropdown_t *dropdown)
{
    return dropdown ? dropdown->selected_index : -1;
}

const char *vg_dropdown_get_selected_text(vg_dropdown_t *dropdown)
{
    if (!dropdown || dropdown->selected_index < 0 ||
        dropdown->selected_index >= dropdown->item_count)
    {
        return NULL;
    }
    return dropdown->items[dropdown->selected_index];
}

void vg_dropdown_set_placeholder(vg_dropdown_t *dropdown, const char *text)
{
    if (!dropdown)
        return;
    // Free existing placeholder if owned
    dropdown->placeholder = text; // Note: not copying, user must keep string alive
}

void vg_dropdown_set_font(vg_dropdown_t *dropdown, vg_font_t *font, float size)
{
    if (!dropdown)
        return;
    dropdown->font = font;
    dropdown->font_size = size;
}

void vg_dropdown_set_on_change(vg_dropdown_t *dropdown,
                               vg_dropdown_callback_t callback,
                               void *user_data)
{
    if (!dropdown)
        return;
    dropdown->on_change = callback;
    dropdown->on_change_data = user_data;
}
