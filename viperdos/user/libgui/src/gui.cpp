//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file gui.cpp
 * @brief ViperDOS GUI client library implementation.
 *
 * This file implements the client-side GUI library that applications use to
 * create windows, handle input events, and perform drawing operations. The
 * library communicates with displayd (the display server) via IPC channels
 * using the display protocol defined in display_protocol.hpp.
 *
 * ## Architecture Overview
 *
 * ```
 * +------------------+        IPC Channel         +-------------------+
 * |   Application    | <----------------------->  |     displayd      |
 * |------------------|                            |-------------------|
 * | libgui (gui.cpp) |  - CreateSurface request   | Display Server    |
 * | - gui_init()     |  - Present request         | - Window Manager  |
 * | - gui_create_win |  - Event notification      | - Compositor      |
 * | - gui_poll_event |                            | - Input Handler   |
 * | - gui_fill_rect  |                            |                   |
 * +------------------+        Shared Memory       +-------------------+
 *         |          <--------------------------->       |
 *         |              (pixel buffer)                  |
 *         +----------------------------------------------+
 * ```
 *
 * ## Communication Model
 *
 * The library uses a request-reply pattern for most operations:
 * 1. Application calls a gui_* function
 * 2. Library sends a request message to displayd via IPC channel
 * 3. Library waits for a reply message from displayd
 * 4. Library returns the result to the application
 *
 * For events, displayd pushes notifications to a dedicated event channel
 * that the application polls via gui_poll_event().
 *
 * ## Shared Memory for Pixel Buffers
 *
 * Window pixel buffers are allocated by displayd and shared with the
 * application via shared memory (SHM). This allows efficient zero-copy
 * rendering:
 * 1. displayd creates SHM region for window surface
 * 2. displayd sends SHM handle to application in CreateSurface reply
 * 3. Application maps SHM into its address space
 * 4. Application draws directly to the pixel buffer
 * 5. Application calls gui_present() to notify displayd to composite
 *
 * ## Thread Safety
 *
 * This library is NOT thread-safe. All gui_* functions should be called
 * from a single thread. The library uses global state for the display
 * channel connection and request ID counter.
 *
 * ## Usage Example
 *
 * @code
 * // Initialize the GUI library
 * if (gui_init() != 0) {
 *     printf("Failed to connect to display server\n");
 *     return -1;
 * }
 *
 * // Create a window
 * gui_window_t *win = gui_create_window("My App", 400, 300);
 * if (!win) {
 *     printf("Failed to create window\n");
 *     gui_shutdown();
 *     return -1;
 * }
 *
 * // Main loop
 * bool running = true;
 * while (running) {
 *     // Handle events
 *     gui_event_t event;
 *     while (gui_poll_event(win, &event) == 0) {
 *         if (event.type == GUI_EVENT_CLOSE) {
 *             running = false;
 *         }
 *     }
 *
 *     // Draw content
 *     gui_fill_rect(win, 0, 0, 400, 300, 0xFFCCCCCC);
 *     gui_draw_text(win, 10, 10, "Hello, World!", 0xFF000000);
 *
 *     // Present to screen
 *     gui_present(win);
 * }
 *
 * // Cleanup
 * gui_destroy_window(win);
 * gui_shutdown();
 * @endcode
 *
 * @see display_protocol.hpp for the IPC message definitions
 * @see gui.h for the public API declarations
 */
//===----------------------------------------------------------------------===//

#include "../include/gui.h"
#include "../../servers/displayd/display_protocol.hpp"
#include "../../syscall.hpp"

using namespace display_protocol;

//===----------------------------------------------------------------------===//
// Internal State
//===----------------------------------------------------------------------===//

/**
 * @brief IPC channel handle for communication with displayd.
 *
 * This channel is obtained via assign_get("DISPLAY") during gui_init() and
 * is used for all request-reply communication with the display server.
 * A value of -1 indicates the library is not initialized.
 */
static int32_t g_display_channel = -1;

/**
 * @brief Monotonically increasing request ID counter.
 *
 * Each request sent to displayd is assigned a unique request_id to allow
 * matching replies to their corresponding requests. This counter is
 * incremented after each request.
 */
static uint32_t g_request_id = 1;

/**
 * @brief Flag indicating whether gui_init() has been called successfully.
 *
 * Used to prevent double-initialization and to validate that the library
 * is ready for use before other operations.
 */
static bool g_initialized = false;

/**
 * @brief Internal window structure containing surface state.
 *
 * This structure is opaque to applications (they only see gui_window_t*).
 * It contains all the state needed to manage a window, including the
 * surface ID for displayd communication, the shared memory pixel buffer,
 * and the event channel for receiving input notifications.
 *
 * ## Memory Layout
 *
 * The pixel buffer is a contiguous array of 32-bit ARGB pixels:
 * - Pixels are stored in row-major order
 * - Each row has `stride` bytes (may be padded for alignment)
 * - Total buffer size is `stride * height` bytes
 *
 * ## Handle Ownership
 *
 * The window owns:
 * - shm_handle: The SHM handle for the pixel buffer (closed on destroy)
 * - event_channel: The channel for receiving events (closed on destroy)
 * - pixels: The mapped virtual address of the SHM (unmapped on destroy)
 */
struct gui_window {
    uint32_t surface_id;     /**< Surface ID assigned by displayd. */
    uint32_t width;          /**< Window content width in pixels. */
    uint32_t height;         /**< Window content height in pixels. */
    uint32_t stride;         /**< Row stride in bytes (may include padding). */
    uint32_t shm_handle;     /**< SHM handle for the pixel buffer. */
    uint32_t *pixels;        /**< Pointer to mapped pixel buffer. */
    char title[64];          /**< Window title (null-terminated). */
    int32_t event_channel;   /**< Channel for receiving events from displayd. */
};

