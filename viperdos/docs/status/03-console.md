# Console Subsystem

**Status:** Fully functional with GUI terminal emulator
**Location:** `kernel/console/` + `user/servers/consoled/` + `user/servers/inputd/`
**SLOC:** ~3,500 (kernel) + ~1,400 (consoled) + ~1,000 (inputd)

## Overview

The console subsystem provides text output capabilities through multiple paths:

- **Kernel console (gcon)**: Boot-time output and kernel debug messages via framebuffer
- **Serial console**: PL011 UART for serial output and debugging
- **consoled server**: User-space GUI terminal emulator running in a window (~1,400 SLOC)
- **inputd server**: User-space keyboard/mouse input server (~1,000 SLOC)

The kernel console is used during boot and can be disabled when the GUI terminal takes over via `gcon_set_gui_mode()`.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      User Applications                           │
│                    (vinit shell, etc.)                          │
└──────────────────────────┬──────────────────────────────────────┘
                           │ IPC (CON_WRITE, CON_INPUT)
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                   consoled (GUI Terminal)                        │
│  • ANSI escape sequence parsing                                  │
│  • 1.5x scaled font rendering (12x12 pixels)                    │
│  • Bidirectional IPC with clients                               │
│  • Keyboard input forwarding from displayd                      │
└──────────────────────────┬──────────────────────────────────────┘
                           │ libgui API
                           ▼
┌─────────────────────────────────────────────────────────────────┐
│                        displayd                                  │
│              (Window compositing, event routing)                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Components

### 1. Serial Console (`serial.cpp`, `serial.hpp`)

**Status:** Complete PL011 UART driver

**Implemented:**

- PL011 UART at 0x09000000 (QEMU virt machine)
- Polling-based transmit and receive
- Non-blocking character availability check
- Automatic CR+LF newline conversion
- Hexadecimal number output
- Decimal number output (signed)
- String output

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize UART (no-op on QEMU) |
| `putc(c)` | Write one character |
| `getc()` | Read one character (blocking) |
| `getc_nonblock()` | Read character or -1 |
| `has_char()` | Check if input available |
| `puts(s)` | Write string |
| `put_hex(v)` | Write hex value with 0x prefix |
| `put_dec(v)` | Write signed decimal |

**Hardware Registers:**
| Offset | Register | Usage |
|--------|----------|-------|
| 0x00 | UART_DR | Data read/write |
| 0x18 | UART_FR | Status flags |

**Not Implemented:**

- Interrupt-driven I/O
- Hardware flow control (RTS/CTS)
- Baud rate configuration (uses QEMU defaults)
- FIFO depth awareness
- Error detection (framing, parity, overrun)

---

### 2. Graphics Console (`gcon.cpp`, `gcon.hpp`)

**Status:** Fully functional text mode console with ANSI support

**Purpose:** Kernel-level text console for boot messages and fallback output. Can be disabled when GUI terminal takes
over.

**Implemented:**

- Text rendering to framebuffer (via ramfb driver)
- 8x16 base font with 5/4 scaling (10x20 effective)
- Dynamic character grid based on resolution (e.g., 152x48 at 1600x1024)
- **Decorative green border** (4px border + 4px padding = 8px total inset)
- Foreground/background color control
- Terminal control characters:
    - `\n` - Newline with scroll
    - `\r` - Carriage return
    - `\t` - Tab (8-column alignment)
    - `\b` - Backspace
- Automatic line wrapping
- Smooth scrolling (pixel copy, respects border region)
- Cursor position tracking
- Screen clear (preserves border)
- **Blinking cursor** (500ms interval, XOR-based rendering)
- **Scrollback buffer** (1000 lines × 200 columns circular buffer)
- **ANSI escape sequence support** (see below)
- **GUI mode control** (`set_gui_mode()` to disable when consoled active)

**GUI Mode Control:**

When the user-space GUI terminal (consoled) connects, it calls `gcon_set_gui_mode(true)` to disable kernel framebuffer
output. This prevents visual conflicts between kernel console and GUI window rendering.

