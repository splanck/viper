# User-Space Display Servers

**Status:** Complete implementation for GUI display services
**Location:** `user/servers/`
**SLOC:** ~4,200

## Overview

ViperDOS uses a hybrid kernel architecture where filesystem, networking, and device drivers run in the kernel, while
display services run in user space. Two user-space servers are implemented:

| Server       | Assign   | SLOC   | Purpose                |
|--------------|----------|--------|------------------------|
| **consoled** | CONSOLED | ~2,000 | GUI terminal emulator  |
| **displayd** | DISPLAY  | ~2,200 | Window management, GUI |

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      User Applications                           │
│   (workbench, calc, vedit, viewer, prefs, taskman, etc.)        │
└───────────────────────────┬─────────────────────────────────────┘
                            │ IPC (Channels)
        ┌───────────────────┼───────────────────┐
        ▼                   ▼                   │
┌───────────────┐  ┌───────────────┐            │
│   consoled    │  │   displayd    │◄───────────┘
│  GUI Terminal │  │  Window/GUI   │
│  CONSOLED:    │  │  DISPLAY:     │
└───────┬───────┘  └───────┬───────┘
        │                  │
        └──────────────────┤
                           │ Syscalls
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                          Kernel                                  │
│   MAP_FRAMEBUFFER │ SHM_CREATE │ SHM_MAP │ INPUT events          │
│   GET_MOUSE_STATE │ GCON_SET_GUI_MODE                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## Kernel GUI/Display Syscalls

The kernel provides syscalls for user-space display servers:

| Syscall               | Number | Description                               |
|-----------------------|--------|-------------------------------------------|
| SYS_MAP_FRAMEBUFFER   | 0x111  | Map framebuffer into user address space   |
| SYS_GET_MOUSE_STATE   | 0x110  | Get current mouse position and buttons    |
| SYS_SET_MOUSE_BOUNDS  | 0x112  | Set mouse cursor bounds                   |
| SYS_INPUT_HAS_EVENT   | 0x113  | Check if input events are available       |
| SYS_INPUT_GET_EVENT   | 0x114  | Get next input event from kernel queue    |
| SYS_GCON_SET_GUI_MODE | 0x115  | Enable/disable GUI mode for gcon          |
| SYS_SHM_CREATE        | 0x109  | Create shared memory object               |
| SYS_SHM_MAP           | 0x10A  | Map shared memory into address space      |
| SYS_SHM_UNMAP         | 0x10B  | Unmap shared memory                       |
| SYS_SHM_CLOSE         | 0x10C  | Close shared memory handle                |

---

## Console Server (consoled)

**Location:** `user/servers/consoled/`
**Status:** Complete - Full GUI terminal emulator
**Registration:** `sys::assign_set("CONSOLED", channel_handle)`

### Files

| File                   | Lines  | Description                             |
|------------------------|--------|-----------------------------------------|
| `main.cpp`             | ~1,800 | GUI terminal emulator with ANSI support |
| `console_protocol.hpp` | ~225   | IPC message definitions                 |

### Overview

Consoled is a GUI-based terminal emulator that runs as a window within displayd. It provides:

- A graphical window displaying text in a scrollable terminal
- Bidirectional IPC with connected clients (output and keyboard input)
- Full ANSI escape sequence processing for colors and cursor control
- Keyboard input forwarding from displayd to connected clients

### Features

**Terminal Emulation:**

- 106x50 character grid (12x12 pixel cells at 1.5x font scaling)
- Per-cell foreground and background colors with attributes (bold, dim, italic, underline, blink, reverse, hidden,
  strikethrough)
- Block cursor with blinking animation (500ms interval)
- ANSI escape sequence parsing (CSI sequences)

**ANSI Escape Sequences Supported:**

- `CSI n m` - SGR (Select Graphic Rendition): colors 0-7, bright 90-97/100-107, 256-color (38;5;n), 24-bit RGB (
  38;2;r;g;b)
- `CSI n A/B/C/D` - Cursor movement (up/down/forward/back)
- `CSI n;m H` or `CSI n;m f` - Cursor positioning
- `CSI n J` - Erase display (0=below, 1=above, 2=all)
- `CSI n K` - Erase line (0=right, 1=left, 2=all)
- `CSI s/u` - Save/restore cursor position
- `CSI ?25h/l` - Show/hide cursor

**Display:**

- Creates GUI window via libgui (DISPLAY service)
- 1.5x scaled font (12x12 pixels, half-unit scaling: scale=3)
- Window positioned at (20, 20) for visibility
- Dirty cell tracking for efficient partial updates
- Row-based damage coalescing

