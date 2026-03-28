//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_listbox.c
//
//===----------------------------------------------------------------------===//
// vg_listbox.c - ListBox widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward declarations
//=============================================================================

static void listbox_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void listbox_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void listbox_paint(vg_widget_t *widget, void *canvas);
static bool listbox_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool listbox_can_focus(vg_widget_t *widget);
static void listbox_destroy(vg_widget_t *widget);

//=============================================================================
// VTable
//=============================================================================

static vg_widget_vtable_t g_listbox_vtable = {
    .destroy = listbox_destroy,
    .measure = listbox_measure,
    .arrange = listbox_arrange,
    .paint = listbox_paint,
    .handle_event = listbox_handle_event,
    .can_focus = listbox_can_focus,
    .on_focus = NULL,
};

//=============================================================================
// VTable Implementations
//=============================================================================

static void listbox_free_virtual_cache(vg_listbox_t *lb) {
    if (!lb->visible_cache)
        return;

    for (size_t i = 0; i < lb->cache_capacity; i++) {
        free(lb->visible_cache[i].text);
        lb->visible_cache[i].text = NULL;
        lb->visible_cache[i].selected = false;
    }
}

static size_t listbox_virtual_item_count(const vg_listbox_t *lb) {
    return lb->virtual_mode ? lb->total_item_count : (size_t)lb->item_count;
}

static void listbox_clamp_scroll(vg_listbox_t *lb, float viewport_height) {
    if (!lb)
        return;

    if (lb->scroll_y < 0.0f)
        lb->scroll_y = 0.0f;

    float max_scroll = (float)listbox_virtual_item_count(lb) * lb->item_height - viewport_height;
    if (max_scroll < 0.0f)
        max_scroll = 0.0f;
    if (lb->scroll_y > max_scroll)
        lb->scroll_y = max_scroll;
}

static void listbox_ensure_virtual_cache(vg_listbox_t *lb, size_t needed) {
    if (needed <= lb->cache_capacity)
        return;

    vg_listbox_cache_entry_t *new_cache =
        realloc(lb->visible_cache, needed * sizeof(vg_listbox_cache_entry_t));
    if (!new_cache)
        return;

    memset(new_cache + lb->cache_capacity,
           0,
           (needed - lb->cache_capacity) * sizeof(vg_listbox_cache_entry_t));
    lb->visible_cache = new_cache;
    lb->cache_capacity = needed;
}

static void listbox_sync_virtual_cache(vg_listbox_t *lb, float viewport_height) {
    if (!lb || !lb->virtual_mode)
        return;

    listbox_clamp_scroll(lb, viewport_height);

    size_t total = lb->total_item_count;
    if (total == 0 || lb->item_height <= 0.0f) {
        listbox_free_virtual_cache(lb);
        lb->visible_start = 0;
        lb->visible_count = 0;
        return;
    }

    size_t start = (size_t)(lb->scroll_y / lb->item_height);
    size_t visible = (size_t)(viewport_height / lb->item_height) + 2;
    if (visible > total - start)
        visible = total - start;

    listbox_ensure_virtual_cache(lb, visible);
    if (!lb->visible_cache)
        return;

    if (lb->visible_start == start && lb->visible_count == visible) {
        for (size_t i = 0; i < visible; i++) {
            size_t index = start + i;
            lb->visible_cache[i].selected =
                index < lb->selection_bitmap_size && lb->selection_bitmap[index];
        }
        return;
    }

    listbox_free_virtual_cache(lb);
    lb->visible_start = start;
    lb->visible_count = visible;

    for (size_t i = 0; i < visible; i++) {
        const char *text = "";
        size_t index = start + i;
        if (lb->data_provider) {
            lb->data_provider(&lb->base, index, &text, NULL, lb->data_provider_user_data);
        }
        lb->visible_cache[i].text = strdup(text ? text : "");
        lb->visible_cache[i].selected =
            index < lb->selection_bitmap_size && lb->selection_bitmap[index];
    }
}

