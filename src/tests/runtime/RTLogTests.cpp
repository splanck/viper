//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTLogTests.cpp
// Purpose: Tests for Viper.Diagnostics.Log simple logging functions.
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_log.h"
#include "rt_string.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

// ============================================================================
// Helper
// ============================================================================

static rt_string make_str(const char *s) {
    return rt_const_cstr(s);
}

static rt_string make_bytes(const char *s, size_t len) {
    return rt_string_from_bytes(s, len);
}

#if !defined(_WIN32)
static std::string read_file(FILE *file) {
    std::string out;
    char buffer[256];
    rewind(file);
    while (true) {
        size_t n = fread(buffer, 1, sizeof(buffer), file);
        if (n > 0)
            out.append(buffer, n);
        if (n < sizeof(buffer))
            break;
    }
    return out;
}

static std::string capture_stderr(void (*fn)()) {
    fflush(stderr);
    int stderr_fd = fileno(stderr);
    int saved_fd = dup(stderr_fd);
    assert(saved_fd >= 0);

    FILE *tmp = tmpfile();
    assert(tmp != nullptr);
    int tmp_fd = fileno(tmp);
    assert(tmp_fd >= 0);
    assert(dup2(tmp_fd, stderr_fd) >= 0);

    fn();
    fflush(stderr);

    std::string out = read_file(tmp);
    assert(dup2(saved_fd, stderr_fd) >= 0);
    close(saved_fd);
    fclose(tmp);
    return out;
}
#endif

// ============================================================================
// Level Constant Tests
// ============================================================================

static void test_level_constants() {
    assert(rt_log_level_debug() == 0);
    assert(rt_log_level_info() == 1);
    assert(rt_log_level_warn() == 2);
    assert(rt_log_level_error() == 3);
    assert(rt_log_level_off() == 4);

    printf("test_level_constants: PASSED\n");
}

// ============================================================================
// Level Get/Set Tests
// ============================================================================

static void test_level_get_set() {
    // Save original level
    int64_t original = rt_log_level();

    // Test setting each level
    rt_log_set_level(rt_log_level_debug());
    assert(rt_log_level() == 0);

    rt_log_set_level(rt_log_level_info());
    assert(rt_log_level() == 1);

    rt_log_set_level(rt_log_level_warn());
    assert(rt_log_level() == 2);

    rt_log_set_level(rt_log_level_error());
    assert(rt_log_level() == 3);

    rt_log_set_level(rt_log_level_off());
    assert(rt_log_level() == 4);

    // Test clamping - below minimum
    rt_log_set_level(-1);
    assert(rt_log_level() == 0);

    // Test clamping - above maximum
    rt_log_set_level(100);
    assert(rt_log_level() == 4);

    // Restore original level
    rt_log_set_level(original);

    printf("test_level_get_set: PASSED\n");
}

// ============================================================================
// Enabled Tests
// ============================================================================

static void test_enabled() {
    // Save original level
    int64_t original = rt_log_level();

    // At DEBUG level, all levels are enabled
    rt_log_set_level(rt_log_level_debug());
    assert(rt_log_enabled(rt_log_level_debug()) == 1);
    assert(rt_log_enabled(rt_log_level_info()) == 1);
    assert(rt_log_enabled(rt_log_level_warn()) == 1);
    assert(rt_log_enabled(rt_log_level_error()) == 1);

    // At INFO level, DEBUG is disabled
    rt_log_set_level(rt_log_level_info());
    assert(rt_log_enabled(rt_log_level_debug()) == 0);
    assert(rt_log_enabled(rt_log_level_info()) == 1);
    assert(rt_log_enabled(rt_log_level_warn()) == 1);
    assert(rt_log_enabled(rt_log_level_error()) == 1);

    // At WARN level, DEBUG and INFO are disabled
    rt_log_set_level(rt_log_level_warn());
    assert(rt_log_enabled(rt_log_level_debug()) == 0);
    assert(rt_log_enabled(rt_log_level_info()) == 0);
    assert(rt_log_enabled(rt_log_level_warn()) == 1);
    assert(rt_log_enabled(rt_log_level_error()) == 1);

    // At ERROR level, only ERROR is enabled
    rt_log_set_level(rt_log_level_error());
    assert(rt_log_enabled(rt_log_level_debug()) == 0);
    assert(rt_log_enabled(rt_log_level_info()) == 0);
    assert(rt_log_enabled(rt_log_level_warn()) == 0);
    assert(rt_log_enabled(rt_log_level_error()) == 1);

    // At OFF level, nothing is enabled
    rt_log_set_level(rt_log_level_off());
    assert(rt_log_enabled(rt_log_level_debug()) == 0);
    assert(rt_log_enabled(rt_log_level_info()) == 0);
    assert(rt_log_enabled(rt_log_level_warn()) == 0);
    assert(rt_log_enabled(rt_log_level_error()) == 0);
    assert(rt_log_enabled(rt_log_level_off()) == 0);

    // Restore original level
    rt_log_set_level(original);

    printf("test_enabled: PASSED\n");
}

