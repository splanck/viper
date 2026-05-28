//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/tests/runtime/RTGuiIdeTests.cpp
// Purpose: Tests for GUI IDE automation, virtualization, and accessibility helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_gui_ide.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <string>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static std::string take(rt_string value) {
    std::string out(rt_string_cstr(value), (size_t)rt_str_len(value));
    rt_string_unref(value);
    return out;
}

int main() {
    void *h = rt_gui_test_harness_new();
    rt_gui_test_harness_register_widget(h,
                                        rt_const_cstr("editor"),
                                        rt_const_cstr("CodeEditor"),
                                        rt_const_cstr("Editor"),
                                        10,
                                        10,
                                        300,
                                        200);
    rt_gui_test_harness_register_widget(h,
                                        rt_const_cstr("results"),
                                        rt_const_cstr("List"),
                                        rt_const_cstr("Search Results"),
                                        10,
                                        220,
                                        300,
                                        120);
    void *found = rt_gui_test_harness_find_by_id(h, rt_const_cstr("editor"));
    assert(rt_map_get_bool(found, rt_const_cstr("found")) == 1);
    rt_gui_test_harness_send_mouse(h, rt_const_cstr("down"), 20, 20, 1);
    assert(take(rt_gui_test_harness_get_focus(h)) == "editor");
    assert(rt_gui_test_harness_tick(h, 3) == 3);
    void *snapshot = rt_gui_test_harness_capture_region(h, 0, 0, 64, 64);
    assert(rt_gui_test_harness_assert_nonblank(snapshot) == 1);
    assert(rt_seq_len(rt_gui_test_harness_focus_order(h)) == 2);

    void *list = rt_virtual_list_new(10000, 20, 100);
    rt_virtual_list_set_row_id(list, 4242, rt_const_cstr("file://target.zia"));
    void *range = rt_virtual_list_visible_range(list, 40000);
    assert(rt_map_get_int(range, rt_const_cstr("count")) <= 9);
    rt_virtual_list_select_id(list, rt_const_cstr("file://target.zia"));
    assert(rt_virtual_list_get_selected_index(list) == 4242);

    void *tree = rt_virtual_tree_new();
    rt_virtual_tree_add_node(tree, rt_const_cstr(""), rt_const_cstr("src"), rt_const_cstr("src"));
    rt_virtual_tree_add_node(
        tree, rt_const_cstr("src"), rt_const_cstr("main.zia"), rt_const_cstr("main.zia"));
    void *expand = rt_virtual_tree_expand(tree, rt_const_cstr("src"));
    assert(rt_map_get_bool(expand, rt_const_cstr("expanded")) == 1);
    assert(rt_seq_len(rt_virtual_tree_visible_rows(tree)) == 2);
    rt_virtual_tree_refresh_subtree(tree, rt_const_cstr("src"));
    expand = rt_virtual_tree_expand(tree, rt_const_cstr("src"));
    assert(rt_map_get_bool(expand, rt_const_cstr("needsPopulate")) == 1);

    void *command = rt_command_state_new(rt_const_cstr("build"), rt_const_cstr("Build"));
    rt_command_state_set_enabled(command, 0);
    rt_command_state_set_checked(command, 1);
    rt_command_state_set_accessible(
        command, rt_const_cstr("Build project"), rt_const_cstr("Compile active project"));
    void *cmd = rt_command_state_snapshot(command);
    assert(rt_map_get_bool(cmd, rt_const_cstr("enabled")) == 0);
    assert(rt_map_get_bool(cmd, rt_const_cstr("checked")) == 1);
    assert(rt_accessibility_contrast_ratio(0xffffff, 0x000000) > 20.0);
    assert(rt_accessibility_meets_contrast(0x777777, 0xffffff, 4.5) == 0);
    assert(rt_map_get_int(rt_accessibility_high_contrast_tokens(), rt_const_cstr("foreground")) ==
           0xffffff);
    return 0;
}
