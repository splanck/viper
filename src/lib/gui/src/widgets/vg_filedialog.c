//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/lib/gui/src/widgets/vg_filedialog.c
//
//===----------------------------------------------------------------------===//
// vg_filedialog.c - FileDialog widget implementation
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Platform-specific includes
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <shlobj.h> // For SHGetFolderPath
#include <windows.h>
#define strcasecmp _stricmp
#define stat _stat
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
#include <dirent.h>
#include <fnmatch.h>
#include <pwd.h>
#include <strings.h>
#include <unistd.h>
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

static void filedialog_destroy(vg_widget_t *widget);
static void filedialog_measure(vg_widget_t *widget, float available_width, float available_height);
static void filedialog_paint(vg_widget_t *widget, void *canvas);
static bool filedialog_handle_event(vg_widget_t *widget, vg_event_t *event);

#define FILEDIALOG_TITLE_HEIGHT 35.0f
#define FILEDIALOG_SIDEBAR_WIDTH 150.0f
#define FILEDIALOG_PATH_HEIGHT 30.0f
#define FILEDIALOG_ROW_HEIGHT 24.0f
#define FILEDIALOG_BOOKMARK_HEIGHT 25.0f
#define FILEDIALOG_BUTTON_WIDTH 80.0f
#define FILEDIALOG_BUTTON_HEIGHT 28.0f
#define FILEDIALOG_BUTTON_MARGIN 8.0f
#define FILEDIALOG_CLOSE_BUTTON_SIZE 20.0f
#define FILEDIALOG_FILENAME_HEIGHT 28.0f
#define FILEDIALOG_BOTTOM_HEIGHT 54.0f
#define FILEDIALOG_SAVE_EXTRA_HEIGHT 34.0f

//=============================================================================
// FileDialog VTable
//=============================================================================

static vg_widget_vtable_t g_filedialog_vtable = {.destroy = filedialog_destroy,
                                                 .measure = filedialog_measure,
                                                 .arrange = NULL,
                                                 .paint = filedialog_paint,
                                                 .handle_event = filedialog_handle_event,
                                                 .can_focus = NULL,
                                                 .on_focus = NULL};

//=============================================================================
// Platform Abstraction Layer
//=============================================================================

#ifdef _WIN32

// Windows implementation of pattern matching (simple fnmatch equivalent)
static bool win_match_pattern(const char *pattern, const char *filename) {
    const char *p = pattern;
    const char *f = filename;

    while (*p && *f) {
        if (*p == '*') {
            p++;
            if (!*p)
                return true; // Trailing * matches everything
            // Try to match rest of pattern at each position
            while (*f) {
                if (win_match_pattern(p, f))
                    return true;
                f++;
            }
            return false;
        } else if (*p == '?') {
            p++;
            f++;
        } else {
            // Case-insensitive compare
            char pc = *p, fc = *f;
            if (pc >= 'A' && pc <= 'Z')
                pc += 32;
            if (fc >= 'A' && fc <= 'Z')
                fc += 32;
            if (pc != fc)
                return false;
            p++;
            f++;
        }
    }

    while (*p == '*')
        p++; // Skip trailing asterisks
    return !*p && !*f;
}

#endif

static char *get_home_directory(void) {
#ifdef _WIN32
    // Try USERPROFILE first
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile)
        return _strdup(userprofile);

    // Fall back to HOMEDRIVE + HOMEPATH
    const char *homedrive = getenv("HOMEDRIVE");
    const char *homepath = getenv("HOMEPATH");
    if (homedrive && homepath) {
        size_t len = strlen(homedrive) + strlen(homepath) + 1;
        char *result = malloc(len);
        if (result) {
            strcpy(result, homedrive);
            strcat(result, homepath);
            return result;
        }
    }

    return _strdup("C:\\");
#else
    const char *home = getenv("HOME");
    if (home)
        return strdup(home);

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir)
        return strdup(pw->pw_dir);

    return strdup("/");
#endif
}

static char *join_path(const char *dir, const char *file) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);

#ifdef _WIN32
    char sep = '\\';
    bool need_sep = (dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/');
#else
    char sep = '/';
    bool need_sep = (dir_len > 0 && dir[dir_len - 1] != '/');
#endif

    char *result = malloc(dir_len + (need_sep ? 1 : 0) + file_len + 1);
    if (!result)
        return NULL;

    strcpy(result, dir);
    if (need_sep) {
        result[dir_len] = sep;
        result[dir_len + 1] = '\0';
    }
    strcat(result, file);

    return result;
}

static char *get_parent_directory(const char *path) {
    if (!path || !*path) {
#ifdef _WIN32
        return _strdup("C:\\");
#else
        return strdup("/");
#endif
    }

#ifdef _WIN32
    char *result = _strdup(path);
#else
    char *result = strdup(path);
#endif
    if (!result)
        return NULL;

    // Remove trailing slashes
    size_t len = strlen(result);
#ifdef _WIN32
    while (len > 1 && (result[len - 1] == '\\' || result[len - 1] == '/')) {
        result[--len] = '\0';
    }

    // Find last slash
    char *last_slash = strrchr(result, '\\');
    if (!last_slash)
        last_slash = strrchr(result, '/');

    if (last_slash == result) {
        result[1] = '\0';
    } else if (last_slash == result + 2 && result[1] == ':') {
        // C:\ case
        result[3] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    } else {
        free(result);
        return _strdup(".");
    }
#else
    while (len > 1 && result[len - 1] == '/') {
        result[--len] = '\0';
    }

    char *last_slash = strrchr(result, '/');
    if (last_slash == result) {
        result[1] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    } else {
        free(result);
        return strdup(".");
    }
#endif

    return result;
}

static void get_parent_screen_origin(vg_widget_t *widget, float *x, float *y) {
    float sx = 0.0f;
    float sy = 0.0f;
    if (widget && widget->parent) {
        vg_widget_get_screen_bounds(widget->parent, &sx, &sy, NULL, NULL);
    }
    if (x)
        *x = sx;
    if (y)
        *y = sy;
}

static float filedialog_bottom_height(const vg_filedialog_t *dialog) {
    return FILEDIALOG_BOTTOM_HEIGHT +
           (dialog && dialog->mode == VG_FILEDIALOG_SAVE ? FILEDIALOG_SAVE_EXTRA_HEIGHT : 0.0f);
}

static const char *filedialog_accept_label(const vg_filedialog_t *dialog) {
    if (!dialog)
        return "OK";
    switch (dialog->mode) {
        case VG_FILEDIALOG_SAVE:
            return "Save";
        case VG_FILEDIALOG_SELECT_FOLDER:
            return "Select";
        case VG_FILEDIALOG_OPEN:
        default:
            return "Open";
    }
}

