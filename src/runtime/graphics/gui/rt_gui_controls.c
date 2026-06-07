//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_controls.c
// Purpose: Input-control GUI widgets — Dropdown, Slider, ProgressBar, and
//          ListBox (plus list items). Split out of rt_gui_widgets_complex.c;
//          shares GUI types via rt_gui_internal.h.
//
// Key invariants:
//   - Mirrors rt_gui_widgets_complex.c's VIPER_ENABLE_GRAPHICS guard: real
//     widgets when graphics is enabled, no-op stubs otherwise.
//   - Handles are validated via the rt_*_checked casts before use.
//
// Ownership/Lifetime:
//   - Widgets are owned by the GUI widget tree; this layer borrows them.
//
// Links: src/runtime/graphics/gui/rt_gui_widgets_complex.c (other complex widgets),
//        src/runtime/graphics/gui/rt_gui_internal.h (shared GUI types + API)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef VIPER_ENABLE_GRAPHICS

/// @brief Resolve a parent-container handle to its widget (file-local copy).
static vg_widget_t *rt_widget_parent_or_null_if_invalid(void *parent) {
    vg_widget_t *parent_widget = rt_gui_widget_parent_container_from_handle(parent);
    if (parent && !parent_widget)
        return NULL;
    return parent_widget;
}

static vg_dropdown_t *rt_dropdown_checked(void *handle) {
    return (vg_dropdown_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_DROPDOWN);
}

/// @brief Safe-cast a handle to a live Slider widget, or NULL.
static vg_slider_t *rt_slider_checked(void *handle) {
    return (vg_slider_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_SLIDER);
}

/// @brief Safe-cast a handle to a live ProgressBar widget, or NULL.
static vg_progressbar_t *rt_progressbar_checked(void *handle) {
    return (vg_progressbar_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_PROGRESS);
}

/// @brief Safe-cast a handle to a live ListBox widget, or NULL.
static vg_listbox_t *rt_listbox_checked(void *handle) {
    return (vg_listbox_t *)rt_gui_widget_handle_checked_type(handle, VG_WIDGET_LISTBOX);
}

//=============================================================================
// Dropdown Widget
//=============================================================================

/// @brief Create a dropdown (combo box) widget — a button that pops a list of choices.
void *rt_dropdown_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_widget_parent_or_null_if_invalid(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_dropdown_t *dropdown = vg_dropdown_create(parent_widget);
    rt_gui_apply_default_font((vg_widget_t *)dropdown);
    return dropdown;
}

/// @brief Add a selectable item to a dropdown list.
int64_t rt_dropdown_add_item(void *dropdown, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return -1;
    char *ctext = rt_string_to_gui_cstr(text);
    int64_t index = vg_dropdown_add_item(dd, ctext);
    free(ctext);
    return index;
}

/// @brief Remove an item from a dropdown by index.
void rt_dropdown_remove_item(void *dropdown, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (dd) {
        vg_dropdown_remove_item(dd, rt_gui_clamp_i64_to_i32(index, INT32_MIN, INT32_MAX));
    }
}

/// @brief Remove all items from a dropdown, leaving it empty.
void rt_dropdown_clear(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (dd) {
        vg_dropdown_clear(dd);
    }
}

/// @brief Programmatically select a dropdown item by index.
void rt_dropdown_set_selected(void *dropdown, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (dd) {
        vg_dropdown_set_selected(dd, rt_gui_clamp_i64_to_i32(index, INT32_MIN, INT32_MAX));
    }
}

/// @brief Get the index of the currently selected dropdown item (-1 if none).
int64_t rt_dropdown_get_selected(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return -1;
    return vg_dropdown_get_selected(dd);
}

/// @brief Get the selected text of the dropdown.
rt_string rt_dropdown_get_selected_text(void *dropdown) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return rt_str_empty();
    const char *text = vg_dropdown_get_selected_text(dd);
    if (!text)
        return rt_str_empty();
    return rt_string_from_bytes(text, strlen(text));
}

