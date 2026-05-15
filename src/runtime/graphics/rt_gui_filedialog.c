//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/rt_gui_filedialog.c
// Purpose: File dialog runtime bindings for ViperGUI. Provides both simple
//   convenience dialogs (open, open_multiple, save, select_folder) and an
//   object-oriented builder API (new, set_title, add_filter, show, get_path).
//
// Key invariants:
//   - Simple dialogs block until the user selects or cancels, returning a path
//     string (or empty on cancel).
//   - The builder API uses rt_filedialog_data_t (GC-managed) to track the
//     underlying vg_filedialog_t and selected paths.
//   - On macOS, native file dialogs are used via vg_native_open_file/save_file.
//
// Ownership/Lifetime:
//   - rt_filedialog_data_t is a GC heap object; the vg_filedialog_t is
//     manually freed on rt_filedialog_destroy().
//
// Links: src/runtime/graphics/rt_gui_internal.h (internal types/globals),
//        src/lib/gui/src/widgets/vg_dialog.c (underlying widget)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#ifdef VIPER_ENABLE_GRAPHICS

//=============================================================================
// Phase 5: FileDialog
//=============================================================================

/// @brief Return the active GUI app for file-dialog hosting.
/// @details Prefers the app returned by `rt_gui_get_active_app`; falls back to
///          the module-level `s_current_app` pointer so dialogs work even when
///          the "active" app is temporarily null during event dispatch.
static rt_gui_app_t *rt_filedialog_app(void) {
    rt_gui_app_t *app = rt_gui_get_active_app();
    return app ? app : s_current_app;
}

/// @brief Portable `strdup` wrapper — uses `_strdup` on Windows, `strdup` elsewhere.
static char *rt_filedialog_strdup(const char *text) {
    if (!text)
        return NULL;
#ifdef _WIN32
    return _strdup(text);
#else
    return strdup(text);
#endif
}

/// @brief Join selected paths as a semicolon-delimited string with '\\' escaping.
/// @details Escapes literal ';' and '\\' so callers can unambiguously split the result.
static char *rt_filedialog_join_paths_escaped(char **paths, size_t count, size_t *out_len) {
    if (out_len)
        *out_len = 0;
    if (!paths || count == 0)
        return NULL;

    size_t needed = 1; // NUL
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            if (needed == SIZE_MAX)
                return NULL;
            needed++;
        }
        const char *path = paths[i] ? paths[i] : "";
        for (const char *p = path; *p; p++) {
            size_t add = (*p == ';' || *p == '\\') ? 2 : 1;
            if (needed > SIZE_MAX - add)
                return NULL;
            needed += add;
        }
    }

    char *joined = (char *)malloc(needed);
    if (!joined)
        return NULL;

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        if (i > 0)
            joined[off++] = ';';
        const char *path = paths[i] ? paths[i] : "";
        for (const char *p = path; *p; p++) {
            if (*p == ';' || *p == '\\')
                joined[off++] = '\\';
            joined[off++] = *p;
        }
    }
    joined[off] = '\0';
    if (out_len)
        *out_len = off;
    return joined;
}

/// @brief Configure `dialog` for modal presentation and push it onto the app's dialog stack.
/// @details Ensures default font is applied, sets the dialog as the modal root over `app->root`,
///          shows it, and pushes it so the main loop blocks on it. Returns 0 if any pointer is NULL.
static int rt_filedialog_prepare_modal(rt_gui_app_t *app, vg_filedialog_t *dialog) {
    if (!app || !app->window || !app->root || !dialog)
        return 0;
    rt_gui_activate_app(app);
    rt_gui_ensure_default_font();
    rt_gui_apply_default_font(&dialog->base.base);
    vg_dialog_set_modal(&dialog->base, true, app->root);
    vg_filedialog_show(dialog);
    rt_gui_push_dialog(app, &dialog->base);
    return 1;
}

