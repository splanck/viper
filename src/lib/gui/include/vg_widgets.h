//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/include/vg_widgets.h
// Purpose: Core widget library — concrete implementations of standard UI
//          controls: Label, Button, TextInput, Checkbox, ScrollView, ListBox,
//          Dropdown, Slider, ProgressBar, RadioButton, Image, Spinner,
//          ColorSwatch, ColorPalette, and ColorPicker.
// Key invariants:
//   - Widget create functions return NULL on allocation failure.
//   - String parameters (text, placeholder) are copied internally.
//   - Callback user_data pointers are stored but never dereferenced by the
//     widget; the application owns the pointed-to data.
//   - Every widget struct's first member is vg_widget_t base, enabling safe
//     pointer up-casts for generic tree operations.
// Ownership/Lifetime:
//   - Created widgets are owned by their parent once added; destroying the
//     parent destroys all children.
//   - Strings passed to setters are copied; the caller may free the original.
// Links: lib/gui/include/vg_widget.h,
//        lib/gui/include/vg_layout.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_font.h,
//        lib/gui/include/vg_ide_widgets.h
//
//===----------------------------------------------------------------------===//
#ifndef VG_WIDGETS_H
#define VG_WIDGETS_H

#include "vg_font.h"
#include "vg_layout.h"
#include "vg_widget.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Label Widget
//=============================================================================

/// @brief Label widget structure
typedef struct vg_label {
    vg_widget_t base;

    const char *text;     ///< Text content (owned, null-terminated)
    vg_font_t *font;      ///< Font for rendering
    float font_size;      ///< Font size in pixels
    uint32_t text_color;  ///< Text color (ARGB)
    vg_h_align_t h_align; ///< Horizontal text alignment
    vg_v_align_t v_align; ///< Vertical text alignment
    bool word_wrap;       ///< Enable word wrapping
    int max_lines;        ///< Maximum lines (0 = unlimited)

    /* Word-wrap line cache — populated by measure, consumed by paint.
     * Private; do not access from outside vg_label.c. */
    char **wrap_line_bufs; ///< malloc'd array of malloc'd line strings
    int wrap_line_count;   ///< number of entries in wrap_line_bufs
    float wrap_cached_w;   ///< wrap_width for which cache is valid (-1 = invalid)
} vg_label_t;

/// @brief Create a new label widget
/// @param parent Parent widget (can be NULL)
/// @param text Initial text content
/// @return New label widget or NULL on failure
vg_label_t *vg_label_create(vg_widget_t *parent, const char *text);

/// @brief Set label text
/// @param label Label widget
/// @param text New text (copied internally)
void vg_label_set_text(vg_label_t *label, const char *text);

/// @brief Get label text
/// @param label Label widget
/// @return Current text (read-only)
const char *vg_label_get_text(vg_label_t *label);

/// @brief Set label font
/// @param label Label widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_label_set_font(vg_label_t *label, vg_font_t *font, float size);

/// @brief Set text color
/// @param label Label widget
/// @param color Text color (ARGB)
void vg_label_set_color(vg_label_t *label, uint32_t color);

/// @brief Set text alignment
/// @param label Label widget
/// @param h_align Horizontal alignment
/// @param v_align Vertical alignment
void vg_label_set_alignment(vg_label_t *label, vg_h_align_t h_align, vg_v_align_t v_align);

//=============================================================================
// Button Widget
//=============================================================================

/// @brief Button callback function type
typedef void (*vg_button_callback_t)(vg_widget_t *button, void *user_data);

/// @brief Button style enumeration
typedef enum vg_button_style {
    VG_BUTTON_STYLE_DEFAULT,   ///< Standard button
    VG_BUTTON_STYLE_PRIMARY,   ///< Primary action button
    VG_BUTTON_STYLE_SECONDARY, ///< Secondary action
    VG_BUTTON_STYLE_DANGER,    ///< Destructive action
    VG_BUTTON_STYLE_TEXT,      ///< Text-only button
    VG_BUTTON_STYLE_ICON       ///< Icon button
} vg_button_style_t;

/// @brief Button widget structure
typedef struct vg_button {
    vg_widget_t base;

    const char *text;              ///< Button text (owned)
    vg_font_t *font;               ///< Font for text
    float font_size;               ///< Font size
    vg_button_style_t style;       ///< Button style
    vg_button_callback_t on_click; ///< Click callback
    void *user_data;               ///< User data for callback

    // Appearance
    uint32_t bg_color;     ///< Background color
    uint32_t fg_color;     ///< Text color
    uint32_t border_color; ///< Border color
    float border_radius;   ///< Corner radius

    // Icon
    char *icon_text; ///< UTF-8 icon/emoji string (NULL = no icon, owned by button)
    int icon_pos;    ///< Icon position: 0 = left of text (default), 1 = right of text
} vg_button_t;

/// @brief Create a new button widget
/// @param parent Parent widget (can be NULL)
/// @param text Button text
/// @return New button widget or NULL on failure
vg_button_t *vg_button_create(vg_widget_t *parent, const char *text);

/// @brief Set button text
/// @param button Button widget
/// @param text New text (copied internally)
void vg_button_set_text(vg_button_t *button, const char *text);

/// @brief Get the current label text of a button.
/// @param button Button widget
/// @return Pointer to internal text string (valid until next vg_button_set_text call), or NULL
const char *vg_button_get_text(vg_button_t *button);

/// @brief Set button click callback
/// @param button Button widget
/// @param callback Click handler function
/// @param user_data User data passed to callback
void vg_button_set_on_click(vg_button_t *button, vg_button_callback_t callback, void *user_data);

/// @brief Set button style
/// @param button Button widget
/// @param style Button style
void vg_button_set_style(vg_button_t *button, vg_button_style_t style);

/// @brief Set button font
/// @param button Button widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_button_set_font(vg_button_t *button, vg_font_t *font, float size);

/// @brief Set the icon text shown on the button.
///
/// @details The string is copied internally. Pass NULL to remove the icon.
///          Common usage is to pass a UTF-8 emoji or icon font glyph.
///
/// @param button Button widget.
/// @param icon   UTF-8 icon string (copied), or NULL to clear.
void vg_button_set_icon(vg_button_t *button, const char *icon);

/// @brief Set which side of the label the icon appears on.
///
/// @param button Button widget.
/// @param pos    0 = left of label (default), 1 = right of label.
void vg_button_set_icon_position(vg_button_t *button, int pos);

//=============================================================================
// TextInput Widget
//=============================================================================

/// @brief Text input callback for text changes
typedef void (*vg_text_change_callback_t)(vg_widget_t *input, const char *text, void *user_data);

