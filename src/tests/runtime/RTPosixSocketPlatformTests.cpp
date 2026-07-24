//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTPosixSocketPlatformTests.cpp
// Purpose: Focused regression coverage for POSIX socket readiness semantics.
//
// Key invariants:
//   - Invalid descriptors and invalid timeouts publish deterministic errno.
//   - Repeated EINTR cannot restart a finite readiness timeout.
//   - An orderly peer shutdown is readable so callers can observe recv() EOF.
//   - Pending connect errors are queried without narrowing native handles.
//   - Repeated nonblocking-mode requests are idempotent.
//   - Bidirectional shutdown interrupts I/O without consuming descriptor ownership.
//
// Ownership/Lifetime:
//   - Every socketpair descriptor is closed by the test that creates it.
//   - The temporary SIGUSR1 handler is restored before the test exits.
//   - The signal-delivery thread is stopped and joined before handler restore.
//
// Links: src/runtime/network/rt_socket_platform.h,
//        src/runtime/network/rt_socket_platform_posix.c
//
//===----------------------------------------------------------------------===//

#include "rt_internal.h"
#include "rt_socket_platform.h"

#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

static volatile sig_atomic_t g_socket_wait_interrupts = 0;

/// @brief Record one test-only signal without invoking non-signal-safe code.
/// @details The handler intentionally omits SA_RESTART so poll() reports EINTR.
///          Updating sig_atomic_t is the only operation permitted in this
///          asynchronous context.
/// @param signal_number Delivered signal number; ignored after dispatch.
static void socket_wait_signal_handler(int signal_number) {
    (void)signal_number;
    g_socket_wait_interrupts = (sig_atomic_t)(g_socket_wait_interrupts + 1);
}

/// @brief Verify invalid readiness arguments fail with stable POSIX errors.
/// @details A negative descriptor must not be silently ignored by poll(), and
///          a negative timeout must not inherit an unrelated prior errno.
static void test_socket_wait_argument_errors() {
    errno = 0;
    assert(wait_socket(INVALID_SOCK, 0, false) == -1);
    assert(errno == EBADF);

    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    errno = 0;
    assert(wait_socket(sockets[0], -1, false) == -1);
    assert(errno == EINVAL);
    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
}

/// @brief Prove repeated EINTR shares one monotonic readiness deadline.
/// @details A helper sends SIGUSR1 every five milliseconds for longer than the
///          requested wait. The socket remains idle. An implementation that
///          restarts the full timeout after every interruption takes roughly
///          the signal-train duration plus another timeout; the corrected
///          adapter returns near the original 120 ms deadline.
static void test_socket_wait_eintr_deadline() {
    struct sigaction action;
    struct sigaction previous_action;
    action.sa_handler = socket_wait_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    assert(sigaction(SIGUSR1, &action, &previous_action) == 0);

    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    g_socket_wait_interrupts = 0;
    std::atomic<bool> stop{false};
    pthread_t waiting_thread = pthread_self();
    std::thread interrupter([&stop, waiting_thread]() {
        for (int attempt = 0; attempt < 80 && !stop.load(std::memory_order_acquire); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            (void)pthread_kill(waiting_thread, SIGUSR1);
        }
    });

    const auto started = std::chrono::steady_clock::now();
    const int result = wait_socket(sockets[0], 120, false);
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started)
                                .count();
    stop.store(true, std::memory_order_release);
    interrupter.join();

    assert(result == 0);
    assert(g_socket_wait_interrupts > 0);
    assert(elapsed_ms >= 70);
    assert(elapsed_ms < 300);
    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
    assert(sigaction(SIGUSR1, &previous_action, nullptr) == 0);
}

/// @brief Verify read waits surface orderly shutdown as readable EOF.
/// @details poll() may report a peer close primarily as POLLHUP. Returning an
///          error would make higher-level receive paths trap instead of calling
///          recv() and receiving zero, so the adapter must report readiness.
static void test_socket_wait_orderly_eof() {
    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(close(sockets[1]) == 0);
    sockets[1] = INVALID_SOCK;

    assert(wait_socket(sockets[0], 1000, false) > 0);
    char byte = 0;
    assert(recv(sockets[0], &byte, 1, 0) == 0);
    assert(close(sockets[0]) == 0);
}

/// @brief Verify the portable pending-error query validates output storage.
/// @details A connected local socket has no deferred connect error, so the
///          adapter must publish zero through the caller's integer without
///          changing ownership or readiness state. A null output pointer is an
///          argument error and must fail without invoking getsockopt().
static void test_socket_pending_error_query() {
    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    assert(!rt_socket_pending_error(sockets[0], nullptr));
    int pending_error = -1;
    assert(rt_socket_pending_error(sockets[0], &pending_error));
    assert(pending_error == 0);

    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
}

/// @brief Verify repeated blocking-mode transitions preserve descriptor flags.
static void test_socket_nonblocking_idempotence() {
    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    assert(rt_socket_set_nonblocking(sockets[0], false));
    assert(rt_socket_set_nonblocking(sockets[0], false));
    int flags = fcntl(sockets[0], F_GETFL, 0);
    assert(flags >= 0);
    assert((flags & O_NONBLOCK) == 0);

    assert(rt_socket_set_nonblocking(sockets[0], true));
    assert(rt_socket_set_nonblocking(sockets[0], true));
    flags = fcntl(sockets[0], F_GETFL, 0);
    assert(flags >= 0);
    assert((flags & O_NONBLOCK) != 0);

    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
}

/// @brief Verify bidirectional shutdown preserves descriptor ownership.
/// @details The adapter must make the peer observe EOF and disable subsequent
///          I/O while leaving the local descriptor valid for its unique owner
///          to close later. This is the WSS Stop/worker handoff contract.
static void test_socket_shutdown_preserves_ownership() {
    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    assert(rt_socket_shutdown_both(sockets[0]) == 0);
    assert(fcntl(sockets[0], F_GETFD) >= 0);
    assert(wait_socket(sockets[1], 1000, false) > 0);
    char byte = 0;
    assert(recv(sockets[1], &byte, 1, 0) == 0);

    assert(close(sockets[0]) == 0);
    assert(close(sockets[1]) == 0);
    errno = 0;
    assert(rt_socket_shutdown_both(INVALID_SOCK) == -1);
}

/// @brief Run all POSIX readiness adapter regressions.
/// @return Zero after every invariant passes; assertion failure aborts.
int main() {
    test_socket_wait_argument_errors();
    test_socket_wait_eintr_deadline();
    test_socket_wait_orderly_eof();
    test_socket_pending_error_query();
    test_socket_nonblocking_idempotence();
    test_socket_shutdown_preserves_ownership();
    std::puts("RTPosixSocketPlatformTests passed");
    return 0;
}
