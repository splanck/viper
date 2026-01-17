//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTArchiveTests.cpp
// Purpose: Validate Viper.IO.Archive ZIP archive support.
// Key invariants: Round-trip create/read preserves data, ZIP format compatibility.
// Links: docs/viperlib/io.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_archive.h"
#include "rt_box.h"
#include "rt_bytes.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define rmdir _rmdir
#define unlink _unlink
#else
#include "tests/common/PosixCompat.h"
#endif

namespace
{
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_last_trap = msg;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

/// @brief Get bytes data pointer
static uint8_t *get_bytes_data(void *bytes)
{
    struct bytes_impl
    {
        int64_t len;
        uint8_t *data;
    };

    return ((bytes_impl *)bytes)->data;
}

/// @brief Get bytes length
static int64_t get_bytes_len(void *bytes)
{
    return rt_bytes_len(bytes);
}

/// @brief Compare two byte arrays
static bool bytes_equal(void *a, void *b)
{
    int64_t len_a = get_bytes_len(a);
    int64_t len_b = get_bytes_len(b);
    if (len_a != len_b)
        return false;
    return memcmp(get_bytes_data(a), get_bytes_data(b), len_a) == 0;
}

/// @brief Create bytes from string literal
static void *make_bytes_str(const char *str)
{
    size_t len = strlen(str);
    void *bytes = rt_bytes_new((int64_t)len);
    memcpy(get_bytes_data(bytes), str, len);
    return bytes;
}

/// @brief Get a temporary file path for testing
/// @note Uses rotating buffers to allow multiple concurrent paths
static const char *get_temp_path(const char *name)
{
    static char paths[4][256];
    static int idx = 0;
    char *path = paths[idx];
    idx = (idx + 1) % 4;
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp)
        tmp = ".";
    snprintf(path, 256, "%s\\%s", tmp, name);
#else
    snprintf(path, 256, "/tmp/%s", name);
#endif
    return path;
}

/// @brief Delete a file if it exists
static void delete_file(const char *path)
{
    unlink(path);
}

//=============================================================================
// Basic Archive Tests
//=============================================================================

static void test_create_empty_archive()
{
    printf("Testing Create Empty Archive:\n");

    const char *path = get_temp_path("test_empty.zip");
    delete_file(path);

    // Create archive
    void *ar = rt_archive_create(rt_const_cstr(path));
    test_result("Create returns non-null", ar != NULL);

    // Finish immediately (empty archive)
    rt_archive_finish(ar);

    // Verify file exists and is valid ZIP
    test_result("IsZip returns true", rt_archive_is_zip(rt_const_cstr(path)) == 1);

    // Open and verify
    void *ar2 = rt_archive_open(rt_const_cstr(path));
    test_result("Open returns non-null", ar2 != NULL);
    test_result("Count is 0", rt_archive_count(ar2) == 0);

    delete_file(path);
}

static void test_create_single_file()
{
    printf("Testing Create Single File:\n");

    const char *path = get_temp_path("test_single.zip");
    delete_file(path);

    // Create archive with one file
    void *ar = rt_archive_create(rt_const_cstr(path));
    void *content = make_bytes_str("Hello, World!");
    rt_archive_add(ar, rt_const_cstr("hello.txt"), content);
    rt_archive_finish(ar);

    // Reopen and verify
    void *ar2 = rt_archive_open(rt_const_cstr(path));
    test_result("Count is 1", rt_archive_count(ar2) == 1);
    test_result("Has entry", rt_archive_has(ar2, rt_const_cstr("hello.txt")) == 1);

    void *read_content = rt_archive_read(ar2, rt_const_cstr("hello.txt"));
    test_result("Content matches", bytes_equal(content, read_content));

    delete_file(path);
}

