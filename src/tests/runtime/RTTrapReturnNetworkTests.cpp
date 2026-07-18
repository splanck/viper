//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTTrapReturnNetworkTests.cpp
// Purpose: Verify network trap sites honor the returning-trap-hook contract:
//   each failure raises exactly one categorized trap and stops local control
//   flow even when the embedder's vm_trap hook returns (VDOC-141).
// Key invariants:
//   - This binary installs a RETURNING vm_trap override, so any trap site that
//     falls through to a second rt_trap_net call is observable as trap_count > 1.
//   - No rt_trap_set_recovery frames are used; dispatch must reach vm_trap.
// Ownership/Lifetime:
//   - No runtime objects outlive main; sockets are owned by the runtime call.
// Links: src/runtime/network/rt_network.c (rt_tcp_connect_for),
//        src/runtime/network/rt_async_socket.c (Future-returning validation),
//        src/runtime/network/rt_sse.c, src/runtime/network/rt_smtp.c
// Cross-platform touchpoints: Windows initializes Winsock explicitly; other
//                             platforms use the runtime's POSIX socket adapter.
//
//===----------------------------------------------------------------------===//

#include "common/PlatformCapabilities.hpp"
#include "rt_async_socket.h"
#include "rt_connpool.h"
#include "rt_error.h"
#include "rt_future.h"
#include "rt_http_client.h"
#include "rt_http_server.h"
#include "rt_https_server.h"
#include "rt_multipart.h"
#include "rt_network.h"
#include "rt_object.h"
#include "rt_restclient.h"
#include "rt_seq.h"
#include "rt_smtp.h"
#include "rt_sse.h"
#include "rt_string.h"
#include "rt_tls.h"
#include "rt_websocket.h"
#include "rt_ws_server.h"
#include "rt_wss_server.h"

extern "C" int rt_trap_get_net_code(void);

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if ZANNA_HOST_WINDOWS
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

static int g_trap_count = 0;
static int g_last_net_code = 0;
static char g_last_msg[256];

// Returning trap hook: records the trap and resumes the caller, exercising
// the "embedder trap hooks may return" contract from rt_trap.h.
extern "C" void vm_trap(const char *msg) {
    g_trap_count++;
    g_last_net_code = rt_trap_get_net_code();
    snprintf(g_last_msg, sizeof(g_last_msg), "%s", msg ? msg : "");
}