/// @brief Run the GUI event loop until the dialog closes or the app signals shutdown.
/// @details Polls and renders the app in a tight loop; pops the dialog from the stack
///          and syncs the modal root on exit. Returns 1 if at least one file was selected.
static int rt_filedialog_run_modal(rt_gui_app_t *app, vg_filedialog_t *dialog) {
    if (!app || !dialog)
        return 0;
    while (dialog->base.is_open && !app->should_close) {
        rt_gui_app_poll(app);
        rt_gui_app_render(app);
    }
    rt_gui_remove_dialog(app, &dialog->base);
    rt_gui_sync_modal_root(app);
    return dialog->selected_file_count > 0 ? 1 : 0;
}

/// @brief Prepare and then run a modal file dialog in one call.
/// @details Combines `rt_filedialog_prepare_modal` + `rt_filedialog_run_modal`.
static int rt_filedialog_show_modal(rt_gui_app_t *app, vg_filedialog_t *dialog) {
    if (!rt_filedialog_prepare_modal(app, dialog))
        return 0;
    return rt_filedialog_run_modal(app, dialog);
}

/// @brief One-shot "open file" dialog. Blocks the caller until the user picks a single file or
/// cancels when an active GUI app/window exists. Returns the absolute path on selection, or an
/// empty string on cancel or when no modal GUI window is active.
rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter) {
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_open_file(ctitle, cpath, "Files", cfilter);
#else
    char *result = NULL;
    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (dlg) {
        if (ctitle)
            vg_filedialog_set_title(dlg, ctitle);
        if (cpath)
            vg_filedialog_set_initial_path(dlg, cpath);
        if (cfilter && cfilter[0])
            vg_filedialog_add_filter(dlg, "Files", cfilter);
        vg_filedialog_add_default_bookmarks(dlg);
        if (rt_filedialog_show_modal(rt_filedialog_app(), dlg) &&
            dlg->selected_file_count > 0 && dlg->selected_files[0]) {
            result = rt_filedialog_strdup(dlg->selected_files[0]);
        }
        vg_filedialog_destroy(dlg);
    }
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    if (result) {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_str_empty();
}

/// @brief Open dialog with multi-select. Returns paths as a single semicolon-separated string
/// (caller can split on ';'), or empty on cancel/no active GUI window.
rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter) {
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);

#ifdef __APPLE__
    size_t count = 0;
    char **paths = vg_native_open_files(ctitle, cpath, "Files", cfilter, &count);
    rt_string result = rt_str_empty();
    if (paths && count > 0) {
        size_t joined_len = 0;
        char *joined = rt_filedialog_join_paths_escaped(paths, count, &joined_len);
        if (joined) {
            result = rt_string_from_bytes(joined, joined_len);
            free(joined);
        }
    }
    vg_native_free_paths(paths, count);

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);
    return result;
#else
    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (!dlg) {
        if (ctitle)
            free(ctitle);
        if (cpath)
            free(cpath);
        if (cfilter)
            free(cfilter);
        return rt_str_empty();
    }

    vg_filedialog_set_title(dlg, ctitle);
    vg_filedialog_set_initial_path(dlg, cpath);
    vg_filedialog_set_multi_select(dlg, true);
    if (cfilter && cfilter[0]) {
        vg_filedialog_add_filter(dlg, "Files", cfilter);
    }
    vg_filedialog_add_default_bookmarks(dlg);

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    rt_filedialog_show_modal(rt_filedialog_app(), dlg);

    size_t count = 0;
    char **paths = vg_filedialog_get_selected_paths(dlg, &count);

    rt_string result = rt_str_empty();
    if (paths && count > 0) {
        size_t joined_len = 0;
        char *joined = rt_filedialog_join_paths_escaped(paths, count, &joined_len);
        if (joined) {
            result = rt_string_from_bytes(joined, joined_len);
            free(joined);
        }
    }

    vg_filedialog_destroy(dlg);
    return result;
#endif
}

