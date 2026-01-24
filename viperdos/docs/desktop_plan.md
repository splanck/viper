# ViperDOS Desktop Implementation Plan

## Overview

This document outlines the complete implementation plan for transforming ViperDOS's existing GUI infrastructure into a
fully functional desktop environment with interactive applications.

**Current State**: Windows display, mouse cursor works, but apps cannot receive input events.

**Target State**: Interactive desktop with window management, application launcher, and widget toolkit.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         Applications                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────────────┐ │
│  │ Launcher │  │  Editor  │  │ Terminal │  │  File Manager    │ │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────────┬─────────┘ │
├───────┴─────────────┴───────────────┴────────────────┴──────────┤
│                        Widget Toolkit                            │
│     Button, Label, TextBox, ListView, Menu, Dialog, Layout      │
├─────────────────────────────────────────────────────────────────┤
│                           libgui                                 │
│   Windows, Drawing, Events, Fonts, Clipboard, Drag-and-Drop    │
├─────────────────────────────────────────────────────────────────┤
│                          displayd                                │
│   Compositor, Window Manager, Input Router, Cursor, Themes     │
├─────────────────────────────────────────────────────────────────┤
│                           Kernel                                 │
│   Framebuffer (ramfb/virtio-gpu) │ Input (keyboard/mouse)      │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Event Delivery System

**Priority**: CRITICAL
**Estimated Complexity**: Medium
**Blocking**: All interactive functionality

### 1.1 Event Types

Define comprehensive event types in `display_protocol.hpp`:

```cpp
// Event type enumeration
enum GuiEventType : uint32_t {
    GUI_EVENT_NONE = 0,

    // Mouse events
    GUI_EVENT_MOUSE_MOVE = 1,      // Mouse moved within window
    GUI_EVENT_MOUSE_ENTER = 2,     // Mouse entered window
    GUI_EVENT_MOUSE_LEAVE = 3,     // Mouse left window
    GUI_EVENT_MOUSE_DOWN = 4,      // Button pressed
    GUI_EVENT_MOUSE_UP = 5,        // Button released
    GUI_EVENT_MOUSE_CLICK = 6,     // Button press+release (convenience)
    GUI_EVENT_MOUSE_DOUBLE = 7,    // Double click
    GUI_EVENT_MOUSE_SCROLL = 8,    // Scroll wheel

    // Keyboard events
    GUI_EVENT_KEY_DOWN = 20,       // Key pressed
    GUI_EVENT_KEY_UP = 21,         // Key released
    GUI_EVENT_KEY_CHAR = 22,       // Character input (after translation)

    // Window events
    GUI_EVENT_FOCUS_IN = 40,       // Window gained focus
    GUI_EVENT_FOCUS_OUT = 41,      // Window lost focus
    GUI_EVENT_CLOSE = 42,          // Close button clicked
    GUI_EVENT_RESIZE = 43,         // Window resized
    GUI_EVENT_MOVE = 44,           // Window moved
    GUI_EVENT_EXPOSE = 45,         // Window needs redraw

    // System events
    GUI_EVENT_QUIT = 60,           // Application should exit
};

// Mouse button identifiers
enum GuiMouseButton : uint8_t {
    GUI_BUTTON_LEFT = 1,
    GUI_BUTTON_RIGHT = 2,
    GUI_BUTTON_MIDDLE = 4,
};

// Keyboard modifier flags
enum GuiKeyMod : uint8_t {
    GUI_MOD_SHIFT = 0x01,
    GUI_MOD_CTRL = 0x02,
    GUI_MOD_ALT = 0x04,
    GUI_MOD_META = 0x08,
    GUI_MOD_CAPS = 0x10,
};

// Event structure (fixed size for IPC efficiency)
struct GuiEvent {
    uint32_t type;           // GuiEventType
    uint32_t surface_id;     // Target window
    uint64_t timestamp;      // Monotonic time in microseconds

    union {
        // Mouse events
        struct {
            int32_t x, y;        // Position relative to window
            int32_t dx, dy;      // Delta (for move events)
            uint8_t button;      // Button that triggered event
            uint8_t buttons;     // Currently held buttons
            uint8_t modifiers;   // Keyboard modifiers held
            int8_t scroll;       // Scroll delta (-1, 0, +1)
        } mouse;

        // Keyboard events
        struct {
            uint16_t keycode;    // Linux evdev keycode
            uint16_t scancode;   // Raw scancode
            uint8_t modifiers;   // Active modifiers
            uint8_t repeat;      // Repeat count (0 = first press)
            char character[4];   // UTF-8 character (for KEY_CHAR)
        } key;

        // Window events
        struct {
            int32_t x, y;        // New position (for MOVE)
            uint32_t width;      // New size (for RESIZE)
            uint32_t height;
        } window;
    };
};
```

### 1.2 Display Protocol Extensions

Add to `display_protocol.hpp`:

