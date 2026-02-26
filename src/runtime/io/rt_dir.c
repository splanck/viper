//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/io/rt_dir.c
// Purpose: Cross-platform directory operations for the Viper.IO.Dir class.
//          Provides Exists, Make, MakeAll, Remove, RemoveAll, Files, Dirs,
//          GetCurrent, SetCurrent, and related utilities that work on Windows
//          (FindFirstFile/FindNextFile, _mkdir) and Unix (opendir/readdir, mkdir).
//
// Key invariants:
//   - Most operations trap on invalid paths, permission errors, or I/O failures.
//   - MakeAll creates all missing parent directories in a single call.
//   - RemoveAll recursively deletes a directory tree including all contents.
//   - Files() and Dirs() return only entries in the immediate directory, not recursive.
//   - Platform-specific path separators are handled transparently.
//   - Directory operations are not internally synchronized; callers must
//     serialize concurrent access to the same directory from multiple threads.
//
// Ownership/Lifetime:
//   - Path strings passed as arguments are borrowed; Dir operations do not retain them.
//   - Returned sequences and strings are fresh allocations owned by the caller.
//
// Links: src/runtime/io/rt_dir.h (public API),
//        src/runtime/io/rt_path.h (path component manipulation),
//        src/runtime/io/rt_file_ext.h (file-level operations)
//
//===----------------------------------------------------------------------===//

#include "rt_dir.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_path.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#define PATH_SEP '\\'
// MED-4: On Windows, MAX_PATH is 260 characters.  Windows 10 v1607+ supports
// longer paths when the "LongPathsEnabled" registry key is set, but the ANSI
// FindFirstFileA / FindNextFileA APIs used below are still limited to MAX_PATH.
// To support paths longer than 260 characters, these functions would need to
// be rewritten to use the wide-char (W) variants with the "\\?\" extended-path
// prefix.  Until then, directory operations silently truncate or fail for paths
// exceeding MAX_PATH.
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#elif defined(__viperdos__)
// ViperDOS provides POSIX-compatible directory APIs via libc.
#include <dirent.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PATH_SEP '/'
#else
#include <dirent.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PATH_SEP '/'
#endif

/// @brief Check if a directory exists at the specified path.
///
/// Tests whether a directory exists and is accessible at the given path.
/// This function distinguishes directories from files - a path that points
/// to a regular file will return false.
///
/// **Usage example:**
/// ```
/// ' Check before creating
/// If Not Dir.Exists("output") Then
///     Dir.Make("output")
/// End If
///
/// ' Validate user input
/// Dim folder = InputBox("Enter folder path:")
/// If Dir.Exists(folder) Then
///     ProcessFolder(folder)
/// Else
///     Print "Directory not found: " & folder
/// End If
/// ```
///
/// **Behavior:**
/// - Returns true only for directories, not regular files
/// - Follows symbolic links (checks the target)
/// - Returns false for inaccessible directories (permission denied)
/// - Returns false for non-existent paths
///
/// @param path Directory path to check.
///
/// @return 1 (true) if path is an existing directory, 0 (false) otherwise.
///
/// @note O(1) time complexity (single stat() call).
/// @note Does not create the directory - use rt_dir_make for that.
/// @note Returns false rather than trapping on errors.
///
/// @see rt_dir_make For creating directories
/// @see rt_file_exists For checking file existence
int64_t rt_dir_exists(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return 0;

    struct stat st;
    if (stat(cpath, &st) != 0)
        return 0;

#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#elif defined(__viperdos__)
    return S_ISDIR(st.st_mode);
#else
    return S_ISDIR(st.st_mode);
#endif
}

