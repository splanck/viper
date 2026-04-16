//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_toolbar.c
//
//===----------------------------------------------------------------------===//
// vg_toolbar.c - Toolbar widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
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
static void toolbar_paint_overlay(vg_widget_t *widget, void *canvas);
static bool toolbar_handle_event(vg_widget_t *widget, vg_event_t *event);
static float get_item_width(vg_toolbar_t *tb, vg_toolbar_item_t *item);
static float get_item_height(vg_toolbar_t *tb, vg_toolbar_item_t *item);

//=============================================================================
// Toolbar VTable
//=============================================================================

static vg_widget_vtable_t g_toolbar_vtable = {.destroy = toolbar_destroy,
                                              .measure = toolbar_measure,
                                              .arrange = toolbar_arrange,
                                              .paint = toolbar_paint,
                                              .paint_overlay = toolbar_paint_overlay,
                                              .handle_event = toolbar_handle_event,
                                              .can_focus = NULL,
                                              .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

static bool ensure_item_capacity(vg_toolbar_t *tb, size_t needed) {
    if (needed <= tb->item_capacity)
        return true;

    size_t new_capacity = tb->item_capacity == 0 ? INITIAL_ITEM_CAPACITY : tb->item_capacity * 2;
    while (new_capacity < needed) {
        new_capacity *= 2;
    }

    vg_toolbar_item_t **new_items = realloc(tb->items, new_capacity * sizeof(vg_toolbar_item_t *));
    if (!new_items)
        return false;

    tb->items = new_items;
    tb->item_capacity = new_capacity;
    return true;
}

static void free_item(vg_toolbar_item_t *item) {
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

static vg_toolbar_item_t *create_item(vg_toolbar_item_type_t type, const char *id) {
    vg_toolbar_item_t *item = calloc(1, sizeof(vg_toolbar_item_t));
    if (!item)
        return NULL;

    item->type = type;
    item->owner = NULL;
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

static uint32_t get_icon_pixels(vg_toolbar_icon_size_t size) {
    switch (size) {
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

static float toolbar_ui_scale(void) {
    vg_theme_t *theme = vg_theme_get_current();
    float scale = theme ? theme->ui_scale : 1.0f;
    return scale > 0.0f ? scale : 1.0f;
}

static float get_scaled_icon_pixels(vg_toolbar_t *tb) {
    return (float)get_icon_pixels(tb->icon_size) * toolbar_ui_scale();
}

static float get_overflow_button_extent(vg_toolbar_t *tb) {
    float icon_px = get_scaled_icon_pixels(tb);
    float extent = icon_px + (float)tb->item_padding * 2.0f;
    return extent > 18.0f ? extent : 18.0f;
}

static float get_item_extent(vg_toolbar_t *tb, vg_toolbar_item_t *item) {
    return tb->orientation == VG_TOOLBAR_HORIZONTAL ? get_item_width(tb, item) : get_item_height(tb, item);
}

static int toolbar_visible_limit(vg_toolbar_t *tb) {
    return tb->overflow_start_index >= 0 ? tb->overflow_start_index : (int)tb->item_count;
}

static float toolbar_primary_available(vg_toolbar_t *tb) {
    return tb->orientation == VG_TOOLBAR_HORIZONTAL ? tb->base.width : tb->base.height;
}

static float toolbar_spacer_extent(vg_toolbar_t *tb, int max_index) {
    int spacer_count = 0;
    int visible_items = 0;
    float used_extent = 0.0f;

    for (int i = 0; i < max_index; i++) {
        vg_toolbar_item_t *item = tb->items[i];
        if (!item)
            continue;
        if (item->type == VG_TOOLBAR_ITEM_SPACER) {
            spacer_count++;
            visible_items++;
            continue;
        }
        float extent = get_item_extent(tb, item);
        if (extent <= 0.0f)
            continue;
        used_extent += extent;
        visible_items++;
    }

    if (spacer_count == 0)
        return 0.0f;

    float spacing_total =
        visible_items > 1 ? (float)(visible_items - 1) * (float)tb->item_spacing : 0.0f;
    float available = toolbar_primary_available(tb);
    if (tb->overflow_start_index >= 0)
        available -= get_overflow_button_extent(tb);
    float extra = available - used_extent - spacing_total;
    if (extra < 0.0f)
        extra = 0.0f;
    return extra / (float)spacer_count;
}

static float toolbar_layout_extent(vg_toolbar_t *tb,
                                   vg_toolbar_item_t *item,
                                   int max_index,
                                   float spacer_extent) {
    (void)max_index;
    if (!item)
        return 0.0f;
    if (item->type == VG_TOOLBAR_ITEM_SPACER)
        return spacer_extent;
    return get_item_extent(tb, item);
}

static bool toolbar_get_item_rect(vg_toolbar_t *tb,
                                  vg_toolbar_item_t *target,
                                  float *out_x,
                                  float *out_y,
                                  float *out_w,
                                  float *out_h) {
    int max_index = toolbar_visible_limit(tb);
    float spacer_extent = toolbar_spacer_extent(tb, max_index);
    float pos = 0.0f;

    for (int i = 0; i < max_index; i++) {
        vg_toolbar_item_t *item = tb->items[i];
        float item_width = 0.0f;
        float item_height = 0.0f;
        float item_x = 0.0f;
        float item_y = 0.0f;
        float extent = toolbar_layout_extent(tb, item, max_index, spacer_extent);

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
            item_width = extent;
            item_height = tb->base.height - 4.0f;
            if (item_height < 0.0f)
                item_height = 0.0f;
            item_x = pos;
            item_y = 2.0f;
        } else {
            item_width = tb->base.width - 4.0f;
            if (item_width < 0.0f)
                item_width = 0.0f;
            item_height = extent;
            item_x = 2.0f;
            item_y = pos;
        }

        if (item == target) {
            if (out_x)
                *out_x = item_x;
            if (out_y)
                *out_y = item_y;
            if (out_w)
                *out_w = item_width;
            if (out_h)
                *out_h = item_height;
            return true;
        }

        pos += extent + (float)tb->item_spacing;
    }

    return false;
}

static void toolbar_destroy_contextmenu_tree(vg_contextmenu_t *menu) {
    if (!menu)
        return;

    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item && item->submenu)
            toolbar_destroy_contextmenu_tree((vg_contextmenu_t *)item->submenu);
    }
    vg_contextmenu_destroy(menu);
}

static void toolbar_dropdown_menu_on_select(vg_contextmenu_t *menu,
                                            vg_menu_item_t *menu_item,
                                            void *user_data) {
    (void)menu;
    vg_toolbar_t *tb = (vg_toolbar_t *)user_data;
    vg_menu_item_t *source = menu_item ? (vg_menu_item_t *)menu_item->action_data : NULL;
    if (source) {
        source->was_clicked = true;
        if (source->action)
            source->action(source->action_data);
    }
    if (tb)
        tb->base.needs_paint = true;
}

static void toolbar_attach_dropdown_callbacks(vg_contextmenu_t *menu, vg_toolbar_t *tb) {
    if (!menu)
        return;
    vg_contextmenu_set_on_select(menu, toolbar_dropdown_menu_on_select, tb);
    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item && item->submenu)
            toolbar_attach_dropdown_callbacks((vg_contextmenu_t *)item->submenu, tb);
    }
}

