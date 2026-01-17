// vg_filedialog.c - FileDialog widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

// Platform-specific includes
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>  // For SHGetFolderPath
#include <direct.h>
#define strcasecmp _stricmp
#define stat _stat
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
#include <dirent.h>
#include <unistd.h>
#include <pwd.h>
#include <fnmatch.h>
#include <strings.h>
#endif

//=============================================================================
// Forward Declarations
//=============================================================================

static void filedialog_destroy(vg_widget_t* widget);
static void filedialog_measure(vg_widget_t* widget, float available_width, float available_height);
static void filedialog_paint(vg_widget_t* widget, void* canvas);
static bool filedialog_handle_event(vg_widget_t* widget, vg_event_t* event);

//=============================================================================
// FileDialog VTable
//=============================================================================

static vg_widget_vtable_t g_filedialog_vtable = {
    .destroy = filedialog_destroy,
    .measure = filedialog_measure,
    .arrange = NULL,
    .paint = filedialog_paint,
    .handle_event = filedialog_handle_event,
    .can_focus = NULL,
    .on_focus = NULL
};

//=============================================================================
// Platform Abstraction Layer
//=============================================================================

#ifdef _WIN32

// Windows implementation of pattern matching (simple fnmatch equivalent)
static bool win_match_pattern(const char* pattern, const char* filename) {
    const char* p = pattern;
    const char* f = filename;

    while (*p && *f) {
        if (*p == '*') {
            p++;
            if (!*p) return true;  // Trailing * matches everything
            // Try to match rest of pattern at each position
            while (*f) {
                if (win_match_pattern(p, f)) return true;
                f++;
            }
            return false;
        } else if (*p == '?') {
            p++;
            f++;
        } else {
            // Case-insensitive compare
            char pc = *p, fc = *f;
            if (pc >= 'A' && pc <= 'Z') pc += 32;
            if (fc >= 'A' && fc <= 'Z') fc += 32;
            if (pc != fc) return false;
            p++;
            f++;
        }
    }

    while (*p == '*') p++;  // Skip trailing asterisks
    return !*p && !*f;
}

#endif

static char* get_home_directory(void) {
#ifdef _WIN32
    // Try USERPROFILE first
    const char* userprofile = getenv("USERPROFILE");
    if (userprofile) return _strdup(userprofile);

    // Fall back to HOMEDRIVE + HOMEPATH
    const char* homedrive = getenv("HOMEDRIVE");
    const char* homepath = getenv("HOMEPATH");
    if (homedrive && homepath) {
        size_t len = strlen(homedrive) + strlen(homepath) + 1;
        char* result = malloc(len);
        if (result) {
            strcpy(result, homedrive);
            strcat(result, homepath);
            return result;
        }
    }

    return _strdup("C:\\");
#else
    const char* home = getenv("HOME");
    if (home) return strdup(home);

    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_dir) return strdup(pw->pw_dir);

    return strdup("/");
#endif
}

static char* join_path(const char* dir, const char* file) {
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);

#ifdef _WIN32
    char sep = '\\';
    bool need_sep = (dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/');
#else
    char sep = '/';
    bool need_sep = (dir_len > 0 && dir[dir_len - 1] != '/');
#endif

    char* result = malloc(dir_len + (need_sep ? 1 : 0) + file_len + 1);
    if (!result) return NULL;

    strcpy(result, dir);
    if (need_sep) {
        result[dir_len] = sep;
        result[dir_len + 1] = '\0';
    }
    strcat(result, file);

    return result;
}

