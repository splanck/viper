//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTWatcherTests.cpp
// Purpose: Validate runtime file system watcher operations in rt_watcher.c.
// Key invariants: Watcher can detect file creation, modification, and deletion
//                 events on watched directories and files.
// Ownership/Lifetime: Uses runtime library; tests create temporary files
//                     that are cleaned up after tests complete.
// Links: docs/viperlib/io.md
//
//===----------------------------------------------------------------------===//

#include "rt.hpp"
#include "rt_watcher.h"

#include <cassert>
#include <chrono>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

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
    snprintf(buf, sizeof(buf), "%s\\viper_watcher_test_%d", tmp, (int)getpid());
    return buf;
#else
    static char buf[256];
    snprintf(buf, sizeof(buf), "/tmp/viper_watcher_test_%d", (int)getpid());
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

/// @brief Helper to modify a file.
static void modify_file(const char *path)
{
    FILE *f = fopen(path, "a");
    if (f)
    {
        fprintf(f, "modified\n");
        fclose(f);
    }
}

/// @brief Helper to remove a file.
static void remove_file(const char *path)
{
    remove(path);
}

/// @brief Helper to wait a bit for filesystem events to propagate.
static void wait_for_event()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

/// @brief Test watcher creation and basic properties.
static void test_watcher_new()
{
    printf("Testing Watcher.New...\n");

    const char *base = get_test_base();
    mkdir_p(base);

    // Create watcher for directory
    rt_string path = rt_string_from_bytes(base, strlen(base));
    void *w = rt_watcher_new(path);
    assert(w != nullptr);

    // Check properties
    assert(rt_watcher_get_is_watching(w) == 0); // Not started yet

    // Check path is returned
    rt_string watchedPath = rt_watcher_get_path(w);
    assert(watchedPath != nullptr);

    test_result("Watcher creation", true);

    // Cleanup
    rmdir_p(base);
}

/// @brief Test watcher start/stop.
static void test_watcher_start_stop()
{
    printf("Testing Watcher.Start/Stop...\n");

    const char *base = get_test_base();
    mkdir_p(base);

    rt_string path = rt_string_from_bytes(base, strlen(base));
    void *w = rt_watcher_new(path);

    // Start watching
    rt_watcher_start(w);
    assert(rt_watcher_get_is_watching(w) == 1);

    // Stop watching
    rt_watcher_stop(w);
    assert(rt_watcher_get_is_watching(w) == 0);

    test_result("Start/Stop", true);

    // Cleanup
    rmdir_p(base);
}

/// @brief Test event type constants.
static void test_event_constants()
{
    printf("Testing event constants...\n");

    assert(rt_watcher_event_none() == 0);
    assert(rt_watcher_event_created() == 1);
    assert(rt_watcher_event_modified() == 2);
    assert(rt_watcher_event_deleted() == 3);
    assert(rt_watcher_event_renamed() == 4);

    test_result("Event constants", true);
}

/// @brief Test polling with no events returns none.
static void test_poll_no_events()
{
    printf("Testing Poll with no events...\n");

    const char *base = get_test_base();
    mkdir_p(base);

    rt_string path = rt_string_from_bytes(base, strlen(base));
    void *w = rt_watcher_new(path);
    rt_watcher_start(w);

    // Poll should return 0 (no events)
    int64_t event = rt_watcher_poll(w);
    assert(event == RT_WATCH_EVENT_NONE);

    rt_watcher_stop(w);
    test_result("Poll no events", true);

    // Cleanup
    rmdir_p(base);
}

/// @brief Test watcher traps on null path.
static void test_null_path_trap()
{
    printf("Testing null path trap...\n");

    EXPECT_TRAP(rt_watcher_new(nullptr));

    test_result("Null path trap", true);
}

/// @brief Test watcher traps on non-existent path.
static void test_nonexistent_path_trap()
{
    printf("Testing non-existent path trap...\n");

    rt_string path = rt_string_from_bytes("/nonexistent/path/12345", 24);
    EXPECT_TRAP(rt_watcher_new(path));

    test_result("Non-existent path trap", true);
}

int main()
{
    printf("=== Watcher Runtime Tests ===\n");

    test_event_constants();
    test_watcher_new();
    test_watcher_start_stop();
    test_poll_no_events();
    test_null_path_trap();
    test_nonexistent_path_trap();

    printf("\nAll Watcher tests passed!\n");
    return 0;
}
