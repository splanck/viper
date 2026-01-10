// vg_splitpane.c - SplitPane widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void splitpane_destroy(vg_widget_t* widget);
static void splitpane_measure(vg_widget_t* widget, float available_width, float available_height);
static void splitpane_arrange(vg_widget_t* widget, float x, float y, float width, float height);
static void splitpane_paint(vg_widget_t* widget, void* canvas);
static bool splitpane_handle_event(vg_widget_t* widget, vg_event_t* event);

//=============================================================================
// SplitPane VTable
//=============================================================================

static vg_widget_vtable_t g_splitpane_vtable = {
    .destroy = splitpane_destroy,
    .measure = splitpane_measure,
    .arrange = splitpane_arrange,
    .paint = splitpane_paint,
    .handle_event = splitpane_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// SplitPane Implementation
//=============================================================================

vg_splitpane_t* vg_splitpane_create(vg_widget_t* parent, vg_split_direction_t direction) {
    vg_splitpane_t* split = calloc(1, sizeof(vg_splitpane_t));
    if (!split) return NULL;

    // Initialize base widget
    vg_widget_init(&split->base, VG_WIDGET_SPLITPANE, &g_splitpane_vtable);

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize splitpane-specific fields
    split->direction = direction;
    split->split_position = 0.5f;  // 50% by default
    split->min_first_size = 50.0f;
    split->min_second_size = 50.0f;
    split->splitter_size = 4.0f;

    split->splitter_color = theme->colors.border_primary;
    split->splitter_hover_color = theme->colors.border_focus;

    // State
    split->splitter_hovered = false;
    split->dragging = false;
    split->drag_start = 0;
    split->drag_start_split = 0;

    // Create two child panes (containers)
    vg_widget_t* first = vg_widget_create(VG_WIDGET_CONTAINER);
    vg_widget_t* second = vg_widget_create(VG_WIDGET_CONTAINER);

    if (first) vg_widget_add_child(&split->base, first);
    if (second) vg_widget_add_child(&split->base, second);

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &split->base);
    }

    return split;
}

static void splitpane_destroy(vg_widget_t* widget) {
    // Children are destroyed by base widget cleanup
    (void)widget;
}

