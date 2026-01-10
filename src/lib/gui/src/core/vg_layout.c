// vg_layout.c - Layout system implementation
#include "../../include/vg_layout.h"
#include "../../include/vg_widget.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Layout-specific vtables
//=============================================================================

static void vbox_measure(vg_widget_t* self, float available_width, float available_height);
static void vbox_arrange(vg_widget_t* self, float x, float y, float width, float height);
static void hbox_measure(vg_widget_t* self, float available_width, float available_height);
static void hbox_arrange(vg_widget_t* self, float x, float y, float width, float height);
static void flex_measure(vg_widget_t* self, float available_width, float available_height);
static void flex_arrange(vg_widget_t* self, float x, float y, float width, float height);

static const vg_widget_vtable_t g_vbox_vtable = {
    .measure = vbox_measure,
    .arrange = vbox_arrange,
};

static const vg_widget_vtable_t g_hbox_vtable = {
    .measure = hbox_measure,
    .arrange = hbox_arrange,
};

static const vg_widget_vtable_t g_flex_vtable = {
    .measure = flex_measure,
    .arrange = flex_arrange,
};

//=============================================================================
// VBox Implementation
//=============================================================================

vg_widget_t* vg_vbox_create(float spacing) {
    vg_widget_t* widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget) return NULL;

    widget->vtable = &g_vbox_vtable;

    // Allocate layout data
    vg_vbox_layout_t* layout = calloc(1, sizeof(vg_vbox_layout_t));
    if (!layout) {
        vg_widget_destroy(widget);
        return NULL;
    }

    layout->spacing = spacing;
    layout->align = VG_ALIGN_STRETCH;
    layout->justify = VG_JUSTIFY_START;
    widget->impl_data = layout;

    return widget;
}

void vg_vbox_set_spacing(vg_widget_t* vbox, float spacing) {
    if (!vbox || !vbox->impl_data) return;
    vg_vbox_layout_t* layout = (vg_vbox_layout_t*)vbox->impl_data;
    layout->spacing = spacing;
    vbox->needs_layout = true;
}

void vg_vbox_set_align(vg_widget_t* vbox, vg_align_t align) {
    if (!vbox || !vbox->impl_data) return;
    vg_vbox_layout_t* layout = (vg_vbox_layout_t*)vbox->impl_data;
    layout->align = align;
    vbox->needs_layout = true;
}

void vg_vbox_set_justify(vg_widget_t* vbox, vg_justify_t justify) {
    if (!vbox || !vbox->impl_data) return;
    vg_vbox_layout_t* layout = (vg_vbox_layout_t*)vbox->impl_data;
    layout->justify = justify;
    vbox->needs_layout = true;
}

static void vbox_measure(vg_widget_t* self, float available_width, float available_height) {
    if (!self || !self->impl_data) return;

    vg_vbox_layout_t* layout = (vg_vbox_layout_t*)self->impl_data;

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;

    float max_width = 0;
    float total_height = 0;
    int visible_count = 0;

    // First pass: measure children
    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        vg_widget_measure(child, available_width - padding_h, available_height - padding_v);

        float child_width = child->measured_width + child->layout.margin_left + child->layout.margin_right;
        float child_height = child->measured_height + child->layout.margin_top + child->layout.margin_bottom;

        if (child_width > max_width) max_width = child_width;
        total_height += child_height;
        visible_count++;
    }

    // Add spacing between children
    if (visible_count > 1) {
        total_height += layout->spacing * (visible_count - 1);
    }

    // Apply constraints
    self->measured_width = max_width + padding_h;
    self->measured_height = total_height + padding_v;

    if (self->constraints.min_width > 0 && self->measured_width < self->constraints.min_width) {
        self->measured_width = self->constraints.min_width;
    }
    if (self->constraints.min_height > 0 && self->measured_height < self->constraints.min_height) {
        self->measured_height = self->constraints.min_height;
    }
}

