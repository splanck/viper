// vg_layout.c - Layout system implementation
#include "../../include/vg_layout.h"
#include "../../include/vg_widget.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Layout-specific vtables
//=============================================================================

static void vbox_measure(vg_widget_t *self, float available_width, float available_height);
static void vbox_arrange(vg_widget_t *self, float x, float y, float width, float height);
static void hbox_measure(vg_widget_t *self, float available_width, float available_height);
static void hbox_arrange(vg_widget_t *self, float x, float y, float width, float height);
static void flex_measure(vg_widget_t *self, float available_width, float available_height);
static void flex_arrange(vg_widget_t *self, float x, float y, float width, float height);

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

vg_widget_t *vg_vbox_create(float spacing)
{
    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget)
        return NULL;

    widget->vtable = &g_vbox_vtable;

    // Allocate layout data
    vg_vbox_layout_t *layout = calloc(1, sizeof(vg_vbox_layout_t));
    if (!layout)
    {
        vg_widget_destroy(widget);
        return NULL;
    }

    layout->spacing = spacing;
    layout->align = VG_ALIGN_STRETCH;
    layout->justify = VG_JUSTIFY_START;
    widget->impl_data = layout;

    return widget;
}

void vg_vbox_set_spacing(vg_widget_t *vbox, float spacing)
{
    if (!vbox || !vbox->impl_data)
        return;
    vg_vbox_layout_t *layout = (vg_vbox_layout_t *)vbox->impl_data;
    layout->spacing = spacing;
    vbox->needs_layout = true;
}

void vg_vbox_set_align(vg_widget_t *vbox, vg_align_t align)
{
    if (!vbox || !vbox->impl_data)
        return;
    vg_vbox_layout_t *layout = (vg_vbox_layout_t *)vbox->impl_data;
    layout->align = align;
    vbox->needs_layout = true;
}

void vg_vbox_set_justify(vg_widget_t *vbox, vg_justify_t justify)
{
    if (!vbox || !vbox->impl_data)
        return;
    vg_vbox_layout_t *layout = (vg_vbox_layout_t *)vbox->impl_data;
    layout->justify = justify;
    vbox->needs_layout = true;
}

static void vbox_measure(vg_widget_t *self, float available_width, float available_height)
{
    if (!self || !self->impl_data)
        return;

    vg_vbox_layout_t *layout = (vg_vbox_layout_t *)self->impl_data;

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;

    float max_width = 0;
    float total_height = 0;
    int visible_count = 0;

    // First pass: measure children
    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        vg_widget_measure(child, available_width - padding_h, available_height - padding_v);

        float child_width =
            child->measured_width + child->layout.margin_left + child->layout.margin_right;
        float child_height =
            child->measured_height + child->layout.margin_top + child->layout.margin_bottom;

        if (child_width > max_width)
            max_width = child_width;
        total_height += child_height;
        visible_count++;
    }

    // Add spacing between children
    if (visible_count > 1)
    {
        total_height += layout->spacing * (visible_count - 1);
    }

    // Apply constraints
    self->measured_width = max_width + padding_h;
    self->measured_height = total_height + padding_v;

    if (self->constraints.min_width > 0 && self->measured_width < self->constraints.min_width)
    {
        self->measured_width = self->constraints.min_width;
    }
    if (self->constraints.min_height > 0 && self->measured_height < self->constraints.min_height)
    {
        self->measured_height = self->constraints.min_height;
    }
}

