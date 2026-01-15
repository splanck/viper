# GUI Subsystem

**Status:** Complete (desktop shell framework operational)
**Location:** `user/servers/displayd/`, `user/libgui/`, `user/taskbar/`, `user/servers/consoled/`
**SLOC:** ~4,800

## Overview

ViperDOS provides a complete user-space GUI subsystem consisting of:

| Component | Location | SLOC | Purpose |
|-----------|----------|------|---------|
| **displayd** | `user/servers/displayd/` | ~1,500 | Display server and window compositor |
| **consoled** | `user/servers/consoled/` | ~1,600 | GUI terminal emulator |
| **libgui** | `user/libgui/` | ~1,300 | Client library for GUI applications |
| **taskbar** | `user/taskbar/` | ~230 | Desktop shell taskbar |
| **hello_gui** | `user/hello_gui/` | ~100 | Demo GUI application |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     GUI Applications                             │
│   hello_gui  │  taskbar  │  consoled (terminal emulator)        │
└───────────────────────────┬─────────────────────────────────────┘
                            │ libgui API
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                        libgui                                    │
│   gui_create_window() │ gui_present() │ gui_poll_event()        │
│   gui_fill_rect() │ gui_draw_char_scaled() │ gui_list_windows() │
└───────────────────────────┬─────────────────────────────────────┘
                            │ IPC (Channels) + Shared Memory
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                       displayd                                   │
│   Surface management │ Window compositing │ Decoration drawing  │
│   Z-ordering │ Focus tracking │ Event routing │ Cursor          │
└───────────────────────────┬─────────────────────────────────────┘
                            │ MAP_FRAMEBUFFER syscall
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Kernel Framebuffer                            │
│              ramfb / VirtIO-GPU (via QEMU)                      │
└─────────────────────────────────────────────────────────────────┘

consoled acts as both a GUI application (via libgui) and a server for
console clients (vinit). It receives keyboard events from displayd and
forwards them to connected clients via IPC.
```

---

## Display Server (displayd)

**Location:** `user/servers/displayd/`
**Registration:** `sys::assign_set("DISPLAY", channel_handle)`

### Files

| File | Lines | Description |
|------|-------|-------------|
| `main.cpp` | ~1,460 | Server entry, compositing, event handling |
| `display_protocol.hpp` | ~260 | IPC message definitions |

### Features

#### Surface Management
- Up to 32 concurrent surfaces
- Shared memory pixel buffers (zero-copy rendering)
- XRGB8888 pixel format (32-bit color)
- Cascade positioning for new windows

#### Window Decorations
- 24px title bar with window title
- 2px border around content area
- Minimize button (blue `_`)
- Maximize button (green `M`/`R`)
- Close button (red `X`)
- Focused/unfocused color states

#### Compositing
- Back-to-front z-order sorting
- Blue desktop background (#2D5A88) with green Viper border
- Full-surface compositing on present
- Software cursor with background save/restore

#### Event System
- Per-surface event queues (32 events each)
- Event types: key, mouse, focus, close
- Keyboard events routed from inputd to focused window
- Non-blocking poll via DISP_POLL_EVENT
- Click detection for decoration buttons

#### Window States
- Visible/hidden
- Minimized (excluded from compositing)
- Maximized (moves to top-left corner)
- Focused (highlighted title bar)

#### Window Interactions
- Title bar drag to move windows
- Resize handles on edges/corners (visual only - buffer not reallocated)
- Click to focus and bring to front

#### Surface Flags
```cpp
enum SurfaceFlags : uint32_t {
    SURFACE_FLAG_NONE = 0,
    SURFACE_FLAG_SYSTEM = 1,           // System surface (not in window list)
    SURFACE_FLAG_NO_DECORATIONS = 2,   // No title bar or borders
};
```

### IPC Protocol

```cpp
namespace display_protocol {
    enum MsgType : uint32_t {
        // Client requests
        DISP_GET_INFO = 1,          // Query display resolution
        DISP_CREATE_SURFACE = 2,    // Create window surface
        DISP_DESTROY_SURFACE = 3,   // Release surface
        DISP_PRESENT = 4,           // Update display
        DISP_SET_GEOMETRY = 5,      // Move surface
        DISP_SET_VISIBLE = 6,       // Show/hide surface
        DISP_SET_TITLE = 7,         // Set window title
        DISP_SUBSCRIBE_EVENTS = 10, // Subscribe to events
        DISP_POLL_EVENT = 11,       // Poll for events
        DISP_LIST_WINDOWS = 12,     // List windows (for taskbar)
        DISP_RESTORE_WINDOW = 13,   // Restore/focus window