```cpp
// New message types
constexpr uint32_t DISP_SUBSCRIBE_EVENTS = 20;   // Start receiving events
constexpr uint32_t DISP_POLL_EVENT = 21;         // Non-blocking event poll
constexpr uint32_t DISP_WAIT_EVENT = 22;         // Blocking event wait
constexpr uint32_t DISP_EVENT_NOTIFY = 23;       // Server->client event push

// Subscribe request
struct DispSubscribeRequest {
    uint32_t type;           // DISP_SUBSCRIBE_EVENTS
    uint32_t surface_id;
    uint32_t event_mask;     // Bitmask of desired event types
};

// Poll request/response
struct DispPollEventRequest {
    uint32_t type;           // DISP_POLL_EVENT
    uint32_t surface_id;
};

struct DispPollEventReply {
    uint32_t type;           // DISP_POLL_EVENT_REPLY
    int32_t result;          // 1 = event available, 0 = none, -1 = error
    GuiEvent event;          // The event (if result == 1)
};

// Event notification (server-initiated)
struct DispEventNotify {
    uint32_t type;           // DISP_EVENT_NOTIFY
    GuiEvent event;
};
```

### 1.3 displayd Event Queue Implementation

Modify `user/servers/displayd/main.cpp`:

```cpp
// Per-surface event queue
constexpr size_t EVENT_QUEUE_SIZE = 64;

struct EventQueue {
    GuiEvent events[EVENT_QUEUE_SIZE];
    size_t head;
    size_t tail;
    uint32_t event_mask;     // Subscribed events

    bool push(const GuiEvent& ev) {
        size_t next = (tail + 1) % EVENT_QUEUE_SIZE;
        if (next == head) return false;  // Queue full
        events[tail] = ev;
        tail = next;
        return true;
    }

    bool pop(GuiEvent* ev) {
        if (head == tail) return false;  // Queue empty
        *ev = events[head];
        head = (head + 1) % EVENT_QUEUE_SIZE;
        return true;
    }

    bool empty() const { return head == tail; }
};

// Add to Surface structure
struct Surface {
    // ... existing fields ...
    EventQueue event_queue;
    bool subscribed;
};
```

### 1.4 Input Routing in displayd

Add input routing logic to displayd's main loop:

```cpp
// Find surface under mouse cursor
Surface* find_surface_at(int32_t x, int32_t y) {
    // Search back-to-front (topmost window first)
    for (int i = MAX_SURFACES - 1; i >= 0; i--) {
        Surface& s = surfaces[i];
        if (!s.in_use || !s.visible) continue;

        // Include decoration in hit testing
        int32_t sx = s.x - BORDER_WIDTH;
        int32_t sy = s.y - TITLE_HEIGHT - BORDER_WIDTH;
        int32_t sw = s.width + 2 * BORDER_WIDTH;
        int32_t sh = s.height + TITLE_HEIGHT + 2 * BORDER_WIDTH;

        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
            return &s;
        }
    }
    return nullptr;
}

// Route mouse event to appropriate surface
void route_mouse_event(int32_t screen_x, int32_t screen_y,
                       uint8_t buttons, uint8_t old_buttons) {
    Surface* target = find_surface_at(screen_x, screen_y);

    // Handle focus change
    if (buttons && !old_buttons && target != focused_surface) {
        if (focused_surface) {
            GuiEvent ev = {};
            ev.type = GUI_EVENT_FOCUS_OUT;
            ev.surface_id = focused_surface->id;
            focused_surface->event_queue.push(ev);
        }
        focused_surface = target;
        if (target) {
            GuiEvent ev = {};
            ev.type = GUI_EVENT_FOCUS_IN;
            ev.surface_id = target->id;
            target->event_queue.push(ev);
            bring_to_front(target);
        }
    }

    if (!target) return;

    // Convert to window-local coordinates
    int32_t local_x = screen_x - target->x;
    int32_t local_y = screen_y - target->y;

    // Check if in client area (not decorations)
    if (local_x >= 0 && local_y >= 0 &&
        local_x < target->width && local_y < target->height) {

        // Generate appropriate event
        GuiEvent ev = {};
        ev.surface_id = target->id;
        ev.timestamp = get_time_us();
        ev.mouse.x = local_x;
        ev.mouse.y = local_y;
        ev.mouse.buttons = buttons;
        ev.mouse.modifiers = get_keyboard_modifiers();

        // Determine event type
        uint8_t pressed = buttons & ~old_buttons;
        uint8_t released = old_buttons & ~buttons;

        if (pressed) {
            ev.type = GUI_EVENT_MOUSE_DOWN;
            ev.mouse.button = pressed;
            target->event_queue.push(ev);
        }
        if (released) {
            ev.type = GUI_EVENT_MOUSE_UP;
            ev.mouse.button = released;
            target->event_queue.push(ev);
        }
        if (buttons == old_buttons && (local_x != last_x || local_y != last_y)) {
            ev.type = GUI_EVENT_MOUSE_MOVE;
            ev.mouse.dx = local_x - last_x;
            ev.mouse.dy = local_y - last_y;
            target->event_queue.push(ev);
        }
    }

    // Handle decoration clicks
    if (local_y < 0 && local_y >= -TITLE_HEIGHT) {
        // Title bar area
        if (pressed & GUI_BUTTON_LEFT) {
            // Check close button
            if (local_x >= target->width - CLOSE_BUTTON_SIZE) {
                GuiEvent ev = {};
                ev.type = GUI_EVENT_CLOSE;
                ev.surface_id = target->id;
                target->event_queue.push(ev);
            } else {
                // Start window drag
                start_drag(target, screen_x, screen_y);
            }
        }
    }
}

// Route keyboard event to focused surface
void route_keyboard_event(uint16_t keycode, bool pressed, char character) {
    if (!focused_surface) return;

    GuiEvent ev = {};
    ev.surface_id = focused_surface->id;
    ev.timestamp = get_time_us();
    ev.key.keycode = keycode;
    ev.key.modifiers = get_keyboard_modifiers();

    if (pressed) {
        ev.type = GUI_EVENT_KEY_DOWN;
        focused_surface->event_queue.push(ev);

        // Also send character event if printable
        if (character) {
            ev.type = GUI_EVENT_KEY_CHAR;
            ev.key.character[0] = character;
            ev.key.character[1] = '\0';
            focused_surface->event_queue.push(ev);
        }
    } else {
        ev.type = GUI_EVENT_KEY_UP;
        focused_surface->event_queue.push(ev);
    }
}
```