static void vbox_arrange(vg_widget_t *self, float x, float y, float width, float height)
{
    if (!self || !self->impl_data)
        return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    vg_vbox_layout_t *layout = (vg_vbox_layout_t *)self->impl_data;

    float content_x = self->layout.padding_left;
    float content_y = self->layout.padding_top;
    float content_width = width - self->layout.padding_left - self->layout.padding_right;
    float content_height = height - self->layout.padding_top - self->layout.padding_bottom;

    // Calculate total fixed height and flex
    float total_fixed = 0;
    float total_flex = 0;
    int visible_count = 0;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        if (child->layout.flex > 0)
        {
            total_flex += child->layout.flex;
        }
        else
        {
            total_fixed +=
                child->measured_height + child->layout.margin_top + child->layout.margin_bottom;
        }
        visible_count++;
    }

    // Add spacing
    float total_spacing = (visible_count > 1) ? layout->spacing * (visible_count - 1) : 0;
    float available = content_height - total_fixed - total_spacing;
    float flex_unit = (total_flex > 0 && available > 0) ? available / total_flex : 0;

    // Arrange children
    float child_y = content_y;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        float child_height;
        if (child->layout.flex > 0)
        {
            child_height = flex_unit * child->layout.flex;
        }
        else
        {
            child_height = child->measured_height;
        }

        // Calculate child X based on alignment
        float child_x;
        float child_width;

        switch (layout->align)
        {
            case VG_ALIGN_START:
                child_x = content_x + child->layout.margin_left;
                child_width = child->measured_width;
                break;
            case VG_ALIGN_CENTER:
                child_x = content_x + (content_width - child->measured_width) / 2;
                child_width = child->measured_width;
                break;
            case VG_ALIGN_END:
                child_x =
                    content_x + content_width - child->measured_width - child->layout.margin_right;
                child_width = child->measured_width;
                break;
            case VG_ALIGN_STRETCH:
            default:
                child_x = content_x + child->layout.margin_left;
                child_width =
                    content_width - child->layout.margin_left - child->layout.margin_right;
                break;
        }

        vg_widget_arrange(
            child, child_x, child_y + child->layout.margin_top, child_width, child_height);
        child_y +=
            child_height + child->layout.margin_top + child->layout.margin_bottom + layout->spacing;
    }
}

//=============================================================================
// HBox Implementation
//=============================================================================

vg_widget_t *vg_hbox_create(float spacing)
{
    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget)
        return NULL;

    widget->vtable = &g_hbox_vtable;

    vg_hbox_layout_t *layout = calloc(1, sizeof(vg_hbox_layout_t));
    if (!layout)
    {
        vg_widget_destroy(widget);
        return NULL;
    }

    layout->spacing = spacing;
    layout->align = VG_ALIGN_STRETCH;
    layout->justify = VG_JUSTIFY_START;
    widget->impl_data = layout;

    return widget;
}

void vg_hbox_set_spacing(vg_widget_t *hbox, float spacing)
{
    if (!hbox || !hbox->impl_data)
        return;
    vg_hbox_layout_t *layout = (vg_hbox_layout_t *)hbox->impl_data;
    layout->spacing = spacing;
    hbox->needs_layout = true;
}

void vg_hbox_set_align(vg_widget_t *hbox, vg_align_t align)
{
    if (!hbox || !hbox->impl_data)
        return;
    vg_hbox_layout_t *layout = (vg_hbox_layout_t *)hbox->impl_data;
    layout->align = align;
    hbox->needs_layout = true;
}

void vg_hbox_set_justify(vg_widget_t *hbox, vg_justify_t justify)
{
    if (!hbox || !hbox->impl_data)
        return;
    vg_hbox_layout_t *layout = (vg_hbox_layout_t *)hbox->impl_data;
    layout->justify = justify;
    hbox->needs_layout = true;
}

static void hbox_measure(vg_widget_t *self, float available_width, float available_height)
{
    if (!self || !self->impl_data)
        return;

    vg_hbox_layout_t *layout = (vg_hbox_layout_t *)self->impl_data;

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;

    float total_width = 0;
    float max_height = 0;
    int visible_count = 0;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        vg_widget_measure(child, available_width - padding_h, available_height - padding_v);

        float child_width =
            child->measured_width + child->layout.margin_left + child->layout.margin_right;
        float child_height =
            child->measured_height + child->layout.margin_top + child->layout.margin_bottom;

        total_width += child_width;
        if (child_height > max_height)
            max_height = child_height;
        visible_count++;
    }

    if (visible_count > 1)
    {
        total_width += layout->spacing * (visible_count - 1);
    }

    self->measured_width = total_width + padding_h;
    self->measured_height = max_height + padding_v;

    if (self->constraints.min_width > 0 && self->measured_width < self->constraints.min_width)
    {
        self->measured_width = self->constraints.min_width;
    }
    if (self->constraints.min_height > 0 && self->measured_height < self->constraints.min_height)
    {
        self->measured_height = self->constraints.min_height;
    }
}