        // Replies
        DISP_INFO_REPLY = 0x81,
        DISP_CREATE_SURFACE_REPLY = 0x82,
        DISP_GENERIC_REPLY = 0x83,
        DISP_POLL_EVENT_REPLY = 0x84,
        DISP_LIST_WINDOWS_REPLY = 0x85,

        // Events (server -> client)
        DISP_EVENT_KEY = 0x90,
        DISP_EVENT_MOUSE = 0x91,
        DISP_EVENT_FOCUS = 0x92,
        DISP_EVENT_CLOSE = 0x93,
    };
}
```

### Key Structures

```cpp
// Surface creation request
struct CreateSurfaceRequest {
    uint32_t type;      // DISP_CREATE_SURFACE
    uint32_t request_id;
    uint32_t width;
    uint32_t height;
    uint32_t flags;     // SurfaceFlags
    char title[64];
};

// Surface creation reply (includes shared memory handle)
struct CreateSurfaceReply {
    uint32_t type;       // DISP_CREATE_SURFACE_REPLY
    uint32_t request_id;
    int32_t status;      // 0 = success
    uint32_t surface_id;
    uint32_t stride;     // Bytes per row
    // handle[0] = shared memory handle for pixel buffer
};

// Window info for taskbar
struct WindowInfo {
    uint32_t surface_id;
    uint32_t flags;
    uint8_t minimized;
    uint8_t maximized;
    uint8_t focused;
    uint8_t _pad;
    char title[64];
};

// List windows reply
struct ListWindowsReply {
    uint32_t type;
    uint32_t request_id;
    int32_t status;
    uint32_t window_count;
    WindowInfo windows[16];  // Max 16 windows
};
```

### Internal State

```cpp
struct Surface {
    uint32_t id;
    uint32_t width, height, stride;
    int32_t x, y;
    bool visible, in_use;
    uint32_t shm_handle;
    uint32_t *pixels;
    char title[64];
    int32_t client_channel;
    EventQueue event_queue;
    uint32_t z_order;        // Higher = on top
    uint32_t flags;
    bool minimized, maximized;
    int32_t saved_x, saved_y;  // For restore from maximize
};

static Surface g_surfaces[32];
static uint32_t g_focused_surface;
static uint32_t g_next_z_order;
```

---

## Client Library (libgui)

**Location:** `user/libgui/`
**Purpose:** C API for GUI applications

### Files

| File | Lines | Description |
|------|-------|-------------|
| `include/gui.h` | ~400 | Public API declarations |
| `src/gui.cpp` | ~940 | Implementation with built-in 8x8 font |

### Initialization

```c
int gui_init(void);                          // Connect to displayd
void gui_shutdown(void);                     // Disconnect
int gui_get_display_info(gui_display_info_t *info);  // Get resolution
```

### Window Management

```c
// Create window (with or without flags)
gui_window_t *gui_create_window(const char *title, uint32_t w, uint32_t h);
gui_window_t *gui_create_window_ex(const char *title, uint32_t w, uint32_t h,
                                    uint32_t flags);

// Destroy window
void gui_destroy_window(gui_window_t *win);

// Window properties
void gui_set_title(gui_window_t *win, const char *title);
const char *gui_get_title(gui_window_t *win);
void gui_set_position(gui_window_t *win, int32_t x, int32_t y);

