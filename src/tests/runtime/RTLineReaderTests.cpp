//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTLineReaderTests.cpp
// Purpose: Comprehensive tests for Zanna.IO.LineReader text file reading.
// Key invariants: ReadAll begins at the logical cursor and consumes a staged
//                 PeekChar byte exactly once without restarting the stream.
// Ownership/Lifetime: Every test closes its reader and removes its temporary
//                     file; returned managed strings are released by the case.
// Links: src/runtime/io/rt_linereader.c, docs/zannalib/io/streams.md,
//        docs/adr/0119-trap-safe-managed-io-ownership.md
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_linereader.h"
#include "rt_platform.h"
#include "rt_string.h"

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

/// @brief Return a process-unique temporary path on every supported host.
/// @return Stable path string valid for the duration of the test process.
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
        snprintf(path, sizeof(path), "%s\\zanna_linereader_test_%d.txt", tmp, (int)GETPID());
#else
        snprintf(path, sizeof(path), "/tmp/zanna_linereader_test_%d.txt", (int)GETPID());
#endif
        initialized = true;
    }
    return path;
}

static const char *test_file = get_test_file();

/// @brief Fail one selected runtime allocation, delegating all other requests.
/// @param bytes Requested byte count.
/// @param next Default runtime allocator.
/// @return NULL for the selected request, otherwise the default allocation.
static void *fail_countdown_alloc(int64_t bytes, void *(*next)(int64_t)) {
    if (g_alloc_countdown > 0 && --g_alloc_countdown == 0)
        return nullptr;
    return next ? next(bytes) : nullptr;
}

/// @brief Count native process resources without opening a descriptor itself.
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

static rt_string make_string(const char *s) {
    return rt_string_from_bytes(s, strlen(s));
}

static void cleanup_test_file() {
    remove(test_file);
}

static void write_raw_file(const char *data, size_t len) {
    FILE *fp = fopen(test_file, "wb");
    assert(fp != nullptr);
    fwrite(data, 1, len, fp);
    fclose(fp);
}

static void test_open_close() {
    cleanup_test_file();
    write_raw_file("test\n", 5);

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);
    assert(rt_linereader_eof(lr) == 0);

    rt_linereader_close(lr);
    // Should be able to close twice without issue
    rt_linereader_close(lr);

    cleanup_test_file();
}

