//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/consoled_backend.cpp
// Purpose: libc-to-consoled bridge for stdout/stderr/stdin routing.
// Key invariants: Lazily connects to CONSOLED; routes I/O via IPC.
// Ownership/Lifetime: Library; global client persists for process lifetime.
// Links: user/servers/consoled/console_protocol.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file consoled_backend.cpp
 * @brief Routes stdout/stderr/stdin through consoled for GUI display and input.
 *
 * @details
 * When consoled is available, this backend:
 * - Intercepts writes to stdout (fd 1) and stderr (fd 2) and sends them
 *   to consoled via IPC for GUI display.
 * - Receives keyboard input from consoled for stdin (fd 0).
 *
 * The connection to consoled is established lazily on first I/O.
 *
 * Output and input are decoupled:
 * - Output works as soon as we get the CONSOLED service channel
 * - Input requires a CON_CONNECT handshake (can fail without breaking output)
 */

#include "../../syscall.hpp"
#include "../include/sys/types.h"
#include "../include/stddef.h"
#include "../include/stdint.h"

namespace
{

// Console protocol constants (from console_protocol.hpp)
constexpr uint32_t CON_WRITE = 0x1001;
constexpr uint32_t CON_CONNECT = 0x1009;
constexpr uint32_t CON_CONNECT_REPLY = 0x2009;
constexpr uint32_t CON_INPUT = 0x3001;

struct WriteRequest
{
    uint32_t type;
    uint32_t request_id;
    uint32_t length;
    uint32_t reserved;
};

struct ConnectRequest
{
    uint32_t type;
    uint32_t request_id;
};

struct ConnectReply
{
    uint32_t type;
    uint32_t request_id;
    int32_t status;
    uint32_t cols;
    uint32_t rows;
};

struct InputEvent
{
    uint32_t type;
    char ch;
    uint8_t pressed;
    uint16_t keycode;
    uint8_t modifiers;
    uint8_t _pad[3];
};

// Connection state - decoupled output and input
static int32_t g_consoled_channel = -1;   // Channel for sending output to consoled
static int32_t g_input_channel = -1;      // Channel for receiving input from consoled
static bool g_output_ready = false;       // Can send CON_WRITE
static bool g_input_ready = false;        // Can receive CON_INPUT
static uint32_t g_request_id = 0;

/**
 * @brief Attempt to set up bidirectional input channel.
 *
 * This is called after output is ready. If it fails, output still works.
 */
static void try_setup_input_channel()
{
    if (g_input_ready || g_consoled_channel < 0)
        return;

    // Create input channel pair (we keep recv, send to consoled)
    auto input_ch = sys::channel_create();
    if (input_ch.error != 0)
        return;
    int32_t input_send = static_cast<int32_t>(input_ch.val0);
    int32_t input_recv = static_cast<int32_t>(input_ch.val1);

    // Create reply channel for CON_CONNECT response
    auto reply_ch = sys::channel_create();
    if (reply_ch.error != 0)
    {
        sys::channel_close(input_send);
        sys::channel_close(input_recv);
        return;
    }
    int32_t reply_send = static_cast<int32_t>(reply_ch.val0);
    int32_t reply_recv = static_cast<int32_t>(reply_ch.val1);

    // Send CON_CONNECT with handles [reply_send, input_send]
    ConnectRequest req;
    req.type = CON_CONNECT;
    req.request_id = g_request_id++;

    uint32_t handles[2] = {static_cast<uint32_t>(reply_send), static_cast<uint32_t>(input_send)};
    int64_t send_err = sys::channel_send(g_consoled_channel, &req, sizeof(req), handles, 2);
    if (send_err != 0)
    {
        sys::channel_close(input_recv);
        sys::channel_close(reply_recv);
        return;
    }

    // Wait for CON_CONNECT_REPLY
    // Use longer timeout with sleep() to give consoled time to process
    // consoled may be busy with GUI events and not checking service channel immediately
    ConnectReply reply;
    uint32_t recv_handles[4];
    uint32_t recv_handle_count = 4;
    bool got_reply = false;

    for (uint32_t i = 0; i < 500; i++)
    {
        recv_handle_count = 4;
        int64_t n = sys::channel_recv(reply_recv, &reply, sizeof(reply), recv_handles, &recv_handle_count);
        if (n >= static_cast<int64_t>(sizeof(ConnectReply)))
        {
            got_reply = true;
            break;
        }
        if (n == -300 /* VERR_WOULD_BLOCK */)
        {
            // Sleep 10ms to give consoled time to check its service channel
            sys::sleep(10);
            continue;
        }
        break; // Error
    }

    sys::channel_close(reply_recv);

    if (got_reply && reply.type == CON_CONNECT_REPLY && reply.status == 0)
    {
        g_input_channel = input_recv;
        g_input_ready = true;
    }
    else
    {
        sys::channel_close(input_recv);
    }
}

/**
 * @brief Attempt connection to CONSOLED service.
 *
 * Output is enabled as soon as we get the service channel.
 * Input setup is attempted but optional.
 */
static void try_connect_consoled()
{
    // Already connected for output
    if (g_output_ready)
        return;

    // Get CONSOLED service channel
    uint32_t service_handle = 0xFFFFFFFF;
    int32_t err = sys::assign_get("CONSOLED", &service_handle);

    if (err != 0 || service_handle == 0xFFFFFFFF)
        return;

    // Output is now ready - we can send CON_WRITE!
    g_consoled_channel = static_cast<int32_t>(service_handle);
    g_output_ready = true;

    // Now try to set up input channel (optional, can fail)
    try_setup_input_channel();
}

/**
 * @brief Send text to consoled.
 */
static bool send_to_consoled(const void *buf, size_t count)
{
    if (g_consoled_channel < 0)
        return false;

    // Build write request with text appended
    uint8_t msg[4096];
    if (count > sizeof(msg) - sizeof(WriteRequest))
        count = sizeof(msg) - sizeof(WriteRequest);

    WriteRequest *req = reinterpret_cast<WriteRequest *>(msg);
    req->type = CON_WRITE;
    req->request_id = g_request_id++;
    req->length = static_cast<uint32_t>(count);
    req->reserved = 0;

    // Copy text after header
    const uint8_t *src = reinterpret_cast<const uint8_t *>(buf);
    for (size_t i = 0; i < count; i++)
    {
        msg[sizeof(WriteRequest) + i] = src[i];
    }

    // Send with retry if buffer full - use sys::sleep() like vinit does
    size_t total_len = sizeof(WriteRequest) + count;
    for (int retry = 0; retry < 100; retry++)
    {
        int64_t err = sys::channel_send(g_consoled_channel, msg, total_len, nullptr, 0);
        if (err == 0)
            return true;
        if (err == -300 /* VERR_WOULD_BLOCK */)
        {
            // Buffer full - sleep 1ms to let consoled drain its queue
            sys::sleep(1);
            continue;
        }
        // Channel error - mark disconnected
        if (err == -301 /* VERR_CHANNEL_CLOSED */ || err == -100 /* VERR_INVALID_HANDLE */)
        {
            g_output_ready = false;
            g_input_ready = false;
            g_consoled_channel = -1;
            g_input_channel = -1;
        }
        return false;
    }
    return false; // Gave up after retries
}

/**
 * @brief Try to receive a character from consoled input channel.
 * @return Character code (0-255), or -1 if no input available, or -2 on error.
 */
static int try_recv_input()
{
    if (g_input_channel < 0)
        return -2;

    InputEvent ev;
    uint32_t handles[4];
    uint32_t handle_count = 4;

    int64_t n = sys::channel_recv(g_input_channel, &ev, sizeof(ev), handles, &handle_count);
    if (n >= static_cast<int64_t>(sizeof(InputEvent)))
    {
        if (ev.type == CON_INPUT && ev.pressed)
        {
            // Return ASCII character if available
            if (ev.ch != 0)
                return static_cast<int>(static_cast<uint8_t>(ev.ch));

            // Handle special keys - return escape sequences
            // For now, just return -1 (no printable char)
            // TODO: Could return escape sequence codes for arrow keys etc.
        }
        return -1; // Key event but no printable char
    }
    if (n == -300 /* VERR_WOULD_BLOCK */)
        return -1; // No input available

    // Error - channel closed?
    if (n == -301 /* VERR_CHANNEL_CLOSED */ || n == -100 /* VERR_INVALID_HANDLE */)
    {
        g_input_ready = false;
        g_input_channel = -1;
    }
    return -2;
}

} // namespace

