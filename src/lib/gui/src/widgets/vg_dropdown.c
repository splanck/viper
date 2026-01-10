// vg_dropdown.c - Dropdown/ComboBox widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

vg_dropdown_t* vg_dropdown_create(vg_widget_t* parent) {
    vg_dropdown_t* dropdown = calloc(1, sizeof(vg_dropdown_t));
    if (!dropdown) return NULL;

    dropdown->base.type = VG_WIDGET_DROPDOWN;
    dropdown->base.visible = true;
    dropdown->base.enabled = true;
    dropdown->selected_index = -1;
    dropdown->hovered_index = -1;
    dropdown->item_capacity = 8;
    dropdown->items = calloc(dropdown->item_capacity, sizeof(char*));

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

    if (parent) {
        vg_widget_add_child(parent, &dropdown->base);
    }

    return dropdown;
}

int vg_dropdown_add_item(vg_dropdown_t* dropdown, const char* text) {
    if (!dropdown || !text) return -1;

    // Grow array if needed
    if (dropdown->item_count >= dropdown->item_capacity) {
        int new_cap = dropdown->item_capacity * 2;
        char** new_items = realloc(dropdown->items, new_cap * sizeof(char*));
        if (!new_items) return -1;
        dropdown->items = new_items;
        dropdown->item_capacity = new_cap;
    }

    dropdown->items[dropdown->item_count] = strdup(text);
    return dropdown->item_count++;
}

void vg_dropdown_remove_item(vg_dropdown_t* dropdown, int index) {
    if (!dropdown || index < 0 || index >= dropdown->item_count) return;

    free(dropdown->items[index]);

    // Shift remaining items
    for (int i = index; i < dropdown->item_count - 1; i++) {
        dropdown->items[i] = dropdown->items[i + 1];
    }
    dropdown->item_count--;

    // Adjust selected index
    if (dropdown->selected_index == index) {
        dropdown->selected_index = -1;
    } else if (dropdown->selected_index > index) {
        dropdown->selected_index--;
    }
}

void vg_dropdown_clear(vg_dropdown_t* dropdown) {
    if (!dropdown) return;

    for (int i = 0; i < dropdown->item_count; i++) {
        free(dropdown->items[i]);
    }
    dropdown->item_count = 0;
    dropdown->selected_index = -1;
}

void vg_dropdown_set_selected(vg_dropdown_t* dropdown, int index) {
    if (!dropdown) return;

    int old_index = dropdown->selected_index;
    if (index < -1 || index >= dropdown->item_count) {
        dropdown->selected_index = -1;
    } else {
        dropdown->selected_index = index;
    }

    if (old_index != dropdown->selected_index && dropdown->on_change) {
        dropdown->on_change(&dropdown->base, dropdown->selected_index,
                           vg_dropdown_get_selected_text(dropdown),
                           dropdown->on_change_data);
    }
}

int vg_dropdown_get_selected(vg_dropdown_t* dropdown) {
    return dropdown ? dropdown->selected_index : -1;
}

const char* vg_dropdown_get_selected_text(vg_dropdown_t* dropdown) {
    if (!dropdown || dropdown->selected_index < 0 ||
        dropdown->selected_index >= dropdown->item_count) {
        return NULL;
    }
    return dropdown->items[dropdown->selected_index];
}

void vg_dropdown_set_placeholder(vg_dropdown_t* dropdown, const char* text) {
    if (!dropdown) return;
    // Free existing placeholder if owned
    dropdown->placeholder = text; // Note: not copying, user must keep string alive
}

void vg_dropdown_set_font(vg_dropdown_t* dropdown, vg_font_t* font, float size) {
    if (!dropdown) return;
    dropdown->font = font;
    dropdown->font_size = size;
}

void vg_dropdown_set_on_change(vg_dropdown_t* dropdown, vg_dropdown_callback_t callback, void* user_data) {
    if (!dropdown) return;
    dropdown->on_change = callback;
    dropdown->on_change_data = user_data;
}