/**
 * @brief Complete 8x8 bitmap font covering ASCII 32-127.
 *
 * This embedded font provides basic text rendering capability without
 * requiring external font files. Each character is represented as an
 * 8-byte array, where each byte represents one row of pixels.
 *
 * ## Glyph Encoding
 *
 * Each byte represents 8 horizontal pixels, with the MSB (bit 7) being
 * the leftmost pixel:
 * - Bit 7: Column 0 (leftmost)
 * - Bit 6: Column 1
 * - ...
 * - Bit 0: Column 7 (rightmost)
 *
 * A set bit (1) means the pixel should be drawn in the foreground color.
 * A clear bit (0) means the pixel should be drawn in the background color
 * (for gui_draw_char) or left unchanged (for gui_draw_text).
 *
 * ## Character Range
 *
 * The font covers printable ASCII characters:
 * - Index 0 = Space (ASCII 32)
 * - Index 1 = '!' (ASCII 33)
 * - ...
 * - Index 95 = DEL block (ASCII 127)
 *
 * To get the glyph for a character: g_font[c - 32]
 *
 * @note Characters outside the 32-127 range are skipped by drawing functions.
 */
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

//===----------------------------------------------------------------------===//
// Helper Functions
//===----------------------------------------------------------------------===//

/**
 * @brief Sends a request to displayd and waits for the reply.
 *
 * This is the core IPC helper that implements the request-reply pattern
 * for communicating with the display server. It handles channel creation,
 * message sending, reply waiting with timeout, and handle transfer.
 *
 * ## Communication Flow
 *
 * 1. Create a new channel pair (send_ch, recv_ch)
 * 2. Send the request to displayd with send_ch attached
 * 3. displayd processes the request and sends reply to send_ch
 * 4. We receive the reply on recv_ch
 * 5. Clean up channels and return
 *
 * ## Timeout Behavior
 *
 * The function uses a polling loop with sleep() to wait for the reply.
 * Maximum wait time is approximately 5 seconds (500 iterations * 10ms).
 * If no reply arrives within this time, the function returns false.
 *
 * ## Handle Transfer
 *
 * Some replies include kernel handles (e.g., SHM handles for window buffers).
 * If out_handles and handle_count are provided, received handles are copied
 * to the out_handles array.
 *
 * @param req          Pointer to the request message buffer.
 * @param req_len      Size of the request message in bytes.
 * @param reply        Pointer to buffer for the reply message.
 * @param reply_len    Size of the reply buffer in bytes.
 * @param out_handles  Optional array to receive transferred handles.
 * @param handle_count On input: max handles to receive. On output: actual count.
 *
 * @return true if the request was sent and a reply was received successfully.
 * @return false if communication failed (not initialized, channel error, timeout).
 *
 * @note This function is blocking and includes debug output for troubleshooting.
 * @note The send channel is transferred to displayd (we no longer own it).
 */
static bool send_request_recv_reply(const void *req,
                                    size_t req_len,
                                    void *reply,
                                    size_t reply_len,
                                    uint32_t *out_handles = nullptr,
                                    uint32_t *handle_count = nullptr) {
    if (g_display_channel < 0)
        return false;

    // Create reply channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0) {
        return false;
    }

    int32_t send_ch = static_cast<int32_t>(ch_result.val0); // CAP_WRITE - for sending
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1); // CAP_READ - for receiving

    // Send request with the SEND endpoint so displayd can write the reply back
    uint32_t send_handles[1] = {static_cast<uint32_t>(send_ch)};
    int64_t err = sys::channel_send(g_display_channel, req, req_len, send_handles, 1);
    if (err != 0) {
        sys::channel_close(send_ch);
        sys::channel_close(recv_ch);
        return false;
    }

    // Wait for reply on the RECV endpoint with sleep between attempts.
    // Use a reasonable timeout: 500 attempts * 10ms sleep = 5 seconds max
    // CRITICAL: Must use sleep() not yield() - yield() doesn't guarantee
    // any time passes, so the loop can spin through all iterations before
    // displayd gets scheduled to process the message.
    uint32_t recv_handles[4];
    uint32_t recv_handle_count = 4;

    for (uint32_t i = 0; i < 500; i++) {
        recv_handle_count = 4;
        int64_t n = sys::channel_recv(recv_ch, reply, reply_len, recv_handles, &recv_handle_count);
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
            break;
        }
        // Sleep to let displayd get scheduled and process the request
        sys::sleep(10);
    }

    sys::channel_close(recv_ch);
    return false;
}

//===----------------------------------------------------------------------===//
// Initialization
//===----------------------------------------------------------------------===//

/**
 * @brief Initializes the GUI library and connects to the display server.
 *
 * This function must be called before any other gui_* functions. It looks
 * up the display server's IPC channel via the "DISPLAY" assign and stores
 * it for subsequent communication.
 *
 * ## Assign Lookup
 *
 * The display server (displayd) registers itself under the "DISPLAY" assign
 * during system startup. This function uses assign_get() to retrieve the
 * channel handle, enabling communication with displayd.
 *
 * ## Idempotency
 *
 * Calling gui_init() multiple times is safe. Subsequent calls after the
 * first successful initialization return immediately with success.
 *
 * @return 0 on success (library initialized and connected to displayd).
 * @return -1 if displayd is not available (assign lookup failed).
 *
 * @note This function must be called before gui_create_window() or any
 *       other gui_* function that communicates with displayd.
 *
 * @see gui_shutdown() To clean up when done with the GUI library.
 *
 * @code
 * if (gui_init() != 0) {
 *     printf("Display server not available\n");
 *     return -1;
 * }
 * // Now safe to create windows
 * @endcode
 */
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