static bool filedialog_filename_has_extension(const char *filename) {
    if (!filename || !*filename)
        return false;

    const char *last_slash = strrchr(filename, '/');
#ifdef _WIN32
    const char *last_backslash = strrchr(filename, '\\');
    if (!last_slash || (last_backslash && last_backslash > last_slash))
        last_slash = last_backslash;
#endif
    const char *last_dot = strrchr(filename, '.');
    return last_dot && (!last_slash || last_dot > last_slash + 1);
}

static void filedialog_set_default_filename(vg_filedialog_t *dialog, const char *filename) {
    if (!dialog)
        return;
    free(dialog->default_filename);
#ifdef _WIN32
    dialog->default_filename = filename ? _strdup(filename) : NULL;
#else
    dialog->default_filename = filename ? strdup(filename) : NULL;
#endif
}

static void filedialog_delete_last_codepoint(char *text) {
    if (!text)
        return;
    size_t len = strlen(text);
    if (len == 0)
        return;
    do {
        len--;
    } while (len > 0 && (((unsigned char)text[len] & 0xC0) == 0x80));
    text[len] = '\0';
}

static void filedialog_append_codepoint(vg_filedialog_t *dialog, uint32_t codepoint) {
    if (!dialog || codepoint < 0x20 || codepoint == 0x7F)
        return;

    char encoded[5] = {0};
    size_t encoded_len = 0;
    if (codepoint < 0x80) {
        encoded[0] = (char)codepoint;
        encoded_len = 1;
    } else if (codepoint < 0x800) {
        encoded[0] = (char)(0xC0 | (codepoint >> 6));
        encoded[1] = (char)(0x80 | (codepoint & 0x3F));
        encoded_len = 2;
    } else if (codepoint < 0x10000) {
        encoded[0] = (char)(0xE0 | (codepoint >> 12));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[2] = (char)(0x80 | (codepoint & 0x3F));
        encoded_len = 3;
    } else {
        encoded[0] = (char)(0xF0 | (codepoint >> 18));
        encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        encoded[3] = (char)(0x80 | (codepoint & 0x3F));
        encoded_len = 4;
    }

    size_t old_len = dialog->default_filename ? strlen(dialog->default_filename) : 0;
    char *new_name = realloc(dialog->default_filename, old_len + encoded_len + 1);
    if (!new_name)
        return;
    dialog->default_filename = new_name;
    memcpy(dialog->default_filename + old_len, encoded, encoded_len);
    dialog->default_filename[old_len + encoded_len] = '\0';
}

static bool filedialog_absolute_path(const char *path) {
    if (!path || !*path)
        return false;
#ifdef _WIN32
    return (strlen(path) > 2 && isalpha((unsigned char)path[0]) && path[1] == ':') ||
           (path[0] == '\\' || path[0] == '/');
#else
    return path[0] == '/';
#endif
}

static int compare_entries(const void *a, const void *b) {
    const vg_file_entry_t *ea = *(const vg_file_entry_t **)a;
    const vg_file_entry_t *eb = *(const vg_file_entry_t **)b;

    // Directories first
    if (ea->is_directory && !eb->is_directory)
        return -1;
    if (!ea->is_directory && eb->is_directory)
        return 1;

    // Then alphabetically (case-insensitive)
    return strcasecmp(ea->name, eb->name);
}

static bool match_filter(const char *filename, const char *pattern) {
    if (!pattern || !*pattern || strcmp(pattern, "*") == 0 || strcmp(pattern, "*.*") == 0) {
        return true;
    }

    // Pattern can be multiple patterns separated by semicolons
#ifdef _WIN32
    char *patterns = _strdup(pattern);
#else
    char *patterns = strdup(pattern);
#endif
    if (!patterns)
        return true;

    char *saveptr = NULL;
    char *token = NULL;

#ifdef _WIN32
    token = strtok_s(patterns, ";", &saveptr);
#else
    token = strtok_r(patterns, ";", &saveptr);
#endif

    while (token) {
        // Trim whitespace
        while (*token == ' ')
            token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ')
            *end-- = '\0';

#ifdef _WIN32
        if (win_match_pattern(token, filename)) {
            free(patterns);
            return true;
        }
        token = strtok_s(NULL, ";", &saveptr);
#else
        if (fnmatch(token, filename, FNM_CASEFOLD) == 0) {
            free(patterns);
            return true;
        }
        token = strtok_r(NULL, ";", &saveptr);
#endif
    }

    free(patterns);
    return false;
}

static void free_entry(vg_file_entry_t *entry) {
    if (entry) {
        free(entry->name);
        free(entry->full_path);
        free(entry);
    }
}

static void clear_entries(vg_filedialog_t *dialog) {
    for (size_t i = 0; i < dialog->entry_count; i++) {
        free_entry(dialog->entries[i]);
    }
    dialog->entry_count = 0;
}