/// @brief Text input widget structure
typedef struct vg_textinput {
    vg_widget_t base;

    char *text;             ///< Current text content (owned)
    size_t text_len;        ///< Current text length in bytes
    size_t text_capacity;   ///< Allocated capacity in bytes
    size_t cursor_pos;      ///< Cursor position (UTF-8 codepoint index)
    size_t selection_start; ///< Selection start position (UTF-8 codepoint index)
    size_t selection_end;   ///< Selection end position (UTF-8 codepoint index)

    const char *placeholder; ///< Placeholder text (owned)
    vg_font_t *font;         ///< Font for rendering
    float font_size;         ///< Font size

    int max_length;     ///< Maximum text length (0 = unlimited)
    bool password_mode; ///< Show dots instead of characters
    bool read_only;     ///< Prevent text modification
    bool multiline;     ///< Allow multiple lines

    // Appearance
    uint32_t text_color;        ///< Text color
    uint32_t placeholder_color; ///< Placeholder text color
    uint32_t selection_color;   ///< Selection highlight color
    uint32_t cursor_color;      ///< Cursor color
    uint32_t bg_color;          ///< Background color
    uint32_t border_color;      ///< Border color

    // Scrolling (for multiline)
    float scroll_x; ///< Horizontal scroll offset
    float scroll_y; ///< Vertical scroll offset

    // Callbacks
    vg_text_change_callback_t on_change;
    void *on_change_data;
    vg_text_change_callback_t on_commit; ///< Called when Enter is pressed (single-line)
    void *on_commit_data;

    // Internal state
    float cursor_blink_time; ///< Cursor blink timer
    bool cursor_visible;     ///< Cursor visibility state

    // Undo/redo ring buffer (max 32 snapshots)
    char *undo_stack[32];    ///< strdup'd text snapshots; NULL = empty slot
    size_t undo_cursors[32]; ///< Cursor position at time of each snapshot
    int undo_head;           ///< Index of the most-recent (top) snapshot (0..31)
    int undo_count;          ///< Total number of valid snapshots
    int undo_pos; ///< Current position in stack (for redo; equals undo_count after normal
                  ///< edits)
} vg_textinput_t;

/// @brief Create a new text input widget
/// @param parent Parent widget (can be NULL)
/// @return New text input widget or NULL on failure
vg_textinput_t *vg_textinput_create(vg_widget_t *parent);

/// @brief Set text input content
/// @param input Text input widget
/// @param text New text (copied internally)
void vg_textinput_set_text(vg_textinput_t *input, const char *text);

/// @brief Get text input content
/// @param input Text input widget
/// @return Current text (read-only)
const char *vg_textinput_get_text(vg_textinput_t *input);

/// @brief Set placeholder text
/// @param input Text input widget
/// @param placeholder Placeholder text (copied internally)
void vg_textinput_set_placeholder(vg_textinput_t *input, const char *placeholder);

/// @brief Set text change callback
/// @param input Text input widget
/// @param callback Change handler function
/// @param user_data User data passed to callback
void vg_textinput_set_on_change(vg_textinput_t *input,
                                vg_text_change_callback_t callback,
                                void *user_data);

/// @brief Set commit callback (called when Enter is pressed in single-line mode)
/// @param input Text input widget
/// @param callback Commit handler function
/// @param user_data User data passed to callback
void vg_textinput_set_on_commit(vg_textinput_t *input,
                                vg_text_change_callback_t callback,
                                void *user_data);

/// @brief Set cursor position
/// @param input Text input widget
/// @param pos Cursor position (UTF-8 codepoint index)
void vg_textinput_set_cursor(vg_textinput_t *input, size_t pos);

/// @brief Select text range
/// @param input Text input widget
/// @param start Selection start
/// @param end Selection end
void vg_textinput_select(vg_textinput_t *input, size_t start, size_t end);

/// @brief Select all text
/// @param input Text input widget
void vg_textinput_select_all(vg_textinput_t *input);

/// @brief Insert text at cursor position
/// @param input Text input widget
/// @param text Text to insert
void vg_textinput_insert(vg_textinput_t *input, const char *text);

/// @brief Delete selected text
/// @param input Text input widget
void vg_textinput_delete_selection(vg_textinput_t *input);

/// @brief Copy the selected text into a newly allocated UTF-8 string.
/// @param input Text input widget
/// @return Newly allocated selection text, or NULL when nothing is selected.
char *vg_textinput_get_selection(vg_textinput_t *input);

/// @brief Set font for text input
/// @param input Text input widget
/// @param font Font to use; NULL keeps the existing font
/// @param size Finite positive font size in pixels; invalid values use the theme normal size
void vg_textinput_set_font(vg_textinput_t *input, vg_font_t *font, float size);

/// @brief Advance cursor blink timer; call each frame with elapsed seconds
/// @param input Text input widget
/// @param dt Elapsed time in seconds since last call; NaN, infinity, and negative values are ignored
void vg_textinput_tick(vg_textinput_t *input, float dt);

//=============================================================================
// Checkbox Widget
//=============================================================================

/// @brief Checkbox state change callback
typedef void (*vg_checkbox_callback_t)(vg_widget_t *checkbox, bool checked, void *user_data);

/// @brief Checkbox widget structure
typedef struct vg_checkbox {
    vg_widget_t base;

    const char *text;   ///< Label text (owned)
    vg_font_t *font;    ///< Font for label
    float font_size;    ///< Font size
    bool checked;       ///< Checked state
    bool indeterminate; ///< Indeterminate state (tri-state)

    // Appearance
    float box_size;       ///< Checkbox box size
    float gap;            ///< Gap between box and label
    uint32_t check_color; ///< Check mark color
    uint32_t box_color;   ///< Box background color
    uint32_t text_color;  ///< Text color

    // Callback
    vg_checkbox_callback_t on_change;
    void *on_change_data;
} vg_checkbox_t;

/// @brief Create a new checkbox widget
/// @param parent Parent widget (can be NULL)
/// @param text Checkbox label text
/// @return New checkbox widget or NULL on failure
vg_checkbox_t *vg_checkbox_create(vg_widget_t *parent, const char *text);

/// @brief Set checkbox checked state
/// @param checkbox Checkbox widget
/// @param checked New checked state
void vg_checkbox_set_checked(vg_checkbox_t *checkbox, bool checked);

/// @brief Get checkbox checked state
/// @param checkbox Checkbox widget
/// @return Current checked state
bool vg_checkbox_is_checked(vg_checkbox_t *checkbox);

