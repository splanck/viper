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
    void *rt_gui_app_new(rt_string title, int64_t width, int64_t height);

    /// @brief Destroy a GUI application and free resources.
    /// @param app GUI application handle.
    void rt_gui_app_destroy(void *app);

    /// @brief Check if application should close.
    /// @param app GUI application handle.
    /// @return 1 if should close, 0 otherwise.
    int64_t rt_gui_app_should_close(void *app);

    /// @brief Poll and process events, update widget states.
    /// @param app GUI application handle.
    void rt_gui_app_poll(void *app);

    /// @brief Render all widgets to the window.
    /// @param app GUI application handle.
    void rt_gui_app_render(void *app);

    /// @brief Get the root widget container.
    /// @param app GUI application handle.
    /// @return Root widget handle.
    void *rt_gui_app_get_root(void *app);

    /// @brief Set default font for all widgets.
    /// @param app GUI application handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_gui_app_set_font(void *app, void *font, double size);

    //=========================================================================
    // Font Functions
    //=========================================================================

    /// @brief Load a font from a file.
    /// @param path Path to TTF font file.
    /// @return Opaque font handle, or NULL on failure.
    void *rt_font_load(rt_string path);

    /// @brief Destroy a font and free resources.
    /// @param font Font handle.
    void rt_font_destroy(void *font);

    //=========================================================================
    // Widget Functions
    //=========================================================================

    /// @brief Destroy a widget and all its children.
    /// @param widget Widget handle.
    void rt_widget_destroy(void *widget);

    /// @brief Set widget visibility.
    /// @param widget Widget handle.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_widget_set_visible(void *widget, int64_t visible);

    /// @brief Set widget enabled state.
    /// @param widget Widget handle.
    /// @param enabled 1 for enabled, 0 for disabled.
    void rt_widget_set_enabled(void *widget, int64_t enabled);

    /// @brief Set widget fixed size.
    /// @param widget Widget handle.
    /// @param width Width in pixels.
    /// @param height Height in pixels.
    void rt_widget_set_size(void *widget, int64_t width, int64_t height);

    /// @brief Set the flex grow factor for VBox/HBox layout.
    /// @param widget Widget handle.
    /// @param flex Flex factor (0 = fixed size, >0 = expand proportionally).
    void rt_widget_set_flex(void *widget, double flex);

    /// @brief Add a child widget to a parent.
    /// @param parent Parent widget handle.
    /// @param child Child widget handle.
    void rt_widget_add_child(void *parent, void *child);

    /// @brief Check if widget is hovered.
    /// @param widget Widget handle.
    /// @return 1 if hovered, 0 otherwise.
    int64_t rt_widget_is_hovered(void *widget);

    /// @brief Check if widget is pressed.
    /// @param widget Widget handle.
    /// @return 1 if pressed, 0 otherwise.
    int64_t rt_widget_is_pressed(void *widget);

    /// @brief Check if widget is focused.
    /// @param widget Widget handle.
    /// @return 1 if focused, 0 otherwise.
    int64_t rt_widget_is_focused(void *widget);

    /// @brief Check if widget was clicked this frame.
    /// @param widget Widget handle.
    /// @return 1 if clicked, 0 otherwise.
    int64_t rt_widget_was_clicked(void *widget);

    /// @brief Set widget position.
    /// @param widget Widget handle.
    /// @param x X position in pixels.
    /// @param y Y position in pixels.
    void rt_widget_set_position(void *widget, int64_t x, int64_t y);

    //=========================================================================
    // Label Widget
    //=========================================================================

    /// @brief Create a new label widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Label text.
    /// @return Label widget handle.
    void *rt_label_new(void *parent, rt_string text);

    /// @brief Set label text.
    /// @param label Label widget handle.
    /// @param text New text.
    void rt_label_set_text(void *label, rt_string text);

    /// @brief Set label font.
    /// @param label Label widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_label_set_font(void *label, void *font, double size);

    /// @brief Set label text color.
    /// @param label Label widget handle.
    /// @param color ARGB color (0xAARRGGBB).
    void rt_label_set_color(void *label, int64_t color);

    //=========================================================================
    // Button Widget
    //=========================================================================

    /// @brief Create a new button widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Button text.
    /// @return Button widget handle.
    void *rt_button_new(void *parent, rt_string text);

    /// @brief Set button text.
    /// @param button Button widget handle.
    /// @param text New text.
    void rt_button_set_text(void *button, rt_string text);

    /// @brief Set button font.
    /// @param button Button widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_button_set_font(void *button, void *font, double size);

    /// @brief Set button style.
    /// @param button Button widget handle.
    /// @param style Button style (0=default, 1=primary, 2=secondary, 3=danger, 4=text).
    void rt_button_set_style(void *button, int64_t style);

    //=========================================================================
    // TextInput Widget
    //=========================================================================

    /// @brief Create a new text input widget.
    /// @param parent Parent widget (can be NULL).
    /// @return TextInput widget handle.
    void *rt_textinput_new(void *parent);

    /// @brief Set text input content.
    /// @param input TextInput widget handle.
    /// @param text New text.
    void rt_textinput_set_text(void *input, rt_string text);

    /// @brief Get text input content.
    /// @param input TextInput widget handle.
    /// @return Current text as runtime string.
    rt_string rt_textinput_get_text(void *input);

    /// @brief Set placeholder text.
    /// @param input TextInput widget handle.
    /// @param placeholder Placeholder text.
    void rt_textinput_set_placeholder(void *input, rt_string placeholder);

    /// @brief Set text input font.
    /// @param input TextInput widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_textinput_set_font(void *input, void *font, double size);

    //=========================================================================
    // Checkbox Widget
    //=========================================================================

    /// @brief Create a new checkbox widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Checkbox label text.
    /// @return Checkbox widget handle.
    void *rt_checkbox_new(void *parent, rt_string text);

    /// @brief Set checkbox checked state.
    /// @param checkbox Checkbox widget handle.
    /// @param checked 1 for checked, 0 for unchecked.
    void rt_checkbox_set_checked(void *checkbox, int64_t checked);

    /// @brief Get checkbox checked state.
    /// @param checkbox Checkbox widget handle.
    /// @return 1 if checked, 0 if not.
    int64_t rt_checkbox_is_checked(void *checkbox);

    /// @brief Set checkbox text.
    /// @param checkbox Checkbox widget handle.
    /// @param text New text.
    void rt_checkbox_set_text(void *checkbox, rt_string text);

    //=========================================================================
    // ScrollView Widget
    //=========================================================================

    /// @brief Create a new scroll view widget.
    /// @param parent Parent widget (can be NULL).
    /// @return ScrollView widget handle.
    void *rt_scrollview_new(void *parent);

    /// @brief Set scroll position.
    /// @param scroll ScrollView widget handle.
    /// @param x Horizontal scroll position.
    /// @param y Vertical scroll position.
    void rt_scrollview_set_scroll(void *scroll, double x, double y);

    /// @brief Set content size.
    /// @param scroll ScrollView widget handle.
    /// @param width Content width (0 = auto).
    /// @param height Content height (0 = auto).
    void rt_scrollview_set_content_size(void *scroll, double width, double height);

    //=========================================================================
    // TreeView Widget
    //=========================================================================

    /// @brief Create a new tree view widget.
    /// @param parent Parent widget (can be NULL).
    /// @return TreeView widget handle.
    void *rt_treeview_new(void *parent);

    /// @brief Add a node to the tree view.
    /// @param tree TreeView widget handle.
    /// @param parent_node Parent node handle (NULL for root).
    /// @param text Node text.
    /// @return New node handle.
    void *rt_treeview_add_node(void *tree, void *parent_node, rt_string text);

    /// @brief Remove a node from the tree view.
    /// @param tree TreeView widget handle.
    /// @param node Node handle to remove.
    void rt_treeview_remove_node(void *tree, void *node);

    /// @brief Clear all nodes from tree view.
    /// @param tree TreeView widget handle.
    void rt_treeview_clear(void *tree);

    /// @brief Expand a tree node.
    /// @param tree TreeView widget handle.
    /// @param node Node handle.
    void rt_treeview_expand(void *tree, void *node);

    /// @brief Collapse a tree node.
    /// @param tree TreeView widget handle.
    /// @param node Node handle.
    void rt_treeview_collapse(void *tree, void *node);

    /// @brief Select a tree node.
    /// @param tree TreeView widget handle.
    /// @param node Node handle.
    void rt_treeview_select(void *tree, void *node);

    /// @brief Set tree view font.
    /// @param tree TreeView widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_treeview_set_font(void *tree, void *font, double size);

    /// @brief Get the currently selected tree node.
    /// @param tree TreeView widget handle.
    /// @return Selected node handle or NULL if none selected.
    void *rt_treeview_get_selected(void *tree);

    /// @brief Check if selection changed since last call (polling pattern).
    /// @param tree TreeView widget handle.
    /// @return 1 if selection changed, 0 otherwise.
    int64_t rt_treeview_was_selection_changed(void *tree);

    /// @brief Get the text label of a tree node.
    /// @param node Tree node handle.
    /// @return Node text as runtime string.
    rt_string rt_treeview_node_get_text(void *node);

    /// @brief Store user data (file path) in a tree node.
    /// @param node Tree node handle.
    /// @param data String data to store.
    void rt_treeview_node_set_data(void *node, rt_string data);

    /// @brief Get user data stored in a tree node.
    /// @param node Tree node handle.
    /// @return Stored string data.
    rt_string rt_treeview_node_get_data(void *node);

    /// @brief Check if a tree node is expanded.
    /// @param node Tree node handle.
    /// @return 1 if expanded, 0 otherwise.
    int64_t rt_treeview_node_is_expanded(void *node);

    //=========================================================================
    // TabBar Widget
    //=========================================================================

    /// @brief Create a new tab bar widget.
    /// @param parent Parent widget (can be NULL).
    /// @return TabBar widget handle.
    void *rt_tabbar_new(void *parent);

    /// @brief Add a tab to the tab bar.
    /// @param tabbar TabBar widget handle.
    /// @param title Tab title.
    /// @param closable 1 if tab can be closed, 0 otherwise.
    /// @return Tab handle.
    void *rt_tabbar_add_tab(void *tabbar, rt_string title, int64_t closable);

    /// @brief Remove a tab from the tab bar.
    /// @param tabbar TabBar widget handle.
    /// @param tab Tab handle.
    void rt_tabbar_remove_tab(void *tabbar, void *tab);

    /// @brief Set the active tab.
    /// @param tabbar TabBar widget handle.
    /// @param tab Tab handle.
    void rt_tabbar_set_active(void *tabbar, void *tab);

    /// @brief Set tab title.
    /// @param tab Tab handle.
    /// @param title New title.
    void rt_tab_set_title(void *tab, rt_string title);

    /// @brief Set tab modified state.
    /// @param tab Tab handle.
    /// @param modified 1 for modified, 0 for not modified.
    void rt_tab_set_modified(void *tab, int64_t modified);

    /// @brief Get the active tab.
    /// @param tabbar TabBar widget handle.
    /// @return Active tab handle, or NULL if none.
    void *rt_tabbar_get_active(void *tabbar);

    /// @brief Get the index of the active tab.
    /// @param tabbar TabBar widget handle.
    /// @return 0-based index of active tab, or -1 if none.
    int64_t rt_tabbar_get_active_index(void *tabbar);

    /// @brief Check if the active tab changed since last call.
    /// @param tabbar TabBar widget handle.
    /// @return 1 if changed, 0 otherwise. Consumes the change flag.
    int64_t rt_tabbar_was_changed(void *tabbar);

    /// @brief Get the number of tabs.
    /// @param tabbar TabBar widget handle.
    /// @return Number of tabs.
    int64_t rt_tabbar_get_tab_count(void *tabbar);

    /// @brief Check if a tab close button was clicked.
    /// @param tabbar TabBar widget handle.
    /// @return 1 if close was clicked, 0 otherwise.
    int64_t rt_tabbar_was_close_clicked(void *tabbar);

    /// @brief Get the index of the tab whose close button was clicked.
    /// @param tabbar TabBar widget handle.
    /// @return 0-based index, or -1 if none. Consumes the close event.
    int64_t rt_tabbar_get_close_clicked_index(void *tabbar);

    /// @brief Get a tab by index.
    /// @param tabbar TabBar widget handle.
    /// @param index 0-based tab index.
    /// @return Tab handle, or NULL if out of bounds.
    void *rt_tabbar_get_tab_at(void *tabbar, int64_t index);

    /// @brief Set whether tabs auto-close when close button is clicked.
    /// @param tabbar TabBar widget handle.
    /// @param auto_close 1 for auto-close (default), 0 to let Zia code handle removal.
    void rt_tabbar_set_auto_close(void *tabbar, int64_t auto_close);

    //=========================================================================
    // SplitPane Widget
    //=========================================================================

    /// @brief Create a new split pane widget.
    /// @param parent Parent widget (can be NULL).
    /// @param horizontal 1 for horizontal split (left/right), 0 for vertical (top/bottom).
    /// @return SplitPane widget handle.
    void *rt_splitpane_new(void *parent, int64_t horizontal);

    /// @brief Set split position.
    /// @param split SplitPane widget handle.
    /// @param position Split position (0.0 to 1.0).
    void rt_splitpane_set_position(void *split, double position);

    /// @brief Get the first pane.
    /// @param split SplitPane widget handle.
    /// @return First pane widget handle.
    void *rt_splitpane_get_first(void *split);

    /// @brief Get the second pane.
    /// @param split SplitPane widget handle.
    /// @return Second pane widget handle.
    void *rt_splitpane_get_second(void *split);

    //=========================================================================
    // CodeEditor Widget
    //=========================================================================

    /// @brief Create a new code editor widget.
    /// @param parent Parent widget (can be NULL).
    /// @return CodeEditor widget handle.
    void *rt_codeeditor_new(void *parent);

    /// @brief Set code editor text content.
    /// @param editor CodeEditor widget handle.
    /// @param text New text content.
    void rt_codeeditor_set_text(void *editor, rt_string text);

    /// @brief Get code editor text content.
    /// @param editor CodeEditor widget handle.
    /// @return Text content as runtime string.
    rt_string rt_codeeditor_get_text(void *editor);

    /// @brief Set cursor position.
    /// @param editor CodeEditor widget handle.
    /// @param line Line number (0-based).
    /// @param col Column number (0-based).
    void rt_codeeditor_set_cursor(void *editor, int64_t line, int64_t col);

    /// @brief Scroll to a specific line.
    /// @param editor CodeEditor widget handle.
    /// @param line Line number (0-based).
    void rt_codeeditor_scroll_to_line(void *editor, int64_t line);

    /// @brief Get line count.
    /// @param editor CodeEditor widget handle.
    /// @return Number of lines.
    int64_t rt_codeeditor_get_line_count(void *editor);

    /// @brief Check if editor content is modified.
    /// @param editor CodeEditor widget handle.
    /// @return 1 if modified, 0 if not.
    int64_t rt_codeeditor_is_modified(void *editor);

    /// @brief Clear modified flag.
    /// @param editor CodeEditor widget handle.
    void rt_codeeditor_clear_modified(void *editor);

    /// @brief Set code editor font.
    /// @param editor CodeEditor widget handle.
    /// @param font Font handle.
    /// @param size Font size in pixels.
    void rt_codeeditor_set_font(void *editor, void *font, double size);

    //=========================================================================
    // Dropdown Widget
    //=========================================================================

    /// @brief Create a new dropdown widget.
    /// @param parent Parent widget (can be NULL).
    /// @return Dropdown widget handle.
    void *rt_dropdown_new(void *parent);

    /// @brief Add an item to the dropdown.
    /// @param dropdown Dropdown widget handle.
    /// @param text Item text.
    /// @return Index of the added item.
    int64_t rt_dropdown_add_item(void *dropdown, rt_string text);

    /// @brief Remove an item from the dropdown.
    /// @param dropdown Dropdown widget handle.
    /// @param index Item index.
    void rt_dropdown_remove_item(void *dropdown, int64_t index);

    /// @brief Clear all items from the dropdown.
    /// @param dropdown Dropdown widget handle.
    void rt_dropdown_clear(void *dropdown);

    /// @brief Set selected item.
    /// @param dropdown Dropdown widget handle.
    /// @param index Item index (-1 for none).
    void rt_dropdown_set_selected(void *dropdown, int64_t index);

    /// @brief Get selected item index.
    /// @param dropdown Dropdown widget handle.
    /// @return Selected index, or -1 if none.
    int64_t rt_dropdown_get_selected(void *dropdown);

    /// @brief Get selected item text.
    /// @param dropdown Dropdown widget handle.
    /// @return Selected text, or empty string if none.
    rt_string rt_dropdown_get_selected_text(void *dropdown);

    /// @brief Set dropdown placeholder text.
    /// @param dropdown Dropdown widget handle.
    /// @param placeholder Placeholder text.
    void rt_dropdown_set_placeholder(void *dropdown, rt_string placeholder);

    //=========================================================================
    // Slider Widget
    //=========================================================================

    /// @brief Create a new slider widget.
    /// @param parent Parent widget (can be NULL).
    /// @param horizontal 1 for horizontal, 0 for vertical.
    /// @return Slider widget handle.
    void *rt_slider_new(void *parent, int64_t horizontal);

    /// @brief Set slider value.
    /// @param slider Slider widget handle.
    /// @param value Slider value.
    void rt_slider_set_value(void *slider, double value);

    /// @brief Get slider value.
    /// @param slider Slider widget handle.
    /// @return Current value.
    double rt_slider_get_value(void *slider);

    /// @brief Set slider range.
    /// @param slider Slider widget handle.
    /// @param min_val Minimum value.
    /// @param max_val Maximum value.
    void rt_slider_set_range(void *slider, double min_val, double max_val);

    /// @brief Set slider step.
    /// @param slider Slider widget handle.
    /// @param step Step value (0 for continuous).
    void rt_slider_set_step(void *slider, double step);

    //=========================================================================
    // ProgressBar Widget
    //=========================================================================

    /// @brief Create a new progress bar widget.
    /// @param parent Parent widget (can be NULL).
    /// @return ProgressBar widget handle.
    void *rt_progressbar_new(void *parent);

    /// @brief Set progress bar value.
    /// @param progress ProgressBar widget handle.
    /// @param value Progress value (0.0 to 1.0).
    void rt_progressbar_set_value(void *progress, double value);

    /// @brief Get progress bar value.
    /// @param progress ProgressBar widget handle.
    /// @return Current value (0.0 to 1.0).
    double rt_progressbar_get_value(void *progress);

    //=========================================================================
    // ListBox Widget
    //=========================================================================

    /// @brief Create a new list box widget.
    /// @param parent Parent widget (can be NULL).
    /// @return ListBox widget handle.
    void *rt_listbox_new(void *parent);

    /// @brief Add an item to the list box.
    /// @param listbox ListBox widget handle.
    /// @param text Item text.
    /// @return Item handle.
    void *rt_listbox_add_item(void *listbox, rt_string text);

    /// @brief Remove an item from the list box.
    /// @param listbox ListBox widget handle.
    /// @param item Item handle.
    void rt_listbox_remove_item(void *listbox, void *item);

    /// @brief Clear all items from the list box.
    /// @param listbox ListBox widget handle.
    void rt_listbox_clear(void *listbox);

    /// @brief Select an item.
    /// @param listbox ListBox widget handle.
    /// @param item Item handle (NULL to deselect).
    void rt_listbox_select(void *listbox, void *item);

    /// @brief Get selected item.
    /// @param listbox ListBox widget handle.
    /// @return Selected item handle, or NULL if none.
    void *rt_listbox_get_selected(void *listbox);

    //=========================================================================
    // RadioButton Widget
    //=========================================================================

    /// @brief Create a new radio group.
    /// @return RadioGroup handle.
    void *rt_radiogroup_new(void);

    /// @brief Destroy a radio group.
    /// @param group RadioGroup handle.
    void rt_radiogroup_destroy(void *group);

    /// @brief Create a new radio button widget.
    /// @param parent Parent widget (can be NULL).
    /// @param text Radio button text.
    /// @param group RadioGroup handle.
    /// @return RadioButton widget handle.
    void *rt_radiobutton_new(void *parent, rt_string text, void *group);

    /// @brief Check if radio button is selected.
    /// @param radio RadioButton widget handle.
    /// @return 1 if selected, 0 otherwise.
    int64_t rt_radiobutton_is_selected(void *radio);

    /// @brief Set radio button selected state.
    /// @param radio RadioButton widget handle.
    /// @param selected 1 for selected, 0 for not selected.
    void rt_radiobutton_set_selected(void *radio, int64_t selected);

    //=========================================================================
    // Spinner Widget
    //=========================================================================

    /// @brief Create a new spinner widget.
    /// @param parent Parent widget (can be NULL).
    /// @return Spinner widget handle.
    void *rt_spinner_new(void *parent);

    /// @brief Set spinner value.
    /// @param spinner Spinner widget handle.
    /// @param value Spinner value.
    void rt_spinner_set_value(void *spinner, double value);

    /// @brief Get spinner value.
    /// @param spinner Spinner widget handle.
    /// @return Current value.
    double rt_spinner_get_value(void *spinner);

    /// @brief Set spinner range.
    /// @param spinner Spinner widget handle.
    /// @param min_val Minimum value.
    /// @param max_val Maximum value.
    void rt_spinner_set_range(void *spinner, double min_val, double max_val);

    /// @brief Set spinner step.
    /// @param spinner Spinner widget handle.
    /// @param step Step value.
    void rt_spinner_set_step(void *spinner, double step);

    /// @brief Set spinner decimal places.
    /// @param spinner Spinner widget handle.
    /// @param decimals Number of decimal places.
    void rt_spinner_set_decimals(void *spinner, int64_t decimals);

    //=========================================================================
    // Image Widget
    //=========================================================================

    /// @brief Create a new image widget.
    /// @param parent Parent widget (can be NULL).
    /// @return Image widget handle.
    void *rt_image_new(void *parent);

    /// @brief Set image pixels.
    /// @param image Image widget handle.
    /// @param pixels Pixel data (RGBA format).
    /// @param width Image width.
    /// @param height Image height.
    void rt_image_set_pixels(void *image, void *pixels, int64_t width, int64_t height);

    /// @brief Clear image.
    /// @param image Image widget handle.
    void rt_image_clear(void *image);

    /// @brief Set image scale mode.
    /// @param image Image widget handle.
    /// @param mode Scale mode (0=none, 1=fit, 2=fill, 3=stretch).
    void rt_image_set_scale_mode(void *image, int64_t mode);

    /// @brief Set image opacity.
    /// @param image Image widget handle.
    /// @param opacity Opacity (0.0 to 1.0).
    void rt_image_set_opacity(void *image, double opacity);

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
    void *rt_vbox_new(void);

    /// @brief Create a container with horizontal box layout.
    /// @return HBox container widget handle.
    void *rt_hbox_new(void);

    /// @brief Set spacing for a layout container.
    /// @param container Container widget handle.
    /// @param spacing Spacing in pixels.
    void rt_container_set_spacing(void *container, double spacing);

    /// @brief Set padding for a layout container.
    /// @param container Container widget handle.
    /// @param padding Padding in pixels.
    void rt_container_set_padding(void *container, double padding);

    //=========================================================================
    // Clipboard Functions (Phase 1)
    //=========================================================================

    /// @brief Set text to the system clipboard.
    /// @param text Text to copy to clipboard.
    void rt_clipboard_set_text(rt_string text);

    /// @brief Get text from the system clipboard.
    /// @return Text from clipboard, or empty string if not available.
    rt_string rt_clipboard_get_text(void);

    /// @brief Check if clipboard contains text.
    /// @return 1 if text is available, 0 otherwise.
    int64_t rt_clipboard_has_text(void);

    /// @brief Clear all clipboard contents.
    void rt_clipboard_clear(void);

    //=========================================================================
    // Keyboard Shortcuts (Phase 1)
    //=========================================================================

    /// @brief Register a keyboard shortcut.
    /// @param id Unique identifier for the shortcut.
    /// @param keys Key combination string (e.g., "Ctrl+S", "Ctrl+Shift+P").
    /// @param description Human-readable description.
    void rt_shortcuts_register(rt_string id, rt_string keys, rt_string description);

    /// @brief Unregister a keyboard shortcut.
    /// @param id Shortcut identifier to remove.
    void rt_shortcuts_unregister(rt_string id);

    /// @brief Clear all registered shortcuts.
    void rt_shortcuts_clear(void);

    /// @brief Check if a specific shortcut was triggered this frame.
    /// @param id Shortcut identifier to check.
    /// @return 1 if triggered, 0 otherwise.
    int64_t rt_shortcuts_was_triggered(rt_string id);

    /// @brief Get the ID of the shortcut triggered this frame.
    /// @return Shortcut ID, or empty string if none triggered.
    rt_string rt_shortcuts_get_triggered(void);

    /// @brief Enable or disable a specific shortcut.
    /// @param id Shortcut identifier.
    /// @param enabled 1 to enable, 0 to disable.
    void rt_shortcuts_set_enabled(rt_string id, int64_t enabled);

    /// @brief Check if a specific shortcut is enabled.
    /// @param id Shortcut identifier.
    /// @return 1 if enabled, 0 otherwise.
    int64_t rt_shortcuts_is_enabled(rt_string id);

    /// @brief Enable or disable all shortcuts globally.
    /// @param enabled 1 to enable, 0 to disable.
    void rt_shortcuts_set_global_enabled(int64_t enabled);

    /// @brief Check if shortcuts are globally enabled.
    /// @return 1 if enabled, 0 otherwise.
    int64_t rt_shortcuts_get_global_enabled(void);

    //=========================================================================
    // Window Management (Phase 1)
    //=========================================================================

    /// @brief Set the window title.
    /// @param app GUI application handle.
    /// @param title New window title.
    void rt_app_set_title(void *app, rt_string title);

    /// @brief Get the window title.
    /// @param app GUI application handle.
    /// @return Current window title.
    rt_string rt_app_get_title(void *app);

    /// @brief Set the window size.
    /// @param app GUI application handle.
    /// @param width New width in pixels.
    /// @param height New height in pixels.
    void rt_app_set_size(void *app, int64_t width, int64_t height);

    /// @brief Get the window width.
    /// @param app GUI application handle.
    /// @return Window width in pixels.
    int64_t rt_app_get_width(void *app);

    /// @brief Get the window height.
    /// @param app GUI application handle.
    /// @return Window height in pixels.
    int64_t rt_app_get_height(void *app);

    /// @brief Set the window position.
    /// @param app GUI application handle.
    /// @param x X position in screen coordinates.
    /// @param y Y position in screen coordinates.
    void rt_app_set_position(void *app, int64_t x, int64_t y);

    /// @brief Get the window X position.
    /// @param app GUI application handle.
    /// @return X position in screen coordinates.
    int64_t rt_app_get_x(void *app);

    /// @brief Get the window Y position.
    /// @param app GUI application handle.
    /// @return Y position in screen coordinates.
    int64_t rt_app_get_y(void *app);

    /// @brief Minimize the window.
    /// @param app GUI application handle.
    void rt_app_minimize(void *app);

    /// @brief Maximize the window.
    /// @param app GUI application handle.
    void rt_app_maximize(void *app);

    /// @brief Restore the window from minimized/maximized state.
    /// @param app GUI application handle.
    void rt_app_restore(void *app);

    /// @brief Check if the window is minimized.
    /// @param app GUI application handle.
    /// @return 1 if minimized, 0 otherwise.
    int64_t rt_app_is_minimized(void *app);

    /// @brief Check if the window is maximized.
    /// @param app GUI application handle.
    /// @return 1 if maximized, 0 otherwise.
    int64_t rt_app_is_maximized(void *app);

    /// @brief Set the window fullscreen state.
    /// @param app GUI application handle.
    /// @param fullscreen 1 for fullscreen, 0 for windowed.
    void rt_app_set_fullscreen(void *app, int64_t fullscreen);

    /// @brief Check if the window is fullscreen.
    /// @param app GUI application handle.
    /// @return 1 if fullscreen, 0 otherwise.
    int64_t rt_app_is_fullscreen(void *app);

    /// @brief Bring the window to the front and give it focus.
    /// @param app GUI application handle.
    void rt_app_focus(void *app);

    /// @brief Check if the window has keyboard focus.
    /// @param app GUI application handle.
    /// @return 1 if focused, 0 otherwise.
    int64_t rt_app_is_focused(void *app);

    /// @brief Enable or disable close prevention.
    /// @param app GUI application handle.
    /// @param prevent 1 to prevent close, 0 to allow.
    void rt_app_set_prevent_close(void *app, int64_t prevent);

    /// @brief Check if close was requested.
    /// @param app GUI application handle.
    /// @return 1 if close was requested, 0 otherwise.
    int64_t rt_app_was_close_requested(void *app);

