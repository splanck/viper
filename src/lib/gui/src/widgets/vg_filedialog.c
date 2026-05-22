//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_filedialog.c
// Purpose: File-chooser dialog widget — supports open, save, and folder-select
//          modes with a sidebar of bookmarks, a file list, filters, and an inline
//          save-mode filename field with UTF-8 cursor editing.
// Key invariants:
//   - entries[] is a heap-allocated pointer array sorted directories-first then
//     alphabetically after each load_directory call.
//   - Scrolling uses float offsets (file_scroll_y, bookmark_scroll_y) clamped by
//     filedialog_clamp_scrolls before every paint and event pass.
//   - filename_cursor_pos tracks a UTF-8 byte offset; prev/next boundary helpers
//     skip continuation bytes (mask 0xC0 == 0x80) for correct multi-byte navigation.
//   - confirm_selection navigates into directories rather than selecting them in
//     OPEN mode when selection_count == 1 and the selected item is a directory.
//   - Default extension is appended in SAVE mode only when the filename has no
//     dot after the last path separator.
//   - selected_files[] is owned by the widget and freed in filedialog_destroy.
// Ownership/Lifetime:
//   - entries[], filters[], bookmarks[], selected_files[], current_path,
//     default_filename, and default_extension are all heap-allocated and freed
//     in filedialog_destroy.
//   - The widget extends vg_dialog_t; base.base is the actual widget header.
// Links: lib/gui/include/vg_ide_widgets.h,
//        lib/gui/include/vg_theme.h,
//        lib/gui/include/vg_event.h
//
//===----------------------------------------------------------------------===//
#include "../../../graphics/include/vgfx.h"
#include "../../include/vg_event.h"
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include <ctype.h>
#include <stdint.h>
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
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
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

static vg_filedialog_modal_runner_t g_modal_runner = NULL;
static void *g_modal_runner_user_data = NULL;

static char *filedialog_strdup(const char *text) {
    if (!text)
        return NULL;
#ifdef _WIN32
    return _strdup(text);
#else
    return strdup(text);
#endif
}

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

/// @brief Windows-only case-insensitive glob match with * and ? wildcards.
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

/// @brief Return a heap-allocated string holding the current user's home directory path.
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

/// @brief Heap-allocate the concatenation of dir and file with a platform path separator.
static char *join_path(const char *dir, const char *file) {
    if (!dir || !file)
        return NULL;
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);

#ifdef _WIN32
    char sep = '\\';
    bool need_sep = (dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/');
#else
    char sep = '/';
    bool need_sep = (dir_len > 0 && dir[dir_len - 1] != '/');
#endif

    size_t sep_len = need_sep ? 1u : 0u;
    if (dir_len > SIZE_MAX - sep_len || dir_len + sep_len > SIZE_MAX - file_len ||
        dir_len + sep_len + file_len > SIZE_MAX - 1u) {
        return NULL;
    }
    size_t total = dir_len + sep_len + file_len;
    char *result = malloc(total + 1u);
    if (!result)
        return NULL;

    memcpy(result, dir, dir_len);
    size_t offset = dir_len;
    if (need_sep) {
        result[offset++] = sep;
    }
    memcpy(result + offset, file, file_len);
    result[total] = '\0';

    return result;
}

/// @brief Return a heap-allocated string containing the parent directory of path.
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

/// @brief Fill (x, y) with the screen-space origin of widget's parent, or (0, 0) if none.
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

/// @brief Return the height of the bottom action area, adding SAVE_EXTRA_HEIGHT in save mode.
static float filedialog_bottom_height(const vg_filedialog_t *dialog) {
    return FILEDIALOG_BOTTOM_HEIGHT +
           (dialog && dialog->mode == VG_FILEDIALOG_SAVE ? FILEDIALOG_SAVE_EXTRA_HEIGHT : 0.0f);
}

/// @brief Return the label for the accept button ("Open", "Save", or "Select").
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

/// @brief Return true if filename contains a dot after the last path separator.
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

/// @brief Replace default_filename and reset the cursor to the end of the new name.
static void filedialog_set_default_filename(vg_filedialog_t *dialog, const char *filename) {
    if (!dialog)
        return;
    char *copy = filedialog_strdup(filename);
    if (filename && !copy)
        return;
    free(dialog->default_filename);
    dialog->default_filename = copy;
    dialog->filename_cursor_pos = dialog->default_filename ? strlen(dialog->default_filename) : 0;
}

/// @brief Step the cursor back one UTF-8 codepoint, skipping continuation bytes.
static size_t filedialog_prev_codepoint_boundary(const char *text, size_t cursor) {
    size_t len = 0;
    if (!text)
        return 0;
    len = strlen(text);
    if (cursor > len)
        cursor = len;
    if (cursor == 0)
        return 0;
    do {
        cursor--;
    } while (cursor > 0 && (((unsigned char)text[cursor] & 0xC0) == 0x80));
    return cursor;
}

/// @brief Step the cursor forward one UTF-8 codepoint, skipping continuation bytes.
static size_t filedialog_next_codepoint_boundary(const char *text, size_t cursor) {
    size_t len = 0;
    if (!text)
        return 0;
    len = strlen(text);
    if (cursor >= len)
        return len;
    cursor++;
    while (cursor < len && (((unsigned char)text[cursor] & 0xC0) == 0x80)) {
        cursor++;
    }
    return cursor;
}

/// @brief Clamp filename_cursor_pos to the current length of default_filename.
static void filedialog_sync_filename_cursor(vg_filedialog_t *dialog) {
    size_t len = 0;
    if (!dialog)
        return;
    len = dialog->default_filename ? strlen(dialog->default_filename) : 0;
    if (dialog->filename_cursor_pos > len)
        dialog->filename_cursor_pos = len;
}

/// @brief Delete the codepoint immediately before the cursor (Backspace behaviour).
static void filedialog_delete_last_codepoint(vg_filedialog_t *dialog) {
    char *text = NULL;
    size_t cursor = 0;
    size_t prev = 0;
    size_t len = 0;

    if (!dialog || !dialog->default_filename)
        return;

    text = dialog->default_filename;
    filedialog_sync_filename_cursor(dialog);
    cursor = dialog->filename_cursor_pos;
    if (cursor == 0)
        return;

    prev = filedialog_prev_codepoint_boundary(text, cursor);
    len = strlen(text);
    memmove(text + prev, text + cursor, len - cursor + 1);
    dialog->filename_cursor_pos = prev;
}