static void load_directory(vg_filedialog_t *dialog, const char *path) {
    clear_entries(dialog);

    // Update current path
    free(dialog->current_path);
#ifdef _WIN32
    dialog->current_path = _strdup(path);
#else
    dialog->current_path = strdup(path);
#endif

#ifdef _WIN32
    // Windows directory iteration using FindFirstFile/FindNextFile
    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        // Skip . and ..
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        // Skip hidden files if not showing them
        if (!dialog->show_hidden && (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) {
            continue;
        }

        bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        // In folder select mode, only show directories
        if (dialog->mode == VG_FILEDIALOG_SELECT_FOLDER && !is_dir) {
            continue;
        }

        // Apply filter for non-directories
        if (!is_dir && dialog->filter_count > 0 && dialog->active_filter < dialog->filter_count) {
            if (!match_filter(find_data.cFileName,
                              dialog->filters[dialog->active_filter].pattern)) {
                continue;
            }
        }

        // Create entry
        vg_file_entry_t *fe = calloc(1, sizeof(vg_file_entry_t));
        if (!fe)
            continue;

        fe->name = _strdup(find_data.cFileName);
        fe->full_path = join_path(path, find_data.cFileName);
        fe->is_directory = is_dir;

        // Calculate size (combine high and low parts for 64-bit size)
        fe->size = ((int64_t)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;

        // Convert FILETIME to time_t
        ULARGE_INTEGER ull;
        ull.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
        fe->modified_time = (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

        // Add to array
        if (dialog->entry_count >= dialog->entry_capacity) {
            size_t new_cap = dialog->entry_capacity == 0 ? 64 : dialog->entry_capacity * 2;
            vg_file_entry_t **new_entries =
                realloc(dialog->entries, new_cap * sizeof(vg_file_entry_t *));
            if (!new_entries) {
                free_entry(fe);
                continue;
            }
            dialog->entries = new_entries;
            dialog->entry_capacity = new_cap;
        }

        dialog->entries[dialog->entry_count++] = fe;

    } while (FindNextFileA(hFind, &find_data));

    FindClose(hFind);
#else
    // POSIX directory iteration
    DIR *dir = opendir(path);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Skip hidden files if not showing them
        if (!dialog->show_hidden && entry->d_name[0] == '.') {
            continue;
        }

        // Get full path for stat
        char *full_path = join_path(path, entry->d_name);
        if (!full_path)
            continue;

        struct stat st;
        if (stat(full_path, &st) != 0) {
            free(full_path);
            continue;
        }

        bool is_dir = S_ISDIR(st.st_mode);

        // In folder select mode, only show directories
        if (dialog->mode == VG_FILEDIALOG_SELECT_FOLDER && !is_dir) {
            free(full_path);
            continue;
        }

        // Apply filter for non-directories
        if (!is_dir && dialog->filter_count > 0 && dialog->active_filter < dialog->filter_count) {
            if (!match_filter(entry->d_name, dialog->filters[dialog->active_filter].pattern)) {
                free(full_path);
                continue;
            }
        }

        // Create entry
        vg_file_entry_t *fe = calloc(1, sizeof(vg_file_entry_t));
        if (!fe) {
            free(full_path);
            continue;
        }

        fe->name = strdup(entry->d_name);
        fe->full_path = full_path;
        fe->is_directory = is_dir;
        fe->size = st.st_size;
        fe->modified_time = st.st_mtime;

        // Add to array
        if (dialog->entry_count >= dialog->entry_capacity) {
            size_t new_cap = dialog->entry_capacity == 0 ? 64 : dialog->entry_capacity * 2;
            vg_file_entry_t **new_entries =
                realloc(dialog->entries, new_cap * sizeof(vg_file_entry_t *));
            if (!new_entries) {
                free_entry(fe);
                continue;
            }
            dialog->entries = new_entries;
            dialog->entry_capacity = new_cap;
        }

        dialog->entries[dialog->entry_count++] = fe;
    }

    closedir(dir);
#endif

    // Sort entries
    if (dialog->entry_count > 0) {
        qsort(dialog->entries, dialog->entry_count, sizeof(vg_file_entry_t *), compare_entries);
    }

    // Clear selection
    dialog->selection_count = 0;
}

static void select_entry(vg_filedialog_t *dialog, size_t index) {
    if (index >= dialog->entry_count)
        return;

    if (!dialog->multi_select) {
        if (dialog->selection_capacity == 0) {
            dialog->selected_indices = malloc(sizeof(int));
            if (!dialog->selected_indices)
                return;
            dialog->selection_capacity = 1;
        }
        dialog->selection_count = 1;
        dialog->selected_indices[0] = (int)index;
    } else {
        // Toggle selection
        bool found = false;
        for (size_t i = 0; i < dialog->selection_count; i++) {
            if (dialog->selected_indices[i] == (int)index) {
                // Remove from selection
                for (size_t j = i; j < dialog->selection_count - 1; j++) {
                    dialog->selected_indices[j] = dialog->selected_indices[j + 1];
                }
                dialog->selection_count--;
                found = true;
                break;
            }
        }

        if (!found) {
            // Add to selection
            if (dialog->selection_count >= dialog->selection_capacity) {
                size_t new_cap =
                    dialog->selection_capacity == 0 ? 8 : dialog->selection_capacity * 2;
                int *new_indices = realloc(dialog->selected_indices, new_cap * sizeof(int));
                if (!new_indices)
                    return;
                dialog->selected_indices = new_indices;
                dialog->selection_capacity = new_cap;
            }
            dialog->selected_indices[dialog->selection_count++] = (int)index;
        }
    }
}

static bool is_selected(vg_filedialog_t *dialog, size_t index) {
    for (size_t i = 0; i < dialog->selection_count; i++) {
        if (dialog->selected_indices[i] == (int)index)
            return true;
    }
    return false;
}

static void confirm_selection(vg_filedialog_t *dialog) {
    // Free previous results
    if (dialog->selected_files) {
        for (size_t i = 0; i < dialog->selected_file_count; i++) {
            free(dialog->selected_files[i]);
        }
        free(dialog->selected_files);
        dialog->selected_files = NULL;
        dialog->selected_file_count = 0;
    }

    if (dialog->mode == VG_FILEDIALOG_SAVE) {
        const char *filename = dialog->default_filename;
        if ((!filename || !filename[0]) && dialog->selection_count > 0) {
            int idx = dialog->selected_indices[0];
            if (idx >= 0 && (size_t)idx < dialog->entry_count && !dialog->entries[idx]->is_directory)
                filename = dialog->entries[idx]->name;
        }

        if (!filename || !filename[0])
            return;

        char *save_name = NULL;
#ifdef _WIN32
        save_name = _strdup(filename);
#else
        save_name = strdup(filename);
#endif
        if (!save_name)
            return;

        if (dialog->default_extension && dialog->default_extension[0] &&
            !filedialog_filename_has_extension(save_name)) {
            size_t name_len = strlen(save_name);
            size_t ext_len = strlen(dialog->default_extension);
            bool needs_dot = dialog->default_extension[0] != '.';
            char *with_ext = realloc(save_name, name_len + ext_len + (needs_dot ? 2 : 1));
            if (!with_ext) {
                free(save_name);
                return;
            }
            save_name = with_ext;
            if (needs_dot)
                save_name[name_len++] = '.';
            memcpy(save_name + name_len, dialog->default_extension, ext_len + 1);
        }

        dialog->selected_files = malloc(sizeof(char *));
        if (!dialog->selected_files) {
            free(save_name);
            return;
        }

        if (filedialog_absolute_path(save_name)) {
            dialog->selected_files[0] = save_name;
        } else {
            dialog->selected_files[0] = join_path(dialog->current_path, save_name);
            free(save_name);
        }
        if (!dialog->selected_files[0]) {
            free(dialog->selected_files);
            dialog->selected_files = NULL;
            return;
        }
        dialog->selected_file_count = 1;
    } else if (dialog->selection_count > 0) {
        int single_idx = dialog->selected_indices[0];
        if (dialog->mode == VG_FILEDIALOG_OPEN && dialog->selection_count == 1 &&
            single_idx >= 0 && (size_t)single_idx < dialog->entry_count &&
            dialog->entries[single_idx]->is_directory) {
            load_directory(dialog, dialog->entries[single_idx]->full_path);
            dialog->base.base.needs_paint = true;
            return;
        }

        dialog->selected_files = malloc(dialog->selection_count * sizeof(char *));
        if (dialog->selected_files) {
            for (size_t i = 0; i < dialog->selection_count; i++) {
                int idx = dialog->selected_indices[i];
                if (idx < 0 || (size_t)idx >= dialog->entry_count)
                    continue;
                if (dialog->mode == VG_FILEDIALOG_OPEN && dialog->entries[idx]->is_directory)
                    continue;
#ifdef _WIN32
                dialog->selected_files[dialog->selected_file_count++] =
                    _strdup(dialog->entries[idx]->full_path);
#else
                dialog->selected_files[dialog->selected_file_count++] =
                    strdup(dialog->entries[idx]->full_path);
#endif
            }
        }
    } else if (dialog->mode == VG_FILEDIALOG_SELECT_FOLDER) {
        dialog->selected_files = malloc(sizeof(char *));
        if (dialog->selected_files) {
#ifdef _WIN32
            dialog->selected_files[0] = _strdup(dialog->current_path);
#else
            dialog->selected_files[0] = strdup(dialog->current_path);
#endif
            dialog->selected_file_count = 1;
        }
    }

    if (dialog->selected_file_count == 0)
        return;

    vg_dialog_close(&dialog->base, VG_DIALOG_RESULT_OK);

    if (dialog->on_select) {
        dialog->on_select(
            dialog, dialog->selected_files, dialog->selected_file_count, dialog->user_data);
    }
}

//=============================================================================
// FileDialog Implementation
//=============================================================================

vg_filedialog_t *vg_filedialog_create(vg_filedialog_mode_t mode) {
    vg_filedialog_t *dialog = calloc(1, sizeof(vg_filedialog_t));
    if (!dialog)
        return NULL;

    // Initialize base dialog
    const char *title = "Open File";
    if (mode == VG_FILEDIALOG_SAVE)
        title = "Save File";
    else if (mode == VG_FILEDIALOG_SELECT_FOLDER)
        title = "Select Folder";

    // Initialize base widget
    vg_widget_init(&dialog->base.base, VG_WIDGET_DIALOG, &g_filedialog_vtable);

    vg_theme_t *theme = vg_theme_get_current();

    // Initialize dialog fields
#ifdef _WIN32
    dialog->base.title = _strdup(title);
#else
    dialog->base.title = strdup(title);
#endif
    dialog->base.show_close_button = true;
    dialog->base.draggable = true;
    dialog->base.modal = true;
    dialog->base.min_width = 600;
    dialog->base.min_height = 400;
    dialog->base.resizable = true;
    dialog->base.is_open = false;
    dialog->base.bg_color = theme->colors.bg_primary;
    dialog->base.title_bg_color = theme->colors.bg_secondary;
    dialog->base.title_text_color = theme->colors.fg_primary;
    dialog->base.text_color = theme->colors.fg_primary;
    dialog->base.button_bg_color = theme->colors.bg_tertiary;
    dialog->base.button_hover_color = theme->colors.bg_hover;
    dialog->base.font_size = theme->typography.size_normal;
    dialog->base.title_font_size = theme->typography.size_normal;
    dialog->base.button_preset = VG_DIALOG_BUTTONS_OK_CANCEL;

    // Initialize file dialog fields
    dialog->mode = mode;
    dialog->current_path = get_home_directory();
    dialog->show_hidden = false;
    dialog->confirm_overwrite = true;
    dialog->multi_select = (mode == VG_FILEDIALOG_OPEN);

    // Set default size
    dialog->base.base.width = 700;
    dialog->base.base.height = 500;

    return dialog;
}

static void filedialog_destroy(vg_widget_t *widget) {
    vg_filedialog_t *dialog = (vg_filedialog_t *)widget;
    if (vg_widget_get_input_capture() == widget)
        vg_widget_release_input_capture();
    if (vg_widget_get_modal_root() == widget)
        vg_widget_set_modal_root(NULL);

    // Free entries
    clear_entries(dialog);
    free(dialog->entries);

    // Free selection
    free(dialog->selected_indices);

    // Free filters
    for (size_t i = 0; i < dialog->filter_count; i++) {
        free(dialog->filters[i].name);
        free(dialog->filters[i].pattern);
    }
    free(dialog->filters);

    // Free bookmarks
    for (size_t i = 0; i < dialog->bookmark_count; i++) {
        free(dialog->bookmarks[i].name);
        free(dialog->bookmarks[i].path);
    }
    free(dialog->bookmarks);

    // Free strings
    free(dialog->current_path);
    free(dialog->default_filename);
    free(dialog->default_extension);

    // Free results
    if (dialog->selected_files) {
        for (size_t i = 0; i < dialog->selected_file_count; i++) {
            free(dialog->selected_files[i]);
        }
        free(dialog->selected_files);
    }

    // Free base dialog fields
    free((void *)dialog->base.title);
    free((void *)dialog->base.message);
}

static void filedialog_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_filedialog_t *dialog = (vg_filedialog_t *)widget;
    (void)available_width;
    (void)available_height;

    widget->measured_width = dialog->base.min_width;
    widget->measured_height = dialog->base.min_height;
}