### 1.5 Handle Poll/Wait Requests

Add message handlers in displayd:

```cpp
case DISP_POLL_EVENT: {
    auto* req = reinterpret_cast<DispPollEventRequest*>(buffer);
    Surface* s = find_surface(req->surface_id);

    DispPollEventReply reply = {};
    reply.type = DISP_POLL_EVENT_REPLY;

    if (s && !s->event_queue.empty()) {
        reply.result = 1;
        s->event_queue.pop(&reply.event);
    } else {
        reply.result = 0;
    }

    sys::channel_send(client_channel, &reply, sizeof(reply), nullptr, 0);
    break;
}

case DISP_SUBSCRIBE_EVENTS: {
    auto* req = reinterpret_cast<DispSubscribeRequest*>(buffer);
    Surface* s = find_surface(req->surface_id);
    if (s) {
        s->subscribed = true;
        s->event_queue.event_mask = req->event_mask;
    }
    // Send ack
    uint32_t ack = DISP_SUBSCRIBE_EVENTS_ACK;
    sys::channel_send(client_channel, &ack, sizeof(ack), nullptr, 0);
    break;
}
```

### 1.6 libgui Event API Implementation

Update `user/libgui/src/gui.cpp`:

```cpp
int gui_poll_event(gui_window_t *win, gui_event_t *event) {
    if (!win || !g_display_channel) return -1;

    DispPollEventRequest req = {};
    req.type = DISP_POLL_EVENT;
    req.surface_id = win->surface_id;

    if (sys::channel_send(g_display_channel, &req, sizeof(req), nullptr, 0) < 0) {
        return -1;
    }

    DispPollEventReply reply;
    uint32_t handles[1];
    uint32_t handle_count = 0;

    if (sys::channel_recv(g_display_channel, &reply, sizeof(reply),
                          handles, &handle_count) < 0) {
        return -1;
    }

    if (reply.result == 1) {
        // Convert internal event to public event structure
        event->type = reply.event.type;
        event->timestamp = reply.event.timestamp;

        switch (reply.event.type) {
            case GUI_EVENT_MOUSE_MOVE:
            case GUI_EVENT_MOUSE_DOWN:
            case GUI_EVENT_MOUSE_UP:
                event->mouse.x = reply.event.mouse.x;
                event->mouse.y = reply.event.mouse.y;
                event->mouse.button = reply.event.mouse.button;
                event->mouse.buttons = reply.event.mouse.buttons;
                break;
            case GUI_EVENT_KEY_DOWN:
            case GUI_EVENT_KEY_UP:
            case GUI_EVENT_KEY_CHAR:
                event->key.keycode = reply.event.key.keycode;
                event->key.modifiers = reply.event.key.modifiers;
                memcpy(event->key.character, reply.event.key.character, 4);
                break;
            // ... other event types
        }
        return 1;
    }

    return 0;  // No event available
}

int gui_wait_event(gui_window_t *win, gui_event_t *event) {
    // Simple polling implementation for now
    // TODO: Use blocking channel receive
    while (1) {
        int result = gui_poll_event(win, event);
        if (result != 0) return result;
        sys::yield();
    }
}
```

### 1.7 Testing Phase 1

Create test application `user/gui_event_test/main.c`:

```c
#include <gui.h>

int main() {
    if (gui_init() < 0) {
        return 1;
    }

    gui_window_t *win = gui_create_window("Event Test", 400, 300);
    if (!win) {
        gui_shutdown();
        return 1;
    }

    // Clear to white
    gui_fill_rect(win, 0, 0, 400, 300, 0xFFFFFFFF);
    gui_draw_text(win, 10, 10, "Click or press keys...", 0xFF000000);
    gui_present(win);

    char buf[64];
    int y = 40;

    while (1) {
        gui_event_t ev;
        if (gui_poll_event(win, &ev) == 1) {
            switch (ev.type) {
                case GUI_EVENT_MOUSE_DOWN:
                    snprintf(buf, sizeof(buf), "Mouse down at %d,%d btn=%d",
                             ev.mouse.x, ev.mouse.y, ev.mouse.button);
                    break;
                case GUI_EVENT_KEY_CHAR:
                    snprintf(buf, sizeof(buf), "Key char: '%s'", ev.key.character);
                    break;
                case GUI_EVENT_CLOSE:
                    goto done;
                default:
                    snprintf(buf, sizeof(buf), "Event type %d", ev.type);
            }

            // Scroll log up if needed
            if (y > 280) {
                gui_fill_rect(win, 0, 30, 400, 270, 0xFFFFFFFF);
                y = 40;
            }

            gui_draw_text(win, 10, y, buf, 0xFF000000);
            gui_present(win);
            y += 12;
        }

        sys_yield();
    }

done:
    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
```