/// @brief Create a single directory at the specified path.
///
/// Creates a new directory at the given path. The parent directory must
/// already exist. If the directory already exists, the function succeeds
/// silently (idempotent operation).
///
/// **Usage example:**
/// ```
/// ' Create output directory
/// Dir.Make("output")
///
/// ' Create in existing parent
/// Dir.Make("existing_parent/new_folder")
///
/// ' Safe creation (already exists is OK)
/// Dir.Make("logs")  ' Creates if needed
/// Dir.Make("logs")  ' Succeeds again (no error)
/// ```
///
/// **Permission mode (Unix):**
/// New directories are created with mode 0755:
/// - Owner: read, write, execute (rwx)
/// - Group: read, execute (r-x)
/// - Others: read, execute (r-x)
///
/// **Common errors:**
/// | Error                | Cause                                    |
/// |----------------------|------------------------------------------|
/// | Invalid path         | NULL or malformed path string            |
/// | Parent not found     | Parent directory doesn't exist           |
/// | Permission denied    | No write permission in parent directory  |
/// | Disk full            | No space available on filesystem         |
///
/// @param path Path where the directory should be created.
///
/// @note O(1) time complexity.
/// @note Traps on failure (except when directory already exists).
/// @note Use rt_dir_make_all to create parent directories automatically.
/// @note Does not modify existing directories.
///
/// @see rt_dir_make_all For creating nested directories
/// @see rt_dir_exists For checking if directory exists
/// @see rt_dir_remove For removing directories
void rt_dir_make(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
    {
        rt_trap("Dir.Make: invalid path");
        return;
    }

#ifdef _WIN32
    if (_mkdir(cpath) != 0 && errno != EEXIST)
    {
        rt_trap("Dir.Make: failed to create directory");
    }
#elif defined(__viperdos__)
    if (mkdir(cpath, 0755) != 0 && errno != EEXIST)
    {
        rt_trap("Dir.Make: failed to create directory");
    }
#else
    if (mkdir(cpath, 0755) != 0 && errno != EEXIST)
    {
        rt_trap("Dir.Make: failed to create directory");
    }
#endif
}

/// @brief Create a directory and all missing parent directories.
///
/// Creates the target directory and any intermediate directories that don't
/// exist along the path. This is similar to the Unix `mkdir -p` command.
/// If the full path already exists, the function succeeds silently.
///
/// **Usage example:**
/// ```
/// ' Create nested directory structure
/// Dir.MakeAll("output/reports/2024/january")
/// ' Creates: output/, output/reports/, output/reports/2024/,
/// '          output/reports/2024/january/
///
/// ' Create path with existing parents (only missing parts created)
/// Dir.MakeAll("existing/new/deep/path")
///
/// ' Safe to call multiple times
/// Dir.MakeAll("logs/app")
/// Dir.MakeAll("logs/app")  ' No error
/// ```
///
/// **Creation order:**
/// ```
/// Path: "a/b/c/d"
///
/// Step 1: Check/Create "a"
/// Step 2: Check/Create "a/b"
/// Step 3: Check/Create "a/b/c"
/// Step 4: Check/Create "a/b/c/d"
/// ```
///
/// **Permission mode (Unix):**
/// Each created directory uses mode 0755 (rwxr-xr-x).
///
/// **Edge cases:**
/// - Trailing slashes are automatically removed
/// - Empty path is a no-op
/// - Absolute paths (starting with / or drive letter) work correctly
///
/// @param path Full path to create, including all intermediate directories.
///
/// @note O(n) time complexity where n is the path depth.
/// @note Traps on failure (permission denied, disk full, etc.).
/// @note Idempotent - safe to call multiple times with same path.
/// @note Handles both forward and backslashes as separators.
///
/// @see rt_dir_make For creating a single directory
/// @see rt_dir_remove_all For recursively removing directories
void rt_dir_make_all(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
    {
        rt_trap("Dir.MakeAll: invalid path");
        return;
    }

    size_t len = strlen(cpath);
    if (len == 0)
        return;

    // Make a mutable copy
    char *tmp = (char *)malloc(len + 1);
    if (!tmp)
    {
        rt_trap("Dir.MakeAll: out of memory");
        return;
    }
    memcpy(tmp, cpath, len + 1);

    // Remove trailing separators
    while (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\'))
    {
        tmp[--len] = '\0';
    }

    // Create each level
    for (char *p = tmp + 1; *p; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            char sep = *p;
            *p = '\0';

            // Check if this level exists
            struct stat st;
            if (stat(tmp, &st) != 0)
            {
#ifdef _WIN32
                if (_mkdir(tmp) != 0 && errno != EEXIST)
                {
                    free(tmp);
                    rt_trap("Dir.MakeAll: failed to create intermediate directory");
                    return;
                }
#elif defined(__viperdos__)
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                {
                    free(tmp);
                    rt_trap("Dir.MakeAll: failed to create intermediate directory");
                    return;
                }
#else
                if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
                {
                    free(tmp);
                    rt_trap("Dir.MakeAll: failed to create intermediate directory");
                    return;
                }
#endif
            }

            *p = sep;
        }
    }

    // Create the final directory
    struct stat st;
    if (stat(tmp, &st) != 0)
    {
#ifdef _WIN32
        if (_mkdir(tmp) != 0 && errno != EEXIST)
        {
            free(tmp);
            rt_trap("Dir.MakeAll: failed to create directory");
            return;
        }
#elif defined(__viperdos__)
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        {
            free(tmp);
            rt_trap("Dir.MakeAll: failed to create directory");
            return;
        }
#else
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        {
            free(tmp);
            rt_trap("Dir.MakeAll: failed to create directory");
            return;
        }
#endif
    }

    free(tmp);
}