static vg_contextmenu_t *toolbar_clone_menu_tree(vg_menu_t *menu, vg_toolbar_t *tb) {
    if (!menu)
        return NULL;

    vg_contextmenu_t *popup = vg_contextmenu_create();
    if (!popup)
        return NULL;

    vg_contextmenu_set_font(popup, tb->font, tb->font_size);
    for (vg_menu_item_t *item = menu->first_item; item; item = item->next) {
        if (item->separator) {
            vg_contextmenu_add_separator(popup);
            continue;
        }

        if (item->submenu) {
            vg_contextmenu_t *submenu = toolbar_clone_menu_tree(item->submenu, tb);
            if (!submenu)
                continue;
            vg_menu_item_t *clone = vg_contextmenu_add_submenu(popup, item->text, submenu);
            if (!clone) {
                toolbar_destroy_contextmenu_tree(submenu);
                continue;
            }
            vg_contextmenu_item_set_enabled(clone, item->enabled);
            vg_contextmenu_item_set_checked(clone, item->checked);
            if (item->icon.type != VG_ICON_NONE)
                vg_contextmenu_item_set_icon(clone, item->icon);
            continue;
        }

        vg_menu_item_t *clone =
            vg_contextmenu_add_item(popup, item->text, item->shortcut, NULL, item);
        if (!clone)
            continue;
        clone->action_data = item;
        vg_contextmenu_item_set_enabled(clone, item->enabled);
        vg_contextmenu_item_set_checked(clone, item->checked);
        if (item->icon.type != VG_ICON_NONE)
            vg_contextmenu_item_set_icon(clone, item->icon);
    }

    toolbar_attach_dropdown_callbacks(popup, tb);
    return popup;
}

static void measure_toolbar_item_widget(vg_toolbar_t *tb,
                                        vg_toolbar_item_t *item,
                                        float available_width,
                                        float available_height) {
    if (!item || item->type != VG_TOOLBAR_ITEM_WIDGET || !item->custom_widget)
        return;
    vg_widget_measure(item->custom_widget, available_width, available_height);
    (void)tb;
}

static void toolbar_compute_overflow(vg_toolbar_t *tb, float available_primary) {
    tb->overflow_start_index = -1;
    if (!tb->overflow_menu)
        return;

    float total_extent = 0.0f;
    for (size_t i = 0; i < tb->item_count; i++) {
        float item_extent = get_item_extent(tb, tb->items[i]);
        if (item_extent <= 0.0f)
            continue;
        total_extent += item_extent;
        if (i + 1 < tb->item_count)
            total_extent += (float)tb->item_spacing;
    }

    if (total_extent <= available_primary)
        return;

    float limit = available_primary - get_overflow_button_extent(tb);
    if (limit < 0.0f)
        limit = 0.0f;

    float pos = 0.0f;
    for (size_t i = 0; i < tb->item_count; i++) {
        float item_extent = get_item_extent(tb, tb->items[i]);
        if (item_extent <= 0.0f)
            continue;
        if (pos + item_extent > limit) {
            tb->overflow_start_index = (int)i;
            return;
        }
        pos += item_extent + (float)tb->item_spacing;
    }
}

static void toolbar_get_overflow_button_rect(vg_toolbar_t *tb,
                                             float *out_x,
                                             float *out_y,
                                             float *out_w,
                                             float *out_h) {
    float extent = get_overflow_button_extent(tb);
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
        w = extent;
        h = tb->base.height - 4.0f;
        if (h < 0.0f)
            h = 0.0f;
        x = tb->base.width - w;
        y = 2.0f;
    } else {
        w = tb->base.width - 4.0f;
        if (w < 0.0f)
            w = 0.0f;
        h = extent;
        x = 2.0f;
        y = tb->base.height - h;
    }

    if (out_x)
        *out_x = x;
    if (out_y)
        *out_y = y;
    if (out_w)
        *out_w = w;
    if (out_h)
        *out_h = h;
}