---

## Phase 2: Window Management

**Priority**: High
**Estimated Complexity**: Low-Medium
**Depends on**: Phase 1

### 2.1 Z-Order Management

Add z-order tracking to displayd:

```cpp
// Z-order array (indices into surfaces, front-to-back)
uint32_t z_order[MAX_SURFACES];
uint32_t z_order_count = 0;

void bring_to_front(Surface* s) {
    // Find current position
    int pos = -1;
    for (uint32_t i = 0; i < z_order_count; i++) {
        if (z_order[i] == s->id) {
            pos = i;
            break;
        }
    }

    if (pos < 0) return;

    // Shift others down
    for (uint32_t i = pos; i > 0; i--) {
        z_order[i] = z_order[i - 1];
    }
    z_order[0] = s->id;

    recomposite();
}

void send_to_back(Surface* s) {
    // Similar but moves to end
}
```

### 2.2 Window Resizing

Add resize handles to window decorations:

```cpp
constexpr int RESIZE_HANDLE_SIZE = 8;

enum ResizeEdge {
    RESIZE_NONE = 0,
    RESIZE_N = 1, RESIZE_S = 2, RESIZE_E = 4, RESIZE_W = 8,
    RESIZE_NE = RESIZE_N | RESIZE_E,
    RESIZE_NW = RESIZE_N | RESIZE_W,
    RESIZE_SE = RESIZE_S | RESIZE_E,
    RESIZE_SW = RESIZE_S | RESIZE_W,
};

ResizeEdge hit_test_resize(Surface* s, int32_t x, int32_t y) {
    int32_t sx = s->x - BORDER_WIDTH;
    int32_t sy = s->y - TITLE_HEIGHT - BORDER_WIDTH;
    int32_t sw = s->width + 2 * BORDER_WIDTH;
    int32_t sh = s->height + TITLE_HEIGHT + 2 * BORDER_WIDTH;

    ResizeEdge edge = RESIZE_NONE;

    // Check corners first (8x8 corner zones)
    if (x < sx + RESIZE_HANDLE_SIZE) edge |= RESIZE_W;
    if (x >= sx + sw - RESIZE_HANDLE_SIZE) edge |= RESIZE_E;
    if (y < sy + RESIZE_HANDLE_SIZE) edge |= RESIZE_N;
    if (y >= sy + sh - RESIZE_HANDLE_SIZE) edge |= RESIZE_S;

    return edge;
}

// During resize drag
void handle_resize(Surface* s, ResizeEdge edge, int32_t dx, int32_t dy) {
    int32_t new_x = s->x, new_y = s->y;
    int32_t new_w = s->width, new_h = s->height;

    if (edge & RESIZE_W) { new_x += dx; new_w -= dx; }
    if (edge & RESIZE_E) { new_w += dx; }
    if (edge & RESIZE_N) { new_y += dy; new_h -= dy; }
    if (edge & RESIZE_S) { new_h += dy; }

    // Enforce minimum size
    if (new_w < 100) new_w = 100;
    if (new_h < 50) new_h = 50;

    // Reallocate surface buffer if size changed
    if (new_w != s->width || new_h != s->height) {
        reallocate_surface(s, new_w, new_h);

        // Notify client
        GuiEvent ev = {};
        ev.type = GUI_EVENT_RESIZE;
        ev.surface_id = s->id;
        ev.window.width = new_w;
        ev.window.height = new_h;
        s->event_queue.push(ev);
    }

    s->x = new_x;
    s->y = new_y;
    recomposite();
}
```

### 2.3 Minimize/Maximize

Add window state tracking:

```cpp
enum WindowState {
    WINDOW_NORMAL,
    WINDOW_MINIMIZED,
    WINDOW_MAXIMIZED,
};

struct Surface {
    // ... existing fields ...
    WindowState state;
    // Saved geometry for restore
    int32_t saved_x, saved_y;
    uint32_t saved_width, saved_height;
};

void minimize_window(Surface* s) {
    s->state = WINDOW_MINIMIZED;
    s->visible = false;
    recomposite();
    // Notify taskbar (future)
}

void maximize_window(Surface* s) {
    if (s->state == WINDOW_MAXIMIZED) {
        // Restore
        s->x = s->saved_x;
        s->y = s->saved_y;
        reallocate_surface(s, s->saved_width, s->saved_height);
        s->state = WINDOW_NORMAL;
    } else {
        // Save current geometry
        s->saved_x = s->x;
        s->saved_y = s->y;
        s->saved_width = s->width;
        s->saved_height = s->height;

        // Expand to full screen (minus taskbar)
        s->x = 0;
        s->y = 0;
        reallocate_surface(s, screen_width, screen_height - TASKBAR_HEIGHT);
        s->state = WINDOW_MAXIMIZED;
    }
    recomposite();
}
```