static void filedialog_paint(vg_widget_t *widget, void *canvas) {
    vg_filedialog_t *dialog = (vg_filedialog_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();

    if (!dialog->base.is_open)
        return;

    float x = widget->x;
    float y = widget->y;
    float w = widget->width;
    float h = widget->height;
    float sidebar_width = FILEDIALOG_SIDEBAR_WIDTH;
    float title_height = FILEDIALOG_TITLE_HEIGHT;
    float path_height = FILEDIALOG_PATH_HEIGHT;
    float bottom_height = filedialog_bottom_height(dialog);
    float list_x = x + sidebar_width;
    float list_y = y + title_height + path_height;
    float list_width = w - sidebar_width;
    float list_height = h - title_height - path_height - bottom_height;
    float button_y = y + h - FILEDIALOG_BUTTON_HEIGHT - 10.0f;
    float ok_x = x + w - FILEDIALOG_BUTTON_WIDTH - FILEDIALOG_BUTTON_MARGIN;
    float cancel_x = ok_x - FILEDIALOG_BUTTON_WIDTH - FILEDIALOG_BUTTON_MARGIN;

    vgfx_window_t win = (vgfx_window_t)canvas;

    // Draw modal overlay (dark semi-transparent background behind dialog)
    int32_t win_w = 0, win_h = 0;
    if (vgfx_get_size(win, &win_w, &win_h) == 0) {
        vgfx_fill_rect(win, 0, 0, win_w, win_h, 0x60101010u);
    }

    // Draw dialog background
    vgfx_fill_rect(win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, theme->colors.bg_primary);
    vgfx_rect(win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h, theme->colors.border_primary);

    // Title bar
    vgfx_fill_rect(
        win, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)title_height, dialog->base.title_bg_color);
    vgfx_fill_rect(win,
                   (int32_t)x,
                   (int32_t)(y + title_height - 1.0f),
                   (int32_t)w,
                   1,
                   theme->colors.border_primary);

    if (dialog->base.title && dialog->base.font) {
        vg_font_draw_text(canvas,
                          dialog->base.font,
                          dialog->base.title_font_size,
                          x + 12.0f,
                          y + title_height / 2.0f + dialog->base.title_font_size / 3.0f,
                          dialog->base.title,
                          dialog->base.title_text_color);
        vg_font_draw_text(canvas,
                          dialog->base.font,
                          dialog->base.font_size,
                          x + w - FILEDIALOG_CLOSE_BUTTON_SIZE - 8.0f,
                          y + title_height / 2.0f + dialog->base.font_size / 3.0f,
                          "X",
                          dialog->base.title_text_color);
    }

    // Path bar and sidebar chrome
    vgfx_fill_rect(win,
                   (int32_t)x,
                   (int32_t)(y + title_height),
                   (int32_t)w,
                   (int32_t)path_height,
                   theme->colors.bg_secondary);
    vgfx_fill_rect(win,
                   (int32_t)(x + sidebar_width - 1.0f),
                   (int32_t)(y + title_height),
                   1,
                   (int32_t)(h - title_height),
                   theme->colors.border_primary);
    vgfx_fill_rect(win,
                   (int32_t)x,
                   (int32_t)(y + title_height + path_height - 1.0f),
                   (int32_t)w,
                   1,
                   theme->colors.border_primary);

    // Draw path
    if (dialog->base.font && dialog->current_path) {
        vg_font_draw_text(canvas,
                          dialog->base.font,
                          dialog->base.font_size,
                          x + sidebar_width + 10.0f,
                          y + title_height + path_height / 2.0f + dialog->base.font_size / 3.0f,
                          dialog->current_path,
                          dialog->base.title_text_color);
    }

    // Draw bookmarks
    float bookmark_y = list_y + 5.0f;
    for (size_t i = 0; i < dialog->bookmark_count && bookmark_y < list_y + list_height; i++) {
        if (dialog->base.font) {
            vg_font_draw_text(canvas,
                              dialog->base.font,
                              dialog->base.font_size,
                              x + 10.0f,
                              bookmark_y + 18.0f,
                              dialog->bookmarks[i].name,
                              theme->colors.fg_primary);
        }
        bookmark_y += FILEDIALOG_BOOKMARK_HEIGHT;
    }

    // Draw file list
    float file_x = list_x + 10.0f;
    float file_y = list_y + 5.0f;
    vgfx_set_clip(win,
                  (int32_t)list_x,
                  (int32_t)list_y,
                  (int32_t)list_width,
                  (int32_t)(list_height > 0.0f ? list_height : 0.0f));

    for (size_t i = 0; i < dialog->entry_count && file_y < list_y + list_height; i++) {
        vg_file_entry_t *entry = dialog->entries[i];

        // Highlight if selected
        uint32_t text_color = theme->colors.fg_primary;
        if (is_selected(dialog, i)) {
            vgfx_fill_rect(win,
                           (int32_t)list_x,
                           (int32_t)file_y,
                           (int32_t)list_width,
                           (int32_t)FILEDIALOG_ROW_HEIGHT,
                           theme->colors.bg_selected);
        }

        // Draw icon indicator
        const char *icon = entry->is_directory ? "[D]" : "   ";
        if (dialog->base.font) {
            vg_font_draw_text(canvas,
                              dialog->base.font,
                              dialog->base.font_size,
                              file_x,
                              file_y + 18,
                              icon,
                              theme->colors.fg_secondary);

            vg_font_draw_text(canvas,
                              dialog->base.font,
                              dialog->base.font_size,
                              file_x + 30,
                              file_y + 18,
                              entry->name,
                              text_color);
        }

        file_y += FILEDIALOG_ROW_HEIGHT;
    }
    vgfx_clear_clip(win);

    // Bottom action area
    vgfx_fill_rect(win,
                   (int32_t)x,
                   (int32_t)(y + h - bottom_height),
                   (int32_t)w,
                   (int32_t)bottom_height,
                   theme->colors.bg_secondary);
    vgfx_fill_rect(win,
                   (int32_t)x,
                   (int32_t)(y + h - bottom_height),
                   (int32_t)w,
                   1,
                   theme->colors.border_primary);

    if (dialog->mode == VG_FILEDIALOG_SAVE) {
        float field_x = list_x + 10.0f;
        float field_y = y + h - bottom_height + 8.0f;
        float field_w = w - sidebar_width - FILEDIALOG_BUTTON_WIDTH * 2.0f -
                        FILEDIALOG_BUTTON_MARGIN * 3.0f - 20.0f;
        if (field_w < 120.0f)
            field_w = 120.0f;

        vgfx_fill_rect(win,
                       (int32_t)field_x,
                       (int32_t)field_y,
                       (int32_t)field_w,
                       (int32_t)FILEDIALOG_FILENAME_HEIGHT,
                       theme->colors.bg_primary);
        vgfx_rect(win,
                  (int32_t)field_x,
                  (int32_t)field_y,
                  (int32_t)field_w,
                  (int32_t)FILEDIALOG_FILENAME_HEIGHT,
                  dialog->filename_active ? theme->colors.border_focus
                                          : theme->colors.border_primary);
        if (dialog->base.font) {
            const char *name_text =
                (dialog->default_filename && dialog->default_filename[0]) ? dialog->default_filename
                                                                          : "File name";
            uint32_t name_color = (dialog->default_filename && dialog->default_filename[0])
                                      ? theme->colors.fg_primary
                                      : theme->colors.fg_placeholder;
            vg_font_draw_text(canvas,
                              dialog->base.font,
                              dialog->base.font_size,
                              field_x + 8.0f,
                              field_y + FILEDIALOG_FILENAME_HEIGHT / 2.0f +
                                  dialog->base.font_size / 3.0f,
                              name_text,
                              name_color);
        }
    }

    // Draw OK/Cancel buttons at bottom right
    if (dialog->base.font) {
        uint32_t btn_bg = dialog->base.button_bg_color;
        uint32_t btn_border = theme->colors.border_primary;
        uint32_t btn_fg = dialog->base.title_text_color;
        const char *ok_label = filedialog_accept_label(dialog);

        // Cancel button
        vgfx_fill_rect(
            win,
            (int32_t)cancel_x,
            (int32_t)button_y,
            (int32_t)FILEDIALOG_BUTTON_WIDTH,
            (int32_t)FILEDIALOG_BUTTON_HEIGHT,
            btn_bg);
        vgfx_rect(
            win,
            (int32_t)cancel_x,
            (int32_t)button_y,
            (int32_t)FILEDIALOG_BUTTON_WIDTH,
            (int32_t)FILEDIALOG_BUTTON_HEIGHT,
            btn_border);
        vg_font_draw_text(canvas,
                          dialog->base.font,
                          dialog->base.font_size,
                          cancel_x + 16.0f,
                          button_y + 18.0f,
                          "Cancel",
                          btn_fg);

        // OK button
        vgfx_fill_rect(win,
                       (int32_t)ok_x,
                       (int32_t)button_y,
                       (int32_t)FILEDIALOG_BUTTON_WIDTH,
                       (int32_t)FILEDIALOG_BUTTON_HEIGHT,
                       theme->colors.accent_primary);
        vgfx_rect(win,
                  (int32_t)ok_x,
                  (int32_t)button_y,
                  (int32_t)FILEDIALOG_BUTTON_WIDTH,
                  (int32_t)FILEDIALOG_BUTTON_HEIGHT,
                  btn_border);
        vg_font_draw_text(canvas,
                          dialog->base.font,
                          dialog->base.font_size,
                          ok_x + 16.0f,
                          button_y + 18.0f,
                          ok_label,
                          btn_fg);
    }
}