//=========================================================================
// Cursor Styles (Phase 1)
//=========================================================================

/// Cursor type constants
#define RT_CURSOR_ARROW 0
#define RT_CURSOR_IBEAM 1
#define RT_CURSOR_WAIT 2
#define RT_CURSOR_CROSSHAIR 3
#define RT_CURSOR_HAND 4
#define RT_CURSOR_RESIZE_H 5
#define RT_CURSOR_RESIZE_V 6
#define RT_CURSOR_RESIZE_NE 7
#define RT_CURSOR_RESIZE_NW 8
#define RT_CURSOR_MOVE 9
#define RT_CURSOR_NOT_ALLOWED 10

    /// @brief Set the global cursor style.
    /// @param type Cursor type constant (RT_CURSOR_*).
    void rt_cursor_set(int64_t type);

    /// @brief Reset cursor to default (arrow).
    void rt_cursor_reset(void);

    /// @brief Set cursor visibility.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_cursor_set_visible(int64_t visible);

    /// @brief Set cursor for a specific widget.
    /// @param widget Widget handle.
    /// @param type Cursor type constant.
    void rt_widget_set_cursor(void *widget, int64_t type);

    /// @brief Reset widget cursor to default.
    /// @param widget Widget handle.
    void rt_widget_reset_cursor(void *widget);

    //=========================================================================
    // MenuBar Widget (Phase 2)
    //=========================================================================

    /// @brief Create a new menu bar widget.
    /// @param parent Parent widget (can be NULL).
    /// @return MenuBar widget handle.
    void *rt_menubar_new(void *parent);

    /// @brief Destroy a menu bar widget.
    /// @param menubar MenuBar widget handle.
    void rt_menubar_destroy(void *menubar);

    /// @brief Add a menu to the menu bar.
    /// @param menubar MenuBar widget handle.
    /// @param title Menu title.
    /// @return Menu handle.
    void *rt_menubar_add_menu(void *menubar, rt_string title);

    /// @brief Remove a menu from the menu bar.
    /// @param menubar MenuBar widget handle.
    /// @param menu Menu handle to remove.
    void rt_menubar_remove_menu(void *menubar, void *menu);

    /// @brief Get the number of menus in the menu bar.
    /// @param menubar MenuBar widget handle.
    /// @return Number of menus.
    int64_t rt_menubar_get_menu_count(void *menubar);

    /// @brief Get a menu by index.
    /// @param menubar MenuBar widget handle.
    /// @param index Menu index.
    /// @return Menu handle, or NULL if out of bounds.
    void *rt_menubar_get_menu(void *menubar, int64_t index);

    /// @brief Set menu bar visibility.
    /// @param menubar MenuBar widget handle.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_menubar_set_visible(void *menubar, int64_t visible);

    /// @brief Check if menu bar is visible.
    /// @param menubar MenuBar widget handle.
    /// @return 1 if visible, 0 otherwise.
    int64_t rt_menubar_is_visible(void *menubar);

    //=========================================================================
    // Menu Widget (Phase 2)
    //=========================================================================

    /// @brief Add a menu item.
    /// @param menu Menu handle.
    /// @param text Item text.
    /// @return MenuItem handle.
    void *rt_menu_add_item(void *menu, rt_string text);

    /// @brief Add a menu item with keyboard shortcut.
    /// @param menu Menu handle.
    /// @param text Item text.
    /// @param shortcut Shortcut string (e.g., "Ctrl+S").
    /// @return MenuItem handle.
    void *rt_menu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut);

    /// @brief Add a separator to the menu.
    /// @param menu Menu handle.
    /// @return MenuItem handle (separator).
    void *rt_menu_add_separator(void *menu);

    /// @brief Add a submenu.
    /// @param menu Parent menu handle.
    /// @param title Submenu title.
    /// @return Menu handle for the submenu.
    void *rt_menu_add_submenu(void *menu, rt_string title);

    /// @brief Remove an item from the menu.
    /// @param menu Menu handle.
    /// @param item MenuItem handle to remove.
    void rt_menu_remove_item(void *menu, void *item);

    /// @brief Clear all items from the menu.
    /// @param menu Menu handle.
    void rt_menu_clear(void *menu);

    /// @brief Set menu title.
    /// @param menu Menu handle.
    /// @param title New title.
    void rt_menu_set_title(void *menu, rt_string title);

    /// @brief Get menu title.
    /// @param menu Menu handle.
    /// @return Menu title.
    rt_string rt_menu_get_title(void *menu);

    /// @brief Get number of items in the menu.
    /// @param menu Menu handle.
    /// @return Number of items.
    int64_t rt_menu_get_item_count(void *menu);

    /// @brief Get a menu item by index.
    /// @param menu Menu handle.
    /// @param index Item index.
    /// @return MenuItem handle, or NULL if out of bounds.
    void *rt_menu_get_item(void *menu, int64_t index);

    /// @brief Enable or disable the menu.
    /// @param menu Menu handle.
    /// @param enabled 1 to enable, 0 to disable.
    void rt_menu_set_enabled(void *menu, int64_t enabled);

    /// @brief Check if menu is enabled.
    /// @param menu Menu handle.
    /// @return 1 if enabled, 0 otherwise.
    int64_t rt_menu_is_enabled(void *menu);

    //=========================================================================
    // MenuItem Widget (Phase 2)
    //=========================================================================

    /// @brief Set menu item text.
    /// @param item MenuItem handle.
    /// @param text New text.
    void rt_menuitem_set_text(void *item, rt_string text);

    /// @brief Get menu item text.
    /// @param item MenuItem handle.
    /// @return Item text.
    rt_string rt_menuitem_get_text(void *item);

    /// @brief Set menu item keyboard shortcut.
    /// @param item MenuItem handle.
    /// @param shortcut Shortcut string.
    void rt_menuitem_set_shortcut(void *item, rt_string shortcut);

    /// @brief Get menu item keyboard shortcut.
    /// @param item MenuItem handle.
    /// @return Shortcut string.
    rt_string rt_menuitem_get_shortcut(void *item);

    /// @brief Set menu item icon.
    /// @param item MenuItem handle.
    /// @param pixels Pixel data handle.
    void rt_menuitem_set_icon(void *item, void *pixels);

    /// @brief Set whether menu item is checkable.
    /// @param item MenuItem handle.
    /// @param checkable 1 for checkable, 0 otherwise.
    void rt_menuitem_set_checkable(void *item, int64_t checkable);

    /// @brief Check if menu item is checkable.
    /// @param item MenuItem handle.
    /// @return 1 if checkable, 0 otherwise.
    int64_t rt_menuitem_is_checkable(void *item);

    /// @brief Set menu item checked state.
    /// @param item MenuItem handle.
    /// @param checked 1 for checked, 0 for unchecked.
    void rt_menuitem_set_checked(void *item, int64_t checked);

    /// @brief Check if menu item is checked.
    /// @param item MenuItem handle.
    /// @return 1 if checked, 0 otherwise.
    int64_t rt_menuitem_is_checked(void *item);

    /// @brief Enable or disable the menu item.
    /// @param item MenuItem handle.
    /// @param enabled 1 to enable, 0 to disable.
    void rt_menuitem_set_enabled(void *item, int64_t enabled);

    /// @brief Check if menu item is enabled.
    /// @param item MenuItem handle.
    /// @return 1 if enabled, 0 otherwise.
    int64_t rt_menuitem_is_enabled(void *item);

    /// @brief Check if menu item is a separator.
    /// @param item MenuItem handle.
    /// @return 1 if separator, 0 otherwise.
    int64_t rt_menuitem_is_separator(void *item);

    /// @brief Check if menu item was clicked this frame.
    /// @param item MenuItem handle.
    /// @return 1 if clicked, 0 otherwise.
    int64_t rt_menuitem_was_clicked(void *item);

    //=========================================================================
    // ContextMenu Widget (Phase 2)
    //=========================================================================

    /// @brief Create a new context menu.
    /// @return ContextMenu handle.
    void *rt_contextmenu_new(void);

    /// @brief Destroy a context menu.
    /// @param menu ContextMenu handle.
    void rt_contextmenu_destroy(void *menu);

    /// @brief Add an item to the context menu.
    /// @param menu ContextMenu handle.
    /// @param text Item text.
    /// @return MenuItem handle.
    void *rt_contextmenu_add_item(void *menu, rt_string text);

    /// @brief Add an item with shortcut to the context menu.
    /// @param menu ContextMenu handle.
    /// @param text Item text.
    /// @param shortcut Shortcut string.
    /// @return MenuItem handle.
    void *rt_contextmenu_add_item_with_shortcut(void *menu, rt_string text, rt_string shortcut);

    /// @brief Add a separator to the context menu.
    /// @param menu ContextMenu handle.
    /// @return MenuItem handle (separator).
    void *rt_contextmenu_add_separator(void *menu);

    /// @brief Add a submenu to the context menu.
    /// @param menu ContextMenu handle.
    /// @param title Submenu title.
    /// @return Menu handle for the submenu.
    void *rt_contextmenu_add_submenu(void *menu, rt_string title);

    /// @brief Clear all items from the context menu.
    /// @param menu ContextMenu handle.
    void rt_contextmenu_clear(void *menu);

    /// @brief Show the context menu at a specific position.
    /// @param menu ContextMenu handle.
    /// @param x X position in screen coordinates.
    /// @param y Y position in screen coordinates.
    void rt_contextmenu_show(void *menu, int64_t x, int64_t y);

    /// @brief Hide the context menu.
    /// @param menu ContextMenu handle.
    void rt_contextmenu_hide(void *menu);

    /// @brief Check if context menu is visible.
    /// @param menu ContextMenu handle.
    /// @return 1 if visible, 0 otherwise.
    int64_t rt_contextmenu_is_visible(void *menu);

    /// @brief Get the clicked menu item.
    /// @param menu ContextMenu handle.
    /// @return MenuItem handle that was clicked, or NULL if none.
    void *rt_contextmenu_get_clicked_item(void *menu);