static bool listbox_item_at_y(vg_listbox_t *lb,
                              vg_widget_t *widget,
                              float screen_y,
                              size_t *index) {
    if (!lb || !widget || !index || lb->item_height <= 0.0f)
        return false;

    float ry = screen_y - widget->y + lb->scroll_y;
    if (ry < 0.0f)
        return false;

    size_t idx = (size_t)(ry / lb->item_height);
    size_t total = listbox_virtual_item_count(lb);
    if (idx >= total)
        return false;

    *index = idx;
    return true;
}

static void listbox_select_virtual_index(vg_listbox_t *lb, size_t index) {
    if (!lb || !lb->virtual_mode || index >= lb->total_item_count)
        return;

    if (!lb->multi_select && lb->selected_index < lb->selection_bitmap_size) {
        lb->selection_bitmap[lb->selected_index] = false;
    }

    lb->selected_index = index;
    if (index < lb->selection_bitmap_size)
        lb->selection_bitmap[index] = true;
}

static void listbox_destroy(vg_widget_t *widget) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;
    vg_listbox_clear(lb);
    listbox_free_virtual_cache(lb);
    free(lb->visible_cache);
    lb->visible_cache = NULL;
    lb->cache_capacity = 0;
    free(lb->selection_bitmap);
    lb->selection_bitmap = NULL;
    lb->selection_bitmap_size = 0;
}

static void listbox_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;
    (void)avail_w;
    (void)avail_h;
    int count = lb->virtual_mode ? (int)lb->total_item_count : lb->item_count;
    int visible = count > 5 ? count : 5;
    widget->measured_width = 200.0f;
    widget->measured_height = (float)(visible * (int)lb->item_height);
}

static void listbox_arrange(vg_widget_t *widget, float x, float y, float w, float h) {
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

static void listbox_paint(vg_widget_t *widget, void *canvas) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;
    vgfx_window_t win = (vgfx_window_t)canvas;
    int32_t x = (int32_t)widget->x, y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width, h = (int32_t)widget->height;

    /* Background */
    vgfx_fill_rect(win, x, y, w, h, lb->bg_color);

    /* Draw items */
    float item_y = widget->y - lb->scroll_y;
    float ih = lb->item_height;

    if (lb->virtual_mode) {
        listbox_sync_virtual_cache(lb, widget->height);
        item_y = widget->y + (float)lb->visible_start * ih - lb->scroll_y;
        for (size_t i = 0; i < lb->visible_count; i++) {
            size_t index = lb->visible_start + i;
            uint32_t bg = lb->item_bg;
            if (index == lb->selected_index)
                bg = lb->selected_bg;
            else if (index == lb->hovered_index)
                bg = lb->hover_bg;

            int32_t iy = (int32_t)item_y;
            int32_t ih32 = (int32_t)ih;
            vgfx_fill_rect(win, x + 1, iy, w - 2, ih32, bg);

            if (lb->visible_cache[i].text && lb->font) {
                float ty = item_y + ih * 0.7f;
                vg_font_draw_text(canvas,
                                  lb->font,
                                  lb->font_size,
                                  widget->x + 4.0f,
                                  ty,
                                  lb->visible_cache[i].text,
                                  lb->text_color);
            }

            item_y += ih;
        }
    } else {
        for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
            float item_bottom = item_y + ih;
            if (item_bottom < widget->y) {
                item_y += ih;
                continue;
            }
            if (item_y > widget->y + widget->height)
                break;

            uint32_t bg;
            if (item == lb->selected)
                bg = lb->selected_bg;
            else if (item == lb->hovered)
                bg = lb->hover_bg;
            else
                bg = lb->item_bg;

            int32_t iy = (int32_t)item_y;
            int32_t ih32 = (int32_t)ih;
            vgfx_fill_rect(win, x + 1, iy, w - 2, ih32, bg);

            if (item->text && lb->font) {
                float ty = item_y + ih * 0.7f;
                vg_font_draw_text(canvas,
                                  lb->font,
                                  lb->font_size,
                                  widget->x + 4.0f,
                                  ty,
                                  item->text,
                                  lb->text_color);
            }

            item_y += ih;
        }
    }

    /* Border: use focus color when the listbox has keyboard focus */
    uint32_t border = (widget->state & VG_STATE_FOCUSED)
                          ? vg_theme_get_current()->colors.border_focus
                          : lb->border_color;
    vgfx_rect(win, x, y, w, h, border);
}