static bool filedialog_handle_event(vg_widget_t *widget, vg_event_t *event) {
    vg_filedialog_t *dialog = (vg_filedialog_t *)widget;

    if (!dialog->base.is_open)
        return false;

    float title_height = FILEDIALOG_TITLE_HEIGHT;
    float sidebar_width = FILEDIALOG_SIDEBAR_WIDTH;
    float path_height = FILEDIALOG_PATH_HEIGHT;
    float bottom_height = filedialog_bottom_height(dialog);
    float list_y = title_height + path_height;
    float list_height = widget->height - title_height - path_height - bottom_height;
    float button_y = widget->height - FILEDIALOG_BUTTON_HEIGHT - 10.0f;
    float ok_x = widget->width - FILEDIALOG_BUTTON_WIDTH - FILEDIALOG_BUTTON_MARGIN;
    float cancel_x = ok_x - FILEDIALOG_BUTTON_WIDTH - FILEDIALOG_BUTTON_MARGIN;

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE:
            if (dialog->base.is_dragging) {
                float parent_sx = 0.0f;
                float parent_sy = 0.0f;
                get_parent_screen_origin(widget, &parent_sx, &parent_sy);
                widget->x = event->mouse.screen_x - parent_sx - (float)dialog->base.drag_offset_x;
                widget->y = event->mouse.screen_y - parent_sy - (float)dialog->base.drag_offset_y;
                widget->needs_paint = true;
                widget->needs_layout = true;
                return true;
            }
            return dialog->base.modal;

        case VG_EVENT_MOUSE_DOWN: {
            float mx = event->mouse.x;
            float my = event->mouse.y;

            // Title bar close button
            if (dialog->base.show_close_button &&
                mx >= widget->width - FILEDIALOG_CLOSE_BUTTON_SIZE - 8.0f &&
                mx < widget->width - 8.0f && my >= (title_height - FILEDIALOG_CLOSE_BUTTON_SIZE) / 2.0f &&
                my < (title_height + FILEDIALOG_CLOSE_BUTTON_SIZE) / 2.0f) {
                vg_dialog_close(&dialog->base, VG_DIALOG_RESULT_CANCEL);
                if (dialog->on_cancel)
                    dialog->on_cancel(dialog, dialog->user_data);
                return true;
            }

            if (dialog->base.draggable && my >= 0.0f && my < title_height) {
                float widget_sx = 0.0f;
                float widget_sy = 0.0f;
                vg_widget_get_screen_bounds(widget, &widget_sx, &widget_sy, NULL, NULL);
                dialog->base.is_dragging = true;
                dialog->base.drag_offset_x = (int)(event->mouse.screen_x - widget_sx);
                dialog->base.drag_offset_y = (int)(event->mouse.screen_y - widget_sy);
                vg_widget_set_input_capture(widget);
                return true;
            }

            if (mx >= cancel_x && mx < cancel_x + FILEDIALOG_BUTTON_WIDTH && my >= button_y &&
                my < button_y + FILEDIALOG_BUTTON_HEIGHT) {
                vg_dialog_close(&dialog->base, VG_DIALOG_RESULT_CANCEL);
                if (dialog->on_cancel)
                    dialog->on_cancel(dialog, dialog->user_data);
                return true;
            }

            if (mx >= ok_x && mx < ok_x + FILEDIALOG_BUTTON_WIDTH && my >= button_y &&
                my < button_y + FILEDIALOG_BUTTON_HEIGHT) {
                confirm_selection(dialog);
                return true;
            }

            if (dialog->mode == VG_FILEDIALOG_SAVE) {
                float field_x = sidebar_width + 10.0f;
                float field_y = widget->height - bottom_height + 8.0f;
                float field_w = widget->width - sidebar_width - FILEDIALOG_BUTTON_WIDTH * 2.0f -
                                FILEDIALOG_BUTTON_MARGIN * 3.0f - 20.0f;
                if (field_w < 120.0f)
                    field_w = 120.0f;
                dialog->filename_active = mx >= field_x && mx < field_x + field_w && my >= field_y &&
                                          my < field_y + FILEDIALOG_FILENAME_HEIGHT;
                if (dialog->filename_active)
                    return true;
            } else {
                dialog->filename_active = false;
            }

            // Check if clicking in file list
            if (mx > sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_index = (size_t)((my - list_y - 5.0f) / FILEDIALOG_ROW_HEIGHT);
                if (clicked_index < dialog->entry_count) {
                    select_entry(dialog, clicked_index);
                    if (dialog->mode == VG_FILEDIALOG_SAVE && !dialog->entries[clicked_index]->is_directory)
                        filedialog_set_default_filename(dialog, dialog->entries[clicked_index]->name);
                    widget->needs_paint = true;
                    return true;
                }
            }

            // Check if clicking in bookmarks
            if (mx < sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_bookmark = (size_t)((my - list_y - 5.0f) / FILEDIALOG_BOOKMARK_HEIGHT);
                if (clicked_bookmark < dialog->bookmark_count) {
                    load_directory(dialog, dialog->bookmarks[clicked_bookmark].path);
                    dialog->filename_active = false;
                    widget->needs_paint = true;
                    return true;
                }
            }

            return dialog->base.modal;
        }

        case VG_EVENT_DOUBLE_CLICK: {
            float mx = event->mouse.x;
            float my = event->mouse.y;

            // Check double-click in file list
            if (mx > sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_index = (size_t)((my - list_y - 5.0f) / FILEDIALOG_ROW_HEIGHT);
                if (clicked_index < dialog->entry_count) {
                    vg_file_entry_t *entry = dialog->entries[clicked_index];
                    if (entry->is_directory) {
                        // Navigate into directory
                        load_directory(dialog, entry->full_path);
                        widget->needs_paint = true;
                    } else {
                        // Select file and confirm
                        select_entry(dialog, clicked_index);
                        if (dialog->mode == VG_FILEDIALOG_SAVE)
                            filedialog_set_default_filename(dialog, entry->name);
                        confirm_selection(dialog);
                    }
                    return true;
                }
            }
            return dialog->base.modal;
        }

        case VG_EVENT_MOUSE_UP:
            if (dialog->base.is_dragging) {
                dialog->base.is_dragging = false;
                if (vg_widget_get_input_capture() == widget)
                    vg_widget_release_input_capture();
                return true;
            }
            return dialog->base.modal;

        case VG_EVENT_KEY_DOWN: {
            if (event->key.key == VG_KEY_ESCAPE) {
                vg_dialog_close(&dialog->base, VG_DIALOG_RESULT_CANCEL);
                if (dialog->on_cancel) {
                    dialog->on_cancel(dialog, dialog->user_data);
                }
                return true;
            }

            if (dialog->mode == VG_FILEDIALOG_SAVE && dialog->filename_active) {
                if (event->key.key == VG_KEY_BACKSPACE || event->key.key == VG_KEY_DELETE) {
                    if (dialog->default_filename)
                        filedialog_delete_last_codepoint(dialog->default_filename);
                    widget->needs_paint = true;
                    return true;
                }
            }

            if (event->key.key == VG_KEY_ENTER) {
                if (dialog->selection_count > 0) {
                    // If selected item is directory, navigate
                    int idx = dialog->selected_indices[0];
                    if (idx >= 0 && (size_t)idx < dialog->entry_count) {
                        if (dialog->entries[idx]->is_directory) {
                            load_directory(dialog, dialog->entries[idx]->full_path);
                            widget->needs_paint = true;
                            return true;
                        }
                    }
                }
                // Confirm selection
                confirm_selection(dialog);
                return true;
            }

            // Backspace - go up
            if (event->key.key == VG_KEY_BACKSPACE) {
                char *parent = get_parent_directory(dialog->current_path);
                if (parent) {
                    load_directory(dialog, parent);
                    free(parent);
                    widget->needs_paint = true;
                }
                return true;
            }

            return dialog->base.modal;
        }

        case VG_EVENT_KEY_CHAR:
            if (dialog->mode == VG_FILEDIALOG_SAVE && dialog->filename_active) {
                filedialog_append_codepoint(dialog, event->key.codepoint);
                widget->needs_paint = true;
                return true;
            }
            return dialog->base.modal;

        default:
            break;
    }

    return dialog->base.modal;
}

