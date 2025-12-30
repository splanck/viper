# Console Subsystem

**Status:** Fully functional for both serial and graphics output
**Location:** `kernel/console/`
**SLOC:** ~2,500

## Overview

The console subsystem provides text output capabilities through both a serial UART and a framebuffer-based graphics console. It is designed to work from earliest boot through normal operation.

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

**Status:** Fully functional text mode console with decorative border

**Implemented:**
- Text rendering to framebuffer (via ramfb driver)
- 8x16 base font with 1.5x scaling (12x24 effective)
- 80x29 character grid at 1024x768 (with border)
- **Decorative green border** (20px outer, 8px inner padding)
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

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize graphics console |
| `is_available()` | Check if framebuffer ready |
| `putc(c)` | Write one character |
| `puts(s)` | Write string |
| `clear()` | Clear screen |
| `set_colors(fg, bg)` | Set text colors |
| `get_cursor(&x, &y)` | Get cursor position |
| `set_cursor(x, y)` | Set cursor position |
| `get_size(&cols, &rows)` | Get console dimensions |

**Default Colors (Viper Theme):**
| Color | RGB | Usage |
|-------|-----|-------|
| VIPER_GREEN | 0x00AA00 | Default foreground |
| VIPER_DARK_BROWN | 0x221100 | Default background |
| VIPER_WHITE | 0xFFFFFF | Highlight text |
| VIPER_RED | 0xFF0000 | Error text |

**Not Implemented:**
- Cursor blinking/rendering
- Double-buffering
- Hardware acceleration
- Unicode/UTF-8 support
- ANSI escape sequences
- Multiple virtual consoles
- Font selection/loading
- Bold/italic text styles

**Recommendations:**
- Add visible cursor with blink
- Implement ANSI escape codes for color/positioning
- Add double-buffering for flicker-free updates
- Consider hardware-accelerated scroll if available

---

### 3. Font (`font.cpp`, `font.hpp`)

**Status:** Complete 8x16 bitmap font

**Implemented:**
- Full ASCII printable character set (32-126)
- 8x16 pixel base glyphs
- 1-bit-per-pixel bitmap format
- Fractional scaling support (3/2 = 1.5x default)
- Fallback glyph for unsupported characters

**Font Metrics:**
| Parameter | Value |
|-----------|-------|
| BASE_WIDTH | 8 pixels |
| BASE_HEIGHT | 16 pixels |
| SCALE_NUM | 3 |
| SCALE_DEN | 2 |
| Effective WIDTH | 12 pixels |
| Effective HEIGHT | 24 pixels |

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

## Priority Recommendations

1. **Medium:** Add ANSI escape sequence support
2. **Medium:** Implement visible cursor
3. **Low:** Add interrupt-driven serial receive
4. **Low:** Double-buffered graphics for smooth updates
5. **Low:** Unicode support (at least Latin-1)
