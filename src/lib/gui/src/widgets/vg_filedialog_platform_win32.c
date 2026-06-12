//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_filedialog_platform_win32.c
// Purpose: Win32 filesystem adapter for the GUI file-dialog widget.
//
// Key invariants:
//   - Directory enumeration uses FindFirstFileA/FindNextFileA and closes each
//     search handle before returning.
//   - Hidden entries are identified via FILE_ATTRIBUTE_HIDDEN.
//
// Ownership/Lifetime:
//   - All returned buffers and entry arrays are caller-owned.
//
// Links: lib/gui/src/widgets/vg_filedialog_platform.h,
//        lib/gui/src/widgets/vg_filedialog.c
//
//===----------------------------------------------------------------------===//

#include "vg_filedialog_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <windows.h>

/// @brief Duplicate a C string using malloc-owned storage.
/// @details Avoids mixing CRT-specific duplicate helpers with widget code.
///          NULL input returns NULL.
/// @param text Source string to duplicate.
/// @return Newly allocated copy, or NULL on allocation failure.
static char *vg_filedialog_platform_strdup(const char *text) {
    if (!text)
        return NULL;
    size_t len = strlen(text);
    char *copy = (char *)malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

/// @brief Append one entry to a growing directory-entry array.
/// @details Takes ownership of the strings stored in @p entry only when the
///          append succeeds. On allocation failure the caller still owns them.
/// @param entries_io Entry array pointer to grow.
/// @param count_io Current entry count, incremented on success.
/// @param capacity_io Current array capacity, updated on growth.
/// @param entry Entry value to append.
/// @return Non-zero on success, zero on allocation failure.
static bool vg_filedialog_platform_append_entry(vg_filedialog_platform_entry_t **entries_io,
                                                size_t *count_io,
                                                size_t *capacity_io,
                                                vg_filedialog_platform_entry_t entry) {
    if (*count_io >= *capacity_io) {
        size_t new_cap = *capacity_io == 0u ? 64u : *capacity_io * 2u;
        if (*capacity_io > SIZE_MAX / (2u * sizeof(**entries_io)))
            return false;
        vg_filedialog_platform_entry_t *grown =
            (vg_filedialog_platform_entry_t *)realloc(*entries_io, new_cap * sizeof(**entries_io));
        if (!grown)
            return false;
        *entries_io = grown;
        *capacity_io = new_cap;
    }
    (*entries_io)[(*count_io)++] = entry;
    return true;
}

/// @brief Return the default Windows root used as the file dialog fallback.
/// @return Static "C:\\" string; caller must not free it.
const char *vg_filedialog_platform_root_path(void) {
    return "C:\\";
}

/// @brief Test whether a character is a Windows path separator.
/// @param ch Character to classify.
/// @return true when @p ch is '/' or '\\'.
bool vg_filedialog_platform_is_separator(char ch) {
    return ch == '/' || ch == '\\';
}

/// @brief Join two Windows path components with a single backslash when needed.
/// @param dir Directory component.
/// @param file Child name component.
/// @return Newly allocated joined path, or NULL on invalid input/allocation failure.
char *vg_filedialog_platform_join_path(const char *dir, const char *file) {
    if (!dir || !file)
        return NULL;
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    size_t sep_len = dir_len > 0u && !vg_filedialog_platform_is_separator(dir[dir_len - 1]) ? 1u : 0u;
    if (dir_len > SIZE_MAX - sep_len || dir_len + sep_len > SIZE_MAX - file_len ||
        dir_len + sep_len + file_len > SIZE_MAX - 1u) {
        return NULL;
    }

    size_t total = dir_len + sep_len + file_len;
    char *result = (char *)malloc(total + 1u);
    if (!result)
        return NULL;
    memcpy(result, dir, dir_len);
    size_t offset = dir_len;
    if (sep_len)
        result[offset++] = '\\';
    memcpy(result + offset, file, file_len);
    result[total] = '\0';
    return result;
}

/// @brief Resolve the current user's Windows home directory.
/// @details Uses USERPROFILE first, then HOMEDRIVE + HOMEPATH, and finally
///          falls back to "C:\\".
/// @return Newly allocated path string, or NULL on allocation failure.
char *vg_filedialog_platform_home_dir(void) {
    const char *userprofile = getenv("USERPROFILE");
    if (userprofile)
        return vg_filedialog_platform_strdup(userprofile);

    const char *homedrive = getenv("HOMEDRIVE");
    const char *homepath = getenv("HOMEPATH");
    if (homedrive && homepath) {
        size_t drive_len = strlen(homedrive);
        size_t path_len = strlen(homepath);
        if (drive_len <= SIZE_MAX - path_len && drive_len + path_len <= SIZE_MAX - 1u) {
            char *result = (char *)malloc(drive_len + path_len + 1u);
            if (result) {
                memcpy(result, homedrive, drive_len);
                memcpy(result + drive_len, homepath, path_len + 1u);
                return result;
            }
        }
    }

    return vg_filedialog_platform_strdup("C:\\");
}

/// @brief Return a newly allocated parent directory path.
/// @details Trims trailing separators before finding the parent. Drive roots
///          remain rooted; relative single-component paths return ".".
/// @param path Input path.
/// @return Newly allocated parent path, or NULL on allocation failure.
char *vg_filedialog_platform_parent_dir(const char *path) {
    if (!path || !*path)
        return vg_filedialog_platform_strdup("C:\\");

    char *result = vg_filedialog_platform_strdup(path);
    if (!result)
        return NULL;

    size_t len = strlen(result);
    while (len > 1u && vg_filedialog_platform_is_separator(result[len - 1u]))
        result[--len] = '\0';

    char *last_slash = strrchr(result, '\\');
    char *last_forward = strrchr(result, '/');
    if (!last_slash || (last_forward && last_forward > last_slash))
        last_slash = last_forward;

    if (last_slash == result) {
        result[1] = '\0';
    } else if (last_slash == result + 2 && result[1] == ':') {
        result[3] = '\0';
    } else if (last_slash) {
        *last_slash = '\0';
    } else {
        free(result);
        return vg_filedialog_platform_strdup(".");
    }

    return result;
}

/// @brief Test whether a path is absolute on Windows.
/// @param path Path string to inspect.
/// @return true for drive-rooted and slash-rooted paths.
bool vg_filedialog_platform_is_absolute_path(const char *path) {
    if (!path || !*path)
        return false;
    return (strlen(path) > 2u && isalpha((unsigned char)path[0]) && path[1] == ':') ||
           path[0] == '\\' || path[0] == '/';
}

/// @brief Test whether a path exists and is a directory.
/// @param path Path string to stat.
/// @return true when _stat() succeeds and reports a directory.
bool vg_filedialog_platform_path_is_dir(const char *path) {
    if (!path)
        return false;
    struct _stat st;
    return _stat(path, &st) == 0 && ((st.st_mode & _S_IFMT) == _S_IFDIR);
}

/// @brief Enumerate a Windows directory into malloc-owned entry records.
/// @details Uses FindFirstFileA/FindNextFileA, skips "." and "..", and returns
///          entry strings owned by the caller.
/// @param path Directory to enumerate.
/// @param entries_out Receives the allocated entry array.
/// @param count_out Receives the number of entries.
/// @return true when the directory was opened and scanned, false otherwise.
bool vg_filedialog_platform_list_directory(const char *path,
                                           vg_filedialog_platform_entry_t **entries_out,
                                           size_t *count_out) {
    if (entries_out)
        *entries_out = NULL;
    if (count_out)
        *count_out = 0u;
    if (!path || !entries_out || !count_out)
        return false;

    char *search_path = vg_filedialog_platform_join_path(path, "*");
    if (!search_path)
        return false;

    WIN32_FIND_DATAA find_data;
    HANDLE find = FindFirstFileA(search_path, &find_data);
    free(search_path);
    if (find == INVALID_HANDLE_VALUE)
        return false;

    vg_filedialog_platform_entry_t *entries = NULL;
    size_t count = 0u;
    size_t capacity = 0u;

    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
            continue;

        vg_filedialog_platform_entry_t out;
        memset(&out, 0, sizeof(out));
        out.name = vg_filedialog_platform_strdup(find_data.cFileName);
        out.full_path = vg_filedialog_platform_join_path(path, find_data.cFileName);
        out.is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.is_hidden = (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        out.size = ((uint64_t)find_data.nFileSizeHigh << 32) | (uint64_t)find_data.nFileSizeLow;

        ULARGE_INTEGER ull;
        ull.LowPart = find_data.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = find_data.ftLastWriteTime.dwHighDateTime;
        out.modified_time = (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);

        if (!out.name || !out.full_path ||
            !vg_filedialog_platform_append_entry(&entries, &count, &capacity, out)) {
            free(out.name);
            free(out.full_path);
            continue;
        }
    } while (FindNextFileA(find, &find_data));

    FindClose(find);
    *entries_out = entries;
    *count_out = count;
    return true;
}

/// @brief Free an entry array returned by vg_filedialog_platform_list_directory().
/// @param entries Entry array to release; NULL is accepted.
/// @param count Number of entries in @p entries.
void vg_filedialog_platform_free_entries(vg_filedialog_platform_entry_t *entries, size_t count) {
    if (!entries)
        return;
    for (size_t i = 0; i < count; i++) {
        free(entries[i].name);
        free(entries[i].full_path);
    }
    free(entries);
}