static char* get_parent_directory(const char* path) {
    if (!path || !*path) {
#ifdef _WIN32
        return _strdup("C:\\");
#else
        return strdup("/");
#endif
    }

#ifdef _WIN32
    char* result = _strdup(path);
#else
    char* result = strdup(path);
#endif
    if (!result) return NULL;

    // Remove trailing slashes
    size_t len = strlen(result);
#ifdef _WIN32
    while (len > 1 && (result[len - 1] == '\\' || result[len - 1] == '/')) {
        result[--len] = '\0';
    }

    // Find last slash
    char* last_slash = strrchr(result, '\\');
    if (!last_slash) last_slash = strrchr(result, '/');

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

    char* last_slash = strrchr(result, '/');
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

static int compare_entries(const void* a, const void* b) {
    const vg_file_entry_t* ea = *(const vg_file_entry_t**)a;
    const vg_file_entry_t* eb = *(const vg_file_entry_t**)b;

    // Directories first
    if (ea->is_directory && !eb->is_directory) return -1;
    if (!ea->is_directory && eb->is_directory) return 1;

    // Then alphabetically (case-insensitive)
    return strcasecmp(ea->name, eb->name);
}

static bool match_filter(const char* filename, const char* pattern) {
    if (!pattern || !*pattern || strcmp(pattern, "*") == 0 || strcmp(pattern, "*.*") == 0) {
        return true;
    }

    // Pattern can be multiple patterns separated by semicolons
#ifdef _WIN32
    char* patterns = _strdup(pattern);
#else
    char* patterns = strdup(pattern);
#endif
    if (!patterns) return true;

    char* saveptr = NULL;
    char* token = NULL;

#ifdef _WIN32
    token = strtok_s(patterns, ";", &saveptr);
#else
    token = strtok_r(patterns, ";", &saveptr);
#endif

    while (token) {
        // Trim whitespace
        while (*token == ' ') token++;
        char* end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';

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

static void free_entry(vg_file_entry_t* entry) {
    if (entry) {
        free(entry->name);
        free(entry->full_path);
        free(entry);
    }
}

static void clear_entries(vg_filedialog_t* dialog) {
    for (size_t i = 0; i < dialog->entry_count; i++) {
        free_entry(dialog->entries[i]);
    }
    dialog->entry_count = 0;
}

static void load_directory(vg_filedialog_t* dialog, const char* path) {
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

    if (hFind == INVALID_HANDLE_VALUE) return;

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
            if (!match_filter(find_data.cFileName, dialog->filters[dialog->active_filter].pattern)) {
                continue;
            }
        }

        // Create entry
        vg_file_entry_t* fe = calloc(1, sizeof(vg_file_entry_t));
        if (!fe) continue;

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
            vg_file_entry_t** new_entries = realloc(dialog->entries, new_cap * sizeof(vg_file_entry_t*));
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
    DIR* dir = opendir(path);
    if (!dir) return;

    struct dirent* entry;
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
        char* full_path = join_path(path, entry->d_name);
        if (!full_path) continue;

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
        vg_file_entry_t* fe = calloc(1, sizeof(vg_file_entry_t));
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
            vg_file_entry_t** new_entries = realloc(dialog->entries, new_cap * sizeof(vg_file_entry_t*));
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
        qsort(dialog->entries, dialog->entry_count, sizeof(vg_file_entry_t*), compare_entries);
    }

    // Clear selection
    dialog->selection_count = 0;
}

static void select_entry(vg_filedialog_t* dialog, size_t index) {
    if (index >= dialog->entry_count) return;

    if (!dialog->multi_select) {
        dialog->selection_count = 1;
        if (dialog->selection_capacity == 0) {
            dialog->selected_indices = malloc(sizeof(int));
            dialog->selection_capacity = 1;
        }
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
                size_t new_cap = dialog->selection_capacity == 0 ? 8 : dialog->selection_capacity * 2;
                int* new_indices = realloc(dialog->selected_indices, new_cap * sizeof(int));
                if (!new_indices) return;
                dialog->selected_indices = new_indices;
                dialog->selection_capacity = new_cap;
            }
            dialog->selected_indices[dialog->selection_count++] = (int)index;
        }
    }
}

static bool is_selected(vg_filedialog_t* dialog, size_t index) {
    for (size_t i = 0; i < dialog->selection_count; i++) {
        if (dialog->selected_indices[i] == (int)index) return true;
    }
    return false;
}