// Window list (for taskbar)
int gui_list_windows(gui_window_list_t *list);
int gui_restore_window(uint32_t surface_id);
```

### Pixel Buffer Access

```c
uint32_t *gui_get_pixels(gui_window_t *win);  // Direct XRGB8888 buffer
uint32_t gui_get_width(gui_window_t *win);
uint32_t gui_get_height(gui_window_t *win);
uint32_t gui_get_stride(gui_window_t *win);   // Bytes per row
```

### Display Update

```c
void gui_present(gui_window_t *win);  // Full surface update
void gui_present_region(gui_window_t *win, uint32_t x, uint32_t y,
                        uint32_t w, uint32_t h);  // Partial update
```

### Events

```c
int gui_poll_event(gui_window_t *win, gui_event_t *event);  // Non-blocking
int gui_wait_event(gui_window_t *win, gui_event_t *event);  // Blocking
```

### Drawing Helpers

```c
// Rectangles
void gui_fill_rect(gui_window_t *win, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color);
void gui_draw_rect(gui_window_t *win, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color);

// Lines
void gui_draw_hline(gui_window_t *win, uint32_t x1, uint32_t x2,
                    uint32_t y, uint32_t color);
void gui_draw_vline(gui_window_t *win, uint32_t x, uint32_t y1,
                    uint32_t y2, uint32_t color);

// Text (uses built-in 8x8 bitmap font)
void gui_draw_text(gui_window_t *win, uint32_t x, uint32_t y,
                   const char *text, uint32_t color);

// Character drawing with foreground and background colors (8x8 cell)
void gui_draw_char(gui_window_t *win, uint32_t x, uint32_t y,
                   char c, uint32_t fg, uint32_t bg);

// Scaled character drawing (supports fractional sizes)
// scale is in half-units: 2=1x (8x8), 3=1.5x (12x12), 4=2x (16x16)
void gui_draw_char_scaled(gui_window_t *win, uint32_t x, uint32_t y,
                          char c, uint32_t fg, uint32_t bg, uint32_t scale);
```

The `gui_draw_char_scaled()` function uses a half-unit scaling system to support fractional font sizes:

| Scale Value | Multiplier | Cell Size | Use Case |
|-------------|------------|-----------|----------|
| 2 | 1x | 8x8 | Small text, dense displays |
| 3 | 1.5x | 12x12 | Console terminal (default) |
| 4 | 2x | 16x16 | Large text, high-DPI |
| 6 | 3x | 24x24 | Very large text |

Scaling uses nearest-neighbor interpolation with source pixel lookup:
```cpp
uint32_t dest_size = 8 * scale / 2;  // Destination cell size
for (uint32_t dy = 0; dy < dest_size; dy++) {
    uint32_t src_row = dy * 2 / scale;  // Map dest row to source
    // ...
}
```

### Event Types

```c
typedef enum {
    GUI_EVENT_NONE = 0,
    GUI_EVENT_KEY,      // Keyboard event
    GUI_EVENT_MOUSE,    // Mouse move/button
    GUI_EVENT_FOCUS,    // Focus change
    GUI_EVENT_RESIZE,   // Window resized
    GUI_EVENT_CLOSE,    // Close button clicked
} gui_event_type_t;

typedef struct {
    gui_event_type_t type;
    union {
        gui_key_event_t key;
        gui_mouse_event_t mouse;
        gui_focus_event_t focus;
        gui_resize_event_t resize;
    };
} gui_event_t;
```

### Surface Flags

```c
typedef enum {
    GUI_FLAG_NONE = 0,
    GUI_FLAG_SYSTEM = 1,           // System surface (taskbar)
    GUI_FLAG_NO_DECORATIONS = 2,   // No title bar/borders
} gui_surface_flags_t;
```

---

## Taskbar Application

**Location:** `user/taskbar/`
**Purpose:** Desktop shell component

### Features

- 32px height at screen bottom
- Dark blue-gray background (#303050)
- Lists all non-system windows
- Click to restore/focus windows
- Visual feedback:
  - Blue highlight for focused window
  - Dimmer color for minimized windows
  - White text (gray for minimized)
- Periodic refresh of window list

### Implementation

```c
// Create system surface (no decorations, not in window list)
g_taskbar = gui_create_window_ex("Taskbar", g_screen_width, TASKBAR_HEIGHT,
                                  GUI_FLAG_SYSTEM | GUI_FLAG_NO_DECORATIONS);