static void test_create_multiple_files()
{
    printf("Testing Create Multiple Files:\n");

    const char *path = get_temp_path("test_multi.zip");
    delete_file(path);

    // Create archive with multiple files
    void *ar = rt_archive_create(rt_const_cstr(path));

    void *content1 = make_bytes_str("File 1 content");
    void *content2 = make_bytes_str("File 2 has different content");
    void *content3 = make_bytes_str("Third file");

    rt_archive_add(ar, rt_const_cstr("file1.txt"), content1);
    rt_archive_add(ar, rt_const_cstr("file2.txt"), content2);
    rt_archive_add(ar, rt_const_cstr("subdir/file3.txt"), content3);

    rt_archive_finish(ar);

    // Reopen and verify
    void *ar2 = rt_archive_open(rt_const_cstr(path));
    test_result("Count is 3", rt_archive_count(ar2) == 3);

    test_result("Has file1", rt_archive_has(ar2, rt_const_cstr("file1.txt")) == 1);
    test_result("Has file2", rt_archive_has(ar2, rt_const_cstr("file2.txt")) == 1);
    test_result("Has subdir/file3", rt_archive_has(ar2, rt_const_cstr("subdir/file3.txt")) == 1);
    test_result("No missing file", rt_archive_has(ar2, rt_const_cstr("missing.txt")) == 0);

    void *r1 = rt_archive_read(ar2, rt_const_cstr("file1.txt"));
    void *r2 = rt_archive_read(ar2, rt_const_cstr("file2.txt"));
    void *r3 = rt_archive_read(ar2, rt_const_cstr("subdir/file3.txt"));

    test_result("Content1 matches", bytes_equal(content1, r1));
    test_result("Content2 matches", bytes_equal(content2, r2));
    test_result("Content3 matches", bytes_equal(content3, r3));

    delete_file(path);
}

static void test_add_string()
{
    printf("Testing AddStr:\n");

    const char *path = get_temp_path("test_addstr.zip");
    delete_file(path);

    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add_str(ar, rt_const_cstr("text.txt"), rt_const_cstr("Hello from string!"));
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    rt_string text = rt_archive_read_str(ar2, rt_const_cstr("text.txt"));
    test_result("ReadStr works", strcmp(rt_string_cstr(text), "Hello from string!") == 0);

    delete_file(path);
}

static void test_add_directory()
{
    printf("Testing AddDir:\n");

    const char *path = get_temp_path("test_dir.zip");
    delete_file(path);

    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add_dir(ar, rt_const_cstr("mydir"));
    rt_archive_add_str(ar, rt_const_cstr("mydir/file.txt"), rt_const_cstr("Inside dir"));
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    test_result("Count is 2", rt_archive_count(ar2) == 2);
    test_result("Has directory", rt_archive_has(ar2, rt_const_cstr("mydir/")) == 1);

    // Check info for directory
    void *info = rt_archive_info(ar2, rt_const_cstr("mydir/"));
    void *is_dir = rt_map_get(info, rt_const_cstr("isDirectory"));
    test_result("isDirectory is true", rt_unbox_i1(is_dir) == 1);

    delete_file(path);
}

static void test_invalid_entry_names()
{
    printf("Testing Invalid Entry Names:\n");

    const char *path = get_temp_path("test_invalid_names.zip");
    delete_file(path);

    void *ar = rt_archive_create(rt_const_cstr(path));
    void *content = make_bytes_str("payload");

    EXPECT_TRAP(rt_archive_add(ar, rt_const_cstr("../evil.txt"), content));
    EXPECT_TRAP(rt_archive_add(ar, rt_const_cstr("..\\evil.txt"), content));
    EXPECT_TRAP(rt_archive_add(ar, rt_const_cstr("/absolute.txt"), content));
    EXPECT_TRAP(rt_archive_add(ar, rt_const_cstr("C:\\absolute.txt"), content));

    rt_archive_add(ar, rt_const_cstr("subdir\\file.txt"), content);
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    test_result("Normalized name found",
                rt_archive_has(ar2, rt_const_cstr("subdir/file.txt")) == 1);
    EXPECT_TRAP(rt_archive_read(ar2, rt_const_cstr("../missing.txt")));

    delete_file(path);
}

//=============================================================================
// Compression Tests
//=============================================================================

static void test_compression_stored()
{
    printf("Testing Stored Compression:\n");

    const char *path = get_temp_path("test_stored.zip");
    delete_file(path);

    // Small data should be stored uncompressed
    void *ar = rt_archive_create(rt_const_cstr(path));
    void *small = make_bytes_str("Small data");
    rt_archive_add(ar, rt_const_cstr("small.txt"), small);
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    void *read_small = rt_archive_read(ar2, rt_const_cstr("small.txt"));
    test_result("Small data round-trip", bytes_equal(small, read_small));

    delete_file(path);
}