/// @brief One-shot "save file" dialog. Returns the chosen path (with extension if user typed
/// one or accepted the default), or empty on cancel. Does not actually create the file — the
/// caller writes to the returned path.
rt_string rt_filedialog_save(rt_string title,
                             rt_string default_path,
                             rt_string filter,
                             rt_string default_name) {
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cname = rt_string_to_cstr(default_name);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_save_file(ctitle, cpath, cname, "Files", cfilter);
#else
    char *result = NULL;
    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_SAVE);
    if (dlg) {
        if (ctitle)
            vg_filedialog_set_title(dlg, ctitle);
        if (cpath)
            vg_filedialog_set_initial_path(dlg, cpath);
        if (cname)
            vg_filedialog_set_filename(dlg, cname);
        if (cfilter && cfilter[0])
            vg_filedialog_add_filter(dlg, "Files", cfilter);
        vg_filedialog_add_default_bookmarks(dlg);
        if (rt_filedialog_show_modal(rt_filedialog_app(), dlg) &&
            dlg->selected_file_count > 0 && dlg->selected_files[0]) {
            result = rt_filedialog_strdup(dlg->selected_files[0]);
        }
        vg_filedialog_destroy(dlg);
    }
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);
    if (cname)
        free(cname);

    if (result) {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_str_empty();
}

/// @brief One-shot folder-picker dialog. Returns the absolute folder path or empty on cancel.
rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path) {
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_select_folder(ctitle, cpath);
#else
    char *result = NULL;
    vg_filedialog_t *dlg = vg_filedialog_create(VG_FILEDIALOG_SELECT_FOLDER);
    if (dlg) {
        if (ctitle)
            vg_filedialog_set_title(dlg, ctitle);
        if (cpath)
            vg_filedialog_set_initial_path(dlg, cpath);
        vg_filedialog_add_default_bookmarks(dlg);
        if (rt_filedialog_show_modal(rt_filedialog_app(), dlg) &&
            dlg->selected_file_count > 0 && dlg->selected_files[0]) {
            result = rt_filedialog_strdup(dlg->selected_files[0]);
        }
        vg_filedialog_destroy(dlg);
    }
#endif

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);

    if (result) {
        rt_string ret = rt_string_from_bytes(result, strlen(result));
        free(result);
        return ret;
    }
    return rt_str_empty();
}

// Custom FileDialog structure
#define RT_FILEDIALOG_DATA_MAGIC UINT64_C(0x525446494C45444C)

typedef struct {
    uint64_t magic;
    rt_gui_app_t *owner_app;
    vg_filedialog_t *dialog;
    char **selected_paths;
    size_t selected_count;
    int64_t result;
} rt_filedialog_data_t;

static rt_filedialog_data_t *rt_filedialog_data_checked(void *dialog) {
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    return data && data->magic == RT_FILEDIALOG_DATA_MAGIC && data->dialog &&
                   vg_widget_is_live(&data->dialog->base.base)
               ? data
               : NULL;
}

/// @brief Free the selected-paths array and reset count to zero.
static void rt_filedialog_clear_selected_paths(rt_filedialog_data_t *data) {
    if (!data || !data->selected_paths)
        return;
    for (size_t i = 0; i < data->selected_count; i++) {
        free(data->selected_paths[i]);
    }
    free(data->selected_paths);
    data->selected_paths = NULL;
    data->selected_count = 0;
}

