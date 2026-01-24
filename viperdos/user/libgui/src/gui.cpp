/**
 * @file gui.cpp
 * @brief ViperDOS GUI client library implementation.
 *
 * @details
 * Communicates with displayd via IPC to create windows, manage surfaces,
 * and handle input events.
 */

#include "../include/gui.h"
#include "../../servers/displayd/display_protocol.hpp"
#include "../../syscall.hpp"

using namespace display_protocol;

// =============================================================================
// Internal State
// =============================================================================

static int32_t g_display_channel = -1; // Channel to displayd
static uint32_t g_request_id = 1;
static bool g_initialized = false;

struct gui_window {
    uint32_t surface_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t shm_handle;
    uint32_t *pixels;
    char title[64];
    int32_t event_channel; // Channel for receiving events
};

// Complete 8x8 bitmap font (ASCII 32-127)
static const uint8_t g_font[96][8] = {
    // 32: Space
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 33: !
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    // 34: "
    {0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 35: #
    {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    // 36: $
    {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00},
    // 37: %
    {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00},
    // 38: &
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    // 39: '
    {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 40: (
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    // 41: )
    {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    // 42: *
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    // 43: +
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    // 44: ,
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    // 45: -
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    // 46: .
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    // 47: /
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    // 48: 0
    {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
    // 49: 1
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    // 50: 2
    {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    // 51: 3
    {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00},
    // 52: 4
    {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00},
    // 53: 5
    {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00},
    // 54: 6
    {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00},
    // 55: 7
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    // 56: 8
    {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00},
    // 57: 9
    {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00},
    // 58: :
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00},
    // 59: ;
    {0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30},
    // 60: <
    {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    // 61: =
    {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    // 62: >
    {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00},
    // 63: ?
    {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00},
    // 64: @
    {0x3C, 0x66, 0x6E, 0x6A, 0x6E, 0x60, 0x3C, 0x00},
    // 65: A
    {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00},
    // 66: B
    {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    // 67: C
    {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    // 68: D
    {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00},
    // 69: E
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00},
    // 70: F
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00},
    // 71: G
    {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00},
    // 72: H
    {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    // 73: I
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    // 74: J
    {0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00},
    // 75: K
    {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00},
    // 76: L
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00},
    // 77: M
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
    // 78: N
    {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00},
    // 79: O
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    // 80: P
    {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00},
    // 81: Q
    {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00},
    // 82: R
    {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00},
    // 83: S
    {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00},
    // 84: T
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    // 85: U
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00},
    // 86: V
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    // 87: W
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    // 88: X
    {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00},
    // 89: Y
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    // 90: Z
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00},
    // 91: [
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    // 92: backslash
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00},
    // 93: ]
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    // 94: ^
    {0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 95: _
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    // 96: `
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 97: a
    {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00},
    // 98: b
    {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00},
    // 99: c
    {0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00},
    // 100: d
    {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
    // 101: e
    {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
    // 102: f
    {0x1C, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00},
    // 103: g
    {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    // 104: h
    {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    // 105: i
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    // 106: j
    {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38},
    // 107: k
    {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00},
    // 108: l
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    // 109: m
    {0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    // 110: n
    {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
    // 111: o
    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
    // 112: p
    {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60},
    // 113: q
    {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06},
    // 114: r
    {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00},
    // 115: s
    {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
    // 116: t
    {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00},
    // 117: u
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
    // 118: v
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00},
    // 119: w
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    // 120: x
    {0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00},
    // 121: y
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C},
    // 122: z
    {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00},
    // 123: {
    {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    // 124: |
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    // 125: }
    {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    // 126: ~
    {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // 127: DEL (block)
    {0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0x00, 0x00},
};

// =============================================================================
// Helper Functions
// =============================================================================

// Debug helper for libgui
static void debug_num(const char *prefix, int64_t val) {
    sys::print(prefix);
    if (val < 0) {
        sys::print("-");
        val = -val;
    }
    char buf[24];
    int i = 22;
    buf[23] = '\0';
    do {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    } while (val && i >= 0);
    sys::print(&buf[i + 1]);
    sys::print("\n");
}

static bool send_request_recv_reply(const void *req,
                                    size_t req_len,
                                    void *reply,
                                    size_t reply_len,
                                    uint32_t *out_handles = nullptr,
                                    uint32_t *handle_count = nullptr) {
    sys::print("[gui] send_request_recv_reply entry\n");
    if (g_display_channel < 0)
        return false;

    sys::print("[gui] calling channel_create\n");
    // Create reply channel
    auto ch_result = sys::channel_create();
    sys::print("[gui] channel_create returned\n");
    if (ch_result.error != 0) {
        debug_num("[libgui] channel_create failed: ", ch_result.error);
        return false;
    }

    int32_t send_ch = static_cast<int32_t>(ch_result.val0); // CAP_WRITE - for sending
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1); // CAP_READ - for receiving
    debug_num("[gui] send_ch=", send_ch);
    debug_num("[gui] recv_ch=", recv_ch);

    // Send request with the SEND endpoint so displayd can write the reply back
    uint32_t send_handles[1] = {static_cast<uint32_t>(send_ch)};
    sys::print("[gui] calling channel_send\n");
    int64_t err = sys::channel_send(g_display_channel, req, req_len, send_handles, 1);
    sys::print("[gui] channel_send returned\n");
    if (err != 0) {
        debug_num("[libgui] send failed: ", err);
        sys::channel_close(send_ch);
        sys::channel_close(recv_ch);
        return false;
    }

    // Wait for reply on the RECV endpoint with yield between attempts.
    // Previously this was a busy spin loop that wasted CPU cycles and could
    // prevent displayd from getting scheduled to process the request.
    uint32_t recv_handles[4];
    uint32_t recv_handle_count = 4;

    sys::print("[gui] entering recv loop\n");
    // Note: send_ch was transferred to displayd, so we no longer own it
    // Use a reasonable timeout: 500 attempts * 10ms sleep = 5 seconds max
    // CRITICAL: Must use sleep() not yield() - yield() doesn't guarantee
    // any time passes, so the loop can spin through all iterations before
    // displayd gets scheduled to process the message.
    for (uint32_t i = 0; i < 500; i++) {
        recv_handle_count = 4;
        int64_t n = sys::channel_recv(recv_ch, reply, reply_len, recv_handles, &recv_handle_count);
        if (i == 0)
            debug_num("[gui] first recv returned: ", n);
        if (n > 0) {
            sys::channel_close(recv_ch);
            if (out_handles && handle_count) {
                for (uint32_t j = 0; j < recv_handle_count && j < *handle_count; j++) {
                    out_handles[j] = recv_handles[j];
                }
                *handle_count = recv_handle_count;
            }
            return true;
        }
        if (n != VERR_WOULD_BLOCK) {
            // Debug: unexpected error
            debug_num("[libgui] recv error: ", n);
            break;
        }
        // Sleep to let displayd get scheduled and process the request
        // sleep() actually waits, unlike yield() which just gives up timeslice
        sys::sleep(10);
    }

    debug_num("[libgui] recv timeout after loops: ", 500);
    sys::channel_close(recv_ch);
    return false;
}

// =============================================================================
// Initialization
// =============================================================================

extern "C" int gui_init(void) {
    if (g_initialized)
        return 0;

    // Connect to displayd via DISPLAY assign
    uint32_t handle = 0xFFFFFFFFu;
    if (sys::assign_get("DISPLAY", &handle) != 0 || handle == 0xFFFFFFFFu) {
        return -1; // displayd not available
    }

    g_display_channel = static_cast<int32_t>(handle);
    g_initialized = true;
    return 0;
}

extern "C" void gui_shutdown(void) {
    if (!g_initialized)
        return;

    if (g_display_channel >= 0) {
        sys::channel_close(g_display_channel);
        g_display_channel = -1;
    }

    g_initialized = false;
}

extern "C" int gui_get_display_info(gui_display_info_t *info) {
    if (!g_initialized || !info)
        return -1;

    GetInfoRequest req;
    req.type = DISP_GET_INFO;
    req.request_id = g_request_id++;

    GetInfoReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply))) {
        return -1;
    }

    if (reply.status != 0) {
        return reply.status;
    }

    info->width = reply.width;
    info->height = reply.height;
    info->format = reply.format;
    return 0;
}

// =============================================================================
// Window Management
// =============================================================================

extern "C" gui_window_t *gui_create_window(const char *title, uint32_t width, uint32_t height) {
    if (!g_initialized)
        return nullptr;

    CreateSurfaceRequest req;
    req.type = DISP_CREATE_SURFACE;
    req.request_id = g_request_id++;
    req.width = width;
    req.height = height;
    req.flags = 0;

    // Copy title
    size_t i = 0;
    if (title) {
        while (i < 63 && title[i]) {
            req.title[i] = title[i];
            i++;
        }
    }
    req.title[i] = '\0';

    CreateSurfaceReply reply;
    uint32_t handles[4];
    uint32_t handle_count = 4;

    if (!send_request_recv_reply(
            &req, sizeof(req), &reply, sizeof(reply), handles, &handle_count)) {
        return nullptr;
    }

    if (reply.status != 0 || handle_count == 0) {
        return nullptr;
    }

    // Map shared memory
    auto map_result = sys::shm_map(handles[0]);
    if (map_result.error != 0) {
        sys::shm_close(handles[0]);
        return nullptr;
    }

    // Allocate window structure
    gui_window_t *win = new gui_window_t();
    if (!win) {
        sys::shm_unmap(map_result.virt_addr);
        sys::shm_close(handles[0]);
        return nullptr;
    }

    win->surface_id = reply.surface_id;
    win->width = width;
    win->height = height;
    win->stride = reply.stride;
    win->shm_handle = handles[0];
    win->pixels = reinterpret_cast<uint32_t *>(map_result.virt_addr);
    win->event_channel = -1;

    // Copy title
    i = 0;
    if (title) {
        while (i < 63 && title[i]) {
            win->title[i] = title[i];
            i++;
        }
    }
    win->title[i] = '\0';

    // Subscribe to events via dedicated channel (avoids flooding service channel)
    auto ev_ch = sys::channel_create();
    if (ev_ch.error == 0) {
        int32_t ev_send = static_cast<int32_t>(ev_ch.val0); // Write end for displayd
        int32_t ev_recv = static_cast<int32_t>(ev_ch.val1); // Read end for us

        SubscribeEventsRequest sub_req;
        sub_req.type = DISP_SUBSCRIBE_EVENTS;
        sub_req.request_id = g_request_id++;
        sub_req.surface_id = win->surface_id;

        // Create reply channel for subscribe request
        auto reply_ch = sys::channel_create();
        if (reply_ch.error == 0) {
            int32_t reply_send = static_cast<int32_t>(reply_ch.val0);
            int32_t reply_recv = static_cast<int32_t>(reply_ch.val1);

            // Send subscribe request with reply channel first, then event channel
            uint32_t send_handles[2] = {static_cast<uint32_t>(reply_send),
                                        static_cast<uint32_t>(ev_send)};
            int64_t err =
                sys::channel_send(g_display_channel, &sub_req, sizeof(sub_req), send_handles, 2);

            if (err == 0) {
                // Wait for reply (100 attempts * 10ms = 1 second timeout)
                GenericReply sub_reply;
                for (uint32_t j = 0; j < 100; j++) {
                    int64_t n = sys::channel_recv(
                        reply_recv, &sub_reply, sizeof(sub_reply), nullptr, nullptr);
                    if (n > 0) {
                        if (sub_reply.status == 0) {
                            win->event_channel = ev_recv;
                        }
                        break;
                    }
                    if (n != VERR_WOULD_BLOCK)
                        break;
                    sys::sleep(10);
                }
            }
            sys::channel_close(reply_recv);
        }

        // If subscription failed, close the event channel
        if (win->event_channel < 0) {
            sys::channel_close(ev_recv);
        }
    }

    return win;
}

extern "C" void gui_destroy_window(gui_window_t *win) {
    if (!win)
        return;

    // Send destroy request
    DestroySurfaceRequest req;
    req.type = DISP_DESTROY_SURFACE;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));

    // Clean up local resources
    if (win->pixels) {
        sys::shm_unmap(reinterpret_cast<uint64_t>(win->pixels));
    }
    sys::shm_close(win->shm_handle);

    if (win->event_channel >= 0) {
        sys::channel_close(win->event_channel);
    }

    delete win;
}

extern "C" void gui_set_title(gui_window_t *win, const char *title) {
    if (!win || !title)
        return;

    SetTitleRequest req;
    req.type = DISP_SET_TITLE;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;

    size_t i = 0;
    while (i < 63 && title[i]) {
        req.title[i] = title[i];
        win->title[i] = title[i];
        i++;
    }
    req.title[i] = '\0';
    win->title[i] = '\0';

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));
}

extern "C" const char *gui_get_title(gui_window_t *win) {
    return win ? win->title : nullptr;
}

extern "C" gui_window_t *gui_create_window_ex(const char *title,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t flags) {
    sys::print("[gui] create_window_ex entry\n");
    if (!g_initialized)
        return nullptr;

    CreateSurfaceRequest req;
    req.type = DISP_CREATE_SURFACE;
    req.request_id = g_request_id++;
    req.width = width;
    req.height = height;
    req.flags = flags;

    // Copy title
    size_t i = 0;
    if (title) {
        while (i < 63 && title[i]) {
            req.title[i] = title[i];
            i++;
        }
    }
    req.title[i] = '\0';

    CreateSurfaceReply reply;
    uint32_t handles[4];
    uint32_t handle_count = 4;

    sys::print("[gui] sending create request\n");
    if (!send_request_recv_reply(
            &req, sizeof(req), &reply, sizeof(reply), handles, &handle_count)) {
        sys::print("[gui] send_request_recv_reply failed\n");
        return nullptr;
    }
    sys::print("[gui] got reply\n");

    if (reply.status != 0 || handle_count == 0) {
        sys::print("[gui] reply status bad\n");
        return nullptr;
    }

    // Map shared memory
    sys::print("[gui] mapping shm\n");
    auto map_result = sys::shm_map(handles[0]);
    if (map_result.error != 0) {
        sys::print("[gui] shm_map failed\n");
        sys::shm_close(handles[0]);
        return nullptr;
    }
    sys::print("[gui] shm mapped OK\n");

    // Allocate window structure
    sys::print("[gui] allocating window struct\n");
    gui_window_t *win = new gui_window_t();
    sys::print("[gui] new returned\n");
    if (!win) {
        sys::shm_unmap(map_result.virt_addr);
        sys::shm_close(handles[0]);
        return nullptr;
    }
    sys::print("[gui] window struct allocated\n");

    win->surface_id = reply.surface_id;
    win->width = width;
    win->height = height;
    win->stride = reply.stride;
    win->shm_handle = handles[0];
    win->pixels = reinterpret_cast<uint32_t *>(map_result.virt_addr);
    win->event_channel = -1;

    // Copy title
    i = 0;
    if (title) {
        while (i < 63 && title[i]) {
            win->title[i] = title[i];
            i++;
        }
    }
    win->title[i] = '\0';

    // Subscribe to events via dedicated channel (avoids flooding service channel)
    auto ev_ch = sys::channel_create();
    if (ev_ch.error == 0) {
        int32_t ev_send = static_cast<int32_t>(ev_ch.val0); // Write end for displayd
        int32_t ev_recv = static_cast<int32_t>(ev_ch.val1); // Read end for us

        SubscribeEventsRequest sub_req;
        sub_req.type = DISP_SUBSCRIBE_EVENTS;
        sub_req.request_id = g_request_id++;
        sub_req.surface_id = win->surface_id;

        // Create reply channel for subscribe request
        auto reply_ch = sys::channel_create();
        if (reply_ch.error == 0) {
            int32_t reply_send = static_cast<int32_t>(reply_ch.val0);
            int32_t reply_recv = static_cast<int32_t>(reply_ch.val1);

            // Send subscribe request with reply channel first, then event channel
            uint32_t send_handles[2] = {static_cast<uint32_t>(reply_send),
                                        static_cast<uint32_t>(ev_send)};
            int64_t err =
                sys::channel_send(g_display_channel, &sub_req, sizeof(sub_req), send_handles, 2);

            if (err == 0) {
                // Wait for reply (100 attempts * 10ms = 1 second timeout)
                GenericReply sub_reply;
                for (uint32_t j = 0; j < 100; j++) {
                    int64_t n = sys::channel_recv(
                        reply_recv, &sub_reply, sizeof(sub_reply), nullptr, nullptr);
                    if (n > 0) {
                        if (sub_reply.status == 0) {
                            win->event_channel = ev_recv;
                        }
                        break;
                    }
                    if (n != VERR_WOULD_BLOCK)
                        break;
                    sys::sleep(10);
                }
            }
            sys::channel_close(reply_recv);
        }

        // If subscription failed, close the event channel
        if (win->event_channel < 0) {
            sys::channel_close(ev_recv);
        }
    }

    return win;
}

extern "C" int gui_list_windows(gui_window_list_t *list) {
    if (!g_initialized || !list)
        return -1;

    ListWindowsRequest req;
    req.type = DISP_LIST_WINDOWS;
    req.request_id = g_request_id++;

    ListWindowsReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply))) {
        return -1;
    }

    if (reply.status != 0) {
        return reply.status;
    }

    list->count = reply.window_count;
    for (uint32_t i = 0; i < reply.window_count && i < 16; i++) {
        list->windows[i].surface_id = reply.windows[i].surface_id;
        list->windows[i].minimized = reply.windows[i].minimized;
        list->windows[i].maximized = reply.windows[i].maximized;
        list->windows[i].focused = reply.windows[i].focused;
        for (int j = 0; j < 64; j++) {
            list->windows[i].title[j] = reply.windows[i].title[j];
        }
    }

    return 0;
}

extern "C" int gui_restore_window(uint32_t surface_id) {
    if (!g_initialized)
        return -1;

    RestoreWindowRequest req;
    req.type = DISP_RESTORE_WINDOW;
    req.request_id = g_request_id++;
    req.surface_id = surface_id;

    GenericReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply))) {
        return -1;
    }

    return reply.status;
}