//=========================================================================
// StatusBar Widget (Phase 3)
//=========================================================================

/// StatusBar zone constants
#define RT_STATUSBAR_ZONE_LEFT 0
#define RT_STATUSBAR_ZONE_CENTER 1
#define RT_STATUSBAR_ZONE_RIGHT 2

    /// @brief Create a new status bar widget.
    /// @param parent Parent widget.
    /// @return StatusBar widget handle.
    void *rt_statusbar_new(void *parent);

    /// @brief Destroy a status bar widget.
    /// @param bar StatusBar widget handle.
    void rt_statusbar_destroy(void *bar);

    /// @brief Set left zone text.
    /// @param bar StatusBar widget handle.
    /// @param text Text to display.
    void rt_statusbar_set_left_text(void *bar, rt_string text);

    /// @brief Set center zone text.
    /// @param bar StatusBar widget handle.
    /// @param text Text to display.
    void rt_statusbar_set_center_text(void *bar, rt_string text);

    /// @brief Set right zone text.
    /// @param bar StatusBar widget handle.
    /// @param text Text to display.
    void rt_statusbar_set_right_text(void *bar, rt_string text);

    /// @brief Get left zone text.
    /// @param bar StatusBar widget handle.
    /// @return Left zone text.
    rt_string rt_statusbar_get_left_text(void *bar);

    /// @brief Get center zone text.
    /// @param bar StatusBar widget handle.
    /// @return Center zone text.
    rt_string rt_statusbar_get_center_text(void *bar);

    /// @brief Get right zone text.
    /// @param bar StatusBar widget handle.
    /// @return Right zone text.
    rt_string rt_statusbar_get_right_text(void *bar);

    /// @brief Add a text item to the status bar.
    /// @param bar StatusBar widget handle.
    /// @param text Text to display.
    /// @param zone Zone (RT_STATUSBAR_ZONE_*).
    /// @return StatusBarItem handle.
    void *rt_statusbar_add_text(void *bar, rt_string text, int64_t zone);

    /// @brief Add a button item to the status bar.
    /// @param bar StatusBar widget handle.
    /// @param text Button text.
    /// @param zone Zone (RT_STATUSBAR_ZONE_*).
    /// @return StatusBarItem handle.
    void *rt_statusbar_add_button(void *bar, rt_string text, int64_t zone);

    /// @brief Add a progress item to the status bar.
    /// @param bar StatusBar widget handle.
    /// @param zone Zone (RT_STATUSBAR_ZONE_*).
    /// @return StatusBarItem handle.
    void *rt_statusbar_add_progress(void *bar, int64_t zone);

    /// @brief Add a separator to the status bar.
    /// @param bar StatusBar widget handle.
    /// @param zone Zone (RT_STATUSBAR_ZONE_*).
    /// @return StatusBarItem handle.
    void *rt_statusbar_add_separator(void *bar, int64_t zone);

    /// @brief Add a spacer to the status bar.
    /// @param bar StatusBar widget handle.
    /// @param zone Zone (RT_STATUSBAR_ZONE_*).
    /// @return StatusBarItem handle.
    void *rt_statusbar_add_spacer(void *bar, int64_t zone);

    /// @brief Remove an item from the status bar.
    /// @param bar StatusBar widget handle.
    /// @param item StatusBarItem handle.
    void rt_statusbar_remove_item(void *bar, void *item);

    /// @brief Clear all items from the status bar.
    /// @param bar StatusBar widget handle.
    void rt_statusbar_clear(void *bar);

    /// @brief Set status bar visibility.
    /// @param bar StatusBar widget handle.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_statusbar_set_visible(void *bar, int64_t visible);

    /// @brief Check if status bar is visible.
    /// @param bar StatusBar widget handle.
    /// @return 1 if visible, 0 otherwise.
    int64_t rt_statusbar_is_visible(void *bar);

    //=========================================================================
    // StatusBarItem Widget (Phase 3)
    //=========================================================================

    /// @brief Set status bar item text.
    /// @param item StatusBarItem handle.
    /// @param text New text.
    void rt_statusbaritem_set_text(void *item, rt_string text);

    /// @brief Get status bar item text.
    /// @param item StatusBarItem handle.
    /// @return Item text.
    rt_string rt_statusbaritem_get_text(void *item);

    /// @brief Set status bar item tooltip.
    /// @param item StatusBarItem handle.
    /// @param tooltip Tooltip text.
    void rt_statusbaritem_set_tooltip(void *item, rt_string tooltip);

    /// @brief Set status bar item progress value.
    /// @param item StatusBarItem handle.
    /// @param value Progress value (0.0-1.0).
    void rt_statusbaritem_set_progress(void *item, double value);

    /// @brief Get status bar item progress value.
    /// @param item StatusBarItem handle.
    /// @return Progress value (0.0-1.0).
    double rt_statusbaritem_get_progress(void *item);

    /// @brief Set status bar item visibility.
    /// @param item StatusBarItem handle.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_statusbaritem_set_visible(void *item, int64_t visible);

    /// @brief Check if status bar item was clicked.
    /// @param item StatusBarItem handle.
    /// @return 1 if clicked, 0 otherwise.
    int64_t rt_statusbaritem_was_clicked(void *item);

