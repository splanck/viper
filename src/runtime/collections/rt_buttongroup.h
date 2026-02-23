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
/// Key invariants: At most one button is selected at any time. Button IDs
///     within a group are unique. The group holds at most
///     RT_BUTTONGROUP_MAX buttons.
/// Ownership/Lifetime: Caller owns the group handle; destroy with
///     rt_buttongroup_destroy().
/// Links: Viper.ButtonGroup standard library module.
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
#define RT_BUTTONGROUP_MAX 256
#define RT_BUTTONGROUP_MAX_STR "256"

    /// Opaque handle to a ButtonGroup instance.
    typedef struct rt_buttongroup_impl *rt_buttongroup;

    /// @brief Create a new ButtonGroup.
    /// @return A new empty ButtonGroup instance with no selection.
    rt_buttongroup rt_buttongroup_new(void);

    /// @brief Destroy a ButtonGroup and free its memory.
    /// @param group The button group to destroy.
    void rt_buttongroup_destroy(rt_buttongroup group);

    /// @brief Add a button to the group.
    /// @param group The button group.
    /// @param button_id Unique identifier for the button.
    /// @return 1 on success, 0 if @p button_id already exists.
    /// @note Traps if the group already contains RT_BUTTONGROUP_MAX buttons.
    int8_t rt_buttongroup_add(rt_buttongroup group, int64_t button_id);

    /// @brief Remove a button from the group.
    /// @param group The button group.
    /// @param button_id The button to remove. If this button is currently
    ///                  selected, the selection is cleared.
    /// @return 1 on success, 0 if @p button_id does not exist in the group.
    int8_t rt_buttongroup_remove(rt_buttongroup group, int64_t button_id);

    /// @brief Check if a button exists in the group.
    /// @param group The button group.
    /// @param button_id The button to check.
    /// @return 1 if the button exists in the group, 0 otherwise.
    int8_t rt_buttongroup_has(rt_buttongroup group, int64_t button_id);

    /// @brief Get the number of buttons in the group.
    /// @param group The button group.
    /// @return Number of buttons currently in the group.
    int64_t rt_buttongroup_count(rt_buttongroup group);

    /// @brief Select a button (deselects all others).
    /// @param group The button group.
    /// @param button_id The button to select.
    /// @return 1 on success, 0 if @p button_id does not exist in the group.
    int8_t rt_buttongroup_select(rt_buttongroup group, int64_t button_id);

    /// @brief Deselect all buttons (clear the selection).
    /// @param group The button group.
    void rt_buttongroup_clear_selection(rt_buttongroup group);

    /// @brief Get the currently selected button.
    /// @param group The button group.
    /// @return Selected button ID, or -1 if no button is selected.
    int64_t rt_buttongroup_selected(rt_buttongroup group);

    /// @brief Check if a specific button is the currently selected one.
    /// @param group The button group.
    /// @param button_id The button to check.
    /// @return 1 if @p button_id is the selected button, 0 otherwise.
    int8_t rt_buttongroup_is_selected(rt_buttongroup group, int64_t button_id);

    /// @brief Check if any button is selected.
    /// @param group The button group.
    /// @return 1 if any button is selected, 0 if the selection is empty.
    int8_t rt_buttongroup_has_selection(rt_buttongroup group);

    /// @brief Check if the selection changed since the last frame.
    /// @details Returns true once after select() changes the selection, then
    ///          resets. Use rt_buttongroup_clear_changed_flag() to manually
    ///          reset at end of frame.
    /// @param group The button group.
    /// @return 1 if the selection just changed, 0 otherwise.
    int8_t rt_buttongroup_selection_changed(rt_buttongroup group);

    /// @brief Clear the selection-changed flag (call at end of frame).
    /// @param group The button group.
    void rt_buttongroup_clear_changed_flag(rt_buttongroup group);

    /// @brief Get the button ID at a specific index (for iteration).
    /// @param group The button group.
    /// @param index Zero-based index (0 to count-1).
    /// @return Button ID at @p index, or -1 if out of range.
    int64_t rt_buttongroup_get_at(rt_buttongroup group, int64_t index);

    /// @brief Select the next button in the group (wraps around).
    /// @param group The button group.
    /// @return The newly selected button ID, or -1 if the group is empty.
    int64_t rt_buttongroup_select_next(rt_buttongroup group);

    /// @brief Select the previous button in the group (wraps around).
    /// @param group The button group.
    /// @return The newly selected button ID, or -1 if the group is empty.
    int64_t rt_buttongroup_select_prev(rt_buttongroup group);

#ifdef __cplusplus
}
#endif

#endif // VIPER_RT_BUTTONGROUP_H