// Position at bottom of screen
gui_set_position(g_taskbar, 0, g_screen_height - TASKBAR_HEIGHT);

// Event loop
while (1) {
    gui_event_t event;
    if (gui_poll_event(g_taskbar, &event) == 0) {
        if (event.type == GUI_EVENT_MOUSE && event.mouse.event_type == 1) {
            handle_click(event.mouse.x, event.mouse.y);
        }
    }

    // Periodic refresh
    if (++update_counter >= 50) {
        redraw_taskbar();
        update_counter = 0;
    }

    sys_yield();
}
```

### Button Layout

```
┌──────────────────────────────────────────────────────────────────┐
│ [Window 1] [Window 2] [Window 3]                                 │
└──────────────────────────────────────────────────────────────────┘
  ↑ 120px    ↑ 4px spacing
```

---

## Demo Application (hello_gui)

**Location:** `user/hello_gui/`
**Purpose:** Demonstrates libgui usage

```c
int main(void) {
    gui_init();

    gui_window_t *win = gui_create_window("Hello GUI", 400, 300);

    // Fill with color
    gui_fill_rect(win, 0, 0, 400, 300, 0xFF4080C0);

    // Draw text
    gui_draw_text(win, 100, 140, "Hello from ViperDOS!", 0xFFFFFFFF);

    gui_present(win);

    // Event loop
    while (1) {
        gui_event_t ev;
        if (gui_poll_event(win, &ev) == 0) {
            if (ev.type == GUI_EVENT_CLOSE) break;
        }
        sys_yield();
    }

    gui_destroy_window(win);
    gui_shutdown();
    return 0;
}
```

---

## Shared Memory IPC Pattern

The GUI system uses shared memory for efficient pixel buffer access:

```
1. Client calls gui_create_window()
2. libgui sends DISP_CREATE_SURFACE to displayd
3. displayd allocates shared memory via SYS_SHM_CREATE
4. displayd sends SHM handle back to client
5. libgui maps shared memory via SYS_SHM_MAP
6. Client writes directly to pixel buffer
7. Client calls gui_present() to signal compositor
8. displayd reads shared memory and composites
```

This provides zero-copy rendering - no pixel data is copied between client and server.

---

## Compositing Algorithm

```cpp
static void composite() {
    // 1. Clear to desktop background
    fill_rect(0, 0, g_fb_width, g_fb_height, COLOR_DESKTOP);

    // 2. Collect visible, non-minimized surfaces
    Surface *sorted[MAX_SURFACES];
    uint32_t count = 0;
    for (auto &surf : g_surfaces) {
        if (surf.in_use && surf.visible && !surf.minimized)
            sorted[count++] = &surf;
    }

    // 3. Sort by z-order (lowest first = drawn under)
    insertion_sort(sorted, count, by_z_order);

    // 4. Draw back-to-front
    for (uint32_t i = 0; i < count; i++) {
        draw_window_decorations(sorted[i]);  // Title bar, buttons
        blit_surface_content(sorted[i]);      // Pixel buffer
    }

    // 5. Draw cursor on top
    save_cursor_background();
    draw_cursor();
}
```

---

## Window Decoration Constants

```cpp
static constexpr uint32_t TITLE_BAR_HEIGHT = 24;
static constexpr uint32_t BORDER_WIDTH = 2;
static constexpr uint32_t CLOSE_BUTTON_SIZE = 16;