static void confirm_selection(vg_filedialog_t* dialog) {
    // Free previous results
    if (dialog->selected_files) {
        for (size_t i = 0; i < dialog->selected_file_count; i++) {
            free(dialog->selected_files[i]);
        }
        free(dialog->selected_files);
        dialog->selected_files = NULL;
        dialog->selected_file_count = 0;
    }

    // Build result array
    if (dialog->selection_count > 0) {
        dialog->selected_files = malloc(dialog->selection_count * sizeof(char*));
        if (dialog->selected_files) {
            for (size_t i = 0; i < dialog->selection_count; i++) {
                int idx = dialog->selected_indices[i];
                if (idx >= 0 && (size_t)idx < dialog->entry_count) {
#ifdef _WIN32
                    dialog->selected_files[dialog->selected_file_count++] =
                        _strdup(dialog->entries[idx]->full_path);
#else
                    dialog->selected_files[dialog->selected_file_count++] =
                        strdup(dialog->entries[idx]->full_path);
#endif
                }
            }
        }
    } else if (dialog->mode == VG_FILEDIALOG_SELECT_FOLDER) {
        // In folder mode with no selection, return current directory
        dialog->selected_files = malloc(sizeof(char*));
        if (dialog->selected_files) {
#ifdef _WIN32
            dialog->selected_files[0] = _strdup(dialog->current_path);
#else
            dialog->selected_files[0] = strdup(dialog->current_path);
#endif
            dialog->selected_file_count = 1;
        }
    }

    // Close dialog
    vg_dialog_close(&dialog->base, VG_DIALOG_RESULT_OK);

    // Invoke callback
    if (dialog->on_select && dialog->selected_file_count > 0) {
        dialog->on_select(dialog, dialog->selected_files,
                          dialog->selected_file_count, dialog->user_data);
    }
}

//=============================================================================
// FileDialog Implementation
//=============================================================================

vg_filedialog_t* vg_filedialog_create(vg_filedialog_mode_t mode) {
    vg_filedialog_t* dialog = calloc(1, sizeof(vg_filedialog_t));
    if (!dialog) return NULL;

    // Initialize base dialog
    const char* title = "Open File";
    if (mode == VG_FILEDIALOG_SAVE) title = "Save File";
    else if (mode == VG_FILEDIALOG_SELECT_FOLDER) title = "Select Folder";

    // Initialize base widget
    vg_widget_init(&dialog->base.base, VG_WIDGET_DIALOG, &g_filedialog_vtable);

    vg_theme_t* theme = vg_theme_get_current();

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

static void filedialog_destroy(vg_widget_t* widget) {
    vg_filedialog_t* dialog = (vg_filedialog_t*)widget;

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
    free((void*)dialog->base.title);
    free((void*)dialog->base.message);
}

static void filedialog_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_filedialog_t* dialog = (vg_filedialog_t*)widget;
    (void)available_width;
    (void)available_height;

    widget->measured_width = dialog->base.min_width;
    widget->measured_height = dialog->base.min_height;
}