static void splitpane_measure(vg_widget_t* widget, float available_width, float available_height) {
    // SplitPane fills available space
    widget->measured_width = available_width > 0 ? available_width : 400;
    widget->measured_height = available_height > 0 ? available_height : 300;

    // Apply constraints
    if (widget->constraints.preferred_width > 0) {
        widget->measured_width = widget->constraints.preferred_width;
    }
    if (widget->constraints.preferred_height > 0) {
        widget->measured_height = widget->constraints.preferred_height;
    }
    if (widget->measured_width < widget->constraints.min_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height) {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void splitpane_arrange(vg_widget_t* widget, float x, float y, float width, float height) {
    vg_splitpane_t* split = (vg_splitpane_t*)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    // Get the two panes
    vg_widget_t* first = widget->first_child;
    vg_widget_t* second = first ? first->next_sibling : NULL;

    if (!first || !second) return;

    if (split->direction == VG_SPLIT_HORIZONTAL) {
        // Left/Right split
        float available = width - split->splitter_size;
        float first_width = available * split->split_position;

        // Clamp to minimum sizes
        if (first_width < split->min_first_size) {
            first_width = split->min_first_size;
        }
        if (available - first_width < split->min_second_size) {
            first_width = available - split->min_second_size;
        }

        float second_width = available - first_width;

        // Arrange first pane
        if (first->vtable && first->vtable->arrange) {
            first->vtable->arrange(first, 0, 0, first_width, height);
        } else {
            first->x = 0;
            first->y = 0;
            first->width = first_width;
            first->height = height;
        }

        // Arrange second pane
        if (second->vtable && second->vtable->arrange) {
            second->vtable->arrange(second, first_width + split->splitter_size, 0, second_width, height);
        } else {
            second->x = first_width + split->splitter_size;
            second->y = 0;
            second->width = second_width;
            second->height = height;
        }
    } else {
        // Top/Bottom split
        float available = height - split->splitter_size;
        float first_height = available * split->split_position;

        // Clamp to minimum sizes
        if (first_height < split->min_first_size) {
            first_height = split->min_first_size;
        }
        if (available - first_height < split->min_second_size) {
            first_height = available - split->min_second_size;
        }

        float second_height = available - first_height;

        // Arrange first pane
        if (first->vtable && first->vtable->arrange) {
            first->vtable->arrange(first, 0, 0, width, first_height);
        } else {
            first->x = 0;
            first->y = 0;
            first->width = width;
            first->height = first_height;
        }

        // Arrange second pane
        if (second->vtable && second->vtable->arrange) {
            second->vtable->arrange(second, 0, first_height + split->splitter_size, width, second_height);
        } else {
            second->x = 0;
            second->y = first_height + split->splitter_size;
            second->width = width;
            second->height = second_height;
        }
    }
}

static void splitpane_paint(vg_widget_t* widget, void* canvas) {
    vg_splitpane_t* split = (vg_splitpane_t*)widget;

    // Paint children first
    for (vg_widget_t* child = widget->first_child; child; child = child->next_sibling) {
        if (child->visible && child->vtable && child->vtable->paint) {
            child->vtable->paint(child, canvas);
        }
    }

    // Draw splitter
    uint32_t color = split->splitter_hovered || split->dragging
        ? split->splitter_hover_color : split->splitter_color;

    vg_widget_t* first = widget->first_child;
    if (!first) return;

    float splitter_x, splitter_y, splitter_w, splitter_h;

    if (split->direction == VG_SPLIT_HORIZONTAL) {
        splitter_x = widget->x + first->width;
        splitter_y = widget->y;
        splitter_w = split->splitter_size;
        splitter_h = widget->height;
    } else {
        splitter_x = widget->x;
        splitter_y = widget->y + first->height;
        splitter_w = widget->width;
        splitter_h = split->splitter_size;
    }

    // Draw splitter bar
    // TODO: Use vgfx primitives
    (void)splitter_x;
    (void)splitter_y;
    (void)splitter_w;
    (void)splitter_h;
    (void)color;
}

static bool splitpane_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_splitpane_t* split = (vg_splitpane_t*)widget;

    vg_widget_t* first = widget->first_child;
    if (!first) return false;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            float local_x = event->mouse.x;
            float local_y = event->mouse.y;

            if (split->dragging) {
                // Calculate new split position
                float pos;
                if (split->direction == VG_SPLIT_HORIZONTAL) {
                    float available = widget->width - split->splitter_size;
                    pos = (local_x - split->drag_start + split->drag_start_split * available) / available;
                } else {
                    float available = widget->height - split->splitter_size;
                    pos = (local_y - split->drag_start + split->drag_start_split * available) / available;
                }

                // Clamp position
                if (pos < 0) pos = 0;
                if (pos > 1) pos = 1;

                split->split_position = pos;
                widget->needs_layout = true;
                widget->needs_paint = true;
                return true;
            }

            // Check if over splitter
            bool was_hovered = split->splitter_hovered;
            if (split->direction == VG_SPLIT_HORIZONTAL) {
                split->splitter_hovered = local_x >= first->width &&
                                          local_x < first->width + split->splitter_size;
            } else {
                split->splitter_hovered = local_y >= first->height &&
                                          local_y < first->height + split->splitter_size;
            }

            if (was_hovered != split->splitter_hovered) {
                widget->needs_paint = true;
            }

            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (split->splitter_hovered) {
                split->splitter_hovered = false;
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_MOUSE_DOWN:
            if (split->splitter_hovered) {
                split->dragging = true;
                split->drag_start_split = split->split_position;
                if (split->direction == VG_SPLIT_HORIZONTAL) {
                    split->drag_start = event->mouse.x;
                } else {
                    split->drag_start = event->mouse.y;
                }
                return true;
            }
            return false;

        case VG_EVENT_MOUSE_UP:
            if (split->dragging) {
                split->dragging = false;
                return true;
            }
            return false;

        default:
            break;
    }

    return false;
}

//=============================================================================
// SplitPane API
//=============================================================================

void vg_splitpane_set_position(vg_splitpane_t* split, float position) {
    if (!split) return;

    if (position < 0) position = 0;
    if (position > 1) position = 1;

    split->split_position = position;
    split->base.needs_layout = true;
    split->base.needs_paint = true;
}

float vg_splitpane_get_position(vg_splitpane_t* split) {
    return split ? split->split_position : 0.5f;
}

void vg_splitpane_set_min_sizes(vg_splitpane_t* split, float min_first, float min_second) {
    if (!split) return;

    split->min_first_size = min_first > 0 ? min_first : 0;
    split->min_second_size = min_second > 0 ? min_second : 0;
    split->base.needs_layout = true;
}

vg_widget_t* vg_splitpane_get_first(vg_splitpane_t* split) {
    return split ? split->base.first_child : NULL;
}

vg_widget_t* vg_splitpane_get_second(vg_splitpane_t* split) {
    vg_widget_t* first = split ? split->base.first_child : NULL;
    return first ? first->next_sibling : NULL;
}
