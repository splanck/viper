//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/tests/test_vg_virtual_models.c
// Purpose: Unit tests for viewport-backed ListBox and TreeView model bindings.
// Key invariants:
//   - Bindings notify their external model exactly once when replaced, cleared,
//     or destroyed.
//   - Visible-range queries are O(1), bounded by the viewport, and independent
//     of the logical 100k-row model size.
//   - Virtual pointer/keyboard input reports semantic row actions without
//     constructing retained ListBox items or TreeView nodes.
// Ownership/Lifetime:
//   - Each test owns its unparented widget and destroys it with
//     vg_widget_destroy; provider strings remain callback-borrowed.
// Links: lib/gui/include/vg_widgets.h,
//        lib/gui/include/vg_ide_widgets_tree.h,
//        lib/gui/src/widgets/vg_listbox.c,
//        lib/gui/src/widgets/vg_treeview.c
//
//===----------------------------------------------------------------------===//

#include "vg_event.h"
#include "vg_ide_widgets_tree.h"
#include "vg_widgets.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct list_model_probe {
    size_t provider_calls;
    size_t last_index;
    int unbind_calls;
    char text[32];
} list_model_probe_t;

/// @brief Supply deterministic borrowed text for one virtual ListBox row.
/// @param listbox Requesting ListBox widget; unused by this probe.
/// @param index Logical row index requested by the lower control.
/// @param text Receives a pointer to probe-owned NUL-terminated storage.
/// @param icon Optional icon output; unused by ListBox text tests.
/// @param user_data Pointer to the mutable list-model probe.
static void list_provider(
    vg_widget_t *listbox, size_t index, const char **text, struct vg_icon *icon, void *user_data) {
    (void)listbox;
    (void)icon;
    list_model_probe_t *probe = (list_model_probe_t *)user_data;
    probe->provider_calls++;
    probe->last_index = index;
    snprintf(probe->text, sizeof(probe->text), "Row %zu", index);
    *text = probe->text;
}

/// @brief Count one ListBox detach notification for exact-once lifetime assertions.
/// @param listbox ListBox being detached; valid for the callback duration.
/// @param user_data Pointer to the mutable list-model probe.
static void list_unbound(vg_widget_t *listbox, void *user_data) {
    (void)listbox;
    ((list_model_probe_t *)user_data)->unbind_calls++;
}

typedef struct tree_model_probe {
    size_t provider_calls;
    size_t last_provider_index;
    size_t action_index;
    vg_treeview_virtual_action_t action;
    int action_calls;
    int unbind_calls;
    char text[32];
} tree_model_probe_t;

/// @brief Supply one synthetic flattened TreeView row descriptor.
/// @param tree Requesting TreeView; unused by this probe.
/// @param index Flattened visible-row index.
/// @param out_row Receives borrowed text, depth, and expansion metadata.
/// @param user_data Pointer to the mutable tree-model probe.
/// @return true for every index used by the test.
static bool tree_provider(vg_treeview_t *tree,
                          size_t index,
                          vg_treeview_virtual_row_t *out_row,
                          void *user_data) {
    (void)tree;
    tree_model_probe_t *probe = (tree_model_probe_t *)user_data;
    probe->provider_calls++;
    probe->last_provider_index = index;
    snprintf(probe->text, sizeof(probe->text), "Node %zu", index);
    out_row->text = probe->text;
    out_row->depth = index == 0 ? 0u : 1u;
    out_row->expanded = index == 0;
    out_row->has_children = index == 0;
    out_row->loading = false;
    return true;
}

/// @brief Record one semantic TreeView model action.
/// @param tree TreeView that received input; unused by this probe.
/// @param index Flattened row targeted by the action.
/// @param action Selection, toggle, parent, or activation action.
/// @param user_data Pointer to the mutable tree-model probe.
static void tree_action(vg_treeview_t *tree,
                        size_t index,
                        vg_treeview_virtual_action_t action,
                        void *user_data) {
    (void)tree;
    tree_model_probe_t *probe = (tree_model_probe_t *)user_data;
    probe->action_calls++;
    probe->action_index = index;
    probe->action = action;
}