**Bidirectional IPC:**

- Clients connect via CON_CONNECT with a channel handle for receiving input
- Text output via CON_WRITE with ANSI sequence processing
- Keyboard input forwarded via CON_INPUT events
- Console dimensions reported in CON_CONNECT_REPLY

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        consoled                              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐  │
│  │ Cell Grid   │  │ ANSI Parser │  │ Keyboard Translator │  │
│  │ 106x50      │  │ CSI/SGR     │  │ Keycode → ASCII     │  │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘  │
│         │                │                     │             │
│         ▼                │                     │             │
│  ┌─────────────────────────────────────────────┐            │
│  │              Main Event Loop                 │            │
│  │  1. Drain ALL client messages               │            │
│  │  2. Present dirty cells to window           │            │
│  │  3. Poll GUI events (keyboard)              │            │
│  │  4. Forward keys to connected client        │            │
│  └─────────────────────────────────────────────┘            │
└─────────────────────────────────────────────────────────────┘
         │ libgui                        ▲ IPC
         ▼                               │
    ┌─────────┐                    ┌──────────┐
    │displayd │                    │  vinit   │
    │(window) │                    │ (client) │
    └─────────┘                    └──────────┘
```

### IPC Protocol

```cpp
namespace console_protocol {
    // Request types
    constexpr uint32_t CON_WRITE = 0x1001;       // Write text (with ANSI)
    constexpr uint32_t CON_CLEAR = 0x1002;       // Clear screen
    constexpr uint32_t CON_SET_CURSOR = 0x1003;  // Set cursor position
    constexpr uint32_t CON_GET_CURSOR = 0x1004;  // Get cursor position
    constexpr uint32_t CON_SET_COLORS = 0x1005;  // Set default colors
    constexpr uint32_t CON_GET_SIZE = 0x1006;    // Get dimensions
    constexpr uint32_t CON_SHOW_CURSOR = 0x1007; // Show cursor
    constexpr uint32_t CON_HIDE_CURSOR = 0x1008; // Hide cursor
    constexpr uint32_t CON_CONNECT = 0x1009;     // Connect with input channel

    // Events (consoled → client)
    constexpr uint32_t CON_INPUT = 0x3001;       // Keyboard input event

    // Reply types (0x2000 + request)
    constexpr uint32_t CON_CONNECT_REPLY = 0x2009;

    struct ConnectRequest {
        uint32_t type;       // CON_CONNECT
        uint32_t request_id;
        // handle[0] = reply channel (send endpoint)
    };

    struct ConnectReply {
        uint32_t type;       // CON_CONNECT_REPLY
        uint32_t request_id;
        int32_t status;      // 0 = success
        uint32_t cols;       // Console columns (106)
        uint32_t rows;       // Console rows (50)
    };

    struct WriteRequest {
        uint32_t type;       // CON_WRITE
        uint32_t request_id;
        uint32_t length;     // Text length
        uint32_t reserved;
        // Followed by text data (up to 4080 bytes)
    };