//=============================================================================
// FileDialog API
//=============================================================================

void vg_filedialog_destroy(vg_filedialog_t *dialog) {
    if (dialog) {
        filedialog_destroy(&dialog->base.base);
        free(dialog);
    }
}

/// @brief Filedialog set title.
void vg_filedialog_set_title(vg_filedialog_t *dialog, const char *title) {
    if (!dialog)
        return;
    free((void *)dialog->base.title);
#ifdef _WIN32
    dialog->base.title = title ? _strdup(title) : NULL;
#else
    dialog->base.title = title ? strdup(title) : NULL;
#endif
}

/// @brief Filedialog set initial path.
void vg_filedialog_set_initial_path(vg_filedialog_t *dialog, const char *path) {
    if (!dialog)
        return;
    free(dialog->current_path);
#ifdef _WIN32
    dialog->current_path = path ? _strdup(path) : get_home_directory();
#else
    dialog->current_path = path ? strdup(path) : get_home_directory();
#endif
}

/// @brief Filedialog set filename.
void vg_filedialog_set_filename(vg_filedialog_t *dialog, const char *filename) {
    if (!dialog)
        return;
    filedialog_set_default_filename(dialog, filename);
    dialog->base.base.needs_paint = true;
}

/// @brief Filedialog set multi select.
void vg_filedialog_set_multi_select(vg_filedialog_t *dialog, bool multi) {
    if (dialog)
        dialog->multi_select = multi;
}