static void hbox_arrange(vg_widget_t *self, float x, float y, float width, float height)
{
    if (!self || !self->impl_data)
        return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    vg_hbox_layout_t *layout = (vg_hbox_layout_t *)self->impl_data;

    float content_x = self->layout.padding_left;
    float content_y = self->layout.padding_top;
    float content_width = width - self->layout.padding_left - self->layout.padding_right;
    float content_height = height - self->layout.padding_top - self->layout.padding_bottom;

    float total_fixed = 0;
    float total_flex = 0;
    int visible_count = 0;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        if (child->layout.flex > 0)
        {
            total_flex += child->layout.flex;
        }
        else
        {
            total_fixed +=
                child->measured_width + child->layout.margin_left + child->layout.margin_right;
        }
        visible_count++;
    }

    float total_spacing = (visible_count > 1) ? layout->spacing * (visible_count - 1) : 0;
    float available = content_width - total_fixed - total_spacing;
    float flex_unit = (total_flex > 0 && available > 0) ? available / total_flex : 0;

    float child_x = content_x;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        float child_width;
        if (child->layout.flex > 0)
        {
            child_width = flex_unit * child->layout.flex;
        }
        else
        {
            child_width = child->measured_width;
        }

        float child_y;
        float child_height;

        switch (layout->align)
        {
            case VG_ALIGN_START:
                child_y = content_y + child->layout.margin_top;
                child_height = child->measured_height;
                break;
            case VG_ALIGN_CENTER:
                child_y = content_y + (content_height - child->measured_height) / 2;
                child_height = child->measured_height;
                break;
            case VG_ALIGN_END:
                child_y = content_y + content_height - child->measured_height -
                          child->layout.margin_bottom;
                child_height = child->measured_height;
                break;
            case VG_ALIGN_STRETCH:
            default:
                child_y = content_y + child->layout.margin_top;
                child_height =
                    content_height - child->layout.margin_top - child->layout.margin_bottom;
                break;
        }

        vg_widget_arrange(
            child, child_x + child->layout.margin_left, child_y, child_width, child_height);
        child_x +=
            child_width + child->layout.margin_left + child->layout.margin_right + layout->spacing;
    }
}

//=============================================================================
// Flex Layout Implementation
//=============================================================================

vg_widget_t *vg_flex_create(void)
{
    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget)
        return NULL;

    widget->vtable = &g_flex_vtable;

    vg_flex_layout_t *layout = calloc(1, sizeof(vg_flex_layout_t));
    if (!layout)
    {
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

void vg_flex_set_direction(vg_widget_t *flex, vg_direction_t direction)
{
    if (!flex || !flex->impl_data)
        return;
    vg_flex_layout_t *layout = (vg_flex_layout_t *)flex->impl_data;
    layout->direction = direction;
    flex->needs_layout = true;
}

void vg_flex_set_align_items(vg_widget_t *flex, vg_align_t align)
{
    if (!flex || !flex->impl_data)
        return;
    vg_flex_layout_t *layout = (vg_flex_layout_t *)flex->impl_data;
    layout->align_items = align;
    flex->needs_layout = true;
}

void vg_flex_set_justify_content(vg_widget_t *flex, vg_justify_t justify)
{
    if (!flex || !flex->impl_data)
        return;
    vg_flex_layout_t *layout = (vg_flex_layout_t *)flex->impl_data;
    layout->justify_content = justify;
    flex->needs_layout = true;
}

void vg_flex_set_gap(vg_widget_t *flex, float gap)
{
    if (!flex || !flex->impl_data)
        return;
    vg_flex_layout_t *layout = (vg_flex_layout_t *)flex->impl_data;
    layout->gap = gap;
    flex->needs_layout = true;
}

void vg_flex_set_wrap(vg_widget_t *flex, bool wrap)
{
    if (!flex || !flex->impl_data)
        return;
    vg_flex_layout_t *layout = (vg_flex_layout_t *)flex->impl_data;
    layout->wrap = wrap;
    flex->needs_layout = true;
}

static void flex_measure(vg_widget_t *self, float available_width, float available_height)
{
    if (!self || !self->impl_data)
        return;

    vg_flex_layout_t *layout = (vg_flex_layout_t *)self->impl_data;
    bool is_row =
        (layout->direction == VG_DIRECTION_ROW || layout->direction == VG_DIRECTION_ROW_REVERSE);

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;

    float main_size = 0;
    float cross_size = 0;
    int visible_count = 0;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        vg_widget_measure(child, available_width - padding_h, available_height - padding_v);

        float child_main =
            is_row
                ? child->measured_width + child->layout.margin_left + child->layout.margin_right
                : child->measured_height + child->layout.margin_top + child->layout.margin_bottom;
        float child_cross =
            is_row ? child->measured_height + child->layout.margin_top + child->layout.margin_bottom
                   : child->measured_width + child->layout.margin_left + child->layout.margin_right;

        main_size += child_main;
        if (child_cross > cross_size)
            cross_size = child_cross;
        visible_count++;
    }

    if (visible_count > 1)
    {
        main_size += layout->gap * (visible_count - 1);
    }

    if (is_row)
    {
        self->measured_width = main_size + padding_h;
        self->measured_height = cross_size + padding_v;
    }
    else
    {
        self->measured_width = cross_size + padding_h;
        self->measured_height = main_size + padding_v;
    }
}