/// @brief Remove an empty directory.
///
/// Deletes a directory that contains no files or subdirectories. The directory
/// must be empty - use rt_dir_remove_all to delete directories with contents.
///
/// **Usage example:**
/// ```
/// ' Remove temporary directory after use
/// Dir.Remove("temp_work")
///
/// ' Clean up after processing
/// For Each file In Dir.Files("staging")
///     File.Delete(Path.Join("staging", file))
/// Next
/// Dir.Remove("staging")  ' Now empty, can remove
/// ```
///
/// **Requirements:**
/// - Directory must exist
/// - Directory must be empty (no files or subdirectories)
/// - Caller must have write permission to parent directory
/// - Directory must not be in use (current working directory, open handles)
///
/// **Common errors:**
/// | Error                | Cause                                    |
/// |----------------------|------------------------------------------|
/// | Invalid path         | NULL or malformed path string            |
/// | Directory not empty  | Contains files or subdirectories         |
/// | Permission denied    | No write permission to parent directory  |
/// | Directory in use     | Is current working directory or has open handles |
///
/// @param path Path to the empty directory to remove.
///
/// @note O(1) time complexity.
/// @note Traps on failure.
/// @note Does not remove parent directories (even if empty after removal).
///
/// @see rt_dir_remove_all For removing directories with contents
/// @see rt_dir_make For creating directories
/// @see rt_dir_exists For checking if directory exists
void rt_dir_remove(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
    {
        rt_trap("Dir.Remove: invalid path");
        return;
    }

#ifdef _WIN32
    if (_rmdir(cpath) != 0)
    {
        rt_trap("Dir.Remove: failed to remove directory");
    }
#elif defined(__viperdos__)
    if (rmdir(cpath) != 0)
    {
        rt_trap("Dir.Remove: failed to remove directory");
    }
#else
    if (rmdir(cpath) != 0)
    {
        rt_trap("Dir.Remove: failed to remove directory");
    }
#endif
}

/// @brief Internal helper to delete a single file.
///
/// Used by rt_dir_remove_all during recursive directory deletion. Silently
/// ignores errors to continue deletion of other files.
///
/// @param path Absolute or relative path to the file.
///
/// @note Errors are silently ignored (best-effort deletion).
/// @note Not part of the public API.
static void delete_file(const char *path)
{
#ifdef _WIN32
    (void)_unlink(path);
#elif defined(__viperdos__)
    (void)unlink(path);
#else
    (void)unlink(path);
#endif
}

