//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_treeview_dnd.c
// Purpose: Unit tests for retained TreeView multi-selection and the
//          application-directed drag-and-drop latch used by the Zia runtime.
// Key invariants:
//   - A completed drop in app-directed mode latches source/target/position and
//     does NOT self-reorder or fire the on_drop callback.
//   - Legacy poll mode remains container-only/INTO while row-aware mode
//     classifies BEFORE/INTO/AFTER and accepts leaf targets.
//   - Retained multi-selection follows Ctrl/Command toggle and Shift-range
//     semantics without changing virtual-tree selection behavior.
//   - Scrollbar cleanup never steals a retained-tree drag's input capture.
// Links: src/lib/gui/src/widgets/vg_treeview.c
//        docs/adr/0163-stable-multiselect-and-row-aware-treeview-editing.md
//
//===----------------------------------------------------------------------===//

#include "vg_event.h"
#include "vg_ide_widgets.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;

static void check(const char *name, int cond) {
    if (cond) {
        printf("  ok   %s\n", name);
    } else {
        printf("  FAIL %s\n", name);
        g_failures++;
    }
}

static vg_event_t mouse_up_left(void) {
    vg_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = VG_EVENT_MOUSE_UP;
    ev.mouse.button = VG_MOUSE_LEFT;
    return ev;
}

static vg_event_t mouse_event(vg_event_type_t type, float x, float y) {
    return vg_event_mouse(type, x, y, VG_MOUSE_LEFT, 0);
}

static vg_event_t mouse_event_with_modifiers(vg_event_type_t type,
                                             float x,
                                             float y,
                                             uint32_t modifiers) {
    return vg_event_mouse(type, x, y, VG_MOUSE_LEFT, modifiers);
}

static void send_row_click(vg_treeview_t *tree, int row, uint32_t modifiers) {
    vg_event_t click = mouse_event_with_modifiers(
        VG_EVENT_CLICK, 80.0f, tree->row_height * ((float)row + 0.5f), modifiers);
    check("row click is handled", vg_event_send(&tree->base, &click));
}

static void send_drag(vg_treeview_t *tree, int source_row, int target_row, float target_fraction) {
    vg_event_t down =
        mouse_event(VG_EVENT_MOUSE_DOWN, 80.0f, tree->row_height * ((float)source_row + 0.5f));
    (void)vg_event_send(&tree->base, &down);
    vg_event_t move = mouse_event(
        VG_EVENT_MOUSE_MOVE, 80.0f, tree->row_height * ((float)target_row + target_fraction));
    (void)vg_event_send(&tree->base, &move);
    vg_event_t up = mouse_event(
        VG_EVENT_MOUSE_UP, 80.0f, tree->row_height * ((float)target_row + target_fraction));
    (void)vg_event_send(&tree->base, &up);
}

static void test_retained_multi_selection_pointer_and_keyboard(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *first = vg_treeview_add_node(tv, NULL, "first");
    vg_tree_node_t *second = vg_treeview_add_node(tv, NULL, "second");
    vg_tree_node_t *third = vg_treeview_add_node(tv, NULL, "third");
    vg_tree_node_t *fourth = vg_treeview_add_node(tv, NULL, "fourth");
    check("multi-selection tree and rows created", tv && first && second && third && fourth);
    tv->base.width = 240.0f;
    tv->base.height = tv->row_height * 4.0f;
    vg_treeview_set_multi_select(tv, true);

    send_row_click(tv, 0, VG_MOD_NONE);
    send_row_click(tv, 2, VG_MOD_CTRL);
    check("Ctrl-click retains prior row", first->selected);
    check("Ctrl-click adds target row", third->selected);
    check("Ctrl-click target becomes primary", tv->selected == third);

    send_row_click(tv, 3, VG_MOD_SHIFT);
    check("Shift-click replaces with visible anchor range",
          !first->selected && !second->selected && third->selected && fourth->selected);
    check("Shift-click target becomes primary", tv->selected == fourth);

    send_row_click(tv, 3, VG_MOD_SUPER);
    check("Command-click toggles primary off", !fourth->selected);
    check("first remaining retained row is promoted", tv->selected == third);

    vg_event_t shifted_up = vg_event_key(VG_EVENT_KEY_DOWN, VG_KEY_UP, 0, VG_MOD_SHIFT);
    check("Shift-Up is handled", vg_event_send(&tv->base, &shifted_up));
    check("Shift-Up extends visible range from anchor",
          !first->selected && second->selected && third->selected && !fourth->selected);
    check("keyboard target becomes primary", tv->selected == second);

    vg_event_t plain_down = vg_event_key(VG_EVENT_KEY_DOWN, VG_KEY_DOWN, 0, VG_MOD_NONE);
    check("plain Down is handled", vg_event_send(&tv->base, &plain_down));
    check("plain keyboard navigation replaces selection",
          !first->selected && !second->selected && third->selected && !fourth->selected);

    vg_widget_destroy(&tv->base);
}

