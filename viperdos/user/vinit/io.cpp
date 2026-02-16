//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file io.cpp
 * @brief Console I/O, string helpers, and paging for vinit.
 *
 * This file implements all console input/output operations for the vinit
 * shell, including:
 * - Console service connection and messaging
 * - Character and string output
 * - Line editing with readline()
 * - Paging for long output (e.g., help text)
 *
 * ## Console Protocol
 *
 * Communication with consoled uses a message-based protocol:
 *
 * | Message        | Direction | Description                  |
 * |----------------|-----------|------------------------------|
 * | CON_CONNECT    | Request   | Establish console connection |
 * | CON_WRITE      | Request   | Write text to console        |
 *
 * All messages include a request_id for matching replies.
 *
 * ## Output Functions
 *
 * | Function      | Description                              |
 * |---------------|------------------------------------------|
 * | print_char()  | Output a single character                |
 * | print_str()   | Output a null-terminated string          |
 * | put_num()     | Output a signed decimal number           |
 * | put_hex()     | Output an unsigned hexadecimal number    |
 *
 * ## Line Editing
 *
 * The readline() function provides basic line editing:
 * - Printable characters are echoed and appended
 * - Backspace deletes the last character
 * - Enter submits the line
 * - Lines are null-terminated in the output buffer
 *
 * ## Paging System
 *
 * For commands with long output (like Help), paging prevents text
 * from scrolling off screen:
 * - paging_enable(): Starts counting lines
 * - paging_check(): Called after each newline, prompts "-- More --"
 * - paging_disable(): Turns off paging
 *
 * @see vinit.hpp for shared declarations
 * @see shell.cpp for command dispatch
 */
//===----------------------------------------------------------------------===//

#include "vinit.hpp"

// =============================================================================
// Console Server Connection
// =============================================================================

// Console protocol constants (from console_protocol.hpp)
static constexpr u32 CON_WRITE = 0x1001;
static constexpr u32 CON_CONNECT = 0x1009;
static constexpr u32 CON_CONNECT_REPLY = 0x2009;
static constexpr u32 CON_INPUT = 0x3001;

struct WriteRequest {
    u32 type;
    u32 request_id;
    u32 length;
    u32 reserved;
};

struct ConnectRequest {
    u32 type;
    u32 request_id;
};

struct ConnectReply {
    u32 type;
    u32 request_id;
    i32 status;
    u32 cols;
    u32 rows;
};

/// Input event from consoled (matches console_protocol.hpp)
struct InputEvent {
    u32 type;     // CON_INPUT
    char ch;      // ASCII character (0 if special key)
    u8 pressed;   // 1 = key down, 0 = key up
    u16 keycode;  // Raw evdev keycode
    u8 modifiers; // Shift=1, Ctrl=2, Alt=4
    u8 _pad[3];
};

// Console service handles (set by init_console())
static i32 g_console_service = -1; // Send endpoint to consoled
static u32 g_request_id = 0;
static bool g_console_ready = false;
static u32 g_console_cols = 80;
static u32 g_console_rows = 25;

// =============================================================================
// Console Mode State
// =============================================================================

static ConsoleMode g_console_mode = ConsoleMode::STANDALONE;
static i32 g_attached_input_ch = -1;  // Channel to receive input from consoled
static i32 g_attached_output_ch = -1; // Channel to send output to consoled

ConsoleMode get_console_mode() {
    return g_console_mode;
}

void init_console_attached(i32 input_ch, i32 output_ch) {
    g_attached_input_ch = input_ch;
    g_attached_output_ch = output_ch;
    g_console_mode = ConsoleMode::CONSOLE_ATTACHED;
    g_console_ready = true; // Mark console as ready

    // Debug: log channel handles
    sys::print("[vinit] init_console_attached: input=");
    char buf[16];
    int bi = 0;
    i32 tmp = input_ch;
    char tmp2[16];
    int ti = 0;
    do {
        tmp2[ti++] = '0' + (tmp % 10);
        tmp /= 10;
    } while (tmp > 0);
    while (ti > 0)
        buf[bi++] = tmp2[--ti];
    buf[bi] = '\0';
    sys::print(buf);
    sys::print(" output=");
    bi = 0;
    tmp = output_ch;
    ti = 0;
    do {
        tmp2[ti++] = '0' + (tmp % 10);
        tmp /= 10;
    } while (tmp > 0);
    while (ti > 0)
        buf[bi++] = tmp2[--ti];
    buf[bi] = '\0';
    sys::print(buf);
    sys::print("\n");
}