extern "C" void gui_set_position(gui_window_t *win, int32_t x, int32_t y) {
    if (!win)
        return;

    SetGeometryRequest req;
    req.type = DISP_SET_GEOMETRY;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;
    req.x = x;
    req.y = y;

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));
}

// =============================================================================
// Scrollbar Support
// =============================================================================

extern "C" void gui_set_vscrollbar(gui_window_t *win,
                                   int32_t content_height,
                                   int32_t viewport_height,
                                   int32_t scroll_pos) {
    if (!win)
        return;

    SetScrollbarRequest req;
    req.type = DISP_SET_SCROLLBAR;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;
    req.vertical = 1;
    req.enabled = (content_height > 0 && content_height > viewport_height) ? 1 : 0;
    req.content_size = content_height;
    req.viewport_size = viewport_height;
    req.scroll_pos = scroll_pos;
    req._pad[0] = 0;
    req._pad[1] = 0;

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));
}

extern "C" void gui_set_hscrollbar(gui_window_t *win,
                                   int32_t content_width,
                                   int32_t viewport_width,
                                   int32_t scroll_pos) {
    if (!win)
        return;

    SetScrollbarRequest req;
    req.type = DISP_SET_SCROLLBAR;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;
    req.vertical = 0;
    req.enabled = (content_width > 0 && content_width > viewport_width) ? 1 : 0;
    req.content_size = content_width;
    req.viewport_size = viewport_width;
    req.scroll_pos = scroll_pos;
    req._pad[0] = 0;
    req._pad[1] = 0;

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));
}

