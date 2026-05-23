//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_listbox.c
// Purpose: ListBox widget implementation — scrollable list of selectable items
//          supporting normal and virtual-scroll modes, multi-select, item
//          retirement pool, and a data-provider callback for large datasets.
// Key invariants:
//   - Items carry a magic value (VG_LISTBOX_ITEM_MAGIC) and an owner pointer;
//     vg_listbox_item_is_live validates both before any item operation.
//   - Virtual mode allocates a selection bitmap and a visible-item cache;
//     non-virtual mode uses a doubly-linked item list.
//   - Retired items are pooled (up to VG_LISTBOX_RETIRE_POOL_SIZE) to reduce
//     malloc/free churn during rapid updates.
// Ownership/Lifetime:
//   - Items are owned by the listbox; freed on remove, clear, or destroy.
// Links: lib/gui/include/vg_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define VG_LISTBOX_ITEM_MAGIC UINT64_C(0x56474C424954454D)
#define VG_LISTBOX_ITEM_RETIRED_MAGIC UINT64_C(0x56474C4244524F50)

//=============================================================================
// Forward declarations
//=============================================================================

static void listbox_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void listbox_arrange(vg_widget_t *widget, float x, float y, float w, float h);
static void listbox_paint(vg_widget_t *widget, void *canvas);
static bool listbox_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool listbox_can_focus(vg_widget_t *widget);
static void listbox_destroy(vg_widget_t *widget);
static float listbox_default_item_height(vg_listbox_t *lb);
static float listbox_text_baseline(vg_listbox_t *lb, float item_y, float item_h);
static vg_listbox_item_t *listbox_item_at_index_nonvirtual(vg_listbox_t *lb, size_t index);
static size_t listbox_index_of_item(vg_listbox_t *lb, vg_listbox_item_t *target);
static bool listbox_clear_nonvirtual_selection(vg_listbox_t *lb);
static bool listbox_clear_virtual_selection(vg_listbox_t *lb);
static void listbox_select_nonvirtual_with_modifiers(vg_listbox_t *lb,
                                                     vg_listbox_item_t *item,
                                                     bool toggle,
                                                     bool range);
static void listbox_select_virtual_with_modifiers(vg_listbox_t *lb,
                                                  size_t index,
                                                  bool toggle,
                                                  bool range);
static void listbox_free_item_payload(vg_listbox_item_t *item);
static void listbox_retire_item(vg_listbox_t *lb, vg_listbox_item_t *item);
static void listbox_free_retired_items(vg_listbox_t *lb);
static vg_listbox_item_t *listbox_first_selected_nonvirtual(vg_listbox_t *lb);
static size_t listbox_first_selected_virtual(vg_listbox_t *lb);
static void listbox_refresh_virtual_cache_entry(vg_listbox_t *lb, size_t cache_index);

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

/// @brief Frees the text strings inside each cached visible row slot without releasing the cache array itself.
static void listbox_free_virtual_cache(vg_listbox_t *lb) {
    if (!lb->visible_cache)
        return;

    for (size_t i = 0; i < lb->cache_capacity; i++) {
        free(lb->visible_cache[i].text);
        lb->visible_cache[i].text = NULL;
        lb->visible_cache[i].selected = false;
    }
}

/// @brief Walks the non-virtual item list and returns the item at zero-based @p index, or NULL.
static vg_listbox_item_t *listbox_item_at_index_nonvirtual(vg_listbox_t *lb, size_t index) {
    if (!lb)
        return NULL;
    vg_listbox_item_t *item = lb->first_item;
    for (size_t i = 0; i < index && item; i++)
        item = item->next;
    return item;
}

/// @brief Returns the zero-based index of @p target in the non-virtual list, or SIZE_MAX if not found.
static size_t listbox_index_of_item(vg_listbox_t *lb, vg_listbox_item_t *target) {
    if (!lb || !vg_listbox_item_is_live(target) || target->owner != lb)
        return SIZE_MAX;
    size_t index = 0;
    for (vg_listbox_item_t *item = lb->first_item; item; item = item->next, index++) {
        if (item == target)
            return index;
    }
    return SIZE_MAX;
}

/// @brief Returns true if @p item is non-NULL, has the correct magic value, and still belongs to a listbox.
bool vg_listbox_item_is_live(const vg_listbox_item_t *item) {
    return item && item->magic == VG_LISTBOX_ITEM_MAGIC && item->owner != NULL;
}

/// @brief Override the text color for one live item.
void vg_listbox_item_set_text_color(vg_listbox_item_t *item, uint32_t color) {
    if (!vg_listbox_item_is_live(item))
        return;
    item->text_color = color;
    item->has_text_color = true;
    item->owner->base.needs_paint = true;
}

/// @brief Frees the text string inside @p item and zeroes its length, leaving the item struct itself intact.
static void listbox_free_item_payload(vg_listbox_item_t *item) {
    if (!item)
        return;
    free(item->text);
    item->text = NULL;
    item->text_len = 0;
    if (item->owns_user_data)
        free(item->user_data);
    item->user_data = NULL;
    item->owns_user_data = false;
    item->has_text_color = false;
    item->text_color = 0;
}

/// @brief Frees @p item's payload and then frees the item struct itself.
static void listbox_free_item(vg_listbox_item_t *item) {
    if (!item)
        return;
    listbox_free_item_payload(item);
    free(item);
}

