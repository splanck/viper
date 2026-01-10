// vg_radiobutton.c - RadioButton widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

vg_radiogroup_t* vg_radiogroup_create(void) {
    vg_radiogroup_t* group = calloc(1, sizeof(vg_radiogroup_t));
    if (!group) return NULL;

    group->button_capacity = 8;
    group->buttons = calloc(group->button_capacity, sizeof(vg_radiobutton_t*));
    group->selected_index = -1;

    return group;
}

void vg_radiogroup_destroy(vg_radiogroup_t* group) {
    if (!group) return;
    free(group->buttons);
    free(group);
}

vg_radiobutton_t* vg_radiobutton_create(vg_widget_t* parent, const char* text, vg_radiogroup_t* group) {
    vg_radiobutton_t* radio = calloc(1, sizeof(vg_radiobutton_t));
    if (!radio) return NULL;

    radio->base.type = VG_WIDGET_RADIO;
    radio->base.visible = true;
    radio->base.enabled = true;
    radio->text = text ? strdup(text) : NULL;
    radio->group = group;

    // Default appearance
    radio->circle_size = 16;
    radio->gap = 8;
    radio->font_size = 14;
    radio->circle_color = 0xFF5A5A5A;
    radio->fill_color = 0xFF0078D4;
    radio->text_color = 0xFFCCCCCC;

    // Add to group
    if (group) {
        if (group->button_count >= group->button_capacity) {
            int new_cap = group->button_capacity * 2;
            vg_radiobutton_t** new_btns = realloc(group->buttons, new_cap * sizeof(vg_radiobutton_t*));
            if (new_btns) {
                group->buttons = new_btns;
                group->button_capacity = new_cap;
            }
        }
        if (group->button_count < group->button_capacity) {
            group->buttons[group->button_count++] = radio;
        }
    }

    if (parent) {
        vg_widget_add_child(parent, &radio->base);
    }

    return radio;
}

void vg_radiobutton_set_selected(vg_radiobutton_t* radio, bool selected) {
    if (!radio) return;

    if (selected && radio->group) {
        // Deselect all others in group
        for (int i = 0; i < radio->group->button_count; i++) {
            if (radio->group->buttons[i] != radio) {
                radio->group->buttons[i]->selected = false;
            }
        }
        // Update group selected index
        for (int i = 0; i < radio->group->button_count; i++) {
            if (radio->group->buttons[i] == radio) {
                radio->group->selected_index = i;
                break;
            }
        }
    }

    bool old = radio->selected;
    radio->selected = selected;

    if (old != selected && radio->on_change) {
        radio->on_change(&radio->base, selected, radio->on_change_data);
    }
}

bool vg_radiobutton_is_selected(vg_radiobutton_t* radio) {
    return radio ? radio->selected : false;
}

int vg_radiogroup_get_selected(vg_radiogroup_t* group) {
    return group ? group->selected_index : -1;
}

void vg_radiogroup_set_selected(vg_radiogroup_t* group, int index) {
    if (!group || index < 0 || index >= group->button_count) return;
    vg_radiobutton_set_selected(group->buttons[index], true);
}
