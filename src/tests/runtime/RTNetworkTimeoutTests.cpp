//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTNetworkTimeoutTests.cpp
// Purpose: Verify network timeouts return promptly and cleanly.
// Key invariants:
//   - A receive timeout returns empty bytes without trapping.
//   - Repeated EINTR wakeups do not restart a socket timeout.
// Ownership/Lifetime:
//   Tests own and close their local sockets and restore process signal state.
// Links: src/runtime/network/rt_network.c
//        src/runtime/network/rt_socket_platform_posix.c
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_network.h"
#include "rt_platform.h"
#include "rt_socket_platform.h"
#include "rt_string.h"
#include "tests/common/NetworkTestCompat.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

// -- vm_trap override ---------------------------------------------------------
namespace {
int g_trap_count = 0;
std::string g_last_trap;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

// -- Test: TCP recv times out cleanly -----------------------------------------
// Strategy: Create a localhost TCP server that accepts connections but never
// sends data. Connect via rt_tcp_connect, set a 100ms recv timeout, call
// rt_tcp_recv -> should return empty bytes (length 0) without trap.
static void test_tcp_recv_timeout() {
#if defined(_WIN32)
    // Initialize WinSock2
    WSADATA wsa;
    int wsarc = WSAStartup(MAKEWORD(2, 2), &wsa);
    assert(wsarc == 0);
#endif

    // Create a TCP listener on a random port
#if defined(_WIN32)
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) {
        printf("  SKIP: TCP recv timeout → local listener unavailable in this environment\n");
        WSACleanup();
        return;
    }
#else
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        printf("  SKIP: TCP recv timeout → local listener unavailable in this environment\n");
        return;
    }
#endif

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(viper::tests::kIpv4LoopbackHostOrder);
    addr.sin_port = 0; // Let OS assign port

    int rc = bind(listener, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0) {
#if defined(_WIN32)
        closesocket(listener);
        WSACleanup();
#else
        close(listener);
#endif
        printf("  SKIP: TCP recv timeout → local bind unavailable in this environment\n");
        return;
    }

    rc = listen(listener, 1);
    if (rc != 0) {
#if defined(_WIN32)
        closesocket(listener);
        WSACleanup();
#else
        close(listener);
#endif
        printf("  SKIP: TCP recv timeout → local listen unavailable in this environment\n");
        return;
    }

    // Get the assigned port
#if defined(_WIN32)
    int addrlen = sizeof(addr);
#else
    socklen_t addrlen = sizeof(addr);
#endif
    getsockname(listener, (struct sockaddr *)&addr, &addrlen);
    int port = ntohs(addr.sin_port);

    // Connect using the runtime API
    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != NULL);

    // Accept on the server side (but never send anything)
#if defined(_WIN32)
    SOCKET client_fd = accept(listener, NULL, NULL);
    assert(client_fd != INVALID_SOCKET);
#else
    int client_fd = accept(listener, NULL, NULL);
    assert(client_fd >= 0);
#endif

    // Set a short recv timeout (100ms) and try to receive
    rt_tcp_set_recv_timeout(conn, 100);

    g_trap_count = 0;
    g_last_trap.clear();

    void *result = rt_tcp_recv(conn, 1024);
    assert(result != NULL);
    assert(rt_bytes_len(result) == 0); // Timeout -> empty bytes
    assert(g_trap_count == 0);         // No trap

    // Clean up
    rt_tcp_close(conn);
#if defined(_WIN32)
    closesocket(client_fd);
    closesocket(listener);
    WSACleanup();
#else
    close(client_fd);
    close(listener);
#endif
}

#if !RT_PLATFORM_WINDOWS
static void timeout_signal_handler(int signal_number) {
    (void)signal_number;
}

static void test_wait_socket_eintr_preserves_deadline() {
    int sockets[2];
    struct sigaction action = {};
    struct sigaction previous_action = {};
    struct itimerval timer = {};

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    action.sa_handler = timeout_signal_handler;
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGALRM, &action, &previous_action) == 0);

    timer.it_value.tv_usec = 10000;
    timer.it_interval.tv_usec = 10000;
    assert(setitimer(ITIMER_REAL, &timer, nullptr) == 0);

    const auto started = std::chrono::steady_clock::now();
    const int ready = wait_socket(sockets[0], 100, false);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    timer = {};
    assert(setitimer(ITIMER_REAL, &timer, nullptr) == 0);
    assert(sigaction(SIGALRM, &previous_action, nullptr) == 0);
    close(sockets[0]);
    close(sockets[1]);

    assert(ready == 0);
    assert(elapsed.count() >= 50);
    assert(elapsed.count() < 1000);
}
#endif

int main() {
    test_tcp_recv_timeout();
    printf("  PASS: TCP recv with 100ms timeout -> empty bytes, no crash\n");

#if !RT_PLATFORM_WINDOWS
    test_wait_socket_eintr_preserves_deadline();
    printf("  PASS: repeated EINTR wakeups preserve the socket timeout deadline\n");
#endif

    printf("All network-timeout tests passed.\n");
    return 0;
}
