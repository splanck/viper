//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file types.hpp
 * @brief Shared types, constants, and data structures for displayd.
 */

#pragma once

#include "../../include/viper_colors.h"
#include "../../syscall.hpp"
#include "display_protocol.hpp"

using namespace display_protocol;

// ============================================================================
// Constants
// ============================================================================

// Surface limits
static constexpr uint32_t MAX_SURFACES = 32;
static constexpr size_t EVENT_QUEUE_SIZE = 32;

// Cursor
static constexpr int CURSOR_SIZE = 24;

// Window decorations
static constexpr uint32_t TITLE_BAR_HEIGHT = 24;
static constexpr uint32_t BORDER_WIDTH = 2;
static constexpr uint32_t CLOSE_BUTTON_SIZE = 16;

// Menu bar
static constexpr uint32_t MENU_BAR_HEIGHT = 20;
static constexpr uint32_t MENU_ITEM_HEIGHT = 18;
static constexpr uint32_t MENU_PADDING = 8;

// Minimum Y position for window client area (title bar must be below menu bar)
static constexpr int32_t MIN_WINDOW_Y =
    static_cast<int32_t>(MENU_BAR_HEIGHT + TITLE_BAR_HEIGHT + BORDER_WIDTH);

// Scrollbar
static constexpr uint32_t SCROLLBAR_WIDTH = 16;
static constexpr uint32_t SCROLLBAR_MIN_THUMB = 20;

// Screen border
static constexpr uint32_t SCREEN_BORDER_WIDTH = 20;

// Resize
static constexpr int32_t RESIZE_BORDER = 6;
static constexpr uint32_t MIN_WINDOW_WIDTH = 100;
static constexpr uint32_t MIN_WINDOW_HEIGHT = 60;

// Scroll throttle
static constexpr int32_t SCROLL_THROTTLE_DELTA = 8;

// ============================================================================
// Colors
// ============================================================================

// From centralized viper_colors.h
static constexpr uint32_t COLOR_DESKTOP = VIPER_COLOR_DESKTOP;
static constexpr uint32_t COLOR_TITLE_FOCUSED = VIPER_COLOR_TITLE_FOCUSED;
static constexpr uint32_t COLOR_TITLE_UNFOCUSED = VIPER_COLOR_TITLE_UNFOCUSED;
static constexpr uint32_t COLOR_BORDER = VIPER_COLOR_WINDOW_BORDER;
static constexpr uint32_t COLOR_CLOSE_BTN = VIPER_COLOR_BTN_CLOSE;
static constexpr uint32_t COLOR_MIN_BTN = VIPER_COLOR_BTN_MIN;
static constexpr uint32_t COLOR_MAX_BTN = VIPER_COLOR_BTN_MAX;
static constexpr uint32_t COLOR_WHITE = VIPER_COLOR_WHITE;
static constexpr uint32_t COLOR_SCREEN_BORDER = VIPER_COLOR_BORDER;
static constexpr uint32_t COLOR_CURSOR = 0xFFFF8800; // Amiga orange

// Menu colors (Amiga Workbench 2.0+ style)
static constexpr uint32_t COLOR_MENU_BG = 0xFF8899AA;
static constexpr uint32_t COLOR_MENU_TEXT = 0xFF000000;
static constexpr uint32_t COLOR_MENU_HIGHLIGHT = 0xFF0055AA;
static constexpr uint32_t COLOR_MENU_HIGHLIGHT_TEXT = 0xFFFFFFFF;
static constexpr uint32_t COLOR_MENU_DISABLED = 0xFF556677;
static constexpr uint32_t COLOR_MENU_BORDER_LIGHT = 0xFFCCDDEE;
static constexpr uint32_t COLOR_MENU_BORDER_DARK = 0xFF334455;

// Scrollbar colors
static constexpr uint32_t COLOR_SCROLLBAR_TRACK = 0xFFCCCCCC;
static constexpr uint32_t COLOR_SCROLLBAR_THUMB = 0xFF888888;
static constexpr uint32_t COLOR_SCROLLBAR_ARROW = 0xFF666666;

// ============================================================================
// Event Queue
// ============================================================================

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

// ============================================================================
// Surface
// ============================================================================

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
