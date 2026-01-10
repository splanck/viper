// vg_progressbar.c - ProgressBar widget implementation
#include "../../include/vg_widgets.h"
#include <stdlib.h>

vg_progressbar_t* vg_progressbar_create(vg_widget_t* parent) {
    vg_progressbar_t* progress = calloc(1, sizeof(vg_progressbar_t));
    if (!progress) return NULL;

    progress->base.type = VG_WIDGET_PROGRESS;
    progress->base.visible = true;
    progress->base.enabled = true;

    // Default values
    progress->value = 0;
    progress->style = VG_PROGRESS_BAR;

    // Default appearance
    progress->track_color = 0xFF3C3C3C;
    progress->fill_color = 0xFF0078D4;
    progress->corner_radius = 4;
    progress->font_size = 12;

    if (parent) {
        vg_widget_add_child(parent, &progress->base);
    }

    return progress;
}

void vg_progressbar_set_value(vg_progressbar_t* progress, float value) {
    if (!progress) return;
    if (value < 0) value = 0;
    if (value > 1) value = 1;
    progress->value = value;
}

float vg_progressbar_get_value(vg_progressbar_t* progress) {
    return progress ? progress->value : 0;
}

void vg_progressbar_set_style(vg_progressbar_t* progress, vg_progress_style_t style) {
    if (!progress) return;
    progress->style = style;
}

void vg_progressbar_show_percentage(vg_progressbar_t* progress, bool show) {
    if (!progress) return;
    progress->show_percentage = show;
}
