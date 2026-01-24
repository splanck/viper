/**
 * @file syscall_tests.cpp
 * @brief Syscall dispatch tests for QEMU integration testing.
 *
 * @details
 * Tests the table-driven syscall dispatch mechanism:
 * 1. Valid syscall (task_yield) returns success
 * 2. Invalid syscall number returns VERR_NOT_SUPPORTED
 * 3. Invalid pointer returns VERR_INVALID_ARG
 */

#include "../console/serial.hpp"
#include "../include/error.hpp"
#include "../include/syscall.hpp"
#include "../include/syscall_nums.hpp"
#include "tests.hpp"

namespace tests {

// Low-level syscall helper to call with arbitrary syscall number
static inline i64 raw_syscall1(u64 num, u64 arg0) {
    register u64 x8 asm("x8") = num;
    register u64 r0 asm("x0") = arg0;
    register i64 result asm("x0");
    asm volatile("svc #0" : "=r"(result) : "r"(x8), "r"(r0) : "memory");
    return result;
}

static inline i64 raw_syscall0(u64 num) {
    register u64 x8 asm("x8") = num;
    register i64 result asm("x0");
    asm volatile("svc #0" : "=r"(result) : "r"(x8) : "memory");
    return result;
}

// Test counters
static int tests_passed = 0;
static int tests_failed = 0;

static void test_pass(const char *name) {
    serial::puts("  [PASS] ");
    serial::puts(name);
    serial::puts("\n");
    tests_passed++;
}

static void test_fail(const char *name, i64 expected, i64 actual) {
    serial::puts("  [FAIL] ");
    serial::puts(name);
    serial::puts(" - expected ");
    serial::put_dec(expected);
    serial::puts(", got ");
    serial::put_dec(actual);
    serial::puts("\n");
    tests_failed++;
}

void run_syscall_tests() {
    serial::puts("\n=== Syscall Dispatch Tests ===\n");

    tests_passed = 0;
    tests_failed = 0;

    // Test 1: Valid syscall (task_yield)
    {
        serial::puts("\n[Valid syscall]\n");
        i64 result = sys::yield();
        if (result == error::VOK) {
            test_pass("task_yield returns VOK");
        } else {
            test_fail("task_yield returns VOK", error::VOK, result);
        }
    }

    // Test 2: Invalid syscall number
    // Use a syscall number that is definitely not in the table (0xFE)
    {
        serial::puts("\n[Invalid syscall number]\n");
        constexpr u64 INVALID_SYSCALL_NUM = 0xFE; // Not defined in the table
        i64 result = raw_syscall0(INVALID_SYSCALL_NUM);
        if (result == error::VERR_NOT_SUPPORTED) {
            test_pass("unknown syscall returns VERR_NOT_SUPPORTED");
        } else {
            test_fail(
                "unknown syscall returns VERR_NOT_SUPPORTED", error::VERR_NOT_SUPPORTED, result);
        }
    }

    // Test 3: Invalid pointer (bad address for debug_print)
    // Pass an obviously invalid pointer (0xDEAD0000) to debug_print
    {
        serial::puts("\n[Invalid pointer]\n");
        // Use the raw syscall to pass a bad pointer
        constexpr u64 BAD_POINTER = 0xDEAD000000000000ULL;
        i64 result = raw_syscall1(syscall::DEBUG_PRINT, BAD_POINTER);
        if (result == error::VERR_INVALID_ARG) {
            test_pass("bad pointer returns VERR_INVALID_ARG");
        } else {
            test_fail("bad pointer returns VERR_INVALID_ARG", error::VERR_INVALID_ARG, result);
        }
    }

    // Summary
    serial::puts("\n--- Results: ");
    serial::put_dec(tests_passed);
    serial::puts(" passed, ");
    serial::put_dec(tests_failed);
    serial::puts(" failed ---\n");

    if (tests_failed == 0) {
        serial::puts("ALL SYSCALL TESTS PASSED\n");
    } else {
        serial::puts("SYSCALL TESTS FAILED\n");
    }
}

} // namespace tests