static void flex_arrange(vg_widget_t *self, float x, float y, float width, float height)
{
    if (!self || !self->impl_data)
        return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    vg_flex_layout_t *layout = (vg_flex_layout_t *)self->impl_data;
    bool is_row =
        (layout->direction == VG_DIRECTION_ROW || layout->direction == VG_DIRECTION_ROW_REVERSE);
    bool is_reverse = (layout->direction == VG_DIRECTION_ROW_REVERSE ||
                       layout->direction == VG_DIRECTION_COLUMN_REVERSE);

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

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        float child_main =
            is_row
                ? child->measured_width + child->layout.margin_left + child->layout.margin_right
                : child->measured_height + child->layout.margin_top + child->layout.margin_bottom;

        if (child->layout.flex > 0)
        {
            total_flex += child->layout.flex;
        }
        else
        {
            total_fixed += child_main;
        }
        visible_count++;
    }

    float gap_total = (visible_count > 1) ? layout->gap * (visible_count - 1) : 0;
    float available = main_size - total_fixed - gap_total;
    float flex_unit = (total_flex > 0 && available > 0) ? available / total_flex : 0;

    float main_pos = is_reverse ? main_size : 0;

    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        float child_main_size;
        if (child->layout.flex > 0)
        {
            child_main_size = flex_unit * child->layout.flex;
        }
        else
        {
            child_main_size = is_row ? child->measured_width : child->measured_height;
        }

        float child_cross_size;
        if (layout->align_items == VG_ALIGN_STRETCH)
        {
            child_cross_size = cross_size;
        }
        else
        {
            child_cross_size = is_row ? child->measured_height : child->measured_width;
        }

        float child_x, child_y, child_w, child_h;

        if (is_row)
        {
            child_w = child_main_size;
            child_h = child_cross_size - child->layout.margin_top - child->layout.margin_bottom;

            if (is_reverse)
            {
                main_pos -= child_main_size + child->layout.margin_right;
                child_x = content_x + main_pos;
                main_pos -= child->layout.margin_left + layout->gap;
            }
            else
            {
                child_x = content_x + main_pos + child->layout.margin_left;
                main_pos += child_main_size + child->layout.margin_left +
                            child->layout.margin_right + layout->gap;
            }

            switch (layout->align_items)
            {
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
        }
        else
        {
            child_h = child_main_size;
            child_w = child_cross_size - child->layout.margin_left - child->layout.margin_right;

            if (is_reverse)
            {
                main_pos -= child_main_size + child->layout.margin_bottom;
                child_y = content_y + main_pos;
                main_pos -= child->layout.margin_top + layout->gap;
            }
            else
            {
                child_y = content_y + main_pos + child->layout.margin_top;
                main_pos += child_main_size + child->layout.margin_top +
                            child->layout.margin_bottom + layout->gap;
            }

            switch (layout->align_items)
            {
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
// Grid Layout Implementation
//=============================================================================

/// @brief Per-child grid placement entry stored inside grid_impl_t.
typedef struct grid_placement
{
    vg_widget_t *child;
    vg_grid_item_t item;
} grid_placement_t;

/// @brief Grid container internal state (stored in impl_data).
typedef struct grid_impl
{
    vg_grid_layout_t layout;       ///< column/row counts, gaps, width/height arrays
    grid_placement_t *placements;  ///< per-child placement records
    int placement_count;
    int placement_capacity;
} grid_impl_t;

static void grid_destroy(vg_widget_t *self)
{
    if (!self || !self->impl_data)
        return;
    grid_impl_t *g = (grid_impl_t *)self->impl_data;
    free(g->layout.column_widths);
    free(g->layout.row_heights);
    free(g->placements);
    free(g);
    self->impl_data = NULL;
}

static grid_placement_t *grid_find_placement(grid_impl_t *g, vg_widget_t *child)
{
    for (int i = 0; i < g->placement_count; i++)
    {
        if (g->placements[i].child == child)
            return &g->placements[i];
    }
    return NULL;
}

static void grid_measure(vg_widget_t *self, float available_width, float available_height)
{
    if (!self || !self->impl_data)
        return;

    grid_impl_t *g = (grid_impl_t *)self->impl_data;
    int cols = g->layout.columns > 0 ? g->layout.columns : 1;
    int rows = g->layout.rows > 0 ? g->layout.rows : 1;

    float padding_h = self->layout.padding_left + self->layout.padding_right;
    float padding_v = self->layout.padding_top + self->layout.padding_bottom;
    float content_w = available_width - padding_h;
    float content_h = available_height - padding_v;

    /* Compute column widths */
    float total_col_gap = g->layout.column_gap * (cols - 1);
    float auto_col_w = (content_w - total_col_gap) / (float)cols;
    if (auto_col_w < 0)
        auto_col_w = 0;

    /* Compute row heights */
    float total_row_gap = g->layout.row_gap * (rows - 1);
    float auto_row_h = (content_h - total_row_gap) / (float)rows;
    if (auto_row_h < 0)
        auto_row_h = 0;

    /* Measure each child at its cell size */
    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        grid_placement_t *p = grid_find_placement(g, child);
        int cs = p ? (p->item.col_span > 0 ? p->item.col_span : 1) : 1;
        int rs = p ? (p->item.row_span > 0 ? p->item.row_span : 1) : 1;

        float cell_w =
            auto_col_w * cs + g->layout.column_gap * (cs - 1);
        float cell_h =
            auto_row_h * rs + g->layout.row_gap * (rs - 1);

        vg_widget_measure(child, cell_w, cell_h);
    }

    /* Measured size: full grid including gaps */
    float grid_w = 0;
    float grid_h = 0;

    for (int c = 0; c < cols; c++)
    {
        float w = (g->layout.column_widths && g->layout.column_widths[c] > 0)
                      ? g->layout.column_widths[c]
                      : auto_col_w;
        grid_w += w;
        if (c < cols - 1)
            grid_w += g->layout.column_gap;
    }

    for (int r = 0; r < rows; r++)
    {
        float h = (g->layout.row_heights && g->layout.row_heights[r] > 0)
                      ? g->layout.row_heights[r]
                      : auto_row_h;
        grid_h += h;
        if (r < rows - 1)
            grid_h += g->layout.row_gap;
    }

    self->measured_width = grid_w + padding_h;
    self->measured_height = grid_h + padding_v;

    if (self->constraints.min_width > 0 && self->measured_width < self->constraints.min_width)
        self->measured_width = self->constraints.min_width;
    if (self->constraints.min_height > 0 && self->measured_height < self->constraints.min_height)
        self->measured_height = self->constraints.min_height;
}

static void grid_arrange(vg_widget_t *self, float x, float y, float width, float height)
{
    if (!self || !self->impl_data)
        return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    grid_impl_t *g = (grid_impl_t *)self->impl_data;
    int cols = g->layout.columns > 0 ? g->layout.columns : 1;
    int rows = g->layout.rows > 0 ? g->layout.rows : 1;

    float content_x = self->layout.padding_left;
    float content_y = self->layout.padding_top;
    float content_w = width - self->layout.padding_left - self->layout.padding_right;
    float content_h = height - self->layout.padding_top - self->layout.padding_bottom;

    /* Compute column X positions */
    float total_col_gap = g->layout.column_gap * (cols - 1);
    float auto_col_w = (content_w - total_col_gap) / (float)cols;
    if (auto_col_w < 0)
        auto_col_w = 0;

    float *col_x = calloc(cols, sizeof(float));
    float *col_w = calloc(cols, sizeof(float));
    if (!col_x || !col_w)
    {
        free(col_x);
        free(col_w);
        return;
    }

    float cursor = content_x;
    for (int c = 0; c < cols; c++)
    {
        col_x[c] = cursor;
        col_w[c] = (g->layout.column_widths && g->layout.column_widths[c] > 0)
                       ? g->layout.column_widths[c]
                       : auto_col_w;
        cursor += col_w[c] + g->layout.column_gap;
    }

    /* Compute row Y positions */
    float total_row_gap = g->layout.row_gap * (rows - 1);
    float auto_row_h = (content_h - total_row_gap) / (float)rows;
    if (auto_row_h < 0)
        auto_row_h = 0;

    float *row_y = calloc(rows, sizeof(float));
    float *row_h = calloc(rows, sizeof(float));
    if (!row_y || !row_h)
    {
        free(col_x);
        free(col_w);
        free(row_y);
        free(row_h);
        return;
    }

    cursor = content_y;
    for (int r = 0; r < rows; r++)
    {
        row_y[r] = cursor;
        row_h[r] = (g->layout.row_heights && g->layout.row_heights[r] > 0)
                       ? g->layout.row_heights[r]
                       : auto_row_h;
        cursor += row_h[r] + g->layout.row_gap;
    }

    /* Arrange each child at its cell */
    int auto_idx = 0; /* sequential auto-placement counter */
    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        grid_placement_t *p = grid_find_placement(g, child);

        int col, row, cs, rs;
        if (p)
        {
            col = p->item.column < cols ? p->item.column : 0;
            row = p->item.row < rows ? p->item.row : 0;
            cs = p->item.col_span > 0 ? p->item.col_span : 1;
            rs = p->item.row_span > 0 ? p->item.row_span : 1;
        }
        else
        {
            /* Auto-flow: place sequentially left-to-right, top-to-bottom */
            col = auto_idx % cols;
            row = auto_idx / cols;
            cs = 1;
            rs = 1;
            auto_idx++;
        }

        /* Clamp spans to grid bounds */
        if (col + cs > cols)
            cs = cols - col;
        if (row + rs > rows)
            rs = rows - row;
        if (cs < 1)
            cs = 1;
        if (rs < 1)
            rs = 1;

        /* Compute cell bounds spanning multiple columns/rows */
        float cell_x = col < cols ? col_x[col] : 0;
        float cell_y = row < rows ? row_y[row] : 0;
        float cell_w = 0;
        float cell_h = 0;

        for (int c = col; c < col + cs && c < cols; c++)
            cell_w += col_w[c] + (c < col + cs - 1 ? g->layout.column_gap : 0);
        for (int r = row; r < row + rs && r < rows; r++)
            cell_h += row_h[r] + (r < row + rs - 1 ? g->layout.row_gap : 0);

        vg_widget_arrange(child, cell_x, cell_y, cell_w, cell_h);

        if (!p)
            auto_idx = row * cols + col + cs; /* advance past spanned cells */
    }

    free(col_x);
    free(col_w);
    free(row_y);
    free(row_h);
}

static const vg_widget_vtable_t g_grid_vtable = {
    .destroy = grid_destroy,
    .measure = grid_measure,
    .arrange = grid_arrange,
};

vg_widget_t *vg_grid_create(int columns, int rows)
{
    if (columns < 1)
        columns = 1;
    if (rows < 1)
        rows = 1;

    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget)
        return NULL;

    widget->vtable = &g_grid_vtable;

    grid_impl_t *g = calloc(1, sizeof(grid_impl_t));
    if (!g)
    {
        vg_widget_destroy(widget);
        return NULL;
    }

    g->layout.columns = columns;
    g->layout.rows = rows;
    g->layout.column_gap = 0;
    g->layout.row_gap = 0;
    g->layout.column_widths = NULL;
    g->layout.row_heights = NULL;
    g->placement_capacity = 8;
    g->placement_count = 0;
    g->placements = calloc(g->placement_capacity, sizeof(grid_placement_t));
    if (!g->placements)
    {
        free(g);
        vg_widget_destroy(widget);
        return NULL;
    }

    widget->impl_data = g;
    return widget;
}

