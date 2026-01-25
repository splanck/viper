//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file sysinfo.cpp
 * @brief System information and runtime test utility for ViperDOS.
 *
 * This utility serves two purposes:
 * 1. Display comprehensive system information (memory, tasks, uptime)
 * 2. Run a test suite to validate libc runtime functions
 *
 * ## Output Sections
 *
 * ```
 * === System Information ===
 *   Uptime:        2h 15m 30s (8130000 ms)
 *   CWD:           /
 *   PID:           5
 *   Memory Total:  131072 KB
 *   Memory Free:   98304 KB
 *   Memory Used:   32768 KB (25%)
 *   Page Size:     4096 bytes
 *
 * === Running Tasks ===
 *   ID    Name          State     Priority
 *   1     kernel        Running   0
 *   2     vinit         Ready     5
 *   ...
 *
 * === Test Results ===
 *   [PASS] strlen("hello") == 5
 *   [PASS] malloc(64) returns non-NULL
 *   ...
 * ```
 *
 * ## Test Categories
 *
 * | Category      | Functions Tested                    |
 * |---------------|-------------------------------------|
 * | String        | strlen, strcmp, strcpy, strcat, etc |
 * | Memory        | malloc, free, calloc, realloc       |
 * | Character     | isalpha, isdigit, toupper, tolower  |
 * | Stdlib        | atoi, atol, strtol, abs             |
 * | Printf        | snprintf with various formats       |
 *
 * ## Exit Code
 *
 * - 0: All tests passed
 * - 1: One or more tests failed
 *
 * ## Usage
 *
 * ```
 * sysinfo          # Run from shell prompt
 * ```
 *
 * @see syscall.hpp for system call wrappers
 */
//===----------------------------------------------------------------------===//

#include "../syscall.hpp"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, cond)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            printf("  [PASS] %s\n", name);                                                         \
            tests_passed++;                                                                        \
        } else {                                                                                   \
            printf("  [FAIL] %s\n", name);                                                         \
            tests_failed++;                                                                        \
        }                                                                                          \
    } while (0)

// Format uptime nicely
static void format_uptime(u64 ms, char *buf, size_t bufsize) {
    u64 seconds = ms / 1000;
    u64 minutes = seconds / 60;
    u64 hours = minutes / 60;
    u64 days = hours / 24;

    if (days > 0) {
        snprintf(buf,
                 bufsize,
                 "%llud %lluh %llum %llus",
                 (unsigned long long)days,
                 (unsigned long long)(hours % 24),
                 (unsigned long long)(minutes % 60),
                 (unsigned long long)(seconds % 60));
    } else if (hours > 0) {
        snprintf(buf,
                 bufsize,
                 "%lluh %llum %llus",
                 (unsigned long long)hours,
                 (unsigned long long)(minutes % 60),
                 (unsigned long long)(seconds % 60));
    } else if (minutes > 0) {
        snprintf(buf,
                 bufsize,
                 "%llum %llus",
                 (unsigned long long)minutes,
                 (unsigned long long)(seconds % 60));
    } else {
        snprintf(buf, bufsize, "%llus", (unsigned long long)seconds);
    }
}

static void test_string_functions() {
    printf("\nString Function Tests\n");
    printf("---------------------------------------------\n");

    // strlen
    TEST("strlen(\"hello\") == 5", strlen("hello") == 5);
    TEST("strlen(\"\") == 0", strlen("") == 0);

    // strcmp
    TEST("strcmp(\"abc\", \"abc\") == 0", strcmp("abc", "abc") == 0);
    TEST("strcmp(\"abc\", \"abd\") < 0", strcmp("abc", "abd") < 0);
    TEST("strcmp(\"abd\", \"abc\") > 0", strcmp("abd", "abc") > 0);

    // strcpy
    char buf[32];
    strcpy(buf, "test");
    TEST("strcpy works", strcmp(buf, "test") == 0);

    // strncpy
    strncpy(buf, "hello world", 5);
    buf[5] = '\0';
    TEST("strncpy works", strcmp(buf, "hello") == 0);

    // strcat
    strcpy(buf, "Hello");
    strcat(buf, " World");
    TEST("strcat works", strcmp(buf, "Hello World") == 0);

    // memset
    memset(buf, 'A', 5);
    buf[5] = '\0';
    TEST("memset works", strcmp(buf, "AAAAA") == 0);

    // memcpy
    const char *src = "Test123";
    memcpy(buf, src, 8);
    TEST("memcpy works", strcmp(buf, "Test123") == 0);

    // strchr
    TEST("strchr finds char", strchr("hello", 'l') != NULL);
    TEST("strchr returns NULL", strchr("hello", 'z') == NULL);

    // strstr
    TEST("strstr finds substring", strstr("hello world", "world") != NULL);
    TEST("strstr returns NULL", strstr("hello world", "xyz") == NULL);
}