/// @brief Recursively remove a directory and all its contents.
///
/// Deletes a directory along with all files and subdirectories it contains.
/// This is equivalent to the Unix `rm -rf` command. Use with caution as this
/// operation is irreversible.
///
/// **Usage example:**
/// ```
/// ' Clean up temporary build directory
/// Dir.RemoveAll("build/temp")
///
/// ' Remove project and all contents
/// If Confirm("Delete project folder?") Then
///     Dir.RemoveAll("my_project")
/// End If
///
/// ' Safe cleanup pattern
/// If Dir.Exists("cache") Then
///     Dir.RemoveAll("cache")
/// End If
/// ```
///
/// **Deletion order (depth-first):**
/// ```
/// project/
/// ├── src/
/// │   ├── main.bas  ← deleted first (file)
/// │   └── utils.bas ← deleted second (file)
/// │   └── (src/ deleted after contents)
/// └── docs/
///     └── readme.txt ← deleted
///     └── (docs/ deleted after contents)
/// └── (project/ deleted last)
/// ```
///
/// **Warning:** This function:
/// - Permanently deletes files (no recycle bin)
/// - Cannot be undone
/// - May delete more than expected if symlinks point elsewhere
/// - Continues on errors (best-effort deletion)
///
/// **Edge cases:**
/// - Non-existent directory: attempts rmdir anyway (may fail silently)
/// - Empty directory: removes it directly
/// - Read-only files: deletion may fail on some platforms
///
/// @param path Path to the directory to remove recursively.
///
/// @note O(n) time complexity where n is total number of files/directories.
/// @note Does not follow symbolic links into other directories.
/// @note Silent on most errors (best-effort deletion).
///
/// @see rt_dir_remove For removing empty directories only
/// @see rt_dir_make_all For creating nested directories
void rt_dir_remove_all(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
    {
        rt_trap("Dir.RemoveAll: invalid path");
        return;
    }

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[PATH_MAX];
    snprintf(pattern, PATH_MAX, "%s\\*", cpath);

    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        // Directory might be empty or not exist
        _rmdir(cpath);
        return;
    }

    do
    {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        char full_path[PATH_MAX];
        snprintf(full_path, PATH_MAX, "%s\\%s", cpath, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            // Recurse into subdirectory
            rt_string sub = rt_string_from_bytes(full_path, strlen(full_path));
            rt_dir_remove_all(sub);
            rt_string_unref(sub);
        }
        else
        {
            delete_file(full_path);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    _rmdir(cpath);
#elif defined(__viperdos__)
    // ViperDOS: Use POSIX directory APIs from libc (same as Unix).
    // Falls through to the shared Unix implementation.
#else
    // Shared Unix/ViperDOS implementation.
#endif

#if !defined(_WIN32)
    DIR *dir = opendir(cpath);
    if (!dir)
    {
        rmdir(cpath);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full_path[PATH_MAX];
        snprintf(full_path, PATH_MAX, "%s/%s", cpath, ent->d_name);

        struct stat st;
        /* Use lstat() so we do not follow symlinks into other trees.
           A symlink pointing to a directory reports S_ISLNK, not S_ISDIR,
           so delete_file() will call unlink() on the symlink itself. */
        if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
            rt_string sub = rt_string_from_bytes(full_path, strlen(full_path));
            rt_dir_remove_all(sub);
            rt_string_unref(sub);
        }
        else
        {
            delete_file(full_path);
        }
    }

    closedir(dir);
    rmdir(cpath);
#endif
}

/// @brief List all entries (files and subdirectories) in a directory.
///
/// Returns a sequence containing the names of all files and subdirectories
/// in the specified directory. The special entries "." and ".." are excluded.
/// Entry names are returned without the directory path prefix.
///
/// **Usage example:**
/// ```
/// ' List all items in current directory
/// Dim entries = Dir.List(".")
/// For Each entry In entries
///     Print entry
/// Next
///
/// ' Process each entry
/// Dim items = Dir.List("downloads")
/// Print "Found " & items.Len() & " items"
///
/// ' Check if directory is empty
/// If Dir.List("folder").Len() = 0 Then
///     Print "Directory is empty"
/// End If
/// ```
///
/// **Return format:**
/// ```
/// Directory: project/
/// ├── main.bas
/// ├── utils.bas
/// └── docs/
///
/// Returns: ["main.bas", "utils.bas", "docs"]
/// (Names only, no "project/" prefix)
/// ```
///
/// **Ordering:**
/// The order of entries is filesystem-dependent and not guaranteed:
/// - May be alphabetical on some systems
/// - May be insertion order on others
/// - Use sorting if consistent order is needed
///
/// @param path Directory path to list.
///
/// @return Seq containing entry names as strings. Returns empty Seq on error.
///
/// @note O(n) time complexity where n is number of entries.
/// @note Returns empty sequence on error (does not trap).
/// @note Does not recurse into subdirectories.
/// @note Hidden files (starting with .) are included.
///
/// @see rt_dir_files For listing only files
/// @see rt_dir_dirs For listing only subdirectories
/// @see rt_dir_entries_seq For trapping version
void *rt_dir_list(rt_string path)
{
    void *result = rt_seq_new();

    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return result;

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[PATH_MAX];
    snprintf(pattern, PATH_MAX, "%s\\*", cpath);

    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0)
        {
            rt_string name = rt_string_from_bytes(fd.cFileName, strlen(fd.cFileName));
            rt_seq_push(result, (void *)name);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    // Unix and ViperDOS: use POSIX directory APIs.
    DIR *dir = opendir(cpath);
    if (!dir)
        return result;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
        {
            rt_string name = rt_string_from_bytes(ent->d_name, strlen(ent->d_name));
            rt_seq_push(result, (void *)name);
        }
    }

    closedir(dir);