static void filedialog_paint(vg_widget_t* widget, void* canvas) {
    vg_filedialog_t* dialog = (vg_filedialog_t*)widget;
    vg_theme_t* theme = vg_theme_get_current();

    if (!dialog->base.is_open) return;

    float x = widget->x;
    float y = widget->y;
    float w = widget->width;
    float h = widget->height;

    // Draw modal overlay
    // TODO: Use vgfx primitives

    // Draw dialog background
    (void)theme;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)canvas;

    // Title bar
    float title_height = 35.0f;

    // Bookmarks sidebar
    float sidebar_width = 150.0f;

    // Path bar
    float path_height = 30.0f;

    // File list area
    float list_y = y + title_height + path_height;
    float list_height = h - title_height - path_height - 80.0f; // Leave room for buttons

    // Draw path
    if (dialog->base.font && dialog->current_path) {
        vg_font_draw_text(canvas, dialog->base.font, dialog->base.font_size,
                          x + sidebar_width + 10, y + title_height + 20,
                          dialog->current_path, dialog->base.title_text_color);
    }

    // Draw bookmarks
    float bookmark_y = list_y + 5;
    for (size_t i = 0; i < dialog->bookmark_count && bookmark_y < list_y + list_height; i++) {
        if (dialog->base.font) {
            vg_font_draw_text(canvas, dialog->base.font, dialog->base.font_size,
                              x + 10, bookmark_y + 18,
                              dialog->bookmarks[i].name, theme->colors.fg_primary);
        }
        bookmark_y += 25;
    }

    // Draw file list
    float row_height = 24.0f;
    float file_x = x + sidebar_width + 10;
    float file_y = list_y + 5;

    for (size_t i = 0; i < dialog->entry_count && file_y < list_y + list_height; i++) {
        vg_file_entry_t* entry = dialog->entries[i];

        // Highlight if selected
        uint32_t text_color = theme->colors.fg_primary;
        if (is_selected(dialog, i)) {
            // TODO: Draw selection background
            text_color = theme->colors.fg_primary;
        }

        // Draw icon indicator
        const char* icon = entry->is_directory ? "[D]" : "   ";
        if (dialog->base.font) {
            vg_font_draw_text(canvas, dialog->base.font, dialog->base.font_size,
                              file_x, file_y + 18, icon, theme->colors.fg_secondary);

            vg_font_draw_text(canvas, dialog->base.font, dialog->base.font_size,
                              file_x + 30, file_y + 18, entry->name, text_color);
        }

        file_y += row_height;
    }

    // Draw buttons
    // TODO: Draw OK/Cancel buttons using vgfx primitives
}

static bool filedialog_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_filedialog_t* dialog = (vg_filedialog_t*)widget;

    if (!dialog->base.is_open) return false;

    float title_height = 35.0f;
    float sidebar_width = 150.0f;
    float path_height = 30.0f;
    float list_y = widget->y + title_height + path_height;
    float list_height = widget->height - title_height - path_height - 80.0f;
    float row_height = 24.0f;

    switch (event->type) {
        case VG_EVENT_MOUSE_DOWN: {
            float mx = event->mouse.x;
            float my = event->mouse.y;

            // Check if clicking in file list
            if (mx > widget->x + sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_index = (size_t)((my - list_y - 5) / row_height);
                if (clicked_index < dialog->entry_count) {
                    select_entry(dialog, clicked_index);
                    widget->needs_paint = true;
                    return true;
                }
            }

            // Check if clicking in bookmarks
            if (mx < widget->x + sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_bookmark = (size_t)((my - list_y - 5) / 25.0f);
                if (clicked_bookmark < dialog->bookmark_count) {
                    load_directory(dialog, dialog->bookmarks[clicked_bookmark].path);
                    widget->needs_paint = true;
                    return true;
                }
            }

            return false;
        }

        case VG_EVENT_DOUBLE_CLICK: {
            float mx = event->mouse.x;
            float my = event->mouse.y;

            // Check double-click in file list
            if (mx > widget->x + sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_index = (size_t)((my - list_y - 5) / row_height);
                if (clicked_index < dialog->entry_count) {
                    vg_file_entry_t* entry = dialog->entries[clicked_index];
                    if (entry->is_directory) {
                        // Navigate into directory
                        load_directory(dialog, entry->full_path);
                        widget->needs_paint = true;
                    } else {
                        // Select file and confirm
                        select_entry(dialog, clicked_index);
                        confirm_selection(dialog);
                    }
                    return true;
                }
            }
            return false;
        }

        case VG_EVENT_KEY_DOWN: {
            if (event->key.key == VG_KEY_ESCAPE) {
                vg_dialog_close(&dialog->base, VG_DIALOG_RESULT_CANCEL);
                if (dialog->on_cancel) {
                    dialog->on_cancel(dialog, dialog->user_data);
                }
                return true;
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
                char* parent = get_parent_directory(dialog->current_path);
                if (parent) {
                    load_directory(dialog, parent);
                    free(parent);
                    widget->needs_paint = true;
                }
                return true;
            }

            return false;
        }

        default:
            break;
    }

    return false;
}