/// @brief Delete the codepoint at the cursor position (Delete key behaviour).
static void filedialog_delete_codepoint_at_cursor(vg_filedialog_t *dialog) {
    char *text = NULL;
    size_t cursor = 0;
    size_t next = 0;
    size_t len = 0;

    if (!dialog || !dialog->default_filename)
        return;

    text = dialog->default_filename;
    filedialog_sync_filename_cursor(dialog);
    cursor = dialog->filename_cursor_pos;
    len = strlen(text);
    if (cursor >= len)
        return;

    next = filedialog_next_codepoint_boundary(text, cursor);
    memmove(text + cursor, text + next, len - next + 1);
}

/// @brief Insert a UTF-8 encoded codepoint at the current cursor position.
static void filedialog_append_codepoint(vg_filedialog_t *dialog, uint32_t codepoint) {
    char encoded[5] = {0};
    size_t encoded_len = 0;
    size_t old_len = 0;
    size_t insert_at = 0;
    char *new_name = NULL;

    if (!dialog || codepoint < 0x20 || codepoint == 0x7F || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF))
        return;

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

    old_len = dialog->default_filename ? strlen(dialog->default_filename) : 0;
    insert_at = dialog->filename_cursor_pos <= old_len ? dialog->filename_cursor_pos : old_len;
    new_name = realloc(dialog->default_filename, old_len + encoded_len + 1);
    if (!new_name)
        return;
    dialog->default_filename = new_name;
    memmove(dialog->default_filename + insert_at + encoded_len,
            dialog->default_filename + insert_at,
            old_len - insert_at + 1);
    memcpy(dialog->default_filename + insert_at, encoded, encoded_len);
    dialog->filename_cursor_pos = insert_at + encoded_len;
}

/// @brief Return the usable height of the scrollable list view (list_height minus margins).
static float filedialog_list_view_height(float list_height) {
    float view_h = list_height - 10.0f;
    return view_h > 0.0f ? view_h : 0.0f;
}

/// @brief Compute the maximum scroll offset given item_count rows, row_height, and view_height.
static float filedialog_max_scroll(size_t item_count, float row_height, float view_height) {
    float content_h = (float)item_count * row_height;
    float max_scroll = content_h - view_height;
    return max_scroll > 0.0f ? max_scroll : 0.0f;
}

/// @brief Clamp both file_scroll_y and bookmark_scroll_y to their valid ranges.
static void filedialog_clamp_scrolls(vg_filedialog_t *dialog, float list_height) {
    float view_h = filedialog_list_view_height(list_height);
    float max_file_scroll = 0.0f;
    float max_bookmark_scroll = 0.0f;

    if (!dialog)
        return;

    max_file_scroll = filedialog_max_scroll(dialog->entry_count, FILEDIALOG_ROW_HEIGHT, view_h);
    max_bookmark_scroll =
        filedialog_max_scroll(dialog->bookmark_count, FILEDIALOG_BOOKMARK_HEIGHT, view_h);

    if (dialog->file_scroll_y < 0.0f)
        dialog->file_scroll_y = 0.0f;
    if (dialog->file_scroll_y > max_file_scroll)
        dialog->file_scroll_y = max_file_scroll;
    if (dialog->bookmark_scroll_y < 0.0f)
        dialog->bookmark_scroll_y = 0.0f;
    if (dialog->bookmark_scroll_y > max_bookmark_scroll)
        dialog->bookmark_scroll_y = max_bookmark_scroll;
}

/// @brief Adjust file_scroll_y so the first selected item is within the visible list area.
static void filedialog_scroll_selection_into_view(vg_filedialog_t *dialog, float list_height) {
    float view_h = filedialog_list_view_height(list_height);
    float item_top = 0.0f;
    float item_bottom = 0.0f;

    if (!dialog || dialog->selection_count == 0)
        return;

    item_top = (float)dialog->selected_indices[0] * FILEDIALOG_ROW_HEIGHT;
    item_bottom = item_top + FILEDIALOG_ROW_HEIGHT;
    if (item_top < dialog->file_scroll_y)
        dialog->file_scroll_y = item_top;
    else if (item_bottom > dialog->file_scroll_y + view_h)
        dialog->file_scroll_y = item_bottom - view_h;
    if (dialog->file_scroll_y < 0.0f)
        dialog->file_scroll_y = 0.0f;
}

/// @brief Compute the draw origin X for text, right-aligning it when align_end and overflow.
static float filedialog_text_origin(vg_font_t *font,
                                    float font_size,
                                    const char *text,
                                    float base_x,
                                    float available_w,
                                    bool align_end) {
    vg_text_metrics_t metrics = {0};

    if (!font || !text || available_w <= 0.0f)
        return base_x;

    vg_font_measure_text(font, font_size, text, &metrics);
    if (align_end && metrics.width > available_w)
        return base_x + available_w - metrics.width;
    return base_x;
}

/// @brief Draw text clipped to a rectangle, then clear the clip.
static void filedialog_draw_clipped_text(void *canvas,
                                         vg_font_t *font,
                                         float font_size,
                                         float clip_x,
                                         float clip_y,
                                         float clip_w,
                                         float clip_h,
                                         float text_x,
                                         float text_y,
                                         const char *text,
                                         uint32_t color) {
    vgfx_window_t win = (vgfx_window_t)canvas;
    if (!canvas || !font || !text || clip_w <= 0.0f || clip_h <= 0.0f)
        return;
    vgfx_set_clip(win, (int32_t)clip_x, (int32_t)clip_y, (int32_t)clip_w, (int32_t)clip_h);
    vg_font_draw_text(canvas, font, font_size, text_x, text_y, text, color);
    vgfx_clear_clip(win);
}

/// @brief Convert a local Y coordinate plus scroll offset to a list item index.
static size_t filedialog_index_from_scroll(float local_y,
                                           float scroll_y,
                                           float row_height,
                                           size_t item_count) {
    size_t index = 0;
    if (local_y < 0.0f || row_height <= 0.0f)
        return SIZE_MAX;
    index = (size_t)((local_y + scroll_y) / row_height);
    return index < item_count ? index : SIZE_MAX;
}

