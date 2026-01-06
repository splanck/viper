/**
 * @file io.cpp
 * @brief Console I/O and string helpers for vinit.
 */
#include "vinit.hpp"

// =============================================================================
// Console Server Connection
// =============================================================================

// Console protocol constants (from console_protocol.hpp)
static constexpr u32 CON_WRITE = 0x1001;
static constexpr u32 CON_CONNECT = 0x1009;
static constexpr u32 CON_INPUT = 0x3001;
static constexpr u32 CON_CONNECT_REPLY = 0x2009;

struct WriteRequest
{
    u32 type;
    u32 request_id;
    u32 length;
    u32 reserved;
};

struct ConnectRequest
{
    u32 type;
    u32 request_id;
};

struct ConnectReply
{
    u32 type;
    u32 request_id;
    i32 status;
    u32 cols;
    u32 rows;
};

struct InputEvent
{
    u32 type;
    char ch;
    u8 pressed;
    u16 keycode;
    u8 modifiers;
    u8 _pad[3];
};

// Console service handles (set by init_console())
static i32 g_console_service = -1;  // Send endpoint to consoled
static i32 g_input_channel = -1;    // Recv endpoint for keyboard input
static u32 g_request_id = 0;
static bool g_console_ready = false;
static u32 g_console_cols = 80;
static u32 g_console_rows = 25;

bool init_console()
{
    // Connect to CONSOLED service - get a send endpoint to consoled
    u32 service_handle = 0xFFFFFFFF;
    if (sys::assign_get("CONSOLED", &service_handle) != 0 || service_handle == 0xFFFFFFFF)
    {
        sys::print("[vinit] init_console: assign_get failed\n");
        return false;
    }

    g_console_service = static_cast<i32>(service_handle);

    // Create a channel pair for input events
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0)
    {
        sys::print("[vinit] init_console: input channel_create failed\n");
        sys::channel_close(g_console_service);
        g_console_service = -1;
        return false;
    }

    i32 input_send = static_cast<i32>(ch_result.val0);  // Send to consoled
    i32 input_recv = static_cast<i32>(ch_result.val1);  // We keep recv

    // Send CON_CONNECT with the input channel handle
    ConnectRequest req;
    req.type = CON_CONNECT;
    req.request_id = g_request_id++;

    // Create a reply channel
    auto reply_ch = sys::channel_create();
    if (reply_ch.error != 0)
    {
        sys::print("[vinit] init_console: reply channel_create failed\n");
        sys::channel_close(g_console_service);
        sys::channel_close(input_send);
        sys::channel_close(input_recv);
        g_console_service = -1;
        return false;
    }

    i32 reply_send = static_cast<i32>(reply_ch.val0);
    i32 reply_recv = static_cast<i32>(reply_ch.val1);

    // Send CON_CONNECT with handles: [reply_send, input_send]
    u32 handles[2] = {static_cast<u32>(reply_send), static_cast<u32>(input_send)};
    i64 err = sys::channel_send(g_console_service, &req, sizeof(req), handles, 2);
    if (err != 0)
    {
        sys::print("[vinit] init_console: channel_send failed\n");
        sys::channel_close(g_console_service);
        sys::channel_close(input_send);
        sys::channel_close(input_recv);
        sys::channel_close(reply_recv);
        g_console_service = -1;
        return false;
    }

    // Wait for reply
    ConnectReply reply;
    u32 recv_handles[4];
    u32 recv_handle_count = 4;
    bool got_reply = false;

    for (u32 i = 0; i < 2000; i++)
    {
        recv_handle_count = 4;
        i64 n = sys::channel_recv(reply_recv, &reply, sizeof(reply), recv_handles, &recv_handle_count);
        if (n >= static_cast<i64>(sizeof(ConnectReply)))
        {
            got_reply = true;
            break;
        }
        if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
            continue;
        }
        // Error case
        sys::print("[vinit] init_console: recv error\n");
        break;
    }

    sys::channel_close(reply_recv);

    if (!got_reply)
    {
        sys::print("[vinit] init_console: timeout waiting for reply\n");
        sys::channel_close(g_console_service);
        sys::channel_close(input_recv);
        g_console_service = -1;
        return false;
    }

    if (reply.type != CON_CONNECT_REPLY)
    {
        sys::print("[vinit] init_console: wrong reply type\n");
        sys::channel_close(g_console_service);
        sys::channel_close(input_recv);
        g_console_service = -1;
        return false;
    }

    if (reply.status != 0)
    {
        sys::print("[vinit] init_console: reply status != 0\n");
        sys::channel_close(g_console_service);
        sys::channel_close(input_recv);
        g_console_service = -1;
        return false;
    }

    g_input_channel = input_recv;
    g_console_cols = reply.cols;
    g_console_rows = reply.rows;
    g_console_ready = true;

    // Disable kernel gcon now that we're connected to consoled
    sys::gcon_set_gui_mode(true);

    return true;
}

static void console_write(const char *s, usize len)
{
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
    for (usize i = 0; i < len; i++)
    {
        buf[sizeof(WriteRequest) + i] = static_cast<u8>(s[i]);
    }

    // Send with retry if buffer is full - keep trying until success
    usize total_len = sizeof(WriteRequest) + len;
    while (true)
    {
        i64 err = sys::channel_send(g_console_service, buf, total_len, nullptr, 0);
        if (err == 0)
            break;
        // Buffer full - sleep briefly to let consoled catch up
        sys::sleep(1);
    }
}