static bool listbox_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN: {
            size_t idx = 0;
            if (lb->virtual_mode) {
                if (listbox_item_at_y(lb, widget, event->mouse.screen_y, &idx)) {
                    listbox_select_virtual_index(lb, idx);
                    if (lb->on_select)
                        lb->on_select(widget, NULL, lb->on_select_data);
                    widget->needs_paint = true;
                    event->handled = true;
                    return true;
                }
            } else {
                /* Find which item was clicked */
                float ry = event->mouse.screen_y - widget->y + lb->scroll_y;
                int idx32 = (lb->item_height > 0) ? (int)(ry / lb->item_height) : -1;
                if (idx32 >= 0 && idx32 < lb->item_count) {
                    vg_listbox_item_t *item = lb->first_item;
                    for (int i = 0; i < idx32 && item; i++)
                        item = item->next;
                    if (item) {
                        vg_listbox_select(lb, item);
                        event->handled = true;
                        return true;
                    }
                }
            }
            break;
        }

        case VG_EVENT_MOUSE_MOVE: {
            if (lb->virtual_mode) {
                size_t idx = 0;
                lb->hovered_index = SIZE_MAX;
                if (listbox_item_at_y(lb, widget, event->mouse.screen_y, &idx))
                    lb->hovered_index = idx;
            } else {
                float ry = event->mouse.screen_y - widget->y + lb->scroll_y;
                int idx = (lb->item_height > 0) ? (int)(ry / lb->item_height) : -1;
                vg_listbox_item_t *hovered = NULL;
                if (idx >= 0 && idx < lb->item_count) {
                    hovered = lb->first_item;
                    for (int i = 0; i < idx && hovered; i++)
                        hovered = hovered->next;
                }
                lb->hovered = hovered;
            }
            break;
        }

        case VG_EVENT_MOUSE_LEAVE: {
            lb->hovered = NULL;
            lb->hovered_index = SIZE_MAX;
            break;
        }

        case VG_EVENT_MOUSE_WHEEL: {
            float scroll_delta = -event->wheel.delta_y * lb->item_height;
            lb->scroll_y += scroll_delta;
            listbox_clamp_scroll(lb, widget->height);
            event->handled = true;
            return true;
        }

        case VG_EVENT_DOUBLE_CLICK: {
            if (lb->virtual_mode) {
                if (lb->selected_index != SIZE_MAX && lb->on_activate) {
                    lb->on_activate(widget, NULL, lb->on_activate_data);
                    event->handled = true;
                    return true;
                }
            } else if (lb->selected && lb->on_activate) {
                lb->on_activate(widget, lb->selected, lb->on_activate_data);
                event->handled = true;
                return true;
            }
            break;
        }

        case VG_EVENT_KEY_DOWN: {
            int total = lb->virtual_mode ? (int)lb->total_item_count : lb->item_count;
            if (total <= 0)
                return false;
            int page_items = (lb->item_height > 0.0f) ? (int)(widget->height / lb->item_height) : 8;
            if (page_items < 1)
                page_items = 1;

            int current_idx = (int)vg_listbox_get_selected_index(lb);
            int new_idx = current_idx;

            switch (event->key.key) {
                case VG_KEY_UP:
                    new_idx = (current_idx > 0) ? current_idx - 1 : 0;
                    break;
                case VG_KEY_DOWN:
                    new_idx = (current_idx < total - 1) ? current_idx + 1 : total - 1;
                    break;
                case VG_KEY_HOME:
                    new_idx = 0;
                    break;
                case VG_KEY_END:
                    new_idx = total - 1;
                    break;
                case VG_KEY_PAGE_UP:
                    new_idx = current_idx - page_items;
                    if (new_idx < 0)
                        new_idx = 0;
                    break;
                case VG_KEY_PAGE_DOWN:
                    new_idx = current_idx + page_items;
                    if (new_idx >= total)
                        new_idx = total - 1;
                    break;
                case VG_KEY_ENTER:
                    if (lb->virtual_mode) {
                        if (lb->selected_index != SIZE_MAX && lb->on_activate)
                            lb->on_activate(widget, NULL, lb->on_activate_data);
                    } else if (lb->selected && lb->on_activate) {
                        lb->on_activate(widget, lb->selected, lb->on_activate_data);
                    }
                    event->handled = true;
                    return true;
                default:
                    break;
            }

            if (new_idx != current_idx && new_idx >= 0 && new_idx < total) {
                if (lb->virtual_mode) {
                    listbox_select_virtual_index(lb, (size_t)new_idx);
                    if (lb->on_select)
                        lb->on_select(widget, NULL, lb->on_select_data);
                } else {
                    vg_listbox_select_index(lb, (size_t)new_idx);
                }
                /* Scroll the selected item into view */
                float item_top = new_idx * lb->item_height;
                float item_bot = item_top + lb->item_height;
                if (item_top < lb->scroll_y)
                    lb->scroll_y = item_top;
                else if (item_bot > lb->scroll_y + widget->height)
                    lb->scroll_y = item_bot - widget->height;
                widget->needs_paint = true;
                event->handled = true;
                return true;
            }
            break;
        }

        default:
            break;
    }
    return false;
}