static void vbox_arrange(vg_widget_t* self, float x, float y, float width, float height) {
    if (!self || !self->impl_data) return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    vg_vbox_layout_t* layout = (vg_vbox_layout_t*)self->impl_data;

    float content_x = self->layout.padding_left;
    float content_y = self->layout.padding_top;
    float content_width = width - self->layout.padding_left - self->layout.padding_right;
    float content_height = height - self->layout.padding_top - self->layout.padding_bottom;

    // Calculate total fixed height and flex
    float total_fixed = 0;
    float total_flex = 0;
    int visible_count = 0;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        if (child->layout.flex > 0) {
            total_flex += child->layout.flex;
        } else {
            total_fixed += child->measured_height + child->layout.margin_top + child->layout.margin_bottom;
        }
        visible_count++;
    }

    // Add spacing
    float total_spacing = (visible_count > 1) ? layout->spacing * (visible_count - 1) : 0;
    float available = content_height - total_fixed - total_spacing;
    float flex_unit = (total_flex > 0 && available > 0) ? available / total_flex : 0;

    // Arrange children
    float child_y = content_y;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        float child_height;
        if (child->layout.flex > 0) {
            child_height = flex_unit * child->layout.flex;
        } else {
            child_height = child->measured_height;
        }

        // Calculate child X based on alignment
        float child_x;
        float child_width;

        switch (layout->align) {
            case VG_ALIGN_START:
                child_x = content_x + child->layout.margin_left;
                child_width = child->measured_width;
                break;
            case VG_ALIGN_CENTER:
                child_x = content_x + (content_width - child->measured_width) / 2;
                child_width = child->measured_width;
                break;
            case VG_ALIGN_END:
                child_x = content_x + content_width - child->measured_width - child->layout.margin_right;
                child_width = child->measured_width;
                break;
            case VG_ALIGN_STRETCH:
            default:
                child_x = content_x + child->layout.margin_left;
                child_width = content_width - child->layout.margin_left - child->layout.margin_right;
                break;
        }

        vg_widget_arrange(child, child_x, child_y + child->layout.margin_top, child_width, child_height);
        child_y += child_height + child->layout.margin_top + child->layout.margin_bottom + layout->spacing;
    }
}

//=============================================================================
// HBox Implementation
//=============================================================================

vg_widget_t* vg_hbox_create(float spacing) {
    vg_widget_t* widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget) return NULL;

    widget->vtable = &g_hbox_vtable;

    vg_hbox_layout_t* layout = calloc(1, sizeof(vg_hbox_layout_t));
    if (!layout) {
        vg_widget_destroy(widget);
        return NULL;
    }

    layout->spacing = spacing;
    layout->align = VG_ALIGN_STRETCH;
    layout->justify = VG_JUSTIFY_START;
    widget->impl_data = layout;

    return widget;
}

void vg_hbox_set_spacing(vg_widget_t* hbox, float spacing) {
    if (!hbox || !hbox->impl_data) return;
    vg_hbox_layout_t* layout = (vg_hbox_layout_t*)hbox->impl_data;
    layout->spacing = spacing;
    hbox->needs_layout = true;
}

void vg_hbox_set_align(vg_widget_t* hbox, vg_align_t align) {
    if (!hbox || !hbox->impl_data) return;
    vg_hbox_layout_t* layout = (vg_hbox_layout_t*)hbox->impl_data;
    layout->align = align;
    hbox->needs_layout = true;
}

void vg_hbox_set_justify(vg_widget_t* hbox, vg_justify_t justify) {
    if (!hbox || !hbox->impl_data) return;
    vg_hbox_layout_t* layout = (vg_hbox_layout_t*)hbox->impl_data;
    layout->justify = justify;
    hbox->needs_layout = true;
}

static void hbox_measure(vg_widget_t* self, float available_width, float available_height) {
    if (!self || !self->impl_data) return;

    vg_hbox_layout_t* layout = (vg_hbox_layout_t*)self->impl_data;

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;

    float total_width = 0;
    float max_height = 0;
    int visible_count = 0;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        vg_widget_measure(child, available_width - padding_h, available_height - padding_v);

        float child_width = child->measured_width + child->layout.margin_left + child->layout.margin_right;
        float child_height = child->measured_height + child->layout.margin_top + child->layout.margin_bottom;

        total_width += child_width;
        if (child_height > max_height) max_height = child_height;
        visible_count++;
    }

    if (visible_count > 1) {
        total_width += layout->spacing * (visible_count - 1);
    }

    self->measured_width = total_width + padding_h;
    self->measured_height = max_height + padding_v;

    if (self->constraints.min_width > 0 && self->measured_width < self->constraints.min_width) {
        self->measured_width = self->constraints.min_width;
    }
    if (self->constraints.min_height > 0 && self->measured_height < self->constraints.min_height) {
        self->measured_height = self->constraints.min_height;
    }
}