// =============================================================================
// Output Buffering
// =============================================================================

// Buffer console output to reduce IPC overhead
static constexpr usize OUTPUT_BUFFER_SIZE = 2048;
static char g_output_buffer[OUTPUT_BUFFER_SIZE];
static usize g_output_len = 0;

bool init_console() {
    // Connect to CONSOLED service - get a send endpoint to consoled
    u32 service_handle = 0xFFFFFFFF;
    if (sys::assign_get("CONSOLED", &service_handle) != 0 || service_handle == 0xFFFFFFFF) {
        // Silent failure - expected when polling before consoled starts
        return false;
    }

    g_console_service = static_cast<i32>(service_handle);

    // Send CON_CONNECT to get console dimensions
    // Input now comes via kernel TTY buffer, not IPC
    ConnectRequest req;
    req.type = CON_CONNECT;
    req.request_id = g_request_id++;

    // Create a reply channel
    auto reply_ch = sys::channel_create();
    if (reply_ch.error != 0) {
        sys::print("[vinit] init_console: reply channel_create failed\n");
        sys::channel_close(g_console_service);
        g_console_service = -1;
        return false;
    }

    i32 reply_send = static_cast<i32>(reply_ch.val0);
    i32 reply_recv = static_cast<i32>(reply_ch.val1);

    // Send CON_CONNECT with reply handle only (no input channel needed)
    u32 handles[1] = {static_cast<u32>(reply_send)};
    i64 err = sys::channel_send(g_console_service, &req, sizeof(req), handles, 1);
    if (err != 0) {
        sys::print("[vinit] init_console: channel_send failed\n");
        sys::channel_close(g_console_service);
        sys::channel_close(reply_recv);
        g_console_service = -1;
        return false;
    }

    // Wait for reply with proper timeout (5 seconds)
    // Use sleep instead of yield to give consoled time to process
    ConnectReply reply;
    u32 recv_handles[4];
    u32 recv_handle_count = 4;
    bool got_reply = false;
    const u32 timeout_ms = 5000;
    const u32 interval_ms = 10;

    for (u32 waited = 0; waited < timeout_ms; waited += interval_ms) {
        recv_handle_count = 4;
        i64 n =
            sys::channel_recv(reply_recv, &reply, sizeof(reply), recv_handles, &recv_handle_count);
        if (n >= static_cast<i64>(sizeof(ConnectReply))) {
            got_reply = true;
            break;
        }
        if (n == VERR_WOULD_BLOCK) {
            sys::sleep(interval_ms);
            continue;
        }
        // Error case
        sys::print("[vinit] init_console: recv error\n");
        break;
    }

    sys::channel_close(reply_recv);

    if (!got_reply) {
        sys::print("[vinit] init_console: timeout waiting for reply\n");
        sys::channel_close(g_console_service);
        g_console_service = -1;
        return false;
    }

    if (reply.type != CON_CONNECT_REPLY) {
        sys::print("[vinit] init_console: wrong reply type\n");
        sys::channel_close(g_console_service);
        g_console_service = -1;
        return false;
    }

    if (reply.status != 0) {
        sys::print("[vinit] init_console: reply status != 0\n");
        sys::channel_close(g_console_service);
        g_console_service = -1;
        return false;
    }

    g_console_cols = reply.cols;
    g_console_rows = reply.rows;
    g_console_ready = true;

    // Disable kernel gcon now that we're connected to consoled
    sys::gcon_set_gui_mode(true);

    return true;
}

