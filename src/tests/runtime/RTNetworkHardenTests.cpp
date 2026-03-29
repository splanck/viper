//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTNetworkHardenTests.cpp
// Purpose: Adversarial network scenario tests verifying that every failure
//          produces a clean, categorized error code — never a crash, hang,
//          or platform-specific exception leaking through.
// Key invariants:
//   - Network failures always trap with a specific Err_* code.
//   - SIGPIPE never kills the process.
//   - Programming errors (NULL args) still hard-trap.
// Ownership/Lifetime: Creates ephemeral localhost sockets cleaned up per test.
// Links: src/runtime/network/rt_network.c, src/runtime/core/rt_error.h
//
//===----------------------------------------------------------------------===//

#include "tests/common/PosixCompat.h"

#include "rt_bytes.h"
#include "rt_error.h"
#include "rt_internal.h"
#include "rt_network.h"
#include "rt_string.h"

#include <cassert>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
// WinSock uses SOCKET (unsigned) with INVALID_SOCKET; POSIX uses int with -1.
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define SOCK_CLOSE(s) closesocket(s)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define SOCK_CLOSE(s) close(s)
#endif

// ── Trap interception ──────────────────────────────────────────────────────
namespace {
jmp_buf g_trap_jmp;
const char *g_last_trap = nullptr;
bool g_trap_expected = false;
int g_trap_count = 0;
} // namespace

extern "C" void vm_trap(const char *msg) {
    g_last_trap = msg;
    g_trap_count++;
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    // Unexpected trap — print and abort.
    fprintf(stderr, "UNEXPECTED TRAP: %s\n", msg ? msg : "(null)");
    _exit(1);
}

extern "C" int rt_trap_get_net_code(void);

/// Expect a trap to fire; capture it and continue.
#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        g_trap_count = 0;                                                                          \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

// ── Platform init/cleanup ─────────────────────────────────────────────────

static void net_init() {
#if defined(_WIN32)
    WSADATA wsa;
    int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
    assert(rc == 0);
#endif
}

static void net_cleanup() {
#if defined(_WIN32)
    WSACleanup();
#endif
}

// ── Helpers ────────────────────────────────────────────────────────────────

/// Create a localhost TCP listener on a random port; returns socket and port.
static sock_t make_listener(int *out_port) {
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_INVALID)
        return SOCK_INVALID;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0) {
        SOCK_CLOSE(fd);
        return SOCK_INVALID;
    }

    rc = listen(fd, 1);
    if (rc != 0) {
        SOCK_CLOSE(fd);
        return SOCK_INVALID;
    }

#if defined(_WIN32)
    int addrlen = sizeof(addr);
#else
    socklen_t addrlen = sizeof(addr);
#endif
    getsockname(fd, (struct sockaddr *)&addr, &addrlen);
    *out_port = ntohs(addr.sin_port);

    return fd;
}

/// Platform-portable microsecond sleep.
static void sleep_us(int us) {
#if defined(_WIN32)
    Sleep((unsigned)(us / 1000));
#else
    usleep(us);
#endif
}

// ── Scenario 1: Connect to nonexistent host ────────────────────────────────
static void test_connect_nonexistent_host() {
    rt_string host = rt_string_from_bytes("this.host.does.not.exist.invalid", 32);
    EXPECT_TRAP(rt_tcp_connect_for(host, 80, 2000));

    assert(g_last_trap != nullptr);
    assert(strstr(g_last_trap, "not found") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_HostNotFound);

    printf("  PASS: ConnectNonexistentHost → Err_HostNotFound (%d)\n", code);
}

