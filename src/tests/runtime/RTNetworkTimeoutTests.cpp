//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTNetworkTimeoutTests.cpp
// Purpose: Verify that TCP recv with a short timeout returns empty bytes
//          (length 0) without crashing or hanging.
// Key invariants: Timeout must produce a clean result, not a trap or hang.
// Ownership/Lifetime: Creates a localhost listener that accepts but never sends.
// Links: src/runtime/network/rt_network.c
//
//===----------------------------------------------------------------------===//

#include "rt_bytes.h"
#include "rt_internal.h"
#include "rt_network.h"
#include "rt_string.h"

#include <cassert>
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
#include <sys/socket.h>
#include <unistd.h>
#endif

// -- vm_trap override ---------------------------------------------------------
namespace
{
int g_trap_count = 0;
std::string g_last_trap;
} // namespace

extern "C" void vm_trap(const char *msg)
{
    g_trap_count++;
    g_last_trap = msg ? msg : "";
}

// -- Test: TCP recv times out cleanly -----------------------------------------
// Strategy: Create a localhost TCP server that accepts connections but never
// sends data. Connect via rt_tcp_connect, set a 100ms recv timeout, call
// rt_tcp_recv -> should return empty bytes (length 0) without trap.
static void test_tcp_recv_timeout()
{
#if defined(_WIN32)
    // Initialize WinSock2
    WSADATA wsa;
    int wsarc = WSAStartup(MAKEWORD(2, 2), &wsa);
    assert(wsarc == 0);
#endif

    // Create a TCP listener on a random port
#if defined(_WIN32)
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener != INVALID_SOCKET);
#else
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    assert(listener >= 0);
#endif

    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // Let OS assign port

    int rc = bind(listener, (struct sockaddr *)&addr, sizeof(addr));
    assert(rc == 0);

    rc = listen(listener, 1);
    assert(rc == 0);

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

int main()
{
    test_tcp_recv_timeout();
    printf("  PASS: TCP recv with 100ms timeout -> empty bytes, no crash\n");

    printf("All network-timeout tests passed.\n");
    return 0;
}