/// @brief Filedialog set show hidden.
void vg_filedialog_set_show_hidden(vg_filedialog_t *dialog, bool show) {
    if (dialog)
        dialog->show_hidden = show;
}

/// @brief Filedialog set confirm overwrite.
void vg_filedialog_set_confirm_overwrite(vg_filedialog_t *dialog, bool confirm) {
    if (dialog)
        dialog->confirm_overwrite = confirm;
}

/// @brief Filedialog add filter.
void vg_filedialog_add_filter(vg_filedialog_t *dialog, const char *name, const char *pattern) {
    if (!dialog || !name || !pattern)
        return;

    if (dialog->filter_count >= dialog->filter_capacity) {
        size_t new_cap = dialog->filter_capacity == 0 ? 4 : dialog->filter_capacity * 2;
        vg_file_filter_t *new_filters =
            realloc(dialog->filters, new_cap * sizeof(vg_file_filter_t));
        if (!new_filters)
            return;
        dialog->filters = new_filters;
        dialog->filter_capacity = new_cap;
    }

#ifdef _WIN32
    dialog->filters[dialog->filter_count].name = _strdup(name);
    dialog->filters[dialog->filter_count].pattern = _strdup(pattern);
#else
    dialog->filters[dialog->filter_count].name = strdup(name);
    dialog->filters[dialog->filter_count].pattern = strdup(pattern);
#endif
    dialog->filter_count++;
}

/// @brief Filedialog clear filters.
void vg_filedialog_clear_filters(vg_filedialog_t *dialog) {
    if (!dialog)
        return;

    for (size_t i = 0; i < dialog->filter_count; i++) {
        free(dialog->filters[i].name);
        free(dialog->filters[i].pattern);
    }
    dialog->filter_count = 0;
}

/// @brief Filedialog set default extension.
void vg_filedialog_set_default_extension(vg_filedialog_t *dialog, const char *ext) {
    if (!dialog)
        return;
    free(dialog->default_extension);
#ifdef _WIN32
    dialog->default_extension = ext ? _strdup(ext) : NULL;
#else
    dialog->default_extension = ext ? strdup(ext) : NULL;
#endif
}

/// @brief Filedialog add bookmark.
void vg_filedialog_add_bookmark(vg_filedialog_t *dialog, const char *name, const char *path) {
    if (!dialog || !name || !path)
        return;

    if (dialog->bookmark_count >= dialog->bookmark_capacity) {
        size_t new_cap = dialog->bookmark_capacity == 0 ? 8 : dialog->bookmark_capacity * 2;
        vg_bookmark_t *new_bookmarks = realloc(dialog->bookmarks, new_cap * sizeof(vg_bookmark_t));
        if (!new_bookmarks)
            return;
        dialog->bookmarks = new_bookmarks;
        dialog->bookmark_capacity = new_cap;
    }

#ifdef _WIN32
    dialog->bookmarks[dialog->bookmark_count].name = _strdup(name);
    dialog->bookmarks[dialog->bookmark_count].path = _strdup(path);
#else
    dialog->bookmarks[dialog->bookmark_count].name = strdup(name);
    dialog->bookmarks[dialog->bookmark_count].path = strdup(path);
#endif
    dialog->bookmarks[dialog->bookmark_count].icon.type = VG_ICON_NONE;
    dialog->bookmark_count++;
}