#endif

    return result;
}

/// @brief List all entries in a directory as a Viper.Collections.Seq.
///
/// Wrapper for rt_dir_list that returns entries as a Viper.Collections.Seq.
/// Preserves the same behavior: entry name formatting, enumeration order,
/// and empty-on-error handling.
///
/// **Usage example:**
/// ```
/// Dim entries = Dir.ListSeq("src")
/// Print "Directory contains " & entries.Len() & " items"
/// ```
///
/// @param path Directory path to list.
///
/// @return Seq containing entry names. Returns empty Seq on error.
///
/// @note Delegates to rt_dir_list.
/// @note Same behavior as rt_dir_list.
///
/// @see rt_dir_list For implementation details
void *rt_dir_list_seq(rt_string path)
{
    return rt_dir_list(path);
}

/// @brief List all directory entries with error trapping on failure.
///
/// Similar to rt_dir_list but traps (terminates with error) if the directory
/// does not exist or cannot be read. Use this when directory existence is
/// required, not optional.
///
/// **Usage example:**
/// ```
/// ' Will trap if "config" doesn't exist
/// Dim configs = Dir.Entries("config")
///
/// ' Use when directory must exist
/// Dim sources = Dir.Entries("src")
/// For Each src In sources
///     Compile(src)
/// Next
/// ```
///
/// **Difference from rt_dir_list:**
/// | Function       | Missing Dir | Permission Denied |
/// |----------------|-------------|-------------------|
/// | Dir.List       | Empty Seq   | Empty Seq         |
/// | Dir.Entries    | Trap        | Trap              |
///
/// **When to use which:**
/// - Dir.List: Directory may or may not exist, handle both cases
/// - Dir.Entries: Directory must exist, trap on absence is appropriate
///
/// @param path Directory path to list.
///
/// @return Viper.Collections.Seq containing entry names as strings.
///
/// @note O(n) time complexity where n is number of entries.
/// @note Traps if directory doesn't exist or can't be opened.
/// @note Does not sort entries - order is filesystem-dependent.
///
/// @see rt_dir_list For non-trapping version
/// @see rt_dir_files For listing only files
/// @see rt_dir_dirs For listing only subdirectories
void *rt_dir_entries_seq(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        rt_trap("Viper.IO.Dir.Entries: invalid directory path");

    struct stat st;
    if (stat(cpath, &st) != 0)
        rt_trap("Viper.IO.Dir.Entries: directory not found");

#ifdef _WIN32
    if ((st.st_mode & _S_IFDIR) == 0)
        rt_trap("Viper.IO.Dir.Entries: directory not found");
#elif defined(__viperdos__)
    if (!S_ISDIR(st.st_mode))
        rt_trap("Viper.IO.Dir.Entries: directory not found");
#else
    if (!S_ISDIR(st.st_mode))
        rt_trap("Viper.IO.Dir.Entries: directory not found");
#endif

    void *result = rt_seq_new();

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[PATH_MAX];
    snprintf(pattern, PATH_MAX, "%s\\*", cpath);

    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND)
            return result;
        rt_trap("Viper.IO.Dir.Entries: failed to open directory");
    }

    do
    {
        if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0)
        {
            rt_string name = rt_string_from_bytes(fd.cFileName, strlen(fd.cFileName));
            rt_seq_push(result, (void *)name);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    // Unix and ViperDOS: use POSIX directory APIs.
    DIR *dir = opendir(cpath);
    if (!dir)
        rt_trap("Viper.IO.Dir.Entries: failed to open directory");

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0)
        {
            rt_string name = rt_string_from_bytes(ent->d_name, strlen(ent->d_name));
            rt_seq_push(result, (void *)name);
        }
    }

    closedir(dir);
#endif

    return result;
}