/// @brief Set the placeholder of the dropdown.
void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder) {
    RT_ASSERT_MAIN_THREAD();
    vg_dropdown_t *dd = rt_dropdown_checked(dropdown);
    if (!dd)
        return;
    char *ctext = rt_string_to_gui_cstr(placeholder);
    vg_dropdown_set_placeholder(dd, ctext);
    free(ctext);
}

//=============================================================================
// Slider Widget
//=============================================================================

/// @brief Create a slider widget for picking a numeric value within a range.
/// @param horizontal Non-zero for left-right slider, zero for top-bottom.
void *rt_slider_new(void *parent, int64_t horizontal) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_orientation_t orient = horizontal ? VG_SLIDER_HORIZONTAL : VG_SLIDER_VERTICAL;
    vg_widget_t *parent_widget = rt_widget_parent_or_null_if_invalid(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_slider_t *slider = vg_slider_create(parent_widget, orient);
    if (slider)
        rt_gui_apply_default_font((vg_widget_t *)slider);
    return slider;
}

/// @brief Set the value of the slider.
void rt_slider_set_value(void *slider, double value) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (sl) {
        if (!rt_gui_double_is_finite(value))
            return;
        vg_slider_set_value(sl, rt_gui_sanitize_signed_float(value, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

/// @brief Get the value of the slider.
double rt_slider_get_value(void *slider) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (!sl)
        return 0.0;
    return (double)vg_slider_get_value(sl);
}

/// @brief Set the range of the slider.
void rt_slider_set_range(void *slider, double min_val, double max_val) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (sl) {
        if (!rt_gui_double_is_finite(min_val) || !rt_gui_double_is_finite(max_val))
            return;
        if (min_val > max_val) {
            double tmp = min_val;
            min_val = max_val;
            max_val = tmp;
        }
        vg_slider_set_range(sl,
                            rt_gui_sanitize_signed_float(min_val, RT_GUI_MAX_LAYOUT_VALUE),
                            rt_gui_sanitize_signed_float(max_val, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

/// @brief Set the step of the slider.
void rt_slider_set_step(void *slider, double step) {
    RT_ASSERT_MAIN_THREAD();
    vg_slider_t *sl = rt_slider_checked(slider);
    if (sl) {
        vg_slider_set_step(sl, rt_gui_sanitize_nonnegative_float(step, RT_GUI_MAX_LAYOUT_VALUE));
    }
}

//=============================================================================
// ProgressBar Widget
//=============================================================================

/// @brief Create a horizontal progress bar (0.0–1.0 fill range).
void *rt_progressbar_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_widget_parent_or_null_if_invalid(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_progressbar_t *pb = vg_progressbar_create(parent_widget);
    if (pb)
        rt_gui_apply_default_font((vg_widget_t *)pb);
    return pb;
}

/// @brief Set the value of the progressbar.
void rt_progressbar_set_value(void *progress, double value) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (pb) {
        double sanitized = rt_gui_double_is_finite(value) ? value : 0.0;
        vg_progressbar_set_value(pb, (float)rt_gui_clamp_f64(sanitized, 0.0, 1.0));
    }
}

/// @brief Get the value of the progressbar.
double rt_progressbar_get_value(void *progress) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (!pb)
        return 0.0;
    return (double)vg_progressbar_get_value(pb);
}

/// @brief Set the visual style of a progress bar (bar, circle, or indeterminate).
/// @details Controls how progress is rendered: VG_PROGRESS_BAR for a standard
///          horizontal fill, VG_PROGRESS_INDETERMINATE for a looping animation
///          when the total duration is unknown. Out-of-range values default to bar.
/// @param progress Progress bar widget handle.
/// @param style    Style enum value (VG_PROGRESS_BAR, VG_PROGRESS_INDETERMINATE, etc.).
void rt_progressbar_set_style(void *progress, int64_t style) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (!pb)
        return;
    if (style < (int64_t)VG_PROGRESS_BAR || style > (int64_t)VG_PROGRESS_INDETERMINATE)
        style = (int64_t)VG_PROGRESS_BAR;
    vg_progressbar_set_style(pb, (vg_progress_style_t)style);
}

/// @brief Show or hide the percentage text label on a progress bar.
/// @param progress Progress bar widget handle.
/// @param show     Non-zero to render the "XX%" label, zero to hide it.
void rt_progressbar_show_percentage(void *progress, int64_t show) {
    RT_ASSERT_MAIN_THREAD();
    vg_progressbar_t *pb = rt_progressbar_checked(progress);
    if (pb)
        vg_progressbar_show_percentage(pb, show != 0);
}

//=============================================================================
// ListBox Widget
//=============================================================================

/// @brief Create an empty scrollable list-box widget.
void *rt_listbox_new(void *parent) {
    RT_ASSERT_MAIN_THREAD();
    vg_widget_t *parent_widget = rt_widget_parent_or_null_if_invalid(parent);
    if (parent && !parent_widget)
        return NULL;
    vg_listbox_t *listbox = vg_listbox_create(parent_widget);
    rt_gui_apply_default_font((vg_widget_t *)listbox);
    return listbox;
}

/// @brief Append `text` as a new list-box item; returns the new item handle.
void *rt_listbox_add_item(void *listbox, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return NULL;
    char *ctext = rt_string_to_gui_cstr(text);
    vg_listbox_item_t *item = vg_listbox_add_item(lb, ctext, NULL);
    free(ctext);
    return rt_gui_wrap_listbox_item(item);
}

/// @brief Remove an item from the listbox.
void rt_listbox_remove_item(void *listbox, void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    vg_listbox_item_t *it = item ? rt_gui_listbox_item_from_handle(item) : NULL;
    if (lb && it && vg_listbox_item_is_live(it) && it->owner == lb)
        vg_listbox_remove_item(lb, it);
}

/// @brief Remove all entries from the listbox.
void rt_listbox_clear(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb) {
        vg_listbox_clear(lb);
    }
}

/// @brief Programmatically select a listbox item by handle.
void rt_listbox_select(void *listbox, void *item) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    vg_listbox_item_t *it = item ? rt_gui_listbox_item_from_handle(item) : NULL;
    if (!lb)
        return;
    if (item && (!it || it->owner != lb))
        return;
    vg_listbox_select(lb, it);
}

