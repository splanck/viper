//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libc/src/consoled_backend.cpp
// Purpose: libc-to-consoled bridge for stdout/stderr routing; stdin via kernel TTY.
// Key invariants: Lazily connects to CONSOLED; output via IPC, input via kernel.
// Ownership/Lifetime: Library; global client persists for process lifetime.
// Links: user/servers/consoled/console_protocol.hpp
//
//===----------------------------------------------------------------------===//

/**
 * @file consoled_backend.cpp
 * @brief Routes stdout/stderr through consoled for GUI display.
 *        Stdin comes from kernel TTY buffer (populated by consoled).
 *
 * @details
 * When consoled is available, this backend:
 * - Intercepts writes to stdout (fd 1) and stderr (fd 2) and sends them
 *   to consoled via IPC for GUI display.
 * - Reads keyboard input from kernel TTY buffer via sys::tty_read().
 *   This is much simpler than the old IPC-based approach.
 *
 * The connection to consoled is established lazily on first I/O.
 */

#include "../../syscall.hpp"
#include "../include/stddef.h"
#include "../include/stdint.h"
#include "../include/sys/types.h"
#include <viperdos/syscall_abi.hpp>

namespace {

// Console protocol constants (from console_protocol.hpp)
constexpr uint32_t CON_WRITE = 0x1001;

struct WriteRequest {
    uint32_t type;
    uint32_t request_id;
    uint32_t length;
    uint32_t reserved;
};

// Connection state for output only
static int32_t g_consoled_channel = -1; // Channel for sending output to consoled
static bool g_output_ready = false;     // Can send CON_WRITE
static uint32_t g_request_id = 0;

/**
 * @brief Attempt connection to CONSOLED service for output.
 *
 * @details
 * Always attempts reconnection if not currently connected. This allows
 * recovery from temporary disconnection (e.g., after buffer overflow).
 */
static void try_connect_consoled() {
    // Already connected for output
    if (g_output_ready && g_consoled_channel >= 0)
        return;

    // Get CONSOLED service channel
    uint32_t service_handle = 0xFFFFFFFF;
    int32_t err = sys::assign_get("CONSOLED", &service_handle);

    if (err != 0 || service_handle == 0xFFFFFFFF)
        return;

    // Output is now ready - we can send CON_WRITE!
    g_consoled_channel = static_cast<int32_t>(service_handle);
    g_output_ready = true;
}

/**
 * @brief Send text to consoled.
 *
 * @details
 * Uses retry with timeout on buffer-full (WOULD_BLOCK).
 * Only gives up on actual fatal channel errors (closed/invalid).
 */
static bool send_to_consoled(const void *buf, size_t count) {
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
    for (size_t i = 0; i < count; i++) {
        msg[sizeof(WriteRequest) + i] = src[i];
    }

    // Retry with timeout if buffer full (500ms max wait)
    size_t total_len = sizeof(WriteRequest) + count;
    for (int retry = 0; retry < 500; retry++) {
        int64_t err = sys::channel_send(g_consoled_channel, msg, total_len, nullptr, 0);
        if (err == 0)
            return true;
        if (err == VERR_WOULD_BLOCK) {
            // Buffer full - sleep 1ms to let consoled drain its queue
            sys::sleep(1);
            continue;
        }
        // Fatal channel error - mark disconnected but allow reconnection
        if (err == VERR_CHANNEL_CLOSED || err == VERR_INVALID_HANDLE) {
            g_output_ready = false;
            g_consoled_channel = -1;
        }
        return false;
    }
    return false; // Gave up after retries
}

} // namespace

extern "C" {

/**
 * @brief Check if consoled output is available.
 */
int __viper_consoled_is_available(void) {
    try_connect_consoled();
    return g_output_ready ? 1 : 0;
}

/**
 * @brief Write to consoled if available.
 * @return Number of bytes written, or -1 if consoled not available.
 *
 * @details
 * Sends data in chunks of up to ~4080 bytes (4096 - WriteRequest header).
 * This ensures large writes (e.g., SSH welcome banners) are fully transmitted.
 */
ssize_t __viper_consoled_write(const void *buf, size_t count) {
    try_connect_consoled();

    if (!g_output_ready)
        return -1;

    // Maximum payload per message (4096 buffer - 16 byte WriteRequest header)
    constexpr size_t MAX_PAYLOAD = 4096 - sizeof(WriteRequest);

    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(buf);
    size_t remaining = count;
    size_t total_sent = 0;

    // Send in chunks to handle large writes (e.g., SSH welcome banners)
    while (remaining > 0) {
        size_t chunk = (remaining > MAX_PAYLOAD) ? MAX_PAYLOAD : remaining;

        if (!send_to_consoled(ptr, chunk)) {
            // If we sent some data before failing, return what we sent
            return (total_sent > 0) ? static_cast<ssize_t>(total_sent) : -1;
        }

        ptr += chunk;
        remaining -= chunk;
        total_sent += chunk;
    }

    return static_cast<ssize_t>(total_sent);
}

/**
 * @brief Check if consoled input is available (now uses kernel TTY).
 */
int __viper_consoled_input_available(void) {
    // Check if kernel TTY buffer has data waiting
    return sys::tty_has_input() ? 1 : 0;
}

/**
 * @brief Read a character from kernel TTY buffer (blocking).
 * @return Character code (0-255), or -1 on error.
 */
int __viper_consoled_getchar(void) {
    char c;
    i64 result = sys::tty_read(&c, 1);
    if (result == 1)
        return static_cast<int>(static_cast<unsigned char>(c));
    return -1;
}

/**
 * @brief Try to read a character from kernel TTY (non-blocking).
 * @return Character code (0-255), or -1 if no input available.
 */
int __viper_consoled_trygetchar(void) {
    if (!sys::tty_has_input())
        return -1;

    char c;
    i64 result = sys::tty_read(&c, 1);
    if (result == 1)
        return static_cast<int>(static_cast<unsigned char>(c));
    return -1;
}

} // extern "C"
