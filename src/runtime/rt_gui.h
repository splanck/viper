//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_gui.h
// Purpose: Runtime bridge functions for ViperGUI widget library.
// Key invariants: All widget pointers are opaque handles.
// Ownership/Lifetime: Widgets must be destroyed with their destroy functions.
// Links: src/lib/gui/include/vg_widget.h, src/lib/gui/include/vg_widgets.h
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#include "rt_string.h"

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // GUI Application
    //=========================================================================

    /// @brief Create a new GUI application with window.
    /// @param title Window title.
    /// @param width Window width in pixels.
    /// @param height Window height in pixels.
    /// @return GUI application handle, or NULL on failure.
    void* rt_gui_app_new(rt_string title, int64_t width, int64_t height);

    /// @brief Destroy a GUI application and free resources.
    /// @param app GUI application handle.
    void rt_gui_app_destroy(void* app);

    /// @brief Check if application should close.
    /// @param app GUI application handle.
    /// @return 1 if should close, 0 otherwise.
    int64_t rt_gui_app_should_close(void* app);

    /// @brief Poll and process events, update widget states.
    /// @param app GUI application handle.
    void rt_gui_app_poll(void* app);

    /// @brief Render all widgets to the window.
    /// @param app GUI application handle.
    void rt_gui_app_render(void* app);

    /// @brief Get the root widget container.
    /// @param app GUI application handle.
    /// @return Root widget handle.
    void* rt_gui_app_get_root(void* app);

    /// @brief Set default font for all widgets.
    /// @param app GUI application handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_gui_app_set_font(void* app, void* font, double size);

    //=========================================================================
    // Font Functions
    //=========================================================================

    /// @brief Load a font from a file.
    /// @param path Path to TTF font file.
    /// @return Opaque font handle, or NULL on failure.
    void* rt_font_load(rt_string path);

    /// @brief Destroy a font and free resources.
    /// @param font Font handle.
    void rt_font_destroy(void* font);

    //=========================================================================
    // Widget Functions
    //=========================================================================

    /// @brief Destroy a widget and all its children.
    /// @param widget Widget handle.
    void rt_widget_destroy(void* widget);

    /// @brief Set widget visibility.
    /// @param widget Widget handle.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_widget_set_visible(void* widget, int64_t visible);

    /// @brief Set widget enabled state.
    /// @param widget Widget handle.
    /// @param enabled 1 for enabled, 0 for disabled.
    void rt_widget_set_enabled(void* widget, int64_t enabled);

    /// @brief Set widget fixed size.
    /// @param widget Widget handle.
    /// @param width Width in pixels.
    /// @param height Height in pixels.
    void rt_widget_set_size(void* widget, int64_t width, int64_t height);

    /// @brief Add a child widget to a parent.
    /// @param parent Parent widget handle.
    /// @param child Child widget handle.
    void rt_widget_add_child(void* parent, void* child);

    /// @brief Check if widget is hovered.
    /// @param widget Widget handle.
    /// @return 1 if hovered, 0 otherwise.
    int64_t rt_widget_is_hovered(void* widget);

    /// @brief Check if widget is pressed.
    /// @param widget Widget handle.
    /// @return 1 if pressed, 0 otherwise.
    int64_t rt_widget_is_pressed(void* widget);

    /// @brief Check if widget is focused.
    /// @param widget Widget handle.
    /// @return 1 if focused, 0 otherwise.
    int64_t rt_widget_is_focused(void* widget);

    /// @brief Check if widget was clicked this frame.
    /// @param widget Widget handle.
    /// @return 1 if clicked, 0 otherwise.
    int64_t rt_widget_was_clicked(void* widget);

    /// @brief Set widget position.
    /// @param widget Widget handle.
    /// @param x X position in pixels.
    /// @param y Y position in pixels.
    void rt_widget_set_position(void* widget, int64_t x, int64_t y);

    //=========================================================================
    // Label Widget
    //=========================================================================

    /// @brief Create a new label widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Label text.
    /// @return Label widget handle.
    void* rt_label_new(void* parent, rt_string text);

    /// @brief Set label text.
    /// @param label Label widget handle.
    /// @param text New text.
    void rt_label_set_text(void* label, rt_string text);

    /// @brief Set label font.
    /// @param label Label widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_label_set_font(void* label, void* font, double size);

    /// @brief Set label text color.
    /// @param label Label widget handle.
    /// @param color ARGB color (0xAARRGGBB).
    void rt_label_set_color(void* label, int64_t color);

    //=========================================================================
    // Button Widget
    //=========================================================================

    /// @brief Create a new button widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Button text.
    /// @return Button widget handle.
    void* rt_button_new(void* parent, rt_string text);

    /// @brief Set button text.
    /// @param button Button widget handle.
    /// @param text New text.
    void rt_button_set_text(void* button, rt_string text);

    /// @brief Set button font.
    /// @param button Button widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_button_set_font(void* button, void* font, double size);

    /// @brief Set button style.
    /// @param button Button widget handle.
    /// @param style Button style (0=default, 1=primary, 2=secondary, 3=danger, 4=text).
    void rt_button_set_style(void* button, int64_t style);

    //=========================================================================
    // TextInput Widget
    //=========================================================================

    /// @brief Create a new text input widget.
    /// @param parent Parent widget (can be NULL).
    /// @return TextInput widget handle.
    void* rt_textinput_new(void* parent);

    /// @brief Set text input content.
    /// @param input TextInput widget handle.
    /// @param text New text.
    void rt_textinput_set_text(void* input, rt_string text);

    /// @brief Get text input content.
    /// @param input TextInput widget handle.
    /// @return Current text as runtime string.
    rt_string rt_textinput_get_text(void* input);

    /// @brief Set placeholder text.
    /// @param input TextInput widget handle.
    /// @param placeholder Placeholder text.
    void rt_textinput_set_placeholder(void* input, rt_string placeholder);

    /// @brief Set text input font.
    /// @param input TextInput widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_textinput_set_font(void* input, void* font, double size);

    //=========================================================================
    // Checkbox Widget
    //=========================================================================

    /// @brief Create a new checkbox widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Checkbox label text.
    /// @return Checkbox widget handle.
    void* rt_checkbox_new(void* parent, rt_string text);

    /// @brief Set checkbox checked state.
    /// @param checkbox Checkbox widget handle.
    /// @param checked 1 for checked, 0 for unchecked.
    void rt_checkbox_set_checked(void* checkbox, int64_t checked);

    /// @brief Get checkbox checked state.
    /// @param checkbox Checkbox widget handle.
    /// @return 1 if checked, 0 if not.
    int64_t rt_checkbox_is_checked(void* checkbox);

    /// @brief Set checkbox text.
    /// @param checkbox Checkbox widget handle.
    /// @param text New text.
    void rt_checkbox_set_text(void* checkbox, rt_string text);

    //=========================================================================
    // ScrollView Widget
    //=========================================================================

    /// @brief Create a new scroll view widget.
    /// @param parent Parent widget (can be NULL).
    /// @return ScrollView widget handle.
    void* rt_scrollview_new(void* parent);

    /// @brief Set scroll position.
    /// @param scroll ScrollView widget handle.
    /// @param x Horizontal scroll position.
    /// @param y Vertical scroll position.
    void rt_scrollview_set_scroll(void* scroll, double x, double y);

    /// @brief Set content size.
    /// @param scroll ScrollView widget handle.
    /// @param width Content width (0 = auto).
    /// @param height Content height (0 = auto).
    void rt_scrollview_set_content_size(void* scroll, double width, double height);

    //=========================================================================
    // TreeView Widget
    //=========================================================================

    /// @brief Create a new tree view widget.
    /// @param parent Parent widget (can be NULL).
    /// @return TreeView widget handle.
    void* rt_treeview_new(void* parent);

    /// @brief Add a node to the tree view.
    /// @param tree TreeView widget handle.
    /// @param parent_node Parent node handle (NULL for root).
    /// @param text Node text.
    /// @return New node handle.
    void* rt_treeview_add_node(void* tree, void* parent_node, rt_string text);

    /// @brief Remove a node from the tree view.
    /// @param tree TreeView widget handle.
    /// @param node Node handle to remove.
    void rt_treeview_remove_node(void* tree, void* node);

    /// @brief Clear all nodes from tree view.
    /// @param tree TreeView widget handle.
    void rt_treeview_clear(void* tree);

    /// @brief Expand a tree node.
    /// @param tree TreeView widget handle.
    /// @param node Node handle.
    void rt_treeview_expand(void* tree, void* node);

    /// @brief Collapse a tree node.
    /// @param tree TreeView widget handle.
    /// @param node Node handle.
    void rt_treeview_collapse(void* tree, void* node);

    /// @brief Select a tree node.
    /// @param tree TreeView widget handle.
    /// @param node Node handle.
    void rt_treeview_select(void* tree, void* node);

    /// @brief Set tree view font.
    /// @param tree TreeView widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_treeview_set_font(void* tree, void* font, double size);

    //=========================================================================
    // TabBar Widget
    //=========================================================================

    /// @brief Create a new tab bar widget.
    /// @param parent Parent widget (can be NULL).
    /// @return TabBar widget handle.
    void* rt_tabbar_new(void* parent);

    /// @brief Add a tab to the tab bar.
    /// @param tabbar TabBar widget handle.
    /// @param title Tab title.
    /// @param closable 1 if tab can be closed, 0 otherwise.
    /// @return Tab handle.
    void* rt_tabbar_add_tab(void* tabbar, rt_string title, int64_t closable);

    /// @brief Remove a tab from the tab bar.
    /// @param tabbar TabBar widget handle.
    /// @param tab Tab handle.
    void rt_tabbar_remove_tab(void* tabbar, void* tab);

    /// @brief Set the active tab.
    /// @param tabbar TabBar widget handle.
    /// @param tab Tab handle.
    void rt_tabbar_set_active(void* tabbar, void* tab);

    /// @brief Set tab title.
    /// @param tab Tab handle.
    /// @param title New title.
    void rt_tab_set_title(void* tab, rt_string title);

    /// @brief Set tab modified state.
    /// @param tab Tab handle.
    /// @param modified 1 for modified, 0 for not modified.
    void rt_tab_set_modified(void* tab, int64_t modified);

    //=========================================================================
    // SplitPane Widget
    //=========================================================================

    /// @brief Create a new split pane widget.
    /// @param parent Parent widget (can be NULL).
    /// @param horizontal 1 for horizontal split (left/right), 0 for vertical (top/bottom).
    /// @return SplitPane widget handle.
    void* rt_splitpane_new(void* parent, int64_t horizontal);

    /// @brief Set split position.
    /// @param split SplitPane widget handle.
    /// @param position Split position (0.0 to 1.0).
    void rt_splitpane_set_position(void* split, double position);

    /// @brief Get the first pane.
    /// @param split SplitPane widget handle.
    /// @return First pane widget handle.
    void* rt_splitpane_get_first(void* split);

    /// @brief Get the second pane.
    /// @param split SplitPane widget handle.
    /// @return Second pane widget handle.
    void* rt_splitpane_get_second(void* split);

    //=========================================================================
    // CodeEditor Widget
    //=========================================================================

    /// @brief Create a new code editor widget.
    /// @param parent Parent widget (can be NULL).
    /// @return CodeEditor widget handle.
    void* rt_codeeditor_new(void* parent);

    /// @brief Set code editor text content.
    /// @param editor CodeEditor widget handle.
    /// @param text New text content.
    void rt_codeeditor_set_text(void* editor, rt_string text);

    /// @brief Get code editor text content.
    /// @param editor CodeEditor widget handle.
    /// @return Text content as runtime string.
    rt_string rt_codeeditor_get_text(void* editor);

    /// @brief Set cursor position.
    /// @param editor CodeEditor widget handle.
    /// @param line Line number (0-based).
    /// @param col Column number (0-based).
    void rt_codeeditor_set_cursor(void* editor, int64_t line, int64_t col);

    /// @brief Scroll to a specific line.
    /// @param editor CodeEditor widget handle.
    /// @param line Line number (0-based).
    void rt_codeeditor_scroll_to_line(void* editor, int64_t line);

    /// @brief Get line count.
    /// @param editor CodeEditor widget handle.
    /// @return Number of lines.
    int64_t rt_codeeditor_get_line_count(void* editor);

    /// @brief Check if editor content is modified.
    /// @param editor CodeEditor widget handle.
    /// @return 1 if modified, 0 if not.
    int64_t rt_codeeditor_is_modified(void* editor);

    /// @brief Clear modified flag.
    /// @param editor CodeEditor widget handle.
    void rt_codeeditor_clear_modified(void* editor);

    /// @brief Set code editor font.
    /// @param editor CodeEditor widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_codeeditor_set_font(void* editor, void* font, double size);

    //=========================================================================
    // Dropdown Widget
    //=========================================================================

    /// @brief Create a new dropdown widget.
    /// @param parent Parent widget (can be NULL).
    /// @return Dropdown widget handle.
    void* rt_dropdown_new(void* parent);

    /// @brief Add an item to the dropdown.
    /// @param dropdown Dropdown widget handle.
    /// @param text Item text.
    /// @return Index of the added item.
    int64_t rt_dropdown_add_item(void* dropdown, rt_string text);

    /// @brief Remove an item from the dropdown.
    /// @param dropdown Dropdown widget handle.
    /// @param index Item index.
    void rt_dropdown_remove_item(void* dropdown, int64_t index);

    /// @brief Clear all items from the dropdown.
    /// @param dropdown Dropdown widget handle.
    void rt_dropdown_clear(void* dropdown);

    /// @brief Set selected item.
    /// @param dropdown Dropdown widget handle.
    /// @param index Item index (-1 for none).
    void rt_dropdown_set_selected(void* dropdown, int64_t index);

    /// @brief Get selected item index.
    /// @param dropdown Dropdown widget handle.
    /// @return Selected index, or -1 if none.
    int64_t rt_dropdown_get_selected(void* dropdown);

    /// @brief Get selected item text.
    /// @param dropdown Dropdown widget handle.
    /// @return Selected text, or empty string if none.
    rt_string rt_dropdown_get_selected_text(void* dropdown);

    /// @brief Set dropdown placeholder text.
    /// @param dropdown Dropdown widget handle.
    /// @param placeholder Placeholder text.
    void rt_dropdown_set_placeholder(void* dropdown, rt_string placeholder);

    //=========================================================================
    // Slider Widget
    //=========================================================================

    /// @brief Create a new slider widget.
    /// @param parent Parent widget (can be NULL).
    /// @param horizontal 1 for horizontal, 0 for vertical.
    /// @return Slider widget handle.
    void* rt_slider_new(void* parent, int64_t horizontal);

    /// @brief Set slider value.
    /// @param slider Slider widget handle.
    /// @param value Slider value.
    void rt_slider_set_value(void* slider, double value);

    /// @brief Get slider value.
    /// @param slider Slider widget handle.
    /// @return Current value.
    double rt_slider_get_value(void* slider);

    /// @brief Set slider range.
    /// @param slider Slider widget handle.
    /// @param min_val Minimum value.
    /// @param max_val Maximum value.
    void rt_slider_set_range(void* slider, double min_val, double max_val);

    /// @brief Set slider step.
    /// @param slider Slider widget handle.
    /// @param step Step value (0 for continuous).
    void rt_slider_set_step(void* slider, double step);

    //=========================================================================
    // ProgressBar Widget
    //=========================================================================

    /// @brief Create a new progress bar widget.
    /// @param parent Parent widget (can be NULL).
    /// @return ProgressBar widget handle.
    void* rt_progressbar_new(void* parent);

    /// @brief Set progress bar value.
    /// @param progress ProgressBar widget handle.
    /// @param value Progress value (0.0 to 1.0).
    void rt_progressbar_set_value(void* progress, double value);

    /// @brief Get progress bar value.
    /// @param progress ProgressBar widget handle.
    /// @return Current value (0.0 to 1.0).
    double rt_progressbar_get_value(void* progress);

    //=========================================================================
    // ListBox Widget
    //=========================================================================

    /// @brief Create a new list box widget.
    /// @param parent Parent widget (can be NULL).
    /// @return ListBox widget handle.
    void* rt_listbox_new(void* parent);

    /// @brief Add an item to the list box.
    /// @param listbox ListBox widget handle.
    /// @param text Item text.
    /// @return Item handle.
    void* rt_listbox_add_item(void* listbox, rt_string text);

    /// @brief Remove an item from the list box.
    /// @param listbox ListBox widget handle.
    /// @param item Item handle.
    void rt_listbox_remove_item(void* listbox, void* item);

    /// @brief Clear all items from the list box.
    /// @param listbox ListBox widget handle.
    void rt_listbox_clear(void* listbox);

    /// @brief Select an item.
    /// @param listbox ListBox widget handle.
    /// @param item Item handle (NULL to deselect).
    void rt_listbox_select(void* listbox, void* item);

    /// @brief Get selected item.
    /// @param listbox ListBox widget handle.
    /// @return Selected item handle, or NULL if none.
    void* rt_listbox_get_selected(void* listbox);

    //=========================================================================
    // RadioButton Widget
    //=========================================================================

    /// @brief Create a new radio group.
    /// @return RadioGroup handle.
    void* rt_radiogroup_new(void);

    /// @brief Destroy a radio group.
    /// @param group RadioGroup handle.
    void rt_radiogroup_destroy(void* group);

    /// @brief Create a new radio button widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Radio button text.
    /// @param group RadioGroup handle.
    /// @return RadioButton widget handle.
    void* rt_radiobutton_new(void* parent, rt_string text, void* group);

    /// @brief Check if radio button is selected.
    /// @param radio RadioButton widget handle.
    /// @return 1 if selected, 0 otherwise.
    int64_t rt_radiobutton_is_selected(void* radio);

    /// @brief Set radio button selected state.
    /// @param radio RadioButton widget handle.
    /// @param selected 1 for selected, 0 for not selected.
    void rt_radiobutton_set_selected(void* radio, int64_t selected);

    //=========================================================================
    // Spinner Widget
    //=========================================================================

    /// @brief Create a new spinner widget.
    /// @param parent Parent widget (can be NULL).
    /// @return Spinner widget handle.
    void* rt_spinner_new(void* parent);

    /// @brief Set spinner value.
    /// @param spinner Spinner widget handle.
    /// @param value Spinner value.
    void rt_spinner_set_value(void* spinner, double value);

    /// @brief Get spinner value.
    /// @param spinner Spinner widget handle.
    /// @return Current value.
    double rt_spinner_get_value(void* spinner);

    /// @brief Set spinner range.
    /// @param spinner Spinner widget handle.
    /// @param min_val Minimum value.
    /// @param max_val Maximum value.
    void rt_spinner_set_range(void* spinner, double min_val, double max_val);

    /// @brief Set spinner step.
    /// @param spinner Spinner widget handle.
    /// @param step Step value.
    void rt_spinner_set_step(void* spinner, double step);

    /// @brief Set spinner decimal places.
    /// @param spinner Spinner widget handle.
    /// @param decimals Number of decimal places.
    void rt_spinner_set_decimals(void* spinner, int64_t decimals);

    //=========================================================================
    // Image Widget
    //=========================================================================

    /// @brief Create a new image widget.
    /// @param parent Parent widget (can be NULL).
    /// @return Image widget handle.
    void* rt_image_new(void* parent);

    /// @brief Set image pixels.
    /// @param image Image widget handle.
    /// @param pixels Pixel data (RGBA format).
    /// @param width Image width.
    /// @param height Image height.
    void rt_image_set_pixels(void* image, void* pixels, int64_t width, int64_t height);

    /// @brief Clear image.
    /// @param image Image widget handle.
    void rt_image_clear(void* image);

    /// @brief Set image scale mode.
    /// @param image Image widget handle.
    /// @param mode Scale mode (0=none, 1=fit, 2=fill, 3=stretch).
    void rt_image_set_scale_mode(void* image, int64_t mode);

    /// @brief Set image opacity.
    /// @param image Image widget handle.
    /// @param opacity Opacity (0.0 to 1.0).
    void rt_image_set_opacity(void* image, double opacity);

    //=========================================================================
    // Theme Functions
    //=========================================================================

    /// @brief Set the current theme to dark.
    void rt_theme_set_dark(void);

    /// @brief Set the current theme to light.
    void rt_theme_set_light(void);

    //=========================================================================
    // Layout Functions
    //=========================================================================

    /// @brief Create a container with vertical box layout.
    /// @return VBox container widget handle.
    void* rt_vbox_new(void);

    /// @brief Create a container with horizontal box layout.
    /// @return HBox container widget handle.
    void* rt_hbox_new(void);

    /// @brief Set spacing for a layout container.
    /// @param container Container widget handle.
    /// @param spacing Spacing in pixels.
    void rt_container_set_spacing(void* container, double spacing);

    /// @brief Set padding for a layout container.
    /// @param container Container widget handle.
    /// @param padding Padding in pixels.
    void rt_container_set_padding(void* container, double padding);

#ifdef __cplusplus
}
#endif