static void test_compression_deflate()
{
    printf("Testing Deflate Compression:\n");

    const char *path = get_temp_path("test_deflate.zip");
    delete_file(path);

    // Create compressible data (repeated pattern)
    char buffer[2000];
    for (int i = 0; i < 2000; i++)
    {
        buffer[i] = 'A' + (i % 26);
    }
    void *large = rt_bytes_new(2000);
    memcpy(get_bytes_data(large), buffer, 2000);

    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add(ar, rt_const_cstr("large.txt"), large);
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));

    // Check compression worked
    void *info = rt_archive_info(ar2, rt_const_cstr("large.txt"));
    void *size = rt_map_get(info, rt_const_cstr("size"));
    void *comp_size = rt_map_get(info, rt_const_cstr("compressedSize"));

    int64_t orig_size = rt_unbox_i64(size);
    int64_t compressed_size = rt_unbox_i64(comp_size);

    test_result("Size correct", orig_size == 2000);
    test_result("Compression occurred", compressed_size < orig_size);

    printf("    Original: %lld bytes, Compressed: %lld bytes (%.1f%%)\n",
           (long long)orig_size,
           (long long)compressed_size,
           100.0 * compressed_size / orig_size);

    // Verify content
    void *read_large = rt_archive_read(ar2, rt_const_cstr("large.txt"));
    test_result("Large data round-trip", bytes_equal(large, read_large));

    delete_file(path);
}

//=============================================================================
// Property Tests
//=============================================================================

static void test_properties()
{
    printf("Testing Properties:\n");

    const char *path = get_temp_path("test_props.zip");
    delete_file(path);

    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add_str(ar, rt_const_cstr("a.txt"), rt_const_cstr("A"));
    rt_archive_add_str(ar, rt_const_cstr("b.txt"), rt_const_cstr("B"));
    rt_archive_add_str(ar, rt_const_cstr("c.txt"), rt_const_cstr("C"));
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));

    // Test Path property
    rt_string ar_path = rt_archive_path(ar2);
    test_result("Path not empty", strlen(rt_string_cstr(ar_path)) > 0);

    // Test Count property
    test_result("Count is 3", rt_archive_count(ar2) == 3);

    // Test Names property
    void *names = rt_archive_names(ar2);
    test_result("Names has 3 entries", rt_seq_len(names) == 3);

    delete_file(path);
}

static void test_entry_info()
{
    printf("Testing Entry Info:\n");

    const char *path = get_temp_path("test_info.zip");
    delete_file(path);

    void *ar = rt_archive_create(rt_const_cstr(path));
    void *content = make_bytes_str("Test content for info");
    rt_archive_add(ar, rt_const_cstr("info.txt"), content);
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    void *info = rt_archive_info(ar2, rt_const_cstr("info.txt"));

    // Check all expected keys
    test_result("Has size key", rt_map_has(info, rt_const_cstr("size")) == 1);
    test_result("Has compressedSize key", rt_map_has(info, rt_const_cstr("compressedSize")) == 1);
    test_result("Has modifiedTime key", rt_map_has(info, rt_const_cstr("modifiedTime")) == 1);
    test_result("Has isDirectory key", rt_map_has(info, rt_const_cstr("isDirectory")) == 1);

    // Verify values
    void *size = rt_map_get(info, rt_const_cstr("size"));
    test_result("Size correct", rt_unbox_i64(size) == get_bytes_len(content));

    void *is_dir = rt_map_get(info, rt_const_cstr("isDirectory"));
    test_result("isDirectory is false", rt_unbox_i1(is_dir) == 0);

    delete_file(path);
}

//=============================================================================
// FromBytes Tests
//=============================================================================

static void test_from_bytes()
{
    printf("Testing FromBytes:\n");

    const char *path = get_temp_path("test_frombytes.zip");
    delete_file(path);

    // Create a ZIP file
    void *ar = rt_archive_create(rt_const_cstr(path));
    void *content = make_bytes_str("Memory test content");
    rt_archive_add(ar, rt_const_cstr("memory.txt"), content);
    rt_archive_finish(ar);

    // Read the ZIP file into bytes
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *zip_bytes = rt_bytes_new(size);
    size_t read_count = fread(get_bytes_data(zip_bytes), 1, size, f);
    fclose(f);
    assert((long)read_count == size);

    // Open from bytes
    void *ar2 = rt_archive_from_bytes(zip_bytes);
    test_result("FromBytes returns non-null", ar2 != NULL);
    test_result("Count is 1", rt_archive_count(ar2) == 1);

    void *read_content = rt_archive_read(ar2, rt_const_cstr("memory.txt"));
    test_result("Content matches", bytes_equal(content, read_content));

    // Path should be empty for FromBytes
    rt_string ar_path = rt_archive_path(ar2);
    test_result("Path is empty", strlen(rt_string_cstr(ar_path)) == 0);

    delete_file(path);
}

//=============================================================================
// Static Methods Tests
//=============================================================================