```cpp
// In vinit after connecting to consoled:
sys::gcon_set_gui_mode(true);  // Disable kernel gcon output
```

**ANSI Escape Sequences Supported:**

| Sequence   | Name    | Description                                 |
|------------|---------|---------------------------------------------|
| `ESC[H`    | CUP     | Move cursor to home (0,0)                   |
| `ESC[n;mH` | CUP     | Move cursor to row n, column m              |
| `ESC[nA`   | CUU     | Move cursor up n lines                      |
| `ESC[nB`   | CUD     | Move cursor down n lines                    |
| `ESC[nC`   | CUF     | Move cursor forward n columns               |
| `ESC[nD`   | CUB     | Move cursor back n columns                  |
| `ESC[J`    | ED      | Erase display (0=to end, 1=to start, 2=all) |
| `ESC[K`    | EL      | Erase line (0=to end, 1=to start, 2=all)    |
| `ESC[nm`   | SGR     | Set graphics rendition (colors)             |
| `ESC[?25h` | DECTCEM | Show cursor                                 |
| `ESC[?25l` | DECTCEM | Hide cursor                                 |

**SGR Color Codes:**

| Range   | Meaning                    |
|---------|----------------------------|
| 30-37   | Standard foreground colors |
| 40-47   | Standard background colors |
| 90-97   | Bright foreground colors   |
| 100-107 | Bright background colors   |
| 0       | Reset to default colors    |

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize graphics console |
| `is_available()` | Check if framebuffer ready |
| `putc(c)` | Write one character (ANSI-aware) |
| `puts(s)` | Write string |
| `clear()` | Clear screen |
| `set_colors(fg, bg)` | Set text colors |
| `get_cursor(&x, &y)` | Get cursor position |
| `set_cursor(x, y)` | Set cursor position |
| `get_size(&cols, &rows)` | Get console dimensions |
| `show_cursor(bool)` | Show/hide cursor |
| `scroll_up(lines)` | Scroll up n lines |
| `scroll_down(lines)` | Scroll down n lines (scrollback) |
| `set_gui_mode(bool)` | Enable/disable GUI mode (disables output) |

**Default Colors (Viper Theme):**
| Color | RGB | Usage |
|-------|-----|-------|
| VIPER_GREEN | 0x00AA44 | Default foreground |
| VIPER_DARK_BROWN | 0x1A1208 | Default background |
| VIPER_WHITE | 0xFFFFFF | Highlight text |
| VIPER_YELLOW | 0xFFDD00 | Accents |
| VIPER_RED | 0xCC3333 | Error text |

---

### 3. GUI Terminal Emulator (`consoled`)

**Status:** Complete GUI terminal running in a window
**Location:** `user/servers/consoled/`
**SLOC:** ~1,400

**Purpose:** User-space terminal emulator that runs the vinit shell in a GUI window. Provides bidirectional IPC for text
output and keyboard input.

**Implemented:**

- **GUI window** via libgui/displayd
- **1.5x scaled font** (12x12 pixels from 8x8 base)
- **Window positioning** at top-left corner (20, 20)
- **Full ANSI escape sequence support** (colors, cursor, clearing)
- **Bidirectional IPC** with vinit:
    - `CON_WRITE`: Text output from client
    - `CON_INPUT`: Keyboard input to client
    - `CON_CONNECT`: Client connection with input channel exchange
- **Keyboard forwarding**: Receives key events from displayd, forwards to client
- **Message draining**: Processes all pending messages before presenting (prevents output lag)
- **Cursor rendering**: Visible cursor with blink support

**IPC Protocol:**