    struct InputEvent {
        uint32_t type;       // CON_INPUT
        char ch;             // ASCII character (0 for special keys)
        uint8_t pressed;     // 1 = key down, 0 = key up
        uint16_t keycode;    // Raw evdev keycode
        uint8_t modifiers;   // Shift=1, Ctrl=2, Alt=4
        uint8_t _pad[3];
    };
}
```

### Connection Flow

1. Client creates a channel pair for receiving the connect reply
2. Client sends CON_CONNECT with send endpoint as handle
3. Consoled sends CON_CONNECT_REPLY with console dimensions
4. Client can now send CON_WRITE to output text
5. Keyboard input comes via kernel TTY syscalls (SYS_TTY_READ)

### Keycode Translation

Consoled translates Linux evdev keycodes to ASCII characters:

| Keycode Range | Description          |
|---------------|----------------------|
| 2-11          | Number keys (1-9, 0) |
| 16-25         | QWERTYUIOP row       |
| 30-38         | ASDFGHJKL row        |
| 44-50         | ZXCVBNM row          |
| 28            | Enter (→ '\n')       |
| 14            | Backspace (→ '\b')   |
| 57            | Space                |
| 15            | Tab (→ '\t')         |

Shift modifier produces uppercase letters and symbols. Special keys (arrows, function keys) are passed as raw keycodes
with `ch=0`.

---

## Display Server (displayd)

**Location:** `user/servers/displayd/`
**Status:** Complete (desktop shell framework operational)
**Registration:** `sys::assign_set("DISPLAY", channel_handle)`

See [16-gui.md](16-gui.md) for complete GUI documentation including:

- displayd architecture and IPC protocol
- libgui client library API
- Taskbar desktop shell
- Window management and compositing

### Summary

| File                   | Lines  | Description                               |
|------------------------|--------|-------------------------------------------|
| `main.cpp`             | ~1,900 | Server entry, compositing, event handling |
| `display_protocol.hpp` | ~260   | IPC message definitions                   |

### Key Features

- Up to 32 concurrent window surfaces
- Per-surface event queues (32 events each)
- Z-ordering for window stacking
- Window decorations with minimize/maximize/close buttons
- Shared memory pixel buffers (zero-copy)
- Software mouse cursor
- Desktop taskbar support via window list protocol
- Window move via title bar drag
- Window resize via edge/corner drag

---

## Client Libraries

### libgui

**Location:** `user/libgui/`
**Purpose:** Client library for displayd communication

See [16-gui.md](16-gui.md) for complete API documentation.

Key functions:

- `gui_init()` / `gui_shutdown()` - Initialization
- `gui_create_window()` / `gui_create_window_ex()` - Window creation
- `gui_get_pixels()` - Direct pixel buffer access
- `gui_present()` - Display update
- `gui_poll_event()` / `gui_wait_event()` - Event handling
- `gui_list_windows()` / `gui_restore_window()` - Taskbar support
- Drawing helpers: `gui_fill_rect()`, `gui_draw_text()`, etc.

### libwidget

**Location:** `user/libwidget/`
**Purpose:** Widget toolkit built on libgui

Key features:

- Button, label, checkbox, radio button widgets
- Text input fields
- List views and scroll containers
- Layout managers (horizontal, vertical, grid)
- Event propagation and focus management

---

## Shared Memory IPC

For high-performance data transfer between display servers and clients, ViperDOS supports shared memory:

### Pattern

```
1. Server creates shared memory region (SYS_SHM_CREATE)
2. Server sends handle to client via IPC channel
3. Client maps shared memory (SYS_SHM_MAP)
4. Both parties read/write shared region (pixel buffer)
5. Synchronization via IPC messages (gui_present)
6. Cleanup with SYS_SHM_UNMAP
```

### Usage in displayd

- Each window surface uses a shared memory region for its pixel buffer
- Client receives SHM handle when window is created
- Client draws directly to shared memory
- Client calls gui_present() to notify displayd of changes
- displayd composites all surfaces to framebuffer

---

## Service Discovery

Display servers register using the assign system:

```cpp
// Server-side registration
u32 service_channel = /* create channel */;
sys::assign_set("DISPLAY", service_channel);

// Client-side discovery
u32 display_handle;
i64 result = sys::assign_get("DISPLAY", &display_handle);
if (result == 0) {
    // display_handle is valid channel to displayd
}
```

---

## Startup Sequence

1. **Kernel boots** and starts vinit as init process
2. **vinit** starts consoled server
3. **consoled** connects to displayd (starts displayd if needed)
4. **displayd** maps framebuffer via SYS_MAP_FRAMEBUFFER
5. **displayd** registers as DISPLAY assign
6. **consoled** creates GUI window via displayd
7. **consoled** registers as CONSOLED assign
8. **vinit** connects to consoled for terminal I/O
9. **workbench** starts and creates desktop window
10. GUI applications can now create windows via libgui

---

## Performance Considerations

### IPC Overhead

Each GUI operation requires:

1. Message copy to channel buffer
2. Context switch to displayd
3. Processing (compositing, damage tracking)
4. Context switch back to client

### Mitigation Strategies

- Shared memory for pixel buffers (zero-copy rendering)
- Dirty rectangle tracking (only composite changed regions)
- Double buffering at application level
- Batched present calls

### Measured Latency (Approximate)

| Operation           | Latency |
|---------------------|---------|
| Window creation     | ~500μs  |
| gui_present (small) | ~100μs  |
| gui_present (full)  | ~500μs  |
| Event poll          | ~50μs   |

---

## Priority Recommendations: Next Steps

### 1. Window Resize with SHM Reallocation

**Impact:** Proper window resizing

- Allocate new SHM when window size changes
- Send resize event with new SHM handle to client
- Client remaps buffer and redraws at new size

### 2. Scrollbars

**Impact:** Scrollable content in windows

- Vertical and horizontal scrollbar rendering
- Scrollbar drag interaction
- Scroll events to applications

### 3. Clipboard Support

**Impact:** Copy/paste between applications

- Kernel clipboard syscalls
- Text and binary data formats
- Selection ownership protocol
