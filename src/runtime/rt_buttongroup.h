//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file rt_buttongroup.h
/// @brief Button group manager for mutually exclusive selections.
///
/// Provides a container for managing groups of buttons where only one
/// button can be selected at a time (like radio buttons or tool palettes).
///
//===----------------------------------------------------------------------===//

#ifndef VIPER_RT_BUTTONGROUP_H
#define VIPER_RT_BUTTONGROUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/// Maximum number of buttons in a group.
#define RT_BUTTONGROUP_MAX 64

    /// Opaque handle to a ButtonGroup instance.
    typedef struct rt_buttongroup_impl *rt_buttongroup;

    /// Creates a new ButtonGroup.
    /// @return A new ButtonGroup instance.
    rt_buttongroup rt_buttongroup_new(void);

    /// Destroys a ButtonGroup and frees its memory.
    /// @param group The button group to destroy.
    void rt_buttongroup_destroy(rt_buttongroup group);

    /// Adds a button to the group.
    /// @param group The button group.
    /// @param button_id Unique identifier for the button.
    /// @return 1 on success, 0 if button_id already exists or group is full.
    int8_t rt_buttongroup_add(rt_buttongroup group, int64_t button_id);

    /// Removes a button from the group.
    /// @param group The button group.
    /// @param button_id The button to remove.
    /// @return 1 on success, 0 if button doesn't exist.
    int8_t rt_buttongroup_remove(rt_buttongroup group, int64_t button_id);

    /// Checks if a button exists in the group.
    /// @param group The button group.
    /// @param button_id The button to check.
    /// @return 1 if exists, 0 otherwise.
    int8_t rt_buttongroup_has(rt_buttongroup group, int64_t button_id);

    /// Gets the number of buttons in the group.
    /// @param group The button group.
    /// @return Number of buttons.
    int64_t rt_buttongroup_count(rt_buttongroup group);

    /// Selects a button (deselects all others).
    /// @param group The button group.
    /// @param button_id The button to select.
    /// @return 1 on success, 0 if button doesn't exist.
    int8_t rt_buttongroup_select(rt_buttongroup group, int64_t button_id);

    /// Deselects all buttons.
    /// @param group The button group.
    void rt_buttongroup_clear_selection(rt_buttongroup group);

    /// Gets the currently selected button.
    /// @param group The button group.
    /// @return Selected button ID, or -1 if none selected.
    int64_t rt_buttongroup_selected(rt_buttongroup group);

    /// Checks if a specific button is selected.
    /// @param group The button group.
    /// @param button_id The button to check.
    /// @return 1 if selected, 0 otherwise.
    int8_t rt_buttongroup_is_selected(rt_buttongroup group, int64_t button_id);

    /// Checks if any button is selected.
    /// @param group The button group.
    /// @return 1 if any button is selected, 0 otherwise.
    int8_t rt_buttongroup_has_selection(rt_buttongroup group);

    /// Checks if the selection just changed this frame.
    /// Call after select(), returns true once then resets.
    /// @param group The button group.
    /// @return 1 if selection just changed, 0 otherwise.
    int8_t rt_buttongroup_selection_changed(rt_buttongroup group);

    /// Clears the selection-changed flag (call at end of frame).
    /// @param group The button group.
    void rt_buttongroup_clear_changed_flag(rt_buttongroup group);

    /// Gets the button ID at a specific index (for iteration).
    /// @param group The button group.
    /// @param index Zero-based index (0 to count-1).
    /// @return Button ID at index, or -1 if out of range.
    int64_t rt_buttongroup_get_at(rt_buttongroup group, int64_t index);

    /// Selects the next button in the group (wraps around).
    /// @param group The button group.
    /// @return The newly selected button ID, or -1 if group is empty.
    int64_t rt_buttongroup_select_next(rt_buttongroup group);

    /// Selects the previous button in the group (wraps around).
    /// @param group The button group.
    /// @return The newly selected button ID, or -1 if group is empty.
    int64_t rt_buttongroup_select_prev(rt_buttongroup group);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_BUTTONGROUP_H
