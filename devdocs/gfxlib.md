---
status: draft
last-updated: 2025-11-20
version: 1.0.0
---

# ViperGFX – Cross-Platform Software 2D Graphics Library

**Specification Version:** 1.0.0
**Target Release:** Phase 1 (Core Features)
**Status:** 🟡 In Specification

---

## Progress Tracker

### Phase 1A: Core Infrastructure (macOS)
- [ ] Project structure & CMake scaffolding
- [ ] Public API header (`include/vgfx.h`)
- [ ] Internal structures (`src/vgfx_internal.h`)
- [ ] macOS Cocoa backend (`src/vgfx_platform_macos.m`)
  - [ ] Window creation
  - [ ] Event loop integration
  - [ ] Framebuffer-to-window blitting
- [ ] Core implementation (`src/vgfx.c`)
  - [ ] Framebuffer allocation
  - [ ] `vgfx_pset`, `vgfx_point`, `vgfx_cls`
  - [ ] `vgfx_update` with FPS limiting
- [ ] Unit tests: window + pixels
- [ ] Example: `hello_pixel.c`

### Phase 1B: Drawing Primitives
- [ ] Bresenham line algorithm (`src/vgfx_draw.c`)
- [ ] Rectangle outline & filled
- [ ] Midpoint circle outline & filled
- [ ] Unit tests: lines, rectangles, circles
- [ ] Example: `bouncing_ball.c`

### Phase 1C: Input & Events
- [ ] Keyboard state tracking (macOS)
- [ ] Mouse position & button tracking (macOS)
- [ ] Event queue implementation
- [ ] `vgfx_poll_event` API
- [ ] Mock platform backend (`src/vgfx_platform_mock.c`)
- [ ] Unit tests: input & events
- [ ] Example: `input_demo.c`

### Phase 1D: Linux Support
- [ ] X11 backend (`src/vgfx_platform_linux.c`)
- [ ] Full test suite on Linux

### Phase 1E: Windows Support
- [ ] Win32 backend (`src/vgfx_platform_win32.c`)
- [ ] Full test suite on Windows

### Documentation
- [ ] README.md with build instructions
- [ ] API documentation
- [ ] Example programs documented
- [ ] Integration guide for Viper runtime

---

## Table of Contents

