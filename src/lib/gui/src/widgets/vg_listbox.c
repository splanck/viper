// vg_listbox.c - ListBox widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

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