/// @brief Toggle checkbox state
/// @param checkbox Checkbox widget
void vg_checkbox_toggle(vg_checkbox_t *checkbox);

/// @brief Set checkbox text
/// @param checkbox Checkbox widget
/// @param text New text (copied internally)
void vg_checkbox_set_text(vg_checkbox_t *checkbox, const char *text);

/// @brief Set state change callback
/// @param checkbox Checkbox widget
/// @param callback Change handler function
/// @param user_data User data passed to callback
void vg_checkbox_set_on_change(vg_checkbox_t *checkbox,
                               vg_checkbox_callback_t callback,
                               void *user_data);

/// @brief Set checkbox label font
/// @param checkbox Checkbox widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_checkbox_set_font(vg_checkbox_t *checkbox, vg_font_t *font, float size);

/// @brief Set the indeterminate (tri-state) state of a checkbox.
/// @param checkbox     Checkbox widget.
/// @param indeterminate true to show dash (indeterminate); false to clear it.
void vg_checkbox_set_indeterminate(vg_checkbox_t *checkbox, bool indeterminate);

/// @brief Return true when the checkbox is currently in its indeterminate state.
bool vg_checkbox_is_indeterminate(vg_checkbox_t *checkbox);

//=============================================================================
// ScrollView Widget
//=============================================================================

/// @brief Scroll direction flags
typedef enum vg_scroll_direction {
    VG_SCROLL_HORIZONTAL = (1 << 0),
    VG_SCROLL_VERTICAL = (1 << 1),
    VG_SCROLL_BOTH = VG_SCROLL_HORIZONTAL | VG_SCROLL_VERTICAL
} vg_scroll_direction_t;

/// @brief ScrollView widget structure
typedef struct vg_scrollview {
    vg_widget_t base;

    float scroll_x;                  ///< Horizontal scroll position
    float scroll_y;                  ///< Vertical scroll position
    float content_width;             ///< Effective content width after auto/explicit resolution
    float content_height;            ///< Effective content height after auto/explicit resolution
    float explicit_content_width;    ///< Caller-provided content width, when has_explicit_content_width
    float explicit_content_height;   ///< Caller-provided content height, when has_explicit_content_height
    bool has_explicit_content_width; ///< Keep width fixed instead of deriving from children
    bool has_explicit_content_height;///< Keep height fixed instead of deriving from children
    vg_scroll_direction_t direction; ///< Scroll direction

    // Scrollbars
    bool show_h_scrollbar;     ///< Show horizontal scrollbar
    bool show_v_scrollbar;     ///< Show vertical scrollbar
    bool auto_hide_scrollbars; ///< Auto-hide scrollbars when not needed
    float scrollbar_width;     ///< Scrollbar width

    // Scrollbar appearance
    uint32_t track_color;       ///< Scrollbar track color
    uint32_t thumb_color;       ///< Scrollbar thumb color
    uint32_t thumb_hover_color; ///< Thumb color when hovered

    // State
    bool h_scrollbar_hovered;  ///< Is horizontal scrollbar hovered
    bool v_scrollbar_hovered;  ///< Is vertical scrollbar hovered
    bool h_scrollbar_dragging; ///< Is horizontal scrollbar being dragged
    bool v_scrollbar_dragging; ///< Is vertical scrollbar being dragged
    float drag_offset;         ///< Drag offset for scrollbar
} vg_scrollview_t;

/// @brief Create a new scroll view widget
/// @param parent Parent widget (can be NULL)
/// @return New scroll view widget or NULL on failure
vg_scrollview_t *vg_scrollview_create(vg_widget_t *parent);

/// @brief Set scroll position
/// @param scroll Scroll view widget
/// @param x Horizontal scroll position
/// @param y Vertical scroll position
void vg_scrollview_set_scroll(vg_scrollview_t *scroll, float x, float y);

/// @brief Get scroll position
/// @param scroll Scroll view widget
/// @param out_x Pointer to receive X position (can be NULL)
/// @param out_y Pointer to receive Y position (can be NULL)
void vg_scrollview_get_scroll(vg_scrollview_t *scroll, float *out_x, float *out_y);

/// @brief Set content size
/// @param scroll Scroll view widget
/// @param width Content width (0 = auto)
/// @param height Content height (0 = auto)
void vg_scrollview_set_content_size(vg_scrollview_t *scroll, float width, float height);

/// @brief Scroll to make a child widget visible
/// @param scroll Scroll view widget
/// @param child Child widget to scroll into view
void vg_scrollview_scroll_to_widget(vg_scrollview_t *scroll, vg_widget_t *child);

/// @brief Set scroll direction
/// @param scroll Scroll view widget
/// @param direction Scroll direction flags
void vg_scrollview_set_direction(vg_scrollview_t *scroll, vg_scroll_direction_t direction);

//=============================================================================
// ListBox Widget
//=============================================================================

/// @brief ListBox item structure
typedef struct vg_listbox_item {
    uint64_t magic;      ///< Live-item sentinel for stale handle detection
    struct vg_listbox *owner; ///< Owning listbox while the item is live
    char *text;          ///< Item text (owned)
    void *user_data;     ///< User data
    bool owns_user_data; ///< Free user_data when the item is destroyed
    bool selected;       ///< Is item selected
    struct vg_listbox_item *next;
    struct vg_listbox_item *prev;
    struct vg_listbox_item *retired_next; ///< Retired-item list link
} vg_listbox_item_t;

/// @brief ListBox selection callback
typedef void (*vg_listbox_callback_t)(vg_widget_t *listbox,
                                      vg_listbox_item_t *item,
                                      void *user_data);

// Forward declaration for icon
struct vg_icon;

/// @brief Virtual mode data provider callback
/// @param listbox ListBox widget
/// @param index Item index
/// @param text Output: item text (must NOT be freed by caller)
/// @param icon Output: item icon (optional, can be NULL)
/// @param user_data User data
typedef void (*vg_listbox_data_provider_t)(
    vg_widget_t *listbox, size_t index, const char **text, struct vg_icon *icon, void *user_data);

/// @brief Virtual item cache entry
typedef struct vg_listbox_cache_entry {
    char *text;    ///< Cached text (owned)
    bool selected; ///< Selection state
} vg_listbox_cache_entry_t;