/// @brief Copy the dialog's current selected-path list into the wrapper struct.
/// @details Fetches the path array from the VG backend, deep-copies every string, then
///          atomically replaces the wrapper's previous list. Partial copy failures clean
///          up fully and return 0 rather than leaving a corrupt partial list.
static int rt_filedialog_copy_selected_paths(rt_filedialog_data_t *data) {
    if (!data || !data->dialog)
        return 0;

    size_t count = 0;
    char **paths = vg_filedialog_get_selected_paths(data->dialog, &count);
    if (!paths || count == 0) {
        rt_filedialog_clear_selected_paths(data);
        return 0;
    }

    char **copy = (char **)calloc(count, sizeof(char *));
    if (!copy)
        return 0;

    size_t copied = 0;
    for (; copied < count; copied++) {
        copy[copied] = rt_filedialog_strdup(paths[copied] ? paths[copied] : "");
        if (!copy[copied])
            break;
    }
    if (copied != count) {
        for (size_t i = 0; i < copied; i++)
            free(copy[i]);
        free(copy);
        return 0;
    }

    rt_filedialog_clear_selected_paths(data);
    data->selected_paths = copy;
    data->selected_count = count;
    return 1;
}

/// @brief Release all resources owned by the file-dialog wrapper.
static void rt_filedialog_dispose(rt_filedialog_data_t *data) {
    if (!data)
        return;
    rt_filedialog_clear_selected_paths(data);
    if (data->dialog) {
        rt_gui_app_t *app = rt_gui_is_app_handle(data->owner_app) ? data->owner_app
                                                                  : rt_filedialog_app();
        if (app)
            rt_gui_remove_dialog(app, &data->dialog->base);
        vg_filedialog_destroy(data->dialog);
        data->dialog = NULL;
    }
    data->owner_app = NULL;
    data->result = 0;
    data->magic = 0;
}

/// @brief GC finalizer — delegates to `rt_filedialog_dispose`.
static void rt_filedialog_finalize(void *dialog) {
    rt_filedialog_dispose((rt_filedialog_data_t *)dialog);
}

/// @brief Construct a stateful FileDialog object — `type` is RT_FILEDIALOG_OPEN/SAVE/FOLDER.
/// Use the setters (`_set_title`, `_set_path`, `_add_filter`, ...) to configure, then `_show`
/// to display modally. Returns NULL on backend or allocation failure.
void *rt_filedialog_new(int64_t type) {
    vg_filedialog_mode_t mode;
    switch (type) {
        case RT_FILEDIALOG_OPEN:
            mode = VG_FILEDIALOG_OPEN;
            break;
        case RT_FILEDIALOG_SAVE:
            mode = VG_FILEDIALOG_SAVE;
            break;
        case RT_FILEDIALOG_FOLDER:
            mode = VG_FILEDIALOG_SELECT_FOLDER;
            break;
        default:
            mode = VG_FILEDIALOG_OPEN;
            break;
    }

    vg_filedialog_t *dlg = vg_filedialog_create(mode);
    if (!dlg)
        return NULL;

    rt_filedialog_data_t *data =
        (rt_filedialog_data_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_filedialog_data_t));
    if (!data) {
        vg_filedialog_destroy(dlg);
        return NULL;
    }
    data->magic = RT_FILEDIALOG_DATA_MAGIC;
    data->owner_app = rt_filedialog_app();
    data->dialog = dlg;
    data->selected_paths = NULL;
    data->selected_count = 0;
    data->result = 0;
    rt_obj_set_finalizer(data, rt_filedialog_finalize);

    return data;
}

/// @brief Convenience constructor for an Open-file dialog.
void *rt_filedialog_new_open(void) {
    return rt_filedialog_new(RT_FILEDIALOG_OPEN);
}

/// @brief Convenience constructor for a Save-file dialog.
void *rt_filedialog_new_save(void) {
    return rt_filedialog_new(RT_FILEDIALOG_SAVE);
}

/// @brief Convenience constructor for a Select-folder dialog.
void *rt_filedialog_new_folder(void) {
    return rt_filedialog_new(RT_FILEDIALOG_FOLDER);
}

/// @brief Set the dialog's titlebar text. No-op if `dialog` is NULL.
void rt_filedialog_set_title(void *dialog, rt_string title) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return;
    char *ctitle = rt_string_to_cstr(title);
    vg_filedialog_set_title(data->dialog, ctitle);
    if (ctitle)
        free(ctitle);
}