void vg_grid_set_columns(vg_widget_t *grid, int columns)
{
    if (!grid || !grid->impl_data || columns < 1)
        return;
    ((grid_impl_t *)grid->impl_data)->layout.columns = columns;
    grid->needs_layout = true;
}

void vg_grid_set_rows(vg_widget_t *grid, int rows)
{
    if (!grid || !grid->impl_data || rows < 1)
        return;
    ((grid_impl_t *)grid->impl_data)->layout.rows = rows;
    grid->needs_layout = true;
}

void vg_grid_set_gap(vg_widget_t *grid, float column_gap, float row_gap)
{
    if (!grid || !grid->impl_data)
        return;
    grid_impl_t *g = (grid_impl_t *)grid->impl_data;
    g->layout.column_gap = column_gap;
    g->layout.row_gap = row_gap;
    grid->needs_layout = true;
}

void vg_grid_set_column_width(vg_widget_t *grid, int column, float width)
{
    if (!grid || !grid->impl_data || column < 0)
        return;
    grid_impl_t *g = (grid_impl_t *)grid->impl_data;
    int cols = g->layout.columns;
    if (column >= cols)
        return;
    if (!g->layout.column_widths)
    {
        g->layout.column_widths = calloc(cols, sizeof(float));
        if (!g->layout.column_widths)
            return;
    }
    g->layout.column_widths[column] = width;
    grid->needs_layout = true;
}

