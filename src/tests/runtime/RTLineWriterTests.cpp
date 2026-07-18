//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTLineWriterTests.cpp
// Purpose: Comprehensive tests for Zanna.IO.LineWriter text file writing.
// Key invariants: Buffered writes preserve ordering and all close/error paths
//                 leave the native descriptor in a terminal state.
// Ownership/Lifetime: Every case closes its writer and removes its temporary
//                     file before returning.
// Links: src/runtime/io/rt_linewriter.c, docs/zannalib/io/streams.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_linewriter.h"
#include "rt_platform.h"
#include "rt_string.h"
#include "tests/common/PlatformSkip.h"

#include <cassert>
#include <cerrno>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if RT_PLATFORM_WINDOWS
#include <process.h>
#include <windows.h>
#define GETPID _getpid
#else
#include <fcntl.h>
#include <unistd.h>
#define GETPID getpid
#endif

namespace {
static jmp_buf g_trap_jmp;
static const char *g_last_trap = nullptr;
static bool g_trap_expected = false;
static int g_alloc_countdown = 0;
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
        assert(g_last_trap != nullptr);                                                            \
    } while (0)

static const char *get_test_file() {
    static char path[512];
    static bool initialized = false;
    if (!initialized) {
#if RT_PLATFORM_WINDOWS
        const char *tmp = getenv("TEMP");
        if (!tmp)
            tmp = getenv("TMP");
        if (!tmp)
            tmp = ".";
        snprintf(path, sizeof(path), "%s\\zanna_linewriter_test_%d.txt", tmp, (int)GETPID());
#else
        snprintf(path, sizeof(path), "/tmp/zanna_linewriter_test_%d.txt", (int)GETPID());
#endif
        initialized = true;
    }
    return path;
}

static rt_string make_string(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

/// @brief Fail one selected runtime allocation and delegate all others.
/// @param bytes Requested byte count.
/// @param next Default allocator supplied by the runtime.
/// @return NULL for the selected allocation, otherwise the default result.
static void *fail_countdown_alloc(int64_t bytes, void *(*next)(int64_t)) {
    if (g_alloc_countdown > 0 && --g_alloc_countdown == 0)
        return nullptr;
    return next ? next(bytes) : nullptr;
}

/// @brief Count native process resources without perturbing the descriptor table.
/// @return Current Windows handle count or occupied low POSIX descriptor count.
static uint64_t process_open_resource_count() {
#if RT_PLATFORM_WINDOWS
    DWORD count = 0;
    assert(GetProcessHandleCount(GetCurrentProcess(), &count) != 0);
    return (uint64_t)count;
#else
    uint64_t count = 0;
    for (int fd = 0; fd < 4096; ++fd) {
        errno = 0;
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
            ++count;
    }
    return count;
#endif
}

static void cleanup_test_file() {
    remove(get_test_file());
}

static char *read_file_contents(size_t *out_len) {
    FILE *fp = fopen(get_test_file(), "rb");
    if (!fp) {
        *out_len = 0;
        return nullptr;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)len + 1);
    size_t read = fread(buf, 1, (size_t)len, fp);
    buf[read] = '\0';
    fclose(fp);

    *out_len = read;
    return buf;
}

static void test_open_close() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_linewriter_close(lw);
    // Should be able to close twice without issue
    rt_linewriter_close(lw);

    cleanup_test_file();
}

static void test_write_string() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_string text = make_string("Hello");
    rt_linewriter_write(lw, text);

    rt_string text2 = make_string(" World");
    rt_linewriter_write(lw, text2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 11);
    assert(strncmp(contents, "Hello World", 11) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_write_ln() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_string line1 = make_string("Line 1");
    rt_linewriter_write_ln(lw, line1);

    rt_string line2 = make_string("Line 2");
    rt_linewriter_write_ln(lw, line2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);

    // Platform-specific newline
#if RT_PLATFORM_WINDOWS
    assert(strncmp(contents, "Line 1\r\nLine 2\r\n", len) == 0);
#else
    assert(strncmp(contents, "Line 1\nLine 2\n", len) == 0);
#endif
    free(contents);

    cleanup_test_file();
}

