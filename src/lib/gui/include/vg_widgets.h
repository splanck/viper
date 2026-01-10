// vg_widgets.h - Core widget library
#ifndef VG_WIDGETS_H
#define VG_WIDGETS_H

#include "vg_widget.h"
#include "vg_layout.h"
#include "vg_font.h"

#ifdef __cplusplus
extern "C" {
#endif

//=============================================================================
// Label Widget
//=============================================================================

/// @brief Label widget structure
typedef struct vg_label {
    vg_widget_t base;

    const char* text;      ///< Text content (owned, null-terminated)
    vg_font_t* font;       ///< Font for rendering
    float font_size;       ///< Font size in pixels
    uint32_t text_color;   ///< Text color (ARGB)
    vg_h_align_t h_align;  ///< Horizontal text alignment
    vg_v_align_t v_align;  ///< Vertical text alignment
    bool word_wrap;        ///< Enable word wrapping
    int max_lines;         ///< Maximum lines (0 = unlimited)
} vg_label_t;

/// @brief Create a new label widget
/// @param parent Parent widget (can be NULL)
/// @param text Initial text content
/// @return New label widget or NULL on failure
vg_label_t* vg_label_create(vg_widget_t* parent, const char* text);

/// @brief Set label text
/// @param label Label widget
/// @param text New text (copied internally)
void vg_label_set_text(vg_label_t* label, const char* text);

/// @brief Get label text
/// @param label Label widget
/// @return Current text (read-only)
const char* vg_label_get_text(vg_label_t* label);

/// @brief Set label font
/// @param label Label widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_label_set_font(vg_label_t* label, vg_font_t* font, float size);

/// @brief Set text color
/// @param label Label widget
/// @param color Text color (ARGB)
void vg_label_set_color(vg_label_t* label, uint32_t color);

/// @brief Set text alignment
/// @param label Label widget
/// @param h_align Horizontal alignment
/// @param v_align Vertical alignment
void vg_label_set_alignment(vg_label_t* label, vg_h_align_t h_align, vg_v_align_t v_align);

//=============================================================================
// Button Widget
//=============================================================================

/// @brief Button callback function type
typedef void (*vg_button_callback_t)(vg_widget_t* button, void* user_data);

/// @brief Button style enumeration
typedef enum vg_button_style {
    VG_BUTTON_STYLE_DEFAULT,    ///< Standard button
    VG_BUTTON_STYLE_PRIMARY,    ///< Primary action button
    VG_BUTTON_STYLE_SECONDARY,  ///< Secondary action
    VG_BUTTON_STYLE_DANGER,     ///< Destructive action
    VG_BUTTON_STYLE_TEXT,       ///< Text-only button
    VG_BUTTON_STYLE_ICON        ///< Icon button
} vg_button_style_t;

/// @brief Button widget structure
typedef struct vg_button {
    vg_widget_t base;

    const char* text;            ///< Button text (owned)
    vg_font_t* font;             ///< Font for text
    float font_size;             ///< Font size
    vg_button_style_t style;     ///< Button style
    vg_button_callback_t on_click; ///< Click callback
    void* user_data;             ///< User data for callback

    // Appearance
    uint32_t bg_color;           ///< Background color
    uint32_t fg_color;           ///< Text color
    uint32_t border_color;       ///< Border color
    float border_radius;         ///< Corner radius
} vg_button_t;

/// @brief Create a new button widget
/// @param parent Parent widget (can be NULL)
/// @param text Button text
/// @return New button widget or NULL on failure
vg_button_t* vg_button_create(vg_widget_t* parent, const char* text);

/// @brief Set button text
/// @param button Button widget
/// @param text New text (copied internally)
void vg_button_set_text(vg_button_t* button, const char* text);

/// @brief Set button click callback
/// @param button Button widget
/// @param callback Click handler function
/// @param user_data User data passed to callback
void vg_button_set_on_click(vg_button_t* button, vg_button_callback_t callback, void* user_data);

/// @brief Set button style
/// @param button Button widget
/// @param style Button style
void vg_button_set_style(vg_button_t* button, vg_button_style_t style);

/// @brief Set button font
/// @param button Button widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_button_set_font(vg_button_t* button, vg_font_t* font, float size);

//=============================================================================
// TextInput Widget
//=============================================================================

/// @brief Text input callback for text changes
typedef void (*vg_text_change_callback_t)(vg_widget_t* input, const char* text, void* user_data);

/// @brief Text input widget structure
typedef struct vg_textinput {
    vg_widget_t base;

    char* text;                  ///< Current text content (owned)
    size_t text_len;             ///< Current text length
    size_t text_capacity;        ///< Allocated capacity
    size_t cursor_pos;           ///< Cursor position (byte offset)
    size_t selection_start;      ///< Selection start position
    size_t selection_end;        ///< Selection end position

    const char* placeholder;     ///< Placeholder text (owned)
    vg_font_t* font;             ///< Font for rendering
    float font_size;             ///< Font size

    int max_length;              ///< Maximum text length (0 = unlimited)
    bool password_mode;          ///< Show dots instead of characters
    bool read_only;              ///< Prevent text modification
    bool multiline;              ///< Allow multiple lines

    // Appearance
    uint32_t text_color;         ///< Text color
    uint32_t placeholder_color;  ///< Placeholder text color
    uint32_t selection_color;    ///< Selection highlight color
    uint32_t cursor_color;       ///< Cursor color
    uint32_t bg_color;           ///< Background color
    uint32_t border_color;       ///< Border color

    // Scrolling (for multiline)
    float scroll_x;              ///< Horizontal scroll offset
    float scroll_y;              ///< Vertical scroll offset

    // Callbacks
    vg_text_change_callback_t on_change;
    void* on_change_data;

    // Internal state
    float cursor_blink_time;     ///< Cursor blink timer
    bool cursor_visible;         ///< Cursor visibility state
} vg_textinput_t;

/// @brief Create a new text input widget
/// @param parent Parent widget (can be NULL)
/// @return New text input widget or NULL on failure
vg_textinput_t* vg_textinput_create(vg_widget_t* parent);

/// @brief Set text input content
/// @param input Text input widget
/// @param text New text (copied internally)
void vg_textinput_set_text(vg_textinput_t* input, const char* text);

/// @brief Get text input content
/// @param input Text input widget
/// @return Current text (read-only)
const char* vg_textinput_get_text(vg_textinput_t* input);

/// @brief Set placeholder text
/// @param input Text input widget
/// @param placeholder Placeholder text (copied internally)
void vg_textinput_set_placeholder(vg_textinput_t* input, const char* placeholder);

/// @brief Set text change callback
/// @param input Text input widget
/// @param callback Change handler function
/// @param user_data User data passed to callback
void vg_textinput_set_on_change(vg_textinput_t* input, vg_text_change_callback_t callback, void* user_data);

/// @brief Set cursor position
/// @param input Text input widget
/// @param pos Cursor position (byte offset)
void vg_textinput_set_cursor(vg_textinput_t* input, size_t pos);

/// @brief Select text range
/// @param input Text input widget
/// @param start Selection start
/// @param end Selection end
void vg_textinput_select(vg_textinput_t* input, size_t start, size_t end);

/// @brief Select all text
/// @param input Text input widget
void vg_textinput_select_all(vg_textinput_t* input);

/// @brief Insert text at cursor position
/// @param input Text input widget
/// @param text Text to insert
void vg_textinput_insert(vg_textinput_t* input, const char* text);

/// @brief Delete selected text
/// @param input Text input widget
void vg_textinput_delete_selection(vg_textinput_t* input);

/// @brief Set font for text input
/// @param input Text input widget
/// @param font Font to use
/// @param size Font size in pixels
void vg_textinput_set_font(vg_textinput_t* input, vg_font_t* font, float size);

//=============================================================================
// Checkbox Widget
//=============================================================================

/// @brief Checkbox state change callback
typedef void (*vg_checkbox_callback_t)(vg_widget_t* checkbox, bool checked, void* user_data);

/// @brief Checkbox widget structure
typedef struct vg_checkbox {
    vg_widget_t base;

    const char* text;            ///< Label text (owned)
    vg_font_t* font;             ///< Font for label
    float font_size;             ///< Font size
    bool checked;                ///< Checked state
    bool indeterminate;          ///< Indeterminate state (tri-state)

    // Appearance
    float box_size;              ///< Checkbox box size
    float gap;                   ///< Gap between box and label
    uint32_t check_color;        ///< Check mark color
    uint32_t box_color;          ///< Box background color
    uint32_t text_color;         ///< Text color

    // Callback
    vg_checkbox_callback_t on_change;
    void* on_change_data;
} vg_checkbox_t;

/// @brief Create a new checkbox widget
/// @param parent Parent widget (can be NULL)
/// @param text Checkbox label text
/// @return New checkbox widget or NULL on failure
vg_checkbox_t* vg_checkbox_create(vg_widget_t* parent, const char* text);

/// @brief Set checkbox checked state
/// @param checkbox Checkbox widget
/// @param checked New checked state
void vg_checkbox_set_checked(vg_checkbox_t* checkbox, bool checked);

/// @brief Get checkbox checked state
/// @param checkbox Checkbox widget
/// @return Current checked state
bool vg_checkbox_is_checked(vg_checkbox_t* checkbox);

/// @brief Toggle checkbox state
/// @param checkbox Checkbox widget
void vg_checkbox_toggle(vg_checkbox_t* checkbox);

/// @brief Set checkbox text
/// @param checkbox Checkbox widget
/// @param text New text (copied internally)
void vg_checkbox_set_text(vg_checkbox_t* checkbox, const char* text);

/// @brief Set state change callback
/// @param checkbox Checkbox widget
/// @param callback Change handler function
/// @param user_data User data passed to callback
void vg_checkbox_set_on_change(vg_checkbox_t* checkbox, vg_checkbox_callback_t callback, void* user_data);

//=============================================================================
// ScrollView Widget
//=============================================================================

/// @brief Scroll direction flags
typedef enum vg_scroll_direction {
    VG_SCROLL_HORIZONTAL = (1 << 0),
    VG_SCROLL_VERTICAL   = (1 << 1),
    VG_SCROLL_BOTH       = VG_SCROLL_HORIZONTAL | VG_SCROLL_VERTICAL
} vg_scroll_direction_t;

/// @brief ScrollView widget structure
typedef struct vg_scrollview {
    vg_widget_t base;

    float scroll_x;              ///< Horizontal scroll position
    float scroll_y;              ///< Vertical scroll position
    float content_width;         ///< Content width (0 = auto from children)
    float content_height;        ///< Content height (0 = auto from children)
    vg_scroll_direction_t direction; ///< Scroll direction

    // Scrollbars
    bool show_h_scrollbar;       ///< Show horizontal scrollbar
    bool show_v_scrollbar;       ///< Show vertical scrollbar
    bool auto_hide_scrollbars;   ///< Auto-hide scrollbars when not needed
    float scrollbar_width;       ///< Scrollbar width

    // Scrollbar appearance
    uint32_t track_color;        ///< Scrollbar track color
    uint32_t thumb_color;        ///< Scrollbar thumb color
    uint32_t thumb_hover_color;  ///< Thumb color when hovered

    // State
    bool h_scrollbar_hovered;    ///< Is horizontal scrollbar hovered
    bool v_scrollbar_hovered;    ///< Is vertical scrollbar hovered
    bool h_scrollbar_dragging;   ///< Is horizontal scrollbar being dragged
    bool v_scrollbar_dragging;   ///< Is vertical scrollbar being dragged
    float drag_offset;           ///< Drag offset for scrollbar
} vg_scrollview_t;

/// @brief Create a new scroll view widget
/// @param parent Parent widget (can be NULL)
/// @return New scroll view widget or NULL on failure
vg_scrollview_t* vg_scrollview_create(vg_widget_t* parent);

/// @brief Set scroll position
/// @param scroll Scroll view widget
/// @param x Horizontal scroll position
/// @param y Vertical scroll position
void vg_scrollview_set_scroll(vg_scrollview_t* scroll, float x, float y);

/// @brief Get scroll position
/// @param scroll Scroll view widget
/// @param out_x Pointer to receive X position (can be NULL)
/// @param out_y Pointer to receive Y position (can be NULL)
void vg_scrollview_get_scroll(vg_scrollview_t* scroll, float* out_x, float* out_y);

/// @brief Set content size
/// @param scroll Scroll view widget
/// @param width Content width (0 = auto)
/// @param height Content height (0 = auto)
void vg_scrollview_set_content_size(vg_scrollview_t* scroll, float width, float height);

/// @brief Scroll to make a child widget visible
/// @param scroll Scroll view widget
/// @param child Child widget to scroll into view
void vg_scrollview_scroll_to_widget(vg_scrollview_t* scroll, vg_widget_t* child);

/// @brief Set scroll direction
/// @param scroll Scroll view widget
/// @param direction Scroll direction flags
void vg_scrollview_set_direction(vg_scrollview_t* scroll, vg_scroll_direction_t direction);

//=============================================================================
// ListBox Widget
//=============================================================================

/// @brief ListBox item structure
typedef struct vg_listbox_item {
    char* text;                  ///< Item text (owned)
    void* user_data;             ///< User data
    bool selected;               ///< Is item selected
    struct vg_listbox_item* next;
    struct vg_listbox_item* prev;
} vg_listbox_item_t;

/// @brief ListBox selection callback
typedef void (*vg_listbox_callback_t)(vg_widget_t* listbox, vg_listbox_item_t* item, void* user_data);

/// @brief ListBox widget structure
typedef struct vg_listbox {
    vg_widget_t base;

    vg_listbox_item_t* first_item;   ///< First item
    vg_listbox_item_t* last_item;    ///< Last item
    int item_count;                  ///< Number of items
    vg_listbox_item_t* selected;     ///< Currently selected item
    vg_listbox_item_t* hovered;      ///< Currently hovered item

    vg_font_t* font;                 ///< Font for rendering
    float font_size;                 ///< Font size
    float item_height;               ///< Height of each item
    float scroll_y;                  ///< Vertical scroll position

    bool multi_select;               ///< Allow multiple selection

    // Appearance
    uint32_t bg_color;               ///< Background color
    uint32_t item_bg;                ///< Item background
    uint32_t selected_bg;            ///< Selected item background
    uint32_t hover_bg;               ///< Hovered item background
    uint32_t text_color;             ///< Text color
    uint32_t border_color;           ///< Border color

    // Callbacks
    vg_listbox_callback_t on_select;
    void* on_select_data;
    vg_listbox_callback_t on_activate; ///< Double-click
    void* on_activate_data;
} vg_listbox_t;

/// @brief Create a new listbox widget
vg_listbox_t* vg_listbox_create(vg_widget_t* parent);

/// @brief Add an item to the listbox
vg_listbox_item_t* vg_listbox_add_item(vg_listbox_t* listbox, const char* text, void* user_data);

/// @brief Remove an item from the listbox
void vg_listbox_remove_item(vg_listbox_t* listbox, vg_listbox_item_t* item);

/// @brief Clear all items
void vg_listbox_clear(vg_listbox_t* listbox);

/// @brief Select an item
void vg_listbox_select(vg_listbox_t* listbox, vg_listbox_item_t* item);

/// @brief Get selected item
vg_listbox_item_t* vg_listbox_get_selected(vg_listbox_t* listbox);

/// @brief Set font for listbox
void vg_listbox_set_font(vg_listbox_t* listbox, vg_font_t* font, float size);

/// @brief Set selection callback
void vg_listbox_set_on_select(vg_listbox_t* listbox, vg_listbox_callback_t callback, void* user_data);

//=============================================================================
// Dropdown/ComboBox Widget
//=============================================================================

/// @brief Dropdown selection callback
typedef void (*vg_dropdown_callback_t)(vg_widget_t* dropdown, int index, const char* text, void* user_data);

/// @brief Dropdown widget structure
typedef struct vg_dropdown {
    vg_widget_t base;

    char** items;                    ///< Array of item strings (owned)
    int item_count;                  ///< Number of items
    int item_capacity;               ///< Allocated capacity
    int selected_index;              ///< Currently selected index (-1 = none)

    vg_font_t* font;                 ///< Font for rendering
    float font_size;                 ///< Font size
    const char* placeholder;         ///< Placeholder when nothing selected

    bool open;                       ///< Is dropdown list open
    int hovered_index;               ///< Hovered item index
    float dropdown_height;           ///< Max height of dropdown list
    float scroll_y;                  ///< Scroll position when list is long

    // Appearance
    uint32_t bg_color;               ///< Background color
    uint32_t text_color;             ///< Text color
    uint32_t border_color;           ///< Border color
    uint32_t dropdown_bg;            ///< Dropdown list background
    uint32_t hover_bg;               ///< Hovered item background
    uint32_t selected_bg;            ///< Selected item in list background
    float arrow_size;                ///< Dropdown arrow size

    // Callbacks
    vg_dropdown_callback_t on_change;
    void* on_change_data;
} vg_dropdown_t;

/// @brief Create a new dropdown widget
vg_dropdown_t* vg_dropdown_create(vg_widget_t* parent);

/// @brief Add an item to the dropdown
int vg_dropdown_add_item(vg_dropdown_t* dropdown, const char* text);

/// @brief Remove an item by index
void vg_dropdown_remove_item(vg_dropdown_t* dropdown, int index);

/// @brief Clear all items
void vg_dropdown_clear(vg_dropdown_t* dropdown);

/// @brief Set selected index
void vg_dropdown_set_selected(vg_dropdown_t* dropdown, int index);

/// @brief Get selected index
int vg_dropdown_get_selected(vg_dropdown_t* dropdown);

/// @brief Get selected text
const char* vg_dropdown_get_selected_text(vg_dropdown_t* dropdown);

/// @brief Set placeholder text
void vg_dropdown_set_placeholder(vg_dropdown_t* dropdown, const char* text);

/// @brief Set font for dropdown
void vg_dropdown_set_font(vg_dropdown_t* dropdown, vg_font_t* font, float size);

/// @brief Set change callback
void vg_dropdown_set_on_change(vg_dropdown_t* dropdown, vg_dropdown_callback_t callback, void* user_data);

//=============================================================================
// Slider Widget
//=============================================================================

/// @brief Slider orientation
typedef enum vg_slider_orientation {
    VG_SLIDER_HORIZONTAL,
    VG_SLIDER_VERTICAL
} vg_slider_orientation_t;

/// @brief Slider value change callback
typedef void (*vg_slider_callback_t)(vg_widget_t* slider, float value, void* user_data);

/// @brief Slider widget structure
typedef struct vg_slider {
    vg_widget_t base;

    float value;                     ///< Current value
    float min_value;                 ///< Minimum value
    float max_value;                 ///< Maximum value
    float step;                      ///< Step increment (0 = continuous)
    vg_slider_orientation_t orientation;

    // Appearance
    float track_thickness;           ///< Track thickness
    float thumb_size;                ///< Thumb diameter
    uint32_t track_color;            ///< Track color
    uint32_t fill_color;             ///< Filled portion color
    uint32_t thumb_color;            ///< Thumb color
    uint32_t thumb_hover_color;      ///< Thumb hover color

    // Display
    bool show_value;                 ///< Show value label
    vg_font_t* font;                 ///< Font for value label
    float font_size;                 ///< Font size

    // State
    bool dragging;                   ///< Is thumb being dragged
    bool thumb_hovered;              ///< Is thumb hovered

    // Callbacks
    vg_slider_callback_t on_change;
    void* on_change_data;
} vg_slider_t;

/// @brief Create a new slider widget
vg_slider_t* vg_slider_create(vg_widget_t* parent, vg_slider_orientation_t orientation);

/// @brief Set slider value
void vg_slider_set_value(vg_slider_t* slider, float value);

/// @brief Get slider value
float vg_slider_get_value(vg_slider_t* slider);

/// @brief Set value range
void vg_slider_set_range(vg_slider_t* slider, float min_val, float max_val);

/// @brief Set step increment
void vg_slider_set_step(vg_slider_t* slider, float step);

/// @brief Set change callback
void vg_slider_set_on_change(vg_slider_t* slider, vg_slider_callback_t callback, void* user_data);

//=============================================================================
// ProgressBar Widget
//=============================================================================

/// @brief ProgressBar style
typedef enum vg_progress_style {
    VG_PROGRESS_BAR,                 ///< Standard horizontal bar
    VG_PROGRESS_CIRCULAR,            ///< Circular progress
    VG_PROGRESS_INDETERMINATE        ///< Indeterminate animation
} vg_progress_style_t;

/// @brief ProgressBar widget structure
typedef struct vg_progressbar {
    vg_widget_t base;

    float value;                     ///< Current value (0-1)
    vg_progress_style_t style;       ///< Progress style

    // Appearance
    uint32_t track_color;            ///< Track/background color
    uint32_t fill_color;             ///< Fill/progress color
    float corner_radius;             ///< Corner radius for bar style

    // Display
    bool show_percentage;            ///< Show percentage text
    vg_font_t* font;                 ///< Font for percentage
    float font_size;                 ///< Font size

    // Animation (for indeterminate)
    float animation_phase;           ///< Current animation phase
} vg_progressbar_t;

/// @brief Create a new progress bar widget
vg_progressbar_t* vg_progressbar_create(vg_widget_t* parent);

/// @brief Set progress value (0-1)
void vg_progressbar_set_value(vg_progressbar_t* progress, float value);

/// @brief Get progress value
float vg_progressbar_get_value(vg_progressbar_t* progress);

/// @brief Set progress style
void vg_progressbar_set_style(vg_progressbar_t* progress, vg_progress_style_t style);

/// @brief Set whether to show percentage text
void vg_progressbar_show_percentage(vg_progressbar_t* progress, bool show);

//=============================================================================
// RadioButton Widget
//=============================================================================

/// @brief RadioButton group - manages mutual exclusivity
typedef struct vg_radiogroup {
    struct vg_radiobutton** buttons; ///< Array of buttons in group
    int button_count;                ///< Number of buttons
    int button_capacity;             ///< Allocated capacity
    int selected_index;              ///< Currently selected index
} vg_radiogroup_t;

/// @brief RadioButton callback
typedef void (*vg_radio_callback_t)(vg_widget_t* radio, bool selected, void* user_data);

/// @brief RadioButton widget structure
typedef struct vg_radiobutton {
    vg_widget_t base;

    const char* text;                ///< Label text (owned)
    vg_font_t* font;                 ///< Font for label
    float font_size;                 ///< Font size
    bool selected;                   ///< Is this button selected
    vg_radiogroup_t* group;          ///< Group this button belongs to

    // Appearance
    float circle_size;               ///< Radio circle size
    float gap;                       ///< Gap between circle and label
    uint32_t circle_color;           ///< Circle border color
    uint32_t fill_color;             ///< Selected fill color
    uint32_t text_color;             ///< Text color

    // Callback
    vg_radio_callback_t on_change;
    void* on_change_data;
} vg_radiobutton_t;

/// @brief Create a new radio group
vg_radiogroup_t* vg_radiogroup_create(void);

/// @brief Destroy a radio group
void vg_radiogroup_destroy(vg_radiogroup_t* group);

/// @brief Create a new radio button
vg_radiobutton_t* vg_radiobutton_create(vg_widget_t* parent, const char* text, vg_radiogroup_t* group);

/// @brief Set radio button selected state
void vg_radiobutton_set_selected(vg_radiobutton_t* radio, bool selected);

/// @brief Check if radio button is selected
bool vg_radiobutton_is_selected(vg_radiobutton_t* radio);

/// @brief Get selected index in a group
int vg_radiogroup_get_selected(vg_radiogroup_t* group);

/// @brief Set selected index in a group
void vg_radiogroup_set_selected(vg_radiogroup_t* group, int index);

//=============================================================================
// Image Widget
//=============================================================================

/// @brief Image scaling mode
typedef enum vg_image_scale {
    VG_IMAGE_SCALE_NONE,             ///< No scaling (original size)
    VG_IMAGE_SCALE_FIT,              ///< Scale to fit, maintain aspect ratio
    VG_IMAGE_SCALE_FILL,             ///< Scale to fill, may crop
    VG_IMAGE_SCALE_STRETCH           ///< Stretch to fill (distorts)
} vg_image_scale_t;

/// @brief Image widget structure
typedef struct vg_image {
    vg_widget_t base;

    uint8_t* pixels;                 ///< Pixel data (RGBA, owned)
    int img_width;                   ///< Original image width
    int img_height;                  ///< Original image height
    vg_image_scale_t scale_mode;     ///< Scaling mode

    // Appearance
    uint32_t bg_color;               ///< Background color (shown if image doesn't fill)
    float opacity;                   ///< Image opacity (0-1)
    float corner_radius;             ///< Corner radius for rounded images
} vg_image_t;

/// @brief Create a new image widget
vg_image_t* vg_image_create(vg_widget_t* parent);

/// @brief Set image from RGBA pixel data
void vg_image_set_pixels(vg_image_t* image, const uint8_t* pixels, int width, int height);

/// @brief Load image from file (PNG, JPEG, BMP)
bool vg_image_load_file(vg_image_t* image, const char* path);

/// @brief Clear image data
void vg_image_clear(vg_image_t* image);

/// @brief Set scaling mode
void vg_image_set_scale_mode(vg_image_t* image, vg_image_scale_t mode);

/// @brief Set image opacity
void vg_image_set_opacity(vg_image_t* image, float opacity);

//=============================================================================
// Spinner/NumberInput Widget
//=============================================================================

/// @brief Spinner value change callback
typedef void (*vg_spinner_callback_t)(vg_widget_t* spinner, double value, void* user_data);

/// @brief Spinner widget structure
typedef struct vg_spinner {
    vg_widget_t base;

    double value;                    ///< Current value
    double min_value;                ///< Minimum value
    double max_value;                ///< Maximum value
    double step;                     ///< Step increment
    int decimal_places;              ///< Decimal places to display

    vg_font_t* font;                 ///< Font for value display
    float font_size;                 ///< Font size
    char* text_buffer;               ///< Text buffer for display
    bool editing;                    ///< Is user editing the text

    // Appearance
    uint32_t bg_color;               ///< Background color
    uint32_t text_color;             ///< Text color
    uint32_t border_color;           ///< Border color
    uint32_t button_color;           ///< Up/down button color
    float button_width;              ///< Width of up/down buttons

    // State
    bool up_hovered;                 ///< Is up button hovered
    bool down_hovered;               ///< Is down button hovered
    bool up_pressed;                 ///< Is up button pressed
    bool down_pressed;               ///< Is down button pressed

    // Callbacks
    vg_spinner_callback_t on_change;
    void* on_change_data;
} vg_spinner_t;

/// @brief Create a new spinner widget
vg_spinner_t* vg_spinner_create(vg_widget_t* parent);

/// @brief Set spinner value
void vg_spinner_set_value(vg_spinner_t* spinner, double value);

/// @brief Get spinner value
double vg_spinner_get_value(vg_spinner_t* spinner);

/// @brief Set value range
void vg_spinner_set_range(vg_spinner_t* spinner, double min_val, double max_val);

/// @brief Set step increment
void vg_spinner_set_step(vg_spinner_t* spinner, double step);

/// @brief Set decimal places
void vg_spinner_set_decimals(vg_spinner_t* spinner, int decimals);

/// @brief Set font for spinner
void vg_spinner_set_font(vg_spinner_t* spinner, vg_font_t* font, float size);

/// @brief Set change callback
void vg_spinner_set_on_change(vg_spinner_t* spinner, vg_spinner_callback_t callback, void* user_data);

#ifdef __cplusplus
}
#endif

#endif // VG_WIDGETS_H