1. [Summary & Objective](#1-summary--objective)
2. [Scope](#2-scope)
3. [Configuration](#3-configuration)
4. [Core Concepts & Semantics](#4-core-concepts--semantics)
5. [Public API](#5-public-api)
6. [Error Handling](#6-error-handling)
7. [Tests](#7-tests)
8. [Internal Architecture](#8-internal-architecture)
9. [Build System](#9-build-system)
10. [Implementation Plan](#10-implementation-plan)

---

## 1. Summary & Objective

### 1.1 Objective

**ViperGFX** is a pure software-rendered, single-window, cross-platform 2D graphics library written in C.

**Core Features:**
- Window creation & event loop
- Software framebuffer rendering (32-bit RGBA)
- Basic 2D drawing primitives
- Keyboard & mouse input
- Configurable FPS limiting

**Design Principles:**
- **Zero external dependencies** – No SDL, GLFW, or third-party libraries
- **Native platform APIs only** – Cocoa (macOS), X11 (Linux), Win32 (Windows)
- **Pure C implementation** – C99 standard, C++ compatible
- **Software rendering only** – No OpenGL/Vulkan/Metal

### 1.2 Target Platforms

| Platform | API | Backend File |
|----------|-----|--------------|
| macOS | Cocoa | `vgfx_platform_macos.m` |
| Linux | X11 | `vgfx_platform_linux.c` |
| Windows | Win32 GDI | `vgfx_platform_win32.c` |

### 1.3 Integration Path

```
Phase 1: Standalone static library (libvipergfx.a / vipergfx.lib)
          ↓
Phase 2: Integration into Viper runtime
          ↓
Phase 3: BASIC frontend support (SCREEN, PSET, LINE, etc.)
```

---

## 2. Scope

### 2.1 In Scope (Phase 1)

✅ **Window Management**
- Single window creation/destruction
- Event loop stepping
- Optional user resizing
- Window close detection

✅ **Framebuffer**
- 32-bit RGBA software buffer
- Direct pixel access API
- Automatic presentation to window

✅ **Pixel Operations**
- Set pixel (with bounds checking)
- Get pixel color
- Clear screen

✅ **Drawing Primitives**
- Line (Bresenham algorithm)
- Rectangle (outline & filled)
- Circle (outline & filled, midpoint algorithm)

✅ **Input**
- Keyboard state polling (A-Z, 0-9, arrows, Enter, Escape, Space)
- Mouse position tracking
- Mouse button state (left, right, middle)
- Event queue for close, resize, focus, key up/down, mouse move/button

✅ **Frame Timing**
- Configurable per-window FPS limit
- Unlimited FPS mode

### 2.2 Out of Scope (Phase 1)

❌ Hardware acceleration (OpenGL/Vulkan/Metal)
❌ Image file loading (PNG/JPEG)
❌ Text rendering / font support
❌ Audio subsystem
❌ 3D graphics
❌ Advanced blending/compositing
❌ Multi-window support
❌ Modifier keys (Shift/Ctrl/Alt)
❌ Per-monitor high-DPI awareness

---

## 3. Configuration

### 3.1 Compile-Time Configuration

**File:** `include/vgfx_config.h`

```c
// Default window dimensions if not specified
#ifndef VGFX_DEFAULT_WIDTH
#define VGFX_DEFAULT_WIDTH   640
#endif

#ifndef VGFX_DEFAULT_HEIGHT
#define VGFX_DEFAULT_HEIGHT  480
#endif

// Default window title
#ifndef VGFX_DEFAULT_TITLE
#define VGFX_DEFAULT_TITLE   "ViperGFX"
#endif

// Default frame rate limit (-1 = unlimited)
#ifndef VGFX_DEFAULT_FPS
#define VGFX_DEFAULT_FPS     60
#endif

// Color depth (internal framebuffer)
#ifndef VGFX_COLOR_DEPTH
#define VGFX_COLOR_DEPTH     32  // RGBA (8-8-8-8)
#endif

// Maximum window dimensions (safety limit)
#ifndef VGFX_MAX_WIDTH
#define VGFX_MAX_WIDTH       4096
#endif

#ifndef VGFX_MAX_HEIGHT
#define VGFX_MAX_HEIGHT      4096
#endif

// Event queue capacity (must be power of 2 for efficient ring buffer)
#ifndef VGFX_EVENT_QUEUE_SIZE
#define VGFX_EVENT_QUEUE_SIZE 256
#endif
```

**Usage:** Projects may provide their own `vgfx_config.h` before including `vgfx.h`.

### 3.2 Runtime Configuration

```c
// Set per-window target FPS
//   fps == -1: unlimited (no frame limiting)
//   fps == 0:  use current global default
//   fps > 0:   specific target FPS
void vgfx_set_fps(vgfx_window_t window, int32_t fps);

// Set global default FPS for future windows created with params.fps == 0
//   fps == -1: unlimited
//   fps > 0:   specific FPS
// Initial default: VGFX_DEFAULT_FPS (60)
void vgfx_set_default_fps(int32_t fps);
```

---

## 4. Core Concepts & Semantics

### 4.1 Coordinate System

```
(0,0) ──────────> X
  │
  │
  │
  ▼
  Y

Origin: Top-left corner
X-axis: Increases to the right
Y-axis: Increases downward
Valid range: [0, width) × [0, height)
```

### 4.2 Window & Framebuffer

**Framebuffer Format:**
- **Bits per pixel:** 32 (RGBA)
- **Byte layout:** `[R, G, B, A]` (8 bits each)
- **Memory layout:** Row-major, no padding
- **Stride:** Always `width * 4` bytes

**Color Model:**
- **API color type:** `vgfx_color_t` (24-bit RGB, `0x00RRGGBB`)
- **Internal storage:** 32-bit RGBA
- **Alpha channel:** Always set to `0xFF` (fully opaque) by all drawing operations
- **Direct access:** Users may manipulate alpha via `vgfx_get_framebuffer()`

**Color Conversion:**
```c
// When vgfx_color_t (0x00RRGGBB) is written to framebuffer:
uint8_t r = (color >> 16) & 0xFF;  // → pixels[offset + 0]
uint8_t g = (color >>  8) & 0xFF;  // → pixels[offset + 1]
uint8_t b = (color >>  0) & 0xFF;  // → pixels[offset + 2]
uint8_t a = 0xFF;                  // → pixels[offset + 3]
```

**Pixel Addressing:**
```c
// Pixel at (x, y):
int32_t offset = y * stride + x * 4;
uint8_t r = pixels[offset + 0];
uint8_t g = pixels[offset + 1];
uint8_t b = pixels[offset + 2];
uint8_t a = pixels[offset + 3];
```

### 4.3 Main Loop & Frame Lifecycle

**Recommended Pattern:**

```c
vgfx_window_params_t params = {
    .width    = 800,
    .height   = 600,
    .title    = "Example",
    .fps      = 60,
    .resizable = 1
};

vgfx_window_t win = vgfx_create_window(&params);
if (!win) {
    fprintf(stderr, "Failed to create window: %s\n", vgfx_get_last_error());
    return 1;
}

int running = 1;
while (running) {
    // 1. Process events and present framebuffer (with FPS limiting)
    if (!vgfx_update(win)) {
        // Fatal error or window lost
        break;
    }

    // 2. Poll events
    vgfx_event_t ev;
    while (vgfx_poll_event(win, &ev)) {
        switch (ev.type) {
        case VGFX_EVENT_CLOSE:
            running = 0;
            break;
        case VGFX_EVENT_RESIZE:
            // Framebuffer has been resized; redraw fully
            break;
        case VGFX_EVENT_KEY_DOWN:
            // Handle key press
            break;
        default:
            break;
        }
    }

    // 3. Draw next frame
    vgfx_cls(win, VGFX_BLACK);
    // ... draw primitives ...
}

vgfx_destroy_window(win);
```

**Frame Timing:**
```
Frame N start
    ↓
vgfx_update() called
    ↓
1. Process OS events → translate to vgfx events → enqueue
2. Present framebuffer to window (blit)
3. Calculate elapsed time since last frame
4. If FPS limiting enabled:
       target_time = 1000ms / fps
       if elapsed < target_time:
           sleep(target_time - elapsed)
    ↓
vgfx_update() returns 1
    ↓
Application polls events via vgfx_poll_event()
    ↓
Application draws next frame
    ↓
Frame N+1 start
```

### 4.4 Window Resizing

**Behavior:**
1. User resizes window (if `resizable == 1`)
2. `VGFX_EVENT_RESIZE` queued with new `width` and `height`
3. Internal framebuffer reallocated to new dimensions
4. **New framebuffer cleared to black** (`0x000000`, alpha `0xFF`)
5. Application should redraw fully after receiving `VGFX_EVENT_RESIZE`

**Querying Size:**
```c
int32_t width, height;
if (vgfx_get_size(win, &width, &height)) {
    // width, height populated
}
```

### 4.5 Out-of-Bounds Behavior

| Function | Out-of-Bounds Behavior |
|----------|------------------------|
| `vgfx_pset(x, y, color)` | No-op (silent, no write) |
| `vgfx_point(x, y)` | Returns `0x000000` (black) |
| `vgfx_line(...)` | Clipped to window bounds |
| `vgfx_rect(...)` | Clipped to window bounds |
| `vgfx_circle(...)` | Clipped to window bounds |

**Rationale:** Performance (no error checking overhead in inner loops) and simplicity.

### 4.6 Input Semantics

**Keyboard:**
- `vgfx_key_down(win, key)` returns **current state** (1 = pressed, 0 = released)
- Not edge-triggered; applications must track edges if needed
- Key codes represent **physical keys**
- Letters map to uppercase ASCII (`'A'`-`'Z'`) regardless of Shift/CapsLock
- Modifiers (Shift/Ctrl/Alt) are **out of scope** for Phase 1

**Key Repeat:**
- OS auto-repeat may generate multiple `VGFX_EVENT_KEY_DOWN` events
- `event.data.key.is_repeat` flag indicates repeat (best-effort, platform-dependent)
- Applications needing precise input should track `KEY_DOWN`/`KEY_UP` pairs

**Mouse:**
- `vgfx_mouse_pos(win, &x, &y)` reports position in window coordinates
- Coordinates may be **negative or exceed window dimensions** if mouse is outside
- Return value: `1` if inside window bounds `[0, width) × [0, height)`, `0` otherwise
- `vgfx_mouse_button(win, button)` returns current button state (1 = pressed, 0 = released)

### 4.7 Threading Model

**Restrictions:**
- ViperGFX is **NOT thread-safe**
- All functions for a given window **must be called from the thread that created it**
- **macOS:** All calls **MUST** be on the main thread (Cocoa requirement)
- **Linux/Windows:** Any single thread is acceptable

**Violation Consequences:**
- **macOS:** Cocoa will assert/crash
- **Other platforms:** Undefined behavior (likely crashes or corruption)

**No Enforcement:** Library does not validate thread IDs (performance overhead).

---

## 5. Public API

**Header:** `include/vgfx.h`

**C++ Compatibility:**
```c
#ifdef __cplusplus
extern "C" {
#endif
// ... API ...
#ifdef __cplusplus
}
#endif
```

### 5.0 Library Version

```c
// Library version
#define VGFX_VERSION_MAJOR 1
#define VGFX_VERSION_MINOR 0
#define VGFX_VERSION_PATCH 0

// Query runtime version
// Returns: (major << 16) | (minor << 8) | patch
uint32_t vgfx_version(void);
```

### 5.1 Core Data Types

```c
#include <stdint.h>

// Opaque window handle
typedef struct vgfx_window* vgfx_window_t;

// 24-bit RGB color (0x00RRGGBB)
// Note: Internally stored as 32-bit RGBA with alpha = 0xFF
typedef uint32_t vgfx_color_t;
```

**Window Creation Parameters:**
```c
typedef struct {
    int32_t     width;      // Pixels; <= 0 → VGFX_DEFAULT_WIDTH
    int32_t     height;     // Pixels; <= 0 → VGFX_DEFAULT_HEIGHT
    const char* title;      // UTF-8 string; NULL → VGFX_DEFAULT_TITLE
    int32_t     fps;        // -1 = unlimited, 0 = use default, >0 = target FPS
    int32_t     resizable;  // 0 = fixed size, non-zero = user-resizable
} vgfx_window_params_t;
```

**Event Types:**
```c
typedef enum {
    VGFX_EVENT_NONE = 0,
    VGFX_EVENT_CLOSE,
    VGFX_EVENT_RESIZE,
    VGFX_EVENT_FOCUS_GAINED,
    VGFX_EVENT_FOCUS_LOST,
    VGFX_EVENT_KEY_DOWN,
    VGFX_EVENT_KEY_UP,
    VGFX_EVENT_MOUSE_MOVE,
    VGFX_EVENT_MOUSE_DOWN,
    VGFX_EVENT_MOUSE_UP
} vgfx_event_type_t;
```

**Key Codes:**
```c
typedef enum {
    VGFX_KEY_UNKNOWN = 0,

    // Letters (always uppercase)
    VGFX_KEY_A = 'A', VGFX_KEY_B, VGFX_KEY_C, VGFX_KEY_D,
    VGFX_KEY_E, VGFX_KEY_F, VGFX_KEY_G, VGFX_KEY_H,
    VGFX_KEY_I, VGFX_KEY_J, VGFX_KEY_K, VGFX_KEY_L,
    VGFX_KEY_M, VGFX_KEY_N, VGFX_KEY_O, VGFX_KEY_P,
    VGFX_KEY_Q, VGFX_KEY_R, VGFX_KEY_S, VGFX_KEY_T,
    VGFX_KEY_U, VGFX_KEY_V, VGFX_KEY_W, VGFX_KEY_X,
    VGFX_KEY_Y, VGFX_KEY_Z = 'Z',

    // Digits
    VGFX_KEY_0 = '0', VGFX_KEY_1, VGFX_KEY_2, VGFX_KEY_3,
    VGFX_KEY_4, VGFX_KEY_5, VGFX_KEY_6, VGFX_KEY_7,
    VGFX_KEY_8, VGFX_KEY_9 = '9',

    VGFX_KEY_SPACE  = ' ',
    VGFX_KEY_ENTER  = 256,
    VGFX_KEY_ESCAPE,
    VGFX_KEY_LEFT,
    VGFX_KEY_RIGHT,
    VGFX_KEY_UP,
    VGFX_KEY_DOWN
} vgfx_key_t;
```

**Mouse Buttons:**
```c
typedef enum {
    VGFX_MOUSE_LEFT   = 0,
    VGFX_MOUSE_RIGHT  = 1,
    VGFX_MOUSE_MIDDLE = 2
} vgfx_mouse_button_t;
```

**Event Structure:**
```c
typedef struct {
    vgfx_event_type_t type;

    union {
        struct {
            int32_t width;
            int32_t height;
        } resize;

        struct {
            vgfx_key_t key;
            int32_t    is_repeat;  // 1 = auto-repeat, 0 = initial press
        } key;

        struct {
            int32_t x;
            int32_t y;
        } mouse_move;

        struct {
            int32_t x;
            int32_t y;
            vgfx_mouse_button_t button;
        } mouse_button;
    } data;
} vgfx_event_t;
```

**Framebuffer Info:**
```c
typedef struct {
    uint8_t* pixels;  // Pointer to first byte of row 0
    int32_t  width;   // Width in pixels
    int32_t  height;  // Height in pixels
    int32_t  stride;  // Bytes per row (always width * 4)
} vgfx_framebuffer_t;
```

### 5.2 Window Management

```c
// Create window with specified parameters
// Returns NULL on failure (check vgfx_get_last_error())
vgfx_window_t vgfx_create_window(const vgfx_window_params_t* params);

// Destroy window and free all resources
// NULL is allowed and is a no-op
void vgfx_destroy_window(vgfx_window_t window);

// Process events, present framebuffer, enforce FPS limit
// Returns: 1 = success, 0 = fatal error / window lost
//
// NOTE: Receiving VGFX_EVENT_CLOSE does NOT cause this to return 0.
// Application must check for VGFX_EVENT_CLOSE and call vgfx_destroy_window.
int32_t vgfx_update(vgfx_window_t window);

// Set per-window target FPS
//   -1 = unlimited, 0 = use default, >0 = specific FPS
void vgfx_set_fps(vgfx_window_t window, int32_t fps);

// Set global default FPS for future windows (params.fps == 0)
//   -1 = unlimited, >0 = specific FPS
void vgfx_set_default_fps(int32_t fps);

// Get current window size
// Returns 1 on success, 0 on error (e.g., NULL window or NULL outputs)
int32_t vgfx_get_size(vgfx_window_t window, int32_t* width, int32_t* height);
```

### 5.3 Event Handling

```c
// Poll next pending event
// Returns 1 if event was written to out_event, 0 if queue is empty
//
// Event queue:
//   - Minimum capacity: VGFX_EVENT_QUEUE_SIZE (256)
//   - When full: oldest events are dropped (FIFO eviction)
//   - Events are queued by vgfx_update()
int32_t vgfx_poll_event(vgfx_window_t window, vgfx_event_t* out_event);
```

### 5.4 Drawing Operations

```c
// Set pixel at (x, y) to color
// No-op if window is NULL or coordinates are out of bounds
void vgfx_pset(vgfx_window_t window, int32_t x, int32_t y, vgfx_color_t color);

// Get pixel color at (x, y)
// Returns 0x000000 if out of bounds or window is NULL
vgfx_color_t vgfx_point(vgfx_window_t window, int32_t x, int32_t y);

// Clear entire framebuffer to color
void vgfx_cls(vgfx_window_t window, vgfx_color_t color);

// Draw line from (x1, y1) to (x2, y2), clipped to window
void vgfx_line(vgfx_window_t window,
               int32_t x1, int32_t y1,
               int32_t x2, int32_t y2,
               vgfx_color_t color);

// Draw axis-aligned rectangle outline
// Coverage: [x, x+w) × [y, y+h)
// No-op if w <= 0 or h <= 0
void vgfx_rect(vgfx_window_t window,
               int32_t x, int32_t y,
               int32_t w, int32_t h,
               vgfx_color_t color);

// Draw filled rectangle
// Coverage: [x, x+w) × [y, y+h)
// No-op if w <= 0 or h <= 0
void vgfx_fill_rect(vgfx_window_t window,
                    int32_t x, int32_t y,
                    int32_t w, int32_t h,
                    vgfx_color_t color);

// Draw circle outline (midpoint algorithm)
// No-op if radius <= 0
void vgfx_circle(vgfx_window_t window,
                 int32_t cx, int32_t cy,
                 int32_t radius,
                 vgfx_color_t color);

// Draw filled circle
// No-op if radius <= 0
void vgfx_fill_circle(vgfx_window_t window,
                      int32_t cx, int32_t cy,
                      int32_t radius,
                      vgfx_color_t color);
```

### 5.5 Color Utilities

```c
// Construct RGB color from components
vgfx_color_t vgfx_rgb(uint8_t r, uint8_t g, uint8_t b);

// Extract RGB components
void vgfx_color_to_rgb(vgfx_color_t color, uint8_t* r, uint8_t* g, uint8_t* b);

// Common colors
#define VGFX_BLACK    0x000000
#define VGFX_WHITE    0xFFFFFF
#define VGFX_RED      0xFF0000
#define VGFX_GREEN    0x00FF00
#define VGFX_BLUE     0x0000FF
#define VGFX_YELLOW   0xFFFF00
#define VGFX_CYAN     0x00FFFF
#define VGFX_MAGENTA  0xFF00FF
```

### 5.6 Input Polling

```c
// Check if key is currently pressed (1) or not (0)
int32_t vgfx_key_down(vgfx_window_t window, vgfx_key_t key);

// Get mouse position in window coordinates
// Coordinates may be negative or > width/height if mouse is outside
// Returns 1 if mouse is inside window bounds [0, width) × [0, height), 0 otherwise
int32_t vgfx_mouse_pos(vgfx_window_t window, int32_t* x, int32_t* y);

// Check if mouse button is currently pressed (1) or not (0)
int32_t vgfx_mouse_button(vgfx_window_t window, vgfx_mouse_button_t button);
```

### 5.7 Advanced Framebuffer Access

```c
// Get direct access to internal framebuffer for low-level drawing
// Returns 1 on success, 0 on failure
//
// Pixel format:
//   - 32-bpp RGBA
//   - Byte layout: [R, G, B, A]
//   - stride = width * 4 (no padding)
//   - Pixel (x, y): offset = y * stride + x * 4
//   - Row 0 is top of window
//
// Validity:
//   Returned pointer valid until:
//     - Next vgfx_update() call
//     - Window resize
//     - vgfx_destroy_window() call
int32_t vgfx_get_framebuffer(vgfx_window_t window, vgfx_framebuffer_t* out_info);
```

---

## 6. Error Handling

### 6.1 General Strategy

**Return Values:**
- `NULL` for handle-returning functions (e.g., `vgfx_create_window`)
- `0` for integer-returning functions on failure
- Silent no-op for drawing functions with invalid inputs

**Diagnostics:**
- Error messages printed to `stderr` with prefix `"vgfx: "`
- Library **never** calls `abort()` or `exit()`
- Last error string available via `vgfx_get_last_error()`

### 6.2 Error Scenarios

| Scenario | Return Value | stderr Output | Last Error String |
|----------|--------------|---------------|-------------------|
| `vgfx_create_window`: width/height > max | `NULL` | `vgfx: Window dimensions exceed maximum (4096x4096)` | Set |
| `vgfx_create_window`: width/height ≤ 0 | Valid handle | `vgfx: Using default dimensions (640x480)` | Not set |
| `vgfx_create_window`: NULL params | Valid handle | `vgfx: NULL params; using defaults` | Not set |
| `vgfx_create_window`: platform failure | `NULL` | `vgfx: Failed to create native window` | Set |
| `vgfx_destroy_window`: NULL window | No-op | None | Not set |
| `vgfx_update`: NULL window | `0` | None | Not set |
| `vgfx_update`: platform error | `0` | `vgfx: Event processing error` | Set |
| `vgfx_get_size`: NULL window or outputs | `0` | None | Not set |
| `vgfx_poll_event`: NULL window or out_event | `0` | None | Not set |
| `vgfx_get_framebuffer`: NULL window or out_info | `0` | None | Not set |
| `vgfx_pset`: out of bounds | No-op | None | Not set |
| `vgfx_point`: out of bounds | `0x000000` | None | Not set |
| `vgfx_rect`: w ≤ 0 or h ≤ 0 | No-op | None | Not set |
| `vgfx_circle`: radius ≤ 0 | No-op | None | Not set |
| Drawing functions: NULL window | No-op | None | Not set |

### 6.3 Last Error String

```c
// Returns human-readable description of last error on calling thread
// Returns NULL if no error has occurred
//
// Stored in thread-local storage (TLS)
// Valid until:
//   - Next ViperGFX call on same thread
//   - Thread termination
//
// String is owned by library; caller must NOT free it
const char* vgfx_get_last_error(void);

// Clear last error string for calling thread
void vgfx_clear_last_error(void);
```

---

## 7. Tests

### 7.1 Unit Tests (Automated)

**Test Framework:** Custom C test harness using mock platform backend

**Location:** `tests/test_*.c`

#### T1: Window Creation – Valid Parameters

```
Given: params = {width=800, height=600, title="Test", fps=60, resizable=1}
When:  win = vgfx_create_window(&params)
Then:  win != NULL
  And: vgfx_get_size(win, &w, &h) returns 1
  And: w == 800, h == 600
  And: vgfx_update(win) returns 1
```

#### T2: Window Creation – Dimensions Exceed Max

```
Given: params = {width=5000, height=5000, ...}
When:  win = vgfx_create_window(&params)
Then:  win == NULL
  And: vgfx_get_last_error() contains "exceed maximum"
  And: stderr contains "exceed maximum"
```

#### T3: Window Creation – Invalid Dimensions Use Defaults

```
Given: params = {width=0, height=-10, ...}
When:  win = vgfx_create_window(&params)
Then:  win != NULL
  And: vgfx_get_size(win, &w, &h) returns 1
  And: w == VGFX_DEFAULT_WIDTH, h == VGFX_DEFAULT_HEIGHT
```

#### T4: Pixel Set/Get

```
Given: win created at 640×480
When:  vgfx_pset(win, 100, 100, 0xFF0000)
  And: color = vgfx_point(win, 100, 100)
Then:  color == 0xFF0000
```

#### T5: Out-of-Bounds Write Ignored

```
Given: win at 640×480, pixel (639, 479) is black
When:  vgfx_pset(win, 1000, 1000, 0x00FF00)
Then:  vgfx_point(win, 639, 479) == 0x000000 (unchanged)
  And: vgfx_point(win, 1000, 1000) == 0x000000
```

#### T6: Clear Screen

```
Given: win at 100×100
When:  vgfx_cls(win, 0xFF0000)
Then:  For all (x, y) in [0, 100) × [0, 100):
         vgfx_point(win, x, y) == 0xFF0000
```

#### T7: Line Drawing – Horizontal

```
Given: win created
When:  vgfx_line(win, 10, 10, 50, 10, 0xFFFFFF)
Then:  For all x in [10, 50]:
         vgfx_point(win, x, 10) == 0xFFFFFF
```

#### T8: Line Drawing – Vertical

```
Given: win created
When:  vgfx_line(win, 20, 10, 20, 30, 0xFF0000)
Then:  For all y in [10, 30]:
         vgfx_point(win, 20, y) == 0xFF0000
```

#### T9: Line Drawing – Diagonal

```
Given: win created
When:  vgfx_line(win, 0, 0, 10, 10, 0x00FF00)
Then:  vgfx_point(win, 0, 0) == 0x00FF00
  And: vgfx_point(win, 5, 5) == 0x00FF00
  And: vgfx_point(win, 10, 10) == 0x00FF00
```

#### T10: Rectangle Outline

```
Given: win created, cls to black
When:  vgfx_rect(win, 10, 10, 20, 15, 0xFFFFFF)
Then:  Top edge: [10, 30) × {10} all white
  And: Bottom edge: [10, 30) × {24} all white
  And: Left edge: {10} × [10, 25) all white
  And: Right edge: {29} × [10, 25) all white
  And: Interior (e.g., 15, 15) remains black
```

#### T11: Filled Rectangle

```
Given: win created, cls to black
When:  vgfx_fill_rect(win, 5, 5, 10, 10, 0xFF0000)
Then:  For all (x, y) in [5, 15) × [5, 15):
         vgfx_point(win, x, y) == 0xFF0000
```

#### T12: Circle Outline – Sanity

```
Given: win at 200×200, cls to black
When:  vgfx_circle(win, 100, 100, 50, 0xFF0000)
Then:  Known octant points are red:
         (150, 100), (50, 100), (100, 150), (100, 50)
  And: Total red pixels > 200 and < 400 (approximate circle)
```

#### T13: Filled Circle

```
Given: win at 200×200, cls to black
When:  vgfx_fill_circle(win, 100, 100, 30, 0x00FF00)
Then:  All pixels within distance 30 from (100, 100) are green
  And: Approximate count: π × 30² ≈ 2827 green pixels (±5%)
```

#### T14: Framebuffer Access

```
Given: win created
When:  vgfx_get_framebuffer(win, &fb) returns 1
  And: Write pixel directly: fb.pixels[y * fb.stride + x * 4 + 0..2] = RGB
Then:  vgfx_point(win, x, y) returns matching color
  And: fb.stride == fb.width * 4
```

#### T15: Frame Rate Limiting

```
Given: win with vgfx_set_fps(win, 60)
When:  t_start = now()
  And: Call vgfx_update(win) 120 times with minimal work
  And: t_end = now()
Then:  elapsed = t_end - t_start
  And: 1.7s < elapsed < 2.3s (2.0s ± 15%)
```

#### T16: Keyboard Input (Mock Backend)

```
Given: win with mock platform backend
When:  Mock injects KEY_DOWN for VGFX_KEY_A
Then:  vgfx_key_down(win, VGFX_KEY_A) == 1

When:  Mock injects KEY_UP for VGFX_KEY_A
Then:  vgfx_key_down(win, VGFX_KEY_A) == 0
```

#### T17: Mouse Position (Mock Backend)

```
Given: win at 640×480 with mock backend
When:  Mock reports mouse at (150, 200)
Then:  vgfx_mouse_pos(win, &x, &y) == 1
  And: x == 150, y == 200

When:  Mock reports mouse at (-10, -10)
Then:  vgfx_mouse_pos(win, &x, &y) == 0
  And: x == -10, y == -10
```

#### T18: Mouse Button (Mock Backend)

```
Given: win with mock backend
When:  Mock injects MOUSE_DOWN for VGFX_MOUSE_LEFT
Then:  vgfx_mouse_button(win, VGFX_MOUSE_LEFT) == 1

When:  Mock injects MOUSE_UP for VGFX_MOUSE_LEFT
Then:  vgfx_mouse_button(win, VGFX_MOUSE_LEFT) == 0
```

#### T19: Event Queue – Basic

```
Given: win created
When:  Mock injects events: KEY_DOWN, MOUSE_MOVE, KEY_UP
  And: vgfx_update(win) called
Then:  vgfx_poll_event(win, &ev) returns 1, ev.type == VGFX_EVENT_KEY_DOWN
  And: vgfx_poll_event(win, &ev) returns 1, ev.type == VGFX_EVENT_MOUSE_MOVE
  And: vgfx_poll_event(win, &ev) returns 1, ev.type == VGFX_EVENT_KEY_UP
  And: vgfx_poll_event(win, &ev) returns 0 (queue empty)
```

#### T20: Event Queue – Overflow

```
Given: win created, queue capacity = 256
When:  Mock injects 300 events
  And: vgfx_update(win) called
Then:  vgfx_poll_event retrieves 256 events
  And: Oldest 44 events were dropped
```

#### T21: Resize Event

```
Given: win at 640×480
When:  Mock injects RESIZE event (800, 600)
  And: vgfx_update(win) called
Then:  vgfx_poll_event(win, &ev) returns 1
  And: ev.type == VGFX_EVENT_RESIZE
  And: ev.data.resize.width == 800
  And: ev.data.resize.height == 600
  And: vgfx_get_size(win, &w, &h): w == 800, h == 600
  And: All pixels are black (framebuffer cleared)
```

### 7.2 Visual / Manual Tests

**Location:** `tests/visual_*.c`, `examples/*.c`

#### V1: RGB Color Rendering

```
Program: Draw three filled rectangles (red, green, blue) side-by-side
Expected: Visually distinct primary colors, no artifacts
```

#### V2: Graphics Performance

```
Program: Each frame, draw 10,000 random pixels + 100 random lines
Expected: Smooth animation at 60 FPS, no stutter
```

#### V3: Resize Behavior

```
Program: Display colored content filling window
Action: Resize window repeatedly
Expected: Framebuffer adjusts, content redraws, no crashes
```

#### V4: Input Demo

```
Program: Display keyboard/mouse state on screen
Action: Press keys, move mouse, click buttons
Expected: Visual feedback updates correctly
```

#### V5: Bouncing Ball

```
Program: Ball bounces within window bounds with physics
Expected: Smooth animation, collision detection works
```

---

## 8. Internal Architecture

### 8.1 File Structure

```
vipergfx/
├── include/
│   ├── vgfx.h                    # Public API header
│   └── vgfx_config.h             # Configuration macros
├── src/
│   ├── vgfx.c                    # Core implementation (window lifecycle, events)
│   ├── vgfx_internal.h           # Internal structures & helpers
│   ├── vgfx_draw.c               # Drawing primitives (Bresenham, midpoint)
│   ├── vgfx_platform_macos.m     # macOS Cocoa backend
│   ├── vgfx_platform_linux.c     # Linux X11 backend
│   ├── vgfx_platform_win32.c     # Windows Win32 backend
│   └── vgfx_platform_mock.c      # Mock backend for testing
├── tests/
│   ├── test_window.c             # Window creation tests
│   ├── test_drawing.c            # Drawing primitive tests
│   ├── test_input.c              # Input & event tests (uses mock)
│   ├── visual_test.c             # Manual visual verification
│   └── test_harness.h            # Custom test framework
├── examples/
│   ├── hello_pixel.c             # Minimal pixel demo
│   ├── bouncing_ball.c           # Animation + collision demo
│   └── input_demo.c              # Keyboard/mouse visualization
├── CMakeLists.txt                # Build configuration
└── README.md                     # Usage & build instructions
```

### 8.2 Platform Abstraction

**Internal Window Structure:**

```c
// src/vgfx_internal.h

#define VGFX_EVENT_QUEUE_SIZE 256

typedef struct vgfx_window {
    // Public attributes
    int32_t  width;
    int32_t  height;
    int32_t  fps;
    int32_t  resizable;

    // Framebuffer
    uint8_t* pixels;      // RGBA data (width × height × 4 bytes)
    int32_t  stride;      // Always width * 4

    // Event queue (ring buffer)
    vgfx_event_t event_queue[VGFX_EVENT_QUEUE_SIZE];
    int32_t      event_head;  // Next write position
    int32_t      event_tail;  // Next read position

    // Input state
    uint8_t key_state[512];          // Indexed by vgfx_key_t
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_button_state[8];   // Indexed by vgfx_mouse_button_t

    // Timing
    int64_t last_frame_time_ms;

    // Platform-specific data (opaque pointer)
    void* platform_data;
} vgfx_window;
```

**Platform API (per backend):**

```c
// Initialize platform-specific window
// Returns 0 on failure, 1 on success
int vgfx_platform_init_window(vgfx_window* win, const vgfx_window_params_t* params);

// Destroy platform-specific window resources
void vgfx_platform_destroy_window(vgfx_window* win);

// Process OS events and translate to vgfx events
// Returns 0 on fatal error, 1 on success
int vgfx_platform_process_events(vgfx_window* win);

// Present framebuffer to screen (blit)
// Returns 0 on failure, 1 on success
int vgfx_platform_present(vgfx_window* win);

// Sleep for specified milliseconds (for FPS limiting)
void vgfx_platform_sleep_ms(int32_t ms);

// High-resolution timer (milliseconds since arbitrary epoch)
int64_t vgfx_platform_now_ms(void);
```

**Mock Platform API (tests only):**

```c
// src/vgfx_platform_mock.c

void vgfx_mock_inject_key_event(vgfx_window* win, vgfx_key_t key, int down);
void vgfx_mock_inject_mouse_move(vgfx_window* win, int32_t x, int32_t y);
void vgfx_mock_inject_mouse_button(vgfx_window* win, vgfx_mouse_button_t btn, int down);
void vgfx_mock_inject_resize(vgfx_window* win, int32_t width, int32_t height);
void vgfx_mock_inject_close(vgfx_window* win);
void vgfx_mock_set_time_ms(int64_t ms);  // Control time for FPS tests
```

### 8.3 Drawing Algorithms

**Bresenham Line Algorithm:**
```c
// Integer-only, deterministic
void bresenham_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                    void (*plot)(int32_t x, int32_t y, void* ctx), void* ctx);
```

**Midpoint Circle Algorithm:**
```c
// Integer-only, draws 8 octants symmetrically
void midpoint_circle(int32_t cx, int32_t cy, int32_t radius,
                     void (*plot)(int32_t x, int32_t y, void* ctx), void* ctx);
```

**Filled Circle:**
```c
// Scanline fill using midpoint circle for horizontal spans
void filled_circle(int32_t cx, int32_t cy, int32_t radius,
                   void (*hline)(int32_t x0, int32_t x1, int32_t y, void* ctx), void* ctx);
```

### 8.4 HiDPI Considerations

**Phase 1 Behavior:**
- All dimensions in **logical pixels**
- Platform handles backing scale automatically
- **macOS Retina:** 800×600 window → 800×600 framebuffer (OS scales to physical pixels)
- **Windows/Linux:** Default system scaling applies
- No explicit DPI management

**Future Phases:**
```c
// Query backing scale factor (e.g., 2.0 for Retina)
int32_t vgfx_get_scale(vgfx_window_t window, float* scale_x, float* scale_y);
```

---

## 9. Build System

### 9.1 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(vipergfx C)

set(CMAKE_C_STANDARD 99)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    add_compile_options(/W4 /WX)
endif()

# Platform detection
if(APPLE)
    set(PLATFORM_SOURCES src/vgfx_platform_macos.m)
    set(PLATFORM_LIBS "-framework Cocoa")
elseif(UNIX)
    find_package(X11 REQUIRED)
    set(PLATFORM_SOURCES src/vgfx_platform_linux.c)
    set(PLATFORM_LIBS X11::X11)
elseif(WIN32)
    set(PLATFORM_SOURCES src/vgfx_platform_win32.c)
    set(PLATFORM_LIBS user32 gdi32)
endif()

# Library
add_library(vipergfx STATIC
    src/vgfx.c
    src/vgfx_draw.c
    ${PLATFORM_SOURCES}
)

target_include_directories(vipergfx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(vipergfx PUBLIC ${PLATFORM_LIBS})

# Tests (optional)
option(VGFX_BUILD_TESTS "Build test suite" ON)
if(VGFX_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Examples (optional)
option(VGFX_BUILD_EXAMPLES "Build examples" ON)
if(VGFX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

### 9.2 Build Commands

```bash
# Configure
cmake -S . -B build

# Build library
cmake --build build

# Build with tests and examples
cmake -S . -B build -DVGFX_BUILD_TESTS=ON -DVGFX_BUILD_EXAMPLES=ON
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Install (optional)
cmake --install build --prefix /usr/local
```

---

## 10. Implementation Plan

### 10.1 Phase 1A: Core Infrastructure (macOS)

**Estimated Effort:** 2-3 days

**Deliverables:**
1. ✅ Project structure
2. ✅ CMakeLists.txt (macOS only)
3. ✅ `include/vgfx.h` with all API declarations
4. ✅ `include/vgfx_config.h` with configuration macros
5. ✅ `src/vgfx_internal.h` with `vgfx_window` struct
6. ✅ `src/vgfx.c` with:
   - Window lifecycle (create/destroy)
   - Framebuffer allocation
   - `vgfx_pset`, `vgfx_point`, `vgfx_cls`
   - `vgfx_update` with FPS limiting
   - Error handling (TLS last error string)
7. ✅ `src/vgfx_platform_macos.m` with:
   - NSWindow creation
   - NSView subclass for drawing
   - Event loop integration
   - Framebuffer-to-CGImage-to-view blitting
   - High-resolution timer
8. ✅ Unit tests: T1-T6 (window creation, pixels)
9. ✅ Example: `examples/hello_pixel.c`

**Acceptance Criteria:**
- Builds on macOS without errors/warnings
- Window appears with correct dimensions
- Pixels can be set and read back
- Screen clears to specified color
- FPS limiting works (manual timing check)
- No memory leaks (run example under Instruments)

### 10.2 Phase 1B: Drawing Primitives

**Estimated Effort:** 2 days

**Deliverables:**
1. ✅ `src/vgfx_draw.c` with:
   - `bresenham_line()`
   - `vgfx_line()` wrapper with clipping
   - `vgfx_rect()` and `vgfx_fill_rect()`
   - `midpoint_circle()` helper
   - `vgfx_circle()` and `vgfx_fill_circle()`
2. ✅ Unit tests: T7-T13 (lines, rectangles, circles)
3. ✅ Example: `examples/bouncing_ball.c` (demonstrates animation + primitives)

**Acceptance Criteria:**
- All drawing primitives produce correct integer geometry
- Clipping works correctly (no segfaults on OOB draws)
- Visual tests show smooth shapes
- Unit tests pass

### 10.3 Phase 1C: Input & Events

**Estimated Effort:** 2-3 days

**Deliverables:**
1. ✅ Event queue implementation in `src/vgfx.c`
2. ✅ `vgfx_poll_event()` API
3. ✅ Input state tracking in `vgfx_window`
4. ✅ macOS event translation in `src/vgfx_platform_macos.m`:
   - Keyboard events (key down/up, repeat flag)
   - Mouse events (move, button down/up)
   - Window events (close, resize, focus)
5. ✅ `src/vgfx_platform_mock.c` for testing
6. ✅ Unit tests: T16-T21 (input, events, queue)
7. ✅ Example: `examples/input_demo.c` (visualize key/mouse state)

**Acceptance Criteria:**
- Keyboard state tracked correctly
- Mouse position/buttons tracked correctly
- Event queue works (FIFO, overflow handling)
- Resize event triggers framebuffer reallocation + clear
- Close event queued (window doesn't close automatically)
- Unit tests pass with mock backend

### 10.4 Phase 1D: Linux Support

**Estimated Effort:** 3-4 days

**Deliverables:**
1. ✅ `src/vgfx_platform_linux.c` with:
   - X11 window creation (`XCreateWindow`)
   - XImage-based framebuffer blitting (`XPutImage`)
   - Event translation (KeyPress/KeyRelease, MotionNotify, ButtonPress/Release, ConfigureNotify, ClientMessage for WM_DELETE_WINDOW)
   - High-resolution timer (`clock_gettime`)
2. ✅ CMakeLists.txt updated for Linux
3. ✅ Full test suite runs on Linux
4. ✅ Examples tested on Linux

**Acceptance Criteria:**
- All Phase 1A-C features work on Linux
- All unit tests pass
- Visual tests show correct behavior
- No X11-specific crashes or artifacts

### 10.5 Phase 1E: Windows Support

**Estimated Effort:** 3-4 days

**Deliverables:**
1. ✅ `src/vgfx_platform_win32.c` with:
   - Win32 window creation (`CreateWindowEx`)
   - DIB section for framebuffer (`CreateDIBSection`)
   - GDI blitting (`BitBlt` or `StretchDIBits`)
   - Event translation (WM_KEYDOWN/UP, WM_MOUSEMOVE, WM_LBUTTONDOWN/UP, etc., WM_SIZE, WM_CLOSE)
   - High-resolution timer (`QueryPerformanceCounter`)
2. ✅ CMakeLists.txt updated for Windows
3. ✅ Full test suite runs on Windows
4. ✅ Examples tested on Windows

**Acceptance Criteria:**
- All Phase 1A-C features work on Windows
- All unit tests pass
- Visual tests show correct behavior
- No Win32-specific crashes or artifacts

### 10.6 Documentation & Polish

**Estimated Effort:** 1-2 days

**Deliverables:**
1. ✅ `README.md` with:
   - Overview
   - Build instructions (all platforms)
   - API quick reference
   - Example usage
   - Threading rules
   - Limitations
2. ✅ API documentation (Doxygen or similar)
3. ✅ Example programs documented with comments
4. ✅ Integration guide for Viper runtime

**Acceptance Criteria:**
- README is clear and complete
- Examples are well-commented
- API reference is accurate
- Integration guide covers all necessary steps

---

## 11. Future Phases (Out of Scope for v1.0)

### Phase 2: Advanced Features
- Image loading (BMP, PNG via stb_image)
- Sprite/texture support
- Bitmap fonts / text rendering
- Alpha blending modes
- Palette support (8-bit indexed color)

### Phase 3: Viper Integration
- Runtime signatures for ViperGFX functions
- BASIC frontend support:
  ```basic
  SCREEN 800, 600, "My Game"
  PSET 100, 100, RGB(255, 0, 0)
  LINE (0, 0) - (100, 100), RGB(0, 255, 0)
  CIRCLE (200, 200), 50, RGB(0, 0, 255)
  ```
- Integration with Viper's main loop

### Phase 4: Performance Optimization
- SIMD optimizations (SSE2/NEON) for bulk operations
- Multi-threaded rendering (tiled framebuffer)
- Dirty rectangle tracking (avoid full-screen blits)

---

## Appendix A: Platform-Specific Notes

### macOS (Cocoa)

**Window Creation:**
```objc
NSWindow* window = [[NSWindow alloc]
    initWithContentRect:NSMakeRect(0, 0, width, height)
    styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
               NSWindowStyleMaskResizable)
    backing:NSBackingStoreBuffered
    defer:NO];

[window setTitle:@"Title"];
[window makeKeyAndOrderFront:nil];
```

**Framebuffer Blitting:**
```objc
// Create CGImageRef from RGBA buffer
CGDataProviderRef provider = CGDataProviderCreateWithData(
    NULL, pixels, width * height * 4, NULL);
CGImageRef image = CGImageCreate(
    width, height, 8, 32, width * 4,
    CGColorSpaceCreateDeviceRGB(),
    kCGBitmapByteOrderDefault | kCGImageAlphaLast,
    provider, NULL, false, kCGRenderingIntentDefault);

// Draw in NSView's drawRect:
[image drawInRect:bounds];
```

**Event Handling:**
```objc
NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                       untilDate:[NSDate distantPast]
                       inMode:NSDefaultRunLoopMode
                       dequeue:YES];
[NSApp sendEvent:event];
```

### Linux (X11)

**Window Creation:**
```c
Display* display = XOpenDisplay(NULL);
Window window = XCreateSimpleWindow(display, root, 0, 0, width, height,
                                     1, black, white);
XMapWindow(display, window);
```

**Framebuffer Blitting:**
```c
XImage* ximage = XCreateImage(display, visual, 24, ZPixmap, 0,
                               (char*)pixels, width, height, 32, stride);
XPutImage(display, window, gc, ximage, 0, 0, 0, 0, width, height);
```

**Event Handling:**
```c
while (XPending(display)) {
    XEvent event;
    XNextEvent(display, &event);
    // Translate event.type (KeyPress, MotionNotify, etc.)
}
```

### Windows (Win32)

**Window Creation:**
```c
HWND hwnd = CreateWindowEx(0, "ViperGFXClass", "Title",
                            WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            width, height,
                            NULL, NULL, hInstance, NULL);
ShowWindow(hwnd, SW_SHOW);
```

**Framebuffer Blitting (DIB Section):**
```c
BITMAPINFO bmi = { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB };
HDC hdc = GetDC(hwnd);
HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void**)&pixels, NULL, 0);
HDC memdc = CreateCompatibleDC(hdc);
SelectObject(memdc, hbmp);

// Blit
BitBlt(hdc, 0, 0, width, height, memdc, 0, 0, SRCCOPY);
```

**Event Handling:**
```c
MSG msg;
while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
}
```

---

## Appendix B: Example Programs

### hello_pixel.c

```c
#include <vgfx.h>
#include <stdio.h>

int main(void) {
    vgfx_window_params_t params = {
        .width = 320,
        .height = 240,
        .title = "Hello Pixel",
        .fps = 60,
        .resizable = 0
    };

    vgfx_window_t win = vgfx_create_window(&params);
    if (!win) {
        fprintf(stderr, "Error: %s\n", vgfx_get_last_error());
        return 1;
    }

    int running = 1;
    while (running) {
        if (!vgfx_update(win)) break;

        vgfx_event_t ev;
        while (vgfx_poll_event(win, &ev)) {
            if (ev.type == VGFX_EVENT_CLOSE) {
                running = 0;
            }
        }

        // Draw gradient
        for (int y = 0; y < 240; y++) {
            for (int x = 0; x < 320; x++) {
                vgfx_color_t c = vgfx_rgb(x * 255 / 320, y * 255 / 240, 128);
                vgfx_pset(win, x, y, c);
            }
        }
    }

    vgfx_destroy_window(win);
    return 0;
}
```

### bouncing_ball.c

```c
#include <vgfx.h>
#include <stdio.h>

int main(void) {
    vgfx_window_params_t params = {640, 480, "Bouncing Ball", 60, 0};
    vgfx_window_t win = vgfx_create_window(&params);
    if (!win) return 1;

    int x = 320, y = 240;
    int vx = 3, vy = 2;
    int radius = 20;

    int running = 1;
    while (running) {
        if (!vgfx_update(win)) break;

        vgfx_event_t ev;
        while (vgfx_poll_event(win, &ev)) {
            if (ev.type == VGFX_EVENT_CLOSE) running = 0;
        }

        // Clear screen
        vgfx_cls(win, VGFX_BLACK);

        // Update position
        x += vx;
        y += vy;

        // Bounce off walls
        if (x - radius < 0 || x + radius >= 640) vx = -vx;
        if (y - radius < 0 || y + radius >= 480) vy = -vy;

        // Draw ball
        vgfx_fill_circle(win, x, y, radius, VGFX_RED);
    }

    vgfx_destroy_window(win);
    return 0;
}
```

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-11-20 | Initial specification |

---

**End of Specification**