### 2.4 Window Buttons

Update decoration rendering:

```cpp
void draw_window_buttons(Surface* s) {
    int32_t btn_y = s->y - TITLE_HEIGHT + 4;
    int32_t btn_x = s->x + s->width - BORDER_WIDTH;

    // Close button (X) - red
    btn_x -= 18;
    draw_filled_rect(btn_x, btn_y, 16, 16, 0xFFCC4444);
    draw_text(btn_x + 4, btn_y + 2, "X", 0xFFFFFFFF);

    // Maximize button ([]) - gray
    btn_x -= 20;
    draw_filled_rect(btn_x, btn_y, 16, 16, 0xFF888888);
    draw_rect(btn_x + 3, btn_y + 3, 10, 10, 0xFFFFFFFF);

    // Minimize button (_) - gray
    btn_x -= 20;
    draw_filled_rect(btn_x, btn_y, 16, 16, 0xFF888888);
    draw_hline(btn_x + 3, btn_x + 13, btn_y + 12, 0xFFFFFFFF);
}
```

---

## Phase 3: Desktop Shell

**Priority**: Medium
**Estimated Complexity**: Medium
**Depends on**: Phase 1, Phase 2

### 3.1 Taskbar

Create `user/taskbar/main.cpp`:

```cpp
// Taskbar configuration
constexpr int TASKBAR_HEIGHT = 32;
constexpr int BUTTON_WIDTH = 150;
constexpr int START_BUTTON_WIDTH = 80;

struct TaskbarButton {
    uint32_t surface_id;
    char title[32];
    bool active;
};

TaskbarButton buttons[MAX_SURFACES];
int button_count = 0;

void draw_taskbar() {
    gui_fill_rect(win, 0, 0, screen_width, TASKBAR_HEIGHT, 0xFF303030);

    // Start button
    uint32_t start_color = start_menu_open ? 0xFF4080C0 : 0xFF404040;
    gui_fill_rect(win, 2, 2, START_BUTTON_WIDTH, TASKBAR_HEIGHT - 4, start_color);
    gui_draw_text(win, 10, 8, "Start", 0xFFFFFFFF);

    // Window buttons
    int x = START_BUTTON_WIDTH + 10;
    for (int i = 0; i < button_count; i++) {
        uint32_t btn_color = buttons[i].active ? 0xFF4080C0 : 0xFF505050;
        gui_fill_rect(win, x, 2, BUTTON_WIDTH, TASKBAR_HEIGHT - 4, btn_color);
        gui_draw_text(win, x + 5, 8, buttons[i].title, 0xFFFFFFFF);
        x += BUTTON_WIDTH + 4;
    }

    // Clock (right side)
    char time_str[16];
    get_time_string(time_str);
    gui_draw_text(win, screen_width - 60, 8, time_str, 0xFFFFFFFF);

    gui_present(win);
}

void handle_taskbar_click(int x, int y) {
    if (x < START_BUTTON_WIDTH + 5) {
        toggle_start_menu();
        return;
    }

    // Find clicked window button
    int btn_x = START_BUTTON_WIDTH + 10;
    for (int i = 0; i < button_count; i++) {
        if (x >= btn_x && x < btn_x + BUTTON_WIDTH) {
            activate_window(buttons[i].surface_id);
            return;
        }
        btn_x += BUTTON_WIDTH + 4;
    }
}
```

### 3.2 Start Menu / Application Launcher

```cpp
struct MenuItem {
    const char* name;
    const char* path;
    const char* icon;  // Future: icon path
};

MenuItem menu_items[] = {
    {"Terminal", "/c/terminal.prg", nullptr},
    {"Editor", "/c/gui_edit.prg", nullptr},
    {"File Manager", "/c/files.prg", nullptr},
    {"Settings", "/c/settings.prg", nullptr},
    {"---", nullptr, nullptr},  // Separator
    {"Shutdown", nullptr, nullptr},
};

void draw_start_menu() {
    if (!start_menu_open) return;

    int menu_width = 200;
    int menu_height = (sizeof(menu_items) / sizeof(menu_items[0])) * 24 + 8;
    int menu_x = 2;
    int menu_y = screen_height - TASKBAR_HEIGHT - menu_height;

    // Menu background
    gui_fill_rect(menu_win, 0, 0, menu_width, menu_height, 0xFF404040);
    gui_draw_rect(menu_win, 0, 0, menu_width, menu_height, 0xFF606060);

    // Menu items
    int y = 4;
    for (auto& item : menu_items) {
        if (item.name[0] == '-') {
            // Separator
            gui_draw_hline(menu_win, 4, menu_width - 4, y + 10, 0xFF606060);
        } else {
            uint32_t bg = (hover_index == &item - menu_items) ? 0xFF5080B0 : 0xFF404040;
            gui_fill_rect(menu_win, 2, y, menu_width - 4, 22, bg);
            gui_draw_text(menu_win, 10, y + 4, item.name, 0xFFFFFFFF);
        }
        y += 24;
    }

    gui_present(menu_win);
}

void handle_menu_click(int index) {
    if (index < 0) return;

    MenuItem& item = menu_items[index];
    if (item.path) {
        // Launch application
        spawn_process(item.path);
    } else if (strcmp(item.name, "Shutdown") == 0) {
        shutdown_system();
    }

    start_menu_open = false;
    draw_start_menu();
}
```

