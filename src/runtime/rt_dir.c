//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_dir.c
// Purpose: Cross-platform directory operations for Viper.IO.Dir.
// Key invariants: Directory operations work consistently across Unix and Windows,
//                 all functions handle rt_string input/output correctly.
// Ownership/Lifetime: Returned strings and sequences are newly allocated.
// Links: docs/viperlib.md
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
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <dirent.h>
#include <unistd.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define PATH_SEP '/'
#endif

/// @brief Check if a directory exists.
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
#else
    return S_ISDIR(st.st_mode);
#endif
}

/// @brief Create a single directory.
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
#else
    if (mkdir(cpath, 0755) != 0 && errno != EEXIST)
    {
        rt_trap("Dir.Make: failed to create directory");
    }
#endif
}

/// @brief Create a directory and all parent directories.
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
#else
    if (rmdir(cpath) != 0)
    {
        rt_trap("Dir.Remove: failed to remove directory");
    }
#endif
}

/// @brief Helper: Delete a file (for use in rt_dir_remove_all).
static void delete_file(const char *path)
{
#ifdef _WIN32
    (void)_unlink(path);
#else
    (void)unlink(path);
#endif
}

/// @brief Recursively remove a directory and all its contents.
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
#else
    DIR *dir = opendir(cpath);
    if (!dir)
    {
        // Try to remove anyway
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
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
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
    }

    closedir(dir);
    rmdir(cpath);
#endif
}

/// @brief List all entries in a directory.
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
/// @details Wrapper for rt_dir_list; preserves entry name formatting, enumeration order, and
///          empty-on-error behavior.
void *rt_dir_list_seq(rt_string path)
{
    return rt_dir_list(path);
}

/// @brief List all directory entries as a Viper.Collections.Seq of strings.
/// @details Returns entry names (excluding . and ..) in the same enumeration order used by
///          rt_dir_list/rt_dir_files/rt_dir_dirs. No sorting is performed, so ordering is
///          platform- and filesystem-dependent.
/// @param path Directory path to list.
/// @return Viper.Collections.Seq containing runtime strings for each entry name.
/// @note Traps when the directory does not exist or cannot be enumerated.
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

/// @brief List only files in a directory.
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
    DIR *dir = opendir(cpath);
    if (!dir)
        return result;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        // Use stat to determine if entry is a regular file
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
/// @details Wrapper for rt_dir_files; preserves entry name formatting, enumeration order, and
///          empty-on-error behavior.
void *rt_dir_files_seq(rt_string path)
{
    return rt_dir_files(path);
}

/// @brief List only subdirectories in a directory.
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
    DIR *dir = opendir(cpath);
    if (!dir)
        return result;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        // Use stat to determine if entry is a directory
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

/// @brief List only subdirectories in a directory as a Viper.Collections.Seq.
/// @details Wrapper for rt_dir_dirs; preserves entry name formatting, enumeration order, and
///          empty-on-error behavior.
void *rt_dir_dirs_seq(rt_string path)
{
    return rt_dir_dirs(path);
}

/// @brief Get the current working directory.
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
    if (getcwd(buffer, PATH_MAX) == NULL)
    {
        rt_trap("Dir.Current: failed to get current directory");
        return rt_str_empty();
    }
#endif

    return rt_string_from_bytes(buffer, strlen(buffer));
}

/// @brief Change the current working directory.
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
#else
    if (chdir(cpath) != 0)
    {
        rt_trap("Dir.SetCurrent: failed to change directory");
    }
#endif
}

/// @brief Move/rename a directory.
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