void vg_grid_set_row_height(vg_widget_t *grid, int row, float height)
{
    if (!grid || !grid->impl_data || row < 0)
        return;
    grid_impl_t *g = (grid_impl_t *)grid->impl_data;
    int rows = g->layout.rows;
    if (row >= rows)
        return;
    if (!g->layout.row_heights)
    {
        g->layout.row_heights = calloc(rows, sizeof(float));
        if (!g->layout.row_heights)
            return;
    }
    g->layout.row_heights[row] = height;
    grid->needs_layout = true;
}

void vg_grid_place(
    vg_widget_t *grid, vg_widget_t *child, int column, int row, int col_span, int row_span)
{
    if (!grid || !grid->impl_data || !child)
        return;

    grid_impl_t *g = (grid_impl_t *)grid->impl_data;

    /* Update existing placement if present */
    grid_placement_t *existing = grid_find_placement(g, child);
    if (existing)
    {
        existing->item.column = column;
        existing->item.row = row;
        existing->item.col_span = col_span > 0 ? col_span : 1;
        existing->item.row_span = row_span > 0 ? row_span : 1;
        grid->needs_layout = true;
        return;
    }

    /* Grow placement array if needed */
    if (g->placement_count >= g->placement_capacity)
    {
        int new_cap = g->placement_capacity * 2;
        grid_placement_t *new_p =
            realloc(g->placements, new_cap * sizeof(grid_placement_t));
        if (!new_p)
            return;
        g->placements = new_p;
        g->placement_capacity = new_cap;
    }

    grid_placement_t *p = &g->placements[g->placement_count++];
    p->child = child;
    p->item.column = column;
    p->item.row = row;
    p->item.col_span = col_span > 0 ? col_span : 1;
    p->item.row_span = row_span > 0 ? row_span : 1;
    grid->needs_layout = true;
}