/**
 * @brief Shuts down the GUI library and releases resources.
 *
 * This function closes the connection to the display server and resets
 * the library to its uninitialized state. After calling gui_shutdown(),
 * gui_init() must be called again before using other gui_* functions.
 *
 * ## Cleanup Behavior
 *
 * - Closes the display channel connection
 * - Resets the initialized flag to false
 * - Does NOT destroy any windows (call gui_destroy_window() first)
 *
 * @note Calling gui_shutdown() when not initialized is safe (no-op).
 *
 * @note Applications should destroy all windows before calling shutdown
 *       to avoid orphaned surfaces on the display server.
 *
 * @see gui_init() To reinitialize the library after shutdown.
 *
 * @code
 * // Clean shutdown sequence
 * gui_destroy_window(win);
 * gui_shutdown();
 * @endcode
 */
extern "C" void gui_shutdown(void) {
    if (!g_initialized)
        return;

    if (g_display_channel >= 0) {
        sys::channel_close(g_display_channel);
        g_display_channel = -1;
    }

    g_initialized = false;
}

/**
 * @brief Retrieves information about the display device.
 *
 * This function queries displayd for the current display configuration,
 * including the screen resolution and pixel format. This is useful for
 * applications that need to adapt their layout to the available screen
 * space.
 *
 * @param info Pointer to a gui_display_info_t structure to receive the
 *             display information. Must not be NULL.
 *
 * @return 0 on success (info structure populated).
 * @return -1 if not initialized, info is NULL, or communication failed.
 * @return Non-zero status code from displayd on other errors.
 *
 * @note The display info may change if the display is reconfigured
 *       (though this is rare in ViperDOS).
 *
 * @code
 * gui_display_info_t info;
 * if (gui_get_display_info(&info) == 0) {
 *     printf("Display: %dx%d\n", info.width, info.height);
 * }
 * @endcode
 */
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

//===----------------------------------------------------------------------===//
// Window Management
//===----------------------------------------------------------------------===//

/**
 * @brief Creates a new window with the specified title and size.
 *
 * This function requests displayd to create a new surface (window) with
 * the given dimensions. The display server allocates a shared memory buffer
 * for the pixel data and returns a handle that the application can map.
 *
 * ## Window Creation Process
 *
 * 1. Send CreateSurface request to displayd with title and dimensions
 * 2. displayd allocates SHM, creates the surface, returns handles
 * 3. Application maps the SHM into its address space
 * 4. Allocate gui_window structure and populate it
 * 5. Subscribe to events via a dedicated event channel
 *
 * ## Event Channel
 *
 * Each window has a dedicated event channel for receiving input events.
 * This avoids flooding the main display channel and allows efficient
 * event polling. The event channel is set up automatically during
 * window creation.
 *
 * ## Pixel Buffer
 *
 * The returned window has a pixel buffer accessible via gui_get_pixels().
 * The buffer is in ARGB format (8 bits per component, alpha in high byte).
 * Applications draw by writing directly to this buffer, then call
 * gui_present() to make changes visible.
 *
 * @param title  The window title displayed in the title bar. Can be NULL
 *               (empty title). Maximum 63 characters.
 * @param width  The width of the window content area in pixels.
 * @param height The height of the window content area in pixels.
 *
 * @return Pointer to the created window structure, or NULL on failure.
 *         The returned pointer is owned by the caller and must be freed
 *         with gui_destroy_window().
 *
 * @note The library must be initialized with gui_init() before calling this.
 *
 * @note The actual window size includes window decorations (title bar,
 *       borders) added by displayd. The width/height specify the content
 *       area dimensions only.
 *
 * @see gui_destroy_window() To close and free the window.
 * @see gui_get_pixels() To access the pixel buffer for drawing.
 * @see gui_present() To make drawn content visible on screen.
 *
 * @code
 * gui_window_t *win = gui_create_window("Calculator", 200, 300);
 * if (!win) {
 *     printf("Failed to create window\n");
 *     return -1;
 * }
 * // Draw and present...
 * gui_destroy_window(win);
 * @endcode
 */
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

/**
 * @brief Destroys a window and releases all associated resources.
 *
 * This function closes the window and frees all resources including:
 * - The shared memory pixel buffer (unmapped and handle closed)
 * - The event channel
 * - The gui_window structure itself
 *
 * The function also sends a destroy request to displayd so the server
 * can remove the surface from its window list and free server-side
 * resources.
 *
 * @param win Pointer to the window to destroy. If NULL, does nothing.
 *
 * @note After calling this function, the win pointer is invalid and
 *       must not be used.
 *
 * @note This function blocks until the destroy request is acknowledged
 *       by displayd.
 *
 * @see gui_create_window() To create a new window.
 *
 * @code
 * gui_destroy_window(win);
 * win = nullptr;  // Good practice to null the pointer
 * @endcode
 */
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

/**
 * @brief Changes the title of an existing window.
 *
 * This function updates both the local copy of the title and sends
 * a request to displayd to update the window's title bar.
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param title The new window title. If NULL, does nothing.
 *              Maximum 63 characters (longer titles are truncated).
 *
 * @note The title change is reflected in the window's title bar
 *       on the next display refresh.
 *
 * @see gui_get_title() To retrieve the current window title.
 *
 * @code
 * // Update title to show current file
 * char title[64];
 * snprintf(title, sizeof(title), "Editor - %s", filename);
 * gui_set_title(win, title);
 * @endcode
 */
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

/**
 * @brief Retrieves the current window title.
 *
 * @param win Pointer to the window. If NULL, returns NULL.
 *
 * @return Pointer to the null-terminated title string (internal buffer),
 *         or NULL if win is NULL.
 *
 * @note The returned pointer is to an internal buffer. Do not free it.
 *       The string is valid until the window is destroyed or the title
 *       is changed with gui_set_title().
 *
 * @see gui_set_title() To change the window title.
 */
extern "C" const char *gui_get_title(gui_window_t *win) {
    return win ? win->title : nullptr;
}

