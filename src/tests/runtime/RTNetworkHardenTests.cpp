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

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

// ── Trap interception ──────────────────────────────────────────────────────
namespace
{
jmp_buf g_trap_jmp;
const char *g_last_trap = nullptr;
bool g_trap_expected = false;
int g_trap_count = 0;
} // namespace

extern "C" void vm_trap(const char *msg)
{
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
    do                                                                                             \
    {                                                                                              \
        g_trap_expected = true;                                                                    \
        g_last_trap = nullptr;                                                                     \
        g_trap_count = 0;                                                                          \
        if (setjmp(g_trap_jmp) == 0)                                                               \
        {                                                                                          \
            expr;                                                                                  \
            assert(false && "Expected trap did not occur");                                        \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

// ── Helpers ────────────────────────────────────────────────────────────────

/// Create a localhost TCP listener on a random port; returns fd and port.
#if !defined(_WIN32)
static int make_listener(int *out_port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    int rc = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    assert(rc == 0);

    rc = listen(fd, 1);
    assert(rc == 0);

    socklen_t addrlen = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &addrlen);
    *out_port = ntohs(addr.sin_port);

    return fd;
}
#endif

// ── Scenario 1: Connect to nonexistent host ────────────────────────────────
static void test_connect_nonexistent_host()
{
#if !defined(_WIN32)
    rt_string host = rt_string_from_bytes("this.host.does.not.exist.invalid", 32);
    EXPECT_TRAP(rt_tcp_connect_for(host, 80, 2000));

    assert(g_last_trap != nullptr);
    assert(strstr(g_last_trap, "not found") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_HostNotFound);

    printf("  PASS: ConnectNonexistentHost → Err_HostNotFound (%d)\n", code);
#else
    printf("  SKIP: ConnectNonexistentHost (Windows)\n");
#endif
}

// ── Scenario 2: Connect to a port that refuses ─────────────────────────────
static void test_connect_refused_port()
{
#if !defined(_WIN32)
    // Port 1 is almost certainly not listening on localhost.
    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    EXPECT_TRAP(rt_tcp_connect_for(host, 1, 2000));

    assert(g_last_trap != nullptr);
    // Could be "connection refused" or "connection failed" depending on OS.
    assert(strstr(g_last_trap, "refused") != nullptr || strstr(g_last_trap, "failed") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_ConnectionRefused || code == Err_NetworkError);

    printf("  PASS: ConnectRefusedPort → code %d\n", code);
#else
    printf("  SKIP: ConnectRefusedPort (Windows)\n");
#endif
}

// ── Scenario 3: Send after remote close (SIGPIPE test) ─────────────────────
static void test_send_after_remote_close()
{
#if !defined(_WIN32)
    int port = 0;
    int listener = make_listener(&port);

    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != nullptr);

    // Accept then immediately close server side.
    int client_fd = accept(listener, NULL, NULL);
    assert(client_fd >= 0);
    close(client_fd);
    close(listener);

    // Small delay to let the FIN propagate.
    usleep(50000);

    // Send should trap with a network error — NOT kill the process via SIGPIPE.
    void *data = rt_bytes_new(1024);
    EXPECT_TRAP(rt_tcp_send(conn, data));

    assert(g_last_trap != nullptr);
    // Should be some kind of send failure or connection closed.
    int code = rt_trap_get_net_code();
    assert(code == Err_ConnectionReset || code == Err_ConnectionClosed || code == Err_NetworkError);

    printf("  PASS: SendAfterRemoteClose → no SIGPIPE crash, code %d\n", code);

    // Connection is now broken; just release.
    rt_tcp_close(conn);
#else
    printf("  SKIP: SendAfterRemoteClose (Windows)\n");
#endif
}

// ── Scenario 4: Recv on a closed connection ────────────────────────────────
static void test_recv_on_closed_connection()
{
#if !defined(_WIN32)
    int port = 0;
    int listener = make_listener(&port);

    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != nullptr);

    int client_fd = accept(listener, NULL, NULL);
    assert(client_fd >= 0);
    close(client_fd);
    close(listener);

    // Close our own connection, then try to recv.
    rt_tcp_close(conn);

    EXPECT_TRAP(rt_tcp_recv(conn, 1024));

    assert(g_last_trap != nullptr);
    assert(strstr(g_last_trap, "closed") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_ConnectionClosed);

    printf("  PASS: RecvOnClosedConnection → Err_ConnectionClosed (%d)\n", code);
#else
    printf("  SKIP: RecvOnClosedConnection (Windows)\n");
#endif
}