//=========================================================================
// Toolbar Widget (Phase 3)
//=========================================================================

/// Toolbar style constants
#define RT_TOOLBAR_STYLE_ICON_ONLY 0
#define RT_TOOLBAR_STYLE_TEXT_ONLY 1
#define RT_TOOLBAR_STYLE_ICON_TEXT 2

/// Toolbar icon size constants
#define RT_TOOLBAR_ICON_SMALL 0
#define RT_TOOLBAR_ICON_MEDIUM 1
#define RT_TOOLBAR_ICON_LARGE 2

    /// @brief Create a new horizontal toolbar widget.
    /// @param parent Parent widget.
    /// @return Toolbar widget handle.
    void *rt_toolbar_new(void *parent);

    /// @brief Create a new vertical toolbar widget.
    /// @param parent Parent widget.
    /// @return Toolbar widget handle.
    void *rt_toolbar_new_vertical(void *parent);

    /// @brief Destroy a toolbar widget.
    /// @param toolbar Toolbar widget handle.
    void rt_toolbar_destroy(void *toolbar);

    /// @brief Add a button to the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @param icon_path Path to icon image.
    /// @param tooltip Tooltip text.
    /// @return ToolbarItem handle.
    void *rt_toolbar_add_button(void *toolbar, rt_string icon_path, rt_string tooltip);

    /// @brief Add a button with text to the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @param icon_path Path to icon image.
    /// @param text Button text.
    /// @param tooltip Tooltip text.
    /// @return ToolbarItem handle.
    void *rt_toolbar_add_button_with_text(void *toolbar,
                                          rt_string icon_path,
                                          rt_string text,
                                          rt_string tooltip);

    /// @brief Add a toggle button to the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @param icon_path Path to icon image.
    /// @param tooltip Tooltip text.
    /// @return ToolbarItem handle.
    void *rt_toolbar_add_toggle(void *toolbar, rt_string icon_path, rt_string tooltip);

    /// @brief Add a separator to the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @return ToolbarItem handle.
    void *rt_toolbar_add_separator(void *toolbar);

    /// @brief Add a spacer to the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @return ToolbarItem handle.
    void *rt_toolbar_add_spacer(void *toolbar);

    /// @brief Add a dropdown button to the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @param tooltip Tooltip text.
    /// @return ToolbarItem handle.
    void *rt_toolbar_add_dropdown(void *toolbar, rt_string tooltip);

    /// @brief Remove an item from the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @param item ToolbarItem handle.
    void rt_toolbar_remove_item(void *toolbar, void *item);

    /// @brief Set toolbar icon size.
    /// @param toolbar Toolbar widget handle.
    /// @param size Icon size (RT_TOOLBAR_ICON_*).
    void rt_toolbar_set_icon_size(void *toolbar, int64_t size);

    /// @brief Get toolbar icon size.
    /// @param toolbar Toolbar widget handle.
    /// @return Icon size (RT_TOOLBAR_ICON_*).
    int64_t rt_toolbar_get_icon_size(void *toolbar);

    /// @brief Set toolbar style.
    /// @param toolbar Toolbar widget handle.
    /// @param style Style (RT_TOOLBAR_STYLE_*).
    void rt_toolbar_set_style(void *toolbar, int64_t style);

    /// @brief Get number of items in the toolbar.
    /// @param toolbar Toolbar widget handle.
    /// @return Number of items.
    int64_t rt_toolbar_get_item_count(void *toolbar);

    /// @brief Get a toolbar item by index.
    /// @param toolbar Toolbar widget handle.
    /// @param index Item index.
    /// @return ToolbarItem handle.
    void *rt_toolbar_get_item(void *toolbar, int64_t index);

    /// @brief Set toolbar visibility.
    /// @param toolbar Toolbar widget handle.
    /// @param visible 1 for visible, 0 for hidden.
    void rt_toolbar_set_visible(void *toolbar, int64_t visible);

    /// @brief Check if toolbar is visible.
    /// @param toolbar Toolbar widget handle.
    /// @return 1 if visible, 0 otherwise.
    int64_t rt_toolbar_is_visible(void *toolbar);

    //=========================================================================
    // ToolbarItem Widget (Phase 3)
    //=========================================================================

    /// @brief Set toolbar item icon from path.
    /// @param item ToolbarItem handle.
    /// @param icon_path Path to icon image.
    void rt_toolbaritem_set_icon(void *item, rt_string icon_path);

    /// @brief Set toolbar item icon from pixels.
    /// @param item ToolbarItem handle.
    /// @param pixels Pixels handle.
    void rt_toolbaritem_set_icon_pixels(void *item, void *pixels);

    /// @brief Set toolbar item text.
    /// @param item ToolbarItem handle.
    /// @param text Item text.
    void rt_toolbaritem_set_text(void *item, rt_string text);

    /// @brief Set toolbar item tooltip.
    /// @param item ToolbarItem handle.
    /// @param tooltip Tooltip text.
    void rt_toolbaritem_set_tooltip(void *item, rt_string tooltip);

    /// @brief Set toolbar item enabled state.
    /// @param item ToolbarItem handle.
    /// @param enabled 1 for enabled, 0 for disabled.
    void rt_toolbaritem_set_enabled(void *item, int64_t enabled);

    /// @brief Check if toolbar item is enabled.
    /// @param item ToolbarItem handle.
    /// @return 1 if enabled, 0 otherwise.
    int64_t rt_toolbaritem_is_enabled(void *item);

    /// @brief Set toolbar item toggle state.
    /// @param item ToolbarItem handle.
    /// @param toggled 1 for toggled, 0 for not toggled.
    void rt_toolbaritem_set_toggled(void *item, int64_t toggled);

    /// @brief Check if toolbar item is toggled.
    /// @param item ToolbarItem handle.
    /// @return 1 if toggled, 0 otherwise.
    int64_t rt_toolbaritem_is_toggled(void *item);

    /// @brief Check if toolbar item was clicked.
    /// @param item ToolbarItem handle.
    /// @return 1 if clicked, 0 otherwise.
    int64_t rt_toolbaritem_was_clicked(void *item);

