//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/include/vg_ide_widgets_dialog.h
// Purpose: Dialog and FileDialog widget declarations.
// Key invariants:
//   - All create functions return NULL on allocation failure.
//   - String parameters are copied internally unless documented otherwise.
// Ownership/Lifetime:
//   - Dialogs may be created without a parent and must be explicitly destroyed.
// Links: vg_ide_widgets_common.h, vg_widget.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "vg_ide_widgets_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

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
    vg_dialog_t *vg_dialog_create(const char *title);

    /// @brief Destroy a dialog widget
    void vg_dialog_destroy(vg_dialog_t *dialog);

    /// @brief Set dialog title
    void vg_dialog_set_title(vg_dialog_t *dialog, const char *title);

    /// @brief Set dialog content widget
    void vg_dialog_set_content(vg_dialog_t *dialog, vg_widget_t *content);

    /// @brief Set dialog message (simple text content)
    void vg_dialog_set_message(vg_dialog_t *dialog, const char *message);

    /// @brief Set dialog icon preset
    void vg_dialog_set_icon(vg_dialog_t *dialog, vg_dialog_icon_t icon);

    /// @brief Set dialog custom icon
    void vg_dialog_set_custom_icon(vg_dialog_t *dialog, vg_icon_t icon);

    /// @brief Set dialog button preset
    void vg_dialog_set_buttons(vg_dialog_t *dialog, vg_dialog_buttons_t buttons);

    /// @brief Set custom buttons
    void vg_dialog_set_custom_buttons(vg_dialog_t *dialog,
                                      vg_dialog_button_def_t *buttons,
                                      size_t count);

    /// @brief Set dialog resizable
    void vg_dialog_set_resizable(vg_dialog_t *dialog, bool resizable);

    /// @brief Set dialog size constraints
    void vg_dialog_set_size_constraints(
        vg_dialog_t *dialog, uint32_t min_w, uint32_t min_h, uint32_t max_w, uint32_t max_h);

    /// @brief Set dialog modal state
    void vg_dialog_set_modal(vg_dialog_t *dialog, bool modal, vg_widget_t *parent);

    /// @brief Show the dialog
    void vg_dialog_show(vg_dialog_t *dialog);

    /// @brief Show the dialog centered relative to another widget
    void vg_dialog_show_centered(vg_dialog_t *dialog, vg_widget_t *relative_to);

    /// @brief Hide the dialog
    void vg_dialog_hide(vg_dialog_t *dialog);

    /// @brief Close the dialog with a result
    void vg_dialog_close(vg_dialog_t *dialog, vg_dialog_result_t result);

    /// @brief Get dialog result
    vg_dialog_result_t vg_dialog_get_result(vg_dialog_t *dialog);

    /// @brief Check if dialog is open
    bool vg_dialog_is_open(vg_dialog_t *dialog);

    /// @brief Set result callback
    void vg_dialog_set_on_result(vg_dialog_t *dialog,
                                 void (*callback)(vg_dialog_t *, vg_dialog_result_t, void *),
                                 void *user_data);

    /// @brief Set close callback
    void vg_dialog_set_on_close(vg_dialog_t *dialog,
                                void (*callback)(vg_dialog_t *, void *),
                                void *user_data);

    /// @brief Set font for dialog
    void vg_dialog_set_font(vg_dialog_t *dialog, vg_font_t *font, float size);

    /// @brief Create a simple message dialog
    vg_dialog_t *vg_dialog_message(const char *title,
                                   const char *message,
                                   vg_dialog_icon_t icon,
                                   vg_dialog_buttons_t buttons);

    /// @brief Create a confirmation dialog
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
    vg_filedialog_t *vg_filedialog_create(vg_filedialog_mode_t mode);

    /// @brief Destroy a file dialog widget
    void vg_filedialog_destroy(vg_filedialog_t *dialog);

    /// @brief Set dialog title
    void vg_filedialog_set_title(vg_filedialog_t *dialog, const char *title);

    /// @brief Set initial directory path
    void vg_filedialog_set_initial_path(vg_filedialog_t *dialog, const char *path);

    /// @brief Set default filename (for save mode)
    void vg_filedialog_set_filename(vg_filedialog_t *dialog, const char *filename);

    /// @brief Set multi-select mode (for open mode)
    void vg_filedialog_set_multi_select(vg_filedialog_t *dialog, bool multi);

    /// @brief Set show hidden files
    void vg_filedialog_set_show_hidden(vg_filedialog_t *dialog, bool show);

    /// @brief Set confirm overwrite (for save mode)
    void vg_filedialog_set_confirm_overwrite(vg_filedialog_t *dialog, bool confirm);

    /// @brief Add a file filter
    void vg_filedialog_add_filter(vg_filedialog_t *dialog, const char *name, const char *pattern);

    /// @brief Clear all file filters
    void vg_filedialog_clear_filters(vg_filedialog_t *dialog);

    /// @brief Set default extension (auto-added to filename in save mode)
    void vg_filedialog_set_default_extension(vg_filedialog_t *dialog, const char *ext);

    /// @brief Add a bookmark
    void vg_filedialog_add_bookmark(vg_filedialog_t *dialog, const char *name, const char *path);

    /// @brief Add default bookmarks (Home, Desktop, Documents)
    void vg_filedialog_add_default_bookmarks(vg_filedialog_t *dialog);

    /// @brief Clear all bookmarks
    void vg_filedialog_clear_bookmarks(vg_filedialog_t *dialog);

    /// @brief Show the file dialog
    void vg_filedialog_show(vg_filedialog_t *dialog);

    /// @brief Get selected file paths (after dialog closes)
    char **vg_filedialog_get_selected_paths(vg_filedialog_t *dialog, size_t *count);

    /// @brief Get single selected file path (convenience)
    char *vg_filedialog_get_selected_path(vg_filedialog_t *dialog);

    /// @brief Set selection callback
    void vg_filedialog_set_on_select(vg_filedialog_t *dialog,
                                     void (*callback)(vg_filedialog_t *, char **, size_t, void *),
                                     void *user_data);

    /// @brief Set cancel callback
    void vg_filedialog_set_on_cancel(vg_filedialog_t *dialog,
                                     void (*callback)(vg_filedialog_t *, void *),
                                     void *user_data);

    /// @brief Convenience: Open a single file
    char *vg_filedialog_open_file(const char *title,
                                  const char *initial_path,
                                  const char *filter_name,
                                  const char *filter_pattern);

    /// @brief Convenience: Save a file
    char *vg_filedialog_save_file(const char *title,
                                  const char *initial_path,
                                  const char *default_name,
                                  const char *filter_name,
                                  const char *filter_pattern);

    /// @brief Convenience: Select a folder
    char *vg_filedialog_select_folder(const char *title, const char *initial_path);

#ifdef __cplusplus
}
#endif