/// @brief Set the directory the dialog opens in. Subsequent navigation may move elsewhere; the
/// returned selection is always an absolute path.
void rt_filedialog_set_path(void *dialog, rt_string path) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return;
    char *cpath = rt_string_to_cstr(path);
    vg_filedialog_set_initial_path(data->dialog, cpath);
    if (cpath)
        free(cpath);
}

/// @brief Replace all filename filters with a single (`name`, `pattern`) entry. `name` is the
/// human label shown in the dialog (e.g., "Image files"), `pattern` is the glob (e.g., "*.png").
void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return;
    char *cname = name ? rt_string_to_cstr(name) : NULL;
    char *cpattern = pattern ? rt_string_to_cstr(pattern) : NULL;
    if ((name && !cname) || (pattern && !cpattern)) {
        free(cname);
        free(cpattern);
        return;
    }
    vg_filedialog_clear_filters(data->dialog);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

/// @brief Append an additional (`name`, `pattern`) filter without clearing existing ones. The
/// dialog typically shows them in a dropdown; users can switch between filters at picking time.
void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return;
    char *cname = name ? rt_string_to_cstr(name) : NULL;
    char *cpattern = pattern ? rt_string_to_cstr(pattern) : NULL;
    if ((name && !cname) || (pattern && !cpattern)) {
        free(cname);
        free(cpattern);
        return;
    }
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

/// @brief Pre-fill the filename field (Save dialogs primarily). User can edit before confirming.
void rt_filedialog_set_default_name(void *dialog, rt_string name) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return;
    char *cname = rt_string_to_cstr(name);
    vg_filedialog_set_filename(data->dialog, cname);
    if (cname)
        free(cname);
}

/// @brief Toggle multi-select. After `_show`, retrieve count via `_get_path_count` and individual
/// paths via `_get_path_at(i)`. Has no effect on Save/Folder dialogs.
void rt_filedialog_set_multiple(void *dialog, int64_t multiple) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return;
    vg_filedialog_set_multi_select(data->dialog, multiple != 0);
}

/// @brief Show the dialog modally. Blocks the caller until the user dismisses it. Returns 1 if
/// the user confirmed at least one selection, 0 if cancelled or if no active GUI window exists.
/// Replaces any prior selection snapshot (calling `_show` twice on the same handle is allowed).
int64_t rt_filedialog_show(void *dialog) {
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return 0;

    // Replace any previous selection snapshot with the latest dialog result.
    rt_filedialog_clear_selected_paths(data);
    rt_gui_app_t *app = rt_filedialog_app();
    if (app)
        data->owner_app = app;
    if (!rt_filedialog_show_modal(app, data->dialog)) {
        data->result = 0;
        return 0;
    }
    rt_filedialog_copy_selected_paths(data);
    data->result = (data->selected_count > 0) ? 1 : 0;

    return data->result;
}

/// @brief Return the first selected path from the most recent `_show`. Empty if no selection.
rt_string rt_filedialog_get_path(void *dialog) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return rt_str_empty();
    if (data->selected_paths && data->selected_count > 0) {
        return rt_string_from_bytes(data->selected_paths[0], strlen(data->selected_paths[0]));
    }
    return rt_str_empty();
}

/// @brief Number of paths selected by the most recent `_show` (0 if cancelled or pre-show).
int64_t rt_filedialog_get_path_count(void *dialog) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return 0;
    return (int64_t)data->selected_count;
}

/// @brief Return the i-th selected path (0-based) from a multi-select dialog. Empty if `index`
/// is out of range. Use `_get_path_count` first to bound the iteration.
rt_string rt_filedialog_get_path_at(void *dialog, int64_t index) {
    rt_filedialog_data_t *data = rt_filedialog_data_checked(dialog);
    if (!data)
        return rt_str_empty();
    if (data->selected_paths && index >= 0 && (size_t)index < data->selected_count) {
        return rt_string_from_bytes(data->selected_paths[index],
                                    strlen(data->selected_paths[index]));
    }
    return rt_str_empty();
}

