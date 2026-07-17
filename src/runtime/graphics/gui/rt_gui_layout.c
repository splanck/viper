//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_layout.c
// Purpose: Runtime bindings for complete box, Flex, LayoutGrid, and DockPanel
//          layout configuration.
// Key invariants:
//   - Public distances are logical units and are converted exactly once using
//     the owning app's effective scale.
//   - Public enum ordinals are validated and explicitly mapped to toolkit
//     enums; private enum ordering is never part of the runtime ABI.
//   - LayoutGrid placement and DockPanel ownership changes report status and
//     preserve prior state when validation or allocation fails.
// Ownership/Lifetime:
//   - Layout containers are ordinary widgets owned by their widget tree.
//   - LayoutGrid.Place borrows an existing direct child; DockChild attaches a
//     detached child or updates an existing direct child.
// Links: src/runtime/graphics/gui/rt_gui.h,
//        src/runtime/graphics/gui/rt_gui_internal.h,
//        src/lib/gui/include/vg_layout.h
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#include <stdint.h>

#ifdef ZANNA_ENABLE_GRAPHICS

/// @brief Resolve a live widget only when it owns the requested layout algorithm.
/// @param handle Opaque runtime widget handle.
/// @param expected Required concrete layout kind.
/// @return Live layout widget, or NULL for an invalid/wrong-type handle.
static vg_widget_t *rt_layout_checked(void *handle, vg_layout_type_t expected) {
    vg_widget_t *widget = rt_gui_widget_handle_checked(handle);
    return widget && vg_layout_get_type(widget) == expected ? widget : NULL;
}

/// @brief Normalize a public alignment ordinal to the supported box/Flex domain.
/// @param value Runtime integer supplied by the caller.
/// @return Start, Center, End, or Stretch; invalid values become Start.
static vg_align_t rt_layout_align_from_public(int64_t value) {
    return value >= 0 && value <= 3 ? (vg_align_t)value : VG_ALIGN_START;
}

/// @brief Normalize a public justification ordinal to the toolkit domain.
/// @param value Runtime integer supplied by the caller.
/// @return One of six justification values; invalid values become Start.
static vg_justify_t rt_layout_justify_from_public(int64_t value) {
    return value >= 0 && value <= 5 ? (vg_justify_t)value : VG_JUSTIFY_START;
}

/// @brief Map stable public FlexDirection ordinals to the toolkit's historical enum ordering.
/// @param value Public direction (`Row`, `Column`, `RowReverse`, `ColumnReverse`).
/// @return Corresponding toolkit direction; invalid values become Row.
static vg_direction_t rt_layout_direction_from_public(int64_t value) {
    switch (value) {
        case 1:
            return VG_DIRECTION_COLUMN;
        case 2:
            return VG_DIRECTION_ROW_REVERSE;
        case 3:
            return VG_DIRECTION_COLUMN_REVERSE;
        case 0:
        default:
            return VG_DIRECTION_ROW;
    }
}

/// @brief Convert a public grid track definition without scaling fractional weights.
/// @details Positive fixed sizes are converted from logical to physical units,
///          zero remains auto/content, and negative magnitudes remain dimensionless.
/// @param grid LayoutGrid used to determine effective scale.
/// @param value Public track definition.
/// @param[out] result Converted toolkit definition on success.
/// @return 1 for a finite input, otherwise 0 with @p result untouched.
static int rt_layout_track_definition(vg_widget_t *grid, double value, float *result) {
    if (!result || !rt_gui_double_is_finite(value))
        return 0;
    if (value > 0.0) {
        *result = rt_gui_logical_length_to_physical(grid, value);
    } else if (value < 0.0) {
        double weight = rt_gui_clamp_f64(-value, 0.0, RT_GUI_MAX_LAYOUT_VALUE);
        *result = -(float)weight;
    } else {
        *result = 0.0f;
    }
    return 1;
}

/// @brief Set the VBox cross-axis alignment.
void rt_vbox_set_align(void *vbox, int64_t align) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(vbox, VG_LAYOUT_VBOX);
    if (widget)
        vg_vbox_set_align(widget, rt_layout_align_from_public(align));
}

/// @brief Read the VBox cross-axis alignment.
int64_t rt_vbox_get_align(void *vbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(vbox, VG_LAYOUT_VBOX);
    return widget ? (int64_t)vg_vbox_get_align(widget) : 0;
}

