//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file layout.c
 * @brief Layout manager implementation for automatic widget positioning.
 *
 * This file implements the layout management system that automatically
 * positions child widgets within a container. Layout managers eliminate
 * the need for manual coordinate calculations and make UI design more
 * maintainable and adaptable to different window sizes.
 *
 * ## Available Layout Types
 *
 * - **LAYOUT_NONE**: Manual positioning; widgets keep their explicit coordinates
 * - **LAYOUT_HORIZONTAL**: Arranges children left-to-right in a row
 * - **LAYOUT_VERTICAL**: Arranges children top-to-bottom in a column
 * - **LAYOUT_GRID**: Arranges children in a grid of rows and columns
 * - **LAYOUT_BORDER**: Five-region layout (north, south, east, west, center)
 *
 * ## Common Layout Properties
 *
 * All layouts support:
 * - **Margins**: Empty space between container edges and content
 * - **Spacing**: Gap between adjacent child widgets
 *
 * ## Usage Pattern
 *
 * @code
 * // Create a vertical layout with margins
 * layout_t *layout = layout_create(LAYOUT_VERTICAL);
 * layout_set_margins(layout, 10, 10, 10, 10);
 * layout_set_spacing(layout, 8);
 * widget_set_layout(panel, layout);
 *
 * // Add children - they will be positioned automatically
 * label_create(panel, "Name:");
 * textbox_create(panel);
 * button_create(panel, "Submit");
 *
 * // Apply the layout to calculate positions
 * layout_apply(panel);
 * @endcode
 *
 * ## When Layouts are Applied
 *
 * Layouts are not automatically applied when children are added. You must
 * call layout_apply() explicitly:
 * - After adding/removing children
 * - After changing widget visibility
 * - After the container is resized
 *
 * @see widget.h for layout_t structure and type definitions
 */
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

//===----------------------------------------------------------------------===//
// Layout API
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new layout manager of the specified type.
 *
 * This function allocates and initializes a layout manager. The created
 * layout has default settings that can be customized via the layout_set_*
 * functions before attaching it to a container.
 *
 * Default settings:
 * - **Spacing**: 4 pixels between child widgets
 * - **Margins**: 0 pixels on all sides
 * - **Grid settings**: 0 columns/rows (must be set for LAYOUT_GRID)
 *
 * @param type The layout algorithm to use:
 *             - LAYOUT_NONE: No automatic positioning
 *             - LAYOUT_HORIZONTAL: Left-to-right row arrangement
 *             - LAYOUT_VERTICAL: Top-to-bottom column arrangement
 *             - LAYOUT_GRID: Row/column grid arrangement
 *             - LAYOUT_BORDER: Five-region (N/S/E/W/Center) arrangement
 *
 * @return Pointer to the new layout manager, or NULL if memory allocation
 *         failed. The caller should either attach this to a container via
 *         widget_set_layout() or free it via layout_destroy().
 *
 * @see widget_set_layout() To attach the layout to a container
 * @see layout_destroy() To free an unattached layout
 */
layout_t *layout_create(layout_type_t type) {
    layout_t *layout = (layout_t *)malloc(sizeof(layout_t));
    if (!layout)
        return NULL;

    memset(layout, 0, sizeof(layout_t));
    layout->type = type;
    layout->spacing = 4;

    return layout;
}

/**
 * @brief Destroys a layout manager and frees its memory.
 *
 * This function frees the memory allocated for a layout manager. You should
 * only call this directly for layouts that were never attached to a container.
 * For attached layouts, the container takes ownership and will destroy the
 * layout when needed.
 *
 * @param layout Pointer to the layout to destroy. If NULL, does nothing.
 *
 * @warning Do not call this on a layout that is currently attached to a
 *          container. The container owns the layout and will free it when
 *          the container is destroyed or a new layout is set.
 *
 * @see layout_create() To create a layout
 * @see widget_set_layout() Which transfers ownership to the container
 */
void layout_destroy(layout_t *layout) {
    free(layout);
}

