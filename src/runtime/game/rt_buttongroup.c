//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/game/rt_buttongroup.c
// Purpose: Exclusive radio-button selection manager for Viper game UIs and
//   menus. A ButtonGroup tracks a set of registered button IDs and enforces
//   the invariant that at most one is selected at any time (radio-button
//   semantics). Selecting a new button automatically deselects the previous
//   one. Typical uses: difficulty selection, game mode picker, weapon wheel,
//   and any menu where exactly one option must be chosen.
//
// Key invariants:
//   - Button IDs are arbitrary int64 values registered via rt_buttongroup_add().
//     The group stores them in a flat array of capacity RT_BUTTONGROUP_MAX (256).
//     Adding a 257th button fires rt_trap() with a descriptive message.
//   - The selected ID is meaningful only when has_selection is true. Selected()
//     returns -1 for no selection, but -1 is still a valid registered button ID.
//     Selecting an unregistered ID is silently ignored.
//   - rt_buttongroup_is_selected() checks the currently selected ID against the
//     given ID; it returns 1 only when there is an active selection AND it
//     matches the given ID.
//   - Removing the selected button clears the active selection and marks the
//     selection as changed, so callers never observe a stale selected ID.
//
// Ownership/Lifetime:
//   - ButtonGroup objects are reference-counted GC objects. The fixed-capacity
//     button ID array is inline and needs no finalizer allocation cleanup.
//
// Links: src/runtime/game/rt_buttongroup.h (public API),
//        docs/viperlib/game.md (ButtonGroup section — RT_BUTTONGROUP_MAX note)
//
//===----------------------------------------------------------------------===//

#include "rt_buttongroup.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>

/// Internal structure for ButtonGroup.
struct rt_buttongroup_impl {
    int64_t buttons[RT_BUTTONGROUP_MAX]; ///< Button IDs.
    int64_t count;                       ///< Number of buttons.
    int64_t selected;                    ///< Currently selected button ID.
    int8_t has_selection;                ///< 1 if selected is meaningful.
    int8_t selection_changed;            ///< Flag: selection just changed.
};

/// @brief Safe-cast a handle to the ButtonGroup impl, trapping @p api on a
///        class-id mismatch. @return The impl, or NULL if @p group is NULL.
static struct rt_buttongroup_impl *checked_group(rt_buttongroup group, const char *api) {
    if (!group)
        return NULL;
    if (rt_obj_class_id(group) != RT_BUTTONGROUP_CLASS_ID) {
        rt_trap(api);
        return NULL;
    }
    return group;
}

/// @brief Create a new buttongroup object.
rt_buttongroup rt_buttongroup_new(void) {
    struct rt_buttongroup_impl *group =
        rt_obj_new_i64(RT_BUTTONGROUP_CLASS_ID, sizeof(struct rt_buttongroup_impl));
    if (!group)
        return NULL;

    group->count = 0;
    group->selected = -1;
    group->has_selection = 0;
    group->selection_changed = 0;

    return group;
}

/// @brief Release resources and destroy the buttongroup.
void rt_buttongroup_destroy(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.Destroy: expected Viper.Game.ButtonGroup");
    if (group && rt_obj_release_check0(group)) {
        rt_obj_free(group);
    }
}

/// Find the index of a button in the group.
/// Returns -1 if not found.
static int64_t find_button_index(rt_buttongroup group, int64_t button_id) {
    for (int64_t i = 0; i < group->count; i++) {
        if (group->buttons[i] == button_id)
            return i;
    }
    return -1;
}