/// @brief Return the currently-selected listbox item handle (NULL when none).
void *rt_listbox_get_selected(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return NULL;
    return rt_gui_wrap_listbox_item(vg_listbox_get_selected(lb));
}

/// @brief Get the number of items in the listbox.
int64_t rt_listbox_get_count(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return 0;
    size_t count = lb->virtual_mode ? lb->total_item_count : (size_t)lb->item_count;
    return count > (size_t)INT64_MAX ? INT64_MAX : (int64_t)count;
}

/// @brief Get the selected index of the listbox.
int64_t rt_listbox_get_selected_index(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return -1;
    size_t idx = vg_listbox_get_selected_index(lb);
    if (idx == (size_t)-1)
        return -1;
    if (idx > (size_t)INT64_MAX)
        return -1;
    return (int64_t)idx;
}

/// @brief Select a listbox item by its zero-based index.
void rt_listbox_select_index(void *listbox, int64_t index) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb || index < 0)
        return;
    if ((uintmax_t)index > (uintmax_t)SIZE_MAX)
        return;
    size_t idx = (size_t)index;
    size_t count = lb->virtual_mode ? lb->total_item_count : lb->item_count;
    if (idx >= count)
        return;
    vg_listbox_select_index(lb, idx);
}

/// @brief Scroll to the first listbox row without changing selection.
void rt_listbox_scroll_to_top(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb)
        vg_listbox_scroll_to_top(lb);
}

/// @brief Scroll to the last listbox row without changing selection.
void rt_listbox_scroll_to_bottom(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb)
        vg_listbox_scroll_to_bottom(lb);
}