/// @brief List only regular files in a directory (excludes subdirectories).
///
/// Returns a sequence containing the names of all regular files in the
/// specified directory. Subdirectories, symbolic links, and special files
/// are excluded from the results.
///
/// **Usage example:**
/// ```
/// ' List all files to process
/// Dim files = Dir.Files("input")
/// For Each file In files
///     ProcessFile(Path.Join("input", file))
/// Next
///
/// ' Count source files
/// Dim sources = Dir.Files("src")
/// Print "Found " & sources.Len() & " source files"
///
/// ' Filter by extension (manual)
/// For Each f In Dir.Files("docs")
///     If Path.Ext(f) = ".txt" Then
///         Print f
///     End If
/// Next
/// ```
///
/// **What is included vs excluded:**
/// | Entry Type       | Included? |
/// |------------------|-----------|
/// | Regular files    | Yes       |
/// | Subdirectories   | No        |
/// | Symbolic links   | No*       |
/// | Device files     | No        |
/// | Named pipes      | No        |
///
/// *On Unix, symlinks to files are excluded; on Windows behavior varies.
///
/// @param path Directory path to scan.
///
/// @return Seq containing file names as strings. Returns empty Seq on error.
///
/// @note O(n) time complexity where n is number of entries.
/// @note Returns empty sequence on error (does not trap).
/// @note Does not recurse into subdirectories.
/// @note File names are returned without directory prefix.
///
/// @see rt_dir_dirs For listing only subdirectories
/// @see rt_dir_list For listing all entries
/// @see rt_dir_files_seq For wrapper version
void *rt_dir_files(rt_string path)
{
    void *result = rt_seq_new();

    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return result;

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[PATH_MAX];
    snprintf(pattern, PATH_MAX, "%s\\*", cpath);

    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            rt_string name = rt_string_from_bytes(fd.cFileName, strlen(fd.cFileName));
            rt_seq_push(result, (void *)name);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    // Unix and ViperDOS: use POSIX directory APIs.
    DIR *dir = opendir(cpath);
    if (!dir)
        return result;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full[PATH_MAX];
        snprintf(full, PATH_MAX, "%s/%s", cpath, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISREG(st.st_mode))
        {
            rt_string name = rt_string_from_bytes(ent->d_name, strlen(ent->d_name));
            rt_seq_push(result, (void *)name);
        }
    }

    closedir(dir);
#endif

    return result;
}

/// @brief List only files in a directory as a Viper.Collections.Seq.
///
/// Wrapper for rt_dir_files that returns file names as a Viper.Collections.Seq.
/// Preserves the same filtering behavior (only regular files, no directories).
///
/// **Usage example:**
/// ```
/// Dim files = Dir.FilesSeq("data")
/// Print "Found " & files.Len() & " data files"
/// ```
///
/// @param path Directory path to scan.
///
/// @return Seq containing file names. Returns empty Seq on error.
///
/// @note Delegates to rt_dir_files.
///
/// @see rt_dir_files For implementation details
void *rt_dir_files_seq(rt_string path)
{
    return rt_dir_files(path);
}

/// @brief List only subdirectories in a directory (excludes files).
///
/// Returns a sequence containing the names of all subdirectories in the
/// specified directory. Regular files and special entries (. and ..) are
/// excluded from the results.
///
/// **Usage example:**
/// ```
/// ' List all project subdirectories
/// Dim folders = Dir.Dirs("projects")
/// For Each folder In folders
///     Print "Project: " & folder
/// Next
///
/// ' Navigate directory structure
/// Sub ShowTree(path As String, indent As Integer)
///     Print Space(indent) & Path.Name(path)
///     For Each subdir In Dir.Dirs(path)
///         ShowTree(Path.Join(path, subdir), indent + 2)
///     Next
/// End Sub
///
/// ' Check for subdirectories
/// If Dir.Dirs("output").Len() > 0 Then
///     Print "Has subdirectories"
/// End If
/// ```
///
/// **What is included vs excluded:**
/// | Entry Type       | Included? |
/// |------------------|-----------|
/// | Subdirectories   | Yes       |
/// | Regular files    | No        |
/// | "." and ".."     | No        |
/// | Symbolic links   | Varies*   |
///
/// *On Unix, follows symlinks to check if target is directory.
///
/// @param path Directory path to scan.
///
/// @return Seq containing subdirectory names as strings. Returns empty Seq on error.
///
/// @note O(n) time complexity where n is number of entries.
/// @note Returns empty sequence on error (does not trap).
/// @note Does not recurse - returns only immediate children.
/// @note Directory names are returned without path prefix.
///
/// @see rt_dir_files For listing only files
/// @see rt_dir_list For listing all entries
/// @see rt_dir_dirs_seq For wrapper version
void *rt_dir_dirs(rt_string path)
{
    void *result = rt_seq_new();

    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
        return result;

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[PATH_MAX];
    snprintf(pattern, PATH_MAX, "%s\\*", cpath);

    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && strcmp(fd.cFileName, ".") != 0 &&
            strcmp(fd.cFileName, "..") != 0)
        {
            rt_string name = rt_string_from_bytes(fd.cFileName, strlen(fd.cFileName));
            rt_seq_push(result, (void *)name);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    // Unix and ViperDOS: use POSIX directory APIs.
    DIR *dir = opendir(cpath);
    if (!dir)
        return result;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char full[PATH_MAX];
        snprintf(full, PATH_MAX, "%s/%s", cpath, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode))
        {
            rt_string name = rt_string_from_bytes(ent->d_name, strlen(ent->d_name));
            rt_seq_push(result, (void *)name);
        }
    }

    closedir(dir);