static bool point_in_rect(float px, float py, float x, float y, float w, float h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

static bool toolbar_overflow_button_hit(vg_toolbar_t *tb, float px, float py) {
    if (!tb || tb->overflow_start_index < 0)
        return false;

    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    toolbar_get_overflow_button_rect(tb, &x, &y, &w, &h);
    return point_in_rect(px, py, x, y, w, h);
}

static const char *toolbar_item_menu_label(vg_toolbar_item_t *item) {
    if (!item)
        return "Item";
    if (item->label && item->label[0] != '\0')
        return item->label;
    if (item->tooltip && item->tooltip[0] != '\0')
        return item->tooltip;
    if (item->id && item->id[0] != '\0')
        return item->id;
    switch (item->type) {
        case VG_TOOLBAR_ITEM_BUTTON:
            return "Button";
        case VG_TOOLBAR_ITEM_TOGGLE:
            return "Toggle";
        case VG_TOOLBAR_ITEM_DROPDOWN:
            return "Menu";
        default:
            return "Item";
    }
}

static void toolbar_dismiss_dropdown_popup(vg_toolbar_t *tb) {
    if (!tb || !tb->dropdown_popup)
        return;
    if (tb->dropdown_popup->is_visible)
        vg_contextmenu_dismiss(tb->dropdown_popup);
    toolbar_destroy_contextmenu_tree(tb->dropdown_popup);
    tb->dropdown_popup = NULL;
    tb->dropdown_item = NULL;
}

static void toolbar_sync_popup_capture(vg_toolbar_t *tb) {
    if (!tb)
        return;
    bool overflow_visible = tb->overflow_popup && tb->overflow_popup->is_visible;
    bool dropdown_visible = tb->dropdown_popup && tb->dropdown_popup->is_visible;
    if (tb->dropdown_popup && !dropdown_visible)
        toolbar_dismiss_dropdown_popup(tb);
    if ((overflow_visible || dropdown_visible) || vg_widget_get_input_capture() != &tb->base)
        return;
    vg_widget_release_input_capture();
}

static void toolbar_show_dropdown_popup(vg_toolbar_t *tb, vg_toolbar_item_t *item) {
    if (!tb || !item || item->type != VG_TOOLBAR_ITEM_DROPDOWN || !item->dropdown_menu)
        return;

    if (tb->dropdown_item == item && tb->dropdown_popup && tb->dropdown_popup->is_visible) {
        toolbar_dismiss_dropdown_popup(tb);
        toolbar_sync_popup_capture(tb);
        tb->base.needs_paint = true;
        return;
    }

    toolbar_dismiss_dropdown_popup(tb);
    tb->dropdown_popup = toolbar_clone_menu_tree(item->dropdown_menu, tb);
    tb->dropdown_item = item;
    if (!tb->dropdown_popup || tb->dropdown_popup->item_count == 0) {
        toolbar_dismiss_dropdown_popup(tb);
        return;
    }

    float item_x = 0.0f;
    float item_y = 0.0f;
    float item_w = 0.0f;
    float item_h = 0.0f;
    if (!toolbar_get_item_rect(tb, item, &item_x, &item_y, &item_w, &item_h)) {
        toolbar_get_overflow_button_rect(tb, &item_x, &item_y, &item_w, &item_h);
    }

    int popup_x = (int)(tb->base.x + item_x);
    int popup_y = (int)(tb->base.y + item_y + item_h);
    if (tb->orientation == VG_TOOLBAR_VERTICAL) {
        popup_x = (int)(tb->base.x + item_x + item_w);
        popup_y = (int)(tb->base.y + item_y);
    }

    vg_contextmenu_show_at(tb->dropdown_popup, popup_x, popup_y);
    vg_widget_set_input_capture(&tb->base);
    tb->base.needs_paint = true;
}

static void toolbar_activate_item(vg_toolbar_t *tb, vg_toolbar_item_t *item) {
    if (!tb || !item || !item->enabled)
        return;

    item->was_clicked = true;
    switch (item->type) {
        case VG_TOOLBAR_ITEM_BUTTON:
            if (item->on_click)
                item->on_click(item, item->user_data);
            break;
        case VG_TOOLBAR_ITEM_TOGGLE:
            item->checked = !item->checked;
            if (item->on_toggle)
                item->on_toggle(item, item->checked, item->user_data);
            break;
        case VG_TOOLBAR_ITEM_DROPDOWN:
            if (item->dropdown_menu) {
                toolbar_show_dropdown_popup(tb, item);
            } else if (item->on_click) {
                item->on_click(item, item->user_data);
            }
            break;
        default:
            break;
    }

    tb->base.needs_paint = true;
}

static void toolbar_overflow_on_select(vg_contextmenu_t *menu,
                                       vg_menu_item_t *menu_item,
                                       void *user_data) {
    (void)menu;
    vg_toolbar_t *tb = (vg_toolbar_t *)user_data;
    vg_toolbar_item_t *item = menu_item ? (vg_toolbar_item_t *)menu_item->action_data : NULL;
    toolbar_activate_item(tb, item);
}

static void toolbar_ensure_overflow_popup(vg_toolbar_t *tb) {
    if (!tb || tb->overflow_popup)
        return;

    tb->overflow_popup = vg_contextmenu_create();
    if (!tb->overflow_popup)
        return;
    vg_contextmenu_set_on_select(tb->overflow_popup, toolbar_overflow_on_select, tb);
    vg_contextmenu_set_font(tb->overflow_popup, tb->font, tb->font_size);
    tb->overflow_popup_dirty = true;
}

static void toolbar_rebuild_overflow_popup(vg_toolbar_t *tb) {
    toolbar_ensure_overflow_popup(tb);
    if (!tb || !tb->overflow_popup)
        return;

    vg_contextmenu_clear(tb->overflow_popup);
    vg_contextmenu_set_font(tb->overflow_popup, tb->font, tb->font_size);
    if (tb->overflow_start_index < 0) {
        tb->overflow_popup_dirty = false;
        return;
    }

    bool added_item = false;
    bool pending_separator = false;
    for (size_t i = (size_t)tb->overflow_start_index; i < tb->item_count; i++) {
        vg_toolbar_item_t *item = tb->items[i];
        if (!item)
            continue;

        if (item->type == VG_TOOLBAR_ITEM_SEPARATOR) {
            if (added_item)
                pending_separator = true;
            continue;
        }
        if (item->type == VG_TOOLBAR_ITEM_SPACER || item->type == VG_TOOLBAR_ITEM_WIDGET)
            continue;

        if (pending_separator) {
            vg_contextmenu_add_separator(tb->overflow_popup);
            pending_separator = false;
        }

        vg_menu_item_t *menu_item = vg_contextmenu_add_item(
            tb->overflow_popup, toolbar_item_menu_label(item), NULL, NULL, item);
        if (!menu_item)
            continue;
        menu_item->action_data = item;
        vg_contextmenu_item_set_enabled(menu_item, item->enabled);
        if (item->type == VG_TOOLBAR_ITEM_TOGGLE)
            vg_contextmenu_item_set_checked(menu_item, item->checked);
        if (item->icon.type != VG_ICON_NONE)
            vg_contextmenu_item_set_icon(menu_item, item->icon);
        added_item = true;
    }

    tb->overflow_popup_dirty = false;
}

static void toolbar_dismiss_overflow_popup(vg_toolbar_t *tb) {
    if (!tb || !tb->overflow_popup || !tb->overflow_popup->is_visible)
        return;
    vg_contextmenu_dismiss(tb->overflow_popup);
    toolbar_sync_popup_capture(tb);
    tb->base.needs_paint = true;
}

static void toolbar_show_overflow_popup(vg_toolbar_t *tb) {
    if (!tb || tb->overflow_start_index < 0)
        return;
    toolbar_dismiss_dropdown_popup(tb);
    if (tb->overflow_popup_dirty || !tb->overflow_popup)
        toolbar_rebuild_overflow_popup(tb);
    if (!tb->overflow_popup || tb->overflow_popup->item_count == 0)
        return;

    float button_x = 0.0f;
    float button_y = 0.0f;
    float button_w = 0.0f;
    float button_h = 0.0f;
    toolbar_get_overflow_button_rect(tb, &button_x, &button_y, &button_w, &button_h);

    int popup_x = (int)(tb->base.x + button_x);
    int popup_y = (int)(tb->base.y + button_y + button_h);
    if (tb->orientation == VG_TOOLBAR_VERTICAL) {
        popup_x = (int)(tb->base.x + button_x + button_w);
        popup_y = (int)(tb->base.y + button_y);
    }

    vg_contextmenu_show_at(tb->overflow_popup, popup_x, popup_y);
    vg_widget_set_input_capture(&tb->base);
    tb->base.needs_paint = true;
}

static bool toolbar_forward_popup_event(vg_toolbar_t *tb, vg_event_t *event) {
    vg_contextmenu_t *popup = NULL;
    if (!tb || !event)
        return false;
    if (tb->dropdown_popup && tb->dropdown_popup->is_visible)
        popup = tb->dropdown_popup;
    else if (tb->overflow_popup && tb->overflow_popup->is_visible)
        popup = tb->overflow_popup;
    if (!popup)
        return false;
    if (!(event->type == VG_EVENT_MOUSE_MOVE || event->type == VG_EVENT_MOUSE_DOWN ||
          event->type == VG_EVENT_MOUSE_UP || event->type == VG_EVENT_CLICK ||
          event->type == VG_EVENT_KEY_DOWN || event->type == VG_EVENT_KEY_UP))
        return false;

    vg_event_t translated = *event;
    if (event->type == VG_EVENT_MOUSE_MOVE || event->type == VG_EVENT_MOUSE_DOWN ||
        event->type == VG_EVENT_MOUSE_UP || event->type == VG_EVENT_CLICK) {
        translated.mouse.x = event->mouse.screen_x;
        translated.mouse.y = event->mouse.screen_y;
        translated.mouse.screen_x = event->mouse.screen_x;
        translated.mouse.screen_y = event->mouse.screen_y;
    }

    if (!popup->base.vtable || !popup->base.vtable->handle_event)
        return false;
    return popup->base.vtable->handle_event(&popup->base, &translated);
}

static float get_item_width(vg_toolbar_t *tb, vg_toolbar_item_t *item) {
    float icon_px = get_scaled_icon_pixels(tb);
    float padding = (float)tb->item_padding;
    float spacing = (float)tb->item_spacing;

    switch (item->type) {
        case VG_TOOLBAR_ITEM_SEPARATOR:
            return toolbar_ui_scale() + spacing * 2.0f;

        case VG_TOOLBAR_ITEM_SPACER:
            return 0; // Spacers expand

        case VG_TOOLBAR_ITEM_BUTTON:
        case VG_TOOLBAR_ITEM_TOGGLE:
        case VG_TOOLBAR_ITEM_DROPDOWN: {
            float width = padding * 2.0f;
            if (item->icon.type != VG_ICON_NONE) {
                width += icon_px;
            }
            if (item->show_label && item->label && tb->font) {
                vg_text_metrics_t metrics;
                vg_font_measure_text(tb->font, tb->font_size, item->label, &metrics);
                if (item->icon.type != VG_ICON_NONE) {
                    width += padding;
                }
                width += metrics.width;
            }
            if (item->type == VG_TOOLBAR_ITEM_DROPDOWN) {
                width += 12.0f * toolbar_ui_scale(); // Arrow indicator
            }
            return width;
        }

        case VG_TOOLBAR_ITEM_WIDGET:
            if (item->custom_widget) {
                return item->custom_widget->measured_width + padding * 2;
            }
            return 0;

        default:
            return 0;
    }
}

static float get_item_height(vg_toolbar_t *tb, vg_toolbar_item_t *item) {
    float icon_px = get_scaled_icon_pixels(tb);
    float padding = (float)tb->item_padding;

    switch (item->type) {
        case VG_TOOLBAR_ITEM_SEPARATOR:
            return icon_px + padding * 2.0f;

        case VG_TOOLBAR_ITEM_SPACER:
            return 0;

        case VG_TOOLBAR_ITEM_BUTTON:
        case VG_TOOLBAR_ITEM_TOGGLE:
        case VG_TOOLBAR_ITEM_DROPDOWN: {
            float content_height = item->icon.type != VG_ICON_NONE ? icon_px : 0.0f;
            if (item->show_label && item->label && tb->font) {
                vg_font_metrics_t metrics;
                vg_font_get_metrics(tb->font, tb->font_size, &metrics);
                float text_height = metrics.ascent - metrics.descent;
                if (text_height > content_height)
                    content_height = text_height;
            }
            if (content_height <= 0.0f)
                content_height = icon_px;
            return content_height + padding * 2.0f;
        }

        case VG_TOOLBAR_ITEM_WIDGET:
            if (item->custom_widget) {
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

vg_toolbar_t *vg_toolbar_create(vg_widget_t *parent, vg_toolbar_orientation_t orientation) {
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

    // Configuration — scale pixel constants by ui_scale for HiDPI displays.
    float s = theme->ui_scale > 0.0f ? theme->ui_scale : 1.0f;
    tb->orientation = orientation;
    tb->icon_size = VG_TOOLBAR_ICONS_MEDIUM;
    tb->item_padding = (int)(TOOLBAR_DEFAULT_PADDING * s);
    tb->item_spacing = (int)(TOOLBAR_DEFAULT_SPACING * s);
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
    if (parent) {
        vg_widget_add_child(parent, &tb->base);
    }

    return tb;
}

static void toolbar_destroy(vg_widget_t *widget) {
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;

    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();
    toolbar_dismiss_dropdown_popup(tb);
    if (tb->overflow_popup) {
        vg_contextmenu_destroy(tb->overflow_popup);
        tb->overflow_popup = NULL;
    }

    for (size_t i = 0; i < tb->item_count; i++) {
        free_item(tb->items[i]);
    }
    free(tb->items);
}

static void toolbar_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;
    for (size_t i = 0; i < tb->item_count; i++)
        measure_toolbar_item_widget(tb, tb->items[i], available_width, available_height);

    float scale = toolbar_ui_scale();
    float icon_px = get_scaled_icon_pixels(tb);
    float bar_thickness = icon_px + (float)tb->item_padding * 2.0f + 4.0f * scale;

    if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
        // Calculate total width of items
        float total_width = 0;
        for (size_t i = 0; i < tb->item_count; i++) {
            float item_width = get_item_width(tb, tb->items[i]);
            if (item_width > 0) {
                total_width += item_width + tb->item_spacing;
            }
        }
        widget->measured_width = total_width > 0 ? total_width - tb->item_spacing : available_width;
        widget->measured_height = bar_thickness;
    } else {
        // Vertical orientation
        float total_height = 0;
        for (size_t i = 0; i < tb->item_count; i++) {
            float item_height = get_item_height(tb, tb->items[i]);
            if (item_height > 0) {
                total_height += item_height + tb->item_spacing;
            }
        }
        widget->measured_width = bar_thickness;
        widget->measured_height =
            total_height > 0 ? total_height - tb->item_spacing : available_height;
    }
}

static void toolbar_arrange(vg_widget_t *widget, float x, float y, float width, float height) {
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    for (size_t i = 0; i < tb->item_count; i++)
        measure_toolbar_item_widget(tb, tb->items[i], width, height);

    toolbar_compute_overflow(
        tb, tb->orientation == VG_TOOLBAR_HORIZONTAL ? width : height);

    // Arrange custom widgets within toolbar items
    float pos = 0.0f;
    int max_index = toolbar_visible_limit(tb);
    float spacer_extent = toolbar_spacer_extent(tb, max_index);
    for (int i = 0; i < max_index; i++) {
        vg_toolbar_item_t *item = tb->items[i];
        float extent = toolbar_layout_extent(tb, item, max_index, spacer_extent);

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
            float item_width = extent;
            if (item->type == VG_TOOLBAR_ITEM_WIDGET && item->custom_widget) {
                float iw = item->custom_widget->measured_width;
                float ih = item->custom_widget->measured_height;
                float ix = x + pos + (item_width - iw) / 2;
                float iy = y + (height - ih) / 2;
                vg_widget_arrange(item->custom_widget, ix, iy, iw, ih);
            }

            pos += item_width + tb->item_spacing;
        } else {
            float item_height = extent;
            if (item->type == VG_TOOLBAR_ITEM_WIDGET && item->custom_widget) {
                float iw = item->custom_widget->measured_width;
                float ih = item->custom_widget->measured_height;
                float ix = x + (width - iw) / 2;
                float iy = y + pos + (item_height - ih) / 2;
                vg_widget_arrange(item->custom_widget, ix, iy, iw, ih);
            }

            pos += item_height + tb->item_spacing;
        }
    }

    if (tb->overflow_popup_dirty && tb->overflow_popup && tb->overflow_popup->is_visible)
        toolbar_rebuild_overflow_popup(tb);
}

static void toolbar_paint(vg_widget_t *widget, void *canvas) {
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;

    // Draw toolbar background
    vgfx_fill_rect(win,
                   (int32_t)widget->x,
                   (int32_t)widget->y,
                   (int32_t)widget->width,
                   (int32_t)widget->height,
                   tb->bg_color);

    float icon_px = get_scaled_icon_pixels(tb);

    float pos = 0.0f;
    int max_index = toolbar_visible_limit(tb);
    float spacer_extent = toolbar_spacer_extent(tb, max_index);

    for (int i = 0; i < max_index; i++) {
        vg_toolbar_item_t *item = tb->items[i];

        float item_width, item_height;
        float item_x, item_y;
        float extent = toolbar_layout_extent(tb, item, max_index, spacer_extent);

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
            item_width = extent;
            item_height = widget->height - 4;
            item_x = widget->x + pos;
            item_y = widget->y + 2;
        } else {
            item_width = widget->width - 4;
            item_height = extent;
            item_x = widget->x + 2;
            item_y = widget->y + pos;
        }

        switch (item->type) {
            case VG_TOOLBAR_ITEM_SEPARATOR: {
                // Draw vertical or horizontal line
                vg_theme_t *theme = vg_theme_get_current();
                uint32_t sep_color = theme->colors.border_primary;
                if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
                    int32_t sep_x = (int32_t)(item_x + item_width / 2);
                    int32_t sep_y1 = (int32_t)(widget->y + 4);
                    int32_t sep_y2 = (int32_t)(widget->y + widget->height - 4);
                    vgfx_fill_rect(win, sep_x, sep_y1, 1, sep_y2 - sep_y1, sep_color);
                } else {
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
            case VG_TOOLBAR_ITEM_DROPDOWN: {
                // Draw button background based on state
                uint32_t btn_bg = 0; // No background by default
                if (item == tb->pressed_item) {
                    btn_bg = tb->active_color;
                } else if (item == tb->hovered_item) {
                    btn_bg = tb->hover_color;
                } else if (item->type == VG_TOOLBAR_ITEM_TOGGLE && item->checked) {
                    btn_bg = tb->active_color;
                }

                if (btn_bg != 0) {
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

                if (item->show_label && item->label) {
                    // Icon on left, label on right
                    icon_x = item_x + tb->item_padding;
                }

                switch (item->icon.type) {
                    case VG_ICON_GLYPH:
                        // Draw glyph using font
                        if (tb->font) {
                            char buf[8] = {0};
                            // UTF-8 encode the glyph
                            uint32_t cp = item->icon.data.glyph;
                            if (cp < 0x80) {
                                buf[0] = (char)cp;
                            } else if (cp < 0x800) {
                                buf[0] = (char)(0xC0 | (cp >> 6));
                                buf[1] = (char)(0x80 | (cp & 0x3F));
                            } else if (cp < 0x10000) {
                                buf[0] = (char)(0xE0 | (cp >> 12));
                                buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[2] = (char)(0x80 | (cp & 0x3F));
                            } else {
                                buf[0] = (char)(0xF0 | (cp >> 18));
                                buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                buf[3] = (char)(0x80 | (cp & 0x3F));
                            }
                            vg_font_draw_text(canvas,
                                              tb->font,
                                              icon_px,
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
                if (item->show_label && item->label && tb->font) {
                    float label_x = (item->icon.type == VG_ICON_NONE)
                                        ? item_x + tb->item_padding
                                        : icon_x + icon_px + tb->item_padding;
                    vg_font_metrics_t font_metrics;
                    vg_font_get_metrics(tb->font, tb->font_size, &font_metrics);
                    float label_y =
                        item_y + (item_height + font_metrics.ascent + font_metrics.descent) / 2.0f;
                    vg_font_draw_text(
                        canvas, tb->font, tb->font_size, label_x, label_y, item->label, txt_color);
                }

                // Draw dropdown arrow
                if (item->type == VG_TOOLBAR_ITEM_DROPDOWN) {
                    // Draw small triangle at right edge
                    float arrow_x = item_x + item_width - 8;
                    float arrow_y = item_y + item_height / 2;
                    vgfx_fill_rect(win, (int32_t)arrow_x, (int32_t)(arrow_y - 1), 5, 1, txt_color);
                    vgfx_fill_rect(win, (int32_t)(arrow_x + 1), (int32_t)arrow_y, 3, 1, txt_color);
                    vgfx_fill_rect(
                        win, (int32_t)(arrow_x + 2), (int32_t)(arrow_y + 1), 1, 1, txt_color);
                }
                break;
            }

            case VG_TOOLBAR_ITEM_WIDGET:
                // Custom widget draws itself
                if (item->custom_widget) {
                    vg_widget_paint(item->custom_widget, canvas);
                }
                break;
        }

        pos += extent + tb->item_spacing;
    }

    // Draw overflow button if needed
    if (tb->overflow_start_index >= 0) {
        float ov_x = 0.0f;
        float ov_y = 0.0f;
        float ov_w = 0.0f;
        float ov_h = 0.0f;
        toolbar_get_overflow_button_rect(tb, &ov_x, &ov_y, &ov_w, &ov_h);

        uint32_t ov_bg = 0;
        if (tb->overflow_popup && tb->overflow_popup->is_visible)
            ov_bg = tb->active_color;
        else if (tb->overflow_button_hovered)
            ov_bg = tb->hover_color;
        if (ov_bg != 0) {
            vgfx_fill_rect(win,
                           (int32_t)(widget->x + ov_x),
                           (int32_t)(widget->y + ov_y),
                           (int32_t)ov_w,
                           (int32_t)ov_h,
                           ov_bg);
        }

        float dot_y = widget->y + ov_y + ov_h / 2.0f;
        if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
            float center_x = widget->x + ov_x + ov_w / 2.0f;
            vgfx_fill_rect(win, (int32_t)(center_x - 6.0f), (int32_t)dot_y, 2, 2, tb->text_color);
            vgfx_fill_rect(win, (int32_t)(center_x - 1.0f), (int32_t)dot_y, 2, 2, tb->text_color);
            vgfx_fill_rect(win, (int32_t)(center_x + 4.0f), (int32_t)dot_y, 2, 2, tb->text_color);
        } else {
            float center_x = widget->x + ov_x + ov_w / 2.0f;
            float center_y = widget->y + ov_y + ov_h / 2.0f;
            vgfx_fill_rect(win, (int32_t)center_x, (int32_t)(center_y - 6.0f), 2, 2, tb->text_color);
            vgfx_fill_rect(win, (int32_t)center_x, (int32_t)(center_y - 1.0f), 2, 2, tb->text_color);
            vgfx_fill_rect(win, (int32_t)center_x, (int32_t)(center_y + 4.0f), 2, 2, tb->text_color);
        }
    }
}

static void toolbar_paint_overlay(vg_widget_t *widget, void *canvas) {
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;
    if (tb->overflow_popup && tb->overflow_popup->is_visible && tb->overflow_popup->base.vtable &&
        tb->overflow_popup->base.vtable->paint) {
        tb->overflow_popup->base.vtable->paint(&tb->overflow_popup->base, canvas);
    }
    if (tb->dropdown_popup && tb->dropdown_popup->is_visible && tb->dropdown_popup->base.vtable &&
        tb->dropdown_popup->base.vtable->paint) {
        tb->dropdown_popup->base.vtable->paint(&tb->dropdown_popup->base, canvas);
    }
}

static vg_toolbar_item_t *find_item_at(vg_toolbar_t *tb, float px, float py) {
    float pos = 0.0f;
    int max_index = toolbar_visible_limit(tb);
    float spacer_extent = toolbar_spacer_extent(tb, max_index);

    for (int i = 0; i < max_index; i++) {
        vg_toolbar_item_t *item = tb->items[i];

        float item_width, item_height;
        float item_x, item_y;
        float extent = toolbar_layout_extent(tb, item, max_index, spacer_extent);

        if (tb->orientation == VG_TOOLBAR_HORIZONTAL) {
            item_width = extent;
            item_height = tb->base.height - 4.0f;
            if (item_height < 0.0f)
                item_height = 0.0f;
            item_x = pos;
            item_y = 2.0f;
        } else {
            item_width = tb->base.width - 4.0f;
            if (item_width < 0.0f)
                item_width = 0.0f;
            item_height = extent;
            item_x = 2.0f;
            item_y = pos;
        }

        if (px >= item_x && px < item_x + item_width && py >= item_y && py < item_y + item_height) {
            if (item->type != VG_TOOLBAR_ITEM_SEPARATOR && item->type != VG_TOOLBAR_ITEM_SPACER) {
                return item;
            }
        }

        pos += extent + tb->item_spacing;
    }

    return NULL;
}

static bool toolbar_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_toolbar_t *tb = (vg_toolbar_t *)widget;
    bool popup_was_visible = (tb->overflow_popup && tb->overflow_popup->is_visible) ||
                             (tb->dropdown_popup && tb->dropdown_popup->is_visible);

    if (popup_was_visible) {
        bool popup_handled = toolbar_forward_popup_event(tb, event);
        toolbar_sync_popup_capture(tb);
        if (popup_handled) {
            widget->needs_paint = true;
            return true;
        }
        if (event->type == VG_EVENT_MOUSE_DOWN &&
            toolbar_overflow_button_hit(tb, event->mouse.x, event->mouse.y)) {
            widget->needs_paint = true;
            return true;
        }
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            vg_toolbar_item_t *item = find_item_at(tb, event->mouse.x, event->mouse.y);
            bool overflow_hovered = toolbar_overflow_button_hit(tb, event->mouse.x, event->mouse.y);
            if (item != tb->hovered_item || overflow_hovered != tb->overflow_button_hovered) {
                tb->hovered_item = item;
                tb->overflow_button_hovered = overflow_hovered;
                widget->needs_paint = true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (tb->hovered_item) {
                tb->hovered_item = NULL;
                widget->needs_paint = true;
            }
            if (tb->overflow_button_hovered) {
                tb->overflow_button_hovered = false;
                widget->needs_paint = true;
            }
            if (tb->pressed_item) {
                tb->pressed_item = NULL;
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_MOUSE_DOWN: {
            if (toolbar_overflow_button_hit(tb, event->mouse.x, event->mouse.y)) {
                toolbar_dismiss_dropdown_popup(tb);
                if (tb->overflow_popup && tb->overflow_popup->is_visible)
                    toolbar_dismiss_overflow_popup(tb);
                else
                    toolbar_show_overflow_popup(tb);
                tb->pressed_item = NULL;
                return true;
            }

            vg_toolbar_item_t *item = find_item_at(tb, event->mouse.x, event->mouse.y);
            if (item && item->enabled) {
                toolbar_dismiss_overflow_popup(tb);
                if (item->type != VG_TOOLBAR_ITEM_DROPDOWN || item != tb->dropdown_item)
                    toolbar_dismiss_dropdown_popup(tb);
                tb->pressed_item = item;
                widget->needs_paint = true;
                return true;
            }
            toolbar_dismiss_overflow_popup(tb);
            toolbar_dismiss_dropdown_popup(tb);
            toolbar_sync_popup_capture(tb);
            return false;
        }

        case VG_EVENT_MOUSE_UP: {
            if (toolbar_overflow_button_hit(tb, event->mouse.x, event->mouse.y)) {
                tb->pressed_item = NULL;
                return true;
            }
            vg_toolbar_item_t *item = find_item_at(tb, event->mouse.x, event->mouse.y);
            if (item && item == tb->pressed_item && item->enabled) {
                toolbar_activate_item(tb, item);
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
                                         void *user_data) {
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
    item->owner = tb;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_toggle(vg_toolbar_t *tb,
                                         const char *id,
                                         const char *label,
                                         vg_icon_t icon,
                                         bool initial_checked,
                                         void (*on_toggle)(vg_toolbar_item_t *, bool, void *),
                                         void *user_data) {
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
    item->owner = tb;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_dropdown(
    vg_toolbar_t *tb, const char *id, const char *label, vg_icon_t icon, struct vg_menu *menu) {
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
    item->owner = tb;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_separator(vg_toolbar_t *tb) {
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_SEPARATOR, NULL);
    if (!item)
        return NULL;
    item->owner = tb;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_spacer(vg_toolbar_t *tb) {
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_SPACER, NULL);
    if (!item)
        return NULL;
    item->owner = tb;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    return item;
}

vg_toolbar_item_t *vg_toolbar_add_widget(vg_toolbar_t *tb, const char *id, vg_widget_t *widget) {
    if (!tb)
        return NULL;
    if (!ensure_item_capacity(tb, tb->item_count + 1))
        return NULL;

    vg_toolbar_item_t *item = create_item(VG_TOOLBAR_ITEM_WIDGET, id);
    if (!item)
        return NULL;

    item->custom_widget = widget;
    item->owner = tb;

    tb->items[tb->item_count++] = item;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    return item;
}

/// @brief Toolbar remove item.
void vg_toolbar_remove_item(vg_toolbar_t *tb, const char *id) {
    if (!tb || !id)
        return;

    for (size_t i = 0; i < tb->item_count; i++) {
        if (tb->items[i]->id && strcmp(tb->items[i]->id, id) == 0) {
            if (tb->hovered_item == tb->items[i])
                tb->hovered_item = NULL;
            if (tb->pressed_item == tb->items[i])
                tb->pressed_item = NULL;
            if (tb->dropdown_item == tb->items[i])
                toolbar_dismiss_dropdown_popup(tb);
            free_item(tb->items[i]);
            memmove(&tb->items[i],
                    &tb->items[i + 1],
                    (tb->item_count - i - 1) * sizeof(vg_toolbar_item_t *));
            tb->item_count--;
            tb->base.needs_layout = true;
            tb->overflow_popup_dirty = true;
            toolbar_dismiss_overflow_popup(tb);
            return;
        }
    }
}

vg_toolbar_item_t *vg_toolbar_get_item(vg_toolbar_t *tb, const char *id) {
    if (!tb || !id)
        return NULL;

    for (size_t i = 0; i < tb->item_count; i++) {
        if (tb->items[i]->id && strcmp(tb->items[i]->id, id) == 0) {
            return tb->items[i];
        }
    }
    return NULL;
}

/// @brief Toolbar item set enabled.
void vg_toolbar_item_set_enabled(vg_toolbar_item_t *item, bool enabled) {
    if (!item)
        return;
    item->enabled = enabled;
    if (item->owner) {
        item->owner->overflow_popup_dirty = true;
        item->owner->base.needs_paint = true;
    }
}

/// @brief Toolbar item set checked.
void vg_toolbar_item_set_checked(vg_toolbar_item_t *item, bool checked) {
    if (!item)
        return;
    item->checked = checked;
    if (item->owner) {
        item->owner->overflow_popup_dirty = true;
        item->owner->base.needs_paint = true;
    }
}

/// @brief Toolbar item set tooltip.
void vg_toolbar_item_set_tooltip(vg_toolbar_item_t *item, const char *tooltip) {
    if (!item)
        return;
    if (item->tooltip)
        free(item->tooltip);
    item->tooltip = tooltip ? strdup(tooltip) : NULL;
    if (item->owner) {
        item->owner->overflow_popup_dirty = true;
        item->owner->base.needs_paint = true;
    }
}

/// @brief Toolbar item set icon.
void vg_toolbar_item_set_icon(vg_toolbar_item_t *item, vg_icon_t icon) {
    if (!item)
        return;
    vg_icon_destroy(&item->icon);
    item->icon = icon;
    if (item->owner) {
        item->owner->overflow_popup_dirty = true;
        item->owner->base.needs_layout = true;
        item->owner->base.needs_paint = true;
    }
}

/// @brief Toolbar set icon size.
void vg_toolbar_set_icon_size(vg_toolbar_t *tb, vg_toolbar_icon_size_t size) {
    if (!tb)
        return;
    tb->icon_size = size;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
}

/// @brief Toolbar set show labels.
void vg_toolbar_set_show_labels(vg_toolbar_t *tb, bool show) {
    if (!tb)
        return;
    tb->show_labels = show;
    for (size_t i = 0; i < tb->item_count; i++) {
        tb->items[i]->show_label = show;
    }
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
}

/// @brief Toolbar set font.
void vg_toolbar_set_font(vg_toolbar_t *tb, vg_font_t *font, float size) {
    if (!tb)
        return;
    tb->font = font;
    tb->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_small;
    tb->base.needs_layout = true;
    tb->overflow_popup_dirty = true;
    if (tb->overflow_popup)
        vg_contextmenu_set_font(tb->overflow_popup, tb->font, tb->font_size);
}

//=============================================================================
// Icon Helpers
//=============================================================================

vg_icon_t vg_icon_from_glyph(uint32_t codepoint) {
    vg_icon_t icon = {0};
    icon.type = VG_ICON_GLYPH;
    icon.data.glyph = codepoint;
    return icon;
}

vg_icon_t vg_icon_from_pixels(uint8_t *rgba, uint32_t w, uint32_t h) {
    vg_icon_t icon = {0};
    if (!rgba || w == 0 || h == 0)
        return icon;

    icon.type = VG_ICON_IMAGE;
    size_t size = w * h * 4;
    icon.data.image.pixels = malloc(size);
    if (icon.data.image.pixels) {
        memcpy(icon.data.image.pixels, rgba, size);
        icon.data.image.width = w;
        icon.data.image.height = h;
    } else {
        icon.type = VG_ICON_NONE;
    }
    return icon;
}

vg_icon_t vg_icon_from_file(const char *path) {
    vg_icon_t icon = {0};
    if (!path)
        return icon;

    icon.type = VG_ICON_PATH;
    icon.data.path = strdup(path);
    if (!icon.data.path) {
        icon.type = VG_ICON_NONE;
    }
    return icon;
}

/// @brief Icon destroy.
void vg_icon_destroy(vg_icon_t *icon) {
    if (!icon)
        return;

    switch (icon->type) {
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
