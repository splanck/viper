/**
 * @file gui.cpp
 * @brief ViperOS GUI client library implementation.
 *
 * @details
 * Communicates with displayd via IPC to create windows, manage surfaces,
 * and handle input events.
 */

#include "../include/gui.h"
#include "../../syscall.hpp"
#include "../../servers/displayd/display_protocol.hpp"

using namespace display_protocol;

// =============================================================================
// Internal State
// =============================================================================

static int32_t g_display_channel = -1;  // Channel to displayd
static uint32_t g_request_id = 1;
static bool g_initialized = false;

struct gui_window
{
    uint32_t surface_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t shm_handle;
    uint32_t *pixels;
    char title[64];
    int32_t event_channel;  // Channel for receiving events
};

// 8x8 bitmap font (same as displayd)
static const uint8_t g_font[96][8] = {
    // Space (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! (33)
    {0x18,0x18,0x18,0x18,0x00,0x00,0x18,0x00},
    // " through / (34-47)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 34
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 35
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 36
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 37
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 38
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 39
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 40
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 41
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 42
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 43
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 44
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 45
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 46
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 47
    // 0-9
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 0
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 1
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 2
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 3
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, // 4
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 5
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 6
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00}, // 7
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 8
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 9
    // : through @
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // : 58
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ; 59
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // < 60
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // = 61
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // > 62
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ? 63
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // @ 64
    // A-Z (65-90)
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // A
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // B
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // C
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // D
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00}, // E
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, // F
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00}, // G
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // H
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00}, // I
    {0x3E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, // J
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // K
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // L
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // M
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // N
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // O
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // P
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, // Q
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // R
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // S
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // T
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // U
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // V
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // X
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // Y
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // Z
    // [ through `
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // [ 91
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // \ 92
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ] 93
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ^ 94
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // _ 95
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ` 96
    // a-z (97-122)
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // a
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // b
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00}, // c
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // d
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // e
    {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00}, // f
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // g
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // h
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // i
    {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38}, // j
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // k
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // l
    {0x00,0x00,0x66,0x7F,0x7F,0x6B,0x63,0x00}, // m
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // n
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // o
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // p
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // q
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, // r
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // s
    {0x30,0x30,0x7C,0x30,0x30,0x30,0x1C,0x00}, // t
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // u
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // v
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // w
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // x
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, // y
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // z
    // { through ~ and DEL
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // { 123
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // | 124
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // } 125
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ~ 126
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // DEL 127
};

// =============================================================================
// Helper Functions
// =============================================================================

static bool send_request_recv_reply(const void *req, size_t req_len,
                                     void *reply, size_t reply_len,
                                     uint32_t *out_handles = nullptr,
                                     uint32_t *handle_count = nullptr)
{
    if (g_display_channel < 0) return false;

    // Create reply channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0) return false;

    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);

    // Send request with reply channel handle
    uint32_t send_handles[1] = {static_cast<uint32_t>(recv_ch)};
    int64_t err = sys::channel_send(g_display_channel, req, req_len, send_handles, 1);
    if (err != 0)
    {
        sys::channel_close(send_ch);
        sys::channel_close(recv_ch);
        return false;
    }

    // Wait for reply
    uint32_t recv_handles[4];
    uint32_t recv_handle_count = 4;

    for (uint32_t i = 0; i < 2000; i++)
    {
        recv_handle_count = 4;
        int64_t n = sys::channel_recv(send_ch, reply, reply_len, recv_handles, &recv_handle_count);
        if (n > 0)
        {
            sys::channel_close(send_ch);
            if (out_handles && handle_count)
            {
                for (uint32_t j = 0; j < recv_handle_count && j < *handle_count; j++)
                {
                    out_handles[j] = recv_handles[j];
                }
                *handle_count = recv_handle_count;
            }
            return true;
        }
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        break;
    }

    sys::channel_close(send_ch);
    return false;
}

// =============================================================================
// Initialization
// =============================================================================

extern "C" int gui_init(void)
{
    if (g_initialized) return 0;

    // Connect to displayd via DISPLAY assign
    uint32_t handle = 0xFFFFFFFFu;
    if (sys::assign_get("DISPLAY", &handle) != 0 || handle == 0xFFFFFFFFu)
    {
        return -1;  // displayd not available
    }

    g_display_channel = static_cast<int32_t>(handle);
    g_initialized = true;
    return 0;
}

extern "C" void gui_shutdown(void)
{
    if (!g_initialized) return;

    if (g_display_channel >= 0)
    {
        sys::channel_close(g_display_channel);
        g_display_channel = -1;
    }

    g_initialized = false;
}

extern "C" int gui_get_display_info(gui_display_info_t *info)
{
    if (!g_initialized || !info) return -1;

    GetInfoRequest req;
    req.type = DISP_GET_INFO;
    req.request_id = g_request_id++;

    GetInfoReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply)))
    {
        return -1;
    }

    if (reply.status != 0)
    {
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

extern "C" gui_window_t *gui_create_window(const char *title, uint32_t width, uint32_t height)
{
    if (!g_initialized) return nullptr;

    CreateSurfaceRequest req;
    req.type = DISP_CREATE_SURFACE;
    req.request_id = g_request_id++;
    req.width = width;
    req.height = height;
    req.flags = 0;

    // Copy title
    size_t i = 0;
    if (title)
    {
        while (i < 63 && title[i])
        {
            req.title[i] = title[i];
            i++;
        }
    }
    req.title[i] = '\0';

    CreateSurfaceReply reply;
    uint32_t handles[4];
    uint32_t handle_count = 4;

    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply), handles, &handle_count))
    {
        return nullptr;
    }

    if (reply.status != 0 || handle_count == 0)
    {
        return nullptr;
    }

    // Map shared memory
    auto map_result = sys::shm_map(handles[0]);
    if (map_result.error != 0)
    {
        sys::shm_close(handles[0]);
        return nullptr;
    }

    // Allocate window structure
    gui_window_t *win = new gui_window_t();
    if (!win)
    {
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
    if (title)
    {
        while (i < 63 && title[i])
        {
            win->title[i] = title[i];
            i++;
        }
    }
    win->title[i] = '\0';

    return win;
}

extern "C" void gui_destroy_window(gui_window_t *win)
{
    if (!win) return;

    // Send destroy request
    DestroySurfaceRequest req;
    req.type = DISP_DESTROY_SURFACE;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));

    // Clean up local resources
    if (win->pixels)
    {
        sys::shm_unmap(reinterpret_cast<uint64_t>(win->pixels));
    }
    sys::shm_close(win->shm_handle);

    if (win->event_channel >= 0)
    {
        sys::channel_close(win->event_channel);
    }

    delete win;
}

extern "C" void gui_set_title(gui_window_t *win, const char *title)
{
    if (!win || !title) return;

    SetTitleRequest req;
    req.type = DISP_SET_TITLE;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;

    size_t i = 0;
    while (i < 63 && title[i])
    {
        req.title[i] = title[i];
        win->title[i] = title[i];
        i++;
    }
    req.title[i] = '\0';
    win->title[i] = '\0';

    GenericReply reply;
    send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply));
}

extern "C" const char *gui_get_title(gui_window_t *win)
{
    return win ? win->title : nullptr;
}

extern "C" gui_window_t *gui_create_window_ex(const char *title, uint32_t width, uint32_t height,
                                               uint32_t flags)
{
    if (!g_initialized) return nullptr;

    CreateSurfaceRequest req;
    req.type = DISP_CREATE_SURFACE;
    req.request_id = g_request_id++;
    req.width = width;
    req.height = height;
    req.flags = flags;

    // Copy title
    size_t i = 0;
    if (title)
    {
        while (i < 63 && title[i])
        {
            req.title[i] = title[i];
            i++;
        }
    }
    req.title[i] = '\0';

    CreateSurfaceReply reply;
    uint32_t handles[4];
    uint32_t handle_count = 4;

    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply), handles, &handle_count))
    {
        return nullptr;
    }

    if (reply.status != 0 || handle_count == 0)
    {
        return nullptr;
    }

    // Map shared memory
    auto map_result = sys::shm_map(handles[0]);
    if (map_result.error != 0)
    {
        sys::shm_close(handles[0]);
        return nullptr;
    }

    // Allocate window structure
    gui_window_t *win = new gui_window_t();
    if (!win)
    {
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
    if (title)
    {
        while (i < 63 && title[i])
        {
            win->title[i] = title[i];
            i++;
        }
    }
    win->title[i] = '\0';

    return win;
}

extern "C" int gui_list_windows(gui_window_list_t *list)
{
    if (!g_initialized || !list) return -1;

    ListWindowsRequest req;
    req.type = DISP_LIST_WINDOWS;
    req.request_id = g_request_id++;

    ListWindowsReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply)))
    {
        return -1;
    }

    if (reply.status != 0)
    {
        return reply.status;
    }

    list->count = reply.window_count;
    for (uint32_t i = 0; i < reply.window_count && i < 16; i++)
    {
        list->windows[i].surface_id = reply.windows[i].surface_id;
        list->windows[i].minimized = reply.windows[i].minimized;
        list->windows[i].maximized = reply.windows[i].maximized;
        list->windows[i].focused = reply.windows[i].focused;
        for (int j = 0; j < 64; j++)
        {
            list->windows[i].title[j] = reply.windows[i].title[j];
        }
    }

    return 0;
}

extern "C" int gui_restore_window(uint32_t surface_id)
{
    if (!g_initialized) return -1;

    RestoreWindowRequest req;
    req.type = DISP_RESTORE_WINDOW;
    req.request_id = g_request_id++;
    req.surface_id = surface_id;

    GenericReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply)))
    {
        return -1;
    }

    return reply.status;
}

extern "C" void gui_set_position(gui_window_t *win, int32_t x, int32_t y)
{
    if (!win) return;

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
// Pixel Buffer Access
// =============================================================================

extern "C" uint32_t *gui_get_pixels(gui_window_t *win)
{
    return win ? win->pixels : nullptr;
}

extern "C" uint32_t gui_get_width(gui_window_t *win)
{
    return win ? win->width : 0;
}

extern "C" uint32_t gui_get_height(gui_window_t *win)
{
    return win ? win->height : 0;
}

extern "C" uint32_t gui_get_stride(gui_window_t *win)
{
    return win ? win->stride : 0;
}

// =============================================================================
// Display Update
// =============================================================================

extern "C" void gui_present(gui_window_t *win)
{
    gui_present_region(win, 0, 0, 0, 0);  // 0,0,0,0 = full surface
}

extern "C" void gui_present_region(gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!win) return;

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

extern "C" int gui_poll_event(gui_window_t *win, gui_event_t *event)
{
    if (!win || !event) return -1;

    // Send poll event request to displayd
    PollEventRequest req;
    req.type = DISP_POLL_EVENT;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;

    PollEventReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply)))
    {
        return -1;  // Communication error
    }

    if (reply.has_event == 0)
    {
        event->type = GUI_EVENT_NONE;
        return -1;  // No event available
    }

    // Convert displayd event to libgui event
    switch (reply.event_type)
    {
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

    return 0;  // Event available
}

extern "C" int gui_wait_event(gui_window_t *win, gui_event_t *event)
{
    if (!win || !event) return -1;

    // Poll with yield until an event arrives
    while (true)
    {
        if (gui_poll_event(win, event) == 0)
        {
            return 0;
        }
        sys::yield();
    }
}

// =============================================================================
// Drawing Helpers
// =============================================================================

extern "C" void gui_fill_rect(gui_window_t *win, uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h, uint32_t color)
{
    if (!win || !win->pixels) return;

    uint32_t x2 = x + w;
    uint32_t y2 = y + h;
    if (x2 > win->width) x2 = win->width;
    if (y2 > win->height) y2 = win->height;

    uint32_t stride_pixels = win->stride / 4;
    for (uint32_t py = y; py < y2; py++)
    {
        for (uint32_t px = x; px < x2; px++)
        {
            win->pixels[py * stride_pixels + px] = color;
        }
    }
}

extern "C" void gui_draw_rect(gui_window_t *win, uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h, uint32_t color)
{
    if (!win || !win->pixels || w == 0 || h == 0) return;

    gui_draw_hline(win, x, x + w - 1, y, color);
    gui_draw_hline(win, x, x + w - 1, y + h - 1, color);
    gui_draw_vline(win, x, y, y + h - 1, color);
    gui_draw_vline(win, x + w - 1, y, y + h - 1, color);
}

extern "C" void gui_draw_text(gui_window_t *win, uint32_t x, uint32_t y,
                               const char *text, uint32_t color)
{
    if (!win || !win->pixels || !text) return;

    uint32_t stride_pixels = win->stride / 4;

    while (*text)
    {
        char c = *text++;
        if (c < 32 || c > 127) continue;

        const uint8_t *glyph = g_font[c - 32];

        for (int row = 0; row < 8; row++)
        {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++)
            {
                if (bits & (0x80 >> col))
                {
                    uint32_t px = x + col;
                    uint32_t py = y + row;
                    if (px < win->width && py < win->height)
                    {
                        win->pixels[py * stride_pixels + px] = color;
                    }
                }
            }
        }

        x += 8;
    }
}

extern "C" void gui_draw_hline(gui_window_t *win, uint32_t x1, uint32_t x2,
                                uint32_t y, uint32_t color)
{
    if (!win || !win->pixels) return;
    if (y >= win->height) return;
    if (x1 > x2)
    {
        uint32_t tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (x2 >= win->width) x2 = win->width - 1;

    uint32_t stride_pixels = win->stride / 4;
    for (uint32_t x = x1; x <= x2; x++)
    {
        win->pixels[y * stride_pixels + x] = color;
    }
}

extern "C" void gui_draw_vline(gui_window_t *win, uint32_t x, uint32_t y1,
                                uint32_t y2, uint32_t color)
{
    if (!win || !win->pixels) return;
    if (x >= win->width) return;
    if (y1 > y2)
    {
        uint32_t tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    if (y2 >= win->height) y2 = win->height - 1;

    uint32_t stride_pixels = win->stride / 4;
    for (uint32_t y = y1; y <= y2; y++)
    {
        win->pixels[y * stride_pixels + x] = color;
    }
}
