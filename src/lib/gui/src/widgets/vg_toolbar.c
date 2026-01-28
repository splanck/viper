// vg_toolbar.c - Toolbar widget implementation
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../../graphics/include/vgfx.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Constants
//=============================================================================

#define INITIAL_ITEM_CAPACITY 16
#define TOOLBAR_DEFAULT_PADDING 4
#define TOOLBAR_DEFAULT_SPACING 2
#define ICON_SIZE_SMALL 16
#define ICON_SIZE_MEDIUM 24
#define ICON_SIZE_LARGE 32

//=============================================================================
// Forward Declarations
//=============================================================================

static void toolbar_destroy(vg_widget_t *widget);
static void toolbar_measure(vg_widget_t *widget, float available_width, float available_height);
static void toolbar_arrange(vg_widget_t *widget, float x, float y, float width, float height);
static void toolbar_paint(vg_widget_t *widget, void *canvas);
static bool toolbar_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// Toolbar VTable
//=============================================================================

static vg_widget_vtable_t g_toolbar_vtable = {.destroy = toolbar_destroy,
                                              .measure = toolbar_measure,
                                              .arrange = toolbar_arrange,
                                              .paint = toolbar_paint,
                                              .handle_event = toolbar_handle_event,
                                              .can_focus = NULL,
                                              .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_item_capacity(vg_toolbar_t *tb, size_t needed)
{
    if (needed <= tb->item_capacity)
        return true;

    size_t new_capacity = tb->item_capacity == 0 ? INITIAL_ITEM_CAPACITY : tb->item_capacity * 2;
    while (new_capacity < needed)
    {
        new_capacity *= 2;
    }

    vg_toolbar_item_t **new_items = realloc(tb->items, new_capacity * sizeof(vg_toolbar_item_t *));
    if (!new_items)
        return false;

    tb->items = new_items;
    tb->item_capacity = new_capacity;
    return true;
}

static void free_item(vg_toolbar_item_t *item)
{
    if (!item)
        return;
    if (item->id)
        free(item->id);
    if (item->label)
        free(item->label);
    if (item->tooltip)
        free(item->tooltip);
    vg_icon_destroy(&item->icon);
    free(item);
}

static vg_toolbar_item_t *create_item(vg_toolbar_item_type_t type, const char *id)
{
    vg_toolbar_item_t *item = calloc(1, sizeof(vg_toolbar_item_t));
    if (!item)
        return NULL;

    item->type = type;
    item->id = id ? strdup(id) : NULL;
    item->label = NULL;
    item->tooltip = NULL;
    item->icon.type = VG_ICON_NONE;
    item->enabled = true;
    item->checked = false;
    item->show_label = false;
    item->dropdown_menu = NULL;
    item->custom_widget = NULL;
    item->user_data = NULL;
    item->on_click = NULL;
    item->on_toggle = NULL;

    return item;
}

static uint32_t get_icon_pixels(vg_toolbar_icon_size_t size)
{
    switch (size)
    {
        case VG_TOOLBAR_ICONS_SMALL:
            return ICON_SIZE_SMALL;
        case VG_TOOLBAR_ICONS_MEDIUM:
            return ICON_SIZE_MEDIUM;
        case VG_TOOLBAR_ICONS_LARGE:
            return ICON_SIZE_LARGE;
        default:
            return ICON_SIZE_MEDIUM;
    }
}

static float get_item_width(vg_toolbar_t *tb, vg_toolbar_item_t *item)
{
    uint32_t icon_px = get_icon_pixels(tb->icon_size);
    float padding = (float)tb->item_padding;

    switch (item->type)
    {
        case VG_TOOLBAR_ITEM_SEPARATOR:
            return 1.0f + tb->item_spacing * 2;

        case VG_TOOLBAR_ITEM_SPACER:
            return 0; // Spacers expand

        case VG_TOOLBAR_ITEM_BUTTON:
        case VG_TOOLBAR_ITEM_TOGGLE:
        case VG_TOOLBAR_ITEM_DROPDOWN:
        {
            float width = (float)icon_px + padding * 2;
            if (item->show_label && item->label && tb->font)
            {
                vg_text_metrics_t metrics;
                vg_font_measure_text(tb->font, tb->font_size, item->label, &metrics);
                width += metrics.width + padding;
            }
            if (item->type == VG_TOOLBAR_ITEM_DROPDOWN)
            {
                width += 12; // Arrow indicator
            }
            return width;
        }

        case VG_TOOLBAR_ITEM_WIDGET:
            if (item->custom_widget)
            {
                return item->custom_widget->measured_width + padding * 2;
            }
            return 0;

        default:
            return 0;
    }
}

static float get_item_height(vg_toolbar_t *tb, vg_toolbar_item_t *item)
{
    uint32_t icon_px = get_icon_pixels(tb->icon_size);
    float padding = (float)tb->item_padding;

    switch (item->type)
    {
        case VG_TOOLBAR_ITEM_SEPARATOR:
            return (float)icon_px + padding * 2;

        case VG_TOOLBAR_ITEM_SPACER:
            return 0;

        case VG_TOOLBAR_ITEM_BUTTON:
        case VG_TOOLBAR_ITEM_TOGGLE:
        case VG_TOOLBAR_ITEM_DROPDOWN:
            return (float)icon_px + padding * 2;

        case VG_TOOLBAR_ITEM_WIDGET:
            if (item->custom_widget)
            {
                return item->custom_widget->measured_height + padding * 2;
            }
            return 0;

        default:
            return 0;
    }
}

//=============================================================================
// Toolbar Implementation
//=============================================================================

vg_toolbar_t *vg_toolbar_create(vg_widget_t *parent, vg_toolbar_orientation_t orientation)
{
    vg_toolbar_t *tb = calloc(1, sizeof(vg_toolbar_t));
    if (!tb)
        return NULL;

    // Initialize base widget
    vg_widget_init(&tb->base, VG_WIDGET_TOOLBAR, &g_toolbar_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize arrays
    tb->items = NULL;
    tb->item_count = 0;
    tb->item_capacity = 0;

    // Configuration
    tb->orientation = orientation;
    tb->icon_size = VG_TOOLBAR_ICONS_MEDIUM;
    tb->item_padding = TOOLBAR_DEFAULT_PADDING;
    tb->item_spacing = TOOLBAR_DEFAULT_SPACING;
    tb->show_labels = false;
    tb->overflow_menu = true;

    // Font
    tb->font = NULL;
    tb->font_size = theme->typography.size_small;

    // Colors
    tb->bg_color = theme->colors.bg_secondary;
    tb->hover_color = theme->colors.bg_hover;
    tb->active_color = theme->colors.bg_active;
    tb->text_color = theme->colors.fg_primary;
    tb->disabled_color = theme->colors.fg_disabled;

    // State
    tb->hovered_item = NULL;
    tb->pressed_item = NULL;
    tb->overflow_start_index = -1;

    // Add to parent
    if (parent)
    {
        vg_widget_add_child(parent, &tb->base);
    }

    return tb;
}

static void toolbar_destroy(vg_widget_t *widget)
{
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;

    for (size_t i = 0; i < tb->item_count; i++)
    {
        free_item(tb->items[i]);
    }
    free(tb->items);
}

static void toolbar_measure(vg_widget_t *widget, float available_width, float available_height)
{
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;

    uint32_t icon_px = get_icon_pixels(tb->icon_size);
    float bar_thickness = (float)icon_px + tb->item_padding * 2 + 4; // Extra for borders

    if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
    {
        // Calculate total width of items
        float total_width = 0;
        for (size_t i = 0; i < tb->item_count; i++)
        {
            float item_width = get_item_width(tb, tb->items[i]);
            if (item_width > 0)
            {
                total_width += item_width + tb->item_spacing;
            }
        }
        widget->measured_width = total_width > 0 ? total_width - tb->item_spacing : available_width;
        widget->measured_height = bar_thickness;
    }
    else
    {
        // Vertical orientation
        float total_height = 0;
        for (size_t i = 0; i < tb->item_count; i++)
        {
            float item_height = get_item_height(tb, tb->items[i]);
            if (item_height > 0)
            {
                total_height += item_height + tb->item_spacing;
            }
        }
        widget->measured_width = bar_thickness;
        widget->measured_height =
            total_height > 0 ? total_height - tb->item_spacing : available_height;
    }
}

static void toolbar_arrange(vg_widget_t *widget, float x, float y, float width, float height)
{
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    // Reset overflow tracking
    tb->overflow_start_index = -1;

    // Arrange custom widgets within toolbar items
    float pos = 0;
    for (size_t i = 0; i < tb->item_count; i++)
    {
        vg_toolbar_item_t *item = tb->items[i];

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
        {
            float item_width = get_item_width(tb, item);
            if (pos + item_width > width && tb->overflow_menu)
            {
                // This item overflows
                if (tb->overflow_start_index < 0)
                {
                    tb->overflow_start_index = (int)i;
                }
            }

            if (item->type == VG_TOOLBAR_ITEM_WIDGET && item->custom_widget)
            {
                float iw = item->custom_widget->measured_width;
                float ih = item->custom_widget->measured_height;
                float ix = x + pos + (item_width - iw) / 2;
                float iy = y + (height - ih) / 2;
                vg_widget_arrange(item->custom_widget, ix, iy, iw, ih);
            }

            pos += item_width + tb->item_spacing;
        }
        else
        {
            float item_height = get_item_height(tb, item);
            if (pos + item_height > height && tb->overflow_menu)
            {
                if (tb->overflow_start_index < 0)
                {
                    tb->overflow_start_index = (int)i;
                }
            }

            if (item->type == VG_TOOLBAR_ITEM_WIDGET && item->custom_widget)
            {
                float iw = item->custom_widget->measured_width;
                float ih = item->custom_widget->measured_height;
                float ix = x + (width - iw) / 2;
                float iy = y + pos + (item_height - ih) / 2;
                vg_widget_arrange(item->custom_widget, ix, iy, iw, ih);
            }

            pos += item_height + tb->item_spacing;
        }
    }
}

static void toolbar_paint(vg_widget_t *widget, void *canvas)
{
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;

    // Draw toolbar background
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   tb->bg_color);

    uint32_t icon_px = get_icon_pixels(tb->icon_size);

    float pos = 0;
    int max_index = tb->overflow_start_index >= 0 ? tb->overflow_start_index : (int)tb->item_count;

    for (int i = 0; i < max_index; i++)
    {
        vg_toolbar_item_t *item = tb->items[i];

        float item_width, item_height;
        float item_x, item_y;

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
        {
            item_width = get_item_width(tb, item);
            item_height = widget->height - 4;
            item_x = widget->x + pos;
            item_y = widget->y + 2;
        }
        else
        {
            item_width = widget->width - 4;
            item_height = get_item_height(tb, item);
            item_x = widget->x + 2;
            item_y = widget->y + pos;
        }

        switch (item->type)
        {
            case VG_TOOLBAR_ITEM_SEPARATOR:
            {
                // Draw vertical or horizontal line
                vg_theme_t *theme = vg_theme_get_current();
                uint32_t sep_color = theme->colors.border_primary;
                if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
                {
                    int32_t sep_x = (int32_t)(item_x + item_width / 2);
                    int32_t sep_y1 = (int32_t)(widget->y + 4);
                    int32_t sep_y2 = (int32_t)(widget->y + widget->height - 4);
                    vgfx_fill_rect(win, sep_x, sep_y1, 1, sep_y2 - sep_y1, sep_color);
                }
                else
                {
                    int32_t sep_x1 = (int32_t)(widget->x + 4);
                    int32_t sep_x2 = (int32_t)(widget->x + widget->width - 4);
                    int32_t sep_y = (int32_t)(item_y + item_height / 2);
                    vgfx_fill_rect(win, sep_x1, sep_y, sep_x2 - sep_x1, 1, sep_color);
                }
                break;
            }

            case VG_TOOLBAR_ITEM_SPACER:
                // Don't draw anything
                break;

            case VG_TOOLBAR_ITEM_BUTTON:
            case VG_TOOLBAR_ITEM_TOGGLE:
            case VG_TOOLBAR_ITEM_DROPDOWN:
            {
                // Draw button background based on state
                uint32_t btn_bg = 0; // No background by default
                if (item == tb->pressed_item)
                {
                    btn_bg = tb->active_color;
                }
                else if (item == tb->hovered_item)
                {
                    btn_bg = tb->hover_color;
                }
                else if (item->type == VG_TOOLBAR_ITEM_TOGGLE && item->checked)
                {
                    btn_bg = tb->active_color;
                }

                if (btn_bg != 0)
                {
                    vgfx_fill_rect(win,
                                   (int32_t)item_x,
                                   (int32_t)item_y,
                                   (int32_t)item_width,
                                   (int32_t)item_height,
                                   btn_bg);
                }

                // Determine text color
                uint32_t txt_color = item->enabled ? tb->text_color : tb->disabled_color;

                // Draw icon
                float icon_x = item_x + (item_width - icon_px) / 2;
                float icon_y = item_y + (item_height - icon_px) / 2;

                if (item->show_label && item->label)
                {
                    // Icon on left, label on right
                    icon_x = item_x + tb->item_padding;
                }

                switch (item->icon.type)
                {
                    case VG_ICON_GLYPH:
                        // Draw glyph using font
                        if (tb->font)
                        {
                            char buf[8] = {0};
                            // UTF-8 encode the glyph
                            uint32_t cp = item->icon.data.glyph;
                            if (cp < 0x80)
                            {
                                buf[0] = (char)cp;
                            }
                            else if (cp < 0x800)
                            {
                                buf[0] = (char)(0xC0 | (cp >> 6));
                                buf[1] = (char)(0x80 | (cp & 0x3F));
                            }
                            else if (cp < 0x10000)
                            {
                                buf[0] = (char)(0xE0 | (cp >> 12));
                                buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[2] = (char)(0x80 | (cp & 0x3F));
                            }
                            else
                            {
                                buf[0] = (char)(0xF0 | (cp >> 18));
                                buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[3] = (char)(0x80 | (cp & 0x3F));
                            }
                            vg_font_draw_text(canvas,
                                              tb->font,
                                              (float)icon_px,
                                              icon_x,
                                              icon_y + icon_px * 0.8f,
                                              buf,
                                              txt_color);
                        }
                        break;

                    case VG_ICON_IMAGE:
                        // Draw image (placeholder - needs vgfx integration)
                        break;

                    default:
                        break;
                }

                // Draw label if shown
                if (item->show_label && item->label && tb->font)
                {
                    float label_x = icon_x + icon_px + tb->item_padding;
                    float label_y = item_y + item_height / 2 + tb->font_size / 2;
                    vg_font_draw_text(canvas,
                                      tb->font,
                                      tb->font_size,
                                      label_x,
                                      label_y,
                                      item->label,
                                      txt_color);
                }

                // Draw dropdown arrow
                if (item->type == VG_TOOLBAR_ITEM_DROPDOWN)
                {
                    // Draw small triangle at right edge
                    float arrow_x = item_x + item_width - 8;
                    float arrow_y = item_y + item_height / 2;
                    vgfx_fill_rect(win, (int32_t)arrow_x, (int32_t)(arrow_y - 1), 5, 1, txt_color);
                    vgfx_fill_rect(win, (int32_t)(arrow_x + 1), (int32_t)arrow_y, 3, 1, txt_color);
                    vgfx_fill_rect(win, (int32_t)(arrow_x + 2), (int32_t)(arrow_y + 1), 1, 1, txt_color);
                }
                break;
            }

            case VG_TOOLBAR_ITEM_WIDGET:
                // Custom widget draws itself
                if (item->custom_widget)
                {
                    vg_widget_paint(item->custom_widget, canvas);
                }
                break;
        }

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
        {
            pos += item_width + tb->item_spacing;
        }
        else
        {
            pos += item_height + tb->item_spacing;
        }
    }

    // Draw overflow button if needed
    if (tb->overflow_start_index >= 0)
    {
        // Draw "..." indicator at end
        float ov_x = widget->x + widget->width - 20;
        float ov_y = widget->y + widget->height / 2;
        vgfx_fill_rect(win, (int32_t)ov_x, (int32_t)ov_y, 2, 2, tb->text_color);
        vgfx_fill_rect(win, (int32_t)(ov_x + 5), (int32_t)ov_y, 2, 2, tb->text_color);
        vgfx_fill_rect(win, (int32_t)(ov_x + 10), (int32_t)ov_y, 2, 2, tb->text_color);
    }
}

static vg_toolbar_item_t *find_item_at(vg_toolbar_t *tb, float px, float py)
{
    float pos = 0;
    int max_index = tb->overflow_start_index >= 0 ? tb->overflow_start_index : (int)tb->item_count;

    for (int i = 0; i < max_index; i++)
    {
        vg_toolbar_item_t *item = tb->items[i];

        float item_width, item_height;
        float item_x, item_y;

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
        {
            item_width = get_item_width(tb, item);
            item_height = tb->base.height;
            item_x = tb->base.x + pos;
            item_y = tb->base.y;
        }
        else
        {
            item_width = tb->base.width;
            item_height = get_item_height(tb, item);
            item_x = tb->base.x;
            item_y = tb->base.y + pos;
        }

        if (px >= item_x && px < item_x + item_width && py >= item_y && py < item_y + item_height)
        {
            if (item->type != VG_TOOLBAR_ITEM_SEPARATOR && item->type != VG_TOOLBAR_ITEM_SPACER)
            {
                return item;
            }
        }

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL)
        {
            pos += item_width + tb->item_spacing;
        }
        else
        {
            pos += item_height + tb->item_spacing;
        }
    }

    return NULL;
}