/**
 * @brief Creates a new window with extended flags.
 *
 * This function is similar to gui_create_window() but accepts additional
 * flags to control window behavior. The flags are passed to displayd
 * in the CreateSurface request.
 *
 * ## Flags
 *
 * - **WINDOW_FLAG_NO_TITLEBAR**: Create a borderless window without
 *   window decorations.
 * - Additional flags may be defined in display_protocol.hpp.
 *
 * @param title  The window title. Can be NULL. Maximum 63 characters.
 * @param width  The content area width in pixels.
 * @param height The content area height in pixels.
 * @param flags  Window creation flags (see display_protocol.hpp).
 *
 * @return Pointer to the created window, or NULL on failure.
 *
 * @note This function includes debug output for troubleshooting.
 *
 * @see gui_create_window() For creating standard windows.
 * @see gui_destroy_window() To close the window.
 */
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

/**
 * @brief Lists all windows currently managed by the display server.
 *
 * This function queries displayd for a list of all surfaces (windows),
 * including their IDs, titles, and state flags. This is used by the
 * task manager to display the window list.
 *
 * @param list Pointer to a gui_window_list_t structure to receive the
 *             window list. Must not be NULL.
 *
 * @return 0 on success (list populated).
 * @return -1 if not initialized, list is NULL, or communication failed.
 * @return Non-zero status code from displayd on other errors.
 *
 * @note The list has a maximum capacity of 16 windows.
 *
 * @code
 * gui_window_list_t list;
 * if (gui_list_windows(&list) == 0) {
 *     for (uint32_t i = 0; i < list.count; i++) {
 *         printf("%s %s\n",
 *                list.windows[i].focused ? "*" : " ",
 *                list.windows[i].title);
 *     }
 * }
 * @endcode
 */
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

/**
 * @brief Restores a minimized window and brings it to the foreground.
 *
 * This function sends a request to displayd to restore a window that
 * has been minimized. The window becomes visible again and receives
 * keyboard focus.
 *
 * @param surface_id The surface ID of the window to restore.
 *
 * @return 0 on success.
 * @return -1 if not initialized or communication failed.
 * @return Non-zero status code from displayd on other errors.
 *
 * @note This function is typically used by the task manager to
 *       allow users to switch between windows.
 *
 * @see gui_list_windows() To get the list of windows and their IDs.
 */
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

/**
 * @brief Sets the window position on screen.
 *
 * This function requests displayd to move the window to the specified
 * screen coordinates. The coordinates refer to the top-left corner of
 * the window (including decorations).
 *
 * @param win Pointer to the window. If NULL, does nothing.
 * @param x   The new X coordinate in screen pixels.
 * @param y   The new Y coordinate in screen pixels.
 *
 * @note The window position change takes effect on the next display
 *       refresh cycle.
 *
 * @note Negative coordinates may place part of the window off-screen.
 */
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

//===----------------------------------------------------------------------===//
// Scrollbar Support
//===----------------------------------------------------------------------===//

/**
 * @brief Configures the vertical scrollbar for a window.
 *
 * This function tells displayd to display (or hide) a vertical scrollbar
 * on the window. The scrollbar is rendered by the display server in the
 * window decorations, not by the application.
 *
 * ## Scrollbar Calculation
 *
 * The scrollbar thumb size and position are calculated from:
 * - **content_height**: Total height of the scrollable content
 * - **viewport_height**: Height of the visible area
 * - **scroll_pos**: Current scroll position (0 = top)
 *
 * The scrollbar is automatically enabled when content_height > viewport_height
 * and disabled otherwise.
 *
 * @param win             Pointer to the window. If NULL, does nothing.
 * @param content_height  Total height of scrollable content in pixels.
 * @param viewport_height Height of the visible viewport in pixels.
 * @param scroll_pos      Current scroll position (0 = top).
 *
 * @note The application receives GUI_EVENT_SCROLL events when the user
 *       interacts with the scrollbar.
 *
 * @see gui_poll_event() For handling scroll events.
 *
 * @code
 * // Configure scrollbar for a 2000px tall document in a 300px viewport
 * gui_set_vscrollbar(win, 2000, 300, 0);  // Start at top
 *
 * // Later, when user scrolls...
 * gui_set_vscrollbar(win, 2000, 300, scroll_offset);
 * @endcode
 */
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

/**
 * @brief Configures the horizontal scrollbar for a window.
 *
 * This function tells displayd to display (or hide) a horizontal scrollbar
 * on the window. The scrollbar is rendered by the display server in the
 * window decorations.
 *
 * ## Scrollbar Calculation
 *
 * The scrollbar thumb size and position are calculated from:
 * - **content_width**: Total width of the scrollable content
 * - **viewport_width**: Width of the visible area
 * - **scroll_pos**: Current scroll position (0 = leftmost)
 *
 * The scrollbar is automatically enabled when content_width > viewport_width
 * and disabled otherwise.
 *
 * @param win            Pointer to the window. If NULL, does nothing.
 * @param content_width  Total width of scrollable content in pixels.
 * @param viewport_width Width of the visible viewport in pixels.
 * @param scroll_pos     Current scroll position (0 = left).
 *
 * @note Horizontal scrollbars are less common than vertical ones but
 *       useful for wide content like spreadsheets or code views.
 *
 * @see gui_set_vscrollbar() For vertical scrollbar configuration.
 */
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

//===----------------------------------------------------------------------===//
// Global Menu Bar (Amiga/Mac style)
//===----------------------------------------------------------------------===//

