//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_treeview_dnd.c
// Purpose: Unit tests for the TreeView application-directed (poll-model)
//          drag-and-drop latch used by the Zia runtime (plan 22).
// Key invariants:
//   - A completed drop in app-directed mode latches source/target/position and
//     does NOT self-reorder or fire the on_drop callback.
//   - The default (callback) mode is unaffected when app-directed mode is off.
// Links: src/lib/gui/src/widgets/vg_treeview.c
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

int main(void) {
    printf("test_vg_treeview_dnd\n");
    test_app_directed_drop_latches();
    test_default_mode_does_not_latch();
    test_disabling_clears_latch();
    if (g_failures == 0) {
        printf("ALL PASSED\n");
        return 0;
    }
    printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
