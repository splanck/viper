/**
 * @file main.cpp
 * @brief Display server (displayd) - window management and compositing.
 *
 * @details
 * Provides display and window management services:
 * - Maps the framebuffer into its address space
 * - Manages window surfaces (create, destroy, composite)
 * - Renders window decorations (title bar, borders, scrollbars)
 * - Renders a mouse cursor
 * - Routes input events to focused windows
 *
 * @section Organization
 * The code is organized into these sections:
 * 1. Debug/Utility Functions
 * 2. Global State (framebuffer, surfaces, cursor)
 * 3. Data Tables (cursor bitmap, font glyphs)
 * 4. Bootstrap/Initialization
 * 5. Drawing Primitives (fill_rect, draw_char, etc.)
 * 6. Cursor Management
 * 7. Window Decorations (title bar, buttons, scrollbars)
 * 8. Compositing (flip_buffers, composite)
 * 9. Surface Management (find, create, destroy)
 * 10. IPC Protocol Handlers
 * 11. Event Queuing (mouse, key, focus, scroll)
 * 12. Input Polling (keyboard, mouse)
 * 13. Main Loop
 */

#include "../../include/viper_colors.h"
#include "../../syscall.hpp"
#include "display_protocol.hpp"

using namespace display_protocol;

// ============================================================================
// Section 1: Debug/Utility Functions
// ============================================================================
static void debug_print(const char *msg) {
    sys::print(msg);
}

static void debug_print_hex(uint64_t val) {
    char buf[17];
    const char *hex = "0123456789abcdef";
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    sys::print(buf);
}