/// @brief ListBox widget structure
typedef struct vg_listbox {
    vg_widget_t base;

    vg_listbox_item_t *first_item;    ///< First item
    vg_listbox_item_t *last_item;     ///< Last item
    int item_count;                   ///< Number of items
    vg_listbox_item_t *selected;      ///< Currently selected item
    vg_listbox_item_t *prev_selected; ///< Previous selection (for change detection)
    vg_listbox_item_t *anchor_selected; ///< Range-selection anchor (non-virtual mode)
    vg_listbox_item_t *hovered;       ///< Currently hovered item
    vg_listbox_item_t *retired_items; ///< Detached stale handles freed when listbox is destroyed

    vg_font_t *font;   ///< Font for rendering
    float font_size;   ///< Font size
    float item_height; ///< Height of each item
    float scroll_y;    ///< Vertical scroll position

    bool multi_select; ///< Allow multiple selection

    // Virtual scrolling mode
    bool virtual_mode;                        ///< Enable virtual scrolling
    size_t total_item_count;                  ///< Total items (when virtual)
    vg_listbox_data_provider_t data_provider; ///< Data provider callback
    void *data_provider_user_data;            ///< Data provider user data

    // Virtual mode visible range
    size_t visible_start; ///< First visible item index
    size_t visible_count; ///< Number of visible items

    // Virtual mode cache
    vg_listbox_cache_entry_t *visible_cache; ///< Cache for visible items
    size_t cache_capacity;                   ///< Cache capacity

    // Virtual mode selection (bitmap for large lists)
    bool *selection_bitmap;       ///< Selection state for virtual mode
    size_t selection_bitmap_size; ///< Bitmap size
    size_t selected_index;        ///< Currently selected index (virtual mode)
    size_t prev_selected_index;   ///< Previous selected index (virtual mode change detection)
    size_t anchor_selected_index; ///< Range-selection anchor (virtual mode)
    size_t hovered_index;         ///< Currently hovered index (virtual mode)

    // Appearance
    uint32_t bg_color;     ///< Background color
    uint32_t item_bg;      ///< Item background
    uint32_t selected_bg;  ///< Selected item background
    uint32_t hover_bg;     ///< Hovered item background
    uint32_t text_color;   ///< Text color
    uint32_t border_color; ///< Border color

    // Callbacks
    vg_listbox_callback_t on_select;
    void *on_select_data;
    vg_listbox_callback_t on_activate; ///< Double-click
    void *on_activate_data;
} vg_listbox_t;

/// @brief Create a new listbox widget.
/// @param parent Parent widget (can be NULL).
/// @return New listbox widget or NULL on failure.
vg_listbox_t *vg_listbox_create(vg_widget_t *parent);

/// @brief Add an item to the listbox.
/// @param listbox  ListBox widget.
/// @param text     Item label text (copied internally).
/// @param user_data Caller-owned data associated with the item.
/// @return Pointer to the new item, or NULL on allocation failure.
vg_listbox_item_t *vg_listbox_add_item(vg_listbox_t *listbox, const char *text, void *user_data);

/// @brief Remove an item from the listbox and free it.
/// @param listbox ListBox widget.
/// @param item    Item to remove (must belong to @p listbox).
void vg_listbox_remove_item(vg_listbox_t *listbox, vg_listbox_item_t *item);

/// @brief Remove and free all items in the listbox.
/// @param listbox ListBox widget.
void vg_listbox_clear(vg_listbox_t *listbox);

/// @brief Select an item (deselects the current selection first).
/// @param listbox ListBox widget.
/// @param item    Item to select, or NULL to deselect all.
void vg_listbox_select(vg_listbox_t *listbox, vg_listbox_item_t *item);

/// @brief Get the currently selected item.
/// @param listbox ListBox widget.
/// @return Selected item pointer, or NULL if nothing is selected.
vg_listbox_item_t *vg_listbox_get_selected(vg_listbox_t *listbox);

/// @brief Return true when an item handle still belongs to a live listbox.
/// @param item Item handle to test.
/// @return true if the item is still live; false if it has been removed or the listbox destroyed.
bool vg_listbox_item_is_live(const vg_listbox_item_t *item);

/// @brief Set the font used for item text rendering.
/// @param listbox ListBox widget.
/// @param font    Font handle.
/// @param size    Font size in pixels.
void vg_listbox_set_font(vg_listbox_t *listbox, vg_font_t *font, float size);

/// @brief Set the callback fired when the selection changes.
/// @param listbox   ListBox widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_listbox_set_on_select(vg_listbox_t *listbox,
                              vg_listbox_callback_t callback,
                              void *user_data);

// --- Virtual Scrolling ---

/// @brief Enable virtual scrolling mode
/// @param listbox ListBox widget
/// @param enabled Enable virtual mode
/// @param total_count Total number of items
/// @param item_height Fixed item height (required for virtual mode)
void vg_listbox_set_virtual_mode(vg_listbox_t *listbox,
                                 bool enabled,
                                 size_t total_count,
                                 float item_height);

/// @brief Set data provider for virtual mode
/// @param listbox ListBox widget
/// @param provider Data provider callback
/// @param user_data User data
void vg_listbox_set_data_provider(vg_listbox_t *listbox,
                                  vg_listbox_data_provider_t provider,
                                  void *user_data);

/// @brief Update total count (e.g., after filtering) in virtual mode
/// @param listbox ListBox widget
/// @param count New total count
void vg_listbox_set_total_count(vg_listbox_t *listbox, size_t count);

/// @brief Invalidate all cached items (force refresh)
/// @param listbox ListBox widget
void vg_listbox_invalidate_items(vg_listbox_t *listbox);

/// @brief Invalidate a specific item in the cache
/// @param listbox ListBox widget
/// @param index Item index to invalidate
void vg_listbox_invalidate_item(vg_listbox_t *listbox, size_t index);

/// @brief Select item by index (virtual mode)
/// @param listbox ListBox widget
/// @param index Item index
void vg_listbox_select_index(vg_listbox_t *listbox, size_t index);

/// @brief Get selected index (virtual mode)
/// @param listbox ListBox widget
/// @return Selected index or SIZE_MAX if none
size_t vg_listbox_get_selected_index(vg_listbox_t *listbox);

//=============================================================================
// Dropdown/ComboBox Widget
//=============================================================================

/// @brief Dropdown selection callback
typedef void (*vg_dropdown_callback_t)(vg_widget_t *dropdown,
                                       int index,
                                       const char *text,
                                       void *user_data);