#endif

    return result;
}

/// @brief List only subdirectories as a Viper.Collections.Seq.
///
/// Wrapper for rt_dir_dirs that returns subdirectory names as a
/// Viper.Collections.Seq. Preserves the same filtering behavior.
///
/// **Usage example:**
/// ```
/// Dim subdirs = Dir.DirsSeq("src")
/// Print "Found " & subdirs.Len() & " subdirectories"
/// ```
///
/// @param path Directory path to scan.
///
/// @return Seq containing subdirectory names. Returns empty Seq on error.
///
/// @note Delegates to rt_dir_dirs.
///
/// @see rt_dir_dirs For implementation details
void *rt_dir_dirs_seq(rt_string path)
{
    return rt_dir_dirs(path);
}

/// @brief Get the current working directory path.
///
/// Returns the absolute path of the process's current working directory.
/// This is the directory used as the base for resolving relative paths in
/// file operations.
///
/// **Usage example:**
/// ```
/// ' Display current location
/// Print "Working in: " & Dir.Current()
///
/// ' Save and restore working directory
/// Dim original = Dir.Current()
/// Dir.SetCurrent("subdir")
/// ' ... do work in subdir ...
/// Dir.SetCurrent(original)  ' Restore
///
/// ' Build absolute path from relative
/// Dim absPath = Path.Join(Dir.Current(), "file.txt")
/// ```
///
/// **Return format:**
/// - Windows: `C:\Users\name\project`
/// - Unix: `/home/user/project`
///
/// **Important notes:**
/// - The current directory is process-wide, not per-thread
/// - Changing directories in one thread affects all threads
/// - Relative paths are resolved against this directory
///
/// @return Absolute path to the current working directory.
///
/// @note O(1) time complexity.
/// @note Traps if the current directory cannot be determined.
/// @note Returns newly allocated string (caller manages memory).
/// @note Maximum path length is PATH_MAX (typically 4096 on Unix, 260 on Windows).
///
/// @see rt_dir_set_current For changing the working directory
/// @see rt_path_absolute For converting relative paths to absolute
rt_string rt_dir_current(void)
{
    char buffer[PATH_MAX];

#ifdef _WIN32
    if (_getcwd(buffer, PATH_MAX) == NULL)
    {
        rt_trap("Dir.Current: failed to get current directory");
        return rt_str_empty();
    }
#else
    // Unix and ViperDOS: use POSIX getcwd.
    if (getcwd(buffer, PATH_MAX) == NULL)
    {
        rt_trap("Dir.Current: failed to get current directory");
        return rt_str_empty();
    }
#endif

    return rt_string_from_bytes(buffer, strlen(buffer));
}