static void debug_print_dec(int64_t val) {
    if (val < 0) {
        sys::print("-");
        val = -val;
    }
    if (val == 0) {
        sys::print("0");
        return;
    }
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    while (val > 0 && i > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    sys::print(&buf[i]);
}

// ============================================================================
// Section 2: Global State
// ============================================================================

// Framebuffer state
static uint32_t *g_fb = nullptr;          // Front buffer (actual framebuffer)
static uint32_t *g_back_buffer = nullptr; // Back buffer for double buffering
static uint32_t *g_draw_target = nullptr; // Current drawing target
static uint32_t g_fb_width = 0;
static uint32_t g_fb_height = 0;
static uint32_t g_fb_pitch = 0;

// Surface management
static constexpr uint32_t MAX_SURFACES = 32;

// Event queue for each surface
static constexpr size_t EVENT_QUEUE_SIZE = 32;

struct QueuedEvent {
    uint32_t event_type; // DISP_EVENT_KEY, DISP_EVENT_MOUSE, etc.

    union {
        KeyEvent key;
        MouseEvent mouse;
        FocusEvent focus;
        CloseEvent close;
        MenuEvent menu;
    };
};

struct EventQueue {
    QueuedEvent events[EVENT_QUEUE_SIZE];
    size_t head;
    size_t tail;

    void init() {
        head = 0;
        tail = 0;
    }

    bool empty() const {
        return head == tail;
    }

    bool push(const QueuedEvent &ev) {
        size_t next = (tail + 1) % EVENT_QUEUE_SIZE;
        if (next == head)
            return false; // Queue full
        events[tail] = ev;
        tail = next;
        return true;
    }

    bool pop(QueuedEvent *ev) {
        if (head == tail)
            return false; // Queue empty
        *ev = events[head];
        head = (head + 1) % EVENT_QUEUE_SIZE;
        return true;
    }
};

struct Surface {
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
    int32_t event_channel; // Channel for pushing events to client (-1 if not subscribed)
    EventQueue event_queue;
    uint32_t z_order; // Higher = on top
    uint32_t flags;   // SurfaceFlags

    // Window state
    bool minimized;
    bool maximized;

    // Saved state for restore from maximized
    int32_t saved_x;
    int32_t saved_y;
    uint32_t saved_width;
    uint32_t saved_height;

    // Scrollbar state
    struct {
        bool enabled;
        int32_t content_size;  // Total content size in pixels
        int32_t viewport_size; // Visible area size
        int32_t scroll_pos;    // Current scroll position
    } vscroll, hscroll;

    // Global menu bar menus (Amiga/Mac style)
    uint8_t menu_count;
    MenuDef menus[MAX_MENUS];
};

static Surface g_surfaces[MAX_SURFACES];
static uint32_t g_next_surface_id = 1;
static uint32_t g_focused_surface = 0;
static uint32_t g_next_z_order = 1;

// Global menu bar state (Amiga/Mac style)
static constexpr uint32_t MENU_BAR_HEIGHT = 20;
static constexpr uint32_t MENU_ITEM_HEIGHT = 18;
static constexpr uint32_t MENU_PADDING = 8;
// Classic Amiga Workbench 2.0+ menu bar colors
static constexpr uint32_t COLOR_MENU_BG = 0xFF8899AA;           // Blue-grey (Amiga style)
static constexpr uint32_t COLOR_MENU_TEXT = 0xFF000000;          // Black text
static constexpr uint32_t COLOR_MENU_HIGHLIGHT = 0xFF0055AA;     // Amiga blue selection
static constexpr uint32_t COLOR_MENU_HIGHLIGHT_TEXT = 0xFFFFFFFF; // White text on selection
static constexpr uint32_t COLOR_MENU_DISABLED = 0xFF556677;      // Darker blue-grey
static constexpr uint32_t COLOR_MENU_BORDER_LIGHT = 0xFFCCDDEE;  // Light highlight
static constexpr uint32_t COLOR_MENU_BORDER_DARK = 0xFF334455;   // Dark shadow

static int32_t g_active_menu = -1;      // Which menu is open (-1 = none)
static int32_t g_hovered_menu_item = -1; // Which item in open menu is hovered
static int32_t g_menu_title_positions[MAX_MENUS]; // X positions of menu titles

// Bring a surface to the front (highest z-order)
static void bring_to_front(Surface *surf) {
    if (!surf)
        return;
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

// Scrollbar constants
static constexpr uint32_t SCROLLBAR_WIDTH = 16;
static constexpr uint32_t SCROLLBAR_MIN_THUMB = 20;
static constexpr uint32_t COLOR_SCROLLBAR_TRACK = 0xFFCCCCCC;
static constexpr uint32_t COLOR_SCROLLBAR_THUMB = 0xFF888888;
static constexpr uint32_t COLOR_SCROLLBAR_ARROW = 0xFF666666;

// Colors (from centralized viper_colors.h)
static constexpr uint32_t COLOR_DESKTOP = VIPER_COLOR_DESKTOP;
static constexpr uint32_t COLOR_TITLE_FOCUSED = VIPER_COLOR_TITLE_FOCUSED;
static constexpr uint32_t COLOR_TITLE_UNFOCUSED = VIPER_COLOR_TITLE_UNFOCUSED;
static constexpr uint32_t COLOR_BORDER = VIPER_COLOR_WINDOW_BORDER;
static constexpr uint32_t COLOR_CLOSE_BTN = VIPER_COLOR_BTN_CLOSE;
static constexpr uint32_t COLOR_WHITE = VIPER_COLOR_WHITE;
static constexpr uint32_t COLOR_SCREEN_BORDER = VIPER_COLOR_BORDER;
static constexpr uint32_t COLOR_CURSOR = 0xFFFF8800; // Amiga orange

// Screen border (matches kernel console)
static constexpr uint32_t SCREEN_BORDER_WIDTH = 20;

// ============================================================================
// Section 3: Data Tables (Cursor, Font)
// ============================================================================

// 16x16 arrow cursor (1 = white, 2 = black outline)
static const uint8_t g_cursor_data[16 * 16] = {
    2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 1, 1, 1, 1, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    2, 1, 1, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 2, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0,
};

// Simple font (8x8 bitmap for basic ASCII)
static const uint8_t g_font[96][8] = {
    // Space (32)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x00},
    // " through A... (simplified - just use blocks for now)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 34
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 35
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 36
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 37
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 38
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 39
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 40
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 41
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 42
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 43
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 44
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 45
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 46
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 47
    // 0-9
    {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00}, // 0
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // 1
    {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x7E, 0x00}, // 2
    {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00}, // 3
    {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x0C, 0x00}, // 4
    {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00}, // 5
    {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0x00}, // 6
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, // 7
    {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0x00}, // 8
    {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0x00}, // 9
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // : 58
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ; 59
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // < 60
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // = 61
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // > 62
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ? 63
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // @ 64
    // A-Z (65-90)
    {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00}, // A
    {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00}, // B
    {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00}, // C
    {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00}, // D
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0x00}, // E
    {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x00}, // F
    {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3E, 0x00}, // G
    {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00}, // H
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, // I
    {0x3E, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00}, // J
    {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x00}, // K
    {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00}, // L
    {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00}, // M
    {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x00}, // N
    {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // O
    {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x00}, // P
    {0x3C, 0x66, 0x66, 0x66, 0x6A, 0x6C, 0x36, 0x00}, // Q
    {0x7C, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x00}, // R
    {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00}, // S
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00}, // T
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U
    {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // V
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // W
    {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00}, // X
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00}, // Y
    {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0x00}, // Z
    // Remaining chars (simplified)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // [ 91
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // \ 92
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ] 93
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ^ 94
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // _ 95
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ` 96
    // a-z (97-122) - lowercase
    {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00}, // a
    {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00}, // b
    {0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0x00}, // c
    {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00}, // d
    {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00}, // e
    {0x1C, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00}, // f
    {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C}, // g
    {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00}, // h
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00}, // i
    {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38}, // j
    {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00}, // k
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // l
    {0x00, 0x00, 0x66, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // m
    {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00}, // n
    {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00}, // o
    {0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60}, // p
    {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06}, // q
    {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x00}, // r
    {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00}, // s
    {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0x00}, // t
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00}, // u
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00}, // v
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // w
    {0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00}, // x
    {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C}, // y
    {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00}, // z
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // { 123
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // | 124
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // } 125
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // ~ 126
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // DEL 127
};

// Service channel
static int32_t g_service_channel = -1;
static int32_t g_poll_set = -1; // Poll set for service channel

// ============================================================================
// Section 4: Bootstrap/Initialization
// ============================================================================

static void recv_bootstrap_caps() {
    constexpr int32_t BOOTSTRAP_RECV = 0;
    uint8_t dummy[1];
    uint32_t handles[4];
    uint32_t handle_count = 4;

    for (uint32_t i = 0; i < 2000; i++) {
        handle_count = 4;
        int64_t n = sys::channel_recv(BOOTSTRAP_RECV, dummy, sizeof(dummy), handles, &handle_count);
        if (n >= 0) {
            sys::channel_close(BOOTSTRAP_RECV);
            return;
        }
        if (n == VERR_WOULD_BLOCK) {
            sys::yield();
            continue;
        }
        return;
    }
}

// ============================================================================
// Section 5: Drawing Primitives
// ============================================================================

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x < g_fb_width && y < g_fb_height) {
        g_draw_target[y * (g_fb_pitch / 4) + x] = color;
    }
}

static inline uint32_t get_pixel(uint32_t x, uint32_t y) {
    if (x < g_fb_width && y < g_fb_height) {
        return g_draw_target[y * (g_fb_pitch / 4) + x];
    }
    return 0;
}

static void fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color) {
    // Clamp to screen
    int32_t x1 = x < 0 ? 0 : x;
    int32_t y1 = y < 0 ? 0 : y;
    int32_t x2 = x + static_cast<int32_t>(w);
    int32_t y2 = y + static_cast<int32_t>(h);
    if (x2 > static_cast<int32_t>(g_fb_width))
        x2 = static_cast<int32_t>(g_fb_width);
    if (y2 > static_cast<int32_t>(g_fb_height))
        y2 = static_cast<int32_t>(g_fb_height);

    for (int32_t py = y1; py < y2; py++) {
        for (int32_t px = x1; px < x2; px++) {
            g_draw_target[py * (g_fb_pitch / 4) + px] = color;
        }
    }
}

static void draw_char(int32_t x, int32_t y, char c, uint32_t color) {
    if (c < 32 || c > 127)
        return;
    const uint8_t *glyph = g_font[c - 32];

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int32_t px = x + col;
                int32_t py = y + row;
                if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                    py < static_cast<int32_t>(g_fb_height)) {
                    put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), color);
                }
            }
        }
    }
}

static void draw_text(int32_t x, int32_t y, const char *text, uint32_t color) {
    while (*text) {
        draw_char(x, y, *text, color);
        x += 8;
        text++;
    }
}

// ============================================================================
// Section 6: Cursor Management
// ============================================================================

static void save_cursor_background() {
    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                py < static_cast<int32_t>(g_fb_height)) {
                g_cursor_saved[dy * 16 + dx] =
                    get_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py));
            }
        }
    }
}

static void restore_cursor_background() {
    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                py < static_cast<int32_t>(g_fb_height)) {
                put_pixel(static_cast<uint32_t>(px),
                          static_cast<uint32_t>(py),
                          g_cursor_saved[dy * 16 + dx]);
            }
        }
    }
}

static void draw_cursor() {
    if (!g_cursor_visible)
        return;

    for (int dy = 0; dy < 16; dy++) {
        for (int dx = 0; dx < 16; dx++) {
            uint8_t pixel = g_cursor_data[dy * 16 + dx];
            if (pixel == 0)
                continue;

            int32_t px = g_cursor_x + dx;
            int32_t py = g_cursor_y + dy;
            if (px >= 0 && px < static_cast<int32_t>(g_fb_width) && py >= 0 &&
                py < static_cast<int32_t>(g_fb_height)) {
                uint32_t color = (pixel == 1) ? COLOR_CURSOR : 0xFF000000;
                put_pixel(static_cast<uint32_t>(px), static_cast<uint32_t>(py), color);
            }
        }
    }
}

// ============================================================================
// Section 7: Window Decorations
// ============================================================================

static constexpr uint32_t COLOR_MIN_BTN = VIPER_COLOR_BTN_MIN;
static constexpr uint32_t COLOR_MAX_BTN = VIPER_COLOR_BTN_MAX;

// Window decoration drawing
static void draw_window_decorations(Surface *surf) {
    if (!surf || !surf->in_use || !surf->visible)
        return;
    if (surf->flags & SURFACE_FLAG_NO_DECORATIONS)
        return;

    int32_t win_x = surf->x - static_cast<int32_t>(BORDER_WIDTH);
    int32_t win_y = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
    uint32_t win_w = surf->width + BORDER_WIDTH * 2;
    uint32_t win_h = surf->height + TITLE_BAR_HEIGHT + BORDER_WIDTH * 2;

    bool focused = (surf->id == g_focused_surface);

    // Border
    fill_rect(win_x, win_y, win_w, win_h, COLOR_BORDER);

    // Title bar
    uint32_t title_color = focused ? COLOR_TITLE_FOCUSED : COLOR_TITLE_UNFOCUSED;
    fill_rect(win_x + BORDER_WIDTH,
              win_y + BORDER_WIDTH,
              win_w - BORDER_WIDTH * 2,
              TITLE_BAR_HEIGHT,
              title_color);

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
    if (surf->maximized) {
        // Draw restore symbol (two overlapping rectangles)
        draw_char(max_x + 4, btn_y + 4, 'R', COLOR_WHITE);
    } else {
        // Draw maximize symbol (box outline)
        draw_char(max_x + 4, btn_y + 4, 'M', COLOR_WHITE);
    }

    // Minimize button (third from right)
    int32_t min_x = max_x - btn_spacing;
    fill_rect(min_x, btn_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, COLOR_MIN_BTN);
    draw_char(min_x + 4, btn_y + 4, '_', COLOR_WHITE);
}

// Draw vertical scrollbar for a surface
static void draw_vscrollbar(Surface *surf) {
    if (!surf || !surf->vscroll.enabled)
        return;
    if (surf->vscroll.content_size <= surf->vscroll.viewport_size)
        return;

    // Scrollbar is drawn INSIDE the client area on the right edge
    int32_t sb_x =
        surf->x + static_cast<int32_t>(surf->width) - static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_y = surf->y;
    int32_t sb_h = static_cast<int32_t>(surf->height);

    // Clamp to screen bounds
    if (sb_x < 0 || sb_x >= static_cast<int32_t>(g_fb_width))
        return;

    // Draw track background
    fill_rect(sb_x, sb_y, SCROLLBAR_WIDTH, static_cast<uint32_t>(sb_h), COLOR_SCROLLBAR_TRACK);

    // Calculate thumb position and size
    int32_t content = surf->vscroll.content_size;
    int32_t viewport = surf->vscroll.viewport_size;
    int32_t scroll_pos = surf->vscroll.scroll_pos;

    // Thumb height proportional to viewport/content ratio
    int32_t thumb_h = (viewport * sb_h) / content;
    if (thumb_h < static_cast<int32_t>(SCROLLBAR_MIN_THUMB))
        thumb_h = static_cast<int32_t>(SCROLLBAR_MIN_THUMB);

    // Thumb position proportional to scroll position
    int32_t scroll_range = content - viewport;
    int32_t track_range = sb_h - thumb_h;
    int32_t thumb_y = sb_y;
    if (scroll_range > 0)
        thumb_y = sb_y + (scroll_pos * track_range) / scroll_range;

    // Draw thumb with 3D appearance
    fill_rect(sb_x + 2,
              thumb_y + 2,
              SCROLLBAR_WIDTH - 4,
              static_cast<uint32_t>(thumb_h - 4),
              COLOR_SCROLLBAR_THUMB);

    // Top highlight
    fill_rect(sb_x + 2, thumb_y + 2, SCROLLBAR_WIDTH - 4, 1, COLOR_WHITE);
    // Left highlight
    fill_rect(sb_x + 2, thumb_y + 2, 1, static_cast<uint32_t>(thumb_h - 4), COLOR_WHITE);
    // Bottom shadow
    fill_rect(sb_x + 2, thumb_y + thumb_h - 3, SCROLLBAR_WIDTH - 4, 1, COLOR_SCROLLBAR_ARROW);
    // Right shadow
    fill_rect(sb_x + SCROLLBAR_WIDTH - 3,
              thumb_y + 2,
              1,
              static_cast<uint32_t>(thumb_h - 4),
              COLOR_SCROLLBAR_ARROW);
}

// Draw horizontal scrollbar for a surface
static void draw_hscrollbar(Surface *surf) {
    if (!surf || !surf->hscroll.enabled)
        return;
    if (surf->hscroll.content_size <= surf->hscroll.viewport_size)
        return;

    // Scrollbar is drawn INSIDE the client area on the bottom edge
    int32_t sb_x = surf->x;
    int32_t sb_y =
        surf->y + static_cast<int32_t>(surf->height) - static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_w = static_cast<int32_t>(surf->width);

    // Draw track
    fill_rect(sb_x, sb_y, static_cast<uint32_t>(sb_w), SCROLLBAR_WIDTH, COLOR_SCROLLBAR_TRACK);

    // Calculate thumb position and size
    int32_t content = surf->hscroll.content_size;
    int32_t viewport = surf->hscroll.viewport_size;
    int32_t scroll_pos = surf->hscroll.scroll_pos;

    // Thumb width proportional to viewport/content ratio
    int32_t thumb_w = (viewport * sb_w) / content;
    if (thumb_w < static_cast<int32_t>(SCROLLBAR_MIN_THUMB))
        thumb_w = static_cast<int32_t>(SCROLLBAR_MIN_THUMB);

    // Thumb position proportional to scroll position
    int32_t scroll_range = content - viewport;
    int32_t track_range = sb_w - thumb_w;
    int32_t thumb_x = sb_x;
    if (scroll_range > 0)
        thumb_x = sb_x + (scroll_pos * track_range) / scroll_range;

    // Draw thumb
    fill_rect(thumb_x + 2,
              sb_y + 2,
              static_cast<uint32_t>(thumb_w - 4),
              SCROLLBAR_WIDTH - 4,
              COLOR_SCROLLBAR_THUMB);
}

// ============================================================================
// Section 8: Global Menu Bar (Amiga/Mac style)
// ============================================================================

// Get the focused surface (returns nullptr if none)
static Surface *get_focused_surface() {
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (g_surfaces[i].in_use && g_surfaces[i].id == g_focused_surface) {
            return &g_surfaces[i];
        }
    }
    return nullptr;
}

// Get the surface whose menus should be displayed in the global menu bar.
// Returns the focused surface if it has menus, otherwise falls back to
// a SYSTEM surface with menus (e.g., the Workbench/desktop).
static Surface *get_menu_surface() {
    // First, try the focused surface
    Surface *focused = get_focused_surface();
    if (focused && focused->menu_count > 0) {
        return focused;
    }

    // Fall back to a SYSTEM surface with menus (the desktop)
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (g_surfaces[i].in_use &&
            (g_surfaces[i].flags & SURFACE_FLAG_SYSTEM) &&
            g_surfaces[i].menu_count > 0) {
            return &g_surfaces[i];
        }
    }

    return nullptr;
}

// Calculate menu title positions for the focused surface
static void calc_menu_positions(Surface *surf) {
    if (!surf || surf->menu_count == 0)
        return;

    int32_t x = MENU_PADDING; // Start at left edge of screen
    for (uint8_t i = 0; i < surf->menu_count && i < MAX_MENUS; i++) {
        g_menu_title_positions[i] = x;
        // Calculate title width (8 pixels per char)
        int32_t title_len = 0;
        for (const char *p = surf->menus[i].title; *p; p++)
            title_len++;
        x += title_len * 8 + MENU_PADDING * 2;
    }
}

// Find which menu title is at position x, y
static int32_t find_menu_at(int32_t x, int32_t y) {
    // Menu bar is at y=0 to y=MENU_BAR_HEIGHT
    if (y < 0 || y >= static_cast<int32_t>(MENU_BAR_HEIGHT))
        return -1;

    Surface *surf = get_menu_surface();
    if (!surf || surf->menu_count == 0)
        return -1;

    for (uint8_t i = 0; i < surf->menu_count; i++) {
        int32_t title_x = g_menu_title_positions[i];
        int32_t title_len = 0;
        for (const char *p = surf->menus[i].title; *p; p++)
            title_len++;
        int32_t title_w = title_len * 8 + MENU_PADDING * 2;

        if (x >= title_x && x < title_x + title_w)
            return i;
    }
    return -1;
}

// Find which menu item is at position x, y (when menu is open)
static int32_t find_menu_item_at(int32_t x, int32_t y) {
    if (g_active_menu < 0)
        return -1;

    Surface *surf = get_menu_surface();
    if (!surf || g_active_menu >= surf->menu_count)
        return -1;

    const MenuDef &menu = surf->menus[g_active_menu];
    int32_t menu_x = g_menu_title_positions[g_active_menu];
    int32_t menu_y = MENU_BAR_HEIGHT; // Dropdown starts just below menu bar

    // Calculate menu width
    int32_t max_width = 0;
    for (uint8_t i = 0; i < menu.item_count; i++) {
        int32_t item_len = 0;
        for (const char *p = menu.items[i].label; *p; p++)
            item_len++;
        int32_t shortcut_len = 0;
        for (const char *p = menu.items[i].shortcut; *p; p++)
            shortcut_len++;
        int32_t w = (item_len + shortcut_len + 4) * 8;
        if (w > max_width)
            max_width = w;
    }
    int32_t menu_w = max_width + MENU_PADDING * 2;
    int32_t menu_h = menu.item_count * MENU_ITEM_HEIGHT + 4;

    if (x < menu_x || x >= menu_x + menu_w ||
        y < menu_y || y >= menu_y + menu_h)
        return -1;

    int32_t item_idx = (y - menu_y - 2) / MENU_ITEM_HEIGHT;
    if (item_idx >= 0 && item_idx < menu.item_count)
        return item_idx;

    return -1;
}

// Draw the global menu bar (at very top of screen, Amiga style)
static void draw_menu_bar() {
    Surface *surf = get_menu_surface();

    // Debug: log menu surface status once
    static bool logged_menu_status = false;
    if (!logged_menu_status) {
        if (surf) {
            debug_print("[displayd] draw_menu_bar: found surface id=");
            debug_print_dec(surf->id);
            debug_print(", menu_count=");
            debug_print_dec(surf->menu_count);
            if (surf->menu_count > 0) {
                debug_print(", first menu title='");
                debug_print(surf->menus[0].title);
                debug_print("'");
            }
            debug_print("\n");
        } else {
            debug_print("[displayd] draw_menu_bar: no menu surface found\n");
        }
        logged_menu_status = true;
    }

    // Menu bar background - full width at top of screen
    int32_t bar_x = 0;
    int32_t bar_y = 0;
    int32_t bar_w = g_fb_width;

    fill_rect(bar_x, bar_y, bar_w, MENU_BAR_HEIGHT, COLOR_MENU_BG);

    // Top highlight, bottom shadow
    for (int32_t px = bar_x; px < bar_x + bar_w; px++) {
        put_pixel(px, bar_y, COLOR_MENU_BORDER_LIGHT);
        put_pixel(px, bar_y + MENU_BAR_HEIGHT - 1, COLOR_MENU_BORDER_DARK);
    }

    // Draw menu titles if focused surface has menus
    if (surf && surf->menu_count > 0) {
        calc_menu_positions(surf);

        for (uint8_t i = 0; i < surf->menu_count; i++) {
            int32_t title_x = g_menu_title_positions[i];
            int32_t title_len = 0;
            for (const char *p = surf->menus[i].title; *p; p++)
                title_len++;
            int32_t title_w = title_len * 8 + MENU_PADDING * 2;

            // Highlight active menu
            if (i == g_active_menu) {
                fill_rect(title_x, bar_y + 1, title_w, MENU_BAR_HEIGHT - 2, COLOR_MENU_HIGHLIGHT);
                draw_text(title_x + MENU_PADDING, bar_y + 4, surf->menus[i].title, COLOR_MENU_HIGHLIGHT_TEXT);
            } else {
                draw_text(title_x + MENU_PADDING, bar_y + 4, surf->menus[i].title, COLOR_MENU_TEXT);
            }
        }
    }

    // Right side: App title or "ViperDOS"
    const char *right_text = surf ? surf->title : "ViperDOS";
    int32_t text_len = 0;
    for (const char *p = right_text; *p; p++)
        text_len++;
    draw_text(bar_x + bar_w - text_len * 8 - MENU_PADDING, bar_y + 4, right_text, COLOR_MENU_DISABLED);
}

// Draw the open pulldown menu
static void draw_pulldown_menu() {
    if (g_active_menu < 0)
        return;

    Surface *surf = get_menu_surface();
    if (!surf || g_active_menu >= surf->menu_count)
        return;

    const MenuDef &menu = surf->menus[g_active_menu];
    int32_t menu_x = g_menu_title_positions[g_active_menu];
    int32_t menu_y = MENU_BAR_HEIGHT; // Dropdown starts just below menu bar

    // Calculate menu width
    int32_t max_width = 0;
    for (uint8_t i = 0; i < menu.item_count; i++) {
        int32_t item_len = 0;
        for (const char *p = menu.items[i].label; *p; p++)
            item_len++;
        int32_t shortcut_len = 0;
        for (const char *p = menu.items[i].shortcut; *p; p++)
            shortcut_len++;
        int32_t w = (item_len + shortcut_len + 4) * 8;
        if (w > max_width)
            max_width = w;
    }

    int32_t menu_w = max_width + MENU_PADDING * 2;
    int32_t menu_h = menu.item_count * MENU_ITEM_HEIGHT + 4;

    // Menu background
    fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_MENU_BG);

    // 3D border
    for (int32_t px = menu_x; px < menu_x + menu_w; px++) {
        put_pixel(px, menu_y, COLOR_MENU_BORDER_LIGHT);
        put_pixel(px, menu_y + menu_h - 1, COLOR_MENU_BORDER_DARK);
    }
    for (int32_t py = menu_y; py < menu_y + menu_h; py++) {
        put_pixel(menu_x, py, COLOR_MENU_BORDER_LIGHT);
        put_pixel(menu_x + menu_w - 1, py, COLOR_MENU_BORDER_DARK);
    }

    // Draw menu items
    int32_t item_y = menu_y + 2;
    for (uint8_t i = 0; i < menu.item_count; i++) {
        const MenuItem &item = menu.items[i];

        // Separator
        if (item.label[0] == '-' || item.label[0] == '\0') {
            int32_t sep_y = item_y + MENU_ITEM_HEIGHT / 2;
            for (int32_t px = menu_x + 4; px < menu_x + menu_w - 4; px++)
                put_pixel(px, sep_y, COLOR_MENU_BORDER_DARK);
            item_y += MENU_ITEM_HEIGHT;
            continue;
        }

        // Highlight hovered item
        uint32_t text_color = item.enabled ? COLOR_MENU_TEXT : COLOR_MENU_DISABLED;
        if (static_cast<int32_t>(i) == g_hovered_menu_item && item.enabled) {
            fill_rect(menu_x + 2, item_y, menu_w - 4, MENU_ITEM_HEIGHT, COLOR_MENU_HIGHLIGHT);
            text_color = COLOR_MENU_HIGHLIGHT_TEXT;
        }

        // Checkmark
        if (item.checked) {
            draw_text(menu_x + 4, item_y + 2, "*", text_color);
        }

        // Label
        draw_text(menu_x + 16, item_y + 2, item.label, text_color);

        // Shortcut (right-aligned)
        if (item.shortcut[0]) {
            int32_t sc_len = 0;
            for (const char *p = item.shortcut; *p; p++)
                sc_len++;
            draw_text(menu_x + menu_w - sc_len * 8 - 8, item_y + 2, item.shortcut, text_color);
        }

        item_y += MENU_ITEM_HEIGHT;
    }
}

// ============================================================================
// Section 9: Compositing
// ============================================================================

static void flip_buffers() {
    uint32_t pixels_per_row = g_fb_pitch / 4;
    uint32_t total_pixels = pixels_per_row * g_fb_height;

    // Fast copy using 64-bit transfers where possible
    uint64_t *dst = reinterpret_cast<uint64_t *>(g_fb);
    uint64_t *src = reinterpret_cast<uint64_t *>(g_back_buffer);
    uint32_t count64 = total_pixels / 2;

    for (uint32_t i = 0; i < count64; i++) {
        dst[i] = src[i];
    }

    // Handle odd pixel if any
    if (total_pixels & 1) {
        g_fb[total_pixels - 1] = g_back_buffer[total_pixels - 1];
    }
}

// Composite all surfaces to framebuffer (double-buffered)
static void composite() {
    // Draw to back buffer to avoid flicker
    g_draw_target = g_back_buffer;

    // Draw blue border around screen edges
    // Top border
    fill_rect(0, 0, g_fb_width, SCREEN_BORDER_WIDTH, COLOR_SCREEN_BORDER);
    // Bottom border
    fill_rect(
        0, g_fb_height - SCREEN_BORDER_WIDTH, g_fb_width, SCREEN_BORDER_WIDTH, COLOR_SCREEN_BORDER);
    // Left border
    fill_rect(0, 0, SCREEN_BORDER_WIDTH, g_fb_height, COLOR_SCREEN_BORDER);
    // Right border
    fill_rect(
        g_fb_width - SCREEN_BORDER_WIDTH, 0, SCREEN_BORDER_WIDTH, g_fb_height, COLOR_SCREEN_BORDER);

    // Clear inner desktop area
    fill_rect(SCREEN_BORDER_WIDTH,
              SCREEN_BORDER_WIDTH,
              g_fb_width - 2 * SCREEN_BORDER_WIDTH,
              g_fb_height - 2 * SCREEN_BORDER_WIDTH,
              COLOR_DESKTOP);

    // Build sorted list of visible surfaces by z-order (lowest first = drawn under)
    Surface *sorted[MAX_SURFACES];
    uint32_t count = 0;

    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        Surface *surf = &g_surfaces[i];
        if (!surf->in_use || !surf->visible || !surf->pixels)
            continue;
        if (surf->minimized)
            continue; // Don't draw minimized windows
        sorted[count++] = surf;
    }

    // Simple insertion sort by z_order (small N, runs frequently)
    for (uint32_t i = 1; i < count; i++) {
        Surface *key = sorted[i];
        int32_t j = static_cast<int32_t>(i) - 1;
        while (j >= 0 && sorted[j]->z_order > key->z_order) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }

    // Draw surfaces back to front (lower z-order first)
    for (uint32_t i = 0; i < count; i++) {
        Surface *surf = sorted[i];

        // Draw decorations first
        draw_window_decorations(surf);

        // Blit surface content to back buffer
        for (uint32_t sy = 0; sy < surf->height; sy++) {
            int32_t dst_y = surf->y + static_cast<int32_t>(sy);
            if (dst_y < 0 || dst_y >= static_cast<int32_t>(g_fb_height))
                continue;

            for (uint32_t sx = 0; sx < surf->width; sx++) {
                int32_t dst_x = surf->x + static_cast<int32_t>(sx);
                if (dst_x < 0 || dst_x >= static_cast<int32_t>(g_fb_width))
                    continue;

                uint32_t pixel = surf->pixels[sy * (surf->stride / 4) + sx];
                g_back_buffer[dst_y * (g_fb_pitch / 4) + dst_x] = pixel;
            }
        }

        // Draw scrollbars on top of content
        draw_vscrollbar(surf);
        draw_hscrollbar(surf);
    }

    // Draw global menu bar (Amiga/Mac style - always on top)
    draw_menu_bar();
    draw_pulldown_menu();

    // Copy back buffer to front buffer in one operation
    flip_buffers();

    // Switch to front buffer for cursor operations
    g_draw_target = g_fb;

    // Save background under cursor, then draw cursor (on front buffer)
    save_cursor_background();
    draw_cursor();
}

// ============================================================================
// Section 9: Surface Management
// ============================================================================

static Surface *find_surface_at(int32_t x, int32_t y) {
    Surface *best = nullptr;
    uint32_t best_z = 0;
    bool found_any = false;

    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        Surface *surf = &g_surfaces[i];
        if (!surf->in_use || !surf->visible || surf->minimized)
            continue;

        // Check if in window bounds (including decorations)
        int32_t win_x = surf->x - static_cast<int32_t>(BORDER_WIDTH);
        int32_t win_y = surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
        int32_t win_x2 = surf->x + static_cast<int32_t>(surf->width + BORDER_WIDTH);
        int32_t win_y2 = surf->y + static_cast<int32_t>(surf->height + BORDER_WIDTH);

        if (x >= win_x && x < win_x2 && y >= win_y && y < win_y2) {
            // Pick the one with highest z-order (top-most)
            // Use >= for first match to handle z_order = 0 (SYSTEM surfaces)
            if (!found_any || surf->z_order > best_z) {
                best = surf;
                best_z = surf->z_order;
                found_any = true;
            }
        }
    }
    return best;
}

// Find surface by ID
static Surface *find_surface_by_id(uint32_t id) {
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (g_surfaces[i].in_use && g_surfaces[i].id == id) {
            return &g_surfaces[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Section 10: IPC Protocol Handlers
// ============================================================================

static void handle_create_surface(int32_t client_channel,
                                  const uint8_t *data,
                                  size_t len,
                                  const uint32_t * /*handles*/,
                                  uint32_t /*handle_count*/) {
    if (len < sizeof(CreateSurfaceRequest))
        return;
    auto *req = reinterpret_cast<const CreateSurfaceRequest *>(data);

    CreateSurfaceReply reply;
    reply.type = DISP_CREATE_SURFACE_REPLY;
    reply.request_id = req->request_id;

    // Find free surface slot
    Surface *surf = nullptr;
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        if (!g_surfaces[i].in_use) {
            surf = &g_surfaces[i];
            break;
        }
    }

    if (!surf) {
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
    if (shm_result.error != 0) {
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
    surf->event_channel = -1; // No event channel until client subscribes
    surf->event_queue.init();
    surf->flags = req->flags;
    // SYSTEM surfaces (like desktop/workbench) stay at z-order 0 (always behind)
    // Other windows get highest z-order (on top)
    if (surf->flags & SURFACE_FLAG_SYSTEM) {
        surf->z_order = 0;
    } else {
        surf->z_order = g_next_z_order++;
    }
    surf->minimized = false;
    surf->maximized = false;

    // Initialize scrollbar state
    surf->vscroll.enabled = false;
    surf->vscroll.content_size = 0;
    surf->vscroll.viewport_size = 0;
    surf->vscroll.scroll_pos = 0;
    surf->hscroll.enabled = false;
    surf->hscroll.content_size = 0;
    surf->hscroll.viewport_size = 0;
    surf->hscroll.scroll_pos = 0;

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
    for (int i = 0; i < 63 && req->title[i]; i++) {
        surf->title[i] = req->title[i];
    }
    surf->title[63] = '\0';

    // Clear surface to white
    for (uint32_t y = 0; y < surf->height; y++) {
        for (uint32_t x = 0; x < surf->width; x++) {
            surf->pixels[y * (stride / 4) + x] = COLOR_WHITE;
        }
    }

    // Set focus to new surface (unless it's a SYSTEM surface like desktop)
    if (!(surf->flags & SURFACE_FLAG_SYSTEM)) {
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
static void handle_request(int32_t client_channel,
                           const uint8_t *data,
                           size_t len,
                           const uint32_t *handles,
                           uint32_t handle_count) {
    if (len < 4)
        return;

    uint32_t msg_type = *reinterpret_cast<const uint32_t *>(data);

    switch (msg_type) {
        case DISP_GET_INFO: {
            debug_print("[displayd] Handling DISP_GET_INFO, client_channel=");
            debug_print_dec(client_channel);
            debug_print("\n");

            if (len < sizeof(GetInfoRequest))
                return;
            auto *req = reinterpret_cast<const GetInfoRequest *>(data);

            GetInfoReply reply;
            reply.type = DISP_INFO_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.width = g_fb_width;
            reply.height = g_fb_height;
            reply.format = 0x34325258; // XRGB8888

            int64_t send_result =
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            debug_print("[displayd] DISP_GET_INFO reply sent, result=");
            debug_print_dec(send_result);
            debug_print("\n");
            break;
        }

        case DISP_CREATE_SURFACE:
            handle_create_surface(client_channel, data, len, handles, handle_count);
            break;

        case DISP_DESTROY_SURFACE: {
            if (len < sizeof(DestroySurfaceRequest))
                return;
            auto *req = reinterpret_cast<const DestroySurfaceRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id) {
                    sys::shm_close(g_surfaces[i].shm_handle);
                    // Close event channel if subscribed
                    if (g_surfaces[i].event_channel >= 0) {
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

        case DISP_PRESENT: {
            if (len < sizeof(PresentRequest))
                return;

            // Just recomposite
            composite();

            // Only send reply if client provided a reply channel
            if (client_channel >= 0) {
                auto *req = reinterpret_cast<const PresentRequest *>(data);
                GenericReply reply;
                reply.type = DISP_GENERIC_REPLY;
                reply.request_id = req->request_id;
                reply.status = 0;
                sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            }
            break;
        }

        case DISP_SET_GEOMETRY: {
            if (len < sizeof(SetGeometryRequest))
                return;
            auto *req = reinterpret_cast<const SetGeometryRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id) {
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

        case DISP_SET_VISIBLE: {
            if (len < sizeof(SetVisibleRequest))
                return;
            auto *req = reinterpret_cast<const SetVisibleRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                if (g_surfaces[i].in_use && g_surfaces[i].id == req->surface_id) {
                    g_surfaces[i].visible = (req->visible != 0);
                    reply.status = 0;
                    break;
                }
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite();
            break;
        }

        case DISP_SET_TITLE: {
            if (len < sizeof(SetTitleRequest))
                return;
            auto *req = reinterpret_cast<const SetTitleRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                // Copy new title (safely)
                for (int i = 0; i < 63 && req->title[i]; i++) {
                    surf->title[i] = req->title[i];
                }
                surf->title[63] = '\0';
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            composite(); // Redraw to update title bar
            break;
        }

        case DISP_SUBSCRIBE_EVENTS: {
            if (len < sizeof(SubscribeEventsRequest))
                return;
            auto *req = reinterpret_cast<const SubscribeEventsRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            // Find the surface and store the event channel
            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf && handle_count > 0) {
                // Close old event channel if any
                if (surf->event_channel >= 0) {
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

        case DISP_POLL_EVENT: {
            if (len < sizeof(PollEventRequest))
                return;
            auto *req = reinterpret_cast<const PollEventRequest *>(data);

            PollEventReply reply;
            reply.type = DISP_POLL_EVENT_REPLY;
            reply.request_id = req->request_id;
            reply.has_event = 0;
            reply.event_type = 0;

            // Find the surface and check for events
            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                QueuedEvent ev;
                if (surf->event_queue.pop(&ev)) {
                    reply.has_event = 1;
                    reply.event_type = ev.event_type;

                    // Copy event data based on type
                    switch (ev.event_type) {
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

        case DISP_LIST_WINDOWS: {
            if (len < sizeof(ListWindowsRequest))
                return;
            auto *req = reinterpret_cast<const ListWindowsRequest *>(data);

            ListWindowsReply reply;
            reply.type = DISP_LIST_WINDOWS_REPLY;
            reply.request_id = req->request_id;
            reply.status = 0;
            reply.window_count = 0;

            // Collect all non-system windows
            for (uint32_t i = 0; i < MAX_SURFACES && reply.window_count < 16; i++) {
                Surface *surf = &g_surfaces[i];
                if (!surf->in_use)
                    continue;
                if (surf->flags & SURFACE_FLAG_SYSTEM)
                    continue;

                WindowInfo &info = reply.windows[reply.window_count];
                info.surface_id = surf->id;
                info.flags = surf->flags;
                info.minimized = surf->minimized ? 1 : 0;
                info.maximized = surf->maximized ? 1 : 0;
                info.focused = (g_focused_surface == surf->id) ? 1 : 0;

                // Copy title
                for (int j = 0; j < 63 && surf->title[j]; j++) {
                    info.title[j] = surf->title[j];
                }
                info.title[63] = '\0';

                reply.window_count++;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_RESTORE_WINDOW: {
            if (len < sizeof(RestoreWindowRequest))
                return;
            auto *req = reinterpret_cast<const RestoreWindowRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                surf->minimized = false;
                bring_to_front(surf);
                g_focused_surface = surf->id;
                composite();
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_SET_SCROLLBAR: {
            if (len < sizeof(SetScrollbarRequest))
                return;
            auto *req = reinterpret_cast<const SetScrollbarRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                if (req->vertical) {
                    surf->vscroll.enabled = req->enabled != 0;
                    surf->vscroll.content_size = req->content_size;
                    surf->vscroll.viewport_size = req->viewport_size;
                    surf->vscroll.scroll_pos = req->scroll_pos;
                } else {
                    surf->hscroll.enabled = req->enabled != 0;
                    surf->hscroll.content_size = req->content_size;
                    surf->hscroll.viewport_size = req->viewport_size;
                    surf->hscroll.scroll_pos = req->scroll_pos;
                }
                composite();
                reply.status = 0;
            }

            sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
            break;
        }

        case DISP_SET_MENU: {
            // Handle global menu bar registration (Amiga/Mac style)
            if (len < sizeof(SetMenuRequest))
                return;
            auto *req = reinterpret_cast<const SetMenuRequest *>(data);

            GenericReply reply;
            reply.type = DISP_GENERIC_REPLY;
            reply.request_id = req->request_id;
            reply.status = -1;

            Surface *surf = find_surface_by_id(req->surface_id);
            if (surf) {
                // Store menu definitions for this surface
                surf->menu_count = req->menu_count;
                if (surf->menu_count > MAX_MENUS) {
                    surf->menu_count = MAX_MENUS;
                }

                // Copy menu data
                for (uint8_t i = 0; i < surf->menu_count; i++) {
                    surf->menus[i] = req->menus[i];
                }

                debug_print("[displayd] Set menu bar for surface ");
                debug_print_dec(surf->id);
                debug_print(", menu_count=");
                debug_print_dec(surf->menu_count);
                debug_print("\n");

                reply.status = 0;
                composite(); // Redraw menu bar
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
static uint8_t g_resize_edge = 0; // Bitmask: 1=left, 2=right, 4=top, 8=bottom
static int32_t g_resize_start_x = 0;
static int32_t g_resize_start_y = 0;
static int32_t g_resize_start_width = 0;
static int32_t g_resize_start_height = 0;
static int32_t g_resize_start_surf_x = 0;
static int32_t g_resize_start_surf_y = 0;

// Scrollbar drag state
static uint32_t g_scrollbar_surface_id = 0;
static bool g_scrollbar_vertical = true;
static int32_t g_scrollbar_start_y = 0;
static int32_t g_scrollbar_start_pos = 0;
static int32_t g_scrollbar_last_sent_pos = 0;       // Throttle: last position we sent to client
static constexpr int32_t SCROLL_THROTTLE_DELTA = 8; // Minimum change before sending event

static constexpr int32_t RESIZE_BORDER = 6; // Width of resize handle area
static constexpr uint32_t MIN_WINDOW_WIDTH = 100;
static constexpr uint32_t MIN_WINDOW_HEIGHT = 60;

// Check if point is on a resize edge of a surface, return edge mask
static uint8_t get_resize_edge(Surface *surf, int32_t x, int32_t y) {
    if (!surf)
        return 0;
    if (surf->maximized)
        return 0; // Can't resize maximized windows

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
    if (x < win_x1 + RESIZE_BORDER)
        edge |= 1; // Left
    if (x >= win_x2 - RESIZE_BORDER)
        edge |= 2; // Right
    if (y >= win_y2 - RESIZE_BORDER)
        edge |= 8; // Bottom

    return edge;
}

// ============================================================================
// Section 11: Event Queuing
// ============================================================================

// Queue a mouse event to a surface
static void queue_mouse_event(Surface *surf,
                              uint8_t event_type,
                              int32_t local_x,
                              int32_t local_y,
                              int32_t dx,
                              int32_t dy,
                              uint8_t buttons,
                              uint8_t button) {
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
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.mouse, sizeof(ev.mouse), nullptr, 0);
    } else {
        // Fall back to queue for legacy poll-based clients
        if (!surf->event_queue.push(ev)) {
            // Overflow - event dropped (don't spam logs for mouse moves)
        }
    }
}

// Queue a scroll event to a surface
static void queue_scroll_event(Surface *surf, int32_t new_position, bool vertical) {
    ScrollEvent ev;
    ev.type = DISP_EVENT_SCROLL;
    ev.surface_id = surf->id;
    ev.new_position = new_position;
    ev.vertical = vertical ? 1 : 0;
    ev._pad[0] = 0;
    ev._pad[1] = 0;
    ev._pad[2] = 0;

    // Send scroll event to client
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev, sizeof(ev), nullptr, 0);
    }
}

// Check if point is on vertical scrollbar, return scroll position or -1
static int32_t check_vscrollbar_click(Surface *surf, int32_t x, int32_t y) {
    if (!surf || !surf->vscroll.enabled)
        return -1;
    if (surf->vscroll.content_size <= surf->vscroll.viewport_size)
        return -1;

    // Scrollbar bounds (inside client area on right edge)
    int32_t sb_x =
        surf->x + static_cast<int32_t>(surf->width) - static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_y = surf->y;
    int32_t sb_w = static_cast<int32_t>(SCROLLBAR_WIDTH);
    int32_t sb_h = static_cast<int32_t>(surf->height);

    // Check if click is in scrollbar area
    if (x < sb_x || x >= sb_x + sb_w)
        return -1;
    if (y < sb_y || y >= sb_y + sb_h)
        return -1;

    // Calculate new scroll position based on click position
    int32_t content = surf->vscroll.content_size;
    int32_t viewport = surf->vscroll.viewport_size;
    int32_t scroll_range = content - viewport;

    // Calculate thumb size
    int32_t thumb_h = (viewport * sb_h) / content;
    if (thumb_h < static_cast<int32_t>(SCROLLBAR_MIN_THUMB))
        thumb_h = static_cast<int32_t>(SCROLLBAR_MIN_THUMB);

    int32_t track_range = sb_h - thumb_h;

    // Map click position to scroll position
    int32_t click_offset = y - sb_y - thumb_h / 2;
    if (click_offset < 0)
        click_offset = 0;
    if (click_offset > track_range)
        click_offset = track_range;

    int32_t new_pos = 0;
    if (track_range > 0)
        new_pos = (click_offset * scroll_range) / track_range;

    return new_pos;
}

// Queue a focus event to a surface
static void queue_focus_event(Surface *surf, bool gained) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_FOCUS;
    ev.focus.type = DISP_EVENT_FOCUS;
    ev.focus.surface_id = surf->id;
    ev.focus.gained = gained ? 1 : 0;
    ev.focus._pad[0] = 0;
    ev.focus._pad[1] = 0;
    ev.focus._pad[2] = 0;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.focus, sizeof(ev.focus), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - focus event dropped
        }
    }
}

// Queue a close event to a surface
static void queue_close_event(Surface *surf) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_CLOSE;
    ev.close.type = DISP_EVENT_CLOSE;
    ev.close.surface_id = surf->id;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.close, sizeof(ev.close), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - close event dropped
        }
    }
}

// Queue a key event to a surface
static void queue_key_event(Surface *surf, uint16_t keycode, uint8_t modifiers, bool pressed) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_KEY;
    ev.key.type = DISP_EVENT_KEY;
    ev.key.surface_id = surf->id;
    ev.key.keycode = keycode;
    ev.key.modifiers = modifiers;
    ev.key.pressed = pressed ? 1 : 0;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.key, sizeof(ev.key), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - key event dropped
        }
    }
}

// Queue a menu event to a surface (Amiga/Mac style global menu bar)
static void queue_menu_event(Surface *surf, uint8_t menu_index, uint8_t item_index, uint8_t action) {
    QueuedEvent ev;
    ev.event_type = DISP_EVENT_MENU;
    ev.menu.type = DISP_EVENT_MENU;
    ev.menu.surface_id = surf->id;
    ev.menu.menu_index = menu_index;
    ev.menu.item_index = item_index;
    ev.menu.action = action;

    // If client has event channel, send directly
    if (surf->event_channel >= 0) {
        sys::channel_send(surf->event_channel, &ev.menu, sizeof(ev.menu), nullptr, 0);
    } else {
        if (!surf->event_queue.push(ev)) {
            // Overflow - menu event dropped
        }
    }
}

// ============================================================================
// Section 12: Input Polling
// ============================================================================

// Complete a window resize by reallocating shared memory and notifying client
// NOTE: Full resize with SHM reallocation is disabled due to a crash bug.
// For now, resize is visual-only (frame changes but content stays same size).
static bool complete_resize(Surface * /*surf*/, uint32_t /*new_width*/, uint32_t /*new_height*/) {
    // TODO: Fix SHM reallocation crash before re-enabling
    // The crash appears to be related to handle management after channel_send
    return true;
}

static void poll_keyboard() {
    // Drain all pending events from the kernel queue (limit to 64 per call)
    for (int i = 0; i < 64; i++) {
        // Check if input is available from kernel
        if (sys::input_has_event() == 0)
            return;

        // Get the event from kernel
        sys::InputEvent ev;
        if (sys::input_get_event(&ev) != 0)
            return;

        // Route keyboard events to focused surface
        if (ev.type == sys::InputEventType::KeyPress ||
            ev.type == sys::InputEventType::KeyRelease) {
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused) {
                bool pressed = (ev.type == sys::InputEventType::KeyPress);
                queue_key_event(focused, ev.code, ev.modifiers, pressed);
            }
        }
        // Note: Mouse events from event_queue are discarded here since
        // poll_mouse() uses get_mouse_state() directly for mouse position
    }
}

static void poll_mouse() {
    sys::MouseState state;
    if (sys::get_mouse_state(&state) != 0)
        return;

    bool cursor_moved = (state.x != g_last_mouse_x || state.y != g_last_mouse_y);

    // Update cursor position
    if (cursor_moved) {
        restore_cursor_background();
        g_cursor_x = state.x;
        g_cursor_y = state.y;

        // Handle menu hover (when a pulldown menu is open)
        if (g_active_menu >= 0) {
            int32_t new_hover = find_menu_item_at(g_cursor_x, g_cursor_y);
            if (new_hover != g_hovered_menu_item) {
                g_hovered_menu_item = new_hover;
                composite(); // Redraw to show hover highlight
            }
            // Also check if hovering over a different menu title
            int32_t hover_menu = find_menu_at(g_cursor_x, g_cursor_y);
            if (hover_menu >= 0 && hover_menu != g_active_menu) {
                // Switch to the hovered menu
                g_active_menu = hover_menu;
                g_hovered_menu_item = -1;
                composite();
            }
        }

        // Handle resizing
        if (g_resize_surface_id != 0) {
            Surface *resize_surf = find_surface_by_id(g_resize_surface_id);
            if (resize_surf) {
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
                if (new_width < static_cast<int32_t>(MIN_WINDOW_WIDTH)) {
                    if (g_resize_edge & 1) // Left edge: adjust x
                        new_x = g_resize_start_surf_x + g_resize_start_width - MIN_WINDOW_WIDTH;
                    new_width = MIN_WINDOW_WIDTH;
                }
                if (new_height < static_cast<int32_t>(MIN_WINDOW_HEIGHT)) {
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
        else if (g_drag_surface_id != 0) {
            Surface *drag_surf = find_surface_by_id(g_drag_surface_id);
            if (drag_surf) {
                drag_surf->x = g_cursor_x - g_drag_offset_x;
                drag_surf->y =
                    g_cursor_y - g_drag_offset_y + static_cast<int32_t>(TITLE_BAR_HEIGHT);
            }
            composite();
        }
        // Handle scrollbar dragging
        else if (g_scrollbar_surface_id != 0) {
            Surface *scroll_surf = find_surface_by_id(g_scrollbar_surface_id);
            if (scroll_surf && g_scrollbar_vertical && scroll_surf->vscroll.enabled) {
                // Calculate scroll position based on cursor movement
                int32_t track_height =
                    static_cast<int32_t>(scroll_surf->height) - SCROLLBAR_MIN_THUMB;
                if (track_height > 0) {
                    int32_t dy = g_cursor_y - g_scrollbar_start_y;
                    int32_t max_scroll =
                        scroll_surf->vscroll.content_size - scroll_surf->vscroll.viewport_size;
                    if (max_scroll > 0) {
                        // Calculate new position: proportional to cursor movement
                        int32_t new_pos = g_scrollbar_start_pos + (dy * max_scroll) / track_height;

                        // Clamp to valid range
                        if (new_pos < 0)
                            new_pos = 0;
                        if (new_pos > max_scroll)
                            new_pos = max_scroll;

                        if (new_pos != scroll_surf->vscroll.scroll_pos) {
                            scroll_surf->vscroll.scroll_pos = new_pos;

                            // Throttle: only send event if delta exceeds threshold
                            int32_t delta = new_pos - g_scrollbar_last_sent_pos;
                            if (delta < 0)
                                delta = -delta;
                            if (delta >= SCROLL_THROTTLE_DELTA) {
                                queue_scroll_event(scroll_surf, new_pos, true);
                                g_scrollbar_last_sent_pos = new_pos;
                            }

                            composite();
                        }
                    }
                }
            }
        } else {
            // Queue mouse move event to focused surface if in client area
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused) {
                int32_t local_x = g_cursor_x - focused->x;
                int32_t local_y = g_cursor_y - focused->y;

                // Only send move events within client area
                if (local_x >= 0 && local_x < static_cast<int32_t>(focused->width) &&
                    local_y >= 0 && local_y < static_cast<int32_t>(focused->height)) {
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
    if (state.buttons != g_last_buttons) {
        uint8_t pressed = state.buttons & ~g_last_buttons;
        uint8_t released = g_last_buttons & ~state.buttons;

        Surface *surf = find_surface_at(g_cursor_x, g_cursor_y);

        if (pressed) {
            // ----------------------------------------------------------------
            // Global Menu Bar Handling (Amiga/Mac style - always on top)
            // ----------------------------------------------------------------
            bool menu_handled = false;

            // Check if click is in the menu bar area (y=0 to MENU_BAR_HEIGHT)
            if (g_cursor_y < static_cast<int32_t>(MENU_BAR_HEIGHT)) {
                int32_t clicked_menu = find_menu_at(g_cursor_x, g_cursor_y);

                if (clicked_menu >= 0) {
                    // Clicked on a menu title
                    if (g_active_menu == clicked_menu) {
                        // Same menu - toggle closed
                        g_active_menu = -1;
                    } else {
                        // Different menu or no menu open - open this menu
                        g_active_menu = clicked_menu;
                    }
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                } else if (g_active_menu >= 0) {
                    // Menu is open but didn't click on a title - close it
                    g_active_menu = -1;
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                }
            }
            // Check if click is in an open pulldown menu
            else if (g_active_menu >= 0) {
                int32_t item_idx = find_menu_item_at(g_cursor_x, g_cursor_y);

                if (item_idx >= 0) {
                    // Clicked on a menu item - execute it
                    Surface *menu_surf = get_menu_surface();
                    if (menu_surf && g_active_menu < menu_surf->menu_count) {
                        const MenuDef &menu = menu_surf->menus[g_active_menu];
                        if (item_idx < menu.item_count) {
                            const MenuItem &item = menu.items[item_idx];
                            // Only trigger if enabled and not a separator
                            if (item.enabled && item.label[0] != '\0' && item.action != 0) {
                                queue_menu_event(menu_surf, static_cast<uint8_t>(g_active_menu),
                                                 static_cast<uint8_t>(item_idx), item.action);
                            }
                        }
                    }
                    g_active_menu = -1;
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                } else {
                    // Clicked outside pulldown - close menu
                    g_active_menu = -1;
                    g_hovered_menu_item = -1;
                    composite();
                    menu_handled = true;
                }
            }

            // If menu handled the click, skip normal window handling
            if (menu_handled) {
                // Do nothing further
            } else if (surf) {
                // Handle focus change and bring to front
                // Don't change focus to SYSTEM surfaces (like desktop/workbench)
                // They should never receive focus or come to front
                if (surf->id != g_focused_surface && !(surf->flags & SURFACE_FLAG_SYSTEM)) {
                    Surface *old_focused = find_surface_by_id(g_focused_surface);
                    if (old_focused) {
                        queue_focus_event(old_focused, false);
                    }
                    g_focused_surface = surf->id;
                    queue_focus_event(surf, true);
                    bring_to_front(surf);
                }

                // Check for resize edges first
                uint8_t edge = get_resize_edge(surf, g_cursor_x, g_cursor_y);
                if (edge != 0) {
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
                else {
                    int32_t title_y1 =
                        surf->y - static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
                    int32_t title_y2 = surf->y - static_cast<int32_t>(BORDER_WIDTH);

                    if (g_cursor_y >= title_y1 && g_cursor_y < title_y2) {
                        // Check for window control buttons
                        // Must match draw_window_decorations() calculation:
                        // close_x = win_x + win_w - BORDER_WIDTH - CLOSE_BUTTON_SIZE - 4
                        // where win_x = surf->x - BORDER_WIDTH, win_w = surf->width + BORDER_WIDTH
                        // * 2 So: close_x = surf->x + surf->width - CLOSE_BUTTON_SIZE - 4
                        int32_t btn_spacing = static_cast<int32_t>(CLOSE_BUTTON_SIZE + 4);
                        int32_t close_x = surf->x + static_cast<int32_t>(surf->width) -
                                          static_cast<int32_t>(CLOSE_BUTTON_SIZE) - 4;
                        int32_t max_x = close_x - btn_spacing;
                        int32_t min_x = max_x - btn_spacing;
                        int32_t btn_size = static_cast<int32_t>(CLOSE_BUTTON_SIZE);

                        if (g_cursor_x >= close_x && g_cursor_x < close_x + btn_size) {
                            // Close button clicked - queue close event
                            queue_close_event(surf);
                        } else if (g_cursor_x >= max_x && g_cursor_x < max_x + btn_size) {
                            // Maximize button clicked - move to top-left corner
                            // Note: True resize would require reallocating shared memory
                            if (surf->maximized) {
                                // Restore from maximized - move back to saved position
                                surf->maximized = false;
                                surf->x = surf->saved_x;
                                surf->y = surf->saved_y;
                            } else {
                                // Maximize - move to top-left corner
                                surf->saved_x = surf->x;
                                surf->saved_y = surf->y;
                                surf->maximized = true;
                                // Position at top-left, accounting for decorations
                                surf->x = static_cast<int32_t>(BORDER_WIDTH);
                                surf->y = static_cast<int32_t>(TITLE_BAR_HEIGHT + BORDER_WIDTH);
                            }
                            composite();
                        } else if (g_cursor_x >= min_x && g_cursor_x < min_x + btn_size) {
                            // Minimize button clicked
                            surf->minimized = true;
                            // If this was focused, find next surface to focus
                            if (g_focused_surface == surf->id) {
                                g_focused_surface = 0;
                                // Find highest z-order non-minimized surface
                                uint32_t best_z = 0;
                                for (uint32_t i = 0; i < MAX_SURFACES; i++) {
                                    if (g_surfaces[i].in_use && !g_surfaces[i].minimized &&
                                        g_surfaces[i].z_order > best_z) {
                                        best_z = g_surfaces[i].z_order;
                                        g_focused_surface = g_surfaces[i].id;
                                    }
                                }
                            }
                            composite();
                        } else {
                            // Start dragging (but not if maximized)
                            if (!surf->maximized) {
                                g_drag_surface_id = surf->id;
                                g_drag_offset_x = g_cursor_x - surf->x;
                                g_drag_offset_y =
                                    g_cursor_y - surf->y + static_cast<int32_t>(TITLE_BAR_HEIGHT);
                            }
                        }
                    } else {
                        // Check for scrollbar click first
                        int32_t scroll_pos = check_vscrollbar_click(surf, g_cursor_x, g_cursor_y);
                        if (scroll_pos >= 0) {
                            // Start scrollbar drag
                            g_scrollbar_surface_id = surf->id;
                            g_scrollbar_vertical = true;
                            g_scrollbar_start_y = g_cursor_y;
                            g_scrollbar_start_pos = surf->vscroll.scroll_pos;
                            g_scrollbar_last_sent_pos = scroll_pos; // Init throttle tracking

                            // Update scroll position and notify client
                            surf->vscroll.scroll_pos = scroll_pos;
                            queue_scroll_event(surf, scroll_pos, true);
                        } else {
                            // Clicked in client area - queue button down event
                            int32_t local_x = g_cursor_x - surf->x;
                            int32_t local_y = g_cursor_y - surf->y;

                            if (local_x >= 0 && local_x < static_cast<int32_t>(surf->width) &&
                                local_y >= 0 && local_y < static_cast<int32_t>(surf->height)) {
                                // Determine which button (0=left, 1=right, 2=middle)
                                uint8_t button = 0;
                                if (pressed & 0x01)
                                    button = 0; // Left
                                else if (pressed & 0x02)
                                    button = 1; // Right
                                else if (pressed & 0x04)
                                    button = 2; // Middle

                                queue_mouse_event(
                                    surf, 1, local_x, local_y, 0, 0, state.buttons, button);
                            }
                        }
                    }
                }

                composite();
            }
        }

        if (released) {
            // Complete resize if we were resizing
            if (g_resize_surface_id != 0) {
                Surface *resize_surf = find_surface_by_id(g_resize_surface_id);
                if (resize_surf) {
                    // Calculate final dimensions
                    int32_t dx = g_cursor_x - g_resize_start_x;
                    int32_t dy = g_cursor_y - g_resize_start_y;

                    int32_t new_width = g_resize_start_width;
                    int32_t new_height = g_resize_start_height;

                    if (g_resize_edge & 2) // Right
                        new_width = g_resize_start_width + dx;
                    if (g_resize_edge & 1) // Left
                        new_width = g_resize_start_width - dx;
                    if (g_resize_edge & 8) // Bottom
                        new_height = g_resize_start_height + dy;

                    // Clamp to minimum size
                    if (new_width < static_cast<int32_t>(MIN_WINDOW_WIDTH))
                        new_width = MIN_WINDOW_WIDTH;
                    if (new_height < static_cast<int32_t>(MIN_WINDOW_HEIGHT))
                        new_height = MIN_WINDOW_HEIGHT;

                    // Complete the resize with new SHM
                    complete_resize(resize_surf,
                                    static_cast<uint32_t>(new_width),
                                    static_cast<uint32_t>(new_height));
                    composite();
                }
            }

            // Send final scroll event if we were scrolling (ensure exact final position)
            if (g_scrollbar_surface_id != 0) {
                Surface *scroll_surf = find_surface_by_id(g_scrollbar_surface_id);
                if (scroll_surf && scroll_surf->vscroll.scroll_pos != g_scrollbar_last_sent_pos) {
                    queue_scroll_event(scroll_surf, scroll_surf->vscroll.scroll_pos, true);
                }
            }

            g_drag_surface_id = 0;
            g_resize_surface_id = 0;
            g_resize_edge = 0;
            g_scrollbar_surface_id = 0;

            // Queue button up event to focused surface
            Surface *focused = find_surface_by_id(g_focused_surface);
            if (focused) {
                int32_t local_x = g_cursor_x - focused->x;
                int32_t local_y = g_cursor_y - focused->y;

                // Determine which button was released
                uint8_t button = 0;
                if (released & 0x01)
                    button = 0; // Left
                else if (released & 0x02)
                    button = 1; // Right
                else if (released & 0x04)
                    button = 2; // Middle

                queue_mouse_event(focused, 2, local_x, local_y, 0, 0, state.buttons, button);
            }
        }

        g_last_buttons = state.buttons;
    }
}

// ============================================================================
// Section 13: Main Entry Point
// ============================================================================

extern "C" void _start() {
    // Reset console colors to defaults (white on blue)
    sys::print("\033[0m");

    debug_print("[displayd] Starting display server...\n");

    // Receive bootstrap capabilities
    recv_bootstrap_caps();

    // Map framebuffer
    sys::FramebufferInfo fb_info;
    if (sys::map_framebuffer(&fb_info) != 0) {
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
    if (back_buffer_result.error != 0) {
        debug_print("[displayd] Failed to allocate back buffer\n");
        sys::exit(1);
    }
    g_back_buffer = reinterpret_cast<uint32_t *>(back_buffer_result.virt_addr);
    g_draw_target = g_fb; // Default to front buffer

    debug_print("[displayd] Double buffering enabled\n");

    // Set mouse bounds
    sys::set_mouse_bounds(g_fb_width, g_fb_height);

    // Initialize cursor to center
    g_cursor_x = static_cast<int32_t>(g_fb_width / 2);
    g_cursor_y = static_cast<int32_t>(g_fb_height / 2);

    // Initialize surfaces
    for (uint32_t i = 0; i < MAX_SURFACES; i++) {
        g_surfaces[i].in_use = false;
        g_surfaces[i].event_queue.init();
    }

    // Initial composite (draw desktop)
    composite();

    // Create service channel
    auto ch_result = sys::channel_create();
    if (ch_result.error != 0) {
        debug_print("[displayd] Failed to create service channel\n");
        sys::exit(1);
    }
    int32_t send_ch = static_cast<int32_t>(ch_result.val0);
    int32_t recv_ch = static_cast<int32_t>(ch_result.val1);
    g_service_channel = recv_ch;

    // Create poll set for efficient message waiting
    g_poll_set = sys::poll_create();
    if (g_poll_set < 0) {
        debug_print("[displayd] Failed to create poll set\n");
        sys::exit(1);
    }

    // Add service channel to poll set (wake when messages arrive)
    if (sys::poll_add(static_cast<uint32_t>(g_poll_set),
                      static_cast<uint32_t>(recv_ch),
                      sys::POLL_CHANNEL_READ) != 0) {
        debug_print("[displayd] Failed to add channel to poll set\n");
        sys::exit(1);
    }

    // Register as DISPLAY
    if (sys::assign_set("DISPLAY", send_ch) < 0) {
        debug_print("[displayd] Failed to register DISPLAY assign\n");
        sys::exit(1);
    }

    // Note: Don't print startup messages here - they would appear on the
    // graphical display before vinit has a chance to disable gcon

    // Main event loop
    uint8_t msg_buf[MAX_PAYLOAD];
    uint32_t handles[4];
    sys::PollEvent poll_events[1];

    // Process messages in batches to avoid starving input polling
    constexpr uint32_t MAX_MESSAGES_PER_BATCH = 16;
    // Short poll timeout for mouse responsiveness (~200Hz polling)
    // IPC messages wake us immediately regardless of timeout
    constexpr int64_t POLL_TIMEOUT_MS = 5;

    // Debug: track loop iterations to detect if displayd is running
    uint64_t loop_count = 0;
    uint64_t last_debug_time = 0;

    while (true) {
        loop_count++;

        // Print periodic heartbeat to show displayd is running
        uint64_t now = sys::uptime();
        if (now - last_debug_time > 5000) {
            debug_print("[displayd] Heartbeat: loop=");
            debug_print_dec(static_cast<int64_t>(loop_count));
            debug_print("\n");
            last_debug_time = now;
        }

        // Wait for messages on service channel
        // - Wakes immediately when IPC message arrives
        // - Times out after 5ms to poll mouse/keyboard
        int32_t poll_result =
            sys::poll_wait(static_cast<uint32_t>(g_poll_set), poll_events, 1, POLL_TIMEOUT_MS);

        // Process messages if channel has data
        uint32_t messages_processed = 0;

        while (messages_processed < MAX_MESSAGES_PER_BATCH) {
            uint32_t handle_count = 4;
            int64_t n = sys::channel_recv(
                g_service_channel, msg_buf, sizeof(msg_buf), handles, &handle_count);

            if (n > 0) {
                messages_processed++;

                // Got a message - show message type
                uint32_t msg_type = *reinterpret_cast<uint32_t *>(msg_buf);
                debug_print("[displayd] Received msg type=");
                debug_print_dec(msg_type);
                debug_print(" len=");
                debug_print_dec(n);
                debug_print(" handles=");
                debug_print_dec(handle_count);
                debug_print("\n");

                if (handle_count > 0) {
                    // Message with reply channel - handle and respond
                    int32_t client_ch = static_cast<int32_t>(handles[0]);

                    handle_request(
                        client_ch, msg_buf, static_cast<size_t>(n), handles + 1, handle_count - 1);

                    // Close client reply channel after responding
                    sys::channel_close(client_ch);
                } else {
                    // Fire-and-forget message (no reply channel) - process directly
                    handle_request(-1, msg_buf, static_cast<size_t>(n), nullptr, 0);
                }
            } else {
                break; // No more messages in this batch
            }
        }

        // Always poll input devices
        poll_mouse();
        poll_keyboard();

        (void)poll_result; // Suppress unused warning
    }

    sys::exit(0);
}