static void hbox_arrange(vg_widget_t* self, float x, float y, float width, float height) {
    if (!self || !self->impl_data) return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    vg_hbox_layout_t* layout = (vg_hbox_layout_t*)self->impl_data;

    float content_x = self->layout.padding_left;
    float content_y = self->layout.padding_top;
    float content_width = width - self->layout.padding_left - self->layout.padding_right;
    float content_height = height - self->layout.padding_top - self->layout.padding_bottom;

    float total_fixed = 0;
    float total_flex = 0;
    int visible_count = 0;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        if (child->layout.flex > 0) {
            total_flex += child->layout.flex;
        } else {
            total_fixed += child->measured_width + child->layout.margin_left + child->layout.margin_right;
        }
        visible_count++;
    }

    float total_spacing = (visible_count > 1) ? layout->spacing * (visible_count - 1) : 0;
    float available = content_width - total_fixed - total_spacing;
    float flex_unit = (total_flex > 0 && available > 0) ? available / total_flex : 0;

    float child_x = content_x;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        float child_width;
        if (child->layout.flex > 0) {
            child_width = flex_unit * child->layout.flex;
        } else {
            child_width = child->measured_width;
        }

        float child_y;
        float child_height;

        switch (layout->align) {
            case VG_ALIGN_START:
                child_y = content_y + child->layout.margin_top;
                child_height = child->measured_height;
                break;
            case VG_ALIGN_CENTER:
                child_y = content_y + (content_height - child->measured_height) / 2;
                child_height = child->measured_height;
                break;
            case VG_ALIGN_END:
                child_y = content_y + content_height - child->measured_height - child->layout.margin_bottom;
                child_height = child->measured_height;
                break;
            case VG_ALIGN_STRETCH:
            default:
                child_y = content_y + child->layout.margin_top;
                child_height = content_height - child->layout.margin_top - child->layout.margin_bottom;
                break;
        }

        vg_widget_arrange(child, child_x + child->layout.margin_left, child_y, child_width, child_height);
        child_x += child_width + child->layout.margin_left + child->layout.margin_right + layout->spacing;
    }
}

//=============================================================================
// Flex Layout Implementation
//=============================================================================

vg_widget_t* vg_flex_create(void) {
    vg_widget_t* widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget) return NULL;

    widget->vtable = &g_flex_vtable;

    vg_flex_layout_t* layout = calloc(1, sizeof(vg_flex_layout_t));
    if (!layout) {
        vg_widget_destroy(widget);
        return NULL;
    }

    layout->direction = VG_DIRECTION_ROW;
    layout->align_items = VG_ALIGN_STRETCH;
    layout->justify_content = VG_JUSTIFY_START;
    layout->gap = 0;
    layout->wrap = false;
    widget->impl_data = layout;

    return widget;
}

void vg_flex_set_direction(vg_widget_t* flex, vg_direction_t direction) {
    if (!flex || !flex->impl_data) return;
    vg_flex_layout_t* layout = (vg_flex_layout_t*)flex->impl_data;
    layout->direction = direction;
    flex->needs_layout = true;
}

void vg_flex_set_align_items(vg_widget_t* flex, vg_align_t align) {
    if (!flex || !flex->impl_data) return;
    vg_flex_layout_t* layout = (vg_flex_layout_t*)flex->impl_data;
    layout->align_items = align;
    flex->needs_layout = true;
}

void vg_flex_set_justify_content(vg_widget_t* flex, vg_justify_t justify) {
    if (!flex || !flex->impl_data) return;
    vg_flex_layout_t* layout = (vg_flex_layout_t*)flex->impl_data;
    layout->justify_content = justify;
    flex->needs_layout = true;
}

void vg_flex_set_gap(vg_widget_t* flex, float gap) {
    if (!flex || !flex->impl_data) return;
    vg_flex_layout_t* layout = (vg_flex_layout_t*)flex->impl_data;
    layout->gap = gap;
    flex->needs_layout = true;
}

void vg_flex_set_wrap(vg_widget_t* flex, bool wrap) {
    if (!flex || !flex->impl_data) return;
    vg_flex_layout_t* layout = (vg_flex_layout_t*)flex->impl_data;
    layout->wrap = wrap;
    flex->needs_layout = true;
}

static void flex_measure(vg_widget_t* self, float available_width, float available_height) {
    if (!self || !self->impl_data) return;

    vg_flex_layout_t* layout = (vg_flex_layout_t*)self->impl_data;
    bool is_row = (layout->direction == VG_DIRECTION_ROW || layout->direction == VG_DIRECTION_ROW_REVERSE);

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;

    float main_size = 0;
    float cross_size = 0;
    int visible_count = 0;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        vg_widget_measure(child, available_width - padding_h, available_height - padding_v);

        float child_main = is_row ?
            child->measured_width + child->layout.margin_left + child->layout.margin_right :
            child->measured_height + child->layout.margin_top + child->layout.margin_bottom;
        float child_cross = is_row ?
            child->measured_height + child->layout.margin_top + child->layout.margin_bottom :
            child->measured_width + child->layout.margin_left + child->layout.margin_right;

        main_size += child_main;
        if (child_cross > cross_size) cross_size = child_cross;
        visible_count++;
    }

    if (visible_count > 1) {
        main_size += layout->gap * (visible_count - 1);
    }

    if (is_row) {
        self->measured_width = main_size + padding_h;
        self->measured_height = cross_size + padding_v;
    } else {
        self->measured_width = cross_size + padding_h;
        self->measured_height = main_size + padding_v;
    }
}