/// @brief Detaches @p item from the live list and moves it onto @p lb->retired_items for deferred freeing.
static void listbox_retire_item(vg_listbox_t *lb, vg_listbox_item_t *item) {
    if (!lb || !item)
        return;
    listbox_free_item_payload(item);
    item->magic = VG_LISTBOX_ITEM_RETIRED_MAGIC;
    item->owner = NULL;
    item->selected = false;
    item->next = NULL;
    item->prev = NULL;
    item->retired_next = lb->retired_items;
    lb->retired_items = item;
}

/// @brief Walks @p lb->retired_items and frees every item in the deferred-free list.
static void listbox_free_retired_items(vg_listbox_t *lb) {
    if (!lb)
        return;
    vg_listbox_item_t *item = lb->retired_items;
    while (item) {
        vg_listbox_item_t *next = item->retired_next;
        listbox_free_item(item);
        item = next;
    }
    lb->retired_items = NULL;
}

/// @brief Returns the total item count: total_item_count in virtual mode, item_count otherwise.
static size_t listbox_virtual_item_count(const vg_listbox_t *lb) {
    return lb->virtual_mode ? lb->total_item_count : (size_t)lb->item_count;
}

/// @brief Clamps lb->scroll_y to [0, max_scroll] based on total item count and viewport height.
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

/// @brief Grows the visible-row cache array to at least @p needed entries, zeroing new slots.
static void listbox_ensure_virtual_cache(vg_listbox_t *lb, size_t needed) {
    if (needed <= lb->cache_capacity)
        return;
    if (needed > SIZE_MAX / sizeof(vg_listbox_cache_entry_t))
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

/// @brief Re-fetches text and selection state for the visible-cache slot at @p cache_index via the data provider.
static void listbox_refresh_virtual_cache_entry(vg_listbox_t *lb, size_t cache_index) {
    if (!lb || !lb->visible_cache || cache_index >= lb->visible_count ||
        cache_index >= lb->cache_capacity)
        return;

    size_t index = lb->visible_start + cache_index;
    const char *text = "";
    if (lb->data_provider)
        lb->data_provider(&lb->base, index, &text, NULL, lb->data_provider_user_data);

    char *copy = strdup(text ? text : "");
    if (!copy)
        return;

    free(lb->visible_cache[cache_index].text);
    lb->visible_cache[cache_index].text = copy;
}

/// @brief Updates the virtual visible-row cache to match the current scroll position, re-fetching changed rows.
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
    if (visible > 4096)
        visible = 4096;

    listbox_ensure_virtual_cache(lb, visible);
    if (!lb->visible_cache)
        return;

    if (lb->visible_start == start && lb->visible_count == visible) {
        for (size_t i = 0; i < visible; i++) {
            size_t index = start + i;
            if (!lb->visible_cache[i].text)
                listbox_refresh_virtual_cache_entry(lb, i);
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

/// @brief Converts @p local_y (relative to the viewport top) to an item index accounting for scroll; false if out of range.
static bool listbox_item_at_y(vg_listbox_t *lb,
                              vg_widget_t *widget,
                              float local_y,
                              size_t *index) {
    if (!lb || !widget || !index || lb->item_height <= 0.0f)
        return false;

    float ry = local_y + lb->scroll_y;
    if (ry < 0.0f)
        return false;

    size_t idx = (size_t)(ry / lb->item_height);
    size_t total = listbox_virtual_item_count(lb);
    if (idx >= total)
        return false;

    *index = idx;
    return true;
}

/// @brief Clears the selected flag on every item in the non-virtual linked list.
static bool listbox_clear_nonvirtual_selection(vg_listbox_t *lb) {
    if (!lb)
        return false;
    bool changed = false;
    for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
        if (item->selected)
            changed = true;
        item->selected = false;
    }
    return changed;
}

static void listbox_note_selection_changed(vg_listbox_t *lb) {
    if (lb)
        lb->selection_revision++;
}

static bool listbox_has_nonvirtual_selection(vg_listbox_t *lb) {
    if (!lb)
        return false;
    if (lb->selected)
        return true;
    for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
        if (item->selected)
            return true;
    }
    return false;
}

static bool listbox_has_nonvirtual_selection_except(vg_listbox_t *lb,
                                                    vg_listbox_item_t *except) {
    if (!lb)
        return false;
    for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
        if (item != except && item->selected)
            return true;
    }
    return false;
}

/// @brief Zeroes the entire selection bitmap in virtual mode, deselecting all rows.
static bool listbox_clear_virtual_selection(vg_listbox_t *lb) {
    if (!lb || !lb->selection_bitmap)
        return false;
    bool changed = false;
    for (size_t i = 0; i < lb->selection_bitmap_size; i++) {
        if (lb->selection_bitmap[i]) {
            changed = true;
            break;
        }
    }
    memset(lb->selection_bitmap, 0, lb->selection_bitmap_size * sizeof(bool));
    return changed;
}

/// @brief Returns the first selected item in the non-virtual list, or NULL if none is selected.
static vg_listbox_item_t *listbox_first_selected_nonvirtual(vg_listbox_t *lb) {
    if (!lb)
        return NULL;
    for (vg_listbox_item_t *candidate = lb->first_item; candidate; candidate = candidate->next) {
        if (candidate->selected)
            return candidate;
    }
    return NULL;
}

/// @brief Returns the index of the first selected row in the bitmap, or SIZE_MAX if nothing is selected.
static size_t listbox_first_selected_virtual(vg_listbox_t *lb) {
    if (!lb || !lb->selection_bitmap)
        return SIZE_MAX;
    for (size_t i = 0; i < lb->selection_bitmap_size; i++) {
        if (lb->selection_bitmap[i])
            return i;
    }
    return SIZE_MAX;
}