// =============================================================================
// Pixel Buffer Access
// =============================================================================

extern "C" uint32_t *gui_get_pixels(gui_window_t *win) {
    return win ? win->pixels : nullptr;
}

extern "C" uint32_t gui_get_width(gui_window_t *win) {
    return win ? win->width : 0;
}

extern "C" uint32_t gui_get_height(gui_window_t *win) {
    return win ? win->height : 0;
}

extern "C" uint32_t gui_get_stride(gui_window_t *win) {
    return win ? win->stride : 0;
}

// =============================================================================
// Display Update
// =============================================================================

extern "C" void gui_present(gui_window_t *win) {
    gui_present_region(win, 0, 0, 0, 0); // 0,0,0,0 = full surface
}

extern "C" void gui_present_async(gui_window_t *win) {
    if (!win)
        return;

    PresentRequest req;
    req.type = DISP_PRESENT;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;
    req.damage_x = 0;
    req.damage_y = 0;
    req.damage_w = 0;
    req.damage_h = 0;

    // Fire-and-forget: send without waiting for reply
    sys::channel_send(g_display_channel, &req, sizeof(req), nullptr, 0);
}

extern "C" void gui_present_region(
    gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!win)
        return;

    PresentRequest req;
    req.type = DISP_PRESENT;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;
    req.damage_x = x;
    req.damage_y = y;
    req.damage_w = w;
    req.damage_h = h;

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));
}