static void test_programmatic_multi_selection_and_disable(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *first = vg_treeview_add_node(tv, NULL, "first");
    vg_tree_node_t *second = vg_treeview_add_node(tv, NULL, "second");
    vg_tree_node_t *third = vg_treeview_add_node(tv, NULL, "third");
    check("programmatic selection rows created", tv && first && second && third);

    vg_treeview_set_multi_select(tv, true);
    vg_treeview_select(tv, first);
    vg_treeview_select(tv, third);
    check("programmatic Select is additive in multi mode",
          first->selected && !second->selected && third->selected);
    check("last programmatic target is primary", tv->selected == third);

    vg_treeview_set_multi_select(tv, false);
    check("disabling multi-select keeps primary only",
          !first->selected && !second->selected && third->selected);

    vg_treeview_select(tv, NULL);
    check("Select(NULL) clears all retained selection",
          tv->selected == NULL && !first->selected && !second->selected && !third->selected);

    vg_widget_destroy(&tv->base);
}

static void test_drop_position_numbers_match_public_contract(void) {
    check("BEFORE has public value zero", (int)VG_TREE_DROP_BEFORE == 0);
    check("INTO has public value one", (int)VG_TREE_DROP_INTO == 1);
    check("AFTER has public value two", (int)VG_TREE_DROP_AFTER == 2);
}

static void test_app_directed_drop_latches(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *file = vg_treeview_add_node(tv, NULL, "file.zia");
    vg_tree_node_t *folder = vg_treeview_add_node(tv, NULL, "src");
    vg_tree_node_set_has_children(folder, true);
    vg_treeview_set_app_directed_dnd(tv, true);

    check("no pending drop initially", !vg_treeview_has_pending_drop(tv));

    // Simulate a drag in progress that ends on the folder (INTO).
    tv->is_dragging = true;
    tv->drag_node = file;
    tv->drop_target = folder;
    tv->drop_position = VG_TREE_DROP_INTO;
    vg_event_t up = mouse_up_left();
    tv->base.vtable->handle_event(&tv->base, &up);

    check("drop is latched", vg_treeview_has_pending_drop(tv));
    check("latched source is dragged node", vg_treeview_drop_source(tv) == file);
    check("latched target is folder", vg_treeview_drop_target_node(tv) == folder);
    check("latched position is INTO",
          vg_treeview_drop_position_value(tv) == (int)VG_TREE_DROP_INTO);

    // Consuming the latch clears it.
    vg_treeview_clear_drop(tv);
    check("clear consumes the latch", !vg_treeview_has_pending_drop(tv));

    vg_widget_destroy(&tv->base);
}

