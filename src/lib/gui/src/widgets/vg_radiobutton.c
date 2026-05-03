//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_radiobutton.c
//
//===----------------------------------------------------------------------===//
// vg_radiobutton.c - RadioButton widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void radio_destroy(vg_widget_t *widget);
static void radio_measure(vg_widget_t *widget, float avail_w, float avail_h);
static void radio_paint(vg_widget_t *widget, void *canvas);
static bool radio_handle_event(vg_widget_t *widget, vg_event_t *event);
static bool radio_can_focus(vg_widget_t *widget);
static void radiogroup_unregister(vg_radiogroup_t *group, vg_radiobutton_t *radio);
static void radio_apply_selected(vg_radiobutton_t *radio, bool selected, bool notify);

//=============================================================================
// RadioButton VTable
//=============================================================================

static vg_widget_vtable_t g_radio_vtable = {.destroy = radio_destroy,
                                            .measure = radio_measure,
                                            .arrange = NULL,
                                            .paint = radio_paint,
                                            .handle_event = radio_handle_event,
                                            .can_focus = radio_can_focus,
                                            .on_focus = NULL};

static void radio_destroy(vg_widget_t *widget) {
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
    if (radio->group)
        radiogroup_unregister(radio->group, radio);
    free((void *)radio->text);
    radio->text = NULL;
}

static void radio_measure(vg_widget_t *widget, float avail_w, float avail_h) {
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
    (void)avail_w;
    (void)avail_h;

    float w = radio->circle_size;
    float h = radio->circle_size;

    if (radio->text && radio->text[0] && radio->font) {
        vg_text_metrics_t m;
        vg_font_measure_text(radio->font, radio->font_size, radio->text, &m);
        w += radio->gap + m.width;
        if (m.height > h)
            h = m.height;
    }

    widget->measured_width = w;
    widget->measured_height = h;
    vg_widget_apply_constraints(widget);
}

static void radio_paint(vg_widget_t *widget, void *canvas) {
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();
    vgfx_window_t win = (vgfx_window_t)canvas;

    float r = radio->circle_size / 2.0f;
    float cx = widget->x + r;
    float cy = widget->y + widget->height / 2.0f;

    uint32_t border_col =
        (widget->state & VG_STATE_DISABLED) ? theme->colors.fg_disabled : radio->circle_color;
    uint32_t text_col =
        (widget->state & VG_STATE_DISABLED) ? theme->colors.fg_disabled : radio->text_color;

    // Outer circle (border)
    vgfx_fill_circle(win, (int32_t)cx, (int32_t)cy, (int32_t)r, border_col);

    // Inner background (slightly smaller)
    vgfx_fill_circle(win, (int32_t)cx, (int32_t)cy, (int32_t)(r - 1), theme->colors.bg_primary);

    // Fill dot when selected
    if (radio->selected)
        vgfx_fill_circle(win, (int32_t)cx, (int32_t)cy, (int32_t)(r * 0.5f), radio->fill_color);

    // Focus ring
    if (widget->state & VG_STATE_FOCUSED)
        vgfx_circle(win, (int32_t)cx, (int32_t)cy, (int32_t)(r + 2), theme->colors.border_focus);

    // Label text
    if (radio->text && radio->font) {
        float tx = widget->x + radio->circle_size + radio->gap;
        float ty = cy + radio->font_size * 0.35f;
        vg_font_draw_text(canvas, radio->font, radio->font_size, tx, ty, radio->text, text_col);
    }
}

static bool radio_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;

    if (!widget->enabled)
        return false;

    if (event->type == VG_EVENT_CLICK) {
        vg_radiobutton_set_selected(radio, true);
        widget->needs_paint = true;
        event->handled = true;
        return true;
    }

    if (event->type == VG_EVENT_KEY_DOWN && event->key.key == VG_KEY_SPACE) {
        vg_radiobutton_set_selected(radio, true);
        widget->needs_paint = true;
        event->handled = true;
        return true;
    }

    return false;
}

static bool radio_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

vg_radiogroup_t *vg_radiogroup_create(void) {
    vg_radiogroup_t *group = calloc(1, sizeof(vg_radiogroup_t));
    if (!group)
        return NULL;

    group->button_capacity = 8;
    group->buttons = calloc(group->button_capacity, sizeof(vg_radiobutton_t *));
    if (!group->buttons) {
        free(group);
        return NULL;
    }
    group->selected_index = -1;

    return group;
}

static void radiogroup_unregister(vg_radiogroup_t *group, vg_radiobutton_t *radio) {
    if (!group || !radio)
        return;

    int removed_index = -1;
    for (int i = 0; i < group->button_count; i++) {
        if (group->buttons[i] == radio) {
            removed_index = i;
            break;
        }
    }
    if (removed_index < 0)
        return;

    for (int i = removed_index; i < group->button_count - 1; i++)
        group->buttons[i] = group->buttons[i + 1];
    group->button_count--;
    if (group->button_count >= 0)
        group->buttons[group->button_count] = NULL;

    radio->group = NULL;
    if (group->selected_index == removed_index) {
        group->selected_index = -1;
        for (int i = 0; i < group->button_count; i++) {
            if (group->buttons[i] && group->buttons[i]->selected) {
                group->selected_index = i;
                break;
            }
        }
    } else if (group->selected_index > removed_index) {
        group->selected_index--;
    }
}

