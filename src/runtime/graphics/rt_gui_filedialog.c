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

rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter) {
    char *ctitle = rt_string_to_cstr(title);
    char *cfilter = rt_string_to_cstr(filter);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_open_file(ctitle, cpath, "Files", cfilter);
#else
    // vg_filedialog_open_file expects: title, path, filter_name, filter_pattern
    char *result = vg_filedialog_open_file(ctitle, cpath, "Files", cfilter);
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

rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter) {
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);
    char *cfilter = rt_string_to_cstr(filter);

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

    if (ctitle)
        free(ctitle);
    if (cpath)
        free(cpath);
    if (cfilter)
        free(cfilter);

    vg_filedialog_show(dlg);

    size_t count = 0;
    char **paths = vg_filedialog_get_selected_paths(dlg, &count);

    rt_string result = rt_str_empty();
    if (paths && count > 0) {
        // Join paths with semicolon
        size_t total_len = 0;
        for (size_t i = 0; i < count; i++) {
            total_len += strlen(paths[i]) + 1;
        }
        char *joined = (char *)malloc(total_len);
        if (joined) {
            size_t off = 0;
            for (size_t i = 0; i < count; i++) {
                if (i > 0)
                    joined[off++] = ';';
                size_t len = strlen(paths[i]);
                memcpy(joined + off, paths[i], len);
                off += len;
            }
            joined[off] = '\0';
            result = rt_string_from_bytes(joined, off);
            free(joined);
        }
        for (size_t i = 0; i < count; i++) {
            free(paths[i]);
        }
        free(paths);
    }

    vg_filedialog_destroy(dlg);
    return result;
}

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
    // vg_filedialog_save_file expects: title, path, default_name, filter_name, filter_pattern
    char *result = vg_filedialog_save_file(ctitle, cpath, cname, "Files", cfilter);
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

rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path) {
    char *ctitle = rt_string_to_cstr(title);
    char *cpath = rt_string_to_cstr(default_path);

#ifdef __APPLE__
    // Use native macOS file dialog
    char *result = vg_native_select_folder(ctitle, cpath);
#else
    char *result = vg_filedialog_select_folder(ctitle, cpath);
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
typedef struct {
    vg_filedialog_t *dialog;
    char **selected_paths;
    size_t selected_count;
    int64_t result;
} rt_filedialog_data_t;

static void rt_filedialog_dispose(rt_filedialog_data_t *data) {
    if (!data)
        return;
    if (data->selected_paths) {
        for (size_t i = 0; i < data->selected_count; i++) {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
        data->selected_paths = NULL;
        data->selected_count = 0;
    }
    if (data->dialog) {
        vg_filedialog_destroy(data->dialog);
        data->dialog = NULL;
    }
    data->result = 0;
}

static void rt_filedialog_finalize(void *dialog) {
    rt_filedialog_dispose((rt_filedialog_data_t *)dialog);
}

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
    data->dialog = dlg;
    data->selected_paths = NULL;
    data->selected_count = 0;
    data->result = 0;
    rt_obj_set_finalizer(data, rt_filedialog_finalize);

    return data;
}

void *rt_filedialog_new_open(void) {
    return rt_filedialog_new(RT_FILEDIALOG_OPEN);
}

void *rt_filedialog_new_save(void) {
    return rt_filedialog_new(RT_FILEDIALOG_SAVE);
}

void *rt_filedialog_new_folder(void) {
    return rt_filedialog_new(RT_FILEDIALOG_FOLDER);
}

void rt_filedialog_set_title(void *dialog, rt_string title) {
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *ctitle = rt_string_to_cstr(title);
    vg_filedialog_set_title(data->dialog, ctitle);
    if (ctitle)
        free(ctitle);
}

void rt_filedialog_set_path(void *dialog, rt_string path) {
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cpath = rt_string_to_cstr(path);
    vg_filedialog_set_initial_path(data->dialog, cpath);
    if (cpath)
        free(cpath);
}

void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern) {
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_clear_filters(data->dialog);
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern) {
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    char *cpattern = rt_string_to_cstr(pattern);
    vg_filedialog_add_filter(data->dialog, cname, cpattern);
    if (cname)
        free(cname);
    if (cpattern)
        free(cpattern);
}