/// @brief Return true if path is absolute (starts with '/' on POSIX or drive letter on Windows).
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

/// @brief qsort comparator — directories before files, then case-insensitive alphabetical.
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

/// @brief Return true if filename matches any semicolon-separated glob in pattern.
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
        size_t token_len = strlen(token);
        while (token_len > 0 && token[token_len - 1] == ' ')
            token[--token_len] = '\0';
        if (token_len == 0) {
#ifdef _WIN32
            token = strtok_s(NULL, ";", &saveptr);
#else
            token = strtok_r(NULL, ";", &saveptr);
#endif
            continue;
        }

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

/// @brief Free the name, full_path, and entry struct for a single file entry.
static void free_entry(vg_file_entry_t *entry) {
    if (entry) {
        free(entry->name);
        free(entry->full_path);
        free(entry);
    }
}

/// @brief Free all entries in dialog->entries[] and reset entry_count to zero.
static void clear_entries(vg_filedialog_t *dialog) {
    for (size_t i = 0; i < dialog->entry_count; i++) {
        free_entry(dialog->entries[i]);
    }
    dialog->entry_count = 0;
}

/// @brief Load the directory at path into dialog->entries[], sorted and filtered.
static void load_directory(vg_filedialog_t *dialog, const char *path) {
    if (!dialog || !path)
        return;

    char *new_current_path = filedialog_strdup(path);
    if (!new_current_path)
        return;

#ifdef _WIN32
    // Windows directory iteration using FindFirstFile/FindNextFile
    char *search_path = join_path(path, "*");
    if (!search_path) {
        free(new_current_path);
        return;
    }

    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    free(search_path);

    if (hFind == INVALID_HANDLE_VALUE) {
        free(new_current_path);
        return;
    }

    clear_entries(dialog);
    free(dialog->current_path);
    dialog->current_path = new_current_path;

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
        if (!fe->name || !fe->full_path) {
            free_entry(fe);
            continue;
        }
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
            if (dialog->entry_capacity > SIZE_MAX / (2u * sizeof(vg_file_entry_t *))) {
                free_entry(fe);
                continue;
            }
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
    if (!dir) {
        free(new_current_path);
        return;
    }

    clear_entries(dialog);
    free(dialog->current_path);
    dialog->current_path = new_current_path;

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
        if (!fe->name || !fe->full_path) {
            free_entry(fe);
            continue;
        }
        fe->is_directory = is_dir;
        fe->size = S_ISREG(st.st_mode) && st.st_size > 0 ? (uint64_t)st.st_size : 0u;
        fe->modified_time = st.st_mtime;

        // Add to array
        if (dialog->entry_count >= dialog->entry_capacity) {
            if (dialog->entry_capacity > SIZE_MAX / (2u * sizeof(vg_file_entry_t *))) {
                free_entry(fe);
                continue;
            }
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
    dialog->file_scroll_y = 0.0f;
    dialog->bookmark_scroll_y = 0.0f;
}

/// @brief Set or toggle selection for the entry at index; handles multi-select mode.
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

/// @brief Return true if the entry at index is in the current selection.
static bool is_selected(vg_filedialog_t *dialog, size_t index) {
    for (size_t i = 0; i < dialog->selection_count; i++) {
        if (dialog->selected_indices[i] == (int)index)
            return true;
    }
    return false;
}

/// @brief Validate and commit the selection, populating selected_files[] and closing the dialog.
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
                char *path = NULL;
#ifdef _WIN32
                path = _strdup(dialog->entries[idx]->full_path);
#else
                path = strdup(dialog->entries[idx]->full_path);
#endif
                if (!path) {
                    for (size_t j = 0; j < dialog->selected_file_count; j++)
                        free(dialog->selected_files[j]);
                    free(dialog->selected_files);
                    dialog->selected_files = NULL;
                    dialog->selected_file_count = 0;
                    return;
                }
                dialog->selected_files[dialog->selected_file_count++] = path;
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
            if (!dialog->selected_files[0]) {
                free(dialog->selected_files);
                dialog->selected_files = NULL;
                dialog->selected_file_count = 0;
                return;
            }
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

/// @brief Create a file dialog for the given mode with default theme colours.
///
/// @details Default title is "Open File", "Save File", or "Select Folder" based on mode.
///          The initial directory is the user's home directory.  Attach a font to
///          base.font with vg_dialog_set_font before calling vg_filedialog_show.
///
/// @param mode VG_FILEDIALOG_OPEN, VG_FILEDIALOG_SAVE, or VG_FILEDIALOG_SELECT_FOLDER.
/// @return Newly allocated vg_filedialog_t, or NULL on allocation failure.
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
    dialog->base.title = filedialog_strdup(title);
    if (!dialog->base.title) {
        vg_widget_destroy(&dialog->base.base);
        return NULL;
    }
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
    if (!dialog->current_path) {
#ifdef _WIN32
        dialog->current_path = _strdup(".");
#else
        dialog->current_path = strdup(".");
#endif
    }
    if (!dialog->current_path) {
        vg_widget_destroy(&dialog->base.base);
        return NULL;
    }
    dialog->show_hidden = false;
    dialog->confirm_overwrite = true;
    dialog->multi_select = (mode == VG_FILEDIALOG_OPEN);

    // Set default size
    dialog->base.base.width = 700;
    dialog->base.base.height = 500;

    return dialog;
}

/// @brief VTable destroy: releases input capture and modal root if held, then frees entries, selections, filters, bookmarks, string fields, and result arrays.
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

/// @brief VTable measure: sizes the dialog to its configured minimum width and height (full measure is handled by the dialog's fixed-size layout constants).
static void filedialog_measure(vg_widget_t *widget, float available_width, float available_height) {
    vg_filedialog_t *dialog = (vg_filedialog_t *)widget;
    (void)available_width;
    (void)available_height;

    widget->measured_width = dialog->base.min_width;
    widget->measured_height = dialog->base.min_height;
}