// =============================================================================
// Events
// =============================================================================

extern "C" int gui_poll_event(gui_window_t *win, gui_event_t *event) {
    if (!win || !event)
        return -1;

    // If we have an event channel, receive directly from it (fast path)
    if (win->event_channel >= 0) {
        // Buffer large enough for any event type
        uint8_t ev_buf[64];
        uint32_t recv_handles[4];
        uint32_t handle_count = 4;
        int64_t n = sys::channel_recv(
            win->event_channel, ev_buf, sizeof(ev_buf), recv_handles, &handle_count);

        if (n <= 0) {
            // No event available - yield to prevent busy loop
            // This is critical: without yield, fast polling can starve other processes
            sys::yield();
            event->type = GUI_EVENT_NONE;
            return -1;
        }

        // First 4 bytes is event type
        uint32_t ev_type = *reinterpret_cast<uint32_t *>(ev_buf);

        switch (ev_type) {
            case DISP_EVENT_MOUSE: {
                auto *mouse = reinterpret_cast<MouseEvent *>(ev_buf);
                event->type = GUI_EVENT_MOUSE;
                event->mouse.x = mouse->x;
                event->mouse.y = mouse->y;
                event->mouse.dx = mouse->dx;
                event->mouse.dy = mouse->dy;
                event->mouse.buttons = mouse->buttons;
                event->mouse.event_type = mouse->event_type;
                event->mouse.button = mouse->button;
                event->mouse._pad = 0;
                break;
            }

            case DISP_EVENT_KEY: {
                auto *key = reinterpret_cast<KeyEvent *>(ev_buf);
                event->type = GUI_EVENT_KEY;
                event->key.keycode = key->keycode;
                event->key.modifiers = key->modifiers;
                event->key.pressed = key->pressed;
                break;
            }

            case DISP_EVENT_FOCUS: {
                auto *focus = reinterpret_cast<FocusEvent *>(ev_buf);
                event->type = GUI_EVENT_FOCUS;
                event->focus.gained = focus->gained;
                event->focus._pad[0] = 0;
                event->focus._pad[1] = 0;
                event->focus._pad[2] = 0;
                break;
            }

            case DISP_EVENT_CLOSE:
                event->type = GUI_EVENT_CLOSE;
                break;

            case DISP_EVENT_RESIZE: {
                auto *resize = reinterpret_cast<ResizeEvent *>(ev_buf);

                // Check if we received a new SHM handle
                if (handle_count > 0) {
                    // Unmap old shared memory
                    if (win->pixels) {
                        sys::shm_unmap(reinterpret_cast<uint64_t>(win->pixels));
                    }
                    sys::shm_close(win->shm_handle);

                    // Map new shared memory
                    uint32_t new_handle = recv_handles[0];
                    auto map_result = sys::shm_map(new_handle);
                    if (map_result.error == 0) {
                        win->shm_handle = new_handle;
                        win->pixels = reinterpret_cast<uint32_t *>(map_result.virt_addr);
                        win->width = resize->new_width;
                        win->height = resize->new_height;
                        win->stride = resize->new_stride;
                    } else {
                        // Failed to map - window is now in broken state
                        win->pixels = nullptr;
                        win->shm_handle = new_handle;
                    }
                }

                event->type = GUI_EVENT_RESIZE;
                event->resize.width = resize->new_width;
                event->resize.height = resize->new_height;
                break;
            }

            case DISP_EVENT_SCROLL: {
                auto *scroll = reinterpret_cast<ScrollEvent *>(ev_buf);
                event->type = GUI_EVENT_SCROLL;
                event->scroll.position = scroll->new_position;
                event->scroll.vertical = scroll->vertical;
                event->scroll._pad[0] = 0;
                event->scroll._pad[1] = 0;
                event->scroll._pad[2] = 0;
                break;
            }

            default:
                event->type = GUI_EVENT_NONE;
                return -1;
        }

        return 0; // Event available
    }

    // Fall back to request/reply for legacy compatibility (slow path)
    PollEventRequest req;
    req.type = DISP_POLL_EVENT;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;

    PollEventReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply))) {
        return -1; // Communication error
    }

    if (reply.has_event == 0) {
        event->type = GUI_EVENT_NONE;
        return -1; // No event available
    }

    // Convert displayd event to libgui event
    switch (reply.event_type) {
        case DISP_EVENT_MOUSE:
            event->type = GUI_EVENT_MOUSE;
            event->mouse.x = reply.mouse.x;
            event->mouse.y = reply.mouse.y;
            event->mouse.dx = reply.mouse.dx;
            event->mouse.dy = reply.mouse.dy;
            event->mouse.buttons = reply.mouse.buttons;
            event->mouse.event_type = reply.mouse.event_type;
            event->mouse.button = reply.mouse.button;
            event->mouse._pad = 0;
            break;

        case DISP_EVENT_KEY:
            event->type = GUI_EVENT_KEY;
            event->key.keycode = reply.key.keycode;
            event->key.modifiers = reply.key.modifiers;
            event->key.pressed = reply.key.pressed;
            break;

        case DISP_EVENT_FOCUS:
            event->type = GUI_EVENT_FOCUS;
            event->focus.gained = reply.focus.gained;
            event->focus._pad[0] = 0;
            event->focus._pad[1] = 0;
            event->focus._pad[2] = 0;
            break;

        case DISP_EVENT_CLOSE:
            event->type = GUI_EVENT_CLOSE;
            break;

        default:
            event->type = GUI_EVENT_NONE;
            return -1;
    }

    return 0; // Event available
}