static void console_write_direct(const char *s, usize len) {
    if (!g_console_ready || len == 0)
        return;

    // Build write request with text appended
    u8 buf[4096];
    if (len > 4096 - sizeof(WriteRequest))
        len = 4096 - sizeof(WriteRequest);

    WriteRequest *req = reinterpret_cast<WriteRequest *>(buf);
    req->type = CON_WRITE;
    req->request_id = g_request_id++;
    req->length = static_cast<u32>(len);
    req->reserved = 0;

    // Copy text after header
    for (usize i = 0; i < len; i++) {
        buf[sizeof(WriteRequest) + i] = static_cast<u8>(s[i]);
    }

    // Choose channel based on console mode
    i32 channel = (g_console_mode == ConsoleMode::CONSOLE_ATTACHED) ? g_attached_output_ch
                                                                    : g_console_service;

    // Debug: log first few sends
    static u32 send_count = 0;
    if (++send_count <= 5) {
        sys::print("[vinit] CON_WRITE #");
        char buf2[16];
        int bi = 0;
        u32 tmp = send_count;
        char tmp2[16];
        int ti = 0;
        do {
            tmp2[ti++] = '0' + (tmp % 10);
            tmp /= 10;
        } while (tmp > 0);
        while (ti > 0)
            buf2[bi++] = tmp2[--ti];
        buf2[bi] = '\0';
        sys::print(buf2);
        sys::print(" len=");
        bi = 0;
        tmp = static_cast<u32>(len);
        ti = 0;
        do {
            tmp2[ti++] = '0' + (tmp % 10);
            tmp /= 10;
        } while (tmp > 0);
        while (ti > 0)
            buf2[bi++] = tmp2[--ti];
        buf2[bi] = '\0';
        sys::print(buf2);
        sys::print("\n");
    }

    // Send with retry if buffer is full
    usize total_len = sizeof(WriteRequest) + len;
    u32 retry_count = 0;
    constexpr u32 MAX_RETRIES = 500;
    while (true) {
        i64 err = sys::channel_send(channel, buf, total_len, nullptr, 0);
        if (err == 0)
            break;
        if (err == VERR_CHANNEL_CLOSED)
            break; // Receiver gone, don't retry
        retry_count++;
        if (retry_count >= MAX_RETRIES) {
            // Drop message rather than freeze the system
            break;
        }
        sys::yield(); // Give consoled CPU time to drain the channel
    }
}

static void console_flush_buffer() {
    if (g_output_len > 0) {
        console_write_direct(g_output_buffer, g_output_len);
        g_output_len = 0;
    }
}

static void console_write(const char *s, usize len) {
    if (!g_console_ready || len == 0)
        return;

    // Add to buffer, flushing as needed
    for (usize i = 0; i < len; i++) {
        g_output_buffer[g_output_len++] = s[i];

        // Flush when buffer is full (newlines are batched)
        if (g_output_len >= OUTPUT_BUFFER_SIZE - 1) {
            console_flush_buffer();
        }
    }
}

// =============================================================================
// String Helpers
// =============================================================================

usize strlen(const char *s) {
    usize len = 0;
    while (s[len])
        len++;
    return len;
}

bool streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool strstart(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix)
            return false;
        s++;
        prefix++;
    }
    return true;
}

static char to_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

