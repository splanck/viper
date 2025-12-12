//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTDirTests.cpp
// Purpose: Validate runtime directory operations in rt_dir.c.
// Key invariants: Directory operations work correctly across platforms,
//                 List/Files/Dirs return proper Seq objects, paths are handled
//                 correctly.
// Ownership/Lifetime: Uses runtime library; tests return newly allocated strings
//                     and sequences that must be released.
// Links: docs/viperlib.md

#include "rt.hpp"
#include "rt_dir.h"
#include "rt_seq.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define mkdir_p(path) _mkdir(path)
#define rmdir_p(path) _rmdir(path)
#define getpid _getpid
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_p(path) mkdir(path, 0755)
#define rmdir_p(path) rmdir(path)
#endif

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Get a unique temp directory path for testing.
static const char *get_test_base()
{
#ifdef _WIN32
    static char buf[256];
    const char *tmp = getenv("TEMP");
    if (!tmp)
        tmp = getenv("TMP");
    if (!tmp)
        tmp = "C:\\Temp";
    snprintf(buf, sizeof(buf), "%s\\viper_dir_test_%d", tmp, (int)getpid());
    return buf;
#else
    static char buf[256];
    snprintf(buf, sizeof(buf), "/tmp/viper_dir_test_%d", (int)getpid());
    return buf;
#endif
}

/// @brief Helper to create a file.
static void create_file(const char *path)
{
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "test\n");
        fclose(f);
    }
}

/// @brief Helper to remove a file.
static void remove_file(const char *path)
{
#ifdef _WIN32
    _unlink(path);
#else
    unlink(path);
#endif
}

/// @brief Test rt_dir_exists.
static void test_exists()
{
    printf("Testing rt_dir_exists:\n");

    const char *base = get_test_base();

    // Directory doesn't exist yet
    rt_string path = rt_const_cstr(base);
    test_result("non-existent", rt_dir_exists(path) == 0);

    // Create directory
    mkdir_p(base);
    test_result("exists after create", rt_dir_exists(path) == 1);

    // Clean up
    rmdir_p(base);
    test_result("not exists after remove", rt_dir_exists(path) == 0);

    printf("\n");
}

/// @brief Test rt_dir_make and rt_dir_remove.
static void test_make_remove()
{
    printf("Testing rt_dir_make and rt_dir_remove:\n");

    const char *base = get_test_base();
    rt_string path = rt_const_cstr(base);

    // Make directory
    rt_dir_make(path);
    test_result("make creates dir", rt_dir_exists(path) == 1);

    // Remove directory
    rt_dir_remove(path);
    test_result("remove deletes dir", rt_dir_exists(path) == 0);

    printf("\n");
}

/// @brief Test rt_dir_make_all.
static void test_make_all()
{
    printf("Testing rt_dir_make_all:\n");

    const char *base = get_test_base();
    char nested[512];
    snprintf(nested, sizeof(nested), "%s/a/b/c", base);

    rt_string path = rt_const_cstr(nested);
    rt_string base_path = rt_const_cstr(base);

    // Make nested directories
    rt_dir_make_all(path);
    test_result("make_all creates nested", rt_dir_exists(path) == 1);

    // Clean up (need to remove in reverse order)
    char level2[512], level1[512];
    snprintf(level2, sizeof(level2), "%s/a/b", base);
    snprintf(level1, sizeof(level1), "%s/a", base);

    rmdir_p(nested);
    rmdir_p(level2);
    rmdir_p(level1);
    rmdir_p(base);

    test_result("cleanup succeeded", rt_dir_exists(base_path) == 0);

    printf("\n");
}

