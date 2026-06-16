//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_gui_ide.h
// Purpose: GUI automation, virtualization, and command/accessibility helpers
//          for IDE-style applications.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "rt_string.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_GUI_TEST_HARNESS_CLASS_ID INT64_C(-0x4755495445535401)
#define RT_GUI_VIRTUAL_LIST_CLASS_ID INT64_C(-0x475549564c495354)
#define RT_GUI_VIRTUAL_TREE_CLASS_ID INT64_C(-0x4755495654524545)
#define RT_GUI_COMMAND_STATE_CLASS_ID INT64_C(-0x475549434d445354)

// --- Headless GUI test harness: register synthetic widgets, drive input, and
//     assert on the result without a real window. ---
/// @brief Create a new headless GUI test harness object.
void *rt_gui_test_harness_new(void);
/// @brief Remove all registered widgets and reset harness state.
void rt_gui_test_harness_clear(void *harness);
/// @brief Advance the harness by @p frames simulated frames (clamped to ≥1).
/// @return The harness's new accumulated frame counter.
int64_t rt_gui_test_harness_tick(void *harness, int64_t frames);
/// @brief Register (or replace by id) a synthetic widget with the given id/type/name
///        and bounds in the harness's widget table.
void rt_gui_test_harness_register_widget(void *harness,
                                         rt_string id,
                                         rt_string type,
                                         rt_string name,
                                         int64_t x,
                                         int64_t y,
                                         int64_t w,
                                         int64_t h);
/// @brief Find a registered widget record by exact id (NULL if none).
void *rt_gui_test_harness_find_by_id(void *harness, rt_string id);
/// @brief Find the first registered widget record with the given name (NULL if none).
void *rt_gui_test_harness_find_by_name(void *harness, rt_string name);
/// @brief Find the first registered widget record of the given type (NULL if none).
void *rt_gui_test_harness_find_by_type(void *harness, rt_string type);
/// @brief Inject a key event (@p key plus a modifier bitmask) into the harness.
void rt_gui_test_harness_send_key(void *harness, rt_string key, int64_t modifiers);
/// @brief Inject a mouse event of @p event_type at (@p x, @p y) for the given button.
void rt_gui_test_harness_send_mouse(
    void *harness, rt_string event_type, int64_t x, int64_t y, int64_t button);
/// @brief Return the number of synthetic input events recorded by the harness.
int64_t rt_gui_test_harness_event_count(void *harness);
/// @brief Return a Map snapshot for the event at @p index, or found=0 if out of range.
void *rt_gui_test_harness_event_at(void *harness, int64_t index);
/// @brief Remove all recorded synthetic input events without changing widgets or focus.
void rt_gui_test_harness_clear_events(void *harness);
/// @brief Return the id of the currently focused widget (empty string if none).
rt_string rt_gui_test_harness_get_focus(void *harness);
/// @brief Return a sequence of widget ids in registration (focus-traversal) order.
void *rt_gui_test_harness_focus_order(void *harness);
/// @brief Capture a synthetic snapshot of a region as a Map with x/y/width/height,
///        `nonBlankPixels` and a `nonBlank` flag (derived from widget coverage, not real pixels).
void *rt_gui_test_harness_capture_region(void *harness, int64_t x, int64_t y, int64_t w, int64_t h);
/// @brief Read the `nonBlank` flag from a capture_region snapshot; 0 if @p snapshot is not one.
int8_t rt_gui_test_harness_assert_nonblank(void *snapshot);