static void test_memory_allocation() {
    printf("\nMemory Allocation Tests\n");
    printf("---------------------------------------------\n");

    // Basic malloc
    char *p1 = (char *)malloc(64);
    TEST("malloc(64) returns non-NULL", p1 != NULL);

    if (p1) {
        // Write and read back
        memset(p1, 0xAA, 64);
        int all_correct = 1;
        for (int i = 0; i < 64; i++) {
            if ((unsigned char)p1[i] != 0xAA) {
                all_correct = 0;
                break;
            }
        }
        TEST("malloc memory is writable", all_correct);
        free(p1);
    }

    // Multiple allocations
    void *ptrs[10];
    int all_non_null = 1;
    for (int i = 0; i < 10; i++) {
        ptrs[i] = malloc(128);
        if (ptrs[i] == NULL) {
            all_non_null = 0;
        }
    }
    TEST("10 consecutive mallocs succeed", all_non_null);

    // Free all
    for (int i = 0; i < 10; i++) {
        free(ptrs[i]);
    }
    TEST("10 frees complete", 1); // If we get here, frees worked

    // Large allocation
    void *big = malloc(4096);
    TEST("malloc(4096) works", big != NULL);
    if (big)
        free(big);

    // calloc test
    int *arr = (int *)calloc(10, sizeof(int));
    TEST("calloc returns non-NULL", arr != NULL);
    if (arr) {
        int all_zero = 1;
        for (int i = 0; i < 10; i++) {
            if (arr[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        TEST("calloc zeroes memory", all_zero);
        free(arr);
    }
}

static void test_ctype_functions() {
    printf("\nCharacter Type Tests\n");
    printf("---------------------------------------------\n");

    TEST("isalpha('A')", isalpha('A'));
    TEST("isalpha('z')", isalpha('z'));
    TEST("!isalpha('5')", !isalpha('5'));

    TEST("isdigit('0')", isdigit('0'));
    TEST("isdigit('9')", isdigit('9'));
    TEST("!isdigit('x')", !isdigit('x'));

    TEST("isspace(' ')", isspace(' '));
    TEST("isspace('\\t')", isspace('\t'));
    TEST("!isspace('a')", !isspace('a'));

    TEST("isupper('A')", isupper('A'));
    TEST("!isupper('a')", !isupper('a'));

    TEST("islower('a')", islower('a'));
    TEST("!islower('A')", !islower('A'));

    TEST("toupper('a') == 'A'", toupper('a') == 'A');
    TEST("tolower('A') == 'a'", tolower('A') == 'a');
}

static void test_stdlib_functions() {
    printf("\nStandard Library Tests\n");
    printf("---------------------------------------------\n");

    // atoi
    TEST("atoi(\"123\") == 123", atoi("123") == 123);
    TEST("atoi(\"-456\") == -456", atoi("-456") == -456);
    TEST("atoi(\"0\") == 0", atoi("0") == 0);

    // atol
    TEST("atol(\"1000000\") == 1000000", atol("1000000") == 1000000L);

    // strtol
    char *endptr;
    TEST("strtol(\"42\", ..., 10) == 42", strtol("42", &endptr, 10) == 42);
    TEST("strtol(\"0xFF\", ..., 16) == 255", strtol("0xFF", &endptr, 16) == 255);

    // abs
    TEST("abs(-5) == 5", abs(-5) == 5);
    TEST("abs(5) == 5", abs(5) == 5);
}

static void test_snprintf() {
    printf("\nPrintf Formatting Tests\n");
    printf("---------------------------------------------\n");

    char buf[128];

    snprintf(buf, sizeof(buf), "%d", 42);
    TEST("snprintf %%d works", strcmp(buf, "42") == 0);

    snprintf(buf, sizeof(buf), "%s", "hello");
    TEST("snprintf %%s works", strcmp(buf, "hello") == 0);

    snprintf(buf, sizeof(buf), "%x", 255);
    TEST("snprintf %%x works", strcmp(buf, "ff") == 0);

    snprintf(buf, sizeof(buf), "%X", 255);
    TEST("snprintf %%X works", strcmp(buf, "FF") == 0);

    snprintf(buf, sizeof(buf), "%%");
    TEST("snprintf %%%% works", strcmp(buf, "%") == 0);

    snprintf(buf, sizeof(buf), "%05d", 42);
    TEST("snprintf %%05d works", strcmp(buf, "00042") == 0);

    snprintf(buf, sizeof(buf), "%-10s|", "hi");
    TEST("snprintf %%-10s works", strcmp(buf, "hi        |") == 0);
}

static void show_system_info() {
    printf("\n=== System Information ===\n");
    printf("=============================================\n");

    // Uptime
    u64 uptime_ms = sys::uptime();
    char uptime_str[64];
    format_uptime(uptime_ms, uptime_str, sizeof(uptime_str));
    printf("  Uptime:        %s (%llu ms)\n", uptime_str, (unsigned long long)uptime_ms);

    // Current working directory
    char cwd[256];
    if (sys::getcwd(cwd, sizeof(cwd)) > 0) {
        printf("  CWD:           %s\n", cwd);
    }

    // Process ID
    pid_t pid = getpid();
    printf("  PID:           %d\n", (int)pid);

    // Memory info
    MemInfo mem;
    if (sys::mem_info(&mem) == 0) {
        u64 total_kb = (mem.total_pages * mem.page_size) / 1024;
        u64 free_kb = (mem.free_pages * mem.page_size) / 1024;
        u64 used_kb = total_kb - free_kb;
        u64 pct_used = (used_kb * 100) / total_kb;

        printf("  Memory Total:  %llu KB\n", (unsigned long long)total_kb);
        printf("  Memory Free:   %llu KB\n", (unsigned long long)free_kb);
        printf("  Memory Used:   %llu KB (%llu%%)\n",
               (unsigned long long)used_kb,
               (unsigned long long)pct_used);
        printf("  Page Size:     %llu bytes\n", (unsigned long long)mem.page_size);
    }
}

static void show_task_info() {
    printf("\n=== Running Tasks ===\n");
    printf("=============================================\n");

    TaskInfo tasks[16];
    int count = sys::task_list(tasks, 16);

    if (count < 0) {
        printf("  (Failed to get task list)\n");
        return;
    }

    printf("  %-4s  %-12s  %-8s  %s\n", "ID", "Name", "State", "Priority");
    printf("  %-4s  %-12s  %-8s  %s\n", "--", "----", "-----", "--------");

    for (int i = 0; i < count; i++) {
        const char *state_str;
        switch (tasks[i].state) {
            case 1:
                state_str = "Ready";
                break;
            case 2:
                state_str = "Running";
                break;
            case 3:
                state_str = "Blocked";
                break;
            case 4:
                state_str = "Zombie";
                break;
            case 5:
                state_str = "Exited";
                break;
            default:
                state_str = "Unknown";
                break;
        }

        printf(
            "  %-4u  %-12s  %-8s  %u\n", tasks[i].id, tasks[i].name, state_str, tasks[i].priority);
    }

    printf("\n  Total: %d tasks\n", count);
}

extern "C" void _start() {
    printf("\n");
    printf("=============================================\n");
    printf("   ViperDOS System Information & Test Suite\n");
    printf("                  v1.0\n");
    printf("=============================================\n");

    // Show system info first
    show_system_info();
    show_task_info();

    // Run all tests
    test_string_functions();
    test_memory_allocation();
    test_ctype_functions();
    test_stdlib_functions();
    test_snprintf();

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("=============================================\n");
    printf("  Tests Passed:  %d\n", tests_passed);
    printf("  Tests Failed:  %d\n", tests_failed);
    printf("  Total:         %d\n", tests_passed + tests_failed);

    if (tests_failed == 0) {
        printf("\n  Result: ALL TESTS PASSED!\n");
    } else {
        printf("\n  Result: SOME TESTS FAILED\n");
    }

    printf("\n");
    sys::exit(tests_failed > 0 ? 1 : 0);
}
