//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_buttongroup.c
/// @brief Implementation of button group manager.
///
//===----------------------------------------------------------------------===//

#include "rt_buttongroup.h"
#include "rt_internal.h"
#include "rt_object.h"

#include <stdlib.h>

/// Internal structure for ButtonGroup.
struct rt_buttongroup_impl
{
    int64_t buttons[RT_BUTTONGROUP_MAX]; ///< Button IDs.
    int64_t count;                       ///< Number of buttons.
    int64_t selected;                    ///< Currently selected button ID (-1 if none).
    int8_t selection_changed;            ///< Flag: selection just changed.
};

rt_buttongroup rt_buttongroup_new(void)
{
    struct rt_buttongroup_impl *group = rt_obj_new_i64(0, sizeof(struct rt_buttongroup_impl));
    if (!group)
        return NULL;

    group->count = 0;
    group->selected = -1;
    group->selection_changed = 0;

    return group;
}

void rt_buttongroup_destroy(rt_buttongroup group)
{
    // Object is GC-managed via rt_obj_new_i64; no manual free needed.
    (void)group;
}

/// Find the index of a button in the group.
/// Returns -1 if not found.
static int64_t find_button_index(rt_buttongroup group, int64_t button_id)
{
    for (int64_t i = 0; i < group->count; i++)
    {
        if (group->buttons[i] == button_id)
            return i;
    }
    return -1;
}

int8_t rt_buttongroup_add(rt_buttongroup group, int64_t button_id)
{
    if (!group)
        return 0;
    if (group->count >= RT_BUTTONGROUP_MAX)
    {
        rt_trap("ButtonGroup.Add: button limit (" RT_BUTTONGROUP_MAX_STR
                ") exceeded — increase RT_BUTTONGROUP_MAX and recompile");
        return 0;
    }
    if (find_button_index(group, button_id) >= 0)
        return 0; // Already exists

    group->buttons[group->count] = button_id;
    group->count++;
    return 1;
}

int8_t rt_buttongroup_remove(rt_buttongroup group, int64_t button_id)
{
    if (!group)
        return 0;

    int64_t index = find_button_index(group, button_id);
    if (index < 0)
        return 0;

    // If removing selected button, clear selection
    if (group->selected == button_id)
    {
        group->selected = -1;
        group->selection_changed = 1;
    }

    // Shift remaining buttons down
    for (int64_t i = index; i < group->count - 1; i++)
    {
        group->buttons[i] = group->buttons[i + 1];
    }
    group->count--;

    return 1;
}

int8_t rt_buttongroup_has(rt_buttongroup group, int64_t button_id)
{
    if (!group)
        return 0;
    return find_button_index(group, button_id) >= 0 ? 1 : 0;
}

int64_t rt_buttongroup_count(rt_buttongroup group)
{
    if (!group)
        return 0;
    return group->count;
}

int8_t rt_buttongroup_select(rt_buttongroup group, int64_t button_id)
{
    if (!group)
        return 0;
    if (find_button_index(group, button_id) < 0)
        return 0;

    if (group->selected != button_id)
    {
        group->selected = button_id;
        group->selection_changed = 1;
    }
    return 1;
}

void rt_buttongroup_clear_selection(rt_buttongroup group)
{
    if (!group)
        return;
    if (group->selected >= 0)
    {
        group->selected = -1;
        group->selection_changed = 1;
    }
}

int64_t rt_buttongroup_selected(rt_buttongroup group)
{
    if (!group)
        return -1;
    return group->selected;
}

int8_t rt_buttongroup_is_selected(rt_buttongroup group, int64_t button_id)
{
    if (!group)
        return 0;
    return group->selected == button_id ? 1 : 0;
}

int8_t rt_buttongroup_has_selection(rt_buttongroup group)
{
    if (!group)
        return 0;
    return group->selected >= 0 ? 1 : 0;
}

int8_t rt_buttongroup_selection_changed(rt_buttongroup group)
{
    if (!group)
        return 0;
    return group->selection_changed;
}

void rt_buttongroup_clear_changed_flag(rt_buttongroup group)
{
    if (!group)
        return;
    group->selection_changed = 0;
}

int64_t rt_buttongroup_get_at(rt_buttongroup group, int64_t index)
{
    if (!group)
        return -1;
    if (index < 0 || index >= group->count)
        return -1;
    return group->buttons[index];
}

int64_t rt_buttongroup_select_next(rt_buttongroup group)
{
    if (!group || group->count == 0)
        return -1;

    int64_t current_index = -1;
    if (group->selected >= 0)
    {
        current_index = find_button_index(group, group->selected);
    }

    int64_t next_index = (current_index + 1) % group->count;
    int64_t next_id = group->buttons[next_index];

    group->selected = next_id;
    group->selection_changed = 1;

    return next_id;
}

int64_t rt_buttongroup_select_prev(rt_buttongroup group)
{
    if (!group || group->count == 0)
        return -1;

    int64_t current_index = 0;
    if (group->selected >= 0)
    {
        current_index = find_button_index(group, group->selected);
        if (current_index < 0)
            current_index = 0;
    }

    int64_t prev_index = (current_index - 1 + group->count) % group->count;
    int64_t prev_id = group->buttons[prev_index];

    group->selected = prev_id;
    group->selection_changed = 1;

    return prev_id;
}
