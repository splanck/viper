/**
 * @file main.cpp
 * @brief Display server (displayd) main entry point.
 *
 * @details
 * This server provides display and window management services:
 * - Maps the framebuffer into its address space
 * - Manages window surfaces (create, destroy, composite)
 * - Renders a mouse cursor
 * - Routes input events to focused windows
 */

#include "../../syscall.hpp"
#include "display_protocol.hpp"

using namespace display_protocol;

// Debug output
static void debug_print(const char *msg)
{
    sys::print(msg);
}

static void debug_print_hex(uint64_t val)
{
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--)
    {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    sys::print(buf);
}

static void debug_print_dec(int64_t val)
{
    if (val < 0)
    {
        sys::print("-");
        val = -val;
    }
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

// Framebuffer state
static uint32_t *g_fb = nullptr;        // Front buffer (actual framebuffer)
static uint32_t *g_back_buffer = nullptr; // Back buffer for double buffering
static uint32_t *g_draw_target = nullptr; // Current drawing target
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint32_t g_fb_pitch = 0;

// Surface management
static constexpr uint32_t MAX_SURFACES = 32;

// Event queue for each surface
static constexpr size_t EVENT_QUEUE_SIZE = 32;

struct QueuedEvent
{
    uint32_t event_type;  // DISP_EVENT_KEY, DISP_EVENT_MOUSE, etc.
    union
    {
        KeyEvent key;
        MouseEvent mouse;
        FocusEvent focus;
        CloseEvent close;
    };
};

struct EventQueue
{
    QueuedEvent events[EVENT_QUEUE_SIZE];
    size_t head;
    size_t tail;

    void init()
    {
        head = 0;
        tail = 0;
    }

    bool empty() const
    {
        return head == tail;
    }

    bool push(const QueuedEvent &ev)
    {
        size_t next = (tail + 1) % EVENT_QUEUE_SIZE;
        if (next == head)
            return false; // Queue full
        events[tail] = ev;
        tail = next;
        return true;
    }

    bool pop(QueuedEvent *ev)
    {
        if (head == tail)
            return false; // Queue empty
        *ev = events[head];
        head = (head + 1) % EVENT_QUEUE_SIZE;
        return true;
    }
};

struct Surface
{
    uint32_t id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int32_t x;
    int32_t y;
    bool visible;
    bool in_use;
    uint32_t shm_handle;
    uint32_t *pixels;
    char title[64];
    int32_t event_channel;  // Channel for pushing events to client (-1 if not subscribed)
    EventQueue event_queue;
    uint32_t z_order;       // Higher = on top
    uint32_t flags;         // SurfaceFlags

    // Window state
    bool minimized;
    bool maximized;

    // Saved state for restore from maximized
    int32_t saved_x;
    int32_t saved_y;
    uint32_t saved_width;
    uint32_t saved_height;
};

static Surface g_surfaces[MAX_SURFACES];
static uint32_t g_next_surface_id = 1;
static uint32_t g_focused_surface = 0;
static uint32_t g_next_z_order = 1;

// Bring a surface to the front (highest z-order)
static void bring_to_front(Surface *surf)
{
    if (!surf) return;
    surf->z_order = g_next_z_order++;
}

// Cursor state
static int32_t g_cursor_x = 0;
static int32_t g_cursor_y = 0;
static uint32_t g_cursor_saved[16 * 16];
static bool g_cursor_visible = true;

// Window decoration constants
static constexpr uint32_t TITLE_BAR_HEIGHT = 24;
static constexpr uint32_t BORDER_WIDTH = 2;
static constexpr uint32_t CLOSE_BUTTON_SIZE = 16;

// Colors
static constexpr uint32_t COLOR_DESKTOP = 0xFF2D5A88;      // Blue desktop
static constexpr uint32_t COLOR_TITLE_FOCUSED = 0xFF4080C0;
static constexpr uint32_t COLOR_TITLE_UNFOCUSED = 0xFF606060;
static constexpr uint32_t COLOR_BORDER = 0xFF303030;
static constexpr uint32_t COLOR_CLOSE_BTN = 0xFFCC4444;
static constexpr uint32_t COLOR_WHITE = 0xFFFFFFFF;
static constexpr uint32_t COLOR_VIPER_GREEN = 0xFF00AA44;  // Match kernel console border

// Screen border (matches kernel console)
static constexpr uint32_t SCREEN_BORDER_WIDTH = 20;

// 16x16 arrow cursor (1 = white, 2 = black outline)
static const uint8_t g_cursor_data[16 * 16] = {
    2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,2,0,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,2,0,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,2,0,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,2,0,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,2,0,0,0,0,0,0,0,0,
    2,1,1,1,1,1,1,1,2,0,0,0,0,0,0,0,
    2,1,1,1,1,1,2,2,2,2,0,0,0,0,0,0,
    2,1,1,2,1,1,2,0,0,0,0,0,0,0,0,0,
    2,1,2,0,2,1,1,2,0,0,0,0,0,0,0,0,
    2,2,0,0,2,1,1,2,0,0,0,0,0,0,0,0,
    2,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0,
    0,0,0,0,0,2,1,1,2,0,0,0,0,0,0,0,
    0,0,0,0,0,0,2,2,0,0,0,0,0,0,0,0,
};

// Simple font (8x8 bitmap for basic ASCII)
static const uint8_t g_font[96][8] = {
    // Space (32)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // ! (33)
    {0x18,0x18,0x18,0x18,0x00,0x00,0x18,0x00},
    // " through A... (simplified - just use blocks for now)
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
    // Remaining chars (simplified)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // [ 91
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // \ 92
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ] 93
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ^ 94
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // _ 95
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ` 96
    // a-z (97-122) - lowercase
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
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // { 123
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // | 124
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // } 125
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ~ 126
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // DEL 127
};

// Service channel
static int32_t g_service_channel = -1;
static int32_t g_poll_set = -1;  // Poll set for service channel

// Receive bootstrap capabilities
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

// Drawing primitives - use g_draw_target for compositing, g_fb for cursor
static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x < g_fb_width && y < g_fb_height)
    {
        g_draw_target[y * (g_fb_pitch / 4) + x] = color;
    }
}

static inline uint32_t get_pixel(uint32_t x, uint32_t y)
{
    if (x < g_fb_width && y < g_fb_height)
    {
        return g_draw_target[y * (g_fb_pitch / 4) + x];
    }
    return 0;
}

static void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color)
{
    // Clamp to screen
    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + static_cast<int32_t>(w);
    int32_t y2 = y + static_cast<int32_t>(h);
    if (x2 > static_cast<int32_t>(g_fb_width)) x2 = static_cast<int32_t>(g_fb_width);
    if (y2 > static_cast<int32_t>(g_fb_height)) y2 = static_cast<int32_t>(g_fb_height);

    for (int32_t py = y1; py < y2; py++)
    {
        for (int32_t px = x1; px < x2; px++)
        {
            g_draw_target[py * (g_fb_pitch / 4) + px] = color;
        }
    }
}

static void draw_char(int32_t x, int32_t y, char c, uint32_t color)
{
    if (c < 32 || c > 127) return;
    const uint8_t *glyph = g_font[c - 32];

    for (int row = 0; row < 8; row++)
    {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
        {
            if (bits & (0x80 >> col))
            {
                int32_t px = x + col;
                int32_t py = y + row;
                if (px >= 0 && px < static_cast<int32_t>(g_fb_width) &&
                    py >= 0 && py < static_cast<int32_t>(g_fb_height))
                {
                    put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), color);
                }
            }
        }
    }
}

static void draw_text(int32_t x, int32_t y, const char *text, uint32_t color)
{
    while (*text)
    {
        draw_char(x, y, *text, color);
        x += 8;
        text++;
    }
}

// Cursor handling
static void save_cursor_background()
{
    for (int dy = 0; dy < 16; dy++)
    {
        for (int dx = 0; dx < 16; dx++)
        {
            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) &&
                py >= 0 && py < static_cast<int32_t>(g_fb_height))
            {
                g_cursor_saved[dy * 16 + dx] = get_pixel(static_cast<uint32_t>(px),
                                                          static_cast<uint32_t>(py));
            }
        }
    }
}

static void restore_cursor_background()
{
    for (int dy = 0; dy < 16; dy++)
    {
        for (int dx = 0; dx < 16; dx++)
        {
            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) &&
                py >= 0 && py < static_cast<int32_t>(g_fb_height))
            {
                put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py),
                         g_cursor_saved[dy * 16 + dx]);
            }
        }
    }
}

static void draw_cursor()
{
    if (!g_cursor_visible) return;

    for (int dy = 0; dy < 16; dy++)
    {
        for (int dx = 0; dx < 16; dx++)
        {
            uint8_t pixel = g_cursor_data[dy * 16 + dx];
            if (pixel == 0) continue;

            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) &&
                py >= 0 && py < static_cast<int32_t>(g_fb_height))
            {
                uint32_t color = (pixel == 1) ? COLOR_WHITE : 0xFF000000;
                put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), color);
            }
        }
    }
}

// Button colors
static constexpr uint32_t COLOR_MIN_BTN = 0xFF4040C0;  // Blue for minimize
static constexpr uint32_t COLOR_MAX_BTN = 0xFF40C040;  // Green for maximize

// Window decoration drawing
static void draw_window_decorations(Surface *surf)
{
    if (!surf || !surf->in_use || !surf->visible) return;
    if (surf->flags & SURFACE_FLAG_NO_DECORATIONS) return;

    int32_t win_x = surf->x - static_cast<int32_t>(BORDER_WIDTH);
    int32_t win_y = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
    uint32_t win_w = surf->width + BORDER_WIDTH * 2;
    uint32_t win_h = surf->height + TITLE_BAR_HEIGHT + BORDER_WIDTH * 2;

    bool focused = (surf->id == g_focused_surface);

    // Border
    fill_rect(win_x, win_y, win_w, win_h, COLOR_BORDER);

    // Title bar
    uint32_t title_color = focused ? COLOR_TITLE_FOCUSED : COLOR_TITLE_UNFOCUSED;
    fill_rect(win_x + BORDER_WIDTH, win_y + BORDER_WIDTH,
              win_w - BORDER_WIDTH * 2, TITLE_BAR_HEIGHT, title_color);

    // Title text
    draw_text(win_x + BORDER_WIDTH + 8, win_y + BORDER_WIDTH + 8, surf->title, COLOR_WHITE);

    int32_t btn_y = win_y + BORDER_WIDTH + 4;
    int32_t btn_spacing = CLOSE_BUTTON_SIZE + 4;

    // Close button (rightmost)
    int32_t close_x = win_x + static_cast<int32_t>(win_w) - BORDER_WIDTH - CLOSE_BUTTON_SIZE - 4;
    fill_rect(close_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_CLOSE_BTN);
    draw_char(close_x + 4, btn_y + 4, 'X', COLOR_WHITE);

    // Maximize button (second from right)
    int32_t max_x = close_x - btn_spacing;
    fill_rect(max_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_MAX_BTN);
    // Draw box symbol for maximize (or arrows if maximized)
    if (surf->maximized)
    {
        // Draw restore symbol (two overlapping rectangles)
        draw_char(max_x + 4, btn_y + 4, 'R', COLOR_WHITE);
    }
    else
    {
        // Draw maximize symbol (box outline)
        draw_char(max_x + 4, btn_y + 4, 'M', COLOR_WHITE);
    }

    // Minimize button (third from right)
    int32_t min_x = max_x - btn_spacing;
    fill_rect(min_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_MIN_BTN);
    draw_char(min_x + 4, btn_y + 4, '_', COLOR_WHITE);
}

// Copy back buffer to front buffer (fast page flip)
static void flip_buffers()
{
    uint32_t pixels_per_row = g_fb_pitch / 4;
    uint32_t total_pixels = pixels_per_row * g_fb_height;

    // Fast copy using 64-bit transfers where possible
    uint64_t *dst = reinterpret_cast<uint64_t *>(g_fb);
    uint64_t *src = reinterpret_cast<uint64_t *>(g_back_buffer);
    uint32_t count64 = total_pixels / 2;

    for (uint32_t i = 0; i < count64; i++)
    {
        dst[i] = src[i];
    }

    // Handle odd pixel if any
    if (total_pixels & 1)
    {
        g_fb[total_pixels - 1] = g_back_buffer[total_pixels - 1];
    }
}

// Composite all surfaces to framebuffer (double-buffered)
static void composite()
{
    // Draw to back buffer to avoid flicker
    g_draw_target = g_back_buffer;

    // Draw green border around screen edges (matches kernel console theme)
    // Top border
    fill_rect(0, 0, g_fb_width, SCREEN_BORDER_WIDTH, COLOR_VIPER_GREEN);
    // Bottom border
    fill_rect(0, g_fb_height - SCREEN_BORDER_WIDTH, g_fb_width, SCREEN_BORDER_WIDTH, COLOR_VIPER_GREEN);
    // Left border
    fill_rect(0, 0, SCREEN_BORDER_WIDTH, g_fb_height, COLOR_VIPER_GREEN);
    // Right border
    fill_rect(g_fb_width - SCREEN_BORDER_WIDTH, 0, SCREEN_BORDER_WIDTH, g_fb_height, COLOR_VIPER_GREEN);

    // Clear inner desktop area
    fill_rect(SCREEN_BORDER_WIDTH, SCREEN_BORDER_WIDTH,
              g_fb_width - 2 * SCREEN_BORDER_WIDTH,
              g_fb_height - 2 * SCREEN_BORDER_WIDTH,
              COLOR_DESKTOP);

    // Build sorted list of visible surfaces by z-order (lowest first = drawn under)
    Surface *sorted[MAX_SURFACES];
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_SURFACES; i++)
    {
        Surface *surf = &g_surfaces[i];
        if (!surf->in_use || !surf->visible || !surf->pixels) continue;
        if (surf->minimized) continue;  // Don't draw minimized windows
        sorted[count++] = surf;
    }

    // Simple insertion sort by z_order (small N, runs frequently)
    for (uint32_t i = 1; i < count; i++)
    {
        Surface *key = sorted[i];
        int32_t j = static_cast<int32_t>(i) - 1;
        while (j >= 0 && sorted[j]->z_order > key->z_order)
        {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    // Draw surfaces back to front (lower z-order first)
    for (uint32_t i = 0; i < count; i++)
    {
        Surface *surf = sorted[i];

        // Draw decorations first
        draw_window_decorations(surf);

        // Blit surface content to back buffer
        for (uint32_t sy = 0; sy < surf->height; sy++)
        {
            int32_t dst_y = surf->y + static_cast<int32_t>(sy);
            if (dst_y < 0 || dst_y >= static_cast<int32_t>(g_fb_height)) continue;

            for (uint32_t sx = 0; sx < surf->width; sx++)
            {
                int32_t dst_x = surf->x + static_cast<int32_t>(sx);
                if (dst_x < 0 || dst_x >= static_cast<int32_t>(g_fb_width)) continue;

                uint32_t pixel = surf->pixels[sy * (surf->stride / 4) + sx];
                g_back_buffer[dst_y * (g_fb_pitch / 4) + dst_x] = pixel;
            }
        }
    }

    // Copy back buffer to front buffer in one operation
    flip_buffers();

    // Switch to front buffer for cursor operations
    g_draw_target = g_fb;

    // Save background under cursor, then draw cursor (on front buffer)
    save_cursor_background();
    draw_cursor();
}

// Find surface at screen coordinates (checks top-most first by z-order)
static Surface *find_surface_at(int32_t x, int32_t y)
{
    Surface *best = nullptr;
    uint32_t best_z = 0;
    bool found_any = false;

    for (uint32_t i = 0; i < MAX_SURFACES; i++)
    {
        Surface *surf = &g_surfaces[i];
        if (!surf->in_use || !surf->visible || surf->minimized) continue;

        // Check if in window bounds (including decorations)
        int32_t win_x = surf->x - static_cast<int32_t>(BORDER_WIDTH);
        int32_t win_y = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
        int32_t win_x2 = surf->x + static_cast<int32_t>(surf->width + BORDER_WIDTH);
        int32_t win_y2 = surf->y + static_cast<int32_t>(surf->height + BORDER_WIDTH);

        if (x >= win_x && x < win_x2 && y >= win_y && y < win_y2)
        {
            // Pick the one with highest z-order (top-most)
            // Use >= for first match to handle z_order = 0 (SYSTEM surfaces)
            if (!found_any || surf->z_order > best_z)
            {
                best = surf;
                best_z = surf->z_order;
                found_any = true;
            }
        }
    }
    return best;
}

// Find surface by ID
static Surface *find_surface_by_id(uint32_t id)
{
    for (uint32_t i = 0; i < MAX_SURFACES; i++)
    {
        if (g_surfaces[i].in_use && g_surfaces[i].id == id)
        {
            return &g_surfaces[i];
        }
    }
    return nullptr;
}

// Handle surface creation
static void handle_create_surface(int32_t client_channel, const uint8_t *data, size_t len,
                                   const uint32_t * /*handles*/, uint32_t /*handle_count*/)
{
    if (len < sizeof(CreateSurfaceRequest)) return;
    auto *req = reinterpret_cast<const CreateSurfaceRequest *>(data);

    CreateSurfaceReply reply;
    reply.type = DISP_CREATE_SURFACE_REPLY;
    reply.request_id = req->request_id;

    // Find free surface slot
    Surface *surf = nullptr;
    for (uint32_t i = 0; i < MAX_SURFACES; i++)
    {
        if (!g_surfaces[i].in_use)
        {
            surf = &g_surfaces[i];
            break;
        }
    }

    if (!surf)
    {
        reply.status = -1;
        reply.surface_id = 0;
        reply.stride = 0;
        sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Allocate shared memory for surface pixels
    uint32_t stride = req->width * 4;
    uint64_t size = static_cast<uint64_t>(stride) * req->height;

    auto shm_result = sys::shm_create(size);
    if (shm_result.error != 0)
    {
        reply.status = -2;
        reply.surface_id = 0;
        reply.stride = 0;
        sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
        return;
    }

    // Initialize surface
    surf->id = g_next_surface_id++;
    surf->width = req->width;
    surf->height = req->height;
    surf->stride = stride;
    // Cascade windows with better spread (10 positions before repeating)
    uint32_t cascade_idx = g_next_surface_id % 10;
    surf->x = static_cast<int32_t>(SCREEN_BORDER_WIDTH + 40 + cascade_idx * 30);
    surf->y = static_cast<int32_t>(SCREEN_BORDER_WIDTH + TITLE_BAR_HEIGHT + 40 + cascade_idx * 25);
    surf->visible = true;
    surf->in_use = true;
    surf->shm_handle = shm_result.handle;
    surf->pixels = reinterpret_cast<uint32_t *>(shm_result.virt_addr);
    surf->event_channel = -1;  // No event channel until client subscribes
    surf->event_queue.init();
    surf->flags = req->flags;
    // SYSTEM surfaces (like desktop/workbench) stay at z-order 0 (always behind)
    // Other windows get highest z-order (on top)
    if (surf->flags & SURFACE_FLAG_SYSTEM)
    {
        surf->z_order = 0;
    }
    else
    {
        surf->z_order = g_next_z_order++;
    }
    surf->minimized = false;
    surf->maximized = false;

    debug_print("[displayd] Created surface id=");
    debug_print_dec(surf->id);
    debug_print(" flags=");
    debug_print_dec(surf->flags);
    debug_print(" at ");
    debug_print_dec(surf->x);
    debug_print(",");
    debug_print_dec(surf->y);
    debug_print("\n");
    surf->saved_x = surf->x;
    surf->saved_y = surf->y;
    surf->saved_width = surf->width;
    surf->saved_height = surf->height;

    // Copy title
    for (int i = 0; i < 63 && req->title[i]; i++)
    {
        surf->title[i] = req->title[i];
    }
    surf->title[63] = '\0';

    // Clear surface to white
    for (uint32_t y = 0; y < surf->height; y++)
    {
        for (uint32_t x = 0; x < surf->width; x++)
        {
            surf->pixels[y * (stride / 4) + x] = COLOR_WHITE;
        }
    }

    // Set focus to new surface (unless it's a SYSTEM surface like desktop)
    if (!(surf->flags & SURFACE_FLAG_SYSTEM))
    {
        g_focused_surface = surf->id;
    }

    reply.status = 0;
    reply.surface_id = surf->id;
    reply.stride = stride;

    // Transfer SHM handle to client
    uint32_t send_handles[1] = {shm_result.handle};
    sys::channel_send(client_channel, &reply, sizeof(reply), send_handles, 1);

    debug_print("[displayd] Created surface ");
    debug_print_dec(surf->id);
    debug_print(" (");
    debug_print_dec(surf->width);
    debug_print("x");
    debug_print_dec(surf->height);
    debug_print(")\n");

    // Recomposite
    composite();
}

// Handle client request
static void handle_request(int32_t client_channel, const uint8_t *data, size_t len,
                            const uint32_t *handles, uint32_t handle_count)
{
    if (len < 4) return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type)
    {
        case DISP_GET_INFO:
        {
            if (len < sizeof(GetInfoRequest)) return;
            auto *req = reinterpret_cast<const GetInfoRequest *>(data);

            GetInfoReply reply;
            reply.type = DISP_INFO_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.width = g_fb_width;
            reply.height = g_fb_height;
            reply.format = 0x34325258; // XRGB8888

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_CREATE_SURFACE:
            handle_create_surface(client_channel, data, len, handles, handle_count);
            break;

        case DISP_DESTROY_SURFACE:
        {
            if (len < sizeof(DestroySurfaceRequest)) return;
            auto *req = reinterpret_cast<const DestroySurfaceRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++)
            {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id)
                {
                    sys::shm_close(g_surfaces[i].shm_handle);
                    // Close event channel if subscribed
                    if (g_surfaces[i].event_channel >= 0)
                    {
                        sys::channel_close(g_surfaces[i].event_channel);
                        g_surfaces[i].event_channel = -1;
                    }
                    g_surfaces[i].in_use = false;
                    g_surfaces[i].pixels = nullptr;
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_PRESENT:
        {
            if (len < sizeof(PresentRequest)) return;

            // Just recomposite
            composite();

            // Only send reply if client provided a reply channel
            if (client_channel >= 0)
            {
                auto *req = reinterpret_cast<const PresentRequest *>(data);
                GenericReply reply;
                reply.type = DISP_GENERIC_REPLY;
                reply.request_id = req->request_id;
                reply.status = 0;
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            }
            break;
        }

        case DISP_SET_GEOMETRY:
        {
            if (len < sizeof(SetGeometryRequest)) return;
            auto *req = reinterpret_cast<const SetGeometryRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++)
            {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id)
                {
                    g_surfaces[i].x = req->x;
                    g_surfaces[i].y = req->y;
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_SET_VISIBLE:
        {
            if (len < sizeof(SetVisibleRequest)) return;
            auto *req = reinterpret_cast<const SetVisibleRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++)
            {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id)
                {
                    g_surfaces[i].visible = (req->visible != 0);
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_SET_TITLE:
        {
            if (len < sizeof(SetTitleRequest)) return;
            auto *req = reinterpret_cast<const SetTitleRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf)
            {
                // Copy new title (safely)
                for (int i = 0; i < 63 && req->title[i]; i++)
                {
                    surf->title[i] = req->title[i];
                }
                surf->title[63] = '\0';
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite(); // Redraw to update title bar
            break;
        }

        case DISP_SUBSCRIBE_EVENTS:
        {
            if (len < sizeof(SubscribeEventsRequest)) return;
            auto *req = reinterpret_cast<const SubscribeEventsRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            // Find the surface and store the event channel
            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf && handle_count > 0)
            {
                // Close old event channel if any
                if (surf->event_channel >= 0)
                {
                    sys::channel_close(surf->event_channel);
                }
                // Store the new event channel (write endpoint from client)
                surf->event_channel = static_cast<int32_t>(handles[0]);
                reply.status = 0;

                debug_print("[displayd] Subscribed events for surface ");
                debug_print_dec(surf->id);
                debug_print(" channel=");
                debug_print_dec(surf->event_channel);
                debug_print("\n");
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_POLL_EVENT:
        {
            if (len < sizeof(PollEventRequest)) return;
            auto *req = reinterpret_cast<const PollEventRequest *>(data);

            PollEventReply reply;
            reply.type = DISP_POLL_EVENT_REPLY;
            reply.request_id = req->request_id;
            reply.has_event = 0;
            reply.event_type = 0;

            // Find the surface and check for events
            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf)
            {
                QueuedEvent ev;
                if (surf->event_queue.pop(&ev))
                {
                    reply.has_event = 1;
                    reply.event_type = ev.event_type;

                    // Copy event data based on type
                    switch (ev.event_type)
                    {
                        case DISP_EVENT_KEY:
                            reply.key = ev.key;
                            break;
                        case DISP_EVENT_MOUSE:
                            reply.mouse = ev.mouse;
                            break;
                        case DISP_EVENT_FOCUS:
                            reply.focus = ev.focus;
                            break;
                        case DISP_EVENT_CLOSE:
                            reply.close = ev.close;
                            break;
                    }
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_LIST_WINDOWS:
        {
            if (len < sizeof(ListWindowsRequest)) return;
            auto *req = reinterpret_cast<const ListWindowsRequest *>(data);

            ListWindowsReply reply;
            reply.type = DISP_LIST_WINDOWS_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.window_count = 0;

            // Collect all non-system windows
            for (uint32_t i = 0; i < MAX_SURFACES && reply.window_count < 16; i++)
            {
                Surface *surf = &g_surfaces[i];
                if (!surf->in_use) continue;
                if (surf->flags & SURFACE_FLAG_SYSTEM) continue;

                WindowInfo &info = reply.windows[reply.window_count];
                info.surface_id = surf->id;
                info.flags = surf->flags;
                info.minimized = surf->minimized ? 1 : 0;
                info.maximized = surf->maximized ? 1 : 0;
                info.focused = (g_focused_surface == surf->id) ? 1 : 0;

                // Copy title
                for (int j = 0; j < 63 && surf->title[j]; j++)
                {
                    info.title[j] = surf->title[j];
                }
                info.title[63] = '\0';

                reply.window_count++;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_RESTORE_WINDOW:
        {
            if (len < sizeof(RestoreWindowRequest)) return;
            auto *req = reinterpret_cast<const RestoreWindowRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf)
            {
                surf->minimized = false;
                bring_to_front(surf);
                g_focused_surface = surf->id;
                composite();
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        default:
            debug_print("[displayd] Unknown message type: ");
            debug_print_dec(msg_type);
            debug_print("\n");
            break;
    }
}

// Poll for mouse updates and handle dragging/resizing
static uint32_t g_drag_surface_id = 0;
static int32_t g_drag_offset_x = 0;
static int32_t g_drag_offset_y = 0;
static uint8_t g_last_buttons = 0;
static int32_t g_last_mouse_x = 0;
static int32_t g_last_mouse_y = 0;

// Resize state
static uint32_t g_resize_surface_id = 0;
static uint8_t g_resize_edge = 0;  // Bitmask: 1=left, 2=right, 4=top, 8=bottom
static int32_t g_resize_start_x = 0;
static int32_t g_resize_start_y = 0;
static int32_t g_resize_start_width = 0;
static int32_t g_resize_start_height = 0;
static int32_t g_resize_start_surf_x = 0;
static int32_t g_resize_start_surf_y = 0;

static constexpr int32_t RESIZE_BORDER = 6;  // Width of resize handle area
static constexpr uint32_t MIN_WINDOW_WIDTH = 100;
static constexpr uint32_t MIN_WINDOW_HEIGHT = 60;

// Check if point is on a resize edge of a surface, return edge mask
static uint8_t get_resize_edge(Surface *surf, int32_t x, int32_t y)
{
    if (!surf) return 0;
    if (surf->maximized) return 0;  // Can't resize maximized windows

    int32_t win_x1 = surf->x - static_cast<int32_t>(BORDER_WIDTH);
    int32_t win_y1 = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
    int32_t win_x2 = surf->x + static_cast<int32_t>(surf->width + BORDER_WIDTH);
    int32_t win_y2 = surf->y + static_cast<int32_t>(surf->height + BORDER_WIDTH);

    // Check if inside window at all
    if (x < win_x1 || x >= win_x2 || y < win_y1 || y >= win_y2)
        return 0;

    // Check if in title bar (not resizable)
    int32_t title_y2 = surf->y - static_cast<int32_t>(BORDER_WIDTH);
    if (y >= win_y1 && y < title_y2)
        return 0;

    uint8_t edge = 0;

    // Check edges (only if in border area)
    if (x < win_x1 + RESIZE_BORDER) edge |= 1;  // Left
    if (x >= win_x2 - RESIZE_BORDER) edge |= 2; // Right
    if (y >= win_y2 - RESIZE_BORDER) edge |= 8; // Bottom

    return edge;
}

// Queue a mouse event to a surface
static void queue_mouse_event(Surface *surf, uint8_t event_type, int32_t local_x, int32_t local_y,
                               int32_t dx, int32_t dy, uint8_t buttons, uint8_t button)
{
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_MOUSE;
    ev.mouse.type = DISP_EVENT_MOUSE;
    ev.mouse.surface_id = surf->id;
    ev.mouse.x = local_x;
    ev.mouse.y = local_y;
    ev.mouse.dx = dx;
    ev.mouse.dy = dy;
    ev.mouse.buttons = buttons;
    ev.mouse.event_type = event_type;
    ev.mouse.button = button;
    ev.mouse._pad = 0;

    // If client has event channel, send directly (preferred path)
    if (surf->event_channel >= 0)
    {
        sys::channel_send(surf->event_channel, &ev.mouse, sizeof(ev.mouse), nullptr, 0);
    }
    else
    {
        // Fall back to queue for legacy poll-based clients
        if (!surf->event_queue.push(ev))
        {
            // Overflow - event dropped (don't spam logs for mouse moves)
        }
    }
}

// Queue a focus event to a surface
static void queue_focus_event(Surface *surf, bool gained)
{
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_FOCUS;
    ev.focus.type = DISP_EVENT_FOCUS;
    ev.focus.surface_id = surf->id;
    ev.focus.gained = gained ? 1 : 0;
    ev.focus._pad[0] = 0;
    ev.focus._pad[1] = 0;
    ev.focus._pad[2] = 0;

    // If client has event channel, send directly
    if (surf->event_channel >= 0)
    {
        sys::channel_send(surf->event_channel, &ev.focus, sizeof(ev.focus), nullptr, 0);
    }
    else
    {
        if (!surf->event_queue.push(ev))
        {
            // Overflow - focus event dropped
        }
    }
}

// Queue a close event to a surface
static void queue_close_event(Surface *surf)
{
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_CLOSE;
    ev.close.type = DISP_EVENT_CLOSE;
    ev.close.surface_id = surf->id;

    // If client has event channel, send directly
    if (surf->event_channel >= 0)
    {
        sys::channel_send(surf->event_channel, &ev.close, sizeof(ev.close), nullptr, 0);
    }
    else
    {
        if (!surf->event_queue.push(ev))
        {
            // Overflow - close event dropped
        }
    }
}

// Queue a key event to a surface
static void queue_key_event(Surface *surf, uint16_t keycode, uint8_t modifiers, bool pressed)
{
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_KEY;
    ev.key.type = DISP_EVENT_KEY;
    ev.key.surface_id = surf->id;
    ev.key.keycode = keycode;
    ev.key.modifiers = modifiers;
    ev.key.pressed = pressed ? 1 : 0;

    // If client has event channel, send directly
    if (surf->event_channel >= 0)
    {
        sys::channel_send(surf->event_channel, &ev.key, sizeof(ev.key), nullptr, 0);
    }
    else
    {
        if (!surf->event_queue.push(ev))
        {
            // Overflow - key event dropped
        }
    }
}

// Poll kernel for keyboard events and route to focused window
static void poll_keyboard()
{
    // Drain all pending events from the kernel queue (limit to 64 per call)
    for (int i = 0; i < 64; i++)
    {
        // Check if input is available from kernel
        if (sys::input_has_event() == 0) return;

        // Get the event from kernel
        sys::InputEvent ev;
        if (sys::input_get_event(&ev) != 0) return;

        // Route keyboard events to focused surface
        if (ev.type == sys::InputEventType::KeyPress ||
            ev.type == sys::InputEventType::KeyRelease)
        {
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused)
            {
                bool pressed = (ev.type == sys::InputEventType::KeyPress);
                queue_key_event(focused, ev.code, ev.modifiers, pressed);
            }
        }
        // Note: Mouse events from event_queue are discarded here since
        // poll_mouse() uses get_mouse_state() directly for mouse position
    }
}

static void poll_mouse()
{
    sys::MouseState state;
    if (sys::get_mouse_state(&state) != 0) return;

    bool cursor_moved = (state.x != g_last_mouse_x || state.y != g_last_mouse_y);

    // Update cursor position
    if (cursor_moved)
    {
        restore_cursor_background();
        g_cursor_x = state.x;
        g_cursor_y = state.y;

        // Handle resizing
        if (g_resize_surface_id != 0)
        {
            Surface *resize_surf = find_surface_by_id(g_resize_surface_id);
            if (resize_surf)
            {
                int32_t dx = g_cursor_x - g_resize_start_x;
                int32_t dy = g_cursor_y - g_resize_start_y;

                // Calculate new size based on which edges are being dragged
                int32_t new_width = g_resize_start_width;
                int32_t new_height = g_resize_start_height;
                int32_t new_x = g_resize_start_surf_x;
                int32_t new_y = g_resize_start_surf_y;

                if (g_resize_edge & 2) // Right
                {
                    new_width = g_resize_start_width + dx;
                }
                if (g_resize_edge & 1) // Left
                {
                    new_width = g_resize_start_width - dx;
                    new_x = g_resize_start_surf_x + dx;
                }
                if (g_resize_edge & 8) // Bottom
                {
                    new_height = g_resize_start_height + dy;
                }

                // Clamp to minimum size
                if (new_width < static_cast<int32_t>(MIN_WINDOW_WIDTH))
                {
                    if (g_resize_edge & 1) // Left edge: adjust x
                        new_x = g_resize_start_surf_x + g_resize_start_width - MIN_WINDOW_WIDTH;
                    new_width = MIN_WINDOW_WIDTH;
                }
                if (new_height < static_cast<int32_t>(MIN_WINDOW_HEIGHT))
                {
                    new_height = MIN_WINDOW_HEIGHT;
                }

                // Note: Actual resize would require reallocating shared memory
                // For now, just update the window frame dimensions for visual feedback
                // The client won't see the actual content resize
                resize_surf->x = new_x;
                resize_surf->y = new_y;
                // Don't update width/height without realloc - just visual resize of frame
            }
            composite();
        }
        // Handle dragging
        else if (g_drag_surface_id != 0)
        {
            Surface *drag_surf = find_surface_by_id(g_drag_surface_id);
            if (drag_surf)
            {
                drag_surf->x = g_cursor_x - g_drag_offset_x;
                drag_surf->y = g_cursor_y - g_drag_offset_y + static_cast<int32_t>(TITLE_BAR_HEIGHT);
            }
            composite();
        }
        else
        {
            // Queue mouse move event to focused surface if in client area
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused)
            {
                int32_t local_x = g_cursor_x - focused->x;
                int32_t local_y = g_cursor_y - focused->y;

                // Only send move events within client area
                if (local_x >= 0 && local_x < static_cast<int32_t>(focused->width) &&
                    local_y >= 0 && local_y < static_cast<int32_t>(focused->height))
                {
                    int32_t dx = g_cursor_x - g_last_mouse_x;
                    int32_t dy = g_cursor_y - g_last_mouse_y;
                    queue_mouse_event(focused, 0, local_x, local_y, dx, dy, state.buttons, 0);
                }
            }

            save_cursor_background();
            draw_cursor();
        }

        g_last_mouse_x = state.x;
        g_last_mouse_y = state.y;
    }

    // Handle button changes
    if (state.buttons != g_last_buttons)
    {
        uint8_t pressed = state.buttons & ~g_last_buttons;
        uint8_t released = g_last_buttons & ~state.buttons;

        Surface *surf = find_surface_at(g_cursor_x, g_cursor_y);

        if (pressed)
        {
            if (surf)
            {
                // Handle focus change and bring to front
                // Don't change focus to SYSTEM surfaces (like desktop/workbench)
                // They should never receive focus or come to front
                if (surf->id != g_focused_surface && !(surf->flags & SURFACE_FLAG_SYSTEM))
                {
                    Surface *old_focused = find_surface_by_id(g_focused_surface);
                    if (old_focused)
                    {
                        queue_focus_event(old_focused, false);
                    }
                    g_focused_surface = surf->id;
                    queue_focus_event(surf, true);
                    bring_to_front(surf);
                }

                // Check for resize edges first
                uint8_t edge = get_resize_edge(surf, g_cursor_x, g_cursor_y);
                if (edge != 0)
                {
                    // Start resizing
                    g_resize_surface_id = surf->id;
                    g_resize_edge = edge;
                    g_resize_start_x = g_cursor_x;
                    g_resize_start_y = g_cursor_y;
                    g_resize_start_width = static_cast<int32_t>(surf->width);
                    g_resize_start_height = static_cast<int32_t>(surf->height);
                    g_resize_start_surf_x = surf->x;
                    g_resize_start_surf_y = surf->y;
                }
                // Check if clicked on title bar (for dragging/close)
                else
                {
                    int32_t title_y1 = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
                    int32_t title_y2 = surf->y - static_cast<int32_t>(BORDER_WIDTH);

                    if (g_cursor_y >= title_y1 && g_cursor_y < title_y2)
                    {
                        // Check for window control buttons
                        int32_t win_x2 = surf->x + static_cast<int32_t>(surf->width + BORDER_WIDTH);
                        int32_t btn_spacing = static_cast<int32_t>(CLOSE_BUTTON_SIZE + 4);
                        int32_t close_x = win_x2 - static_cast<int32_t>(CLOSE_BUTTON_SIZE + 4);
                        int32_t max_x = close_x - btn_spacing;
                        int32_t min_x = max_x - btn_spacing;
                        int32_t btn_size = static_cast<int32_t>(CLOSE_BUTTON_SIZE);

                        if (g_cursor_x >= close_x && g_cursor_x < close_x + btn_size)
                        {
                            // Close button clicked - queue close event
                            queue_close_event(surf);
                        }
                        else if (g_cursor_x >= max_x && g_cursor_x < max_x + btn_size)
                        {
                            // Maximize button clicked - move to top-left corner
                            // Note: True resize would require reallocating shared memory
                            if (surf->maximized)
                            {
                                // Restore from maximized - move back to saved position
                                surf->maximized = false;
                                surf->x = surf->saved_x;
                                surf->y = surf->saved_y;
                            }
                            else
                            {
                                // Maximize - move to top-left corner
                                surf->saved_x = surf->x;
                                surf->saved_y = surf->y;
                                surf->maximized = true;
                                // Position at top-left, accounting for decorations
                                surf->x = static_cast<int32_t>(BORDER_WIDTH);
                                surf->y = static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
                            }
                            composite();
                        }
                        else if (g_cursor_x >= min_x && g_cursor_x < min_x + btn_size)
                        {
                            // Minimize button clicked
                            surf->minimized = true;
                            // If this was focused, find next surface to focus
                            if (g_focused_surface == surf->id)
                            {
                                g_focused_surface = 0;
                                // Find highest z-order non-minimized surface
                                uint32_t best_z = 0;
                                for (uint32_t i = 0; i < MAX_SURFACES; i++)
                                {
                                    if (g_surfaces[i].in_use && !g_surfaces[i].minimized &&
                                        g_surfaces[i].z_order > best_z)
                                    {
                                        best_z = g_surfaces[i].z_order;
                                        g_focused_surface = g_surfaces[i].id;
                                    }
                                }
                            }
                            composite();
                        }
                        else
                        {
                            // Start dragging (but not if maximized)
                            if (!surf->maximized)
                            {
                                g_drag_surface_id = surf->id;
                                g_drag_offset_x = g_cursor_x - surf->x;
                                g_drag_offset_y = g_cursor_y - surf->y + static_cast<int32_t>(TITLE_BAR_HEIGHT);
                            }
                        }
                    }
                    else
                    {
                        // Clicked in client area - queue button down event
                        int32_t local_x = g_cursor_x - surf->x;
                        int32_t local_y = g_cursor_y - surf->y;

                        if (local_x >= 0 && local_x < static_cast<int32_t>(surf->width) &&
                            local_y >= 0 && local_y < static_cast<int32_t>(surf->height))
                        {
                            // Determine which button (0=left, 1=right, 2=middle)
                            uint8_t button = 0;
                            if (pressed & 0x01) button = 0;      // Left
                            else if (pressed & 0x02) button = 1; // Right
                            else if (pressed & 0x04) button = 2; // Middle

                            queue_mouse_event(surf, 1, local_x, local_y, 0, 0, state.buttons, button);
                        }
                    }
                }

                composite();
            }
        }

        if (released)
        {
            g_drag_surface_id = 0;
            g_resize_surface_id = 0;
            g_resize_edge = 0;

            // Queue button up event to focused surface
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused)
            {
                int32_t local_x = g_cursor_x - focused->x;
                int32_t local_y = g_cursor_y - focused->y;

                // Determine which button was released
                uint8_t button = 0;
                if (released & 0x01) button = 0;      // Left
                else if (released & 0x02) button = 1; // Right
                else if (released & 0x04) button = 2; // Middle

                queue_mouse_event(focused, 2, local_x, local_y, 0, 0, state.buttons, button);
            }
        }

        g_last_buttons = state.buttons;
    }
}

// Main entry point
extern "C" void _start()
{
    debug_print("[displayd] Starting display server...\n");

    // Receive bootstrap capabilities
    recv_bootstrap_caps();

    // Map framebuffer
    sys::FramebufferInfo fb_info;
    if (sys::map_framebuffer(&fb_info) != 0)
    {
        debug_print("[displayd] Failed to map framebuffer\n");
        sys::exit(1);
    }

    g_fb = reinterpret_cast<uint32_t *>(fb_info.address);
    g_fb_width = fb_info.width;
    g_fb_height = fb_info.height;
    g_fb_pitch = fb_info.pitch;

    debug_print("[displayd] Framebuffer: ");
    debug_print_dec(g_fb_width);
    debug_print("x");
    debug_print_dec(g_fb_height);
    debug_print(" at 0x");
    debug_print_hex(fb_info.address);
    debug_print("\n");

    // Allocate back buffer for double buffering (eliminates flicker)
    uint64_t back_buffer_size = static_cast<uint64_t>(g_fb_pitch) * g_fb_height;
    auto back_buffer_result = sys::shm_create(back_buffer_size);
    if (back_buffer_result.error != 0)
    {
        debug_print("[displayd] Failed to allocate back buffer\n");
        sys::exit(1);
    }
    g_back_buffer = reinterpret_cast<uint32_t *>(back_buffer_result.virt_addr);
    g_draw_target = g_fb;  // Default to front buffer

    debug_print("[displayd] Double buffering enabled\n");

    // Set mouse bounds
    sys::set_mouse_bounds(g_fb_width, g_fb_height);

    // Initialize cursor to center
    g_cursor_x = static_cast<int32_t>(g_fb_width / 2);
    g_cursor_y = static_cast<int32_t>(g_fb_height / 2);

    // Initialize surfaces
    for (uint32_t i = 0; i < MAX_SURFACES; i++)
    {
        g_surfaces[i].in_use = false;
        g_surfaces[i].event_queue.init();
    }

    // Initial composite (draw desktop)
    composite();

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0)
    {
        debug_print("[displayd] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Create poll set for efficient message waiting
    g_poll_set = sys::poll_create();
    if (g_poll_set < 0)
    {
        debug_print("[displayd] Failed to create poll set\n");
        sys::exit(1);
    }

    // Add service channel to poll set (wake when messages arrive)
    if (sys::poll_add(static_cast<uint32_t>(g_poll_set),
                      static_cast<uint32_t>(recv_ch),
                      sys::POLL_CHANNEL_READ) != 0)
    {
        debug_print("[displayd] Failed to add channel to poll set\n");
        sys::exit(1);
    }

    // Register as DISPLAY
    if (sys::assign_set("DISPLAY", send_ch) < 0)
    {
        debug_print("[displayd] Failed to register DISPLAY assign\n");
        sys::exit(1);
    }

    debug_print("[displayd] Service registered as DISPLAY\n");
    debug_print("[displayd] Ready.\n");

    // Main event loop
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];
    sys::PollEvent poll_events[1];

    // Process messages in batches to avoid starving input polling
    constexpr uint32_t MAX_MESSAGES_PER_BATCH = 16;
    // Short poll timeout for mouse responsiveness (~200Hz polling)
    // IPC messages wake us immediately regardless of timeout
    constexpr int64_t POLL_TIMEOUT_MS = 5;

    while (true)
    {
        // Wait for messages on service channel
        // - Wakes immediately when IPC message arrives
        // - Times out after 5ms to poll mouse/keyboard
        int32_t poll_result = sys::poll_wait(static_cast<uint32_t>(g_poll_set),
                                              poll_events, 1, POLL_TIMEOUT_MS);

        // Process messages if channel has data
        uint32_t messages_processed = 0;

        while (messages_processed < MAX_MESSAGES_PER_BATCH)
        {
            uint32_t handle_count = 4;
            int64_t n = sys::channel_recv(g_service_channel, msg_buf, sizeof(msg_buf),
                                           handles, &handle_count);

            if (n > 0)
            {
                messages_processed++;

                // Got a message
                if (handle_count > 0)
                {
                    // Message with reply channel - handle and respond
                    int32_t client_ch = static_cast<int32_t>(handles[0]);

                    handle_request(client_ch, msg_buf, static_cast<size_t>(n),
                                  handles + 1, handle_count - 1);

                    // Close client reply channel after responding
                    sys::channel_close(client_ch);
                }
                else
                {
                    // Fire-and-forget message (no reply channel) - process directly
                    handle_request(-1, msg_buf, static_cast<size_t>(n), nullptr, 0);
                }
            }
            else
            {
                break;  // No more messages in this batch
            }
        }

        // Always poll input devices
        poll_mouse();
        poll_keyboard();

        (void)poll_result;  // Suppress unused warning
    }

    sys::exit(0);
}
