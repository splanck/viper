//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTWindowsRuntimeTests.cpp
// Purpose: Focused regression coverage for shared Windows runtime adapters.
//
// Key invariants:
//   - Finite Win32 deadlines never become the INFINITE sentinel.
//   - Concurrent WinSock initialization is idempotent.
//   - WinSock error classifiers cover documented non-blocking states.
//   - Entropy adapters reject invalid outputs without a fallback.
//
// Ownership/Lifetime:
//   - The test owns all worker threads and joins them before exit.
//
// Links: src/runtime/rt_win32_wait.h,
//        src/runtime/network/rt_socket_platform.h,
//        src/runtime/network/rt_entropy_platform.h
//
//===----------------------------------------------------------------------===//

#include "rt_entropy_platform.h"
#include "rt_internal.h"
#include "rt_socket_platform.h"
#include "rt_win32_wait.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static void test_finite_wait_deadlines() {
    assert(rt_win32_deadline_after_ms(100, -1) == 100);
    assert(rt_win32_deadline_after_ms(100, 0) == 100);
    assert(rt_win32_deadline_after_ms(100, 25) == 125);
    assert(rt_win32_deadline_after_ms(ULLONG_MAX - 5, 10) == ULLONG_MAX);

    assert(rt_win32_wait_slice_at(100, 100) == 0);
    assert(rt_win32_wait_slice_at(100, 101) == 1);
    assert(rt_win32_wait_slice_at(100, 100 + (ULONGLONG)INFINITE) == RT_WIN32_MAX_FINITE_WAIT_MS);
    assert(rt_win32_wait_slice_at(0, ULLONG_MAX) == RT_WIN32_MAX_FINITE_WAIT_MS);
    assert(RT_WIN32_MAX_FINITE_WAIT_MS != INFINITE);
}

static void test_concurrent_winsock_initialization() {
    std::array<std::thread, 8> workers;
    for (std::thread &worker : workers)
        worker = std::thread([] { rt_net_init_wsa(); });
    for (std::thread &worker : workers)
        worker.join();
    rt_net_init_wsa();
}

static void test_winsock_error_contracts() {
    assert(rt_socket_error_is_in_progress(WSAEWOULDBLOCK));
    assert(rt_socket_error_is_in_progress(WSAEINPROGRESS));
    assert(rt_socket_error_is_in_progress(WSAEALREADY));
    assert(!rt_socket_error_is_in_progress(WSAECONNRESET));
    assert(rt_socket_accept_interrupted_by_close(WSAESHUTDOWN));

    WSASetLastError(0);
    assert(wait_socket(INVALID_SOCK, 0, false) == -1);
    assert(WSAGetLastError() == WSAENOTSOCK);
    assert(wait_socket(0, -1, false) == -1);
    assert(WSAGetLastError() == WSAEINVAL);
}

static void test_entropy_argument_contracts() {
    uint64_t random_value = 0;
    assert(rt_entropy_platform_random_bytes(nullptr, 0) == 0);
    assert(rt_entropy_platform_random_bytes(nullptr, 1) == -1);
    assert(rt_entropy_platform_random_u64(nullptr) == -1);
    assert(rt_entropy_platform_random_u64(&random_value) == 0);
}

int main() {
    test_finite_wait_deadlines();
    test_concurrent_winsock_initialization();
    test_winsock_error_contracts();
    test_entropy_argument_contracts();
    std::puts("RTWindowsRuntimeTests passed");
    return 0;
}