bool strcaseeq(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool strcasestart(const char *s, const char *prefix) {
    while (*prefix) {
        if (to_lower(*s) != to_lower(*prefix))
            return false;
        s++;
        prefix++;
    }
    return true;
}

// =============================================================================
// Paging State
// =============================================================================

static bool g_paging = false;
static bool g_page_quit = false;
static int g_page_line = 0;

// =============================================================================
// Paging Support
// =============================================================================

bool page_wait() {
    // Use reverse video for prompt, then reset to defaults (white on blue)
    sys::print("\x1b[7m-- More (Space=page, Enter=line, Q=quit) --\x1b[0m");

    int c = sys::getchar();

    // Clear the prompt
    sys::print("\r\x1b[K");

    if (c == 'q' || c == 'Q') {
        g_page_quit = true;
        return false;
    } else if (c == ' ') {
        g_page_line = 0;
        return true;
    } else if (c == '\r' || c == '\n') {
        g_page_line = SCREEN_HEIGHT - 1;
        return true;
    } else {
        g_page_line = 0;
        return true;
    }
}

static void paged_print(const char *s) {
    if (!g_paging || g_page_quit) {
        if (!g_page_quit) {
            if (g_console_ready)
                console_write(s, strlen(s));
            else
                sys::print(s);
        }
        return;
    }

    while (*s) {
        if (g_page_quit)
            return;

        if (g_console_ready) {
            char buf[2] = {*s, '\0'};
            console_write(buf, 1);
        } else {
            sys::putchar(*s);
        }

        if (*s == '\n') {
            g_page_line++;
            if (g_page_line >= SCREEN_HEIGHT - 1) {
                if (!page_wait())
                    return;
            }
        }
        s++;
    }
}

void paging_enable() {
    g_paging = true;
    g_page_line = 0;
    g_page_quit = false;
}

void paging_disable() {
    g_paging = false;
    g_page_line = 0;
    g_page_quit = false;
}

// =============================================================================
// Console Output
// =============================================================================

void print_str(const char *s) {
    if (g_paging) {
        paged_print(s);
    } else if (g_console_ready) {
        console_write(s, strlen(s));
    } else {
        sys::print(s);
    }
}

void flush_console() {
    // Flush any buffered output to consoled
    if (g_console_ready) {
        console_flush_buffer();
    }
}

void print_char(char c) {
    if (g_console_ready) {
        char buf[2] = {c, '\0'};
        console_write(buf, 1);
        // Don't flush here — let the buffer accumulate.
        // Callers that need immediate display (readline, prompt)
        // call flush_console() at the right points.
    } else {
        sys::putchar(c);
    }
}

void put_num(i64 n) {
    char buf[32];
    char *p = buf + 31;
    *p = '\0';

    bool neg = false;
    if (n < 0) {
        neg = true;
        n = -n;
    }

    do {
        *--p = '0' + (n % 10);
        n /= 10;
    } while (n > 0);

    if (neg)
        *--p = '-';

    print_str(p);
}

void put_hex(u32 n) {
    print_str("0x");
    char buf[16];
    char *p = buf + 15;
    *p = '\0';

    do {
        int digit = n & 0xF;
        *--p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        n >>= 4;
    } while (n > 0);

    print_str(p);
}

// =============================================================================
// Console Input
// =============================================================================

bool is_console_ready() {
    return g_console_ready;
}

i32 getchar_from_console() {
    if (!g_console_ready)
        return -1;

    if (g_console_mode == ConsoleMode::CONSOLE_ATTACHED) {
        // Read input event from consoled via channel (blocking)
        InputEvent event;
        u32 handles[4];
        u32 handle_count = 4;

        while (true) {
            i64 n = sys::channel_recv(
                g_attached_input_ch, &event, sizeof(event), handles, &handle_count);
            if (n >= static_cast<i64>(sizeof(InputEvent))) {
                if (event.type == CON_INPUT && event.pressed) {
                    // Return the ASCII character (or special key code as negative)
                    if (event.ch != 0) {
                        return static_cast<i32>(static_cast<u8>(event.ch));
                    }
                    // Special keys: return negative keycode
                    // Arrow keys: Up=103, Down=108, Left=105, Right=106
                    return -static_cast<i32>(event.keycode);
                }
            } else if (n == VERR_WOULD_BLOCK) {
                // No data yet - sleep briefly to give other tasks CPU time
                // Using sleep instead of yield because SCHED_RR tasks would
                // otherwise monopolize the CPU while polling for input
                sys::sleep(5);
                continue;
            } else {
                // Channel error
                return -1;
            }
        }
    }

    // Standalone mode: read from kernel TTY buffer (blocking)
    char c;
    i64 result = sys::tty_read(&c, 1);
    if (result == 1) {
        return static_cast<i32>(static_cast<u8>(c));
    }
    return -1; // Error
}

i32 try_getchar_from_console() {
    if (!g_console_ready)
        return -1;

    if (g_console_mode == ConsoleMode::CONSOLE_ATTACHED) {
        // Non-blocking read from consoled input channel
        InputEvent event;
        u32 handles[4];
        u32 handle_count = 4;

        i64 n =
            sys::channel_recv(g_attached_input_ch, &event, sizeof(event), handles, &handle_count);
        if (n >= static_cast<i64>(sizeof(InputEvent))) {
            if (event.type == CON_INPUT && event.pressed) {
                if (event.ch != 0) {
                    return static_cast<i32>(static_cast<u8>(event.ch));
                }
                return -static_cast<i32>(event.keycode);
            }
        }
        return -1; // No input available
    }

    // Standalone mode: non-blocking read from kernel TTY buffer
    if (!sys::tty_has_input())
        return -1;

    char c;
    i64 result = sys::tty_read(&c, 1);
    if (result == 1) {
        return static_cast<i32>(static_cast<u8>(c));
    }
    return -1; // No input available
}

// Memory routines (memcpy, memmove, memset) are provided by viperlibc