/// @brief Dropdown widget structure
typedef struct vg_dropdown {
    vg_widget_t base;

    char **items;       ///< Array of item strings (owned)
    int item_count;     ///< Number of items
    int item_capacity;  ///< Allocated capacity
    int selected_index; ///< Currently selected index (-1 = none)

    vg_font_t *font;   ///< Font for rendering
    float font_size;   ///< Font size
    char *placeholder; ///< Placeholder when nothing selected (owned)

    bool open;             ///< Is dropdown list open
    int hovered_index;     ///< Hovered item index
    float dropdown_height; ///< Max height of dropdown list
    float scroll_y;        ///< Scroll position when list is long

    // Appearance
    uint32_t bg_color;     ///< Background color
    uint32_t text_color;   ///< Text color
    uint32_t border_color; ///< Border color
    uint32_t dropdown_bg;  ///< Dropdown list background
    uint32_t hover_bg;     ///< Hovered item background
    uint32_t selected_bg;  ///< Selected item in list background
    float arrow_size;      ///< Dropdown arrow size

    // Callbacks
    vg_dropdown_callback_t on_change;
    void *on_change_data;
} vg_dropdown_t;

/// @brief Create a new dropdown widget.
/// @param parent Parent widget (can be NULL).
/// @return New dropdown widget or NULL on failure.
vg_dropdown_t *vg_dropdown_create(vg_widget_t *parent);

/// @brief Add an item to the dropdown.
/// @param dropdown Dropdown widget.
/// @param text     Item label text (copied internally).
/// @return Index of the newly added item.
int vg_dropdown_add_item(vg_dropdown_t *dropdown, const char *text);

/// @brief Remove an item by index.
/// @param dropdown Dropdown widget.
/// @param index    Zero-based index of the item to remove.
void vg_dropdown_remove_item(vg_dropdown_t *dropdown, int index);

/// @brief Remove and free all items from the dropdown.
/// @param dropdown Dropdown widget.
void vg_dropdown_clear(vg_dropdown_t *dropdown);

/// @brief Set the currently selected item by index.
/// @param dropdown Dropdown widget.
/// @param index    Zero-based item index, or -1 to show the placeholder.
void vg_dropdown_set_selected(vg_dropdown_t *dropdown, int index);

/// @brief Get the currently selected item index.
/// @param dropdown Dropdown widget.
/// @return Zero-based index of the selected item, or -1 if nothing is selected.
int vg_dropdown_get_selected(vg_dropdown_t *dropdown);

/// @brief Get the text of the currently selected item.
/// @param dropdown Dropdown widget.
/// @return Read-only pointer to the selected item's text, or NULL if nothing is selected.
const char *vg_dropdown_get_selected_text(vg_dropdown_t *dropdown);

/// @brief Set placeholder text shown when no item is selected.
/// @param dropdown Dropdown widget.
/// @param text     Placeholder text (copied internally; NULL clears it).
void vg_dropdown_set_placeholder(vg_dropdown_t *dropdown, const char *text);

/// @brief Set the font used for item text rendering.
/// @param dropdown Dropdown widget.
/// @param font     Font handle.
/// @param size     Font size in pixels.
void vg_dropdown_set_font(vg_dropdown_t *dropdown, vg_font_t *font, float size);

/// @brief Set the callback fired when the selection changes.
/// @param dropdown  Dropdown widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_dropdown_set_on_change(vg_dropdown_t *dropdown,
                               vg_dropdown_callback_t callback,
                               void *user_data);

//=============================================================================
// Slider Widget
//=============================================================================

/// @brief Slider orientation
typedef enum vg_slider_orientation {
    VG_SLIDER_HORIZONTAL,
    VG_SLIDER_VERTICAL
} vg_slider_orientation_t;

/// @brief Slider value change callback
typedef void (*vg_slider_callback_t)(vg_widget_t *slider, float value, void *user_data);

/// @brief Slider widget structure
typedef struct vg_slider {
    vg_widget_t base;

    float value;     ///< Current value
    float min_value; ///< Minimum value
    float max_value; ///< Maximum value
    float step;      ///< Step increment (0 = continuous)
    vg_slider_orientation_t orientation;

    // Appearance
    float track_thickness;      ///< Track thickness
    float thumb_size;           ///< Thumb diameter
    uint32_t track_color;       ///< Track color
    uint32_t fill_color;        ///< Filled portion color
    uint32_t thumb_color;       ///< Thumb color
    uint32_t thumb_hover_color; ///< Thumb hover color

    // Display
    bool show_value; ///< Show value label
    vg_font_t *font; ///< Font for value label
    float font_size; ///< Font size

    // State
    bool dragging;      ///< Is thumb being dragged
    bool thumb_hovered; ///< Is thumb hovered

    // Callbacks
    vg_slider_callback_t on_change;
    void *on_change_data;
} vg_slider_t;

/// @brief Create a new slider widget.
/// @param parent      Parent widget (can be NULL).
/// @param orientation VG_SLIDER_HORIZONTAL or VG_SLIDER_VERTICAL.
/// @return New slider widget or NULL on failure.
vg_slider_t *vg_slider_create(vg_widget_t *parent, vg_slider_orientation_t orientation);

/// @brief Set the current slider value (clamped to [min, max]).
/// @param slider Slider widget.
/// @param value  New value.
void vg_slider_set_value(vg_slider_t *slider, float value);

/// @brief Get the current slider value.
/// @param slider Slider widget.
/// @return Current value in [min, max].
float vg_slider_get_value(vg_slider_t *slider);

/// @brief Set the allowed value range.
/// @param slider  Slider widget.
/// @param min_val Minimum value.
/// @param max_val Maximum value.
void vg_slider_set_range(vg_slider_t *slider, float min_val, float max_val);

/// @brief Set the discrete step increment.
/// @param slider Slider widget.
/// @param step   Step size; pass 0 for continuous movement.
void vg_slider_set_step(vg_slider_t *slider, float step);

/// @brief Set the callback fired when the value changes.
/// @param slider    Slider widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_slider_set_on_change(vg_slider_t *slider, vg_slider_callback_t callback, void *user_data);

//=============================================================================
// ProgressBar Widget
//=============================================================================

/// @brief ProgressBar style
typedef enum vg_progress_style {
    VG_PROGRESS_BAR,          ///< Standard horizontal bar
    VG_PROGRESS_CIRCULAR,     ///< Circular progress
    VG_PROGRESS_INDETERMINATE ///< Indeterminate animation
} vg_progress_style_t;

/// @brief ProgressBar widget structure
typedef struct vg_progressbar {
    vg_widget_t base;

    float value;               ///< Current value (0-1)
    vg_progress_style_t style; ///< Progress style

    // Appearance
    uint32_t track_color; ///< Track/background color
    uint32_t fill_color;  ///< Fill/progress color
    float corner_radius;  ///< Corner radius for bar style

    // Display
    bool show_percentage; ///< Show percentage text
    vg_font_t *font;      ///< Font for percentage
    float font_size;      ///< Font size

    // Animation (for indeterminate)
    float animation_phase; ///< Current animation phase
} vg_progressbar_t;