// Colors
static constexpr uint32_t COLOR_DESKTOP = 0xFF2D5A88;       // Blue
static constexpr uint32_t COLOR_TITLE_FOCUSED = 0xFF4080C0;
static constexpr uint32_t COLOR_TITLE_UNFOCUSED = 0xFF606060;
static constexpr uint32_t COLOR_BORDER = 0xFF303030;
static constexpr uint32_t COLOR_CLOSE_BTN = 0xFFCC4444;     // Red
static constexpr uint32_t COLOR_MIN_BTN = 0xFF4040C0;       // Blue
static constexpr uint32_t COLOR_MAX_BTN = 0xFF40C040;       // Green
```

---

## Building GUI Applications

### CMakeLists.txt Helper Function

```cmake
function(add_gui_program name)
    add_executable(${name}.prg ${ARGN})
    target_compile_options(${name}.prg PRIVATE ${USER_COMPILE_OPTIONS})
    target_link_options(${name}.prg PRIVATE ${USER_LINK_OPTIONS})
    target_include_directories(${name}.prg PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/libgui/include
        ${CMAKE_CURRENT_SOURCE_DIR}/libc/include
    )
    target_link_libraries(${name}.prg PRIVATE vipergui viperlibc)
endfunction()

# Usage
add_gui_program(hello_gui hello_gui/hello_gui.c)
add_gui_program(taskbar taskbar/taskbar.c)
```

---

## Current Limitations

### Not Yet Implemented
- True window resize (buffer reallocation on resize)
- Alt+Tab window switching
- Desktop icons
- Right-click context menus
- Clipboard support
- Multiple monitors
- Damage region tracking (partial compositing)

### Known Issues
- Maximize only moves window (doesn't resize buffer to fill screen)
- Window resize is visual-only (frame moves but buffer stays same size)
- No window minimum size constraints exposed to clients
- Cursor flicker during fast movement (no double-buffering)

---

## Priority Recommendations: Next 5 Steps

### 1. True Window Resize
**Impact:** User-adjustable window sizes
- Add DISP_RESIZE_SURFACE message type
- Reallocate shared memory for new size
- Send GUI_EVENT_RESIZE to client
- Client remaps buffer and redraws

### 3. Application Launcher
**Impact:** Program launching from GUI
- Add launcher button to taskbar
- Show menu of available programs from filesystem
- Spawn selected program via vinit

### 4. Desktop Background Image
**Impact:** Visual polish
- Load image from filesystem
- Scale/tile to screen size
- Draw before windows in compositor

### 5. Cursor Type Changes
**Impact:** Visual feedback for resize handles
- Define resize cursor bitmaps (arrows)
- Change cursor when over window edges
- Indicate draggable regions to user

---

## Version History

- **January 2026**: Consoled GUI terminal and font scaling
  - Added `gui_draw_char()` for character drawing with fg/bg colors
  - Added `gui_draw_char_scaled()` with half-unit fractional scaling (1.5x, 2x, etc.)
  - Consoled now runs as a GUI window (via libgui/displayd)
  - Consoled uses 1.5x font scaling (12x12 pixels) for better readability
  - Full ANSI escape sequence support in consoled terminal
  - Bidirectional IPC: consoled forwards keyboard events to connected clients

- **January 2026**: GUI improvements
  - Implemented keyboard event routing (inputd → displayd → focused window)
  - Implemented DISP_SET_TITLE handler (window title updates at runtime)
  - Added event queue overflow detection
  - Improved window cascade positioning (10 positions before repeating)
  - Added green Viper border around desktop (matches console theme)
  - Updated hello_gui to display key events

- **January 2026**: Desktop shell framework
  - Taskbar with window list
  - Per-surface event queues
  - Minimize/maximize/close buttons
  - Z-ordering for window stacking
  - Surface flags (SYSTEM, NO_DECORATIONS)
  - Window list protocol for taskbar
  - Title bar drag for window moving
  - Resize handles (visual only)

- **January 2026**: Initial GUI implementation
  - displayd server with compositing
  - libgui client library
  - Shared memory pixel buffers
  - Window decorations
  - Mouse cursor
  - hello_gui demo application