static void test_is_zip()
{
    printf("Testing IsZip:\n");

    const char *zip_path = get_temp_path("test_iszip.zip");
    const char *txt_path = get_temp_path("test_iszip.txt");
    delete_file(zip_path);
    delete_file(txt_path);

    // Create a valid ZIP
    void *ar = rt_archive_create(rt_const_cstr(zip_path));
    rt_archive_add_str(ar, rt_const_cstr("test.txt"), rt_const_cstr("test"));
    rt_archive_finish(ar);

    // Create a non-ZIP file
    FILE *f = fopen(txt_path, "w");
    fprintf(f, "This is not a ZIP file");
    fclose(f);

    // Test IsZip
    test_result("IsZip on ZIP returns true", rt_archive_is_zip(rt_const_cstr(zip_path)) == 1);
    test_result("IsZip on TXT returns false", rt_archive_is_zip(rt_const_cstr(txt_path)) == 0);
    test_result("IsZip on missing returns false",
                rt_archive_is_zip(rt_const_cstr("/nonexistent/file.zip")) == 0);

    delete_file(zip_path);
    delete_file(txt_path);
}

static void test_is_zip_bytes()
{
    printf("Testing IsZipBytes:\n");

    const char *path = get_temp_path("test_iszipbytes.zip");
    delete_file(path);

    // Create a valid ZIP
    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add_str(ar, rt_const_cstr("test.txt"), rt_const_cstr("test"));
    rt_archive_finish(ar);

    // Read into bytes
    FILE *f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *zip_bytes = rt_bytes_new(size);
    fread(get_bytes_data(zip_bytes), 1, size, f);
    fclose(f);

    // Create non-ZIP bytes
    void *txt_bytes = make_bytes_str("Not a ZIP file");

    test_result("IsZipBytes on ZIP returns true", rt_archive_is_zip_bytes(zip_bytes) == 1);
    test_result("IsZipBytes on text returns false", rt_archive_is_zip_bytes(txt_bytes) == 0);

    delete_file(path);
}

//=============================================================================
// Binary Data Tests
//=============================================================================

static void test_binary_data()
{
    printf("Testing Binary Data:\n");

    const char *path = get_temp_path("test_binary.zip");
    delete_file(path);

    // Create binary data with all byte values
    void *binary = rt_bytes_new(256);
    uint8_t *data = get_bytes_data(binary);
    for (int i = 0; i < 256; i++)
    {
        data[i] = (uint8_t)i;
    }

    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add(ar, rt_const_cstr("binary.bin"), binary);
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    void *read_binary = rt_archive_read(ar2, rt_const_cstr("binary.bin"));
    test_result("Binary data round-trip", bytes_equal(binary, read_binary));

    delete_file(path);
}

static void test_large_file()
{
    printf("Testing Large File:\n");

    const char *path = get_temp_path("test_large.zip");
    delete_file(path);

    // 100KB of data
    size_t size = 100 * 1024;
    void *large = rt_bytes_new((int64_t)size);
    uint8_t *data = get_bytes_data(large);
    for (size_t i = 0; i < size; i++)
    {
        data[i] = (uint8_t)(i & 0xFF);
    }

    void *ar = rt_archive_create(rt_const_cstr(path));
    rt_archive_add(ar, rt_const_cstr("large.bin"), large);
    rt_archive_finish(ar);

    void *ar2 = rt_archive_open(rt_const_cstr(path));
    void *read_large = rt_archive_read(ar2, rt_const_cstr("large.bin"));
    test_result("Large file round-trip", bytes_equal(large, read_large));

    delete_file(path);
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
#ifdef _WIN32
    // Skip on Windows: test uses /tmp paths not available on Windows
    printf("Test skipped: POSIX temp paths not available on Windows\n");
    return 0;
#endif
    printf("=== RT Archive Tests ===\n\n");

    // Basic tests
    test_create_empty_archive();
    printf("\n");
    test_create_single_file();
    printf("\n");
    test_create_multiple_files();
    printf("\n");
    test_add_string();
    printf("\n");
    test_add_directory();
    printf("\n");
    test_invalid_entry_names();
    printf("\n");

    // Compression tests
    test_compression_stored();
    printf("\n");
    test_compression_deflate();
    printf("\n");

    // Property tests
    test_properties();
    printf("\n");
    test_entry_info();
    printf("\n");

    // FromBytes tests
    test_from_bytes();
    printf("\n");

    // Static method tests
    test_is_zip();
    printf("\n");
    test_is_zip_bytes();
    printf("\n");

    // Binary data tests
    test_binary_data();
    printf("\n");
    test_large_file();
    printf("\n");

    printf("All Archive tests passed!\n");
    return 0;
}
