// vg_breadcrumb.c - Breadcrumb widget implementation
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void breadcrumb_destroy(vg_widget_t *widget);
static void breadcrumb_measure(vg_widget_t *widget, float available_width, float available_height);
static void breadcrumb_paint(vg_widget_t *widget, void *canvas);
static bool breadcrumb_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// Breadcrumb VTable
//=============================================================================

static vg_widget_vtable_t g_breadcrumb_vtable = {.destroy = breadcrumb_destroy,
                                                 .measure = breadcrumb_measure,
                                                 .arrange = NULL,
                                                 .paint = breadcrumb_paint,
                                                 .handle_event = breadcrumb_handle_event,
                                                 .can_focus = NULL,
                                                 .on_focus = NULL};

//=============================================================================
// Breadcrumb Item Management
//=============================================================================

static void free_breadcrumb_item(vg_breadcrumb_item_t *item)
{
    if (!item)
        return;

    free(item->label);
    free(item->tooltip);

    for (size_t i = 0; i < item->dropdown_count; i++)
    {
        free(item->dropdown_items[i].label);
    }
    free(item->dropdown_items);
}

//=============================================================================
// Breadcrumb Implementation
//=============================================================================

vg_breadcrumb_t *vg_breadcrumb_create(void)
{
    vg_breadcrumb_t *bc = calloc(1, sizeof(vg_breadcrumb_t));
    if (!bc)
        return NULL;

    vg_widget_init(&bc->base, VG_WIDGET_CUSTOM, &g_breadcrumb_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Defaults
    bc->separator = strdup(">");
    bc->item_padding = 8;
    bc->separator_padding = 4;
    bc->bg_color = 0xFF252526;
    bc->text_color = 0xFFCCCCCC;
    bc->hover_bg = 0xFF2A2D2E;
    bc->separator_color = 0xFF808080;

    bc->font_size = theme->typography.size_normal;
    bc->hovered_index = -1;
    bc->dropdown_index = -1;
    bc->dropdown_hovered = -1;

    return bc;
}

static void breadcrumb_destroy(vg_widget_t *widget)
{
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;

    for (size_t i = 0; i < bc->item_count; i++)
    {
        free_breadcrumb_item(&bc->items[i]);
    }
    free(bc->items);
    free(bc->separator);
}

void vg_breadcrumb_destroy(vg_breadcrumb_t *bc)
{
    if (!bc)
        return;
    vg_widget_destroy(&bc->base);
}

static void breadcrumb_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;
    (void)available_width;
    (void)available_height;

    float width = 0;
    float height = 0;

    if (!bc->font)
    {
        widget->measured_width = 0;
        widget->measured_height = 24;
        return;
    }

    for (size_t i = 0; i < bc->item_count; i++)
    {
        vg_breadcrumb_item_t *item = &bc->items[i];

        vg_text_metrics_t metrics;
        vg_font_measure_text(bc->font, bc->font_size, item->label, &metrics);

        width += metrics.width + bc->item_padding * 2;
        if (metrics.height > height)
            height = metrics.height;

        // Add separator width
        if (i < bc->item_count - 1 && bc->separator)
        {
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            width += metrics.width + bc->separator_padding * 2;
        }
    }

    widget->measured_width = width;
    widget->measured_height = height + 8; // Vertical padding
}

static void breadcrumb_paint(vg_widget_t *widget, void *canvas)
{
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;

    // Draw background (placeholder)
    (void)bc->bg_color;

    if (!bc->font)
        return;

    float x = widget->x;
    float y = widget->y + 4; // Top padding

    for (size_t i = 0; i < bc->item_count; i++)
    {
        vg_breadcrumb_item_t *item = &bc->items[i];

        // Measure item
        vg_text_metrics_t metrics;
        vg_font_measure_text(bc->font, bc->font_size, item->label, &metrics);

        float item_width = metrics.width + bc->item_padding * 2;

        // Draw hover background
        if ((int)i == bc->hovered_index)
        {
            // Draw hover background (placeholder)
            (void)bc->hover_bg;
        }

        // Draw text
        vg_font_draw_text(
            canvas, bc->font, bc->font_size, x + bc->item_padding, y, item->label, bc->text_color);

        x += item_width;

        // Draw separator
        if (i < bc->item_count - 1 && bc->separator)
        {
            x += bc->separator_padding;
            vg_font_draw_text(
                canvas, bc->font, bc->font_size, x, y, bc->separator, bc->separator_color);
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            x += metrics.width + bc->separator_padding;
        }
    }

    // Draw dropdown if open
    if (bc->dropdown_open && bc->dropdown_index >= 0 && bc->dropdown_index < (int)bc->item_count)
    {
        vg_breadcrumb_item_t *item = &bc->items[bc->dropdown_index];
        // Dropdown rendering would go here
        (void)item;
    }
}