static bool toolbar_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;

    switch (event->type)
    {
        case VG_EVENT_MOUSE_MOVE:
        {
            vg_toolbar_item_t *item = find_item_at(tb, event->mouse.x, event->mouse.y);
            if (item != tb->hovered_item)
            {
                tb->hovered_item = item;
                widget->needs_paint = true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (tb->hovered_item)
            {
                tb->hovered_item = NULL;
                widget->needs_paint = true;
            }
            if (tb->pressed_item)
            {
                tb->pressed_item = NULL;
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_MOUSE_DOWN:
        {
            vg_toolbar_item_t *item = find_item_at(tb, event->mouse.x, event->mouse.y);
            if (item && item->enabled)
            {
                tb->pressed_item = item;
                widget->needs_paint = true;
                return true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_UP:
        {
            vg_toolbar_item_t *item = find_item_at(tb, event->mouse.x, event->mouse.y);
            if (item && item == tb->pressed_item && item->enabled)
            {
                // Set polling flag for runtime API
                item->was_clicked = true;

                // Trigger action
                switch (item->type)
                {
                    case VG_TOOLBAR_ITEM_BUTTON:
                        if (item->on_click)
                        {
                            item->on_click(item, item->user_data);
                        }
                        break;

                    case VG_TOOLBAR_ITEM_TOGGLE:
                        item->checked = !item->checked;
                        if (item->on_toggle)
                        {
                            item->on_toggle(item, item->checked, item->user_data);
                        }
                        break;

                    case VG_TOOLBAR_ITEM_DROPDOWN:
                        // Show dropdown menu
                        // (placeholder - needs menu integration)
                        if (item->on_click)
                        {
                            item->on_click(item, item->user_data);
                        }
                        break;

                    default:
                        break;
                }
            }
            tb->pressed_item = NULL;
            widget->needs_paint = true;
            return item != NULL;
        }

        default:
            return false;
    }
}

//=============================================================================
// Toolbar API
//=============================================================================

vg_toolbar_item_t *vg_toolbar_add_button(vg_toolbar_t *tb,
                                         const char *id,
                                         const char *label,
                                         vg_icon_t icon,
                                         void (*on_click)(vg_toolbar_item_t *, void *),
                                         void *user_data)
{
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_BUTTON, id);
    if (!item)
        return NULL;

    item->label = label ? strdup(label) : NULL;
    item->icon = icon;
    item->show_label = tb->show_labels;
    item->on_click = on_click;
    item->user_data = user_data;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_toggle(vg_toolbar_t *tb,
                                         const char *id,
                                         const char *label,
                                         vg_icon_t icon,
                                         bool initial_checked,
                                         void (*on_toggle)(vg_toolbar_item_t *, bool, void *),
                                         void *user_data)
{
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_TOGGLE, id);
    if (!item)
        return NULL;

    item->label = label ? strdup(label) : NULL;
    item->icon = icon;
    item->checked = initial_checked;
    item->show_label = tb->show_labels;
    item->on_toggle = on_toggle;
    item->user_data = user_data;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_dropdown(
    vg_toolbar_t *tb, const char *id, const char *label, vg_icon_t icon, struct vg_menu *menu)
{
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_DROPDOWN, id);
    if (!item)
        return NULL;

    item->label = label ? strdup(label) : NULL;
    item->icon = icon;
    item->show_label = tb->show_labels;
    item->dropdown_menu = menu;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_separator(vg_toolbar_t *tb)
{
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_SEPARATOR, NULL);
    if (!item)
        return NULL;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_spacer(vg_toolbar_t *tb)
{
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_SPACER, NULL);
    if (!item)
        return NULL;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_widget(vg_toolbar_t *tb, const char *id, vg_widget_t *widget)
{
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_WIDGET, id);
    if (!item)
        return NULL;

    item->custom_widget = widget;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    return item;
}

void vg_toolbar_remove_item(vg_toolbar_t *tb, const char *id)
{
    if (!tb || !id)
        return;

    for (size_t i = 0; i < tb->item_count; i++)
    {
        if (tb->items[i]->id && strcmp(tb->items[i]->id, id) == 0)
        {
            free_item(tb->items[i]);
            memmove(&tb->items[i],
                    &tb->items[i + 1],
                    (tb->item_count - i - 1) * sizeof(vg_toolbar_item_t *));
            tb->item_count--;
            tb->base.needs_layout = true;
            return;
        }
    }
}

vg_toolbar_item_t *vg_toolbar_get_item(vg_toolbar_t *tb, const char *id)
{
    if (!tb || !id)
        return NULL;

    for (size_t i = 0; i < tb->item_count; i++)
    {
        if (tb->items[i]->id && strcmp(tb->items[i]->id, id) == 0)
        {
            return tb->items[i];
        }
    }
    return NULL;
}

void vg_toolbar_item_set_enabled(vg_toolbar_item_t *item, bool enabled)
{
    if (!item)
        return;
    item->enabled = enabled;
}

void vg_toolbar_item_set_checked(vg_toolbar_item_t *item, bool checked)
{
    if (!item)
        return;
    item->checked = checked;
}

void vg_toolbar_item_set_tooltip(vg_toolbar_item_t *item, const char *tooltip)
{
    if (!item)
        return;
    if (item->tooltip)
        free(item->tooltip);
    item->tooltip = tooltip ? strdup(tooltip) : NULL;
}

void vg_toolbar_item_set_icon(vg_toolbar_item_t *item, vg_icon_t icon)
{
    if (!item)
        return;
    vg_icon_destroy(&item->icon);
    item->icon = icon;
}

void vg_toolbar_set_icon_size(vg_toolbar_t *tb, vg_toolbar_icon_size_t size)
{
    if (!tb)
        return;
    tb->icon_size = size;
    tb->base.needs_layout = true;
}

void vg_toolbar_set_show_labels(vg_toolbar_t *tb, bool show)
{
    if (!tb)
        return;
    tb->show_labels = show;
    for (size_t i = 0; i < tb->item_count; i++)
    {
        tb->items[i]->show_label = show;
    }
    tb->base.needs_layout = true;
}

void vg_toolbar_set_font(vg_toolbar_t *tb, vg_font_t *font, float size)
{
    if (!tb)
        return;
    tb->font = font;
    tb->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_small;
    tb->base.needs_layout = true;
}

//=============================================================================
// Icon Helpers
//=============================================================================

vg_icon_t vg_icon_from_glyph(uint32_t codepoint)
{
    vg_icon_t icon = {0};
    icon.type = VG_ICON_GLYPH;
    icon.data.glyph = codepoint;
    return icon;
}

vg_icon_t vg_icon_from_pixels(uint8_t *rgba, uint32_t w, uint32_t h)
{
    vg_icon_t icon = {0};
    if (!rgba || w == 0 || h == 0)
        return icon;

    icon.type = VG_ICON_IMAGE;
    size_t size = w * h * 4;
    icon.data.image.pixels = malloc(size);
    if (icon.data.image.pixels)
    {
        memcpy(icon.data.image.pixels, rgba, size);
        icon.data.image.width = w;
        icon.data.image.height = h;
    }
    else
    {
        icon.type = VG_ICON_NONE;
    }
    return icon;
}

vg_icon_t vg_icon_from_file(const char *path)
{
    vg_icon_t icon = {0};
    if (!path)
        return icon;

    icon.type = VG_ICON_PATH;
    icon.data.path = strdup(path);
    if (!icon.data.path)
    {
        icon.type = VG_ICON_NONE;
    }
    return icon;
}

void vg_icon_destroy(vg_icon_t *icon)
{
    if (!icon)
        return;

    switch (icon->type)
    {
        case VG_ICON_IMAGE:
            free(icon->data.image.pixels);
            break;
        case VG_ICON_PATH:
            free(icon->data.path);
            break;
        default:
            break;
    }
    icon->type = VG_ICON_NONE;
}