/**
 * @brief Sets the spacing between child widgets in a layout.
 *
 * Spacing controls the gap between adjacent child widgets after they are
 * positioned. The spacing is applied in the primary direction of the layout:
 *
 * - **LAYOUT_HORIZONTAL**: Horizontal gap between widgets in the row
 * - **LAYOUT_VERTICAL**: Vertical gap between widgets in the column
 * - **LAYOUT_GRID**: Both horizontal and vertical gaps between cells
 * - **LAYOUT_BORDER**: Gap between regions
 *
 * @param layout  Pointer to the layout manager. If NULL, does nothing.
 * @param spacing The gap in pixels between adjacent widgets. Default is 4.
 *                A value of 0 makes widgets touch with no gap.
 *
 * @note Changes take effect on the next call to layout_apply().
 *
 * @see layout_set_margins() For the gap between content and container edges
 */
void layout_set_spacing(layout_t *layout, int spacing) {
    if (layout) {
        layout->spacing = spacing;
    }
}

/**
 * @brief Sets the margins (padding) around the content area.
 *
 * Margins define empty space between the container's edges and where child
 * widgets can be placed. This creates padding inside the container that
 * keeps content away from the borders.
 *
 * The content area where children are laid out is:
 * - X range: container.x + left to container.x + width - right
 * - Y range: container.y + top to container.y + height - bottom
 *
 * @param layout Pointer to the layout manager. If NULL, does nothing.
 * @param left   Left margin in pixels.
 * @param top    Top margin in pixels.
 * @param right  Right margin in pixels.
 * @param bottom Bottom margin in pixels.
 *
 * @note Default margins are 0 on all sides. Changes take effect on the
 *       next call to layout_apply().
 *
 * @code
 * // Create a layout with 10px margins on all sides
 * layout_set_margins(layout, 10, 10, 10, 10);
 *
 * // Create a layout with only top/bottom margins
 * layout_set_margins(layout, 0, 8, 0, 8);
 * @endcode
 *
 * @see layout_set_spacing() For gaps between widgets
 */
void layout_set_margins(layout_t *layout, int left, int top, int right, int bottom) {
    if (layout) {
        layout->margin_left = left;
        layout->margin_top = top;
        layout->margin_right = right;
        layout->margin_bottom = bottom;
    }
}

/**
 * @brief Configures the grid dimensions for LAYOUT_GRID.
 *
 * This function sets the number of columns and optionally rows for a grid
 * layout. The grid layout distributes child widgets into a regular grid
 * of cells, filling cells left-to-right, top-to-bottom.
 *
 * @param layout  Pointer to the layout manager. If NULL, does nothing.
 * @param columns The number of columns in the grid. Must be > 0 for the
 *                grid layout to function. Children wrap to the next row
 *                after filling all columns.
 * @param rows    The number of rows in the grid. If > 0, this fixes the
 *                grid height. If 0, rows are calculated automatically
 *                based on the number of children.
 *
 * @note Cell sizes are calculated by dividing the available content area
 *       (after margins and spacing) equally among all cells.
 *
 * @note Children are centered within their grid cells. Their original
 *       size is preserved (they are not stretched to fill cells).
 *
 * @code
 * // Create a 3-column grid (rows calculated automatically)
 * layout_t *grid = layout_create(LAYOUT_GRID);
 * layout_set_grid(grid, 3, 0);
 *
 * // Create a fixed 4x2 grid
 * layout_t *fixed_grid = layout_create(LAYOUT_GRID);
 * layout_set_grid(fixed_grid, 4, 2);
 * @endcode
 *
 * @see layout_apply_grid() For the layout algorithm details
 */
void layout_set_grid(layout_t *layout, int columns, int rows) {
    if (layout) {
        layout->columns = columns;
        layout->rows = rows;
    }
}

/**
 * @brief Attaches a layout manager to a container widget.
 *
 * This function assigns a layout manager to control the positioning of
 * the container's child widgets. The container takes ownership of the
 * layout—do not free the layout separately after calling this.
 *
 * If the container already has a layout attached, the previous layout
 * is destroyed before the new one is assigned.
 *
 * @param container Pointer to the container widget that will use this layout.
 *                  If NULL, does nothing.
 * @param layout    Pointer to the layout manager to attach. Can be NULL to
 *                  remove the current layout (children will then use their
 *                  explicit positions).
 *
 * @note The layout is not automatically applied. Call layout_apply() after
 *       attaching the layout and adding children.
 *
 * @warning After calling this function, the container owns the layout.
 *          Do not call layout_destroy() on the layout or use it with
 *          another container.
 *
 * @see layout_apply() To apply the layout and position children
 */