// ── Scenario 5: DNS lookup for nonexistent domain ──────────────────────────
static void test_dns_nonexistent_domain()
{
#if !defined(_WIN32)
    rt_string domain = rt_string_from_bytes("nonexistent.invalid", 19);
    EXPECT_TRAP(rt_dns_resolve(domain));

    assert(g_last_trap != nullptr);
    assert(strstr(g_last_trap, "not found") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_DnsError);

    printf("  PASS: DnsNonexistentDomain → Err_DnsError (%d)\n", code);
#else
    printf("  SKIP: DnsNonexistentDomain (Windows)\n");
#endif
}

// ── Scenario 6: HTTP request with malformed URL ────────────────────────────
// Note: This test is skipped if rt_http_get is not available (link-time check).
// The HTTP functions wrap rt_tcp_connect which we've already tested, so we
// verify the URL validation path specifically.
static void test_http_malformed_url()
{
#if !defined(_WIN32)
    // rt_url_parse traps on malformed URLs.
    rt_string bad_url = rt_string_from_bytes("not-a-valid-url", 15);
    EXPECT_TRAP(rt_url_parse(bad_url));

    assert(g_last_trap != nullptr);
    // Should mention "invalid URL" or "Invalid URL".
    assert(strstr(g_last_trap, "nvalid URL") != nullptr ||
           strstr(g_last_trap, "parse URL") != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_InvalidUrl);

    printf("  PASS: HttpMalformedUrl → Err_InvalidUrl (%d)\n", code);
#else
    printf("  SKIP: HttpMalformedUrl (Windows)\n");
#endif
}

// ── Scenario 7: Connection stall mid-transfer (recv timeout) ───────────────
static void test_connection_stall_mid_transfer()
{
#if !defined(_WIN32)
    int port = 0;
    int listener = make_listener(&port);

    rt_string host = rt_string_from_bytes("127.0.0.1", 9);
    void *conn = rt_tcp_connect(host, port);
    assert(conn != nullptr);

    int client_fd = accept(listener, NULL, NULL);
    assert(client_fd >= 0);

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
    close(client_fd);
    close(listener);
#else
    printf("  SKIP: ConnectionStallMidTransfer (Windows)\n");
#endif
}

// ── Scenario 8: Network unreachable (RFC 5737 TEST-NET) ────────────────────
static void test_network_unreachable()
{
#if !defined(_WIN32)
    // 192.0.2.1 is RFC 5737 TEST-NET-1 — should be unreachable on any real network.
    rt_string host = rt_string_from_bytes("192.0.2.1", 9);
    EXPECT_TRAP(rt_tcp_connect_for(host, 80, 1000));

    assert(g_last_trap != nullptr);
    int code = rt_trap_get_net_code();
    // Could be Err_Timeout (most common) or Err_NetworkError.
    assert(code == Err_Timeout || code == Err_NetworkError);

    printf("  PASS: NetworkUnreachable → code %d\n", code);
#else
    printf("  SKIP: NetworkUnreachable (Windows)\n");
#endif
}

// ── Scenario 9: Resolve IPv4 for nonexistent domain ────────────────────────
static void test_dns_resolve4_nonexistent()
{
#if !defined(_WIN32)
    rt_string domain = rt_string_from_bytes("nohost.invalid", 14);
    EXPECT_TRAP(rt_dns_resolve4(domain));

    assert(g_last_trap != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_DnsError);

    printf("  PASS: DnsResolve4Nonexistent → Err_DnsError (%d)\n", code);
#else
    printf("  SKIP: DnsResolve4Nonexistent (Windows)\n");
#endif
}

// ── Scenario 10: Reverse DNS for non-routable address ──────────────────────
static void test_dns_reverse_invalid()
{
#if !defined(_WIN32)
    rt_string addr = rt_string_from_bytes("192.0.2.1", 9);
    EXPECT_TRAP(rt_dns_reverse(addr));

    assert(g_last_trap != nullptr);
    int code = rt_trap_get_net_code();
    assert(code == Err_DnsError);

    printf("  PASS: DnsReverseInvalid → Err_DnsError (%d)\n", code);
#else
    printf("  SKIP: DnsReverseInvalid (Windows)\n");
#endif
}

// ── Main ───────────────────────────────────────────────────────────────────
int main()
{
    SKIP_TEST_NO_FORK();

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

    printf("All network-harden tests passed.\n");
    return 0;
}