```cpp
namespace console_protocol {
    // Client → Server
    constexpr uint32_t CON_WRITE = 0x1001;        // Write text
    constexpr uint32_t CON_CLEAR = 0x1002;        // Clear screen
    constexpr uint32_t CON_SET_CURSOR = 0x1003;   // Set cursor position
    constexpr uint32_t CON_GET_CURSOR = 0x1004;   // Get cursor position
    constexpr uint32_t CON_SET_COLORS = 0x1005;   // Set colors
    constexpr uint32_t CON_GET_SIZE = 0x1006;     // Get dimensions
    constexpr uint32_t CON_SHOW_CURSOR = 0x1007;  // Show cursor
    constexpr uint32_t CON_HIDE_CURSOR = 0x1008;  // Hide cursor
    constexpr uint32_t CON_CONNECT = 0x1009;      // Client connect (exchanges input channel)

    // Server → Client
    constexpr uint32_t CON_INPUT = 0x3001;        // Keyboard input event
    constexpr uint32_t CON_CONNECT_REPLY = 0x2009; // Connection reply with dimensions

    struct ConnectRequest {
        uint32_t type;       // CON_CONNECT
        uint32_t request_id;
        // handles[0] = reply channel (send endpoint)
        // handles[1] = input channel (send endpoint for server to send keys)
    };

    struct ConnectReply {
        uint32_t type;       // CON_CONNECT_REPLY
        uint32_t request_id;
        int32_t status;      // 0 = success
        uint32_t cols;       // Console width in characters
        uint32_t rows;       // Console height in characters
    };

    struct InputEvent {
        uint32_t type;       // CON_INPUT
        char ch;             // ASCII character (0 for special keys)
        uint8_t pressed;     // 1 = key press
        uint16_t keycode;    // Linux evdev keycode
        uint8_t modifiers;   // Shift/Ctrl/Alt flags
        uint8_t _pad[3];
    };

    struct WriteRequest {
        uint32_t type;       // CON_WRITE
        uint32_t request_id;
        uint32_t length;     // Text length
        uint32_t reserved;
        // Followed by text data
    };
}
```

**Connection Flow:**

```
1. vinit calls init_console()
2. vinit gets CONSOLED service handle via assign_get("CONSOLED")
3. vinit creates input channel pair (send + recv)
4. vinit creates reply channel pair
5. vinit sends CON_CONNECT with [reply_send, input_send] handles
6. consoled receives CON_CONNECT, stores input_send for keyboard forwarding
7. consoled sends CON_CONNECT_REPLY with console dimensions
8. vinit receives reply, stores input_recv for receiving keyboard events
9. vinit calls gcon_set_gui_mode(true) to disable kernel console
10. Shell runs: output via CON_WRITE, input via CON_INPUT
```

**ANSI Escape Sequences (consoled):**

consoled implements a complete ANSI escape sequence parser supporting:

| Category          | Sequences                                                                      |
|-------------------|--------------------------------------------------------------------------------|
| Cursor Movement   | `ESC[H`, `ESC[nA/B/C/D`, `ESC[n;mH`, `ESC[s`, `ESC[u`                          |
| Erasing           | `ESC[J`, `ESC[K`, `ESC[2J`                                                     |
| Colors (SGR)      | `ESC[0m` (reset), `ESC[30-37m`, `ESC[40-47m`, `ESC[90-97m`, `ESC[7m` (reverse) |
| Cursor Visibility | `ESC[?25h` (show), `ESC[?25l` (hide)                                           |

**Font Rendering:**

consoled uses `gui_draw_char_scaled()` from libgui for scalable font rendering:

```cpp
// Scale in half-units: 2=1x(8x8), 3=1.5x(12x12), 4=2x(16x16)
static constexpr uint32_t FONT_SCALE = 3;  // 1.5x scaling
static constexpr uint32_t FONT_WIDTH = 8 * FONT_SCALE / 2;   // 12 pixels
static constexpr uint32_t FONT_HEIGHT = 8 * FONT_SCALE / 2;  // 12 pixels
```

**Main Loop Structure:**

```cpp
while (true) {
    // 1. Drain ALL pending client messages (prevents output lag)
    while (channel_recv() > 0) {
        handle_request();
    }

    // 2. Present rendered output
    if (g_needs_present) {
        gui_present(g_window);
    }

    // 3. Check for GUI events (keyboard)
    while (gui_poll_event() == 0) {
        if (event.type == GUI_EVENT_KEY && event.key.pressed) {
            // Forward to client via input channel
            send_input_event(event);
        }
    }

    // 4. Yield if no work
    if (!did_work) sys::yield();
}
```

