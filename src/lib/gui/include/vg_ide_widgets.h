// vg_ide_widgets.h - IDE-specific widget library
#ifndef VG_IDE_WIDGETS_H
#define VG_IDE_WIDGETS_H

#include "vg_font.h"
#include "vg_layout.h"
#include "vg_widget.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Forward declarations
    typedef struct vg_menu_item vg_menu_item_t;
    typedef struct vg_menu vg_menu_t;
    struct vg_treeview;

    //=============================================================================
    // StatusBar Widget
    //=============================================================================

    /// @brief StatusBar item types
    typedef enum vg_statusbar_item_type
    {
        VG_STATUSBAR_ITEM_TEXT,      ///< Static text label
        VG_STATUSBAR_ITEM_BUTTON,    ///< Clickable button
        VG_STATUSBAR_ITEM_PROGRESS,  ///< Progress indicator
        VG_STATUSBAR_ITEM_SEPARATOR, ///< Vertical separator line
        VG_STATUSBAR_ITEM_SPACER     ///< Flexible spacer
    } vg_statusbar_item_type_t;

    /// @brief StatusBar zone for item placement
    typedef enum vg_statusbar_zone
    {
        VG_STATUSBAR_ZONE_LEFT,   ///< Left-aligned zone
        VG_STATUSBAR_ZONE_CENTER, ///< Center-aligned zone
        VG_STATUSBAR_ZONE_RIGHT   ///< Right-aligned zone
    } vg_statusbar_zone_t;

    /// @brief StatusBar item structure
    typedef struct vg_statusbar_item
    {
        vg_statusbar_item_type_t type; ///< Item type
        char *text;                    ///< Item text (owned)
        char *tooltip;                 ///< Tooltip text (owned)
        float min_width;               ///< Minimum width (0 = auto)
        float max_width;               ///< Maximum width (0 = unlimited)
        bool visible;                  ///< Is item visible
        float progress;                ///< Progress value (0-1) for progress items
        void *user_data;               ///< User data
        void (*on_click)(struct vg_statusbar_item *, void *); ///< Click callback for buttons
    } vg_statusbar_item_t;

    /// @brief StatusBar widget structure
    typedef struct vg_statusbar
    {
        vg_widget_t base;

        // Items by zone
        vg_statusbar_item_t **left_items;
        size_t left_count;
        size_t left_capacity;

        vg_statusbar_item_t **center_items;
        size_t center_count;
        size_t center_capacity;

        vg_statusbar_item_t **right_items;
        size_t right_count;
        size_t right_capacity;

        // Styling
        int height;            ///< StatusBar height
        float item_padding;    ///< Padding between items
        float separator_width; ///< Separator line width

        // Font
        vg_font_t *font; ///< Font for text
        float font_size; ///< Font size

        // Colors
        uint32_t bg_color;     ///< Background color
        uint32_t text_color;   ///< Text color
        uint32_t hover_color;  ///< Hover background
        uint32_t border_color; ///< Top border color

        // State
        vg_statusbar_item_t *hovered_item; ///< Currently hovered item
    } vg_statusbar_t;

    /// @brief Create a new status bar widget
    /// @param parent Parent widget (can be NULL)
    /// @return New status bar widget or NULL on failure
    vg_statusbar_t *vg_statusbar_create(vg_widget_t *parent);

    /// @brief Add a text item to the status bar
    /// @param sb StatusBar widget
    /// @param zone Zone to add item to
    /// @param text Item text
    /// @return New item or NULL on failure
    vg_statusbar_item_t *vg_statusbar_add_text(vg_statusbar_t *sb,
                                               vg_statusbar_zone_t zone,
                                               const char *text);

    /// @brief Add a button item to the status bar
    /// @param sb StatusBar widget
    /// @param zone Zone to add item to
    /// @param text Button text
    /// @param on_click Click callback
    /// @param user_data User data for callback
    /// @return New item or NULL on failure
    vg_statusbar_item_t *vg_statusbar_add_button(vg_statusbar_t *sb,
                                                 vg_statusbar_zone_t zone,
                                                 const char *text,
                                                 void (*on_click)(vg_statusbar_item_t *, void *),
                                                 void *user_data);

    /// @brief Add a progress indicator to the status bar
    /// @param sb StatusBar widget
    /// @param zone Zone to add item to
    /// @return New item or NULL on failure
    vg_statusbar_item_t *vg_statusbar_add_progress(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

    /// @brief Add a separator to the status bar
    /// @param sb StatusBar widget
    /// @param zone Zone to add separator to
    /// @return New item or NULL on failure
    vg_statusbar_item_t *vg_statusbar_add_separator(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

    /// @brief Add a spacer to the status bar
    /// @param sb StatusBar widget
    /// @param zone Zone to add spacer to
    /// @return New item or NULL on failure
    vg_statusbar_item_t *vg_statusbar_add_spacer(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

    /// @brief Remove an item from the status bar
    /// @param sb StatusBar widget
    /// @param item Item to remove
    void vg_statusbar_remove_item(vg_statusbar_t *sb, vg_statusbar_item_t *item);

    /// @brief Clear all items in a zone
    /// @param sb StatusBar widget
    /// @param zone Zone to clear
    void vg_statusbar_clear_zone(vg_statusbar_t *sb, vg_statusbar_zone_t zone);

    /// @brief Set item text
    /// @param item StatusBar item
    /// @param text New text
    void vg_statusbar_item_set_text(vg_statusbar_item_t *item, const char *text);

    /// @brief Set item tooltip
    /// @param item StatusBar item
    /// @param tooltip Tooltip text
    void vg_statusbar_item_set_tooltip(vg_statusbar_item_t *item, const char *tooltip);

    /// @brief Set progress value (for progress items)
    /// @param item StatusBar item
    /// @param progress Progress value (0-1)
    void vg_statusbar_item_set_progress(vg_statusbar_item_t *item, float progress);

    /// @brief Set item visibility
    /// @param item StatusBar item
    /// @param visible Visibility state
    void vg_statusbar_item_set_visible(vg_statusbar_item_t *item, bool visible);

    /// @brief Set font for status bar
    /// @param sb StatusBar widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_statusbar_set_font(vg_statusbar_t *sb, vg_font_t *font, float size);

    /// @brief Convenience: set cursor position display (Ln X, Col Y)
    /// @param sb StatusBar widget
    /// @param line Line number
    /// @param col Column number
    void vg_statusbar_set_cursor_position(vg_statusbar_t *sb, int line, int col);

    //=============================================================================
    // Toolbar Widget
    //=============================================================================

    /// @brief Toolbar item types
    typedef enum vg_toolbar_item_type
    {
        VG_TOOLBAR_ITEM_BUTTON,    ///< Standard button
        VG_TOOLBAR_ITEM_TOGGLE,    ///< Toggle button (checkable)
        VG_TOOLBAR_ITEM_DROPDOWN,  ///< Button with dropdown menu
        VG_TOOLBAR_ITEM_SEPARATOR, ///< Vertical line separator
        VG_TOOLBAR_ITEM_SPACER,    ///< Flexible spacer
        VG_TOOLBAR_ITEM_WIDGET     ///< Custom embedded widget
    } vg_toolbar_item_type_t;

    /// @brief Toolbar orientation
    typedef enum vg_toolbar_orientation
    {
        VG_TOOLBAR_HORIZONTAL, ///< Horizontal toolbar
        VG_TOOLBAR_VERTICAL    ///< Vertical toolbar
    } vg_toolbar_orientation_t;

    /// @brief Toolbar icon size presets
    typedef enum vg_toolbar_icon_size
    {
        VG_TOOLBAR_ICONS_SMALL,  ///< 16x16 icons
        VG_TOOLBAR_ICONS_MEDIUM, ///< 24x24 icons
        VG_TOOLBAR_ICONS_LARGE   ///< 32x32 icons
    } vg_toolbar_icon_size_t;

    /// @brief Icon specification
    typedef struct vg_icon
    {
        enum
        {
            VG_ICON_NONE,  ///< No icon
            VG_ICON_GLYPH, ///< Unicode character
            VG_ICON_IMAGE, ///< Pixel data
            VG_ICON_PATH   ///< File path
        } type;

        union
        {
            uint32_t glyph; ///< Unicode codepoint

            struct
            {
                uint8_t *pixels; ///< RGBA pixel data
                uint32_t width;
                uint32_t height;
            } image;

            char *path; ///< File path
        } data;
    } vg_icon_t;

    /// @brief Toolbar item structure
    typedef struct vg_toolbar_item
    {
        vg_toolbar_item_type_t type; ///< Item type
        char *id;                    ///< Unique identifier
        char *label;                 ///< Text label (optional)
        char *tooltip;               ///< Hover tooltip
        vg_icon_t icon;              ///< Icon specification
        bool enabled;                ///< Enabled state
        bool checked;                ///< For toggle items
        bool show_label;             ///< Show text label
        bool was_clicked;            ///< Set true when item is clicked (cleared on read)

        struct vg_menu *dropdown_menu; ///< Dropdown menu (for DROPDOWN type)
        vg_widget_t *custom_widget;    ///< Custom widget (for WIDGET type)

        void *user_data;                                           ///< User data
        void (*on_click)(struct vg_toolbar_item *, void *);        ///< Click callback
        void (*on_toggle)(struct vg_toolbar_item *, bool, void *); ///< Toggle callback
    } vg_toolbar_item_t;

    /// @brief Toolbar widget structure
    typedef struct vg_toolbar
    {
        vg_widget_t base;

        vg_toolbar_item_t **items; ///< Array of items
        size_t item_count;         ///< Number of items
        size_t item_capacity;      ///< Allocated capacity

        // Configuration
        vg_toolbar_orientation_t orientation; ///< Orientation
        vg_toolbar_icon_size_t icon_size;     ///< Icon size preset
        uint32_t item_padding;                ///< Padding around items
        uint32_t item_spacing;                ///< Space between items
        bool show_labels;                     ///< Global label visibility
        bool overflow_menu;                   ///< Show overflow items in dropdown

        // Font
        vg_font_t *font; ///< Font for text
        float font_size; ///< Font size

        // Colors
        uint32_t bg_color;       ///< Background color
        uint32_t hover_color;    ///< Hover color
        uint32_t active_color;   ///< Active/pressed color
        uint32_t text_color;     ///< Text color
        uint32_t disabled_color; ///< Disabled text color

        // State
        vg_toolbar_item_t *hovered_item; ///< Currently hovered item
        vg_toolbar_item_t *pressed_item; ///< Currently pressed item
        int overflow_start_index;        ///< First item in overflow (-1 if none)
    } vg_toolbar_t;

    /// @brief Create a new toolbar widget
    /// @param parent Parent widget (can be NULL)
    /// @param orientation Toolbar orientation
    /// @return New toolbar widget or NULL on failure
    vg_toolbar_t *vg_toolbar_create(vg_widget_t *parent, vg_toolbar_orientation_t orientation);

    /// @brief Add a button to the toolbar
    /// @param tb Toolbar widget
    /// @param id Unique identifier
    /// @param label Button label
    /// @param icon Button icon
    /// @param on_click Click callback
    /// @param user_data User data for callback
    /// @return New item or NULL on failure
    vg_toolbar_item_t *vg_toolbar_add_button(vg_toolbar_t *tb,
                                             const char *id,
                                             const char *label,
                                             vg_icon_t icon,
                                             void (*on_click)(vg_toolbar_item_t *, void *),
                                             void *user_data);

    /// @brief Add a toggle button to the toolbar
    /// @param tb Toolbar widget
    /// @param id Unique identifier
    /// @param label Button label
    /// @param icon Button icon
    /// @param initial_checked Initial checked state
    /// @param on_toggle Toggle callback
    /// @param user_data User data for callback
    /// @return New item or NULL on failure
    vg_toolbar_item_t *vg_toolbar_add_toggle(vg_toolbar_t *tb,
                                             const char *id,
                                             const char *label,
                                             vg_icon_t icon,
                                             bool initial_checked,
                                             void (*on_toggle)(vg_toolbar_item_t *, bool, void *),
                                             void *user_data);

    /// @brief Add a dropdown button to the toolbar
    /// @param tb Toolbar widget
    /// @param id Unique identifier
    /// @param label Button label
    /// @param icon Button icon
    /// @param menu Dropdown menu
    /// @return New item or NULL on failure
    vg_toolbar_item_t *vg_toolbar_add_dropdown(
        vg_toolbar_t *tb, const char *id, const char *label, vg_icon_t icon, struct vg_menu *menu);

    /// @brief Add a separator to the toolbar
    /// @param tb Toolbar widget
    /// @return New item or NULL on failure
    vg_toolbar_item_t *vg_toolbar_add_separator(vg_toolbar_t *tb);

    /// @brief Add a spacer to the toolbar
    /// @param tb Toolbar widget
    /// @return New item or NULL on failure
    vg_toolbar_item_t *vg_toolbar_add_spacer(vg_toolbar_t *tb);

    /// @brief Add a custom widget to the toolbar
    /// @param tb Toolbar widget
    /// @param id Unique identifier
    /// @param widget Custom widget
    /// @return New item or NULL on failure
    vg_toolbar_item_t *vg_toolbar_add_widget(vg_toolbar_t *tb, const char *id, vg_widget_t *widget);

    /// @brief Remove an item from the toolbar by ID
    /// @param tb Toolbar widget
    /// @param id Item ID to remove
    void vg_toolbar_remove_item(vg_toolbar_t *tb, const char *id);

    /// @brief Get an item by ID
    /// @param tb Toolbar widget
    /// @param id Item ID
    /// @return Item or NULL if not found
    vg_toolbar_item_t *vg_toolbar_get_item(vg_toolbar_t *tb, const char *id);

    /// @brief Set item enabled state
    /// @param item Toolbar item
    /// @param enabled Enabled state
    void vg_toolbar_item_set_enabled(vg_toolbar_item_t *item, bool enabled);

    /// @brief Set item checked state (for toggle items)
    /// @param item Toolbar item
    /// @param checked Checked state
    void vg_toolbar_item_set_checked(vg_toolbar_item_t *item, bool checked);

    /// @brief Set item tooltip
    /// @param item Toolbar item
    /// @param tooltip Tooltip text
    void vg_toolbar_item_set_tooltip(vg_toolbar_item_t *item, const char *tooltip);

    /// @brief Set item icon
    /// @param item Toolbar item
    /// @param icon New icon
    void vg_toolbar_item_set_icon(vg_toolbar_item_t *item, vg_icon_t icon);

    /// @brief Set icon size for toolbar
    /// @param tb Toolbar widget
    /// @param size Icon size preset
    void vg_toolbar_set_icon_size(vg_toolbar_t *tb, vg_toolbar_icon_size_t size);

    /// @brief Set whether labels are shown
    /// @param tb Toolbar widget
    /// @param show Show labels
    void vg_toolbar_set_show_labels(vg_toolbar_t *tb, bool show);

    /// @brief Set font for toolbar
    /// @param tb Toolbar widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_toolbar_set_font(vg_toolbar_t *tb, vg_font_t *font, float size);

    /// @brief Create icon from Unicode glyph
    /// @param codepoint Unicode codepoint
    /// @return Icon specification
    vg_icon_t vg_icon_from_glyph(uint32_t codepoint);

    /// @brief Create icon from pixel data
    /// @param rgba RGBA pixel data
    /// @param w Image width
    /// @param h Image height
    /// @return Icon specification
    vg_icon_t vg_icon_from_pixels(uint8_t *rgba, uint32_t w, uint32_t h);

    /// @brief Create icon from file path
    /// @param path File path
    /// @return Icon specification
    vg_icon_t vg_icon_from_file(const char *path);

    /// @brief Destroy icon and free resources
    /// @param icon Icon to destroy
    void vg_icon_destroy(vg_icon_t *icon);

    //=============================================================================
    // Dialog Widget
    //=============================================================================

    /// @brief Dialog button presets
    typedef enum vg_dialog_buttons
    {
        VG_DIALOG_BUTTONS_NONE,
        VG_DIALOG_BUTTONS_OK,
        VG_DIALOG_BUTTONS_OK_CANCEL,
        VG_DIALOG_BUTTONS_YES_NO,
        VG_DIALOG_BUTTONS_YES_NO_CANCEL,
        VG_DIALOG_BUTTONS_RETRY_CANCEL,
        VG_DIALOG_BUTTONS_CUSTOM
    } vg_dialog_buttons_t;

    /// @brief Dialog result codes
    typedef enum vg_dialog_result
    {
        VG_DIALOG_RESULT_NONE, ///< Still open
        VG_DIALOG_RESULT_OK,
        VG_DIALOG_RESULT_CANCEL,
        VG_DIALOG_RESULT_YES,
        VG_DIALOG_RESULT_NO,
        VG_DIALOG_RESULT_RETRY,
        VG_DIALOG_RESULT_CUSTOM_1,
        VG_DIALOG_RESULT_CUSTOM_2,
        VG_DIALOG_RESULT_CUSTOM_3
    } vg_dialog_result_t;

    /// @brief Dialog icon presets
    typedef enum vg_dialog_icon
    {
        VG_DIALOG_ICON_NONE,
        VG_DIALOG_ICON_INFO,
        VG_DIALOG_ICON_WARNING,
        VG_DIALOG_ICON_ERROR,
        VG_DIALOG_ICON_QUESTION,
        VG_DIALOG_ICON_CUSTOM
    } vg_dialog_icon_t;

    /// @brief Custom button definition
    typedef struct vg_dialog_button_def
    {
        char *label;               ///< Button label
        vg_dialog_result_t result; ///< Result code when clicked
        bool is_default;           ///< Activated on Enter
        bool is_cancel;            ///< Activated on Escape
    } vg_dialog_button_def_t;

    /// @brief Dialog widget structure
    typedef struct vg_dialog
    {
        vg_widget_t base;

        // Title bar
        char *title;            ///< Dialog title
        bool show_close_button; ///< Show close button
        bool draggable;         ///< Can be dragged

        // Content
        vg_widget_t *content;  ///< Content widget
        vg_dialog_icon_t icon; ///< Icon preset
        vg_icon_t custom_icon; ///< Custom icon
        char *message;         ///< Simple text message

        // Buttons
        vg_dialog_buttons_t button_preset;      ///< Button preset
        vg_dialog_button_def_t *custom_buttons; ///< Custom buttons
        size_t custom_button_count;             ///< Number of custom buttons

        // Sizing
        uint32_t min_width;  ///< Minimum width
        uint32_t min_height; ///< Minimum height
        uint32_t max_width;  ///< Maximum width
        uint32_t max_height; ///< Maximum height
        bool resizable;      ///< Can be resized

        // Modal behavior
        bool modal;                ///< Block input to parent
        vg_widget_t *modal_parent; ///< Parent to block

        // Font
        vg_font_t *font;       ///< Font for text
        float font_size;       ///< Font size
        float title_font_size; ///< Title font size

        // Colors
        uint32_t bg_color;           ///< Background color
        uint32_t title_bg_color;     ///< Title bar background
        uint32_t title_text_color;   ///< Title text color
        uint32_t text_color;         ///< Content text color
        uint32_t button_bg_color;    ///< Button background
        uint32_t button_hover_color; ///< Button hover color
        uint32_t overlay_color;      ///< Modal overlay color

        // State
        vg_dialog_result_t result; ///< Dialog result
        bool is_open;              ///< Is dialog open
        bool is_dragging;          ///< Is being dragged
        int drag_offset_x;         ///< Drag offset X
        int drag_offset_y;         ///< Drag offset Y
        int hovered_button;        ///< Currently hovered button (-1 = none)

        // Callbacks
        void *user_data;                                                   ///< User data
        void (*on_result)(struct vg_dialog *, vg_dialog_result_t, void *); ///< Result callback
        void (*on_close)(struct vg_dialog *, void *);                      ///< Close callback
    } vg_dialog_t;

    /// @brief Create a new dialog widget
    /// @param title Dialog title
    /// @return New dialog widget or NULL on failure
    vg_dialog_t *vg_dialog_create(const char *title);

    /// @brief Set dialog title
    /// @param dialog Dialog widget
    /// @param title New title
    void vg_dialog_set_title(vg_dialog_t *dialog, const char *title);

    /// @brief Set dialog content widget
    /// @param dialog Dialog widget
    /// @param content Content widget
    void vg_dialog_set_content(vg_dialog_t *dialog, vg_widget_t *content);

    /// @brief Set dialog message (simple text content)
    /// @param dialog Dialog widget
    /// @param message Message text
    void vg_dialog_set_message(vg_dialog_t *dialog, const char *message);

    /// @brief Set dialog icon preset
    /// @param dialog Dialog widget
    /// @param icon Icon preset
    void vg_dialog_set_icon(vg_dialog_t *dialog, vg_dialog_icon_t icon);

    /// @brief Set dialog custom icon
    /// @param dialog Dialog widget
    /// @param icon Custom icon
    void vg_dialog_set_custom_icon(vg_dialog_t *dialog, vg_icon_t icon);

    /// @brief Set dialog button preset
    /// @param dialog Dialog widget
    /// @param buttons Button preset
    void vg_dialog_set_buttons(vg_dialog_t *dialog, vg_dialog_buttons_t buttons);

    /// @brief Set custom buttons
    /// @param dialog Dialog widget
    /// @param buttons Button definitions
    /// @param count Number of buttons
    void vg_dialog_set_custom_buttons(vg_dialog_t *dialog,
                                      vg_dialog_button_def_t *buttons,
                                      size_t count);

    /// @brief Set dialog resizable
    /// @param dialog Dialog widget
    /// @param resizable Resizable state
    void vg_dialog_set_resizable(vg_dialog_t *dialog, bool resizable);

    /// @brief Set dialog size constraints
    /// @param dialog Dialog widget
    /// @param min_w Minimum width
    /// @param min_h Minimum height
    /// @param max_w Maximum width
    /// @param max_h Maximum height
    void vg_dialog_set_size_constraints(
        vg_dialog_t *dialog, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h);

    /// @brief Set dialog modal state
    /// @param dialog Dialog widget
    /// @param modal Modal state
    /// @param parent Parent to block
    void vg_dialog_set_modal(vg_dialog_t *dialog, bool modal, vg_widget_t *parent);

    /// @brief Show the dialog
    /// @param dialog Dialog widget
    void vg_dialog_show(vg_dialog_t *dialog);

    /// @brief Show the dialog centered relative to another widget
    /// @param dialog Dialog widget
    /// @param relative_to Widget to center on
    void vg_dialog_show_centered(vg_dialog_t *dialog, vg_widget_t *relative_to);

    /// @brief Hide the dialog
    /// @param dialog Dialog widget
    void vg_dialog_hide(vg_dialog_t *dialog);

    /// @brief Close the dialog with a result
    /// @param dialog Dialog widget
    /// @param result Result code
    void vg_dialog_close(vg_dialog_t *dialog, vg_dialog_result_t result);

    /// @brief Get dialog result
    /// @param dialog Dialog widget
    /// @return Result code
    vg_dialog_result_t vg_dialog_get_result(vg_dialog_t *dialog);

    /// @brief Check if dialog is open
    /// @param dialog Dialog widget
    /// @return True if open
    bool vg_dialog_is_open(vg_dialog_t *dialog);

    /// @brief Set result callback
    /// @param dialog Dialog widget
    /// @param callback Callback function
    /// @param user_data User data
    void vg_dialog_set_on_result(vg_dialog_t *dialog,
                                 void (*callback)(vg_dialog_t *, vg_dialog_result_t, void *),
                                 void *user_data);

    /// @brief Set close callback
    /// @param dialog Dialog widget
    /// @param callback Callback function
    /// @param user_data User data
    void vg_dialog_set_on_close(vg_dialog_t *dialog,
                                void (*callback)(vg_dialog_t *, void *),
                                void *user_data);

    /// @brief Set font for dialog
    /// @param dialog Dialog widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_dialog_set_font(vg_dialog_t *dialog, vg_font_t *font, float size);

    /// @brief Create a simple message dialog
    /// @param title Dialog title
    /// @param message Message text
    /// @param icon Icon preset
    /// @param buttons Button preset
    /// @return New dialog or NULL on failure
    vg_dialog_t *vg_dialog_message(const char *title,
                                   const char *message,
                                   vg_dialog_icon_t icon,
                                   vg_dialog_buttons_t buttons);

    /// @brief Create a confirmation dialog
    /// @param title Dialog title
    /// @param message Confirmation message
    /// @param on_confirm Callback when confirmed
    /// @param user_data User data
    /// @return New dialog or NULL on failure
    vg_dialog_t *vg_dialog_confirm(const char *title,
                                   const char *message,
                                   void (*on_confirm)(void *),
                                   void *user_data);

    //=============================================================================
    // FileDialog Widget
    //=============================================================================

    /// @brief File dialog mode
    typedef enum vg_filedialog_mode
    {
        VG_FILEDIALOG_OPEN,         ///< Select existing file(s)
        VG_FILEDIALOG_SAVE,         ///< Select location to save
        VG_FILEDIALOG_SELECT_FOLDER ///< Select directory
    } vg_filedialog_mode_t;

    /// @brief File filter
    typedef struct vg_file_filter
    {
        char *name;    ///< Display name (e.g., "Viper Files")
        char *pattern; ///< Glob pattern (e.g., "*.viper;*.vpr")
    } vg_file_filter_t;

    /// @brief File/directory entry
    typedef struct vg_file_entry
    {
        char *name;             ///< File name
        char *full_path;        ///< Full path
        bool is_directory;      ///< Is this a directory
        uint64_t size;          ///< File size in bytes
        uint64_t modified_time; ///< Unix timestamp
    } vg_file_entry_t;

    /// @brief Bookmark entry
    typedef struct vg_bookmark
    {
        char *name;     ///< Display name
        char *path;     ///< Full path
        vg_icon_t icon; ///< Optional icon
    } vg_bookmark_t;

    /// @brief FileDialog widget structure
    typedef struct vg_filedialog
    {
        vg_dialog_t base; ///< Inherit from dialog

        vg_filedialog_mode_t mode; ///< Dialog mode

        // Current state
        char *current_path;        ///< Current directory
        vg_file_entry_t **entries; ///< Files in current directory
        size_t entry_count;        ///< Number of entries
        size_t entry_capacity;     ///< Entry array capacity

        // Selection
        int *selected_indices;     ///< Selected file indices
        size_t selection_count;    ///< Number of selected files
        size_t selection_capacity; ///< Selection array capacity
        bool multi_select;         ///< Allow multiple selection (open mode only)

        // Filters
        vg_file_filter_t *filters; ///< File filters
        size_t filter_count;       ///< Number of filters
        size_t filter_capacity;    ///< Filter array capacity
        size_t active_filter;      ///< Currently selected filter

        // Bookmarks
        vg_bookmark_t *bookmarks; ///< Bookmarks
        size_t bookmark_count;    ///< Number of bookmarks
        size_t bookmark_capacity; ///< Bookmark array capacity

        // Configuration
        bool show_hidden;        ///< Show hidden files
        bool confirm_overwrite;  ///< Ask before overwriting (save mode)
        char *default_filename;  ///< Pre-filled filename (save mode)
        char *default_extension; ///< Auto-add extension

        // Child widget state (widgets created during show)
        void *path_input;      ///< Path text input
        void *file_list;       ///< File listing
        void *filename_input;  ///< Filename input (save mode)
        void *filter_dropdown; ///< Filter selector
        void *bookmark_list;   ///< Sidebar bookmarks

        // Result
        char **selected_files;      ///< Result: array of selected paths
        size_t selected_file_count; ///< Number of selected files

        // Callbacks
        void *user_data; ///< User data
        void (*on_select)(struct vg_filedialog *dialog,
                          char **paths,
                          size_t count,
                          void *user_data);                               ///< Selection callback
        void (*on_cancel)(struct vg_filedialog *dialog, void *user_data); ///< Cancel callback
    } vg_filedialog_t;

    /// @brief Create a new file dialog widget
    /// @param mode Dialog mode
    /// @return New file dialog widget or NULL on failure
    vg_filedialog_t *vg_filedialog_create(vg_filedialog_mode_t mode);

    /// @brief Destroy a file dialog widget
    /// @param dialog FileDialog widget
    void vg_filedialog_destroy(vg_filedialog_t *dialog);

    /// @brief Set dialog title
    /// @param dialog FileDialog widget
    /// @param title New title
    void vg_filedialog_set_title(vg_filedialog_t *dialog, const char *title);

    /// @brief Set initial directory path
    /// @param dialog FileDialog widget
    /// @param path Initial path
    void vg_filedialog_set_initial_path(vg_filedialog_t *dialog, const char *path);

    /// @brief Set default filename (for save mode)
    /// @param dialog FileDialog widget
    /// @param filename Default filename
    void vg_filedialog_set_filename(vg_filedialog_t *dialog, const char *filename);

    /// @brief Set multi-select mode (for open mode)
    /// @param dialog FileDialog widget
    /// @param multi Enable multi-select
    void vg_filedialog_set_multi_select(vg_filedialog_t *dialog, bool multi);

    /// @brief Set show hidden files
    /// @param dialog FileDialog widget
    /// @param show Show hidden files
    void vg_filedialog_set_show_hidden(vg_filedialog_t *dialog, bool show);

    /// @brief Set confirm overwrite (for save mode)
    /// @param dialog FileDialog widget
    /// @param confirm Confirm overwrite
    void vg_filedialog_set_confirm_overwrite(vg_filedialog_t *dialog, bool confirm);

    /// @brief Add a file filter
    /// @param dialog FileDialog widget
    /// @param name Filter display name
    /// @param pattern Filter pattern (e.g., "*.txt;*.md")
    void vg_filedialog_add_filter(vg_filedialog_t *dialog, const char *name, const char *pattern);

    /// @brief Clear all file filters
    /// @param dialog FileDialog widget
    void vg_filedialog_clear_filters(vg_filedialog_t *dialog);

    /// @brief Set default extension (auto-added to filename in save mode)
    /// @param dialog FileDialog widget
    /// @param ext Default extension (without dot)
    void vg_filedialog_set_default_extension(vg_filedialog_t *dialog, const char *ext);

    /// @brief Add a bookmark
    /// @param dialog FileDialog widget
    /// @param name Bookmark display name
    /// @param path Bookmark path
    void vg_filedialog_add_bookmark(vg_filedialog_t *dialog, const char *name, const char *path);

    /// @brief Add default bookmarks (Home, Desktop, Documents)
    /// @param dialog FileDialog widget
    void vg_filedialog_add_default_bookmarks(vg_filedialog_t *dialog);

    /// @brief Clear all bookmarks
    /// @param dialog FileDialog widget
    void vg_filedialog_clear_bookmarks(vg_filedialog_t *dialog);

    /// @brief Show the file dialog
    /// @param dialog FileDialog widget
    void vg_filedialog_show(vg_filedialog_t *dialog);

    /// @brief Get selected file paths (after dialog closes)
    /// @param dialog FileDialog widget
    /// @param count Output count
    /// @return Array of selected paths (owned by dialog)
    char **vg_filedialog_get_selected_paths(vg_filedialog_t *dialog, size_t *count);

    /// @brief Get single selected file path (convenience)
    /// @param dialog FileDialog widget
    /// @return Selected path or NULL
    char *vg_filedialog_get_selected_path(vg_filedialog_t *dialog);

    /// @brief Set selection callback
    /// @param dialog FileDialog widget
    /// @param callback Selection callback
    /// @param user_data User data
    void vg_filedialog_set_on_select(vg_filedialog_t *dialog,
                                     void (*callback)(vg_filedialog_t *, char **, size_t, void *),
                                     void *user_data);

    /// @brief Set cancel callback
    /// @param dialog FileDialog widget
    /// @param callback Cancel callback
    /// @param user_data User data
    void vg_filedialog_set_on_cancel(vg_filedialog_t *dialog,
                                     void (*callback)(vg_filedialog_t *, void *),
                                     void *user_data);

    /// @brief Convenience: Open a single file
    /// @param title Dialog title
    /// @param initial_path Initial path
    /// @param filter_name Filter name
    /// @param filter_pattern Filter pattern
    /// @return Selected path (caller must free) or NULL
    char *vg_filedialog_open_file(const char *title,
                                  const char *initial_path,
                                  const char *filter_name,
                                  const char *filter_pattern);

    /// @brief Convenience: Save a file
    /// @param title Dialog title
    /// @param initial_path Initial path
    /// @param default_name Default filename
    /// @param filter_name Filter name
    /// @param filter_pattern Filter pattern
    /// @return Selected path (caller must free) or NULL
    char *vg_filedialog_save_file(const char *title,
                                  const char *initial_path,
                                  const char *default_name,
                                  const char *filter_name,
                                  const char *filter_pattern);

    /// @brief Convenience: Select a folder
    /// @param title Dialog title
    /// @param initial_path Initial path
    /// @return Selected path (caller must free) or NULL
    char *vg_filedialog_select_folder(const char *title, const char *initial_path);

    //=============================================================================
    // ContextMenu Widget
    //=============================================================================

    /// @brief ContextMenu widget structure
    typedef struct vg_contextmenu
    {
        vg_widget_t base;

        // Menu items (reuses vg_menu_item_t structure)
        vg_menu_item_t **items; ///< Array of items
        size_t item_count;      ///< Number of items
        size_t item_capacity;   ///< Allocated capacity

        // Positioning
        int anchor_x; ///< Screen X where menu appears
        int anchor_y; ///< Screen Y where menu appears

        // State
        bool is_visible;                       ///< Is menu visible
        int hovered_index;                     ///< Hovered item index (-1 if none)
        struct vg_contextmenu *active_submenu; ///< Open submenu
        struct vg_contextmenu *parent_menu;    ///< Parent menu (for submenus)

        // Styling
        uint32_t min_width;  ///< Minimum menu width (default: 150)
        uint32_t max_height; ///< Maximum height before scrolling

        // Font
        vg_font_t *font; ///< Font for text
        float font_size; ///< Font size

        // Colors
        uint32_t bg_color;        ///< Background color
        uint32_t hover_color;     ///< Hover color
        uint32_t text_color;      ///< Text color
        uint32_t disabled_color;  ///< Disabled text color
        uint32_t border_color;    ///< Border color
        uint32_t separator_color; ///< Separator color

        // Callbacks
        void *user_data; ///< User data
        void (*on_select)(struct vg_contextmenu *menu,
                          vg_menu_item_t *item,
                          void *user_data);                               ///< Selection callback
        void (*on_dismiss)(struct vg_contextmenu *menu, void *user_data); ///< Dismiss callback
    } vg_contextmenu_t;

    /// @brief Create a new context menu widget
    /// @return New context menu widget or NULL on failure
    vg_contextmenu_t *vg_contextmenu_create(void);

    /// @brief Destroy a context menu widget
    /// @param menu ContextMenu widget
    void vg_contextmenu_destroy(vg_contextmenu_t *menu);

    /// @brief Add an item to the context menu
    /// @param menu ContextMenu widget
    /// @param label Item label
    /// @param shortcut Keyboard shortcut text (can be NULL)
    /// @param action Action callback
    /// @param user_data User data for callback
    /// @return New item or NULL on failure
    vg_menu_item_t *vg_contextmenu_add_item(vg_contextmenu_t *menu,
                                            const char *label,
                                            const char *shortcut,
                                            void (*action)(void *),
                                            void *user_data);

    /// @brief Add a submenu to the context menu
    /// @param menu ContextMenu widget
    /// @param label Submenu label
    /// @param submenu Submenu widget
    /// @return New item or NULL on failure
    vg_menu_item_t *vg_contextmenu_add_submenu(vg_contextmenu_t *menu,
                                               const char *label,
                                               vg_contextmenu_t *submenu);

    /// @brief Add a separator to the context menu
    /// @param menu ContextMenu widget
    void vg_contextmenu_add_separator(vg_contextmenu_t *menu);

    /// @brief Clear all items from the context menu
    /// @param menu ContextMenu widget
    void vg_contextmenu_clear(vg_contextmenu_t *menu);

    /// @brief Set item enabled state
    /// @param item Menu item
    /// @param enabled Enabled state
    void vg_contextmenu_item_set_enabled(vg_menu_item_t *item, bool enabled);

    /// @brief Set item checked state
    /// @param item Menu item
    /// @param checked Checked state
    void vg_contextmenu_item_set_checked(vg_menu_item_t *item, bool checked);

    /// @brief Set item icon
    /// @param item Menu item
    /// @param icon Icon specification
    void vg_contextmenu_item_set_icon(vg_menu_item_t *item, vg_icon_t icon);

    /// @brief Show context menu at position
    /// @param menu ContextMenu widget
    /// @param x Screen X position
    /// @param y Screen Y position
    void vg_contextmenu_show_at(vg_contextmenu_t *menu, int x, int y);

    /// @brief Show context menu relative to a widget
    /// @param menu ContextMenu widget
    /// @param widget Widget to position relative to
    /// @param offset_x X offset from widget
    /// @param offset_y Y offset from widget
    void vg_contextmenu_show_for_widget(vg_contextmenu_t *menu,
                                        vg_widget_t *widget,
                                        int offset_x,
                                        int offset_y);

    /// @brief Dismiss (hide) the context menu
    /// @param menu ContextMenu widget
    void vg_contextmenu_dismiss(vg_contextmenu_t *menu);

    /// @brief Set selection callback
    /// @param menu ContextMenu widget
    /// @param callback Selection callback
    /// @param user_data User data
    void vg_contextmenu_set_on_select(vg_contextmenu_t *menu,
                                      void (*callback)(vg_contextmenu_t *,
                                                       vg_menu_item_t *,
                                                       void *),
                                      void *user_data);

    /// @brief Set dismiss callback
    /// @param menu ContextMenu widget
    /// @param callback Dismiss callback
    /// @param user_data User data
    void vg_contextmenu_set_on_dismiss(vg_contextmenu_t *menu,
                                       void (*callback)(vg_contextmenu_t *, void *),
                                       void *user_data);

    /// @brief Register a context menu for a widget (shown on right-click)
    /// @param widget Widget to register for
    /// @param menu Context menu to show
    void vg_contextmenu_register_for_widget(vg_widget_t *widget, vg_contextmenu_t *menu);

    /// @brief Unregister context menu from a widget
    /// @param widget Widget to unregister from
    void vg_contextmenu_unregister_for_widget(vg_widget_t *widget);

    /// @brief Set font for context menu
    /// @param menu ContextMenu widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_contextmenu_set_font(vg_contextmenu_t *menu, vg_font_t *font, float size);

    //=============================================================================
    // FindReplaceBar Widget
    //=============================================================================

    /// @brief Search options
    typedef struct vg_search_options
    {
        bool case_sensitive; ///< Case-sensitive search
        bool whole_word;     ///< Match whole words only
        bool use_regex;      ///< Use regular expressions
        bool in_selection;   ///< Search within selection only
        bool wrap_around;    ///< Wrap to beginning when reaching end
    } vg_search_options_t;

    /// @brief Search match
    typedef struct vg_search_match
    {
        uint32_t line;      ///< Line number (0-based)
        uint32_t start_col; ///< Start column
        uint32_t end_col;   ///< End column
    } vg_search_match_t;

    // Forward declaration for FindReplaceBar
    struct vg_codeeditor;
    struct vg_textinput;
    struct vg_button;
    struct vg_checkbox;

    /// @brief FindReplaceBar widget structure
    typedef struct vg_findreplacebar
    {
        vg_widget_t base;

        // Mode
        bool show_replace; ///< Show replace controls

        // Child widgets (void* to avoid circular deps)
        void *find_input;        ///< Find text input
        void *replace_input;     ///< Replace text input
        void *find_prev_btn;     ///< Find previous button
        void *find_next_btn;     ///< Find next button
        void *replace_btn;       ///< Replace button
        void *replace_all_btn;   ///< Replace all button
        void *close_btn;         ///< Close button
        void *case_sensitive_cb; ///< Case sensitive checkbox
        void *whole_word_cb;     ///< Whole word checkbox
        void *regex_cb;          ///< Regex checkbox

        // Search state
        vg_search_options_t options; ///< Search options
        vg_search_match_t *matches;  ///< All matches in document
        size_t match_count;          ///< Number of matches
        size_t match_capacity;       ///< Match array capacity
        size_t current_match;        ///< Index of current match

        // Target editor
        struct vg_codeeditor *target_editor; ///< Editor to search in

        // Result display
        char result_text[64]; ///< "3 of 42" or "No results"

        // Font
        vg_font_t *font; ///< Font for text
        float font_size; ///< Font size

        // Colors
        uint32_t bg_color;          ///< Background color
        uint32_t border_color;      ///< Border color
        uint32_t match_highlight;   ///< Match highlight color
        uint32_t current_highlight; ///< Current match highlight

        // Callbacks
        void *user_data; ///< User data
        void (*on_find)(struct vg_findreplacebar *bar,
                        const char *query,
                        vg_search_options_t *options,
                        void *user_data);
        void (*on_replace)(struct vg_findreplacebar *bar,
                           const char *find,
                           const char *replace,
                           void *user_data);
        void (*on_replace_all)(struct vg_findreplacebar *bar,
                               const char *find,
                               const char *replace,
                               void *user_data);
        void (*on_close)(struct vg_findreplacebar *bar, void *user_data);
    } vg_findreplacebar_t;

    /// @brief Create a new find/replace bar widget
    /// @return New find/replace bar or NULL on failure
    vg_findreplacebar_t *vg_findreplacebar_create(void);

    /// @brief Destroy a find/replace bar widget
    /// @param bar FindReplaceBar widget
    void vg_findreplacebar_destroy(vg_findreplacebar_t *bar);

    /// @brief Set target editor for searching
    /// @param bar FindReplaceBar widget
    /// @param editor Code editor widget
    void vg_findreplacebar_set_target(vg_findreplacebar_t *bar, struct vg_codeeditor *editor);

    /// @brief Show or hide replace controls
    /// @param bar FindReplaceBar widget
    /// @param show Show replace controls
    void vg_findreplacebar_set_show_replace(vg_findreplacebar_t *bar, bool show);

    /// @brief Set search options
    /// @param bar FindReplaceBar widget
    /// @param options Search options
    void vg_findreplacebar_set_options(vg_findreplacebar_t *bar, vg_search_options_t *options);

    /// @brief Perform search with query
    /// @param bar FindReplaceBar widget
    /// @param query Search query
    void vg_findreplacebar_find(vg_findreplacebar_t *bar, const char *query);

    /// @brief Find next match
    /// @param bar FindReplaceBar widget
    void vg_findreplacebar_find_next(vg_findreplacebar_t *bar);

    /// @brief Find previous match
    /// @param bar FindReplaceBar widget
    void vg_findreplacebar_find_prev(vg_findreplacebar_t *bar);

    /// @brief Replace current match
    /// @param bar FindReplaceBar widget
    void vg_findreplacebar_replace_current(vg_findreplacebar_t *bar);

    /// @brief Replace all matches
    /// @param bar FindReplaceBar widget
    void vg_findreplacebar_replace_all(vg_findreplacebar_t *bar);

    /// @brief Get match count
    /// @param bar FindReplaceBar widget
    /// @return Number of matches
    size_t vg_findreplacebar_get_match_count(vg_findreplacebar_t *bar);

    /// @brief Get current match index
    /// @param bar FindReplaceBar widget
    /// @return Current match index
    size_t vg_findreplacebar_get_current_match(vg_findreplacebar_t *bar);

    /// @brief Focus the find input
    /// @param bar FindReplaceBar widget
    void vg_findreplacebar_focus(vg_findreplacebar_t *bar);

    /// @brief Set find text
    /// @param bar FindReplaceBar widget
    /// @param text Text to search for
    void vg_findreplacebar_set_find_text(vg_findreplacebar_t *bar, const char *text);

    /// @brief Set close callback
    /// @param bar FindReplaceBar widget
    /// @param callback Close callback
    /// @param user_data User data
    void vg_findreplacebar_set_on_close(vg_findreplacebar_t *bar,
                                        void (*callback)(vg_findreplacebar_t *, void *),
                                        void *user_data);

    /// @brief Set font for find/replace bar
    /// @param bar FindReplaceBar widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_findreplacebar_set_font(vg_findreplacebar_t *bar, vg_font_t *font, float size);

    //=============================================================================
    // TreeView Widget
    //=============================================================================

    /// @brief Tree node structure
    typedef struct vg_tree_node
    {
        const char *text;            ///< Node text (owned)
        void *user_data;             ///< User data associated with node
        bool expanded;               ///< Is node expanded
        bool selected;               ///< Is node selected
        bool has_children;           ///< Does node have children (for lazy loading)
        bool loading;                ///< Is node loading children (lazy loading)
        struct vg_tree_node *parent; ///< Parent node
        struct vg_tree_node *first_child;
        struct vg_tree_node *last_child;
        struct vg_tree_node *next_sibling;
        struct vg_tree_node *prev_sibling;
        int child_count;
        int depth; ///< Depth in tree (0 = root)

        // Icon support
        vg_icon_t icon;          ///< Node icon
        vg_icon_t expanded_icon; ///< Icon when expanded (optional, for folders)
    } vg_tree_node_t;

    /// @brief TreeView callback types
    typedef void (*vg_tree_select_callback_t)(vg_widget_t *tree,
                                              vg_tree_node_t *node,
                                              void *user_data);
    typedef void (*vg_tree_expand_callback_t)(vg_widget_t *tree,
                                              vg_tree_node_t *node,
                                              bool expanded,
                                              void *user_data);
    typedef void (*vg_tree_activate_callback_t)(vg_widget_t *tree,
                                                vg_tree_node_t *node,
                                                void *user_data);

    /// @brief Drop position for drag-and-drop
    typedef enum vg_tree_drop_position
    {
        VG_TREE_DROP_BEFORE, ///< Drop before target node
        VG_TREE_DROP_AFTER,  ///< Drop after target node
        VG_TREE_DROP_INTO    ///< Drop as child of target node
    } vg_tree_drop_position_t;

    /// @brief Drag-and-drop callback types
    typedef bool (*vg_tree_can_drag_callback_t)(vg_tree_node_t *node, void *user_data);
    typedef bool (*vg_tree_can_drop_callback_t)(vg_tree_node_t *source,
                                                vg_tree_node_t *target,
                                                vg_tree_drop_position_t position,
                                                void *user_data);
    typedef void (*vg_tree_on_drop_callback_t)(vg_tree_node_t *source,
                                               vg_tree_node_t *target,
                                               vg_tree_drop_position_t position,
                                               void *user_data);

    /// @brief Lazy loading callback type
    typedef void (*vg_tree_load_children_callback_t)(struct vg_treeview *tree,
                                                     vg_tree_node_t *node,
                                                     void *user_data);

    /// @brief TreeView widget structure
    typedef struct vg_treeview
    {
        vg_widget_t base;

        vg_tree_node_t *root;     ///< Root node (hidden, children are top-level)
        vg_tree_node_t *selected; ///< Currently selected node
        vg_font_t *font;          ///< Font for rendering
        float font_size;          ///< Font size

        // Appearance
        float row_height;     ///< Height of each row
        float indent_size;    ///< Indentation per level
        float icon_size;      ///< Icon size
        float icon_gap;       ///< Gap between icon and text
        uint32_t text_color;  ///< Text color
        uint32_t selected_bg; ///< Selected item background
        uint32_t hover_bg;    ///< Hover background

        // Scrolling
        float scroll_y;    ///< Vertical scroll position
        int visible_start; ///< First visible row index
        int visible_count; ///< Number of visible rows

        // Callbacks
        vg_tree_select_callback_t on_select;
        void *on_select_data;
        vg_tree_expand_callback_t on_expand;
        void *on_expand_data;
        vg_tree_activate_callback_t on_activate;
        void *on_activate_data;

        // Lazy loading
        vg_tree_load_children_callback_t on_load_children;
        void *on_load_children_data;

        // Drag and drop
        bool drag_enabled;                     ///< Enable drag-and-drop
        vg_tree_node_t *drag_node;             ///< Node being dragged
        int drag_start_x;                      ///< Drag start X position
        int drag_start_y;                      ///< Drag start Y position
        bool is_dragging;                      ///< Currently dragging
        vg_tree_node_t *drop_target;           ///< Current drop target
        vg_tree_drop_position_t drop_position; ///< Current drop position

        // Drag callbacks
        vg_tree_can_drag_callback_t can_drag;
        vg_tree_can_drop_callback_t can_drop;
        vg_tree_on_drop_callback_t on_drop;
        void *drag_user_data;

        // State
        vg_tree_node_t *hovered; ///< Currently hovered node
    } vg_treeview_t;

    /// @brief Create a new tree view widget
    /// @param parent Parent widget (can be NULL)
    /// @return New tree view widget or NULL on failure
    vg_treeview_t *vg_treeview_create(vg_widget_t *parent);

    /// @brief Get tree root node
    /// @param tree TreeView widget
    /// @return Root node
    vg_tree_node_t *vg_treeview_get_root(vg_treeview_t *tree);

    /// @brief Add a child node
    /// @param tree TreeView widget
    /// @param parent Parent node (NULL for root children)
    /// @param text Node text
    /// @return New node or NULL on failure
    vg_tree_node_t *vg_treeview_add_node(vg_treeview_t *tree,
                                         vg_tree_node_t *parent,
                                         const char *text);

    /// @brief Remove a node and all its children
    /// @param tree TreeView widget
    /// @param node Node to remove
    void vg_treeview_remove_node(vg_treeview_t *tree, vg_tree_node_t *node);

    /// @brief Clear all nodes
    /// @param tree TreeView widget
    void vg_treeview_clear(vg_treeview_t *tree);

    /// @brief Expand a node
    /// @param tree TreeView widget
    /// @param node Node to expand
    void vg_treeview_expand(vg_treeview_t *tree, vg_tree_node_t *node);

    /// @brief Collapse a node
    /// @param tree TreeView widget
    /// @param node Node to collapse
    void vg_treeview_collapse(vg_treeview_t *tree, vg_tree_node_t *node);

    /// @brief Toggle node expansion
    /// @param tree TreeView widget
    /// @param node Node to toggle
    void vg_treeview_toggle(vg_treeview_t *tree, vg_tree_node_t *node);

    /// @brief Select a node
    /// @param tree TreeView widget
    /// @param node Node to select
    void vg_treeview_select(vg_treeview_t *tree, vg_tree_node_t *node);

    /// @brief Scroll to make a node visible
    /// @param tree TreeView widget
    /// @param node Node to scroll to
    void vg_treeview_scroll_to(vg_treeview_t *tree, vg_tree_node_t *node);

    /// @brief Set node user data
    /// @param node Tree node
    /// @param data User data
    void vg_tree_node_set_data(vg_tree_node_t *node, void *data);

    /// @brief Set font for tree view
    /// @param tree TreeView widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_treeview_set_font(vg_treeview_t *tree, vg_font_t *font, float size);

    /// @brief Set selection callback
    /// @param tree TreeView widget
    /// @param callback Selection handler
    /// @param user_data User data
    void vg_treeview_set_on_select(vg_treeview_t *tree,
                                   vg_tree_select_callback_t callback,
                                   void *user_data);

    /// @brief Set expand callback
    /// @param tree TreeView widget
    /// @param callback Expand handler
    /// @param user_data User data
    void vg_treeview_set_on_expand(vg_treeview_t *tree,
                                   vg_tree_expand_callback_t callback,
                                   void *user_data);

    /// @brief Set activate (double-click) callback
    /// @param tree TreeView widget
    /// @param callback Activate handler
    /// @param user_data User data
    void vg_treeview_set_on_activate(vg_treeview_t *tree,
                                     vg_tree_activate_callback_t callback,
                                     void *user_data);

    // --- Icon Support ---

    /// @brief Set node icon
    /// @param node Tree node
    /// @param icon Icon specification
    void vg_tree_node_set_icon(vg_tree_node_t *node, vg_icon_t icon);

    /// @brief Set node expanded icon (used when node is expanded, e.g., open folder)
    /// @param node Tree node
    /// @param icon Icon specification
    void vg_tree_node_set_expanded_icon(vg_tree_node_t *node, vg_icon_t icon);

    // --- Drag and Drop ---

    /// @brief Enable or disable drag-and-drop for tree view
    /// @param tree TreeView widget
    /// @param enabled Enable drag-and-drop
    void vg_treeview_set_drag_enabled(vg_treeview_t *tree, bool enabled);

    /// @brief Set drag-and-drop callbacks
    /// @param tree TreeView widget
    /// @param can_drag Callback to check if node can be dragged
    /// @param can_drop Callback to check if drop is allowed
    /// @param on_drop Callback when drop occurs
    /// @param user_data User data passed to callbacks
    void vg_treeview_set_drag_callbacks(vg_treeview_t *tree,
                                        vg_tree_can_drag_callback_t can_drag,
                                        vg_tree_can_drop_callback_t can_drop,
                                        vg_tree_on_drop_callback_t on_drop,
                                        void *user_data);

    // --- Lazy Loading ---

    /// @brief Set lazy loading callback (called when node with has_children=true is expanded)
    /// @param tree TreeView widget
    /// @param callback Load children callback
    /// @param user_data User data
    void vg_treeview_set_on_load_children(vg_treeview_t *tree,
                                          vg_tree_load_children_callback_t callback,
                                          void *user_data);

    /// @brief Set whether a node has children (for lazy loading indicator)
    /// @param node Tree node
    /// @param has_children Has children flag
    void vg_tree_node_set_has_children(vg_tree_node_t *node, bool has_children);

    /// @brief Set node loading state (shows spinner while loading children)
    /// @param node Tree node
    /// @param loading Loading state
    void vg_tree_node_set_loading(vg_tree_node_t *node, bool loading);

    //=============================================================================
    // TabBar Widget
    //=============================================================================

    /// @brief Tab structure
    typedef struct vg_tab
    {
        const char *title;   ///< Tab title (owned)
        const char *tooltip; ///< Tab tooltip (owned)
        void *user_data;     ///< User data
        bool closable;       ///< Can tab be closed
        bool modified;       ///< Show modified indicator
        struct vg_tab *next;
        struct vg_tab *prev;
    } vg_tab_t;

    /// @brief Tab callbacks
    typedef void (*vg_tab_select_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
    typedef bool (*vg_tab_close_callback_t)(vg_widget_t *tabbar, vg_tab_t *tab, void *user_data);
    typedef void (*vg_tab_reorder_callback_t)(vg_widget_t *tabbar,
                                              vg_tab_t *tab,
                                              int new_index,
                                              void *user_data);

    /// @brief TabBar widget structure
    typedef struct vg_tabbar
    {
        vg_widget_t base;

        vg_tab_t *first_tab;  ///< First tab
        vg_tab_t *last_tab;   ///< Last tab
        vg_tab_t *active_tab; ///< Currently active tab
        int tab_count;        ///< Number of tabs

        vg_font_t *font; ///< Font for rendering
        float font_size; ///< Font size

        // Appearance
        float tab_height;        ///< Tab height
        float tab_padding;       ///< Tab horizontal padding
        float close_button_size; ///< Close button size
        float max_tab_width;     ///< Maximum tab width
        uint32_t active_bg;      ///< Active tab background
        uint32_t inactive_bg;    ///< Inactive tab background
        uint32_t text_color;     ///< Text color
        uint32_t close_color;    ///< Close button color

        // Scrolling (for many tabs)
        float scroll_x;    ///< Horizontal scroll offset
        float total_width; ///< Total width of all tabs

        // Callbacks
        vg_tab_select_callback_t on_select;
        void *on_select_data;
        vg_tab_close_callback_t on_close;
        void *on_close_data;
        vg_tab_reorder_callback_t on_reorder;
        void *on_reorder_data;

        // State
        vg_tab_t *hovered_tab;     ///< Currently hovered tab
        bool close_button_hovered; ///< Is close button hovered
        bool dragging;             ///< Is dragging a tab
        vg_tab_t *drag_tab;        ///< Tab being dragged
        float drag_x;              ///< Drag position

        // Per-frame tracking for Zia runtime
        vg_tab_t *prev_active_tab;   ///< Previous active tab (for change detection)
        vg_tab_t *close_clicked_tab; ///< Tab whose close button was clicked (cleared on read)
        bool auto_close;             ///< Auto-remove tab on close click (default true)
    } vg_tabbar_t;

    /// @brief Create a new tab bar widget
    /// @param parent Parent widget (can be NULL)
    /// @return New tab bar widget or NULL on failure
    vg_tabbar_t *vg_tabbar_create(vg_widget_t *parent);

    /// @brief Add a tab
    /// @param tabbar TabBar widget
    /// @param title Tab title
    /// @param closable Can tab be closed
    /// @return New tab or NULL on failure
    vg_tab_t *vg_tabbar_add_tab(vg_tabbar_t *tabbar, const char *title, bool closable);

    /// @brief Remove a tab
    /// @param tabbar TabBar widget
    /// @param tab Tab to remove
    void vg_tabbar_remove_tab(vg_tabbar_t *tabbar, vg_tab_t *tab);

    /// @brief Set active tab
    /// @param tabbar TabBar widget
    /// @param tab Tab to activate
    void vg_tabbar_set_active(vg_tabbar_t *tabbar, vg_tab_t *tab);

    /// @brief Get active tab
    /// @param tabbar TabBar widget
    /// @return Active tab or NULL
    vg_tab_t *vg_tabbar_get_active(vg_tabbar_t *tabbar);

    /// @brief Get the index of a tab in the tab bar
    /// @param tabbar TabBar widget
    /// @param tab Tab to find
    /// @return 0-based index, or -1 if not found
    int vg_tabbar_get_tab_index(vg_tabbar_t *tabbar, vg_tab_t *tab);

    /// @brief Get a tab by index
    /// @param tabbar TabBar widget
    /// @param index 0-based tab index
    /// @return Tab at the given index, or NULL if out of bounds
    vg_tab_t *vg_tabbar_get_tab_at(vg_tabbar_t *tabbar, int index);

    /// @brief Set tab title
    /// @param tab Tab
    /// @param title New title
    void vg_tab_set_title(vg_tab_t *tab, const char *title);

    /// @brief Set tab modified state
    /// @param tab Tab
    /// @param modified Modified state
    void vg_tab_set_modified(vg_tab_t *tab, bool modified);

    /// @brief Set tab user data
    /// @param tab Tab
    /// @param data User data
    void vg_tab_set_data(vg_tab_t *tab, void *data);

    /// @brief Set font for tab bar
    /// @param tabbar TabBar widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_tabbar_set_font(vg_tabbar_t *tabbar, vg_font_t *font, float size);

    /// @brief Set tab selection callback
    /// @param tabbar TabBar widget
    /// @param callback Selection callback
    /// @param user_data User data for callback
    void vg_tabbar_set_on_select(vg_tabbar_t *tabbar,
                                 vg_tab_select_callback_t callback,
                                 void *user_data);

    /// @brief Set tab close callback
    /// @param tabbar TabBar widget
    /// @param callback Close callback (return true to allow close)
    /// @param user_data User data for callback
    void vg_tabbar_set_on_close(vg_tabbar_t *tabbar,
                                vg_tab_close_callback_t callback,
                                void *user_data);

    /// @brief Set tab reorder callback
    /// @param tabbar TabBar widget
    /// @param callback Reorder callback
    /// @param user_data User data for callback
    void vg_tabbar_set_on_reorder(vg_tabbar_t *tabbar,
                                  vg_tab_reorder_callback_t callback,
                                  void *user_data);

    //=============================================================================
    // SplitPane Widget
    //=============================================================================

    /// @brief Split direction
    typedef enum vg_split_direction
    {
        VG_SPLIT_HORIZONTAL, ///< Left/Right split
        VG_SPLIT_VERTICAL    ///< Top/Bottom split
    } vg_split_direction_t;

    /// @brief SplitPane widget structure
    typedef struct vg_splitpane
    {
        vg_widget_t base;

        vg_split_direction_t direction; ///< Split direction
        float split_position;           ///< Splitter position (0-1 ratio)
        float min_first_size;           ///< Minimum size for first pane
        float min_second_size;          ///< Minimum size for second pane
        float splitter_size;            ///< Splitter bar thickness

        uint32_t splitter_color;       ///< Splitter bar color
        uint32_t splitter_hover_color; ///< Splitter hover color

        // State
        bool splitter_hovered;  ///< Is splitter hovered
        bool dragging;          ///< Is dragging splitter
        float drag_start;       ///< Drag start position
        float drag_start_split; ///< Split position at drag start
    } vg_splitpane_t;

    /// @brief Create a new split pane widget
    /// @param parent Parent widget (can be NULL)
    /// @param direction Split direction
    /// @return New split pane widget or NULL on failure
    vg_splitpane_t *vg_splitpane_create(vg_widget_t *parent, vg_split_direction_t direction);

    /// @brief Set split position
    /// @param split SplitPane widget
    /// @param position Split position (0-1 ratio)
    void vg_splitpane_set_position(vg_splitpane_t *split, float position);

    /// @brief Get split position
    /// @param split SplitPane widget
    /// @return Split position (0-1 ratio)
    float vg_splitpane_get_position(vg_splitpane_t *split);

    /// @brief Set minimum pane sizes
    /// @param split SplitPane widget
    /// @param min_first Minimum size for first pane
    /// @param min_second Minimum size for second pane
    void vg_splitpane_set_min_sizes(vg_splitpane_t *split, float min_first, float min_second);

    /// @brief Get first pane (for adding content)
    /// @param split SplitPane widget
    /// @return First pane widget
    vg_widget_t *vg_splitpane_get_first(vg_splitpane_t *split);

    /// @brief Get second pane (for adding content)
    /// @param split SplitPane widget
    /// @return Second pane widget
    vg_widget_t *vg_splitpane_get_second(vg_splitpane_t *split);

    //=============================================================================
    // MenuBar Widget
    //=============================================================================

    /// @brief Menu item structure (forward declared at top of file)
    /// @brief Parsed keyboard accelerator
    typedef struct vg_accelerator
    {
        int key;            ///< Key code (VG_KEY_*)
        uint32_t modifiers; ///< Modifier flags (VG_MOD_*)
    } vg_accelerator_t;

    struct vg_menu_item
    {
        const char *text;           ///< Item text (owned)
        const char *shortcut;       ///< Keyboard shortcut text (owned)
        vg_accelerator_t accel;     ///< Parsed accelerator
        void (*action)(void *data); ///< Action callback
        void *action_data;          ///< Action data
        bool enabled;               ///< Is item enabled
        bool checked;               ///< Is item checked (for toggles)
        bool separator;             ///< Is this a separator
        bool was_clicked;           ///< Set true when item is clicked (cleared on read)
        struct vg_menu *submenu;    ///< Submenu (if any)
        struct vg_menu_item *next;
        struct vg_menu_item *prev;
    };

    /// @brief Menu structure (forward declared at top of file)
    struct vg_menu
    {
        const char *title; ///< Menu title (owned)
        vg_menu_item_t *first_item;
        vg_menu_item_t *last_item;
        int item_count;
        struct vg_menu *next;
        struct vg_menu *prev;
        bool open; ///< Is menu currently open
    };

    /// @brief MenuBar widget structure
    /// @brief Accelerator table entry
    typedef struct vg_accel_entry
    {
        vg_accelerator_t accel; ///< Accelerator key
        vg_menu_item_t *item;   ///< Menu item to trigger
        struct vg_accel_entry *next;
    } vg_accel_entry_t;

    typedef struct vg_menubar
    {
        vg_widget_t base;

        vg_menu_t *first_menu;       ///< First menu
        vg_menu_t *last_menu;        ///< Last menu
        int menu_count;              ///< Number of menus
        vg_menu_t *open_menu;        ///< Currently open menu
        vg_menu_item_t *highlighted; ///< Currently highlighted item

        vg_font_t *font; ///< Font for rendering
        float font_size; ///< Font size

        // Appearance
        float height;            ///< Menu bar height
        float menu_padding;      ///< Horizontal padding for menu titles
        float item_padding;      ///< Padding for menu items
        uint32_t bg_color;       ///< Background color
        uint32_t text_color;     ///< Text color
        uint32_t highlight_bg;   ///< Highlighted item background
        uint32_t disabled_color; ///< Disabled item text color

        // Keyboard accelerators
        vg_accel_entry_t *accel_table; ///< Accelerator lookup table

        // State
        bool menu_active; ///< Is any menu active
    } vg_menubar_t;

    /// @brief Create a new menu bar widget
    /// @param parent Parent widget (can be NULL)
    /// @return New menu bar widget or NULL on failure
    vg_menubar_t *vg_menubar_create(vg_widget_t *parent);

    /// @brief Add a menu to the menu bar
    /// @param menubar MenuBar widget
    /// @param title Menu title
    /// @return New menu or NULL on failure
    vg_menu_t *vg_menubar_add_menu(vg_menubar_t *menubar, const char *title);

    /// @brief Add an item to a menu
    /// @param menu Menu
    /// @param text Item text
    /// @param shortcut Keyboard shortcut (optional)
    /// @param action Action callback
    /// @param data Action data
    /// @return New item or NULL on failure
    vg_menu_item_t *vg_menu_add_item(vg_menu_t *menu,
                                     const char *text,
                                     const char *shortcut,
                                     void (*action)(void *),
                                     void *data);

    /// @brief Add a separator to a menu
    /// @param menu Menu
    /// @return New separator item or NULL on failure
    vg_menu_item_t *vg_menu_add_separator(vg_menu_t *menu);

    /// @brief Add a submenu
    /// @param menu Parent menu
    /// @param title Submenu title
    /// @return New submenu or NULL on failure
    vg_menu_t *vg_menu_add_submenu(vg_menu_t *menu, const char *title);

    /// @brief Set menu item enabled state
    /// @param item Menu item
    /// @param enabled Enabled state
    void vg_menu_item_set_enabled(vg_menu_item_t *item, bool enabled);

    /// @brief Set menu item checked state
    /// @param item Menu item
    /// @param checked Checked state
    void vg_menu_item_set_checked(vg_menu_item_t *item, bool checked);

    /// @brief Set font for menu bar
    /// @param menubar MenuBar widget
    /// @param font Font to use
    /// @param size Font size in pixels
    void vg_menubar_set_font(vg_menubar_t *menubar, vg_font_t *font, float size);

    // --- Keyboard Accelerators ---

    /// @brief Parse accelerator string (e.g., "Ctrl+S", "Cmd+Shift+N")
    /// @param shortcut Shortcut string
    /// @param accel Output accelerator structure
    /// @return true if parsed successfully
    bool vg_parse_accelerator(const char *shortcut, vg_accelerator_t *accel);

    /// @brief Register a keyboard accelerator
    /// @param menubar MenuBar widget
    /// @param item Menu item to trigger
    /// @param shortcut Shortcut string (e.g., "Ctrl+S")
    void vg_menubar_register_accelerator(vg_menubar_t *menubar,
                                         vg_menu_item_t *item,
                                         const char *shortcut);

    /// @brief Rebuild accelerator table from all menu items
    /// @param menubar MenuBar widget
    void vg_menubar_rebuild_accelerators(vg_menubar_t *menubar);

    /// @brief Handle a key event, triggering accelerator if matched
    /// @param menubar MenuBar widget
    /// @param key Key code
    /// @param modifiers Modifier flags
    /// @return true if an accelerator was triggered
    bool vg_menubar_handle_accelerator(vg_menubar_t *menubar, int key, uint32_t modifiers);

    //=============================================================================
    // CodeEditor Widget
    //=============================================================================

    /// @brief Edit operation types for undo/redo
    typedef enum vg_edit_op_type
    {
        VG_EDIT_INSERT, ///< Text inserted
        VG_EDIT_DELETE, ///< Text deleted
        VG_EDIT_REPLACE ///< Text replaced (delete + insert)
    } vg_edit_op_type_t;

    /// @brief Single edit operation for undo/redo history
    typedef struct vg_edit_op
    {
        vg_edit_op_type_t type; ///< Operation type

        // Position info
        int start_line; ///< Start line
        int start_col;  ///< Start column
        int end_line;   ///< End line
        int end_col;    ///< End column

        // Text data
        char *old_text; ///< Text before operation (for DELETE/REPLACE)
        char *new_text; ///< Text after operation (for INSERT/REPLACE)

        // Cursor position to restore
        int cursor_line_before;
        int cursor_col_before;
        int cursor_line_after;
        int cursor_col_after;

        // Grouping for compound operations
        uint32_t group_id; ///< Non-zero if part of a group
    } vg_edit_op_t;

    /// @brief Undo/redo history
    typedef struct vg_edit_history
    {
        vg_edit_op_t **operations; ///< Array of edit operations
        size_t count;              ///< Number of operations
        size_t capacity;           ///< Allocated capacity
        size_t current_index;      ///< Points to next redo operation
        uint32_t next_group_id;    ///< Counter for grouping
        bool is_grouping;          ///< Currently recording a group
        uint32_t current_group;    ///< Active group ID
    } vg_edit_history_t;

    /// @brief Line information
    typedef struct vg_code_line
    {
        char *text;       ///< Line text (owned)
        size_t length;    ///< Text length
        size_t capacity;  ///< Buffer capacity
        uint32_t *colors; ///< Per-character colors (owned, optional)
        bool modified;    ///< Line modified since last save
    } vg_code_line_t;

    /// @brief Selection range
    typedef struct vg_selection
    {
        int start_line;
        int start_col;
        int end_line;
        int end_col;
    } vg_selection_t;

    /// @brief Syntax highlighter callback
    typedef void (*vg_syntax_callback_t)(
        vg_widget_t *editor, int line_num, const char *text, uint32_t *colors, void *user_data);

    /// @brief CodeEditor widget structure
    typedef struct vg_codeeditor
    {
        vg_widget_t base;

        // Document
        vg_code_line_t *lines; ///< Array of lines
        int line_count;        ///< Number of lines
        int line_capacity;     ///< Allocated capacity

        // Cursor and selection
        int cursor_line;          ///< Cursor line (0-based)
        int cursor_col;           ///< Cursor column (0-based)
        vg_selection_t selection; ///< Current selection
        bool has_selection;       ///< Is there an active selection

        // Scroll
        float scroll_x;         ///< Horizontal scroll
        float scroll_y;         ///< Vertical scroll
        int visible_first_line; ///< First visible line
        int visible_line_count; ///< Number of visible lines

        // Font
        vg_font_t *font;   ///< Monospace font
        float font_size;   ///< Font size
        float char_width;  ///< Character width (monospace)
        float line_height; ///< Line height

        // Gutter
        bool show_line_numbers;     ///< Show line number gutter
        float gutter_width;         ///< Gutter width
        uint32_t gutter_bg;         ///< Gutter background color
        uint32_t line_number_color; ///< Line number color

        // Appearance
        uint32_t bg_color;        ///< Background color
        uint32_t text_color;      ///< Default text color
        uint32_t cursor_color;    ///< Cursor color
        uint32_t selection_color; ///< Selection color
        uint32_t current_line_bg; ///< Current line highlight

        // Syntax highlighting
        vg_syntax_callback_t syntax_highlighter;
        void *syntax_data;

        // Editing options
        bool read_only;   ///< Read-only mode
        bool insert_mode; ///< Insert vs overwrite mode
        int tab_width;    ///< Tab width in spaces
        bool use_spaces;  ///< Use spaces for tabs
        bool auto_indent; ///< Auto-indent on enter
        bool word_wrap;   ///< Word wrapping

        // State
        bool cursor_visible;     ///< Cursor blink state
        float cursor_blink_time; ///< Cursor blink timer
        bool modified;           ///< Document modified since last save

        // Undo/redo history
        vg_edit_history_t *history; ///< Edit history for undo/redo
    } vg_codeeditor_t;

    /// @brief Create a new code editor widget
    /// @param parent Parent widget (can be NULL)
    /// @return New code editor widget or NULL on failure
    vg_codeeditor_t *vg_codeeditor_create(vg_widget_t *parent);

    /// @brief Set editor text content
    /// @param editor CodeEditor widget
    /// @param text New text content
    void vg_codeeditor_set_text(vg_codeeditor_t *editor, const char *text);

    /// @brief Get editor text content
    /// @param editor CodeEditor widget
    /// @return Text content (caller must free)
    char *vg_codeeditor_get_text(vg_codeeditor_t *editor);

    /// @brief Get selected text
    /// @param editor CodeEditor widget
    /// @return Selected text (caller must free) or NULL if no selection
    char *vg_codeeditor_get_selection(vg_codeeditor_t *editor);

    /// @brief Set cursor position
    /// @param editor CodeEditor widget
    /// @param line Line number (0-based)
    /// @param col Column number (0-based)
    void vg_codeeditor_set_cursor(vg_codeeditor_t *editor, int line, int col);

    /// @brief Get cursor position
    /// @param editor CodeEditor widget
    /// @param out_line Output line number
    /// @param out_col Output column number
    void vg_codeeditor_get_cursor(vg_codeeditor_t *editor, int *out_line, int *out_col);

    /// @brief Set selection range
    /// @param editor CodeEditor widget
    /// @param start_line Selection start line
    /// @param start_col Selection start column
    /// @param end_line Selection end line
    /// @param end_col Selection end column
    void vg_codeeditor_set_selection(
        vg_codeeditor_t *editor, int start_line, int start_col, int end_line, int end_col);

    /// @brief Insert text at cursor
    /// @param editor CodeEditor widget
    /// @param text Text to insert
    void vg_codeeditor_insert_text(vg_codeeditor_t *editor, const char *text);

    /// @brief Delete selected text
    /// @param editor CodeEditor widget
    void vg_codeeditor_delete_selection(vg_codeeditor_t *editor);

    /// @brief Scroll to line
    /// @param editor CodeEditor widget
    /// @param line Line number (0-based)
    void vg_codeeditor_scroll_to_line(vg_codeeditor_t *editor, int line);

    /// @brief Set syntax highlighter
    /// @param editor CodeEditor widget
    /// @param callback Syntax highlight callback
    /// @param user_data User data
    void vg_codeeditor_set_syntax(vg_codeeditor_t *editor,
                                  vg_syntax_callback_t callback,
                                  void *user_data);

    /// @brief Undo last action
    /// @param editor CodeEditor widget
    void vg_codeeditor_undo(vg_codeeditor_t *editor);

    /// @brief Redo last undone action
    /// @param editor CodeEditor widget
    void vg_codeeditor_redo(vg_codeeditor_t *editor);

    /// @brief Set font for code editor
    /// @param editor CodeEditor widget
    /// @param font Monospace font to use
    /// @param size Font size in pixels
    void vg_codeeditor_set_font(vg_codeeditor_t *editor, vg_font_t *font, float size);

    /// @brief Get line count
    /// @param editor CodeEditor widget
    /// @return Number of lines
    int vg_codeeditor_get_line_count(vg_codeeditor_t *editor);

    /// @brief Check if document is modified
    /// @param editor CodeEditor widget
    /// @return True if modified
    bool vg_codeeditor_is_modified(vg_codeeditor_t *editor);

    /// @brief Clear modified flag
    /// @param editor CodeEditor widget
    void vg_codeeditor_clear_modified(vg_codeeditor_t *editor);

    //=============================================================================
    // Tooltip Widget
    //=============================================================================

    /// @brief Tooltip position mode
    typedef enum vg_tooltip_position
    {
        VG_TOOLTIP_FOLLOW_CURSOR, ///< Follow mouse cursor
        VG_TOOLTIP_ANCHOR_WIDGET  ///< Anchor to specific widget
    } vg_tooltip_position_t;

    /// @brief Tooltip widget structure
    typedef struct vg_tooltip
    {
        vg_widget_t base;

        // Content
        char *text;           ///< Plain text content
        vg_widget_t *content; ///< Rich content (alternative)

        // Timing
        uint32_t show_delay_ms; ///< Delay before showing (default: 500)
        uint32_t hide_delay_ms; ///< Delay before hiding on leave (default: 100)
        uint32_t duration_ms;   ///< Auto-hide after (0 = stay until leave)

        // Positioning
        vg_tooltip_position_t position_mode;
        int offset_x;               ///< X offset from anchor
        int offset_y;               ///< Y offset from anchor
        vg_widget_t *anchor_widget; ///< Widget to anchor to

        // Styling
        uint32_t max_width;     ///< Max width before wrapping (default: 300)
        uint32_t padding;       ///< Internal padding
        uint32_t corner_radius; ///< Corner radius
        uint32_t bg_color;      ///< Background color
        uint32_t text_color;    ///< Text color
        uint32_t border_color;  ///< Border color

        // Font
        vg_font_t *font;
        float font_size;

        // State
        bool is_visible;     ///< Currently visible
        uint64_t show_timer; ///< Timer for show delay
        uint64_t hide_timer; ///< Timer for hide delay
    } vg_tooltip_t;

    /// @brief Tooltip manager (singleton pattern)
    typedef struct vg_tooltip_manager
    {
        vg_tooltip_t *active_tooltip; ///< Currently showing tooltip
        vg_widget_t *hovered_widget;  ///< Widget mouse is over
        uint64_t hover_start_time;    ///< When hover started
        bool pending_show;            ///< Tooltip pending display
        int cursor_x;                 ///< Cursor position
        int cursor_y;
    } vg_tooltip_manager_t;

    /// @brief Create a new tooltip widget
    /// @return New tooltip or NULL on failure
    vg_tooltip_t *vg_tooltip_create(void);

    /// @brief Destroy a tooltip widget
    /// @param tooltip Tooltip widget
    void vg_tooltip_destroy(vg_tooltip_t *tooltip);

    /// @brief Set tooltip text
    /// @param tooltip Tooltip widget
    /// @param text Tooltip text
    void vg_tooltip_set_text(vg_tooltip_t *tooltip, const char *text);

    /// @brief Show tooltip at position
    /// @param tooltip Tooltip widget
    /// @param x X position
    /// @param y Y position
    void vg_tooltip_show_at(vg_tooltip_t *tooltip, int x, int y);

    /// @brief Hide tooltip
    /// @param tooltip Tooltip widget
    void vg_tooltip_hide(vg_tooltip_t *tooltip);

    /// @brief Set tooltip anchor widget
    /// @param tooltip Tooltip widget
    /// @param anchor Widget to anchor to
    void vg_tooltip_set_anchor(vg_tooltip_t *tooltip, vg_widget_t *anchor);

    /// @brief Set tooltip timing
    /// @param tooltip Tooltip widget
    /// @param show_delay_ms Show delay in ms
    /// @param hide_delay_ms Hide delay in ms
    /// @param duration_ms Auto-hide duration (0 = never)
    void vg_tooltip_set_timing(vg_tooltip_t *tooltip,
                               uint32_t show_delay_ms,
                               uint32_t hide_delay_ms,
                               uint32_t duration_ms);

    // --- Tooltip Manager ---

    /// @brief Get global tooltip manager
    /// @return Tooltip manager singleton
    vg_tooltip_manager_t *vg_tooltip_manager_get(void);

    /// @brief Update tooltip manager (call each frame)
    /// @param mgr Tooltip manager
    /// @param now_ms Current time in ms
    void vg_tooltip_manager_update(vg_tooltip_manager_t *mgr, uint64_t now_ms);

    /// @brief Notify manager of hover
    /// @param mgr Tooltip manager
    /// @param widget Widget being hovered
    /// @param x Mouse X
    /// @param y Mouse Y
    void vg_tooltip_manager_on_hover(vg_tooltip_manager_t *mgr, vg_widget_t *widget, int x, int y);

    /// @brief Notify manager of leave
    /// @param mgr Tooltip manager
    void vg_tooltip_manager_on_leave(vg_tooltip_manager_t *mgr);

    /// @brief Set tooltip for a widget
    /// @param widget Widget
    /// @param text Tooltip text
    void vg_widget_set_tooltip_text(vg_widget_t *widget, const char *text);

    //=============================================================================
    // CommandPalette Widget
    //=============================================================================

    /// @brief Command structure
    typedef struct vg_command
    {
        char *id;                                                ///< Unique ID
        char *label;                                             ///< Display text
        char *description;                                       ///< Optional description
        char *shortcut;                                          ///< Keyboard shortcut display
        char *category;                                          ///< Category for grouping
        vg_icon_t icon;                                          ///< Command icon
        bool enabled;                                            ///< Is command enabled
        void *user_data;                                         ///< User data
        void (*action)(struct vg_command *cmd, void *user_data); ///< Action callback
    } vg_command_t;

    /// @brief CommandPalette widget structure
    typedef struct vg_commandpalette
    {
        vg_widget_t base;

        // Commands
        vg_command_t **commands; ///< All registered commands
        size_t command_count;
        size_t command_capacity;

        // Filtered results
        vg_command_t **filtered; ///< Filtered command list
        size_t filtered_count;
        size_t filtered_capacity;

        // Search input (void* to avoid circular deps)
        void *search_input;  ///< Text input for search
        char *current_query; ///< Current search query

        // State
        bool is_visible;    ///< Is palette visible
        int selected_index; ///< Selected result index
        int hovered_index;  ///< Hovered result index

        // Appearance
        uint32_t item_height; ///< Height of each result item
        uint32_t max_visible; ///< Max visible results
        float width;          ///< Palette width
        uint32_t bg_color;
        uint32_t selected_bg;
        uint32_t text_color;
        uint32_t shortcut_color;

        // Font
        vg_font_t *font;
        float font_size;

        // Callbacks
        void (*on_execute)(struct vg_commandpalette *palette, vg_command_t *cmd, void *user_data);
        void (*on_dismiss)(struct vg_commandpalette *palette, void *user_data);
        void *user_data;
    } vg_commandpalette_t;

    /// @brief Create a new command palette
    /// @return New command palette or NULL on failure
    vg_commandpalette_t *vg_commandpalette_create(void);

    /// @brief Destroy a command palette
    /// @param palette Command palette
    void vg_commandpalette_destroy(vg_commandpalette_t *palette);

    /// @brief Add a command
    /// @param palette Command palette
    /// @param id Command ID
    /// @param label Display label
    /// @param shortcut Shortcut text (or NULL)
    /// @param action Action callback
    /// @param user_data User data
    /// @return New command or NULL on failure
    vg_command_t *vg_commandpalette_add_command(vg_commandpalette_t *palette,
                                                const char *id,
                                                const char *label,
                                                const char *shortcut,
                                                void (*action)(vg_command_t *, void *),
                                                void *user_data);

    /// @brief Remove a command by ID
    /// @param palette Command palette
    /// @param id Command ID
    void vg_commandpalette_remove_command(vg_commandpalette_t *palette, const char *id);

    /// @brief Get command by ID
    /// @param palette Command palette
    /// @param id Command ID
    /// @return Command or NULL
    vg_command_t *vg_commandpalette_get_command(vg_commandpalette_t *palette, const char *id);

    /// @brief Show command palette
    /// @param palette Command palette
    void vg_commandpalette_show(vg_commandpalette_t *palette);

    /// @brief Hide command palette
    /// @param palette Command palette
    void vg_commandpalette_hide(vg_commandpalette_t *palette);

    /// @brief Toggle command palette visibility
    /// @param palette Command palette
    void vg_commandpalette_toggle(vg_commandpalette_t *palette);

    /// @brief Execute selected command
    /// @param palette Command palette
    void vg_commandpalette_execute_selected(vg_commandpalette_t *palette);

    /// @brief Set callbacks
    /// @param palette Command palette
    /// @param on_execute Execute callback
    /// @param on_dismiss Dismiss callback
    /// @param user_data User data
    void vg_commandpalette_set_callbacks(vg_commandpalette_t *palette,
                                         void (*on_execute)(vg_commandpalette_t *,
                                                            vg_command_t *,
                                                            void *),
                                         void (*on_dismiss)(vg_commandpalette_t *, void *),
                                         void *user_data);

    /// @brief Set font
    /// @param palette Command palette
    /// @param font Font
    /// @param size Font size
    void vg_commandpalette_set_font(vg_commandpalette_t *palette, vg_font_t *font, float size);

    //=============================================================================
    // OutputPane Widget (Terminal-like output)
    //=============================================================================

    /// @brief ANSI color codes
    typedef enum vg_ansi_color
    {
        VG_ANSI_DEFAULT = 0,
        VG_ANSI_BLACK = 30,
        VG_ANSI_RED,
        VG_ANSI_GREEN,
        VG_ANSI_YELLOW,
        VG_ANSI_BLUE,
        VG_ANSI_MAGENTA,
        VG_ANSI_CYAN,
        VG_ANSI_WHITE,
        VG_ANSI_BRIGHT_BLACK = 90,
        VG_ANSI_BRIGHT_RED,
        VG_ANSI_BRIGHT_GREEN,
        VG_ANSI_BRIGHT_YELLOW,
        VG_ANSI_BRIGHT_BLUE,
        VG_ANSI_BRIGHT_MAGENTA,
        VG_ANSI_BRIGHT_CYAN,
        VG_ANSI_BRIGHT_WHITE
    } vg_ansi_color_t;

    /// @brief Styled text segment
    typedef struct vg_styled_segment
    {
        char *text;        ///< Segment text
        uint32_t fg_color; ///< Foreground color
        uint32_t bg_color; ///< Background color
        bool bold;         ///< Bold text
        bool italic;       ///< Italic text
        bool underline;    ///< Underlined text
    } vg_styled_segment_t;

    /// @brief Output line
    typedef struct vg_output_line
    {
        vg_styled_segment_t *segments; ///< Styled segments
        size_t segment_count;          ///< Number of segments
        size_t segment_capacity;       ///< Capacity
        uint64_t timestamp;            ///< When line was added
    } vg_output_line_t;

    /// @brief OutputPane widget structure
    typedef struct vg_outputpane
    {
        vg_widget_t base;

        // Lines
        vg_output_line_t *lines; ///< Array of output lines
        size_t line_count;
        size_t line_capacity;
        size_t max_lines; ///< Ring buffer limit (default: 10000)

        // Scrolling
        float scroll_y;     ///< Vertical scroll position
        bool auto_scroll;   ///< Scroll to bottom on new output
        bool scroll_locked; ///< User scrolled up

        // Selection
        bool has_selection;
        uint32_t sel_start_line, sel_start_col;
        uint32_t sel_end_line, sel_end_col;

        // Styling
        float line_height; ///< Height per line
        vg_font_t *font;   ///< Monospace font
        float font_size;
        uint32_t bg_color;
        uint32_t default_fg;

        // ANSI parser state
        uint32_t current_fg;
        uint32_t current_bg;
        bool ansi_bold;
        bool in_escape;
        char escape_buf[32];
        int escape_len;

        // Callbacks
        void (*on_line_click)(struct vg_outputpane *pane, int line, int col, void *user_data);
        void *user_data;
    } vg_outputpane_t;

    /// @brief Create a new output pane
    /// @return New output pane or NULL on failure
    vg_outputpane_t *vg_outputpane_create(void);

    /// @brief Destroy an output pane
    /// @param pane Output pane
    void vg_outputpane_destroy(vg_outputpane_t *pane);

    /// @brief Append text (handles ANSI codes)
    /// @param pane Output pane
    /// @param text Text to append
    void vg_outputpane_append(vg_outputpane_t *pane, const char *text);

    /// @brief Append a complete line
    /// @param pane Output pane
    /// @param text Line text
    void vg_outputpane_append_line(vg_outputpane_t *pane, const char *text);

    /// @brief Append styled text
    /// @param pane Output pane
    /// @param text Text
    /// @param fg Foreground color
    /// @param bg Background color
    /// @param bold Bold flag
    void vg_outputpane_append_styled(
        vg_outputpane_t *pane, const char *text, uint32_t fg, uint32_t bg, bool bold);

    /// @brief Clear all output
    /// @param pane Output pane
    void vg_outputpane_clear(vg_outputpane_t *pane);

    /// @brief Scroll to bottom
    /// @param pane Output pane
    void vg_outputpane_scroll_to_bottom(vg_outputpane_t *pane);

    /// @brief Scroll to top
    /// @param pane Output pane
    void vg_outputpane_scroll_to_top(vg_outputpane_t *pane);

    /// @brief Set auto-scroll behavior
    /// @param pane Output pane
    /// @param auto_scroll Enable auto-scroll
    void vg_outputpane_set_auto_scroll(vg_outputpane_t *pane, bool auto_scroll);

    /// @brief Get selected text
    /// @param pane Output pane
    /// @return Allocated string or NULL (caller must free)
    char *vg_outputpane_get_selection(vg_outputpane_t *pane);

    /// @brief Select all text
    /// @param pane Output pane
    void vg_outputpane_select_all(vg_outputpane_t *pane);

    /// @brief Set maximum lines
    /// @param pane Output pane
    /// @param max Maximum lines
    void vg_outputpane_set_max_lines(vg_outputpane_t *pane, size_t max);

    /// @brief Set font
    /// @param pane Output pane
    /// @param font Font
    /// @param size Font size
    void vg_outputpane_set_font(vg_outputpane_t *pane, vg_font_t *font, float size);

    //=============================================================================
    // Breadcrumb Widget
    //=============================================================================

    /// @brief Breadcrumb dropdown item
    typedef struct vg_breadcrumb_dropdown
    {
        char *label; ///< Dropdown item label
        void *data;  ///< User data
    } vg_breadcrumb_dropdown_t;

    /// @brief Breadcrumb item
    typedef struct vg_breadcrumb_item
    {
        char *label;     ///< Item label
        char *tooltip;   ///< Tooltip text
        vg_icon_t icon;  ///< Optional icon
        void *user_data; ///< User data

        // Dropdown items
        vg_breadcrumb_dropdown_t *dropdown_items;
        size_t dropdown_count;
        size_t dropdown_capacity;
    } vg_breadcrumb_item_t;

    /// @brief Breadcrumb widget structure
    typedef struct vg_breadcrumb
    {
        vg_widget_t base;

        // Items
        vg_breadcrumb_item_t *items; ///< Breadcrumb items
        size_t item_count;
        size_t item_capacity;

        // Styling
        char *separator;            ///< Separator string (default: ">")
        uint32_t item_padding;      ///< Padding around items
        uint32_t separator_padding; ///< Padding around separator
        uint32_t bg_color;
        uint32_t text_color;
        uint32_t hover_bg;
        uint32_t separator_color;

        // Font
        vg_font_t *font;
        float font_size;

        // State
        int hovered_index;    ///< Hovered item index
        bool dropdown_open;   ///< Is dropdown open
        int dropdown_index;   ///< Which item's dropdown is open
        int dropdown_hovered; ///< Hovered dropdown item

        // Callbacks
        void (*on_click)(struct vg_breadcrumb *bc, int index, void *user_data);
        void (*on_dropdown_select)(struct vg_breadcrumb *bc,
                                   int crumb_index,
                                   int dropdown_index,
                                   void *user_data);
        void *user_data;
    } vg_breadcrumb_t;

    /// @brief Create a new breadcrumb widget
    /// @return New breadcrumb or NULL on failure
    vg_breadcrumb_t *vg_breadcrumb_create(void);

    /// @brief Destroy a breadcrumb widget
    /// @param bc Breadcrumb widget
    void vg_breadcrumb_destroy(vg_breadcrumb_t *bc);

    /// @brief Push a new item onto the breadcrumb
    /// @param bc Breadcrumb widget
    /// @param label Item label
    /// @param data User data
    void vg_breadcrumb_push(vg_breadcrumb_t *bc, const char *label, void *data);

    /// @brief Pop the last item from the breadcrumb
    /// @param bc Breadcrumb widget
    void vg_breadcrumb_pop(vg_breadcrumb_t *bc);

    /// @brief Clear all items
    /// @param bc Breadcrumb widget
    void vg_breadcrumb_clear(vg_breadcrumb_t *bc);

    /// @brief Add dropdown item to a breadcrumb item
    /// @param item Breadcrumb item
    /// @param label Dropdown item label
    /// @param data User data
    void vg_breadcrumb_item_add_dropdown(vg_breadcrumb_item_t *item, const char *label, void *data);

    /// @brief Set separator string
    /// @param bc Breadcrumb widget
    /// @param sep Separator string
    void vg_breadcrumb_set_separator(vg_breadcrumb_t *bc, const char *sep);

    /// @brief Set click callback
    /// @param bc Breadcrumb widget
    /// @param callback Click callback
    /// @param user_data User data
    void vg_breadcrumb_set_on_click(vg_breadcrumb_t *bc,
                                    void (*callback)(vg_breadcrumb_t *, int, void *),
                                    void *user_data);

    /// @brief Set font
    /// @param bc Breadcrumb widget
    /// @param font Font
    /// @param size Font size
    void vg_breadcrumb_set_font(vg_breadcrumb_t *bc, vg_font_t *font, float size);

    //=============================================================================
    // Minimap Widget
    //=============================================================================

    /// @brief Minimap widget structure
    typedef struct vg_minimap
    {
        vg_widget_t base;

        // Source editor
        vg_codeeditor_t *editor; ///< Editor to display

        // Rendering
        uint32_t char_width;  ///< Width per character (1-2 pixels)
        uint32_t line_height; ///< Height per line (1-2 pixels)
        bool show_viewport;   ///< Show visible region indicator
        float scale;          ///< Scale factor (default: 0.1)

        // Cached render
        uint8_t *render_buffer; ///< RGBA pixels
        uint32_t buffer_width;
        uint32_t buffer_height;
        bool buffer_dirty; ///< Needs re-render

        // Viewport indicator
        uint32_t viewport_start_line;
        uint32_t viewport_end_line;
        uint32_t viewport_color;

        // Styling
        uint32_t bg_color;
        uint32_t text_color;

        // Interaction
        bool dragging;    ///< Dragging viewport
        int drag_start_y; ///< Drag start Y position
    } vg_minimap_t;

    /// @brief Create a new minimap widget
    /// @param editor Editor to display
    /// @return New minimap or NULL on failure
    vg_minimap_t *vg_minimap_create(vg_codeeditor_t *editor);

    /// @brief Destroy a minimap widget
    /// @param minimap Minimap widget
    void vg_minimap_destroy(vg_minimap_t *minimap);

    /// @brief Set editor for minimap
    /// @param minimap Minimap widget
    /// @param editor Code editor
    void vg_minimap_set_editor(vg_minimap_t *minimap, vg_codeeditor_t *editor);

    /// @brief Set scale factor
    /// @param minimap Minimap widget
    /// @param scale Scale (0.05 - 0.2 typical)
    void vg_minimap_set_scale(vg_minimap_t *minimap, float scale);

    /// @brief Set viewport indicator visibility
    /// @param minimap Minimap widget
    /// @param show Show viewport
    void vg_minimap_set_show_viewport(vg_minimap_t *minimap, bool show);

    /// @brief Invalidate entire minimap (needs re-render)
    /// @param minimap Minimap widget
    void vg_minimap_invalidate(vg_minimap_t *minimap);

    /// @brief Invalidate specific lines
    /// @param minimap Minimap widget
    /// @param start_line Start line
    /// @param end_line End line
    void vg_minimap_invalidate_lines(vg_minimap_t *minimap, uint32_t start_line, uint32_t end_line);

    //=============================================================================
    // Notification Widget
    //=============================================================================

    /// @brief Notification type
    typedef enum vg_notification_type
    {
        VG_NOTIFICATION_INFO,    ///< Informational
        VG_NOTIFICATION_SUCCESS, ///< Success message
        VG_NOTIFICATION_WARNING, ///< Warning message
        VG_NOTIFICATION_ERROR    ///< Error message
    } vg_notification_type_t;

    /// @brief Notification position
    typedef enum vg_notification_position
    {
        VG_NOTIFICATION_TOP_RIGHT,
        VG_NOTIFICATION_TOP_LEFT,
        VG_NOTIFICATION_BOTTOM_RIGHT,
        VG_NOTIFICATION_BOTTOM_LEFT,
        VG_NOTIFICATION_TOP_CENTER,
        VG_NOTIFICATION_BOTTOM_CENTER
    } vg_notification_position_t;

    /// @brief Single notification
    typedef struct vg_notification
    {
        uint32_t id;                 ///< Unique ID
        vg_notification_type_t type; ///< Notification type
        char *title;                 ///< Title text
        char *message;               ///< Message text
        uint32_t duration_ms;        ///< Auto-dismiss duration (0 = sticky)
        uint64_t created_at;         ///< Creation timestamp

        // Action
        char *action_label; ///< Action button label
        void (*action_callback)(uint32_t id, void *user_data);
        void *action_user_data;

        // State
        float opacity;  ///< Current opacity (for animation)
        bool dismissed; ///< Has been dismissed
    } vg_notification_t;

    /// @brief Notification manager widget
    typedef struct vg_notification_manager
    {
        vg_widget_t base;

        // Notifications
        vg_notification_t **notifications;
        size_t notification_count;
        size_t notification_capacity;

        // Positioning
        vg_notification_position_t position;

        // Styling
        uint32_t max_visible; ///< Max visible notifications
        uint32_t notification_width;
        uint32_t spacing; ///< Space between notifications
        uint32_t margin;  ///< Margin from edges
        uint32_t padding; ///< Internal padding

        // Font
        vg_font_t *font;
        float font_size;
        float title_font_size;

        // Colors per type
        uint32_t info_color;
        uint32_t success_color;
        uint32_t warning_color;
        uint32_t error_color;
        uint32_t bg_color;
        uint32_t text_color;

        // Animation
        uint32_t fade_duration_ms;
        uint32_t slide_duration_ms;

        // ID counter
        uint32_t next_id;
    } vg_notification_manager_t;

    /// @brief Create notification manager
    /// @return New notification manager or NULL on failure
    vg_notification_manager_t *vg_notification_manager_create(void);

    /// @brief Destroy notification manager
    /// @param mgr Notification manager
    void vg_notification_manager_destroy(vg_notification_manager_t *mgr);

    /// @brief Update animations (call each frame)
    /// @param mgr Notification manager
    /// @param now_ms Current time in ms
    void vg_notification_manager_update(vg_notification_manager_t *mgr, uint64_t now_ms);

    /// @brief Show a notification
    /// @param mgr Notification manager
    /// @param type Notification type
    /// @param title Title
    /// @param message Message
    /// @param duration_ms Duration (0 = sticky)
    /// @return Notification ID
    uint32_t vg_notification_show(vg_notification_manager_t *mgr,
                                  vg_notification_type_t type,
                                  const char *title,
                                  const char *message,
                                  uint32_t duration_ms);

    /// @brief Show notification with action button
    /// @param mgr Notification manager
    /// @param type Notification type
    /// @param title Title
    /// @param message Message
    /// @param duration_ms Duration
    /// @param action_label Action button label
    /// @param action_callback Action callback
    /// @param user_data User data
    /// @return Notification ID
    uint32_t vg_notification_show_with_action(vg_notification_manager_t *mgr,
                                              vg_notification_type_t type,
                                              const char *title,
                                              const char *message,
                                              uint32_t duration_ms,
                                              const char *action_label,
                                              void (*action_callback)(uint32_t, void *),
                                              void *user_data);

    /// @brief Dismiss a notification
    /// @param mgr Notification manager
    /// @param id Notification ID
    void vg_notification_dismiss(vg_notification_manager_t *mgr, uint32_t id);

    /// @brief Dismiss all notifications
    /// @param mgr Notification manager
    void vg_notification_dismiss_all(vg_notification_manager_t *mgr);

    /// @brief Set notification position
    /// @param mgr Notification manager
    /// @param position Position
    void vg_notification_manager_set_position(vg_notification_manager_t *mgr,
                                              vg_notification_position_t position);

    /// @brief Set font
    /// @param mgr Notification manager
    /// @param font Font
    /// @param size Font size
    void vg_notification_manager_set_font(vg_notification_manager_t *mgr,
                                          vg_font_t *font,
                                          float size);

#ifdef __cplusplus
}
#endif

#endif // VG_IDE_WIDGETS_H