static void test_row_aware_mode_classifies_leaf_drop_regions(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *source = vg_treeview_add_node(tv, NULL, "source");
    vg_tree_node_t *leaf = vg_treeview_add_node(tv, NULL, "leaf");
    check("row-aware drag rows created", tv && source && leaf);
    tv->base.width = 240.0f;
    tv->base.height = tv->row_height * 2.0f;
    vg_treeview_set_app_directed_dnd_mode(tv, VG_TREEVIEW_APP_DND_ROW_AWARE);

    send_drag(tv, 0, 1, 0.1f);
    check("row-aware mode accepts leaf target before", vg_treeview_has_pending_drop(tv));
    check("top row region latches BEFORE",
          vg_treeview_drop_position_value(tv) == (int)VG_TREE_DROP_BEFORE);
    vg_treeview_clear_drop(tv);

    send_drag(tv, 0, 1, 0.5f);
    check("middle row region latches INTO",
          vg_treeview_has_pending_drop(tv) &&
              vg_treeview_drop_position_value(tv) == (int)VG_TREE_DROP_INTO);
    vg_treeview_clear_drop(tv);

    send_drag(tv, 0, 1, 0.9f);
    check("bottom row region latches AFTER",
          vg_treeview_has_pending_drop(tv) &&
              vg_treeview_drop_position_value(tv) == (int)VG_TREE_DROP_AFTER);
    check("row-aware latch keeps source and leaf target",
          vg_treeview_drop_source(tv) == source && vg_treeview_drop_target_node(tv) == leaf);

    vg_widget_destroy(&tv->base);
}

static void test_legacy_mode_stays_container_only_into(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *source = vg_treeview_add_node(tv, NULL, "source");
    vg_tree_node_t *leaf = vg_treeview_add_node(tv, NULL, "leaf");
    vg_tree_node_t *folder = vg_treeview_add_node(tv, NULL, "folder");
    check("legacy drag rows created", tv && source && leaf && folder);
    vg_tree_node_set_has_children(folder, true);
    tv->base.width = 240.0f;
    tv->base.height = tv->row_height * 3.0f;
    vg_treeview_set_app_directed_dnd(tv, true);

    send_drag(tv, 0, 1, 0.5f);
    check("legacy mode rejects leaf targets", !vg_treeview_has_pending_drop(tv));

    send_drag(tv, 0, 2, 0.1f);
    check("legacy mode accepts advertised container", vg_treeview_has_pending_drop(tv));
    check("legacy mode forces INTO regardless of row region",
          vg_treeview_drop_position_value(tv) == (int)VG_TREE_DROP_INTO);

    vg_widget_destroy(&tv->base);
}

static void test_mode_changes_cancel_state_and_invalid_modes_are_ignored(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *source = vg_treeview_add_node(tv, NULL, "source");
    vg_tree_node_t *leaf = vg_treeview_add_node(tv, NULL, "leaf");
    check("mode transition rows created", tv && source && leaf);
    tv->base.width = 240.0f;
    tv->base.height = tv->row_height * 2.0f;
    vg_treeview_set_app_directed_dnd_mode(tv, VG_TREEVIEW_APP_DND_ROW_AWARE);
    vg_treeview_set_app_directed_dnd_mode(tv, -1);
    vg_treeview_set_app_directed_dnd_mode(tv, 3);
    send_drag(tv, 0, 1, 0.1f);
    check("invalid mode values leave row-aware mode unchanged",
          vg_treeview_has_pending_drop(tv) &&
              vg_treeview_drop_position_value(tv) == (int)VG_TREE_DROP_BEFORE);

    vg_treeview_set_app_directed_dnd_mode(tv, VG_TREEVIEW_APP_DND_LEGACY_INTO);
    check("changing modes clears a pending latch", !vg_treeview_has_pending_drop(tv));

    vg_event_t down = mouse_event(VG_EVENT_MOUSE_DOWN, 80.0f, tv->row_height * 0.5f);
    (void)vg_event_send(&tv->base, &down);
    check("active drag press owns capture", vg_widget_get_input_capture() == &tv->base);
    vg_treeview_set_app_directed_dnd_mode(tv, VG_TREEVIEW_APP_DND_DISABLED);
    check("disabling cancels current drag state",
          !tv->is_dragging && tv->drag_node == NULL && tv->drop_target == NULL);
    check("disabling releases owned input capture", vg_widget_get_input_capture() == NULL);
    check("disabling turns dragging off", !tv->drag_enabled);

    vg_treeview_set_drag_enabled(tv, true);
    vg_treeview_set_app_directed_dnd_mode(tv, VG_TREEVIEW_APP_DND_DISABLED);
    check("explicit disabled mode overrides callback dragging", !tv->drag_enabled);

    vg_widget_destroy(&tv->base);
}