/// @brief Test rt_dir_remove_all.
static void test_remove_all()
{
    printf("Testing rt_dir_remove_all:\n");

    const char *base = get_test_base();
    char subdir[512], file1[512], file2[512];
    snprintf(subdir, sizeof(subdir), "%s/subdir", base);
    snprintf(file1, sizeof(file1), "%s/file1.txt", base);
    snprintf(file2, sizeof(file2), "%s/subdir/file2.txt", base);

    // Create structure
    mkdir_p(base);
    mkdir_p(subdir);
    create_file(file1);
    create_file(file2);

    rt_string path = rt_const_cstr(base);
    test_result("structure exists", rt_dir_exists(path) == 1);

    // Remove all
    rt_dir_remove_all(path);
    test_result("remove_all deletes everything", rt_dir_exists(path) == 0);

    printf("\n");
}

/// @brief Test rt_dir_list.
static void test_list()
{
    printf("Testing rt_dir_list:\n");

    const char *base = get_test_base();
    char subdir[512], file1[512];
    snprintf(subdir, sizeof(subdir), "%s/subdir", base);
    snprintf(file1, sizeof(file1), "%s/file1.txt", base);

    // Create structure
    mkdir_p(base);
    mkdir_p(subdir);
    create_file(file1);

    rt_string path = rt_const_cstr(base);
    void *list = rt_dir_list(path);

    // Should have 2 entries (subdir and file1.txt)
    int64_t count = rt_seq_len(list);
    test_result("list has 2 entries", count == 2);

    // Check entries exist (order may vary)
    bool found_subdir = false;
    bool found_file = false;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string entry = (rt_string)rt_seq_get(list, i);
        if (rt_str_eq(entry, rt_const_cstr("subdir")))
            found_subdir = true;
        if (rt_str_eq(entry, rt_const_cstr("file1.txt")))
            found_file = true;
    }
    test_result("found subdir", found_subdir);
    test_result("found file", found_file);

    // Clean up (sequences leak in tests, this is fine)
    remove_file(file1);
    rmdir_p(subdir);
    rmdir_p(base);

    printf("\n");
}

/// @brief Test rt_dir_files.
static void test_files()
{
    printf("Testing rt_dir_files:\n");

    const char *base = get_test_base();
    char subdir[512], file1[512], file2[512];
    snprintf(subdir, sizeof(subdir), "%s/subdir", base);
    snprintf(file1, sizeof(file1), "%s/file1.txt", base);
    snprintf(file2, sizeof(file2), "%s/file2.txt", base);

    // Create structure
    mkdir_p(base);
    mkdir_p(subdir);
    create_file(file1);
    create_file(file2);

    rt_string path = rt_const_cstr(base);
    void *files = rt_dir_files(path);

    // Should have 2 files (not subdir)
    int64_t count = rt_seq_len(files);
    test_result("files has 2 entries", count == 2);

    // Check that subdir is NOT in the list
    bool found_subdir = false;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string entry = (rt_string)rt_seq_get(files, i);
        if (rt_str_eq(entry, rt_const_cstr("subdir")))
            found_subdir = true;
    }
    test_result("subdir not in files", !found_subdir);

    // Clean up
    remove_file(file1);
    remove_file(file2);
    rmdir_p(subdir);
    rmdir_p(base);

    printf("\n");
}

/// @brief Test rt_dir_dirs.
static void test_dirs()
{
    printf("Testing rt_dir_dirs:\n");

    const char *base = get_test_base();
    char subdir1[512], subdir2[512], file1[512];
    snprintf(subdir1, sizeof(subdir1), "%s/dir1", base);
    snprintf(subdir2, sizeof(subdir2), "%s/dir2", base);
    snprintf(file1, sizeof(file1), "%s/file1.txt", base);

    // Create structure
    mkdir_p(base);
    mkdir_p(subdir1);
    mkdir_p(subdir2);
    create_file(file1);

    rt_string path = rt_const_cstr(base);
    void *dirs = rt_dir_dirs(path);

    // Should have 2 dirs (not file)
    int64_t count = rt_seq_len(dirs);
    test_result("dirs has 2 entries", count == 2);

    // Check that file is NOT in the list
    bool found_file = false;
    for (int64_t i = 0; i < count; i++)
    {
        rt_string entry = (rt_string)rt_seq_get(dirs, i);
        if (rt_str_eq(entry, rt_const_cstr("file1.txt")))
            found_file = true;
    }
    test_result("file not in dirs", !found_file);

    // Clean up
    remove_file(file1);
    rmdir_p(subdir1);
    rmdir_p(subdir2);
    rmdir_p(base);

    printf("\n");
}

