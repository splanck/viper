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

static int64_t visible_count(void *tree) {
    return rt_seq_len(rt_virtual_tree_visible_rows(tree));
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
    assert(visible_count(tree) == 2);
    rt_virtual_tree_select_id(tree, rt_const_cstr("main.zia"));
    assert(take(rt_virtual_tree_get_selected_id(tree)) == "main.zia");
    rt_virtual_tree_refresh_subtree(tree, rt_const_cstr("src"));
    assert(take(rt_virtual_tree_get_selected_id(tree)).empty());
    assert(visible_count(tree) == 1);
    expand = rt_virtual_tree_expand(tree, rt_const_cstr("src"));
    assert(rt_map_get_bool(expand, rt_const_cstr("needsPopulate")) == 1);

    void *tree2 = rt_virtual_tree_new();
    rt_virtual_tree_add_node(
        tree2, rt_const_cstr(""), rt_const_cstr("root"), rt_const_cstr("Root"));
    rt_virtual_tree_add_node(tree2, rt_const_cstr("root"), rt_const_cstr("a"), rt_const_cstr("A"));
    rt_virtual_tree_add_node(tree2, rt_const_cstr("root"), rt_const_cstr("b"), rt_const_cstr("B"));
    expand = rt_virtual_tree_expand(tree2, rt_const_cstr("root"));
    assert(rt_map_get_bool(expand, rt_const_cstr("expanded")) == 1);
    assert(visible_count(tree2) == 3);
    rt_virtual_tree_add_node(
        tree2, rt_const_cstr("b"), rt_const_cstr("a"), rt_const_cstr("A moved"));
    assert(visible_count(tree2) == 2);
    expand = rt_virtual_tree_expand(tree2, rt_const_cstr("b"));
    assert(rt_map_get_bool(expand, rt_const_cstr("expanded")) == 1);
    assert(visible_count(tree2) == 3);
    rt_virtual_tree_add_node(
        tree2, rt_const_cstr("a"), rt_const_cstr("root"), rt_const_cstr("Cycle attempt"));
    assert(visible_count(tree2) == 3);

    void *tree3 = rt_virtual_tree_new();
    rt_virtual_tree_add_node(
        tree3, rt_const_cstr("missing"), rt_const_cstr("leaf"), rt_const_cstr("Leaf"));
    assert(visible_count(tree3) == 1);
    expand = rt_virtual_tree_expand(tree3, rt_const_cstr("missing"));
    assert(rt_map_get_bool(expand, rt_const_cstr("expanded")) == 1);
    assert(visible_count(tree3) == 2);

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

    // --- Viper.GUI.Command: logical state, snapshot, and the disabled-never-invoked rule.
    //     (The widget-driven half needs a live windowed app and is covered by ViperIDE probes.)
    void *build = rt_command_new(rt_const_cstr("build"), rt_const_cstr("Build"));
    assert(take(rt_command_get_id(build)) == "build");
    assert(take(rt_command_get_title(build)) == "Build");
    assert(rt_command_is_enabled(build) == 1); // enabled by default
    assert(rt_command_is_checkable(build) == 0);
    assert(rt_command_is_checked(build) == 0);
    assert(rt_command_was_invoked(build) == 0);
    rt_command_set_shortcut(build, rt_const_cstr("Ctrl+B")); // stored even with no app to register with
    assert(take(rt_command_get_shortcut(build)) == "Ctrl+B");
    rt_command_set_checkable(build, 1);
    rt_command_set_checked(build, 1);
    assert(rt_command_is_checkable(build) == 1);
    assert(rt_command_is_checked(build) == 1);
    // No bound widgets and no app: polling never reports the command invoked.
    assert(rt_command_poll(build) == 0);
    assert(rt_command_was_invoked(build) == 0);
    rt_command_set_enabled(build, 0);
    assert(rt_command_is_enabled(build) == 0);
    void *buildSnap = rt_command_snapshot(build);
    assert(rt_map_get_bool(buildSnap, rt_const_cstr("enabled")) == 0);
    assert(rt_map_get_bool(buildSnap, rt_const_cstr("checkable")) == 1);
    assert(rt_map_get_bool(buildSnap, rt_const_cstr("checked")) == 1);
    assert(rt_map_get_bool(buildSnap, rt_const_cstr("invoked")) == 0);

    // --- Viper.GUI.CommandRegistry: ownership, dedup, find, and an idle poll.
    void *registry = rt_command_registry_new();
    assert(rt_command_registry_count(registry) == 0);
    rt_command_registry_add(registry, build);
    assert(rt_command_registry_count(registry) == 1);
    rt_command_registry_add(registry, build); // duplicate add is ignored (no double-retain)
    assert(rt_command_registry_count(registry) == 1);
    void *run = rt_command_new(rt_const_cstr("run"), rt_const_cstr("Run"));
    rt_command_registry_add(registry, run);
    assert(rt_command_registry_count(registry) == 2);
    void *foundRun = rt_command_registry_find(registry, rt_const_cstr("run"));
    assert(foundRun != NULL);
    assert(take(rt_command_get_id(foundRun)) == "run");
    assert(rt_command_registry_find(registry, rt_const_cstr("missing")) == NULL);
    // No palette bound and nothing invoked: poll returns the empty string.
    assert(take(rt_command_registry_poll(registry)).empty());
    rt_command_registry_clear(registry);
    assert(rt_command_registry_count(registry) == 0);
    return 0;
}