/// @brief Enable or disable Ctrl/Shift multi-row selection.
void rt_listbox_set_multi_select(void *listbox, int64_t enabled) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return;

    const bool want_multi = enabled != 0;
    if (lb->multi_select == want_multi)
        return;

    if (want_multi) {
        lb->multi_select = true;
        return;
    }

    lb->multi_select = false;
    if (lb->virtual_mode) {
        size_t keep = vg_listbox_get_selected_index(lb);
        bool changed = false;
        if (lb->selection_bitmap) {
            for (size_t i = 0; i < lb->selection_bitmap_size; ++i) {
                if (lb->selection_bitmap[i])
                    changed = true;
                lb->selection_bitmap[i] = false;
            }
        }
        lb->selected_index = SIZE_MAX;
        lb->anchor_selected_index = SIZE_MAX;
        if (keep != SIZE_MAX && keep < lb->total_item_count) {
            vg_listbox_select_index(lb, keep);
            return;
        }
        if (changed) {
            lb->selection_revision++;
            lb->base.needs_paint = true;
        }
        return;
    }

    vg_listbox_item_t *keep = lb->selected;
    if (!keep) {
        for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
            if (item->selected) {
                keep = item;
                break;
            }
        }
    }
    vg_listbox_select(lb, keep);
}

static bool rt_listbox_append_selected_text(char **buffer,
                                            size_t *length,
                                            size_t *capacity,
                                            bool *first,
                                            const char *text,
                                            size_t text_len) {
    if (!buffer || !length || !capacity || !first)
        return false;
    size_t prefix = *first ? 0u : 1u;
    if (text_len > SIZE_MAX - *length - prefix)
        return false;
    size_t needed = *length + prefix + text_len;
    if (needed + 1 > *capacity) {
        size_t next_cap = *capacity ? *capacity : 64u;
        while (next_cap < needed + 1) {
            if (next_cap > SIZE_MAX / 2) {
                next_cap = needed + 1;
                break;
            }
            next_cap *= 2u;
        }
        char *next = (char *)realloc(*buffer, next_cap);
        if (!next)
            return false;
        *buffer = next;
        *capacity = next_cap;
    }
    if (!*first)
        (*buffer)[(*length)++] = '\n';
    if (text_len > 0 && text)
        memcpy(*buffer + *length, text, text_len);
    *length += text_len;
    (*buffer)[*length] = '\0';
    *first = false;
    return true;
}

/// @brief Return selected row text joined by newlines.
rt_string rt_listbox_get_selected_text(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return rt_str_empty();

    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    bool first = true;

    if (lb->virtual_mode) {
        if (lb->selection_bitmap && lb->data_provider) {
            for (size_t i = 0; i < lb->total_item_count && i < lb->selection_bitmap_size; ++i) {
                if (!lb->selection_bitmap[i])
                    continue;
                const char *text = "";
                lb->data_provider(&lb->base, i, &text, NULL, lb->data_provider_user_data);
                if (!text)
                    text = "";
                if (!rt_listbox_append_selected_text(
                        &buffer, &length, &capacity, &first, text, strlen(text))) {
                    free(buffer);
                    return rt_str_empty();
                }
            }
        }
    } else {
        for (vg_listbox_item_t *item = lb->first_item; item; item = item->next) {
            if (!item->selected)
                continue;
            if (!rt_listbox_append_selected_text(&buffer,
                                                 &length,
                                                 &capacity,
                                                 &first,
                                                 item->text ? item->text : "",
                                                 item->text ? item->text_len : 0u)) {
                free(buffer);
                return rt_str_empty();
            }
        }
    }

    if (first) {
        free(buffer);
        return rt_str_empty();
    }
    rt_string result = rt_string_from_bytes(buffer ? buffer : "", length);
    free(buffer);
    return result;
}

/// @brief Check if the listbox selection changed since the last call (edge-triggered).
int64_t rt_listbox_was_selection_changed(void *listbox) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (!lb)
        return 0;

    if (lb->selection_revision != lb->reported_selection_revision) {
        lb->reported_selection_revision = lb->selection_revision;
        lb->prev_selected = lb->selected;
        lb->prev_selected_index = lb->selected_index;
        return 1;
    }
    return 0;
}

/// @brief Get the display text of a listbox item.
rt_string rt_listbox_item_get_text(void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_listbox_item_t *it = rt_gui_listbox_item_from_handle(item);
    if (!it)
        return rt_str_empty();
    if (it->text)
        return rt_string_from_bytes(it->text, it->text_len);
    return rt_str_empty();
}

