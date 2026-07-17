//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/tests/test_vg_popuplist.c
// Purpose: Headless tests for the popup list widget (Zanna.GUI.PopupList) —
//          item storage, case-insensitive filtering, selection navigation,
//          accept (consume-on-read), and visibility. The filter/selection logic
//          is font-independent, so no font is required.
//
//===----------------------------------------------------------------------===//

#include "vg_ide_widgets.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    vg_popuplist_t *list = vg_popuplist_create(NULL);
    CHECK(list != NULL);
    CHECK(vg_popuplist_visible_count(list) == 0);
    CHECK(vg_popuplist_selected_index(list) == -1);
    CHECK(vg_popuplist_selected_text(list) == NULL);
    CHECK(vg_popuplist_is_visible(list) == false); // hidden initially

    vg_popuplist_add_item(list, "apple");
    vg_popuplist_add_item(list, "banana");
    vg_popuplist_add_item(list, "apricot");
    vg_popuplist_add_item(list, "cherry");
    CHECK(vg_popuplist_visible_count(list) == 4);
    CHECK(vg_popuplist_selected_index(list) == 0);
    CHECK(strcmp(vg_popuplist_selected_text(list), "apple") == 0);

    // Navigation is clamped at both ends.
    vg_popuplist_navigate_down(list);
    CHECK(vg_popuplist_selected_index(list) == 1);
    CHECK(strcmp(vg_popuplist_selected_text(list), "banana") == 0);
    vg_popuplist_navigate_up(list);
    vg_popuplist_navigate_up(list); // already at 0 -> stays
    CHECK(vg_popuplist_selected_index(list) == 0);

    // Case-insensitive filter narrows to matching items and resets the selection.
    vg_popuplist_set_filter(list, "AP"); // matches "apple", "apricot"
    CHECK(vg_popuplist_visible_count(list) == 2);
    CHECK(vg_popuplist_selected_index(list) == 0);
    CHECK(strcmp(vg_popuplist_selected_text(list), "apple") == 0);
    vg_popuplist_navigate_down(list);
    CHECK(strcmp(vg_popuplist_selected_text(list), "apricot") == 0);
    vg_popuplist_navigate_down(list); // clamps at the last visible item
    CHECK(vg_popuplist_selected_index(list) == 1);

    // A filter with no matches has no selection.
    vg_popuplist_set_filter(list, "zzz");
    CHECK(vg_popuplist_visible_count(list) == 0);
    CHECK(vg_popuplist_selected_index(list) == -1);
    CHECK(vg_popuplist_selected_text(list) == NULL);

    // Clearing the filter shows everything again.
    vg_popuplist_set_filter(list, "");
    CHECK(vg_popuplist_visible_count(list) == 4);

    // Accept is consume-on-read.
    CHECK(vg_popuplist_was_accepted(list) == false);
    vg_popuplist_set_selected_index(list, 3);
    CHECK(strcmp(vg_popuplist_selected_text(list), "cherry") == 0);
    vg_popuplist_accept_selected(list);
    CHECK(vg_popuplist_was_accepted(list) == true);
    CHECK(vg_popuplist_was_accepted(list) == false); // consumed by the previous read

    // Visibility toggles.
    vg_popuplist_set_visible(list, true);
    CHECK(vg_popuplist_is_visible(list) == true);
    vg_popuplist_set_visible(list, false);
    CHECK(vg_popuplist_is_visible(list) == false);

    // Clear removes all items.
    vg_popuplist_clear(list);
    CHECK(vg_popuplist_visible_count(list) == 0);

    vg_popuplist_destroy(list);

    printf("test_vg_popuplist: OK\n");
    return 0;
}