//=========================================================================
// CodeEditor Enhancements - Syntax Highlighting (Phase 4)
//=========================================================================

/// Token type constants for syntax highlighting
#define RT_TOKEN_NONE 0
#define RT_TOKEN_KEYWORD 1
#define RT_TOKEN_TYPE 2
#define RT_TOKEN_STRING 3
#define RT_TOKEN_NUMBER 4
#define RT_TOKEN_COMMENT 5
#define RT_TOKEN_OPERATOR 6
#define RT_TOKEN_FUNCTION 7
#define RT_TOKEN_VARIABLE 8
#define RT_TOKEN_CONSTANT 9
#define RT_TOKEN_ERROR 10

    /// @brief Set syntax highlighting language.
    /// @param editor CodeEditor handle.
    /// @param language Language identifier ("zia", "basic", "il").
    void rt_codeeditor_set_language(void *editor, rt_string language);

    /// @brief Set color for a token type.
    /// @param editor CodeEditor handle.
    /// @param token_type Token type (RT_TOKEN_*).
    /// @param color Color value (0xAARRGGBB).
    void rt_codeeditor_set_token_color(void *editor, int64_t token_type, int64_t color);

    /// @brief Set custom keywords for highlighting.
    /// @param editor CodeEditor handle.
    /// @param keywords Comma-separated list of keywords.
    void rt_codeeditor_set_custom_keywords(void *editor, rt_string keywords);

    /// @brief Clear all syntax highlights.
    /// @param editor CodeEditor handle.
    void rt_codeeditor_clear_highlights(void *editor);

    /// @brief Add a syntax highlight region.
    /// @param editor CodeEditor handle.
    /// @param start_line Starting line (0-based).
    /// @param start_col Starting column (0-based).
    /// @param end_line Ending line (0-based).
    /// @param end_col Ending column (0-based).
    /// @param token_type Token type (RT_TOKEN_*).
    void rt_codeeditor_add_highlight(void *editor,
                                     int64_t start_line,
                                     int64_t start_col,
                                     int64_t end_line,
                                     int64_t end_col,
                                     int64_t token_type);

    /// @brief Refresh syntax highlights.
    /// @param editor CodeEditor handle.
    void rt_codeeditor_refresh_highlights(void *editor);

    //=========================================================================
    // CodeEditor Enhancements - Gutter & Line Numbers (Phase 4)
    //=========================================================================

    /// @brief Set whether to show line numbers.
    /// @param editor CodeEditor handle.
    /// @param show 1 to show, 0 to hide.
    void rt_codeeditor_set_show_line_numbers(void *editor, int64_t show);

    /// @brief Check if line numbers are shown.
    /// @param editor CodeEditor handle.
    /// @return 1 if shown, 0 otherwise.
    int64_t rt_codeeditor_get_show_line_numbers(void *editor);

    /// @brief Set line number width.
    /// @param editor CodeEditor handle.
    /// @param width Width in characters.
    void rt_codeeditor_set_line_number_width(void *editor, int64_t width);

    /// @brief Set a gutter icon for a specific line.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    /// @param pixels Pixels handle for icon.
    /// @param slot Gutter slot (0=primary, 1=secondary).
    void rt_codeeditor_set_gutter_icon(void *editor, int64_t line, void *pixels, int64_t slot);

    /// @brief Clear a gutter icon for a specific line.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    /// @param slot Gutter slot (0=primary, 1=secondary).
    void rt_codeeditor_clear_gutter_icon(void *editor, int64_t line, int64_t slot);

    /// @brief Clear all gutter icons for a slot.
    /// @param editor CodeEditor handle.
    /// @param slot Gutter slot (0=primary, 1=secondary).
    void rt_codeeditor_clear_all_gutter_icons(void *editor, int64_t slot);

    /// @brief Check if gutter was clicked this frame.
    /// @param editor CodeEditor handle.
    /// @return 1 if clicked, 0 otherwise.
    int64_t rt_codeeditor_was_gutter_clicked(void *editor);

    /// @brief Get the line where gutter was clicked.
    /// @param editor CodeEditor handle.
    /// @return Line number (0-based), or -1 if no click.
    int64_t rt_codeeditor_get_gutter_clicked_line(void *editor);

    /// @brief Get the slot where gutter was clicked.
    /// @param editor CodeEditor handle.
    /// @return Slot number, or -1 if no click.
    int64_t rt_codeeditor_get_gutter_clicked_slot(void *editor);

    /// @brief Set whether to show fold gutter.
    /// @param editor CodeEditor handle.
    /// @param show 1 to show, 0 to hide.
    void rt_codeeditor_set_show_fold_gutter(void *editor, int64_t show);

    //=========================================================================
    // CodeEditor Enhancements - Code Folding (Phase 4)
    //=========================================================================

    /// @brief Add a foldable region.
    /// @param editor CodeEditor handle.
    /// @param start_line Starting line (0-based).
    /// @param end_line Ending line (0-based).
    void rt_codeeditor_add_fold_region(void *editor, int64_t start_line, int64_t end_line);

    /// @brief Remove a foldable region.
    /// @param editor CodeEditor handle.
    /// @param start_line Starting line of region to remove.
    void rt_codeeditor_remove_fold_region(void *editor, int64_t start_line);

    /// @brief Clear all fold regions.
    /// @param editor CodeEditor handle.
    void rt_codeeditor_clear_fold_regions(void *editor);

    /// @brief Fold a region at the specified line.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    void rt_codeeditor_fold(void *editor, int64_t line);

    /// @brief Unfold a region at the specified line.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    void rt_codeeditor_unfold(void *editor, int64_t line);

    /// @brief Toggle fold state at the specified line.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    void rt_codeeditor_toggle_fold(void *editor, int64_t line);

    /// @brief Check if a line is folded.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    /// @return 1 if folded, 0 otherwise.
    int64_t rt_codeeditor_is_folded(void *editor, int64_t line);

    /// @brief Fold all regions.
    /// @param editor CodeEditor handle.
    void rt_codeeditor_fold_all(void *editor);

    /// @brief Unfold all regions.
    /// @param editor CodeEditor handle.
    void rt_codeeditor_unfold_all(void *editor);

    /// @brief Enable/disable automatic fold region detection.
    /// @param editor CodeEditor handle.
    /// @param enable 1 to enable, 0 to disable.
    void rt_codeeditor_set_auto_fold_detection(void *editor, int64_t enable);

    //=========================================================================
    // CodeEditor Enhancements - Multiple Cursors (Phase 4)
    //=========================================================================

    /// @brief Get number of cursors.
    /// @param editor CodeEditor handle.
    /// @return Number of cursors (always >= 1).
    int64_t rt_codeeditor_get_cursor_count(void *editor);

    /// @brief Add a new cursor at the specified position.
    /// @param editor CodeEditor handle.
    /// @param line Line number (0-based).
    /// @param col Column number (0-based).
    void rt_codeeditor_add_cursor(void *editor, int64_t line, int64_t col);

    /// @brief Remove a cursor by index.
    /// @param editor CodeEditor handle.
    /// @param index Cursor index (0 is primary, cannot be removed).
    void rt_codeeditor_remove_cursor(void *editor, int64_t index);

    /// @brief Clear all extra cursors, keeping only the primary.
    /// @param editor CodeEditor handle.
    void rt_codeeditor_clear_extra_cursors(void *editor);

    /// @brief Get cursor line by index.
    /// @param editor CodeEditor handle.
    /// @param index Cursor index.
    /// @return Line number (0-based).
    int64_t rt_codeeditor_get_cursor_line_at(void *editor, int64_t index);

    /// @brief Get cursor column by index.
    /// @param editor CodeEditor handle.
    /// @param index Cursor index.
    /// @return Column number (0-based).
    int64_t rt_codeeditor_get_cursor_col_at(void *editor, int64_t index);

    /// @brief Get primary cursor line (0-based).
    /// @param editor CodeEditor handle.
    /// @return Line number.
    int64_t rt_codeeditor_get_cursor_line(void *editor);

    /// @brief Get primary cursor column (0-based).
    /// @param editor CodeEditor handle.
    /// @return Column number.
    int64_t rt_codeeditor_get_cursor_col(void *editor);

    /// @brief Set cursor position by index.
    /// @param editor CodeEditor handle.
    /// @param index Cursor index.
    /// @param line Line number (0-based).
    /// @param col Column number (0-based).
    void rt_codeeditor_set_cursor_position_at(void *editor,
                                              int64_t index,
                                              int64_t line,
                                              int64_t col);

    /// @brief Set selection for a specific cursor.
    /// @param editor CodeEditor handle.
    /// @param index Cursor index.
    /// @param start_line Selection start line.
    /// @param start_col Selection start column.
    /// @param end_line Selection end line.
    /// @param end_col Selection end column.
    void rt_codeeditor_set_cursor_selection(void *editor,
                                            int64_t index,
                                            int64_t start_line,
                                            int64_t start_col,
                                            int64_t end_line,
                                            int64_t end_col);

    /// @brief Check if cursor has a selection.
    /// @param editor CodeEditor handle.
    /// @param index Cursor index.
    /// @return 1 if has selection, 0 otherwise.
    int64_t rt_codeeditor_cursor_has_selection(void *editor, int64_t index);

