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
//                 List/Files/Dirs/Entries return proper Seq objects, paths are
//                 handled correctly.
// Ownership/Lifetime: Uses runtime library; tests return newly allocated strings
//                     and sequences that must be released.
// Links: docs/viperlib.md

#include "rt.hpp"
#include "rt_dir.h"
#include "rt_seq.h"
#include "tests/common/PlatformSkip.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define mkdir_p(path) _mkdir(path)
#define rmdir_p(path) _rmdir(path)
#define getpid _getpid
#else
#include "tests/common/PosixCompat.h"
#include <sys/stat.h>
#define mkdir_p(path) mkdir(path, 0755)
#define rmdir_p(path) rmdir(path)
#endif

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Get a unique temp directory path for testing.
static const char *get_test_base() {
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
static void create_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "test\n");
        fclose(f);
    }
}

/// @brief Helper to remove a file.
static void remove_file(const char *path) {
#ifdef _WIN32
    _unlink(path);
#else
    unlink(path);
#endif
}

/// @brief Test rt_dir_exists.
static void test_exists() {
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
static void test_make_remove() {
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

/// @brief Test rt_dir_make rejects existing non-directory paths.
static void test_make_existing_file_traps() {
    printf("Testing rt_dir_make existing-file guard:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_make_file", base);
    create_file(file_path);

    EXPECT_TRAP(rt_dir_make(rt_const_cstr(file_path)));

    remove_file(file_path);
    printf("\n");
}

/// @brief Test rt_dir_make_all.
static void test_make_all() {
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

/// @brief Test rt_dir_make_all treats backslashes as literal filename bytes on POSIX hosts.
static void test_make_all_backslash_path() {
    printf("Testing rt_dir_make_all POSIX backslash literals:\n");

    const char *base = get_test_base();
    char nested[512];
    snprintf(nested, sizeof(nested), "%s\\slash\\nested", base);

    rt_dir_make_all(rt_const_cstr(nested));

    char normalized[512];
    snprintf(normalized, sizeof(normalized), "%s/slash/nested", base);
    test_result("literal backslash path exists", rt_dir_exists(rt_const_cstr(nested)) == 1);
    test_result("backslashes are not normalized", rt_dir_exists(rt_const_cstr(normalized)) == 0);

    rt_dir_remove_all(rt_const_cstr(nested));
    printf("\n");
}

/// @brief Test rt_dir_make_all rejects files in the requested path.
static void test_make_all_existing_file_traps() {
    printf("Testing rt_dir_make_all existing-file guard:\n");

    const char *base = get_test_base();
    char file_path[512], nested[512];
    snprintf(file_path, sizeof(file_path), "%s/a", base);
    snprintf(nested, sizeof(nested), "%s/a/b", base);

    mkdir_p(base);
    create_file(file_path);

    EXPECT_TRAP(rt_dir_make_all(rt_const_cstr(nested)));

    remove_file(file_path);
    rmdir_p(base);
    printf("\n");
}

/// @brief Test rt_dir_remove_all.
static void test_remove_all() {
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

static void test_remove_all_missing_is_success() {
    printf("Testing rt_dir_remove_all missing directory:\n");

    const char *base = get_test_base();
    char missing[512];
    snprintf(missing, sizeof(missing), "%s_missing_remove_all", base);

    rt_dir_remove_all(rt_const_cstr(missing));
    test_result("remove_all missing succeeds", true);

    printf("\n");
}

static void test_remove_all_protected_paths_trap() {
    printf("Testing rt_dir_remove_all protected paths:\n");

    EXPECT_TRAP(rt_dir_remove_all(rt_const_cstr(".")));
    test_result("remove_all current directory traps", true);

    printf("\n");
}

static void test_remove_all_current_ancestor_traps() {
    printf("Testing rt_dir_remove_all current-directory ancestor guard:\n");

    const char *base = get_test_base();
    char child[512];
    snprintf(child, sizeof(child), "%s/child", base);

    rt_string original = rt_dir_current();
    rt_dir_make_all(rt_const_cstr(child));
    rt_dir_set_current(rt_const_cstr(child));

    EXPECT_TRAP(rt_dir_remove_all(rt_const_cstr(base)));
    test_result("ancestor directory preserved", rt_dir_exists(rt_const_cstr(base)) == 1);

    rt_dir_set_current(original);
    rt_string_unref(original);
    rt_dir_remove_all(rt_const_cstr(base));

    printf("\n");
}

/// @brief Test rt_dir_list.
static void test_list() {
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
    for (int64_t i = 0; i < count; i++) {
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

/// @brief Test rt_dir_entries_seq.
static void test_entries() {
    printf("Testing rt_dir_entries_seq:\n");

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
    void *entries = rt_dir_entries_seq(path);

    // Should have 3 entries (subdir and 2 files)
    int64_t count = rt_seq_len(entries);
    test_result("entries has 3 entries", count == 3);

    bool found_subdir = false;
    bool found_file1 = false;
    bool found_file2 = false;
    for (int64_t i = 0; i < count; i++) {
        rt_string entry = (rt_string)rt_seq_get(entries, i);
        if (rt_str_eq(entry, rt_const_cstr("subdir")))
            found_subdir = true;
        if (rt_str_eq(entry, rt_const_cstr("file1.txt")))
            found_file1 = true;
        if (rt_str_eq(entry, rt_const_cstr("file2.txt")))
            found_file2 = true;
    }
    test_result("found subdir", found_subdir);
    test_result("found file1", found_file1);
    test_result("found file2", found_file2);

    // Clean up (sequences leak in tests, this is fine)
    remove_file(file1);
    remove_file(file2);
    rmdir_p(subdir);
    rmdir_p(base);

    printf("\n");
}

/// @brief Test rt_dir_files.
static void test_files() {
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
    for (int64_t i = 0; i < count; i++) {
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

/// @brief Test rt_dir_files excludes symlinks to files on POSIX.
#ifndef _WIN32
static void test_files_excludes_file_symlink() {
    printf("Testing rt_dir_files symlink filtering:\n");

    const char *base = get_test_base();
    char file_path[512], link_path[512];
    snprintf(file_path, sizeof(file_path), "%s/real.txt", base);
    snprintf(link_path, sizeof(link_path), "%s/link.txt", base);

    mkdir_p(base);
    create_file(file_path);
    (void)unlink(link_path);
    assert(symlink(file_path, link_path) == 0);

    void *files = rt_dir_files(rt_const_cstr(base));
    int64_t count = rt_seq_len(files);
    bool found_real = false;
    bool found_link = false;
    for (int64_t i = 0; i < count; ++i) {
        rt_string entry = (rt_string)rt_seq_get(files, i);
        if (rt_str_eq(entry, rt_const_cstr("real.txt")))
            found_real = true;
        if (rt_str_eq(entry, rt_const_cstr("link.txt")))
            found_link = true;
    }

    test_result("regular file included", found_real);
    test_result("symlink to file excluded", !found_link);

    unlink(link_path);
    remove_file(file_path);
    rmdir_p(base);
    printf("\n");
}

/// @brief Test rt_dir_remove_all removes a top-level symlink without deleting its target.
static void test_remove_all_symlink_does_not_delete_target() {
    printf("Testing rt_dir_remove_all top-level symlink safety:\n");

    const char *base = get_test_base();
    char target_dir[512], target_file[512], link_path[512];
    snprintf(target_dir, sizeof(target_dir), "%s_target", base);
    snprintf(target_file, sizeof(target_file), "%s_target/keep.txt", base);
    snprintf(link_path, sizeof(link_path), "%s_link", base);

    mkdir_p(target_dir);
    create_file(target_file);
    (void)unlink(link_path);
    assert(symlink(target_dir, link_path) == 0);

    rt_dir_remove_all(rt_const_cstr(link_path));

    struct stat st;
    test_result("symlink removed", lstat(link_path, &st) != 0);
    test_result("target directory preserved", rt_dir_exists(rt_const_cstr(target_dir)) == 1);

    remove_file(target_file);
    rmdir_p(target_dir);
    printf("\n");
}
#endif

/// @brief Test rt_dir_dirs.
static void test_dirs() {
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
    for (int64_t i = 0; i < count; i++) {
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

/// @brief Test Seq-returning wrappers for directory listing.
static void test_list_seq_wrappers() {
    printf("Testing rt_dir_*_seq wrappers:\n");

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

    void *list = rt_dir_list_seq(path);
    int64_t list_count = rt_seq_len(list);
    test_result("list_seq has 3 entries", list_count == 3);

    bool found_subdir = false;
    bool found_file1 = false;
    bool found_file2 = false;
    for (int64_t i = 0; i < list_count; i++) {
        rt_string entry = (rt_string)rt_seq_get(list, i);
        if (rt_str_eq(entry, rt_const_cstr("subdir")))
            found_subdir = true;
        if (rt_str_eq(entry, rt_const_cstr("file1.txt")))
            found_file1 = true;
        if (rt_str_eq(entry, rt_const_cstr("file2.txt")))
            found_file2 = true;
    }
    test_result("list_seq found subdir", found_subdir);
    test_result("list_seq found file1", found_file1);
    test_result("list_seq found file2", found_file2);

    void *files = rt_dir_files_seq(path);
    int64_t files_count = rt_seq_len(files);
    test_result("files_seq has 2 entries", files_count == 2);

    bool files_has_subdir = false;
    for (int64_t i = 0; i < files_count; i++) {
        rt_string entry = (rt_string)rt_seq_get(files, i);
        if (rt_str_eq(entry, rt_const_cstr("subdir")))
            files_has_subdir = true;
    }
    test_result("files_seq excludes subdir", !files_has_subdir);

    void *dirs = rt_dir_dirs_seq(path);
    int64_t dirs_count = rt_seq_len(dirs);
    test_result("dirs_seq has 1 entry", dirs_count == 1);

    bool dirs_has_file1 = false;
    for (int64_t i = 0; i < dirs_count; i++) {
        rt_string entry = (rt_string)rt_seq_get(dirs, i);
        if (rt_str_eq(entry, rt_const_cstr("file1.txt")))
            dirs_has_file1 = true;
    }
    test_result("dirs_seq excludes files", !dirs_has_file1);

    // Clean up (sequences leak in tests, this is fine)
    remove_file(file1);
    remove_file(file2);
    rmdir_p(subdir);
    rmdir_p(base);

    printf("\n");
}

/// @brief Test rt_dir_current and rt_dir_set_current.
static void test_current() {
    printf("Testing rt_dir_current and rt_dir_set_current:\n");

    // Save current directory
    rt_string original = rt_dir_current();
    test_result("current returns non-empty", rt_str_len(original) > 0);

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
static void test_move() {
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

static void test_move_existing_destination_traps() {
    printf("Testing rt_dir_move existing destination:\n");

    const char *base = get_test_base();
    char src[512], dst[512];
    snprintf(src, sizeof(src), "%s_move_existing_src", base);
    snprintf(dst, sizeof(dst), "%s_move_existing_dst", base);

    mkdir_p(src);
    mkdir_p(dst);

    EXPECT_TRAP(rt_dir_move(rt_const_cstr(src), rt_const_cstr(dst)));
    test_result("move destination exists traps", rt_dir_exists(rt_const_cstr(src)) == 1 &&
                                                     rt_dir_exists(rt_const_cstr(dst)) == 1);

    rmdir_p(src);
    rmdir_p(dst);

    printf("\n");
}

/// @brief Test empty directory listing.
static void test_empty_dir() {
    printf("Testing empty directory:\n");

    const char *base = get_test_base();
    mkdir_p(base);

    rt_string path = rt_const_cstr(base);

    void *list = rt_dir_list(path);
    test_result("empty list has 0 entries", rt_seq_len(list) == 0);

    void *entries = rt_dir_entries_seq(path);
    test_result("empty entries has 0 entries", rt_seq_len(entries) == 0);

    void *files = rt_dir_files(path);
    test_result("empty files has 0 entries", rt_seq_len(files) == 0);

    void *dirs = rt_dir_dirs(path);
    test_result("empty dirs has 0 entries", rt_seq_len(dirs) == 0);

    rmdir_p(base);

    printf("\n");
}

/// @brief Test non-existent directory listing.
static void test_nonexistent_dir() {
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

/// @brief Test rt_dir_entries_seq traps on non-existent directory.
static void test_entries_missing_dir_traps() {
    printf("Testing missing directory traps:\n");

    const char *base = get_test_base();
    char missing[512];
    snprintf(missing, sizeof(missing), "%s_missing", base);
    (void)rmdir_p(missing);

    rt_string path = rt_const_cstr(missing);
    EXPECT_TRAP(rt_dir_entries_seq(path));

    bool ok = g_last_trap && strstr(g_last_trap, "Viper.IO.Dir.Entries") != NULL;
    test_result("trap message mentions Entries", ok);

    printf("\n");
}

/// @brief Entry point for directory tests.
int main() {
#ifdef _WIN32
    // Skip on Windows: test uses /tmp paths not available on Windows
    VIPER_PLATFORM_SKIP("POSIX temp paths not available on Windows");
#endif
    printf("=== RT Dir Tests ===\n\n");

    test_exists();
    test_make_remove();
    test_make_existing_file_traps();
    test_make_all();
    test_make_all_backslash_path();
    test_make_all_existing_file_traps();
    test_remove_all();
    test_remove_all_missing_is_success();
    test_remove_all_protected_paths_trap();
    test_remove_all_current_ancestor_traps();
    test_list();
    test_entries();
    test_files();
#ifndef _WIN32
    test_files_excludes_file_symlink();
    test_remove_all_symlink_does_not_delete_target();
#endif
    test_dirs();
    test_list_seq_wrappers();
    test_current();
    test_move();
    test_move_existing_destination_traps();
    test_empty_dir();
    test_nonexistent_dir();
    test_entries_missing_dir_traps();

    printf("All directory tests passed!\n");
    return 0;
}