// --- Virtualized list: only the rows visible at the current scroll offset are
//     realized, so arbitrarily large row counts stay cheap. ---
/// @brief Create a virtualized list with @p row_count rows of @p row_height pixels
///        shown through a @p viewport_height window.
void *rt_virtual_list_new(int64_t row_count, int64_t row_height, int64_t viewport_height);
/// @brief Update the total row count (clamped to ≥0).
void rt_virtual_list_set_count(void *list, int64_t row_count);
/// @brief Assign a stable id to the row at index @p row (ignored if out of range).
void rt_virtual_list_set_row_id(void *list, int64_t row, rt_string id);
/// @brief Compute the rows to realize at scroll offset @p scroll_y.
/// @return A Map with `start`, `end` and `count` (overscan rows included).
void *rt_virtual_list_visible_range(void *list, int64_t scroll_y);
/// @brief Select the row carrying the given id.
void rt_virtual_list_select_id(void *list, rt_string id);
/// @brief Return the selected row's id (empty string if none).
rt_string rt_virtual_list_get_selected_id(void *list);
/// @brief Return the selected row's index, or -1 if nothing is selected.
int64_t rt_virtual_list_get_selected_index(void *list);

// --- Virtualized tree: lazily expanded, only visible (expanded) rows realized. ---
/// @brief Create an empty virtualized tree.
void *rt_virtual_tree_new(void);
/// @brief Add a node @p id labelled @p text under @p parent_id (empty parent = root).
void rt_virtual_tree_add_node(void *tree, rt_string parent_id, rt_string id, rt_string text);
/// @brief Mark a node expanded.
/// @return A Map describing the result: `found`, `expanded` and `needsPopulate`
///         (set when the node has no loaded children yet).
void *rt_virtual_tree_expand(void *tree, rt_string id);
/// @brief Collapse a node, hiding its subtree.
void rt_virtual_tree_collapse(void *tree, rt_string id);
/// @brief Select the node carrying the given id.
void rt_virtual_tree_select_id(void *tree, rt_string id);
/// @brief Return the selected node's id (empty string if none).
rt_string rt_virtual_tree_get_selected_id(void *tree);
/// @brief Return the currently visible (expanded) rows as a sequence.
void *rt_virtual_tree_visible_rows(void *tree);
/// @brief Rebuild the visible-row cache for a node's subtree after structural changes.
void rt_virtual_tree_refresh_subtree(void *tree, rt_string id);

// --- Command state: the enabled/checked/accessibility state of a UI command,
//     used to drive menu items, toolbar buttons and the command palette. ---
/// @brief Create a command-state object identified by @p id with display @p label.
void *rt_command_state_new(rt_string id, rt_string label);
/// @brief Set whether the command is enabled (invokable).
void rt_command_state_set_enabled(void *state, int8_t enabled);
/// @brief Return 1 if the command is enabled.
int8_t rt_command_state_get_enabled(void *state);
/// @brief Set the command's checked (toggled-on) state.
void rt_command_state_set_checked(void *state, int8_t checked);
/// @brief Return 1 if the command is checked.
int8_t rt_command_state_get_checked(void *state);
/// @brief Set the accessibility label and description announced for the command.
void rt_command_state_set_accessible(void *state, rt_string label, rt_string description);
/// @brief Snapshot the command state as a Map (id, label, accessibleLabel,
///        accessibleDescription, enabled, checked).
void *rt_command_state_snapshot(void *state);

// --- Accessibility: WCAG contrast math and high-contrast theme tokens. ---
/// @brief Compute the WCAG relative-luminance contrast ratio between two 0xRRGGBB colors.
/// @return A ratio in [1, 21] (lighter:darker, order-independent).
double rt_accessibility_contrast_ratio(int64_t fg_rgb, int64_t bg_rgb);
/// @brief Return 1 if the fg/bg contrast ratio meets @p min_ratio.
/// @details A non-finite or non-positive @p min_ratio defaults to the WCAG AA threshold of 4.5.
int8_t rt_accessibility_meets_contrast(int64_t fg_rgb, int64_t bg_rgb, double min_ratio);
/// @brief Return a Map of high-contrast theme color tokens
///        (background, foreground, accent, warning, error).
void *rt_accessibility_high_contrast_tokens(void);

#ifdef __cplusplus
}
#endif