/**
 * @brief Sets the global menu bar for a window (Amiga/Mac style).
 *
 * This function registers menu definitions with displayd. When this window
 * has focus, the menus appear in the global menu bar at the top of the screen
 * (Amiga/Mac style), not in the window itself.
 *
 * When a menu item is selected, the application receives a GUI_EVENT_MENU event
 * with the menu_index, item_index, and action code.
 *
 * @param win        Pointer to the window. If NULL, returns -1.
 * @param menus      Array of menu definitions (can be NULL if menu_count is 0).
 * @param menu_count Number of menus (0 to clear menus, max GUI_MAX_MENUS).
 *
 * @return 0 on success, -1 on failure.
 *
 * @note The gui_menu_def_t structures map directly to display_protocol::MenuDef,
 *       so size/alignment must match.
 *
 * @code
 * gui_menu_def_t menus[2];
 * memset(menus, 0, sizeof(menus));
 *
 * // File menu
 * strcpy(menus[0].title, "File");
 * menus[0].item_count = 3;
 * strcpy(menus[0].items[0].label, "New");
 * strcpy(menus[0].items[0].shortcut, "Ctrl+N");
 * menus[0].items[0].action = 1;
 * menus[0].items[0].enabled = 1;
 *
 * strcpy(menus[0].items[1].label, "-");  // Separator
 *
 * strcpy(menus[0].items[2].label, "Quit");
 * strcpy(menus[0].items[2].shortcut, "Ctrl+Q");
 * menus[0].items[2].action = 2;
 * menus[0].items[2].enabled = 1;
 *
 * gui_set_menu(win, menus, 1);
 * @endcode
 */
extern "C" int gui_set_menu(gui_window_t *win, const gui_menu_def_t *menus, uint8_t menu_count) {
    if (!win)
        return -1;

    SetMenuRequest req;
    req.type = DISP_SET_MENU;
    req.request_id = g_request_id++;
    req.surface_id = win->surface_id;
    req.menu_count = menu_count;
    req._pad[0] = 0;
    req._pad[1] = 0;
    req._pad[2] = 0;

    // Initialize menus array to zero
    for (uint8_t i = 0; i < MAX_MENUS; i++) {
        for (int j = 0; j < 24; j++)
            req.menus[i].title[j] = '\0';
        req.menus[i].item_count = 0;
        req.menus[i]._pad[0] = 0;
        req.menus[i]._pad[1] = 0;
        req.menus[i]._pad[2] = 0;
        for (uint8_t j = 0; j < MAX_MENU_ITEMS; j++) {
            for (int k = 0; k < 32; k++)
                req.menus[i].items[j].label[k] = '\0';
            for (int k = 0; k < 16; k++)
                req.menus[i].items[j].shortcut[k] = '\0';
            req.menus[i].items[j].action = 0;
            req.menus[i].items[j].enabled = 0;
            req.menus[i].items[j].checked = 0;
            req.menus[i].items[j]._pad = 0;
        }
    }

    // Copy menu data (gui_menu_def_t and MenuDef are compatible structures)
    if (menu_count > 0 && menus) {
        uint8_t count = menu_count;
        if (count > MAX_MENUS)
            count = MAX_MENUS;
        for (uint8_t i = 0; i < count; i++) {
            // Copy title
            for (int j = 0; j < 23 && menus[i].title[j]; j++)
                req.menus[i].title[j] = menus[i].title[j];
            req.menus[i].item_count = menus[i].item_count;

            // Copy items
            uint8_t item_count = menus[i].item_count;
            if (item_count > MAX_MENU_ITEMS)
                item_count = MAX_MENU_ITEMS;
            for (uint8_t j = 0; j < item_count; j++) {
                // Copy label
                for (int k = 0; k < 31 && menus[i].items[j].label[k]; k++)
                    req.menus[i].items[j].label[k] = menus[i].items[j].label[k];
                // Copy shortcut
                for (int k = 0; k < 15 && menus[i].items[j].shortcut[k]; k++)
                    req.menus[i].items[j].shortcut[k] = menus[i].items[j].shortcut[k];
                req.menus[i].items[j].action = menus[i].items[j].action;
                req.menus[i].items[j].enabled = menus[i].items[j].enabled;
                req.menus[i].items[j].checked = menus[i].items[j].checked;
            }
        }
    }

    GenericReply reply;
    if (!send_request_recv_reply(&req, sizeof(req), &reply, sizeof(reply))) {
        return -1;
    }

    return (reply.status == 0) ? 0 : -1;
}

//===----------------------------------------------------------------------===//
// Pixel Buffer Access
//===----------------------------------------------------------------------===//

/**
 * @brief Returns a pointer to the window's pixel buffer.
 *
 * The pixel buffer is a contiguous array of 32-bit ARGB pixels that
 * the application can write to directly. Changes become visible after
 * calling gui_present().
 *
 * ## Pixel Format
 *
 * Each pixel is a 32-bit value in ARGB format:
 * - Bits 31-24: Alpha (0xFF = opaque, 0x00 = transparent)
 * - Bits 23-16: Red
 * - Bits 15-8:  Green
 * - Bits 7-0:   Blue
 *
 * ## Buffer Layout
 *
 * Pixels are stored in row-major order. To access pixel (x, y):
 * @code
 * uint32_t stride_pixels = gui_get_stride(win) / 4;
 * pixels[y * stride_pixels + x] = color;
 * @endcode
 *
 * @param win Pointer to the window. If NULL, returns NULL.
 *
 * @return Pointer to the pixel buffer, or NULL if win is NULL.
 *
 * @note The stride may be larger than width*4 due to alignment padding.
 *       Always use gui_get_stride() for row offsets.
 *
 * @warning The returned pointer becomes invalid after the window is
 *          destroyed or resized.
 *
 * @see gui_get_stride() To get the row stride for correct indexing.
 * @see gui_present() To make changes visible on screen.
 */
extern "C" uint32_t *gui_get_pixels(gui_window_t *win) {
    return win ? win->pixels : nullptr;
}

/**
 * @brief Returns the width of the window content area.
 *
 * @param win Pointer to the window. If NULL, returns 0.
 *
 * @return The content width in pixels, or 0 if win is NULL.
 *
 * @note This is the drawable width, not including window decorations.
 *
 * @note The width may change after a resize event.
 *
 * @see gui_get_height() For the content height.
 */