void widget_set_layout(widget_t *container, layout_t *layout) {
    if (container) {
        // Free existing layout
        if (container->layout) {
            layout_destroy(container->layout);
        }
        container->layout = layout;
    }
}

/**
 * @brief Sets a layout constraint for LAYOUT_BORDER positioning.
 *
 * When using LAYOUT_BORDER, each child widget must specify which region
 * it belongs to using a constraint. The constraint determines where in
 * the container the widget will be placed:
 *
 * - **BORDER_NORTH**: Top strip, full width, uses widget's natural height
 * - **BORDER_SOUTH**: Bottom strip, full width, uses widget's natural height
 * - **BORDER_WEST**: Left strip, uses widget's natural width
 * - **BORDER_EAST**: Right strip, uses widget's natural width
 * - **BORDER_CENTER** (default): Remaining space after other regions
 *
 * ```
 * +------- NORTH -------+
 * | W |             | E |
 * | E |   CENTER    | A |
 * | S |             | S |
 * | T |             | T |
 * +------- SOUTH -------+
 * ```
 *
 * @param w          Pointer to the widget. If NULL, does nothing.
 * @param constraint The border region constant (BORDER_NORTH, BORDER_SOUTH,
 *                   BORDER_EAST, BORDER_WEST, or BORDER_CENTER).
 *
 * @note Only one widget should be assigned to each region. If multiple
 *       widgets have the same constraint, only the last one found is used.
 *
 * @see layout_apply_border() For the border layout algorithm
 */
void widget_set_layout_constraint(widget_t *w, int constraint) {
    if (w) {
        w->layout_constraint = constraint;
    }
}

//===----------------------------------------------------------------------===//
// Layout Application
//===----------------------------------------------------------------------===//

/**
 * @brief Applies the LAYOUT_NONE algorithm (no automatic positioning).
 *
 * This is a no-op function. When using LAYOUT_NONE, child widgets retain
 * their explicitly set positions. This layout type is useful when you want
 * the benefits of having a layout object (margins, spacing values) but
 * need full manual control over positioning.
 *
 * @param container The container widget (unused).
 *
 * @note This function intentionally does nothing. Widgets keep their
 *       current x, y, width, and height values unchanged.
 */
static void layout_apply_none(widget_t *container) {
    // Manual layout - widgets keep their positions
    (void)container;
}

/**
 * @brief Applies the LAYOUT_HORIZONTAL algorithm (left-to-right row).
 *
 * This layout arranges all visible child widgets in a horizontal row,
 * starting from the left margin and progressing rightward. Each widget
 * is vertically centered within the available height.
 *
 * ## Algorithm
 *
 * 1. Start X position at container.x + margin_left
 * 2. Start Y position at container.y + margin_top
 * 3. For each visible child:
 *    a. Set child.x to current X position
 *    b. Center child vertically in available height
 *    c. Advance X by child.width + spacing
 *
 * ## Characteristics
 *
 * - Children retain their original width and height
 * - Children are centered vertically within the content area
 * - No horizontal centering—row starts at left margin
 * - Invisible widgets are skipped entirely (no space reserved)
 *
 * @param container The container widget with LAYOUT_HORIZONTAL.
 *
 * @note This layout does not wrap. If children exceed the container width,
 *       they will extend beyond the right edge.
 */
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

/**
 * @brief Applies the LAYOUT_VERTICAL algorithm (top-to-bottom column).
 *
 * This layout arranges all visible child widgets in a vertical column,
 * starting from the top margin and progressing downward. Each widget
 * is horizontally centered within the available width.
 *
 * ## Algorithm
 *
 * 1. Start X position at container.x + margin_left
 * 2. Start Y position at container.y + margin_top
 * 3. For each visible child:
 *    a. Center child horizontally in available width
 *    b. Set child.y to current Y position
 *    c. Advance Y by child.height + spacing
 *
 * ## Characteristics
 *
 * - Children retain their original width and height
 * - Children are centered horizontally within the content area
 * - No vertical centering—column starts at top margin
 * - Invisible widgets are skipped entirely (no space reserved)
 *
 * @param container The container widget with LAYOUT_VERTICAL.
 *
 * @note This layout does not handle overflow. If children exceed the
 *       container height, they will extend beyond the bottom edge.
 */
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