//=========================================================================
// Phase 5: MessageBox Dialog
//=========================================================================

/// MessageBox type constants
#define RT_MESSAGEBOX_INFO 0
#define RT_MESSAGEBOX_WARNING 1
#define RT_MESSAGEBOX_ERROR 2
#define RT_MESSAGEBOX_QUESTION 3

    /// @brief Show an info message box.
    /// @param title Dialog title.
    /// @param message Dialog message.
    /// @return Button index clicked (0 = OK).
    int64_t rt_messagebox_info(rt_string title, rt_string message);

    /// @brief Show a warning message box.
    /// @param title Dialog title.
    /// @param message Dialog message.
    /// @return Button index clicked (0 = OK).
    int64_t rt_messagebox_warning(rt_string title, rt_string message);

    /// @brief Show an error message box.
    /// @param title Dialog title.
    /// @param message Dialog message.
    /// @return Button index clicked (0 = OK).
    int64_t rt_messagebox_error(rt_string title, rt_string message);

    /// @brief Show a yes/no question dialog.
    /// @param title Dialog title.
    /// @param message Dialog message.
    /// @return 1 = Yes, 0 = No.
    int64_t rt_messagebox_question(rt_string title, rt_string message);

    /// @brief Show an OK/Cancel confirmation dialog.
    /// @param title Dialog title.
    /// @param message Dialog message.
    /// @return 1 = OK, 0 = Cancel.
    int64_t rt_messagebox_confirm(rt_string title, rt_string message);

    /// @brief Create a custom message box.
    /// @param title Dialog title.
    /// @param message Dialog message.
    /// @param type Message type (RT_MESSAGEBOX_INFO, etc.).
    /// @return MessageBox handle.
    void *rt_messagebox_new(rt_string title, rt_string message, int64_t type);

    /// @brief Add a button to a custom message box.
    /// @param box MessageBox handle.
    /// @param text Button text.
    /// @param id Button ID (returned when clicked).
    void rt_messagebox_add_button(void *box, rt_string text, int64_t id);

    /// @brief Set the default button for a message box.
    /// @param box MessageBox handle.
    /// @param id Button ID to make default.
    void rt_messagebox_set_default_button(void *box, int64_t id);

    /// @brief Show the message box and wait for user response.
    /// @param box MessageBox handle.
    /// @return ID of clicked button.
    int64_t rt_messagebox_show(void *box);

    /// @brief Destroy a message box.
    /// @param box MessageBox handle.
    void rt_messagebox_destroy(void *box);

//=========================================================================
// Phase 5: FileDialog
//=========================================================================

