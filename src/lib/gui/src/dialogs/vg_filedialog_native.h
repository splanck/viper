//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
///
/// @file vg_filedialog_native.h
/// @brief Platform-native file dialog declarations for open, save, and folder
///        selection operations.
///
/// @details This internal header exposes thin wrappers around the operating
///          system's native file-dialog APIs (e.g. Windows GetOpenFileName /
///          IFileDialog, macOS NSOpenPanel, GTK file chooser). Each function
///          blocks until the user makes a selection or cancels, and returns a
///          heap-allocated path string (or NULL on cancel).
///
///          These functions are called by the cross-platform vg_filedialog
///          convenience API (see vg_ide_widgets.h) but can also be invoked
///          directly when the full widget-based file dialog is not needed.
///
/// Key invariants:
///   - All returned strings are heap-allocated and must be freed by the caller.
///   - A NULL return indicates the user cancelled the dialog.
///   - Filter patterns use semicolon-delimited globs (e.g. "*.c;*.h").
///
/// Ownership/Lifetime:
///   - The returned char* is owned by the caller and must be freed with free().
///
/// Links:
///   - vg_ide_widgets.h -- vg_filedialog_open_file / save_file / select_folder
///
//===----------------------------------------------------------------------===//
#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Show a native "Open File" dialog and return the selected file path.
    ///
    /// @details Opens the operating system's native file-open dialog. The dialog
    ///          blocks until the user selects a file or cancels. The returned
    ///          string is dynamically allocated and must be freed by the caller.
    ///
    /// @param title          Dialog window title (e.g. "Open File").
    /// @param initial_path   Initial directory to display (may be NULL for default).
    /// @param filter_name    Human-readable filter label (e.g. "C Source Files").
    /// @param filter_pattern Semicolon-separated glob patterns (e.g. "*.c;*.h").
    /// @return Heap-allocated full file path, or NULL if the user cancelled.
    char *vg_native_open_file(const char *title,
                              const char *initial_path,
                              const char *filter_name,
                              const char *filter_pattern);

    /// @brief Show a native "Save File" dialog and return the chosen save path.
    ///
    /// @details Opens the operating system's native file-save dialog with an
    ///          optional pre-filled filename. The dialog blocks until the user
    ///          confirms a path or cancels. The returned string is dynamically
    ///          allocated and must be freed by the caller.
    ///
    /// @param title          Dialog window title (e.g. "Save As").
    /// @param initial_path   Initial directory to display (may be NULL for default).
    /// @param default_name   Pre-filled filename suggestion (may be NULL).
    /// @param filter_name    Human-readable filter label.
    /// @param filter_pattern Semicolon-separated glob patterns.
    /// @return Heap-allocated full file path, or NULL if the user cancelled.
    char *vg_native_save_file(const char *title,
                              const char *initial_path,
                              const char *default_name,
                              const char *filter_name,
                              const char *filter_pattern);

    /// @brief Show a native "Select Folder" dialog and return the chosen directory.
    ///
    /// @details Opens the operating system's native folder-selection dialog. The
    ///          dialog blocks until the user selects a folder or cancels. The
    ///          returned string is dynamically allocated and must be freed by
    ///          the caller.
    ///
    /// @param title        Dialog window title (e.g. "Select Folder").
    /// @param initial_path Initial directory to display (may be NULL for default).
    /// @return Heap-allocated full directory path, or NULL if the user cancelled.
    char *vg_native_select_folder(const char *title, const char *initial_path);

#ifdef __cplusplus
}
#endif