extern "C" int gui_wait_event(gui_window_t *win, gui_event_t *event) {
    if (!win || !event)
        return -1;

    // Poll with yield until an event arrives
    while (true) {
        if (gui_poll_event(win, event) == 0) {
            return 0;
        }
        sys::yield();
    }
}

// =============================================================================
// Drawing Helpers
// =============================================================================

extern "C" void gui_fill_rect(
    gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win || !win->pixels)
        return;

    uint32_t x2 = x + w;
    uint32_t y2 = y + h;
    if (x2 > win->width)
        x2 = win->width;
    if (y2 > win->height)
        y2 = win->height;

    uint32_t stride_pixels = win->stride / 4;
    for (uint32_t py = y; py < y2; py++) {
        for (uint32_t px = x; px < x2; px++) {
            win->pixels[py * stride_pixels + px] = color;
        }
    }
}

extern "C" void gui_draw_rect(
    gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win || !win->pixels || w == 0 || h == 0)
        return;

    gui_draw_hline(win, x, x + w - 1, y, color);
    gui_draw_hline(win, x, x + w - 1, y + h - 1, color);
    gui_draw_vline(win, x, y, y + h - 1, color);
    gui_draw_vline(win, x + w - 1, y, y + h - 1, color);
}

extern "C" void gui_draw_text(
    gui_window_t *win, uint32_t x, uint32_t y, const char *text, uint32_t color) {
    if (!win || !win->pixels || !text)
        return;

    uint32_t stride_pixels = win->stride / 4;

    while (*text) {
        char c = *text++;
        if (c < 32 || c > 127)
            continue;

        const uint8_t *glyph = g_font[c - 32];

        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    uint32_t px = x + col;
                    uint32_t py = y + row;
                    if (px < win->width && py < win->height) {
                        win->pixels[py * stride_pixels + px] = color;
                    }
                }
            }
        }

        x += 8;
    }
}