/// @brief Set the VBox main-axis justification.
void rt_vbox_set_justify(void *vbox, int64_t justify) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(vbox, VG_LAYOUT_VBOX);
    if (widget)
        vg_vbox_set_justify(widget, rt_layout_justify_from_public(justify));
}

/// @brief Read the VBox main-axis justification.
int64_t rt_vbox_get_justify(void *vbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(vbox, VG_LAYOUT_VBOX);
    return widget ? (int64_t)vg_vbox_get_justify(widget) : 0;
}

/// @brief Set the HBox cross-axis alignment.
void rt_hbox_set_align(void *hbox, int64_t align) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(hbox, VG_LAYOUT_HBOX);
    if (widget)
        vg_hbox_set_align(widget, rt_layout_align_from_public(align));
}

/// @brief Read the HBox cross-axis alignment.
int64_t rt_hbox_get_align(void *hbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(hbox, VG_LAYOUT_HBOX);
    return widget ? (int64_t)vg_hbox_get_align(widget) : 0;
}

/// @brief Set the HBox main-axis justification.
void rt_hbox_set_justify(void *hbox, int64_t justify) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(hbox, VG_LAYOUT_HBOX);
    if (widget)
        vg_hbox_set_justify(widget, rt_layout_justify_from_public(justify));
}

/// @brief Read the HBox main-axis justification.
int64_t rt_hbox_get_justify(void *hbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(hbox, VG_LAYOUT_HBOX);
    return widget ? (int64_t)vg_hbox_get_justify(widget) : 0;
}

/// @brief Create a detached Flex layout container.
void *rt_flex_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_flex_create();
}

/// @brief Set Flex direction after stable public-to-private enum mapping.
void rt_flex_set_direction(void *flex, int64_t direction) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(flex, VG_LAYOUT_FLEX);
    if (widget)
        vg_flex_set_direction(widget, rt_layout_direction_from_public(direction));
}

/// @brief Set Flex no-wrap, wrap, or wrap-reverse behavior.
void rt_flex_set_wrap(void *flex, int64_t wrap) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(flex, VG_LAYOUT_FLEX);
    if (!widget)
        return;
    vg_flex_wrap_t mode = wrap >= 0 && wrap <= 2 ? (vg_flex_wrap_t)wrap : VG_FLEX_NO_WRAP;
    vg_flex_set_wrap_mode(widget, mode);
}

/// @brief Set Flex cross-axis item alignment.
void rt_flex_set_align(void *flex, int64_t align) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(flex, VG_LAYOUT_FLEX);
    if (widget)
        vg_flex_set_align_items(widget, rt_layout_align_from_public(align));
}

/// @brief Set Flex main-axis item justification.
void rt_flex_set_justify(void *flex, int64_t justify) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(flex, VG_LAYOUT_FLEX);
    if (widget)
        vg_flex_set_justify_content(widget, rt_layout_justify_from_public(justify));
}

/// @brief Set a Flex gap expressed in public logical units.
void rt_flex_set_gap(void *flex, double gap) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(flex, VG_LAYOUT_FLEX);
    if (widget)
        vg_flex_set_gap(widget, rt_gui_logical_length_to_physical(widget, gap));
}

/// @brief Set equal Flex padding expressed in public logical units.
void rt_flex_set_padding(void *flex, double padding) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(flex, VG_LAYOUT_FLEX);
    if (widget)
        vg_widget_set_padding(widget, rt_gui_logical_length_to_physical(widget, padding));
}

/// @brief Create a detached one-row, one-column LayoutGrid.
void *rt_layoutgrid_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_grid_create(1, 1);
}

/// @brief Set the declared LayoutGrid row count within the supported limit.
void rt_layoutgrid_set_rows(void *grid, int64_t count) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(grid, VG_LAYOUT_GRID);
    if (widget)
        vg_grid_set_rows(widget, rt_gui_clamp_i64_to_i32(count, 1, 4096));
}

/// @brief Set the declared LayoutGrid column count within the supported limit.
void rt_layoutgrid_set_columns(void *grid, int64_t count) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(grid, VG_LAYOUT_GRID);
    if (widget)
        vg_grid_set_columns(widget, rt_gui_clamp_i64_to_i32(count, 1, 4096));
}