static bool listbox_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

vg_listbox_t *vg_listbox_create(vg_widget_t *parent) {
    vg_listbox_t *listbox = calloc(1, sizeof(vg_listbox_t));
    if (!listbox)
        return NULL;

    vg_widget_init(&listbox->base, VG_WIDGET_LISTBOX, &g_listbox_vtable);

    // Default appearance — scale pixel constants by ui_scale for HiDPI displays.
    vg_theme_t *_lb_theme = vg_theme_get_current();
    float _lb_s = _lb_theme && _lb_theme->ui_scale > 0.0f ? _lb_theme->ui_scale : 1.0f;
    listbox->item_height = (int)(24 * _lb_s);
    listbox->font_size = 14.0f * _lb_s;
    listbox->bg_color = 0xFF1E1E1E;
    listbox->item_bg = 0xFF1E1E1E;
    listbox->selected_bg = 0xFF094771;
    listbox->hover_bg = 0xFF2A2D2E;
    listbox->text_color = 0xFFCCCCCC;
    listbox->border_color = 0xFF3C3C3C;
    listbox->selected_index = SIZE_MAX;
    listbox->hovered_index = SIZE_MAX;

    if (parent) {
        vg_widget_add_child(parent, &listbox->base);
    }

    return listbox;
}

vg_listbox_item_t *vg_listbox_add_item(vg_listbox_t *listbox, const char *text, void *user_data) {
    if (!listbox || !text)
        return NULL;

    vg_listbox_item_t *item = calloc(1, sizeof(vg_listbox_item_t));
    if (!item)
        return NULL;

    item->text = strdup(text);
    item->user_data = user_data;

    // Add to end of list
    if (listbox->last_item) {
        listbox->last_item->next = item;
        item->prev = listbox->last_item;
        listbox->last_item = item;
    } else {
        listbox->first_item = listbox->last_item = item;
    }
    listbox->item_count++;

    return item;
}