/// @brief Add an element to the buttongroup.
int8_t rt_buttongroup_add(rt_buttongroup group, int64_t button_id) {
    group = checked_group(group, "ButtonGroup.Add: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    // Check for an existing ID before enforcing capacity so an idempotent re-add of
    // an already-present button returns false (its normal duplicate contract) rather
    // than trapping just because the group happens to be full (VDOC-282). The trap is
    // reserved for a genuinely new ID that would overflow the array.
    if (find_button_index(group, button_id) >= 0)
        return 0; // Already exists
    if (group->count >= RT_BUTTONGROUP_MAX) {
        rt_trap("ButtonGroup.Add: button limit (" RT_BUTTONGROUP_MAX_STR
                ") exceeded — increase RT_BUTTONGROUP_MAX and recompile");
        return 0;
    }

    group->buttons[group->count] = button_id;
    group->count++;
    return 1;
}

/// @brief Remove an entry from the buttongroup.
int8_t rt_buttongroup_remove(rt_buttongroup group, int64_t button_id) {
    group = checked_group(group, "ButtonGroup.Remove: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;

    int64_t index = find_button_index(group, button_id);
    if (index < 0)
        return 0;

    // If removing selected button, clear selection
    if (group->has_selection && group->selected == button_id) {
        group->selected = -1;
        group->has_selection = 0;
        group->selection_changed = 1;
    }

    // Shift remaining buttons down
    for (int64_t i = index; i < group->count - 1; i++) {
        group->buttons[i] = group->buttons[i + 1];
    }
    group->count--;

    return 1;
}

/// @brief Check whether a key/element exists in the buttongroup.
int8_t rt_buttongroup_has(rt_buttongroup group, int64_t button_id) {
    group = checked_group(group, "ButtonGroup.Has: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    return find_button_index(group, button_id) >= 0 ? 1 : 0;
}

/// @brief Return the count of elements in the buttongroup.
int64_t rt_buttongroup_count(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.Count: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    return group->count;
}

/// @brief Select the buttongroup.
int8_t rt_buttongroup_select(rt_buttongroup group, int64_t button_id) {
    group = checked_group(group, "ButtonGroup.Select: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    if (find_button_index(group, button_id) < 0)
        return 0;

    if (!group->has_selection || group->selected != button_id) {
        group->selected = button_id;
        group->has_selection = 1;
        group->selection_changed = 1;
    }
    return 1;
}

/// @brief Deselect the currently selected button (sets selection to -1).
void rt_buttongroup_clear_selection(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.ClearSelection: expected Viper.Game.ButtonGroup");
    if (!group)
        return;
    if (group->has_selection) {
        group->selected = -1;
        group->has_selection = 0;
        group->selection_changed = 1;
    }
}

/// @brief Return the button ID of the currently selected button, or -1 if none.
int64_t rt_buttongroup_selected(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.Selected: expected Viper.Game.ButtonGroup");
    if (!group)
        return -1;
    return group->has_selection ? group->selected : -1;
}

/// @brief Check whether a specific button ID is the currently selected one.
int8_t rt_buttongroup_is_selected(rt_buttongroup group, int64_t button_id) {
    group = checked_group(group, "ButtonGroup.IsSelected: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    return (group->has_selection && group->selected == button_id) ? 1 : 0;
}

/// @brief Check whether any button is currently selected.
int8_t rt_buttongroup_has_selection(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.HasSelection: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    return group->has_selection;
}

/// @brief Check whether the selection changed since the last clear_changed_flag call.
int8_t rt_buttongroup_selection_changed(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.SelectionChanged: expected Viper.Game.ButtonGroup");
    if (!group)
        return 0;
    return group->selection_changed;
}

/// @brief Clear the changed flag of the buttongroup.
void rt_buttongroup_clear_changed_flag(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.ClearChangedFlag: expected Viper.Game.ButtonGroup");
    if (!group)
        return;
    group->selection_changed = 0;
}

/// @brief Return the button ID at a given index in the group's button list.
int64_t rt_buttongroup_get_at(rt_buttongroup group, int64_t index) {
    group = checked_group(group, "ButtonGroup.GetAt: expected Viper.Game.ButtonGroup");
    if (!group)
        return -1;
    if (index < 0 || index >= group->count)
        return -1;
    return group->buttons[index];
}

/// @brief Move selection to the next button in the group, wrapping at the end.
int64_t rt_buttongroup_select_next(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.SelectNext: expected Viper.Game.ButtonGroup");
    if (!group || group->count == 0)
        return -1;

    int64_t current_index = -1;
    if (group->has_selection) {
        current_index = find_button_index(group, group->selected);
    }

    int64_t next_index = (current_index + 1) % group->count;
    int64_t next_id = group->buttons[next_index];

    if (!group->has_selection || group->selected != next_id) {
        group->selection_changed = 1;
    }
    group->selected = next_id;
    group->has_selection = 1;

    return next_id;
}

/// @brief Move selection to the previous button in the group, wrapping at the start.
int64_t rt_buttongroup_select_prev(rt_buttongroup group) {
    group = checked_group(group, "ButtonGroup.SelectPrevious: expected Viper.Game.ButtonGroup");
    if (!group || group->count == 0)
        return -1;

    int64_t current_index = 0;
    if (group->has_selection) {
        current_index = find_button_index(group, group->selected);
        if (current_index < 0)
            current_index = 0;
    }

    int64_t prev_index = (current_index - 1 + group->count) % group->count;
    int64_t prev_id = group->buttons[prev_index];

    if (!group->has_selection || group->selected != prev_id) {
        group->selection_changed = 1;
    }
    group->selected = prev_id;
    group->has_selection = 1;

    return prev_id;
}