//=============================================================================
// Dock Layout Implementation
//=============================================================================

/// @brief Per-child dock position stored as a tagged entry in dock_impl_t.
typedef struct dock_entry
{
    vg_widget_t *child;
    vg_dock_t position;
} dock_entry_t;

/// @brief Dock container internal state.
typedef struct dock_impl
{
    dock_entry_t *entries;
    int entry_count;
    int entry_capacity;
} dock_impl_t;

static void dock_destroy(vg_widget_t *self)
{
    if (!self || !self->impl_data)
        return;
    dock_impl_t *d = (dock_impl_t *)self->impl_data;
    free(d->entries);
    free(d);
    self->impl_data = NULL;
}

static void dock_measure(vg_widget_t *self, float available_width, float available_height)
{
    if (!self || !self->impl_data)
        return;

    /* Dock measures as available space; children determine actual content */
    self->measured_width = available_width > 0 ? available_width : 100.0f;
    self->measured_height = available_height > 0 ? available_height : 100.0f;
}

static void dock_arrange(vg_widget_t *self, float x, float y, float width, float height)
{
    if (!self || !self->impl_data)
        return;

    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;

    dock_impl_t *d = (dock_impl_t *)self->impl_data;

    /* Remaining area after docked children claim their edges */
    float rem_x = self->layout.padding_left;
    float rem_y = self->layout.padding_top;
    float rem_w = width - self->layout.padding_left - self->layout.padding_right;
    float rem_h = height - self->layout.padding_top - self->layout.padding_bottom;

    /* Process children in order; each docked child takes from the remaining area */
    VG_FOREACH_VISIBLE_CHILD(self, child)
    {
        /* Look up this child's dock position */
        vg_dock_t pos = VG_DOCK_FILL;
        for (int i = 0; i < d->entry_count; i++)
        {
            if (d->entries[i].child == child)
            {
                pos = d->entries[i].position;
                break;
            }
        }

        /* Measure child in the remaining area */
        vg_widget_measure(child, rem_w, rem_h);

        switch (pos)
        {
            case VG_DOCK_LEFT:
            {
                float cw = child->measured_width;
                vg_widget_arrange(child, rem_x, rem_y, cw, rem_h);
                rem_x += cw;
                rem_w -= cw;
                if (rem_w < 0)
                    rem_w = 0;
                break;
            }
            case VG_DOCK_RIGHT:
            {
                float cw = child->measured_width;
                vg_widget_arrange(child, rem_x + rem_w - cw, rem_y, cw, rem_h);
                rem_w -= cw;
                if (rem_w < 0)
                    rem_w = 0;
                break;
            }
            case VG_DOCK_TOP:
            {
                float ch = child->measured_height;
                vg_widget_arrange(child, rem_x, rem_y, rem_w, ch);
                rem_y += ch;
                rem_h -= ch;
                if (rem_h < 0)
                    rem_h = 0;
                break;
            }
            case VG_DOCK_BOTTOM:
            {
                float ch = child->measured_height;
                vg_widget_arrange(child, rem_x, rem_y + rem_h - ch, rem_w, ch);
                rem_h -= ch;
                if (rem_h < 0)
                    rem_h = 0;
                break;
            }
            case VG_DOCK_FILL:
            default:
                vg_widget_arrange(child, rem_x, rem_y, rem_w, rem_h);
                break;
        }
    }
}