extern "C" uint32_t gui_get_width(gui_window_t *win) {
    return win ? win->width : 0;
}

/**
 * @brief Returns the height of the window content area.
 *
 * @param win Pointer to the window. If NULL, returns 0.
 *
 * @return The content height in pixels, or 0 if win is NULL.
 *
 * @note This is the drawable height, not including window decorations.
 *
 * @note The height may change after a resize event.
 *
 * @see gui_get_width() For the content width.
 */
extern "C" uint32_t gui_get_height(gui_window_t *win) {
    return win ? win->height : 0;
}

/**
 * @brief Returns the row stride of the pixel buffer in bytes.
 *
 * The stride is the number of bytes between the start of one row and
 * the start of the next row in the pixel buffer. This may be larger
 * than width * 4 if the buffer has alignment padding.
 *
 * ## Usage
 *
 * To correctly index into the pixel buffer:
 * @code
 * uint32_t *pixels = gui_get_pixels(win);
 * uint32_t stride_pixels = gui_get_stride(win) / 4;
 *
 * // Set pixel at (x, y)
 * pixels[y * stride_pixels + x] = color;
 * @endcode
 *
 * @param win Pointer to the window. If NULL, returns 0.
 *
 * @return The stride in bytes, or 0 if win is NULL.
 *
 * @note The stride may change after a resize event.
 *
 * @see gui_get_pixels() For the pixel buffer pointer.
 */
extern "C" uint32_t gui_get_stride(gui_window_t *win) {
    return win ? win->stride : 0;
}

//===----------------------------------------------------------------------===//
// Display Update
//===----------------------------------------------------------------------===//

/**
 * @brief Presents the entire window content to the screen.
 *
 * This function notifies displayd that the window's pixel buffer has
 * been updated and the window should be recomposited to the screen.
 * This is a synchronous operation that waits for acknowledgement.
 *
 * ## Compositing Process
 *
 * 1. Application draws to pixel buffer
 * 2. Application calls gui_present()
 * 3. displayd reads the shared memory buffer
 * 4. displayd composites the window onto the screen
 * 5. displayd sends reply to application
 *
 * @param win Pointer to the window. If NULL, does nothing.
 *
 * @note This function blocks until displayd acknowledges the present.
 *       For non-blocking behavior, use gui_present_async().
 *
 * @note Calling gui_present() too frequently may limit frame rate.
 *       Consider using damage regions via gui_present_region() for
 *       partial updates.
 *
 * @see gui_present_async() For fire-and-forget presentation.
 * @see gui_present_region() For partial screen updates.
 *
 * @code
 * // Draw frame
 * gui_fill_rect(win, 0, 0, width, height, bg_color);
 * draw_content(win);
 *
 * // Present to screen
 * gui_present(win);
 * @endcode
 */
extern "C" void gui_present(gui_window_t *win) {
    gui_present_region(win, 0, 0, 0, 0); // 0,0,0,0 = full surface
}

/**
 * @brief Presents the window content without waiting for acknowledgement.
 *
 * This function sends a present request to displayd but does not wait
 * for a reply. This is useful when the application wants to continue
 * processing immediately after initiating the present.
 *
 * ## Trade-offs
 *
 * - **Faster**: Application continues immediately
 * - **No confirmation**: No guarantee the present was processed
 * - **Potential tearing**: Next frame may start before previous is shown
 *
 * @param win Pointer to the window. If NULL, does nothing.
 *
 * @note For games or animations where frame rate is critical, async
 *       present can improve responsiveness.
 *
 * @note There is no way to know when the present completes. If you
 *       need confirmation, use gui_present() instead.
 *
 * @see gui_present() For synchronous presentation with confirmation.
 */
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

/**
 * @brief Presents a specific region of the window content.
 *
 * This function is similar to gui_present() but allows specifying a
 * damage rectangle indicating which portion of the window was updated.
 * The compositor can use this hint to optimize compositing.
 *
 * ## Damage Rectangle
 *
 * The damage rectangle (x, y, w, h) specifies the region that changed:
 * - (0, 0, 0, 0) means the entire surface (same as gui_present())
 * - Specific values indicate only that region needs recompositing
 *
 * ## Performance
 *
 * Specifying damage regions can improve performance when only a small
 * portion of the window changes (e.g., cursor blink, scrolling a single
 * line). The compositor may skip unchanged regions.
 *
 * @param win Pointer to the window. If NULL, does nothing.
 * @param x   X coordinate of the damage region.
 * @param y   Y coordinate of the damage region.
 * @param w   Width of the damage region (0 = full width).
 * @param h   Height of the damage region (0 = full height).
 *
 * @note This is a synchronous operation that waits for acknowledgement.
 *
 * @see gui_present() For full-surface presentation.
 *
 * @code
 * // Only the status bar changed
 * draw_status_bar(win, 0, height - 20, width, 20);
 * gui_present_region(win, 0, height - 20, width, 20);
 * @endcode
 */
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

//===----------------------------------------------------------------------===//
// Events
//===----------------------------------------------------------------------===//