### 3.3 Alt+Tab Window Switcher

Handle in displayd:

```cpp
bool alt_tab_active = false;
int alt_tab_index = 0;

void handle_alt_tab() {
    if (!alt_tab_active) {
        alt_tab_active = true;
        alt_tab_index = 0;
        draw_window_switcher();
    } else {
        alt_tab_index = (alt_tab_index + 1) % visible_window_count;
        draw_window_switcher();
    }
}

void handle_alt_release() {
    if (alt_tab_active) {
        alt_tab_active = false;
        // Bring selected window to front
        Surface* s = get_window_at_index(alt_tab_index);
        if (s) {
            bring_to_front(s);
            focus_window(s);
        }
        recomposite();
    }
}

void draw_window_switcher() {
    // Overlay showing all windows as thumbnails
    int sw_width = 400;
    int sw_height = 80;
    int sw_x = (screen_width - sw_width) / 2;
    int sw_y = (screen_height - sw_height) / 2;

    // Draw background
    fill_rect(sw_x, sw_y, sw_width, sw_height, 0xE0303030);

    // Draw window previews
    int x = sw_x + 10;
    for (int i = 0; i < visible_window_count; i++) {
        Surface* s = get_visible_window(i);

        // Highlight selected
        uint32_t border = (i == alt_tab_index) ? 0xFF4080C0 : 0xFF505050;
        draw_rect(x - 2, sw_y + 8, 64, 48, border);

        // Mini preview (scaled down)
        draw_scaled_surface(s, x, sw_y + 10, 60, 44);

        // Title below
        draw_text_centered(x + 30, sw_y + 60, s->title, 0xFFFFFFFF);

        x += 80;
    }
}
```

### 3.4 Desktop Background

Add wallpaper support:

```cpp
uint32_t* wallpaper_pixels = nullptr;
uint32_t wallpaper_width = 0;
uint32_t wallpaper_height = 0;

bool load_wallpaper(const char* path) {
    // Load BMP file (simple format to start)
    // Future: support PNG via stb_image

    int fd = sys::open(path, O_RDONLY);
    if (fd < 0) return false;

    // Parse BMP header
    BMPHeader header;
    sys::read(fd, &header, sizeof(header));

    wallpaper_width = header.width;
    wallpaper_height = header.height;
    wallpaper_pixels = new uint32_t[wallpaper_width * wallpaper_height];

    // Read pixel data (BMP is bottom-up)
    for (int y = wallpaper_height - 1; y >= 0; y--) {
        sys::read(fd, &wallpaper_pixels[y * wallpaper_width],
                  wallpaper_width * 4);
    }

    sys::close(fd);
    return true;
}

void draw_desktop_background() {
    if (wallpaper_pixels) {
        // Scale/tile wallpaper to fill screen
        blit_scaled(wallpaper_pixels, wallpaper_width, wallpaper_height,
                   framebuffer, screen_width, screen_height);
    } else {
        // Solid color fallback
        fill_rect(0, 0, screen_width, screen_height, 0xFF2D5A88);
    }
}
```

---

## Phase 4: Widget Toolkit

**Priority**: Medium
**Estimated Complexity**: High
**Depends on**: Phase 1

### 4.1 Widget Base Class

Create `user/libwidget/widget.hpp`:

```cpp
namespace widget {

class Widget {
public:
    Widget(Widget* parent = nullptr);
    virtual ~Widget();

    // Geometry
    void set_geometry(int x, int y, int width, int height);
    void set_size(int width, int height);
    void set_position(int x, int y);
    int x() const { return m_x; }
    int y() const { return m_y; }
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Hierarchy
    Widget* parent() const { return m_parent; }
    const std::vector<Widget*>& children() const { return m_children; }
    void add_child(Widget* child);
    void remove_child(Widget* child);

    // Rendering
    virtual void paint(gui_window_t* win);
    void repaint();

    // Events
    virtual void mouse_press(int x, int y, int button);
    virtual void mouse_release(int x, int y, int button);
    virtual void mouse_move(int x, int y);
    virtual void key_press(int keycode, char character);
    virtual void focus_in();
    virtual void focus_out();

    // State
    bool is_visible() const { return m_visible; }
    void set_visible(bool v);
    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool e);
    bool has_focus() const { return m_focused; }

protected:
    Widget* m_parent;
    std::vector<Widget*> m_children;
    int m_x, m_y, m_width, m_height;
    bool m_visible = true;
    bool m_enabled = true;
    bool m_focused = false;
};

} // namespace widget
```

### 4.2 Common Widgets

#### Button