static void flex_arrange(vg_widget_t* self, float x, float y, float width, float height) {
    if (!self || !self->impl_data) return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    vg_flex_layout_t* layout = (vg_flex_layout_t*)self->impl_data;
    bool is_row = (layout->direction == VG_DIRECTION_ROW || layout->direction == VG_DIRECTION_ROW_REVERSE);
    bool is_reverse = (layout->direction == VG_DIRECTION_ROW_REVERSE || layout->direction == VG_DIRECTION_COLUMN_REVERSE);

    float content_x = self->layout.padding_left;
    float content_y = self->layout.padding_top;
    float content_width = width - self->layout.padding_left - self->layout.padding_right;
    float content_height = height - self->layout.padding_top - self->layout.padding_bottom;

    float main_size = is_row ? content_width : content_height;
    float cross_size = is_row ? content_height : content_width;

    // Calculate total fixed and flex
    float total_fixed = 0;
    float total_flex = 0;
    int visible_count = 0;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        float child_main = is_row ?
            child->measured_width + child->layout.margin_left + child->layout.margin_right :
            child->measured_height + child->layout.margin_top + child->layout.margin_bottom;

        if (child->layout.flex > 0) {
            total_flex += child->layout.flex;
        } else {
            total_fixed += child_main;
        }
        visible_count++;
    }

    float gap_total = (visible_count > 1) ? layout->gap * (visible_count - 1) : 0;
    float available = main_size - total_fixed - gap_total;
    float flex_unit = (total_flex > 0 && available > 0) ? available / total_flex : 0;

    float main_pos = is_reverse ? main_size : 0;

    for (vg_widget_t* child = self->first_child; child; child = child->next_sibling) {
        if (!child->visible) continue;

        float child_main_size;
        if (child->layout.flex > 0) {
            child_main_size = flex_unit * child->layout.flex;
        } else {
            child_main_size = is_row ? child->measured_width : child->measured_height;
        }

        float child_cross_size;
        if (layout->align_items == VG_ALIGN_STRETCH) {
            child_cross_size = cross_size;
        } else {
            child_cross_size = is_row ? child->measured_height : child->measured_width;
        }

        float child_x, child_y, child_w, child_h;

        if (is_row) {
            child_w = child_main_size;
            child_h = child_cross_size - child->layout.margin_top - child->layout.margin_bottom;

            if (is_reverse) {
                main_pos -= child_main_size + child->layout.margin_right;
                child_x = content_x + main_pos;
                main_pos -= child->layout.margin_left + layout->gap;
            } else {
                child_x = content_x + main_pos + child->layout.margin_left;
                main_pos += child_main_size + child->layout.margin_left + child->layout.margin_right + layout->gap;
            }

            switch (layout->align_items) {
                case VG_ALIGN_START:
                    child_y = content_y + child->layout.margin_top;
                    break;
                case VG_ALIGN_CENTER:
                    child_y = content_y + (cross_size - child_h) / 2;
                    break;
                case VG_ALIGN_END:
                    child_y = content_y + cross_size - child_h - child->layout.margin_bottom;
                    break;
                default:
                    child_y = content_y + child->layout.margin_top;
                    break;
            }
        } else {
            child_h = child_main_size;
            child_w = child_cross_size - child->layout.margin_left - child->layout.margin_right;

            if (is_reverse) {
                main_pos -= child_main_size + child->layout.margin_bottom;
                child_y = content_y + main_pos;
                main_pos -= child->layout.margin_top + layout->gap;
            } else {
                child_y = content_y + main_pos + child->layout.margin_top;
                main_pos += child_main_size + child->layout.margin_top + child->layout.margin_bottom + layout->gap;
            }

            switch (layout->align_items) {
                case VG_ALIGN_START:
                    child_x = content_x + child->layout.margin_left;
                    break;
                case VG_ALIGN_CENTER:
                    child_x = content_x + (cross_size - child_w) / 2;
                    break;
                case VG_ALIGN_END:
                    child_x = content_x + cross_size - child_w - child->layout.margin_right;
                    break;
                default:
                    child_x = content_x + child->layout.margin_left;
                    break;
            }
        }

        vg_widget_arrange(child, child_x, child_y, child_w, child_h);
    }
}

//=============================================================================
// Layout Engine Entry Points
//=============================================================================

void vg_layout_vbox(vg_widget_t* container, float width, float height) {
    vbox_arrange(container, container->x, container->y, width, height);
}

void vg_layout_hbox(vg_widget_t* container, float width, float height) {
    hbox_arrange(container, container->x, container->y, width, height);
}

void vg_layout_flex(vg_widget_t* container, float width, float height) {
    flex_arrange(container, container->x, container->y, width, height);
}