/// @brief Manually free dialog resources (paths, backend handle). The GC finalizer also calls
/// this, so explicit destruction is optional — useful for early cleanup before GC catches up.
void rt_filedialog_destroy(void *dialog) {
    if (!dialog)
        return;
    rt_filedialog_dispose((rt_filedialog_data_t *)dialog);
}

#else /* !VIPER_ENABLE_GRAPHICS */

/// @brief Stub: returns empty string — file open dialog requires graphics.
rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter) {
    (void)title;
    (void)default_path;
    (void)filter;
    return rt_str_empty();
}

/// @brief Stub: returns empty string — multi-select open dialog requires graphics.
rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter) {
    (void)title;
    (void)default_path;
    (void)filter;
    return rt_str_empty();
}

/// @brief Stub: returns empty string — save dialog requires graphics.
rt_string rt_filedialog_save(rt_string title,
                             rt_string default_path,
                             rt_string filter,
                             rt_string default_name) {
    (void)title;
    (void)default_path;
    (void)filter;
    (void)default_name;
    return rt_str_empty();
}

/// @brief Stub: returns empty string — folder picker requires graphics.
rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path) {
    (void)title;
    (void)default_path;
    return rt_str_empty();
}

/// @brief Stub: returns NULL — file dialog object requires graphics.
void *rt_filedialog_new(int64_t type) {
    (void)type;
    return NULL;
}

/// @brief Stub: returns NULL — open-file dialog requires graphics.
void *rt_filedialog_new_open(void) {
    return NULL;
}

/// @brief Stub: returns NULL — save-file dialog requires graphics.
void *rt_filedialog_new_save(void) {
    return NULL;
}

/// @brief Stub: returns NULL — folder-picker dialog requires graphics.
void *rt_filedialog_new_folder(void) {
    return NULL;
}

/// @brief Stub: `FileDialog.SetTitle` is a no-op without graphics.
void rt_filedialog_set_title(void *dialog, rt_string title) {
    (void)dialog;
    (void)title;
}

/// @brief Stub: `FileDialog.SetPath` is a no-op without graphics.
void rt_filedialog_set_path(void *dialog, rt_string path) {
    (void)dialog;
    (void)path;
}

/// @brief Stub: `FileDialog.SetFilter` is a no-op without graphics.
void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern) {
    (void)dialog;
    (void)name;
    (void)pattern;
}

/// @brief Stub: `FileDialog.AddFilter` is a no-op without graphics.
void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern) {
    (void)dialog;
    (void)name;
    (void)pattern;
}

/// @brief Stub: `FileDialog.SetDefaultName` is a no-op without graphics.
void rt_filedialog_set_default_name(void *dialog, rt_string name) {
    (void)dialog;
    (void)name;
}

/// @brief Stub: `FileDialog.SetMultiple` is a no-op without graphics.
void rt_filedialog_set_multiple(void *dialog, int64_t multiple) {
    (void)dialog;
    (void)multiple;
}

/// @brief Stub: returns 0 — dialog cannot be shown without graphics.
int64_t rt_filedialog_show(void *dialog) {
    (void)dialog;
    return 0;
}

/// @brief Stub: returns empty string — no path available without graphics.
rt_string rt_filedialog_get_path(void *dialog) {
    (void)dialog;
    return rt_str_empty();
}

/// @brief Stub: returns 0 — no paths available without graphics.
int64_t rt_filedialog_get_path_count(void *dialog) {
    (void)dialog;
    return 0;
}

/// @brief Stub: returns empty string — no path at index without graphics.
rt_string rt_filedialog_get_path_at(void *dialog, int64_t index) {
    (void)dialog;
    (void)index;
    return rt_str_empty();
}

/// @brief Stub: `FileDialog.Destroy` is a no-op without graphics.
void rt_filedialog_destroy(void *dialog) {
    (void)dialog;
}

#endif /* VIPER_ENABLE_GRAPHICS */
