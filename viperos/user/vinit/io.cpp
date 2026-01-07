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

// Console service handles (set by init_console())
static i32 g_console_service = -1;  // Send endpoint to consoled
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

    // Send CON_CONNECT to get console dimensions
    // Input now comes via kernel TTY buffer, not IPC
    ConnectRequest req;
    req.type = CON_CONNECT;
    req.request_id = g_request_id++;

    // Create a reply channel
    auto reply_ch = sys::channel_create();
    if (reply_ch.error != 0)
    {
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
    if (err != 0)
    {
        sys::print("[vinit] init_console: channel_send failed\n");
        sys::channel_close(g_console_service);
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
        g_console_service = -1;
        return false;
    }

    if (reply.type != CON_CONNECT_REPLY)
    {
        sys::print("[vinit] init_console: wrong reply type\n");
        sys::channel_close(g_console_service);
        g_console_service = -1;
        return false;
    }

    if (reply.status != 0)
    {
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
// Console Input (from kernel TTY buffer)
// =============================================================================

bool is_console_ready()
{
    return g_console_ready;
}

i32 getchar_from_console()
{
    if (!g_console_ready)
        return -1;

    // Read from kernel TTY buffer (blocking)
    char c;
    i64 result = sys::tty_read(&c, 1);
    if (result == 1)
    {
        return static_cast<i32>(static_cast<u8>(c));
    }
    return -1;  // Error
}

i32 try_getchar_from_console()
{
    if (!g_console_ready)
        return -1;

    // Non-blocking read from kernel TTY buffer
    if (!sys::tty_has_input())
        return -1;

    char c;
    i64 result = sys::tty_read(&c, 1);
    if (result == 1)
    {
        return static_cast<i32>(static_cast<u8>(c));
    }
    return -1;  // No input available
}

// Memory routines (memcpy, memmove, memset) are provided by viperlibc