/// FileDialog type constants
#define RT_FILEDIALOG_OPEN 0
#define RT_FILEDIALOG_SAVE 1
#define RT_FILEDIALOG_FOLDER 2

    /// @brief Show a file open dialog (quick version).
    /// @param title Dialog title.
    /// @param default_path Default directory path.
    /// @param filter File filter (e.g., "*.txt;*.md").
    /// @return Selected file path, or empty string if cancelled.
    rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter);

    /// @brief Show a file open dialog for multiple files.
    /// @param title Dialog title.
    /// @param default_path Default directory path.
    /// @param filter File filter.
    /// @return Semicolon-separated list of paths, or empty string if cancelled.
    rt_string rt_filedialog_open_multiple(rt_string title,
                                          rt_string default_path,
                                          rt_string filter);

    /// @brief Show a file save dialog (quick version).
    /// @param title Dialog title.
    /// @param default_path Default directory path.
    /// @param filter File filter.
    /// @param default_name Default file name.
    /// @return Selected file path, or empty string if cancelled.
    rt_string rt_filedialog_save(rt_string title,
                                 rt_string default_path,
                                 rt_string filter,
                                 rt_string default_name);

    /// @brief Show a folder selection dialog (quick version).
    /// @param title Dialog title.
    /// @param default_path Default directory path.
    /// @return Selected folder path, or empty string if cancelled.
    rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path);

    /// @brief Create a custom file dialog.
    /// @param type Dialog type (RT_FILEDIALOG_OPEN, SAVE, or FOLDER).
    /// @return FileDialog handle.
    void *rt_filedialog_new(int64_t type);

    /// @brief Set the title of a file dialog.
    /// @param dialog FileDialog handle.
    /// @param title Dialog title.
    void rt_filedialog_set_title(void *dialog, rt_string title);

    /// @brief Set the initial directory path.
    /// @param dialog FileDialog handle.
    /// @param path Directory path.
    void rt_filedialog_set_path(void *dialog, rt_string path);

    /// @brief Set the file filter (replaces existing).
    /// @param dialog FileDialog handle.
    /// @param name Filter name (e.g., "Text Files").
    /// @param pattern Filter pattern (e.g., "*.txt").
    void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern);

    /// @brief Add an additional file filter.
    /// @param dialog FileDialog handle.
    /// @param name Filter name.
    /// @param pattern Filter pattern.
    void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern);

    /// @brief Set the default file name (for save dialogs).
    /// @param dialog FileDialog handle.
    /// @param name Default file name.
    void rt_filedialog_set_default_name(void *dialog, rt_string name);

    /// @brief Enable/disable multiple file selection.
    /// @param dialog FileDialog handle.
    /// @param multiple 1 to enable, 0 to disable.
    void rt_filedialog_set_multiple(void *dialog, int64_t multiple);

    /// @brief Show the file dialog and wait for user response.
    /// @param dialog FileDialog handle.
    /// @return 1 = OK, 0 = Cancel.
    int64_t rt_filedialog_show(void *dialog);

    /// @brief Get the selected path (single selection).
    /// @param dialog FileDialog handle.
    /// @return Selected path.
    rt_string rt_filedialog_get_path(void *dialog);

    /// @brief Get the number of selected paths (multiple selection).
    /// @param dialog FileDialog handle.
    /// @return Number of selected paths.
    int64_t rt_filedialog_get_path_count(void *dialog);

    /// @brief Get a selected path by index.
    /// @param dialog FileDialog handle.
    /// @param index Path index.
    /// @return Selected path at index.
    rt_string rt_filedialog_get_path_at(void *dialog, int64_t index);

    /// @brief Destroy a file dialog.
    /// @param dialog FileDialog handle.
    void rt_filedialog_destroy(void *dialog);

    //=========================================================================
    // Phase 6: FindBar (Search & Replace)
    //=========================================================================

    /// @brief Create a new find/replace bar.
    /// @param parent Parent widget.
    /// @return FindBar handle.
    void *rt_findbar_new(void *parent);

    /// @brief Destroy a find bar.
    /// @param bar FindBar handle.
    void rt_findbar_destroy(void *bar);

    /// @brief Bind the find bar to a code editor.
    /// @param bar FindBar handle.
    /// @param editor CodeEditor handle.
    void rt_findbar_bind_editor(void *bar, void *editor);

    /// @brief Unbind the find bar from the current editor.
    /// @param bar FindBar handle.
    void rt_findbar_unbind_editor(void *bar);

    /// @brief Set find/replace mode.
    /// @param bar FindBar handle.
    /// @param replace 1 for replace mode, 0 for find only.
    void rt_findbar_set_replace_mode(void *bar, int64_t replace);

    /// @brief Check if in replace mode.
    /// @param bar FindBar handle.
    /// @return 1 if replace mode, 0 if find only.
    int64_t rt_findbar_is_replace_mode(void *bar);

    /// @brief Set the search text.
    /// @param bar FindBar handle.
    /// @param text Text to search for.
    void rt_findbar_set_find_text(void *bar, rt_string text);

    /// @brief Get the current search text.
    /// @param bar FindBar handle.
    /// @return Current search text.
    rt_string rt_findbar_get_find_text(void *bar);

    /// @brief Set the replacement text.
    /// @param bar FindBar handle.
    /// @param text Replacement text.
    void rt_findbar_set_replace_text(void *bar, rt_string text);

    /// @brief Get the current replacement text.
    /// @param bar FindBar handle.
    /// @return Current replacement text.
    rt_string rt_findbar_get_replace_text(void *bar);

    /// @brief Enable/disable case-sensitive search.
    /// @param bar FindBar handle.
    /// @param sensitive 1 for case-sensitive, 0 for case-insensitive.
    void rt_findbar_set_case_sensitive(void *bar, int64_t sensitive);

    /// @brief Check if case-sensitive search is enabled.
    /// @param bar FindBar handle.
    /// @return 1 if case-sensitive, 0 otherwise.
    int64_t rt_findbar_is_case_sensitive(void *bar);

    /// @brief Enable/disable whole word matching.
    /// @param bar FindBar handle.
    /// @param whole 1 for whole word, 0 for partial.
    void rt_findbar_set_whole_word(void *bar, int64_t whole);

    /// @brief Check if whole word matching is enabled.
    /// @param bar FindBar handle.
    /// @return 1 if whole word, 0 otherwise.
    int64_t rt_findbar_is_whole_word(void *bar);

    /// @brief Enable/disable regex search.
    /// @param bar FindBar handle.
    /// @param regex 1 to enable regex, 0 for literal.
    void rt_findbar_set_regex(void *bar, int64_t regex);

    /// @brief Check if regex search is enabled.
    /// @param bar FindBar handle.
    /// @return 1 if regex enabled, 0 otherwise.
    int64_t rt_findbar_is_regex(void *bar);

    /// @brief Find next match.
    /// @param bar FindBar handle.
    /// @return 1 if found, 0 if not found.
    int64_t rt_findbar_find_next(void *bar);

    /// @brief Find previous match.
    /// @param bar FindBar handle.
    /// @return 1 if found, 0 if not found.
    int64_t rt_findbar_find_previous(void *bar);

    /// @brief Replace current match.
    /// @param bar FindBar handle.
    /// @return 1 if replaced, 0 if nothing to replace.
    int64_t rt_findbar_replace(void *bar);

    /// @brief Replace all matches.
    /// @param bar FindBar handle.
    /// @return Number of replacements made.
    int64_t rt_findbar_replace_all(void *bar);

    /// @brief Get total match count.
    /// @param bar FindBar handle.
    /// @return Number of matches found.
    int64_t rt_findbar_get_match_count(void *bar);

    /// @brief Get current match index.
    /// @param bar FindBar handle.
    /// @return Current match index (1-based), 0 if no matches.
    int64_t rt_findbar_get_current_match(void *bar);

    /// @brief Set find bar visibility.
    /// @param bar FindBar handle.
    /// @param visible 1 to show, 0 to hide.
    void rt_findbar_set_visible(void *bar, int64_t visible);

    /// @brief Check if find bar is visible.
    /// @param bar FindBar handle.
    /// @return 1 if visible, 0 otherwise.
    int64_t rt_findbar_is_visible(void *bar);

    /// @brief Focus the find bar input.
    /// @param bar FindBar handle.
    void rt_findbar_focus(void *bar);

    //=========================================================================
    // Phase 6: CommandPalette
    //=========================================================================

    /// @brief Create a new command palette.
    /// @param parent Parent widget.
    /// @return CommandPalette handle.
    void *rt_commandpalette_new(void *parent);

    /// @brief Destroy a command palette.
    /// @param palette CommandPalette handle.
    void rt_commandpalette_destroy(void *palette);

    /// @brief Add a command to the palette.
    /// @param palette CommandPalette handle.
    /// @param id Command identifier.
    /// @param label Display label.
    /// @param category Command category.
    void rt_commandpalette_add_command(void *palette,
                                       rt_string id,
                                       rt_string label,
                                       rt_string category);

    /// @brief Add a command with a keyboard shortcut.
    /// @param palette CommandPalette handle.
    /// @param id Command identifier.
    /// @param label Display label.
    /// @param category Command category.
    /// @param shortcut Keyboard shortcut (e.g., "Ctrl+S").
    void rt_commandpalette_add_command_with_shortcut(
        void *palette, rt_string id, rt_string label, rt_string category, rt_string shortcut);

    /// @brief Remove a command from the palette.
    /// @param palette CommandPalette handle.
    /// @param id Command identifier to remove.
    void rt_commandpalette_remove_command(void *palette, rt_string id);

    /// @brief Clear all commands from the palette.
    /// @param palette CommandPalette handle.
    void rt_commandpalette_clear(void *palette);

    /// @brief Show the command palette.
    /// @param palette CommandPalette handle.
    void rt_commandpalette_show(void *palette);

    /// @brief Hide the command palette.
    /// @param palette CommandPalette handle.
    void rt_commandpalette_hide(void *palette);

    /// @brief Check if the command palette is visible.
    /// @param palette CommandPalette handle.
    /// @return 1 if visible, 0 otherwise.
    int64_t rt_commandpalette_is_visible(void *palette);

    /// @brief Set the placeholder text.
    /// @param palette CommandPalette handle.
    /// @param text Placeholder text.
    void rt_commandpalette_set_placeholder(void *palette, rt_string text);

    /// @brief Get the selected command ID.
    /// @param palette CommandPalette handle.
    /// @return Selected command ID, or empty string if none.
    rt_string rt_commandpalette_get_selected_command(void *palette);

    /// @brief Check if a command was selected since last check.
    /// @param palette CommandPalette handle.
    /// @return 1 if command was selected, 0 otherwise.
    int64_t rt_commandpalette_was_command_selected(void *palette);

    //=========================================================================
    // Phase 7: Tooltip
    //=========================================================================

    /// @brief Show a tooltip at the specified position.
    /// @param text Tooltip text.
    /// @param x X position.
    /// @param y Y position.
    void rt_tooltip_show(rt_string text, int64_t x, int64_t y);

    /// @brief Show a rich tooltip with title and body.
    /// @param title Tooltip title.
    /// @param body Tooltip body text.
    /// @param x X position.
    /// @param y Y position.
    void rt_tooltip_show_rich(rt_string title, rt_string body, int64_t x, int64_t y);

    /// @brief Hide the current tooltip.
    void rt_tooltip_hide(void);

    /// @brief Set the tooltip show delay.
    /// @param delay_ms Delay in milliseconds before showing tooltip.
    void rt_tooltip_set_delay(int64_t delay_ms);

    /// @brief Set tooltip text for a widget.
    /// @param widget Widget handle.
    /// @param text Tooltip text.
    void rt_widget_set_tooltip(void *widget, rt_string text);

    /// @brief Set rich tooltip for a widget.
    /// @param widget Widget handle.
    /// @param title Tooltip title.
    /// @param body Tooltip body text.
    void rt_widget_set_tooltip_rich(void *widget, rt_string title, rt_string body);

    /// @brief Clear tooltip from a widget.
    /// @param widget Widget handle.
    void rt_widget_clear_tooltip(void *widget);

