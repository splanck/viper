//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/layout.c
// Purpose: Layout manager implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Layout API
//===----------------------------------------------------------------------===//

layout_t *layout_create(layout_type_t type) {
    layout_t *layout = (layout_t *)malloc(sizeof(layout_t));
    if (!layout)
        return NULL;

    memset(layout, 0, sizeof(layout_t));
    layout->type = type;
    layout->spacing = 4;

    return layout;
}

void layout_destroy(layout_t *layout) {
    free(layout);
}

void layout_set_spacing(layout_t *layout, int spacing) {
    if (layout) {
        layout->spacing = spacing;
    }
}

void layout_set_margins(layout_t *layout, int left, int top, int right, int bottom) {
    if (layout) {
        layout->margin_left = left;
        layout->margin_top = top;
        layout->margin_right = right;
        layout->margin_bottom = bottom;
    }
}

void layout_set_grid(layout_t *layout, int columns, int rows) {
    if (layout) {
        layout->columns = columns;
        layout->rows = rows;
    }
}

void widget_set_layout(widget_t *container, layout_t *layout) {
    if (container) {
        // Free existing layout
        if (container->layout) {
            layout_destroy(container->layout);
        }
        container->layout = layout;
    }
}

void widget_set_layout_constraint(widget_t *w, int constraint) {
    if (w) {
        w->layout_constraint = constraint;
    }
}

//===----------------------------------------------------------------------===//
// Layout Application
//===----------------------------------------------------------------------===//

static void layout_apply_none(widget_t *container) {
    // Manual layout - widgets keep their positions
    (void)container;
}

static void layout_apply_horizontal(widget_t *container) {
    layout_t *layout = container->layout;
    if (!layout)
        return;

    int x = container->x + layout->margin_left;
    int y = container->y + layout->margin_top;
    int available_height =
        container->height - layout->margin_top - layout->margin_bottom;

    for (int i = 0; i < container->child_count; i++) {
        widget_t *child = container->children[i];
        if (!child->visible)
            continue;

        child->x = x;
        child->y = y + (available_height - child->height) / 2; // Center vertically

        x += child->width + layout->spacing;
    }
}

static void layout_apply_vertical(widget_t *container) {
    layout_t *layout = container->layout;
    if (!layout)
        return;

    int x = container->x + layout->margin_left;
    int y = container->y + layout->margin_top;
    int available_width =
        container->width - layout->margin_left - layout->margin_right;

    for (int i = 0; i < container->child_count; i++) {
        widget_t *child = container->children[i];
        if (!child->visible)
            continue;

        child->x = x + (available_width - child->width) / 2; // Center horizontally
        child->y = y;

        y += child->height + layout->spacing;
    }
}

static void layout_apply_grid(widget_t *container) {
    layout_t *layout = container->layout;
    if (!layout || layout->columns <= 0)
        return;

    int content_width =
        container->width - layout->margin_left - layout->margin_right;
    int content_height =
        container->height - layout->margin_top - layout->margin_bottom;

    int cell_width = (content_width - (layout->columns - 1) * layout->spacing) / layout->columns;
    int cell_height;

    if (layout->rows > 0) {
        cell_height = (content_height - (layout->rows - 1) * layout->spacing) / layout->rows;
    } else {
        // Calculate rows from child count
        int rows = (container->child_count + layout->columns - 1) / layout->columns;
        if (rows <= 0)
            rows = 1;
        cell_height = (content_height - (rows - 1) * layout->spacing) / rows;
    }

    int base_x = container->x + layout->margin_left;
    int base_y = container->y + layout->margin_top;

    for (int i = 0; i < container->child_count; i++) {
        widget_t *child = container->children[i];
        if (!child->visible)
            continue;

        int col = i % layout->columns;
        int row = i / layout->columns;

        child->x = base_x + col * (cell_width + layout->spacing);
        child->y = base_y + row * (cell_height + layout->spacing);

        // Center within cell
        child->x += (cell_width - child->width) / 2;
        child->y += (cell_height - child->height) / 2;
    }
}

static void layout_apply_border(widget_t *container) {
    layout_t *layout = container->layout;
    if (!layout)
        return;

    int content_x = container->x + layout->margin_left;
    int content_y = container->y + layout->margin_top;
    int content_width = container->width - layout->margin_left - layout->margin_right;
    int content_height = container->height - layout->margin_top - layout->margin_bottom;

    // Find widgets for each region
    widget_t *north = NULL, *south = NULL, *east = NULL, *west = NULL, *center = NULL;

    for (int i = 0; i < container->child_count; i++) {
        widget_t *child = container->children[i];
        if (!child->visible)
            continue;

        switch (child->layout_constraint) {
        case BORDER_NORTH:
            north = child;
            break;
        case BORDER_SOUTH:
            south = child;
            break;
        case BORDER_EAST:
            east = child;
            break;
        case BORDER_WEST:
            west = child;
            break;
        case BORDER_CENTER:
        default:
            center = child;
            break;
        }
    }

    // Calculate remaining space
    int north_height = north ? north->height : 0;
    int south_height = south ? south->height : 0;
    int west_width = west ? west->width : 0;
    int east_width = east ? east->width : 0;

    int center_y = content_y + north_height + (north ? layout->spacing : 0);
    int center_height = content_height - north_height - south_height -
                        (north ? layout->spacing : 0) - (south ? layout->spacing : 0);

    int center_x = content_x + west_width + (west ? layout->spacing : 0);
    int center_width = content_width - west_width - east_width -
                       (west ? layout->spacing : 0) - (east ? layout->spacing : 0);

    // Position widgets
    if (north) {
        north->x = content_x;
        north->y = content_y;
        north->width = content_width;
    }

    if (south) {
        south->x = content_x;
        south->y = content_y + content_height - south_height;
        south->width = content_width;
    }

    if (west) {
        west->x = content_x;
        west->y = center_y;
        west->height = center_height;
    }

    if (east) {
        east->x = content_x + content_width - east_width;
        east->y = center_y;
        east->height = center_height;
    }

    if (center) {
        center->x = center_x;
        center->y = center_y;
        center->width = center_width;
        center->height = center_height;
    }
}

void layout_apply(widget_t *container) {
    if (!container || !container->layout)
        return;

    switch (container->layout->type) {
    case LAYOUT_NONE:
        layout_apply_none(container);
        break;
    case LAYOUT_HORIZONTAL:
        layout_apply_horizontal(container);
        break;
    case LAYOUT_VERTICAL:
        layout_apply_vertical(container);
        break;
    case LAYOUT_GRID:
        layout_apply_grid(container);
        break;
    case LAYOUT_BORDER:
        layout_apply_border(container);
        break;
    }
}