/// @brief Create a new progress bar widget.
/// @param parent Parent widget (can be NULL).
/// @return New progress bar widget or NULL on failure.
vg_progressbar_t *vg_progressbar_create(vg_widget_t *parent);

/// @brief Set the progress value (clamped to [0, 1]).
/// @param progress Progress bar widget.
/// @param value    Normalised progress in the range [0.0, 1.0].
void vg_progressbar_set_value(vg_progressbar_t *progress, float value);

/// @brief Get the current progress value.
/// @param progress Progress bar widget.
/// @return Normalised progress in [0.0, 1.0].
float vg_progressbar_get_value(vg_progressbar_t *progress);

/// @brief Set the rendering style.
/// @param progress Progress bar widget.
/// @param style    One of VG_PROGRESS_BAR, VG_PROGRESS_CIRCULAR, or VG_PROGRESS_INDETERMINATE.
void vg_progressbar_set_style(vg_progressbar_t *progress, vg_progress_style_t style);

/// @brief Show or hide the percentage text label.
/// @param progress Progress bar widget.
/// @param show     true to overlay the percentage string on the bar.
void vg_progressbar_show_percentage(vg_progressbar_t *progress, bool show);

/// @brief Advance indeterminate animation; call each frame with elapsed seconds
/// @param progress Progress bar widget
/// @param dt Elapsed time in seconds since last call
void vg_progressbar_tick(vg_progressbar_t *progress, float dt);

//=============================================================================
// RadioButton Widget
//=============================================================================

/// @brief RadioButton group - manages mutual exclusivity
typedef struct vg_radiogroup {
    struct vg_radiobutton **buttons; ///< Array of buttons in group
    int button_count;                ///< Number of buttons
    int button_capacity;             ///< Allocated capacity
    int selected_index;              ///< Currently selected index
} vg_radiogroup_t;

/// @brief RadioButton callback
typedef void (*vg_radio_callback_t)(vg_widget_t *radio, bool selected, void *user_data);

/// @brief RadioButton widget structure
typedef struct vg_radiobutton {
    vg_widget_t base;

    const char *text;       ///< Label text (owned)
    vg_font_t *font;        ///< Font for label
    float font_size;        ///< Font size
    bool selected;          ///< Is this button selected
    vg_radiogroup_t *group; ///< Group this button belongs to

    // Appearance
    float circle_size;     ///< Radio circle size
    float gap;             ///< Gap between circle and label
    uint32_t circle_color; ///< Circle border color
    uint32_t fill_color;   ///< Selected fill color
    uint32_t text_color;   ///< Text color

    // Callback
    vg_radio_callback_t on_change;
    void *on_change_data;
} vg_radiobutton_t;

/// @brief Create a new radio group to manage mutual exclusivity.
/// @return Newly allocated radio group, or NULL on failure.
vg_radiogroup_t *vg_radiogroup_create(void);

/// @brief Destroy a radio group (does not destroy the member buttons).
/// @param group Radio group to destroy (may be NULL).
void vg_radiogroup_destroy(vg_radiogroup_t *group);

/// @brief Create a new radio button and register it in a group.
/// @param parent Parent widget (can be NULL).
/// @param text   Label text (copied internally).
/// @param group  Group that enforces mutual exclusivity (can be NULL for standalone).
/// @return New radio button widget or NULL on failure.
vg_radiobutton_t *vg_radiobutton_create(vg_widget_t *parent,
                                        const char *text,
                                        vg_radiogroup_t *group);

/// @brief Set this radio button's selected state and deselect siblings in its group.
/// @param radio    Radio button widget.
/// @param selected true to select; false to deselect.
void vg_radiobutton_set_selected(vg_radiobutton_t *radio, bool selected);

/// @brief Check whether this radio button is currently selected.
/// @param radio Radio button widget.
/// @return true if selected.
bool vg_radiobutton_is_selected(vg_radiobutton_t *radio);

/// @brief Get the index of the currently selected button within the group.
/// @param group Radio group.
/// @return Zero-based index of the selected button, or -1 if none is selected.
int vg_radiogroup_get_selected(vg_radiogroup_t *group);

/// @brief Select the button at the given index within the group.
/// @param group Radio group.
/// @param index Zero-based index of the button to select.
void vg_radiogroup_set_selected(vg_radiogroup_t *group, int index);

//=============================================================================
// Image Widget
//=============================================================================

/// @brief Image scaling mode
typedef enum vg_image_scale {
    VG_IMAGE_SCALE_NONE,   ///< No scaling (original size)
    VG_IMAGE_SCALE_FIT,    ///< Scale to fit, maintain aspect ratio
    VG_IMAGE_SCALE_FILL,   ///< Scale to fill, may crop
    VG_IMAGE_SCALE_STRETCH ///< Stretch to fill (distorts)
} vg_image_scale_t;

/// @brief Image widget structure
typedef struct vg_image {
    vg_widget_t base;

    uint8_t *pixels;             ///< Pixel data (RGBA, owned)
    int img_width;               ///< Original image width
    int img_height;              ///< Original image height
    vg_image_scale_t scale_mode; ///< Scaling mode

    // Appearance
    uint32_t bg_color;   ///< Background color (shown if image doesn't fill)
    float opacity;       ///< Image opacity (0-1)
    float corner_radius; ///< Corner radius for rounded images
} vg_image_t;

/// @brief Create a new image widget with no initial pixel data.
/// @param parent Parent widget (can be NULL).
/// @return New image widget or NULL on failure.
vg_image_t *vg_image_create(vg_widget_t *parent);

/// @brief Set image pixel data from an RGBA buffer (copied internally).
/// @param image  Image widget.
/// @param pixels RGBA pixel data (4 bytes per pixel).
/// @param width  Image width in pixels.
/// @param height Image height in pixels.
void vg_image_set_pixels(vg_image_t *image, const uint8_t *pixels, int width, int height);

/// @brief Load image pixel data from a file (PNG, JPEG, or BMP).
/// @param image Image widget.
/// @param path  Filesystem path to the image file (UTF-8).
/// @return true on success; false if the file cannot be read or decoded.
bool vg_image_load_file(vg_image_t *image, const char *path);

/// @brief Free the current pixel data and reset the image to an empty state.
/// @param image Image widget.
void vg_image_clear(vg_image_t *image);

/// @brief Set the scaling mode used when the widget size differs from the image size.
/// @param image Image widget.
/// @param mode  One of VG_IMAGE_SCALE_NONE, FIT, FILL, or STRETCH.
void vg_image_set_scale_mode(vg_image_t *image, vg_image_scale_t mode);