/// @brief Set one LayoutGrid row's fixed, auto, or fractional definition.
void rt_layoutgrid_set_row_size(void *grid, int64_t row, double size) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(grid, VG_LAYOUT_GRID);
    if (!widget || row < 0 || row > INT32_MAX)
        return;
    float definition = 0.0f;
    if (rt_layout_track_definition(widget, size, &definition))
        vg_grid_set_row_height(widget, (int)row, definition);
}

/// @brief Set one LayoutGrid column's fixed, auto, or fractional definition.
void rt_layoutgrid_set_column_size(void *grid, int64_t column, double size) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(grid, VG_LAYOUT_GRID);
    if (!widget || column < 0 || column > INT32_MAX)
        return;
    float definition = 0.0f;
    if (rt_layout_track_definition(widget, size, &definition))
        vg_grid_set_column_width(widget, (int)column, definition);
}

/// @brief Set LayoutGrid horizontal and vertical gaps in logical units.
void rt_layoutgrid_set_gap(void *grid, double horizontal, double vertical) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(grid, VG_LAYOUT_GRID);
    if (!widget)
        return;
    vg_grid_set_gap(widget,
                    rt_gui_logical_length_to_physical(widget, horizontal),
                    rt_gui_logical_length_to_physical(widget, vertical));
}

/// @brief Set equal LayoutGrid padding in logical units.
void rt_layoutgrid_set_padding(void *grid, double padding) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(grid, VG_LAYOUT_GRID);
    if (widget)
        vg_widget_set_padding(widget, rt_gui_logical_length_to_physical(widget, padding));
}

/// @brief Atomically place one existing direct child in declared LayoutGrid tracks.
int64_t rt_layoutgrid_place(
    void *grid, void *child, int64_t row, int64_t column, int64_t row_span, int64_t column_span) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *layout = rt_layout_checked(grid, VG_LAYOUT_GRID);
    vg_widget_t *child_widget = rt_gui_widget_handle_checked(child);
    if (!layout || !child_widget || row < 0 || column < 0 || row_span < 1 || column_span < 1 ||
        row > INT32_MAX || column > INT32_MAX || row_span > INT32_MAX || column_span > INT32_MAX) {
        return 0;
    }
    return vg_grid_place_checked(
               layout, child_widget, (int)column, (int)row, (int)column_span, (int)row_span)
               ? 1
               : 0;
}

/// @brief Create a detached DockPanel layout container.
void *rt_dockpanel_new(void) {
    RT_ASSERT_MAIN_THREAD();
    return vg_dock_create();
}

/// @brief Set equal DockPanel padding in logical units.
void rt_dockpanel_set_padding(void *dock, double padding) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(dock, VG_LAYOUT_DOCK);
    if (widget)
        vg_widget_set_padding(widget, rt_gui_logical_length_to_physical(widget, padding));
}

/// @brief Set DockPanel gap in logical units.
void rt_dockpanel_set_gap(void *dock, double gap) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *widget = rt_layout_checked(dock, VG_LAYOUT_DOCK);
    if (widget)
        vg_dock_set_gap(widget, rt_gui_logical_length_to_physical(widget, gap));
}

/// @brief Attach or update one DockPanel child after ownership and enum validation.
int64_t rt_dockpanel_dock_child(void *dock, void *child, int64_t position) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *layout = rt_layout_checked(dock, VG_LAYOUT_DOCK);
    vg_widget_t *child_widget = rt_gui_widget_handle_checked(child);
    if (!layout || !child_widget || position < 0 || position > 4)
        return 0;
    return vg_dock_add_checked(layout, child_widget, (vg_dock_t)(position + 1)) ? 1 : 0;
}

#else /* !ZANNA_ENABLE_GRAPHICS */

/// @brief Stub: ignore VBox alignment when graphics is disabled.
void rt_vbox_set_align(void *vbox, int64_t align) {
    (void)vbox;
    (void)align;
}

/// @brief Stub: return Start alignment when graphics is disabled.
int64_t rt_vbox_get_align(void *vbox) {
    (void)vbox;
    return 0;
}

/// @brief Stub: ignore VBox justification when graphics is disabled.
void rt_vbox_set_justify(void *vbox, int64_t justify) {
    (void)vbox;
    (void)justify;
}

/// @brief Stub: return Start justification when graphics is disabled.
int64_t rt_vbox_get_justify(void *vbox) {
    (void)vbox;
    return 0;
}