/// @brief Applies Ctrl/Shift selection semantics for @p item in non-virtual mode: range, toggle, or replace.
static void listbox_select_nonvirtual_with_modifiers(vg_listbox_t *lb,
                                                     vg_listbox_item_t *item,
                                                     bool toggle,
                                                     bool range) {
    if (!lb || !vg_listbox_item_is_live(item) || item->owner != lb)
        return;
    vg_listbox_item_t *old_selected = lb->selected;

    if (!lb->multi_select || (!toggle && !range)) {
        bool changed = listbox_clear_nonvirtual_selection(lb);
        changed = changed || !item->selected || lb->selected != item;
        item->selected = true;
        lb->selected = item;
        lb->anchor_selected = item;
        if (changed || lb->selected != old_selected)
            listbox_note_selection_changed(lb);
        return;
    }

    if (range) {
        bool changed = listbox_clear_nonvirtual_selection(lb);
        vg_listbox_item_t *anchor = lb->anchor_selected ? lb->anchor_selected : lb->selected;
        size_t anchor_index = listbox_index_of_item(lb, anchor ? anchor : item);
        size_t target_index = listbox_index_of_item(lb, item);
        if (anchor_index == SIZE_MAX || target_index == SIZE_MAX) {
            changed = changed || !item->selected;
            item->selected = true;
        } else {
            size_t start = anchor_index < target_index ? anchor_index : target_index;
            size_t end = anchor_index > target_index ? anchor_index : target_index;
            for (size_t index = start; index <= end; index++) {
                vg_listbox_item_t *selected_item = listbox_item_at_index_nonvirtual(lb, index);
                if (selected_item) {
                    if (!selected_item->selected)
                        changed = true;
                    selected_item->selected = true;
                }
            }
        }
        lb->selected = item;
        if (changed || lb->selected != old_selected)
            listbox_note_selection_changed(lb);
        return;
    }

    bool was_selected = item->selected;
    item->selected = !item->selected;
    if (item->selected) {
        lb->selected = item;
        lb->anchor_selected = item;
    } else {
        if (lb->selected == item)
            lb->selected = listbox_first_selected_nonvirtual(lb);
        if (lb->anchor_selected == item)
            lb->anchor_selected = lb->selected;
    }
    if (was_selected != item->selected || lb->selected != old_selected)
        listbox_note_selection_changed(lb);
}

/// @brief Applies Ctrl/Shift selection semantics for row @p index in virtual mode using the selection bitmap.
static void listbox_select_virtual_with_modifiers(vg_listbox_t *lb,
                                                  size_t index,
                                                  bool toggle,
                                                  bool range) {
    if (!lb || index >= lb->total_item_count)
        return;
    size_t old_selected = lb->selected_index;

    if (!lb->multi_select || (!toggle && !range)) {
        bool changed = listbox_clear_virtual_selection(lb);
        changed = changed || lb->selected_index != index ||
                  !lb->selection_bitmap || index >= lb->selection_bitmap_size ||
                  !lb->selection_bitmap[index];
        lb->selected_index = index;
        if (lb->selection_bitmap && index < lb->selection_bitmap_size)
            lb->selection_bitmap[index] = true;
        lb->anchor_selected_index = index;
        if (changed || lb->selected_index != old_selected)
            listbox_note_selection_changed(lb);
        return;
    }

    if (range) {
        bool changed = listbox_clear_virtual_selection(lb);
        size_t anchor =
            lb->anchor_selected_index < lb->total_item_count ? lb->anchor_selected_index : index;
        size_t start = anchor < index ? anchor : index;
        size_t end = anchor > index ? anchor : index;
        for (size_t i = start; lb->selection_bitmap && i <= end && i < lb->selection_bitmap_size;
             i++) {
            if (!lb->selection_bitmap[i])
                changed = true;
            lb->selection_bitmap[i] = true;
        }
        lb->selected_index = index;
        if (changed || lb->selected_index != old_selected)
            listbox_note_selection_changed(lb);
        return;
    }

    if (!lb->selection_bitmap || index >= lb->selection_bitmap_size)
        return;

    bool was_selected = lb->selection_bitmap[index];
    lb->selection_bitmap[index] = !lb->selection_bitmap[index];
    if (lb->selection_bitmap[index]) {
        lb->selected_index = index;
        lb->anchor_selected_index = index;
    } else {
        if (lb->selected_index == index)
            lb->selected_index = listbox_first_selected_virtual(lb);
        if (lb->anchor_selected_index == index)
            lb->anchor_selected_index = lb->selected_index;
    }
    if (was_selected != lb->selection_bitmap[index] || lb->selected_index != old_selected)
        listbox_note_selection_changed(lb);
}

/// @brief VTable destroy: clears all items, frees the virtual cache, selection bitmap, and retired-item list.
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
    lb->selection_bitmap_capacity = 0;
    listbox_free_retired_items(lb);
}

/// @brief Computes the default row height from the theme input height and font metrics, taking the larger value.
static float listbox_default_item_height(vg_listbox_t *lb) {
    vg_theme_t *theme = vg_theme_get_current();
    float scale = theme && theme->ui_scale > 0.0f ? theme->ui_scale : 1.0f;
    float height = theme ? theme->input.height : 28.0f * scale;

    if (lb && lb->font) {
        vg_font_metrics_t metrics = {0};
        vg_font_get_metrics(lb->font, lb->font_size, &metrics);
        float metrics_height = (float)metrics.line_height + 8.0f * scale;
        if (metrics_height > height)
            height = metrics_height;
    }

    return height;
}

