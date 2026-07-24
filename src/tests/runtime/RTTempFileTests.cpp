//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTempFileTests.cpp
// Purpose: Validate temporary file utilities.
//
//===----------------------------------------------------------------------===//

#include "rt_dir.h"
#include "rt_file_path.h"
#include "rt_internal.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "rt_tempfile.h"

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if RT_PLATFORM_WINDOWS
#include <string>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
} // namespace

#if RT_PLATFORM_WINDOWS
class ScopedWindowsEnvironment {
  public:
    explicit ScopedWindowsEnvironment(const wchar_t *name) : name_(name) {
        DWORD required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
        if (required == 0)
            return;
        value_.resize(required);
        DWORD length = GetEnvironmentVariableW(name_.c_str(), value_.data(), required);
        if (length > 0 && length < required) {
            value_.resize(length);
            present_ = true;
        }
    }

    ~ScopedWindowsEnvironment() {
        (void)SetEnvironmentVariableW(name_.c_str(), present_ ? value_.c_str() : nullptr);
    }

    ScopedWindowsEnvironment(const ScopedWindowsEnvironment &) = delete;
    ScopedWindowsEnvironment &operator=(const ScopedWindowsEnvironment &) = delete;

  private:
    std::wstring name_;
    std::wstring value_;
    bool present_{false};
};
#endif

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
        assert(g_last_trap != nullptr);                                                            \
    } while (0)

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed) {
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// TempFile Tests
//=============================================================================

static void test_tempfile() {
    printf("Testing TempFile:\n");

    // Test 1: Dir returns a valid path
    {
        rt_string dir = rt_tempfile_dir();
        test_result("Dir returns non-empty", rt_str_len(dir) > 0);
        test_result("Dir exists", rt_dir_exists(dir) == 1);
    }

    // Test 2: Path generates unique paths
    {
        rt_string path1 = rt_tempfile_path();
        rt_string path2 = rt_tempfile_path();
        test_result("Path generates unique paths",
                    strcmp(rt_string_cstr(path1), rt_string_cstr(path2)) != 0);
    }

    // Test 3: PathWithPrefix includes prefix
    {
        rt_string path = rt_tempfile_path_with_prefix(rt_const_cstr("mytest_"));
        const char *path_cstr = rt_string_cstr(path);
        test_result("PathWithPrefix includes prefix", strstr(path_cstr, "mytest_") != NULL);
    }

    // Test 4: PathWithExt includes extension
    {
        rt_string path = rt_tempfile_path_with_ext(rt_const_cstr("test_"), rt_const_cstr(".log"));
        const char *path_cstr = rt_string_cstr(path);
        test_result("PathWithExt includes extension", strstr(path_cstr, ".log") != NULL);
    }

    // Test 5: Create actually creates a file
    {
        rt_string path = rt_tempfile_create();
        // Check file was created (it should exist)
#if RT_PLATFORM_WINDOWS
        wchar_t *wide_path = rt_file_path_utf8_to_wide(rt_string_cstr(path));
        FILE *f = wide_path ? _wfopen(wide_path, L"rb") : nullptr;
#else
        FILE *f = fopen(rt_string_cstr(path), "r");
#endif
        test_result("Create creates file", f != NULL);
        if (f) {
            fclose(f);
            // Clean up
#if RT_PLATFORM_WINDOWS
            (void)_wremove(wide_path);
#else
            remove(rt_string_cstr(path));
#endif
        }
#if RT_PLATFORM_WINDOWS
        free(wide_path);
#endif
    }

    // Test 6: CreateDir creates a directory
    {
        rt_string path = rt_tempdir_create();
        test_result("CreateDir creates directory", rt_dir_exists(path) == 1);
        // Clean up
        rt_dir_remove(path);
    }

#if !RT_PLATFORM_WINDOWS
    // Test 7: Root TMPDIR remains absolute instead of becoming an empty path
    {
        const char *old_tmpdir = getenv("TMPDIR");
        char old_copy[512] = {0};
        if (old_tmpdir)
            snprintf(old_copy, sizeof(old_copy), "%s", old_tmpdir);
        setenv("TMPDIR", "/", 1);
        rt_string dir = rt_tempfile_dir();
        test_result("root TMPDIR preserved", strcmp(rt_string_cstr(dir), "/") == 0);
        if (old_tmpdir)
            setenv("TMPDIR", old_copy, 1);
        else
            unsetenv("TMPDIR");
    }
#endif

#if RT_PLATFORM_WINDOWS
    // Test 8: A drive-root temp path remains absolute and root-qualified.
    {
        ScopedWindowsEnvironment saved_tmp(L"TMP");
        ScopedWindowsEnvironment saved_temp(L"TEMP");
        assert(SetEnvironmentVariableW(L"TMP", L"C:\\"));
        assert(SetEnvironmentVariableW(L"TEMP", L"C:\\"));
        rt_string dir = rt_tempfile_dir();
        test_result("drive-root temp path preserved", strcmp(rt_string_cstr(dir), "C:\\") == 0);
        rt_string_unref(dir);
    }
#endif

#if !RT_PLATFORM_WINDOWS
    // Test 8: Invalid TMPDIR values fall back instead of being trusted.
    {
        char bad_tmpdir[256];
        snprintf(bad_tmpdir, sizeof(bad_tmpdir), "/tmp/zanna_tmpdir_file_%d", (int)getpid());
        FILE *fp = fopen(bad_tmpdir, "wb");
        assert(fp != nullptr);
        fclose(fp);

        const char *old_tmpdir = getenv("TMPDIR");
        char old_copy[512] = {0};
        if (old_tmpdir)
            snprintf(old_copy, sizeof(old_copy), "%s", old_tmpdir);
        setenv("TMPDIR", bad_tmpdir, 1);
        rt_string dir = rt_tempfile_dir();
        test_result("file TMPDIR rejected", strcmp(rt_string_cstr(dir), "/tmp") == 0);
        if (old_tmpdir)
            setenv("TMPDIR", old_copy, 1);
        else
            unsetenv("TMPDIR");
        remove(bad_tmpdir);
    }
#endif

    // Test 9: Prefix and extension are filename fragments, not paths.
    {
        EXPECT_TRAP(rt_tempfile_path_with_prefix(rt_const_cstr("../bad")));
        EXPECT_TRAP(rt_tempfile_path_with_ext(rt_const_cstr("ok_"), rt_const_cstr("../bad")));

        const char raw_prefix[] = {'o', 'k', '\0', '/', 'b', 'a', 'd'};
        rt_string nul_prefix = rt_string_from_bytes(raw_prefix, sizeof(raw_prefix));
        EXPECT_TRAP(rt_tempfile_path_with_prefix(nul_prefix));
        rt_string_unref(nul_prefix);

        const char raw_ext[] = {'.', 't', 'm', 'p', '\0', '/', 'b', 'a', 'd'};
        rt_string nul_ext = rt_string_from_bytes(raw_ext, sizeof(raw_ext));
        EXPECT_TRAP(rt_tempfile_path_with_ext(rt_const_cstr("ok_"), nul_ext));
        rt_string_unref(nul_ext);
    }

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main() {
    printf("=== RT TempFile Tests ===\n\n");

    test_tempfile();

    printf("All TempFile tests passed!\n");
    return 0;
}
