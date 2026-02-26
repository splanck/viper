// vg_statusbar.c - Status bar widget implementation
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

#define INITIAL_ITEM_CAPACITY 8
#define STATUSBAR_DEFAULT_HEIGHT 24
#define STATUSBAR_ITEM_PADDING 8
#define STATUSBAR_SEPARATOR_WIDTH 1

//=============================================================================
// Forward Declarations
//=============================================================================

static void statusbar_destroy(vg_widget_t *widget);
static void statusbar_measure(vg_widget_t *widget, float available_width, float available_height);
static void statusbar_arrange(vg_widget_t *widget, float x, float y, float width, float height);
static void statusbar_paint(vg_widget_t *widget, void *canvas);
static bool statusbar_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// StatusBar VTable
//=============================================================================

static vg_widget_vtable_t g_statusbar_vtable = {.destroy = statusbar_destroy,
                                                .measure = statusbar_measure,
                                                .arrange = statusbar_arrange,
                                                .paint = statusbar_paint,
                                                .handle_event = statusbar_handle_event,
                                                .can_focus = NULL,
                                                .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_item_capacity(vg_statusbar_item_t ***items, size_t *capacity, size_t needed)
{
    if (needed <= *capacity)
        return true;

    size_t new_capacity = *capacity == 0 ? INITIAL_ITEM_CAPACITY : *capacity * 2;
    while (new_capacity < needed)
    {
        new_capacity *= 2;
    }

    vg_statusbar_item_t **new_items = realloc(*items, new_capacity * sizeof(vg_statusbar_item_t *));
    if (!new_items)
        return false;

    *items = new_items;
    *capacity = new_capacity;
    return true;
}

static void free_item(vg_statusbar_item_t *item)
{
    if (!item)
        return;
    if (item->text)
        free(item->text);
    if (item->tooltip)
        free(item->tooltip);
    free(item);
}

static vg_statusbar_item_t *create_item(vg_statusbar_item_type_t type, const char *text)
{
    vg_statusbar_item_t *item = calloc(1, sizeof(vg_statusbar_item_t));
    if (!item)
        return NULL;

    item->type = type;
    item->text = text ? strdup(text) : NULL;
    item->tooltip = NULL;
    item->min_width = 0;
    item->max_width = 0;
    item->visible = true;
    item->progress = 0.0f;
    item->user_data = NULL;
    item->on_click = NULL;

    return item;
}

static float measure_item_width(vg_statusbar_t *sb, vg_statusbar_item_t *item)
{
    if (!item->visible)
        return 0;

    switch (item->type)
    {
        case VG_STATUSBAR_ITEM_SEPARATOR:
            return STATUSBAR_SEPARATOR_WIDTH + STATUSBAR_ITEM_PADDING;

        case VG_STATUSBAR_ITEM_SPACER:
            return 0; // Spacers expand to fill

        case VG_STATUSBAR_ITEM_PROGRESS:
            return item->min_width > 0 ? item->min_width : 60;

        case VG_STATUSBAR_ITEM_TEXT:
        case VG_STATUSBAR_ITEM_BUTTON:
            if (sb->font && item->text)
            {
                vg_text_metrics_t metrics;
                vg_font_measure_text(sb->font, sb->font_size, item->text, &metrics);
                float width = metrics.width + STATUSBAR_ITEM_PADDING * 2;
                if (item->min_width > 0 && width < item->min_width)
                    width = item->min_width;
                if (item->max_width > 0 && width > item->max_width)
                    width = item->max_width;
                return width;
            }
            return item->min_width > 0 ? item->min_width : 40;

        default:
            return 40;
    }
}

//=============================================================================
// StatusBar Implementation
//=============================================================================

vg_statusbar_t *vg_statusbar_create(vg_widget_t *parent)
{
    vg_statusbar_t *sb = calloc(1, sizeof(vg_statusbar_t));
    if (!sb)
        return NULL;

    // Initialize base widget
    vg_widget_init(&sb->base, VG_WIDGET_STATUSBAR, &g_statusbar_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize arrays
    sb->left_items = NULL;
    sb->left_count = 0;
    sb->left_capacity = 0;

    sb->center_items = NULL;
    sb->center_count = 0;
    sb->center_capacity = 0;

    sb->right_items = NULL;
    sb->right_count = 0;
    sb->right_capacity = 0;

    // Styling — scale pixel constants by ui_scale for HiDPI displays.
    float s = theme->ui_scale > 0.0f ? theme->ui_scale : 1.0f;
    sb->height = (int)(STATUSBAR_DEFAULT_HEIGHT * s);
    sb->item_padding = (int)(STATUSBAR_ITEM_PADDING * s);
    sb->separator_width = STATUSBAR_SEPARATOR_WIDTH;

    // Font
    sb->font = NULL;
    sb->font_size = theme->typography.size_small;

    // Colors from theme
    sb->bg_color = theme->colors.bg_secondary;
    sb->text_color = theme->colors.fg_secondary;
    sb->hover_color = theme->colors.bg_hover;
    sb->border_color = theme->colors.border_secondary;

    // State
    sb->hovered_item = NULL;

    // Set minimum size
    sb->base.constraints.min_height = (float)sb->height;

    // Add to parent
    if (parent)
    {
        vg_widget_add_child(parent, &sb->base);
    }

    return sb;
}

static void statusbar_destroy(vg_widget_t *widget)
{
    vg_statusbar_t *sb = (vg_statusbar_t *)widget;

    // Free left items
    for (size_t i = 0; i < sb->left_count; i++)
    {
        free_item(sb->left_items[i]);
    }
    free(sb->left_items);

    // Free center items
    for (size_t i = 0; i < sb->center_count; i++)
    {
        free_item(sb->center_items[i]);
    }
    free(sb->center_items);

    // Free right items
    for (size_t i = 0; i < sb->right_count; i++)
    {
        free_item(sb->right_items[i]);
    }
    free(sb->right_items);
}

static void statusbar_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_statusbar_t *sb = (vg_statusbar_t *)widget;
    (void)available_height;

    widget->measured_width = available_width > 0 ? available_width : 400;
    widget->measured_height = (float)sb->height;
}

static void statusbar_arrange(vg_widget_t *widget, float x, float y, float width, float height)
{
    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;
}

static void statusbar_draw_item(vg_statusbar_t *sb,
                                vgfx_window_t win,
                                vg_statusbar_item_t *item,
                                float x,
                                float item_width,
                                float text_y,
                                void *canvas)
{
    float wy = sb->base.y, wh = (float)sb->height;

    // Draw hover background for buttons
    if (item->type == VG_STATUSBAR_ITEM_BUTTON && item == sb->hovered_item)
    {
        vgfx_fill_rect(
            win, (int32_t)x, (int32_t)wy, (int32_t)item_width, (int32_t)wh, sb->hover_color);
    }

    switch (item->type)
    {
        case VG_STATUSBAR_ITEM_TEXT:
        case VG_STATUSBAR_ITEM_BUTTON:
            if (item->text)
            {
                vg_font_draw_text(canvas,
                                  sb->font,
                                  sb->font_size,
                                  x + sb->item_padding,
                                  text_y,
                                  item->text,
                                  sb->text_color);
            }
            break;

        case VG_STATUSBAR_ITEM_SEPARATOR:
        {
            // Vertical separator centered within the item
            int32_t sx = (int32_t)(x + item_width / 2.0f);
            vgfx_fill_rect(win, sx, (int32_t)(wy + 3), 1, (int32_t)(wh - 6), sb->border_color);
            break;
        }

        case VG_STATUSBAR_ITEM_PROGRESS:
        {
            float pw = item->min_width > 0 ? (float)item->min_width : 60.0f;
            float bar_h = 4.0f;
            float bar_y = wy + (wh - bar_h) / 2.0f;
            // Track background
            vgfx_fill_rect(
                win, (int32_t)x, (int32_t)bar_y, (int32_t)pw, (int32_t)bar_h, 0x00404040);
            // Progress fill
            float fill_w = item->progress * pw;
            if (fill_w > 0.0f)
            {
                vgfx_fill_rect(
                    win, (int32_t)x, (int32_t)bar_y, (int32_t)fill_w, (int32_t)bar_h, 0x000078D4);
            }
            break;
        }

        default:
            break;
    }
}

static void statusbar_paint(vg_widget_t *widget, void *canvas)
{
    vg_statusbar_t *sb = (vg_statusbar_t *)widget;

    if (!sb->font)
        return;

    vgfx_window_t win = (vgfx_window_t)canvas;
    float wx = widget->x, wy = widget->y, ww = widget->width, wh = widget->height;

    vg_font_metrics_t font_metrics;
    vg_font_get_metrics(sb->font, sb->font_size, &font_metrics);

    // Draw background
    vgfx_fill_rect(win, (int32_t)wx, (int32_t)wy, (int32_t)ww, (int32_t)wh, sb->bg_color);

    // Draw top border (1px separator line)
    vgfx_fill_rect(win, (int32_t)wx, (int32_t)wy, (int32_t)ww, 1, sb->border_color);

    float left_x = wx + sb->item_padding;
    float right_x = wx + ww - sb->item_padding;
    float text_y = wy + (wh - font_metrics.line_height) / 2.0f + font_metrics.ascent;

    // Calculate widths for each zone
    float center_width = 0;
    for (size_t i = 0; i < sb->center_count; i++)
    {
        if (sb->center_items[i]->type != VG_STATUSBAR_ITEM_SPACER)
            center_width += measure_item_width(sb, sb->center_items[i]);
    }

    // Draw left zone items
    float x = left_x;
    for (size_t i = 0; i < sb->left_count; i++)
    {
        vg_statusbar_item_t *item = sb->left_items[i];
        if (!item->visible)
            continue;

        float item_width = measure_item_width(sb, item);
        statusbar_draw_item(sb, win, item, x, item_width, text_y, canvas);
        x += item_width;
    }

    // Draw right zone items (right to left)
    x = right_x;
    for (size_t i = sb->right_count; i > 0; i--)
    {
        vg_statusbar_item_t *item = sb->right_items[i - 1];
        if (!item->visible)
            continue;

        float item_width = measure_item_width(sb, item);
        x -= item_width;
        statusbar_draw_item(sb, win, item, x, item_width, text_y, canvas);
    }

    // Draw center zone items
    float center_start = wx + ww / 2.0f - center_width / 2.0f;
    x = center_start;
    for (size_t i = 0; i < sb->center_count; i++)
    {
        vg_statusbar_item_t *item = sb->center_items[i];
        if (!item->visible || item->type == VG_STATUSBAR_ITEM_SPACER)
            continue;

        float item_width = measure_item_width(sb, item);
        statusbar_draw_item(sb, win, item, x, item_width, text_y, canvas);
        x += item_width;
    }
}

static vg_statusbar_item_t *find_item_at(vg_statusbar_t *sb, float mouse_x)
{
    float left_x = sb->base.x + sb->item_padding;
    float right_x = sb->base.x + sb->base.width - sb->item_padding;

    // Check left zone
    float x = left_x;
    for (size_t i = 0; i < sb->left_count; i++)
    {
        vg_statusbar_item_t *item = sb->left_items[i];
        if (!item->visible)
            continue;

        float item_width = measure_item_width(sb, item);
        if (mouse_x >= x && mouse_x < x + item_width)
        {
            if (item->type == VG_STATUSBAR_ITEM_BUTTON)
            {
                return item;
            }
        }
        x += item_width;
    }

    // Check right zone
    x = right_x;
    for (size_t i = sb->right_count; i > 0; i--)
    {
        vg_statusbar_item_t *item = sb->right_items[i - 1];
        if (!item->visible)
            continue;

        float item_width = measure_item_width(sb, item);
        x -= item_width;
        if (mouse_x >= x && mouse_x < x + item_width)
        {
            if (item->type == VG_STATUSBAR_ITEM_BUTTON)
            {
                return item;
            }
        }
    }

    // Check center zone
    float center_width = 0;
    for (size_t i = 0; i < sb->center_count; i++)
    {
        if (sb->center_items[i]->type != VG_STATUSBAR_ITEM_SPACER)
        {
            center_width += measure_item_width(sb, sb->center_items[i]);
        }
    }
    x = sb->base.x + sb->base.width / 2 - center_width / 2;
    for (size_t i = 0; i < sb->center_count; i++)
    {
        vg_statusbar_item_t *item = sb->center_items[i];
        if (!item->visible || item->type == VG_STATUSBAR_ITEM_SPACER)
            continue;

        float item_width = measure_item_width(sb, item);
        if (mouse_x >= x && mouse_x < x + item_width)
        {
            if (item->type == VG_STATUSBAR_ITEM_BUTTON)
            {
                return item;
            }
        }
        x += item_width;
    }

    return NULL;
}

static bool statusbar_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_statusbar_t *sb = (vg_statusbar_t *)widget;

    switch (event->type)
    {
        case VG_EVENT_MOUSE_MOVE:
        {
            vg_statusbar_item_t *item = find_item_at(sb, event->mouse.x);
            if (item != sb->hovered_item)
            {
                sb->hovered_item = item;
                widget->needs_paint = true;
            }
            return false; // Don't consume
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (sb->hovered_item)
            {
                sb->hovered_item = NULL;
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_CLICK:
        {
            vg_statusbar_item_t *item = find_item_at(sb, event->mouse.x);
            if (item && item->on_click)
            {
                item->on_click(item, item->user_data);
                return true;
            }
            return false;
        }

        default:
            return false;
    }
}

//=============================================================================
// StatusBar API
//=============================================================================

static vg_statusbar_item_t *add_item_to_zone(vg_statusbar_t *sb,
                                             vg_statusbar_zone_t zone,
                                             vg_statusbar_item_t *item)
{
    vg_statusbar_item_t ***items;
    size_t *count;
    size_t *capacity;

    switch (zone)
    {
        case VG_STATUSBAR_ZONE_LEFT:
            items = &sb->left_items;
            count = &sb->left_count;
            capacity = &sb->left_capacity;
            break;
        case VG_STATUSBAR_ZONE_CENTER:
            items = &sb->center_items;
            count = &sb->center_count;
            capacity = &sb->center_capacity;
            break;
        case VG_STATUSBAR_ZONE_RIGHT:
            items = &sb->right_items;
            count = &sb->right_count;
            capacity = &sb->right_capacity;
            break;
        default:
            return NULL;
    }

    if (!ensure_item_capacity(items, capacity, *count + 1))
    {
        free_item(item);
        return NULL;
    }

    (*items)[*count] = item;
    (*count)++;

    sb->base.needs_paint = true;
    return item;
}

vg_statusbar_item_t *vg_statusbar_add_text(vg_statusbar_t *sb,
                                           vg_statusbar_zone_t zone,
                                           const char *text)
{
    if (!sb)
        return NULL;

    vg_statusbar_item_t *item = create_item(VG_STATUSBAR_ITEM_TEXT, text);
    if (!item)
        return NULL;

    return add_item_to_zone(sb, zone, item);
}

vg_statusbar_item_t *vg_statusbar_add_button(vg_statusbar_t *sb,
                                             vg_statusbar_zone_t zone,
                                             const char *text,
                                             void (*on_click)(vg_statusbar_item_t *, void *),
                                             void *user_data)
{
    if (!sb)
        return NULL;

    vg_statusbar_item_t *item = create_item(VG_STATUSBAR_ITEM_BUTTON, text);
    if (!item)
        return NULL;

    item->on_click = on_click;
    item->user_data = user_data;

    return add_item_to_zone(sb, zone, item);
}

vg_statusbar_item_t *vg_statusbar_add_progress(vg_statusbar_t *sb, vg_statusbar_zone_t zone)
{
    if (!sb)
        return NULL;

    vg_statusbar_item_t *item = create_item(VG_STATUSBAR_ITEM_PROGRESS, NULL);
    if (!item)
        return NULL;

    item->min_width = 60;
    return add_item_to_zone(sb, zone, item);
}

vg_statusbar_item_t *vg_statusbar_add_separator(vg_statusbar_t *sb, vg_statusbar_zone_t zone)
{
    if (!sb)
        return NULL;

    vg_statusbar_item_t *item = create_item(VG_STATUSBAR_ITEM_SEPARATOR, NULL);
    if (!item)
        return NULL;

    return add_item_to_zone(sb, zone, item);
}

vg_statusbar_item_t *vg_statusbar_add_spacer(vg_statusbar_t *sb, vg_statusbar_zone_t zone)
{
    if (!sb)
        return NULL;

    vg_statusbar_item_t *item = create_item(VG_STATUSBAR_ITEM_SPACER, NULL);
    if (!item)
        return NULL;

    return add_item_to_zone(sb, zone, item);
}

void vg_statusbar_remove_item(vg_statusbar_t *sb, vg_statusbar_item_t *item)
{
    if (!sb || !item)
        return;

    // Search in all zones
    vg_statusbar_item_t ***zones[] = {&sb->left_items, &sb->center_items, &sb->right_items};
    size_t *counts[] = {&sb->left_count, &sb->center_count, &sb->right_count};

    for (int z = 0; z < 3; z++)
    {
        vg_statusbar_item_t **items = *zones[z];
        size_t count = *counts[z];

        for (size_t i = 0; i < count; i++)
        {
            if (items[i] == item)
            {
                free_item(item);
                memmove(&items[i], &items[i + 1], (count - i - 1) * sizeof(vg_statusbar_item_t *));
                (*counts[z])--;
                sb->base.needs_paint = true;
                return;
            }
        }
    }
}

void vg_statusbar_clear_zone(vg_statusbar_t *sb, vg_statusbar_zone_t zone)
{
    if (!sb)
        return;

    vg_statusbar_item_t ***items;
    size_t *count;

    switch (zone)
    {
        case VG_STATUSBAR_ZONE_LEFT:
            items = &sb->left_items;
            count = &sb->left_count;
            break;
        case VG_STATUSBAR_ZONE_CENTER:
            items = &sb->center_items;
            count = &sb->center_count;
            break;
        case VG_STATUSBAR_ZONE_RIGHT:
            items = &sb->right_items;
            count = &sb->right_count;
            break;
        default:
            return;
    }

    for (size_t i = 0; i < *count; i++)
    {
        free_item((*items)[i]);
    }
    *count = 0;
    sb->base.needs_paint = true;
}

void vg_statusbar_item_set_text(vg_statusbar_item_t *item, const char *text)
{
    if (!item)
        return;

    if (item->text)
        free(item->text);
    item->text = text ? strdup(text) : NULL;
}

void vg_statusbar_item_set_tooltip(vg_statusbar_item_t *item, const char *tooltip)
{
    if (!item)
        return;

    if (item->tooltip)
        free(item->tooltip);
    item->tooltip = tooltip ? strdup(tooltip) : NULL;
}

void vg_statusbar_item_set_progress(vg_statusbar_item_t *item, float progress)
{
    if (!item)
        return;

    if (progress < 0.0f)
        progress = 0.0f;
    if (progress > 1.0f)
        progress = 1.0f;
    item->progress = progress;
}

void vg_statusbar_item_set_visible(vg_statusbar_item_t *item, bool visible)
{
    if (!item)
        return;
    item->visible = visible;
}

void vg_statusbar_set_font(vg_statusbar_t *sb, vg_font_t *font, float size)
{
    if (!sb)
        return;

    sb->font = font;
    sb->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_small;
    sb->base.needs_layout = true;
    sb->base.needs_paint = true;
}

// Convenience functions for common IDE status items
void vg_statusbar_set_cursor_position(vg_statusbar_t *sb, int line, int col)
{
    if (!sb)
        return;

    // Find or create the cursor position item
    // For now, just update if there's a text item in the right zone
    char buf[64];
    snprintf(buf, sizeof(buf), "Ln %d, Col %d", line, col);

    // This is a simplified version - a full implementation would
    // track specific items by ID
    if (sb->right_count > 0)
    {
        vg_statusbar_item_t *item = sb->right_items[sb->right_count - 1];
        if (item->type == VG_STATUSBAR_ITEM_TEXT)
        {
            vg_statusbar_item_set_text(item, buf);
            sb->base.needs_paint = true;
        }
    }
}