//=========================================================================
// Phase 7: Toast/Notifications
//=========================================================================

// Toast type constants
#define RT_TOAST_INFO 0
#define RT_TOAST_SUCCESS 1
#define RT_TOAST_WARNING 2
#define RT_TOAST_ERROR 3

// Toast position constants
#define RT_TOAST_POSITION_TOP_RIGHT 0
#define RT_TOAST_POSITION_TOP_LEFT 1
#define RT_TOAST_POSITION_BOTTOM_RIGHT 2
#define RT_TOAST_POSITION_BOTTOM_LEFT 3
#define RT_TOAST_POSITION_TOP_CENTER 4
#define RT_TOAST_POSITION_BOTTOM_CENTER 5

    /// @brief Show an info toast notification.
    /// @param message Toast message.
    void rt_toast_info(rt_string message);

    /// @brief Show a success toast notification.
    /// @param message Toast message.
    void rt_toast_success(rt_string message);

    /// @brief Show a warning toast notification.
    /// @param message Toast message.
    void rt_toast_warning(rt_string message);

    /// @brief Show an error toast notification.
    /// @param message Toast message.
    void rt_toast_error(rt_string message);

    /// @brief Create a custom toast notification.
    /// @param message Toast message.
    /// @param type Toast type (RT_TOAST_INFO, etc.).
    /// @param duration_ms Duration in milliseconds (0 for sticky).
    /// @return Toast handle.
    void *rt_toast_new(rt_string message, int64_t type, int64_t duration_ms);

    /// @brief Set an action button on a toast.
    /// @param toast Toast handle.
    /// @param label Action button label.
    void rt_toast_set_action(void *toast, rt_string label);

    /// @brief Check if the toast action was clicked.
    /// @param toast Toast handle.
    /// @return 1 if action was clicked, 0 otherwise.
    int64_t rt_toast_was_action_clicked(void *toast);

    /// @brief Check if the toast was dismissed.
    /// @param toast Toast handle.
    /// @return 1 if dismissed, 0 otherwise.
    int64_t rt_toast_was_dismissed(void *toast);

    /// @brief Dismiss a toast notification.
    /// @param toast Toast handle.
    void rt_toast_dismiss(void *toast);

    /// @brief Set the position for toast notifications.
    /// @param position Position constant (RT_TOAST_POSITION_*).
    void rt_toast_set_position(int64_t position);

    /// @brief Set the maximum number of visible toasts.
    /// @param count Maximum visible toast count.
    void rt_toast_set_max_visible(int64_t count);

    /// @brief Dismiss all toast notifications.
    void rt_toast_dismiss_all(void);

    //=========================================================================
    // Phase 8: Breadcrumb
    //=========================================================================

    /// @brief Create a new breadcrumb widget.
    /// @param parent Parent widget.
    /// @return Breadcrumb handle.
    void *rt_breadcrumb_new(void *parent);

    /// @brief Destroy a breadcrumb widget.
    /// @param crumb Breadcrumb handle.
    void rt_breadcrumb_destroy(void *crumb);

    /// @brief Set breadcrumb path from a path string.
    /// @param crumb Breadcrumb handle.
    /// @param path Path string (e.g., "src/lib/gui").
    /// @param separator Path separator (e.g., "/").
    void rt_breadcrumb_set_path(void *crumb, rt_string path, rt_string separator);

    /// @brief Set breadcrumb items from a comma-separated string.
    /// @param crumb Breadcrumb handle.
    /// @param items Comma-separated items.
    void rt_breadcrumb_set_items(void *crumb, rt_string items);

    /// @brief Add an item to the breadcrumb.
    /// @param crumb Breadcrumb handle.
    /// @param text Item text.
    /// @param data User data string.
    void rt_breadcrumb_add_item(void *crumb, rt_string text, rt_string data);

    /// @brief Clear all breadcrumb items.
    /// @param crumb Breadcrumb handle.
    void rt_breadcrumb_clear(void *crumb);

    /// @brief Check if a breadcrumb item was clicked.
    /// @param crumb Breadcrumb handle.
    /// @return 1 if item was clicked, 0 otherwise.
    int64_t rt_breadcrumb_was_item_clicked(void *crumb);

    /// @brief Get the index of the clicked breadcrumb item.
    /// @param crumb Breadcrumb handle.
    /// @return Clicked item index, or -1 if none.
    int64_t rt_breadcrumb_get_clicked_index(void *crumb);

    /// @brief Get the data of the clicked breadcrumb item.
    /// @param crumb Breadcrumb handle.
    /// @return Clicked item data, or empty string if none.
    rt_string rt_breadcrumb_get_clicked_data(void *crumb);

    /// @brief Set the breadcrumb separator string.
    /// @param crumb Breadcrumb handle.
    /// @param sep Separator string.
    void rt_breadcrumb_set_separator(void *crumb, rt_string sep);

    /// @brief Set the maximum number of visible items.
    /// @param crumb Breadcrumb handle.
    /// @param max Maximum visible items.
    void rt_breadcrumb_set_max_items(void *crumb, int64_t max);

    //=========================================================================
    // Phase 8: Minimap
    //=========================================================================

    /// @brief Create a new minimap widget.
    /// @param parent Parent widget.
    /// @return Minimap handle.
    void *rt_minimap_new(void *parent);

    /// @brief Destroy a minimap widget.
    /// @param minimap Minimap handle.
    void rt_minimap_destroy(void *minimap);

    /// @brief Bind the minimap to a code editor.
    /// @param minimap Minimap handle.
    /// @param editor CodeEditor handle.
    void rt_minimap_bind_editor(void *minimap, void *editor);

    /// @brief Unbind the minimap from its editor.
    /// @param minimap Minimap handle.
    void rt_minimap_unbind_editor(void *minimap);

    /// @brief Set the minimap width.
    /// @param minimap Minimap handle.
    /// @param width Width in pixels.
    void rt_minimap_set_width(void *minimap, int64_t width);

    /// @brief Get the minimap width.
    /// @param minimap Minimap handle.
    /// @return Width in pixels.
    int64_t rt_minimap_get_width(void *minimap);

    /// @brief Set the minimap scale factor.
    /// @param minimap Minimap handle.
    /// @param scale Scale factor (0.05 - 0.2 typical).
    void rt_minimap_set_scale(void *minimap, double scale);

    /// @brief Show or hide the viewport slider on the minimap.
    /// @param minimap Minimap handle.
    /// @param show 1 to show, 0 to hide.
    void rt_minimap_set_show_slider(void *minimap, int64_t show);

// Minimap marker type constants
#define RT_MINIMAP_MARKER_ERROR 0
#define RT_MINIMAP_MARKER_WARNING 1
#define RT_MINIMAP_MARKER_INFO 2
#define RT_MINIMAP_MARKER_SEARCH 3

    /// @brief Add a marker to the minimap.
    /// @param minimap Minimap handle.
    /// @param line Line number.
    /// @param color Marker color (RGBA).
    /// @param type Marker type (RT_MINIMAP_MARKER_*).
    void rt_minimap_add_marker(void *minimap, int64_t line, int64_t color, int64_t type);

    /// @brief Remove markers at a specific line.
    /// @param minimap Minimap handle.
    /// @param line Line number.
    void rt_minimap_remove_markers(void *minimap, int64_t line);

    /// @brief Clear all markers from the minimap.
    /// @param minimap Minimap handle.
    void rt_minimap_clear_markers(void *minimap);

    //=========================================================================
    // Phase 8: Drag and Drop
    //=========================================================================

    /// @brief Set whether a widget is draggable.
    /// @param widget Widget handle.
    /// @param draggable 1 to enable dragging, 0 to disable.
    void rt_widget_set_draggable(void *widget, int64_t draggable);

    /// @brief Set the drag data for a widget.
    /// @param widget Widget handle.
    /// @param type Data type identifier.
    /// @param data Data string.
    void rt_widget_set_drag_data(void *widget, rt_string type, rt_string data);

    /// @brief Check if a widget is currently being dragged.
    /// @param widget Widget handle.
    /// @return 1 if being dragged, 0 otherwise.
    int64_t rt_widget_is_being_dragged(void *widget);

    /// @brief Set whether a widget is a drop target.
    /// @param widget Widget handle.
    /// @param target 1 to enable drop target, 0 to disable.
    void rt_widget_set_drop_target(void *widget, int64_t target);

    /// @brief Set accepted drop types for a widget.
    /// @param widget Widget handle.
    /// @param types Comma-separated list of accepted types.
    void rt_widget_set_accepted_drop_types(void *widget, rt_string types);

    /// @brief Check if a drag is currently over a widget.
    /// @param widget Widget handle.
    /// @return 1 if drag is over widget, 0 otherwise.
    int64_t rt_widget_is_drag_over(void *widget);

    /// @brief Check if a drop occurred on a widget.
    /// @param widget Widget handle.
    /// @return 1 if drop occurred, 0 otherwise.
    int64_t rt_widget_was_dropped(void *widget);

    /// @brief Get the type of the dropped data.
    /// @param widget Widget handle.
    /// @return Drop type string.
    rt_string rt_widget_get_drop_type(void *widget);

    /// @brief Get the dropped data.
    /// @param widget Widget handle.
    /// @return Drop data string.
    rt_string rt_widget_get_drop_data(void *widget);

    /// @brief Check if a file was dropped on the application.
    /// @param app Application handle.
    /// @return 1 if file was dropped, 0 otherwise.
    int64_t rt_app_was_file_dropped(void *app);

    /// @brief Get the number of dropped files.
    /// @param app Application handle.
    /// @return Number of dropped files.
    int64_t rt_app_get_dropped_file_count(void *app);

    /// @brief Get a dropped file path by index.
    /// @param app Application handle.
    /// @param index File index.
    /// @return File path string.
    rt_string rt_app_get_dropped_file(void *app, int64_t index);

#ifdef __cplusplus
}
#endif
