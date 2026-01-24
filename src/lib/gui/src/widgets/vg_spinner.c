// vg_spinner.c - Spinner/NumberInput widget implementation
#include "../../include/vg_widgets.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

vg_spinner_t *vg_spinner_create(vg_widget_t *parent)
{
    vg_spinner_t *spinner = calloc(1, sizeof(vg_spinner_t));
    if (!spinner)
        return NULL;

    spinner->base.type = VG_WIDGET_SPINNER;
    spinner->base.visible = true;
    spinner->base.enabled = true;

    // Default values
    spinner->min_value = 0;
    spinner->max_value = 100;
    spinner->value = 0;
    spinner->step = 1;
    spinner->decimal_places = 0;

    // Allocate text buffer
    spinner->text_buffer = malloc(64);
    if (spinner->text_buffer)
    {
        snprintf(spinner->text_buffer, 64, "%.0f", spinner->value);
    }

    // Default appearance
    spinner->font_size = 14;
    spinner->button_width = 24;
    spinner->bg_color = 0xFF3C3C3C;
    spinner->text_color = 0xFFCCCCCC;
    spinner->border_color = 0xFF5A5A5A;
    spinner->button_color = 0xFF4A4A4A;

    if (parent)
    {
        vg_widget_add_child(parent, &spinner->base);
    }

    return spinner;
}

static void update_text_buffer(vg_spinner_t *spinner)
{
    if (!spinner || !spinner->text_buffer)
        return;
    if (spinner->decimal_places <= 0)
    {
        snprintf(spinner->text_buffer, 64, "%.0f", spinner->value);
    }
    else
    {
        snprintf(spinner->text_buffer, 64, "%.*f", spinner->decimal_places, spinner->value);
    }
}

void vg_spinner_set_value(vg_spinner_t *spinner, double value)
{
    if (!spinner)
        return;

    // Clamp to range
    if (value < spinner->min_value)
        value = spinner->min_value;
    if (value > spinner->max_value)
        value = spinner->max_value;

    double old = spinner->value;
    spinner->value = value;
    update_text_buffer(spinner);

    if (old != value && spinner->on_change)
    {
        spinner->on_change(&spinner->base, value, spinner->on_change_data);
    }
}

double vg_spinner_get_value(vg_spinner_t *spinner)
{
    return spinner ? spinner->value : 0;
}

void vg_spinner_set_range(vg_spinner_t *spinner, double min_val, double max_val)
{
    if (!spinner)
        return;
    spinner->min_value = min_val;
    spinner->max_value = max_val;
    // Re-clamp current value
    vg_spinner_set_value(spinner, spinner->value);
}

void vg_spinner_set_step(vg_spinner_t *spinner, double step)
{
    if (!spinner)
        return;
    spinner->step = step > 0 ? step : 1;
}

void vg_spinner_set_decimals(vg_spinner_t *spinner, int decimals)
{
    if (!spinner)
        return;
    spinner->decimal_places = decimals > 0 ? decimals : 0;
    update_text_buffer(spinner);
}

void vg_spinner_set_font(vg_spinner_t *spinner, vg_font_t *font, float size)
{
    if (!spinner)
        return;
    spinner->font = font;
    spinner->font_size = size;
}

void vg_spinner_set_on_change(vg_spinner_t *spinner,
                              vg_spinner_callback_t callback,
                              void *user_data)
{
    if (!spinner)
        return;
    spinner->on_change = callback;
    spinner->on_change_data = user_data;
}
