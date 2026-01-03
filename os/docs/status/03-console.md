# Console Subsystem

**Status:** Fully functional for both serial and graphics output
**Location:** `kernel/console/` + `user/servers/consoled/` + `user/servers/inputd/`
**SLOC:** ~3,500 (kernel) + ~1,600 (servers)

## Overview

The console subsystem provides text output capabilities through both a serial UART and a framebuffer-based graphics console. In the microkernel architecture:

- **Kernel console**: Boot-time output and kernel debug messages
- **consoled server**: User-space console output server (~600 SLOC)
- **inputd server**: User-space keyboard/mouse input server (~1,000 SLOC)

The kernel console is always available, while the user-space servers provide IPC-based console access for applications in microkernel mode.

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

**Recommendations:**
- Add interrupt-driven receive for better responsiveness
- Add output buffering to reduce polling overhead

---

### 2. Graphics Console (`gcon.cpp`, `gcon.hpp`)

**Status:** Fully functional text mode console with ANSI support

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

**ANSI Escape Sequences Supported:**

| Sequence | Name | Description |
|----------|------|-------------|
| `ESC[H` | CUP | Move cursor to home (0,0) |
| `ESC[n;mH` | CUP | Move cursor to row n, column m |
| `ESC[nA` | CUU | Move cursor up n lines |
| `ESC[nB` | CUD | Move cursor down n lines |
| `ESC[nC` | CUF | Move cursor forward n columns |
| `ESC[nD` | CUB | Move cursor back n columns |
| `ESC[J` | ED | Erase display (0=to end, 1=to start, 2=all) |
| `ESC[K` | EL | Erase line (0=to end, 1=to start, 2=all) |
| `ESC[nm` | SGR | Set graphics rendition (colors) |
| `ESC[?25h` | DECTCEM | Show cursor |
| `ESC[?25l` | DECTCEM | Hide cursor |

**SGR Color Codes:**

| Range | Meaning |
|-------|---------|
| 30-37 | Standard foreground colors |
| 40-47 | Standard background colors |
| 90-97 | Bright foreground colors |
| 100-107 | Bright background colors |
| 0 | Reset to default colors |

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

**Default Colors (Viper Theme):**
| Color | RGB | Usage |
|-------|-----|-------|
| VIPER_GREEN | 0x00AA44 | Default foreground |
| VIPER_DARK_BROWN | 0x1A1208 | Default background |
| VIPER_WHITE | 0xFFFFFF | Highlight text |
| VIPER_YELLOW | 0xFFDD00 | Accents |
| VIPER_RED | 0xCC3333 | Error text |

**Scrollback Buffer:**
```
┌─────────────────────────────────────────┐
│  Scrollback Buffer (1000 × 200)         │
│  ┌─────────────────────────────────┐    │
│  │  Line 0: [char, fg, bg] × 200   │    │
│  │  Line 1: ...                    │    │
│  │  ...                            │    │
│  │  Line 999: ...                  │ ← wrap │
│  └─────────────────────────────────┘    │
│  write_index: current line              │
│  view_offset: scroll position           │
└─────────────────────────────────────────┘
```

**Not Implemented:**
- Double-buffering
- Hardware acceleration
- Unicode/UTF-8 support
- Multiple virtual consoles
- Font selection/loading
- Bold/italic text styles

**Recommendations:**
- Add double-buffering for flicker-free updates
- Consider hardware-accelerated scroll if available

---

### 3. Font (`font.cpp`, `font.hpp`)

**Status:** Complete 8x16 bitmap font with scaling

**Implemented:**
- Full ASCII printable character set (32-126)
- 8x16 pixel base glyphs
- 1-bit-per-pixel bitmap format (MSB first)
- Fractional scaling support (5/4 = 1.25x default)
- Fallback glyph (`?`) for unsupported characters

**Font Metrics:**
| Parameter | Value |
|-----------|-------|
| BASE_WIDTH | 8 pixels |
| BASE_HEIGHT | 16 pixels |
| SCALE_NUM | 5 |
| SCALE_DEN | 4 |
| Effective WIDTH | 10 pixels |
| Effective HEIGHT | 20 pixels |

**Font Data Format:**
```cpp
// Each glyph is 16 bytes (one per row)
// Each byte represents 8 horizontal pixels, MSB=leftmost
const uint8_t font_data[96][16] = {
    { 0x00, 0x00, ... },  // Space (ASCII 32)
    { 0x18, 0x18, ... },  // ! (ASCII 33)
    // ...
};
```

**Not Implemented:**
- Multiple fonts
- Runtime font loading
- Variable-width fonts
- Extended character sets (Latin-1, etc.)
- Anti-aliasing

---

### 4. Console Abstraction (`console.cpp`, `console.hpp`)

**Status:** Unified console interface with buffered input

**Purpose:** Provides a single interface for console I/O:
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

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Code                          │
│              (kernel, user space via syscalls)               │
└──────────────────────────┬──────────────────────────────────┘
                           │
        ┌──────────────────┴──────────────────┐
        │                                      │
        ▼                                      ▼
┌───────────────────┐              ┌───────────────────┐
│   serial::puts()  │              │    gcon::puts()   │
│   serial::putc()  │              │    gcon::putc()   │
└────────┬──────────┘              └────────┬──────────┘
         │                                  │
         ▼                                  ▼
┌───────────────────┐              ┌───────────────────┐
│   PL011 UART      │              │   ramfb driver    │
│   (0x09000000)    │              │   (framebuffer)   │
└───────────────────┘              └────────┬──────────┘
                                            │
                                            ▼
                                   ┌───────────────────┐
                                   │   font::get_glyph │
                                   │   (8x16 bitmap)   │
                                   └───────────────────┘
```

## Syscall Access

User space accesses the console through these syscalls:

| Syscall | Number | Description |
|---------|--------|-------------|
| debug_print | 0xF0 | Print string to both consoles |
| getchar | 0xF1 | Read character (keyboard or serial) |
| putchar | 0xF2 | Write single character |

---

## Testing

The console subsystem is tested via:
- `qemu_kernel_boot` - Boot banner appears on both consoles
- `qemu_toolchain_test` - "Toolchain works!" output verification
- All tests use serial output for verification

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `serial.cpp` | ~89 | PL011 UART driver |
| `serial.hpp` | ~13 | Serial interface |
| `gcon.cpp` | ~648 | Graphics console |
| `gcon.hpp` | ~27 | Graphics console interface |
| `font.cpp` | ~1550 | Bitmap font data |
| `font.hpp` | ~12 | Font metrics and API |
| `console.cpp` | ~147 | Unified console with input buffer |
| `console.hpp` | ~16 | Console interface |

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

The following features have been implemented since initial documentation:

- ANSI escape sequence support (cursor, colors, clearing)
- Blinking cursor with show/hide control
- Scrollback buffer (1000 lines)
- Dynamic console sizing based on framebuffer resolution

## Priority Recommendations

1. **Medium:** Double-buffered graphics for smooth updates
2. **Low:** Add interrupt-driven serial receive
3. **Low:** Unicode support (at least Latin-1)
4. **Low:** Multiple virtual consoles