static int find_item_at(vg_breadcrumb_t *bc, float px, float py)
{
    (void)py;

    if (!bc->font)
        return -1;

    float x = bc->base.x;

    for (size_t i = 0; i < bc->item_count; i++)
    {
        vg_breadcrumb_item_t *item = &bc->items[i];

        vg_text_metrics_t metrics;
        vg_font_measure_text(bc->font, bc->font_size, item->label, &metrics);

        float item_width = metrics.width + bc->item_padding * 2;

        if (px >= x && px < x + item_width)
        {
            return (int)i;
        }

        x += item_width;

        // Skip separator
        if (i < bc->item_count - 1 && bc->separator)
        {
            vg_font_measure_text(bc->font, bc->font_size, bc->separator, &metrics);
            x += metrics.width + bc->separator_padding * 2;
        }
    }

    return -1;
}

static bool breadcrumb_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_breadcrumb_t *bc = (vg_breadcrumb_t *)widget;

    switch (event->type)
    {
        case VG_EVENT_MOUSE_MOVE:
        {
            int idx = find_item_at(bc, event->mouse.x, event->mouse.y);
            if (idx != bc->hovered_index)
            {
                bc->hovered_index = idx;
                bc->base.needs_paint = true;
            }
            return true;
        }

        case VG_EVENT_MOUSE_LEAVE:
            bc->hovered_index = -1;
            bc->base.needs_paint = true;
            return true;

        case VG_EVENT_CLICK:
        {
            int idx = find_item_at(bc, event->mouse.x, event->mouse.y);
            if (idx >= 0)
            {
                vg_breadcrumb_item_t *item = &bc->items[idx];

                // Check if item has dropdown
                if (item->dropdown_count > 0)
                {
                    bc->dropdown_open = !bc->dropdown_open;
                    bc->dropdown_index = idx;
                    bc->dropdown_hovered = -1;
                    bc->base.needs_paint = true;
                }
                else
                {
                    // Regular click
                    bc->dropdown_open = false;
                    if (bc->on_click)
                    {
                        bc->on_click(bc, idx, bc->user_data);
                    }
                }
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}

void vg_breadcrumb_push(vg_breadcrumb_t *bc, const char *label, void *data)
{
    if (!bc || !label)
        return;

    // Expand capacity if needed
    if (bc->item_count >= bc->item_capacity)
    {
        size_t new_cap = bc->item_capacity * 2;
        if (new_cap < 8)
            new_cap = 8;
        vg_breadcrumb_item_t *new_items =
            realloc(bc->items, new_cap * sizeof(vg_breadcrumb_item_t));
        if (!new_items)
            return;
        bc->items = new_items;
        bc->item_capacity = new_cap;
    }

    vg_breadcrumb_item_t *item = &bc->items[bc->item_count++];
    memset(item, 0, sizeof(vg_breadcrumb_item_t));
    item->label = strdup(label);
    item->user_data = data;

    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

void vg_breadcrumb_pop(vg_breadcrumb_t *bc)
{
    if (!bc || bc->item_count == 0)
        return;

    bc->item_count--;
    free_breadcrumb_item(&bc->items[bc->item_count]);

    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

void vg_breadcrumb_clear(vg_breadcrumb_t *bc)
{
    if (!bc)
        return;

    for (size_t i = 0; i < bc->item_count; i++)
    {
        free_breadcrumb_item(&bc->items[i]);
    }
    bc->item_count = 0;

    bc->dropdown_open = false;
    bc->hovered_index = -1;

    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

void vg_breadcrumb_item_add_dropdown(vg_breadcrumb_item_t *item, const char *label, void *data)
{
    if (!item || !label)
        return;

    // Expand capacity if needed
    if (item->dropdown_count >= item->dropdown_capacity)
    {
        size_t new_cap = item->dropdown_capacity * 2;
        if (new_cap < 4)
            new_cap = 4;
        vg_breadcrumb_dropdown_t *new_items =
            realloc(item->dropdown_items, new_cap * sizeof(vg_breadcrumb_dropdown_t));
        if (!new_items)
            return;
        item->dropdown_items = new_items;
        item->dropdown_capacity = new_cap;
    }

    vg_breadcrumb_dropdown_t *dd = &item->dropdown_items[item->dropdown_count++];
    dd->label = strdup(label);
    dd->data = data;
}

void vg_breadcrumb_set_separator(vg_breadcrumb_t *bc, const char *sep)
{
    if (!bc)
        return;

    free(bc->separator);
    bc->separator = sep ? strdup(sep) : NULL;
    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}

void vg_breadcrumb_set_on_click(vg_breadcrumb_t *bc,
                                void (*callback)(vg_breadcrumb_t *, int, void *),
                                void *user_data)
{
    if (!bc)
        return;
    bc->on_click = callback;
    bc->user_data = user_data;
}

void vg_breadcrumb_set_font(vg_breadcrumb_t *bc, vg_font_t *font, float size)
{
    if (!bc)
        return;

    bc->font = font;
    bc->font_size = size;
    bc->base.needs_layout = true;
    bc->base.needs_paint = true;
}