static void test_default_mode_does_not_latch(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *file = vg_treeview_add_node(tv, NULL, "file.zia");
    vg_tree_node_t *folder = vg_treeview_add_node(tv, NULL, "src");
    vg_tree_node_set_has_children(folder, true);
    // App-directed mode NOT enabled — default callback behavior.

    tv->is_dragging = true;
    tv->drag_node = file;
    tv->drop_target = folder;
    tv->drop_position = VG_TREE_DROP_INTO;
    vg_event_t up = mouse_up_left();
    tv->base.vtable->handle_event(&tv->base, &up);

    check("default mode never latches (no poll consumer)", !vg_treeview_has_pending_drop(tv));
    vg_widget_destroy(&tv->base);
}

static void test_disabling_clears_latch(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *file = vg_treeview_add_node(tv, NULL, "a");
    vg_tree_node_t *folder = vg_treeview_add_node(tv, NULL, "b");
    vg_tree_node_set_has_children(folder, true);
    vg_treeview_set_app_directed_dnd(tv, true);
    tv->is_dragging = true;
    tv->drag_node = file;
    tv->drop_target = folder;
    tv->drop_position = VG_TREE_DROP_INTO;
    vg_event_t up = mouse_up_left();
    tv->base.vtable->handle_event(&tv->base, &up);
    check("latched before disable", vg_treeview_has_pending_drop(tv));
    vg_treeview_set_app_directed_dnd(tv, false);
    check("disabling app-directed DnD clears the latch", !vg_treeview_has_pending_drop(tv));
    vg_widget_destroy(&tv->base);
}

static void test_app_directed_drag_survives_without_scrollbar(void) {
    vg_treeview_t *tv = vg_treeview_create(NULL);
    vg_tree_node_t *file = vg_treeview_add_node(tv, NULL, "file.zia");
    vg_tree_node_t *folder = vg_treeview_add_node(tv, NULL, "src");
    vg_tree_node_set_has_children(folder, true);
    tv->base.width = 240.0f;
    tv->base.height = 120.0f; // Two rows fit, so no scrollbar exists.
    vg_treeview_set_app_directed_dnd(tv, true);

    vg_event_t down = mouse_event(VG_EVENT_MOUSE_DOWN, 24.0f, tv->row_height * 0.5f);
    check("press captures tree for app-directed drag",
          !vg_event_send(&tv->base, &down) && vg_widget_get_input_capture() == &tv->base);

    vg_event_t move = mouse_event(VG_EVENT_MOUSE_MOVE, 24.0f, tv->row_height * 1.5f);
    check("no-scrollbar move promotes retained-tree drag", vg_event_send(&tv->base, &move));
    check("drag still owns capture", vg_widget_get_input_capture() == &tv->base);

    vg_event_t up = mouse_event(VG_EVENT_MOUSE_UP, 24.0f, tv->row_height * 1.5f);
    check("app-directed drop completes", vg_event_send(&tv->base, &up));
    check("actual event sequence latches source", vg_treeview_drop_source(tv) == file);
    check("actual event sequence latches folder", vg_treeview_drop_target_node(tv) == folder);
    check("completed drag releases capture", vg_widget_get_input_capture() == NULL);

    vg_widget_destroy(&tv->base);
}

int main(void) {
    printf("test_vg_treeview_dnd\n");
    test_retained_multi_selection_pointer_and_keyboard();
    test_programmatic_multi_selection_and_disable();
    test_drop_position_numbers_match_public_contract();
    test_app_directed_drop_latches();
    test_row_aware_mode_classifies_leaf_drop_regions();
    test_legacy_mode_stays_container_only_into();
    test_mode_changes_cancel_state_and_invalid_modes_are_ignored();
    test_default_mode_does_not_latch();
    test_disabling_clears_latch();
    test_app_directed_drag_survives_without_scrollbar();
    if (g_failures == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