extern "C" {

/**
 * @brief Check if consoled output is available.
 */
int __viper_consoled_is_available(void)
{
    try_connect_consoled();
    return g_output_ready ? 1 : 0;
}

/**
 * @brief Write to consoled if available.
 * @return Number of bytes written, or -1 if consoled not available.
 */
ssize_t __viper_consoled_write(const void *buf, size_t count)
{
    try_connect_consoled();

    if (!g_output_ready)
        return -1;

    if (send_to_consoled(buf, count))
        return static_cast<ssize_t>(count);

    return -1;
}

/**
 * @brief Check if consoled input is available.
 */
int __viper_consoled_input_available(void)
{
    try_connect_consoled();
    return g_input_ready ? 1 : 0;
}

/**
 * @brief Read a character from consoled (blocking).
 * @return Character code (0-255), or -1 on error.
 */
int __viper_consoled_getchar(void)
{
    try_connect_consoled();

    if (!g_input_ready)
        return -1;

    // Blocking wait for input
    while (true)
    {
        int c = try_recv_input();
        if (c >= 0)
            return c;
        if (c == -2)
            return -1; // Error

        // No input yet - yield and retry
        sys::yield();
    }
}

/**
 * @brief Try to read a character from consoled (non-blocking).
 * @return Character code (0-255), or -1 if no input available.
 */
int __viper_consoled_trygetchar(void)
{
    try_connect_consoled();

    if (!g_input_ready)
        return -1;

    int c = try_recv_input();
    return (c >= 0) ? c : -1;
}

} // extern "C"