static const vg_widget_vtable_t g_dock_vtable = {
    .destroy = dock_destroy,
    .measure = dock_measure,
    .arrange = dock_arrange,
};

vg_widget_t *vg_dock_create(void)
{
    vg_widget_t *widget = vg_widget_create(VG_WIDGET_CONTAINER);
    if (!widget)
        return NULL;

    widget->vtable = &g_dock_vtable;

    dock_impl_t *d = calloc(1, sizeof(dock_impl_t));
    if (!d)
    {
        vg_widget_destroy(widget);
        return NULL;
    }

    d->entry_capacity = 8;
    d->entries = calloc(d->entry_capacity, sizeof(dock_entry_t));
    if (!d->entries)
    {
        free(d);
        vg_widget_destroy(widget);
        return NULL;
    }

    widget->impl_data = d;
    return widget;
}

void vg_dock_add(vg_widget_t *dock, vg_widget_t *child, vg_dock_t position)
{
    if (!dock || !dock->impl_data || !child)
        return;

    dock_impl_t *d = (dock_impl_t *)dock->impl_data;

    /* Update existing entry if child already registered */
    for (int i = 0; i < d->entry_count; i++)
    {
        if (d->entries[i].child == child)
        {
            d->entries[i].position = position;
            dock->needs_layout = true;
            return;
        }
    }

    /* Grow array if needed */
    if (d->entry_count >= d->entry_capacity)
    {
        int new_cap = d->entry_capacity * 2;
        dock_entry_t *new_e = realloc(d->entries, new_cap * sizeof(dock_entry_t));
        if (!new_e)
            return;
        d->entries = new_e;
        d->entry_capacity = new_cap;
    }

    d->entries[d->entry_count].child = child;
    d->entries[d->entry_count].position = position;
    d->entry_count++;

    /* Also add as a widget child so the vtable can iterate it */
    vg_widget_add_child(dock, child);
    dock->needs_layout = true;
}

//=============================================================================
// Layout Engine Entry Points
//=============================================================================

void vg_layout_vbox(vg_widget_t *container, float width, float height)
{
    vbox_arrange(container, container->x, container->y, width, height);
}

void vg_layout_hbox(vg_widget_t *container, float width, float height)
{
    hbox_arrange(container, container->x, container->y, width, height);
}

void vg_layout_flex(vg_widget_t *container, float width, float height)
{
    flex_arrange(container, container->x, container->y, width, height);
}

void vg_layout_grid(vg_widget_t *container, float width, float height)
{
    grid_arrange(container, container->x, container->y, width, height);
}

void vg_layout_dock(vg_widget_t *container, float width, float height)
{
    dock_arrange(container, container->x, container->y, width, height);
}