/// @brief Update the display text of a listbox item.
void rt_listbox_item_set_text(void *item, rt_string text) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = rt_gui_listbox_item_from_handle(item);
    if (!it)
        return;
    char *ctext = rt_string_to_gui_cstr(text);
    if (!ctext)
        return;
    free(it->text);
    it->text = ctext; // Takes ownership
    it->text_len = strlen(ctext);
    if (it->owner) {
        it->owner->base.needs_layout = true;
        it->owner->base.needs_paint = true;
    }
}

/// @brief Attach arbitrary string data to a listbox item (replaces previous data).
void rt_listbox_item_set_data(void *item, rt_string data) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = rt_gui_listbox_item_from_handle(item);
    if (!it)
        return;
    rt_gui_string_data_t *new_data = data ? rt_gui_string_data_new(data) : NULL;
    if (data && !new_data)
        return;
    if (it->owns_user_data && it->user_data)
        rt_gui_string_data_free_if_owned(it->user_data);
    it->user_data = new_data;
    it->owns_user_data = new_data != NULL;
}

/// @brief Retrieve the string data previously attached to a listbox item.
rt_string rt_listbox_item_get_data(void *item) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return rt_str_empty();
    vg_listbox_item_t *it = rt_gui_listbox_item_from_handle(item);
    if (!it)
        return rt_str_empty();
    if (!it->user_data || !it->owns_user_data)
        return rt_str_empty();
    return rt_gui_string_data_to_rt_string(it->user_data);
}

/// @brief Override a listbox item's text color.
void rt_listbox_item_set_text_color(void *item, int64_t color) {
    RT_ASSERT_MAIN_THREAD();
    if (!item)
        return;
    vg_listbox_item_t *it = rt_gui_listbox_item_from_handle(item);
    if (!it)
        return;
    vg_listbox_item_set_text_color(it, (uint32_t)color);
}

/// @brief Set the font of the listbox.
void rt_listbox_set_font(void *listbox, void *font, double size) {
    RT_ASSERT_MAIN_THREAD();
    vg_listbox_t *lb = rt_listbox_checked(listbox);
    if (lb) {
        vg_font_t *checked_font = rt_gui_font_handle_checked(font);
        if (!checked_font)
            return;
        vg_listbox_set_font(lb, checked_font, (float)rt_gui_sanitize_font_size(size, 14.0));
    }
}

//=============================================================================

#else /* !VIPER_ENABLE_GRAPHICS */

/// @brief Stub: graphics disabled — returns NULL; no dropdown widget is created.
void *rt_dropdown_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Add a selectable item to a dropdown list.
int64_t rt_dropdown_add_item(void *dropdown, rt_string text) {
    (void)dropdown;
    (void)text;
    return -1;
}

/// @brief Remove an item from a dropdown by index.
void rt_dropdown_remove_item(void *dropdown, int64_t index) {
    (void)dropdown;
    (void)index;
}

/// @brief Remove all items from a dropdown, leaving it empty.
void rt_dropdown_clear(void *dropdown) {
    (void)dropdown;
}

/// @brief Programmatically select a dropdown item by index.
void rt_dropdown_set_selected(void *dropdown, int64_t index) {
    (void)dropdown;
    (void)index;
}

/// @brief Get the index of the currently selected dropdown item (-1 if none).
int64_t rt_dropdown_get_selected(void *dropdown) {
    (void)dropdown;
    return -1;
}

/// @brief Get the selected text of the dropdown.
rt_string rt_dropdown_get_selected_text(void *dropdown) {
    (void)dropdown;
    return rt_str_empty();
}

/// @brief Set the placeholder of the dropdown.
void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder) {
    (void)dropdown;
    (void)placeholder;
}

/// @brief Stub: graphics disabled — returns NULL; no slider widget is created.
void *rt_slider_new(void *parent, int64_t horizontal) {
    (void)parent;
    (void)horizontal;
    return NULL;
}

/// @brief Set the value of the slider.
void rt_slider_set_value(void *slider, double value) {
    (void)slider;
    (void)value;
}