/// @brief VTable paint: renders the modal backdrop, title bar, sidebar bookmarks, file entry grid, filename input row, filter selector, and action buttons.
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
    float bookmark_view_h = filedialog_list_view_height(list_height);
    float file_view_h = filedialog_list_view_height(list_height);
    size_t first_bookmark = 0;
    size_t first_entry = 0;
    float bookmark_offset_y = 0.0f;
    float file_offset_y = 0.0f;

    filedialog_clamp_scrolls(dialog, list_height);
    first_bookmark = (size_t)(dialog->bookmark_scroll_y / FILEDIALOG_BOOKMARK_HEIGHT);
    bookmark_offset_y =
        dialog->bookmark_scroll_y - (float)first_bookmark * FILEDIALOG_BOOKMARK_HEIGHT;
    first_entry = (size_t)(dialog->file_scroll_y / FILEDIALOG_ROW_HEIGHT);
    file_offset_y = dialog->file_scroll_y - (float)first_entry * FILEDIALOG_ROW_HEIGHT;

    // Draw modal overlay (dark semi-transparent background behind dialog)
    int32_t win_w = 0, win_h = 0;
    if (vgfx_get_size(win, &win_w, &win_h)) {
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
        vg_font_metrics_t path_metrics = {0};
        float path_clip_x = x + sidebar_width + 10.0f;
        float path_clip_y = y + title_height + 2.0f;
        float path_clip_w = w - sidebar_width - FILEDIALOG_CLOSE_BUTTON_SIZE - 30.0f;
        float path_clip_h = path_height - 4.0f;
        float path_baseline = 0.0f;
        float path_text_x = 0.0f;

        vg_font_get_metrics(dialog->base.font, dialog->base.font_size, &path_metrics);
        path_baseline = y + title_height + (path_height - dialog->base.font_size) * 0.5f +
                        path_metrics.ascent;
        path_text_x = filedialog_text_origin(dialog->base.font,
                                             dialog->base.font_size,
                                             dialog->current_path,
                                             path_clip_x,
                                             path_clip_w,
                                             true);
        filedialog_draw_clipped_text(canvas,
                                     dialog->base.font,
                                     dialog->base.font_size,
                                     path_clip_x,
                                     path_clip_y,
                                     path_clip_w,
                                     path_clip_h,
                                     path_text_x,
                                     path_baseline,
                                     dialog->current_path,
                                     dialog->base.title_text_color);
    }

    // Draw bookmarks
    float bookmark_y = list_y + 5.0f - bookmark_offset_y;
    vgfx_set_clip(win,
                  (int32_t)x,
                  (int32_t)list_y,
                  (int32_t)sidebar_width,
                  (int32_t)(bookmark_view_h > 0.0f ? bookmark_view_h : 0.0f));
    for (size_t i = first_bookmark; i < dialog->bookmark_count && bookmark_y < list_y + list_height;
         i++) {
        if (dialog->base.font) {
            if (bookmark_y + FILEDIALOG_BOOKMARK_HEIGHT >= list_y) {
                filedialog_draw_clipped_text(canvas,
                                             dialog->base.font,
                                             dialog->base.font_size,
                                             x + 6.0f,
                                             bookmark_y,
                                             sidebar_width - 12.0f,
                                             FILEDIALOG_BOOKMARK_HEIGHT,
                                             x + 10.0f,
                                             bookmark_y + 18.0f,
                                             dialog->bookmarks[i].name,
                                             theme->colors.fg_primary);
            }
        }
        bookmark_y += FILEDIALOG_BOOKMARK_HEIGHT;
    }
    vgfx_clear_clip(win);

    // Draw file list
    float file_x = list_x + 10.0f;
    float file_y = list_y + 5.0f - file_offset_y;
    vgfx_set_clip(win,
                  (int32_t)list_x,
                  (int32_t)list_y,
                  (int32_t)list_width,
                  (int32_t)(list_height > 0.0f ? list_height : 0.0f));

    for (size_t i = first_entry; i < dialog->entry_count && file_y < list_y + list_height; i++) {
        vg_file_entry_t *entry = dialog->entries[i];

        // Highlight if selected
        uint32_t text_color = theme->colors.fg_primary;
        if (file_y + FILEDIALOG_ROW_HEIGHT < list_y) {
            file_y += FILEDIALOG_ROW_HEIGHT;
            continue;
        }
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

            filedialog_draw_clipped_text(canvas,
                                         dialog->base.font,
                                         dialog->base.font_size,
                                         file_x + 28.0f,
                                         file_y,
                                         list_width - 42.0f,
                                         FILEDIALOG_ROW_HEIGHT,
                                         file_x + 30.0f,
                                         file_y + 18.0f,
                                         entry->name,
                                         text_color);
        }

        file_y += FILEDIALOG_ROW_HEIGHT;
    }
    vgfx_clear_clip(win);

    if (dialog->entry_count > 0 && file_view_h > 0.0f) {
        float max_file_scroll =
            filedialog_max_scroll(dialog->entry_count, FILEDIALOG_ROW_HEIGHT, file_view_h);
        if (max_file_scroll > 0.0f) {
            float track_h = file_view_h;
            float content_h = (float)dialog->entry_count * FILEDIALOG_ROW_HEIGHT;
            float thumb_h = track_h * (file_view_h / content_h);
            float thumb_y = list_y + (track_h - thumb_h) * (dialog->file_scroll_y / max_file_scroll);
            if (thumb_h < 16.0f)
                thumb_h = 16.0f;
            vgfx_fill_rect(win,
                           (int32_t)(x + w - 12.0f),
                           (int32_t)list_y,
                           6,
                           (int32_t)track_h,
                           theme->colors.bg_tertiary);
            vgfx_fill_rect(win,
                           (int32_t)(x + w - 12.0f),
                           (int32_t)thumb_y,
                           6,
                           (int32_t)thumb_h,
                           theme->colors.accent_primary);
        }
    }

    if (dialog->bookmark_count > 0 && bookmark_view_h > 0.0f) {
        float max_bookmark_scroll = filedialog_max_scroll(
            dialog->bookmark_count, FILEDIALOG_BOOKMARK_HEIGHT, bookmark_view_h);
        if (max_bookmark_scroll > 0.0f) {
            float track_h = bookmark_view_h;
            float content_h = (float)dialog->bookmark_count * FILEDIALOG_BOOKMARK_HEIGHT;
            float thumb_h = track_h * (bookmark_view_h / content_h);
            float thumb_y =
                list_y + (track_h - thumb_h) * (dialog->bookmark_scroll_y / max_bookmark_scroll);
            if (thumb_h < 16.0f)
                thumb_h = 16.0f;
            vgfx_fill_rect(win,
                           (int32_t)(x + sidebar_width - 8.0f),
                           (int32_t)list_y,
                           4,
                           (int32_t)track_h,
                           theme->colors.bg_tertiary);
            vgfx_fill_rect(win,
                           (int32_t)(x + sidebar_width - 8.0f),
                           (int32_t)thumb_y,
                           4,
                           (int32_t)thumb_h,
                           theme->colors.border_focus);
        }
    }

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
            vg_font_metrics_t name_metrics = {0};
            float clip_x = field_x + 6.0f;
            float clip_y = field_y + 2.0f;
            float clip_w = field_w - 12.0f;
            float clip_h = FILEDIALOG_FILENAME_HEIGHT - 4.0f;
            float baseline_y = 0.0f;
            float draw_x = field_x + 8.0f;
            const char *name_text =
                (dialog->default_filename && dialog->default_filename[0]) ? dialog->default_filename
                                                                          : "File name";
            uint32_t name_color = (dialog->default_filename && dialog->default_filename[0])
                                      ? theme->colors.fg_primary
                                      : theme->colors.fg_placeholder;

            vg_font_get_metrics(dialog->base.font, dialog->base.font_size, &name_metrics);
            baseline_y = field_y + (FILEDIALOG_FILENAME_HEIGHT - dialog->base.font_size) * 0.5f +
                         name_metrics.ascent;

            if (dialog->default_filename && dialog->default_filename[0]) {
                vg_text_metrics_t prefix_metrics = {0};
                size_t cursor_len = dialog->filename_cursor_pos;
                char *prefix = NULL;
                filedialog_sync_filename_cursor(dialog);
                cursor_len = dialog->filename_cursor_pos;
                prefix = malloc(cursor_len + 1);
                if (prefix) {
                    memcpy(prefix, dialog->default_filename, cursor_len);
                    prefix[cursor_len] = '\0';
                    vg_font_measure_text(
                        dialog->base.font, dialog->base.font_size, prefix, &prefix_metrics);
                    free(prefix);
                    if (prefix_metrics.width > clip_w - 4.0f) {
                        draw_x -= prefix_metrics.width - (clip_w - 4.0f);
                    }
                }
            }

            filedialog_draw_clipped_text(canvas,
                                         dialog->base.font,
                                         dialog->base.font_size,
                                         clip_x,
                                         clip_y,
                                         clip_w,
                                         clip_h,
                                         draw_x,
                                         baseline_y,
                                         name_text,
                                         name_color);

            if (dialog->filename_active && dialog->default_filename) {
                vg_text_metrics_t cursor_metrics = {0};
                size_t cursor_len = dialog->filename_cursor_pos;
                char *prefix = malloc(cursor_len + 1);
                if (prefix) {
                    memcpy(prefix, dialog->default_filename, cursor_len);
                    prefix[cursor_len] = '\0';
                    vg_font_measure_text(
                        dialog->base.font, dialog->base.font_size, prefix, &cursor_metrics);
                    free(prefix);
                    vgfx_fill_rect(win,
                                   (int32_t)(draw_x + cursor_metrics.width),
                                   (int32_t)(field_y + 6.0f),
                                   1,
                                   (int32_t)(FILEDIALOG_FILENAME_HEIGHT - 12.0f),
                                   theme->colors.border_focus);
                }
            }
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