/// @brief Test rt_dir_current and rt_dir_set_current.
static void test_current()
{
    printf("Testing rt_dir_current and rt_dir_set_current:\n");

    // Save current directory
    rt_string original = rt_dir_current();
    test_result("current returns non-empty", rt_len(original) > 0);

    // Create test directory
    const char *base = get_test_base();
    mkdir_p(base);

    // Change to test directory
    rt_string new_dir = rt_const_cstr(base);
    rt_dir_set_current(new_dir);

    // Verify we're in the new directory
    rt_string current = rt_dir_current();
    // The current path should end with our test directory name
    // (may have different prefix due to realpath)
    const char *current_cstr = rt_string_cstr(current);
    test_result("changed directory", strstr(current_cstr, "viper_dir_test_") != NULL);
    rt_string_unref(current);

    // Restore original directory
    rt_dir_set_current(original);
    rt_string_unref(original);

    // Clean up
    rmdir_p(base);

    printf("\n");
}

/// @brief Test rt_dir_move.
static void test_move()
{
    printf("Testing rt_dir_move:\n");

    const char *base = get_test_base();
    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s_src", base);
    snprintf(dst, sizeof(dst), "%s_dst", base);

    // Create source directory
    mkdir_p(src);

    rt_string src_path = rt_const_cstr(src);
    rt_string dst_path = rt_const_cstr(dst);

    test_result("source exists", rt_dir_exists(src_path) == 1);
    test_result("dest not exists", rt_dir_exists(dst_path) == 0);

    // Move directory
    rt_dir_move(src_path, dst_path);

    test_result("source gone after move", rt_dir_exists(src_path) == 0);
    test_result("dest exists after move", rt_dir_exists(dst_path) == 1);

    // Clean up
    rmdir_p(dst);

    printf("\n");
}

/// @brief Test empty directory listing.
static void test_empty_dir()
{
    printf("Testing empty directory:\n");

    const char *base = get_test_base();
    mkdir_p(base);

    rt_string path = rt_const_cstr(base);

    void *list = rt_dir_list(path);
    test_result("empty list has 0 entries", rt_seq_len(list) == 0);

    void *files = rt_dir_files(path);
    test_result("empty files has 0 entries", rt_seq_len(files) == 0);

    void *dirs = rt_dir_dirs(path);
    test_result("empty dirs has 0 entries", rt_seq_len(dirs) == 0);

    rmdir_p(base);

    printf("\n");
}

/// @brief Test non-existent directory listing.
static void test_nonexistent_dir()
{
    printf("Testing non-existent directory:\n");

    rt_string path = rt_const_cstr("/nonexistent_dir_12345");

    void *list = rt_dir_list(path);
    test_result("nonexistent list has 0 entries", rt_seq_len(list) == 0);

    void *files = rt_dir_files(path);
    test_result("nonexistent files has 0 entries", rt_seq_len(files) == 0);

    void *dirs = rt_dir_dirs(path);
    test_result("nonexistent dirs has 0 entries", rt_seq_len(dirs) == 0);

    printf("\n");
}

/// @brief Entry point for directory tests.
int main()
{
    printf("=== RT Dir Tests ===\n\n");

    test_exists();
    test_make_remove();
    test_make_all();
    test_remove_all();
    test_list();
    test_files();
    test_dirs();
    test_current();
    test_move();
    test_empty_dir();
    test_nonexistent_dir();

    printf("All directory tests passed!\n");
    return 0;
}