---

### 4. Font (`font.cpp`, `font.hpp`)

**Status:** Complete 8x16 bitmap font with scaling

**Implemented:**

- Full ASCII printable character set (32-126)
- 8x16 pixel base glyphs
- 1-bit-per-pixel bitmap format (MSB first)
- Fractional scaling support (5/4 = 1.25x default for kernel, 3/2 = 1.5x for consoled)
- Fallback glyph (`?`) for unsupported characters

**Font Metrics:**
| Parameter | Kernel (gcon) | consoled |
|-----------|---------------|----------|
| BASE_WIDTH | 8 pixels | 8 pixels |
| BASE_HEIGHT | 16 pixels | 8 pixels |
| SCALE | 5/4 (1.25x) | 3/2 (1.5x) |
| Effective WIDTH | 10 pixels | 12 pixels |
| Effective HEIGHT | 20 pixels | 12 pixels |

---

### 5. Console Abstraction (`console.cpp`, `console.hpp`)

**Status:** Unified console interface with buffered input

**Purpose:** Provides a single interface for console I/O in the kernel:

- Output routing to both serial and graphics console
- Unified input buffer merging keyboard and serial input
- Canonical mode line editing

**Implemented:**

- 1KB ring buffer for input characters
- Merged input from virtio-keyboard and serial UART
- Non-blocking character retrieval
- Line editing with:
    - Backspace/Delete handling
    - Ctrl+C (cancel line)
    - Ctrl+D (EOF)
    - Ctrl+U (clear line)
- Automatic echo to both consoles

**API:**
| Function | Description |
|----------|-------------|
| `init_input()` | Initialize input buffer |
| `poll_input()` | Poll keyboard and serial for input |
| `has_input()` | Check if character available |
| `getchar()` | Get one character (non-blocking) |
| `input_available()` | Get count of buffered characters |
| `readline(buf, max)` | Read line with editing |
| `print(s)` | Print string |
| `print_dec(v)` | Print decimal number |
| `print_hex(v)` | Print hex number |

---

## vinit Console Integration

vinit (`user/vinit/`) integrates with consoled for GUI terminal support:

### Files

| File           | Description                                |
|----------------|--------------------------------------------|
| `io.cpp`       | Console connection, output/input functions |
| `readline.cpp` | Line editing with console input            |
| `vinit.hpp`    | Console function declarations              |

### Key Functions

```cpp
// io.cpp
bool init_console();              // Connect to CONSOLED, exchange channels
void print_str(const char *s);    // Output text (via consoled if connected)
i32 getchar_from_console();       // Blocking read from input channel
i32 try_getchar_from_console();   // Non-blocking read
bool is_console_ready();          // Check if connected to consoled
void flush_console();             // No-op (messages sent immediately)

// readline.cpp
usize readline(char *buf, usize maxlen);  // Full line editing with history
```

### Blocking Send

To prevent message loss when the channel buffer is full, `console_write()` uses blocking send with retry:

```cpp
static void console_write(const char *s, usize len) {
    // ... build message ...

    // Send with retry - keep trying until success
    while (true) {
        i64 err = sys::channel_send(g_console_service, buf, total_len, nullptr, 0);
        if (err == 0) break;
        sys::sleep(1);  // Buffer full - wait for consoled to catch up
    }
}
```

---

## Syscall Access

User space accesses the console through these syscalls:

| Syscall           | Number | Description                                      |
|-------------------|--------|--------------------------------------------------|
| debug_print       | 0xF0   | Print string to kernel console (serial + gcon)   |
| getchar           | 0xF1   | Read character from kernel input buffer          |
| putchar           | 0xF2   | Write single character to kernel console         |
| sleep             | 0x31   | Sleep for milliseconds (used for console timing) |
| gcon_set_gui_mode | 0xF4   | Enable/disable kernel gcon output                |