/// @brief Get the value of the slider.
double rt_slider_get_value(void *slider) {
    (void)slider;
    return 0.0;
}

/// @brief Set the range of the slider.
void rt_slider_set_range(void *slider, double min_val, double max_val) {
    (void)slider;
    (void)min_val;
    (void)max_val;
}

/// @brief Set the step of the slider.
void rt_slider_set_step(void *slider, double step) {
    (void)slider;
    (void)step;
}

/// @brief Stub: graphics disabled — returns NULL; no progress bar widget is created.
void *rt_progressbar_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Set the value of the progressbar.
void rt_progressbar_set_value(void *progress, double value) {
    (void)progress;
    (void)value;
}

/// @brief Get the value of the progressbar.
double rt_progressbar_get_value(void *progress) {
    (void)progress;
    return 0.0;
}

/// @brief Stub: graphics disabled — returns NULL; no list box widget is created.
void *rt_listbox_new(void *parent) {
    (void)parent;
    return NULL;
}

/// @brief Stub: graphics disabled — returns NULL; no list item is added.
void *rt_listbox_add_item(void *listbox, rt_string text) {
    (void)listbox;
    (void)text;
    return NULL;
}

/// @brief Remove an item from the listbox.
void rt_listbox_remove_item(void *listbox, void *item) {
    (void)listbox;
    (void)item;
}

/// @brief Remove all entries from the listbox.
void rt_listbox_clear(void *listbox) {
    (void)listbox;
}

/// @brief Programmatically select a listbox item by handle.
void rt_listbox_select(void *listbox, void *item) {
    (void)listbox;
    (void)item;
}

/// @brief Stub: graphics disabled — returns NULL; no selection exists without a list box.
void *rt_listbox_get_selected(void *listbox) {
    (void)listbox;
    return NULL;
}

/// @brief Get the number of items in the listbox.
int64_t rt_listbox_get_count(void *listbox) {
    (void)listbox;
    return 0;
}

/// @brief Get the selected index of the listbox.
int64_t rt_listbox_get_selected_index(void *listbox) {
    (void)listbox;
    return -1;
}

/// @brief Select a listbox item by its zero-based index.
void rt_listbox_select_index(void *listbox, int64_t index) {
    (void)listbox;
    (void)index;
}

/// @brief Stub: graphics disabled — no listbox scroll state exists.
void rt_listbox_scroll_to_top(void *listbox) {
    (void)listbox;
}

/// @brief Stub: graphics disabled — no listbox scroll state exists.
void rt_listbox_scroll_to_bottom(void *listbox) {
    (void)listbox;
}

/// @brief Stub: graphics disabled — no listbox selection exists.
void rt_listbox_set_multi_select(void *listbox, int64_t enabled) {
    (void)listbox;
    (void)enabled;
}

/// @brief Stub: graphics disabled — no selected row text exists.
rt_string rt_listbox_get_selected_text(void *listbox) {
    (void)listbox;
    return rt_str_empty();
}

/// @brief Check if the listbox selection changed since the last call (edge-triggered).
int64_t rt_listbox_was_selection_changed(void *listbox) {
    (void)listbox;
    return 0;
}

/// @brief Get the display text of a listbox item.
rt_string rt_listbox_item_get_text(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Update the display text of a listbox item.
void rt_listbox_item_set_text(void *item, rt_string text) {
    (void)item;
    (void)text;
}

/// @brief Attach arbitrary string data to a listbox item (replaces previous data).
void rt_listbox_item_set_data(void *item, rt_string data) {
    (void)item;
    (void)data;
}

/// @brief Retrieve the string data previously attached to a listbox item.
rt_string rt_listbox_item_get_data(void *item) {
    (void)item;
    return rt_str_empty();
}

/// @brief Stub: graphics disabled — no listbox item exists.
void rt_listbox_item_set_text_color(void *item, int64_t color) {
    (void)item;
    (void)color;
}

/// @brief Set the font of the listbox.
void rt_listbox_set_font(void *listbox, void *font, double size) {
    (void)listbox;
    (void)font;
    (void)size;
}


#endif /* VIPER_ENABLE_GRAPHICS */