// ── Scenario 2: Connect to a port that refuses ─────────────────────────────
static void test_connect_refused_port() {
    // Port 1 is almost certainly not listening on localhost.
    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    EXPECT_TRAP(rt_tcp_connect_for(host, 1, 2000));

    assert(g_last_trap != nullptr);
    // Could be "connection refused", "connection failed", or "timed out" depending on OS.
    // On Windows, the WinSock error code may be reported without these keywords.
    const bool matchRefused =
        strstr(g_last_trap, "refused") != nullptr || strstr(g_last_trap, "failed") != nullptr ||
        strstr(g_last_trap, "timed out") != nullptr || strstr(g_last_trap, "error") != nullptr;
    if (!matchRefused) {
        fprintf(stderr, "  DEBUG: actual trap = [%s]\n", g_last_trap);
    }
    assert(matchRefused);
    int code = rt_trap_get_net_code();
    // On Windows, connecting to port 1 on localhost may return Err_Timeout
    // instead of Err_ConnectionRefused (WinSock behavior varies).
    assert(code == Err_ConnectionRefused || code == Err_NetworkError || code == Err_Timeout);

    printf("  PASS: ConnectRefusedPort → code %d\n", code);
}

// ── Scenario 3: Send after remote close (SIGPIPE test) ─────────────────────
static void test_send_after_remote_close() {
    int port = 0;
    sock_t listener = make_listener(&port);
    if (listener == SOCK_INVALID) {
        printf("  SKIP: SendAfterRemoteClose → local bind unavailable in this environment\n");
        return;
    }

    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != nullptr);

    // Accept then immediately close server side.
    sock_t client_fd = accept(listener, NULL, NULL);
    assert(client_fd != SOCK_INVALID);
    SOCK_CLOSE(client_fd);
    SOCK_CLOSE(listener);

    // Small delay to let the FIN propagate.
    sleep_us(50000);

    // TCP allows the first send() after peer FIN to succeed (data goes into
    // kernel send buffer; the RST comes back asynchronously).  Send in a loop
    // until the runtime traps with a network error — the key invariant is that
    // we must NOT crash via SIGPIPE.
    void *data = rt_bytes_new(1024);
    bool trapped = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        g_trap_expected = true;
        g_last_trap = nullptr;
        g_trap_count = 0;
        if (setjmp(g_trap_jmp) == 0) {
            rt_tcp_send(conn, data);
            g_trap_expected = false;
            // Send succeeded (kernel buffered) — wait for RST and retry.
            sleep_us(50000);
            continue;
        }
        g_trap_expected = false;
        trapped = true;
        break;
    }
    assert(trapped && "Expected trap did not occur after repeated sends");

    assert(g_last_trap != nullptr);
    // Should be some kind of send failure or connection closed.
    int code = rt_trap_get_net_code();
    assert(code == Err_ConnectionReset || code == Err_ConnectionClosed || code == Err_NetworkError);

    printf("  PASS: SendAfterRemoteClose → no SIGPIPE crash, code %d\n", code);

    // Connection is now broken; just release.
    rt_tcp_close(conn);
}

// ── Scenario 4: Recv on a closed connection ────────────────────────────────
static void test_recv_on_closed_connection() {
    int port = 0;
    sock_t listener = make_listener(&port);
    if (listener == SOCK_INVALID) {
        printf("  SKIP: RecvOnClosedConnection → local bind unavailable in this environment\n");
        return;
    }

    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != nullptr);

    sock_t client_fd = accept(listener, NULL, NULL);
    assert(client_fd != SOCK_INVALID);
    SOCK_CLOSE(client_fd);
    SOCK_CLOSE(listener);

    // Close our own connection, then try to recv.
    rt_tcp_close(conn);

    EXPECT_TRAP(rt_tcp_recv(conn, 1024));

    assert(g_last_trap != nullptr);
    assert(strstr(g_last_trap, "closed") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_ConnectionClosed);

    printf("  PASS: RecvOnClosedConnection → Err_ConnectionClosed (%d)\n", code);
}

// ── Scenario 5: DNS lookup for nonexistent domain ──────────────────────────
static void test_dns_nonexistent_domain() {
    rt_string domain = rt_string_from_bytes("nonexistent.invalid", 19);
    EXPECT_TRAP(rt_dns_resolve(domain));

    assert(g_last_trap != nullptr);
    assert(strstr(g_last_trap, "not found") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_DnsError);

    printf("  PASS: DnsNonexistentDomain → Err_DnsError (%d)\n", code);
}

