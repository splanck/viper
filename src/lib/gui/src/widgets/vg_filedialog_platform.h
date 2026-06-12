//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: lib/gui/src/widgets/vg_filedialog_platform.h
// Purpose: Platform filesystem adapter for the GUI file-dialog widget.
//          Keeps host-specific path, home-directory, metadata, and directory
//          enumeration logic out of vg_filedialog.c.
//
// Key invariants:
//   - All returned strings are heap-allocated with malloc-compatible storage.
//   - Directory enumeration omits "." and ".." before returning entries.
//   - Entry arrays returned by vg_filedialog_platform_list_directory must be
//     released with vg_filedialog_platform_free_entries.
//
// Ownership/Lifetime:
//   - Callers own every char* returned directly by this API.
//   - Callers own directory entry arrays until passed to the free helper.
//
// Links: lib/gui/src/widgets/vg_filedialog.c,
//        lib/gui/src/widgets/vg_filedialog_platform_win32.c,
//        lib/gui/src/widgets/vg_filedialog_platform_posix.c
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Platform-neutral directory entry data used by the file-dialog widget.
/// @details The adapter fills this struct from native directory APIs, while
///          the widget owns filtering, sorting, selection, and rendering. Both
///          @c name and @c full_path are malloc-owned and freed by
///          vg_filedialog_platform_free_entries().
typedef struct vg_filedialog_platform_entry {
    char *name;
    char *full_path;
    bool is_directory;
    bool is_hidden;
    uint64_t size;
    int64_t modified_time;
} vg_filedialog_platform_entry_t;

/// @brief Return the platform's preferred root bookmark path.
/// @details Windows returns "C:\\" and POSIX-style platforms return "/".
///          The returned pointer is a string literal and must not be freed.
/// @return Stable string literal for the root/computer sidebar bookmark.
const char *vg_filedialog_platform_root_path(void);

/// @brief Return true when @p ch is a native path separator for this platform.
/// @details Windows accepts both slash forms because Win32 APIs tolerate '/',
///          while POSIX-style platforms accept only '/'.
/// @param ch Character to classify.
/// @return Non-zero if @p ch is considered a path separator.
bool vg_filedialog_platform_is_separator(char ch);

/// @brief Join @p dir and @p file using the native separator.
/// @details The returned path is malloc-owned. A separator is inserted only
///          when @p dir is non-empty and does not already end in a platform
///          separator. Returns NULL on invalid input, overflow, or allocation
///          failure.
/// @param dir Directory prefix.
/// @param file Child filename or relative path.
/// @return Newly allocated joined path, or NULL on failure.
char *vg_filedialog_platform_join_path(const char *dir, const char *file);

/// @brief Return the current user's home directory.
/// @details Uses the normal platform environment/account lookup cascade and
///          falls back to the platform root when no user home can be found.
///          The returned string is malloc-owned.
/// @return Newly allocated home-directory path, or NULL on allocation failure.
char *vg_filedialog_platform_home_dir(void);

/// @brief Return the parent directory of @p path.
/// @details Preserves platform root semantics such as "C:\\" on Windows and
///          "/" on POSIX-style systems. The returned string is malloc-owned.
/// @param path Path whose parent should be computed; NULL/empty returns root.
/// @return Newly allocated parent path, or NULL on allocation failure.
char *vg_filedialog_platform_parent_dir(const char *path);

/// @brief Test whether @p path is absolute on the host platform.
/// @details Windows accepts drive-qualified paths and slash-prefixed paths;
///          POSIX-style platforms accept leading '/'.
/// @param path Candidate path.
/// @return Non-zero if @p path is absolute.
bool vg_filedialog_platform_is_absolute_path(const char *path);

/// @brief Test whether @p path exists and is a directory.
/// @details Returns false for missing paths, files, inaccessible paths, and
///          NULL input. This is intentionally non-trapping for UI probing.
/// @param path Candidate filesystem path.
/// @return Non-zero when @p path names an existing directory.
bool vg_filedialog_platform_path_is_dir(const char *path);

/// @brief Enumerate direct children of @p path.
/// @details Allocates an array of platform entries and stores it in
///          @p entries_out. The caller must release the array with
///          vg_filedialog_platform_free_entries(). Entries named "." and ".."
///          are omitted. On failure, outputs are reset to NULL/0.
/// @param path Directory to read.
/// @param entries_out Receives the allocated entry array.
/// @param count_out Receives the number of entries.
/// @return Non-zero on successful directory open/enumeration, zero on failure.
bool vg_filedialog_platform_list_directory(const char *path,
                                           vg_filedialog_platform_entry_t **entries_out,
                                           size_t *count_out);

/// @brief Free entries returned by vg_filedialog_platform_list_directory().
/// @details Frees each entry's @c name and @c full_path, then the outer array.
///          Safe to call with NULL or count zero.
/// @param entries Entry array to release.
/// @param count Number of entries in @p entries.
void vg_filedialog_platform_free_entries(vg_filedialog_platform_entry_t *entries, size_t count);

#ifdef __cplusplus
}
#endif