/// @brief Listbox remove item.
void vg_listbox_remove_item(vg_listbox_t *listbox, vg_listbox_item_t *item) {
    if (!listbox || !item)
        return;

    if (item->prev)
        item->prev->next = item->next;
    else
        listbox->first_item = item->next;

    if (item->next)
        item->next->prev = item->prev;
    else
        listbox->last_item = item->prev;

    if (listbox->selected == item)
        listbox->selected = NULL;
    if (listbox->hovered == item)
        listbox->hovered = NULL;

    free(item->text);
    free(item);
    listbox->item_count--;
}

/// @brief Listbox clear.
void vg_listbox_clear(vg_listbox_t *listbox) {
    if (!listbox)
        return;

    vg_listbox_item_t *item = listbox->first_item;
    while (item) {
        vg_listbox_item_t *next = item->next;
        free(item->text);
        free(item);
        item = next;
    }

    listbox->first_item = listbox->last_item = NULL;
    listbox->selected = listbox->hovered = NULL;
    listbox->item_count = 0;
}

/// @brief Listbox select.
void vg_listbox_select(vg_listbox_t *listbox, vg_listbox_item_t *item) {
    if (!listbox)
        return;

    // Clear previous selection if not multi-select
    if (!listbox->multi_select && listbox->selected) {
        listbox->selected->selected = false;
    }

    listbox->selected = item;
    if (item) {
        item->selected = true;
        if (listbox->on_select) {
            listbox->on_select(&listbox->base, item, listbox->on_select_data);
        }
    }
}

vg_listbox_item_t *vg_listbox_get_selected(vg_listbox_t *listbox) {
    return listbox ? listbox->selected : NULL;
}

/// @brief Listbox set font.
void vg_listbox_set_font(vg_listbox_t *listbox, vg_font_t *font, float size) {
    if (!listbox)
        return;
    listbox->font = font;
    listbox->font_size = size;
}

/// @brief Listbox set on select.
void vg_listbox_set_on_select(vg_listbox_t *listbox,
                              vg_listbox_callback_t callback,
                              void *user_data) {
    if (!listbox)
        return;
    listbox->on_select = callback;
    listbox->on_select_data = user_data;
}

//=============================================================================
// Virtual Scrolling
//=============================================================================

void vg_listbox_set_virtual_mode(vg_listbox_t *listbox,
                                 bool enabled,
                                 size_t total_count,
                                 float item_height) {
    if (!listbox)
        return;

    listbox->virtual_mode = enabled;
    listbox->total_item_count = total_count;
    listbox->hovered_index = SIZE_MAX;
    if (item_height > 0) {
        listbox->item_height = item_height;
    }

    // Initialize selection bitmap
    if (enabled && total_count > 0) {
        free(listbox->selection_bitmap);
        listbox->selection_bitmap = calloc(total_count, sizeof(bool));
        listbox->selection_bitmap_size = total_count;
        listbox->selected_index = SIZE_MAX; // None selected

        // Allocate visible-item cache (capped at 64 — enough for one viewport page)
        size_t cap = total_count < 64 ? total_count : 64;
        free(listbox->visible_cache);
        listbox->visible_cache = calloc(cap, sizeof(vg_listbox_cache_entry_t));
        listbox->cache_capacity = listbox->visible_cache ? cap : 0;
    } else {
        listbox_free_virtual_cache(listbox);
        free(listbox->visible_cache);
        listbox->visible_cache = NULL;
        listbox->cache_capacity = 0;
        free(listbox->selection_bitmap);
        listbox->selection_bitmap = NULL;
        listbox->selection_bitmap_size = 0;
        listbox->selected_index = SIZE_MAX;
    }

    // Reset scroll and invalidate
    listbox->scroll_y = 0;
    listbox->visible_start = 0;
    listbox->visible_count = 0;
    listbox->base.needs_paint = true;
    listbox->base.needs_layout = true;
}