// ── Scenario 6: HTTP request with malformed URL ────────────────────────────
// Note: This test is skipped if rt_http_get is not available (link-time check).
// The HTTP functions wrap rt_tcp_connect which we've already tested, so we
// verify the URL validation path specifically.
static void test_http_malformed_url() {
    // rt_url_parse traps on empty URLs (the parser is lenient for
    // scheme-less strings, treating them as relative path references).
    rt_string bad_url = rt_string_from_bytes("", 0);
    EXPECT_TRAP(rt_url_parse(bad_url));

    assert(g_last_trap != nullptr);
    // Should mention "invalid URL" or "Invalid URL".
    assert(strstr(g_last_trap, "nvalid URL") != nullptr ||
           strstr(g_last_trap, "parse URL") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_InvalidUrl);

    printf("  PASS: HttpMalformedUrl → Err_InvalidUrl (%d)\n", code);
}

// ── Scenario 7: Connection stall mid-transfer (recv timeout) ───────────────
static void test_connection_stall_mid_transfer() {
    int port = 0;
    sock_t listener = make_listener(&port);
    if (listener == SOCK_INVALID) {
        printf("  SKIP: ConnectionStallMidTransfer → local bind unavailable in this environment\n");
        return;
    }

    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != nullptr);

    sock_t client_fd = accept(listener, NULL, NULL);
    assert(client_fd != SOCK_INVALID);

    // Send a few bytes then stall (never send more).
    const char *partial = "partial";
    send(client_fd, partial, 7, 0);

    // Set a very short recv timeout (200ms).
    rt_tcp_set_recv_timeout(conn, 200);

    // First recv should succeed (gets partial data).
    g_trap_count = 0;
    void *result = rt_tcp_recv(conn, 1024);
    assert(result != nullptr);
    assert(rt_bytes_len(result) == 7);
    assert(g_trap_count == 0);

    // Second recv should timeout (server is stalling).
    result = rt_tcp_recv(conn, 1024);
    assert(result != nullptr);
    assert(rt_bytes_len(result) == 0); // Timeout → empty bytes.
    assert(g_trap_count == 0);

    printf("  PASS: ConnectionStallMidTransfer → timeout returns empty bytes\n");

    rt_tcp_close(conn);
    SOCK_CLOSE(client_fd);
    SOCK_CLOSE(listener);
}

// ── Scenario 8: Network unreachable (RFC 5737 TEST-NET) ────────────────────
static void test_network_unreachable() {
    // 192.0.2.1 is RFC 5737 TEST-NET-1 — should be unreachable on any real network.
    rt_string host = rt_string_from_bytes("192.0.2.1", 9);
    EXPECT_TRAP(rt_tcp_connect_for(host, 80, 1000));

    assert(g_last_trap != nullptr);
    int code = rt_trap_get_net_code();
    // Could be Err_Timeout (most common) or Err_NetworkError.
    assert(code == Err_Timeout || code == Err_NetworkError);

    printf("  PASS: NetworkUnreachable → code %d\n", code);
}

// ── Scenario 9: Resolve IPv4 for nonexistent domain ────────────────────────
static void test_dns_resolve4_nonexistent() {
    rt_string domain = rt_string_from_bytes("nohost.invalid", 14);
    EXPECT_TRAP(rt_dns_resolve4(domain));

    assert(g_last_trap != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_DnsError);

    printf("  PASS: DnsResolve4Nonexistent → Err_DnsError (%d)\n", code);
}

// ── Scenario 10: Reverse DNS for non-routable address ──────────────────────
static void test_dns_reverse_invalid() {
    rt_string addr = rt_string_from_bytes("192.0.2.1", 9);
    EXPECT_TRAP(rt_dns_reverse(addr));

    assert(g_last_trap != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_DnsError);

    printf("  PASS: DnsReverseInvalid → Err_DnsError (%d)\n", code);
}

// ── Main ───────────────────────────────────────────────────────────────────
int main() {
    net_init();

    test_connect_nonexistent_host();
    test_connect_refused_port();
    test_send_after_remote_close();
    test_recv_on_closed_connection();
    test_dns_nonexistent_domain();
    test_http_malformed_url();
    test_connection_stall_mid_transfer();
    test_network_unreachable();
    test_dns_resolve4_nonexistent();
    test_dns_reverse_invalid();

    net_cleanup();

    printf("All network-harden tests passed.\n");
    return 0;
}