// ============================================================================
// Log Output Tests
// ============================================================================

static void emit_sanitized_info() {
    rt_log_set_level(rt_log_level_debug());
    const char bytes[] = {'l',  'i', 'n', 'e', '1', '\n', 'l', 'i', 'n', 'e',
                          '2', '\0', 't', 'a', 'i', 'l', '\t', 'e', 'n', 'd'};
    rt_string s = make_bytes(bytes, sizeof(bytes));
    rt_log_info(s);
    rt_string_unref(s);
}

static void emit_suppressed_debug() {
    rt_log_set_level(rt_log_level_error());
    rt_log_debug(make_str("DEBUG - should NOT appear"));
}

static void emit_concurrent_info() {
    rt_log_set_level(rt_log_level_info());
    std::vector<std::thread> threads;
    for (int t = 0; t < 6; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 40; ++i) {
                char buffer[160];
                int len = snprintf(buffer,
                                   sizeof(buffer),
                                   "thread=%d item=%d payload=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                   t,
                                   i);
                assert(len > 0 && (size_t)len < sizeof(buffer));
                rt_string s = rt_string_from_bytes(buffer, (size_t)len);
                rt_log_info(s);
                rt_string_unref(s);
            }
        });
    }
    for (auto &thread : threads)
        thread.join();
}

static void test_log_output() {
    int64_t original = rt_log_level();

#if !defined(_WIN32)
    std::string out = capture_stderr(emit_sanitized_info);
    assert(out.rfind("[INFO] ", 0) == 0);
    assert(out.size() >= 28);
    assert(out[11] == '-');
    assert(out[14] == '-');
    assert(out[20] == ':');
    assert(out[23] == ':');
    assert(out.find("line1\\nline2\\0tail\\tend") != std::string::npos);
    assert(out.find("line1\nline2") == std::string::npos);

    out = capture_stderr(emit_suppressed_debug);
    assert(out.empty());
#else
    emit_sanitized_info();
    emit_suppressed_debug();
#endif

    rt_log_set_level(original);

    printf("test_log_output: PASSED\n");
}

static void test_concurrent_log_lines_are_atomic() {
    int64_t original = rt_log_level();

#if !defined(_WIN32)
    std::string out = capture_stderr(emit_concurrent_info);
    size_t lines = 0;
    size_t start = 0;
    while (start < out.size()) {
        size_t end = out.find('\n', start);
        assert(end != std::string::npos);
        std::string line = out.substr(start, end - start);
        assert(line.rfind("[INFO] ", 0) == 0);
        assert(line.find("[INFO] ", 1) == std::string::npos);
        assert(line.find("thread=") != std::string::npos);
        assert(line.find("payload=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") != std::string::npos);
        ++lines;
        start = end + 1;
    }
    assert(lines == 240);
#else
    emit_concurrent_info();
#endif

    rt_log_set_level(original);

    printf("test_concurrent_log_lines_are_atomic: PASSED\n");
}

static void test_threaded_level_access() {
    int64_t original = rt_log_level();
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 1000; ++i) {
                rt_log_set_level((t + i) % 5);
                (void)rt_log_level();
                (void)rt_log_enabled(i % 5);
            }
        });
    }
    for (auto &thread : threads)
        thread.join();
    rt_log_set_level(original);

    printf("test_threaded_level_access: PASSED\n");
}

// ============================================================================
// Default Level Tests
// ============================================================================

static void test_default_level() {
    // Note: This test assumes the default level is INFO (1)
    // If run first before other tests modify the level
    // We can't really test this without resetting global state

    printf("test_default_level: SKIPPED (depends on global state)\n");
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("=== Viper.Diagnostics.Log Tests ===\n\n");

    // Level constants
    test_level_constants();

    // Level get/set
    test_level_get_set();

    // Enabled checks
    test_enabled();

    // Log output (visual)
    test_log_output();
    test_concurrent_log_lines_are_atomic();
    test_threaded_level_access();

    // Default level
    test_default_level();

    printf("\nAll RTLogTests passed!\n");
    return 0;
}