/// @brief Listbox set data provider.
void vg_listbox_set_data_provider(vg_listbox_t *listbox,
                                  vg_listbox_data_provider_t provider,
                                  void *user_data) {
    if (!listbox)
        return;

    listbox->data_provider = provider;
    listbox->data_provider_user_data = user_data;
}

/// @brief Listbox set total count.
void vg_listbox_set_total_count(vg_listbox_t *listbox, size_t count) {
    if (!listbox || !listbox->virtual_mode)
        return;

    listbox->total_item_count = count;

    // Resize selection bitmap if needed
    if (count > listbox->selection_bitmap_size) {
        bool *new_bitmap = realloc(listbox->selection_bitmap, count * sizeof(bool));
        if (new_bitmap) {
            // Zero out new entries
            memset(new_bitmap + listbox->selection_bitmap_size,
                   0,
                   (count - listbox->selection_bitmap_size) * sizeof(bool));
            listbox->selection_bitmap = new_bitmap;
            listbox->selection_bitmap_size = count;
        }
    }

    // Reset selection if out of bounds
    if (listbox->selected_index >= count) {
        listbox->selected_index = SIZE_MAX;
    }

    // Clamp scroll position
    float max_scroll = count * listbox->item_height - listbox->base.height;
    if (max_scroll < 0)
        max_scroll = 0;
    if (listbox->scroll_y > max_scroll) {
        listbox->scroll_y = max_scroll;
    }

    listbox->base.needs_paint = true;
}

/// @brief Listbox invalidate items.
void vg_listbox_invalidate_items(vg_listbox_t *listbox) {
    if (!listbox)
        return;

    // Clear cache
    if (listbox->visible_cache) {
        for (size_t i = 0; i < listbox->cache_capacity; i++) {
            free(listbox->visible_cache[i].text);
            listbox->visible_cache[i].text = NULL;
        }
    }

    // Force re-fetch on next paint
    listbox->visible_start = SIZE_MAX;
    listbox->visible_count = 0;
    listbox->base.needs_paint = true;
}

/// @brief Listbox invalidate item.
void vg_listbox_invalidate_item(vg_listbox_t *listbox, size_t index) {
    if (!listbox || !listbox->virtual_mode)
        return;

    // Check if item is in visible cache
    if (index >= listbox->visible_start &&
        index < listbox->visible_start + listbox->visible_count) {
        size_t cache_index = index - listbox->visible_start;
        if (cache_index < listbox->cache_capacity && listbox->visible_cache) {
            free(listbox->visible_cache[cache_index].text);
            listbox->visible_cache[cache_index].text = NULL;
        }
    }

    listbox->base.needs_paint = true;
}

/// @brief Listbox select index.
void vg_listbox_select_index(vg_listbox_t *listbox, size_t index) {
    if (!listbox)
        return;

    if (!listbox->virtual_mode) {
        // Non-virtual mode: find item at index
        vg_listbox_item_t *item = listbox->first_item;
        for (size_t i = 0; i < index && item; i++) {
            item = item->next;
        }
        vg_listbox_select(listbox, item);
        return;
    }

    // Virtual mode
    if (index >= listbox->total_item_count)
        return;

    // Clear previous selection
    if (!listbox->multi_select && listbox->selected_index < listbox->selection_bitmap_size) {
        listbox->selection_bitmap[listbox->selected_index] = false;
    }

    // Set new selection
    listbox_select_virtual_index(listbox, index);

    listbox->base.needs_paint = true;
}

/// @brief Listbox get selected index.
size_t vg_listbox_get_selected_index(vg_listbox_t *listbox) {
    if (!listbox)
        return SIZE_MAX;

    if (!listbox->virtual_mode) {
        // Non-virtual mode: find index of selected item
        if (!listbox->selected)
            return SIZE_MAX;
        size_t index = 0;
        for (vg_listbox_item_t *item = listbox->first_item; item; item = item->next) {
            if (item == listbox->selected)
                return index;
            index++;
        }
        return SIZE_MAX;
    }

    return listbox->selected_index;
}
