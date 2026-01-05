//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/servers/consoled/main.cpp
// Purpose: Console server (consoled) main entry point.
// Key invariants: Uses serial output; registered as "CONSOLED:" service.
// Ownership/Lifetime: Long-running service process.
// Links: user/servers/consoled/console_protocol.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file main.cpp
 * @brief Console server (consoled) main entry point.
 *
 * @details
 * This server provides console output services to user-space processes via IPC.
 * Initial implementation uses serial output; graphics console support can be
 * added later.
 *
 * The server:
 * - Creates a service channel
 * - Registers with the assign system as "CONSOLED:"
 * - Handles console output requests from clients
 */

#include "../../syscall.hpp"
#include "console_protocol.hpp"

using namespace console_protocol;

// Debug output helpers
static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_dec(uint64_t val)
{
    if (val == 0)
    {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0)
    {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// Console state
static uint32_t g_cursor_x = 0;
static uint32_t g_cursor_y = 0;
static uint32_t g_cols = 80;
static uint32_t g_rows = 25;
static uint32_t g_fg_color = 0xFF00AA44; // VIPER_GREEN
static uint32_t g_bg_color = 0xFF1A1208; // VIPER_DARK_BROWN
static bool g_cursor_visible = false;
static int32_t g_service_channel = -1;

static void recv_bootstrap_caps()
{
    constexpr int32_t BOOTSTRAP_RECV = 0;
    uint8_t dummy[1];
    uint32_t handles[4];
    uint32_t handle_count = 4;

    for (uint32_t i = 0; i < 2000; i++)
    {
        handle_count = 4;
        int64_t n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
        if (n >= 0)
        {
            sys::channel_close(BOOTSTRAP_RECV);
            return;
        }
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        return;
    }
}

/**
 * @brief Write text to the console.
 */
static void write_text(const char *text, size_t len)
{
    // For now, just output to serial via sys::print
    // We output character by character to handle non-null-terminated strings
    for (size_t i = 0; i < len; i++)
    {
        char c = text[i];
        if (c == '\0')
            break;

        // Handle newline
        if (c == '\n')
        {
            sys::print("\n");
            g_cursor_x = 0;
            g_cursor_y++;
            if (g_cursor_y >= g_rows)
            {
                g_cursor_y = g_rows - 1;
            }
        }
        else if (c == '\r')
        {
            g_cursor_x = 0;
        }
        else if (c == '\t')
        {
            // Tab to next 8-column boundary
            uint32_t next_tab = (g_cursor_x + 8) & ~7;
            while (g_cursor_x < next_tab && g_cursor_x < g_cols)
            {
                char space[2] = {' ', '\0'};
                sys::print(space);
                g_cursor_x++;
            }
            if (g_cursor_x >= g_cols)
            {
                g_cursor_x = 0;
                g_cursor_y++;
            }
        }
        else if (c == '\b')
        {
            if (g_cursor_x > 0)
            {
                g_cursor_x--;
                sys::print("\b \b");
            }
        }
        else if (c >= 0x20 && c < 0x7F)
        {
            // Printable character
            char buf[2] = {c, '\0'};
            sys::print(buf);
            g_cursor_x++;
            if (g_cursor_x >= g_cols)
            {
                g_cursor_x = 0;
                g_cursor_y++;
                if (g_cursor_y >= g_rows)
                {
                    g_cursor_y = g_rows - 1;
                }
            }
        }
    }
}

/**
 * @brief Handle a client request.
 */
static void handle_request(int32_t client_channel, const uint8_t *data, size_t len)
{
    if (len < 4)
        return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type)
    {
        case CON_WRITE:
        {
            if (len < sizeof(WriteRequest))
                return;
            auto *req = reinterpret_cast<const WriteRequest *>(data);

            // Text follows the header
            const char *text = reinterpret_cast<const char *>(data + sizeof(WriteRequest));
            size_t text_len = len - sizeof(WriteRequest);
            if (text_len > req->length)
                text_len = req->length;

            write_text(text, text_len);

            WriteReply reply;
            reply.type = CON_WRITE_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.written = static_cast<uint32_t>(text_len);
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_CLEAR:
        {
            if (len < sizeof(ClearRequest))
                return;
            auto *req = reinterpret_cast<const ClearRequest *>(data);

            // Clear screen - send ANSI clear sequence
            sys::print("\033[2J\033[H");
            g_cursor_x = 0;
            g_cursor_y = 0;

            ClearReply reply;
            reply.type = CON_CLEAR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SET_CURSOR:
        {
            if (len < sizeof(SetCursorRequest))
                return;
            auto *req = reinterpret_cast<const SetCursorRequest *>(data);

            if (req->x < g_cols)
                g_cursor_x = req->x;
            if (req->y < g_rows)
                g_cursor_y = req->y;

            SetCursorReply reply;
            reply.type = CON_SET_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_GET_CURSOR:
        {
            if (len < sizeof(GetCursorRequest))
                return;
            auto *req = reinterpret_cast<const GetCursorRequest *>(data);

            GetCursorReply reply;
            reply.type = CON_GET_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.x = g_cursor_x;
            reply.y = g_cursor_y;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SET_COLORS:
        {
            if (len < sizeof(SetColorsRequest))
                return;
            auto *req = reinterpret_cast<const SetColorsRequest *>(data);

            g_fg_color = req->foreground;
            g_bg_color = req->background;

            SetColorsReply reply;
            reply.type = CON_SET_COLORS_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_GET_SIZE:
        {
            if (len < sizeof(GetSizeRequest))
                return;
            auto *req = reinterpret_cast<const GetSizeRequest *>(data);

            GetSizeReply reply;
            reply.type = CON_GET_SIZE_REPLY;
            reply.request_id = req->request_id;
            reply.cols = g_cols;
            reply.rows = g_rows;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_SHOW_CURSOR:
        {
            if (len < sizeof(ShowCursorRequest))
                return;
            auto *req = reinterpret_cast<const ShowCursorRequest *>(data);

            g_cursor_visible = true;

            ShowCursorReply reply;
            reply.type = CON_SHOW_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case CON_HIDE_CURSOR:
        {
            if (len < sizeof(HideCursorRequest))
                return;
            auto *req = reinterpret_cast<const HideCursorRequest *>(data);

            g_cursor_visible = false;

            HideCursorReply reply;
            reply.type = CON_HIDE_CURSOR_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.reserved = 0;
            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        default:
            debug_print("[consoled] Unknown message type: ");
            debug_print_dec(msg_type);
            debug_print("\n");
            break;
    }
}

/**
 * @brief Main entry point.
 */
extern "C" void _start()
{
    debug_print("[consoled] Starting console server...\n");

    // Receive bootstrap capabilities
    recv_bootstrap_caps();

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0)
    {
        debug_print("[consoled] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Register with assign system
    if (sys::assign_set("CONSOLED", send_ch) < 0)
    {
        debug_print("[consoled] Failed to register CONSOLED assign\n");
        sys::exit(1);
    }

    debug_print("[consoled] Service registered as CONSOLED\n");
    debug_print("[consoled] Ready.\n");

    // Main event loop
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];

    while (true)
    {
        // Check for client messages
        uint32_t handle_count = 4;
        int64_t n =
            sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);

        if (n > 0)
        {
            // Got a message - first handle is client's reply channel
            if (handle_count > 0)
            {
                int32_t client_ch = static_cast<int32_t>(handles[0]);
                handle_request(client_ch, msg_buf, static_cast<size_t>(n));

                // Close unused handles
                for (uint32_t i = 0; i < handle_count; i++)
                {
                    sys::channel_close(static_cast<int32_t>(handles[i]));
                }
            }
        }
        else if (n == VERR_WOULD_BLOCK)
        {
            // No message, yield
            sys::yield();
        }
    }

    // Unreachable - server runs forever
    sys::exit(0);
}