```cpp
class Button : public Widget {
public:
    Button(const std::string& text, Widget* parent = nullptr);

    void set_text(const std::string& text);
    const std::string& text() const { return m_text; }

    // Callback for click
    std::function<void()> on_click;

    void paint(gui_window_t* win) override;
    void mouse_press(int x, int y, int button) override;
    void mouse_release(int x, int y, int button) override;

private:
    std::string m_text;
    bool m_pressed = false;
};

void Button::paint(gui_window_t* win) {
    uint32_t bg = m_pressed ? 0xFF505050 : (m_enabled ? 0xFF707070 : 0xFF404040);
    uint32_t fg = m_enabled ? 0xFFFFFFFF : 0xFF909090;

    // 3D effect
    gui_fill_rect(win, m_x, m_y, m_width, m_height, bg);

    if (!m_pressed) {
        // Highlight (top/left)
        gui_draw_hline(win, m_x, m_x + m_width - 1, m_y, 0xFFAAAAAA);
        gui_draw_vline(win, m_x, m_y, m_y + m_height - 1, 0xFFAAAAAA);
        // Shadow (bottom/right)
        gui_draw_hline(win, m_x, m_x + m_width - 1, m_y + m_height - 1, 0xFF404040);
        gui_draw_vline(win, m_x + m_width - 1, m_y, m_y + m_height - 1, 0xFF404040);
    }

    // Center text
    int text_w = m_text.length() * 8;
    int text_x = m_x + (m_width - text_w) / 2;
    int text_y = m_y + (m_height - 8) / 2;
    gui_draw_text(win, text_x, text_y, m_text.c_str(), fg);
}
```

#### Label

```cpp
class Label : public Widget {
public:
    Label(const std::string& text, Widget* parent = nullptr);

    void set_text(const std::string& text);
    void set_color(uint32_t color) { m_color = color; }
    void set_alignment(Alignment align) { m_align = align; }

    void paint(gui_window_t* win) override;

private:
    std::string m_text;
    uint32_t m_color = 0xFF000000;
    Alignment m_align = Alignment::Left;
};
```

#### TextBox

```cpp
class TextBox : public Widget {
public:
    TextBox(Widget* parent = nullptr);

    void set_text(const std::string& text);
    const std::string& text() const { return m_text; }

    void set_placeholder(const std::string& placeholder);
    void set_password_mode(bool enabled);

    std::function<void(const std::string&)> on_change;
    std::function<void()> on_enter;

    void paint(gui_window_t* win) override;
    void key_press(int keycode, char character) override;
    void mouse_press(int x, int y, int button) override;
    void focus_in() override;
    void focus_out() override;

private:
    std::string m_text;
    std::string m_placeholder;
    int m_cursor_pos = 0;
    int m_scroll_offset = 0;
    bool m_password_mode = false;
    bool m_cursor_visible = true;
};
```

#### ListView

```cpp
class ListView : public Widget {
public:
    ListView(Widget* parent = nullptr);

    void add_item(const std::string& text);
    void remove_item(int index);
    void clear();

    int selected_index() const { return m_selected; }
    void set_selected(int index);

    std::function<void(int)> on_selection_change;
    std::function<void(int)> on_double_click;

    void paint(gui_window_t* win) override;
    void mouse_press(int x, int y, int button) override;
    void key_press(int keycode, char character) override;

private:
    std::vector<std::string> m_items;
    int m_selected = -1;
    int m_scroll_offset = 0;
};
```

### 4.3 Layout System

```cpp
enum class LayoutType {
    None,       // Manual positioning
    Horizontal, // Left to right
    Vertical,   // Top to bottom
    Grid,       // Row/column grid
};

class Layout {
public:
    Layout(LayoutType type);

    void add_widget(Widget* w, int row = 0, int col = 0);
    void set_spacing(int spacing) { m_spacing = spacing; }
    void set_margins(int left, int top, int right, int bottom);

    void apply(Widget* container);

private:
    LayoutType m_type;
    int m_spacing = 5;
    int m_margin_left = 5, m_margin_top = 5;
    int m_margin_right = 5, m_margin_bottom = 5;
    std::vector<std::pair<Widget*, std::pair<int,int>>> m_widgets;
};

void Layout::apply(Widget* container) {
    int x = m_margin_left;
    int y = m_margin_top;
    int max_h = 0;

    for (auto& [widget, pos] : m_widgets) {
        switch (m_type) {
            case LayoutType::Horizontal:
                widget->set_position(x, y);
                x += widget->width() + m_spacing;
                max_h = std::max(max_h, widget->height());
                break;

            case LayoutType::Vertical:
                widget->set_position(x, y);
                y += widget->height() + m_spacing;
                break;

            // ... other layouts
        }
    }
}
```

### 4.4 Dialog Boxes

```cpp
class Dialog : public Widget {
public:
    Dialog(const std::string& title, Widget* parent = nullptr);

    enum Result { Accepted, Rejected };

    void set_title(const std::string& title);
    Result exec();  // Modal execution

    void accept();
    void reject();

protected:
    virtual void setup_ui() = 0;

private:
    std::string m_title;
    Result m_result;
    bool m_running = false;
};

// Convenience dialogs
class MessageBox : public Dialog {
public:
    enum Type { Info, Warning, Error, Question };

    static void show(const std::string& title, const std::string& message, Type type);
    static bool ask(const std::string& title, const std::string& question);
};

class FileDialog : public Dialog {
public:
    enum Mode { Open, Save, SelectFolder };

    static std::string get_open_filename(const std::string& filter = "*");
    static std::string get_save_filename(const std::string& filter = "*");
    static std::string get_directory();
};
```

