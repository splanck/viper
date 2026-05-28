//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_radiobutton.c
// Purpose: RadioButton widget implementation — mutually-exclusive buttons linked
//          through a shared vg_radiogroup_t, with per-button change callbacks.
// Key invariants:
//   - Selecting a button automatically deselects all other members of its group.
//   - group->selected_index tracks the index of the selected button within
//     group->buttons[]; -1 means none selected.
//   - Destroying a group nulls each member's group pointer but does not destroy
//     the buttons themselves.
// Ownership/Lifetime:
//   - vg_radiogroup_t is allocated and freed by the caller (vg_radiogroup_create
//     / vg_radiogroup_destroy); buttons outlive the group and gain group==NULL.
//   - radio->text is heap-allocated and freed in radio_destroy.
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

/// @brief VTable destroy: unregisters the button from its group (if any) and frees the label text.
static void radio_destroy(vg_widget_t *widget) {
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
    if (radio->group)
        radiogroup_unregister(radio->group, radio);
    free((void *)radio->text);
    radio->text = NULL;
}

/// @brief VTable measure: sizes the widget to the circle diameter plus optional text extent.
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

/// @brief VTable paint: draws the outer border circle, inner background, optional fill dot
/// (selected), focus ring, and label text.
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

/// @brief VTable handle_event: selects this button on click or Space key; deselection of siblings
/// is handled inside vg_radiobutton_set_selected.
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

/// @brief VTable can_focus: returns true when the widget is both enabled and visible.
static bool radio_can_focus(vg_widget_t *widget) {
    return widget->enabled && widget->visible;
}

/// @brief Create an empty radio group.
///
/// @return Newly allocated vg_radiogroup_t, or NULL on allocation failure.
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

/// @brief Removes @p radio from @p group's button array, compacts the array, and updates
/// selected_index accordingly.
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

/// @brief Destroy a radio group, nulling the group pointer of all member buttons.
///
/// @param group The group to destroy; buttons remain valid after this call.
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

/// @brief Create a radio button and register it with an optional group.
///
/// @param parent Widget to attach to as a child (may be NULL).
/// @param text   Label text displayed beside the button (copied internally).
/// @param group  Group to register with (may be NULL for an ungrouped button).
/// @return Newly allocated vg_radiobutton_t, or NULL on allocation failure.
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

/// @brief Directly updates @p radio's selected state and VG_STATE_CHECKED flag, firing on_change if
/// @p notify is true and the state changed.
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

/// @brief Set this button's selected state; deselects all siblings in its group.
///
/// @param radio    The radio button to update.
/// @param selected true to select; false to deselect.
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

/// @brief Return true if this radio button is currently selected.
///
/// @param radio The radio button to query.
/// @return The selected state, or false if radio is NULL.
bool vg_radiobutton_is_selected(vg_radiobutton_t *radio) {
    return radio ? radio->selected : false;
}

/// @brief Return the index of the currently selected button within a group.
///
/// @param group The radio group to query.
/// @return Zero-based index of the selected button, or -1 if none is selected
///         or if group is NULL.
int vg_radiogroup_get_selected(vg_radiogroup_t *group) {
    return group ? group->selected_index : -1;
}

/// @brief Select a button in the group by zero-based index.
///
/// @details Passing a negative index deselects all buttons without selecting
///          any (group->selected_index is set to -1). Passing an out-of-range
///          positive index is a no-op.
///
/// @param group The radio group to update.
/// @param index Zero-based index of the button to select, or a negative value
///              to clear the selection entirely.
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