/// @brief Returns the Y coordinate of the text baseline vertically centred within an item row.
static float listbox_text_baseline(vg_listbox_t *lb, float item_y, float item_h) {
    if (!lb || !lb->font)
        return item_y;

    vg_font_metrics_t metrics = {0};
    vg_font_get_metrics(lb->font, lb->font_size, &metrics);
    return item_y + (item_h + (float)metrics.ascent + (float)metrics.descent) / 2.0f;
}

/// @brief VTable measure: sizes to the widest item text (up to avail_w) and up to 5 visible rows tall.
static void listbox_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;
    (void)avail_h;
    size_t count = lb->virtual_mode ? lb->total_item_count
                                    : (lb->item_count > 0 ? (size_t)lb->item_count : 0u);
    size_t visible = count > 0 ? (count < 5u ? count : 5u) : 5u;
    float measured_width = 200.0f;
    if (lb->font && !lb->virtual_mode) {
        vg_text_metrics_t metrics = {0};
        for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
            if (!item->text)
                continue;
            vg_font_measure_text(lb->font, lb->font_size, item->text, &metrics);
            float candidate =
                metrics.width + vg_theme_get_current()->input.padding_h * 2.0f + 12.0f;
            if (candidate > measured_width)
                measured_width = candidate;
        }
    }
    if (avail_w > 0.0f && measured_width > avail_w)
        measured_width = avail_w;
    widget->measured_width = measured_width;
    widget->measured_height = (float)visible * lb->item_height;
    vg_widget_apply_constraints(widget);
}

/// @brief VTable arrange: sets the widget's final position and size from the layout pass.
static void listbox_arrange(vg_widget_t *widget, float x, float y, float w, float h) {
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
}

/// @brief VTable paint: draws background, selection highlights, row text, scrollbar thumb, and focus border.
static void listbox_paint(vg_widget_t *widget, void *canvas) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;
    int32_t x = (int32_t)widget->x, y = (int32_t)widget->y;
    int32_t w = (int32_t)widget->width, h = (int32_t)widget->height;
    float text_padding = theme->input.padding_h;

    /* Background */
    vgfx_fill_rect(win, x, y, w, h, lb->bg_color);
    if (w > 2)
        vgfx_fill_rect(win, x + 1, y + 1, w - 2, 1, vg_color_lighten(lb->bg_color, 0.05f));
    if (w <= 0 || h <= 0)
        return;
    vgfx_set_clip(win, x, y, w, h);

    /* Draw items */
    float item_y = widget->y - lb->scroll_y;
    float ih = lb->item_height;
    int32_t text_clip_x = x + (int32_t)text_padding;
    int32_t text_clip_y = y + 1;
    int32_t text_clip_w = w - (int32_t)text_padding - 6;
    int32_t text_clip_h = h - 2;

    if (lb->virtual_mode) {
        listbox_sync_virtual_cache(lb, widget->height);
        item_y = widget->y + (float)lb->visible_start * ih - lb->scroll_y;
        for (size_t i = 0; i < lb->visible_count; i++) {
            size_t index = lb->visible_start + i;
            float item_bottom = item_y + ih;
            if (item_bottom <= widget->y) {
                item_y += ih;
                continue;
            }
            if (item_y >= widget->y + widget->height)
                break;

            uint32_t bg =
                ((index & 1u) == 0u)
                    ? lb->item_bg
                    : vg_color_blend(lb->item_bg, theme->colors.bg_secondary, 0.35f);
            bool is_selected =
                index < lb->selection_bitmap_size && lb->selection_bitmap[index];
            if (is_selected)
                bg = lb->selected_bg;
            else if (index == lb->hovered_index)
                bg = lb->hover_bg;

            int32_t iy = (int32_t)item_y;
            int32_t ih32 = (int32_t)ih;
            vgfx_fill_rect(win, x + 1, iy, w - 2, ih32, bg);
            if (is_selected)
                vgfx_fill_rect(win, x + 1, iy, 3, ih32, theme->colors.accent_primary);
            vgfx_fill_rect(win, x + 1, iy + ih32 - 1, w - 2, 1, theme->colors.border_secondary);

            if (lb->visible_cache[i].text && lb->font) {
                if (text_clip_w > 0 && text_clip_h > 0)
                    vgfx_set_clip(win, text_clip_x, text_clip_y, text_clip_w, text_clip_h);
                vg_font_draw_text(canvas,
                                  lb->font,
                                  lb->font_size,
                                  widget->x + text_padding,
                                  listbox_text_baseline(lb, item_y, ih),
                                  lb->visible_cache[i].text,
                                  lb->text_color);
                if (text_clip_w > 0 && text_clip_h > 0)
                    vgfx_set_clip(win, x, y, w, h);
            }

            item_y += ih;
        }
    } else {
        int row_index = 0;
        for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
            float item_bottom = item_y + ih;
            if (item_bottom < widget->y) {
                item_y += ih;
                row_index++;
                continue;
            }
            if (item_y > widget->y + widget->height)
                break;

            uint32_t bg;
            if (item->selected)
                bg = lb->selected_bg;
            else if (item == lb->hovered)
                bg = lb->hover_bg;
            else
                bg = (row_index & 1) == 0 ? lb->item_bg
                                          : vg_color_blend(lb->item_bg, theme->colors.bg_secondary, 0.35f);

            int32_t iy = (int32_t)item_y;
            int32_t ih32 = (int32_t)ih;
            vgfx_fill_rect(win, x + 1, iy, w - 2, ih32, bg);
            if (item->selected)
                vgfx_fill_rect(win, x + 1, iy, 3, ih32, theme->colors.accent_primary);
            vgfx_fill_rect(win, x + 1, iy + ih32 - 1, w - 2, 1, theme->colors.border_secondary);

            if (item->text && lb->font) {
                if (text_clip_w > 0 && text_clip_h > 0)
                    vgfx_set_clip(win, text_clip_x, text_clip_y, text_clip_w, text_clip_h);
                vg_font_draw_text(canvas,
                                  lb->font,
                                  lb->font_size,
                                  widget->x + text_padding,
                                  listbox_text_baseline(lb, item_y, ih),
                                  item->text,
                                  item->has_text_color ? item->text_color : lb->text_color);
                if (text_clip_w > 0 && text_clip_h > 0)
                    vgfx_set_clip(win, x, y, w, h);
            }

            item_y += ih;
            row_index++;
        }
    }

    vgfx_clear_clip(win);

    /* Border: use focus color when the listbox has keyboard focus */
    uint32_t border = (widget->state & VG_STATE_FOCUSED)
                          ? vg_theme_get_current()->colors.border_focus
                          : lb->border_color;
    vgfx_rect(win, x, y, w, h, border);
}

