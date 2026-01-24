#include <stdio.h>

#include "servers/netd/net_protocol.hpp"
#include "syscall.hpp"

static void print_ip_be(u32 ip_be) {
    printf("%u.%u.%u.%u",
           (unsigned)((ip_be >> 24) & 0xFF),
           (unsigned)((ip_be >> 16) & 0xFF),
           (unsigned)((ip_be >> 8) & 0xFF),
           (unsigned)(ip_be & 0xFF));
}

static i64 recv_reply_blocking(i32 ch, void *buf, usize buf_len) {
    while (true) {
        u32 handles[4];
        u32 handle_count = 4;
        i64 n = sys::channel_recv(ch, buf, buf_len, handles, &handle_count);
        if (n == VERR_WOULD_BLOCK) {
            sys::yield();
            continue;
        }
        if (n >= 0 && handle_count != 0) {
            // netd_smoke only expects inline replies. Close any unexpected
            // transferred handles to avoid capability table exhaustion.
            for (u32 i = 0; i < handle_count; i++) {
                if (handles[i] == 0)
                    continue;
                i32 close_err = sys::shm_close(handles[i]);
                if (close_err != 0) {
                    (void)sys::cap_revoke(handles[i]);
                }
            }
            return VERR_NOT_SUPPORTED;
        }
        return n;
    }
}

/* Wait for NETD server to be registered (up to ~10 seconds).
 * Each yield gives up the time slice (~1ms at 1000Hz timer). */
static bool wait_for_netd(u32 *handle_out) {
    for (int i = 0; i < 10000; i++) {
        u32 handle = 0xFFFFFFFFu;
        i32 err = sys::assign_get("NETD", &handle);
        if (err == 0 && handle != 0xFFFFFFFFu) {
            *handle_out = handle;
            return true;
        }
        sys::yield();
    }
    return false;
}

extern "C" void _start() {
    /* Wait for NETD server to be available before running tests.
     * This is necessary because the smoke test may be spawned before
     * servers are fully registered (to load ELF before blkd resets device). */
    u32 netd = 0xFFFFFFFFu;
    if (!wait_for_netd(&netd)) {
        printf("[netd_smoke] FAIL: NETD server not available\n");
        sys::exit(1);
    }

    netproto::InfoRequest req = {};
    req.type = netproto::NET_INFO;
    req.request_id = 1;

    auto ch = sys::channel_create();
    if (!ch.ok()) {
        sys::channel_close((i32)netd);
        printf("[netd_smoke] channel_create failed: %ld\n", (long)ch.error);
        sys::exit(1);
    }

    i32 reply_send = static_cast<i32>(ch.val0);
    i32 reply_recv = static_cast<i32>(ch.val1);

    u32 send_handles[1] = {static_cast<u32>(reply_send)};
    i64 send_err = sys::channel_send(static_cast<i32>(netd), &req, sizeof(req), send_handles, 1);
    if (send_err != 0) {
        sys::channel_close(reply_send);
        sys::channel_close(reply_recv);
        sys::channel_close((i32)netd);
        printf("[netd_smoke] request send failed: %ld\n", (long)send_err);
        sys::exit(1);
    }

    netproto::InfoReply reply = {};
    i64 n = recv_reply_blocking(reply_recv, &reply, sizeof(reply));
    sys::channel_close(reply_recv);
    sys::channel_close((i32)netd);

    if (n < 0) {
        printf("[netd_smoke] reply recv failed: %ld\n", (long)n);
        sys::exit(1);
    }

    if (reply.type != netproto::NET_INFO_REPLY || reply.request_id != req.request_id ||
        reply.status != 0) {
        printf("[netd_smoke] bad reply: type=%u req=%u status=%d\n",
               (unsigned)reply.type,
               (unsigned)reply.request_id,
               (int)reply.status);
        sys::exit(1);
    }

    if (reply.ip == 0) {
        printf("[netd_smoke] FAIL: NETD returned ip=0\n");
        sys::exit(1);
    }

    printf("[netd_smoke] OK: NETD ip=");
    print_ip_be(reply.ip);
    printf("\n");
    sys::exit(0);
}