/// @brief VTable handle_event: routes mouse, key, and scroll events to title-drag, close-button, sidebar, entry-list navigation, filename editing, and OK/Cancel handling.
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

        case VG_EVENT_MOUSE_WHEEL: {
            float mx = event->wheel.screen_x - widget->x;
            float my = event->wheel.screen_y - widget->y;
            if (mx > sidebar_width && my > list_y && my < list_y + list_height) {
                dialog->file_scroll_y +=
                    FILEDIALOG_ROW_HEIGHT * (event->wheel.delta_y > 0.0f ? -3.0f : 3.0f);
                filedialog_clamp_scrolls(dialog, list_height);
                widget->needs_paint = true;
                return true;
            }
            if (mx >= 0.0f && mx < sidebar_width && my > list_y && my < list_y + list_height) {
                dialog->bookmark_scroll_y +=
                    FILEDIALOG_BOOKMARK_HEIGHT * (event->wheel.delta_y > 0.0f ? -3.0f : 3.0f);
                filedialog_clamp_scrolls(dialog, list_height);
                widget->needs_paint = true;
                return true;
            }
            return dialog->base.modal;
        }

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
                if (dialog->filename_active) {
                    filedialog_sync_filename_cursor(dialog);
                    dialog->filename_cursor_pos =
                        dialog->default_filename ? strlen(dialog->default_filename) : 0;
                }
                if (dialog->filename_active)
                    return true;
            } else {
                dialog->filename_active = false;
            }

            // Check if clicking in file list
            if (mx > sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_index = filedialog_index_from_scroll(
                    my - list_y - 5.0f, dialog->file_scroll_y, FILEDIALOG_ROW_HEIGHT, dialog->entry_count);
                if (clicked_index < dialog->entry_count) {
                    select_entry(dialog, clicked_index);
                    if (dialog->mode == VG_FILEDIALOG_SAVE && !dialog->entries[clicked_index]->is_directory)
                        filedialog_set_default_filename(dialog, dialog->entries[clicked_index]->name);
                    filedialog_scroll_selection_into_view(dialog, list_height);
                    widget->needs_paint = true;
                    return true;
                }
            }

            // Check if clicking in bookmarks
            if (mx < sidebar_width && my > list_y && my < list_y + list_height) {
                size_t clicked_bookmark = filedialog_index_from_scroll(my - list_y - 5.0f,
                                                                       dialog->bookmark_scroll_y,
                                                                       FILEDIALOG_BOOKMARK_HEIGHT,
                                                                       dialog->bookmark_count);
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
                size_t clicked_index = filedialog_index_from_scroll(
                    my - list_y - 5.0f, dialog->file_scroll_y, FILEDIALOG_ROW_HEIGHT, dialog->entry_count);
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
                if (event->key.key == VG_KEY_BACKSPACE) {
                    filedialog_delete_last_codepoint(dialog);
                    widget->needs_paint = true;
                    return true;
                }
                if (event->key.key == VG_KEY_DELETE) {
                    filedialog_delete_codepoint_at_cursor(dialog);
                    widget->needs_paint = true;
                    return true;
                }
                if (event->key.key == VG_KEY_LEFT) {
                    dialog->filename_cursor_pos =
                        filedialog_prev_codepoint_boundary(dialog->default_filename,
                                                           dialog->filename_cursor_pos);
                    widget->needs_paint = true;
                    return true;
                }
                if (event->key.key == VG_KEY_RIGHT) {
                    dialog->filename_cursor_pos =
                        filedialog_next_codepoint_boundary(dialog->default_filename,
                                                           dialog->filename_cursor_pos);
                    widget->needs_paint = true;
                    return true;
                }
                if (event->key.key == VG_KEY_HOME) {
                    dialog->filename_cursor_pos = 0;
                    widget->needs_paint = true;
                    return true;
                }
                if (event->key.key == VG_KEY_END) {
                    dialog->filename_cursor_pos =
                        dialog->default_filename ? strlen(dialog->default_filename) : 0;
                    widget->needs_paint = true;
                    return true;
                }
            }

            if ((event->key.key == VG_KEY_UP || event->key.key == VG_KEY_DOWN) &&
                dialog->entry_count > 0) {
                int current = dialog->selection_count > 0 ? dialog->selected_indices[0] : -1;
                int next = current;
                if (event->key.key == VG_KEY_UP) {
                    next = current <= 0 ? 0 : current - 1;
                } else {
                    next = current < 0 ? 0 : current + 1;
                    if (next >= (int)dialog->entry_count)
                        next = (int)dialog->entry_count - 1;
                }
                if (next >= 0) {
                    select_entry(dialog, (size_t)next);
                    filedialog_scroll_selection_into_view(dialog, list_height);
                    if (dialog->mode == VG_FILEDIALOG_SAVE &&
                        !dialog->entries[next]->is_directory) {
                        filedialog_set_default_filename(dialog, dialog->entries[next]->name);
                    }
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

/// @brief Destroy the file dialog, freeing all entries, filters, bookmarks, and strings.
///
/// @param dialog The file dialog to destroy; may be NULL.
void vg_filedialog_destroy(vg_filedialog_t *dialog) {
    if (dialog)
        vg_widget_destroy(&dialog->base.base);
}

/// @brief Replace the file dialog title bar text.
///
/// @param dialog The dialog to update; may be NULL.
/// @param title  New title text; copied internally.
void vg_filedialog_set_title(vg_filedialog_t *dialog, const char *title) {
    if (!dialog)
        return;
#ifdef _WIN32
    char *new_title = title ? _strdup(title) : NULL;
#else
    char *new_title = title ? strdup(title) : NULL;
#endif
    if (title && !new_title)
        return;
    free((void *)dialog->base.title);
    dialog->base.title = new_title;
}

/// @brief Set the directory the dialog opens at; defaults to home if NULL.
///
/// @param dialog The dialog to configure; may be NULL.
/// @param path   Absolute directory path; copied internally.  NULL → home directory.
void vg_filedialog_set_initial_path(vg_filedialog_t *dialog, const char *path) {
    if (!dialog)
        return;
#ifdef _WIN32
    char *new_path = path ? _strdup(path) : get_home_directory();
#else
    char *new_path = path ? strdup(path) : get_home_directory();
#endif
    if (!new_path)
        return;
    free(dialog->current_path);
    dialog->current_path = new_path;
}

/// @brief Set the default filename pre-filled in the save-mode text field.
///
/// @param dialog   The dialog to configure; may be NULL.
/// @param filename Initial filename string; copied internally.
void vg_filedialog_set_filename(vg_filedialog_t *dialog, const char *filename) {
    if (!dialog)
        return;
    filedialog_set_default_filename(dialog, filename);
    dialog->base.base.needs_paint = true;
}

/// @brief Enable or disable multi-file selection in the dialog.
///
/// @details In multi-select mode, clicking a file row without a modifier toggles
///          its selection while retaining other selections. When false (the default),
///          each click replaces the current selection with a single entry and the
///          confirm button remains labelled for a single file.
///
/// @param dialog The dialog to configure; may be NULL.
/// @param multi  true to allow multiple files to be selected simultaneously.
void vg_filedialog_set_multi_select(vg_filedialog_t *dialog, bool multi) {
    if (dialog)
        dialog->multi_select = multi;
}

/// @brief Control whether hidden files and directories appear in the file list.
///
/// @details On POSIX, entries whose names begin with '.' are hidden unless show
///          is true. On Windows, entries carrying FILE_ATTRIBUTE_HIDDEN are treated
///          equivalently. Changing this flag takes effect the next time the directory
///          is (re-)loaded via load_directory.
///
/// @param dialog The dialog to configure; may be NULL.
/// @param show   true to include hidden entries in the file list.
void vg_filedialog_set_show_hidden(vg_filedialog_t *dialog, bool show) {
    if (dialog)
        dialog->show_hidden = show;
}

/// @brief Enable or disable an overwrite-confirmation prompt in save mode.
///
/// @details When true and the dialog is in VG_FILEDIALOG_SAVE mode,
///          confirm_selection checks whether the chosen filename already exists
///          and presents a confirmation prompt before populating selected_files[].
///          Has no effect in OPEN or SELECT_FOLDER modes.
///
/// @param dialog   The dialog to configure; may be NULL.
/// @param confirm  true to prompt before overwriting an existing file.
void vg_filedialog_set_confirm_overwrite(vg_filedialog_t *dialog, bool confirm) {
    if (dialog)
        dialog->confirm_overwrite = confirm;
}

/// @brief Append a named file-type filter to the dialog's filter list.
///
/// @details Filters are (name, pattern) pairs where pattern is a semicolon-separated
///          list of glob patterns (e.g., "*.c;*.h"). The active filter determines
///          which entries are visible in the file list. Both name and pattern are
///          copied internally. The backing array doubles in capacity starting at 4;
///          if realloc fails the filter is silently dropped.
///
/// @param dialog   The dialog to configure; may be NULL.
/// @param name     Display label for the filter (e.g., "C Source Files").
/// @param pattern  Semicolon-separated glob pattern (e.g., "*.c;*.h").
void vg_filedialog_add_filter(vg_filedialog_t *dialog, const char *name, const char *pattern) {
    if (!dialog || !name || !pattern)
        return;

    if (dialog->filter_count >= dialog->filter_capacity) {
        size_t new_cap = dialog->filter_capacity == 0 ? 4 : dialog->filter_capacity;
        while (new_cap <= dialog->filter_count) {
            if (new_cap > SIZE_MAX / 2)
                return;
            new_cap *= 2;
        }
        if (new_cap > SIZE_MAX / sizeof(vg_file_filter_t))
            return;
        vg_file_filter_t *new_filters =
            realloc(dialog->filters, new_cap * sizeof(vg_file_filter_t));
        if (!new_filters)
            return;
        dialog->filters = new_filters;
        dialog->filter_capacity = new_cap;
    }

#ifdef _WIN32
    char *name_copy = _strdup(name);
    char *pattern_copy = _strdup(pattern);
#else
    char *name_copy = strdup(name);
    char *pattern_copy = strdup(pattern);
#endif
    if (!name_copy || !pattern_copy) {
        free(name_copy);
        free(pattern_copy);
        return;
    }
    dialog->filters[dialog->filter_count].name = name_copy;
    dialog->filters[dialog->filter_count].pattern = pattern_copy;
    dialog->filter_count++;
}

/// @brief Remove all file-type filters, freeing their name and pattern strings.
///
/// @details The backing filters[] array is retained at its current capacity;
///          only per-entry strings are freed and filter_count reset to zero.
///          After clearing, no filter drop-down is shown and all files are visible.
///
/// @param dialog The dialog to modify; may be NULL.
void vg_filedialog_clear_filters(vg_filedialog_t *dialog) {
    if (!dialog)
        return;

    for (size_t i = 0; i < dialog->filter_count; i++) {
        free(dialog->filters[i].name);
        free(dialog->filters[i].pattern);
    }
    dialog->filter_count = 0;
}

/// @brief Set the extension auto-appended in save mode when the filename lacks one.
///
/// @details In VG_FILEDIALOG_SAVE mode, confirm_selection checks whether the typed
///          filename contains a dot after the last path separator; if none is found
///          the default_extension is appended with a leading '.' unless ext itself
///          already starts with '.'. Passing NULL clears the default extension.
///          Has no effect in OPEN or SELECT_FOLDER modes.
///
/// @param dialog The dialog to configure; may be NULL.
/// @param ext    Extension with or without leading dot (e.g., "txt" or ".txt"),
///               or NULL to disable automatic extension appending.
void vg_filedialog_set_default_extension(vg_filedialog_t *dialog, const char *ext) {
    if (!dialog)
        return;
    char *copy = filedialog_strdup(ext);
    if (ext && !copy)
        return;
    free(dialog->default_extension);
    dialog->default_extension = copy;
}

/// @brief Append a named shortcut to the sidebar bookmark list.
///
/// @details Bookmarks appear in the left sidebar; clicking one navigates the file
///          list to the bookmark's path via load_directory. Both name and path are
///          copied internally. The backing array doubles in capacity starting at 8;
///          if realloc fails the bookmark is silently dropped. The icon field is
///          initialised to VG_ICON_NONE.
///
/// @param dialog The dialog to configure; may be NULL.
/// @param name   Display label shown in the sidebar (e.g., "Projects").
/// @param path   Absolute directory path to navigate to when clicked.
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

    char *name_copy = filedialog_strdup(name);
    char *path_copy = filedialog_strdup(path);
    if (!name_copy || !path_copy) {
        free(name_copy);
        free(path_copy);
        return;
    }
    dialog->bookmarks[dialog->bookmark_count].name = name_copy;
    dialog->bookmarks[dialog->bookmark_count].path = path_copy;
    dialog->bookmarks[dialog->bookmark_count].icon.type = VG_ICON_NONE;
    dialog->bookmark_count++;
}

/// @brief Populate the sidebar with standard OS locations.
///
/// @details On all platforms adds "Home" (the user's home directory). Any of
///          Desktop, Documents, and Downloads that exist as subdirectories of home
///          are stat-checked and added only if they are directories. Finally,
///          "Computer" is added pointing to '/' on POSIX or 'C:\' on Windows.
///          The home string from get_home_directory is freed internally after use.
///
/// @param dialog The dialog to populate; may be NULL.
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

/// @brief Remove all sidebar bookmarks, freeing their name and path strings.
///
/// @details The backing bookmarks[] array is retained at its current capacity;
///          only per-entry strings are freed and bookmark_count reset to zero.
///
/// @param dialog The dialog to modify; may be NULL.
void vg_filedialog_clear_bookmarks(vg_filedialog_t *dialog) {
    if (!dialog)
        return;

    for (size_t i = 0; i < dialog->bookmark_count; i++) {
        free(dialog->bookmarks[i].name);
        free(dialog->bookmarks[i].path);
    }
    dialog->bookmark_count = 0;
}

/// @brief Show the dialog by loading the current directory and making it visible.
///
/// @details Calls load_directory to populate entries[] from current_path, resets
///          drag/result state, marks the widget visible, and forces layout and paint
///          passes. In SAVE mode activates the filename text field immediately
///          (filename_active = true). If the dialog is modal, registers it as the
///          modal root via vg_widget_set_modal_root so pointer events outside it
///          are blocked.
///
/// @param dialog The dialog to display; may be NULL.
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

/// @brief Return the array of selected file paths populated after the dialog confirms.
///
/// @details selected_files[] is owned by the dialog and remains valid until the
///          next confirm_selection call or vg_filedialog_destroy. Callers that need
///          paths to outlive the dialog must copy each string. The count written to
///          *count is zero until the user confirms a selection.
///
/// @param dialog The dialog to query; may be NULL (returns NULL, sets *count to 0).
/// @param count  Out-parameter receiving the number of entries; may be NULL.
/// @return       Pointer to the internal selected_files[] array, or NULL on failure.
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

/// @brief Return the first (or only) selected path after the dialog confirms.
///
/// @details Convenience accessor equivalent to indexing vg_filedialog_get_selected_paths[0].
///          The returned pointer is owned by the dialog; copy it before destroying
///          the dialog.
///
/// @param dialog The dialog to query; may be NULL.
/// @return       selected_files[0], or NULL if no selection has been confirmed.
char *vg_filedialog_get_selected_path(vg_filedialog_t *dialog) {
    if (!dialog || dialog->selected_file_count == 0)
        return NULL;
    return dialog->selected_files[0];
}

/// @brief Register the callback invoked when the user confirms a selection.
///
/// @details The callback receives the dialog pointer, the selected_files[] array,
///          the selection count, and user_data. It is fired inside confirm_selection
///          before the dialog closes, so vg_filedialog_get_selected_path remains
///          valid during the callback. Passing NULL removes the callback.
///          user_data is shared with the on_cancel callback (both read from
///          dialog->user_data).
///
/// @param dialog    The dialog to configure; may be NULL.
/// @param callback  Function called on selection confirmation, or NULL to clear.
/// @param user_data Opaque pointer forwarded to the callback unchanged.
void vg_filedialog_set_on_select(vg_filedialog_t *dialog,
                                 void (*callback)(vg_filedialog_t *, char **, size_t, void *),
                                 void *user_data) {
    if (!dialog)
        return;
    dialog->on_select = callback;
    dialog->user_data = user_data;
}

/// @brief Register the callback invoked when the user cancels the dialog.
///
/// @details Fired when the close/cancel button is activated. The callback receives
///          the dialog pointer and user_data. Passing NULL removes the callback.
///          user_data is shared with the on_select callback (stored in
///          dialog->user_data).
///
/// @param dialog    The dialog to configure; may be NULL.
/// @param callback  Function called on cancellation, or NULL to clear.
/// @param user_data Opaque pointer forwarded to the callback unchanged.
void vg_filedialog_set_on_cancel(vg_filedialog_t *dialog,
                                 void (*callback)(vg_filedialog_t *, void *),
                                 void *user_data) {
    if (!dialog)
        return;
    dialog->on_cancel = callback;
    dialog->user_data = user_data;
}

void vg_filedialog_set_modal_runner(vg_filedialog_modal_runner_t runner, void *user_data) {
    g_modal_runner = runner;
    g_modal_runner_user_data = user_data;
}

static bool filedialog_run_modal(vg_filedialog_t *dialog) {
    if (!dialog)
        return false;
    vg_filedialog_show(dialog);
    if (!g_modal_runner)
        return dialog->selected_file_count > 0;
    return g_modal_runner(dialog, g_modal_runner_user_data) && dialog->selected_file_count > 0;
}

//=============================================================================
// Convenience Functions
//=============================================================================

/// @brief Convenience: create, configure, show, and destroy an open-file dialog.
///
/// @details Creates a VG_FILEDIALOG_OPEN dialog, optionally sets a title, initial
///          path, and one filter, then adds default bookmarks and runs the
///          installed modal runner until the dialog closes.
///
/// @param title          Dialog window title, or NULL for the default.
/// @param initial_path   Directory to open at, or NULL for the home directory.
/// @param filter_name    Display name for the single filter, or NULL for none.
/// @param filter_pattern Glob pattern for the filter (e.g., "*.c;*.h"), or NULL.
/// @return  Heap-allocated selected path (caller must free), or NULL if cancelled.
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

    char *result = NULL;
    if (filedialog_run_modal(dialog) && dialog->selected_file_count > 0 &&
        dialog->selected_files[0]) {
#ifdef _WIN32
        result = _strdup(dialog->selected_files[0]);
#else
        result = strdup(dialog->selected_files[0]);
#endif
    }

    vg_filedialog_destroy(dialog);
    return result;
}

