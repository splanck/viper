//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTTrapReturnNetworkTests.cpp
// Purpose: Verify network trap sites honor the returning-trap-hook contract:
//   each failure raises exactly one categorized trap and stops local control
//   flow even when the embedder's vm_trap hook returns (VDOC-141).
// Key invariants:
//   - This binary installs a RETURNING vm_trap override, so any trap site that
//     falls through to a second rt_trap_net call is observable as trap_count > 1.
//   - No rt_trap_set_recovery frames are used; dispatch must reach vm_trap.
// Ownership/Lifetime:
//   - No runtime objects outlive main; sockets are owned by the runtime call.
// Links: src/runtime/network/rt_network.c (rt_tcp_connect_for)
//
//===----------------------------------------------------------------------===//

#include "rt_connpool.h"
#include "rt_error.h"
#include "rt_https_server.h"
#include "rt_multipart.h"
#include "rt_network.h"
#include "rt_sse.h"
#include "rt_string.h"

extern "C" int rt_trap_get_net_code(void);

#include <cassert>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
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

int main() {
#if defined(_WIN32)
    WSADATA wsa;
    assert(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
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
    printf("  connect trap count: %d (code %d, \"%s\")\n",
           g_trap_count,
           g_last_net_code,
           g_last_msg);
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
    } probes[4];

    g_trap_count = 0;
    probes[0] = {"ConnectionPool NULL receiver", 0, rt_connpool_acquire(nullptr, host, 80)};
    probes[0].traps = g_trap_count;

    g_trap_count = 0;
    probes[1] = {"HttpsServer invalid port",
                 0,
                 rt_https_server_new(70000, rt_const_cstr("c"), rt_const_cstr("k"))};
    probes[1].traps = g_trap_count;

    g_trap_count = 0;
    probes[2] = {"Multipart NULL body parse",
                 0,
                 rt_multipart_parse(rt_const_cstr("multipart/form-data; boundary=x"), nullptr)};
    probes[2].traps = g_trap_count;

    g_trap_count = 0;
    probes[3] = {"SSE connect failure", 0, rt_sse_connect(rt_const_cstr("http://192.0.2.1:81/e"))};
    probes[3].traps = g_trap_count;

    for (const auto &probe : probes) {
        printf("  %s: traps=%d value=%s\n",
               probe.name,
               probe.traps,
               probe.value ? "non-null" : "null");
        assert(probe.traps == 1);
        assert(probe.value == nullptr);
    }
    printf("  PASS: rejected inputs raise exactly one trap and stop\n");

#if defined(_WIN32)
    WSACleanup();
#endif
    printf("All returning-trap-hook network tests passed.\n");
    return 0;
}
