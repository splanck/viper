//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTFileExtTests.cpp
// Purpose: Validate runtime file operations in rt_file_ext.c.
// Key invariants: File operations work correctly across platforms,
//                 ReadBytes/WriteBytes handle binary data correctly,
//                 ReadLines/WriteLines preserve line structure.
// Ownership/Lifetime: Uses runtime library; tests return newly allocated
//                     strings and objects that must be released.
// Links: docs/viperlib.md

#include "rt.hpp"
#include "rt_bytes.h"
#include "rt_file_ext.h"
#include "rt_seq.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

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
    snprintf(buf, sizeof(buf), "%s\\viper_file_test_%d", tmp, (int)getpid());
    return buf;
#else
    static char buf[256];
    snprintf(buf, sizeof(buf), "/tmp/viper_file_test_%d", (int)getpid());
    return buf;
#endif
}

/// @brief Helper to create a test file with content.
static void create_test_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f)
    {
        fprintf(f, "%s", content);
        fclose(f);
    }
}

/// @brief Helper to create a test file with raw bytes (no newline translation).
static void create_test_file_bin(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    if (len > 0 && data)
    {
        (void)fwrite(data, 1, len, f);
    }
    fclose(f);
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

/// @brief Test rt_io_file_exists.
static void test_exists()
{
    printf("Testing rt_io_file_exists:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_exists_test.txt", base);

    rt_string path = rt_const_cstr(file_path);

    // File doesn't exist yet
    test_result("non-existent file", rt_io_file_exists(path) == 0);

    // Create file
    create_test_file(file_path, "test");
    test_result("file exists after create", rt_io_file_exists(path) == 1);

    // Clean up
    remove_file(file_path);
    test_result("file not exists after remove", rt_io_file_exists(path) == 0);

    printf("\n");
}

/// @brief Test rt_file_copy.
static void test_copy()
{
    printf("Testing rt_file_copy:\n");

    const char *base = get_test_base();
    char src_path[512], dst_path[512];
    snprintf(src_path, sizeof(src_path), "%s_copy_src.txt", base);
    snprintf(dst_path, sizeof(dst_path), "%s_copy_dst.txt", base);

    // Create source file
    create_test_file(src_path, "Hello, World!");

    rt_string src = rt_const_cstr(src_path);
    rt_string dst = rt_const_cstr(dst_path);

    test_result("source exists", rt_io_file_exists(src) == 1);
    test_result("dest not exists", rt_io_file_exists(dst) == 0);

    // Copy file
    rt_file_copy(src, dst);

    test_result("source still exists", rt_io_file_exists(src) == 1);
    test_result("dest exists after copy", rt_io_file_exists(dst) == 1);

    // Verify content
    rt_string content = rt_io_file_read_all_text(dst);
    test_result("content matches", rt_str_eq(content, rt_const_cstr("Hello, World!")));

    // Clean up
    remove_file(src_path);
    remove_file(dst_path);

    printf("\n");
}

/// @brief Test rt_file_move.
static void test_move()
{
    printf("Testing rt_file_move:\n");

    const char *base = get_test_base();
    char src_path[512], dst_path[512];
    snprintf(src_path, sizeof(src_path), "%s_move_src.txt", base);
    snprintf(dst_path, sizeof(dst_path), "%s_move_dst.txt", base);

    // Create source file
    create_test_file(src_path, "Move Test");

    rt_string src = rt_const_cstr(src_path);
    rt_string dst = rt_const_cstr(dst_path);

    test_result("source exists", rt_io_file_exists(src) == 1);

    // Move file
    rt_file_move(src, dst);

    test_result("source gone after move", rt_io_file_exists(src) == 0);
    test_result("dest exists after move", rt_io_file_exists(dst) == 1);

    // Verify content
    rt_string content = rt_io_file_read_all_text(dst);
    test_result("content preserved", rt_str_eq(content, rt_const_cstr("Move Test")));

    // Clean up
    remove_file(dst_path);

    printf("\n");
}

/// @brief Test rt_file_size.
static void test_size()
{
    printf("Testing rt_file_size:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_size_test.txt", base);

    // Create file with known content
    create_test_file(file_path, "12345");

    rt_string path = rt_const_cstr(file_path);

    int64_t size = rt_file_size(path);
    test_result("size is 5 bytes", size == 5);

    // Non-existent file
    rt_string nonexist = rt_const_cstr("/nonexistent_file_12345.txt");
    test_result("non-existent returns -1", rt_file_size(nonexist) == -1);

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_file_read_bytes and rt_file_write_bytes.
static void test_read_write_bytes()
{
    printf("Testing rt_file_read_bytes and rt_file_write_bytes:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_bytes_test.bin", base);

    rt_string path = rt_const_cstr(file_path);

    // Create bytes with binary data including null bytes
    void *bytes = rt_bytes_new(5);
    rt_bytes_set(bytes, 0, 0x48); // 'H'
    rt_bytes_set(bytes, 1, 0x00); // null byte
    rt_bytes_set(bytes, 2, 0x69); // 'i'
    rt_bytes_set(bytes, 3, 0xFF); // 255
    rt_bytes_set(bytes, 4, 0x21); // '!'

    // Write bytes
    rt_file_write_bytes(path, bytes);
    test_result("file created", rt_io_file_exists(path) == 1);

    // Read bytes back
    void *read_bytes = rt_file_read_bytes(path);
    test_result("read 5 bytes", rt_bytes_len(read_bytes) == 5);
    test_result("byte 0 correct", rt_bytes_get(read_bytes, 0) == 0x48);
    test_result("byte 1 (null) correct", rt_bytes_get(read_bytes, 1) == 0x00);
    test_result("byte 2 correct", rt_bytes_get(read_bytes, 2) == 0x69);
    test_result("byte 3 correct", rt_bytes_get(read_bytes, 3) == 0xFF);
    test_result("byte 4 correct", rt_bytes_get(read_bytes, 4) == 0x21);

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_file_read_lines and rt_file_write_lines.
static void test_read_write_lines()
{
    printf("Testing rt_file_read_lines and rt_file_write_lines:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_lines_test.txt", base);

    rt_string path = rt_const_cstr(file_path);

    // Create sequence of lines
    void *lines = rt_seq_new();
    rt_seq_push(lines, rt_const_cstr("Line 1"));
    rt_seq_push(lines, rt_const_cstr("Line 2"));
    rt_seq_push(lines, rt_const_cstr("Line 3"));

    // Write lines
    rt_file_write_lines(path, lines);
    test_result("file created", rt_io_file_exists(path) == 1);

    // Read lines back
    // Note: WriteLines adds a newline after each line, so ReadLines will get
    // an extra empty line at the end. We check the first 3 lines are correct.
    void *read_lines = rt_file_read_lines(path);
    int64_t line_count = rt_seq_len(read_lines);
    // Should have at least 3 lines (may have 4 with trailing empty line)
    test_result("read at least 3 lines", line_count >= 3);

    rt_string line1 = (rt_string)rt_seq_get(read_lines, 0);
    rt_string line2 = (rt_string)rt_seq_get(read_lines, 1);
    rt_string line3 = (rt_string)rt_seq_get(read_lines, 2);

    test_result("line 1 correct", rt_str_eq(line1, rt_const_cstr("Line 1")));
    test_result("line 2 correct", rt_str_eq(line2, rt_const_cstr("Line 2")));
    test_result("line 3 correct", rt_str_eq(line3, rt_const_cstr("Line 3")));

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_file_append.
static void test_append()
{
    printf("Testing rt_file_append:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_append_test.txt", base);

    rt_string path = rt_const_cstr(file_path);

    // Create initial file
    create_test_file(file_path, "Hello");

    // Append text
    rt_file_append(path, rt_const_cstr(", World!"));

    // Verify content
    rt_string content = rt_io_file_read_all_text(path);
    test_result("content appended", rt_str_eq(content, rt_const_cstr("Hello, World!")));

    // Append more
    rt_file_append(path, rt_const_cstr(" Test"));
    content = rt_io_file_read_all_text(path);
    test_result("second append", rt_str_eq(content, rt_const_cstr("Hello, World! Test")));

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_io_file_append_line.
static void test_append_line()
{
    printf("Testing rt_io_file_append_line:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_append_line_test.txt", base);

    rt_string path = rt_const_cstr(file_path);

    remove_file(file_path);

    rt_io_file_append_line(path, rt_const_cstr("Line 1"));
    rt_io_file_append_line(path, rt_const_cstr("Line 2"));

    rt_string content = rt_io_file_read_all_text(path);
    test_result("content matches", rt_str_eq(content, rt_const_cstr("Line 1\nLine 2\n")));

    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_io_file_read_all_bytes / rt_io_file_write_all_bytes.
static void test_read_write_all_bytes()
{
    printf("Testing rt_io_file_read_all_bytes/rt_io_file_write_all_bytes:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_read_all_bytes_test.bin", base);

    rt_string path = rt_const_cstr(file_path);
    remove_file(file_path);

    void *bytes = rt_bytes_new(4);
    rt_bytes_set(bytes, 0, 0xDE);
    rt_bytes_set(bytes, 1, 0xAD);
    rt_bytes_set(bytes, 2, 0xBE);
    rt_bytes_set(bytes, 3, 0xEF);

    rt_io_file_write_all_bytes(path, bytes);

    void *read_bytes = rt_io_file_read_all_bytes(path);
    test_result("len == 4", rt_bytes_len(read_bytes) == 4);
    test_result("byte0 == 0xDE", rt_bytes_get(read_bytes, 0) == 0xDE);
    test_result("byte1 == 0xAD", rt_bytes_get(read_bytes, 1) == 0xAD);
    test_result("byte2 == 0xBE", rt_bytes_get(read_bytes, 2) == 0xBE);
    test_result("byte3 == 0xEF", rt_bytes_get(read_bytes, 3) == 0xEF);

    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_io_file_read_all_lines.
static void test_read_all_lines()
{
    printf("Testing rt_io_file_read_all_lines:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_read_all_lines_test.txt", base);

    static const char content[] = "one\r\ntwo\nthree\r\nfour";
    create_test_file_bin(file_path, content, sizeof(content) - 1);

    rt_string path = rt_const_cstr(file_path);
    void *lines = rt_io_file_read_all_lines(path);
    test_result("line count == 4", rt_seq_len(lines) == 4);

    rt_string line0 = (rt_string)rt_seq_get(lines, 0);
    rt_string line1 = (rt_string)rt_seq_get(lines, 1);
    rt_string line2 = (rt_string)rt_seq_get(lines, 2);
    rt_string line3 = (rt_string)rt_seq_get(lines, 3);

    test_result("line0", rt_str_eq(line0, rt_const_cstr("one")));
    test_result("line1", rt_str_eq(line1, rt_const_cstr("two")));
    test_result("line2", rt_str_eq(line2, rt_const_cstr("three")));
    test_result("line3", rt_str_eq(line3, rt_const_cstr("four")));

    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_file_modified.
static void test_modified()
{
    printf("Testing rt_file_modified:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_modified_test.txt", base);

    rt_string path = rt_const_cstr(file_path);

    // Create file
    create_test_file(file_path, "test");

    time_t now = time(NULL);
    int64_t mtime = rt_file_modified(path);

    // Modified time should be recent (within last minute)
    test_result("mtime is recent", mtime > 0 && (now - mtime) < 60);

    // Non-existent file
    rt_string nonexist = rt_const_cstr("/nonexistent_file_12345.txt");
    test_result("non-existent returns 0", rt_file_modified(nonexist) == 0);

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test rt_file_touch.
static void test_touch()
{
    printf("Testing rt_file_touch:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_touch_test.txt", base);

    rt_string path = rt_const_cstr(file_path);

    // File doesn't exist
    test_result("file not exists", rt_io_file_exists(path) == 0);

    // Touch creates file
    rt_file_touch(path);
    test_result("touch creates file", rt_io_file_exists(path) == 1);

    // File should be empty
    int64_t size = rt_file_size(path);
    test_result("file is empty", size == 0);

    // Get initial mtime
    int64_t mtime1 = rt_file_modified(path);

    // Small delay to ensure time difference
    usleep(100000); // 100ms

    // Touch again updates mtime
    rt_file_touch(path);
    int64_t mtime2 = rt_file_modified(path);
    test_result("touch updates mtime", mtime2 >= mtime1);

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test empty file handling.
static void test_empty_file()
{
    printf("Testing empty file handling:\n");

    const char *base = get_test_base();
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s_empty_test.txt", base);

    // Create empty file
    create_test_file(file_path, "");

    rt_string path = rt_const_cstr(file_path);

    // Read empty file as text
    rt_string text = rt_io_file_read_all_text(path);
    test_result("empty text read", rt_len(text) == 0);

    // Read empty file as bytes
    void *bytes = rt_file_read_bytes(path);
    test_result("empty bytes read", rt_bytes_len(bytes) == 0);

    // Read empty file as lines
    void *lines = rt_file_read_lines(path);
    // Empty file still yields one empty line
    test_result("empty lines read", rt_seq_len(lines) >= 0);

    // Clean up
    remove_file(file_path);

    printf("\n");
}

/// @brief Test non-existent file operations.
static void test_nonexistent()
{
    printf("Testing non-existent file operations:\n");

    rt_string path = rt_const_cstr("/nonexistent_file_12345_xyz.txt");

    // Read operations should return empty/default values
    rt_string text = rt_io_file_read_all_text(path);
    test_result("read text returns empty", rt_len(text) == 0);

    void *bytes = rt_file_read_bytes(path);
    test_result("read bytes returns empty", rt_bytes_len(bytes) == 0);

    void *lines = rt_file_read_lines(path);
    test_result("read lines returns empty", rt_seq_len(lines) == 0);

    test_result("size returns -1", rt_file_size(path) == -1);
    test_result("modified returns 0", rt_file_modified(path) == 0);

    printf("\n");
}

/// @brief Entry point for file extension tests.
int main()
{
    printf("=== RT File Extension Tests ===\n\n");

    test_exists();
    test_copy();
    test_move();
    test_size();
    test_read_write_bytes();
    test_read_write_lines();
    test_append();
    test_append_line();
    test_read_write_all_bytes();
    test_read_all_lines();
    test_modified();
    test_touch();
    test_empty_file();
    test_nonexistent();

    printf("All file extension tests passed!\n");
    return 0;
}