//=============================================================================
// FileDialog API
//=============================================================================

void vg_filedialog_destroy(vg_filedialog_t* dialog) {
    if (dialog) {
        filedialog_destroy(&dialog->base.base);
        free(dialog);
    }
}

void vg_filedialog_set_title(vg_filedialog_t* dialog, const char* title) {
    if (!dialog) return;
    free((void*)dialog->base.title);
#ifdef _WIN32
    dialog->base.title = title ? _strdup(title) : NULL;
#else
    dialog->base.title = title ? strdup(title) : NULL;
#endif
}

void vg_filedialog_set_initial_path(vg_filedialog_t* dialog, const char* path) {
    if (!dialog) return;
    free(dialog->current_path);
#ifdef _WIN32
    dialog->current_path = path ? _strdup(path) : get_home_directory();
#else
    dialog->current_path = path ? strdup(path) : get_home_directory();
#endif
}

void vg_filedialog_set_filename(vg_filedialog_t* dialog, const char* filename) {
    if (!dialog) return;
    free(dialog->default_filename);
#ifdef _WIN32
    dialog->default_filename = filename ? _strdup(filename) : NULL;
#else
    dialog->default_filename = filename ? strdup(filename) : NULL;
#endif
}

void vg_filedialog_set_multi_select(vg_filedialog_t* dialog, bool multi) {
    if (dialog) dialog->multi_select = multi;
}

void vg_filedialog_set_show_hidden(vg_filedialog_t* dialog, bool show) {
    if (dialog) dialog->show_hidden = show;
}

void vg_filedialog_set_confirm_overwrite(vg_filedialog_t* dialog, bool confirm) {
    if (dialog) dialog->confirm_overwrite = confirm;
}