/// @brief Set the rendering opacity.
/// @param image   Image widget.
/// @param opacity Opacity in the range [0.0, 1.0] (0 = transparent, 1 = opaque).
void vg_image_set_opacity(vg_image_t *image, float opacity);

//=============================================================================
// Spinner/NumberInput Widget
//=============================================================================

/// @brief Spinner value change callback
typedef void (*vg_spinner_callback_t)(vg_widget_t *spinner, double value, void *user_data);

/// @brief Spinner widget structure
typedef struct vg_spinner {
    vg_widget_t base;

    double value;       ///< Current value
    double min_value;   ///< Minimum value
    double max_value;   ///< Maximum value
    double step;        ///< Step increment
    int decimal_places; ///< Decimal places to display

    vg_font_t *font;   ///< Font for value display
    float font_size;   ///< Font size
    char *text_buffer; ///< Text buffer for display
    bool editing;      ///< Is user editing the text
    size_t cursor_pos; ///< Caret position inside the edit buffer

    // Appearance
    uint32_t bg_color;     ///< Background color
    uint32_t text_color;   ///< Text color
    uint32_t border_color; ///< Border color
    uint32_t button_color; ///< Up/down button color
    float button_width;    ///< Width of up/down buttons

    // State
    bool up_hovered;   ///< Is up button hovered
    bool down_hovered; ///< Is down button hovered
    bool up_pressed;   ///< Is up button pressed
    bool down_pressed; ///< Is down button pressed

    // Callbacks
    vg_spinner_callback_t on_change;
    void *on_change_data;
} vg_spinner_t;

/// @brief Create a new spinner widget with default range [0, 100] and step 1.
/// @param parent Parent widget (can be NULL).
/// @return New spinner widget or NULL on failure.
vg_spinner_t *vg_spinner_create(vg_widget_t *parent);

/// @brief Set the current numeric value (clamped to [min, max]).
/// @param spinner Spinner widget.
/// @param value   New value.
void vg_spinner_set_value(vg_spinner_t *spinner, double value);

/// @brief Get the current numeric value.
/// @param spinner Spinner widget.
/// @return Current value in [min, max].
double vg_spinner_get_value(vg_spinner_t *spinner);

/// @brief Set the allowed value range.
/// @param spinner Spinner widget.
/// @param min_val Minimum allowed value.
/// @param max_val Maximum allowed value.
void vg_spinner_set_range(vg_spinner_t *spinner, double min_val, double max_val);

/// @brief Set the increment applied by the up/down buttons.
/// @param spinner Spinner widget.
/// @param step    Step size (must be > 0).
void vg_spinner_set_step(vg_spinner_t *spinner, double step);

/// @brief Set the number of decimal places shown in the text field.
/// @param spinner  Spinner widget.
/// @param decimals Number of digits after the decimal point (0 = integer display).
void vg_spinner_set_decimals(vg_spinner_t *spinner, int decimals);

/// @brief Set the font used to display the numeric value.
/// @param spinner Spinner widget.
/// @param font    Font handle.
/// @param size    Font size in pixels.
void vg_spinner_set_font(vg_spinner_t *spinner, vg_font_t *font, float size);

/// @brief Set the callback fired when the value changes.
/// @param spinner   Spinner widget.
/// @param callback  Handler function.
/// @param user_data User data passed to the handler.
void vg_spinner_set_on_change(vg_spinner_t *spinner,
                              vg_spinner_callback_t callback,
                              void *user_data);

//=============================================================================
// ColorSwatch Widget
//=============================================================================

/// @brief ColorSwatch callback - called when color is selected
typedef void (*vg_colorswatch_callback_t)(vg_widget_t *swatch, uint32_t color, void *user_data);

/// @brief ColorSwatch widget structure - displays a single color
typedef struct vg_colorswatch {
    vg_widget_t base;

    uint32_t color;   ///< The color displayed (ARGB)
    bool selected;    ///< Is this swatch currently selected
    bool show_border; ///< Show border around swatch

    // Appearance
    float size;               ///< Swatch size (width and height)
    uint32_t border_color;    ///< Border color
    uint32_t selected_border; ///< Border color when selected
    float border_width;       ///< Border width
    float corner_radius;      ///< Corner radius

    // Callback
    vg_colorswatch_callback_t on_select;
    void *on_select_data;
} vg_colorswatch_t;

/// @brief Create a new color swatch widget
/// @param parent Parent widget (can be NULL)
/// @param color Initial color (ARGB)
/// @return New color swatch widget or NULL on failure
vg_colorswatch_t *vg_colorswatch_create(vg_widget_t *parent, uint32_t color);

/// @brief Set swatch color
/// @param swatch ColorSwatch widget
/// @param color New color (ARGB)
void vg_colorswatch_set_color(vg_colorswatch_t *swatch, uint32_t color);

/// @brief Get swatch color
/// @param swatch ColorSwatch widget
/// @return Current color (ARGB)
uint32_t vg_colorswatch_get_color(vg_colorswatch_t *swatch);

/// @brief Set selected state
/// @param swatch ColorSwatch widget
/// @param selected Selected state
void vg_colorswatch_set_selected(vg_colorswatch_t *swatch, bool selected);

/// @brief Check if swatch is selected
/// @param swatch ColorSwatch widget
/// @return true if selected
bool vg_colorswatch_is_selected(vg_colorswatch_t *swatch);

/// @brief Set selection callback
/// @param swatch ColorSwatch widget
/// @param callback Selection handler function
/// @param user_data User data passed to callback
void vg_colorswatch_set_on_select(vg_colorswatch_t *swatch,
                                  vg_colorswatch_callback_t callback,
                                  void *user_data);

/// @brief Set swatch size
/// @param swatch ColorSwatch widget
/// @param size Size (width and height)
void vg_colorswatch_set_size(vg_colorswatch_t *swatch, float size);

//=============================================================================
// ColorPalette Widget
//=============================================================================

/// @brief ColorPalette callback - called when a color is selected from palette
typedef void (*vg_colorpalette_callback_t)(vg_widget_t *palette,
                                           uint32_t color,
                                           int index,
                                           void *user_data);