static void test_read_lf_lines() {
    cleanup_test_file();

    // Unix-style LF line endings
    const char *content = "line1\nline2\nline3\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line1 = rt_linereader_read(lr);
    assert(rt_str_len(line1) == 5);
    assert(strncmp(rt_string_cstr(line1), "line1", 5) == 0);

    rt_string line2 = rt_linereader_read(lr);
    assert(rt_str_len(line2) == 5);
    assert(strncmp(rt_string_cstr(line2), "line2", 5) == 0);

    rt_string line3 = rt_linereader_read(lr);
    assert(rt_str_len(line3) == 5);
    assert(strncmp(rt_string_cstr(line3), "line3", 5) == 0);
    assert(rt_linereader_eof(lr) == 1);

    // Reading at EOF should return empty string and set eof
    rt_string empty = rt_linereader_read(lr);
    assert(rt_str_len(empty) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_crlf_lines() {
    cleanup_test_file();

    // Windows-style CRLF line endings
    const char *content = "line1\r\nline2\r\nline3\r\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line1 = rt_linereader_read(lr);
    assert(rt_str_len(line1) == 5);
    assert(strncmp(rt_string_cstr(line1), "line1", 5) == 0);

    rt_string line2 = rt_linereader_read(lr);
    assert(rt_str_len(line2) == 5);
    assert(strncmp(rt_string_cstr(line2), "line2", 5) == 0);

    rt_string line3 = rt_linereader_read(lr);
    assert(rt_str_len(line3) == 5);
    assert(strncmp(rt_string_cstr(line3), "line3", 5) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_cr_lines() {
    cleanup_test_file();

    // Classic Mac CR line endings
    const char *content = "line1\rline2\rline3\r";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line1 = rt_linereader_read(lr);
    assert(rt_str_len(line1) == 5);
    assert(strncmp(rt_string_cstr(line1), "line1", 5) == 0);

    rt_string line2 = rt_linereader_read(lr);
    assert(rt_str_len(line2) == 5);
    assert(strncmp(rt_string_cstr(line2), "line2", 5) == 0);

    rt_string line3 = rt_linereader_read(lr);
    assert(rt_str_len(line3) == 5);
    assert(strncmp(rt_string_cstr(line3), "line3", 5) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_mixed_endings() {
    cleanup_test_file();

    // Mixed line endings
    const char *content = "lf\ncrlf\r\ncr\rend";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string l1 = rt_linereader_read(lr);
    assert(rt_str_len(l1) == 2);
    assert(strncmp(rt_string_cstr(l1), "lf", 2) == 0);

    rt_string l2 = rt_linereader_read(lr);
    assert(rt_str_len(l2) == 4);
    assert(strncmp(rt_string_cstr(l2), "crlf", 4) == 0);

    rt_string l3 = rt_linereader_read(lr);
    assert(rt_str_len(l3) == 2);
    assert(strncmp(rt_string_cstr(l3), "cr", 2) == 0);

    // Last line without newline
    rt_string l4 = rt_linereader_read(lr);
    assert(rt_str_len(l4) == 3);
    assert(strncmp(rt_string_cstr(l4), "end", 3) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_char() {
    cleanup_test_file();

    const char *content = "ABC";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    assert(rt_linereader_read_char(lr) == 'A');
    assert(rt_linereader_read_char(lr) == 'B');
    assert(rt_linereader_read_char(lr) == 'C');

    // EOF should return -1
    assert(rt_linereader_read_char(lr) == -1);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_peek_char() {
    cleanup_test_file();

    const char *content = "XYZ";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Peek should not consume
    assert(rt_linereader_peek_char(lr) == 'X');
    assert(rt_linereader_peek_char(lr) == 'X');

    // Read should consume
    assert(rt_linereader_read_char(lr) == 'X');

    // Peek next
    assert(rt_linereader_peek_char(lr) == 'Y');
    assert(rt_linereader_read_char(lr) == 'Y');

    assert(rt_linereader_peek_char(lr) == 'Z');
    assert(rt_linereader_read_char(lr) == 'Z');

    // Peek at EOF
    assert(rt_linereader_peek_char(lr) == -1);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_peek_then_read_line() {
    cleanup_test_file();

    const char *content = "hello\nworld\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Peek first char
    assert(rt_linereader_peek_char(lr) == 'h');

    // Read full line (should still work with peeked char)
    rt_string line = rt_linereader_read(lr);
    assert(rt_str_len(line) == 5);
    assert(strncmp(rt_string_cstr(line), "hello", 5) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_all() {
    cleanup_test_file();

    const char *content = "Hello, World!\nThis is a test.\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string all = rt_linereader_read_all(lr);
    assert(rt_str_len(all) == (int64_t)strlen(content));
    assert(strncmp(rt_string_cstr(all), content, strlen(content)) == 0);

    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_all_partial() {
    cleanup_test_file();

    const char *content = "line1\nline2\nline3\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Read first line
    rt_string line1 = rt_linereader_read(lr);
    assert(rt_str_len(line1) == 5);

    // Read remaining with ReadAll
    rt_string rest = rt_linereader_read_all(lr);
    assert(rt_str_len(rest) == strlen("line2\nline3\n"));

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_read_all_with_peek() {
    cleanup_test_file();

    const char *content = "ABCDEF";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Consume first char
    assert(rt_linereader_read_char(lr) == 'A');

    // Peek should see 'B'
    assert(rt_linereader_peek_char(lr) == 'B');

    // ReadAll should include the peeked char
    rt_string rest = rt_linereader_read_all(lr);
    assert(rt_str_len(rest) == 5);
    assert(strncmp(rt_string_cstr(rest), "BCDEF", 5) == 0);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_empty_file() {
    cleanup_test_file();
    write_raw_file("", 0);

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    // Read should return empty and set EOF
    rt_string line = rt_linereader_read(lr);
    assert(rt_str_len(line) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_empty_lines() {
    cleanup_test_file();

    // Three empty lines
    const char *content = "\n\n\n";
    write_raw_file(content, strlen(content));

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string l1 = rt_linereader_read(lr);
    assert(rt_str_len(l1) == 0);
    assert(rt_linereader_eof(lr) == 0);

    rt_string l2 = rt_linereader_read(lr);
    assert(rt_str_len(l2) == 0);
    assert(rt_linereader_eof(lr) == 0);

    rt_string l3 = rt_linereader_read(lr);
    assert(rt_str_len(l3) == 0);
    assert(rt_linereader_eof(lr) == 1);

    // Now we're at EOF
    rt_string l4 = rt_linereader_read(lr);
    assert(rt_str_len(l4) == 0);
    assert(rt_linereader_eof(lr) == 1);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_long_line() {
    cleanup_test_file();

    // Create a line longer than initial buffer (256)
    char content[1000];
    for (int i = 0; i < 999; ++i) {
        content[i] = 'X';
    }
    content[999] = '\0';

    FILE *fp = fopen(test_file, "w");
    assert(fp != nullptr);
    fprintf(fp, "%s\n", content);
    fclose(fp);

    rt_string path = make_string(test_file);
    void *lr = rt_linereader_open(path);
    assert(lr != nullptr);

    rt_string line = rt_linereader_read(lr);
    assert(rt_str_len(line) == 999);

    rt_linereader_close(lr);
    cleanup_test_file();
}

static void test_null_handling() {
    // Null operations should return safe defaults
    assert(rt_linereader_eof(nullptr) == 1);

    // This should not crash
    rt_linereader_close(nullptr);
}

/// @brief Verify constructor and result-allocation traps clean owned resources.
/// @details The path is constructed before fault injection so the first failed
///          allocation is the managed reader payload after the native file has
///          opened. Separate Read and ReadAll failures target runtime-string
///          construction after their malloc-owned staging buffers exist; leak
///          sanitizers therefore cover both cleanup boundaries deterministically.
static void test_allocation_failure_cleanup() {
    cleanup_test_file();
    write_raw_file("alpha\nbeta", 10);
    rt_string path = make_string(test_file);

    uint64_t before = process_open_resource_count();
    g_alloc_countdown = 1;
    rt_set_alloc_hook(fail_countdown_alloc);
    EXPECT_TRAP(rt_linereader_open(path));
    rt_set_alloc_hook(nullptr);
    assert(g_alloc_countdown == 0);
    assert(process_open_resource_count() == before);

    void *lr = rt_linereader_open(path);
    g_alloc_countdown = 1;
    rt_set_alloc_hook(fail_countdown_alloc);
    EXPECT_TRAP(rt_linereader_read(lr));
    rt_set_alloc_hook(nullptr);
    assert(g_alloc_countdown == 0);
    rt_linereader_close(lr);

    lr = rt_linereader_open(path);
    g_alloc_countdown = 1;
    rt_set_alloc_hook(fail_countdown_alloc);
    EXPECT_TRAP(rt_linereader_read_all(lr));
    rt_set_alloc_hook(nullptr);
    assert(g_alloc_countdown == 0);
    rt_linereader_close(lr);

    rt_string_unref(path);
    cleanup_test_file();
}

int main() {
    test_open_close();
    test_read_lf_lines();
    test_read_crlf_lines();
    test_read_cr_lines();
    test_read_mixed_endings();
    test_read_char();
    test_peek_char();
    test_peek_then_read_line();
    test_read_all();
    test_read_all_partial();
    test_read_all_with_peek();
    test_empty_file();
    test_empty_lines();
    test_long_line();
    test_null_handling();
    test_allocation_failure_cleanup();

    cleanup_test_file();
    return 0;
}