/**
 * @brief Polls for the next event on a window.
 *
 * This function checks if an event is available for the window and
 * returns it without blocking. If no event is available, it returns
 * immediately with -1.
 *
 * ## Event Delivery
 *
 * Events are delivered via a dedicated event channel that was set up
 * during window creation. This provides efficient, non-blocking event
 * polling without flooding the main display channel.
 *
 * ## Event Types
 *
 * - **GUI_EVENT_MOUSE**: Mouse movement, button press/release
 * - **GUI_EVENT_KEY**: Keyboard key press/release
 * - **GUI_EVENT_FOCUS**: Window gained or lost keyboard focus
 * - **GUI_EVENT_CLOSE**: User clicked the close button
 * - **GUI_EVENT_RESIZE**: Window was resized (includes new SHM mapping)
 * - **GUI_EVENT_SCROLL**: User interacted with a scrollbar
 * - **GUI_EVENT_NONE**: No event available
 *
 * ## Resize Event Handling
 *
 * When a resize event is received, the library automatically:
 * 1. Unmaps the old shared memory
 * 2. Maps the new shared memory (handle received with the event)
 * 3. Updates the window's width, height, stride, and pixels pointer
 *
 * Applications should check the new dimensions and redraw their content.
 *
 * @param win   Pointer to the window. If NULL, returns -1.
 * @param event Pointer to receive the event data. If NULL, returns -1.
 *
 * @return 0 if an event was received (event populated).
 * @return -1 if no event is available or parameters are invalid.
 *
 * @note This function does not block. For blocking behavior, use
 *       gui_wait_event() or call gui_poll_event() in a loop.
 *
 * @note The function calls sys::yield() when no event is available to
 *       prevent busy-waiting from starving other processes.
 *
 * @see gui_wait_event() For blocking event retrieval.
 *
 * @code
 * gui_event_t event;
 * while (running) {
 *     while (gui_poll_event(win, &event) == 0) {
 *         switch (event.type) {
 *             case GUI_EVENT_CLOSE:
 *                 running = false;
 *                 break;
 *             case GUI_EVENT_MOUSE:
 *                 handle_mouse(event.mouse.x, event.mouse.y);
 *                 break;
 *             case GUI_EVENT_KEY:
 *                 handle_key(event.key.keycode);
 *                 break;
 *         }
 *     }
 *     // Draw and present...
 * }
 * @endcode
 */
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

            case DISP_EVENT_MENU: {
                auto *menu = reinterpret_cast<MenuEvent *>(ev_buf);
                event->type = GUI_EVENT_MENU;
                event->menu.menu_index = menu->menu_index;
                event->menu.item_index = menu->item_index;
                event->menu.action = menu->action;
                event->menu._pad = 0;
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

        case DISP_EVENT_MENU:
            event->type = GUI_EVENT_MENU;
            event->menu.menu_index = reply.menu.menu_index;
            event->menu.item_index = reply.menu.item_index;
            event->menu.action = reply.menu.action;
            event->menu._pad = 0;
            break;

        default:
            event->type = GUI_EVENT_NONE;
            return -1;
    }

    return 0; // Event available
}

/**
 * @brief Waits for the next event on a window (blocking).
 *
 * This function blocks until an event is available, then returns it.
 * It repeatedly polls for events, yielding the CPU between attempts
 * to avoid busy-waiting.
 *
 * @param win   Pointer to the window. If NULL, returns -1.
 * @param event Pointer to receive the event data. If NULL, returns -1.
 *
 * @return 0 when an event is received (event populated).
 * @return -1 if parameters are invalid.
 *
 * @note This function blocks indefinitely until an event arrives.
 *       There is no timeout mechanism.
 *
 * @note For applications that need to do background work while waiting,
 *       use gui_poll_event() in a loop with explicit yield/sleep.
 *
 * @see gui_poll_event() For non-blocking event retrieval.
 *
 * @code
 * // Simple blocking event loop
 * gui_event_t event;
 * while (gui_wait_event(win, &event) == 0) {
 *     if (event.type == GUI_EVENT_CLOSE) {
 *         break;
 *     }
 *     handle_event(&event);
 * }
 * @endcode
 */
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

//===----------------------------------------------------------------------===//
// Drawing Helpers
//===----------------------------------------------------------------------===//

/**
 * @brief Fills a rectangular area with a solid color.
 *
 * This function fills a rectangle in the window's pixel buffer with
 * the specified color. The rectangle is clipped to the window bounds.
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param x     X coordinate of the top-left corner.
 * @param y     Y coordinate of the top-left corner.
 * @param w     Width of the rectangle in pixels.
 * @param h     Height of the rectangle in pixels.
 * @param color Fill color in ARGB format (0xAARRGGBB).
 *
 * @note Rectangles extending beyond window bounds are clipped.
 *
 * @note Changes are not visible until gui_present() is called.
 *
 * @see gui_draw_rect() To draw a rectangle outline.
 *
 * @code
 * // Clear window to light gray
 * gui_fill_rect(win, 0, 0, gui_get_width(win), gui_get_height(win), 0xFFCCCCCC);
 *
 * // Draw a red square
 * gui_fill_rect(win, 50, 50, 100, 100, 0xFFFF0000);
 * @endcode
 */
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

/**
 * @brief Draws a rectangle outline (1 pixel wide).
 *
 * This function draws the outline of a rectangle without filling the
 * interior. The outline is always 1 pixel wide.
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param x     X coordinate of the top-left corner.
 * @param y     Y coordinate of the top-left corner.
 * @param w     Width of the rectangle in pixels.
 * @param h     Height of the rectangle in pixels.
 * @param color Line color in ARGB format (0xAARRGGBB).
 *
 * @note The outline is drawn inside the specified bounds (not outside).
 *
 * @note For rectangles with w=0 or h=0, nothing is drawn.
 *
 * @see gui_fill_rect() To fill a rectangle.
 * @see gui_draw_hline() To draw individual horizontal lines.
 * @see gui_draw_vline() To draw individual vertical lines.
 *
 * @code
 * // Draw a black border around an area
 * gui_draw_rect(win, 10, 10, 100, 50, 0xFF000000);
 * @endcode
 */
extern "C" void gui_draw_rect(
    gui_window_t *win, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!win || !win->pixels || w == 0 || h == 0)
        return;

    gui_draw_hline(win, x, x + w - 1, y, color);
    gui_draw_hline(win, x, x + w - 1, y + h - 1, color);
    gui_draw_vline(win, x, y, y + h - 1, color);
    gui_draw_vline(win, x + w - 1, y, y + h - 1, color);
}