extern "C" void gui_draw_char(
    gui_window_t *win, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if (!win || !win->pixels)
        return;
    if (c < 32 || c > 127)
        c = ' '; // Replace unprintable with space

    uint32_t stride_pixels = win->stride / 4;
    const uint8_t *glyph = g_font[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t px = x + col;
            uint32_t py = y + row;
            if (px < win->width && py < win->height) {
                uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
                win->pixels[py * stride_pixels + px] = color;
            }
        }
    }
}

extern "C" void gui_draw_char_scaled(
    gui_window_t *win, uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg, uint32_t scale) {
    if (!win || !win->pixels || scale == 0)
        return;
    if (c < 32 || c > 127)
        c = ' '; // Replace unprintable with space

    uint32_t stride_pixels = win->stride / 4;
    const uint8_t *glyph = g_font[c - 32];

    // scale is in half-units: 2=1x(8x8), 3=1.5x(12x12), 4=2x(16x16)
    uint32_t dest_size = 8 * scale / 2;

    for (uint32_t dy = 0; dy < dest_size; dy++) {
        // Map destination row to source row (nearest neighbor)
        uint32_t src_row = dy * 2 / scale;
        if (src_row >= 8)
            src_row = 7;
        uint8_t bits = glyph[src_row];

        for (uint32_t dx = 0; dx < dest_size; dx++) {
            // Map destination col to source col (nearest neighbor)
            uint32_t src_col = dx * 2 / scale;
            if (src_col >= 8)
                src_col = 7;

            uint32_t color = (bits & (0x80 >> src_col)) ? fg : bg;
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            if (px < win->width && py < win->height) {
                win->pixels[py * stride_pixels + px] = color;
            }
        }
    }
}

extern "C" void gui_draw_hline(
    gui_window_t *win, uint32_t x1, uint32_t x2, uint32_t y, uint32_t color) {
    if (!win || !win->pixels)
        return;
    if (y >= win->height)
        return;
    if (x1 > x2) {
        uint32_t tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (x2 >= win->width)
        x2 = win->width - 1;

    uint32_t stride_pixels = win->stride / 4;
    for (uint32_t x = x1; x <= x2; x++) {
        win->pixels[y * stride_pixels + x] = color;
    }
}

extern "C" void gui_draw_vline(
    gui_window_t *win, uint32_t x, uint32_t y1, uint32_t y2, uint32_t color) {
    if (!win || !win->pixels)
        return;
    if (x >= win->width)
        return;
    if (y1 > y2) {
        uint32_t tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    if (y2 >= win->height)
        y2 = win->height - 1;

    uint32_t stride_pixels = win->stride / 4;
    for (uint32_t y = y1; y <= y2; y++) {
        win->pixels[y * stride_pixels + x] = color;
    }
}
