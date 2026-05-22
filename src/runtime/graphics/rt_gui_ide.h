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

void *rt_gui_test_harness_new(void);
void rt_gui_test_harness_clear(void *harness);
int64_t rt_gui_test_harness_tick(void *harness, int64_t frames);
void rt_gui_test_harness_register_widget(void *harness,
                                         rt_string id,
                                         rt_string type,
                                         rt_string name,
                                         int64_t x,
                                         int64_t y,
                                         int64_t w,
                                         int64_t h);
void *rt_gui_test_harness_find_by_id(void *harness, rt_string id);
void *rt_gui_test_harness_find_by_name(void *harness, rt_string name);
void *rt_gui_test_harness_find_by_type(void *harness, rt_string type);
void rt_gui_test_harness_send_key(void *harness, rt_string key, int64_t modifiers);
void rt_gui_test_harness_send_mouse(void *harness,
                                    rt_string event_type,
                                    int64_t x,
                                    int64_t y,
                                    int64_t button);
rt_string rt_gui_test_harness_get_focus(void *harness);
void *rt_gui_test_harness_focus_order(void *harness);
void *rt_gui_test_harness_capture_region(void *harness, int64_t x, int64_t y, int64_t w, int64_t h);
int8_t rt_gui_test_harness_assert_nonblank(void *snapshot);

void *rt_virtual_list_new(int64_t row_count, int64_t row_height, int64_t viewport_height);
void rt_virtual_list_set_count(void *list, int64_t row_count);
void rt_virtual_list_set_row_id(void *list, int64_t row, rt_string id);
void *rt_virtual_list_visible_range(void *list, int64_t scroll_y);
void rt_virtual_list_select_id(void *list, rt_string id);
rt_string rt_virtual_list_get_selected_id(void *list);
int64_t rt_virtual_list_get_selected_index(void *list);

void *rt_virtual_tree_new(void);
void rt_virtual_tree_add_node(void *tree, rt_string parent_id, rt_string id, rt_string text);
void *rt_virtual_tree_expand(void *tree, rt_string id);
void rt_virtual_tree_collapse(void *tree, rt_string id);
void rt_virtual_tree_select_id(void *tree, rt_string id);
rt_string rt_virtual_tree_get_selected_id(void *tree);
void *rt_virtual_tree_visible_rows(void *tree);
void rt_virtual_tree_refresh_subtree(void *tree, rt_string id);

void *rt_command_state_new(rt_string id, rt_string label);
void rt_command_state_set_enabled(void *state, int8_t enabled);
int8_t rt_command_state_get_enabled(void *state);
void rt_command_state_set_checked(void *state, int8_t checked);
int8_t rt_command_state_get_checked(void *state);
void rt_command_state_set_accessible(void *state, rt_string label, rt_string description);
void *rt_command_state_snapshot(void *state);

double rt_accessibility_contrast_ratio(int64_t fg_rgb, int64_t bg_rgb);
int8_t rt_accessibility_meets_contrast(int64_t fg_rgb, int64_t bg_rgb, double min_ratio);
void *rt_accessibility_high_contrast_tokens(void);

#ifdef __cplusplus
}
#endif