/// @brief Release one caller-owned managed reference used by async probes.
/// @details Runs the normal deferred-release/free pair and accepts NULL, so a
///          returning-hook test can clean up a Future regardless of whether an
///          earlier validation branch was able to construct it.
/// @param obj Managed object reference, or NULL.
static void release_managed(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

int main() {
#if ZANNA_HOST_WINDOWS
    WSADATA wsa;
    const int startup_status = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (startup_status != 0) {
        fprintf(stderr, "WSAStartup failed with status %d\n", startup_status);
        return 1;
    }
#endif
    printf("=== Returning-trap-hook network tests ===\n");

    // A TEST-NET-1 address (RFC 5737) is not routable, so the connect either
    // times out (the branch VDOC-141 fixed) or fails fast as unreachable.
    // Either way the contract is the same: exactly ONE categorized trap.
    g_trap_count = 0;
    g_last_net_code = 0;
    g_last_msg[0] = '\0';
    rt_string host = rt_const_cstr("192.0.2.1");
    void *conn = rt_tcp_connect_for(host, 81, 300);

    assert(conn == nullptr);
    printf(
        "  connect trap count: %d (code %d, \"%s\")\n", g_trap_count, g_last_net_code, g_last_msg);
    assert(g_trap_count == 1);
    if (strstr(g_last_msg, "timeout") != nullptr) {
        // The timeout branch must keep its categorized code instead of
        // falling through to the generic Err_NetworkError trap.
        assert(g_last_net_code == Err_Timeout);
    }
    printf("  PASS: one categorized trap per failed connect\n");

    // VDOC-160 sweep: each rejected input raises exactly one trap and the
    // function stops (returning NULL) instead of continuing into allocation,
    // dereference, or a second trap.
    struct probe_result {
        const char *name;
        int traps;
        const void *value;
    } probes[19];

    rt_string certificate = rt_const_cstr("c");
    rt_string private_key = rt_const_cstr("k");
    rt_string content_type = rt_const_cstr("multipart/form-data; boundary=x");
    rt_string sse_url = rt_const_cstr("http://192.0.2.1:81/e");
    rt_string empty_http_url = rt_const_cstr("");
    rt_string post_http_url = rt_const_cstr("http://example.invalid/");
    void *wrong_http_handle = rt_seq_new();
    rt_string rest_base_url = rt_const_cstr("http://example.invalid");
    void *rest_client = rt_restclient_new(rest_base_url);
    void *http_client = rt_http_client_new();
    void *smtp_client = rt_smtp_new(rt_const_cstr("127.0.0.1"), 25);

    g_trap_count = 0;
    probes[0] = {"ConnectionPool NULL receiver", 0, rt_connpool_acquire(nullptr, host, 80)};
    probes[0].traps = g_trap_count;

    g_trap_count = 0;
    probes[1] = {
        "HttpsServer invalid port", 0, rt_https_server_new(70000, certificate, private_key)};
    probes[1].traps = g_trap_count;

    g_trap_count = 0;
    probes[2] = {"Multipart NULL body parse", 0, rt_multipart_parse(content_type, nullptr)};
    probes[2].traps = g_trap_count;

    g_trap_count = 0;
    probes[3] = {"SSE connect failure", 0, rt_sse_connect(sse_url)};
    probes[3].traps = g_trap_count;

    // ResolveAll previously allocated and returned a Seq after its validation
    // trap, which was unsafe when the hook resumed execution.
    g_trap_count = 0;
    probes[4] = {"DNS ResolveAll invalid hostname", 0, rt_dns_resolve_all(nullptr)};
    probes[4].traps = g_trap_count;

    // Build used to allocate and return an empty Bytes fallback after trapping,
    // which both continued failed control flow and could raise a second OOM.
    g_trap_count = 0;
    probes[5] = {"Multipart Build invalid receiver", 0, rt_multipart_build(nullptr)};
    probes[5].traps = g_trap_count;

    // One-shot HTTP wrappers must not manufacture empty fallback objects or
    // continue into transport setup after synchronous validation fails.
    g_trap_count = 0;
    probes[6] = {"HTTP GET empty URL", 0, rt_http_get(empty_http_url)};
    probes[6].traps = g_trap_count;

    g_trap_count = 0;
    probes[7] = {
        "HTTP POST invalid Bytes body", 0, rt_http_post_bytes(post_http_url, wrong_http_handle)};
    probes[7].traps = g_trap_count;

    g_trap_count = 0;
    probes[8] = {
        "HttpReq setter forged receiver", 0, rt_http_req_set_timeout(wrong_http_handle, 10)};
    probes[8].traps = g_trap_count;

    // RestClient request setup catches and cleans nested String validation, but
    // must not fall through into Send after re-raising to a returning hook.
    g_trap_count = 0;
    probes[9] = {"RestClient invalid path handle",
                 0,
                 rt_restclient_get(rest_client, (rt_string)wrong_http_handle)};
    probes[9].traps = g_trap_count;

    // HttpClient stable receiver validation and the outer request transaction
    // must each re-raise once, then return without interpreting forged payload.
    g_trap_count = 0;
    probes[10] = {
        "HttpClient forged receiver", 0, rt_http_client_get(wrong_http_handle, post_http_url)};
    probes[10].traps = g_trap_count;

    g_trap_count = 0;
    probes[11] = {"HttpClient invalid URL handle",
                  0,
                  rt_http_client_get(http_client, (rt_string)wrong_http_handle)};
    probes[11].traps = g_trap_count;

    g_trap_count = 0;
    probes[12] = {"HttpServer forged receiver",
                  0,
                  rt_http_server_process_request(wrong_http_handle, post_http_url)};
    probes[12].traps = g_trap_count;

    g_trap_count = 0;
    probes[13] = {"ServerReq forged receiver", 0, rt_server_req_method(wrong_http_handle)};
    probes[13].traps = g_trap_count;

    g_trap_count = 0;
    probes[14] = {"ServerRes forged receiver", 0, rt_server_res_status(wrong_http_handle, 200)};
    probes[14].traps = g_trap_count;

    g_trap_count = 0;
    probes[15] = {"Tls.Host forged receiver", 0, rt_zanna_tls_host(wrong_http_handle)};
    probes[15].traps = g_trap_count;

    g_trap_count = 0;
    probes[16] = {"Tls.Recv forged receiver", 0, rt_zanna_tls_recv(wrong_http_handle, 1)};
    probes[16].traps = g_trap_count;

    g_trap_count = 0;
    probes[17] = {"WebSocket.Url forged receiver", 0, rt_ws_url(wrong_http_handle)};
    probes[17].traps = g_trap_count;

    g_trap_count = 0;
    probes[18] = {"WebSocket.RecvBytes forged receiver", 0, rt_ws_recv_bytes(wrong_http_handle)};
    probes[18].traps = g_trap_count;

    for (const auto &probe : probes) {
        printf("  %s: traps=%d value=%s\n",
               probe.name,
               probe.traps,
               probe.value ? "non-null" : "null");
        assert(probe.traps == 1);
        assert(probe.value == nullptr);
    }
    printf("  PASS: rejected inputs raise exactly one trap and stop\n");

    g_trap_count = 0;
    int64_t forged_http_port = rt_http_server_port(wrong_http_handle);
    assert(g_trap_count == 1 && forged_http_port == 0);
    g_trap_count = 0;
    int64_t forged_https_port = rt_https_server_port(wrong_http_handle);
    assert(g_trap_count == 1 && forged_https_port == 0);
    g_trap_count = 0;
    int64_t forged_tls_port = rt_zanna_tls_port(wrong_http_handle);
    assert(g_trap_count == 1 && forged_tls_port == 0);
    g_trap_count = 0;
    int8_t forged_tls_open = rt_zanna_tls_is_open(wrong_http_handle);
    assert(g_trap_count == 1 && forged_tls_open == 0);
    g_trap_count = 0;
    int64_t forged_tls_send = rt_zanna_tls_send(wrong_http_handle, wrong_http_handle);
    assert(g_trap_count == 1 && forged_tls_send == -1);
    g_trap_count = 0;
    rt_zanna_tls_close(wrong_http_handle);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    int8_t forged_ws_open = rt_ws_is_open(wrong_http_handle);
    assert(g_trap_count == 1 && forged_ws_open == 0);
    g_trap_count = 0;
    rt_ws_send_bytes(wrong_http_handle, wrong_http_handle);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    rt_ws_close(wrong_http_handle);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    int8_t forged_sse_open = rt_sse_is_open(wrong_http_handle);
    assert(g_trap_count == 1 && forged_sse_open == 0);
    g_trap_count = 0;
    rt_string forged_sse_id = rt_sse_last_event_id(wrong_http_handle);
    assert(g_trap_count == 1 && forged_sse_id == nullptr);
    g_trap_count = 0;
    rt_sse_close(wrong_http_handle);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    int64_t forged_ws_server_port = rt_ws_server_port(wrong_http_handle);
    assert(g_trap_count == 1 && forged_ws_server_port == 0);
    g_trap_count = 0;
    rt_ws_server_broadcast_bytes(wrong_http_handle, wrong_http_handle);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    int64_t forged_wss_server_port = rt_wss_server_port(wrong_http_handle);
    assert(g_trap_count == 1 && forged_wss_server_port == 0);
    g_trap_count = 0;
    rt_wss_server_broadcast_bytes(wrong_http_handle, wrong_http_handle);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    rt_string forged_smtp_error = rt_smtp_last_error(wrong_http_handle);
    assert(g_trap_count == 1 && forged_smtp_error == nullptr);
    g_trap_count = 0;
    int8_t forged_smtp_send = rt_smtp_send(wrong_http_handle, nullptr, nullptr, nullptr, nullptr);
    assert(g_trap_count == 1 && forged_smtp_send == 0);
    g_trap_count = 0;
    rt_smtp_set_tls(wrong_http_handle, 1);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    rt_smtp_set_auth(wrong_http_handle, nullptr, nullptr);
    assert(g_trap_count == 1);
    g_trap_count = 0;
    rt_smtp_close(wrong_http_handle);
    assert(g_trap_count == 1);

    // A valid receiver must also reject forged String arguments once and stop
    // before acquiring a socket or mutating cached credentials.
    g_trap_count = 0;
    int8_t forged_smtp_argument = rt_smtp_send(smtp_client,
                                               (rt_string)wrong_http_handle,
                                               rt_const_cstr("dest@example.com"),
                                               nullptr,
                                               nullptr);
    assert(g_trap_count == 1 && forged_smtp_argument == 0);
    g_trap_count = 0;
    rt_smtp_set_auth(smtp_client, (rt_string)wrong_http_handle, (rt_string)wrong_http_handle);
    assert(g_trap_count == 1);
    printf("  PASS: forged HTTP/HTTPS/TLS/WebSocket/SSE/SMTP handles trap once\n");

    // Http.Download intentionally has a non-trapping Boolean contract. Forged
    // managed handles must be rejected through identity checks without ever
    // reaching the returning vm_trap hook or the filesystem/network adapters.
    rt_string download_path = rt_const_cstr("zanna-invalid-download-target.tmp");
    g_trap_count = 0;
    int8_t invalid_download_url = rt_http_download((rt_string)wrong_http_handle, download_path);
    int8_t invalid_download_path = rt_http_download(post_http_url, (rt_string)wrong_http_handle);
    assert(invalid_download_url == 0);
    assert(invalid_download_path == 0);
    assert(g_trap_count == 0);
    printf("  PASS: Http.Download rejects forged handles without trapping\n");
    rt_string_unref(download_path);

    rt_string_unref(certificate);
    rt_string_unref(private_key);
    rt_string_unref(content_type);
    rt_string_unref(sse_url);
    rt_string_unref(empty_http_url);
    rt_string_unref(post_http_url);
    rt_string_unref(rest_base_url);
    release_managed(rest_client);
    release_managed(http_client);
    release_managed(smtp_client);
    release_managed(wrong_http_handle);

    // AsyncSocket preserves synchronous validation diagnostics for embedders
    // whose trap hook returns, but must stop after that one trap and return an
    // already-failed Future instead of continuing into a worker operation.
    struct async_probe_result {
        const char *name;
        int traps;
        void *future;
    } async_probes[6];

    g_trap_count = 0;
    async_probes[0] = {"Async connect invalid host", 0, rt_async_connect_for(nullptr, 80, 100)};
    async_probes[0].traps = g_trap_count;

    g_trap_count = 0;
    async_probes[1] = {"Async connect invalid port", 0, rt_async_connect_for(host, 70000, 100)};
    async_probes[1].traps = g_trap_count;

    g_trap_count = 0;
    async_probes[2] = {"Async send invalid handles", 0, rt_async_send(nullptr, nullptr)};
    async_probes[2].traps = g_trap_count;

    g_trap_count = 0;
    async_probes[3] = {"Async receive invalid handle", 0, rt_async_recv(nullptr, -1)};
    async_probes[3].traps = g_trap_count;

    g_trap_count = 0;
    async_probes[4] = {"Async HTTP GET empty URL", 0, rt_async_http_get(rt_const_cstr(""))};
    async_probes[4].traps = g_trap_count;

    rt_string post_url = rt_const_cstr("http://example.invalid/");
    g_trap_count = 0;
    async_probes[5] = {"Async HTTP POST invalid body",
                       0,
                       rt_async_http_post(post_url, (rt_string)(uintptr_t)0x1234)};
    async_probes[5].traps = g_trap_count;

    for (const auto &probe : async_probes) {
        printf("  %s: traps=%d future=%s\n",
               probe.name,
               probe.traps,
               probe.future ? "non-null" : "null");
        assert(probe.traps == 1);
        assert(probe.future != nullptr);
        assert(rt_future_wait_for(probe.future, 0) == 1);
        assert(rt_future_is_error(probe.future) == 1);
        release_managed(probe.future);
    }
    printf("  PASS: AsyncSocket rejected inputs trap once and return failed Futures\n");
    rt_string_unref(post_url);
    rt_string_unref(host);

#if ZANNA_HOST_WINDOWS
    WSACleanup();
#endif
    printf("All returning-trap-hook network tests passed.\n");
    return 0;
}