/// @brief Radiogroup destroy.
void vg_radiogroup_destroy(vg_radiogroup_t *group) {
    if (!group)
        return;
    for (int i = 0; i < group->button_count; i++) {
        if (group->buttons[i])
            group->buttons[i]->group = NULL;
    }
    free(group->buttons);
    free(group);
}

vg_radiobutton_t *vg_radiobutton_create(vg_widget_t *parent,
                                        const char *text,
                                        vg_radiogroup_t *group) {
    vg_radiobutton_t *radio = calloc(1, sizeof(vg_radiobutton_t));
    if (!radio)
        return NULL;

    vg_widget_init(&radio->base, VG_WIDGET_RADIO, &g_radio_vtable);
    radio->text = text ? strdup(text) : NULL;
    if (text && !radio->text) {
        vg_widget_destroy(&radio->base);
        return NULL;
    }
    radio->group = NULL;

    // Default appearance
    radio->circle_size = 16;
    radio->gap = 8;
    radio->font_size = 14;
    radio->circle_color = 0xFF5A5A5A;
    radio->fill_color = 0xFF0078D4;
    radio->text_color = 0xFFCCCCCC;
    radio->font = vg_theme_get_current()->typography.font_regular;

    // Add to group
    if (group) {
        if (!group->buttons || group->button_capacity <= 0) {
            vg_widget_destroy(&radio->base);
            return NULL;
        }
        if (group->button_count >= group->button_capacity) {
            if (group->button_capacity > INT32_MAX / 2) {
                vg_widget_destroy(&radio->base);
                return NULL;
            }
            int new_cap = group->button_capacity * 2;
            if ((size_t)new_cap > SIZE_MAX / sizeof(vg_radiobutton_t *)) {
                vg_widget_destroy(&radio->base);
                return NULL;
            }
            vg_radiobutton_t **new_btns =
                realloc(group->buttons, (size_t)new_cap * sizeof(vg_radiobutton_t *));
            if (!new_btns) {
                vg_widget_destroy(&radio->base);
                return NULL;
            }
            group->buttons = new_btns;
            group->button_capacity = new_cap;
        }
        group->buttons[group->button_count++] = radio;
        radio->group = group;
    }

    if (parent) {
        vg_widget_add_child(parent, &radio->base);
    }

    return radio;
}

static void radio_apply_selected(vg_radiobutton_t *radio, bool selected, bool notify) {
    if (!radio)
        return;

    bool old = radio->selected;
    radio->selected = selected;
    if (selected)
        radio->base.state |= VG_STATE_CHECKED;
    else
        radio->base.state &= ~VG_STATE_CHECKED;

    if (old != selected) {
        radio->base.needs_paint = true;
        if (notify && radio->on_change)
            radio->on_change(&radio->base, selected, radio->on_change_data);
    }
}

/// @brief Radiobutton set selected.
void vg_radiobutton_set_selected(vg_radiobutton_t *radio, bool selected) {
    if (!radio)
        return;

    if (selected && radio->group) {
        int own_index = -1;
        // Deselect all others in group
        for (int i = 0; i < radio->group->button_count; i++) {
            vg_radiobutton_t *member = radio->group->buttons[i];
            if (!member)
                continue;
            if (member == radio) {
                own_index = i;
            } else {
                radio_apply_selected(member, false, true);
            }
        }
        if (own_index >= 0)
            radio->group->selected_index = own_index;
    }

    radio_apply_selected(radio, selected, true);

    if (!selected && radio->group) {
        int selected_index = radio->group->selected_index;
        if (selected_index >= 0 && selected_index < radio->group->button_count &&
            radio->group->buttons[selected_index] == radio) {
            radio->group->selected_index = -1;
            for (int i = 0; i < radio->group->button_count; i++) {
                if (radio->group->buttons[i] && radio->group->buttons[i]->selected) {
                    radio->group->selected_index = i;
                    break;
                }
            }
        }
    }
}

bool vg_radiobutton_is_selected(vg_radiobutton_t *radio) {
    return radio ? radio->selected : false;
}

/// @brief Radiogroup get selected.
int vg_radiogroup_get_selected(vg_radiogroup_t *group) {
    return group ? group->selected_index : -1;
}

/// @brief Radiogroup set selected.
void vg_radiogroup_set_selected(vg_radiogroup_t *group, int index) {
    if (!group)
        return;
    if (index < 0) {
        for (int i = 0; i < group->button_count; i++)
            radio_apply_selected(group->buttons[i], false, true);
        group->selected_index = -1;
        return;
    }
    if (index >= group->button_count)
        return;
    vg_radiobutton_set_selected(group->buttons[index], true);
}