/// @brief Stub: ignore HBox alignment when graphics is disabled.
void rt_hbox_set_align(void *hbox, int64_t align) {
    (void)hbox;
    (void)align;
}

/// @brief Stub: return Start alignment when graphics is disabled.
int64_t rt_hbox_get_align(void *hbox) {
    (void)hbox;
    return 0;
}

/// @brief Stub: ignore HBox justification when graphics is disabled.
void rt_hbox_set_justify(void *hbox, int64_t justify) {
    (void)hbox;
    (void)justify;
}

/// @brief Stub: return Start justification when graphics is disabled.
int64_t rt_hbox_get_justify(void *hbox) {
    (void)hbox;
    return 0;
}

/// @brief Stub: no Flex widget is created when graphics is disabled.
void *rt_flex_new(void) {
    return NULL;
}

/// @brief Stub: ignore Flex direction when graphics is disabled.
void rt_flex_set_direction(void *flex, int64_t direction) {
    (void)flex;
    (void)direction;
}

/// @brief Stub: ignore Flex wrapping when graphics is disabled.
void rt_flex_set_wrap(void *flex, int64_t wrap) {
    (void)flex;
    (void)wrap;
}

/// @brief Stub: ignore Flex alignment when graphics is disabled.
void rt_flex_set_align(void *flex, int64_t align) {
    (void)flex;
    (void)align;
}

/// @brief Stub: ignore Flex justification when graphics is disabled.
void rt_flex_set_justify(void *flex, int64_t justify) {
    (void)flex;
    (void)justify;
}

/// @brief Stub: ignore Flex gap when graphics is disabled.
void rt_flex_set_gap(void *flex, double gap) {
    (void)flex;
    (void)gap;
}

/// @brief Stub: ignore Flex padding when graphics is disabled.
void rt_flex_set_padding(void *flex, double padding) {
    (void)flex;
    (void)padding;
}

/// @brief Stub: no LayoutGrid widget is created when graphics is disabled.
void *rt_layoutgrid_new(void) {
    return NULL;
}

/// @brief Stub: ignore LayoutGrid row count when graphics is disabled.
void rt_layoutgrid_set_rows(void *grid, int64_t count) {
    (void)grid;
    (void)count;
}

/// @brief Stub: ignore LayoutGrid column count when graphics is disabled.
void rt_layoutgrid_set_columns(void *grid, int64_t count) {
    (void)grid;
    (void)count;
}

/// @brief Stub: ignore LayoutGrid row size when graphics is disabled.
void rt_layoutgrid_set_row_size(void *grid, int64_t row, double size) {
    (void)grid;
    (void)row;
    (void)size;
}

/// @brief Stub: ignore LayoutGrid column size when graphics is disabled.
void rt_layoutgrid_set_column_size(void *grid, int64_t column, double size) {
    (void)grid;
    (void)column;
    (void)size;
}

/// @brief Stub: ignore LayoutGrid gaps when graphics is disabled.
void rt_layoutgrid_set_gap(void *grid, double horizontal, double vertical) {
    (void)grid;
    (void)horizontal;
    (void)vertical;
}

/// @brief Stub: ignore LayoutGrid padding when graphics is disabled.
void rt_layoutgrid_set_padding(void *grid, double padding) {
    (void)grid;
    (void)padding;
}

/// @brief Stub: reject LayoutGrid placement when graphics is disabled.
int64_t rt_layoutgrid_place(
    void *grid, void *child, int64_t row, int64_t column, int64_t row_span, int64_t column_span) {
    (void)grid;
    (void)child;
    (void)row;
    (void)column;
    (void)row_span;
    (void)column_span;
    return 0;
}

/// @brief Stub: no DockPanel widget is created when graphics is disabled.
void *rt_dockpanel_new(void) {
    return NULL;
}

/// @brief Stub: ignore DockPanel padding when graphics is disabled.
void rt_dockpanel_set_padding(void *dock, double padding) {
    (void)dock;
    (void)padding;
}

/// @brief Stub: ignore DockPanel gap when graphics is disabled.
void rt_dockpanel_set_gap(void *dock, double gap) {
    (void)dock;
    (void)gap;
}

/// @brief Stub: reject DockPanel assignments when graphics is disabled.
int64_t rt_dockpanel_dock_child(void *dock, void *child, int64_t position) {
    (void)dock;
    (void)child;
    (void)position;
    return 0;
}

#endif /* ZANNA_ENABLE_GRAPHICS */