/**
 * @brief Draws a text string using the built-in 8x8 font.
 *
 * This function renders a null-terminated string at the specified
 * position using the embedded bitmap font. Only foreground pixels are
 * drawn; background pixels are left unchanged (transparent text).
 *
 * ## Font Properties
 *
 * - Character size: 8x8 pixels
 * - Character spacing: 8 pixels (no gap between characters)
 * - Supported range: ASCII 32-127 (printable characters)
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param x     X coordinate of the first character's top-left corner.
 * @param y     Y coordinate of the first character's top-left corner.
 * @param text  Null-terminated string to draw. If NULL, does nothing.
 * @param color Text color in ARGB format (0xAARRGGBB).
 *
 * @note Characters outside ASCII 32-127 are silently skipped.
 *
 * @note There is no automatic word wrap. Text extending beyond the
 *       window edge is clipped.
 *
 * @note For text with a background color, use gui_draw_char() for
 *       each character or clear the area first with gui_fill_rect().
 *
 * @see gui_draw_char() To draw a single character with background.
 * @see gui_draw_char_scaled() For scaled text rendering.
 *
 * @code
 * // Draw black text on a cleared background
 * gui_fill_rect(win, 0, 0, 200, 20, 0xFFFFFFFF);  // White background
 * gui_draw_text(win, 5, 5, "Hello, World!", 0xFF000000);
 * @endcode
 */
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

/**
 * @brief Draws a single character with foreground and background colors.
 *
 * This function renders a single character at the specified position,
 * drawing both foreground (glyph) and background pixels. Unlike
 * gui_draw_text(), this fills the entire 8x8 cell.
 *
 * @param win Pointer to the window. If NULL, does nothing.
 * @param x   X coordinate of the character's top-left corner.
 * @param y   Y coordinate of the character's top-left corner.
 * @param c   The character to draw (ASCII 32-127). Unprintable
 *            characters are replaced with space.
 * @param fg  Foreground (glyph) color in ARGB format.
 * @param bg  Background color in ARGB format.
 *
 * @note Useful for terminal-style displays or highlighted text.
 *
 * @see gui_draw_text() For strings with transparent background.
 * @see gui_draw_char_scaled() For scaled character rendering.
 *
 * @code
 * // Draw selected text (white on blue)
 * gui_draw_char(win, 10, 10, 'A', 0xFFFFFFFF, 0xFF0000AA);
 * @endcode
 */
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

/**
 * @brief Draws a scaled character using nearest-neighbor interpolation.
 *
 * This function renders a character at a specified scale factor, useful
 * for larger displays like digital clocks or title screens. The scaling
 * uses nearest-neighbor interpolation to preserve the pixel-art
 * appearance of the 8x8 font.
 *
 * ## Scale Factor
 *
 * The scale parameter is in half-units:
 * - scale=2: 1x (8x8 pixels, same as gui_draw_char)
 * - scale=3: 1.5x (12x12 pixels)
 * - scale=4: 2x (16x16 pixels)
 * - scale=6: 3x (24x24 pixels)
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param x     X coordinate of the character's top-left corner.
 * @param y     Y coordinate of the character's top-left corner.
 * @param c     The character to draw (ASCII 32-127).
 * @param fg    Foreground (glyph) color in ARGB format.
 * @param bg    Background color in ARGB format.
 * @param scale Scale factor in half-units (2=1x, 4=2x, 6=3x, etc.).
 *
 * @note The destination size is: (8 * scale / 2) x (8 * scale / 2) pixels.
 *
 * @see gui_draw_char() For standard 8x8 character rendering.
 *
 * @code
 * // Draw a large "A" at 2x scale (16x16 pixels)
 * gui_draw_char_scaled(win, 50, 50, 'A', 0xFF000000, 0xFFFFFFFF, 4);
 *
 * // Draw at 3x scale (24x24 pixels)
 * gui_draw_char_scaled(win, 100, 50, 'B', 0xFF000000, 0xFFFFFFFF, 6);
 * @endcode
 */
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

/**
 * @brief Draws a horizontal line (1 pixel wide).
 *
 * This function draws a horizontal line from (x1, y) to (x2, y).
 * The coordinates are automatically sorted, so x1 > x2 is allowed.
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param x1    X coordinate of the first endpoint.
 * @param x2    X coordinate of the second endpoint.
 * @param y     Y coordinate of the line.
 * @param color Line color in ARGB format (0xAARRGGBB).
 *
 * @note Lines are clipped to window bounds.
 *
 * @note For 3D beveled effects, see the libwidget draw_3d_* functions.
 *
 * @see gui_draw_vline() For vertical lines.
 * @see gui_draw_rect() For rectangle outlines.
 *
 * @code
 * // Draw a horizontal separator line
 * gui_draw_hline(win, 10, 190, 50, 0xFF888888);
 * @endcode
 */
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

/**
 * @brief Draws a vertical line (1 pixel wide).
 *
 * This function draws a vertical line from (x, y1) to (x, y2).
 * The coordinates are automatically sorted, so y1 > y2 is allowed.
 *
 * @param win   Pointer to the window. If NULL, does nothing.
 * @param x     X coordinate of the line.
 * @param y1    Y coordinate of the first endpoint.
 * @param y2    Y coordinate of the second endpoint.
 * @param color Line color in ARGB format (0xAARRGGBB).
 *
 * @note Lines are clipped to window bounds.
 *
 * @note For 3D beveled effects, see the libwidget draw_3d_* functions.
 *
 * @see gui_draw_hline() For horizontal lines.
 * @see gui_draw_rect() For rectangle outlines.
 *
 * @code
 * // Draw a vertical separator line
 * gui_draw_vline(win, 100, 10, 90, 0xFF888888);
 * @endcode
 */
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