/// @brief Filedialog add default bookmarks.
void vg_filedialog_add_default_bookmarks(vg_filedialog_t *dialog) {
    if (!dialog)
        return;

    char *home = get_home_directory();
    if (home) {
        vg_filedialog_add_bookmark(dialog, "Home", home);

        char *desktop = join_path(home, "Desktop");
        if (desktop) {
            struct stat st;
            if (stat(desktop, &st) == 0 && S_ISDIR(st.st_mode)) {
                vg_filedialog_add_bookmark(dialog, "Desktop", desktop);
            }
            free(desktop);
        }

        char *documents = join_path(home, "Documents");
        if (documents) {
            struct stat st;
            if (stat(documents, &st) == 0 && S_ISDIR(st.st_mode)) {
                vg_filedialog_add_bookmark(dialog, "Documents", documents);
            }
            free(documents);
        }

        char *downloads = join_path(home, "Downloads");
        if (downloads) {
            struct stat st;
            if (stat(downloads, &st) == 0 && S_ISDIR(st.st_mode)) {
                vg_filedialog_add_bookmark(dialog, "Downloads", downloads);
            }
            free(downloads);
        }

        free(home);
    }

#ifdef _WIN32
    // Add drives on Windows
    vg_filedialog_add_bookmark(dialog, "Computer", "C:\\");
#else
    // Add root on Unix
    vg_filedialog_add_bookmark(dialog, "Computer", "/");
#endif
}

/// @brief Filedialog clear bookmarks.
void vg_filedialog_clear_bookmarks(vg_filedialog_t *dialog) {
    if (!dialog)
        return;

    for (size_t i = 0; i < dialog->bookmark_count; i++) {
        free(dialog->bookmarks[i].name);
        free(dialog->bookmarks[i].path);
    }
    dialog->bookmark_count = 0;
}

/// @brief Filedialog show.
void vg_filedialog_show(vg_filedialog_t *dialog) {
    if (!dialog)
        return;

    // Load current directory
    load_directory(dialog, dialog->current_path);

    // Show dialog
    dialog->base.is_open = true;
    dialog->base.is_dragging = false;
    dialog->base.result = VG_DIALOG_RESULT_NONE;
    dialog->base.base.visible = true;
    dialog->filename_active = dialog->mode == VG_FILEDIALOG_SAVE;
    dialog->base.base.needs_layout = true;
    dialog->base.base.needs_paint = true;
    if (dialog->base.modal)
        vg_widget_set_modal_root(&dialog->base.base);
}

char **vg_filedialog_get_selected_paths(vg_filedialog_t *dialog, size_t *count) {
    if (!dialog) {
        if (count)
            *count = 0;
        return NULL;
    }

    if (count)
        *count = dialog->selected_file_count;
    return dialog->selected_files;
}

/// @brief Filedialog get selected path.
char *vg_filedialog_get_selected_path(vg_filedialog_t *dialog) {
    if (!dialog || dialog->selected_file_count == 0)
        return NULL;
    return dialog->selected_files[0];
}

/// @brief Filedialog set on select.
void vg_filedialog_set_on_select(vg_filedialog_t *dialog,
                                 void (*callback)(vg_filedialog_t *, char **, size_t, void *),
                                 void *user_data) {
    if (!dialog)
        return;
    dialog->on_select = callback;
    dialog->user_data = user_data;
}

/// @brief Filedialog set on cancel.
void vg_filedialog_set_on_cancel(vg_filedialog_t *dialog,
                                 void (*callback)(vg_filedialog_t *, void *),
                                 void *user_data) {
    if (!dialog)
        return;
    dialog->on_cancel = callback;
    dialog->user_data = user_data;
}

//=============================================================================
// Convenience Functions
//=============================================================================

char *vg_filedialog_open_file(const char *title,
                              const char *initial_path,
                              const char *filter_name,
                              const char *filter_pattern) {
    vg_filedialog_t *dialog = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (!dialog)
        return NULL;

    if (title)
        vg_filedialog_set_title(dialog, title);
    if (initial_path)
        vg_filedialog_set_initial_path(dialog, initial_path);
    if (filter_name && filter_pattern) {
        vg_filedialog_add_filter(dialog, filter_name, filter_pattern);
    }
    vg_filedialog_add_default_bookmarks(dialog);

    vg_filedialog_show(dialog);

    // Note: In a real implementation, this would block and wait for the dialog
    // For now, we just return NULL - proper modal dialog requires event loop integration
    char *result = NULL;
    if (dialog->selected_file_count > 0 && dialog->selected_files[0]) {
#ifdef _WIN32
        result = _strdup(dialog->selected_files[0]);
#else
        result = strdup(dialog->selected_files[0]);
#endif
    }

    vg_filedialog_destroy(dialog);
    return result;
}

/// @brief Filedialog save file.
char *vg_filedialog_save_file(const char *title,
                              const char *initial_path,
                              const char *default_name,
                              const char *filter_name,
                              const char *filter_pattern) {
    vg_filedialog_t *dialog = vg_filedialog_create(VG_FILEDIALOG_SAVE);
    if (!dialog)
        return NULL;

    if (title)
        vg_filedialog_set_title(dialog, title);
    if (initial_path)
        vg_filedialog_set_initial_path(dialog, initial_path);
    if (default_name)
        vg_filedialog_set_filename(dialog, default_name);
    if (filter_name && filter_pattern) {
        vg_filedialog_add_filter(dialog, filter_name, filter_pattern);
    }
    vg_filedialog_add_default_bookmarks(dialog);

    vg_filedialog_show(dialog);

    char *result = NULL;
    if (dialog->selected_file_count > 0 && dialog->selected_files[0]) {
#ifdef _WIN32
        result = _strdup(dialog->selected_files[0]);
#else
        result = strdup(dialog->selected_files[0]);
#endif
    }

    vg_filedialog_destroy(dialog);
    return result;
}

/// @brief Filedialog select folder.
char *vg_filedialog_select_folder(const char *title, const char *initial_path) {
    vg_filedialog_t *dialog = vg_filedialog_create(VG_FILEDIALOG_SELECT_FOLDER);
    if (!dialog)
        return NULL;

    if (title)
        vg_filedialog_set_title(dialog, title);
    if (initial_path)
        vg_filedialog_set_initial_path(dialog, initial_path);
    vg_filedialog_add_default_bookmarks(dialog);

    vg_filedialog_show(dialog);

    char *result = NULL;
    if (dialog->selected_file_count > 0 && dialog->selected_files[0]) {
#ifdef _WIN32
        result = _strdup(dialog->selected_files[0]);
#else
        result = strdup(dialog->selected_files[0]);
#endif
    }

    vg_filedialog_destroy(dialog);
    return result;
}