---

## Testing

The console subsystem is tested via:

- `qemu_kernel_boot` - Boot banner appears on serial and graphics
- `qemu_toolchain_test` - "Toolchain works!" output verification
- GUI console testing - vinit shell runs in consoled window
- All tests use serial output for verification

---

## Files

| File                                         | Lines  | Description                       |
|----------------------------------------------|--------|-----------------------------------|
| `kernel/console/serial.cpp`                  | ~89    | PL011 UART driver                 |
| `kernel/console/serial.hpp`                  | ~13    | Serial interface                  |
| `kernel/console/gcon.cpp`                    | ~700   | Graphics console with GUI mode    |
| `kernel/console/gcon.hpp`                    | ~30    | Graphics console interface        |
| `kernel/console/font.cpp`                    | ~1550  | Bitmap font data                  |
| `kernel/console/font.hpp`                    | ~12    | Font metrics and API              |
| `kernel/console/console.cpp`                 | ~147   | Unified console with input buffer |
| `kernel/console/console.hpp`                 | ~16    | Console interface                 |
| `user/servers/consoled/main.cpp`             | ~1,400 | GUI terminal emulator             |
| `user/servers/consoled/console_protocol.hpp` | ~200   | IPC protocol definitions          |
| `user/vinit/io.cpp`                          | ~540   | Console I/O for vinit             |
| `user/vinit/readline.cpp`                    | ~300   | Line editing                      |

---

## Color Constants

Defined in `gcon.hpp`:

```cpp
namespace colors {
    constexpr u32 BLACK       = 0x000000;
    constexpr u32 WHITE       = 0xFFFFFF;
    constexpr u32 RED         = 0xFF0000;
    constexpr u32 GREEN       = 0x00FF00;
    constexpr u32 BLUE        = 0x0000FF;
    constexpr u32 VIPER_GREEN = 0x00AA00;  // Default FG
    constexpr u32 VIPER_DARK_BROWN = 0x221100;  // Default BG
    constexpr u32 VIPER_WHITE = 0xFFFFFF;
    constexpr u32 VIPER_RED   = 0xFF0000;
}
```

---

## Completed Improvements

The following features have been implemented:

- ANSI escape sequence support (cursor, colors, clearing) in both gcon and consoled
- Blinking cursor with show/hide control
- Scrollback buffer (1000 lines) in kernel gcon
- Dynamic console sizing based on framebuffer resolution
- **GUI terminal emulator (consoled)** with window-based rendering
- **Bidirectional IPC** between vinit and consoled
- **Keyboard input forwarding** from displayd to consoled to vinit
- **1.5x scalable font** for better readability
- **gcon_set_gui_mode()** for kernel/GUI coordination
- **Blocking send** to prevent message loss
- **Message draining** in consoled for responsive output

---

## Priority Recommendations: Next 5 Steps

### 1. Double-Buffered Graphics Console

**Impact:** Flicker-free console updates

- Allocate back buffer in memory
- Render all changes to back buffer
- Swap buffers on vsync or frame complete
- Eliminates visible tearing during scrolling

### 2. UTF-8/Unicode Text Support

**Impact:** International character display

- Decode UTF-8 byte sequences in putc()
- Extend font to cover Latin-1 (128-255)
- Add common Unicode ranges (Latin Extended)
- Required for proper internationalization

### 3. Multiple Virtual Consoles (VTs)

**Impact:** Multiple terminal sessions

- Alt+F1/F2/F3 console switching
- Separate scrollback buffer per console
- Independent cursor position and colors
- Background process output capture

### 4. Console Resize Support

**Impact:** Dynamic window resizing

- Handle GUI_EVENT_RESIZE in consoled
- Reallocate text buffer for new size
- Reflow text content
- Update vinit with new dimensions

### 5. Text Attributes (Bold, Underline)

**Impact:** Richer terminal display

- Parse SGR sequences for bold (1), underline (4)
- Render bold as brighter color or wider stroke
- Underline via extra scanline
- Improves terminal application display