// =============================================================================
// String Helpers
// =============================================================================

usize strlen(const char *s)
{
    usize len = 0;
    while (s[len])
        len++;
    return len;
}

bool streq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool strstart(const char *s, const char *prefix)
{
    while (*prefix)
    {
        if (*s != *prefix)
            return false;
        s++;
        prefix++;
    }
    return true;
}

static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

bool strcaseeq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (to_lower(*a) != to_lower(*b))
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

bool strcasestart(const char *s, const char *prefix)
{
    while (*prefix)
    {
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

bool page_wait()
{
    // Use shell color (yellow) after reverse video ends
    sys::print("\x1b[7m-- More (Space=page, Enter=line, Q=quit) --\x1b[0m\x1b[33m");

    int c = sys::getchar();

    // Clear the prompt
    sys::print("\r\x1b[K");

    if (c == 'q' || c == 'Q')
    {
        g_page_quit = true;
        return false;
    }
    else if (c == ' ')
    {
        g_page_line = 0;
        return true;
    }
    else if (c == '\r' || c == '\n')
    {
        g_page_line = SCREEN_HEIGHT - 1;
        return true;
    }
    else
    {
        g_page_line = 0;
        return true;
    }
}

static void paged_print(const char *s)
{
    if (!g_paging || g_page_quit)
    {
        if (!g_page_quit)
        {
            if (g_console_ready)
                console_write(s, strlen(s));
            else
                sys::print(s);
        }
        return;
    }

    while (*s)
    {
        if (g_page_quit)
            return;

        if (g_console_ready)
        {
            char buf[2] = {*s, '\0'};
            console_write(buf, 1);
        }
        else
        {
            sys::putchar(*s);
        }

        if (*s == '\n')
        {
            g_page_line++;
            if (g_page_line >= SCREEN_HEIGHT - 1)
            {
                if (!page_wait())
                    return;
            }
        }
        s++;
    }
}

void paging_enable()
{
    g_paging = true;
    g_page_line = 0;
    g_page_quit = false;
}

void paging_disable()
{
    g_paging = false;
    g_page_line = 0;
    g_page_quit = false;
}

// =============================================================================
// Console Output
// =============================================================================

void print_str(const char *s)
{
    if (g_paging)
    {
        paged_print(s);
    }
    else if (g_console_ready)
    {
        console_write(s, strlen(s));
    }
    else
    {
        sys::print(s);
    }
}

void flush_console()
{
    // No-op - consoled drains all messages before presenting
}

void print_char(char c)
{
    if (g_console_ready)
    {
        char buf[2] = {c, '\0'};
        console_write(buf, 1);
    }
    else
    {
        sys::putchar(c);
    }
}

void put_num(i64 n)
{
    char buf[32];
    char *p = buf + 31;
    *p = '\0';

    bool neg = false;
    if (n < 0)
    {
        neg = true;
        n = -n;
    }

    do
    {
        *--p = '0' + (n % 10);
        n /= 10;
    } while (n > 0);

    if (neg)
        *--p = '-';

    print_str(p);
}

void put_hex(u32 n)
{
    print_str("0x");
    char buf[16];
    char *p = buf + 15;
    *p = '\0';

    do
    {
        int digit = n & 0xF;
        *--p = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
        n >>= 4;
    } while (n > 0);

    print_str(p);
}

// =============================================================================
// Console Input (from consoled)
// =============================================================================

bool is_console_ready()
{
    return g_console_ready;
}

i32 getchar_from_console()
{
    if (!g_console_ready || g_input_channel < 0)
        return -1;

    InputEvent ev;
    u32 handles[4];
    u32 handle_count = 4;

    // Blocking wait for input
    while (true)
    {
        handle_count = 4;
        i64 n = sys::channel_recv(g_input_channel, &ev, sizeof(ev), handles, &handle_count);
        if (n >= static_cast<i64>(sizeof(InputEvent)))
        {
            if (ev.type == CON_INPUT && ev.pressed)
            {
                // Return ASCII character if available
                if (ev.ch != 0)
                {
                    return static_cast<i32>(static_cast<u8>(ev.ch));
                }
                // Handle special keys (arrow keys, etc.)
                // Return negative keycodes for special keys
                if (ev.keycode == 103)
                    return -103;  // Up arrow
                if (ev.keycode == 108)
                    return -108;  // Down arrow
                if (ev.keycode == 105)
                    return -105;  // Left arrow
                if (ev.keycode == 106)
                    return -106;  // Right arrow
            }
        }
        else if (n == VERR_WOULD_BLOCK)
        {
            sys::yield();
        }
        else
        {
            return -1;  // Error
        }
    }
}

i32 try_getchar_from_console()
{
    if (!g_console_ready || g_input_channel < 0)
        return -1;

    InputEvent ev;
    u32 handles[4];
    u32 handle_count = 4;

    i64 n = sys::channel_recv(g_input_channel, &ev, sizeof(ev), handles, &handle_count);
    if (n >= static_cast<i64>(sizeof(InputEvent)))
    {
        if (ev.type == CON_INPUT && ev.pressed && ev.ch != 0)
        {
            return static_cast<i32>(static_cast<u8>(ev.ch));
        }
    }
    return -1;  // No input available
}

// Memory routines (memcpy, memmove, memset) are provided by viperlibc