void vg_filedialog_add_filter(vg_filedialog_t* dialog, const char* name, const char* pattern) {
    if (!dialog || !name || !pattern) return;

    if (dialog->filter_count >= dialog->filter_capacity) {
        size_t new_cap = dialog->filter_capacity == 0 ? 4 : dialog->filter_capacity * 2;
        vg_file_filter_t* new_filters = realloc(dialog->filters, new_cap * sizeof(vg_file_filter_t));
        if (!new_filters) return;
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

void vg_filedialog_clear_filters(vg_filedialog_t* dialog) {
    if (!dialog) return;

    for (size_t i = 0; i < dialog->filter_count; i++) {
        free(dialog->filters[i].name);
        free(dialog->filters[i].pattern);
    }
    dialog->filter_count = 0;
}

void vg_filedialog_set_default_extension(vg_filedialog_t* dialog, const char* ext) {
    if (!dialog) return;
    free(dialog->default_extension);
#ifdef _WIN32
    dialog->default_extension = ext ? _strdup(ext) : NULL;
#else
    dialog->default_extension = ext ? strdup(ext) : NULL;
#endif
}

void vg_filedialog_add_bookmark(vg_filedialog_t* dialog, const char* name, const char* path) {
    if (!dialog || !name || !path) return;

    if (dialog->bookmark_count >= dialog->bookmark_capacity) {
        size_t new_cap = dialog->bookmark_capacity == 0 ? 8 : dialog->bookmark_capacity * 2;
        vg_bookmark_t* new_bookmarks = realloc(dialog->bookmarks, new_cap * sizeof(vg_bookmark_t));
        if (!new_bookmarks) return;
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

void vg_filedialog_add_default_bookmarks(vg_filedialog_t* dialog) {
    if (!dialog) return;

    char* home = get_home_directory();
    if (home) {
        vg_filedialog_add_bookmark(dialog, "Home", home);

        char* desktop = join_path(home, "Desktop");
        if (desktop) {
            struct stat st;
            if (stat(desktop, &st) == 0 && S_ISDIR(st.st_mode)) {
                vg_filedialog_add_bookmark(dialog, "Desktop", desktop);
            }
            free(desktop);
        }

        char* documents = join_path(home, "Documents");
        if (documents) {
            struct stat st;
            if (stat(documents, &st) == 0 && S_ISDIR(st.st_mode)) {
                vg_filedialog_add_bookmark(dialog, "Documents", documents);
            }
            free(documents);
        }

        char* downloads = join_path(home, "Downloads");
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

void vg_filedialog_clear_bookmarks(vg_filedialog_t* dialog) {
    if (!dialog) return;

    for (size_t i = 0; i < dialog->bookmark_count; i++) {
        free(dialog->bookmarks[i].name);
        free(dialog->bookmarks[i].path);
    }
    dialog->bookmark_count = 0;
}

void vg_filedialog_show(vg_filedialog_t* dialog) {
    if (!dialog) return;

    // Load current directory
    load_directory(dialog, dialog->current_path);

    // Show dialog
    dialog->base.is_open = true;
    dialog->base.result = VG_DIALOG_RESULT_NONE;
    dialog->base.base.visible = true;
    dialog->base.base.needs_paint = true;
}

char** vg_filedialog_get_selected_paths(vg_filedialog_t* dialog, size_t* count) {
    if (!dialog) {
        if (count) *count = 0;
        return NULL;
    }

    if (count) *count = dialog->selected_file_count;
    return dialog->selected_files;
}

char* vg_filedialog_get_selected_path(vg_filedialog_t* dialog) {
    if (!dialog || dialog->selected_file_count == 0) return NULL;
    return dialog->selected_files[0];
}

void vg_filedialog_set_on_select(vg_filedialog_t* dialog,
    void (*callback)(vg_filedialog_t*, char**, size_t, void*), void* user_data) {
    if (!dialog) return;
    dialog->on_select = callback;
    dialog->user_data = user_data;
}

void vg_filedialog_set_on_cancel(vg_filedialog_t* dialog,
    void (*callback)(vg_filedialog_t*, void*), void* user_data) {
    if (!dialog) return;
    dialog->on_cancel = callback;
    dialog->user_data = user_data;
}

//=============================================================================
// Convenience Functions
//=============================================================================

char* vg_filedialog_open_file(const char* title, const char* initial_path,
    const char* filter_name, const char* filter_pattern) {
    vg_filedialog_t* dialog = vg_filedialog_create(VG_FILEDIALOG_OPEN);
    if (!dialog) return NULL;

    if (title) vg_filedialog_set_title(dialog, title);
    if (initial_path) vg_filedialog_set_initial_path(dialog, initial_path);
    if (filter_name && filter_pattern) {
        vg_filedialog_add_filter(dialog, filter_name, filter_pattern);
    }
    vg_filedialog_add_default_bookmarks(dialog);

    vg_filedialog_show(dialog);

    // Note: In a real implementation, this would block and wait for the dialog
    // For now, we just return NULL - proper modal dialog requires event loop integration
    char* result = NULL;
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

char* vg_filedialog_save_file(const char* title, const char* initial_path,
    const char* default_name, const char* filter_name, const char* filter_pattern) {
    vg_filedialog_t* dialog = vg_filedialog_create(VG_FILEDIALOG_SAVE);
    if (!dialog) return NULL;

    if (title) vg_filedialog_set_title(dialog, title);
    if (initial_path) vg_filedialog_set_initial_path(dialog, initial_path);
    if (default_name) vg_filedialog_set_filename(dialog, default_name);
    if (filter_name && filter_pattern) {
        vg_filedialog_add_filter(dialog, filter_name, filter_pattern);
    }
    vg_filedialog_add_default_bookmarks(dialog);

    vg_filedialog_show(dialog);

    char* result = NULL;
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

char* vg_filedialog_select_folder(const char* title, const char* initial_path) {
    vg_filedialog_t* dialog = vg_filedialog_create(VG_FILEDIALOG_SELECT_FOLDER);
    if (!dialog) return NULL;

    if (title) vg_filedialog_set_title(dialog, title);
    if (initial_path) vg_filedialog_set_initial_path(dialog, initial_path);
    vg_filedialog_add_default_bookmarks(dialog);

    vg_filedialog_show(dialog);

    char* result = NULL;
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