/// @brief VTable handle_event: dispatches mouse (click/scroll/keys) to selection and scroll logic.
static bool listbox_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_listbox_t *lb = (vg_listbox_t *)widget;

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN: {
            uint32_t mods = event->modifiers;
            bool toggle =
                lb->multi_select && ((mods & VG_MOD_CTRL) != 0 || (mods & VG_MOD_SUPER) != 0);
            bool range = lb->multi_select && (mods & VG_MOD_SHIFT) != 0;
            size_t idx = 0;
            if (lb->virtual_mode) {
                if (listbox_item_at_y(lb, widget, event->mouse.y, &idx)) {
                    listbox_select_virtual_with_modifiers(lb, idx, toggle, range);
                    if (lb->on_select)
                        lb->on_select(widget, NULL, lb->on_select_data);
                    widget->needs_paint = true;
                    event->handled = true;
                    return true;
                }
            } else {
                /* Find which item was clicked */
                float ry = event->mouse.y + lb->scroll_y;
                int idx32 = (lb->item_height > 0) ? (int)(ry / lb->item_height) : -1;
                if (idx32 >= 0 && idx32 < lb->item_count) {
                    vg_listbox_item_t *item = listbox_item_at_index_nonvirtual(lb, (size_t)idx32);
                    if (item) {
                        listbox_select_nonvirtual_with_modifiers(lb, item, toggle, range);
                        if (lb->on_select)
                            lb->on_select(widget, item, lb->on_select_data);
                        widget->needs_paint = true;
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
                size_t old_hover = lb->hovered_index;
                lb->hovered_index = SIZE_MAX;
                if (listbox_item_at_y(lb, widget, event->mouse.y, &idx))
                    lb->hovered_index = idx;
                if (old_hover != lb->hovered_index)
                    widget->needs_paint = true;
            } else {
                float ry = event->mouse.y + lb->scroll_y;
                int idx = (lb->item_height > 0) ? (int)(ry / lb->item_height) : -1;
                vg_listbox_item_t *old_hover = lb->hovered;
                vg_listbox_item_t *hovered = NULL;
                if (idx >= 0 && idx < lb->item_count) {
                    hovered = lb->first_item;
                    for (int i = 0; i < idx && hovered; i++)
                        hovered = hovered->next;
                }
                lb->hovered = hovered;
                if (old_hover != lb->hovered)
                    widget->needs_paint = true;
            }
            break;
        }

        case VG_EVENT_MOUSE_LEAVE: {
            bool changed = lb->hovered != NULL || lb->hovered_index != SIZE_MAX;
            lb->hovered = NULL;
            lb->hovered_index = SIZE_MAX;
            if (changed)
                widget->needs_paint = true;
            break;
        }

        case VG_EVENT_MOUSE_WHEEL: {
            float scroll_delta = -event->wheel.delta_y * lb->item_height;
            lb->scroll_y += scroll_delta;
            listbox_clamp_scroll(lb, widget->height);
            widget->needs_paint = true;
            event->handled = true;
            return true;
        }

        case VG_EVENT_DOUBLE_CLICK: {
            if (lb->virtual_mode) {
                if (lb->selected_index != SIZE_MAX && lb->selected_index < lb->selection_bitmap_size &&
                    lb->selection_bitmap[lb->selected_index] && lb->on_activate) {
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
            size_t total = lb->virtual_mode ? lb->total_item_count
                                            : (lb->item_count > 0 ? (size_t)lb->item_count : 0u);
            if (total == 0)
                return false;
            uint32_t mods = event->modifiers;
            bool range = lb->multi_select && (mods & VG_MOD_SHIFT) != 0;
            size_t page_items =
                (lb->item_height > 0.0f) ? (size_t)(widget->height / lb->item_height) : 8u;
            if (page_items < 1)
                page_items = 1;

            size_t current_idx = vg_listbox_get_selected_index(lb);
            bool has_current = current_idx != SIZE_MAX && current_idx < total;
            size_t new_idx = has_current ? current_idx : 0u;
            bool navigated = true;

            switch (event->key.key) {
                case VG_KEY_UP:
                    new_idx = (has_current && current_idx > 0) ? current_idx - 1 : 0;
                    break;
                case VG_KEY_DOWN:
                    new_idx = has_current && current_idx < total - 1 ? current_idx + 1 : 0;
                    break;
                case VG_KEY_HOME:
                    new_idx = 0;
                    break;
                case VG_KEY_END:
                    new_idx = total - 1;
                    break;
                case VG_KEY_PAGE_UP:
                    new_idx = has_current && current_idx > page_items ? current_idx - page_items : 0;
                    break;
                case VG_KEY_PAGE_DOWN:
                    if (!has_current)
                        new_idx = page_items > 0 ? page_items - 1 : 0;
                    else if (current_idx > SIZE_MAX - page_items)
                        new_idx = total - 1;
                    else
                        new_idx = current_idx + page_items;
                    if (new_idx >= total)
                        new_idx = total - 1;
                    break;
                case VG_KEY_ENTER:
                    if (lb->virtual_mode) {
                        if (lb->selected_index != SIZE_MAX &&
                            lb->selected_index < lb->selection_bitmap_size &&
                            lb->selection_bitmap[lb->selected_index] && lb->on_activate)
                            lb->on_activate(widget, NULL, lb->on_activate_data);
                    } else if (lb->selected && lb->on_activate) {
                        lb->on_activate(widget, lb->selected, lb->on_activate_data);
                    }
                    event->handled = true;
                    return true;
                default:
                    navigated = false;
                    break;
            }

            if (navigated && (!has_current || new_idx != current_idx) && new_idx < total) {
                if (lb->virtual_mode) {
                    listbox_select_virtual_with_modifiers(lb, new_idx, false, range);
                    if (lb->on_select)
                        lb->on_select(widget, NULL, lb->on_select_data);
                } else {
                    vg_listbox_item_t *item = listbox_item_at_index_nonvirtual(lb, new_idx);
                    if (item) {
                        listbox_select_nonvirtual_with_modifiers(lb, item, false, range);
                        if (lb->on_select)
                            lb->on_select(widget, item, lb->on_select_data);
                    }
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

/// @brief VTable can_focus: returns true when the listbox is both enabled and visible.
static bool listbox_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

/// @brief Create a list box widget.
///
/// @param parent Widget to attach to as a child (may be NULL).
/// @return Newly allocated vg_listbox_t, or NULL on allocation failure.
vg_listbox_t *vg_listbox_create(vg_widget_t *parent) {
    vg_listbox_t *listbox = calloc(1, sizeof(vg_listbox_t));
    if (!listbox)
        return NULL;

    vg_widget_init(&listbox->base, VG_WIDGET_LISTBOX, &g_listbox_vtable);

    // Default appearance — scale pixel constants by ui_scale for HiDPI displays.
    vg_theme_t *_lb_theme = vg_theme_get_current();
    listbox->font = _lb_theme->typography.font_regular;
    listbox->font_size = _lb_theme->typography.size_normal;
    listbox->bg_color = _lb_theme->colors.bg_primary;
    listbox->item_bg = _lb_theme->colors.bg_primary;
    listbox->selected_bg = _lb_theme->colors.bg_selected;
    listbox->hover_bg = _lb_theme->colors.bg_hover;
    listbox->text_color = _lb_theme->colors.fg_primary;
    listbox->border_color = _lb_theme->colors.border_primary;
    listbox->selected_index = SIZE_MAX;
    listbox->prev_selected_index = SIZE_MAX;
    listbox->anchor_selected_index = SIZE_MAX;
    listbox->hovered_index = SIZE_MAX;
    listbox->item_height = listbox_default_item_height(listbox);

    if (parent) {
        vg_widget_add_child(parent, &listbox->base);
    }

    return listbox;
}

/// @brief Append an item to the list box.
///
/// @param listbox   The list box to modify.
/// @param text      Display text for the item (copied internally).
/// @param user_data Opaque pointer stored in the item; not dereferenced here.
/// @return Pointer to the new item, or NULL on allocation failure or invalid args.
vg_listbox_item_t *vg_listbox_add_item(vg_listbox_t *listbox, const char *text, void *user_data) {
    if (!listbox || !text)
        return NULL;

    vg_listbox_item_t *item = calloc(1, sizeof(vg_listbox_item_t));
    if (!item)
        return NULL;

    item->text = strdup(text);
    if (!item->text) {
        free(item);
        return NULL;
    }
    item->text_len = strlen(item->text);
    item->magic = VG_LISTBOX_ITEM_MAGIC;
    item->owner = listbox;
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
    listbox->base.needs_layout = true;
    listbox->base.needs_paint = true;

    return item;
}

/// @brief Remove and retire a single item from the list box.
///
/// @param listbox The list box to modify.
/// @param item    Item to remove; must be live and owned by @p listbox.
void vg_listbox_remove_item(vg_listbox_t *listbox, vg_listbox_item_t *item) {
    if (!listbox || !vg_listbox_item_is_live(item) || item->owner != listbox)
        return;

    if (item->prev)
        item->prev->next = item->next;
    else
        listbox->first_item = item->next;

    if (item->next)
        item->next->prev = item->prev;
    else
        listbox->last_item = item->prev;

    if (listbox->selected == item) {
        listbox->selected = NULL;
        listbox_note_selection_changed(listbox);
    }
    if (listbox->hovered == item)
        listbox->hovered = NULL;
    if (listbox->anchor_selected == item)
        listbox->anchor_selected = listbox->selected;
    if (listbox->prev_selected == item)
        listbox->prev_selected = NULL;

    listbox_retire_item(listbox, item);
    listbox->item_count--;
    listbox->base.needs_layout = true;
    listbox->base.needs_paint = true;
}

/// @brief Remove and retire all items from the list box.
///
/// @param listbox The list box to clear.
void vg_listbox_clear(vg_listbox_t *listbox) {
    if (!listbox)
        return;

    bool had_selection = listbox_has_nonvirtual_selection(listbox);
    bool had_virtual_selection =
        listbox->virtual_mode &&
        (listbox->selected_index != SIZE_MAX || listbox_first_selected_virtual(listbox) != SIZE_MAX);
    vg_listbox_item_t *item = listbox->first_item;
    while (item) {
        vg_listbox_item_t *next = item->next;
        listbox_retire_item(listbox, item);
        item = next;
    }

    listbox->first_item = listbox->last_item = NULL;
    listbox->selected = listbox->hovered = NULL;
    listbox->prev_selected = NULL;
    listbox->anchor_selected = NULL;
    listbox->item_count = 0;
    if (listbox->virtual_mode) {
        listbox_free_virtual_cache(listbox);
        free(listbox->visible_cache);
        listbox->visible_cache = NULL;
        listbox->cache_capacity = 0;
        free(listbox->selection_bitmap);
        listbox->selection_bitmap = NULL;
        listbox->selection_bitmap_size = 0;
        listbox->selection_bitmap_capacity = 0;
        listbox->virtual_mode = false;
        listbox->total_item_count = 0;
        listbox->selected_index = SIZE_MAX;
        listbox->prev_selected_index = SIZE_MAX;
        listbox->anchor_selected_index = SIZE_MAX;
        listbox->hovered_index = SIZE_MAX;
        listbox->visible_start = 0;
        listbox->visible_count = 0;
        listbox->scroll_y = 0.0f;
    }
    if (had_selection || had_virtual_selection)
        listbox_note_selection_changed(listbox);
    listbox->base.needs_layout = true;
    listbox->base.needs_paint = true;
}

/// @brief Set the selected item; fires on_select if the item changed.
///
/// @param listbox The list box to update.
/// @param item    Item to select (must be live and owned by @p listbox), or NULL to deselect.
void vg_listbox_select(vg_listbox_t *listbox, vg_listbox_item_t *item) {
    if (!listbox)
        return;
    if (item && (!vg_listbox_item_is_live(item) || item->owner != listbox))
        return;
    vg_listbox_item_t *old_selected = listbox->selected;
    bool item_was_selected = item && item->selected;
    bool selection_changed = false;

    if (!listbox->multi_select || item == NULL) {
        bool had_other_selected =
            item ? listbox_has_nonvirtual_selection_except(listbox, item) : false;
        bool had_selected_flags = listbox_clear_nonvirtual_selection(listbox);
        selection_changed = (old_selected != item) ||
                            (item ? had_other_selected : had_selected_flags) ||
                            (item && !item_was_selected);
    }

    listbox->selected = item;
    if (item) {
        selection_changed = selection_changed || !item_was_selected;
        item->selected = true;
        listbox->anchor_selected = item;
        if (listbox->on_select) {
            listbox->on_select(&listbox->base, item, listbox->on_select_data);
        }
    } else {
        listbox->anchor_selected = NULL;
    }
    if (selection_changed || listbox->selected != old_selected)
        listbox_note_selection_changed(listbox);
    listbox->base.needs_paint = true;
}

/// @brief Return the currently selected item, or NULL if nothing is selected.
///
/// @param listbox The list box to query.
/// @return Pointer to the selected item, or NULL.
vg_listbox_item_t *vg_listbox_get_selected(vg_listbox_t *listbox) {
    return listbox ? listbox->selected : NULL;
}

/// @brief Override the list item font and size.
///
/// @param listbox The list box to configure.
/// @param font    Font to use; NULL keeps the existing font.
/// @param size    Font size in pixels; <= 0 falls back to the theme normal size.
void vg_listbox_set_font(vg_listbox_t *listbox, vg_font_t *font, float size) {
    if (!listbox)
        return;
    listbox->font = font;
    listbox->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    {
        float min_item_height = listbox_default_item_height(listbox);
        if (listbox->item_height < min_item_height)
            listbox->item_height = min_item_height;
    }
    listbox->base.needs_layout = true;
    listbox->base.needs_paint = true;
}

/// @brief Set the selection callback invoked when the selected item changes.
///
/// @param listbox   The list box to configure.
/// @param callback  Function called with (widget, item, user_data) on selection.
/// @param user_data Opaque pointer passed to @p callback.
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

/// @brief Enables or disables virtual-scroll mode, allocating a selection bitmap and visible-row cache for @p total_count items.
void vg_listbox_set_virtual_mode(vg_listbox_t *listbox,
                                 bool enabled,
                                 size_t total_count,
                                 float item_height) {
    if (!listbox)
        return;

    if (enabled && total_count > 0 && total_count > SIZE_MAX / sizeof(bool))
        return;

    size_t old_virtual_selected = listbox->selected_index;
    vg_listbox_item_t *old_selected = listbox->selected;
    listbox->virtual_mode = enabled;
    listbox->total_item_count = enabled ? total_count : 0;
    listbox->hovered_index = SIZE_MAX;
    listbox->prev_selected_index = SIZE_MAX;
    listbox->anchor_selected_index = SIZE_MAX;
    if (item_height > 0) {
        listbox->item_height = item_height;
    }

    // Initialize selection bitmap
    if (enabled && total_count > 0) {
        bool *new_selection = calloc(total_count, sizeof(bool));
        if (!new_selection) {
            listbox->virtual_mode = false;
            listbox->total_item_count = 0;
            listbox->selected_index = SIZE_MAX;
            listbox->prev_selected_index = SIZE_MAX;
            listbox->anchor_selected_index = SIZE_MAX;
            listbox->base.needs_paint = true;
            listbox->base.needs_layout = true;
            return;
        }

        free(listbox->selection_bitmap);
        listbox->selection_bitmap = new_selection;
        listbox->selection_bitmap_size = total_count;
        listbox->selection_bitmap_capacity = total_count;
        listbox->selected_index = SIZE_MAX; // None selected
        listbox->prev_selected_index = SIZE_MAX;
        listbox->anchor_selected_index = SIZE_MAX;

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
        listbox->selection_bitmap_capacity = 0;
        listbox->selected_index = SIZE_MAX;
        listbox->prev_selected_index = SIZE_MAX;
        listbox->anchor_selected_index = SIZE_MAX;
    }

    // Reset scroll and invalidate
    listbox->scroll_y = 0;
    listbox->visible_start = 0;
    listbox->visible_count = 0;
    if (old_selected || old_virtual_selected != SIZE_MAX)
        listbox_note_selection_changed(listbox);
    listbox->base.needs_paint = true;
    listbox->base.needs_layout = true;
}

/// @brief Set the virtual-mode data provider callback used to populate visible rows.
///
/// @param listbox   The list box to configure (must be in virtual mode).
/// @param provider  Callback that fills a vg_listbox_cache_entry_t for a given index.
/// @param user_data Opaque pointer passed to @p provider.
void vg_listbox_set_data_provider(vg_listbox_t *listbox,
                                  vg_listbox_data_provider_t provider,
                                  void *user_data) {
    if (!listbox)
        return;

    listbox->data_provider = provider;
    listbox->data_provider_user_data = user_data;
}

/// @brief Update the total item count in virtual-scroll mode.
///
/// @details Resizes the selection bitmap if @p count exceeds the current size
///          and clamps the scroll position to the new content height.
///
/// @param listbox The list box to update (must be in virtual mode).
/// @param count   New total number of items.
void vg_listbox_set_total_count(vg_listbox_t *listbox, size_t count) {
    if (!listbox || !listbox->virtual_mode)
        return;
    if (count > SIZE_MAX / sizeof(bool))
        return;

    bool truncated_selection = false;

    // Resize selection bitmap if needed. Capacity stays separate from logical
    // size so shrinking cannot leave stale selected rows addressable.
    if (count > listbox->selection_bitmap_capacity) {
        bool *new_bitmap = realloc(listbox->selection_bitmap, count * sizeof(bool));
        if (!new_bitmap)
            return;
        // Zero out new entries
        memset(new_bitmap + listbox->selection_bitmap_capacity,
               0,
               (count - listbox->selection_bitmap_capacity) * sizeof(bool));
        listbox->selection_bitmap = new_bitmap;
        listbox->selection_bitmap_capacity = count;
    } else if (count == 0) {
        free(listbox->selection_bitmap);
        listbox->selection_bitmap = NULL;
        listbox->selection_bitmap_capacity = 0;
    } else if (count < listbox->selection_bitmap_size) {
        for (size_t i = count; i < listbox->selection_bitmap_size; i++) {
            if (listbox->selection_bitmap[i]) {
                truncated_selection = true;
                break;
            }
        }
        memset(listbox->selection_bitmap + count,
               0,
               (listbox->selection_bitmap_size - count) * sizeof(bool));
    }

    listbox->selection_bitmap_size = count;
    listbox->total_item_count = count;

    // Reset selection if out of bounds
    if (listbox->selected_index >= count) {
        listbox->selected_index = SIZE_MAX;
        listbox->prev_selected_index = SIZE_MAX;
        listbox->anchor_selected_index = SIZE_MAX;
        listbox_note_selection_changed(listbox);
        truncated_selection = false;
    }
    if (listbox->anchor_selected_index >= count)
        listbox->anchor_selected_index = SIZE_MAX;
    if (truncated_selection)
        listbox_note_selection_changed(listbox);

    // Clamp scroll position
    float max_scroll = count * listbox->item_height - listbox->base.height;
    if (max_scroll < 0)
        max_scroll = 0;
    if (listbox->scroll_y > max_scroll) {
        listbox->scroll_y = max_scroll;
    }

    listbox->base.needs_paint = true;
}

/// @brief Invalidate the entire visible-item cache, forcing a re-fetch on next paint.
///
/// @param listbox The list box to invalidate.
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

/// @brief Invalidate a single item's cache entry by index; forces re-fetch on next paint.
///
/// @param listbox The list box to update (must be in virtual mode).
/// @param index   Zero-based index of the item to invalidate.
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

/// @brief Select the item at a zero-based index (works in both normal and virtual mode).
///
/// @param listbox The list box to update.
/// @param index   Zero-based item index to select.
void vg_listbox_select_index(vg_listbox_t *listbox, size_t index) {
    if (!listbox)
        return;

    if (!listbox->virtual_mode) {
        // Non-virtual mode: find item at index
        vg_listbox_item_t *item = listbox->first_item;
        for (size_t i = 0; i < index && item; i++) {
            item = item->next;
        }
        if (!item)
            return;
        vg_listbox_select(listbox, item);
        return;
    }

    // Virtual mode
    if (index >= listbox->total_item_count)
        return;

    listbox_select_virtual_with_modifiers(listbox, index, false, false);

    listbox->base.needs_paint = true;
}

/// @brief Return the zero-based index of the selected item.
///
/// @param listbox The list box to query.
/// @return Zero-based index of the selected item, or SIZE_MAX if nothing is selected.
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
