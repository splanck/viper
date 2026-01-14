// vg_listbox.c - ListBox widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

vg_listbox_t* vg_listbox_create(vg_widget_t* parent) {
    vg_listbox_t* listbox = calloc(1, sizeof(vg_listbox_t));
    if (!listbox) return NULL;

    listbox->base.type = VG_WIDGET_LISTBOX;
    listbox->base.visible = true;
    listbox->base.enabled = true;

    // Default appearance
    listbox->item_height = 24;
    listbox->font_size = 14;
    listbox->bg_color = 0xFF1E1E1E;
    listbox->item_bg = 0xFF1E1E1E;
    listbox->selected_bg = 0xFF094771;
    listbox->hover_bg = 0xFF2A2D2E;
    listbox->text_color = 0xFFCCCCCC;
    listbox->border_color = 0xFF3C3C3C;

    if (parent) {
        vg_widget_add_child(parent, &listbox->base);
    }

    return listbox;
}

vg_listbox_item_t* vg_listbox_add_item(vg_listbox_t* listbox, const char* text, void* user_data) {
    if (!listbox || !text) return NULL;

    vg_listbox_item_t* item = calloc(1, sizeof(vg_listbox_item_t));
    if (!item) return NULL;

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

void vg_listbox_remove_item(vg_listbox_t* listbox, vg_listbox_item_t* item) {
    if (!listbox || !item) return;

    if (item->prev) item->prev->next = item->next;
    else listbox->first_item = item->next;

    if (item->next) item->next->prev = item->prev;
    else listbox->last_item = item->prev;

    if (listbox->selected == item) listbox->selected = NULL;
    if (listbox->hovered == item) listbox->hovered = NULL;

    free(item->text);
    free(item);
    listbox->item_count--;
}

void vg_listbox_clear(vg_listbox_t* listbox) {
    if (!listbox) return;

    vg_listbox_item_t* item = listbox->first_item;
    while (item) {
        vg_listbox_item_t* next = item->next;
        free(item->text);
        free(item);
        item = next;
    }

    listbox->first_item = listbox->last_item = NULL;
    listbox->selected = listbox->hovered = NULL;
    listbox->item_count = 0;
}

void vg_listbox_select(vg_listbox_t* listbox, vg_listbox_item_t* item) {
    if (!listbox) return;

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

vg_listbox_item_t* vg_listbox_get_selected(vg_listbox_t* listbox) {
    return listbox ? listbox->selected : NULL;
}

void vg_listbox_set_font(vg_listbox_t* listbox, vg_font_t* font, float size) {
    if (!listbox) return;
    listbox->font = font;
    listbox->font_size = size;
}

void vg_listbox_set_on_select(vg_listbox_t* listbox, vg_listbox_callback_t callback, void* user_data) {
    if (!listbox) return;
    listbox->on_select = callback;
    listbox->on_select_data = user_data;
}

//=============================================================================
// Virtual Scrolling
//=============================================================================

void vg_listbox_set_virtual_mode(vg_listbox_t* listbox, bool enabled,
    size_t total_count, float item_height) {
    if (!listbox) return;

    listbox->virtual_mode = enabled;
    listbox->total_item_count = total_count;
    if (item_height > 0) {
        listbox->item_height = item_height;
    }

    // Initialize selection bitmap
    if (enabled && total_count > 0) {
        free(listbox->selection_bitmap);
        listbox->selection_bitmap = calloc(total_count, sizeof(bool));
        listbox->selection_bitmap_size = total_count;
        listbox->selected_index = SIZE_MAX;  // None selected
    }

    // Reset scroll and invalidate
    listbox->scroll_y = 0;
    listbox->visible_start = 0;
    listbox->visible_count = 0;
    listbox->base.needs_paint = true;
    listbox->base.needs_layout = true;
}

void vg_listbox_set_data_provider(vg_listbox_t* listbox,
    vg_listbox_data_provider_t provider, void* user_data) {
    if (!listbox) return;

    listbox->data_provider = provider;
    listbox->data_provider_user_data = user_data;
}

void vg_listbox_set_total_count(vg_listbox_t* listbox, size_t count) {
    if (!listbox || !listbox->virtual_mode) return;

    listbox->total_item_count = count;

    // Resize selection bitmap if needed
    if (count > listbox->selection_bitmap_size) {
        bool* new_bitmap = realloc(listbox->selection_bitmap, count * sizeof(bool));
        if (new_bitmap) {
            // Zero out new entries
            memset(new_bitmap + listbox->selection_bitmap_size, 0,
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
    if (max_scroll < 0) max_scroll = 0;
    if (listbox->scroll_y > max_scroll) {
        listbox->scroll_y = max_scroll;
    }

    listbox->base.needs_paint = true;
}

void vg_listbox_invalidate_items(vg_listbox_t* listbox) {
    if (!listbox) return;

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

void vg_listbox_invalidate_item(vg_listbox_t* listbox, size_t index) {
    if (!listbox || !listbox->virtual_mode) return;

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

void vg_listbox_select_index(vg_listbox_t* listbox, size_t index) {
    if (!listbox) return;

    if (!listbox->virtual_mode) {
        // Non-virtual mode: find item at index
        vg_listbox_item_t* item = listbox->first_item;
        for (size_t i = 0; i < index && item; i++) {
            item = item->next;
        }
        vg_listbox_select(listbox, item);
        return;
    }

    // Virtual mode
    if (index >= listbox->total_item_count) return;

    // Clear previous selection
    if (!listbox->multi_select && listbox->selected_index < listbox->selection_bitmap_size) {
        listbox->selection_bitmap[listbox->selected_index] = false;
    }

    // Set new selection
    listbox->selected_index = index;
    if (index < listbox->selection_bitmap_size) {
        listbox->selection_bitmap[index] = true;
    }

    listbox->base.needs_paint = true;
}

size_t vg_listbox_get_selected_index(vg_listbox_t* listbox) {
    if (!listbox) return SIZE_MAX;

    if (!listbox->virtual_mode) {
        // Non-virtual mode: find index of selected item
        if (!listbox->selected) return SIZE_MAX;
        size_t index = 0;
        for (vg_listbox_item_t* item = listbox->first_item; item; item = item->next) {
            if (item == listbox->selected) return index;
            index++;
        }
        return SIZE_MAX;
    }

    return listbox->selected_index;
}