/// @brief Count one TreeView detach notification for exact-once lifetime assertions.
/// @param tree TreeView being detached; valid for the callback duration.
/// @param user_data Pointer to the mutable tree-model probe.
static void tree_unbound(vg_treeview_t *tree, void *user_data) {
    (void)tree;
    ((tree_model_probe_t *)user_data)->unbind_calls++;
}

/// @brief Validate ListBox viewport bounds, selection clearing, replacement, and destruction.
static void test_virtual_list_binding(void) {
    list_model_probe_t first = {0};
    list_model_probe_t second = {0};
    vg_listbox_t *listbox = vg_listbox_create(NULL);
    assert(listbox != NULL);
    listbox->base.height = 100.0f;

    assert(vg_listbox_bind_virtual_model(
        listbox, 100000u, 20.0f, list_provider, &first, list_unbound));
    assert(vg_listbox_get_visible_first(listbox) == 0u);
    assert(vg_listbox_get_visible_count(listbox) == 7u);
    assert(first.provider_calls == 0u);

    listbox->scroll_y = 2000.0f;
    assert(vg_listbox_get_visible_first(listbox) == 100u);
    assert(vg_listbox_get_visible_count(listbox) == 7u);
    assert(first.provider_calls == 0u);

    vg_listbox_select_index(listbox, 100u);
    assert(vg_listbox_get_selected_index(listbox) == 100u);
    vg_listbox_select_index(listbox, SIZE_MAX);
    assert(vg_listbox_get_selected_index(listbox) == SIZE_MAX);

    assert(
        vg_listbox_bind_virtual_model(listbox, 10u, 20.0f, list_provider, &second, list_unbound));
    assert(first.unbind_calls == 1);
    assert(second.unbind_calls == 0);
    vg_widget_destroy(&listbox->base);
    assert(first.unbind_calls == 1);
    assert(second.unbind_calls == 1);
}

/// @brief Validate virtual TreeView viewport arithmetic and semantic input routing.
static void test_virtual_tree_binding(void) {
    tree_model_probe_t probe = {0};
    vg_treeview_t *tree = vg_treeview_create(NULL);
    assert(tree != NULL);
    vg_tree_node_t *retained = vg_treeview_add_node(tree, NULL, "retained");
    assert(retained != NULL);
    tree->base.height = 100.0f;

    assert(vg_treeview_bind_virtual_model(
        tree, 100000u, tree_provider, tree_action, &probe, tree_unbound));
    assert(vg_treeview_get_visible_first(tree) == 0u);
    assert(vg_treeview_get_visible_count(tree) <= 6u);
    assert(probe.provider_calls == 0u);

    tree->scroll_y = tree->row_height * 100.0f;
    assert(vg_treeview_get_visible_first(tree) == 100u);

    vg_event_t click;
    memset(&click, 0, sizeof(click));
    click.type = VG_EVENT_CLICK;
    click.mouse.button = VG_MOUSE_LEFT;
    click.mouse.x = 100.0f;
    click.mouse.y = 5.0f;
    assert(tree->base.vtable->handle_event(&tree->base, &click));
    assert(probe.action_calls == 1);
    assert(probe.action == VG_TREEVIEW_VIRTUAL_SELECT);
    assert(probe.action_index == 100u);
    assert(vg_treeview_get_virtual_selected_index(tree) == 100u);

    vg_event_t key;
    memset(&key, 0, sizeof(key));
    key.type = VG_EVENT_KEY_DOWN;
    key.key.key = VG_KEY_DOWN;
    assert(tree->base.vtable->handle_event(&tree->base, &key));
    assert(probe.action == VG_TREEVIEW_VIRTUAL_SELECT);
    assert(probe.action_index == 101u);

    vg_treeview_set_virtual_row_count(tree, 10u);
    assert(vg_treeview_get_virtual_selected_index(tree) == SIZE_MAX);
    vg_treeview_clear_virtual_model(tree);
    assert(probe.unbind_calls == 1);
    assert(tree->root->first_child == retained);
    vg_widget_destroy(&tree->base);
    assert(probe.unbind_calls == 1);
}

/// @brief Run the focused virtual ListBox and TreeView model contract tests.
/// @return Zero after every assertion succeeds.
int main(void) {
    test_virtual_list_binding();
    test_virtual_tree_binding();
    return 0;
}