/// @brief Convenience: create, configure, show, and destroy a save-file dialog.
///
/// @details Creates a VG_FILEDIALOG_SAVE dialog, optionally sets a title, initial
///          path, a default filename, and one filter, then adds default bookmarks
///          and runs the installed modal runner until the dialog closes. Returns
///          a heap-allocated copy of the first selected path (caller must free)
///          or NULL if cancelled.
///
/// @param title          Dialog window title, or NULL for the default.
/// @param initial_path   Directory to open at, or NULL for the home directory.
/// @param default_name   Pre-filled filename in the save text field, or NULL.
/// @param filter_name    Display name for the single filter, or NULL for none.
/// @param filter_pattern Glob pattern for the filter (e.g., "*.txt"), or NULL.
/// @return  Heap-allocated selected path (caller must free), or NULL if cancelled.
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

    char *result = NULL;
    if (filedialog_run_modal(dialog) && dialog->selected_file_count > 0 &&
        dialog->selected_files[0]) {
#ifdef _WIN32
        result = _strdup(dialog->selected_files[0]);
#else
        result = strdup(dialog->selected_files[0]);
#endif
    }

    vg_filedialog_destroy(dialog);
    return result;
}

/// @brief Convenience: create, configure, show, and destroy a folder-select dialog.
///
/// @details Creates a VG_FILEDIALOG_SELECT_FOLDER dialog, optionally sets a title
///          and initial path, adds default bookmarks, and shows the dialog. Returns
///          a heap-allocated copy of the selected folder path (caller must free),
///          or NULL if cancelled.
///
/// @param title        Dialog window title, or NULL for the default.
/// @param initial_path Directory to open at, or NULL for the home directory.
/// @return  Heap-allocated selected folder path (caller must free), or NULL if cancelled.
char *vg_filedialog_select_folder(const char *title, const char *initial_path) {
    vg_filedialog_t *dialog = vg_filedialog_create(VG_FILEDIALOG_SELECT_FOLDER);
    if (!dialog)
        return NULL;

    if (title)
        vg_filedialog_set_title(dialog, title);
    if (initial_path)
        vg_filedialog_set_initial_path(dialog, initial_path);
    vg_filedialog_add_default_bookmarks(dialog);

    char *result = NULL;
    if (filedialog_run_modal(dialog) && dialog->selected_file_count > 0 &&
        dialog->selected_files[0]) {
#ifdef _WIN32
        result = _strdup(dialog->selected_files[0]);
#else
        result = strdup(dialog->selected_files[0]);
#endif
    }

    vg_filedialog_destroy(dialog);
    return result;
}