/// @brief Change the current working directory.
///
/// Changes the process's current working directory to the specified path.
/// After this call, relative paths in file operations will be resolved
/// relative to the new directory.
///
/// **Usage example:**
/// ```
/// ' Change to project directory
/// Dir.SetCurrent("/home/user/project")
///
/// ' Now relative paths work from project/
/// Dim files = Dir.Files("src")  ' Lists project/src/
///
/// ' Use with pattern: save, change, restore
/// Dim saved = Dir.Current()
/// Dir.SetCurrent("build")
/// RunBuildCommands()
/// Dir.SetCurrent(saved)
/// ```
///
/// **Valid paths:**
/// - Absolute paths: `/home/user/dir` or `C:\Users\dir`
/// - Relative paths: `subdir` or `../sibling`
/// - Home expansion: NOT supported (use full path)
///
/// **Common errors:**
/// | Error                | Cause                              |
/// |----------------------|------------------------------------|
/// | Invalid path         | NULL or malformed path string      |
/// | Directory not found  | Path does not exist                |
/// | Not a directory      | Path points to a file              |
/// | Permission denied    | No execute permission on directory |
///
/// **Warning:** Changing the current directory affects ALL threads in the
/// process. Avoid changing directories in multi-threaded applications or
/// use proper synchronization.
///
/// @param path Path to the new working directory.
///
/// @note O(1) time complexity.
/// @note Traps on failure (directory not found, permission denied).
/// @note Process-wide effect - affects all threads.
///
/// @see rt_dir_current For getting the current working directory
/// @see rt_dir_exists For checking if directory exists before changing
void rt_dir_set_current(rt_string path)
{
    const char *cpath = NULL;
    if (!rt_file_path_from_vstr(path, &cpath) || !cpath)
    {
        rt_trap("Dir.SetCurrent: invalid path");
        return;
    }

#ifdef _WIN32
    if (_chdir(cpath) != 0)
    {
        rt_trap("Dir.SetCurrent: failed to change directory");
    }
#elif defined(__viperdos__)
    if (chdir(cpath) != 0)
    {
        rt_trap("Dir.SetCurrent: failed to change directory");
    }
#else
    if (chdir(cpath) != 0)
    {
        rt_trap("Dir.SetCurrent: failed to change directory");
    }
#endif
}

/// @brief Move or rename a directory.
///
/// Moves a directory from one location to another, or renames it. This
/// operation is atomic on the same filesystem - either the entire directory
/// is moved or the operation fails (no partial moves).
///
/// **Usage example:**
/// ```
/// ' Rename a directory
/// Dir.Move("old_name", "new_name")
///
/// ' Move to different location
/// Dir.Move("project/temp", "archive/temp_backup")
///
/// ' Move with rename
/// Dir.Move("downloads/data", "processed/data_2024")
/// ```
///
/// **Operation modes:**
/// | Source           | Destination      | Result                      |
/// |------------------|------------------|-----------------------------|
/// | `dir1`           | `dir2`           | Rename dir1 to dir2         |
/// | `path/dir1`      | `other/dir1`     | Move to other location      |
/// | `path/dir1`      | `other/dir2`     | Move and rename             |
///
/// **Requirements:**
/// - Source directory must exist
/// - Destination must not exist (won't overwrite)
/// - Parent of destination must exist
/// - Both paths should be on same filesystem for atomic move
///
/// **Common errors:**
/// | Error                | Cause                                    |
/// |----------------------|------------------------------------------|
/// | Invalid path         | NULL or malformed source/dest path       |
/// | Source not found     | Source directory doesn't exist           |
/// | Dest already exists  | Destination path already exists          |
/// | Permission denied    | No write permission to source or dest    |
/// | Cross-device         | Moving across filesystems (may fail)     |
///
/// **Cross-filesystem moves:**
/// Some platforms don't support atomic moves across filesystems. In such
/// cases, use a copy-then-delete pattern instead:
/// ```
/// ' Manual cross-filesystem move
/// CopyDir(src, dst)
/// Dir.RemoveAll(src)
/// ```
///
/// @param src Source directory path.
/// @param dst Destination directory path.
///
/// @note O(1) time complexity for same-filesystem moves.
/// @note Traps on failure.
/// @note Uses rename() system call internally.
/// @note Not atomic across different filesystems.
///
/// @see rt_dir_make For creating directories
/// @see rt_dir_remove For removing directories
/// @see rt_file_move For moving files
void rt_dir_move(rt_string src, rt_string dst)
{
    const char *csrc = NULL;
    const char *cdst = NULL;

    if (!rt_file_path_from_vstr(src, &csrc) || !csrc)
    {
        rt_trap("Dir.Move: invalid source path");
        return;
    }

    if (!rt_file_path_from_vstr(dst, &cdst) || !cdst)
    {
        rt_trap("Dir.Move: invalid destination path");
        return;
    }

    if (rename(csrc, cdst) != 0)
    {
        rt_trap("Dir.Move: failed to move directory");
    }
}