---

## Phase 5: Desktop Applications

**Priority**: Low
**Estimated Complexity**: Varies
**Depends on**: Phases 1-4

### 5.1 Terminal Emulator

`user/terminal/main.cpp`:

Features:

- VT100/ANSI escape sequence parsing
- Scrollback buffer (reuse gcon logic)
- Copy/paste support
- Font size adjustment
- Shell integration (run vinit shell)

### 5.2 GUI Text Editor

`user/gui_edit/main.cpp`:

Features:

- Syntax highlighting (optional)
- Line numbers
- Find/replace
- File open/save dialogs
- Multiple tabs (future)

### 5.3 File Manager

`user/files/main.cpp`:

Features:

- Directory listing (ListView)
- Icon view (future)
- File operations (copy, move, delete)
- Context menu
- Address bar
- Back/forward navigation

### 5.4 Settings Application

`user/settings/main.cpp`:

Features:

- Display settings (resolution, wallpaper)
- Sound settings (future)
- Network settings
- Date/time
- User preferences

---

## Implementation Order

```
Week 1-2: Phase 1 (Event Delivery)
├── Day 1-2: Event structures and protocol
├── Day 3-4: displayd event queue and routing
├── Day 5-6: libgui event API
└── Day 7: Testing and bug fixes

Week 3: Phase 2 (Window Management)
├── Day 1-2: Z-order management
├── Day 3-4: Window resizing
└── Day 5: Minimize/maximize

Week 4-5: Phase 3 (Desktop Shell)
├── Day 1-3: Taskbar implementation
├── Day 4-5: Start menu/launcher
├── Day 6-7: Alt+Tab switcher
└── Day 8: Desktop background

Week 6-8: Phase 4 (Widget Toolkit)
├── Week 6: Base classes, Button, Label, TextBox
├── Week 7: ListView, Layout system
└── Week 8: Dialogs, refinement

Week 9+: Phase 5 (Applications)
├── Terminal emulator
├── File manager
└── Settings app
```

---

## File Modifications Summary

### New Files

| File                          | Purpose               |
|-------------------------------|-----------------------|
| `user/taskbar/main.cpp`       | Taskbar application   |
| `user/taskbar/CMakeLists.txt` | Build config          |
| `user/libwidget/widget.hpp`   | Widget base class     |
| `user/libwidget/widget.cpp`   | Widget implementation |
| `user/libwidget/button.cpp`   | Button widget         |
| `user/libwidget/textbox.cpp`  | TextBox widget        |
| `user/libwidget/listview.cpp` | ListView widget       |
| `user/libwidget/layout.cpp`   | Layout system         |
| `user/libwidget/dialog.cpp`   | Dialog boxes          |
| `user/terminal/main.cpp`      | Terminal emulator     |
| `user/files/main.cpp`         | File manager          |
| `user/settings/main.cpp`      | Settings app          |
| `user/gui_edit/main.cpp`      | GUI text editor       |

### Modified Files

| File                                         | Changes                        |
|----------------------------------------------|--------------------------------|
| `user/servers/displayd/main.cpp`             | Event queues, routing, z-order |
| `user/servers/displayd/display_protocol.hpp` | Event protocol                 |
| `user/libgui/include/gui.h`                  | Event API                      |
| `user/libgui/src/gui.cpp`                    | Event implementation           |
| `CMakeLists.txt`                             | New build targets              |

---

## Testing Checklist

### Phase 1

- [ ] Mouse click events delivered to windows
- [ ] Mouse move events delivered to focused window
- [ ] Keyboard events delivered to focused window
- [ ] Focus change events on window click
- [ ] Close event on close button click
- [ ] Multiple windows receive independent events

### Phase 2

- [ ] Click on window brings to front
- [ ] Window dragging works
- [ ] Window resizing works from all edges
- [ ] Minimize hides window
- [ ] Maximize fills screen
- [ ] Restore returns to previous size

### Phase 3

- [ ] Taskbar displays running windows
- [ ] Clicking taskbar button activates window
- [ ] Start menu opens on click
- [ ] Applications launch from start menu
- [ ] Alt+Tab cycles through windows
- [ ] Desktop background displays

### Phase 4

- [ ] Button click callback fires
- [ ] TextBox accepts keyboard input
- [ ] ListView selection works
- [ ] Layouts position widgets correctly
- [ ] Dialogs display modally

### Phase 5

- [ ] Terminal runs shell commands
- [ ] File manager lists directories
- [ ] Settings changes persist

---

## Performance Considerations

1. **Damage Tracking**: Only recomposite changed regions
2. **Double Buffering**: Prevent tearing on updates
3. **Event Coalescing**: Merge rapid mouse moves
4. **Lazy Rendering**: Don't repaint hidden windows
5. **Shared Memory**: Zero-copy pixel buffers (already implemented)

---

## Future Enhancements

- Multi-monitor support
- Hardware cursor (VirtIO-GPU)
- Font rendering (FreeType)
- Theme engine
- Drag-and-drop
- Clipboard (copy/paste between apps)
- Sound notifications
- System tray
- Accessibility features