/// @brief ColorPalette widget structure - grid of color swatches
typedef struct vg_colorpalette {
    vg_widget_t base;

    uint32_t *colors;   ///< Array of colors (owned)
    int color_count;    ///< Number of colors
    int columns;        ///< Number of columns in grid
    int selected_index; ///< Currently selected color index (-1 = none)

    // Appearance
    float swatch_size;        ///< Size of each swatch
    float gap;                ///< Gap between swatches
    uint32_t bg_color;        ///< Background color
    uint32_t border_color;    ///< Border around swatches
    uint32_t selected_border; ///< Border for selected swatch

    // Callback
    vg_colorpalette_callback_t on_select;
    void *on_select_data;
} vg_colorpalette_t;

/// @brief Create a new color palette widget
/// @param parent Parent widget (can be NULL)
/// @return New color palette widget or NULL on failure
vg_colorpalette_t *vg_colorpalette_create(vg_widget_t *parent);

/// @brief Set palette colors
/// @param palette ColorPalette widget
/// @param colors Array of colors (copied)
/// @param count Number of colors
void vg_colorpalette_set_colors(vg_colorpalette_t *palette, const uint32_t *colors, int count);

/// @brief Add a color to the palette
/// @param palette ColorPalette widget
/// @param color Color to add (ARGB)
void vg_colorpalette_add_color(vg_colorpalette_t *palette, uint32_t color);

/// @brief Clear all colors from palette
/// @param palette ColorPalette widget
void vg_colorpalette_clear(vg_colorpalette_t *palette);

/// @brief Set number of columns
/// @param palette ColorPalette widget
/// @param columns Number of columns
void vg_colorpalette_set_columns(vg_colorpalette_t *palette, int columns);

/// @brief Set selected color index
/// @param palette ColorPalette widget
/// @param index Selected index (-1 to deselect)
void vg_colorpalette_set_selected(vg_colorpalette_t *palette, int index);

/// @brief Get selected color index
/// @param palette ColorPalette widget
/// @return Selected index or -1 if none
int vg_colorpalette_get_selected(vg_colorpalette_t *palette);

/// @brief Get selected color
/// @param palette ColorPalette widget
/// @return Selected color or 0 if none selected
uint32_t vg_colorpalette_get_selected_color(vg_colorpalette_t *palette);

/// @brief Set selection callback
/// @param palette ColorPalette widget
/// @param callback Selection handler function
/// @param user_data User data passed to callback
void vg_colorpalette_set_on_select(vg_colorpalette_t *palette,
                                   vg_colorpalette_callback_t callback,
                                   void *user_data);

/// @brief Set swatch size
/// @param palette ColorPalette widget
/// @param size Swatch size
void vg_colorpalette_set_swatch_size(vg_colorpalette_t *palette, float size);

/// @brief Load standard 16-color palette
/// @param palette ColorPalette widget
void vg_colorpalette_load_standard_16(vg_colorpalette_t *palette);

//=============================================================================
// ColorPicker Widget
//=============================================================================

/// @brief ColorPicker callback - called when color changes
typedef void (*vg_colorpicker_callback_t)(vg_widget_t *picker, uint32_t color, void *user_data);

/// @brief ColorPicker widget structure - full color selection with RGB sliders
typedef struct vg_colorpicker {
    vg_widget_t base;

    uint32_t color;     ///< Current selected color (ARGB)
    uint8_t r, g, b, a; ///< Individual components

    // Child widgets (managed internally)
    vg_slider_t *slider_r; ///< Red slider
    vg_slider_t *slider_g; ///< Green slider
    vg_slider_t *slider_b; ///< Blue slider
    vg_slider_t *slider_a; ///< Alpha slider (optional)

    vg_colorswatch_t *preview;  ///< Color preview swatch
    vg_colorpalette_t *palette; ///< Quick color palette

    // Display options
    bool show_alpha;   ///< Show alpha slider
    bool show_palette; ///< Show quick palette
    bool show_labels;  ///< Show R/G/B labels
    bool show_values;  ///< Show numeric values

    vg_font_t *font; ///< Font for labels
    float font_size; ///< Font size

    // Callback
    vg_colorpicker_callback_t on_change;
    void *on_change_data;

    bool syncing_children; ///< Internal: suppress child slider callbacks during programmatic sync
} vg_colorpicker_t;

/// @brief Create a new color picker widget
/// @param parent Parent widget (can be NULL)
/// @return New color picker widget or NULL on failure
vg_colorpicker_t *vg_colorpicker_create(vg_widget_t *parent);

/// @brief Set picker color
/// @param picker ColorPicker widget
/// @param color Color (ARGB)
void vg_colorpicker_set_color(vg_colorpicker_t *picker, uint32_t color);

/// @brief Get picker color
/// @param picker ColorPicker widget
/// @return Current color (ARGB)
uint32_t vg_colorpicker_get_color(vg_colorpicker_t *picker);

/// @brief Set RGB components
/// @param picker ColorPicker widget
/// @param r Red component (0-255)
/// @param g Green component (0-255)
/// @param b Blue component (0-255)
void vg_colorpicker_set_rgb(vg_colorpicker_t *picker, uint8_t r, uint8_t g, uint8_t b);

/// @brief Get RGB components
/// @param picker ColorPicker widget
/// @param r Pointer to receive red (can be NULL)
/// @param g Pointer to receive green (can be NULL)
/// @param b Pointer to receive blue (can be NULL)
void vg_colorpicker_get_rgb(vg_colorpicker_t *picker, uint8_t *r, uint8_t *g, uint8_t *b);

/// @brief Set alpha component
/// @param picker ColorPicker widget
/// @param alpha Alpha (0-255)
void vg_colorpicker_set_alpha(vg_colorpicker_t *picker, uint8_t alpha);

/// @brief Get alpha component
/// @param picker ColorPicker widget
/// @return Alpha (0-255)
uint8_t vg_colorpicker_get_alpha(vg_colorpicker_t *picker);

/// @brief Show/hide alpha slider
/// @param picker ColorPicker widget
/// @param show true to show alpha slider
void vg_colorpicker_show_alpha(vg_colorpicker_t *picker, bool show);

/// @brief Show/hide quick palette
/// @param picker ColorPicker widget
/// @param show true to show palette
void vg_colorpicker_show_palette(vg_colorpicker_t *picker, bool show);

/// @brief Set change callback
/// @param picker ColorPicker widget
/// @param callback Change handler function
/// @param user_data User data passed to callback
void vg_colorpicker_set_on_change(vg_colorpicker_t *picker,
                                  vg_colorpicker_callback_t callback,
                                  void *user_data);

/// @brief Set font for labels
/// @param picker ColorPicker widget
/// @param font Font to use
/// @param size Font size
void vg_colorpicker_set_font(vg_colorpicker_t *picker, vg_font_t *font, float size);

#ifdef __cplusplus
}
#endif

#endif // VG_WIDGETS_H
