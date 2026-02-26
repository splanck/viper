// vg_radiobutton.c - RadioButton widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
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

static void radio_destroy(vg_widget_t *widget)
{
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
    free((void *)radio->text);
    radio->text = NULL;
}

static void radio_measure(vg_widget_t *widget, float avail_w, float avail_h)
{
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;
    (void)avail_w;
    (void)avail_h;

    float w = radio->circle_size;
    float h = radio->circle_size;

    if (radio->text && radio->text[0] && radio->font)
    {
        vg_text_metrics_t m;
        vg_font_measure_text(radio->font, radio->font_size, radio->text, &m);
        w += radio->gap + m.width;
        if (m.height > h)
            h = m.height;
    }

    widget->measured_width = w;
    widget->measured_height = h;
    if (widget->measured_height < widget->constraints.min_height)
        widget->measured_height = widget->constraints.min_height;
}

static void radio_paint(vg_widget_t *widget, void *canvas)
{
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
    if (radio->text && radio->font)
    {
        float tx = widget->x + radio->circle_size + radio->gap;
        float ty = cy + radio->font_size * 0.35f;
        vg_font_draw_text(canvas, radio->font, radio->font_size, tx, ty, radio->text, text_col);
    }
}

static bool radio_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_radiobutton_t *radio = (vg_radiobutton_t *)widget;

    if (!widget->enabled)
        return false;

    if (event->type == VG_EVENT_CLICK)
    {
        vg_radiobutton_set_selected(radio, true);
        widget->needs_paint = true;
        event->handled = true;
        return true;
    }

    if (event->type == VG_EVENT_KEY_DOWN && event->key.key == VG_KEY_SPACE)
    {
        vg_radiobutton_set_selected(radio, true);
        widget->needs_paint = true;
        event->handled = true;
        return true;
    }

    return false;
}

static bool radio_can_focus(vg_widget_t *widget)
{
    return widget->enabled && widget->visible;
}

vg_radiogroup_t *vg_radiogroup_create(void)
{
    vg_radiogroup_t *group = calloc(1, sizeof(vg_radiogroup_t));
    if (!group)
        return NULL;

    group->button_capacity = 8;
    group->buttons = calloc(group->button_capacity, sizeof(vg_radiobutton_t *));
    group->selected_index = -1;

    return group;
}

void vg_radiogroup_destroy(vg_radiogroup_t *group)
{
    if (!group)
        return;
    free(group->buttons);
    free(group);
}

vg_radiobutton_t *vg_radiobutton_create(vg_widget_t *parent,
                                        const char *text,
                                        vg_radiogroup_t *group)
{
    vg_radiobutton_t *radio = calloc(1, sizeof(vg_radiobutton_t));
    if (!radio)
        return NULL;

    vg_widget_init(&radio->base, VG_WIDGET_RADIO, &g_radio_vtable);
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
    if (group)
    {
        if (group->button_count >= group->button_capacity)
        {
            int new_cap = group->button_capacity * 2;
            vg_radiobutton_t **new_btns =
                realloc(group->buttons, new_cap * sizeof(vg_radiobutton_t *));
            if (new_btns)
            {
                group->buttons = new_btns;
                group->button_capacity = new_cap;
            }
        }
        if (group->button_count < group->button_capacity)
        {
            group->buttons[group->button_count++] = radio;
        }
    }

    if (parent)
    {
        vg_widget_add_child(parent, &radio->base);
    }

    return radio;
}

void vg_radiobutton_set_selected(vg_radiobutton_t *radio, bool selected)
{
    if (!radio)
        return;

    if (selected && radio->group)
    {
        // Deselect all others in group
        for (int i = 0; i < radio->group->button_count; i++)
        {
            if (radio->group->buttons[i] != radio)
            {
                radio->group->buttons[i]->selected = false;
            }
        }
        // Update group selected index
        for (int i = 0; i < radio->group->button_count; i++)
        {
            if (radio->group->buttons[i] == radio)
            {
                radio->group->selected_index = i;
                break;
            }
        }
    }

    bool old = radio->selected;
    radio->selected = selected;

    if (old != selected && radio->on_change)
    {
        radio->on_change(&radio->base, selected, radio->on_change_data);
    }
}

bool vg_radiobutton_is_selected(vg_radiobutton_t *radio)
{
    return radio ? radio->selected : false;
}

int vg_radiogroup_get_selected(vg_radiogroup_t *group)
{
    return group ? group->selected_index : -1;
}

void vg_radiogroup_set_selected(vg_radiogroup_t *group, int index)
{
    if (!group || index < 0 || index >= group->button_count)
        return;
    vg_radiobutton_set_selected(group->buttons[index], true);
}