/**
 * @brief Applies the LAYOUT_GRID algorithm (row/column grid).
 *
 * This layout arranges child widgets in a regular grid of rows and columns.
 * Children fill cells left-to-right, wrapping to the next row when a row
 * is full. Each child is centered within its grid cell.
 *
 * ## Algorithm
 *
 * 1. Calculate cell dimensions:
 *    - cell_width = (content_width - (columns-1)*spacing) / columns
 *    - cell_height = (content_height - (rows-1)*spacing) / rows
 * 2. For each visible child i:
 *    - col = i % columns
 *    - row = i / columns
 *    - Position at (base_x + col*cell_stride, base_y + row*cell_stride)
 *    - Center within cell based on child's actual size
 *
 * ## Characteristics
 *
 * - Children retain their original size (not stretched to fill cells)
 * - Children are centered both horizontally and vertically within cells
 * - All cells have equal dimensions
 * - If rows=0, rows are calculated from child count
 *
 * @param container The container widget with LAYOUT_GRID.
 *
 * @note The layout requires columns > 0 to function. If columns is 0,
 *       this function returns without making changes.
 *
 * @see layout_set_grid() To configure the grid dimensions
 */
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

/**
 * @brief Applies the LAYOUT_BORDER algorithm (five-region layout).
 *
 * This layout divides the container into five regions: North, South, East,
 * West, and Center. Each child widget specifies which region it belongs to
 * via widget_set_layout_constraint(). The algorithm:
 *
 * 1. North gets full width, positioned at top, uses its natural height
 * 2. South gets full width, positioned at bottom, uses its natural height
 * 3. West gets remaining height, positioned at left, uses its natural width
 * 4. East gets remaining height, positioned at right, uses its natural width
 * 5. Center fills all remaining space
 *
 * ## Visual Layout
 *
 * ```
 * +-------------- container.width --------------+
 * |                   NORTH                     |
 * +------+---------------------------+----------+
 * |      |                           |          |
 * | WEST |         CENTER            |   EAST   |
 * |      |                           |          |
 * +------+---------------------------+----------+
 * |                   SOUTH                     |
 * +---------------------------------------------+
 * ```
 *
 * ## Characteristics
 *
 * - North and South are resized to full container width
 * - East and West keep their width but fill remaining height
 * - Center is resized to fill all remaining space
 * - Only one widget per region; extras are ignored
 * - Regions without widgets take no space
 *
 * @param container The container widget with LAYOUT_BORDER.
 *
 * @note Widgets assigned to North/South have their width changed to match
 *       the container width. Widgets in Center have both dimensions changed.
 *
 * @see widget_set_layout_constraint() To assign widgets to regions
 */
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

/**
 * @brief Applies a container's layout to position all child widgets.
 *
 * This function calculates and sets the positions (and sometimes sizes)
 * of all child widgets based on the container's layout manager settings.
 * It dispatches to the appropriate layout algorithm based on the layout type.
 *
 * Call this function:
 * - After adding or removing child widgets
 * - After changing the visibility of child widgets
 * - After resizing the container
 * - After changing layout properties (margins, spacing, grid config)
 *
 * @param container Pointer to the container widget to lay out. If NULL or
 *                  if the container has no layout attached, does nothing.
 *
 * @note This function modifies the x and y fields of child widgets, and
 *       may also modify width and height for some layout types (LAYOUT_BORDER).
 *
 * @note After calling this function, you should trigger a repaint to see
 *       the updated positions.
 *
 * @note Child layouts are NOT automatically applied recursively. If child
 *       widgets are themselves containers with layouts, you must call
 *       layout_apply() on each separately.
 *
 * @code
 * // Typical usage after building a UI
 * widget_set_layout(main_panel, layout);
 * label_create(main_panel, "Username:");
 * textbox_create(main_panel);
 * button_create(main_panel, "Login");
 * layout_apply(main_panel);  // Calculate positions
 * widget_app_repaint(app);   // Show the result
 * @endcode
 *
 * @see layout_create() To create a layout manager
 * @see widget_set_layout() To attach a layout to a container
 */
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