void rt_filedialog_set_default_name(void *dialog, rt_string name) {
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    char *cname = rt_string_to_cstr(name);
    vg_filedialog_set_filename(data->dialog, cname);
    if (cname)
        free(cname);
}

void rt_filedialog_set_multiple(void *dialog, int64_t multiple) {
    if (!dialog)
        return;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_set_multi_select(data->dialog, multiple != 0);
}

int64_t rt_filedialog_show(void *dialog) {
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    vg_filedialog_show(data->dialog);

    // Replace any previous selection snapshot with the latest dialog result.
    if (data->selected_paths) {
        for (size_t i = 0; i < data->selected_count; i++) {
            free(data->selected_paths[i]);
        }
        free(data->selected_paths);
        data->selected_paths = NULL;
        data->selected_count = 0;
    }
    data->selected_paths = vg_filedialog_get_selected_paths(data->dialog, &data->selected_count);
    data->result = (data->selected_count > 0) ? 1 : 0;

    return data->result;
}

rt_string rt_filedialog_get_path(void *dialog) {
    if (!dialog)
        return rt_str_empty();
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && data->selected_count > 0) {
        return rt_string_from_bytes(data->selected_paths[0], strlen(data->selected_paths[0]));
    }
    return rt_str_empty();
}

int64_t rt_filedialog_get_path_count(void *dialog) {
    if (!dialog)
        return 0;
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    return (int64_t)data->selected_count;
}

rt_string rt_filedialog_get_path_at(void *dialog, int64_t index) {
    if (!dialog)
        return rt_str_empty();
    rt_filedialog_data_t *data = (rt_filedialog_data_t *)dialog;
    if (data->selected_paths && index >= 0 && (size_t)index < data->selected_count) {
        return rt_string_from_bytes(data->selected_paths[index],
                                    strlen(data->selected_paths[index]));
    }
    return rt_str_empty();
}

void rt_filedialog_destroy(void *dialog) {
    if (!dialog)
        return;
    rt_filedialog_dispose((rt_filedialog_data_t *)dialog);
}

#else /* !VIPER_ENABLE_GRAPHICS */

rt_string rt_filedialog_open(rt_string title, rt_string default_path, rt_string filter) {
    (void)title;
    (void)default_path;
    (void)filter;
    return rt_str_empty();
}

rt_string rt_filedialog_open_multiple(rt_string title, rt_string default_path, rt_string filter) {
    (void)title;
    (void)default_path;
    (void)filter;
    return rt_str_empty();
}

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

rt_string rt_filedialog_select_folder(rt_string title, rt_string default_path) {
    (void)title;
    (void)default_path;
    return rt_str_empty();
}

void *rt_filedialog_new(int64_t type) {
    (void)type;
    return NULL;
}

void *rt_filedialog_new_open(void) {
    return NULL;
}

void *rt_filedialog_new_save(void) {
    return NULL;
}

void *rt_filedialog_new_folder(void) {
    return NULL;
}

void rt_filedialog_set_title(void *dialog, rt_string title) {
    (void)dialog;
    (void)title;
}

void rt_filedialog_set_path(void *dialog, rt_string path) {
    (void)dialog;
    (void)path;
}

void rt_filedialog_set_filter(void *dialog, rt_string name, rt_string pattern) {
    (void)dialog;
    (void)name;
    (void)pattern;
}

void rt_filedialog_add_filter(void *dialog, rt_string name, rt_string pattern) {
    (void)dialog;
    (void)name;
    (void)pattern;
}

void rt_filedialog_set_default_name(void *dialog, rt_string name) {
    (void)dialog;
    (void)name;
}

void rt_filedialog_set_multiple(void *dialog, int64_t multiple) {
    (void)dialog;
    (void)multiple;
}

int64_t rt_filedialog_show(void *dialog) {
    (void)dialog;
    return 0;
}

rt_string rt_filedialog_get_path(void *dialog) {
    (void)dialog;
    return rt_str_empty();
}

int64_t rt_filedialog_get_path_count(void *dialog) {
    (void)dialog;
    return 0;
}

rt_string rt_filedialog_get_path_at(void *dialog, int64_t index) {
    (void)dialog;
    (void)index;
    return rt_str_empty();
}

void rt_filedialog_destroy(void *dialog) {
    (void)dialog;
}

#endif /* VIPER_ENABLE_GRAPHICS */