static void test_write_char() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_linewriter_write_char(lw, 'A');
    rt_linewriter_write_char(lw, 'B');
    rt_linewriter_write_char(lw, 'C');
    rt_linewriter_write_char(lw, '1');
    rt_linewriter_write_char(lw, '2');
    rt_linewriter_write_char(lw, '3');

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 6);
    assert(strncmp(contents, "ABC123", 6) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_append_mode() {
    cleanup_test_file();

    // Create initial file
    {
        rt_string path = make_string(get_test_file());
        void *lw = rt_linewriter_open(path);
        rt_string text = make_string("First");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Append to file
    {
        rt_string path = make_string(get_test_file());
        void *lw = rt_linewriter_append(path);
        rt_string text = make_string("Second");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 11);
    assert(strncmp(contents, "FirstSecond", 11) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_custom_newline() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set custom newline (Windows-style)
    rt_string crlf = make_string("\r\n");
    rt_linewriter_set_newline(lw, crlf);

    // Verify newline was set
    rt_string nl = rt_linewriter_newline(lw);
    assert(rt_str_len(nl) == 2);
    assert(strncmp(rt_string_cstr(nl), "\r\n", 2) == 0);

    // Write lines with custom newline
    rt_string line1 = make_string("Line 1");
    rt_linewriter_write_ln(lw, line1);

    rt_string line2 = make_string("Line 2");
    rt_linewriter_write_ln(lw, line2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 16); // "Line 1\r\nLine 2\r\n"
    assert(strncmp(contents, "Line 1\r\nLine 2\r\n", 16) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_unix_newline() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set Unix-style newline
    rt_string lf = make_string("\n");
    rt_linewriter_set_newline(lw, lf);

    rt_string line1 = make_string("Line 1");
    rt_linewriter_write_ln(lw, line1);

    rt_string line2 = make_string("Line 2");
    rt_linewriter_write_ln(lw, line2);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 14); // "Line 1\nLine 2\n"
    assert(strncmp(contents, "Line 1\nLine 2\n", 14) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_flush() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    rt_string text = make_string("Flushed");
    rt_linewriter_write(lw, text);

    // Flush should not crash
    rt_linewriter_flush(lw);

    rt_linewriter_close(lw);

    // Verify byte was written
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 7);
    assert(strncmp(contents, "Flushed", 7) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_write_ln_empty() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set Unix newline for predictable test
    rt_string lf = make_string("\n");
    rt_linewriter_set_newline(lw, lf);

    // Write empty line (just newline)
    rt_string empty = make_string("");
    rt_linewriter_write_ln(lw, empty);

    rt_string line = make_string("Content");
    rt_linewriter_write_ln(lw, line);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 9); // "\nContent\n"
    assert(strncmp(contents, "\nContent\n", 9) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_overwrite_existing() {
    cleanup_test_file();

    // Create initial file with content
    {
        rt_string path = make_string(get_test_file());
        void *lw = rt_linewriter_open(path);
        rt_string text = make_string("This is a long initial content");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Overwrite with shorter content
    {
        rt_string path = make_string(get_test_file());
        void *lw = rt_linewriter_open(path);
        rt_string text = make_string("Short");
        rt_linewriter_write(lw, text);
        rt_linewriter_close(lw);
    }

    // Verify contents (should be only "Short")
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    assert(len == 5);
    assert(strncmp(contents, "Short", 5) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_mixed_write_methods() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    // Set Unix newline
    rt_string lf = make_string("\n");
    rt_linewriter_set_newline(lw, lf);

    // Mix different write methods
    rt_string hello = make_string("Hello");
    rt_linewriter_write(lw, hello);
    rt_linewriter_write_char(lw, ',');
    rt_linewriter_write_char(lw, ' ');
    rt_string world = make_string("World");
    rt_linewriter_write_ln(lw, world);
    rt_string bye = make_string("Goodbye");
    rt_linewriter_write_ln(lw, bye);

    rt_linewriter_close(lw);

    // Verify contents
    size_t len;
    char *contents = read_file_contents(&len);
    assert(contents != nullptr);
    // "Hello, World\nGoodbye\n" = 21 chars
    assert(len == 21);
    assert(strncmp(contents, "Hello, World\nGoodbye\n", 21) == 0);
    free(contents);

    cleanup_test_file();
}

static void test_write_char_out_of_range_traps() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    EXPECT_TRAP(rt_linewriter_write_char(lw, -1));
    EXPECT_TRAP(rt_linewriter_write_char(lw, 256));

    rt_linewriter_close(lw);
    cleanup_test_file();
}

static void test_invalid_string_inputs_trap() {
    cleanup_test_file();

    rt_string path = make_string(get_test_file());
    void *lw = rt_linewriter_open(path);
    assert(lw != nullptr);

    EXPECT_TRAP(rt_linewriter_write(lw, nullptr));
    EXPECT_TRAP(rt_linewriter_write_ln(lw, nullptr));
    rt_linewriter_set_newline(lw, nullptr);
    rt_string nl = rt_linewriter_newline(lw);
    assert(nl != nullptr);
    assert(rt_str_len(nl) > 0);

    rt_linewriter_close(lw);
    cleanup_test_file();
}

static void test_null_handling() {
    // Null operations should not crash
    rt_linewriter_close(nullptr);
    rt_linewriter_flush(nullptr);

    // Null writer get_NewLine returns default
    rt_string nl = rt_linewriter_newline(nullptr);
    assert(nl != nullptr);
    assert(rt_str_len(nl) > 0);
}

/// @brief Verify constructor OOM and newline replacement are transactional.
/// @details The second runtime allocation in Open is the managed writer
///          payload: the default newline is created first, then the native file
///          opens. Failing that payload must restore the exact descriptor
///          baseline. A separate failure while resetting NewLine must preserve
///          the previously retained separator.
static void test_allocation_failure_cleanup() {
    cleanup_test_file();
    rt_string path = make_string(get_test_file());

    uint64_t before = process_open_resource_count();
    g_alloc_countdown = 2;
    rt_set_alloc_hook(fail_countdown_alloc);
    EXPECT_TRAP(rt_linewriter_open(path));
    rt_set_alloc_hook(nullptr);
    assert(g_alloc_countdown == 0);
    assert(process_open_resource_count() == before);

    void *lw = rt_linewriter_open(path);
    rt_string keep = make_string("KEEP");
    rt_linewriter_set_newline(lw, keep);
    g_alloc_countdown = 1;
    rt_set_alloc_hook(fail_countdown_alloc);
    EXPECT_TRAP(rt_linewriter_set_newline(lw, nullptr));
    rt_set_alloc_hook(nullptr);
    assert(g_alloc_countdown == 0);

    rt_string after = rt_linewriter_newline(lw);
    assert(rt_str_len(after) == 4);
    assert(memcmp(rt_string_cstr(after), "KEEP", 4) == 0);
    rt_string_unref(after);
    rt_string_unref(keep);
    rt_linewriter_close(lw);
    rt_string_unref(path);
    cleanup_test_file();
}

int main() {
    test_open_close();
    test_write_string();
    test_write_ln();
    test_write_char();
    test_append_mode();
    test_custom_newline();
    test_unix_newline();
    test_flush();
    test_write_ln_empty();
    test_overwrite_existing();
    test_mixed_write_methods();
    test_write_char_out_of_range_traps();
    test_invalid_string_inputs_trap();
    test_null_handling();
    test_allocation_failure_cleanup();

    cleanup_test_file();
    return 0;
}
